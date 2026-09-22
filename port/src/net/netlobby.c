/**
 * netlobby.c -- Network lobby state management.
 *
 * Builds authoritative state from authenticated clients and applies complete
 * presentation snapshots on remote clients without fabricating gameplay slots.
 * Tracks player slots, leader assignment, and ready states.
 *
 * Listen hosts participate as real players. Lobby leadership is retained by
 * client identity while the visible list is compacted; room authority comes
 * from the server-owned room record or its replicated cache.
 */

#include <PR/ultratypes.h>
#include <string.h>
#include "types.h"
#include "constants.h"
#include "net/net.h"
#include "net/netlobby.h"
#include "net/lobby_roster_wire.h"
#include "assetcatalog.h"
#include "system.h"
#include "room.h"

struct lobbystate g_Lobby;
static s32 s_RemoteRosterValid;

/* R-3: Client-side room cache — populated by SVC_ROOM_LIST */
room_cache_entry_t g_RoomCache[ROOM_CACHE_MAX];
s32 g_RoomCacheCount = 0;
u8  g_LocalRoomId = 0xFF;

void lobbyInit(void)
{
    s_RemoteRosterValid = 0;
    memset(&g_Lobby, 0, sizeof(g_Lobby));
    g_Lobby.leaderSlot = 0xFF; /* No leader assigned yet */
    g_Lobby.settings.scenario = 0;
    g_Lobby.settings.stage_id[0] = '\0';
    g_Lobby.settings.stagenum = 0;
    g_Lobby.settings.numSimulants = 0;
    sysLogPrintf(LOG_NOTE, "LOBBY: initialized");
}

s32 lobbyClientIsRosterParticipant(const struct netclient *client)
{
    /* Reconnect authentication is provisional until its preserved transaction
     * commits; CLSTATE_LOBBY alone does not authorize presentation publication. */
    return client && client->id < NET_MAX_CLIENTS
        && client->state >= CLSTATE_LOBBY && client->state <= CLSTATE_PREPARING
        && client->reconnect_preserved_index == NET_NULL_CLIENT;
}

