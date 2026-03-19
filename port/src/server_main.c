/**
 * server_main.c -- Dedicated server entry point.
 *
 * This is a separate executable (pd-server) that shares the game's networking
 * and multiplayer logic but has its own minimal startup:
 *   - No 3D rendering (no GBI translator, no OpenGL scene rendering)
 *   - No audio (no music, no SFX)
 *   - No game menus (no Agent Select, no CI Training)
 *   - Lightweight SDL2 window with ImGui for the server GUI
 *   - ENet networking + UPnP + lobby management
 *   - Console logging
 *
 * The server GUI shows: status panel, player list, log viewer, server controls.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "platform.h"
#include "system.h"
#include "config.h"
#include "fs.h"
#include "video.h"
#include "input.h"
#include "romdata.h"
#include "modmgr.h"
#include "net/net.h"
#include "net/netupnp.h"
#include "net/netlobby.h"

/* Game globals that the shared code references — stub them for the server */
s32 g_SkipIntro = 1;
s32 g_StageNum = 0x26; /* STAGE_CITRAINING */

/* Audio stubs — server has no audio */
void sndSetSfxVolume(s32 vol) { (void)vol; }
void optionsSetMusicVolume(s32 vol) { (void)vol; }

/* Forward declarations */
extern s32 g_NetDedicated;
extern s32 g_NetHostLatch;
extern void pdguiInit(void *windowHandle);
extern void pdguiNewFrame(void);
extern void pdguiRender(void);
extern void pdguiShutdown(void);

/* Game init functions we need */
extern void gameInit(void);
extern void bootCreateSched(void);
extern s32 osGetMemSize(void);
extern void *sysMemZeroAlloc(u32 size);

extern u32 g_OsMemSize;
extern u32 g_MempHeapSize;
extern void *g_MempHeap;
extern s32 g_FileAutoSelect;

int main(int argc, const char **argv)
{
    sysInitArgs(argc, argv);

    /* Force dedicated server mode */
    g_NetDedicated = 1;
    g_NetHostLatch = 1;
    g_FileAutoSelect = 0;

    /* Minimal init sequence — no audio, no crash handler complexity */
    conInit();
    sysInit();
    fsInit();
    configInit();

    /* Video init — creates a simple SDL2 window for the ImGui server GUI.
     * No 3D rendering pipeline, just a flat 2D surface for ImGui. */
    videoInit();
    pdguiInit(videoGetWindowHandle());
    inputInit();

    /* Load ROM data and mods (needed for map/stage info) */
    romdataInit();
    netInit();

    /* Game init — sets up multiplayer state, player configs, etc.
     * This pulls in a lot of game code but we need it for match management. */
    gameInit();
    modmgrInit();

    /* Memory setup */
    g_OsMemSize = osGetMemSize();
    g_MempHeapSize = g_OsMemSize;
    g_MempHeap = sysMemZeroAlloc(g_MempHeapSize);

    /* Load defaults — server doesn't use agent files */
    {
        extern struct gamefile g_GameFile;
        extern void gamefileLoadDefaults(struct gamefile *file);
        gamefileLoadDefaults(&g_GameFile);
    }

    /* Start the server immediately */
    {
        extern u32 g_NetServerPort;
        extern s32 g_NetMaxClients;
        s32 result = netStartServer((u16)g_NetServerPort, g_NetMaxClients);
        if (result != 0) {
            sysLogPrintf(LOG_ERROR, "SERVER: Failed to start server (error %d)", result);
            return 1;
        }
        sysLogPrintf(LOG_NOTE, "SERVER: Dedicated server started on port %u", g_NetServerPort);
    }

    /* Main loop — process network events, render server GUI */
    sysLogPrintf(LOG_NOTE, "SERVER: Entering main loop. Close window to stop.");

    bootCreateSched(); /* Starts the game scheduler which runs the main loop */

    /* Cleanup (reached when window closes) */
    netDisconnect();
    pdguiShutdown();

    return 0;
}
