/**
 * server_main.c -- Dedicated server entry point.
 *
 * Separate executable (pd-server) with its own main loop.
 * No game state machine, no stage loading, no gameplay rendering.
 * Just: SDL window for ImGui GUI + ENet networking + tick loop.
 */

#include <stdlib.h>
#include <stdio.h>
#include <SDL.h>
#include <PR/ultratypes.h>
#include <PR/ultrasched.h>
#include <PR/os_message.h>

#include "lib/main.h"
#include "lib/mempc.h"
#include "bss.h"
#include "data.h"

#include "video.h"
#include "input.h"
#include "fs.h"
#include "romdata.h"
#include "config.h"
#include "modmgr.h"
#include "pdgui.h"
#include "system.h"
#include "console.h"
#include "net/net.h"
#include "net/netupnp.h"
#include "net/netlobby.h"

/* Globals that the shared game code references.
 * These are normally defined in main.c — the server needs its own copies. */
u32 g_OsMemSize = 0;
s32 g_OsMemSizeMb = 64;
s8 g_Resetting = false;
OSSched g_Sched;
OSMesgQueue g_MainMesgQueue;
OSMesg g_MainMesgBuf[32];
u8 *g_MempHeap = NULL;
u32 g_MempHeapSize = 0;
s32 g_SkipIntro = 1;
s32 g_FileAutoSelect = 0;
u8  g_VmShowStats = 0;
s32 g_TickRateDiv = 1;
s32 g_TickExtraSleep = 1;

/* Stubs needed by shared code */
void *bootAllocateStack(s32 threadid, s32 size)
{
    static u8 stackbuf[0x1000];
    return stackbuf;
}

/* Net globals are defined in net.c */
extern s32 g_NetDedicated;
extern s32 g_NetHostLatch;
extern u32 g_NetServerPort;
extern s32 g_NetMaxClients;

/* ImGui render functions */
extern void pdguiProcessEvent(void *sdlEvent);
extern void pdguiNewFrame(void);
extern void pdguiRender(void);

/* Video frame functions */
extern void videoStartFrame(void);
extern void videoEndFrame(void);

int main(int argc, const char **argv)
{
    sysInitArgs(argc, argv);

    /* Force dedicated server mode */
    g_NetDedicated = 1;
    g_NetHostLatch = 1;

    printf("PD2 Dedicated Server starting...\n");

    conInit();
    sysInit();

    sysLogPrintf(LOG_NOTE, "SERVER: Initializing...");

    fsInit();
    configInit();
    videoInit();
    pdguiInit(videoGetWindowHandle());
    inputInit();

    /* No audioInit — server has no audio */

    romdataInit();
    netInit();

    sysLogPrintf(LOG_NOTE, "SERVER: Init complete, setting up memory...");

    /* Minimal game init — just memory, no game state machine */
    osMemSize = g_OsMemSizeMb * 1024 * 1024;
    g_OsMemSize = osGetMemSize();
    g_MempHeapSize = g_OsMemSize;
    g_MempHeap = sysMemZeroAlloc(g_MempHeapSize);

    modmgrInit();

    /* Load defaults — server doesn't need agent files */
    {
        extern struct gamefile g_GameFile;
        extern void gamefileLoadDefaults(struct gamefile *file);
        gamefileLoadDefaults(&g_GameFile);
    }

    /* Start the server */
    {
        s32 result = netStartServer((u16)g_NetServerPort, g_NetMaxClients);
        if (result != 0) {
            sysLogPrintf(LOG_ERROR, "SERVER: Failed to start (error %d)", result);
            printf("SERVER: Failed to start! Check log.\n");
            SDL_Delay(3000);
            return 1;
        }
        sysLogPrintf(LOG_NOTE, "SERVER: Listening on port %u (max %d clients)",
                     g_NetServerPort, g_NetMaxClients);
    }

    sysLogPrintf(LOG_NOTE, "SERVER: Entering main loop. Close window to stop.");

    /* === Server main loop ===
     * This replaces the game's osCreateScheduler/bootCreateSched.
     * Simple loop: poll SDL events, process network, render ImGui GUI. */
    s32 running = 1;
    while (running) {
        /* Poll SDL events (window close, input for ImGui) */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
            }
            pdguiProcessEvent(&ev);
        }

        /* Process network — receive packets, send updates */
        netStartFrame();
        netEndFrame();

        /* Render the server GUI via ImGui */
        videoStartFrame();
        pdguiNewFrame();
        pdguiRender();
        videoEndFrame();

        /* Cap at ~60 FPS to save CPU */
        SDL_Delay(16);
    }

    /* Cleanup */
    sysLogPrintf(LOG_NOTE, "SERVER: Shutting down...");
    netDisconnect();
    pdguiShutdown();

    return 0;
}
