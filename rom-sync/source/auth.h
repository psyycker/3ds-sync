#pragma once
#include <stddef.h>
#include <stdbool.h>

// Gets a fresh Drive access token for the service account configured in
// DriveServiceAccount.h - the same one save-sync/checkpoint uses. Builds and
// RS256-signs a JWT with the service account's private key and exchanges it
// with Google for an access token (JWT-bearer grant). No interactive sign-in
// and nothing persisted to the SD card: this runs once per launch and the
// token is good for an hour.
//
// Note: a service account has no access to your Drive files by default -
// you must share the folders/files you want to sync with the service
// account's client email address (the same one save-sync uses) first.
// accessToken must be at least 2048 bytes.
bool auth_get_access_token(char* accessToken, size_t accessTokenSize);
