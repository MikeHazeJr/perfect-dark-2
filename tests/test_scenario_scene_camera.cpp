/* B-1172: the scenario scene's camera is in the game's draw space.
 *
 * The game draws a world point p at q = (p - draw_offset) * world_scale and
 * projects it with its lookat from the eye (cam_pos - draw_offset) *
 * world_scale and the viewport's perspective (playerAllocateMatrices,
 * vi0000ad5c). The scene camera (scenario_scene_camera.c) must give the same
 * clip positions, depth included, or the props that follow the scene cannot
 * test against its depth. The oracle below transcribes the game's lookat
 * (src/lib/mtx.c mtx00016874) and perspective (src/lib/ultra/gu/perspective.c
 * guPerspectiveF, scale 1). */

#include "catch.hpp"

#include <cmath>
#include <cstring>

extern "C" {
#include "scenario_scene_camera.h"
}

namespace {

/* @SYNC src/lib/mtx.c mtx00016874 */
void gameLookAt(float m[4][4], float posx, float posy, float posz, float lookx, float looky,
	float lookz, float upx, float upy, float upz)
{
	float tmp = -1 / std::sqrt(lookx * lookx + looky * looky + lookz * lookz);
	lookx *= tmp;
	looky *= tmp;
	lookz *= tmp;

	float a = upy * lookz - upz * looky;
	float b = upz * lookx - upx * lookz;
	float c = upx * looky - upy * lookx;
	tmp = 1 / std::sqrt(a * a + b * b + c * c);
	a *= tmp;
	b *= tmp;
	c *= tmp;

	upx = looky * c - lookz * b;
	upy = lookz * a - lookx * c;
	upz = lookx * b - looky * a;
	tmp = 1 / std::sqrt(upx * upx + upy * upy + upz * upz);
	upx *= tmp;
	upy *= tmp;
	upz *= tmp;

	m[0][0] = a;
	m[1][0] = b;
	m[2][0] = c;
	m[3][0] = -(posx * a + posy * b + posz * c);
	m[0][1] = upx;
	m[1][1] = upy;
	m[2][1] = upz;
	m[3][1] = -(posx * upx + posy * upy + posz * upz);
	m[0][2] = lookx;
	m[1][2] = looky;
	m[2][2] = lookz;
	m[3][2] = -(posx * lookx + posy * looky + posz * lookz);
	m[0][3] = 0;
	m[1][3] = 0;
	m[2][3] = 0;
	m[3][3] = 1;
}

/* @SYNC src/lib/ultra/gu/perspective.c guPerspectiveF (scale 1, as vi.c builds it) */
void gamePerspective(float m[4][4], float fovy, float aspect, float znear, float zfar)
{
	std::memset(m, 0, sizeof(float) * 16);
	fovy *= 3.1415926f / 180.0f;
	const float cot = std::cos(fovy * 0.5f) / std::sin(fovy * 0.5f);
	m[0][0] = cot / aspect;
	m[1][1] = cot;
	m[2][2] = (znear + zfar) / (znear - zfar);
	m[2][3] = -1;
	m[3][2] = (2.0f * znear * zfar) / (znear - zfar);
	m[3][3] = 0;
}

struct GameCamera {
	float pos[3];
	float look[3];
	float up[3];
	float fovy;
	float aspect;
	float znear;
	float zfar;
	float scale;
	float offset[3];
};

/* The game's clip position of world point p (row-vector, as fast3d). */
void gameClip(const GameCamera &cam, const float p[3], float clip[4])
{
	float view[4][4];
	float persp[4][4];
	float vp[4][4];
	const float eye[3] = {
		(cam.pos[0] - cam.offset[0]) * cam.scale,
		(cam.pos[1] - cam.offset[1]) * cam.scale,
		(cam.pos[2] - cam.offset[2]) * cam.scale,
	};
	const float q[3] = {
		(p[0] - cam.offset[0]) * cam.scale,
		(p[1] - cam.offset[1]) * cam.scale,
		(p[2] - cam.offset[2]) * cam.scale,
	};
	gameLookAt(view, eye[0], eye[1], eye[2], cam.look[0], cam.look[1], cam.look[2],
		cam.up[0], cam.up[1], cam.up[2]);
	gamePerspective(persp, cam.fovy, cam.aspect, cam.znear, cam.zfar);
	scenarioSceneMatrixMultiply(vp, view, persp);
	scenarioSceneMatrixTransform(vp, q, clip);
}

bool buildSceneCamera(const GameCamera &cam, float scale, scenario_scene_camera_t *out)
{
	return scenarioSceneCameraBuild(out, cam.pos, cam.look, cam.up, cam.fovy, cam.aspect,
		cam.znear, cam.zfar, scale, cam.offset) != 0;
}

GameCamera villaLikeCamera(float scale)
{
	GameCamera cam = {
		{ 2410.0f, 132.5f, -5620.0f },
		{ 0.30f, -0.12f, 0.946f },
		{ 0.0f, 1.0f, 0.0f },
		60.0f, 4.0f / 3.0f, 10.0f, 15000.0f,
		scale,
		{ 2000.0f, 0.0f, -6000.0f },
	};
	return cam;
}

/* Points in front of the camera at a range of distances. */
const float kPoints[][3] = {
	{ 2500.0f, 120.0f, -5400.0f },
	{ 2700.0f, 60.0f, -4900.0f },
	{ 3100.0f, 300.0f, -3600.0f },
	{ 2200.0f, -40.0f, -5100.0f },
	{ 4200.0f, 900.0f, -900.0f },
};

void requireSameNdc(const float a[4], const float b[4], float xy_eps, float z_eps)
{
	REQUIRE(a[3] > 0.0f);
	REQUIRE(b[3] > 0.0f);
	CHECK(std::fabs(a[0] / a[3] - b[0] / b[3]) < xy_eps);
	CHECK(std::fabs(a[1] / a[3] - b[1] / b[3]) < xy_eps);
	CHECK(std::fabs(a[2] / a[3] - b[2] / b[3]) < z_eps);
}

} // namespace