void lobbyUpdate(void)
{
    if (g_NetMode == NETMODE_NONE) {
        return;
    }
    if (g_NetMode == NETMODE_CLIENT && s_RemoteRosterValid) return;

    /* Sync player list from network clients */
    u8 count = 0;
    const u8 previousLeaderClient = g_Lobby.leaderSlot < g_Lobby.numPlayers
        && g_Lobby.leaderSlot < LOBBY_MAX_PLAYERS
        && g_Lobby.players[g_Lobby.leaderSlot].active
        ? g_Lobby.players[g_Lobby.leaderSlot].clientId : 0xFF;
    u8 retainedLeaderSlot = 0xFF;
    u8 firstLobbySlot = 0xFF;

    for (s32 i = 0; i < NET_MAX_CLIENTS; i++) {
        struct netclient *cl = &g_NetClients[i];
        if (!lobbyClientIsRosterParticipant(cl)) {
            continue;
        }

        /* A listen host occupies a real client slot. Dedicated processes
         * have no local client and therefore need no exclusion here. */

        if (count >= LOBBY_MAX_PLAYERS) {
            break;
        }

        struct lobbyplayer *lp = &g_Lobby.players[count];
        lp->active = 1;
        lp->clientId = (u8)i;
        lp->state = cl->state;
        lp->roomId = cl->room_id;
        {
            /* Catalog ID strings are the sole identity. 2026-04-23: the legacy
             * integer derivations (lp->bodynum / lp->headnum) were removed
             * because (a) no consumer was reading them, and (b) the integer
             * path is inherently wrong for non-MP heads (Maian / Skedar /
             * Dr Carroll have mp_index=-1 per constraints.md). Anyone who
             * needs an integer should assetCatalogResolve() at use-site. */
            strncpy(lp->body_id, cl->settings.body_id, sizeof(lp->body_id) - 1);
            lp->body_id[sizeof(lp->body_id) - 1] = '\0';
            strncpy(lp->head_id, cl->settings.head_id, sizeof(lp->head_id) - 1);
            lp->head_id[sizeof(lp->head_id) - 1] = '\0';
        }
        lp->team = cl->settings.team;

        /* Copy player name — use Agent name from settings.
         * If the name is empty, the player hasn't loaded an agent yet. */
        if (cl->settings.name[0]) {
            strncpy(lp->name, cl->settings.name, LOBBY_NAME_LEN - 1);
        } else {
            snprintf(lp->name, LOBBY_NAME_LEN, "Player %d", i);
        }
        lp->name[LOBBY_NAME_LEN - 1] = '\0';

        /* Track the first client in LOBBY state for leader election */
        if (cl->state >= CLSTATE_LOBBY && firstLobbySlot == 0xFF) {
            firstLobbySlot = count;
        }

        if (lp->clientId == previousLeaderClient) retainedLeaderSlot = count;

        /* Ready state: in-game (CLSTATE_GAME exactly) means ready.
         * CLSTATE_PREPARING (5) > CLSTATE_GAME (4) — use == to avoid false positives. */
        lp->isReady = (cl->state == CLSTATE_GAME) ? 1 : 0;
        lp->isLeader = 0; /* Will be set below */

        count++;
    }

    /* Clear remaining slots */
    for (s32 i = count; i < LOBBY_MAX_PLAYERS; i++) {
        memset(&g_Lobby.players[i], 0, sizeof(struct lobbyplayer));
    }

    g_Lobby.numPlayers = count;

    /* Preserve the previous leader by client identity, not by a row index
     * that changes when an earlier player disconnects. */
    g_Lobby.leaderSlot = retainedLeaderSlot != 0xFF ? retainedLeaderSlot
        : firstLobbySlot != 0xFF ? firstLobbySlot : count ? 0 : 0xFF;

    /* Apply leader flag */
    for (s32 i = 0; i < count; i++) {
        g_Lobby.players[i].isLeader = (i == g_Lobby.leaderSlot) ? 1 : 0;
    }

    /* g_NetLocalClient is NULL on dedicated server, so checking it directly always yields inGame=0.
     * Walk g_NetClients[] instead to check if any client is actively in a match. */
    {
        /* Use == CLSTATE_GAME, not >= CLSTATE_GAME.
         * CLSTATE_PREPARING (5) > CLSTATE_GAME (4), so the >= check incorrectly
         * fires the "Preparing -> Match" room transition during the ready gate. */
        u8 anyInGame = 0;
        for (s32 i = 0; i < NET_MAX_CLIENTS; ++i) {
            if (g_NetClients[i].state == CLSTATE_GAME) {
                anyInGame = 1;
                break;
            }
        }
        g_Lobby.inGame = anyInGame;
    }
}

void lobbyCaptureRoster(lobby_roster_snapshot_t *snapshot)
{
    if (!snapshot) return;
    lobbyUpdate();
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->count = g_Lobby.numPlayers;
    snapshot->leaderClientId = g_Lobby.leaderSlot < g_Lobby.numPlayers
        ? g_Lobby.players[g_Lobby.leaderSlot].clientId : 0xff;
    memcpy(snapshot->players, g_Lobby.players, sizeof(snapshot->players));
}

s32 lobbyAcceptRoster(const lobby_roster_snapshot_t *snapshot)
{
    if (g_NetMode != NETMODE_CLIENT || !g_NetLocalClient
            || !lobbyRosterValid(snapshot)) return 0;
    s32 local = -1;
    for (u8 i = 0; i < snapshot->count; ++i) {
        if (snapshot->players[i].clientId == g_NetLocalClient->id) local = i;
    }
    if (local < 0) return 0;
    memcpy(g_Lobby.players, snapshot->players, sizeof(g_Lobby.players));
    g_Lobby.numPlayers = snapshot->count;
    g_Lobby.leaderSlot = 0xff;
    for (u8 i = 0; i < g_Lobby.numPlayers; ++i) {
        struct lobbyplayer *p = &g_Lobby.players[i];
        p->isLeader = p->clientId == snapshot->leaderClientId;
        p->isReady = p->state == CLSTATE_GAME;
        if (p->isLeader) g_Lobby.leaderSlot = i;
    }
    for (u8 i = g_Lobby.numPlayers; i < LOBBY_MAX_PLAYERS; ++i)
        memset(&g_Lobby.players[i], 0, sizeof(g_Lobby.players[i]));
    g_Lobby.inGame = g_Lobby.players[local].state == CLSTATE_GAME;
    s_RemoteRosterValid = 1;
    return 1;
}

