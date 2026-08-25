#ifndef DRIVESYNCSETTINGSSCREEN_HPP
#define DRIVESYNCSETTINGSSCREEN_HPP

#include "DriveSyncConfig.hpp"
#include "Screen.hpp"
#include <memory>
#include <mutex>
#include <string>

// Settings screen for the Drive sync feature: signs in (service-account JWT
// exchange, see DriveAuth.hpp), then lets the user pick which Drive folder
// each of the four save categories (3DS / GBA / NDS / GB-GBC) syncs against,
// via DriveFolderBrowserOverlay.
class DriveSyncSettingsScreen : public Screen {
public:
    explicit DriveSyncSettingsScreen(std::shared_ptr<Screen> previous);

    void drawTop(void) const override;
    void drawBottom(void) const override;
    void update(const InputState& input) override;
    bool allowsExit(void) const override { return true; }

private:
    struct State {
        std::mutex mutex;
        std::string status;
        std::string accessToken; // filled once signed in
        bool signedIn   = false;
        bool authFailed = false;
    };

    std::shared_ptr<Screen> mPrevious;
    std::shared_ptr<State> mState;
    int mCursor = 0;
};

#endif
