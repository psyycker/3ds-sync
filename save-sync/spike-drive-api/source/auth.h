#pragma once
#include <stddef.h>
#include <stdbool.h>

// Ensures we have a valid access token, printing progress to the console.
// If a refresh token is already saved on the SD card, silently exchanges it
// for a fresh access token. Otherwise runs the full interactive OAuth
// device flow (prints a URL + code, polls until approved) and saves the
// resulting refresh token to the SD card for next time.
// accessToken must be at least 2048 bytes.
bool auth_get_access_token(char* accessToken, size_t accessTokenSize);
