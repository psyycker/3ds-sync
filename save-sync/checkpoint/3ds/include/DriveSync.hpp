#ifndef DRIVESYNC_HPP
#define DRIVESYNC_HPP

#include <cstdint>
#include <string>

// The actual push/pull engine, for the three flat (single-file) categories -
// GBA, NDS, GB/GBC - only. The 3DS category (mirroring a whole save-archive
// directory tree against Azahar's sdmc layout) is a separate, harder problem
// and is not implemented here yet; syncTitle() reports it as such rather
// than silently doing nothing.
//
// Direction is decided by comparing both sides' current MD5 against the MD5
// recorded at the last successful sync (DriveSyncConfig::TitleTag::
// lastSyncedMd5), not by timestamp - 3DS save archives don't expose a
// trustworthy host-wall-clock mtime the way a normal filesystem file would.
// See DriveSyncConfig.hpp's field comment for the four-way comparison this
// enables (unchanged / local-only / remote-only / conflict).
//
// Blocking (does real HTTPS + on-console save IO); call from a worker
// thread, never the render thread.
namespace DriveSync {
    struct Outcome {
        bool ok;
        std::string message; // always set, success or failure - shown to the user as-is
        // True when neither ok nor a hard failure: both the console's save and
        // the Drive save have changed since the last sync, so there's no safe
        // automatic direction. Nothing has been written on either side yet;
        // `message` is a prompt to show the user, and the caller must get a
        // choice out of them and call resolveConflict() with it.
        bool conflict = false;
    };

    Outcome syncTitle(uint64_t titleId, const std::string& accessToken);

    // Finishes a syncTitle() call that came back with conflict=true, once the
    // user has picked a side. uploadLocal=true keeps the console's save
    // (uploads it, overwriting the Drive file); false keeps the Drive save
    // (downloads it, overwriting the console's save - same safety-backup net
    // as a normal pull). Re-stages the console's save and re-fetches the
    // Drive file itself rather than reusing anything from the syncTitle()
    // call that detected the conflict, since a user prompt sat in between.
    Outcome resolveConflict(uint64_t titleId, const std::string& accessToken, bool uploadLocal);

    // Folder-mirror sync for TWiLight Menu++-run NDS ROMs: recursively finds
    // every save under sdmc:/roms/nds (in any nested "saves/" folder,
    // wherever TWiLight Menu++ put it) and every .nds ROM (so a save that
    // only exists on Drive so far still has somewhere to land), and syncs
    // each by filename against the flat NDS category's Drive folder - same
    // MD5-vs-last-synced logic as syncTitle, just not tied to a Checkpoint
    // title id. See DriveSyncConfig::ndsLastSyncedMd5 for the per-filename
    // tracking this uses instead of TitleTag.
    Outcome syncNdsFolder(const std::string& accessToken);
}

#endif
