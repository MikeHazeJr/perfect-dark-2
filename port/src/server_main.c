/**
 * server_main.c -- Dedicated server entry point.
 *
 * Separate executable (pd-server) that shares the game's networking
 * and multiplayer logic but has its own minimal startup.
 */

#include <stdlib.h>
#include <stdio.h>
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

/* Globals that the shared game code references */
u32 g_OsMemSize = 0;
s32 g_OsMemSizeMb = 64;
s8 g_Resetting = false;
OSSched g_Sched;
OSMesgQueue g_MainMesgQueue;
OSMesg g_MainMesgBuf[32];
u8 *g_MempHeap = NULL;
u32 g_MempHeapSize = 0;
s32 g_SkipIntro = 1;

/* Forward declarations */
extern s32 g_NetDedicated;
extern s32 g_NetHostLatch;
extern s32 g_FileAutoSelect;
extern u32 g_NetServerPort;
extern s32 g_NetMaxClients;

int main(int argc, const char **argv)
{
    sysInitArgs(argc, argv);

    /* Force dedicated server mode */
    g_NetDedicated = 1;
    g_NetHostLatch = 1;
    g_FileAutoSelect = 0;

    conInit();
    sysInit();
    fsInit();
    configInit();
    videoInit();
    pdguiInit(videoGetWindowHandle());
    inputInit();

    /* No audioInit — server has no audio */

    romdataInit();
    netInit();

    /* Server-local gameInit — sets up player configs and HUD alignment.
     * The client's gameInit is static in main.c so we replicate it here. */
    {
        extern s32 g_HudCenter;
        extern u8 g_HudAlignModeL, g_HudAlignModeR;
        osMemSize = g_OsMemSizeMb * 1024 * 1024;
        for (s32 i = 0; i < MAX_LOCAL_PLAYERS; ++i) {
            struct extplayerconfig *cfg = g_PlayerExtCfg + i;
            cfg->fovzoommult = cfg->fovzoom ? cfg->fovy / 60.0f : 1.0f;
        }
    }

    modmgrInit();

    g_OsMemSize = osGetMemSize();
    g_MempHeapSize = g_OsMemSize;
    g_MempHeap = sysMemZeroAlloc(g_MempHeapSize);

    /* Load defaults — server doesn't need agent files */
    {
        extern struct gamefile g_GameFile;
        extern void gamefileLoadDefaults(struct gamefile *file);
        gamefileLoadDefaults(&g_GameFile);
    }

    /* Start the server immediately */
    {
        s32 result = netStartServer((u16)g_NetServerPort, g_NetMaxClients);
        if (result != 0) {
            sysLogPrintf(LOG_ERROR, "SERVER: Failed to start (error %d)", result);
            return 1;
        }
        sysLogPrintf(LOG_NOTE, "SERVER: Started on port %u (max %d clients)",
                     g_NetServerPort, g_NetMaxClients);
    }

    sysLogPrintf(LOG_NOTE, "SERVER: Entering main loop");

    /* Start the scheduler — this is the main game loop.
     * Same as the client's bootCreateSched in main.c. */
    osCreateMesgQueue(&g_MainMesgQueue, g_MainMesgBuf, ARRAYCOUNT(g_MainMesgBuf));
    if (osTvType == OS_TV_MPAL) {
        osCreateScheduler(&g_Sched, NULL, OS_VI_MPAL_LAN1, 1);
    } else {
        osCreateScheduler(&g_Sched, NULL, OS_VI_NTSC_LAN1, 1);
    }

    /* Cleanup */
    netDisconnect();
    pdguiShutdown();

    return 0;
}