TEST_CASE("Scene camera: at BG scale 1 it projects like the game's BG matrices",
	"[scene_camera][b1172]")
{
	const GameCamera cam = villaLikeCamera(1.0f);
	scenario_scene_camera_t scene;
	REQUIRE(buildSceneCamera(cam, cam.scale, &scene));
	for (const auto &p : kPoints) {
		float game[4];
		float own[4];
		gameClip(cam, p, game);
		scenarioSceneMatrixTransform(scene.view_projection, p, own);
		requireSameNdc(game, own, 1e-4f, 1e-5f);
	}
}

TEST_CASE("Scene camera: at Villa's 0.5 BG scale it keeps the game's depth space",
	"[scene_camera][b1172]")
{
	const GameCamera cam = villaLikeCamera(0.5f);
	scenario_scene_camera_t scene;
	scenario_scene_camera_t unscaled;
	REQUIRE(buildSceneCamera(cam, cam.scale, &scene));
	/* The step 1 camera: world units with the same near and far planes. */
	REQUIRE(buildSceneCamera(cam, 1.0f, &unscaled));
	bool unscaled_depth_differs = false;
	for (const auto &p : kPoints) {
		float game[4];
		float own[4];
		float old_clip[4];
		gameClip(cam, p, game);
		scenarioSceneMatrixTransform(scene.view_projection, p, own);
		scenarioSceneMatrixTransform(unscaled.view_projection, p, old_clip);
		requireSameNdc(game, own, 1e-4f, 1e-5f);
		/* x and y agree without the scale; depth does not (B-936/B-1172). */
		CHECK(std::fabs(game[0] / game[3] - old_clip[0] / old_clip[3]) < 1e-4f);
		CHECK(std::fabs(game[1] / game[3] - old_clip[1] / old_clip[3]) < 1e-4f);
		if (std::fabs(game[2] / game[3] - old_clip[2] / old_clip[3]) > 1e-4f) {
			unscaled_depth_differs = true;
		}
	}
	CHECK(unscaled_depth_differs);
}

