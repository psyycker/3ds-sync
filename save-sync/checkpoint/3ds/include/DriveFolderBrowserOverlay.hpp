#ifndef DRIVEFOLDERBROWSEROVERLAY_HPP
#define DRIVEFOLDERBROWSEROVERLAY_HPP

#include "DriveApi.hpp"
#include "ListPickerOverlay.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Drive equivalent of FolderBrowserOverlay (same chrome, same A/X/B scheme),
// starting at My Drive's root and listing only sub-folders. Every navigation
// step is a network call, so listings load on a worker thread; the overlay
// shows a loading state in the meantime rather than blocking the render loop.
//   A     descend into the highlighted folder
//   X     use the current folder (invokes onPick with its Drive folder id
//         and a "/"-joined display path, then dismisses)
//   B     go up one level; at the root it cancels the overlay
class DriveFolderBrowserOverlay : public ListPickerOverlay {
public:
    DriveFolderBrowserOverlay(
        Screen& screen, const std::string& accessToken, const std::string& prompt, std::function<void(const std::string&, const std::string&)> onPick);
    void update(const InputState& input) override;

private:
    int rowCount(void) const override;
    void drawHeaderExtra(void) const override;
    void drawEmptyMessage(void) const override;
    void drawRowContent(int k, int rowY, bool selected) const override;
    std::string bottomHints(void) const override;

    // Kicks off an async listChildren() for mStack.back()'s (or root's) id.
    void reload();

    std::string mAccessToken;
    std::function<void(const std::string&, const std::string&)> mOnPick;

    // Navigation stack: each entry is a folder we've descended into. Empty =
    // at the root. currentId()/currentPath() derive from this.
    struct StackEntry {
        std::string id;
        std::string name;
    };
    std::vector<StackEntry> mStack;
    std::string currentId(void) const;
    std::string currentPath(void) const;

    // Shared with the worker thread doing the actual listing.
    struct LoadState {
        std::mutex mutex;
        std::vector<DriveApi::Entry> folders; // folders only, filtered+sorted
        bool loading = true;
        bool failed  = false;
        // Bumped each reload(); a worker thread that finishes after being
        // superseded by a newer reload() checks this and discards its result.
        int generation = 0;
    };
    std::shared_ptr<LoadState> mLoad;
    int mGeneration = 0;
};

#endif
