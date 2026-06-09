#ifndef _IN_SCENARIO_SCENE_RENDERER_H
#define _IN_SCENARIO_SCENE_RENDERER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct scenario_scene_renderer_probe {
	size_t vertices;
	size_t groups;
	size_t materials;
	size_t images;
	size_t alpha_textures;
	size_t alpha_materials;
	size_t secondary_materials;
} scenario_scene_renderer_probe_t;

int scenarioSceneRendererProbeSource(const char *scenario_id,
	const char *scene_path, scenario_scene_renderer_probe_t *out_probe);
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
