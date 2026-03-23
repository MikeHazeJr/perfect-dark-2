/**
 * pdgui_bridge.c -- C bridge functions for ImGui menu code.
 *
 * ImGui menu files (.cpp) cannot include types.h because it #defines
 * bool as s32, which breaks C++. This file provides small accessor
 * functions that the C++ menu code can call to safely read/write
 * game data structures that require types.h knowledge.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.c in CMakeLists.txt.
 */

#include <PR/ultratypes.h>
#include <string.h>
#include "net/netenet.h"  /* must precede types.h — enet.h #undef's bool */
#include "types.h"
#include "data.h"
#include "bss.h"
#include "system.h"
#include "net/net.h"
#include "net/netbuf.h"
#include "net/netmsg.h"
#include "net/netlobby.h"
#include "game/lang.h"
#include "game/mplayer/mplayer.h"
#include "modmgr.h"

/**
 * Set the MP player config name for a given player number.
 * Safely handles bounds checking and null termination.
 */
void mpPlayerConfigSetName(s32 playernum, const char *name)
{
    if (playernum < 0 || playernum >= ARRAYCOUNT(g_PlayerConfigsArray)) {
        return;
    }
    if (!name) {
        return;
    }

    strncpy(g_PlayerConfigsArray[playernum].base.name, name, 14);
    g_PlayerConfigsArray[playernum].base.name[14] = '\0';
}

/**
 * Set the MP player config head and body indices.
 */
void mpPlayerConfigSetHeadBody(s32 playernum, u8 headnum, u8 bodynum)
{
    if (playernum < 0 || playernum >= ARRAYCOUNT(g_PlayerConfigsArray)) {
        return;
    }

    g_PlayerConfigsArray[playernum].base.mpheadnum = headnum;
    g_PlayerConfigsArray[playernum].base.mpbodynum = bodynum;
}

/**
 * Get the MP player config head index.
 */
u8 mpPlayerConfigGetHead(s32 playernum)
{
    if (playernum < 0 || playernum >= ARRAYCOUNT(g_PlayerConfigsArray)) {
        return 0;
    }
    return g_PlayerConfigsArray[playernum].base.mpheadnum;
}

/**
 * Get the MP player config body index.
 */
u8 mpPlayerConfigGetBody(s32 playernum)
{
    if (playernum < 0 || playernum >= ARRAYCOUNT(g_PlayerConfigsArray)) {
        return 0;
    }
    return g_PlayerConfigsArray[playernum].base.mpbodynum;
}

/**
 * Get the MP player config name (read-only pointer).
 */
const char *mpPlayerConfigGetName(s32 playernum)
{
    if (playernum < 0 || playernum >= ARRAYCOUNT(g_PlayerConfigsArray)) {
        return "";
    }
    return g_PlayerConfigsArray[playernum].base.name;
}

/* ========================================================================
 * Network lobby bridge functions
 * ======================================================================== */

s32 netGetMode(void)
{
    return g_NetMode;
}

s32 netGetNumClients(void)
{
    return g_NetNumClients;
}

s32 netGetMaxClients(void)
{
    return g_NetMaxClients;
}

u32 netGetServerPort(void)
{
    return g_NetServerPort;
}

const char *netGetPublicIP(void)
{
    extern const char *netUpnpGetExternalIP(void);
    extern s32 netUpnpIsActive(void);
    if (netUpnpIsActive()) {
        return netUpnpGetExternalIP();
    }
    return "";
}

/**
 * Get number of connected client slots (including server's local client).
 * Returns count of clients in non-disconnected states.
 */
s32 netLobbyGetClientCount(void)
{
    if (g_NetMode == NETMODE_NONE) return 0;

    s32 count = 0;
    for (s32 i = 0; i <= NET_MAX_CLIENTS; i++) {
        if (g_NetClients[i].state != CLSTATE_DISCONNECTED) {
            count++;
        }
    }
    return count;
}

/**
 * Get client state by sequential index (skipping disconnected slots).
 * Returns CLSTATE_DISCONNECTED if index out of range.
 */
s32 netLobbyGetClientState(s32 idx)
{
    s32 count = 0;
    for (s32 i = 0; i <= NET_MAX_CLIENTS; i++) {
        if (g_NetClients[i].state != CLSTATE_DISCONNECTED) {
            if (count == idx) return g_NetClients[i].state;
            count++;
        }
    }
    return CLSTATE_DISCONNECTED;
}

