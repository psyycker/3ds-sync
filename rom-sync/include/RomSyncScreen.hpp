#pragma once
#include "Screen.hpp"
#include "channels.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// The one screen of rom-sync: signs in as the service account, then shows
// the configured sync channels plus "Add channel" / "Sync all". Adding a
// channel chains two overlays (both ported from checkpoint) - pick a Drive
// file, then pick an SD destination folder - so a channel is always backed
// by a Drive file id resolved once at pick time, never a typed/re-resolved
// path.
class RomSyncScreen : public Screen {
public:
    RomSyncScreen(void);

    void drawTop(void) const override;
    void drawBottom(void) const override;
    void update(const InputState& input) override;
    bool allowsExit(void) const override;

private:
    struct AuthState {
        std::mutex mutex;
        bool signedIn   = false;
        bool authFailed = false;
        std::string accessToken;
        std::string status = "Signing in...";
    };
    std::shared_ptr<AuthState> mAuth;

    struct SyncState {
        std::mutex mutex;
        bool running = false;
        std::string message;
    };
    std::shared_ptr<SyncState> mSync;

    std::vector<SyncChannel> mChannels;
    int mCursor = 0;

    // itemCount = mChannels.size() + 2 ("Add channel", "Sync all channels").
    int itemCount(void) const { return (int)mChannels.size() + 2; }

    void startAddChannel(const std::string& accessToken);
    void startSync(int channelIndex); // -1 = sync every channel
};
