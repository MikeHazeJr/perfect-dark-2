/**
 * room.c -- Hub room lifecycle management.
 *
 * Room 0 is created by roomsInit() and represents the server's primary
 * (and currently only) match slot.  Its state is kept in sync with the
 * existing lobby / net layer by hubTick() in hub.c.
 *
 * All rooms share a fixed pool of HUB_MAX_ROOMS slots.  A slot is in use
 * when state != ROOM_STATE_CLOSED.  Room 0 resets to LOBBY on roomDestroy
 * rather than closing, preserving the primary slot permanently.
 */

#include "room.h"
#include "system.h"

#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Module state
 * ------------------------------------------------------------------------- */

static hub_room_t s_Rooms[HUB_MAX_ROOMS];
static int        s_Initialised = 0;

/* External: current network tick (from net.h / net.c) */
extern u32 g_NetTick;

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static hub_room_t *findFreeSlot(void)
{
    for (int i = 0; i < HUB_MAX_ROOMS; i++) {
        if (s_Rooms[i].state == ROOM_STATE_CLOSED) return &s_Rooms[i];
    }
    return NULL;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void roomsInit(void)
{
    if (s_Initialised) return;

    memset(s_Rooms, 0, sizeof(s_Rooms));

    /* All slots start closed. */
    for (int i = 0; i < HUB_MAX_ROOMS; i++) {
        s_Rooms[i].id    = (u8)i;
        s_Rooms[i].state = ROOM_STATE_CLOSED;
    }

    /* Room 0 always exists as the primary match room. */
    s_Rooms[0].state            = ROOM_STATE_LOBBY;
    s_Rooms[0].created_tick     = 0;
    s_Rooms[0].state_enter_tick = 0;
    strncpy(s_Rooms[0].name, "Lounge", ROOM_NAME_MAX - 1);

    s_Initialised = 1;
    sysLogPrintf(LOG_NOTE, "HUB ROOM: subsystem initialised, room 0 open");
}

hub_room_t *roomCreate(const char *name)
{
    hub_room_t *r = findFreeSlot();
    if (!r) {
        sysLogPrintf(LOG_WARNING, "HUB ROOM: no free slots (max %d)", HUB_MAX_ROOMS);
        return NULL;
    }

    memset(r->clients, 0, sizeof(r->clients));
    r->client_count      = 0;
    r->stagenum          = 0;
    r->scenario          = 0;
    r->rng_seed          = 0;
    r->state             = ROOM_STATE_LOBBY;
    r->created_tick      = g_NetTick;
    r->state_enter_tick  = g_NetTick;

    strncpy(r->name, name ? name : "Room", ROOM_NAME_MAX - 1);
    r->name[ROOM_NAME_MAX - 1] = '\0';

    sysLogPrintf(LOG_NOTE, "HUB ROOM: created room %u \"%s\"", (unsigned)r->id, r->name);
    return r;
}

void roomDestroy(hub_room_t *room)
{
    if (!room) return;

    if (room->id == 0) {
        /* Room 0 is permanent — reset to lobby instead of closing. */
        roomTransition(room, ROOM_STATE_LOBBY);
        room->client_count = 0;
        sysLogPrintf(LOG_NOTE, "HUB ROOM: room 0 reset to lobby");
        return;
    }

    sysLogPrintf(LOG_NOTE, "HUB ROOM: closing room %u \"%s\"",
                 (unsigned)room->id, room->name);
    room->state        = ROOM_STATE_CLOSED;
    room->client_count = 0;
}

void roomTransition(hub_room_t *room, room_state_t state)
{
    if (!room) return;
    if (room->state == state) return;

    sysLogPrintf(LOG_NOTE, "HUB ROOM: room %u \"%s\" %s -> %s",
                 (unsigned)room->id, room->name,
                 roomStateName(room->state),
                 roomStateName(state));

    room->state            = state;
    room->state_enter_tick = g_NetTick;
}

hub_room_t *roomGetById(u8 id)
{
    if (id >= HUB_MAX_ROOMS) return NULL;
    if (s_Rooms[id].state == ROOM_STATE_CLOSED) return NULL;
    return &s_Rooms[id];
}

hub_room_t *roomGetByIndex(s32 idx)
{
    s32 count = 0;
    for (int i = 0; i < HUB_MAX_ROOMS; i++) {
        if (s_Rooms[i].state != ROOM_STATE_CLOSED) {
            if (count == idx) return &s_Rooms[i];
            count++;
        }
    }
    return NULL;
}

s32 roomGetActiveCount(void)
{
    s32 n = 0;
    for (int i = 0; i < HUB_MAX_ROOMS; i++) {
        if (s_Rooms[i].state != ROOM_STATE_CLOSED) n++;
    }
    return n;
}

const char *roomStateName(room_state_t state)
{
    switch (state) {
        case ROOM_STATE_LOBBY:    return "Lobby";
        case ROOM_STATE_LOADING:  return "Loading";
        case ROOM_STATE_MATCH:    return "Match";
        case ROOM_STATE_POSTGAME: return "Postgame";
        case ROOM_STATE_CLOSED:   return "Closed";
        default:                  return "?";
    }
}
