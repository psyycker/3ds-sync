#include "json_lite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* find_key(const char* json, const char* key)
{
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    const char* pos = strstr(json, needle);
    if (!pos)
        return NULL;

    pos += strlen(needle);
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r')
        pos++;
    if (*pos != ':')
        return NULL;
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r')
        pos++;
    return pos;
}

bool json_get_string(const char* json, const char* key, char* out, size_t outsize)
{
    const char* pos = find_key(json, key);
    if (!pos || *pos != '"')
        return false;
    pos++;

    size_t i = 0;
    while (*pos && *pos != '"' && i < outsize - 1) {
        if (*pos == '\\' && *(pos + 1) != '\0') {
            pos++; // skip escape char, copy the escaped char verbatim (fine for our fields)
        }
        out[i++] = *pos++;
    }
    out[i] = '\0';
    return true;
}

bool json_get_int(const char* json, const char* key, int* out)
{
    const char* pos = find_key(json, key);
    if (!pos)
        return false;
    *out = (int) strtol(pos, NULL, 10);
    return true;
}

bool json_get_array_object(const char* json, const char* arrayKey, int index, char* out, size_t outsize)
{
    const char* pos = find_key(json, arrayKey);
    if (!pos || *pos != '[')
        return false;
    pos++;

    for (int i = 0; i <= index; i++) {
        while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r' || *pos == ',')
            pos++;
        if (*pos != '{')
            return false; // array ended (']') or malformed before reaching index

        const char* objStart = pos;
        int depth = 0;
        bool inString = false;
        for (; *pos; pos++) {
            if (inString) {
                if (*pos == '\\' && *(pos + 1) != '\0') {
                    pos++;
                    continue;
                }
                if (*pos == '"')
                    inString = false;
                continue;
            }
            if (*pos == '"') {
                inString = true;
            } else if (*pos == '{') {
                depth++;
            } else if (*pos == '}') {
                depth--;
                if (depth == 0) {
                    pos++;
                    break;
                }
            }
        }
        if (depth != 0)
            return false; // unterminated object

        if (i == index) {
            size_t objLen = (size_t) (pos - objStart);
            if (objLen >= outsize)
                objLen = outsize - 1;
            memcpy(out, objStart, objLen);
            out[objLen] = '\0';
            return true;
        }
    }
    return false;
}
