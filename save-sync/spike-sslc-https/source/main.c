// Spike: prove HTTPS to a real Google endpoint works from 3DS homebrew via
// libctru's sslc service, using a genuine root CA (GTS Root R1) instead of
// SSLCOPT_DisableVerify. This validates the networking assumption behind the
// planned Google Drive save-sync client before any Drive/OAuth code is written.

#include <3ds.h>
#include <malloc.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "gts_root_r1_der.h"

#define HOST "www.googleapis.com"
#define REQUEST_PATH "/oauth2/v3/certs"

static char readbuf[4096];

static void https_get(const char* host, const char* path)
{
    Result ret;
    struct addrinfo hints, *resaddr = NULL, *cur;
    int sockfd;
    sslcContext ctx;
    u32 rootCertChain = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    printf("Resolving %s...\n", host);
    if (getaddrinfo(host, "443", &hints, &resaddr) != 0) {
        printf("getaddrinfo() failed.\n");
        return;
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("socket() failed.\n");
        freeaddrinfo(resaddr);
        return;
    }

    printf("Connecting...\n");
    for (cur = resaddr; cur != NULL; cur = cur->ai_next) {
        if (connect(sockfd, cur->ai_addr, cur->ai_addrlen) == 0)
            break;
    }
    freeaddrinfo(resaddr);

    if (cur == NULL) {
        printf("connect() failed.\n");
        closesocket(sockfd);
        return;
    }

    ret = sslcCreateRootCertChain(&rootCertChain);
    if (R_FAILED(ret)) {
        printf("sslcCreateRootCertChain: 0x%08lx\n", ret);
        closesocket(sockfd);
        return;
    }

    ret = sslcAddTrustedRootCA(rootCertChain, (u8*) gts_root_r1_der, gts_root_r1_der_size, NULL);
    if (R_FAILED(ret)) {
        printf("sslcAddTrustedRootCA: 0x%08lx\n", ret);
        goto cleanup_chain;
    }

    ret = sslcCreateContext(&ctx, sockfd, SSLCOPT_Default, host);
    if (R_FAILED(ret)) {
        printf("sslcCreateContext: 0x%08lx\n", ret);
        goto cleanup_chain;
    }

    ret = sslcContextSetRootCertChain(&ctx, rootCertChain);
    if (R_FAILED(ret)) {
        printf("sslcContextSetRootCertChain: 0x%08lx\n", ret);
        goto cleanup_ctx;
    }

    printf("TLS handshake (real cert verification)...\n");
    ret = sslcStartConnection(&ctx, NULL, NULL);
    if (R_FAILED(ret)) {
        printf("sslcStartConnection FAILED: 0x%08lx\n", ret);
        printf("(cert verification likely rejected the chain)\n");
        goto cleanup_ctx;
    }
    printf("Handshake OK - server certificate verified against\n");
    printf("genuine GTS Root R1.\n\n");

    char req[256];
    snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: 3ds-save-sync-spike\r\n"
        "Connection: close\r\n\r\n",
        path, host);

    ret = sslcWrite(&ctx, (u8*) req, strlen(req));
    if (R_FAILED(ret)) {
        printf("sslcWrite: 0x%08lx\n", ret);
        goto cleanup_ctx;
    }

    printf("Response (first %d bytes):\n", (int) sizeof(readbuf) - 1);
    memset(readbuf, 0, sizeof(readbuf));
    ret = sslcRead(&ctx, readbuf, sizeof(readbuf) - 1, false);
    if (R_FAILED(ret)) {
        printf("sslcRead: 0x%08lx\n", ret);
        goto cleanup_ctx;
    }
    printf("%s\n", readbuf);

cleanup_ctx:
    sslcDestroyContext(&ctx);
cleanup_chain:
    sslcDestroyRootCertChain(rootCertChain);
    closesocket(sockfd);
}

int main(void)
{
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    printf("sslc HTTPS spike: %s%s\n\n", HOST, REQUEST_PATH);

    u32 socBufferSize = 0x100000;
    u32* socBuffer = (u32*) memalign(0x1000, socBufferSize);
    if (socBuffer == NULL) {
        printf("Failed to allocate SOC buffer.\n");
    } else {
        Result ret = socInit(socBuffer, socBufferSize);
        if (R_FAILED(ret)) {
            printf("socInit failed: 0x%08lx\n", ret);
        } else {
            ret = sslcInit(0);
            if (R_FAILED(ret)) {
                printf("sslcInit failed: 0x%08lx\n", ret);
            } else {
                https_get(HOST, REQUEST_PATH);
                sslcExit();
            }
            socExit();
        }
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
