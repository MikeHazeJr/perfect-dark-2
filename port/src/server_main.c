/**
 * server_main.c -- Dedicated server entry point.
 *
 * Completely independent of the game's rendering pipeline.
 * Own SDL window, own GL context, own ImGui instance, own main loop.
 * The game's networking and multiplayer logic hooks into this.
 *
 * Usage:
 *   pd2_server [--port PORT] [--maxclients N] [--gamemode MODE] [--headless]
 *
 * Defaults: port=27100, maxclients=8, gamemode=mp
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#ifdef _WIN32
#include <windows.h>
#endif
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
#include "connectcode.h"
#include "hub.h"
#include "versioninfo.h"
#include "updater.h"
#include "updateversion.h"

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
extern u8 g_NetGameMode;

/* Server GUI — implemented in server_gui.cpp */
extern s32 serverGuiInit(SDL_Window *window, void *glContext);
extern void serverGuiFrame(SDL_Window *window);
extern void serverGuiProcessEvent(SDL_Event *ev);
extern void serverGuiShutdown(void);

/* Game mode constants */
#define GAMEMODE_MP   0
#define GAMEMODE_COOP 1
#define GAMEMODE_ANTI 2

/* ========================================================================
 * Signal handling for graceful shutdown
 * ======================================================================== */

static volatile s32 s_ShutdownRequested = 0;
static s32 s_UpdateCheckLogged = 0;  /* set once after first check completes */

#ifdef _WIN32
static BOOL WINAPI serverConsoleHandler(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT) {
        s_ShutdownRequested = 1;
        return TRUE;
    }
    return FALSE;
}
#else
static void serverSignalHandler(int sig)
{
    (void)sig;
    s_ShutdownRequested = 1;
}
#endif

static void serverInstallSignalHandlers(void)
{
#ifdef _WIN32
    SetConsoleCtrlHandler(serverConsoleHandler, TRUE);
#else
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = serverSignalHandler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
#endif
}

/* ========================================================================
 * Command-line argument parsing
 * ======================================================================== */

static void serverPrintUsage(const char *argv0)
{
    printf("Usage: %s [OPTIONS]\n", argv0);
    printf("  --port PORT        Listen port (default: %u)\n", NET_DEFAULT_PORT);
    printf("  --maxclients N     Max players 1-%d (default: %d)\n", NET_MAX_CLIENTS, NET_MAX_CLIENTS);
    printf("  --gamemode MODE    Initial mode: mp, coop, anti (default: mp)\n");
    printf("  --headless         Run without GUI window\n");
    printf("  --check-update     Check for server updates and exit\n");
    printf("  --no-update-check  Skip automatic update check on startup\n");
    printf("  --help             Show this help\n");
}

static s32 s_Headless = 0;