TEST_CASE("Scene camera: its world matrix is the game's draw space",
	"[scene_camera][b1172]")
{
	const GameCamera cam = villaLikeCamera(0.5f);
	scenario_scene_camera_t scene;
	REQUIRE(buildSceneCamera(cam, cam.scale, &scene));
	CHECK(scene.world_scale == 0.5f);
	CHECK(scene.draw_offset[0] == cam.offset[0]);
	CHECK(scene.draw_offset[2] == cam.offset[2]);
	for (const auto &p : kPoints) {
		float q[4];
		scenarioSceneMatrixTransform(scene.world, p, q);
		CHECK(std::fabs(q[0] - (p[0] - cam.offset[0]) * 0.5f) < 1e-3f);
		CHECK(std::fabs(q[1] - (p[1] - cam.offset[1]) * 0.5f) < 1e-3f);
		CHECK(std::fabs(q[2] - (p[2] - cam.offset[2]) * 0.5f) < 1e-3f);
		CHECK(q[3] == 1.0f);
	}
	/* The world matrix followed by the game's BG matrix (orthomtxl: the lookat
	 * from the scaled eye, then the perspective) is the scene camera. */
	float view[4][4];
	float persp[4][4];
	float ortho[4][4];
	float world_then_ortho[4][4];
	gameLookAt(view, (cam.pos[0] - cam.offset[0]) * 0.5f, (cam.pos[1] - cam.offset[1]) * 0.5f,
		(cam.pos[2] - cam.offset[2]) * 0.5f, cam.look[0], cam.look[1], cam.look[2],
		cam.up[0], cam.up[1], cam.up[2]);
	gamePerspective(persp, cam.fovy, cam.aspect, cam.znear, cam.zfar);
	scenarioSceneMatrixMultiply(ortho, view, persp);
	scenarioSceneMatrixMultiply(world_then_ortho, scene.world, ortho);
	for (const auto &p : kPoints) {
		float a[4];
		float b[4];
		scenarioSceneMatrixTransform(world_then_ortho, p, a);
		scenarioSceneMatrixTransform(scene.view_projection, p, b);
		requireSameNdc(a, b, 1e-4f, 1e-5f);
	}
}

TEST_CASE("Scene camera: degenerate inputs", "[scene_camera][b1172]")
{
	GameCamera cam = villaLikeCamera(1.0f);
	scenario_scene_camera_t scene;
	std::memset(&scene, 0x5a, sizeof(scene));
	const scenario_scene_camera_t before = scene;

	/* A scale that is not a positive finite number counts as 1. */
	REQUIRE(buildSceneCamera(cam, 0.0f, &scene));
	CHECK(scene.world_scale == 1.0f);
	REQUIRE(buildSceneCamera(cam, -2.0f, &scene));
	CHECK(scene.world_scale == 1.0f);
	REQUIRE(buildSceneCamera(cam, NAN, &scene));
	CHECK(scene.world_scale == 1.0f);

	/* No offset: the origin. */
	REQUIRE(scenarioSceneCameraBuild(&scene, cam.pos, cam.look, cam.up, cam.fovy, cam.aspect,
		cam.znear, cam.zfar, 1.0f, nullptr));
	CHECK(scene.draw_offset[0] == 0.0f);
	CHECK(scene.world[3][0] == 0.0f);

	/* Refused, and *out left alone: a non-finite offset, a zero look, a bad
	 * perspective. */
	scenario_scene_camera_t untouched = before;
	const float bad_offset[3] = { 0.0f, INFINITY, 0.0f };
	CHECK(scenarioSceneCameraBuild(&untouched, cam.pos, cam.look, cam.up, cam.fovy, cam.aspect,
		cam.znear, cam.zfar, 1.0f, bad_offset) == 0);
	const float zero[3] = { 0.0f, 0.0f, 0.0f };
	CHECK(scenarioSceneCameraBuild(&untouched, cam.pos, zero, cam.up, cam.fovy, cam.aspect,
		cam.znear, cam.zfar, 1.0f, cam.offset) == 0);
	CHECK(scenarioSceneCameraBuild(&untouched, cam.pos, cam.look, cam.up, cam.fovy, cam.aspect,
		20.0f, 10.0f, 1.0f, cam.offset) == 0);
	CHECK(std::memcmp(&untouched, &before, sizeof(before)) == 0);
	CHECK(scenarioSceneCameraBuild(nullptr, cam.pos, cam.look, cam.up, cam.fovy, cam.aspect,
		cam.znear, cam.zfar, 1.0f, cam.offset) == 0);
}
