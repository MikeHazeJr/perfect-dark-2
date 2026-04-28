/*
 * scene_transition.h -- narrow stage-transition cleanup substrate.
 *
 * This is deliberately smaller than the full Scene Manager. It centralizes
 * cleanup steps that must happen around a stage change so callsites stop
 * open-coding manifest/menu ordering.
 */

#ifndef _IN_SCENE_TRANSITION_H
#define _IN_SCENE_TRANSITION_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCENE_STAGE_TRANSITION_CLEAR_CLIENT_MANIFEST 0x01u
#define SCENE_STAGE_TRANSITION_RELEASE_MENU_POOL     0x02u
#define SCENE_STAGE_TRANSITION_DISCONNECT            0x04u

void sceneStageTransitionPrepare(u32 flags, const char *reason);
void sceneStageChangeTo(s32 stagenum, u32 flags, const char *reason);

#ifdef __cplusplus
}
#endif

#endif /* _IN_SCENE_TRANSITION_H */