const char *netLobbyGetClientName(s32 idx)
{
    s32 count = 0;
    for (s32 i = 0; i <= NET_MAX_CLIENTS; i++) {
        if (g_NetClients[i].state != CLSTATE_DISCONNECTED) {
            if (count == idx) {
                if (g_NetClients[i].settings.name[0]) {
                    return g_NetClients[i].settings.name;
                }
                return "Player";
            }
            count++;
        }
    }
    return "???";
}

u8 netLobbyGetClientHead(s32 idx)
{
    s32 count = 0;
    for (s32 i = 0; i <= NET_MAX_CLIENTS; i++) {
        if (g_NetClients[i].state != CLSTATE_DISCONNECTED) {
            if (count == idx) return g_NetClients[i].settings.headnum;
            count++;
        }
    }
    return 0;
}

u8 netLobbyGetClientBody(s32 idx)
{
    s32 count = 0;
    for (s32 i = 0; i <= NET_MAX_CLIENTS; i++) {
        if (g_NetClients[i].state != CLSTATE_DISCONNECTED) {
            if (count == idx) return g_NetClients[i].settings.bodynum;
            count++;
        }
    }
    return 0;
}

u8 netLobbyGetClientTeam(s32 idx)
{
    s32 count = 0;
    for (s32 i = 0; i <= NET_MAX_CLIENTS; i++) {
        if (g_NetClients[i].state != CLSTATE_DISCONNECTED) {
            if (count == idx) return g_NetClients[i].settings.team;
            count++;
        }
    }
    return 0;
}

s32 netLobbyIsLocalClient(s32 idx)
{
    s32 count = 0;
    for (s32 i = 0; i <= NET_MAX_CLIENTS; i++) {
        if (g_NetClients[i].state != CLSTATE_DISCONNECTED) {
            if (count == idx) return (&g_NetClients[i] == g_NetLocalClient) ? 1 : 0;
            count++;
        }
    }
    return 0;
}

/* ========================================================================
 * Lobby state bridge functions
 * ======================================================================== */

s32 lobbyGetPlayerCount(void)
{
    lobbyUpdate();
    return g_Lobby.numPlayers;
}

/* Fills a simplified player view struct for ImGui.
 * The struct layout must match lobbyplayer_view in pdgui_lobby.cpp. */
s32 lobbyGetPlayerInfo(s32 idx, void *out)
{
    if (idx < 0 || idx >= g_Lobby.numPlayers || !out) return 0;

    struct lobbyplayer *lp = &g_Lobby.players[idx];
    if (!lp->active) return 0;

    /* Write fields matching lobbyplayer_view layout */
    u8 *p = (u8 *)out;
    p[0] = lp->active;
    p[1] = lp->isLeader;
    p[2] = lp->isReady;
    p[3] = lp->headnum;
    p[4] = lp->bodynum;
    p[5] = lp->team;
    strncpy((char *)(p + 6), lp->name, 31);
    p[37] = '\0';

    /* isLocal (s32 at offset 40, aligned after name[32]) */
    s32 isLocal = (&g_NetClients[lp->clientId] == g_NetLocalClient) ? 1 : 0;
    memcpy(p + 40, &isLocal, sizeof(s32));

    /* state (s32 at offset 44) */
    s32 state = g_NetClients[lp->clientId].state;
    memcpy(p + 44, &state, sizeof(s32));

    return 1;
}

s32 netLocalClientInLobby(void)
{
    if (g_NetMode == NETMODE_NONE || !g_NetLocalClient) return 0;
    return (g_NetLocalClient->state == CLSTATE_LOBBY) ? 1 : 0;
}

u32 netGetClientPing(s32 clientId)
{
    if (clientId < 0 || clientId > NET_MAX_CLIENTS) return 0;
    struct netclient *cl = &g_NetClients[clientId];
    if (cl->state == CLSTATE_DISCONNECTED || !cl->peer) return 0;
    /* ENet peer round-trip time in milliseconds */
    return cl->peer->roundTripTime;
}

