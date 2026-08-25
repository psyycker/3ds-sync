#include "DriveApi.hpp"

#include "httpcall.hpp"
#include "json.hpp"

namespace {
    constexpr const char* DRIVE_HOST   = "www.googleapis.com";
    constexpr const char* CA_CERT_PATH = "romfs:/certs/gts_root_r1.pem";
    constexpr const char* FOLDER_MIME  = "application/vnd.google-apps.folder";

    std::string authHeader(const std::string& accessToken)
    {
        return "Authorization: Bearer " + accessToken + "\r\n";
    }

    Http::Response get(const std::string& accessToken, const std::string& url, const std::string& extraHeaders = "")
    {
        Http::Request req;
        req.method     = "GET";
        req.url        = url.c_str();
        std::string hdr = authHeader(accessToken) + extraHeaders;
        req.headers    = hdr.c_str();
        req.verifyPeer = true;
        req.caInfoPath = CA_CERT_PATH;
        return Http::perform(req);
    }
}

namespace DriveApi {
    bool listChildren(const std::string& accessToken, const std::string& folderId, std::vector<Entry>& out)
    {
        // A service account owns nothing of its own: everything it can reach
        // was explicitly shared with it, which Drive surfaces as top-level
        // "Shared with me" items, not as children of its own (empty) "root".
        // Once inside one of those shared folders, normal parent-based
        // traversal works fine for descending further.
        std::string q = folderId == "root" ? "sharedWithMe = true and trashed = false"
                                            : "'" + folderId + "' in parents and trashed = false";
        std::string url = std::string("https://") + DRIVE_HOST + "/drive/v3/files?q=" + Http::encode(q.c_str())
            + "&fields=" + Http::encode("files(id,name,mimeType,modifiedTime,md5Checksum)") + "&orderBy=folder,name&pageSize=200";

        Http::Response res = get(accessToken, url);
        if (res.result != Http::Result::Ok || res.httpStatus != 200) {
            return false;
        }

        auto j = nlohmann::json::parse(res.body, nullptr, false);
        if (j.is_discarded() || !j.contains("files") || !j["files"].is_array()) {
            return false;
        }

        out.clear();
        for (auto& f : j["files"]) {
            Entry e;
            e.id       = f.value("id", "");
            e.name     = f.value("name", "");
            std::string mime = f.value("mimeType", "");
            e.isFolder = (mime == FOLDER_MIME);
            if (!e.isFolder) {
                e.modifiedTime = f.value("modifiedTime", "");
                e.md5          = f.value("md5Checksum", "");
            }
            out.push_back(std::move(e));
        }
        return true;
    }

    std::string createFolder(const std::string& accessToken, const std::string& parentId, const std::string& name)
    {
        nlohmann::json body;
        body["name"]     = name;
        body["mimeType"] = FOLDER_MIME;
        body["parents"]  = { parentId };
        std::string bodyStr = body.dump();

        Http::Request req;
        req.method   = "POST";
        req.url      = "https://www.googleapis.com/drive/v3/files";
        std::string hdr = authHeader(accessToken) + "Content-Type: application/json\r\n";
        req.headers  = hdr.c_str();
        req.body     = bodyStr.c_str();
        req.bodySize = (long)bodyStr.size();
        req.verifyPeer = true;
        req.caInfoPath = CA_CERT_PATH;

        Http::Response res = Http::perform(req);
        if (res.result != Http::Result::Ok || res.httpStatus != 200) {
            return "";
        }
        auto j = nlohmann::json::parse(res.body, nullptr, false);
        if (j.is_discarded() || !j.contains("id")) {
            return "";
        }
        return j["id"].get<std::string>();
    }

    bool downloadFile(const std::string& accessToken, const std::string& fileId, std::string& out)
    {
        std::string url = "https://www.googleapis.com/drive/v3/files/" + fileId + "?alt=media";
        Http::Response res = get(accessToken, url);
        if (res.result != Http::Result::Ok || res.httpStatus != 200) {
            return false;
        }
        out = std::move(res.body);
        return true;
    }

    std::string createFile(
        const std::string& accessToken, const std::string& parentId, const std::string& name, const std::string& content)
    {
        static constexpr const char* BOUNDARY = "3ds_checkpoint_drivesync_boundary";

        nlohmann::json meta;
        meta["name"]    = name;
        meta["parents"] = { parentId };

        std::string body = std::string("--") + BOUNDARY + "\r\n" + "Content-Type: application/json; charset=UTF-8\r\n\r\n"
            + meta.dump() + "\r\n" + "--" + BOUNDARY + "\r\n" + "Content-Type: application/octet-stream\r\n\r\n" + content
            + "\r\n--" + BOUNDARY + "--\r\n";

        Http::Request req;
        req.method   = "POST";
        req.url      = "https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart";
        std::string hdr = authHeader(accessToken) + "Content-Type: multipart/related; boundary=" + BOUNDARY + "\r\n";
        req.headers  = hdr.c_str();
        req.body     = body.c_str();
        req.bodySize = (long)body.size();
        req.verifyPeer = true;
        req.caInfoPath = CA_CERT_PATH;

        Http::Response res = Http::perform(req);
        if (res.result != Http::Result::Ok || res.httpStatus != 200) {
            return "";
        }
        auto j = nlohmann::json::parse(res.body, nullptr, false);
        if (j.is_discarded() || !j.contains("id")) {
            return "";
        }
        return j["id"].get<std::string>();
    }

    bool getFileMd5(const std::string& accessToken, const std::string& fileId, std::string& outMd5)
    {
        std::string url = "https://www.googleapis.com/drive/v3/files/" + fileId + "?fields=" + Http::encode("md5Checksum");
        Http::Response res = get(accessToken, url);
        if (res.result != Http::Result::Ok || res.httpStatus != 200) {
            return false;
        }
        auto j = nlohmann::json::parse(res.body, nullptr, false);
        if (j.is_discarded() || !j.contains("md5Checksum")) {
            return false;
        }
        outMd5 = j["md5Checksum"].get<std::string>();
        return true;
    }

    bool updateFileContent(const std::string& accessToken, const std::string& fileId, const std::string& content)
    {
        std::string url = "https://www.googleapis.com/upload/drive/v3/files/" + fileId + "?uploadType=media";

        Http::Request req;
        req.method   = "PATCH";
        req.url      = url.c_str();
        std::string hdr = authHeader(accessToken) + "Content-Type: application/octet-stream\r\n";
        req.headers  = hdr.c_str();
        req.body     = content.c_str();
        req.bodySize = (long)content.size();
        req.verifyPeer = true;
        req.caInfoPath = CA_CERT_PATH;

        Http::Response res = Http::perform(req);
        return res.result == Http::Result::Ok && res.httpStatus == 200;
    }
}
