#include "DriveSync.hpp"

#include "DriveApi.hpp"
#include "DriveSyncConfig.hpp"
#include "archive.hpp"
#include "backuptarget.hpp"
#include "directory.hpp"
#include "io.hpp"
#include "loader.hpp"
#include "progress.hpp"
#include "title.hpp"
#include "util.hpp"

#include <mbedtls/md5.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <unordered_map>

namespace {
    // No UI to drive - this runs on our own worker thread, off TransferJob,
    // with its own status text (see DriveSyncScreen). io::backup/restore just
    // need something conforming to report through.
    struct NullProgressSink : ProgressSink {
        void begin(const std::string&, size_t) override {}
        void startFile(const std::u16string&, u32) override {}
        void advanceBytes(u32) override {}
        void finishFile() override {}
        void end() override {}
    };

    constexpr const char* TMP_DIR_REL = "/3ds/Checkpoint/drivesync/tmp"; // FS-archive-relative (Directory/io::backup)
    constexpr const char* TMP_DIR_SD  = "sdmc:/3ds/Checkpoint/drivesync/tmp"; // same folder, stdio-relative

    std::string hexMd5(const std::string& data)
    {
        unsigned char digest[16];
        mbedtls_md5((const unsigned char*)data.data(), data.size(), digest);
        static const char* hex = "0123456789abcdef";
        std::string out;
        out.reserve(32);
        for (unsigned char b : digest) {
            out += hex[b >> 4];
            out += hex[b & 0xF];
        }
        return out;
    }

    bool readFile(const std::string& path, std::string& out)
    {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) {
            return false;
        }
        char buf[4096];
        size_t n;
        out.clear();
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            out.append(buf, n);
        }
        fclose(f);
        return true;
    }

    bool writeFile(const std::string& path, const std::string& data)
    {
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) {
            return false;
        }
        size_t written = fwrite(data.data(), 1, data.size(), f);
        fclose(f);
        return written == data.size();
    }

    // Finds the single non-folder entry directly under TMP_DIR_REL. Returns
    // false (with a message) if there isn't exactly one - a title whose save
    // materializes as more than one file isn't something this flat-category
    // syncer (built for a single raw .sav) knows how to reconcile with a
    // MyBoy/MelonDS-style one-file-per-game folder.
    bool findSingleTempFile(std::u16string& outName, std::string& outError)
    {
        Directory dir(Archive::sdmc(), StringUtils::UTF8toUTF16(TMP_DIR_REL));
        if (!dir.good()) {
            outError = "Could not read the staged save.";
            return false;
        }
        std::u16string found;
        int fileCount = 0;
        for (size_t i = 0, sz = dir.size(); i < sz; i++) {
            if (!dir.folder(i)) {
                found = dir.entry(i);
                fileCount++;
            }
        }
        if (fileCount != 1) {
            outError = "This title's save is " + std::to_string(fileCount) + " files, not a single .sav - can't sync it as a flat file yet.";
            return false;
        }
        outName = found;
        return true;
    }

    constexpr const char* NDS_ROMS_ROOT = "sdmc:/roms/nds";

    bool isJunkFile(const char* name)
    {
        // macOS AppleDouble sidecar files (e.g. "._Pokemon Sacred gold.nds"),
        // common on SD cards that have touched a Mac.
        return name[0] == '.' && name[1] == '_';
    }

    // Recursively walks `dir`, filling:
    //  - romFolders[<name>.sav] = the folder a same-named .nds ROM lives in
    //    (so a Drive-only save - never played on this 3DS yet - still has
    //    somewhere to land).
    //  - existingSaves[<name>.sav] = full stdio path, for every .sav actually
    //    sitting in a folder literally named "saves" (TWiLight Menu++'s
    //    convention, confirmed against this project's real SD card).
    void scanNdsFolder(const std::string& dir, std::unordered_map<std::string, std::string>& romFolders,
        std::unordered_map<std::string, std::string>& existingSaves)
    {
        DIR* d = opendir(dir.c_str());
        if (!d) {
            return;
        }
        bool inSavesFolder = dir.size() >= 6 && dir.compare(dir.size() - 6, 6, "/saves") == 0;

        struct dirent* ent;
        while ((ent = readdir(d)) != nullptr) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0 || isJunkFile(ent->d_name)) {
                continue;
            }
            std::string full = dir + "/" + ent->d_name;
            struct stat st;
            if (stat(full.c_str(), &st) != 0) {
                continue;
            }
            if (S_ISDIR(st.st_mode)) {
                scanNdsFolder(full, romFolders, existingSaves);
                continue;
            }
            std::string name = ent->d_name;
            if (inSavesFolder && name.size() > 4 && name.ends_with(".sav")) {
                existingSaves[name] = full;
            }
            else if (name.size() > 4 && name.ends_with(".nds")) {
                std::string saveName = name.substr(0, name.size() - 4) + ".sav";
                romFolders[saveName] = dir; // the ROM's own folder, not "saves" - caller appends that
            }
        }
        closedir(d);
    }
}

