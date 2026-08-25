#ifndef DRIVEFILEPICKEROVERLAY_HPP
#define DRIVEFILEPICKEROVERLAY_HPP

#include "DriveApi.hpp"
#include "ListPickerOverlay.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Browses a Drive folder (starting at rootId) for the tag-time file-pairing
// step: lists both sub-folders (to navigate into) and files (to pick), and
// offers creating a brand new file named `suggestedName` in the current
// folder for the common case where no existing file matches.
//   A     on a folder: descend. On a file: pick it (invokes onPick, dismisses).
//   X     create a new file named suggestedName here, then pick it
//   B     go up one level; at rootId it cancels the overlay
class DriveFilePickerOverlay : public ListPickerOverlay {
public:
    DriveFilePickerOverlay(Screen& screen, const std::string& accessToken, const std::string& rootId, const std::string& rootName,
        const std::string& suggestedName, std::function<void(const std::string& fileId, const std::string& fileName)> onPick);
    void update(const InputState& input) override;

private:
    int rowCount(void) const override;
    void drawHeaderExtra(void) const override;
    void drawEmptyMessage(void) const override;
    void drawRowContent(int k, int rowY, bool selected) const override;
    std::string bottomHints(void) const override;

    void reload(void);
    void createHere(void);

    std::string mAccessToken;
    std::string mSuggestedName;
    std::function<void(const std::string&, const std::string&)> mOnPick;

    struct StackEntry {
        std::string id;
        std::string name;
    };
    std::string mRootId, mRootName;
    std::vector<StackEntry> mStack;
    std::string currentId(void) const;
    std::string currentPath(void) const;

    struct LoadState {
        std::mutex mutex;
        std::vector<DriveApi::Entry> entries; // folders first, then files (server-sorted)
        bool loading  = true;
        bool failed   = false;
        bool creating = false;
        // Set by createHere()'s worker thread on success; update() (main
        // thread) picks this up next frame and performs the actual
        // onPick()+removeOverlay() itself - both of those touch Screen/
        // Overlay state and must never run off the render thread.
        bool created = false;
        std::string createdFileId, createdFileName;
        int generation = 0;
    };
    std::shared_ptr<LoadState> mLoad;
    int mGeneration = 0;
};

#endif