const struct lobbyplayer *lobbyPlayerForView(s32 index, s32 roomOnly)
{
    if (index < 0) return NULL;
    const u8 room = g_NetLocalClient ? g_NetLocalClient->room_id : 0xff;
    if (roomOnly && room == 0xff) return NULL;
    for (u8 i = 0; i < g_Lobby.numPlayers && i < LOBBY_MAX_PLAYERS; ++i) {
        const struct lobbyplayer *p = &g_Lobby.players[i];
        if (!p->active || (roomOnly && p->roomId != room)) continue;
        if (index-- == 0) return p;
    }
    return NULL;
}

s32 lobbyPlayerCountForView(s32 roomOnly)
{
    lobbyUpdate();
    s32 count = 0;
    const u8 room = g_NetLocalClient ? g_NetLocalClient->room_id : 0xff;
    if (roomOnly && room == 0xff) return 0;
    for (u8 i = 0; i < g_Lobby.numPlayers && i < LOBBY_MAX_PLAYERS; ++i) {
        const struct lobbyplayer *p = &g_Lobby.players[i];
        if (p->active && (!roomOnly || p->roomId == room)) ++count;
    }
    return count;
}

s32 lobbyRoomLeaderClientId(void)
{
    if (!g_NetLocalClient || g_NetLocalClient->room_id == 0xff) return -1;
    if (g_NetMode == NETMODE_SERVER) {
        const hub_room_t *room = roomGetById(g_NetLocalClient->room_id);
        return room ? room->creator_client_id : -1;
    }
    for (s32 i = 0; i < g_RoomCacheCount && i < ROOM_CACHE_MAX; ++i) {
        if (g_RoomCache[i].id == g_NetLocalClient->room_id)
            return g_RoomCache[i].creator_client_id;
    }
    return -1;
}

void lobbySetLeader(u8 slot)
{
    if (slot >= LOBBY_MAX_PLAYERS) return;

    /* Clear old leader */
    for (s32 i = 0; i < LOBBY_MAX_PLAYERS; i++) {
        g_Lobby.players[i].isLeader = 0;
    }

    g_Lobby.leaderSlot = slot;
    if (g_Lobby.players[slot].active) {
        g_Lobby.players[slot].isLeader = 1;
        sysLogPrintf(LOG_NOTE, "LOBBY: leader changed to slot %d (%s)",
                     slot, g_Lobby.players[slot].name);
    }
}

u8 lobbyGetLeader(void)
{
    return g_Lobby.leaderSlot;
}

s32 lobbyIsLocalLeader(void)
{
    if (g_NetMode == NETMODE_NONE) {
        return 1; /* Offline = always leader */
    }
    if (!g_NetLocalClient) return 0;

    /* Room controls follow authoritative room ownership, which can differ
     * from the first/global lobby player. Missing client cache stays read-only. */
    if (g_NetLocalClient->room_id != 0xFF) {
        if (g_NetMode == NETMODE_SERVER) {
            const hub_room_t *room = roomGetById(g_NetLocalClient->room_id);
            return room && room->creator_client_id == g_NetLocalClient->id;
        }
        for (s32 i = 0; i < g_RoomCacheCount && i < ROOM_CACHE_MAX; ++i) {
            if (g_RoomCache[i].id == g_NetLocalClient->room_id)
                return g_RoomCache[i].creator_client_id == g_NetLocalClient->id;
        }
        return 0;
    }

    /* Find local client in lobby */
    for (s32 i = 0; i < g_Lobby.numPlayers; i++) {
        if (g_Lobby.players[i].active &&
            &g_NetClients[g_Lobby.players[i].clientId] == g_NetLocalClient) {
            return g_Lobby.players[i].isLeader;
        }
    }

    return 0;
}
