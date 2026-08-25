#include <3ds.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include "auth.h"
#include "channels.h"
#include "drive.h"

static SyncChannel g_channels[MAX_CHANNELS];
static int g_channelCount = 0;
static char g_accessToken[2048];

static void redraw_frame(void)
{
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

static void wait_for_a_or_b(void)
{
    printf("\n(A/B to continue)\n");
    for (;;) {
        hidScanInput();
        u32 keys = hidKeysDown();
        if (keys & (KEY_A | KEY_B))
            return;
        redraw_frame();
    }
}

static bool prompt_text(const char* hint, char* out, size_t outsize)
{
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, -1);
    swkbdSetHintText(&swkbd, hint);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "OK", true);
    SwkbdButton button = swkbdInputText(&swkbd, out, outsize);
    return button == SWKBD_BUTTON_RIGHT;
}

static void progress_cb(const char* fileName, size_t received, size_t total, void* userdata)
{
    (void) userdata;
    static int callCount = 0;
    if (total > 0)
        printf("\r  %.40s: %u/%u KB   ", fileName, (unsigned) (received / 1024), (unsigned) (total / 1024));
    else
        printf("\r  %.40s: %u KB   ", fileName, (unsigned) (received / 1024));

    // Flushing/swapping every callback (once per ~4KB read) would cap
    // throughput at one vblank per chunk; only refresh the screen
    // periodically so the transfer isn't bottlenecked by the display.
    if (++callCount % 16 == 0)
        redraw_frame();
}

static void add_channel(void)
{
    if (g_channelCount >= MAX_CHANNELS) {
        printf("\nChannel list is full (%d max).\n", MAX_CHANNELS);
        wait_for_a_or_b();
        return;
    }

    SyncChannel ch = { 0 };
    printf("\nAdd sync channel\n");
    if (!prompt_text("Channel name", ch.name, sizeof(ch.name)))
        return;
    if (!prompt_text("Drive path (e.g. Games/3DS/roms)", ch.drivePath, sizeof(ch.drivePath)))
        return;
    if (!prompt_text("Local path (e.g. sdmc:/roms/3ds)", ch.localPath, sizeof(ch.localPath)))
        return;

    g_channels[g_channelCount++] = ch;
    channels_save(g_channels, g_channelCount);
    printf("\nSaved channel \"%s\".\n", ch.name);
    wait_for_a_or_b();
}

static void remove_channel(int index)
{
    if (index < 0 || index >= g_channelCount)
        return;
    printf("\nDelete channel \"%s\"? (A: yes, B: cancel)\n", g_channels[index].name);
    for (;;) {
        hidScanInput();
        u32 keys = hidKeysDown();
        if (keys & KEY_B)
            return;
        if (keys & KEY_A)
            break;
        redraw_frame();
    }
    for (int i = index; i < g_channelCount - 1; i++)
        g_channels[i] = g_channels[i + 1];
    g_channelCount--;
    channels_save(g_channels, g_channelCount);
    printf("Deleted.\n");
    wait_for_a_or_b();
}

static void sync_channel(int index)
{
    SyncChannel* ch = &g_channels[index];
    printf("\nSyncing \"%s\" (%s -> %s)...\n", ch->name, ch->drivePath, ch->localPath);
    bool ok = drive_sync_path(g_accessToken, ch->drivePath, ch->localPath, progress_cb, NULL);
    printf("\n%s: %s\n", ch->name, ok ? "done" : "FAILED (see log above)");
}

static void sync_all(void)
{
    if (g_channelCount == 0) {
        printf("\nNo channels configured yet.\n");
    } else {
        for (int i = 0; i < g_channelCount; i++)
            sync_channel(i);
    }
    wait_for_a_or_b();
}

int main(void)
{
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    printf("rom-sync - Google Drive sync channels\n\n");

    u32 socBufferSize = 0x100000;
    u32* socBuffer = (u32*) memalign(0x1000, socBufferSize);
    bool networkReady = false;
    if (socBuffer == NULL) {
        printf("Failed to allocate SOC buffer.\n");
    } else if (R_FAILED(socInit(socBuffer, socBufferSize))) {
        printf("socInit failed.\n");
    } else if (R_FAILED(sslcInit(0))) {
        printf("sslcInit failed.\n");
    } else {
        networkReady = true;
    }

    g_channelCount = channels_load(g_channels, MAX_CHANNELS);

    bool authed = false;
    if (networkReady) {
        printf("Signing in to Google Drive...\n");
        authed = auth_get_access_token(g_accessToken, sizeof(g_accessToken));
        if (!authed)
            printf("\nSign-in failed - syncing will be unavailable this session.\n");
        wait_for_a_or_b();
    }

    int selected = 0;
    bool running = true;
    while (running && aptMainLoop()) {
        int itemCount = g_channelCount + 3; // channels, then Add / Sync all / Exit
        int addIdx = g_channelCount;
        int syncAllIdx = g_channelCount + 1;
        int exitIdx = g_channelCount + 2;

        consoleClear();
        printf("rom-sync\n");
        printf(authed ? "signed in\n\n" : "NOT signed in - syncing disabled\n\n");

        if (g_channelCount == 0)
            printf("  (no sync channels yet)\n\n");

        for (int i = 0; i < g_channelCount; i++) {
            printf("%s %s\n", i == selected ? ">" : " ", g_channels[i].name);
            printf("    %s -> %s\n", g_channels[i].drivePath, g_channels[i].localPath);
        }
        printf("\n%s Add new channel\n", selected == addIdx ? ">" : " ");
        printf("%s Sync all channels now\n", selected == syncAllIdx ? ">" : " ");
        printf("%s Exit\n", selected == exitIdx ? ">" : " ");
        printf("\nUP/DOWN: move  A: select  X: delete channel  START: exit\n");

        redraw_frame();

        bool actioned = false;
        while (!actioned && aptMainLoop()) {
            hidScanInput();
            u32 keys = hidKeysDown();

            if (keys & KEY_START) {
                running = false;
                actioned = true;
            } else if (keys & KEY_DOWN) {
                selected = (selected + 1) % itemCount;
                actioned = true;
            } else if (keys & KEY_UP) {
                selected = (selected - 1 + itemCount) % itemCount;
                actioned = true;
            } else if (keys & KEY_A) {
                if (selected < g_channelCount) {
                    if (authed)
                        sync_channel(selected);
                    else
                        printf("\nNot signed in.\n");
                    wait_for_a_or_b();
                } else if (selected == addIdx) {
                    add_channel();
                } else if (selected == syncAllIdx) {
                    if (authed)
                        sync_all();
                    else {
                        printf("\nNot signed in.\n");
                        wait_for_a_or_b();
                    }
                } else if (selected == exitIdx) {
                    running = false;
                }
                actioned = true;
            } else if ((keys & KEY_X) && selected < g_channelCount) {
                remove_channel(selected);
                if (selected >= g_channelCount && g_channelCount > 0)
                    selected = g_channelCount - 1;
                actioned = true;
            }

            redraw_frame();
        }
    }

    if (networkReady) {
        sslcExit();
        socExit();
    }

    gfxExit();
    return 0;
}
