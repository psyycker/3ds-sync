#include "auth.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

#include "DriveServiceAccount.h"
#include "https.h"
#include "json_lite.h"

#define OAUTH_HOST "oauth2.googleapis.com"
#define OAUTH_TOKEN_PATH "/token"
// Read-only: rom-sync only ever pulls files down. The service account can
// only see files/folders explicitly shared with its client email, same as
// save-sync's Drive integration - this scope just bounds what it's allowed
// to *do* with whatever it can see.
#define DRIVE_SCOPE "https://www.googleapis.com/auth/drive.readonly"
#define JWT_GRANT_TYPE "urn:ietf:params:oauth:grant-type:jwt-bearer"

static char respbuf[4096];

// Standard base64url (RFC 4648 sec. 5): '+'/'/' -> '-'/'_', no '=' padding -
// the alphabet JWTs are defined over, not plain base64.
static size_t base64url_encode(const unsigned char* data, size_t len, char* out, size_t outsize)
{
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t o = 0, i = 0;

    while (i + 3 <= len && o + 4 < outsize) {
        uint32_t n = ((uint32_t) data[i] << 16) | ((uint32_t) data[i + 1] << 8) | data[i + 2];
        out[o++] = alphabet[(n >> 18) & 0x3F];
        out[o++] = alphabet[(n >> 12) & 0x3F];
        out[o++] = alphabet[(n >> 6) & 0x3F];
        out[o++] = alphabet[n & 0x3F];
        i += 3;
    }
    size_t rem = len - i;
    if (rem == 1 && o + 2 < outsize) {
        uint32_t n = (uint32_t) data[i] << 16;
        out[o++] = alphabet[(n >> 18) & 0x3F];
        out[o++] = alphabet[(n >> 12) & 0x3F];
    } else if (rem == 2 && o + 3 < outsize) {
        uint32_t n = ((uint32_t) data[i] << 16) | ((uint32_t) data[i + 1] << 8);
        out[o++] = alphabet[(n >> 18) & 0x3F];
        out[o++] = alphabet[(n >> 12) & 0x3F];
        out[o++] = alphabet[(n >> 6) & 0x3F];
    }
    out[o] = '\0';
    return o;
}

static size_t base64url_encode_str(const char* s, char* out, size_t outsize)
{
    return base64url_encode((const unsigned char*) s, strlen(s), out, outsize);
}

// Builds and RS256-signs the JWT assertion Google's token endpoint wants for
// the service-account (server-to-server) flow. Returns false on failure.
static bool build_signed_jwt(char* out, size_t outsize)
{
    time_t now = time(NULL);

    char header[64];
    snprintf(header, sizeof(header), "{\"alg\":\"RS256\",\"typ\":\"JWT\"}");

    char claims[512];
    snprintf(claims, sizeof(claims), "{\"iss\":\"%s\",\"scope\":\"%s\",\"aud\":\"%s\",\"iat\":%lld,\"exp\":%lld}",
        DRIVE_SA_CLIENT_EMAIL, DRIVE_SCOPE, DRIVE_SA_TOKEN_URI, (long long) now, (long long) now + 3600);

    char headerB64[128];
    char claimsB64[700];
    base64url_encode_str(header, headerB64, sizeof(headerB64));
    base64url_encode_str(claims, claimsB64, sizeof(claimsB64));

    char signingInput[900];
    int signingInputLen = snprintf(signingInput, sizeof(signingInput), "%s.%s", headerB64, claimsB64);
    if (signingInputLen <= 0 || signingInputLen >= (int) sizeof(signingInput))
        return false;

    unsigned char hash[32];
    mbedtls_sha256((const unsigned char*) signingInput, (size_t) signingInputLen, hash, 0);

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int ret = mbedtls_pk_parse_key(
        &pk, (const unsigned char*) DRIVE_SA_PRIVATE_KEY_PEM, strlen(DRIVE_SA_PRIVATE_KEY_PEM) + 1, NULL, 0);
    if (ret != 0) {
        printf("Could not parse the service account private key (0x%x).\n", -ret);
        mbedtls_pk_free(&pk);
        return false;
    }

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctrDrbg;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctrDrbg);
    static const char* PERS = "drive_jwt_sign";
    ret = mbedtls_ctr_drbg_seed(&ctrDrbg, mbedtls_entropy_func, &entropy, (const unsigned char*) PERS, strlen(PERS));
    if (ret != 0) {
        printf("Could not seed the RNG for signing.\n");
        mbedtls_ctr_drbg_free(&ctrDrbg);
        mbedtls_entropy_free(&entropy);
        mbedtls_pk_free(&pk);
        return false;
    }

    unsigned char sig[MBEDTLS_PK_SIGNATURE_MAX_SIZE];
    size_t sigLen = 0;
    ret = mbedtls_pk_sign(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), sig, &sigLen, mbedtls_ctr_drbg_random, &ctrDrbg);

    mbedtls_ctr_drbg_free(&ctrDrbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_pk_free(&pk);

    if (ret != 0) {
        printf("Signing the JWT failed (0x%x).\n", -ret);
        return false;
    }

    char sigB64[512];
    base64url_encode(sig, sigLen, sigB64, sizeof(sigB64));

    int written = snprintf(out, outsize, "%s.%s", signingInput, sigB64);
    return written > 0 && written < (int) outsize;
}

bool auth_get_access_token(char* accessToken, size_t accessTokenSize)
{
    char jwt[2048];
    if (!build_signed_jwt(jwt, sizeof(jwt)))
        return false;

    char body[2200];
    int bodyLen = snprintf(body, sizeof(body), "grant_type=%s&assertion=%s", JWT_GRANT_TYPE, jwt);
    if (bodyLen <= 0 || bodyLen >= (int) sizeof(body))
        return false;

    if (!https_request("POST", OAUTH_HOST, OAUTH_TOKEN_PATH, "Content-Type: application/x-www-form-urlencoded\r\n",
            body, (size_t) bodyLen, respbuf, sizeof(respbuf), NULL)) {
        printf("Could not reach Google (network/TLS error).\n");
        return false;
    }

    if (!json_get_string(respbuf, "access_token", accessToken, accessTokenSize)) {
        printf("Sign-in failed, raw response:\n%s\n", respbuf);
        return false;
    }
    return true;
}
