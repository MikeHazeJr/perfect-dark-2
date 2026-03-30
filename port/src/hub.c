/**
 * hub.c -- Server hub lifecycle.
 *
 * The hub is a thin layer that owns:
 *   - The room array (via room.h)
 *   - The player identity (via identity.h)
 *   - The hub-level state machine (LOUNGE / ACTIVE)
 *
 * hubTick() runs every server frame and performs two jobs:
 *   1. Syncs room 0's lifecycle state from g_Lobby.inGame so the existing
 *      single-match path remains backward compatible without any changes to
 *      net.c or netlobby.c.
 *   2. Derives the hub state from the aggregate of all room states.
 */

#include <SDL.h>
#include "hub.h"
#include "room.h"
#include "identity.h"
#include "system.h"
#include "types.h"
#include "net/net.h"
#include "net/netlobby.h"

/* g_MpSetup — defined in game bss (client) or server_stubs.c (dedicated server).
 * We only read timelimit, which is set by netmsgClcLobbyStartRead before the match
 * starts, so the value is stable for the duration of a match. */
extern struct mpsetup g_MpSetup;

/* -------------------------------------------------------------------------
 * Module state
 * ------------------------------------------------------------------------- */

static hub_state_t s_HubState   = HUB_STATE_LOUNGE;
static int         s_Initialised = 0;

/* Track the last known inGame value so we only log on transitions. */
static int s_LastInGame = 0;

/* Server-side match timer.  Dedicated servers don't run lv.c, so the engine's
 * built-in timer never fires.  We track wall-clock time here and send
 * SVC_STAGE_END when the configured timelimit expires. */
static u32  s_MatchStartMs = 0;
static bool s_MatchEndSent = false;

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

static hub_state_t deriveHubState(void)
{
    s32 active = roomGetActiveCount();
    if (active == 0) return HUB_STATE_LOUNGE;

    for (s32 i = 0; i < active; i++) {
        hub_room_t *r = roomGetByIndex(i);
        if (!r) continue;
        if (r->state == ROOM_STATE_LOADING ||
            r->state == ROOM_STATE_MATCH   ||
            r->state == ROOM_STATE_POSTGAME) {
            return HUB_STATE_ACTIVE;
        }
    }
    return HUB_STATE_LOUNGE;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void hubInit(void)
{
    if (s_Initialised) return;

    roomsInit();
    identityInit();

    s_HubState    = HUB_STATE_LOUNGE;
    s_LastInGame  = 0;
    s_Initialised = 1;

    sysLogPrintf(LOG_NOTE, "HUB: initialised, state=LOUNGE");
}

void hubTick(void)
{
    if (!s_Initialised) return;
    /* Sync room 0 state from the existing lobby flag. */
    int inGame = (int)g_Lobby.inGame;
    hub_room_t *room0 = roomGetById(0);

    if (room0) {
        if (inGame && !s_LastInGame) {
            /* Match just started. */
            roomTransition(room0, ROOM_STATE_MATCH);
            s_MatchStartMs = SDL_GetTicks();
            s_MatchEndSent = false;
        } else if (!inGame && s_LastInGame) {
            /* Match just ended — move to Postgame briefly, then back to Lobby
             * on the next tick (one-frame Postgame marks the transition). */
            if (room0->state == ROOM_STATE_MATCH) {
                roomTransition(room0, ROOM_STATE_POSTGAME);
            }
        } else if (!inGame && room0->state == ROOM_STATE_POSTGAME) {
            /* Postgame: return to lobby after one tick. */
            roomTransition(room0, ROOM_STATE_LOBBY);
        }
    }
    s_LastInGame = inGame;

    /* Server-side match timer: send SVC_STAGE_END when the timelimit expires.
     * Only runs on the dedicated server (g_NetMode == NETMODE_SERVER) while a
     * match is active.  timelimit >= 60 means no time limit. */
    if (g_NetMode == NETMODE_SERVER && inGame && !s_MatchEndSent) {
        u8 tl = g_MpSetup.timelimit;
        if (tl < 60) {
            u32 limitMs = (u32)(tl + 1u) * 60u * 1000u;
            u32 elapsed = SDL_GetTicks() - s_MatchStartMs;
            if (elapsed >= limitMs) {
                sysLogPrintf(LOG_NOTE, "HUB: timelimit expired (tl=%u, elapsed=%ums), ending match",
                             (unsigned)tl, (unsigned)elapsed);
                netServerStageEnd();
                s_MatchEndSent = true;
            }
        }
    }

    /* Recompute hub state. */
    hub_state_t newState = deriveHubState();
    if (newState != s_HubState) {
        sysLogPrintf(LOG_NOTE, "HUB: state %s -> %s",
                     hubGetStateName(s_HubState),
                     hubGetStateName(newState));
        s_HubState = newState;
    }
}

void hubShutdown(void)
{
    if (!s_Initialised) return;
    sysLogPrintf(LOG_NOTE, "HUB: shutting down");
    s_Initialised = 0;
}

hub_state_t hubGetState(void)
{
    return s_HubState;
}

const char *hubGetStateName(hub_state_t state)
{
    switch (state) {
        case HUB_STATE_LOUNGE: return "Lounge";
        case HUB_STATE_ACTIVE: return "Active";
        default:               return "Unknown";
    }
}

/* -------------------------------------------------------------------------
 * Slot pool API
 * ------------------------------------------------------------------------- */

s32 hubGetMaxSlots(void)
{
    return g_NetMaxClients;
}

void hubSetMaxSlots(s32 max)
{
    if (max < 1) max = 1;
    if (max > NET_MAX_CLIENTS) max = NET_MAX_CLIENTS;
    g_NetMaxClients = max;
}

s32 hubGetUsedSlots(void)
{
    return g_NetNumClients;
}

s32 hubGetFreeSlots(void)
{
    return g_NetMaxClients - g_NetNumClients;
}