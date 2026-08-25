#include "channels.hpp"

#include <cstdio>
#include <sys/stat.h>

namespace {
    constexpr const char* CONFIG_DIR  = "sdmc:/3ds/rom-sync";
    constexpr const char* CONFIG_PATH = "sdmc:/3ds/rom-sync/channels.cfg";

    // One channel per line: driveFileId|driveFileName|localFolder. Fields
    // can't contain '|' or '\n' - both are already impossible in a Drive
    // file id and in an SD folder path/Drive file name picked through the
    // browsers (neither the keyboard nor Drive names offer a way to type one).
    void writeField(FILE* f, const std::string& field)
    {
        std::fputs(field.c_str(), f);
        std::fputc('|', f);
    }

    bool readField(FILE* f, std::string& out)
    {
        out.clear();
        int c;
        bool sawAny = false;
        while ((c = std::fgetc(f)) != EOF) {
            sawAny = true;
            if (c == '|') {
                break;
            }
            if (c == '\n') {
                std::ungetc(c, f);
                break;
            }
            out.push_back((char)c);
        }
        return sawAny;
    }
}

std::vector<SyncChannel> Channels::load(void)
{
    std::vector<SyncChannel> channels;
    FILE* f = std::fopen(CONFIG_PATH, "r");
    if (!f) {
        return channels;
    }

    for (;;) {
        SyncChannel ch;
        if (!readField(f, ch.driveFileId)) {
            break;
        }
        if (!readField(f, ch.driveFileName)) {
            break;
        }
        if (!readField(f, ch.localFolder)) {
            break;
        }
        int c = std::fgetc(f);
        while (c == '\r') {
            c = std::fgetc(f);
        }
        channels.push_back(std::move(ch));
    }

    std::fclose(f);
    return channels;
}

void Channels::save(const std::vector<SyncChannel>& channels)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir(CONFIG_DIR, 0777);

    FILE* f = std::fopen(CONFIG_PATH, "w");
    if (!f) {
        return;
    }
    for (auto& ch : channels) {
        writeField(f, ch.driveFileId);
        writeField(f, ch.driveFileName);
        writeField(f, ch.localFolder);
        std::fputc('\n', f);
    }
    std::fclose(f);
}
