/**
 * netlobby.c -- Network lobby state management.
 *
 * Syncs lobby state from the network client array each frame.
 * Tracks player slots, leader assignment, and ready states.
 */

#include <PR/ultratypes.h>
#include <string.h>
#include "types.h"
#include "constants.h"
#include "net/net.h"
#include "net/netlobby.h"
#include "system.h"

struct lobbystate g_Lobby;

void lobbyInit(void)
{
    memset(&g_Lobby, 0, sizeof(g_Lobby));
    g_Lobby.leaderSlot = 0;
    g_Lobby.settings.scenario = 0;
    g_Lobby.settings.stagenum = 0;
    g_Lobby.settings.numSimulants = 0;
    sysLogPrintf(LOG_NOTE, "LOBBY: initialized");
}

void lobbyUpdate(void)
{
    if (g_NetMode == NETMODE_NONE) {
        return;
    }

    /* Sync player list from network clients */
    u8 count = 0;
    u8 leaderFound = 0;

    for (s32 i = 0; i <= NET_MAX_CLIENTS; i++) {
        struct netclient *cl = &g_NetClients[i];
        if (cl->state == CLSTATE_DISCONNECTED) {
            continue;
        }

        if (count >= LOBBY_MAX_PLAYERS) {
            break;
        }

        struct lobbyplayer *lp = &g_Lobby.players[count];
        lp->active = 1;
        lp->clientId = (u8)i;
        lp->headnum = cl->settings.headnum;
        lp->bodynum = cl->settings.bodynum;
        lp->team = cl->settings.team;
        strncpy(lp->name, cl->settings.name, LOBBY_NAME_LEN - 1);
        lp->name[LOBBY_NAME_LEN - 1] = '\0';

        /* Leader is the first connected player (server = client 0) */
        if (!leaderFound) {
            if (g_NetMode == NETMODE_SERVER && !g_NetDedicated && i == 0) {
                /* Non-dedicated server: host is always leader */
                lp->isLeader = 1;
                g_Lobby.leaderSlot = count;
                leaderFound = 1;
            } else if (g_NetDedicated && cl->state >= CLSTATE_LOBBY && i > 0) {
                /* Dedicated server: first non-server client is leader */
                if (g_Lobby.leaderSlot == 0 || !g_Lobby.players[g_Lobby.leaderSlot].active) {
                    lp->isLeader = 1;
                    g_Lobby.leaderSlot = count;
                    leaderFound = 1;
                }
            } else if (g_NetMode == NETMODE_CLIENT) {
                /* Client: leader info comes from server (for now, slot 0) */
                if (count == 0) {
                    lp->isLeader = 1;
                    g_Lobby.leaderSlot = 0;
                    leaderFound = 1;
                }
            }
        } else {
            lp->isLeader = (count == g_Lobby.leaderSlot) ? 1 : 0;
        }

        /* Ready state: in-game means ready */
        lp->isReady = (cl->state >= CLSTATE_GAME) ? 1 : 0;

        count++;
    }

    /* Clear remaining slots */
    for (s32 i = count; i < LOBBY_MAX_PLAYERS; i++) {
        memset(&g_Lobby.players[i], 0, sizeof(struct lobbyplayer));
    }

    g_Lobby.numPlayers = count;
    g_Lobby.inGame = (g_NetLocalClient && g_NetLocalClient->state >= CLSTATE_GAME) ? 1 : 0;
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
