#include "httpcall.hpp"

#include <curl/curl.h>

namespace {
    bool curlReady(void)
    {
        static bool ready = false;
        if (!ready) {
            ready = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
        }
        return ready;
    }

    size_t writeToString(char* ptr, size_t size, size_t nmemb, void* userdata)
    {
        const size_t n = size * nmemb;
        try {
            ((std::string*)userdata)->append(ptr, n);
        }
        catch (...) {
            return 0;
        }
        return n;
    }

    // "\n"-separated "Key: Value" lines (a trailing '\r' per line, from
    // callers that wrote "\r\n", is trimmed) into a curl slist.
    struct curl_slist* headerSlist(const char* headers)
    {
        struct curl_slist* hl = nullptr;
        if (headers && headers[0]) {
            std::string all(headers);
            size_t start = 0;
            while (start < all.size()) {
                const size_t nl  = all.find('\n', start);
                std::string line = all.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (!line.empty()) {
                    hl = curl_slist_append(hl, line.c_str());
                }
                if (nl == std::string::npos) {
                    break;
                }
                start = nl + 1;
            }
        }
        return hl;
    }
}

namespace Http {
    Response perform(const Request& req)
    {
        Response res;

        CURL* curl = curlReady() ? curl_easy_init() : nullptr;
        if (!curl) {
            res.result = Result::Unavailable;
            return res;
        }

        struct curl_slist* hl = headerSlist(req.headers);

        curl_easy_setopt(curl, CURLOPT_URL, req.url ? req.url : "");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);
        if (req.verifyPeer && req.caInfoPath) {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
            curl_easy_setopt(curl, CURLOPT_CAINFO, req.caInfoPath);
        }
        else {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        }
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "rom-sync-curl");
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 300L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 10L);

        if (req.method && req.method[0]) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req.method);
        }
        if (hl) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hl);
        }
        if (req.body && req.bodySize > 0) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, req.bodySize);
        }

        const CURLcode code = curl_easy_perform(curl);
        if (code == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.httpStatus);
        }
        if (hl) {
            curl_slist_free_all(hl);
        }
        curl_easy_cleanup(curl);

        if (code != CURLE_OK) {
            res.code   = (int)code;
            res.result = code == CURLE_WRITE_ERROR ? Result::OutOfMemory : Result::TransferFailed;
            res.body.clear();
        }
        return res;
    }

    std::string encode(const char* s)
    {
        CURL* curl = curlReady() ? curl_easy_init() : nullptr;
        if (!curl) {
            return std::string();
        }
        char* enc = curl_easy_escape(curl, s ? s : "", 0);
        std::string out;
        if (enc) {
            try {
                out.assign(enc);
            }
            catch (...) {
                out.clear();
            }
            curl_free(enc);
        }
        curl_easy_cleanup(curl);
        return out;
    }
}
