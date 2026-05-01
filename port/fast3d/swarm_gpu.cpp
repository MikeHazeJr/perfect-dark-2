/*
 * swarm_gpu.cpp -- GPU compute boid sim for the Swarm GPU benchmark
 * scenario (S483).
 *
 * Per design doc context/designs/gpu-swarm-and-test-scenarios-2026-04-27.md
 * sections B.2-B.5 + G.2 (compute symbols loaded via SDL_GL_GetProcAddress
 * rather than regenerating glad).
 *
 * Approach
 * --------
 *  - First call to swarmGpuAvailable() probes the active GL context. We
 *    require GL >= 4.3 (compute shaders + SSBOs). If the context is too
 *    old, we cache "unavailable" and the runtime falls back to the CPU
 *    seek path.
 *  - Compute shader is compiled lazily on first dispatch and cached for
 *    the program lifetime.
 *  - A single SSBO holds the boid state (pos + vel + alive flag, 32
 *    bytes per boid; capacity = TESTSCEN_SWARM_MAX_COUNT = 256). A
 *    smaller params SSBO carries player_pos / count / max_speed / dt.
 *  - swarmGpuStepAndApply uploads positions from the chr table (so the
 *    CPU is the source of truth between frames -- positions can be
 *    written by despawn / respawn / engine collision response and the
 *    GPU step picks up the latest), dispatches the compute shader,
 *    glMemoryBarriers, reads positions back, and writes them into
 *    chr->prop->pos.
 *
 * Sim algorithm
 * -------------
 * v1 ships pure seek-player so the GPU side is byte-for-byte equivalent
 * to swarm_test.c::cpu_seek_tick. Separation / alignment / cohesion are
 * stubbed in the shader as 0-weighted; turning them on later is a
 * params change, not a shader change.
 *
 * Render
 * ------
 * The chr render path is unchanged. swarmGpuStepAndApply mutates
 * chr->prop->pos and the existing chr render path picks the chrs up
 * via g_Vars.props[] just like a CPU-driven swarm. Per design B.4
 * Option B-R-2.
 */

#include <PR/ultratypes.h>
#include "types.h"
#include "swarm_test.h"
#include "system.h"
#include "testscenarios.h"

#include "glad/glad.h"
#include <SDL2/SDL.h>

#include <stdio.h>
#include <string.h>
#include <math.h>

/* Forward declare so that atan2f resolves in C++ context. <math.h> exposes
 * it as ::atan2f in the global namespace on MinGW; redeclaring with the
 * same C linkage is harmless and avoids a build break if a future header
 * order change hides it. */
extern "C" float atan2f(float y, float x);

extern "C" {

/* From src/game/chraction.c -- proper chr position update that syncs
 * model root, room registration, and ground tracking. */
extern bool chrSetPos(struct chrdata *chr, struct coord *pos, RoomNum *rooms,
	f32 theta, bool findground);

/* ------------------------------------------------------------------
 * GL 4.3 compute symbols loaded on demand
 * ------------------------------------------------------------------ */

#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER                 0x91B9
#endif
#ifndef GL_SHADER_STORAGE_BUFFER
#define GL_SHADER_STORAGE_BUFFER          0x90D2
#endif
#ifndef GL_SHADER_STORAGE_BARRIER_BIT
#define GL_SHADER_STORAGE_BARRIER_BIT     0x00002000
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW                   0x88E8
#endif
#ifndef APIENTRY
#define APIENTRY
#endif

typedef void (APIENTRY *swarm_glDispatchCompute_t)(GLuint, GLuint, GLuint);
typedef void (APIENTRY *swarm_glMemoryBarrier_t)(GLbitfield);
typedef void (APIENTRY *swarm_glBindBufferBase_t)(GLenum, GLuint, GLuint);

static swarm_glDispatchCompute_t  s_glDispatchCompute   = NULL;
static swarm_glMemoryBarrier_t    s_glMemoryBarrier     = NULL;
static swarm_glBindBufferBase_t   s_glBindBufferBase    = NULL;

/* ------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------ */

#define SWARM_GPU_LOCAL_X    64
#define SWARM_GPU_MAX        256       /* must match TESTSCEN_SWARM_MAX_COUNT */

struct boid_record {
	float px, py, pz, _pad_p;   /* vec4 alignment for std430 */
	float vx, vy, vz, _pad_v;
};

struct swarm_params {
	float player_x, player_y, player_z, _pad_p;
	int   count;
	float max_speed;
	float dt;
	int   _pad_t;
};

static int           s_Probed       = 0;
static int           s_Available    = 0;
static GLuint        s_Program      = 0;
static GLuint        s_BoidSsbo     = 0;
static GLuint        s_ParamsSsbo   = 0;
static boid_record   s_BoidScratch[SWARM_GPU_MAX];
static struct swarm_params s_Params;

/* ------------------------------------------------------------------
 * Compute shader source (GLSL 4.3)
 * ------------------------------------------------------------------ */

static const char *kSwarmCs = R"GLSL(
#version 430 core
layout(local_size_x = 64) in;

struct Boid {
    float px, py, pz, _pp;
    float vx, vy, vz, _pv;
};

layout(std430, binding = 0) buffer Boids {
    Boid b[];
};

layout(std430, binding = 1) buffer Params {
    float player_x, player_y, player_z, _pp;
    int   count;
    float max_speed;
    float dt;
    int   _pt;
} P;

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(P.count)) return;

    vec3 pos = vec3(b[i].px, b[i].py, b[i].pz);
    vec3 to_player = vec3(P.player_x - pos.x, 0.0, P.player_z - pos.z);
    float d = length(to_player);
    if (d > 1.0) {
        float step = P.max_speed * P.dt * 60.0;
        float speed = P.max_speed;
        if (step > d) {
            speed = d / (P.dt * 60.0);
        }
        vec3 vel = (to_player / d) * speed;
        pos.x += vel.x * P.dt * 60.0;
        pos.z += vel.z * P.dt * 60.0;
        b[i].vx = vel.x;
        b[i].vy = 0.0;
        b[i].vz = vel.z;
    }
    b[i].px = pos.x;
    b[i].py = pos.y;
    b[i].pz = pos.z;
}
)GLSL";

