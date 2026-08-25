#include "DriveFolderBrowserOverlay.hpp"
#include "colors.hpp"
#include "glyphs.hpp"
#include "gui.hpp"
#include "i18n.hpp"
#include "textpool.hpp"
#include "thread.hpp"
#include <3ds.h>
#include <algorithm>

namespace {
    void drawFolderMark(int x, int y, int side, u32 color)
    {
        const int h = side - 4;
        C2D_DrawRectSolid(x, y + 3, 0.65f, side * 0.45f, 2, color);
        C2D_DrawRectSolid(x, y + 5, 0.65f, side, h - 3, color);
    }
}

DriveFolderBrowserOverlay::DriveFolderBrowserOverlay(
    Screen& screen, const std::string& accessToken, const std::string& prompt, std::function<void(const std::string&, const std::string&)> onPick)
    : ListPickerOverlay(screen, prompt, 62, 26, 0.46f), mAccessToken(accessToken), mOnPick(std::move(onPick)), mLoad(std::make_shared<LoadState>())
{
    reload();
}

std::string DriveFolderBrowserOverlay::currentId(void) const
{
    return mStack.empty() ? "root" : mStack.back().id;
}

std::string DriveFolderBrowserOverlay::currentPath(void) const
{
    if (mStack.empty()) {
        return "My Drive";
    }
    std::string path = "My Drive";
    for (auto& e : mStack) {
        path += "/" + e.name;
    }
    return path;
}

void DriveFolderBrowserOverlay::reload(void)
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
            return; // superseded by a newer navigation step
        }
        load->loading = false;
        load->failed  = !ok;
        load->folders.clear();
        if (ok) {
            for (auto& e : entries) {
                if (e.isFolder) {
                    load->folders.push_back(e);
                }
            }
        }
    });

    mHid.reset();
}

int DriveFolderBrowserOverlay::rowCount(void) const
{
    std::lock_guard<std::mutex> lock(mLoad->mutex);
    return (int)mLoad->folders.size();
}

void DriveFolderBrowserOverlay::drawHeaderExtra(void) const
{
    TextPool& text   = TextPool::get();
    std::string path = currentPath();
    text.draw(text.truncate(path, 328, 0.42f), 36, 40, 0.42f, COLOR_TEAL, OVERLAY_Z);
}

void DriveFolderBrowserOverlay::drawEmptyMessage(void) const
{
    TextPool& text = TextPool::get();
    bool loading, failed;
    {
        std::lock_guard<std::mutex> lock(mLoad->mutex);
        loading = mLoad->loading;
        failed  = mLoad->failed;
    }
    if (loading) {
        text.drawCentered("Loading...", 0, 400, 120, 0.46f, COLOR_MUTED, OVERLAY_Z);
    }
    else if (failed) {
        text.drawCentered("Could not reach Google Drive.", 0, 400, 120, 0.46f, COLOR_DANGER, OVERLAY_Z);
    }
    else {
        text.drawCentered("No sub-folders here.", 0, 400, 120, 0.46f, COLOR_MUTED, OVERLAY_Z);
        text.drawCentered("X to use this folder.", 0, 400, 142, 0.42f, COLOR_FAINT, OVERLAY_Z);
    }
}

void DriveFolderBrowserOverlay::drawRowContent(int k, int rowY, bool selected) const
{
    drawFolderMark(38, rowY + 4, 16, selected ? COLOR_TEAL : COLOR_MUTED);
    std::string name;
    {
        std::lock_guard<std::mutex> lock(mLoad->mutex);
        if (k < 0 || (size_t)k >= mLoad->folders.size()) {
            return;
        }
        name = mLoad->folders[k].name;
    }
    TextPool& text = TextPool::get();
    text.draw(text.truncate(name, 292, 0.46f), 64, rowY + 4, 0.46f, selected ? COLOR_TEXT : COLOR_MUTED, OVERLAY_Z);
}

std::string DriveFolderBrowserOverlay::bottomHints(void) const
{
    const bool atRoot = mStack.empty();
    return std::string(GLYPH_A) + " Open   " + GLYPH_X + " Use folder   " + GLYPH_B + " " + (atRoot ? i18n::t("common.cancel") : i18n::t("overlay.up"));
}

void DriveFolderBrowserOverlay::update(const InputState& input)
{
    (void)input;
    bool loading;
    {
        std::lock_guard<std::mutex> lock(mLoad->mutex);
        loading = mLoad->loading;
    }

    const int count = rowCount();
    mHid.update(count > 0 ? count : 1);

    if (loading) {
        return; // ignore input while a listing is in flight
    }

    u32 kDown = hidKeysDown();

    if ((kDown & KEY_A) && count > 0) {
        DriveApi::Entry picked;
        {
            std::lock_guard<std::mutex> lock(mLoad->mutex);
            picked = mLoad->folders[mHid.fullIndex()];
        }
        mStack.push_back({ picked.id, picked.name });
        reload();
        return;
    }

    if (kDown & KEY_X) {
        std::string id = currentId(), path = currentPath();
        auto pick = mOnPick;
        screen.removeOverlay();
        pick(id, path);
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
