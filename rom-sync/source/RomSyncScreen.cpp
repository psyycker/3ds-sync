#include "RomSyncScreen.hpp"

#include "DriveApi.hpp"
#include "DriveAuth.hpp"
#include "DriveFilePickerOverlay.hpp"
#include "FolderBrowserOverlay.hpp"
#include "colors.hpp"
#include "gui.hpp"
#include "main.hpp"
#include "stringutils.hpp"
#include "textpool.hpp"
#include "thread.hpp"
#include <3ds.h>
#include <cstdio>
#include <sys/stat.h>

RomSyncScreen::RomSyncScreen(void) : mAuth(std::make_shared<AuthState>()), mSync(std::make_shared<SyncState>())
{
    mChannels = Channels::load();

    std::shared_ptr<AuthState> auth = mAuth;
    Threads::create([auth]() {
        std::string accessToken, error;
        bool ok = DriveAuth::getAccessToken(accessToken, error);

        std::lock_guard<std::mutex> lock(auth->mutex);
        if (ok) {
            auth->signedIn    = true;
            auth->accessToken = accessToken;
            auth->status      = "Signed in.";
        }
        else {
            auth->authFailed = true;
            auth->status     = "Sign-in failed: " + error;
        }
    });
}

bool RomSyncScreen::allowsExit(void) const
{
    std::lock_guard<std::mutex> lock(mSync->mutex);
    return !mSync->running;
}

void RomSyncScreen::drawTop(void) const
{
    C2D_TargetClear(g_top, COLOR_BASE);
    C2D_SceneBegin(g_top);

    TextPool& text = TextPool::get();
    text.draw("rom-sync", 16.0f, 16.0f, 0.6f, COLOR_MUTED);

    bool signedIn, authFailed;
    std::string status;
    {
        std::lock_guard<std::mutex> lock(mAuth->mutex);
        signedIn   = mAuth->signedIn;
        authFailed = mAuth->authFailed;
        status     = mAuth->status;
    }

    if (!signedIn) {
        text.drawWrapped(status, 16.0f, 48.0f, 0.55f, authFailed ? COLOR_DANGER : COLOR_TEXT, 368.0f);
        return;
    }

    float y = 48.0f;
    for (size_t i = 0; i < mChannels.size(); i++) {
        bool selected      = ((int)i == mCursor);
        u32 labelColor     = selected ? COLOR_TEAL : COLOR_TEXT;
        const SyncChannel& ch = mChannels[i];
        text.draw(selected ? "> " + ch.driveFileName : "  " + ch.driveFileName, 16.0f, y, 0.5f, labelColor);
        text.draw(text.truncate(ch.localFolder, 280.0f, 0.42f), 32.0f, y + 20.0f, 0.42f, COLOR_MUTED);
        y += 44.0f;
    }

    bool addSelected  = mCursor == (int)mChannels.size();
    bool syncSelected = mCursor == (int)mChannels.size() + 1;
    text.draw(addSelected ? "> Add channel" : "  Add channel", 16.0f, y, 0.5f, addSelected ? COLOR_TEAL : COLOR_TEXT);
    y += 26.0f;
    text.draw(syncSelected ? "> Sync all channels" : "  Sync all channels", 16.0f, y, 0.5f, syncSelected ? COLOR_TEAL : COLOR_TEXT);

    bool running;
    std::string message;
    {
        std::lock_guard<std::mutex> lock(mSync->mutex);
        running = mSync->running;
        message = mSync->message;
    }
    if (running || !message.empty()) {
        text.drawWrapped(message, 16.0f, 200.0f, 0.46f, running ? COLOR_MUTED : COLOR_TEAL, 368.0f);
    }
}

void RomSyncScreen::drawBottom(void) const
{
    C2D_TargetClear(g_bottom, COLOR_BASE);
    C2D_SceneBegin(g_bottom);
    TextPool::get().draw("A: select   X: delete channel   START: exit", 8.0f, 220.0f, 0.42f, COLOR_FAINT);
}

