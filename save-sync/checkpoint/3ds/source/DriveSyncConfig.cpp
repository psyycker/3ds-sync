#include "DriveSyncConfig.hpp"

#include "json.hpp"

#include <cstdio>
#include <sys/stat.h>
#include <unordered_map>

namespace {
    constexpr const char* CONFIG_DIR  = "sdmc:/3ds/Checkpoint/drivesync";
    constexpr const char* CONFIG_PATH = "sdmc:/3ds/Checkpoint/drivesync/sync_config.json";

    const char* categoryKey(DriveSyncConfig::Category c)
    {
        switch (c) {
        case DriveSyncConfig::Category::GbGbc:
            return "gbgbc";
        case DriveSyncConfig::Category::Gba:
            return "gba";
        case DriveSyncConfig::Category::Nds:
            return "nds";
        case DriveSyncConfig::Category::ThreeDs:
            return "3ds";
        }
        return "";
    }

    bool categoryFromKey(const std::string& key, DriveSyncConfig::Category& out)
    {
        if (key == "gbgbc") {
            out = DriveSyncConfig::Category::GbGbc;
        }
        else if (key == "gba") {
            out = DriveSyncConfig::Category::Gba;
        }
        else if (key == "nds") {
            out = DriveSyncConfig::Category::Nds;
        }
        else if (key == "3ds") {
            out = DriveSyncConfig::Category::ThreeDs;
        }
        else {
            return false;
        }
        return true;
    }

    std::unordered_map<std::string, DriveSyncConfig::CategoryFolder> gCategoryFolders; // categoryKey -> folder
    std::unordered_map<uint64_t, DriveSyncConfig::TitleTag> gTitleTags;
    std::unordered_map<std::string, std::string> gNdsSyncedMd5; // save filename -> last-synced MD5
    bool gLoaded = false;
}

namespace DriveSyncConfig {
    const char* categoryLabel(Category c)
    {
        switch (c) {
        case Category::GbGbc:
            return "GB / GBC";
        case Category::Gba:
            return "GBA";
        case Category::Nds:
            return "NDS";
        case Category::ThreeDs:
            return "3DS";
        }
        return "?";
    }

    const std::vector<Category>& allCategories()
    {
        static const std::vector<Category> all = { Category::ThreeDs, Category::Gba, Category::Nds, Category::GbGbc };
        return all;
    }

    void load()
    {
        gCategoryFolders.clear();
        gTitleTags.clear();
        gNdsSyncedMd5.clear();
        gLoaded = true;

        FILE* f = fopen(CONFIG_PATH, "rb");
        if (!f) {
            return;
        }
        std::string content;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            content.append(buf, n);
        }
        fclose(f);

        auto j = nlohmann::json::parse(content, nullptr, false);
        if (j.is_discarded()) {
            return;
        }

        if (j.contains("categoryFolders") && j["categoryFolders"].is_object()) {
            for (auto it = j["categoryFolders"].begin(); it != j["categoryFolders"].end(); ++it) {
                CategoryFolder cf;
                cf.id                      = it.value().value("id", "");
                cf.path                    = it.value().value("path", "");
                gCategoryFolders[it.key()] = cf;
            }
        }

        if (j.contains("titles") && j["titles"].is_object()) {
            for (auto it = j["titles"].begin(); it != j["titles"].end(); ++it) {
                uint64_t id = strtoull(it.key().c_str(), nullptr, 16);
                Category cat;
                if (!it.value().contains("category") || !categoryFromKey(it.value()["category"].get<std::string>(), cat)) {
                    continue;
                }
                TitleTag tag;
                tag.category      = cat;
                tag.driveFileId   = it.value().value("driveFileId", "");
                tag.driveFileName = it.value().value("driveFileName", "");
                tag.lastSyncedMd5     = it.value().value("lastSyncedMd5", "");
                tag.lastSyncedAt      = it.value().value("lastSyncedAt", (int64_t)0);
                tag.lastSyncDirection = it.value().value("lastSyncDirection", "");
                gTitleTags[id]    = tag;
            }
        }

        if (j.contains("ndsSyncedMd5") && j["ndsSyncedMd5"].is_object()) {
            for (auto it = j["ndsSyncedMd5"].begin(); it != j["ndsSyncedMd5"].end(); ++it) {
                gNdsSyncedMd5[it.key()] = it.value().get<std::string>();
            }
        }
    }

    void save()
    {
        mkdir("sdmc:/3ds", 0777);
        mkdir("sdmc:/3ds/Checkpoint", 0777);
        mkdir(CONFIG_DIR, 0777);

        nlohmann::json j;
        j["categoryFolders"] = nlohmann::json::object();
        for (auto& [key, folder] : gCategoryFolders) {
            nlohmann::json f;
            f["id"]                   = folder.id;
            f["path"]                 = folder.path;
            j["categoryFolders"][key] = f;
        }

        j["titles"] = nlohmann::json::object();
        for (auto& [id, tag] : gTitleTags) {
            char idHex[17];
            snprintf(idHex, sizeof(idHex), "%016llX", (unsigned long long)id);
            nlohmann::json t;
            t["category"]      = categoryKey(tag.category);
            t["driveFileId"]   = tag.driveFileId;
            t["driveFileName"] = tag.driveFileName;
            t["lastSyncedMd5"]     = tag.lastSyncedMd5;
            t["lastSyncedAt"]      = tag.lastSyncedAt;
            t["lastSyncDirection"] = tag.lastSyncDirection;
            j["titles"][idHex] = t;
        }

        j["ndsSyncedMd5"] = nlohmann::json::object();
        for (auto& [fileName, md5] : gNdsSyncedMd5) {
            j["ndsSyncedMd5"][fileName] = md5;
        }

        std::string content = j.dump(2);
        FILE* f              = fopen(CONFIG_PATH, "wb");
        if (!f) {
            return;
        }
        fwrite(content.data(), 1, content.size(), f);
        fclose(f);
    }

    CategoryFolder categoryFolder(Category c)
    {
        if (!gLoaded) {
            load();
        }
        auto it = gCategoryFolders.find(categoryKey(c));
        return it == gCategoryFolders.end() ? CategoryFolder{} : it->second;
    }

    void setCategoryFolder(Category c, const std::string& folderId, const std::string& path)
    {
        if (!gLoaded) {
            load();
        }
        gCategoryFolders[categoryKey(c)] = CategoryFolder{ folderId, path };
        save();
    }

    std::optional<TitleTag> titleTag(uint64_t titleId)
    {
        if (!gLoaded) {
            load();
        }
        auto it = gTitleTags.find(titleId);
        if (it == gTitleTags.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void setTitleTag(uint64_t titleId, const TitleTag& tag)
    {
        if (!gLoaded) {
            load();
        }
        gTitleTags[titleId] = tag;
        save();
    }

    void clearTitleTag(uint64_t titleId)
    {
        if (!gLoaded) {
            load();
        }
        gTitleTags.erase(titleId);
        save();
    }

    std::string ndsLastSyncedMd5(const std::string& fileName)
    {
        if (!gLoaded) {
            load();
        }
        auto it = gNdsSyncedMd5.find(fileName);
        return it == gNdsSyncedMd5.end() ? "" : it->second;
    }

    void setNdsLastSyncedMd5(const std::string& fileName, const std::string& md5)
    {
        if (!gLoaded) {
            load();
        }
        gNdsSyncedMd5[fileName] = md5;
        save();
    }
}
