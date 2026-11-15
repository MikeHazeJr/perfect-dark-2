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
	size_t instances;
	int selected_scene;
	float bounds_min[3];
	float bounds_max[3];
	/* Emitted transformed triangle positions, xyz float32 little-endian. */
	char geometry_sha256[65];
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
/* B-1172: the scene draws where the display list asks (G_SCENE_RENDER_EXT,
 * gSPSceneRenderEXT) with the camera that was current when the command was
 * queued (its slot), into the caller's viewport. Slots reset after each frame. */
int scenarioSceneRendererCurrentCameraSlot(void);
void scenarioSceneRendererEndFrame(void);
void scenarioSceneRendererRenderSlot(int view_x, int view_y, int width, int height, int slot);

#ifdef __cplusplus
}
#endif

#endif
