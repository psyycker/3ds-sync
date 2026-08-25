#include "channels.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define CONFIG_DIR "sdmc:/3ds/rom-sync"
#define CONFIG_PATH CONFIG_DIR "/channels.cfg"

// One channel per line: name|drivePath|localPath. Fields can't themselves
// contain '|' or a newline - good enough for a first version; the in-app
// text entry doesn't offer a way to type one anyway.
static void write_field(FILE* f, const char* field)
{
    fputs(field, f);
    fputc('|', f);
}

static bool read_field(FILE* f, char* out, size_t outsize)
{
    size_t i = 0;
    int c;
    bool sawAny = false;
    while ((c = fgetc(f)) != EOF) {
        sawAny = true;
        if (c == '|')
            break;
        if (c == '\n') {
            // malformed line (missing field) - stop here, caller decides
            ungetc(c, f);
            break;
        }
        if (i < outsize - 1)
            out[i++] = (char) c;
    }
    out[i] = '\0';
    return sawAny;
}

int channels_load(SyncChannel* out, int maxCount)
{
    FILE* f = fopen(CONFIG_PATH, "r");
    if (!f)
        return 0;

    int count = 0;
    while (count < maxCount) {
        SyncChannel* ch = &out[count];
        if (!read_field(f, ch->name, sizeof(ch->name)))
            break;
        if (!read_field(f, ch->drivePath, sizeof(ch->drivePath)))
            break;
        if (!read_field(f, ch->localPath, sizeof(ch->localPath)))
            break;

        int c = fgetc(f);
        while (c == '\r')
            c = fgetc(f);
        // expect '\n' or EOF here; either way the line is complete

        count++;
    }

    fclose(f);
    return count;
}

bool channels_save(const SyncChannel* channels, int count)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir(CONFIG_DIR, 0777);

    FILE* f = fopen(CONFIG_PATH, "w");
    if (!f)
        return false;

    for (int i = 0; i < count; i++) {
        write_field(f, channels[i].name);
        write_field(f, channels[i].drivePath);
        write_field(f, channels[i].localPath);
        fputc('\n', f);
    }

    fclose(f);
    return true;
}
