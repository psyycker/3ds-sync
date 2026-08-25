#include "DriveSyncNdsScreen.hpp"

#include "DriveAuth.hpp"
#include "DriveSync.hpp"
#include "colors.hpp"
#include "gui.hpp"
#include "main.hpp"
#include "textpool.hpp"
#include "thread.hpp"

#include <3ds.h>

DriveSyncNdsScreen::DriveSyncNdsScreen(std::shared_ptr<Screen> previous) : mPrevious(std::move(previous)), mState(std::make_shared<State>())
{
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        mState->status = "Signing in...";
    }

    std::shared_ptr<State> state = mState;
    Threads::create([state]() {
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
            state->status = "Scanning roms/nds and syncing...";
        }

        DriveSync::Outcome outcome = DriveSync::syncNdsFolder(accessToken);

        std::lock_guard<std::mutex> lock(state->mutex);
        state->done   = true;
        state->failed = !outcome.ok;
        state->status = outcome.message;
    });
}

void DriveSyncNdsScreen::drawTop(void) const
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

    TextPool::get().draw("Sync NDS Saves", 16.0f, 16.0f, 0.6f, COLOR_MUTED);
    TextPool::get().drawWrapped(status, 16.0f, 48.0f, 0.55f, failed ? COLOR_DANGER : COLOR_TEXT, 368.0f);
    TextPool::get().draw("B: back", 16.0f, 220.0f, 0.5f, COLOR_FAINT);
}

void DriveSyncNdsScreen::drawBottom(void) const
{
    C2D_SceneBegin(g_bottom);
}

void DriveSyncNdsScreen::update(const InputState&)
{
    if (hidKeysDown() & KEY_B) {
        bool done;
        {
            std::lock_guard<std::mutex> lock(mState->mutex);
            done = mState->done;
        }
        if (done) {
            g_pendingScreen = mPrevious;
        }
    }
}
