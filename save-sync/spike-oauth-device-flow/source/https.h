#pragma once
#include <stddef.h>
#include <stdbool.h>

// Performs a POST over HTTPS (TLS verified against the bundled GTS Root R1
// CA) to host+path, with Content-Type: application/x-www-form-urlencoded
// and the given body. On success, writes the *body* of the HTTP response
// (chunked transfer-encoding is decoded automatically) into out, always
// nul-terminated within outsize. Returns true iff the request completed
// end-to-end (a non-2xx HTTP status still returns true - check the JSON
// body's "error" field yourself; this only reports transport failure).
bool https_post_form(const char* host, const char* path, const char* body, char* out, size_t outsize);
