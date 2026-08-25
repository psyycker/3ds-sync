#ifndef DRIVESYNCSCREEN_HPP
#define DRIVESYNCSCREEN_HPP

#include "Screen.hpp"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

// Runs DriveSync::syncTitle() for one title: signs in, syncs, shows the
// result, returns to the previous screen on B. If syncTitle() reports a
// conflict (both sides changed since the last sync), prompts the user to
// pick a side via a ChoiceOverlay and finishes the sync with
// DriveSync::resolveConflict() instead of guessing.
class DriveSyncScreen : public Screen {
public:
    DriveSyncScreen(std::shared_ptr<Screen> previous, uint64_t titleId, std::string titleName);

    void drawTop(void) const override;
    void drawBottom(void) const override;
    void update(const InputState& input) override;
    bool allowsExit(void) const override { return true; }

private:
    struct State {
        std::mutex mutex;
        std::string status;
        std::string accessToken; // kept around in case resolveConflict() is needed
        bool done     = false;
        bool failed   = false;
        bool conflict = false; // set once by the sync worker; cleared once the choice overlay is shown
    };

    void startSync(void);
    void showConflictChoice(void);
    void resolve(bool uploadLocal);

    std::shared_ptr<Screen> mPrevious;
    std::shared_ptr<State> mState;
    uint64_t mTitleId;
    bool mConflictShown = false;
};

#endif
