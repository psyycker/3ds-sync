#ifndef DRIVESYNCCONFIG_HPP
#define DRIVESYNCCONFIG_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Local (SD card) storage for the Drive sync feature's own settings: which
// Drive folder each save "category" points to, and which category (+, for
// the flat-file categories, which exact Drive file) a given 3DS title is
// tagged with. Not the OAuth token - see DriveAuth.hpp for that.
//
// UI-thread only; no locking. Callers that touch this from a worker thread
// (as the sync/tag flows will) must marshal back to the main thread first,
// same as every other Configuration-style store in this codebase.
namespace DriveSyncConfig {
    enum class Category {
        GbGbc, // GB/GBC Virtual Console injects <-> MyOldBoy
        Gba,   // GBA Virtual Console injects <-> MyBoy
        Nds,   // NDS Virtual Console injects <-> MelonDS
        ThreeDs, // normal 3DS titles <-> Azahar sdmc mirror
    };

    const char* categoryLabel(Category c);
    // All four, in the order they should list in the UI.
    const std::vector<Category>& allCategories();

    // A flat-file category's pairing to one specific file in its Drive
    // folder (MyOldBoy/MyBoy/MelonDS). Not used for ThreeDs, which is fully
    // path-derived from the title id - see the design notes in DriveAuth.hpp
    // and this session's design discussion for why.
    struct TitleTag {
        Category category;
        // Empty for ThreeDs. For the flat categories, the paired file's
        // Drive id and the name it was paired under (kept mainly for display
        // - re-deriving it from the id is one more API call).
        std::string driveFileId;
        std::string driveFileName;
        // MD5 (hex) of the save content as of the last successful sync, on
        // whichever side won that sync. Comparing both sides' *current* MD5
        // against this - not against each other - is what lets DriveSync
        // tell "only local changed" apart from "only Drive changed" apart
        // from "neither changed" apart from a genuine conflict (both did).
        // Empty = never synced.
        std::string lastSyncedMd5;
        // Unix time of the last sync that actually moved data (a no-op "already
        // up to date" run does not update these). 0 = never. Direction is
        // "uploaded" or "downloaded", matching what actually happened on a
        // conflict (both sides changed) too - see DriveSync.hpp.
        int64_t lastSyncedAt = 0;
        std::string lastSyncDirection;
    };

    // Loads from sdmc:/3ds/Checkpoint/drivesync/sync_config.json. Safe to
    // call repeatedly; missing/corrupt file is treated as "nothing configured
    // yet", not an error.
    void load();
    // Writes the current in-memory state back to the SD card. Called after
    // every mutator below - the config is small, so there is no batching.
    void save();

    // ---- category -> Drive folder -------------------------------------
    struct CategoryFolder {
        std::string id;   // Drive folder id; empty = not configured
        std::string path; // display-only ("My Drive/Emulation/..."), never re-derived from id
    };
    CategoryFolder categoryFolder(Category c);
    void setCategoryFolder(Category c, const std::string& folderId, const std::string& path);

    // ---- title -> tag ---------------------------------------------------
    std::optional<TitleTag> titleTag(uint64_t titleId);
    void setTitleTag(uint64_t titleId, const TitleTag& tag);
    void clearTitleTag(uint64_t titleId);

    // ---- NDS folder-mirror sync (TWiLight Menu++) ------------------------
    // TWiLight Menu++-run NDS ROMs are not Checkpoint titles at all (no title
    // id, not in its catalog), so this tracks last-synced MD5 per save
    // *filename* instead - the same role lastSyncedMd5 plays in TitleTag,
    // just keyed differently. See DriveSync.hpp's syncNdsFolder for how it's
    // used.
    std::string ndsLastSyncedMd5(const std::string& fileName);
    void setNdsLastSyncedMd5(const std::string& fileName, const std::string& md5);
}

#endif
