/**
 * playerstats.h -- Granular player statistics tracking
 *
 * Tracks every countable gameplay event as a named stat counter.
 * Stats are accumulated locally and synced to the server when connected.
 * Achievements are a query layer on top -- not implemented here.
 *
 * Design:
 *   - Every stat is a named counter (string key -> u64 value)
 *   - Stats are incremented by gameplay systems via statIncrement()
 *   - Persisted to disk in JSON format
 *   - Categories: kills, deaths, weapons, accuracy, modes, maps, time
 *
 * Usage:
 *   statIncrement("kills.total", 1);
 *   statIncrement("kills.weapon.falcon2", 1);
 *   statIncrement("kills.vs_sim.perfect", 1);
 *   statIncrement("deaths.total", 1);
 *   statIncrement("shots.fired.falcon2", 1);
 *   statIncrement("shots.hit.falcon2", 1);
 *   statIncrement("time.played_ms", deltaMs);
 *   statIncrement("matches.won", 1);
 */

#ifndef PLAYERSTATS_H
#define PLAYERSTATS_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the stats system. Loads stats from disk if available.
 * Call once at startup after config/save system init.
 */
void statsInit(void);

/**
 * Shut down the stats system. Saves to disk.
 */
void statsShutdown(void);

/**
 * Increment a named stat counter by the given amount.
 * Creates the stat if it doesn't exist yet.
 * This is the primary interface -- call from anywhere in gameplay code.
 */
void statIncrement(const char *key, u64 amount);

/**
 * Set a named stat to a specific value (for high-water-mark stats like
 * "longest_life_seconds" or "most_kills_single_match").
 * Only updates if newval > current value.
 */
void statSetMax(const char *key, u64 newval);

/**
 * Get the current value of a named stat.
 * Returns 0 if the stat doesn't exist.
 */
u64 statGet(const char *key);

/**
 * Save all stats to disk. Called automatically on shutdown,
 * but can be called manually (e.g., after a match ends).
 */
void statsSave(void);

/**
 * Get the total number of tracked stats (for UI enumeration).
 */
s32 statsGetCount(void);

/**
 * Get a stat by index (for UI enumeration).
 * Returns the key name and value. Returns NULL key if index out of range.
 */
const char *statsGetKeyByIndex(s32 idx);
u64 statsGetValueByIndex(s32 idx);

#ifdef __cplusplus
}
#endif

#endif /* PLAYERSTATS_H */
