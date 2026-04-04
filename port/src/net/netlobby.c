/**
 * netlobby.c -- Network lobby state management.
 *
 * Syncs lobby state from the network client array each frame.
 * Tracks player slots, leader assignment, and ready states.
 *
 * Architecture: Dedicated-server-only model.
 * - The server is always a dedicated process (g_NetDedicated == 1).
 * - The first client to reach CLSTATE_LOBBY becomes the lobby leader.
 * - Leader can be reassigned if the current leader disconnects.
 * - All players (including leader) are clients — no host player.
 */

#include <PR/ultratypes.h>
#include <string.h>
#include "types.h"
#include "constants.h"
#include "net/net.h"
#include "net/netlobby.h"
#include "assetcatalog.h"
#include "system.h"

struct lobbystate g_Lobby;

void lobbyInit(void)
{
    memset(&g_Lobby, 0, sizeof(g_Lobby));
    g_Lobby.leaderSlot = 0xFF; /* No leader assigned yet */
    g_Lobby.settings.scenario = 0;
    g_Lobby.settings.stagenum = 0;
    g_Lobby.settings.numSimulants = 0;
    sysLogPrintf(LOG_NOTE, "LOBBY: initialized (dedicated server model)");
}

void lobbyUpdate(void)
{
    if (g_NetMode == NETMODE_NONE) {
        return;
    }

    /* Sync player list from network clients */
    u8 count = 0;
    u8 currentLeaderFound = 0;
    u8 firstLobbySlot = 0xFF;

    for (s32 i = 0; i < NET_MAX_CLIENTS; i++) {
        struct netclient *cl = &g_NetClients[i];
        if (cl->state == CLSTATE_DISCONNECTED) {
            continue;
        }

        /* Skip the local server's own slot — after B-28, dedicated servers have
         * g_NetLocalClient == NULL, so this check is always false on dedicated servers
         * (all slots are real players).  On listen-server builds it skips the host slot.
         * On clients (NETMODE_CLIENT), do NOT skip: the local slot IS the player and must
         * appear in their own lobby list so they see themselves and are elected leader. */
        if (g_NetMode != NETMODE_CLIENT && cl == g_NetLocalClient) {
            continue;
        }

        if (count >= LOBBY_MAX_PLAYERS) {
            break;
        }

        struct lobbyplayer *lp = &g_Lobby.players[count];
        lp->active = 1;
        lp->clientId = (u8)i;
        {
            const asset_entry_t *be = assetCatalogResolve(cl->settings.body_id);
            const asset_entry_t *he = assetCatalogResolve(cl->settings.head_id);
            lp->bodynum = be ? (u8)be->runtime_index : 0;
            lp->headnum = he ? (u8)he->runtime_index : 0;
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

        /* Eager leader: if no leader is set yet and this is the first
         * lobby-state client, assign them immediately.  This ensures the
         * leader slot is populated before the post-loop election code runs,
         * so CLC_LOBBY_START processed in the same server frame as CLC_AUTH
         * sees a valid leader slot. */
        if (g_Lobby.leaderSlot == 0xFF && cl->state >= CLSTATE_LOBBY) {
            g_Lobby.leaderSlot = count;
            sysLogPrintf(LOG_NOTE, "LOBBY: immediate leader assigned: slot %d (client %d, %s)",
                         count, i, cl->settings.name[0] ? cl->settings.name : "?");
        }

        /* Check if current leader is still present */
        if (g_Lobby.leaderSlot < LOBBY_MAX_PLAYERS &&
            g_Lobby.players[g_Lobby.leaderSlot].active &&
            g_Lobby.players[g_Lobby.leaderSlot].clientId == lp->clientId &&
            g_Lobby.leaderSlot == count) {
            currentLeaderFound = 1;
        }

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

    /* Leader election:
     * - Dedicated server: first client in CLSTATE_LOBBY+ becomes leader.
     * - Client: the server tells us who the leader is via lobby state.
     *   For now, we use the same logic client-side (first connected player). */
    if (!currentLeaderFound || g_Lobby.leaderSlot >= count) {
        /* Leader disconnected or invalid — elect a new one */
        if (firstLobbySlot != 0xFF && firstLobbySlot < count) {
            g_Lobby.leaderSlot = firstLobbySlot;
            sysLogPrintf(LOG_NOTE, "LOBBY: leader elected: slot %d (%s)",
                         firstLobbySlot, g_Lobby.players[firstLobbySlot].name);
        } else if (count > 0) {
            /* No one in LOBBY state yet — assign first connected client */
            g_Lobby.leaderSlot = 0;
        } else {
            g_Lobby.leaderSlot = 0xFF;
        }
    }

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
    if (g_NetMode == NETMODE_NONE || !g_NetLocalClient) {
        return 1; /* Offline = always leader */
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
