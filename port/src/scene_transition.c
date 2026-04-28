/*
 * scene_transition.c -- shared cleanup helper for stage transitions.
 *
 * The current migration keeps existing stage-start logic in place. This
 * helper only centralizes the ordering-sensitive cleanup that callsites used
 * to duplicate by hand.
 */

#include "scene_transition.h"

#include <stddef.h>

#include "lib/main.h"
#include "net/netmanifest.h"
#include "system.h"

#if !defined(PD_SERVER)
#include "menupool.h"
#include "scene.h"
#endif

static const char *sceneStageTransitionReason(const char *reason)
{
	return reason ? reason : "(unspecified)";
}

void sceneStageTransitionPrepare(u32 flags, const char *reason)
{
	const char *label = sceneStageTransitionReason(reason);

	sysLogPrintf(LOG_NOTE,
		"TRANSITION.STAGE.PREP reason='%s' flags=0x%02x client_manifest=%u",
		label, (unsigned)flags, (unsigned)g_ClientManifest.num_entries);

	if (flags & SCENE_STAGE_TRANSITION_CLEAR_CLIENT_MANIFEST) {
		manifestClear(&g_ClientManifest);
	}

#if !defined(PD_SERVER)
	if (flags & SCENE_STAGE_TRANSITION_DISCONNECT) {
		sceneFire(SCENE_EVENT_DISCONNECT, NULL);
	}

	if (flags & SCENE_STAGE_TRANSITION_RELEASE_MENU_POOL) {
		menupoolReleaseAll();
	}
#endif
}

void sceneStageChangeTo(s32 stagenum, u32 flags, const char *reason)
{
	sceneStageTransitionPrepare(flags, reason);
	mainChangeToStage(stagenum);
}
