#pragma once
#include <stddef.h>
#include <stdbool.h>

// Performs an HTTPS request (TLS verified against the bundled GTS Root R1
// CA) to host+path. extraHeaders is raw "Name: value\r\n"-per-line text
// appended to the request (may be NULL) - use it for Authorization,
// Content-Type, etc. body/bodyLen may be NULL/0 (e.g. for GET).
//
// On success, writes the *body* of the HTTP response (chunked
// transfer-encoding is decoded automatically) into out; outLen (if non-NULL)
// receives the exact decoded byte length (binary-safe). out is always
// nul-terminated within outsize as a convenience for text/JSON responses.
// Returns true iff the request completed end-to-end - a non-2xx HTTP status
// still returns true; check the response body yourself.
bool https_request(const char* method, const char* host, const char* path, const char* extraHeaders,
    const char* body, size_t bodyLen, char* out, size_t outsize, size_t* outLen);

// Optional progress callback for https_download_to_file: called after each
// chunk is written, with the running total and (if known from
// Content-Length) the expected total; totalBytes is 0 if unknown.
typedef void (*https_progress_cb)(size_t receivedBytes, size_t totalBytes, void* userdata);

// Like https_request, but streams the response body straight to a file on
// disk instead of buffering it in RAM - use this for anything that might be
// larger than a few tens of KB (ROMs, save archives, etc). GET-only in
// practice, but any method/body is technically supported.
bool https_download_to_file(const char* method, const char* host, const char* path, const char* extraHeaders,
    const char* body, size_t bodyLen, const char* outFilePath, https_progress_cb onProgress, void* userdata);
