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
 *  - A single SSBO holds the boid state. After B-308 first slice
 *    (c3807, 2026-05-15) the per-boid record is 80 bytes -- pos + vel +
 *    surface_up (the legacy 48-byte block) plus 32 bytes of GPU-AI
 *    fields (action class, anim key, fire request, target propnum,
 *    range to target). The legacy block is filled and consumed in both
 *    SWARM_METHOD_GPU_POS_ONLY and SWARM_METHOD_GPU_FULL; the AI block
 *    is only populated by the shader when do_ai != 0 and consumed by
 *    the CPU readback path only in GPU_FULL mode. Capacity =
 *    SWARM_GPU_MAX = TESTSCEN_SWARM_MAX_COUNT = 4096. A smaller params
 *    SSBO carries player_pos / count / max_speed / dt / fire_range /
 *    player_propnum / do_ai.
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
 * B-308 first slice (c3807, 2026-05-15)
 * -------------------------------------
 * GPU_FULL mode adds a tiny AI step after the seek: the kernel computes
 * the 3D range to the player and writes one of three action classes
 * (idle/seek/attack), an animation key, and a fire-request flag based
 * on a fixed in-range threshold. The CPU readback consumer logs the
 * first N decisions per frame so the round trip can be verified before
 * projectile-spawn / damage / animation side effects are wired up (the
 * Tier 3 work scoped in context/designs/in-flight/gpu-swarm-bot-pipeline.md).
 *
 * The two paths share one shader; the do_ai uniform gates the AI step.
 * Keeping a single program avoids a second compile, a second cache
 * miss, and a second SSBO binding shuffle.
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
/* GPU hard cap. Bumped to 4096 at c029/2026-05-15 to match the engine
 * ceiling (TESTSCEN_SWARM_MAX_COUNT = 4096). The previous 256 cap was a
 * holding pattern from the pre-B-311 era when the chr vertex pool
 * (CHRVTX = 120 slots) couldn't sustain a higher cycle; that root cause
 * was removed at c029/715424c4 (CHRVTX pool bump to 4096). With the
 * upstream pool removed, the SSBO can grow to the full ladder ceiling.
 *
 * Capacity math (c029, 2026-05-15):
 *  - SSBO    = 4096 * sizeof(boid_record=48) = 196,608 B (~192 KB) once
 *    at first dispatch via glBufferData. OpenGL 4.3 spec requires
 *    GL_MAX_SHADER_STORAGE_BLOCK_SIZE >= 128 MB; nVidia/AMD typically
 *    advertise GB-class limits. Trivial.
 *  - s_BoidScratch BSS = 4096 * 48 = ~192 KB. Static, no allocation.
 *  - Per-frame upload = sizeof(boid_record) * count (NOT * SWARM_GPU_MAX);
 *    scales with active bots, not the cap.
 *  - Dispatch groups = ceil(count / 64). At 4096 that's 64 groups, far
 *    under GL_MAX_COMPUTE_WORK_GROUP_COUNT minimum (65535).
 *  - chrSurfaceLocoSampleFloorNormal CPU cost scales with count; CPU
 *    swarm already runs 4096 bots through full AI ticks, so this is
 *    lighter than the CPU path.
 *
 * B-309 (collision constraints) and B-310 (spawn upgrades) parity:
 * spawn_one_skedar() in port/src/swarm_test.c is the SHARED spawn helper
 * for both methods. Lines 598-624 apply the per-bot radius/height
 * scaling (30 * scale, 185 * scale) and the perim-disable lock
 * (0x00040000 + CHRHFLAG_PERIMDISABLED) UNCONDITIONALLY before the
 * method branch. respawn_volume() / respawn_ring() / respawn_slot() /
 * death_poll_and_respawn() are also method-agnostic; the volume picker
 * with 1.2x retry and the 16/frame streaming refill apply to both
 * methods. So bumping SWARM_GPU_MAX exposes the same collision +
 * spawn-quality treatment to GPU mode that CPU mode already has. */
