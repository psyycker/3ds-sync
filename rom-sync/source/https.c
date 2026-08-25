#include "https.h"

#include <3ds.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "gts_root_r1_der.h"

#define RAW_BUF_SIZE 32768
#define READ_CHUNK_SIZE 4096
#define HEADER_BUF_SIZE 4096

typedef struct {
    int sockfd;
    sslcContext ctx;
    u32 rootCertChain;
} HttpsConn;

static char* strcasestr_manual(const char* haystack, const char* needle)
{
    size_t needleLen = strlen(needle);
    for (const char* p = haystack; *p; p++) {
        if (strncasecmp(p, needle, needleLen) == 0)
            return (char*) p;
    }
    return NULL;
}

// Connects, performs the TLS handshake and sends the request line + headers
// + body. On success conn is fully populated and the caller must eventually
// call https_close(). On failure, any partially-opened resources are already
// torn down.
static bool https_open(const char* method, const char* host, const char* path, const char* extraHeaders,
    const char* body, size_t bodyLen, HttpsConn* conn)
{
    Result ret;
    struct addrinfo hints, *resaddr = NULL, *cur;

    conn->sockfd = -1;
    conn->rootCertChain = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, "443", &hints, &resaddr) != 0) {
        printf("getaddrinfo(%s) failed\n", host);
        return false;
    }

    conn->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn->sockfd == -1) {
        freeaddrinfo(resaddr);
        return false;
    }

    for (cur = resaddr; cur != NULL; cur = cur->ai_next) {
        if (connect(conn->sockfd, cur->ai_addr, cur->ai_addrlen) == 0)
            break;
    }
    freeaddrinfo(resaddr);

    if (cur == NULL) {
        printf("connect(%s) failed\n", host);
        closesocket(conn->sockfd);
        conn->sockfd = -1;
        return false;
    }

    ret = sslcCreateRootCertChain(&conn->rootCertChain);
    if (R_FAILED(ret)) {
        printf("sslcCreateRootCertChain: 0x%08lx\n", ret);
        goto fail_sock;
    }

    ret = sslcAddTrustedRootCA(conn->rootCertChain, (u8*) gts_root_r1_der, gts_root_r1_der_size, NULL);
    if (R_FAILED(ret)) {
        printf("sslcAddTrustedRootCA: 0x%08lx\n", ret);
        goto fail_chain;
    }

    ret = sslcCreateContext(&conn->ctx, conn->sockfd, SSLCOPT_Default, host);
    if (R_FAILED(ret)) {
        printf("sslcCreateContext: 0x%08lx\n", ret);
        goto fail_chain;
    }

    ret = sslcContextSetRootCertChain(&conn->ctx, conn->rootCertChain);
    if (R_FAILED(ret)) {
        printf("sslcContextSetRootCertChain: 0x%08lx\n", ret);
        goto fail_ctx;
    }

    ret = sslcStartConnection(&conn->ctx, NULL, NULL);
    if (R_FAILED(ret)) {
        printf("sslcStartConnection: 0x%08lx\n", ret);
        goto fail_ctx;
    }

    {
        char reqHeader[1536];
        int headerLen = snprintf(reqHeader, sizeof(reqHeader),
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: 3ds-rom-sync\r\n"
            "%s"
            "Content-Length: %d\r\n"
            "Connection: close\r\n\r\n",
            method, path, host, extraHeaders ? extraHeaders : "", (int) bodyLen);

        if (headerLen <= 0 || headerLen >= (int) sizeof(reqHeader)) {
            printf("request header too large\n");
            goto fail_ctx;
        }

        ret = sslcWrite(&conn->ctx, (u8*) reqHeader, headerLen);
        if (R_FAILED(ret)) {
            printf("sslcWrite(header): 0x%08lx\n", ret);
            goto fail_ctx;
        }

        if (bodyLen > 0) {
            ret = sslcWrite(&conn->ctx, (u8*) body, bodyLen);
            if (R_FAILED(ret)) {
                printf("sslcWrite(body): 0x%08lx\n", ret);
                goto fail_ctx;
            }
        }
    }

    return true;

fail_ctx:
    sslcDestroyContext(&conn->ctx);
fail_chain:
    sslcDestroyRootCertChain(conn->rootCertChain);
