#pragma once
#include <stddef.h>
#include <stdbool.h>

// Minimal, non-general-purpose JSON field extraction for the flat,
// single-level JSON objects Google's OAuth endpoints return. Not a real
// JSON parser - good enough for this spike, not for arbitrary payloads.
bool json_get_string(const char* json, const char* key, char* out, size_t outsize);
bool json_get_int(const char* json, const char* key, int* out);
