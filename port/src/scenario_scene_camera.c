/* B-1172: the scenario scene's camera in the game's draw space. See
 * scenario_scene_camera.h for why the BG scale belongs in it. */

#include "scenario_scene_camera.h"

#include <string.h>

/* The project's math.h shim (src/include/math.h) does not bring in the C
 * library's declarations, so this file uses the compiler's builtins. */
#define isfinite(x) __builtin_isfinite(x)
#define sqrtf(x) __builtin_sqrtf(x)
#define tanf(x) __builtin_tanf(x)

static int s_finite3(const float v[3])
{
	return v && isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

static int s_normalize3(float v[3])
{
	const float len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
	float inv;

	if (!isfinite(len2) || len2 <= 0.000001f) {
		return 0;
	}

	inv = 1.0f / sqrtf(len2);
	v[0] *= inv;
	v[1] *= inv;
	v[2] *= inv;
	return s_finite3(v);
}

static void s_identity(float m[4][4])
{
	int row;
	int col;

	for (row = 0; row < 4; row++) {
		for (col = 0; col < 4; col++) {
			m[row][col] = row == col ? 1.0f : 0.0f;
		}
	}
}

void scenarioSceneMatrixMultiply(float out[4][4], const float a[4][4],
	const float b[4][4])
{
	float tmp[4][4];
	int i;
	int j;

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			tmp[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j]
				+ a[i][2] * b[2][j] + a[i][3] * b[3][j];
		}
	}
	memcpy(out, tmp, sizeof(tmp));
}

void scenarioSceneMatrixTransform(const float m[4][4], const float p[3],
	float clip[4])
{
	int c;

	for (c = 0; c < 4; c++) {
		clip[c] = p[0] * m[0][c] + p[1] * m[1][c] + p[2] * m[2][c] + m[3][c];
	}
}

int scenarioSceneMatrixKeepsWinding(const float m[4][4])
{
	/* The determinant, by the first row's cofactors, in double: the
	 * entries span the BG scale and the depth range. */
	double d[4][4];
	double det = 0.0;
	int row;
	int col;
	int skip;

	for (row = 0; row < 4; row++) {
		for (col = 0; col < 4; col++) {
			d[row][col] = (double)m[row][col];
		}
	}
	for (skip = 0; skip < 4; skip++) {
		double s[3][3];
		double minor;
		int c = 0;

		for (col = 0; col < 4; col++) {
			if (col == skip) {
				continue;
			}
			for (row = 1; row < 4; row++) {
				s[row - 1][c] = d[row][col];
			}
			c++;
		}
		minor = s[0][0] * (s[1][1] * s[2][2] - s[1][2] * s[2][1])
			- s[0][1] * (s[1][0] * s[2][2] - s[1][2] * s[2][0])
			+ s[0][2] * (s[1][0] * s[2][1] - s[1][1] * s[2][0]);
		det += ((skip & 1) ? -1.0 : 1.0) * d[0][skip] * minor;
	}
	return det < 0.0 ? 1 : 0;
}