/* ------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------ */

static GLuint compile_compute_shader(const char *src)
{
	GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
	if (sh == 0) {
		sysLogPrintf(LOG_WARNING,
			"BENCHMARK.SWARM.GPU: glCreateShader(GL_COMPUTE_SHADER) returned 0");
		return 0;
	}
	glShaderSource(sh, 1, &src, NULL);
	glCompileShader(sh);
	GLint ok = 0;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024] = {0};
		GLint len = 0;
		glGetShaderInfoLog(sh, sizeof(log) - 1, &len, log);
		sysLogPrintf(LOG_WARNING,
			"BENCHMARK.SWARM.GPU: compute shader compile failed:\n%s",
			log[0] ? log : "(no log)");
		glDeleteShader(sh);
		return 0;
	}
	GLuint prog = glCreateProgram();
	glAttachShader(prog, sh);
	glLinkProgram(prog);
	GLint linkOk = 0;
	glGetProgramiv(prog, GL_LINK_STATUS, &linkOk);
	if (!linkOk) {
		char log[1024] = {0};
		GLint len = 0;
		glGetProgramInfoLog(prog, sizeof(log) - 1, &len, log);
		sysLogPrintf(LOG_WARNING,
			"BENCHMARK.SWARM.GPU: compute program link failed:\n%s",
			log[0] ? log : "(no log)");
		glDetachShader(prog, sh);
		glDeleteShader(sh);
		glDeleteProgram(prog);
		return 0;
	}
	glDetachShader(prog, sh);
	glDeleteShader(sh);
	return prog;
}

static int probe_compute(void)
{
	int gl_major = 0, gl_minor = 0;
	if (GLVersion.major <= 0) {
		const char *ver = (const char *)glGetString(GL_VERSION);
		if (ver) {
			sscanf(ver, "%d.%d", &gl_major, &gl_minor);
		}
	} else {
		gl_major = GLVersion.major;
		gl_minor = GLVersion.minor;
	}
	if (gl_major < 4 || (gl_major == 4 && gl_minor < 3)) {
		sysLogPrintf(LOG_NOTE,
			"BENCHMARK.SWARM.GPU: GL %d.%d -- compute shaders unavailable; "
			"GPU swarm scenario will fall back to CPU seek",
			gl_major, gl_minor);
		return 0;
	}

	s_glDispatchCompute  = (swarm_glDispatchCompute_t)
		SDL_GL_GetProcAddress("glDispatchCompute");
	s_glMemoryBarrier    = (swarm_glMemoryBarrier_t)
		SDL_GL_GetProcAddress("glMemoryBarrier");
	s_glBindBufferBase   = (swarm_glBindBufferBase_t)
		SDL_GL_GetProcAddress("glBindBufferBase");
	if (!s_glDispatchCompute || !s_glMemoryBarrier || !s_glBindBufferBase) {
		sysLogPrintf(LOG_WARNING,
			"BENCHMARK.SWARM.GPU: GL 4.3 advertised but compute symbol load "
			"failed (dispatch=%p barrier=%p bindbase=%p)",
			(void *)s_glDispatchCompute, (void *)s_glMemoryBarrier,
			(void *)s_glBindBufferBase);
		return 0;
	}
	sysLogPrintf(LOG_NOTE,
		"BENCHMARK.SWARM.GPU: compute shaders available (GL %d.%d)",
		gl_major, gl_minor);
	return 1;
}

