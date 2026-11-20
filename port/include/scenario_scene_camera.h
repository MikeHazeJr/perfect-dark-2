#ifndef _IN_SCENARIO_SCENE_CAMERA_H
#define _IN_SCENARIO_SCENE_CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

/* B-1172: the scenario scene's camera in the game's draw space.
 *
 * The game's matrices do not work in world units. A world point p is drawn at
 * (p - draw_offset) * world_scale: draw_offset is the camera room's position
 * (playerSetGlobalDrawWorldOffset) and world_scale is the stage's BG scale
 * (bgGetScaleBg2Gfx; 0.5 on Villa, 1 on Defection). The eye of the BG
 * projection is the camera in those units (playerAllocateMatrices), mtxF2L
 * scales the x, y and z columns of every model matrix by the BG scale, and
 * the perspective's near and far planes are in those units (vi.c,
 * guPerspectiveF). Depth values therefore depend on the scale: a scene camera
 * built in world units with the same near and far planes draws the scene in a
 * different depth space from the game's own draws whenever the scale is not 1
 * (the B-936 mismatch).
 *
 * Matrices are row-vector, as fast3d and the scene shader take them:
 * clip = (x, y, z, 1) x M. */
typedef struct scenario_scene_camera {
	/* World point -> clip coordinates, in the game's depth space. */
	float view_projection[4][4];
	/* World point -> the game's draw space: (p - draw_offset) * world_scale. */
	float world[4][4];
	float draw_offset[3];
	float world_scale;
} scenario_scene_camera_t;

/* Builds the camera from the game's camera (world units), its viewport's
 * perspective and the draw space. A world_scale that is not a positive finite
 * number counts as 1; a NULL draw_offset as the origin. Returns 0, leaving
 * *out untouched, for a degenerate camera or perspective. */
int scenarioSceneCameraBuild(scenario_scene_camera_t *out,
	const float position[3], const float look[3], const float up[3],
	float fovy_degrees, float aspect, float znear, float zfar,
	float world_scale, const float draw_offset[3]);

/* A world-units view matrix looking along look (row-vector). */
int scenarioSceneViewMatrix(float out[4][4], const float position[3],
	const float look[3], const float up[3]);
/* The OpenGL perspective guPerspectiveF builds (its scale argument 1). */
int scenarioScenePerspectiveMatrix(float out[4][4], float fovy_degrees,
	float aspect, float znear, float zfar);
/* out = a x b; out may alias a or b. */
void scenarioSceneMatrixMultiply(float out[4][4], const float a[4][4],
	const float b[4][4]);
/* clip = (p, 1) x m. */
void scenarioSceneMatrixTransform(const float m[4][4], const float p[3],
	float clip[4]);
/* B-1267: 1 when a triangle that reads counter-clockwise from the eye
 * still reads counter-clockwise in this view-projection's clip space (x
 * right, y up), 0 when the matrix mirrors it, as the y flip of a flipped
 * framebuffer does. A projection that looks down -z has a negative
 * determinant and a view is a rotation, so the sign of the whole
 * determinant says which. */
int scenarioSceneMatrixKeepsWinding(const float m[4][4]);

#ifdef __cplusplus
}
#endif

#endif
