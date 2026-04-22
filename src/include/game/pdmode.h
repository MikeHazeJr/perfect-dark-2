#ifndef _IN_GAME_PDMODE_H
#define _IN_GAME_PDMODE_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

f32 pdmodeGetEnemyReactionSpeed(void);
f32 pdmodeGetEnemyHealth(void);
f32 pdmodeGetEnemyDamage(void);
f32 pdmodeGetEnemyAccuracy(void);
void func0f01b148(u32 arg0);
void titleSetNextStage(s32 stagenum);

/**
 * Coerce invalid load-time stagenum values (notably 0x00) to a safe default.
 * Used at every boundary that feeds stage load (title queue, lvReset,
 * pending mainChangeToStage) so bad script/death paths cannot crash bg/setup.
 */
s32 stageSanitizeLoadStagenum(s32 stagenum);

#endif