static s32 serverParseArgs(s32 argc, char **argv)
{
    for (s32 i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            u32 port = (u32)atoi(argv[++i]);
            if (port > 0 && port <= 65535) {
                g_NetServerPort = port;
            } else {
                printf("ERROR: invalid port: %s\n", argv[i]);
                return -1;
            }
        } else if (strcmp(argv[i], "--maxclients") == 0 && i + 1 < argc) {
            s32 mc = atoi(argv[++i]);
            if (mc >= 1 && mc <= NET_MAX_CLIENTS) {
                g_NetMaxClients = mc;
            } else {
                printf("ERROR: maxclients must be 1-%d\n", NET_MAX_CLIENTS);
                return -1;
            }
        } else if (strcmp(argv[i], "--gamemode") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "mp") == 0) {
                g_NetGameMode = GAMEMODE_MP;
            } else if (strcmp(argv[i], "coop") == 0) {
                g_NetGameMode = GAMEMODE_COOP;
            } else if (strcmp(argv[i], "anti") == 0) {
                g_NetGameMode = GAMEMODE_ANTI;
            } else {
                printf("ERROR: unknown gamemode: %s (use mp, coop, anti)\n", argv[i]);
                return -1;
            }
        } else if (strcmp(argv[i], "--headless") == 0) {
            s_Headless = 1;
        } else if (strcmp(argv[i], "--check-update") == 0) {
            return 2; /* signal: check update and exit */
        } else if (strcmp(argv[i], "--no-update-check") == 0) {
            /* handled later */
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            serverPrintUsage(argv[0]);
            return 1; /* signal to exit cleanly */
        }
    }
    return 0;
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(int argc, char **argv)
{
    sysInitArgs(argc, (const char **)argv);

    g_NetDedicated = 1;
    g_NetHostLatch = 1;

    /* D13: Apply pending update before anything else */
    updaterApplyPending();

    /* Parse command-line args (may override port/maxclients/gamemode) */
    s32 parseResult = serverParseArgs(argc, argv);
    if (parseResult > 0 && parseResult != 2) return 0;  /* --help */
    if (parseResult < 0) return 1;  /* error */

    /* D13: --check-update: init minimal systems, check, print, exit */
    if (parseResult == 2) {
        SDL_Init(SDL_INIT_TIMER);  /* needed for SDL_Delay + SDL_CreateMutex */
        conInit();
        sysInit();
        fsInit();
        updaterInit();
        updaterCheckAsync();

        /* Block until check completes (acceptable for CLI mode) */
        printf("Checking for server updates...\n");
        for (s32 wait = 0; wait < 300; wait++) { /* 30 second max */
            SDL_Delay(100);
            updater_status_t st = updaterGetStatus();
            if (st != UPDATER_CHECKING) break;
        }

        if (updaterIsUpdateAvailable()) {
            const updater_release_t *lat = updaterGetLatest();
            char verstr[64];
            versionFormat(&lat->version, verstr, sizeof(verstr));
            printf("UPDATE AVAILABLE: v%s (current: v%s)\n",
                verstr, updaterGetVersionString());
            printf("Download: %s\n", lat->assetUrl);
        } else {
            printf("Up to date: v%s\n", updaterGetVersionString());
        }

        updaterShutdown();
        return 0;
    }

    printf("PD2 Dedicated Server v" VERSION_STRING " starting...\n");
    printf("  Port: %u | Max clients: %d | Headless: %s\n",
           g_NetServerPort, g_NetMaxClients, s_Headless ? "yes" : "no");

    serverInstallSignalHandlers();

    /* conInit is stubbed — server uses printf/sysLogPrintf */
    conInit();
    sysInit();
    fsInit();
    configInit();

    /* No videoInit, audioInit, romdataInit, modmgrInit — server doesn't
     * need rendering, audio, ROM data, or mod management. */

    /* D13: Update system for server */
    updaterInit();
    if (!sysArgCheck("--no-update-check")) {
        updaterCheckAsync();
    }

    netInit();
    lobbyInit();
    hubInit();

    /* Minimal memory setup */
    g_OsMemSize = 64 * 1024 * 1024;
    g_MempHeapSize = g_OsMemSize;
    g_MempHeap = sysMemZeroAlloc(g_MempHeapSize);

    /* === GUI setup (unless headless) === */
    SDL_Window *window = NULL;
    SDL_GLContext glCtx = NULL;

    if (!s_Headless) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
            printf("SERVER: SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        window = SDL_CreateWindow(
            "PD2 Server v" VERSION_STRING " - starting...",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            800, 500,
            SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
        );

        if (!window) {
            printf("SERVER: SDL_CreateWindow failed: %s\n", SDL_GetError());
            return 1;
        }

        glCtx = SDL_GL_CreateContext(window);
        if (!glCtx) {
            printf("SERVER: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
            return 1;
        }

        SDL_GL_MakeCurrent(window, glCtx);
        SDL_GL_SetSwapInterval(1);

        sysLogPrintf(LOG_NOTE, "SERVER: Window and GL context created");

        if (serverGuiInit(window, glCtx) != 0) {
            printf("SERVER: GUI init failed\n");
            return 1;
        }
    } else {
        /* Headless: init SDL timer only for SDL_Delay */
        if (SDL_Init(SDL_INIT_TIMER) < 0) {
            printf("SERVER: SDL_Init (timer) failed: %s\n", SDL_GetError());
            return 1;
        }
        sysLogPrintf(LOG_NOTE, "SERVER: Running in headless mode");
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
    while (running && !s_ShutdownRequested) {
        /* Process SDL events (GUI mode only) */
        if (!s_Headless) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) {
                    running = 0;
                }
                serverGuiProcessEvent(&ev);
            }
        }

        /* D13: Tick update system + log if check just completed */
        updaterTick();
        if (!s_UpdateCheckLogged) {
            updater_status_t us = updaterGetStatus();
            if (us == UPDATER_CHECK_DONE || us == UPDATER_CHECK_FAILED) {
                s_UpdateCheckLogged = 1;
                if (us == UPDATER_CHECK_DONE) {
                    if (updaterIsUpdateAvailable()) {
                        const updater_release_t *lat = updaterGetLatest();
                        char verstr[64];
                        versionFormat(&lat->version, verstr, sizeof(verstr));
                        sysLogPrintf(LOG_NOTE,
                            "SERVER: Update available: v%s (current: v%s)",
                            verstr, updaterGetVersionString());
                        sysLogPrintf(LOG_NOTE,
                            "SERVER: Download: %s", lat->assetUrl);
                        if (s_Headless) {
                            sysLogPrintf(LOG_NOTE,
                                "SERVER: Use --check-update flag or open server GUI to update.");
                        }
                    } else {
                        sysLogPrintf(LOG_NOTE,
                            "SERVER: Up to date (v%s)", updaterGetVersionString());
                    }
                }
            }
        }

        /* Network tick */
        netStartFrame();
        lobbyUpdate();
        hubTick();
        netEndFrame();

        /* Update window title periodically */
        if (!s_Headless) {
            static u32 titleCounter = 0;
            if (++titleCounter >= 60) {
                titleCounter = 0;
                char title[256];
                const char *ip = netUpnpIsActive() ? netUpnpGetExternalIP() : "";
                /* After B-28: dedicated server's g_NetNumClients already counts only real
                 * players (no server slot).  No -1 needed for dedicated. */
                s32 displayClients = g_NetDedicated ? g_NetNumClients
                                                    : (g_NetNumClients > 0 ? g_NetNumClients - 1 : 0);
                if (ip && ip[0]) {
                    /* Show connect code, not raw IP (B-29). */
                    char connectCode[256] = "";
                    u32 a = 0, b = 0, c = 0, d = 0;
                    if (sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
                        u32 ipAddr = (a << 24) | (b << 16) | (c << 8) | d;
                        connectCodeEncode(ipAddr, connectCode, sizeof(connectCode));
                    }
                    if (connectCode[0]) {
                        snprintf(title, sizeof(title), "PD2 Server v" VERSION_STRING " - %s - %d/%d connected",
                                 connectCode, displayClients, g_NetMaxClients);
                    } else {
                        snprintf(title, sizeof(title), "PD2 Server v" VERSION_STRING " - port %u - %d/%d connected",
                                 g_NetServerPort, displayClients, g_NetMaxClients);
                    }
                } else {
                    snprintf(title, sizeof(title), "PD2 Server v" VERSION_STRING " - port %u - %d/%d connected",
                             g_NetServerPort, displayClients, g_NetMaxClients);
                }
                SDL_SetWindowTitle(window, title);
            }

            /* Render GUI */
            serverGuiFrame(window);
        }

        SDL_Delay(16);
    }

    /* Cleanup */
    sysLogPrintf(LOG_NOTE, "SERVER: Shutting down%s...",
                 s_ShutdownRequested ? " (signal received)" : "");
    hubShutdown();
    netDisconnect();

    if (!s_Headless) {
        serverGuiShutdown();
        SDL_GL_DeleteContext(glCtx);
        SDL_DestroyWindow(window);
    }

    SDL_Quit();

    return 0;
}
