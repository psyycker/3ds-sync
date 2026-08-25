#include "DriveAuth.hpp"

#include "DriveServiceAccount.hpp"
#include "httpcall.hpp"
#include "json.hpp"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

#include <cstring>
#include <ctime>

namespace {
    constexpr const char* CA_CERT_PATH = "romfs:/certs/gts_root_r1.pem";
    constexpr const char* DRIVE_SCOPE  = "https://www.googleapis.com/auth/drive";
    constexpr const char* JWT_GRANT_TYPE = "urn:ietf:params:oauth:grant-type:jwt-bearer";

    // Standard base64url (RFC 4648 sec. 5): '+'/'/' -> '-'/'_', no '=' padding.
    // JWTs are defined over this alphabet, not plain base64.
    std::string base64url(const unsigned char* data, size_t len)
    {
        static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        std::string out;
        out.reserve((len + 2) / 3 * 4);

        size_t i = 0;
        while (i + 3 <= len) {
            uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
            out += alphabet[(n >> 18) & 0x3F];
            out += alphabet[(n >> 12) & 0x3F];
            out += alphabet[(n >> 6) & 0x3F];
            out += alphabet[n & 0x3F];
            i += 3;
        }
        size_t rem = len - i;
        if (rem == 1) {
            uint32_t n = data[i] << 16;
            out += alphabet[(n >> 18) & 0x3F];
            out += alphabet[(n >> 12) & 0x3F];
        }
        else if (rem == 2) {
            uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
            out += alphabet[(n >> 18) & 0x3F];
            out += alphabet[(n >> 12) & 0x3F];
            out += alphabet[(n >> 6) & 0x3F];
        }
        return out;
    }

    std::string base64url(const std::string& s)
    {
        return base64url(reinterpret_cast<const unsigned char*>(s.data()), s.size());
    }

    // Builds and RS256-signs the JWT assertion Google's token endpoint wants
    // for the service-account (server-to-server) flow. Empty return = failure
    // (outError filled).
    std::string buildSignedJwt(std::string& outError)
    {
        time_t now = time(nullptr);

        nlohmann::json header;
        header["alg"] = "RS256";
        header["typ"] = "JWT";

        nlohmann::json claims;
        claims["iss"]   = DRIVE_SA_CLIENT_EMAIL;
        claims["scope"] = DRIVE_SCOPE;
        claims["aud"]   = DRIVE_SA_TOKEN_URI;
        claims["iat"]   = (long long)now;
        claims["exp"]   = (long long)now + 3600;

        std::string signingInput = base64url(header.dump()) + "." + base64url(claims.dump());

        unsigned char hash[32];
        mbedtls_sha256((const unsigned char*)signingInput.data(), signingInput.size(), hash, 0);

        mbedtls_pk_context pk;
        mbedtls_pk_init(&pk);
        int ret = mbedtls_pk_parse_key(&pk, (const unsigned char*)DRIVE_SA_PRIVATE_KEY_PEM, strlen(DRIVE_SA_PRIVATE_KEY_PEM) + 1, nullptr, 0);
        if (ret != 0) {
            mbedtls_pk_free(&pk);
            outError = "Could not parse the service account private key.";
            return "";
        }

        mbedtls_entropy_context entropy;
        mbedtls_ctr_drbg_context ctrDrbg;
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctrDrbg);
        static const char* PERS = "drive_jwt_sign";
        ret = mbedtls_ctr_drbg_seed(&ctrDrbg, mbedtls_entropy_func, &entropy, (const unsigned char*)PERS, strlen(PERS));
        if (ret != 0) {
            mbedtls_ctr_drbg_free(&ctrDrbg);
            mbedtls_entropy_free(&entropy);
            mbedtls_pk_free(&pk);
            outError = "Could not seed the RNG for signing.";
            return "";
        }

        unsigned char sig[MBEDTLS_PK_SIGNATURE_MAX_SIZE];
        size_t sigLen = 0;
        ret = mbedtls_pk_sign(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), sig, &sigLen, mbedtls_ctr_drbg_random, &ctrDrbg);

        mbedtls_ctr_drbg_free(&ctrDrbg);
        mbedtls_entropy_free(&entropy);
        mbedtls_pk_free(&pk);

        if (ret != 0) {
            outError = "Signing the JWT failed.";
            return "";
        }

        return signingInput + "." + base64url(sig, sigLen);
    }
}

namespace DriveAuth {
    bool getAccessToken(std::string& outAccessToken, std::string& outError)
    {
        std::string jwt = buildSignedJwt(outError);
        if (jwt.empty()) {
            return false;
        }

        std::string body = std::string("grant_type=") + JWT_GRANT_TYPE + "&assertion=" + jwt;

        Http::Request req;
        req.method     = "POST";
        req.url        = DRIVE_SA_TOKEN_URI;
        req.headers    = "Content-Type: application/x-www-form-urlencoded";
        req.body       = body.c_str();
        req.bodySize   = (long)body.size();
        req.verifyPeer = true;
        req.caInfoPath = CA_CERT_PATH;

        Http::Response res = Http::perform(req);
        if (res.result != Http::Result::Ok) {
            outError = "Could not reach Google (network/TLS error).";
            return false;
        }

        auto j = nlohmann::json::parse(res.body, nullptr, false);
        if (j.is_discarded() || !j.contains("access_token")) {
            outError = j.contains("error_description") ? j["error_description"].get<std::string>() : "Unexpected response from Google.";
            return false;
        }

        outAccessToken = j["access_token"].get<std::string>();
        return true;
    }
}