namespace DriveSync {
    Outcome syncTitle(uint64_t titleId, const std::string& accessToken)
    {
        auto tagOpt = DriveSyncConfig::titleTag(titleId);
        if (!tagOpt) {
            return { false, "This title isn't tagged yet." };
        }
        DriveSyncConfig::TitleTag tag = *tagOpt;

        if (tag.category == DriveSyncConfig::Category::ThreeDs) {
            return { false, "3DS sync isn't built yet - only GB/GBC, GBA and NDS for now." };
        }
        if (tag.driveFileId.empty()) {
            return { false, "No paired Drive file - retag this title." };
        }

        Title title;
        if (!TitleCatalog::get().getTitleById(title, titleId)) {
            return { false, "Could not find this title in the catalog." };
        }
        BackupTarget target = title.backup(BackupKind::Save);

        // Stage the console-side save into a plain SD folder via Checkpoint's
        // own (well-tested) backup path - never touch the save archive
        // directly ourselves. Fresh each run: drop any stale leftovers first.
        io::deleteFolderRecursively(Archive::sdmc(), StringUtils::UTF8toUTF16(TMP_DIR_REL));
        io::createDirectory(Archive::sdmc(), StringUtils::UTF8toUTF16(TMP_DIR_REL));

        NullProgressSink sink;
        io::IoOutcome backupOutcome = io::backup(target, StringUtils::UTF8toUTF16(TMP_DIR_REL), sink);
        if (!backupOutcome.ok) {
            return { false, "Could not read the save off this title." };
        }

        std::u16string fileName16;
        std::string findError;
        if (!findSingleTempFile(fileName16, findError)) {
            return { false, findError };
        }
        std::string fileName = StringUtils::UTF16toUTF8(fileName16);
        std::string localPath = std::string(TMP_DIR_SD) + "/" + fileName;

        std::string localContent;
        if (!readFile(localPath, localContent)) {
            return { false, "Could not read the staged save file." };
        }
        std::string localMd5 = hexMd5(localContent);

        std::string remoteMd5;
        bool haveRemoteMd5 = DriveApi::getFileMd5(accessToken, tag.driveFileId, remoteMd5);
        if (!haveRemoteMd5) {
            return { false, "Could not reach Drive to check the remote save." };
        }

        bool localChanged  = localMd5 != tag.lastSyncedMd5;
        bool remoteChanged = remoteMd5 != tag.lastSyncedMd5;

        Outcome outcome;

        if (!localChanged && !remoteChanged) {
            outcome = { true, "Already up to date." };
        }
        else if (localChanged && !remoteChanged) {
            if (!DriveApi::updateFileContent(accessToken, tag.driveFileId, localContent)) {
                return { false, "Upload to Drive failed." };
            }
            tag.lastSyncedMd5     = localMd5;
            tag.lastSyncedAt      = time(nullptr);
            tag.lastSyncDirection = "uploaded";
            outcome               = { true, "Uploaded to Drive." };
        }
        else if (!localChanged && remoteChanged) {
            std::string remoteContent;
            if (!DriveApi::downloadFile(accessToken, tag.driveFileId, remoteContent)) {
                return { false, "Download from Drive failed." };
            }
            if (!writeFile(localPath, remoteContent)) {
                return { false, "Could not stage the downloaded save." };
            }

            // Safety net before overwriting the console-side save: a normal
            // Checkpoint backup slot (so it's restorable through Checkpoint's
            // own Backup/Restore UI, no Drive-sync-specific recovery tool
            // needed), holding whatever was on the title immediately before
            // this pull. Always the same slot name - only the most recent
            // pre-pull state is kept, not an accumulating pile.
            std::u16string safetyPath = target.rootPath() + u"/[drive-sync-safety]";
            io::backup(target, safetyPath, sink); // best-effort: a failure here must not block the pull

            io::IoOutcome restoreOutcome = io::restore(target, StringUtils::UTF8toUTF16(TMP_DIR_REL), sink);
            if (!restoreOutcome.ok) {
                return { false, "Writing the downloaded save to this title failed." };
            }
            tag.lastSyncedMd5     = remoteMd5;
            tag.lastSyncedAt      = time(nullptr);
            tag.lastSyncDirection = "downloaded";
            outcome               = { true, "Downloaded from Drive." };
        }
        else {
            // Both sides changed since the last sync - a genuine conflict.
            // No signal here says which change is "right", so this favors
            // whichever save the user is holding in their hands right now
            // (the console they're actively syncing from) rather than
            // silently discarding it in favor of Drive.
            if (!DriveApi::updateFileContent(accessToken, tag.driveFileId, localContent)) {
                return { false, "Upload to Drive failed." };
            }
            tag.lastSyncedMd5     = localMd5;
            tag.lastSyncedAt      = time(nullptr);
            tag.lastSyncDirection = "uploaded";
            outcome               = { true, "Both sides had changed - kept this console's save (uploaded)." };
        }

        DriveSyncConfig::setTitleTag(titleId, tag);
        io::deleteFolderRecursively(Archive::sdmc(), StringUtils::UTF8toUTF16(TMP_DIR_REL));
        return outcome;
    }

