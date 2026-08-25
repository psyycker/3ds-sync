// Spike: use an OAuth access token to actually round-trip a file through
// Google Drive (upload -> download -> verify) from the 3DS. This is the
// last big unknown before wiring real save files into Checkpoint's backup
// pipeline.

#include <3ds.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include "auth.h"
#include "https.h"
#include "json_lite.h"

#define DRIVE_HOST "www.googleapis.com"
#define BOUNDARY "3ds_save_sync_boundary_xyz"

static char respbuf[16384];
static char reqbuf[2048];

static const char* TEST_CONTENT = "Hello from a Nintendo 3DS! This round-tripped through Google Drive.";

static bool drive_upload(const char* accessToken, char* outFileId, size_t outFileIdSize)
{
    char authHeader[2200];
    snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s\r\nContent-Type: multipart/related; boundary=%s\r\n",
        accessToken, BOUNDARY);

    int bodyLen = snprintf(reqbuf, sizeof(reqbuf),
        "--%s\r\n"
        "Content-Type: application/json; charset=UTF-8\r\n\r\n"
        "{\"name\": \"save-sync-spike-test.txt\"}\r\n"
        "--%s\r\n"
        "Content-Type: text/plain\r\n\r\n"
        "%s\r\n"
        "--%s--\r\n",
        BOUNDARY, BOUNDARY, TEST_CONTENT, BOUNDARY);

    printf("Uploading test file to Drive (app-scoped folder)...\n");
    if (!https_request("POST", DRIVE_HOST, "/upload/drive/v3/files?uploadType=multipart", authHeader, reqbuf,
            (size_t) bodyLen, respbuf, sizeof(respbuf), NULL)) {
        printf("upload request failed (transport)\n");
        return false;
    }

    if (!json_get_string(respbuf, "id", outFileId, outFileIdSize)) {
        printf("upload failed, raw response:\n%s\n", respbuf);
        return false;
    }
    printf("Uploaded. File ID: %s\n", outFileId);
    return true;
}

static bool drive_download(const char* accessToken, const char* fileId)
{
    char authHeader[2200];
    snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s\r\n", accessToken);

    char path[256];
    snprintf(path, sizeof(path), "/drive/v3/files/%s?alt=media", fileId);

    printf("Downloading it back...\n");
    size_t outLen = 0;
    if (!https_request("GET", DRIVE_HOST, path, authHeader, NULL, 0, respbuf, sizeof(respbuf), &outLen)) {
        printf("download request failed (transport)\n");
        return false;
    }

    printf("Downloaded %d bytes:\n  \"%s\"\n\n", (int) outLen, respbuf);

    if (strcmp(respbuf, TEST_CONTENT) == 0) {
        printf("MATCH - round trip verified!\n");
        return true;
    }
    printf("MISMATCH - downloaded content differs from what was uploaded.\n");
    return false;
}

int main(void)
{
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    printf("Drive API upload/download spike\n\n");

    u32 socBufferSize = 0x100000;
    u32* socBuffer = (u32*) memalign(0x1000, socBufferSize);
    if (socBuffer == NULL) {
        printf("Failed to allocate SOC buffer.\n");
    } else if (R_FAILED(socInit(socBuffer, socBufferSize))) {
        printf("socInit failed.\n");
    } else if (R_FAILED(sslcInit(0))) {
        printf("sslcInit failed.\n");
    } else {
        static char accessToken[2048];
        if (auth_get_access_token(accessToken, sizeof(accessToken))) {
            printf("\nAccess token acquired (len %d).\n\n", (int) strlen(accessToken));

            char fileId[128] = { 0 };
            if (drive_upload(accessToken, fileId, sizeof(fileId))) {
                drive_download(accessToken, fileId);
            }
        } else {
            printf("\nFailed to acquire access token.\n");
        }

        sslcExit();
        socExit();
    }

    printf("\nPress START to exit.\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START)
            break;

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    gfxExit();
    return 0;
}
