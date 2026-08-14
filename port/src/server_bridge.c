/**
 * server_bridge.c -- Bridge functions for server_gui.cpp.
 *
 * The server GUI (C++) cannot include game headers like types.h
 * (it #defines bool as s32). These functions provide safe access
 * to networking and lobby data that the server GUI needs.
 *
 * This is the server-side equivalent of pdgui_bridge.c (which is
 * client-only and depends on data.h/bss.h that the server lacks).
 */

#include <PR/ultratypes.h>
#include <string.h>
#include "net/netenet.h"  /* must precede types.h — enet.h #undef's bool */
#include "types.h"
#include "system.h"
#include "net/net.h"
#include "net/netbuf.h"
#include "net/netlobby.h"
#include "server_bans.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

extern struct mpsetup g_MpSetup;

/* ========================================================================
 * Lobby state bridge functions
 * ======================================================================== */

s32 lobbyGetPlayerCount(void)
{
    lobbyUpdate();
    return g_Lobby.numPlayers;
}

/* Fills a simplified player view struct for ImGui.
 * The struct layout must match lobbyplayer_view in server_gui.cpp. */
s32 lobbyGetPlayerInfo(s32 idx, void *out)
{
    if (idx < 0 || idx >= g_Lobby.numPlayers || !out) return 0;

    struct lobbyplayer *lp = &g_Lobby.players[idx];
    if (!lp->active) return 0;

    /* Write fields matching lobbyplayer_view layout.
     * 2026-04-23: struct shrank 2 bytes (deprecated headnum/bodynum removed).
     * Keep layout in sync with pdgui_bridge.c::lobbyGetPlayerInfo. */
    u8 *p = (u8 *)out;
    p[0] = lp->active;
    p[1] = lp->isLeader;
    p[2] = lp->isReady;
    p[3] = lp->team;
    strncpy((char *)(p + 4), lp->name, 31);
    p[35] = '\0';

    /* isLocal (s32 at offset 36, aligned after name[32]) - always 0 on dedicated */
    s32 isLocal = 0;
    memcpy(p + 36, &isLocal, sizeof(s32));

    /* state (s32 at offset 40) */
    s32 state = g_NetClients[lp->clientId].state;
    memcpy(p + 40, &state, sizeof(s32));

    /* clientId (u8 at offset 44) */
    p[44] = lp->clientId;

    return 1;
}

/* Phase 5: catalog ID accessors for lobby player identity */
const char *lobbyGetPlayerBodyId(s32 idx)
{
    if (idx < 0 || idx >= g_Lobby.numPlayers) return "";
    struct lobbyplayer *lp = &g_Lobby.players[idx];
    if (!lp->active) return "";
    return lp->body_id[0] ? lp->body_id : "";
}

const char *lobbyGetPlayerHeadId(s32 idx)
{
    if (idx < 0 || idx >= g_Lobby.numPlayers) return "";
    struct lobbyplayer *lp = &g_Lobby.players[idx];
    if (!lp->active) return "";
    return lp->head_id[0] ? lp->head_id : "";
}

/* ========================================================================
 * Network utility bridge functions
 * ======================================================================== */

u32 netGetClientPing(s32 clientId)
{
    /* Bounds: valid peer indices are [0, NET_MAX_CLIENTS).  Index NET_MAX_CLIENTS
     * is the reserved local/temporary slot and must never be operated on. */
    if (clientId < 0 || clientId >= NET_MAX_CLIENTS) return 0;
    struct netclient *cl = &g_NetClients[clientId];
    if (cl->state == CLSTATE_DISCONNECTED || !cl->peer) return 0;
    return cl->peer->roundTripTime;
}

void netServerKickClient(s32 clientId, const char *reason)
{
    if (clientId < 0 || clientId >= NET_MAX_CLIENTS) return;
    if (g_NetMode != NETMODE_SERVER) return;

    struct netclient *cl = &g_NetClients[clientId];
    if (cl->state == CLSTATE_DISCONNECTED || !cl->peer) return;

    sysLogPrintf(LOG_NOTE, "NET: kicking client %d (%s): %s",
                 clientId, cl->settings.name, reason ? reason : "no reason");
    netServerKick(cl, DISCONNECT_KICKED);
}

void netServerBanClient(s32 clientId, const char *reason)
{
    if (clientId < 0 || clientId >= NET_MAX_CLIENTS) return;
    if (g_NetMode != NETMODE_SERVER) return;

    struct netclient *cl = &g_NetClients[clientId];
    if (cl->state == CLSTATE_DISCONNECTED || !cl->peer) return;

    /* MASTER-C2c: persist to bans.ini so the kick is sticky across reconnects
     * and across server restarts.  Strip port from "ip:port" for matching. */
    char addrBuf[SERVER_BANS_ADDR_LEN];
    addrBuf[0] = '\0';
    char ipBuf[SERVER_BANS_ADDR_LEN];
    ipBuf[0] = '\0';
    if (enet_address_get_ip(&cl->peer->address, ipBuf, sizeof(ipBuf) - 1) == 0) {
        strncpy(addrBuf, ipBuf, sizeof(addrBuf) - 1);
        addrBuf[sizeof(addrBuf) - 1] = '\0';
    }

    if (addrBuf[0]) {
        serverBansAdd(addrBuf, cl->settings.name, reason);
    } else {
        sysLogPrintf(LOG_WARNING,
            "NET: banClient %d: could not resolve address; kicking without persisting",
            clientId);
    }

    sysLogPrintf(LOG_NOTE, "NET: banning client %d (%s @ %s): %s",
                 clientId, cl->settings.name, addrBuf[0] ? addrBuf : "?",
                 reason ? reason : "no reason");
    netServerKick(cl, DISCONNECT_BANNED);
}

/* ========================================================================
 * Server runtime stats bridge functions
 * ======================================================================== */

s32 serverGetMemoryMB(void)
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return (s32)(pmc.WorkingSetSize / (1024 * 1024));
    }
#endif
    return 0;
}

/* ========================================================================
 * Match setup bridge functions (server-side stage/mode control)
 * ======================================================================== */

const char *serverGetStageId(void)
{
    return g_MpSetup.stage_id;
}

void serverSetStageId(const char *stage_id)
{
    if (!stage_id) return;
    strncpy(g_MpSetup.stage_id, stage_id, sizeof(g_MpSetup.stage_id) - 1);
    g_MpSetup.stage_id[sizeof(g_MpSetup.stage_id) - 1] = '\0';
}

u8 serverGetScenario(void)
{
    return g_MpSetup.scenario;
}

void serverSetScenario(u8 scenario)
{
    g_MpSetup.scenario = scenario;
}
