#include "DriveSyncScreen.hpp"

#include "ChoiceOverlay.hpp"
#include "DriveAuth.hpp"
#include "DriveSync.hpp"
#include "ModalChrome.hpp"
#include "colors.hpp"
#include "gui.hpp"
#include "main.hpp"
#include "textpool.hpp"
#include "thread.hpp"

#include <3ds.h>

DriveSyncScreen::DriveSyncScreen(std::shared_ptr<Screen> previous, uint64_t titleId, std::string titleName)
    : mPrevious(std::move(previous)), mState(std::make_shared<State>()), mTitleId(titleId)
{
    (void)titleName;
    startSync();
}

void DriveSyncScreen::startSync(void)
{
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        mState->status = "Signing in...";
    }

    std::shared_ptr<State> state = mState;
    uint64_t titleId             = mTitleId;
    Threads::create([state, titleId]() {
        std::string accessToken, error;
        if (!DriveAuth::getAccessToken(accessToken, error)) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->done   = true;
            state->failed = true;
            state->status = "Sign-in failed: " + error;
            return;
        }

        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->accessToken = accessToken; // kept in case a conflict needs resolveConflict() later
            state->status      = "Syncing...";
        }

        DriveSync::Outcome outcome = DriveSync::syncTitle(titleId, accessToken);

        std::lock_guard<std::mutex> lock(state->mutex);
        if (outcome.conflict) {
            // Neither side has been written yet - update() picks this up and
            // shows a choice overlay instead of treating it as finished.
            state->conflict = true;
            state->status   = outcome.message;
        }
        else {
            state->done   = true;
            state->failed = !outcome.ok;
            state->status = outcome.message;
        }
    });
}

void DriveSyncScreen::showConflictChoice(void)
{
    std::string message;
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        message = mState->status;
    }

    currentOverlay = std::make_shared<ChoiceOverlay>(*this, message,
        ChoiceOverlay::Button{ "Upload", ModalChrome::BTN_LEFT_X, COLOR_ACCENT, COLOR_WHITE, 0,
            [this]() {
                removeOverlay();
                resolve(true);
            } },
        ChoiceOverlay::Button{ "Download", ModalChrome::BTN_RIGHT_X, COLOR_RAISED, COLOR_TEXT, 0,
            [this]() {
                removeOverlay();
                resolve(false);
            } });
}

void DriveSyncScreen::resolve(bool uploadLocal)
{
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        mState->conflict = false;
        mState->status   = "Syncing...";
    }

    std::shared_ptr<State> state = mState;
    uint64_t titleId             = mTitleId;
    Threads::create([state, titleId, uploadLocal]() {
        std::string accessToken;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            accessToken = state->accessToken;
        }

        DriveSync::Outcome outcome = DriveSync::resolveConflict(titleId, accessToken, uploadLocal);

        std::lock_guard<std::mutex> lock(state->mutex);
        state->done   = true;
        state->failed = !outcome.ok;
        state->status = outcome.message;
    });
}

void DriveSyncScreen::drawTop(void) const
{
    C2D_TargetClear(g_top, COLOR_BASE);
    C2D_SceneBegin(g_top);

    std::string status;
    bool failed;
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        status = mState->status;
        failed = mState->failed;
    }

    TextPool::get().draw("Sync to Drive", 16.0f, 16.0f, 0.6f, COLOR_MUTED);
    TextPool::get().drawWrapped(status, 16.0f, 48.0f, 0.55f, failed ? COLOR_DANGER : COLOR_TEXT, 368.0f);
    TextPool::get().draw("B: back", 16.0f, 220.0f, 0.5f, COLOR_FAINT);
}

void DriveSyncScreen::drawBottom(void) const
{
    C2D_SceneBegin(g_bottom);
}

void DriveSyncScreen::update(const InputState&)
{
    bool conflict, done;
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        conflict = mState->conflict;
        done     = mState->done;
    }

    if (conflict && !mConflictShown) {
        mConflictShown = true;
        showConflictChoice();
        return;
    }

    if (hidKeysDown() & KEY_B) {
        if (done) {
            g_pendingScreen = mPrevious;
        }
    }
}
