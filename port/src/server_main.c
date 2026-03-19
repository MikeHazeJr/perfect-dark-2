/**
 * server_main.c -- Dedicated server entry point.
 *
 * Completely independent of the game's rendering pipeline.
 * Own SDL window, own GL context, own ImGui instance, own main loop.
 * The game's networking and multiplayer logic hooks into this.
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

#include "fs.h"
#include "romdata.h"
#include "config.h"
#include "modmgr.h"
#include "system.h"
#include "console.h"
#include "net/net.h"
#include "net/netupnp.h"
#include "net/netlobby.h"

/* Globals that shared game code references */
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

void *bootAllocateStack(s32 threadid, s32 size)
{
    static u8 stackbuf[0x1000];
    return stackbuf;
}

/* Net globals defined in net.c */
extern s32 g_NetDedicated;
extern s32 g_NetHostLatch;
extern u32 g_NetServerPort;
extern s32 g_NetMaxClients;
extern s32 g_NetNumClients;

/* Server GUI — implemented in server_gui.cpp */
extern s32 serverGuiInit(SDL_Window *window, void *glContext);
extern void serverGuiFrame(SDL_Window *window);
extern void serverGuiProcessEvent(SDL_Event *ev);
extern void serverGuiShutdown(void);

int main(int argc, char **argv)
{
    sysInitArgs(argc, (const char **)argv);

    g_NetDedicated = 1;
    g_NetHostLatch = 1;

    printf("PD2 Dedicated Server starting...\n");

    conInit();
    sysInit();
    fsInit();
    configInit();

    /* No videoInit — the server creates its own window below */
    /* No audioInit — server has no audio */

    romdataInit();
    netInit();

    /* Minimal game init */
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

    /* === Create the server's own SDL window and GL context === */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        printf("SERVER: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window *window = SDL_CreateWindow(
        "PD2 Dedicated Server - starting...",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        800, 500,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        printf("SERVER: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GLContext glCtx = SDL_GL_CreateContext(window);
    if (!glCtx) {
        printf("SERVER: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_MakeCurrent(window, glCtx);
    SDL_GL_SetSwapInterval(1); /* vsync */

    sysLogPrintf(LOG_NOTE, "SERVER: Window and GL context created");

    /* Initialize the server GUI (ImGui) */
    if (serverGuiInit(window, glCtx) != 0) {
        printf("SERVER: GUI init failed\n");
        return 1;
    }

    /* Start the network server */
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

    sysLogPrintf(LOG_NOTE, "SERVER: Entering main loop");

    /* === Main loop === */
    s32 running = 1;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
            }
            serverGuiProcessEvent(&ev);
        }

        /* Network tick */
        netStartFrame();
        netEndFrame();

        /* Update window title */
        {
            static u32 titleCounter = 0;
            if (++titleCounter >= 60) {
                titleCounter = 0;
                char title[256];
                const char *ip = netUpnpIsActive() ? netUpnpGetExternalIP() : "";
                if (ip && ip[0]) {
                    snprintf(title, sizeof(title), "PD2 Dedicated Server - %s:%u - %d/%d connected",
                             ip, g_NetServerPort, g_NetNumClients, g_NetMaxClients);
                } else {
                    snprintf(title, sizeof(title), "PD2 Dedicated Server - port %u - %d/%d connected",
                             g_NetServerPort, g_NetNumClients, g_NetMaxClients);
                }
                SDL_SetWindowTitle(window, title);
            }
        }

        /* Render GUI */
        serverGuiFrame(window);

        SDL_Delay(16);
    }

    /* Cleanup */
    sysLogPrintf(LOG_NOTE, "SERVER: Shutting down...");
    netDisconnect();
    serverGuiShutdown();
    SDL_GL_DeleteContext(glCtx);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
