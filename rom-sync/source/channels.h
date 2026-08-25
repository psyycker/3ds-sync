#pragma once
#include <stddef.h>
#include <stdbool.h>

#define MAX_CHANNELS 32

typedef struct {
    char name[64];
    char drivePath[256]; // "/"-separated path within the user's Drive, e.g. "Games/3DS/roms"
    char localPath[256]; // sdmc:/... destination path
} SyncChannel;

// Loads saved channels from sdmc:/3ds/rom-sync/channels.cfg into out (up to
// maxCount entries). Returns the number loaded (0 if the file doesn't exist
// yet or is empty).
int channels_load(SyncChannel* out, int maxCount);

// Overwrites the config file with exactly this set of channels.
bool channels_save(const SyncChannel* channels, int count);
