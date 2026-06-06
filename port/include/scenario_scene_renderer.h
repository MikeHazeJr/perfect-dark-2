#ifndef _IN_SCENARIO_SCENE_RENDERER_H
#define _IN_SCENARIO_SCENE_RENDERER_H

#ifdef __cplusplus
extern "C" {
#endif

int scenarioSceneRendererActivate(const char *scenario_id,
	const char *scene_path);
void scenarioSceneRendererDeactivate(void);
int scenarioSceneRendererIsActive(void);
void scenarioSceneRendererSetCameraFrame(
	const float position[3],
	const float look[3],
	const float up[3],
	float fovy_degrees,
	float aspect,
	float znear,
	float zfar);
void scenarioSceneRendererRender(int width, int height);

#ifdef __cplusplus
}
#endif

#endif