void netServerKickClient(s32 clientId, const char *reason)
{
    if (clientId < 0 || clientId > NET_MAX_CLIENTS) return;
    if (g_NetMode != NETMODE_SERVER) return;

    struct netclient *cl = &g_NetClients[clientId];
    if (cl->state == CLSTATE_DISCONNECTED || !cl->peer) return;

    sysLogPrintf(LOG_NOTE, "NET: kicking client %d (%s): %s",
                 clientId, cl->settings.name, reason ? reason : "no reason");
    enet_peer_disconnect(cl->peer, 0);
}

/* ========================================================================
 * Pause menu bridge functions (pdgui_menu_pausemenu.cpp)
 * ======================================================================== */

u32 pdguiPauseGetChrSlots(void)
{
    return g_MpSetup.chrslots;
}

u32 pdguiPauseGetOptions(void)
{
    return g_MpSetup.options;
}

u8 pdguiPauseGetScenario(void)
{
    return g_MpSetup.scenario;
}

u8 pdguiPauseGetStagenum(void)
{
    return g_MpSetup.stagenum;
}

u8 pdguiPauseGetTimelimit(void)
{
    return g_MpSetup.timelimit;
}

u8 pdguiPauseGetScorelimit(void)
{
    return g_MpSetup.scorelimit;
}

u8 pdguiPauseGetPaused(void)
{
    return g_MpSetup.paused;
}

s32 pdguiPauseGetNormMplayerIsRunning(void)
{
    return g_Vars.normmplayerisrunning ? 1 : 0;
}

void pdguiPauseSetPlayerAborted(void)
{
    if (g_Vars.currentplayer) {
        g_Vars.currentplayer->aborted = true;
    }
}

/**
 * D3R-5 DEBUG: Reset match state for the map cycle test.
 *
 * Called between consecutive matchStart() calls during the automated
 * map test. Resets the "match running" flags so the game sees a clean
 * "no match active" state — same as if we'd returned to the menu.
 *
 * This intentionally avoids mainEndStage()/mpEndMatch() because those
 * trigger the OG endscreen which blocks for player input and can't be
 * auto-dismissed yet.
 */
void pdguiMapTestResetMatchState(void)
{
    g_Vars.normmplayerisrunning = false;
    g_Vars.mplayerisrunning = false;
    g_Vars.lvmpbotlevel = 0;
    g_MainIsEndscreen = false;
    mpSetPaused(MPPAUSEMODE_UNPAUSED);
}

const char *pdguiPauseGetStageName(u8 stagenum)
{
    s32 count = modmgrGetTotalArenas();
    for (s32 i = 0; i < count; i++) {
        struct mparena *arena = modmgrGetArena(i);
        if (arena && arena->stagenum == stagenum) {
            return langGet(arena->name);
        }
    }

    return "Unknown";
}

/* ========================================================================
 * Recent server browser bridge functions
 * ======================================================================== */

s32 netRecentServerGetCount(void)
{
    return g_NetNumRecentServers;
}

s32 netRecentServerGetInfo(s32 idx, char *addr, s32 addrSize,
                           u8 *flags, u8 *numclients, u8 *maxclients,
                           u32 *online)
{
    if (idx < 0 || idx >= g_NetNumRecentServers) return 0;

    struct netrecentserver *srv = &g_NetRecentServers[idx];
    if (addr && addrSize > 0) {
        strncpy(addr, srv->addr, addrSize - 1);
        addr[addrSize - 1] = '\0';
    }
    if (flags) *flags = srv->flags;
    if (numclients) *numclients = srv->numclients;
    if (maxclients) *maxclients = srv->maxclients;
    if (online) *online = srv->online ? 1 : 0;
    return 1;
}

/* ========================================================================
 * Lobby command bridge — send CLC_LOBBY_START from C++ lobby UI
 * ======================================================================== */

s32 netLobbyRequestStart(u8 gamemode, u8 stagenum, u8 difficulty)
{
    if (g_NetMode != NETMODE_CLIENT || !g_NetLocalClient) {
        return -1;
    }
    if (g_NetLocalClient->state < CLSTATE_LOBBY) {
        return -2;
    }

    netmsgClcLobbyStartWrite(&g_NetLocalClient->out, gamemode, stagenum, difficulty);
    sysLogPrintf(LOG_NOTE, "BRIDGE: sent CLC_LOBBY_START gamemode=%u stage=%u diff=%u",
                 gamemode, stagenum, difficulty);
    return 0;
}
