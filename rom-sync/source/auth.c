#include "auth.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"
#include "https.h"
#include "json_lite.h"

#define OAUTH_HOST "oauth2.googleapis.com"
// drive.readonly (not drive.file): rom-sync needs to see and read files the
// user already has in their Drive, not just files the app itself created.
#define SCOPE "https://www.googleapis.com/auth/drive.readonly"
#define TOKEN_DIR "sdmc:/3ds/rom-sync"
#define TOKEN_PATH TOKEN_DIR "/refresh_token.txt"

static char respbuf[8192];

static bool read_refresh_token(char* out, size_t outsize)
{
    FILE* f = fopen(TOKEN_PATH, "r");
    if (!f)
        return false;
    size_t n = fread(out, 1, outsize - 1, f);
    fclose(f);
    out[n] = '\0';
    // strip trailing newline/whitespace
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ')) {
        out[--n] = '\0';
    }
    return n > 0;
}

static void write_refresh_token(const char* token)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir(TOKEN_DIR, 0777);
    FILE* f = fopen(TOKEN_PATH, "w");
    if (!f) {
        printf("WARNING: failed to save refresh token to SD card.\n");
        return;
    }
    fputs(token, f);
    fclose(f);
    printf("Refresh token saved to " TOKEN_PATH "\n");
}

static bool refresh_access_token(const char* refreshToken, char* accessToken, size_t accessTokenSize)
{
    char body[768];
    snprintf(body, sizeof(body),
        "client_id=%s&client_secret=%s&refresh_token=%s&grant_type=refresh_token",
        GOOGLE_CLIENT_ID, GOOGLE_CLIENT_SECRET, refreshToken);

    if (!https_request("POST", OAUTH_HOST, "/token",
            "Content-Type: application/x-www-form-urlencoded\r\n", body, strlen(body), respbuf,
            sizeof(respbuf), NULL))
        return false;

    if (!json_get_string(respbuf, "access_token", accessToken, accessTokenSize)) {
        printf("refresh failed, raw response:\n%s\n", respbuf);
        return false;
    }
    return true;
}

static bool request_device_code(char* deviceCode, size_t deviceCodeSize, char* userCode,
    size_t userCodeSize, char* verificationUrl, size_t verificationUrlSize, int* interval, int* expiresIn)
{
    char body[512];
    snprintf(body, sizeof(body), "client_id=%s&scope=%s", GOOGLE_CLIENT_ID, SCOPE);

    if (!https_request("POST", OAUTH_HOST, "/device/code",
            "Content-Type: application/x-www-form-urlencoded\r\n", body, strlen(body), respbuf,
            sizeof(respbuf), NULL))
        return false;

    if (!json_get_string(respbuf, "device_code", deviceCode, deviceCodeSize))
        return false;
    if (!json_get_string(respbuf, "user_code", userCode, userCodeSize))
        return false;
    if (!json_get_string(respbuf, "verification_url", verificationUrl, verificationUrlSize))
        return false;
    if (!json_get_int(respbuf, "interval", interval))
        *interval = 5;
    if (!json_get_int(respbuf, "expires_in", expiresIn))
        *expiresIn = 1800;
    return true;
}

typedef enum {
    POLL_PENDING,
    POLL_SLOW_DOWN,
    POLL_SUCCESS,
    POLL_DENIED,
    POLL_EXPIRED,
    POLL_TRANSPORT_ERROR,
} PollResult;

static PollResult poll_token(
    const char* deviceCode, char* accessToken, size_t accessTokenSize, char* refreshToken, size_t refreshTokenSize)
{
    char body[768];
    snprintf(body, sizeof(body),
        "client_id=%s&client_secret=%s&device_code=%s&grant_type=urn:ietf:params:oauth:grant-type:device_code",
        GOOGLE_CLIENT_ID, GOOGLE_CLIENT_SECRET, deviceCode);

    if (!https_request("POST", OAUTH_HOST, "/token",
            "Content-Type: application/x-www-form-urlencoded\r\n", body, strlen(body), respbuf,
            sizeof(respbuf), NULL))
        return POLL_TRANSPORT_ERROR;

    char errField[64];
    if (json_get_string(respbuf, "error", errField, sizeof(errField))) {
        if (strcmp(errField, "authorization_pending") == 0)
            return POLL_PENDING;
        if (strcmp(errField, "slow_down") == 0)
            return POLL_SLOW_DOWN;
        if (strcmp(errField, "access_denied") == 0)
            return POLL_DENIED;
        if (strcmp(errField, "expired_token") == 0)
            return POLL_EXPIRED;
        return POLL_TRANSPORT_ERROR;
    }

    if (json_get_string(respbuf, "access_token", accessToken, accessTokenSize)
        && json_get_string(respbuf, "refresh_token", refreshToken, refreshTokenSize)) {
        return POLL_SUCCESS;
    }
    return POLL_TRANSPORT_ERROR;
}

static bool cancellable_sleep(int seconds)
{
    int elapsedMs = 0;
    while (elapsedMs < seconds * 1000) {
        hidScanInput();
        if (hidKeysDown() & KEY_START)
            return true;

        svcSleepThread(100 * 1000 * 1000LL);
        elapsedMs += 100;

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    return false;
}

static bool run_device_flow(char* accessToken, size_t accessTokenSize)
{
    char deviceCode[256] = { 0 };
    char userCode[32] = { 0 };
    char verificationUrl[128] = { 0 };
    char refreshToken[512] = { 0 };
    int interval = 5, expiresIn = 1800;

    printf("No saved refresh token. Starting device flow...\n");
    if (!request_device_code(deviceCode, sizeof(deviceCode), userCode, sizeof(userCode), verificationUrl,
            sizeof(verificationUrl), &interval, &expiresIn)) {
        printf("Failed to get device code. Raw:\n%s\n", respbuf);
        return false;
    }

    printf("\nOn another device, go to:\n  %s\n", verificationUrl);
    printf("And enter this code:\n\n  %s\n\n(START to cancel)\n\n", userCode);

    int elapsedTotal = 0;
    while (elapsedTotal < expiresIn) {
        if (cancellable_sleep(interval)) {
            printf("Cancelled.\n");
            return false;
        }
        elapsedTotal += interval;

        printf("Polling...\n");
        PollResult r = poll_token(deviceCode, accessToken, accessTokenSize, refreshToken, sizeof(refreshToken));
        switch (r) {
        case POLL_PENDING:
            break;
        case POLL_SLOW_DOWN:
            interval += 5;
            break;
        case POLL_SUCCESS:
            write_refresh_token(refreshToken);
            return true;
        case POLL_DENIED:
            printf("Access denied.\n");
            return false;
        case POLL_EXPIRED:
            printf("Device code expired.\n");
            return false;
        case POLL_TRANSPORT_ERROR:
            printf("Transport/parse error. Raw:\n%s\n", respbuf);
            return false;
        }
    }
    printf("Timed out waiting for approval.\n");
    return false;
}

bool auth_get_access_token(char* accessToken, size_t accessTokenSize)
{
    char refreshToken[512];
    if (read_refresh_token(refreshToken, sizeof(refreshToken))) {
        printf("Found saved refresh token, exchanging for access token...\n");
        if (refresh_access_token(refreshToken, accessToken, accessTokenSize))
            return true;
        printf("Refresh failed (token revoked/expired?). Falling back to device flow.\n");
    }
    return run_device_flow(accessToken, accessTokenSize);
}
