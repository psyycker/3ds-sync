#pragma once
#include <string>

// Trimmed-down stand-in for checkpoint's common/script/httpcall.hpp: same
// Http::Request/Response/perform()/encode() surface that DriveApi.cpp and
// DriveAuth.cpp (ported unmodified from checkpoint) expect, minus the
// script-engine-specific bits they don't use here (upload streaming, the
// abort-on-script-cancel hook, the upload progress bar wired to
// ScriptConsole) - rom-sync has no script engine to hook into.
namespace Http {
    enum class Result {
        Ok,
        Unavailable,    // curl_global_init/curl_easy_init failed
        TransferFailed, // curl returned an error; see code
        OutOfMemory,    // the response did not fit in memory (CURLE_WRITE_ERROR)
    };

    struct Request {
        const char* method  = nullptr; // nullptr/"" leaves curl's default verb (GET)
        const char* url     = nullptr;
        const char* headers = nullptr; // "\r\n"-separated "Key: Value" lines
        const char* body    = nullptr;
        long bodySize       = 0;
        bool verifyPeer       = false;
        const char* caInfoPath = nullptr;
    };

    struct Response {
        Result result   = Result::Ok;
        int code        = 0;
        long httpStatus = 0;
        std::string body;
    };

    Response perform(const Request& req);
    std::string encode(const char* s);
}
