#ifndef _IN_SCENARIO_SCENE_RENDERER_H
#define _IN_SCENARIO_SCENE_RENDERER_H

#include <stddef.h>

#include "scenario_scene_camera.h"

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
/* B-1172: world_scale and draw_offset are the game's draw space for this
 * camera (scenario_scene_camera.h): the BG scale and the camera room's offset
 * this frame's matrices were built with. */
void scenarioSceneRendererSetCameraFrame(
	const float position[3],
	const float look[3],
	const float up[3],
	float fovy_degrees,
	float aspect,
	float znear,
	float zfar,
	float world_scale,
	const float draw_offset[3]);
void scenarioSceneRendererRender(int width, int height);
/* B-1172: the scene draws where the display list asks (G_SCENE_RENDER_EXT,
 * gSPSceneRenderEXT) with the camera that was current when the command was
 * queued (its slot), into the caller's viewport. Slots reset after each frame.
 *
 * Passes: the opaque pass (opaque and cutout materials) draws where the BG's
 * opaque rooms drew, before the props, and the blend pass (translucent
 * materials) where the BG's translucent room parts drew, after the props'
 * opaque pass. A world draw keeps its depth, in the game's depth space, so the
 * props that follow are hidden behind the scene's walls. A backdrop draw (the
 * held menu, the lock screen) clears depth after it: what follows it uses
 * cameras of its own. */
#define SCENARIO_SCENE_PASS_OPAQUE   0x1
#define SCENARIO_SCENE_PASS_BLEND    0x2
#define SCENARIO_SCENE_PASS_BACKDROP 0x4

int scenarioSceneRendererCurrentCameraSlot(void);
void scenarioSceneRendererEndFrame(void);
/* A slot's camera, or the last camera set for an unknown slot (backdrops).
 * Returns 0 until a camera frame has been accepted. */
int scenarioSceneRendererSlotCamera(int slot, scenario_scene_camera_t *out);
/* Draws the given passes with the given row-vector view-projection (NULL:
 * the last camera's) into the viewport. */
void scenarioSceneRendererRenderPasses(int view_x, int view_y, int width, int height,
	const float *view_projection, const float *sky_offset, int light_slot, int passes);
/* B-1172: the stage's sky rooms (rooms.json sky). The game draws them relative
 * to the camera's room (roomPopulateMtx), so the scene moves their vertices by
 * the camera's draw offset (sky_offset above: NULL means the last camera's). */
void scenarioSceneRendererSetSkyRooms(const int *rooms, int count);

/* B-1172: a room's lighting as roomHighlight (dlights.c) applies it to the
 * BG's vertex colours, for one player's view. The scene's shader applies the
 * same formula to its vertex colours. */
typedef struct scenario_scene_room_light {
	float brightness; /* settled regional brightness for the player, 0-255 */
	float flash;      /* the room's flash brightness, added on top */
	float tint[3];    /* the scenario's room highlight (King of the Hill, Capture the Case); 1 = none */
	int unlit;        /* render-always rooms keep their source colours */
	int flash_black;  /* lights off or no light transition: the flash brightens black vertices too */
	int greyscale;    /* night vision or the IR scanner */
} scenario_scene_room_light_t;

/* The lighting of every room (indexed by room number, count = the stage's
 * room count) for the camera set last, so each player's view keeps its own. */
void scenarioSceneRendererSetRoomLights(const scenario_scene_room_light_t *lights, int count);
/* For the --debug-scene-matrix-probe log: the scene's vertex positions. */
int scenarioSceneRendererVertexCount(void);
int scenarioSceneRendererVertexPosition(int index, float out[3]);

#ifdef __cplusplus
}
#endif

#endif