void RomSyncScreen::startAddChannel(const std::string& accessToken)
{
    currentOverlay = std::make_shared<DriveFilePickerOverlay>(
        *this, accessToken, "root", "My Drive", "new-file", [this, accessToken](const std::string& fileId, const std::string& fileName) {
            currentOverlay = std::make_shared<FolderBrowserOverlay>(
                *this, "Choose a destination folder for \"" + fileName + "\"", [this, fileId, fileName](const std::u16string& path) {
                    SyncChannel ch;
                    ch.driveFileId   = fileId;
                    ch.driveFileName = fileName;
                    ch.localFolder   = StringUtils::UTF16toUTF8(path);
                    mChannels.push_back(ch);
                    Channels::save(mChannels);
                });
        });
}

namespace {
    bool downloadChannel(const std::string& accessToken, const SyncChannel& ch, std::string& outError)
    {
        std::string content;
        if (!DriveApi::downloadFile(accessToken, ch.driveFileId, content)) {
            outError = "download failed";
            return false;
        }
        mkdir(ch.localFolder.c_str(), 0777);
        FILE* f = std::fopen(ch.localPath().c_str(), "wb");
        if (!f) {
            outError = "could not open " + ch.localPath() + " for writing";
            return false;
        }
        bool ok = std::fwrite(content.data(), 1, content.size(), f) == content.size();
        std::fclose(f);
        if (!ok) {
            outError = "write failed (SD full?)";
        }
        return ok;
    }
}

void RomSyncScreen::startSync(int channelIndex)
{
    {
        std::lock_guard<std::mutex> lock(mSync->mutex);
        if (mSync->running) {
            return;
        }
        mSync->running = true;
        mSync->message = "Starting sync...";
    }

    std::string accessToken;
    {
        std::lock_guard<std::mutex> lock(mAuth->mutex);
        accessToken = mAuth->accessToken;
    }

    std::vector<SyncChannel> toSync;
    if (channelIndex < 0) {
        toSync = mChannels;
    }
    else if (channelIndex < (int)mChannels.size()) {
        toSync.push_back(mChannels[channelIndex]);
    }

    std::shared_ptr<SyncState> sync = mSync;
    Threads::create([sync, accessToken, toSync]() {
        int failures = 0;
        for (auto& ch : toSync) {
            {
                std::lock_guard<std::mutex> lock(sync->mutex);
                sync->message = "Downloading " + ch.driveFileName + "...";
            }
            std::string error;
            if (!downloadChannel(accessToken, ch, error)) {
                failures++;
                std::lock_guard<std::mutex> lock(sync->mutex);
                sync->message = ch.driveFileName + ": " + error;
            }
        }

        std::lock_guard<std::mutex> lock(sync->mutex);
        sync->running = false;
        if (failures == 0) {
            sync->message = toSync.size() == 1 ? toSync[0].driveFileName + ": done." : "Synced " + std::to_string(toSync.size()) + " channel(s).";
        }
        else {
            sync->message += " (" + std::to_string(failures) + " failed)";
        }
    });
}

void RomSyncScreen::update(const InputState&)
{
    bool signedIn;
    std::string accessToken;
    {
        std::lock_guard<std::mutex> lock(mAuth->mutex);
        signedIn    = mAuth->signedIn;
        accessToken = mAuth->accessToken;
    }
    if (!signedIn) {
        return;
    }

    bool syncing;
    {
        std::lock_guard<std::mutex> lock(mSync->mutex);
        syncing = mSync->running;
    }
    if (syncing) {
        return;
    }

    u32 kDown = hidKeysDown();
    int count = itemCount();

    if (kDown & KEY_DOWN) {
        mCursor = (mCursor + 1) % count;
    }
    else if (kDown & KEY_UP) {
        mCursor = (mCursor - 1 + count) % count;
    }
    else if (kDown & KEY_A) {
        if (mCursor < (int)mChannels.size()) {
            startSync(mCursor);
        }
        else if (mCursor == (int)mChannels.size()) {
            startAddChannel(accessToken);
        }
        else {
            startSync(-1);
        }
    }
    else if ((kDown & KEY_X) && mCursor < (int)mChannels.size()) {
        mChannels.erase(mChannels.begin() + mCursor);
        Channels::save(mChannels);
        if (mCursor >= itemCount()) {
            mCursor = itemCount() - 1;
        }
    }
}
