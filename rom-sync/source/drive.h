#pragma once
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    char id[128];
    char name[256];
    char mimeType[128];
} DriveFile;

typedef void (*drive_progress_cb)(const char* fileName, size_t received, size_t total, void* userdata);

// Resolves a "/"-separated path rooted at "My Drive" (e.g.
// "Games/3DS/game.zip") to a Drive file/folder, walking one segment at a
// time via the Drive API. accessToken must already be valid (see auth.h).
// Returns false if any path segment isn't found.
bool drive_resolve_path(const char* accessToken, const char* drivePath, DriveFile* outFile);

// Resolves drivePath and downloads it to localPath (an sdmc:/... path). If
// drivePath names a regular file, localPath is the destination file path. If
// it names a folder, localPath is created as a directory and every file
// inside the Drive folder (recursively) is downloaded into it, mirroring the
// folder structure. Returns false if resolution fails or any single file
// fails to download; already-downloaded files are left in place.
bool drive_sync_path(
    const char* accessToken, const char* drivePath, const char* localPath, drive_progress_cb onProgress, void* userdata);