int scenarioSceneViewMatrix(float out[4][4], const float position[3],
	const float look[3], const float up[3])
{
	float zaxis[3];
	float xaxis[3];
	float yaxis[3];

	if (!s_finite3(position) || !s_finite3(look) || !s_finite3(up)) {
		return 0;
	}

	zaxis[0] = -look[0];
	zaxis[1] = -look[1];
	zaxis[2] = -look[2];
	if (!s_normalize3(zaxis)) {
		return 0;
	}

	xaxis[0] = up[1] * zaxis[2] - up[2] * zaxis[1];
	xaxis[1] = up[2] * zaxis[0] - up[0] * zaxis[2];
	xaxis[2] = up[0] * zaxis[1] - up[1] * zaxis[0];
	if (!s_normalize3(xaxis)) {
		return 0;
	}

	yaxis[0] = zaxis[1] * xaxis[2] - zaxis[2] * xaxis[1];
	yaxis[1] = zaxis[2] * xaxis[0] - zaxis[0] * xaxis[2];
	yaxis[2] = zaxis[0] * xaxis[1] - zaxis[1] * xaxis[0];
	if (!s_normalize3(yaxis)) {
		return 0;
	}

	out[0][0] = xaxis[0];
	out[1][0] = xaxis[1];
	out[2][0] = xaxis[2];
	out[3][0] = -(position[0] * xaxis[0] + position[1] * xaxis[1] + position[2] * xaxis[2]);

	out[0][1] = yaxis[0];
	out[1][1] = yaxis[1];
	out[2][1] = yaxis[2];
	out[3][1] = -(position[0] * yaxis[0] + position[1] * yaxis[1] + position[2] * yaxis[2]);

	out[0][2] = zaxis[0];
	out[1][2] = zaxis[1];
	out[2][2] = zaxis[2];
	out[3][2] = -(position[0] * zaxis[0] + position[1] * zaxis[1] + position[2] * zaxis[2]);

	out[0][3] = 0.0f;
	out[1][3] = 0.0f;
	out[2][3] = 0.0f;
	out[3][3] = 1.0f;
	return 1;
}

int scenarioScenePerspectiveMatrix(float out[4][4], float fovy_degrees,
	float aspect, float znear, float zfar)
{
	float fovy;
	float cot;

	if (!isfinite(fovy_degrees) || !isfinite(aspect) || !isfinite(znear) || !isfinite(zfar)
			|| fovy_degrees <= 1.0f || fovy_degrees >= 179.0f
			|| aspect <= 0.01f || znear <= 0.0f || zfar <= znear) {
		return 0;
	}

	fovy = fovy_degrees * 3.14159265358979323846f / 180.0f;
	cot = 1.0f / tanf(fovy * 0.5f);
	if (!isfinite(cot)) {
		return 0;
	}

	s_identity(out);
	out[0][0] = cot / aspect;
	out[1][1] = cot;
	out[2][2] = (znear + zfar) / (znear - zfar);
	out[2][3] = -1.0f;
	out[3][2] = (2.0f * znear * zfar) / (znear - zfar);
	out[3][3] = 0.0f;
	return 1;
}

int scenarioSceneCameraBuild(scenario_scene_camera_t *out,
	const float position[3], const float look[3], const float up[3],
	float fovy_degrees, float aspect, float znear, float zfar,
	float world_scale, const float draw_offset[3])
{
	float view[4][4];
	float projection[4][4];
	float offset[3] = { 0.0f, 0.0f, 0.0f };
	const float scale = (isfinite(world_scale) && world_scale > 0.0f) ? world_scale : 1.0f;
	int row;
	int col;

	if (!out) {
		return 0;
	}

	if (draw_offset) {
		if (!s_finite3(draw_offset)) {
			return 0;
		}
		offset[0] = draw_offset[0];
		offset[1] = draw_offset[1];
		offset[2] = draw_offset[2];
	}

	if (!scenarioSceneViewMatrix(view, position, look, up)
			|| !scenarioScenePerspectiveMatrix(projection, fovy_degrees, aspect, znear, zfar)) {
		return 0;
	}

	/* The game's camera-space x, y and z are world units times the BG scale;
	 * w stays 1, as mtxF2L leaves the w column unscaled. The camera's offset
	 * cancels out here: the view of (p - offset) * scale from the eye
	 * (position - offset) * scale is the view of p from position, scaled. */
	for (row = 0; row < 4; row++) {
		for (col = 0; col < 3; col++) {
			view[row][col] *= scale;
		}
	}
	scenarioSceneMatrixMultiply(out->view_projection, view, projection);

	s_identity(out->world);
	out->world[0][0] = scale;
	out->world[1][1] = scale;
	out->world[2][2] = scale;
	out->world[3][0] = -offset[0] * scale;
	out->world[3][1] = -offset[1] * scale;
	out->world[3][2] = -offset[2] * scale;
	out->draw_offset[0] = offset[0];
	out->draw_offset[1] = offset[1];
	out->draw_offset[2] = offset[2];
	out->world_scale = scale;
	return 1;
}
