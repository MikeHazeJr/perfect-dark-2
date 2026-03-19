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
#include "types.h"
#include "data.h"
#include "bss.h"
#include "net/net.h"
#include "net/netlobby.h"

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
    strncpy((char *)(p + 6), lp->name, 15);
    p[21] = '\0';

    /* isLocal (s32 at offset 24, aligned) */
    s32 isLocal = (&g_NetClients[lp->clientId] == g_NetLocalClient) ? 1 : 0;
    memcpy(p + 24, &isLocal, sizeof(s32));

    /* state (s32 at offset 28) */
    s32 state = g_NetClients[lp->clientId].state;
    memcpy(p + 28, &state, sizeof(s32));

    return 1;
}

s32 netLocalClientInLobby(void)
{
    if (g_NetMode == NETMODE_NONE || !g_NetLocalClient) return 0;
    return (g_NetLocalClient->state == CLSTATE_LOBBY) ? 1 : 0;
}
