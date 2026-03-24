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

    /* isLocal (s32 at offset 40, aligned after name[32]) — always 0 on dedicated server */
    s32 isLocal = 0;
    memcpy(p + 40, &isLocal, sizeof(s32));

    /* state (s32 at offset 44) */
    s32 state = g_NetClients[lp->clientId].state;
    memcpy(p + 44, &state, sizeof(s32));

    return 1;
}

/* ========================================================================
 * Network utility bridge functions
 * ======================================================================== */

u32 netGetClientPing(s32 clientId)
{
    if (clientId < 0 || clientId > NET_MAX_CLIENTS) return 0;
    struct netclient *cl = &g_NetClients[clientId];
    if (cl->state == CLSTATE_DISCONNECTED || !cl->peer) return 0;
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
