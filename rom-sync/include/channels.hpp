#pragma once
#include <string>
#include <vector>

// A sync channel is one specific Drive file (picked by browsing, so its id
// is known exactly - no path re-resolution/escaping at sync time) synced
// into one destination folder on the SD card. The local file always keeps
// the Drive file's own name: there is no separate "local filename".
struct SyncChannel {
    std::string driveFileId;
    std::string driveFileName;
    std::string localFolder; // "sdmc:/..." destination directory, no trailing slash

    std::string localPath(void) const { return localFolder + "/" + driveFileName; }
};

namespace Channels {
    std::vector<SyncChannel> load(void);
    void save(const std::vector<SyncChannel>& channels);
}
