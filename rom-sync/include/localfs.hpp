#pragma once
#include <3ds.h>
#include <string>
#include <vector>

// Just enough of checkpoint's Archive/Directory (3ds/include/archive.hpp,
// directory.hpp) to support FolderBrowserOverlay: opening the SD archive
// once at startup and listing a directory's immediate children. Checkpoint's
// originals also cover save-data archives, raw GBA VC saves and TWL NAND
// paths - none of which rom-sync needs.
namespace Archive {
    Result init(void);
    void exit(void);
    FS_Archive sdmc(void);
}

class Directory {
public:
    Directory(FS_Archive archive, const std::u16string& root);
    ~Directory() = default;

    Result error(void);
    std::u16string entry(size_t index);
    bool folder(size_t index);
    bool good(void);
    size_t size(void);

private:
    std::vector<FS_DirectoryEntry> mList;
    Result mError;
    bool mGood;
};
