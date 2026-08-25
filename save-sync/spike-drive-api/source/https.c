#include "https.h"

#include <3ds.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "gts_root_r1_der.h"

#define RAW_BUF_SIZE 32768

static char* strcasestr_manual(const char* haystack, const char* needle)
{
    size_t needleLen = strlen(needle);
    for (const char* p = haystack; *p; p++) {
        if (strncasecmp(p, needle, needleLen) == 0)
            return (char*) p;
    }
    return NULL;
}

// Decodes an HTTP/1.1 chunked-transfer-encoded body. Binary-safe: uses the
// declared chunk sizes for memcpy, never strlen, on the chunk payloads
// themselves (only the hex-size line prefix of each chunk is treated as
// text, which is guaranteed ASCII by the protocol).
static size_t dechunk(const char* body, size_t bodyLen, char* out, size_t outSize)
{
    size_t inPos = 0, outPos = 0;
    while (inPos < bodyLen && outPos < outSize - 1) {
        const char* lineEnd = memchr(body + inPos, '\n', bodyLen - inPos);
        if (!lineEnd)
            break;

        long chunkSize = strtol(body + inPos, NULL, 16);
        if (chunkSize <= 0)
            break;

        const char* chunkStart = lineEnd + 1;
        size_t avail = (size_t) chunkSize;
        if (outPos + avail > outSize - 1)
            avail = outSize - 1 - outPos;

        memcpy(out + outPos, chunkStart, avail);
        outPos += avail;

        inPos = (size_t) (chunkStart - body) + chunkSize + 2; // skip data + trailing \r\n
    }
    out[outPos] = '\0';
    return outPos;
}

bool https_request(const char* method, const char* host, const char* path, const char* extraHeaders,
    const char* body, size_t bodyLen, char* out, size_t outsize, size_t* outLen)
{
    bool success = false;
    Result ret;
    struct addrinfo hints, *resaddr = NULL, *cur;
    int sockfd = -1;
    sslcContext ctx;
    u32 rootCertChain = 0;
    char* rawbuf = NULL;

    out[0] = '\0';
    if (outLen)
        *outLen = 0;

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
        char reqHeader[1536];
        int headerLen = snprintf(reqHeader, sizeof(reqHeader),
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: 3ds-save-sync-spike\r\n"
            "%s"
            "Content-Length: %d\r\n"
            "Connection: close\r\n\r\n",
            method, path, host, extraHeaders ? extraHeaders : "", (int) bodyLen);

        if (headerLen <= 0 || headerLen >= (int) sizeof(reqHeader)) {
            printf("request header too large\n");
            goto cleanup_ctx;
        }

        ret = sslcWrite(&ctx, (u8*) reqHeader, headerLen);
        if (R_FAILED(ret)) {
            printf("sslcWrite(header): 0x%08lx\n", ret);
            goto cleanup_ctx;
        }

        if (bodyLen > 0) {
            ret = sslcWrite(&ctx, (u8*) body, bodyLen);
            if (R_FAILED(ret)) {
                printf("sslcWrite(body): 0x%08lx\n", ret);
                goto cleanup_ctx;
            }
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
        size_t headerLen = (size_t) (headerEnd - rawbuf);
        char savedByte = rawbuf[headerLen];
        rawbuf[headerLen] = '\0'; // temporarily terminate for the header-only strcasestr scan
        bool chunked = strcasestr_manual(rawbuf, "Transfer-Encoding: chunked") != NULL;
        rawbuf[headerLen] = savedByte;

        char* bodyStart = headerEnd + 4;
        size_t bodyLenRecv = total - (bodyStart - rawbuf);

        size_t decodedLen;
        if (chunked) {
            decodedLen = dechunk(bodyStart, bodyLenRecv, out, outsize);
        } else {
            decodedLen = bodyLenRecv < outsize - 1 ? bodyLenRecv : outsize - 1;
            memcpy(out, bodyStart, decodedLen);
            out[decodedLen] = '\0';
        }
        if (outLen)
            *outLen = decodedLen;
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
