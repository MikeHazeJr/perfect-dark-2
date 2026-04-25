/**
 * meshdebug.c -- Debug visualization of the collision mesh
 *
 * F9 cycles through modes:
 *   0 = OFF (normal rendering)
 *   1 = TINT (rendered geometry colored by surface normal)
 *   2 = MESH ONLY (game rendering suppressed, collision mesh shown as colored triangles)
 *
 * Colors: Green=floor, Red=wall, Blue=ceiling
 */

#include "../fast3d/glad/glad.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "lib/meshcollision.h"
#include "system.h"

static int s_DebugMode = 0; /* 0=off, 1=tint, 2=mesh only */
static GLuint s_ShaderProgram = 0;
static GLuint s_VAO = 0;
static GLuint s_VBO = 0;
static int s_VertexCount = 0;
static int s_NeedsRebuild = 1;

void meshDebugToggle(void)
{
	/* For now, F9 just logs collision mesh stats. Visual debug modes disabled
	 * pending correct VP matrix / overlay rendering work. */
	sysLogPrintf(LOG_NOTE, "MESHCOL: world mesh has %d tris, grid %dx%d, ready=%d",
		g_WorldMesh.numtris, g_WorldMesh.cellsx, g_WorldMesh.cellsz, g_WorldMesh.ready);
}

int meshDebugIsEnabled(void)
{
	return s_DebugMode > 0;
}

int meshDebugGetMode(void)
{
	return s_DebugMode;
}

/* ---- Shader for collision mesh rendering ---- */

static const char *s_VertSrc =
	"#version 130\n"
	"uniform mat4 u_VP;\n"
	"in vec3 a_Pos;\n"
	"in vec4 a_Color;\n"
	"out vec4 v_Color;\n"
	"void main() {\n"
	"  gl_Position = u_VP * vec4(a_Pos, 1.0);\n"
	"  v_Color = a_Color;\n"
	"}\n";

static const char *s_FragSrc =
	"#version 130\n"
	"in vec4 v_Color;\n"
	"out vec4 fragColor;\n"
	"void main() {\n"
	"  fragColor = v_Color;\n"
	"}\n";

static void buildShader(void)
{
	if (s_ShaderProgram) return;

	GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vs, 1, &s_VertSrc, NULL);
	glCompileShader(vs);

	GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fs, 1, &s_FragSrc, NULL);
	glCompileShader(fs);

	s_ShaderProgram = glCreateProgram();
	glAttachShader(s_ShaderProgram, vs);
	glAttachShader(s_ShaderProgram, fs);
	glBindAttribLocation(s_ShaderProgram, 0, "a_Pos");
	glBindAttribLocation(s_ShaderProgram, 1, "a_Color");
	glLinkProgram(s_ShaderProgram);

	glDeleteShader(vs);
	glDeleteShader(fs);

	sysLogPrintf(LOG_NOTE, "MESHCOL: debug shader built (prog=%u)", s_ShaderProgram);
}

/* Vertex layout: 3 floats pos + 4 floats color = 7 floats per vertex */
#define MESHDBG_FLOATS_PER_VERT 7

static void rebuildVBO(void)
{
	if (!g_WorldMesh.ready || g_WorldMesh.numtris == 0) {
		s_VertexCount = 0;
		return;
	}

	s_VertexCount = g_WorldMesh.numtris * 3;
	int bufFloats = s_VertexCount * MESHDBG_FLOATS_PER_VERT;
	float *verts = (float *)malloc(bufFloats * sizeof(float));
	if (!verts) return;

	int vi = 0;
	for (int i = 0; i < g_WorldMesh.numtris; i++) {
		struct meshtri *tri = &g_WorldMesh.tris[i];

		float r, g, b, a;
		if (tri->normal.y > 0.7f) {
			r = 0.1f; g = 0.8f; b = 0.1f; a = 0.85f;
		} else if (tri->normal.y < -0.7f) {
			r = 0.2f; g = 0.2f; b = 0.9f; a = 0.85f;
		} else {
			r = 0.8f; g = 0.15f; b = 0.15f; a = 0.75f;
		}

		/* v0 */
		verts[vi++] = tri->v0.x; verts[vi++] = tri->v0.y; verts[vi++] = tri->v0.z;
		verts[vi++] = r; verts[vi++] = g; verts[vi++] = b; verts[vi++] = a;
		/* v1 */
		verts[vi++] = tri->v1.x; verts[vi++] = tri->v1.y; verts[vi++] = tri->v1.z;
		verts[vi++] = r; verts[vi++] = g; verts[vi++] = b; verts[vi++] = a;
		/* v2 */
		verts[vi++] = tri->v2.x; verts[vi++] = tri->v2.y; verts[vi++] = tri->v2.z;
		verts[vi++] = r; verts[vi++] = g; verts[vi++] = b; verts[vi++] = a;
	}

	if (!s_VAO) glGenVertexArrays(1, &s_VAO);
	if (!s_VBO) glGenBuffers(1, &s_VBO);

	glBindVertexArray(s_VAO);
	glBindBuffer(GL_ARRAY_BUFFER, s_VBO);
	glBufferData(GL_ARRAY_BUFFER, bufFloats * sizeof(float), verts, GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, MESHDBG_FLOATS_PER_VERT * sizeof(float), (void *)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, MESHDBG_FLOATS_PER_VERT * sizeof(float), (void *)(3 * sizeof(float)));

	glBindVertexArray(0);
	free(verts);

	s_NeedsRebuild = 0;
	sysLogPrintf(LOG_NOTE, "MESHCOL: debug VBO built (%d verts, %d tris)", s_VertexCount, g_WorldMesh.numtris);
}

/**
 * Render the collision mesh as colored triangles using the game's VP matrix.
 * Called from gfx_pc.cpp after end_frame, before ImGui.
 * vp is the View*Projection matrix computed from RSP state.
 */
void meshDebugRenderCollisionMesh(float vp[4][4], int width, int height)
{
	if (!g_WorldMesh.ready || g_WorldMesh.numtris == 0) return;

	buildShader();
	if (!s_ShaderProgram) return;

	if (s_NeedsRebuild) rebuildVBO();
	if (s_VertexCount == 0) return;

	GLint prevProgram = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);

	/* Clear to dark background since game rendering is suppressed */
	glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glViewport(0, 0, width, height);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDepthMask(GL_TRUE);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	/* Draw filled triangles first */
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glUseProgram(s_ShaderProgram);

	GLint vpLoc = glGetUniformLocation(s_ShaderProgram, "u_VP");
	glUniformMatrix4fv(vpLoc, 1, GL_FALSE, (const float *)vp);

	glBindVertexArray(s_VAO);
	glDrawArrays(GL_TRIANGLES, 0, s_VertexCount);

	/* Draw wireframe on top for edge visibility */
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glEnable(GL_POLYGON_OFFSET_LINE);
	glPolygonOffset(-1.0f, -1.0f);
	glLineWidth(1.0f);

	/* Darken colors for wireframe edges */
	/* Re-render with same data -- the edges will be slightly visible */
	glDrawArrays(GL_TRIANGLES, 0, s_VertexCount);

	glBindVertexArray(0);

	/* Restore */
	glDisable(GL_POLYGON_OFFSET_LINE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glUseProgram(prevProgram);
}
