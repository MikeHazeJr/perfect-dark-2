#ifndef PD_TESTS_MPCONFIG_TABLE_PROBE_H
#define PD_TESTS_MPCONFIG_TABLE_PROBE_H

/*
 * tests/support/mpconfig_table_probe.h -- reads src/game/mpconfigs.c for the
 * C++ test runner, which cannot include types.h (its bool macro breaks C++).
 * Plain s32 accessors only (SP-75: no bool in declarations C and C++ share).
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Game constants the test compares against, read from constants.h. */
s32 mpconfigProbeNumConfigs(void);    /* MPCONFIG_CHALLENGE30 + 1 */
s32 mpconfigProbeMaxBots(void);
s32 mpconfigProbeMaxPlayers(void);
s32 mpconfigProbeNumWeaponSlots(void);
s32 mpconfigProbeConst(const char *name); /* a named constant, or -1 */

/* One config's setup. */
s32 mpconfigProbeStagenum(s32 config);
s32 mpconfigProbeStageIdEmpty(s32 config);
s32 mpconfigProbeScenario(s32 config);
s32 mpconfigProbeTimelimit(s32 config);
s32 mpconfigProbeScorelimit(s32 config);
s32 mpconfigProbeTeamscorelimit(s32 config);
u32 mpconfigProbeOptions(s32 config);
s32 mpconfigProbeWeapon(s32 config, s32 slot);

/* One simulant slot of a config. */
s32 mpconfigProbeSimType(s32 config, s32 sim);
s32 mpconfigProbeSimHead(s32 config, s32 sim);
s32 mpconfigProbeSimBody(s32 config, s32 sim);
s32 mpconfigProbeSimTeam(s32 config, s32 sim);
s32 mpconfigProbeSimHeadIdEmpty(s32 config, s32 sim);
s32 mpconfigProbeSimDifficulty(s32 config, s32 sim, s32 playercountindex);

#ifdef __cplusplus
}
#endif

#endif
