/**
 * scenario_save.h -- Shared match-config types and scenario save/load API.
 *
 * This header is the single source of truth for:
 *   - struct matchslot / struct matchconfig
 *   - Constants (MATCH_MAX_SLOTS, SLOT_*, NUM_MPWEAPONSLOTS, MAX_PLAYER_NAME)
 *   - matchConfig* function declarations (matchsetup.c)
 *   - scenarioSave / scenarioLoad / scenarioListFiles declarations
 *
 * IMPORTANT: This file must be safe to include from both C and C++ code.
 *   - Do NOT include types.h (defines bool as s32, breaks C++)
 *   - PR/ultratypes.h (u8/u16/u32/s8/s32) is safe for both.
 *
 * C++ callers must include this inside extern "C" { }.
 */

#ifndef SCENARIO_SAVE_H
#define SCENARIO_SAVE_H

#include <PR/ultratypes.h>

/* ========================================================================
 * Match slot constants
 * ======================================================================== */

#ifndef MAX_PLAYER_NAME
#define MAX_PLAYER_NAME   32
#endif

/* NUM_MPWEAPONSLOTS is also defined in src/include/constants.h (game code).
 * Guard to avoid redefinition when included from C files that already
 * pulled in constants.h. */
#ifndef NUM_MPWEAPONSLOTS
#define NUM_MPWEAPONSLOTS 6
#endif

/* MATCH_MAX_SLOTS: total participant pool (players + bots).
 * Mirrors PARTICIPANT_DEFAULT_CAPACITY = 32.
 * C code should #include "game/mplayer/participant.h" and define
 * MATCH_MAX_SLOTS as PARTICIPANT_DEFAULT_CAPACITY before this header
 * if stricter compile-time coupling is desired; otherwise 32 is used. */
#ifndef MATCH_MAX_SLOTS
#define MATCH_MAX_SLOTS 32
#endif

#ifndef SLOT_EMPTY
#define SLOT_EMPTY  0
#define SLOT_PLAYER 1
#define SLOT_BOT    2
#endif

/* ========================================================================
 * Match config structs — canonical definitions live in net/matchsetup.h.
 * Include it here so callers only need scenario_save.h.
 * ======================================================================== */

#include "net/matchsetup.h"

/* ========================================================================
 * Scenario save/load API
 * ======================================================================== */

#define SCENARIO_PATH_MAX 1024
#define SCENARIO_MAX_LIST  64

/**
 * scenarioSave -- Save current g_MatchConfig to $S/scenarios/<name>.json.
 *
 * Creates $S/scenarios/ if it does not exist.
 * Sanitizes <name> for use as a filename (strips path chars).
 * Saves: arena, scenario, limits, options, weaponset, and all bot slots.
 * Human player slots are NOT saved (they are session-specific).
 *
 * Returns 0 on success, -1 on error.
 */
s32 scenarioSave(const char *name);

/**
 * scenarioLoad -- Load from file at filepath into g_MatchConfig.
 *
 * Calls matchConfigInit() first (resets to local player + default settings).
 * Applies saved arena, scenario, limits, options, and weapon set.
 * Adds bots from the file up to (MATCH_MAX_SLOTS - humanCount);
 * excess bots are silently dropped.
 * Bots are loaded in order: saved bot 0 first, bot 1 second, etc.
 *
 * humanCount: number of human players currently in the room (>=1).
 *
 * Returns 0 on success, -1 on error.
 */
s32 scenarioLoad(const char *filepath, s32 humanCount);

/**
 * scenarioListFiles -- List .json files in $S/scenarios/.
 *
 * Fills outPaths with full file paths (each SCENARIO_PATH_MAX chars).
 * Returns the number of files found (0 if directory does not exist).
 */
s32 scenarioListFiles(char (*outPaths)[SCENARIO_PATH_MAX], s32 maxCount);

#endif /* SCENARIO_SAVE_H */
