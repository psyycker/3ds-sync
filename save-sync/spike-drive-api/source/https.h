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
