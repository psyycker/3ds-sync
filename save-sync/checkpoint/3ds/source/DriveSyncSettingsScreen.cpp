#include "DriveSyncSettingsScreen.hpp"

#include "DriveAuth.hpp"
#include "DriveFolderBrowserOverlay.hpp"
#include "colors.hpp"
#include "gui.hpp"
#include "main.hpp"
#include "textpool.hpp"
#include "thread.hpp"

#include <3ds.h>

DriveSyncSettingsScreen::DriveSyncSettingsScreen(std::shared_ptr<Screen> previous)
    : mPrevious(std::move(previous)), mState(std::make_shared<State>())
{
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        mState->status = "Signing in...";
    }

    std::shared_ptr<State> state = mState;
    Threads::create([state]() {
        std::string accessToken, error;
        bool ok = DriveAuth::getAccessToken(accessToken, error);

        std::lock_guard<std::mutex> lock(state->mutex);
        if (ok) {
            state->signedIn    = true;
            state->accessToken = accessToken;
            state->status      = "Signed in.";
        }
        else {
            state->authFailed = true;
            state->status     = "Sign-in failed: " + error;
        }
    });
}

void DriveSyncSettingsScreen::drawTop(void) const
{
    C2D_TargetClear(g_top, COLOR_BASE);
    C2D_SceneBegin(g_top);

    std::string status, accessToken;
    bool signedIn, authFailed;
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        status      = mState->status;
        accessToken = mState->accessToken;
        signedIn    = mState->signedIn;
        authFailed  = mState->authFailed;
    }

    TextPool::get().draw("Drive sync settings", 16.0f, 16.0f, 0.6f, COLOR_MUTED);

    if (!signedIn) {
        TextPool::get().drawWrapped(status, 16.0f, 48.0f, 0.55f, authFailed ? COLOR_DANGER : COLOR_TEXT, 368.0f);
        TextPool::get().draw("B: back", 16.0f, 220.0f, 0.5f, COLOR_FAINT);
        return;
    }

    TextPool::get().draw("Pick a Drive folder for each category:", 16.0f, 44.0f, 0.5f, COLOR_MUTED);

    float y = 72.0f;
    int i   = 0;
    for (auto cat : DriveSyncConfig::allCategories()) {
        DriveSyncConfig::CategoryFolder cf = DriveSyncConfig::categoryFolder(cat);
        bool selected                      = (i == mCursor);
        u32 labelColor                     = selected ? COLOR_TEAL : COLOR_TEXT;
        std::string label                  = DriveSyncConfig::categoryLabel(cat);
        std::string value                  = cf.id.empty() ? "(not set)" : cf.path;

        TextPool::get().draw(selected ? "> " + label : "  " + label, 16.0f, y, 0.5f, labelColor);
        TextPool::get().draw(TextPool::get().truncate(value, 240.0f, 0.45f), 160.0f, y + 2.0f, 0.45f, cf.id.empty() ? COLOR_FAINT : COLOR_MUTED);
        y += 26.0f;
        i++;
    }

    TextPool::get().draw("A: choose folder   B: back", 16.0f, y + 12.0f, 0.45f, COLOR_FAINT);
}

void DriveSyncSettingsScreen::drawBottom(void) const
{
    C2D_SceneBegin(g_bottom);
}

void DriveSyncSettingsScreen::update(const InputState&)
{
    bool signedIn;
    std::string accessToken;
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        signedIn    = mState->signedIn;
        accessToken = mState->accessToken;
    }

    u32 kDown = hidKeysDown();

    if (!signedIn) {
        if (kDown & KEY_B) {
            g_pendingScreen = mPrevious;
        }
        return;
    }

    const auto& categories = DriveSyncConfig::allCategories();

    if (kDown & KEY_DOWN) {
        mCursor = (mCursor + 1) % (int)categories.size();
    }
    else if (kDown & KEY_UP) {
        mCursor = (mCursor - 1 + (int)categories.size()) % (int)categories.size();
    }
    else if (kDown & KEY_A) {
        DriveSyncConfig::Category cat = categories[mCursor];
        std::string prompt            = std::string("Choose folder for ") + DriveSyncConfig::categoryLabel(cat);
        currentOverlay                = std::make_shared<DriveFolderBrowserOverlay>(
            *this, accessToken, prompt, [cat](const std::string& id, const std::string& path) { DriveSyncConfig::setCategoryFolder(cat, id, path); });
    }
    else if (kDown & KEY_B) {
        g_pendingScreen = mPrevious;
    }
}
