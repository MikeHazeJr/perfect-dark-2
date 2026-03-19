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

#include "system.h"
#include "config.h"
#include "fs.h"
#include "net/net.h"
#include "net/netupnp.h"
#include "net/netlobby.h"

/* Functions stubbed in server_stubs.c */
extern void conInit(void);

/* All game globals are defined in server_stubs.c. */
extern u32 g_OsMemSize;
extern u8 *g_MempHeap;
extern u32 g_MempHeapSize;
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

    /* conInit is stubbed — server uses printf/sysLogPrintf */
    conInit();
    sysInit();
    fsInit();
    configInit();

    /* No videoInit, audioInit, romdataInit, modmgrInit — server doesn't
     * need rendering, audio, ROM data, or mod management. */

    netInit();

    /* Minimal memory setup */
    g_OsMemSize = 64 * 1024 * 1024;
    g_MempHeapSize = g_OsMemSize;
    g_MempHeap = sysMemZeroAlloc(g_MempHeapSize);

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
