#pragma once
#include <stddef.h>
#include <stdbool.h>

// Minimal, non-general-purpose JSON field extraction for the flat,
// single-level JSON objects Google's OAuth endpoints return. Not a real
// JSON parser - good enough for this spike, not for arbitrary payloads.
bool json_get_string(const char* json, const char* key, char* out, size_t outsize);
bool json_get_int(const char* json, const char* key, int* out);

// Finds the arrayKey's value (must be a JSON array of objects, e.g. Drive's
// "files": [{...}, {...}]) and copies the index-th {...} object (brace-
// balanced, so nested objects inside it are handled) into out. Returns false
// if the array or that index doesn't exist. Use json_get_string/int on the
// resulting object text to read its fields.
bool json_get_array_object(const char* json, const char* arrayKey, int index, char* out, size_t outsize);
