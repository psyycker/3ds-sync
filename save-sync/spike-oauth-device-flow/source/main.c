// Spike: OAuth 2.0 Device Flow against Google's endpoints, entirely from
// the 3DS, to prove we can obtain a Drive-scoped access token + refresh
// token without a browser or keyboard-typed password on the console.

#include <3ds.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "https.h"
#include "json_lite.h"

#define OAUTH_HOST "oauth2.googleapis.com"
#define SCOPE "https://www.googleapis.com/auth/drive.file"

static char respbuf[8192];

static bool request_device_code(char* deviceCode, size_t deviceCodeSize, char* userCode,
    size_t userCodeSize, char* verificationUrl, size_t verificationUrlSize, int* interval,
    int* expiresIn)
{
    char body[512];
    snprintf(body, sizeof(body), "client_id=%s&scope=%s", GOOGLE_CLIENT_ID, SCOPE);

    if (!https_post_form(OAUTH_HOST, "/device/code", body, respbuf, sizeof(respbuf)))
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

    if (!https_post_form(OAUTH_HOST, "/token", body, respbuf, sizeof(respbuf)))
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

// Sleeps for `seconds`, in short increments so we can keep servicing
// aptMainLoop/hid and bail early if the user presses START.
static bool cancellable_sleep(int seconds)
{
    int elapsedMs = 0;
    while (elapsedMs < seconds * 1000) {
        hidScanInput();
        if (hidKeysDown() & KEY_START)
            return true; // cancelled

        svcSleepThread(100 * 1000 * 1000LL); // 100ms
        elapsedMs += 100;

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    return false;
}

int main(void)
{
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    printf("OAuth device flow spike\n\n");

    u32 socBufferSize = 0x100000;
    u32* socBuffer = (u32*) memalign(0x1000, socBufferSize);
    if (socBuffer == NULL) {
        printf("Failed to allocate SOC buffer.\n");
    } else if (R_FAILED(socInit(socBuffer, socBufferSize))) {
        printf("socInit failed.\n");
    } else if (R_FAILED(sslcInit(0))) {
        printf("sslcInit failed.\n");
    } else {
        char deviceCode[256] = { 0 };
        char userCode[32] = { 0 };
        char verificationUrl[128] = { 0 };
        int interval = 5, expiresIn = 1800;

        printf("Requesting device code...\n");
        if (!request_device_code(deviceCode, sizeof(deviceCode), userCode, sizeof(userCode),
                verificationUrl, sizeof(verificationUrl), &interval, &expiresIn)) {
            printf("Failed to get device code. Raw response:\n%s\n", respbuf);
        } else {
            printf("\nOn another device, go to:\n  %s\n", verificationUrl);
            printf("And enter this code:\n\n  %s\n\n", userCode);
            printf("(START to cancel)\n\n");

            char accessToken[2048] = { 0 };
            char refreshToken[512] = { 0 };
            bool done = false;
            int elapsedTotal = 0;

            while (!done && elapsedTotal < expiresIn) {
                if (cancellable_sleep(interval)) {
                    printf("Cancelled.\n");
                    break;
                }
                elapsedTotal += interval;

                printf("Polling...\n");
                PollResult r = poll_token(deviceCode, accessToken, sizeof(accessToken), refreshToken, sizeof(refreshToken));

                switch (r) {
                case POLL_PENDING:
                    break;
                case POLL_SLOW_DOWN:
                    interval += 5;
                    printf("Slow down requested, interval now %ds\n", interval);
                    break;
                case POLL_SUCCESS:
                    printf("\nSUCCESS!\n");
                    printf("access_token (len %d): %.40s...\n", (int) strlen(accessToken), accessToken);
                    printf("refresh_token (len %d): %.40s...\n", (int) strlen(refreshToken), refreshToken);
                    done = true;
                    break;
                case POLL_DENIED:
                    printf("Access denied by user.\n");
                    done = true;
                    break;
                case POLL_EXPIRED:
                    printf("Device code expired.\n");
                    done = true;
                    break;
                case POLL_TRANSPORT_ERROR:
                    printf("Transport/parse error. Raw:\n%s\n", respbuf);
                    done = true;
                    break;
                }
            }
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
