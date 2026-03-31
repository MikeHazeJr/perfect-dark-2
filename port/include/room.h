/**
 * room.h -- Hub room system.
 *
 * A room is a match container with a 5-state lifecycle:
 *
 *   LOBBY  -->  LOADING  -->  MATCH  -->  POSTGAME  -->  CLOSED
 *     ^                                       |
 *     +---------------------------------------+  (rematch)
 *
 * The server always has at least one room (room 0).  Room 0 is the
 * backward-compatible wrapper around the existing single-match lifecycle.
 * Future sessions can open additional rooms (up to HUB_MAX_ROOMS).
 *
 * Rooms do not own ENet peers or game state — they track which clients
 * are assigned to them and what phase the match is in.  The authoritative
 * state lives in g_NetClients / g_Lobby as before.
 */

#ifndef _IN_ROOM_H
#define _IN_ROOM_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define HUB_MAX_ROOMS    4
#define HUB_MAX_CLIENTS  32  /* must match NET_MAX_CLIENTS in port/include/net/net.h */
#define ROOM_NAME_MAX   32

/* -------------------------------------------------------------------------
 * Types
 * ------------------------------------------------------------------------- */

/** Room lifecycle state machine.
 *
 *  LOBBY --> PREPARING --> LOADING --> MATCH --> POSTGAME --> CLOSED
 *    ^            |                                  |
 *    |            | (manifest + ready gate)          |
 *    +------------+----------------------------------+  (rematch / cancel)
 *
 *  PREPARING is entered when the leader triggers match start.  The server
 *  broadcasts SVC_MATCH_MANIFEST; all clients must respond READY (or DECLINE)
 *  before the room advances to LOADING.  Clients that DECLINE stay in
 *  CLSTATE_LOBBY as spectators.
 */
typedef enum {
    ROOM_STATE_LOBBY     = 0, /**< Waiting for players / match not started.          */
    ROOM_STATE_LOADING   = 1, /**< Stage assets loading; SVC_STAGE_START sent.       */
    ROOM_STATE_MATCH     = 2, /**< Match in progress.                                */
    ROOM_STATE_POSTGAME  = 3, /**< Scoreboard / brief results phase.                 */
    ROOM_STATE_CLOSED    = 4, /**< Room destroyed, slot available.                   */
    ROOM_STATE_PREPARING = 5, /**< Manifest sent; waiting for all clients READY.     */
} room_state_t;

/** Room access mode. */
typedef enum {
    ROOM_ACCESS_OPEN     = 0, /**< Anyone on the server can join. */
    ROOM_ACCESS_PASSWORD = 1, /**< Requires a password to enter.  */
    ROOM_ACCESS_INVITE   = 2, /**< Invite-only, creator selects from player list. */
} room_access_t;

/** One room in the hub. */
typedef struct hub_room_s {
    u8           id;                          /**< Unique room index (0-3). */
    room_state_t state;
    char         name[ROOM_NAME_MAX];

    u8           clients[HUB_MAX_CLIENTS];    /**< clientId of each member. */
    u8           client_count;
    u8           max_players;                 /**< Slots allocated from server pool. */

    u8           stagenum;
    u8           scenario;
    u32          rng_seed;

    room_access_t access;                     /**< Open, password, or invite-only. */
    char         password[32];                /**< Password if access == PASSWORD. */
    u8           creator_client_id;           /**< Client who created this room. */

    u32          created_tick;                /**< g_NetTick when created.  */
    u32          state_enter_tick;            /**< g_NetTick of last transition. */
} hub_room_t;

/* -------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

/** Initialise the room subsystem and create room 0. */
void roomsInit(void);

/**
 * Create a new room with the given name and slot count.
 * Allocates playerSlots from the server's slot pool.
 * Returns the room pointer, or NULL if no room slots or player slots available.
 */
hub_room_t *roomCreate(const char *name);

/**
 * Create a room with full configuration (name, max players, access mode, password).
 * Returns the room pointer, or NULL if no room slots or player slots available.
 */
hub_room_t *roomCreateConfigured(const char *name, u8 maxPlayers,
                                  room_access_t access, const char *password,
                                  u8 creatorClientId);

/**
 * Generate a random room name (adjective + noun).
 * Examples: "Fat Monkey", "Purple Dinosaur", "Jumpy Van"
 * Writes into buf (at least 64 bytes).
 */
void roomGenerateName(char *buf, s32 bufsize);

/** Destroy a room and free its slot.  Room 0 is never truly destroyed --
 *  transitions to ROOM_STATE_LOBBY instead. */
void roomDestroy(hub_room_t *room);

/** Transition a room to a new state and record the tick. */
void roomTransition(hub_room_t *room, room_state_t state);

/** Look up a room by its id field (0-3). Returns NULL if not found. */
hub_room_t *roomGetById(u8 id);

/** Look up a room by sequential index (0 = first active room, etc.).
 *  Returns NULL if idx is out of range. */
hub_room_t *roomGetByIndex(s32 idx);

/** Total non-CLOSED room count. */
s32 roomGetActiveCount(void);

/** Human-readable name for a room_state_t value. */
const char *roomStateName(room_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* _IN_ROOM_H */
