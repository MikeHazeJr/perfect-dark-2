#ifndef IN_GAME_MPSTATS_H
#define IN_GAME_MPSTATS_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

void mpstatsIncrementPlayerShotCount(struct gset *gset, s32 region);
void mpstatsIncrementPlayerShotCount2(struct gset *gset, s32 region);
void mpstats0f0b0520(void);
s32 mpstatsGetPlayerShotCountByRegion(u32 type);
void mpstatsIncrementTotalKillCount(void);
void mpstatsIncrementTotalKnockoutCount(void);
void mpstatsDecrementTotalKnockoutCount(void);
u8 mpstatsGetTotalKnockoutCount(void);
u32 mpstatsGetTotalKillCount(void);
void mpstatsRecordPlayerKill(void);
s32 mpstatsGetPlayerKillCount(void);
void mpstatsRecordPlayerDeath(void);
void mpstatsRecordPlayerSuicide(void);
/* Both arguments are compact g_MpAllChrPtrs runtime-roster indices. */
void mpstatsRecordDeathByRuntimeIndex(s32 attacker_runtime_index,
	s32 victim_runtime_index);

#endif
