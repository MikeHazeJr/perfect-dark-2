/**
 * achievements.c -- Data-driven achievement query layer (D6 Phase 2)
 *
 * Each achievement is a condition on a persistent stat key.
 * achievementsRefresh() re-evaluates all conditions and tracks
 * which achievements became newly unlocked since the last refresh.
 *
 * Auto-discovered by CMake GLOB_RECURSE for port/src/*.c.
 */

#include "achievements.h"
#include "playerstats.h"
#include "system.h"
#include <string.h>

/* ---- Achievement definitions ---- */

static const achievement_def_t s_Achievements[] = {
    /* Combat milestones */
    { "first_blood",    "First Blood",    "Get your first kill",                ACHIEVE_GE, 1,    "kills.total" },
    { "centurion",      "Centurion",      "Get 100 kills",                     ACHIEVE_GE, 100,  "kills.total" },
    { "warlord",        "Warlord",        "Get 1000 kills",                    ACHIEVE_GE, 1000, "kills.total" },
    { "sharpshooter",   "Sharpshooter",   "Get 100 headshots",                ACHIEVE_GE, 100,  "shots.headshot" },
    { "deadeye",        "Deadeye",        "Get 500 headshots",                ACHIEVE_GE, 500,  "shots.headshot" },
    { "trigger_happy",  "Trigger Happy",  "Fire 10000 shots",                 ACHIEVE_GE, 10000,"shots.total" },

    /* Weapon specialist */
    { "falcon_fan",     "Falcon Fan",     "Get 50 kills with the Falcon 2",   ACHIEVE_GE, 50,   "kills.weapon.falcon2" },
    { "laptop_warrior", "Laptop Warrior", "Get 50 kills with the Laptop Gun", ACHIEVE_GE, 50,   "kills.weapon.laptopgun" },
    { "farsight_ace",   "Farsight Ace",   "Get 25 kills with the Farsight",   ACHIEVE_GE, 25,   "kills.weapon.farsight" },
    { "boom_expert",    "Boom Expert",    "Get 50 kills with explosives",     ACHIEVE_GE, 50,   "kills.weapon.rocketlauncher" },
    { "knife_master",   "Knife Master",   "Get 25 kills with the Combat Knife",ACHIEVE_GE, 25,  "kills.weapon.combatknife" },

    /* Survival */
    { "survivor",       "Survivor",       "Die 100 times",                    ACHIEVE_GE, 100,  "deaths.total" },
    { "bot_slayer",     "Bot Slayer",     "Kill 500 bots",                    ACHIEVE_GE, 500,  "kills.vs_bot" },
    { "pvp_veteran",    "PvP Veteran",    "Kill 100 players",                 ACHIEVE_GE, 100,  "kills.vs_player" },

    /* Mode variety */
    { "combat_regular", "Combat Regular", "Get 50 kills in Combat mode",      ACHIEVE_GE, 50,   "kills.mode.combat" },
    { "case_carrier",   "Case Carrier",   "Get 25 kills in Capture the Case", ACHIEVE_GE, 25,   "kills.mode.capture_case" },
};
#define NUM_ACHIEVEMENTS (s32)(sizeof(s_Achievements) / sizeof(s_Achievements[0]))

/* ---- Runtime state ---- */

static u8 s_Unlocked[ACHIEVE_MAX];       /* 1 if unlocked */
static u8 s_PrevUnlocked[ACHIEVE_MAX];   /* snapshot from last refresh */
static s32 s_Initialized = 0;

/* ---- Implementation ---- */

static s32 checkCondition(const achievement_def_t *a)
{
    u64 val = statGet(a->stat_key);
    switch (a->condition) {
        case ACHIEVE_GE: return val >= a->threshold;
        case ACHIEVE_EQ: return val == a->threshold;
        case ACHIEVE_GT: return val >  a->threshold;
        default:         return 0;
    }
}

void achievementsInit(void)
{
    s32 i;
    memset(s_Unlocked, 0, sizeof(s_Unlocked));
    memset(s_PrevUnlocked, 0, sizeof(s_PrevUnlocked));

    for (i = 0; i < NUM_ACHIEVEMENTS && i < ACHIEVE_MAX; i++) {
        s_Unlocked[i] = checkCondition(&s_Achievements[i]) ? 1 : 0;
        s_PrevUnlocked[i] = s_Unlocked[i];
    }

    s_Initialized = 1;

    s32 count = 0;
    for (i = 0; i < NUM_ACHIEVEMENTS; i++) {
        if (s_Unlocked[i]) count++;
    }
    sysLogPrintf(LOG_NOTE, "ACHIEVEMENTS: initialized — %d/%d unlocked", count, NUM_ACHIEVEMENTS);
}

s32 achievementIsUnlocked(const char *id)
{
    s32 i;
    if (!id) return 0;
    for (i = 0; i < NUM_ACHIEVEMENTS; i++) {
        if (strcmp(s_Achievements[i].id, id) == 0) {
            return s_Unlocked[i];
        }
    }
    return 0;
}

s32 achievementGetCount(void)
{
    return NUM_ACHIEVEMENTS;
}

const achievement_def_t *achievementGetByIndex(s32 idx)
{
    if (idx < 0 || idx >= NUM_ACHIEVEMENTS) return NULL;
    return &s_Achievements[idx];
}

void achievementsRefresh(void)
{
    s32 i;
    if (!s_Initialized) return;

    /* Snapshot previous state */
    memcpy(s_PrevUnlocked, s_Unlocked, sizeof(s_PrevUnlocked));

    /* Re-evaluate all */
    for (i = 0; i < NUM_ACHIEVEMENTS && i < ACHIEVE_MAX; i++) {
        s_Unlocked[i] = checkCondition(&s_Achievements[i]) ? 1 : 0;
    }
}

s32 achievementGetNewlyUnlocked(const char **ids_out, s32 max_out)
{
    s32 i;
    s32 count = 0;
    for (i = 0; i < NUM_ACHIEVEMENTS && count < max_out; i++) {
        if (s_Unlocked[i] && !s_PrevUnlocked[i]) {
            if (ids_out) {
                ids_out[count] = s_Achievements[i].id;
            }
            count++;
        }
    }

    /* Clear "new" state by syncing snapshots */
    memcpy(s_PrevUnlocked, s_Unlocked, sizeof(s_PrevUnlocked));
    return count;
}
