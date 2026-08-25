#include "drive.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "https.h"
#include "json_lite.h"

#define DRIVE_HOST "www.googleapis.com"
#define FOLDER_MIME "application/vnd.google-apps.folder"

static char respbuf[65536];

static void url_encode_into(char* out, size_t outsize, const char* in)
{
    static const char* hex = "0123456789ABCDEF";
    size_t o = 0;
    for (; *in && o + 4 < outsize; in++) {
        unsigned char c = (unsigned char) *in;
        bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'
            || c == '.' || c == '~';
        if (safe) {
            out[o++] = (char) c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0xF];
        }
    }
    out[o] = '\0';
}

// Escapes single quotes for embedding a raw name into a Drive `q` string
// literal (Drive's query grammar uses '\'' as the escape).
static void escape_for_query(char* out, size_t outsize, const char* in)
{
    size_t o = 0;
    for (; *in && o + 2 < outsize; in++) {
        if (*in == '\'')
            out[o++] = '\\';
        out[o++] = *in;
    }
    out[o] = '\0';
}

static bool find_child_by_name(const char* accessToken, const char* parentId, const char* name, DriveFile* out)
{
    char authHeader[2200];
    snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s\r\n", accessToken);

    char escapedName[384];
    escape_for_query(escapedName, sizeof(escapedName), name);

    char q[600];
    snprintf(q, sizeof(q), "'%s' in parents and name = '%s' and trashed = false", parentId, escapedName);

    char qEncoded[1900];
    url_encode_into(qEncoded, sizeof(qEncoded), q);

    char path[2000];
    snprintf(path, sizeof(path), "/drive/v3/files?q=%s&fields=files(id,name,mimeType)&pageSize=1", qEncoded);

    if (!https_request("GET", DRIVE_HOST, path, authHeader, NULL, 0, respbuf, sizeof(respbuf), NULL))
        return false;

    char obj[1024];
    if (!json_get_array_object(respbuf, "files", 0, obj, sizeof(obj)))
        return false;

    if (!json_get_string(obj, "id", out->id, sizeof(out->id)))
        return false;
    json_get_string(obj, "name", out->name, sizeof(out->name));
    json_get_string(obj, "mimeType", out->mimeType, sizeof(out->mimeType));
    return true;
}

bool drive_resolve_path(const char* accessToken, const char* drivePath, DriveFile* outFile)
{
    char pathCopy[512];
    snprintf(pathCopy, sizeof(pathCopy), "%s", drivePath);

    char currentId[128];
    snprintf(currentId, sizeof(currentId), "root");
    strcpy(outFile->name, "My Drive");
    strcpy(outFile->mimeType, FOLDER_MIME);
    strcpy(outFile->id, "root");

    char* saveptr = NULL;
    char* segment = strtok_r(pathCopy, "/", &saveptr);
    while (segment != NULL) {
        if (segment[0] == '\0') {
            segment = strtok_r(NULL, "/", &saveptr);
            continue;
        }
        DriveFile match;
        if (!find_child_by_name(accessToken, currentId, segment, &match)) {
            printf("Drive path not found: ...%s%s\n", "/", segment);
            return false;
        }
        *outFile = match;
        snprintf(currentId, sizeof(currentId), "%s", match.id);
        segment = strtok_r(NULL, "/", &saveptr);
    }
    return true;
}

// https_progress_cb and drive_progress_cb have different signatures (the
// latter also carries a file name, since a folder sync downloads many
// files); adapt via a small static shim rather than casting function
// pointers across incompatible signatures.
static drive_progress_cb g_activeCb;
static void* g_activeUserdata;
static const char* g_activeFileName;

static void https_progress_shim(size_t received, size_t total, void* userdata)
{
    (void) userdata;
    if (g_activeCb)
        g_activeCb(g_activeFileName, received, total, g_activeUserdata);
}

static bool download_file_impl(
    const char* accessToken, const DriveFile* file, const char* localPath, drive_progress_cb onProgress, void* userdata)
{
    char authHeader[2200];
    snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s\r\n", accessToken);

    char path[256];
    snprintf(path, sizeof(path), "/drive/v3/files/%s?alt=media", file->id);

    printf("  %s -> %s\n", file->name, localPath);

    g_activeCb = onProgress;
    g_activeUserdata = userdata;
    g_activeFileName = file->name;

    bool ok = https_download_to_file(
        "GET", DRIVE_HOST, path, authHeader, NULL, 0, localPath, onProgress ? https_progress_shim : NULL, NULL);

    g_activeCb = NULL;
    return ok;
}

static bool sync_folder_recursive(
    const char* accessToken, const char* folderId, const char* localDir, drive_progress_cb onProgress, void* userdata)
{
    mkdir(localDir, 0777);

    char authHeader[2200];
    snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s\r\n", accessToken);

    char q[256];
    snprintf(q, sizeof(q), "'%s' in parents and trashed = false", folderId);
    char qEncoded[512];
    url_encode_into(qEncoded, sizeof(qEncoded), q);

    char path[700];
    snprintf(path, sizeof(path), "/drive/v3/files?q=%s&fields=files(id,name,mimeType)&pageSize=100", qEncoded);

    if (!https_request("GET", DRIVE_HOST, path, authHeader, NULL, 0, respbuf, sizeof(respbuf), NULL)) {
        printf("failed to list folder contents\n");
        return false;
    }

    bool allOk = true;
    char obj[1024];
    for (int i = 0; json_get_array_object(respbuf, "files", i, obj, sizeof(obj)); i++) {
        DriveFile child;
        if (!json_get_string(obj, "id", child.id, sizeof(child.id)))
            continue;
        json_get_string(obj, "name", child.name, sizeof(child.name));
        json_get_string(obj, "mimeType", child.mimeType, sizeof(child.mimeType));

        char childLocalPath[600];
        snprintf(childLocalPath, sizeof(childLocalPath), "%s/%s", localDir, child.name);

        if (strcmp(child.mimeType, FOLDER_MIME) == 0) {
            if (!sync_folder_recursive(accessToken, child.id, childLocalPath, onProgress, userdata))
                allOk = false;
        } else {
            if (!download_file_impl(accessToken, &child, childLocalPath, onProgress, userdata))
                allOk = false;
        }
    }
    return allOk;
}

bool drive_sync_path(
    const char* accessToken, const char* drivePath, const char* localPath, drive_progress_cb onProgress, void* userdata)
{
    DriveFile file;
    if (!drive_resolve_path(accessToken, drivePath, &file))
        return false;

    if (strcmp(file.mimeType, FOLDER_MIME) == 0)
        return sync_folder_recursive(accessToken, file.id, localPath, onProgress, userdata);

    return download_file_impl(accessToken, &file, localPath, onProgress, userdata);
}
