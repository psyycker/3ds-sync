#ifndef DRIVEAPI_HPP
#define DRIVEAPI_HPP

#include <cstdint>
#include <string>
#include <vector>

// Thin wrapper over the bits of the Drive v3 REST API the sync feature
// needs: browsing folders/files (for the settings folder-picker and the
// per-title file-pairing step) and basic file IO. Every call is a single
// blocking HTTPS request - callers on the render thread must run these from
// a worker thread (see DriveAuth.hpp's threading note, which applies here
// too).
namespace DriveApi {
    struct Entry {
        std::string id;
        std::string name;
        bool isFolder = false;
        // RFC3339 UTC, e.g. "2026-08-24T12:24:25.958Z". Empty for folders
        // (we never need a folder's own modified time).
        std::string modifiedTime;
        // Server-computed MD5 (hex, lowercase). Empty for folders. Fetched in
        // the same listing call so a folder-mirror sync (many files) doesn't
        // need one extra request per file just to check what changed.
        std::string md5;
    };

    // Lists the immediate children of `folderId` ("root" = My Drive's root).
    // Folders first, then files, each alphabetical. Only non-trashed items.
    // Returns false on a transport/parse error (out params left as-is).
    bool listChildren(const std::string& accessToken, const std::string& folderId, std::vector<Entry>& out);

    // Creates a folder named `name` under `parentId`. Returns the new
    // folder's id, or empty on failure.
    std::string createFolder(const std::string& accessToken, const std::string& parentId, const std::string& name);

    // Downloads a file's raw content (alt=media). Returns false on failure;
    // out is otherwise replaced (not appended).
    bool downloadFile(const std::string& accessToken, const std::string& fileId, std::string& out);

    // Uploads `content` as a new file named `name` under `parentId`.
    // Returns the new file's id, or empty on failure.
    std::string createFile(
        const std::string& accessToken, const std::string& parentId, const std::string& name, const std::string& content);

    // Overwrites an existing file's content in place (media-only update,
    // does not touch its name/parents). Returns false on failure.
    bool updateFileContent(const std::string& accessToken, const std::string& fileId, const std::string& content);

    // Fetches a file's server-computed MD5 (hex, lowercase) without
    // downloading its content - the cheap way to check "did this change"
    // before paying for a full download. Returns false on failure (e.g. the
    // id doesn't exist, or - for a Google-native doc type, which has no
    // content checksum - is empty).
    bool getFileMd5(const std::string& accessToken, const std::string& fileId, std::string& outMd5);
}

#endif
