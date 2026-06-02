#ifndef _IN_SCENARIO_SCENE_RENDERER_H
#define _IN_SCENARIO_SCENE_RENDERER_H

#ifdef __cplusplus
extern "C" {
#endif

int scenarioSceneRendererActivate(const char *scenario_id,
	const char *scene_path);
void scenarioSceneRendererDeactivate(void);
int scenarioSceneRendererIsActive(void);
void scenarioSceneRendererRender(float vp[4][4], int width, int height);

#ifdef __cplusplus
}
#endif

#endif
