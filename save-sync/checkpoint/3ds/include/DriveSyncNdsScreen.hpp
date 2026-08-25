#ifndef DRIVESYNCNDSSCREEN_HPP
#define DRIVESYNCNDSSCREEN_HPP

#include "Screen.hpp"
#include <memory>
#include <mutex>
#include <string>

// Runs DriveSync::syncNdsFolder() - not tied to a Checkpoint title, unlike
// DriveSyncScreen. Signs in, syncs the whole roms/nds tree, shows the
// summary, returns to the previous screen on B.
class DriveSyncNdsScreen : public Screen {
public:
    explicit DriveSyncNdsScreen(std::shared_ptr<Screen> previous);

    void drawTop(void) const override;
    void drawBottom(void) const override;
    void update(const InputState& input) override;
    bool allowsExit(void) const override { return true; }

private:
    struct State {
        std::mutex mutex;
        std::string status;
        bool done   = false;
        bool failed = false;
    };

    std::shared_ptr<Screen> mPrevious;
    std::shared_ptr<State> mState;
};

#endif
