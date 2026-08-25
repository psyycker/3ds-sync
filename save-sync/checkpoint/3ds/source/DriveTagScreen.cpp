#include "DriveTagScreen.hpp"

#include "DriveApi.hpp"
#include "DriveAuth.hpp"
#include "DriveFilePickerOverlay.hpp"
#include "MenuOverlay.hpp"
#include "colors.hpp"
#include "gui.hpp"
#include "main.hpp"
#include "textpool.hpp"
#include "thread.hpp"

#include <3ds.h>
#include <mutex>

namespace {
    // Shared between resolveFlatCategory()'s worker thread and update() -
    // separate from DriveTagScreen::State so multiple resolve attempts (if
    // the user backs out of the file picker and retries) don't need to worry
    // about stale fields left over from a previous attempt.
    struct ResolveState {
        std::mutex mutex;
        bool checked        = false;
        bool folderMissing  = false;
        bool exactMatch     = false;
        std::string matchId, matchName;
        std::string categoryFolderId, categoryFolderName, suggestedName;
        DriveSyncConfig::Category category;
    };
    std::shared_ptr<ResolveState> gResolve; // only one resolve in flight at a time, by construction
}

DriveTagScreen::DriveTagScreen(std::shared_ptr<Screen> previous, uint64_t titleId, std::string titleName, bool manualAssign)
    : mPrevious(std::move(previous)), mState(std::make_shared<State>()), mTitleId(titleId), mTitleName(std::move(titleName)),
      mManualAssign(manualAssign)
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
            state->status      = "Choose what kind of save this is:";
        }
        else {
            state->authFailed = true;
            state->status     = "Sign-in failed: " + error;
        }
    });
}

void DriveTagScreen::showCategoryMenu(void)
{
    std::vector<MenuOverlay::Item> items;
    for (auto cat : DriveSyncConfig::allCategories()) {
        // Manual assignment only makes sense for the flat (single-file)
        // categories - 3DS has no file to browse for, it is fully
        // path-derived from the title id.
        if (mManualAssign && cat == DriveSyncConfig::Category::ThreeDs) {
            continue;
        }
        items.push_back({ DriveSyncConfig::categoryLabel(cat), [this, cat]() {
                             if (cat == DriveSyncConfig::Category::ThreeDs) {
                                 DriveSyncConfig::setTitleTag(mTitleId, { cat, "", "" });
                                 std::lock_guard<std::mutex> lock(mState->mutex);
                                 mState->done   = true;
                                 mState->status = "Tagged as 3DS.";
                             }
                             else {
                                 resolveFlatCategory(cat);
                             }
                         } });
    }
    std::string prompt = mManualAssign ? "Assign a save to \"" + mTitleName + "\" - category" : "Tag \"" + mTitleName + "\" as";
    currentOverlay      = std::make_shared<MenuOverlay>(*this, prompt, std::move(items));
}

void DriveTagScreen::resolveFlatCategory(DriveSyncConfig::Category cat)
{
    DriveSyncConfig::CategoryFolder cf = DriveSyncConfig::categoryFolder(cat);

    auto resolve = std::make_shared<ResolveState>();
    resolve->category            = cat;
    resolve->categoryFolderId    = cf.id;
    resolve->categoryFolderName  = cf.path;
    resolve->suggestedName       = mTitleName + ".sav";
    gResolve                      = resolve;

    if (cf.id.empty()) {
        resolve->checked       = true;
        resolve->folderMissing = true;
        return;
    }

    if (mManualAssign) {
        // Skip the exact-name auto-pair check entirely - go straight to the
        // file browser (update() already opens it whenever !exactMatch).
        resolve->checked = true;
        return;
    }

    std::string accessToken;
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        accessToken = mState->accessToken;
    }

    Threads::create([resolve, accessToken]() {
        std::vector<DriveApi::Entry> entries;
        bool ok = DriveApi::listChildren(accessToken, resolve->categoryFolderId, entries);

        std::lock_guard<std::mutex> lock(resolve->mutex);
        if (ok) {
            for (auto& e : entries) {
                if (!e.isFolder && e.name == resolve->suggestedName) {
                    resolve->exactMatch = true;
                    resolve->matchId    = e.id;
                    resolve->matchName  = e.name;
                    break;
                }
            }
        }
        resolve->checked = true;
    });
}

void DriveTagScreen::drawTop(void) const
{
    C2D_TargetClear(g_top, COLOR_BASE);
    C2D_SceneBegin(g_top);

    std::string status;
    bool authFailed;
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        status     = mState->status;
        authFailed = mState->authFailed;
    }

    TextPool::get().draw("Set Drive tag", 16.0f, 16.0f, 0.6f, COLOR_MUTED);
    TextPool::get().drawWrapped(status, 16.0f, 48.0f, 0.55f, authFailed ? COLOR_DANGER : COLOR_TEXT, 368.0f);
    TextPool::get().draw("B: back", 16.0f, 220.0f, 0.5f, COLOR_FAINT);
}

void DriveTagScreen::drawBottom(void) const
{
    C2D_SceneBegin(g_bottom);
}

void DriveTagScreen::update(const InputState&)
{
    bool signedIn, authFailed, done;
    std::string accessToken;
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        signedIn    = mState->signedIn;
        authFailed  = mState->authFailed;
        done        = mState->done;
        accessToken = mState->accessToken;
    }

    if (done) {
        g_pendingScreen = mPrevious;
        return;
    }

    if (!signedIn) {
        if (authFailed && (hidKeysDown() & KEY_B)) {
            g_pendingScreen = mPrevious;
        }
        return;
    }

    if (!hasOverlay() && !mMenuShown) {
        mMenuShown = true;
        showCategoryMenu();
        return;
    }

    // Pick up a resolveFlatCategory() worker's result, if any is pending.
    if (gResolve) {
        bool checked, folderMissing, exactMatch;
        std::string matchId, matchName, categoryFolderId, categoryFolderName, suggestedName;
        DriveSyncConfig::Category cat;
        {
            std::lock_guard<std::mutex> lock(gResolve->mutex);
            checked             = gResolve->checked;
            folderMissing       = gResolve->folderMissing;
            exactMatch          = gResolve->exactMatch;
            matchId             = gResolve->matchId;
            matchName           = gResolve->matchName;
            categoryFolderId    = gResolve->categoryFolderId;
            categoryFolderName  = gResolve->categoryFolderName;
            suggestedName       = gResolve->suggestedName;
            cat                 = gResolve->category;
        }

        if (checked) {
            gResolve.reset();

            if (folderMissing) {
                std::lock_guard<std::mutex> lock(mState->mutex);
                mState->status = std::string(DriveSyncConfig::categoryLabel(cat))
                    + " has no Drive folder configured yet - set one in Drive Sync Settings first.";
                mState->done = true;
                return;
            }

            if (exactMatch) {
                DriveSyncConfig::setTitleTag(mTitleId, { cat, matchId, matchName });
                std::lock_guard<std::mutex> lock(mState->mutex);
                mState->status = "Paired with \"" + matchName + "\".";
                mState->done   = true;
                return;
            }

            currentOverlay = std::make_shared<DriveFilePickerOverlay>(
                *this, accessToken, categoryFolderId, categoryFolderName, suggestedName, [this, cat](const std::string& id, const std::string& name) {
                    DriveSyncConfig::setTitleTag(mTitleId, { cat, id, name });
                    std::lock_guard<std::mutex> lock(mState->mutex);
                    mState->status = "Paired with \"" + name + "\".";
                    mState->done   = true;
                });
        }
    }
}
