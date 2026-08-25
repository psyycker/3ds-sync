#include "https.h"

#include <3ds.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "gts_root_r1_der.h"

#define RAW_BUF_SIZE 16384

static char* strcasestr_manual(const char* haystack, const char* needle)
{
    size_t needleLen = strlen(needle);
    for (const char* p = haystack; *p; p++) {
        if (strncasecmp(p, needle, needleLen) == 0)
            return (char*) p;
    }
    return NULL;
}

// Decodes an HTTP/1.1 chunked-transfer-encoded body in place.
// Returns the decoded length.
static size_t dechunk(char* body, size_t bodyLen, char* out, size_t outSize)
{
    size_t inPos = 0, outPos = 0;
    while (inPos < bodyLen && outPos < outSize - 1) {
        char* lineEnd = strstr(body + inPos, "\r\n");
        if (!lineEnd)
            break;

        long chunkSize = strtol(body + inPos, NULL, 16);
        if (chunkSize <= 0)
            break;

        char* chunkStart = lineEnd + 2;
        size_t avail = (size_t) chunkSize;
        if (outPos + avail > outSize - 1)
            avail = outSize - 1 - outPos;

        memcpy(out + outPos, chunkStart, avail);
        outPos += avail;

        inPos = (chunkStart - body) + chunkSize + 2; // skip data + trailing \r\n
    }
    out[outPos] = '\0';
    return outPos;
}

bool https_post_form(const char* host, const char* path, const char* body, char* out, size_t outsize)
{
    bool success = false;
    Result ret;
    struct addrinfo hints, *resaddr = NULL, *cur;
    int sockfd = -1;
    sslcContext ctx;
    u32 rootCertChain = 0;
    char* rawbuf = NULL;

    out[0] = '\0';

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, "443", &hints, &resaddr) != 0) {
        printf("getaddrinfo(%s) failed\n", host);
        return false;
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        freeaddrinfo(resaddr);
        return false;
    }

    for (cur = resaddr; cur != NULL; cur = cur->ai_next) {
        if (connect(sockfd, cur->ai_addr, cur->ai_addrlen) == 0)
            break;
    }
    freeaddrinfo(resaddr);

    if (cur == NULL) {
        printf("connect(%s) failed\n", host);
        closesocket(sockfd);
        return false;
    }

    ret = sslcCreateRootCertChain(&rootCertChain);
    if (R_FAILED(ret)) {
        printf("sslcCreateRootCertChain: 0x%08lx\n", ret);
        goto cleanup_sock;
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

    ret = sslcStartConnection(&ctx, NULL, NULL);
    if (R_FAILED(ret)) {
        printf("sslcStartConnection: 0x%08lx\n", ret);
        goto cleanup_ctx;
    }

    {
        char req[1024];
        int bodyLen = (int) strlen(body);
        int reqLen = snprintf(req, sizeof(req),
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: 3ds-save-sync-spike\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n\r\n"
            "%s",
            path, host, bodyLen, body);

        if (reqLen <= 0 || reqLen >= (int) sizeof(req)) {
            printf("request too large\n");
            goto cleanup_ctx;
        }

        ret = sslcWrite(&ctx, (u8*) req, reqLen);
        if (R_FAILED(ret)) {
            printf("sslcWrite: 0x%08lx\n", ret);
            goto cleanup_ctx;
        }
    }

    rawbuf = malloc(RAW_BUF_SIZE);
    if (!rawbuf) {
        printf("malloc failed\n");
        goto cleanup_ctx;
    }

    {
        size_t total = 0;
        for (;;) {
            if (total >= RAW_BUF_SIZE - 1)
                break;
            ret = sslcRead(&ctx, rawbuf + total, RAW_BUF_SIZE - 1 - total, false);
            if (ret <= 0)
                break;
            total += (size_t) ret;
        }
        rawbuf[total] = '\0';

        char* headerEnd = strstr(rawbuf, "\r\n\r\n");
        if (!headerEnd) {
            printf("malformed HTTP response (no header/body split)\n");
            goto cleanup_raw;
        }
        *headerEnd = '\0';
        char* bodyStart = headerEnd + 4;
        size_t bodyLen = total - (bodyStart - rawbuf);

        if (strcasestr_manual(rawbuf, "Transfer-Encoding: chunked") != NULL) {
            dechunk(bodyStart, bodyLen, out, outsize);
        } else {
            size_t copyLen = bodyLen < outsize - 1 ? bodyLen : outsize - 1;
            memcpy(out, bodyStart, copyLen);
            out[copyLen] = '\0';
        }
        success = true;
    }

cleanup_raw:
    free(rawbuf);
cleanup_ctx:
    sslcDestroyContext(&ctx);
cleanup_chain:
    sslcDestroyRootCertChain(rootCertChain);
cleanup_sock:
    closesocket(sockfd);
    return success;
}
