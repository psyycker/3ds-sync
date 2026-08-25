#ifndef DRIVEAUTH_HPP
#define DRIVEAUTH_HPP

#include <string>

// Google service-account (JWT Bearer, RFC 7523) auth. Replaces an earlier
// OAuth-device-flow implementation: that flow could never request the scope
// this feature needs (Google blocks broad Drive scopes from the device flow
// entirely, and Testing-mode apps' user-delegated tokens expire every 7 days
// regardless of scope besides). A service account sidesteps both problems -
// no consent screen, no expiring refresh token, no PC-side re-auth ever.
// Access is instead granted per-folder, once, by sharing each Drive folder
// with the service account's own email address (see DriveServiceAccount.hpp)
// like sharing with any other person.
//
// This still makes one blocking HTTPS call (self-signs a short-lived JWT
// locally, then exchanges it for an access token) - callers on the render
// thread must run it from a worker thread, same threading note as before.
namespace DriveAuth {
    // Always does a fresh sign+exchange (no caching here - access tokens are
    // cheap to mint and expire in an hour; callers needing many API calls in
    // a row should get one token and reuse it themselves rather than calling
    // this per-request). Returns true and fills outAccessToken on success;
    // on failure outError has a short human-readable reason.
    bool getAccessToken(std::string& outAccessToken, std::string& outError);
}

#endif
