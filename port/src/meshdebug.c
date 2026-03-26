/**
 * meshdebug.c -- Debug wireframe rendering of the collision mesh
 *
 * Renders the world collision mesh as colored wireframe lines using
 * modern GL3+ (no fixed-function pipeline). Uses a minimal shader program.
 *
 * Colors: Green=floor, Red=wall, Blue=ceiling
 * Toggle with F9.
 */

#include "../fast3d/glad/glad.h"
#include <string.h>
#include <stdlib.h>
#include "lib/meshcollision.h"
#include "system.h"

static int s_MeshDebugEnabled = 0;
static GLuint s_ShaderProgram = 0;
static GLuint s_VAO = 0;
static GLuint s_VBO = 0;
static GLint s_UnifProjMat = -1;
static GLint s_UnifMvMat = -1;
static int s_VertexCount = 0;
static int s_NeedsRebuild = 1;

void meshDebugToggle(void)
{
	s_MeshDebugEnabled = !s_MeshDebugEnabled;
	sysLogPrintf(LOG_NOTE, "MESHCOL: debug wireframe %s (%d tris)",
		s_MeshDebugEnabled ? "ON" : "OFF", g_WorldMesh.numtris);
	if (s_MeshDebugEnabled) {
		s_NeedsRebuild = 1;
	}
}

int meshDebugIsEnabled(void)
{
	return s_MeshDebugEnabled;
}

static const char *s_VertSrc =
	"#version 130\n"
	"uniform mat4 u_Proj;\n"
	"uniform mat4 u_MV;\n"
	"in vec3 a_Pos;\n"
	"in vec4 a_Color;\n"
	"out vec4 v_Color;\n"
	"void main() {\n"
	"  gl_Position = u_Proj * u_MV * vec4(a_Pos, 1.0);\n"
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

	s_UnifProjMat = glGetUniformLocation(s_ShaderProgram, "u_Proj");
	s_UnifMvMat = glGetUniformLocation(s_ShaderProgram, "u_MV");

	sysLogPrintf(LOG_NOTE, "MESHCOL: debug shader built (prog=%u)", s_ShaderProgram);
}

/* Vertex layout: 3 floats pos + 4 floats color = 7 floats per vertex */
#define MESHDBG_STRIDE (7 * sizeof(float))

static void rebuildVBO(void)
{
	if (!g_WorldMesh.ready || g_WorldMesh.numtris == 0) {
		s_VertexCount = 0;
		return;
	}

	s_VertexCount = g_WorldMesh.numtris * 3;
	int bufSize = s_VertexCount * 7;
	float *verts = (float *)malloc(bufSize * sizeof(float));
	if (!verts) return;

	int vi = 0;
	for (int i = 0; i < g_WorldMesh.numtris; i++) {
		struct meshtri *tri = &g_WorldMesh.tris[i];

		float r, g, b, a;
		if (tri->normal.y > 0.7f) {
			r = 0.0f; g = 0.9f; b = 0.0f; a = 0.4f;
		} else if (tri->normal.y < -0.7f) {
			r = 0.3f; g = 0.3f; b = 1.0f; a = 0.4f;
		} else {
			r = 0.9f; g = 0.2f; b = 0.2f; a = 0.3f;
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
	glBufferData(GL_ARRAY_BUFFER, bufSize * sizeof(float), verts, GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, MESHDBG_STRIDE, (void *)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, MESHDBG_STRIDE, (void *)(3 * sizeof(float)));

	glBindVertexArray(0);
	free(verts);

	s_NeedsRebuild = 0;
	sysLogPrintf(LOG_NOTE, "MESHCOL: debug VBO built (%d verts)", s_VertexCount);
}

void meshDebugRenderGL(float proj[4][4], float mv[4][4], int width, int height)
{
	if (!s_MeshDebugEnabled) return;
	if (!g_WorldMesh.ready || g_WorldMesh.numtris == 0) return;

	buildShader();
	if (!s_ShaderProgram) return;

	if (s_NeedsRebuild) rebuildVBO();
	if (s_VertexCount == 0) return;

	/* Save current program to restore after */
	GLint prevProgram = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);

	glViewport(0, 0, width, height);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glLineWidth(1.0f);
	glEnable(GL_POLYGON_OFFSET_LINE);
	glPolygonOffset(-1.0f, -1.0f);

	glUseProgram(s_ShaderProgram);
	glUniformMatrix4fv(s_UnifProjMat, 1, GL_FALSE, (const float *)proj);
	glUniformMatrix4fv(s_UnifMvMat, 1, GL_FALSE, (const float *)mv);

	glBindVertexArray(s_VAO);
	glDrawArrays(GL_TRIANGLES, 0, s_VertexCount);
	glBindVertexArray(0);

	/* Restore */
	glDisable(GL_POLYGON_OFFSET_LINE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDepthMask(GL_TRUE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glUseProgram(prevProgram);
}
