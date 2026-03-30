/**
 * hub.h -- Server hub lifecycle.
 *
 * The hub is the server's top-level container.  Every dedicated server IS
 * a hub.  The hub owns the room array and reflects the overall match state:
 *
 *   LOUNGE  -- server running, all rooms in Lobby (no match active)
 *   ACTIVE  -- at least one room is in Loading, Match, or Postgame
 *
 * hubTick() should be called every server frame, after lobbyUpdate().
 * It syncs room 0's state with the existing g_Lobby.inGame flag so the
 * original single-match flow stays backward compatible.
 */

#ifndef _IN_HUB_H
#define _IN_HUB_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Hub state
 * ------------------------------------------------------------------------- */

typedef enum {
    HUB_STATE_LOUNGE = 0,  /**< No match running; server in waiting state. */
    HUB_STATE_ACTIVE = 1,  /**< At least one room is Loading/Match/Postgame.*/
} hub_state_t;

/* -------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

/**
 * Initialise the hub.  Calls roomsInit() and identityInit() internally.
 * Must be called after netInit() and lobbyInit().
 */
void hubInit(void);

/**
 * Tick the hub once per server frame.
 * Syncs room 0 state from the lobby, updates hub_state.
 */
void hubTick(void);

/** Clean up hub resources on server shutdown. */
void hubShutdown(void);

/** Return the current hub state. */
hub_state_t hubGetState(void);

/** Get/set the total player slot pool for the server.
 *  Rooms allocate slots from this pool. Default: 32. */
s32 hubGetMaxSlots(void);
void hubSetMaxSlots(s32 maxSlots);

/** Get the number of currently allocated (in-use) slots across all rooms. */
s32 hubGetUsedSlots(void);

/** Get the number of available (free) slots. */
s32 hubGetFreeSlots(void);

/** Human-readable hub state name. */
const char *hubGetStateName(hub_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* _IN_HUB_H */