#define SWARM_GPU_MAX        TESTSCEN_SWARM_MAX_COUNT

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
	/* ---- B-308 first slice (c3807, 2026-05-15) ----
	 * GPU AI output block. Written by the compute kernel when the
	 * params.do_ai uniform is non-zero (SWARM_METHOD_GPU_FULL). Stays
	 * zero in SWARM_METHOD_GPU_POS_ONLY. CPU readback consumer applies
	 * side effects (only logging this slice; projectile spawn / damage
	 * / animation are the next slice). std430 packing keeps these
	 * lined up on vec4 boundaries so the GLSL Boid struct can mirror
	 * exactly.
	 *
	 * - action_class: 0=idle, 1=seek, 2=attack. Determined by range.
	 * - fire_request: 0 or 1. Latches "this frame the bot wants to
	 *   fire its weapon at target_propnum". CPU side will eventually
	 *   spawn a projectile; for now we just log first-N firing slots
	 *   so the round trip is observable.
	 * - target_propnum: index into g_Vars.props[] for the current
	 *   target. Slice 1 always targets the player (uniform), but the
	 *   field exists so a future slice can swap targets per-bot.
	 * - anim_key: 0=stand, 1=walk, 2=attack-melee. CPU consumer can
	 *   map this to chr->myaction / chr->actiontype later; this slice
	 *   only logs it.
	 * - range_to_target: 3D distance in game units. Used both by the
	 *   shader (action_class threshold) and for the CPU log line.
	 */
	int   action_class;
	int   fire_request;
	int   target_propnum;
	int   anim_key;
	float range_to_target, _pad_r0, _pad_r1, _pad_r2;
};

struct swarm_params {
	float player_x, player_y, player_z, _pad_p;
	int   count;
	float max_speed;
	float dt;
	int   _pad_t;
	/* B-308 first slice (c3807, 2026-05-15): AI uniforms. do_ai gates
	 * the AI step in the shader (0 -> POS_ONLY behaviour preserved);
	 * fire_range is the engage threshold (action_class = attack +
	 * fire_request = 1 when range_to_target < fire_range); attack_range
	 * gates the action_class=2 transition (>= attack_range, < fire_range
	 * is action_class=1=seek). player_propnum is the CPU-side prop
	 * index the kernel writes back into target_propnum. */
	int   do_ai;
	float fire_range;
	float attack_range;
	int   player_propnum;
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

/* Boid record -- MUST stay in sync with C++ struct boid_record in
 * port/fast3d/swarm_gpu.cpp. std430 packs ints and floats as 4 B each
 * and vec4-aligns at every 16 B boundary, so the layout below maps
 * 1:1 to the C struct.  80 B per boid. */
struct Boid {
    float px, py, pz, _pp;            /* offset  0: pos */
    float vx, vy, vz, _pv;            /* offset 16: vel */
    float sux, suy, suz, _pu;         /* offset 32: surface_up */
    int   action_class;               /* offset 48: AI block */
    int   fire_request;
    int   target_propnum;
    int   anim_key;
    float range_to_target;            /* offset 64 */
    float _pad_r0, _pad_r1, _pad_r2;
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
    /* B-308 first slice (c3807) AI uniforms. */
    int   do_ai;
    float fire_range;
    float attack_range;
    int   player_propnum;
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

    vec3 to_player_full = vec3(P.player_x - pos.x,
                               P.player_y - pos.y,
                               P.player_z - pos.z);
    /* range_to_target uses the FULL 3D delta (not the plane-projected
     * version) so it matches what a CPU bot AI distance check would
     * see. The seek vector itself is still projected onto the surface
     * plane below to keep locomotion plausible. */
    float range_full = length(to_player_full);

    vec3 to_player = to_player_full
                   - dot(to_player_full, surface_up) * surface_up;
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

