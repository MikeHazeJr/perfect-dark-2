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

/* From src/game/surface_loco.c -- per-chr surface-normal locomotion
 * sampler. Called once per GPU bot per frame to feed the surface_up
 * vector into the compute kernel. Returns 1 on success, 0 if the floor
 * under the chr could not be resolved (out_up = world-up fallback).
 *
 * This is the CPU-side half of Slice 6 GPU parity. The shader projects
 * its seek vector onto the surface plane defined by this vector, so
 * GPU bots on sloped floors (and eventually walls/ceilings, once the
 * sampler ray-casts along surface_up rather than world-down) move
 * along the plane just like CPU surface-loco chrs already do. */
extern bool chrSurfaceLocoSampleFloorNormal(struct chrdata *chr, f32 *out_up);

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
/* GPU hard cap. CPU side TESTSCEN_SWARM_MAX_COUNT is 4096 but the GPU
 * pipeline is currently a stub (seek-player only; no bot AI per B-308,
 * no collision constraints per B-309, no spawn upgrades per B-310).
 * Until those land, the GPU side caps at 256 so the SSBO + chr->prop
 * pointer array stay bounded. Audit ref:
 * 2026-05-13-followup-and-migration-sweep.md HF-2.
 *
 * B-311 (CHRVTX pool exhaustion at 128+ alive chrs) was fixed at
 * c029/2026-05-15 by bumping the chr vertex-store pool to 4096 slots
 * (src/game/vtxstore.c). The SWARM_GPU_SAFE_MAX=64 runtime warning that
 * surfaced the B-311 workaround in-context has been retired; users can
 * now climb the full ladder up to SWARM_GPU_MAX without the display-list
 * corruption class. */
#define SWARM_GPU_MAX        256

struct boid_record {
	float px, py, pz, _pad_p;   /* vec4 alignment for std430 */
	float vx, vy, vz, _pad_v;
	/* Slice 6 GPU surface-loco parity: per-bot surface-normal vector,
	 * sampled CPU-side every frame from chrSurfaceLocoSampleFloorNormal
	 * and consumed shader-side to project the seek vector onto the
	 * surface plane. Defaults to world-up (0,1,0) when the floor
	 * cannot be resolved, which makes the projection a no-op and the
	 * pre-Slice-6 XZ seek behaviour falls out naturally. .w spare. */
	float sux, suy, suz, _pad_u;
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
    float sux, suy, suz, _pu;
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

    /* Slice 6 GPU surface-loco parity: seek vector is now full 3D and
     * is projected onto the surface plane defined by the per-bot
     * surface_up vector. For flat floors (surface_up == world-up), the
     * dot-product term is zero on X/Z and the projection degrades to
     * the prior XZ-only seek. For sloped floors, walls, and ceilings,
     * the seek runs along the plane just like the CPU-side
     * chrSurfaceLocoTick path does for CPU swarm bots. */
    vec3 surface_up = vec3(b[i].sux, b[i].suy, b[i].suz);
    /* Defensive normalize: CPU sampler returns a unit vector on
     * success and (0,1,0) on failure, but we cheap-guard against
     * pathological zero just in case. */
    float sup_len = length(surface_up);
    if (sup_len < 0.001) {
        surface_up = vec3(0.0, 1.0, 0.0);
    } else {
        surface_up /= sup_len;
    }

    vec3 to_player = vec3(P.player_x - pos.x,
                          P.player_y - pos.y,
                          P.player_z - pos.z);
    to_player -= dot(to_player, surface_up) * surface_up;
    float d = length(to_player);
    if (d > 1.0) {
        float step = P.max_speed * P.dt * 60.0;
        float speed = P.max_speed;
        if (step > d) {
            speed = d / (P.dt * 60.0);
        }
        vec3 vel = (to_player / d) * speed;
        pos += vel * P.dt * 60.0;
        b[i].vx = vel.x;
        b[i].vy = vel.y;
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

	/* B-311 SAFE_MAX=64 rate-limited warning retired at c029/2026-05-15.
	 * The CHRVTX pool exhaustion that caused display-list corruption at
	 * 128+ alive chrs was structurally fixed by bumping the chr vtxstore
	 * slot count to 4096; the runtime workaround is no longer needed. */

	/* Upload current chr positions + per-bot surface_up into the boid
	 * SSBO. CPU is the source of truth between frames; the GPU just
	 * advances. The surface_up sample is the Slice 6 hook into the
	 * CPU surface-loco path: chrSurfaceLocoSampleFloorNormal raycasts
	 * the floor under the chr and returns the surface normal. The
	 * shader projects its seek vector onto that plane. On flat ground
	 * the normal is (0,1,0) and the projection collapses to the
	 * pre-Slice-6 XZ seek behaviour. */
	memset(s_BoidScratch, 0, sizeof(boid_record) * SWARM_GPU_MAX);
	for (s32 i = 0; i < count; i++) {
		struct chrdata *chr = chrs[i];
		/* Default to world-up; sampler overrides on success. */
		s_BoidScratch[i].sux = 0.0f;
		s_BoidScratch[i].suy = 1.0f;
		s_BoidScratch[i].suz = 0.0f;
		if (chr && chr->prop) {
			s_BoidScratch[i].px = chr->prop->pos.x;
			s_BoidScratch[i].py = chr->prop->pos.y;
			s_BoidScratch[i].pz = chr->prop->pos.z;

			f32 sup[3];
			(void)chrSurfaceLocoSampleFloorNormal(chr, sup);
			s_BoidScratch[i].sux = sup[0];
			s_BoidScratch[i].suy = sup[1];
			s_BoidScratch[i].suz = sup[2];
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
	/* 1.5x normal seek speed (S593d 2026-05-01). 18.0 * 1.5 = 27.0.
	 * Matches the CPU-side BOTDIFF_PERFECT 1.47x bump in
	 * swarm_test.c::s_SwarmBotConfig and the
	 * gpu_fallback_seek_tick SWARM_MAX_SPEED constant. */
	s_Params.max_speed = 27.0f;
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
