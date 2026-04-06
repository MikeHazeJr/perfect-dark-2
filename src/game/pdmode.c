#include <ultra64.h>
#include "constants.h"
#include "game/pdmode.h"
#include "game/bondgun.h"
#include "game/game_0b0fd0.h"
#include "game/inv.h"
#include "game/playermgr.h"
#include "game/options.h"
#include "bss.h"
#include "data.h"
#include "types.h"
#include "system.h"

f32 pdmodeGetEnemyReactionSpeed(void)
{
	return 0;
}

f32 pdmodeGetEnemyHealth(void)
{
	if (g_MissionConfig.pdmode) {
		return g_MissionConfig.pdmodehealthf;
	}

	return 1;
}

f32 pdmodeGetEnemyDamage(void)
{
	if (g_MissionConfig.pdmode) {
		return g_MissionConfig.pdmodedamagef;
	}

	return 1;
}

f32 pdmodeGetEnemyAccuracy(void)
{
	if (g_MissionConfig.pdmode) {
		return g_MissionConfig.pdmodeaccuracyf;
	}

	return 1;
}

void func0f01b148(u32 arg0)
{
	var800624e0 = arg0;
}

void titleSetNextStage(s32 stagenum)
{
	sysLogPrintf(LOG_NOTE, "STAGE: titleSetNextStage(0x%02x)", stagenum);

	/* FIX-PLAYTEST-3: Guard against invalid stagenum 0x00.
	 * Death-in-hub (e.g. falling through CI geometry) can trigger a stage
	 * transition with stagenum=0 which is not a valid stage — bgGetStageIndex
	 * returns -1 and the loader crashes on garbage data.  Redirect to CI. */
	if (stagenum == 0) {
		sysLogPrintf(LOG_WARNING, "STAGE: titleSetNextStage(0x00) is invalid — redirecting to CI (0x26)");
		stagenum = STAGE_CITRAINING;
	}

	g_TitleNextStage = stagenum;
}