fail_sock:
    closesocket(conn->sockfd);
    conn->sockfd = -1;
    return false;
}

static void https_close(HttpsConn* conn)
{
    if (conn->sockfd == -1)
        return;
    sslcDestroyContext(&conn->ctx);
    sslcDestroyRootCertChain(conn->rootCertChain);
    closesocket(conn->sockfd);
    conn->sockfd = -1;
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
    HttpsConn conn;
    char* rawbuf = NULL;

    out[0] = '\0';
    if (outLen)
        *outLen = 0;

    if (!https_open(method, host, path, extraHeaders, body, bodyLen, &conn))
        return false;

    rawbuf = malloc(RAW_BUF_SIZE);
    if (!rawbuf) {
        printf("malloc failed\n");
        goto cleanup;
    }

    {
        size_t total = 0;
        for (;;) {
            if (total >= RAW_BUF_SIZE - 1)
                break;
            int received = sslcRead(&conn.ctx, rawbuf + total, RAW_BUF_SIZE - 1 - total, false);
            if (received <= 0)
                break;
            total += (size_t) received;
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
cleanup:
    https_close(&conn);
    return success;
}

typedef enum {
    BODY_PLAIN,
    BODY_CHUNKED,
} BodyMode;

typedef enum {
    CHUNK_SIZE_LINE,
    CHUNK_DATA,
    CHUNK_TRAILING_CRLF,
    CHUNK_DONE,
} ChunkState;

typedef struct {
    BodyMode mode;
    FILE* out;
    size_t written;
    size_t contentLength; // 0 = unknown (BODY_PLAIN only)
    https_progress_cb onProgress;
    void* userdata;
    bool error;

    // chunked-decoder state, persists across socket reads
    ChunkState chunkState;
    char lineBuf[32];
    size_t lineBufLen;
    size_t chunkRemaining;
} BodyDecoder;

static void body_decoder_write(BodyDecoder* d, const char* data, size_t len)
{
    if (len == 0 || d->error)
        return;
    if (fwrite(data, 1, len, d->out) != len) {
        d->error = true;
        return;
    }
    d->written += len;
    if (d->onProgress)
        d->onProgress(d->written, d->contentLength, d->userdata);
}

// Feeds a raw chunk of socket data (already TLS-decrypted) through the
// decoder. Safe to call repeatedly with arbitrarily-sized fragments - all
// chunked-encoding parsing state that might otherwise need to span multiple
// socket reads is kept in *d.
static void body_decoder_feed(BodyDecoder* d, const char* data, size_t len)
{
    if (d->error)
        return;

    if (d->mode == BODY_PLAIN) {
        body_decoder_write(d, data, len);
        return;
    }

    size_t pos = 0;
    while (pos < len && !d->error && d->chunkState != CHUNK_DONE) {
        switch (d->chunkState) {
        case CHUNK_SIZE_LINE: {
            while (pos < len && d->lineBufLen < sizeof(d->lineBuf) - 1) {
                char c = data[pos++];
                if (c == '\n') {
                    d->lineBuf[d->lineBufLen] = '\0';
                    long size = strtol(d->lineBuf, NULL, 16);
                    d->lineBufLen = 0;
                    if (size <= 0) {
                        d->chunkState = CHUNK_DONE;
                    } else {
                        d->chunkRemaining = (size_t) size;
                        d->chunkState = CHUNK_DATA;
                    }
                    break;
                }
                if (c != '\r')
                    d->lineBuf[d->lineBufLen++] = c;
            }
            if (d->lineBufLen >= sizeof(d->lineBuf) - 1) {
                d->error = true; // chunk-size line implausibly long/malformed
            }
            break;
        }
        case CHUNK_DATA: {
            size_t avail = len - pos;
            size_t take = avail < d->chunkRemaining ? avail : d->chunkRemaining;
            body_decoder_write(d, data + pos, take);
            pos += take;
            d->chunkRemaining -= take;
            if (d->chunkRemaining == 0) {
                d->chunkState = CHUNK_TRAILING_CRLF;
                d->lineBufLen = 0;
            }
            break;
        }
        case CHUNK_TRAILING_CRLF: {
            // consume the \r\n after chunk data before the next size line
            while (pos < len) {
                char c = data[pos++];
                if (c == '\n') {
                    d->chunkState = CHUNK_SIZE_LINE;
                    break;
                }
            }
            break;
        }
        case CHUNK_DONE:
            break;
        }
    }
}

bool https_download_to_file(const char* method, const char* host, const char* path, const char* extraHeaders,
    const char* body, size_t bodyLen, const char* outFilePath, https_progress_cb onProgress, void* userdata)
{
    HttpsConn conn;
    if (!https_open(method, host, path, extraHeaders, body, bodyLen, &conn))
        return false;

    FILE* f = fopen(outFilePath, "wb");
    if (!f) {
        printf("failed to open %s for writing\n", outFilePath);
        https_close(&conn);
        return false;
    }

    char headerBuf[HEADER_BUF_SIZE];
    size_t headerBufLen = 0;
    bool headerDone = false;
    char readBuf[READ_CHUNK_SIZE];
    BodyDecoder decoder;
    memset(&decoder, 0, sizeof(decoder));
    decoder.out = f;
    decoder.onProgress = onProgress;
    decoder.userdata = userdata;
    decoder.chunkState = CHUNK_SIZE_LINE;

    bool httpOk = false;
    for (;;) {
        int received = sslcRead(&conn.ctx, readBuf, sizeof(readBuf), false);
        if (received <= 0)
            break;

        const char* dataStart = readBuf;
        size_t dataLen = (size_t) received;

        if (!headerDone) {
            size_t toCopy = dataLen;
            if (headerBufLen + toCopy > sizeof(headerBuf) - 1)
                toCopy = sizeof(headerBuf) - 1 - headerBufLen;
            memcpy(headerBuf + headerBufLen, dataStart, toCopy);
            headerBufLen += toCopy;
            headerBuf[headerBufLen] = '\0';

            char* headerEnd = strstr(headerBuf, "\r\n\r\n");
            if (!headerEnd) {
                if (headerBufLen >= sizeof(headerBuf) - 1) {
                    printf("response headers too large\n");
                    break;
                }
                continue; // need more data before we can even see the headers
            }

            size_t headerLen = (size_t) (headerEnd - headerBuf);
            bool chunked = strcasestr_manual(headerBuf, "Transfer-Encoding: chunked") != NULL;
            char* clField = strcasestr_manual(headerBuf, "Content-Length:");
            size_t contentLength = 0;
            if (clField && (size_t) (clField - headerBuf) < headerLen)
                contentLength = (size_t) strtoul(clField + strlen("Content-Length:"), NULL, 10);

            decoder.mode = chunked ? BODY_CHUNKED : BODY_PLAIN;
            decoder.contentLength = contentLength;

            char statusLine[64];
            size_t statusLen = strcspn(headerBuf, "\r\n");
            if (statusLen >= sizeof(statusLine))
                statusLen = sizeof(statusLine) - 1;
            memcpy(statusLine, headerBuf, statusLen);
            statusLine[statusLen] = '\0';
            httpOk = strstr(statusLine, " 200 ") != NULL || strstr(statusLine, " 206 ") != NULL;
            if (!httpOk)
                printf("unexpected response: %s\n", statusLine);

            headerDone = true;

            // Any body bytes that arrived in the same read as the tail of
            // the headers still need to be processed.
            size_t consumedByHeader = headerLen + 4;
            size_t leftoverInHeaderBuf = headerBufLen - consumedByHeader;
            if (leftoverInHeaderBuf > 0)
                body_decoder_feed(&decoder, headerBuf + consumedByHeader, leftoverInHeaderBuf);

            // The rest of this same socket read that didn't fit in headerBuf
            // (only possible if headerBuf filled up before hitting \r\n\r\n,
            // which can't happen once headerDone is true) is not applicable
            // here since toCopy == dataLen whenever headerDone flips true in
            // this iteration.
            continue;
        }

        body_decoder_feed(&decoder, dataStart, dataLen);
        if (decoder.error)
            break;
    }

    fclose(f);
    https_close(&conn);

    bool bodyComplete = decoder.mode == BODY_CHUNKED ? decoder.chunkState == CHUNK_DONE
                                                      : (decoder.contentLength == 0 || decoder.written >= decoder.contentLength);

    if (!httpOk || decoder.error || !bodyComplete) {
        remove(outFilePath);
        return false;
    }
    return true;
}
