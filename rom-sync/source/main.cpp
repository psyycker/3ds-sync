#include "RomSyncScreen.hpp"
#include "colors.hpp"
#include "gui.hpp"
#include "localfs.hpp"
#include "logging.hpp"
#include "main.hpp"
#include "textpool.hpp"
#include "thread.hpp"
#include <3ds.h>
#include <malloc.h>

namespace {
    constexpr size_t SOC_ALIGN      = 0x1000;
    constexpr size_t SOC_BUFFERSIZE = 0x100000;
}

int main()
{
    hidInit();
    Threads::init(0, 2);
    gfxInitDefault();

    if (R_FAILED(Archive::init())) {
        Logging::error("Archive::init failed - local folder browsing will not work.");
    }
    romfsInit();

    Colors::apply("dark");
    Gui::init();

    u32* socketBuffer = (u32*)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if (socketBuffer) {
        if (socInit(socketBuffer, SOC_BUFFERSIZE) != 0) {
            Logging::warning("socInit failed; network features will not work.");
        }
    }
    else {
        Logging::warning("Failed to allocate the socket buffer; network features will not work.");
    }

    g_screen = std::make_shared<RomSyncScreen>();

    while (aptMainLoop()) {
        touchPosition touch;
        hidScanInput();
        hidTouchRead(&touch);

        if ((hidKeysDown() & KEY_START) && g_screen->allowsExit()) {
            break;
        }

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        g_screen->doDrawTop();
        C2D_SceneBegin(g_bottom);
        g_screen->doDrawBottom();
        Gui::frameEnd();
        TextPool::get().frameTick();
        g_screen->doUpdate(InputState{touch});

        if (g_pendingScreen) {
            g_screen        = std::move(g_pendingScreen);
            g_pendingScreen = nullptr;
        }
    }

    Threads::exit();
    Archive::exit();
    romfsExit();
    Gui::exit();
    gfxExit();
    hidExit();
    return 0;
}