static int ensure_resources(void)
{
	if (s_Program == 0) {
		s_Program = compile_compute_shader(kSwarmCs);
		if (s_Program == 0) return 0;
	}
	if (s_BoidSsbo == 0) {
		glGenBuffers(1, &s_BoidSsbo);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, s_BoidSsbo);
		glBufferData(GL_SHADER_STORAGE_BUFFER,
			sizeof(boid_record) * SWARM_GPU_MAX, NULL, GL_DYNAMIC_DRAW);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}
	if (s_ParamsSsbo == 0) {
		glGenBuffers(1, &s_ParamsSsbo);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, s_ParamsSsbo);
		glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(swarm_params),
			NULL, GL_DYNAMIC_DRAW);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}
	return 1;
}

/* ------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------ */

s32 swarmGpuAvailable(void)
{
	if (!s_Probed) {
		s_Probed    = 1;
		s_Available = probe_compute();
	}
	return s_Available;
}

void swarmGpuStepAndApply(struct coord *player_pos,
                          struct chrdata **chrs, s32 count)
{
	if (!swarmGpuAvailable()) return;
	if (count <= 0)           return;
	if (count > SWARM_GPU_MAX) count = SWARM_GPU_MAX;
	if (!ensure_resources())  return;

	/* Upload current chr positions into the boid SSBO. CPU is the source
	 * of truth between frames; the GPU just advances. */
	memset(s_BoidScratch, 0, sizeof(boid_record) * SWARM_GPU_MAX);
	for (s32 i = 0; i < count; i++) {
		struct chrdata *chr = chrs[i];
		if (chr && chr->prop) {
			s_BoidScratch[i].px = chr->prop->pos.x;
			s_BoidScratch[i].py = chr->prop->pos.y;
			s_BoidScratch[i].pz = chr->prop->pos.z;
		}
	}
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, s_BoidSsbo);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
		sizeof(boid_record) * count, s_BoidScratch);

	/* Upload params */
	s_Params.player_x  = player_pos ? player_pos->x : 0.0f;
	s_Params.player_y  = player_pos ? player_pos->y : 0.0f;
	s_Params.player_z  = player_pos ? player_pos->z : 0.0f;
	s_Params.count     = count;
	s_Params.max_speed = 18.0f;
	s_Params.dt        = 1.0f / 60.0f;
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, s_ParamsSsbo);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
		sizeof(swarm_params), &s_Params);

	/* Dispatch */
	glUseProgram(s_Program);
	s_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s_BoidSsbo);
	s_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, s_ParamsSsbo);

	GLuint groups = (GLuint)((count + SWARM_GPU_LOCAL_X - 1) / SWARM_GPU_LOCAL_X);
	s_glDispatchCompute(groups, 1, 1);
	s_glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	/* Readback */
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, s_BoidSsbo);
	glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
		sizeof(boid_record) * count, s_BoidScratch);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	/* Apply to chr positions via chrSetPos so the model root, ground
	 * tracking, and room registration stay in sync with the new
	 * position. Direct prop->pos writes leave the model rendering at
	 * the old root location. Heading is derived from the velocity
	 * vector the shader produced; idle (vel ~ 0) keeps the existing
	 * yaw. findground=true so chrs follow uneven floors. */
	for (s32 i = 0; i < count; i++) {
		struct chrdata *chr = chrs[i];
		if (!chr || !chr->prop || chr->chrnum < 0 || chr->model == NULL) {
			continue;
		}
		struct coord newpos;
		newpos.x = s_BoidScratch[i].px;
		newpos.y = s_BoidScratch[i].py;
		newpos.z = s_BoidScratch[i].pz;

		float vx = s_BoidScratch[i].vx;
		float vz = s_BoidScratch[i].vz;
		f32 face_deg = 0.0f;
		if (vx * vx + vz * vz > 0.001f) {
			face_deg = atan2f(vx, vz) * (180.0f / 3.14159265f);
			if (face_deg < 0.0f) face_deg += 360.0f;
		}

		RoomNum rooms[8];
		for (s32 r = 0; r < 8; r++) {
			rooms[r] = chr->prop->rooms[r];
		}
		chrSetPos(chr, &newpos, rooms, face_deg, true);
	}
}

} /* extern "C" */
