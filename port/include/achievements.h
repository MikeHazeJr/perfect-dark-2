/**
 * achievements.h -- Data-driven achievement/challenge query layer (D6 Phase 2)
 *
 * Achievements are defined as data: each has a stat key, condition type,
 * and threshold.  achievementCheck() queries the persistent stats system
 * to determine if the condition is met.
 *
 * Auto-discovered by CMake GLOB_RECURSE.
 */

#ifndef ACHIEVEMENTS_H
#define ACHIEVEMENTS_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Condition types */
#define ACHIEVE_GE  0  /* stat >= threshold */
#define ACHIEVE_EQ  1  /* stat == threshold */
#define ACHIEVE_GT  2  /* stat > threshold */

/* Maximum achievements */
#define ACHIEVE_MAX 64

typedef struct {
    const char *id;           /* unique identifier */
    const char *name;         /* display name */
    const char *description;  /* description text */
    s32 condition;            /* ACHIEVE_GE, ACHIEVE_EQ, ACHIEVE_GT */
    u64 threshold;            /* value to compare against */
    const char *stat_key;     /* playerstats key to query */
} achievement_def_t;

/** Initialize the achievement system. Call after statsInit(). */
void achievementsInit(void);

/** Check if a specific achievement is unlocked. */
s32 achievementIsUnlocked(const char *id);

/** Get the number of defined achievements. */
s32 achievementGetCount(void);

/** Get achievement definition by index (for UI enumeration). */
const achievement_def_t *achievementGetByIndex(s32 idx);

/** Get the number of newly unlocked achievements since last check.
 *  Calling this clears the "new" flag. */
s32 achievementGetNewlyUnlocked(const char **ids_out, s32 max_out);

/** Refresh achievement state (call after a match ends). */
void achievementsRefresh(void);

#ifdef __cplusplus
}
#endif

#endif /* ACHIEVEMENTS_H */