    /* B-308 first slice (c3807, 2026-05-15) AI write-out.
     *
     * GPU_POS_ONLY: do_ai = 0, AI fields stay at their current value
     * (CPU consumer ignores them in POS_ONLY mode anyway).
     *
     * GPU_FULL: do_ai = 1, write per-bot decisions based on range to
     * the player. The thresholds are intentionally simple for the
     * first slice -- a real implementation will read weapon range
     * tables from a uniform block. The CPU side consumes these fields
     * and logs the first few decisions per frame so the round trip is
     * observable.
     *
     *   range < fire_range            -> action_class = ATTACK (2),
     *                                    fire_request = 1,
     *                                    anim_key     = ATTACK (2).
     *   fire_range <= range < attack  -> action_class = SEEK   (1),
     *                                    fire_request = 0,
     *                                    anim_key     = WALK   (1).
     *   attack_range <= range         -> action_class = SEEK   (1)
     *                                    (still moving toward target),
     *                                    anim_key     = WALK   (1).
     *
     * Idle is reserved for "no target / out of range / dead" which
     * the slice doesn't surface; the seek path always has a target
     * (the local player). */
    if (P.do_ai != 0) {
        int   act = 1;            /* seek by default */
        int   fire = 0;
        int   anim = 1;           /* walk */
        if (range_full < P.fire_range) {
            act  = 2;             /* attack */
            fire = 1;
            anim = 2;             /* attack-melee */
        }
        b[i].action_class    = act;
        b[i].fire_request    = fire;
        b[i].target_propnum  = P.player_propnum;
        b[i].anim_key        = anim;
        b[i].range_to_target = range_full;
    }
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

/* B-308 first slice (c3807, 2026-05-15) tuning constants. Picked to give
 * Mike something visible the first time he toggles GPU_FULL on the
 * mp_felicity arena (open beach, ~600-2000 game-unit player-to-bot
 * ranges typical):
 *
 *   FIRE_RANGE   = 500.0  -- below this the bot wants to fire. ~3x a
 *                            Skedar's collision diameter; well within
 *                            melee touch range.
 *   ATTACK_RANGE = 1500.0 -- below this the bot is "engaged"; above it
 *                            the bot is just seeking. The slice doesn't
 *                            currently surface a separate behaviour for
 *                            the seek-vs-engage band, but the threshold
 *                            is plumbed so the next slice can layer it.
 *
 * The values intentionally over-trigger fire_request relative to a
 * real Skedar's COMBATKNIFE range so the first frame of GPU_FULL log
 * output shows hundreds of bots requesting fire -- if the slice were
 * tuned conservatively, the visible signal would be near-zero and
 * Mike couldn't confirm the round trip works. */
#define SWARM_AI_FIRE_RANGE     500.0f
#define SWARM_AI_ATTACK_RANGE   1500.0f

/* How many per-frame fire requests we log to stdout. The full readback
 * walks all N bots but we only emit one log line per frame plus a
 * burst of up to SWARM_AI_LOG_MAX individual decisions, so the log
 * stays readable at 4096-bot scale. */
#define SWARM_AI_LOG_MAX        8

void swarmGpuStepAndApply(struct coord *player_pos,
                          struct chrdata **chrs, s32 count,
                          s32 method, s32 player_propnum)
{
	if (!swarmGpuAvailable()) return;
	if (count <= 0)           return;
	if (count > SWARM_GPU_MAX) count = SWARM_GPU_MAX;
	if (!ensure_resources())  return;

	/* B-311 SAFE_MAX=64 rate-limited warning retired at c029/2026-05-15.
	 * The CHRVTX pool exhaustion that caused display-list corruption at
	 * 128+ alive chrs was structurally fixed by bumping the chr vtxstore
	 * slot count to 4096; the runtime workaround is no longer needed. */

	const s32 do_ai = (method == SWARM_METHOD_GPU_FULL) ? 1 : 0;

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
	s_Params.player_x      = player_pos ? player_pos->x : 0.0f;
	s_Params.player_y      = player_pos ? player_pos->y : 0.0f;
	s_Params.player_z      = player_pos ? player_pos->z : 0.0f;
	s_Params.count         = count;
	/* 1.5x normal seek speed (S593d 2026-05-01). 18.0 * 1.5 = 27.0.
	 * Matches the CPU-side BOTDIFF_PERFECT 1.47x bump in
	 * swarm_test.c::s_SwarmBotConfig and the
	 * gpu_fallback_seek_tick SWARM_MAX_SPEED constant. */
	s_Params.max_speed     = 27.0f;
	s_Params.dt            = 1.0f / 60.0f;
	s_Params.do_ai         = do_ai;
	s_Params.fire_range    = SWARM_AI_FIRE_RANGE;
	s_Params.attack_range  = SWARM_AI_ATTACK_RANGE;
	s_Params.player_propnum = player_propnum;
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

	/* B-308 first slice (c3807, 2026-05-15) AI readback consumer.
	 *
	 * In GPU_POS_ONLY mode the AI fields are zero (do_ai = 0) and we
	 * skip the consumer entirely.
	 *
	 * In GPU_FULL mode we walk the readback, count action classes +
	 * fire requests, log a summary line, and burst-log the first few
	 * decisions so Mike can see the round trip in the log. The
	 * projectile-spawn / damage / animation side effects are the
	 * NEXT slice and are intentionally not done here. The current
	 * slice's success criterion is "GPU shader writes AI fields, CPU
	 * reads them back, log line confirms the data flow."
	 *
	 * Rate-limit: emit one summary line per call (the caller is
	 * already calling us at ~60 Hz so this is one line per frame).
	 * Bot-id detail log is capped at SWARM_AI_LOG_MAX per call so the
	 * log stays readable at 4096-bot scale.
	 *
	 * Performance note: the readback above is blocking
	 * (glGetBufferSubData). At 4096 bots * 80 B = ~320 KB read per
	 * frame this is acceptable on a 60 Hz target but should be
	 * promoted to a PBO + fence ring once GPU_FULL grows into a
	 * default-on path. Filed as a follow-up in the design doc. */
	if (do_ai) {
		/* 60-frame throttle for the AI summary log -- matches the
		 * cadence of BENCHMARK.SWARM.* in swarm_test.c so the two log
		 * streams interleave cleanly at ~1 Hz. Decision counters are
		 * still gathered every frame (so a future PBO ring-buffer
		 * consumer that wants per-frame data can drop in here), but
		 * only one line per second hits the log. */
		static s32 s_AiLogTick = 0;
		s32 n_idle = 0, n_seek = 0, n_attack = 0, n_fire = 0;
		f32 sum_range = 0.0f;
		f32 min_range = 1.0e30f, max_range = 0.0f;
		s32 sample_count = 0;
		for (s32 i = 0; i < count; i++) {
			struct chrdata *chr = chrs[i];
			if (!chr || !chr->prop || chr->chrnum < 0
					|| chr->model == NULL) {
				continue;
			}
			const s32 act = s_BoidScratch[i].action_class;
			const s32 fire = s_BoidScratch[i].fire_request;
			const f32 r = s_BoidScratch[i].range_to_target;
			if      (act == 2) n_attack++;
			else if (act == 1) n_seek++;
			else               n_idle++;
			if (fire) n_fire++;
			sum_range += r;
			if (r < min_range) min_range = r;
			if (r > max_range) max_range = r;
			sample_count++;
		}
		s_AiLogTick++;
		if (s_AiLogTick >= 60) {
			s_AiLogTick = 0;
			const f32 avg_range = (sample_count > 0)
				? (sum_range / (f32)sample_count) : 0.0f;
			sysLogPrintf(LOG_NOTE,
				"BENCHMARK.SWARM.GPU.AI: count=%d idle=%d seek=%d attack=%d "
				"fire_req=%d range_min=%.0f avg=%.0f max=%.0f fire_range=%.0f "
				"target_propnum=%d",
				count, n_idle, n_seek, n_attack, n_fire,
				(double)((min_range > 1.0e29f) ? 0.0f : min_range),
				(double)avg_range,
				(double)max_range,
				(double)SWARM_AI_FIRE_RANGE,
				player_propnum);

			/* Burst-log up to SWARM_AI_LOG_MAX individual firing-bot
			 * decisions so the log shows specific examples (slot
			 * index + range). This lets Mike pick a slot and visually
			 * correlate with the bot count progression on-screen. */
			s32 logged = 0;
			for (s32 i = 0; i < count && logged < SWARM_AI_LOG_MAX; i++) {
				struct chrdata *chr = chrs[i];
				if (!chr || !chr->prop || chr->chrnum < 0
						|| chr->model == NULL) {
					continue;
				}
				if (s_BoidScratch[i].fire_request) {
					sysLogPrintf(LOG_NOTE,
						"BENCHMARK.SWARM.GPU.AI.FIRE: slot=%d "
						"chrnum=%d range=%.1f target_propnum=%d "
						"anim_key=%d",
						i, (s32)chr->chrnum,
						(double)s_BoidScratch[i].range_to_target,
						s_BoidScratch[i].target_propnum,
						s_BoidScratch[i].anim_key);
					logged++;
				}
			}
		}
	}
}

} /* extern "C" */