    Outcome syncNdsFolder(const std::string& accessToken)
    {
        DriveSyncConfig::CategoryFolder cf = DriveSyncConfig::categoryFolder(DriveSyncConfig::Category::Nds);
        if (cf.id.empty()) {
            return { false, "NDS has no Drive folder configured yet - set one in Drive Sync Settings first." };
        }

        std::unordered_map<std::string, std::string> romFolders;    // save filename -> ROM's own folder
        std::unordered_map<std::string, std::string> existingSaves; // save filename -> full stdio path
        scanNdsFolder(NDS_ROMS_ROOT, romFolders, existingSaves);

        std::vector<DriveApi::Entry> driveEntries;
        if (!DriveApi::listChildren(accessToken, cf.id, driveEntries)) {
            return { false, "Could not reach Drive to list the NDS folder." };
        }
        std::unordered_map<std::string, DriveApi::Entry> driveByName;
        for (auto& e : driveEntries) {
            if (!e.isFolder) {
                driveByName[e.name] = e;
            }
        }

        std::unordered_map<std::string, bool> allNames; // set of every filename known from either side
        for (auto& [name, path] : existingSaves) {
            allNames[name] = true;
        }
        for (auto& [name, entry] : driveByName) {
            allNames[name] = true;
        }

        int uploaded = 0, downloaded = 0, unchanged = 0, skipped = 0, failed = 0;

        for (auto& [name, _] : allNames) {
            std::string localPath;
            bool localExists = false;
            auto exIt        = existingSaves.find(name);
            if (exIt != existingSaves.end()) {
                localPath   = exIt->second;
                localExists = true;
            }
            else {
                auto romIt = romFolders.find(name);
                if (romIt == romFolders.end()) {
                    // Drive has a save with no matching ROM found anywhere on
                    // this SD card - nowhere to put it.
                    skipped++;
                    continue;
                }
                localPath = romIt->second + "/saves/" + name;
            }

            std::string localContent, localMd5;
            if (localExists) {
                if (!readFile(localPath, localContent)) {
                    failed++;
                    continue;
                }
                localMd5 = hexMd5(localContent);
            }

            auto driveIt        = driveByName.find(name);
            bool remoteExists   = driveIt != driveByName.end();
            std::string remoteMd5 = remoteExists ? driveIt->second.md5 : "";

            std::string lastSynced = DriveSyncConfig::ndsLastSyncedMd5(name);
            bool localChanged      = localMd5 != lastSynced;
            bool remoteChanged     = remoteMd5 != lastSynced;

            if (!localChanged && !remoteChanged) {
                unchanged++;
                continue;
            }

            // localChanged-only and both-changed both resolve to "push" here
            // (conflict favors whichever save is on the console right now,
            // same policy as syncTitle) - remoteChanged-only is the only pull.
            if (!localChanged && remoteChanged) {
                std::string remoteContent;
                if (!DriveApi::downloadFile(accessToken, driveIt->second.id, remoteContent)) {
                    failed++;
                    continue;
                }
                size_t slash = localPath.find_last_of('/');
                if (slash != std::string::npos) {
                    mkdir(localPath.substr(0, slash).c_str(), 0777); // ensure "saves/" exists; ignore EEXIST
                }
                if (!writeFile(localPath, remoteContent)) {
                    failed++;
                    continue;
                }
                DriveSyncConfig::setNdsLastSyncedMd5(name, remoteMd5);
                downloaded++;
            }
            else {
                bool ok = remoteExists ? DriveApi::updateFileContent(accessToken, driveIt->second.id, localContent)
                                       : !DriveApi::createFile(accessToken, cf.id, name, localContent).empty();
                if (!ok) {
                    failed++;
                    continue;
                }
                DriveSyncConfig::setNdsLastSyncedMd5(name, localMd5);
                uploaded++;
            }
        }

        std::string msg =
            std::to_string(uploaded) + " uploaded, " + std::to_string(downloaded) + " downloaded, " + std::to_string(unchanged) + " unchanged";
        if (skipped > 0) {
            msg += ", " + std::to_string(skipped) + " skipped (no local ROM found)";
        }
        if (failed > 0) {
            msg += ", " + std::to_string(failed) + " failed";
        }
        return { failed == 0, msg };
    }
}
