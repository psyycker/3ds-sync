#ifndef DRIVETAGSCREEN_HPP
#define DRIVETAGSCREEN_HPP

#include "DriveSyncConfig.hpp"
#include "Screen.hpp"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

// "Set Tag" flow for one title: sign in, let the user pick a category
// (MenuOverlay), then either tag it immediately (3DS - fully path-derived,
// no file needed) or resolve it against that category's Drive folder - an
// exact "<title name>.sav" match auto-pairs with zero extra taps, otherwise
// DriveFilePickerOverlay lets the user browse/create one. Saves the result
// via DriveSyncConfig and returns to the previous screen.
//
// manualAssign skips the exact-match auto-pair entirely and always opens the
// file browser straight away, for the "save name and game name don't match"
// case - a deliberate override, not the default flow (see the SELECT menu's
// separate "Assign Save File" entry vs. "Set Drive Tag").
class DriveTagScreen : public Screen {
public:
    DriveTagScreen(std::shared_ptr<Screen> previous, uint64_t titleId, std::string titleName, bool manualAssign = false);

    void drawTop(void) const override;
    void drawBottom(void) const override;
    void update(const InputState& input) override;
    bool allowsExit(void) const override { return true; }

private:
    void showCategoryMenu(void);
    void resolveFlatCategory(DriveSyncConfig::Category cat);

    struct State {
        std::mutex mutex;
        std::string status;
        bool signedIn   = false;
        bool authFailed = false;
        bool done       = false; // tag saved (or user backed out) - safe to return
        std::string accessToken;
    };

    std::shared_ptr<Screen> mPrevious;
    std::shared_ptr<State> mState;
    uint64_t mTitleId;
    std::string mTitleName;
    bool mManualAssign;
    bool mMenuShown = false;
};

#endif
