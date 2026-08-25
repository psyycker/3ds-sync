#include "DriveFilePickerOverlay.hpp"
#include "colors.hpp"
#include "glyphs.hpp"
#include "gui.hpp"
#include "i18n.hpp"
#include "textpool.hpp"
#include "thread.hpp"
#include <3ds.h>

namespace {
    void drawFolderMark(int x, int y, int side, u32 color)
    {
        const int h = side - 4;
        C2D_DrawRectSolid(x, y + 3, 0.65f, side * 0.45f, 2, color);
        C2D_DrawRectSolid(x, y + 5, 0.65f, side, h - 3, color);
    }
    void drawFileMark(int x, int y, int side, u32 color)
    {
        C2D_DrawRectSolid(x, y, 0.65f, side * 0.75f, side, color);
    }
}

DriveFilePickerOverlay::DriveFilePickerOverlay(Screen& screen, const std::string& accessToken, const std::string& rootId,
    const std::string& rootName, const std::string& suggestedName, std::function<void(const std::string&, const std::string&)> onPick)
    : ListPickerOverlay(screen, "Pick the matching file", 62, 26, 0.46f), mAccessToken(accessToken), mSuggestedName(suggestedName),
      mOnPick(std::move(onPick)), mRootId(rootId), mRootName(rootName), mLoad(std::make_shared<LoadState>())
{
    reload();
}

std::string DriveFilePickerOverlay::currentId(void) const
{
    return mStack.empty() ? mRootId : mStack.back().id;
}

std::string DriveFilePickerOverlay::currentPath(void) const
{
    std::string path = mRootName;
    for (auto& e : mStack) {
        path += "/" + e.name;
    }
    return path;
}

void DriveFilePickerOverlay::reload(void)
{
    mGeneration++;
    std::shared_ptr<LoadState> load = mLoad;
    int gen                         = mGeneration;
    {
        std::lock_guard<std::mutex> lock(load->mutex);
        load->loading    = true;
        load->failed     = false;
        load->generation = gen;
    }

    std::string accessToken = mAccessToken;
    std::string folderId    = currentId();
    Threads::create([load, gen, accessToken, folderId]() {
        std::vector<DriveApi::Entry> entries;
        bool ok = DriveApi::listChildren(accessToken, folderId, entries);

        std::lock_guard<std::mutex> lock(load->mutex);
        if (load->generation != gen) {
            return;
        }
        load->loading = false;
        load->failed  = !ok;
        load->entries = ok ? entries : std::vector<DriveApi::Entry>{};
    });

    mHid.reset();
}

void DriveFilePickerOverlay::createHere(void)
{
    std::shared_ptr<LoadState> load = mLoad;
    {
        std::lock_guard<std::mutex> lock(load->mutex);
        if (load->creating) {
            return;
        }
        load->creating = true;
    }

    std::string accessToken = mAccessToken;
    std::string folderId    = currentId();
    std::string name        = mSuggestedName;

    Threads::create([load, accessToken, folderId, name]() {
        std::string fileId = DriveApi::createFile(accessToken, folderId, name, "");
        std::lock_guard<std::mutex> lock(load->mutex);
        load->creating = false;
        if (!fileId.empty()) {
            load->created         = true;
            load->createdFileId   = fileId;
            load->createdFileName = name;
        }
        else {
            load->failed = true;
        }
    });
}

int DriveFilePickerOverlay::rowCount(void) const
{
    std::lock_guard<std::mutex> lock(mLoad->mutex);
    return (int)mLoad->entries.size();
}

void DriveFilePickerOverlay::drawHeaderExtra(void) const
{
    TextPool& text = TextPool::get();
    text.draw(text.truncate(currentPath(), 328, 0.42f), 36, 40, 0.42f, COLOR_TEAL, OVERLAY_Z);
}

void DriveFilePickerOverlay::drawEmptyMessage(void) const
{
    TextPool& text = TextPool::get();
    bool loading, failed, creating;
    {
        std::lock_guard<std::mutex> lock(mLoad->mutex);
        loading  = mLoad->loading;
        failed   = mLoad->failed;
        creating = mLoad->creating;
    }
    if (loading || creating) {
        text.drawCentered(creating ? "Creating file..." : "Loading...", 0, 400, 120, 0.46f, COLOR_MUTED, OVERLAY_Z);
    }
    else if (failed) {
        text.drawCentered("Something went wrong.", 0, 400, 120, 0.46f, COLOR_DANGER, OVERLAY_Z);
    }
    else {
        text.drawCentered("Nothing here yet.", 0, 400, 120, 0.46f, COLOR_MUTED, OVERLAY_Z);
        text.drawCentered("X to create \"" + mSuggestedName + "\".", 0, 400, 142, 0.42f, COLOR_FAINT, OVERLAY_Z);
    }
}

void DriveFilePickerOverlay::drawRowContent(int k, int rowY, bool selected) const
{
    DriveApi::Entry e;
    {
        std::lock_guard<std::mutex> lock(mLoad->mutex);
        if (k < 0 || (size_t)k >= mLoad->entries.size()) {
            return;
        }
        e = mLoad->entries[k];
    }
    if (e.isFolder) {
        drawFolderMark(38, rowY + 4, 16, selected ? COLOR_TEAL : COLOR_MUTED);
    }
    else {
        drawFileMark(40, rowY + 3, 12, selected ? COLOR_TEAL : COLOR_MUTED);
    }
    TextPool& text = TextPool::get();
    text.draw(text.truncate(e.name, 292, 0.46f), 64, rowY + 4, 0.46f, selected ? COLOR_TEXT : COLOR_MUTED, OVERLAY_Z);
}

std::string DriveFilePickerOverlay::bottomHints(void) const
{
    const bool atRoot = mStack.empty();
    return std::string(GLYPH_A) + " Open/Pick   " + GLYPH_X + " New file   " + GLYPH_B + " " + (atRoot ? i18n::t("common.cancel") : i18n::t("overlay.up"));
}

void DriveFilePickerOverlay::update(const InputState& input)
{
    (void)input;
    bool busy, created;
    std::string createdId, createdName;
    {
        std::lock_guard<std::mutex> lock(mLoad->mutex);
        busy        = mLoad->loading || mLoad->creating;
        created     = mLoad->created;
        createdId   = mLoad->createdFileId;
        createdName = mLoad->createdFileName;
    }

    if (created) {
        auto pick = mOnPick;
        screen.removeOverlay();
        pick(createdId, createdName);
        return;
    }

    const int count = rowCount();
    mHid.update(count > 0 ? count : 1);

    if (busy) {
        return;
    }

    u32 kDown = hidKeysDown();

    if ((kDown & KEY_A) && count > 0) {
        DriveApi::Entry picked;
        {
            std::lock_guard<std::mutex> lock(mLoad->mutex);
            picked = mLoad->entries[mHid.fullIndex()];
        }
        if (picked.isFolder) {
            mStack.push_back({ picked.id, picked.name });
            reload();
        }
        else {
            auto pick = mOnPick;
            std::string id = picked.id, name = picked.name;
            screen.removeOverlay();
            pick(id, name);
        }
        return;
    }

    if (kDown & KEY_X) {
        createHere();
        return;
    }

    if (kDown & KEY_B) {
        if (mStack.empty()) {
            screen.removeOverlay();
            return;
        }
        mStack.pop_back();
        reload();
        return;
    }
}
