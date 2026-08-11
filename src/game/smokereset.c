#include <ultra64.h>
#include "constants.h"
#include "bss.h"
#include "game/smoke.h"
#include "data.h"
#include "types.h"

void smokeReset(void)
{
	g_MaxSmokes = 20;

	if (STAGE_IS_SYSTEM(g_Vars.stagenum)) {
		g_MaxSmokes = 0;
	}

	if (g_MaxSmokes == 0) {
		g_Smokes = NULL;
	} else {
		/* PC pointer-stable growable pool; initialization is owned by smoke.c. */
		smokesSetupPool(g_MaxSmokes);
	}
}
