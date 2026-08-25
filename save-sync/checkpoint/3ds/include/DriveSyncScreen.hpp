#ifndef DRIVESYNCSCREEN_HPP
#define DRIVESYNCSCREEN_HPP

#include "Screen.hpp"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

// Runs DriveSync::syncTitle() for one title: signs in, syncs, shows the
// result, returns to the previous screen on B.
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
        bool done   = false;
        bool failed = false;
    };

    std::shared_ptr<Screen> mPrevious;
    std::shared_ptr<State> mState;
};

#endif
