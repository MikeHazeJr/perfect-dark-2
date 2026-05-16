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

/* <cmath> must come BEFORE PR/ultratypes.h so the C++ standard library
 * sees the genuine `bool` type rather than the project's `typedef int
 * bool` alias. Same ordering as gfx_pc.cpp / pdgui_theme.cpp /
 * actionmap.cpp. c029/2026-05-16 added the velocity sampler in this
 * file that uses `sqrt(double)`, which surfaced the include-order
 * requirement for the first time in this TU. */
#include <cmath>

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

/* Forward declare so that atan2f resolves in C++ context. <math.h>
 * exposes it as ::atan2f in the global namespace on MinGW; redeclaring
 * with the same C linkage is harmless and avoids a build break if a
 * future header order change hides it. */
extern "C" float atan2f(float y, float x);

extern "C" {

/* From src/game/chraction.c -- proper chr position update that syncs
 * model root, room registration, and ground tracking. */
extern bool chrSetPos(struct chrdata *chr, struct coord *pos, RoomNum *rooms,
	f32 theta, bool findground);

/* c029 Slice 3 (2026-05-16): chrSetPos minus the cdFindGroundInfoAtCyl
 * raycast. The merged-walk loop below samples the floor once per active
 * bot (or reuses the cache) and feeds the resulting ground / floorcol /
 * floortype / floorroom values directly into this helper -- halving the
 * BG raycast load that the pre-refactor chrSetPos(findground=true) path
 * incurred (chrMoveToPos + the inline cdFindGroundInfoAtCyl = 2 per
 * call). See chraction.c::chrSetPosWithCachedGround for the contract. */
extern void chrSetPosWithCachedGround(struct chrdata *chr, struct coord *pos,
	RoomNum *rooms, f32 theta, f32 ground, u16 floorcol, u8 floortype,
	RoomNum floorroom);

/* c029 Slice 3: BG raycast primitive. Same call shape as the one
 * chrSetPos was making internally; we hoist it into the merged walk so
 * the floor cache can decide per-bot whether to incur the cost. */
extern f32 cdFindGroundInfoAtCyl(struct coord *pos, f32 radius, RoomNum *rooms,
	u16 *floorcol, u8 *floortype, u16 *floorflags, RoomNum *floorroom,
	s32 *inlift, struct prop **lift);

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

/* From port/src/swarm_test.c -- B-308 slice 2 GPU AI -> CPU side-effect
 * dispatcher. Called once per active GPU bot from the AI readback block
 * below when SWARM_METHOD_GPU_FULL is armed. The CPU side translates
 * the compute kernel's per-bot decision (fire_request + anim_key) into
 * chrDamageByImpact + modelSetAnimation calls. See swarm_test.c docblock
 * above the implementation for the throttling / transition-guard rules
 * that keep 4096-bot dispatch costs bounded. */
extern s32 swarmTestApplyAiDecision(struct chrdata *chr,
                                    s32 slot_index,
                                    s32 target_propnum,
                                    s32 action_class,
                                    s32 fire_request,
                                    s32 anim_key,
                                    f32 range_to_target);
extern s32 swarmTestGetAndResetGpuAiFireCount(void);

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
#ifndef GL_COPY_READ_BUFFER
#define GL_COPY_READ_BUFFER               0x8F36
#endif
#ifndef GL_COPY_WRITE_BUFFER
#define GL_COPY_WRITE_BUFFER              0x8F37
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE     0x9117
#endif
#ifndef GL_ALREADY_SIGNALED
#define GL_ALREADY_SIGNALED               0x911A
#endif
#ifndef GL_CONDITION_SATISFIED
#define GL_CONDITION_SATISFIED            0x911C
#endif
#ifndef GL_SYNC_FLUSH_COMMANDS_BIT
#define GL_SYNC_FLUSH_COMMANDS_BIT        0x00000001
#endif
#ifndef APIENTRY
#define APIENTRY
#endif

typedef void   (APIENTRY *swarm_glDispatchCompute_t)(GLuint, GLuint, GLuint);
typedef void   (APIENTRY *swarm_glMemoryBarrier_t)(GLbitfield);
typedef void   (APIENTRY *swarm_glBindBufferBase_t)(GLenum, GLuint, GLuint);
typedef void   (APIENTRY *swarm_glCopyBufferSubData_t)(GLenum, GLenum,
                                                       GLintptr, GLintptr, GLsizeiptr);
/* glFenceSync returns a GLsync. GLsync is `struct __GLsync *` per GL spec;
 * use `void *` here so we don't have to drag in a header that may not be
 * available with this MinGW SDK. The signedness is irrelevant -- we treat
 * the value as an opaque handle. */
typedef void * (APIENTRY *swarm_glFenceSync_t)(GLenum, GLbitfield);
typedef GLenum (APIENTRY *swarm_glClientWaitSync_t)(void *, GLbitfield, GLuint64);
typedef void   (APIENTRY *swarm_glDeleteSync_t)(void *);

static swarm_glDispatchCompute_t    s_glDispatchCompute    = NULL;
static swarm_glMemoryBarrier_t      s_glMemoryBarrier      = NULL;
static swarm_glBindBufferBase_t     s_glBindBufferBase     = NULL;
static swarm_glCopyBufferSubData_t  s_glCopyBufferSubData  = NULL;
static swarm_glFenceSync_t          s_glFenceSync          = NULL;
static swarm_glClientWaitSync_t     s_glClientWaitSync     = NULL;
static swarm_glDeleteSync_t         s_glDeleteSync         = NULL;

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
 * Slice 2 c029 (2026-05-16): async readback ring.
 *
 * Problem: pre-Slice-2 we called glGetBufferSubData(BoidSsbo) immediately
 * after the compute dispatch every frame. That call is a SYNCHRONOUS
 * pipeline drain -- it blocks the CPU until ALL queued GL commands
 * complete, then copies the data. At 4096 bots * 80 bytes = 320 KB the
 * memcpy itself is trivial (~50us); the kill was the GPU sync at the
 * tail of a render pipeline that already includes the deferred game
 * frame, ImGui, swap, and the next frame's setup. The fence drain cost
 * scaled badly: ~1-3 ms at 64 bots, but Mike's 512-bot playtest hit
 * ~80 ms/frame (12.5 ticks/sec, 6x slowdown vs 256).
 *
 * Solution: 2-deep PBO-style ring. Each frame N:
 *   1. Dispatch compute (writes boid SSBO)
 *   2. glCopyBufferSubData(BoidSsbo -> ring[write_idx]) -- queues a
 *      GPU-side memcpy, NO sync
 *   3. glFenceSync after the copy -- marks "when this fence signals,
 *      ring[write_idx] holds frame N's data"
 *   4. Look at ring[read_idx] (= write_idx XOR 1 for depth=2). If its
 *      fence is non-NULL AND glClientWaitSync(0 timeout) == SIGNALED,
 *      then glGetBufferSubData(ring[read_idx]) reads previous frame's
 *      data WITHOUT a pipeline drain (the data has already landed).
 *   5. Advance write_idx; the OLD write_idx becomes next-frame's read.
 *
 * Latency cost: AI / chrSetPos decisions are now driven by frame N-1's
 * boid positions (one frame stale). Visually imperceptible at 60 Hz;
 * the player's own input loop is similarly buffered through SDL pump +
 * fast3d frame queue. Position application via chrSetPos at frame N
 * uses N-1's positions; the GPU shader at frame N+1 will pick up the
 * latest chr->prop->pos (which IS the frame-N positions we read at
 * frame N+1's tick). So bots lag the player by one frame but converge
 * correctly. Same trade as classic triple-buffering.
 *
 * First 1-2 frames: ring[read_idx]'s fence is NULL, consumer skips. No
 * fire / anim decisions for those frames. Imperceptible warm-up cost.
 *
 * Why 2 deep, not 3:
 *   - 2 deep is enough to break the sync. The compute, copy, and fence
 *     are all on the GPU side; the readback always gets last frame's
 *     data immediately because the fence has had ~16 ms to signal.
 *   - 3 deep would add 1 more frame of position lag with no perf gain
 *     in typical cases. 3 is what triple-buffered swapchains use to
 *     hide V-sync wait; we don't have a V-sync issue here.
 *   - Memory cost: 2 * 320 KB = 640 KB. Trivial.
 *
 * Fence handle: GLsync. We use void* (see typedef above) to avoid GL
 * header coupling. NULL = "no fence inserted yet" / "previous fence
 * already consumed".
 *
 * If the compute symbols failed to load (probe_compute) we keep the
 * pre-Slice-2 behaviour by NOT initializing ring buffers and falling
 * back to the blocking readback at the call-site. */
#define SWARM_READBACK_RING_DEPTH 2
static GLuint  s_ReadbackRingBuf[SWARM_READBACK_RING_DEPTH] = {0, 0};
static void   *s_ReadbackRingFence[SWARM_READBACK_RING_DEPTH] = {NULL, NULL};
static s32     s_ReadbackRingCount[SWARM_READBACK_RING_DEPTH] = {0, 0};
static s32     s_ReadbackWriteIdx = 0;

/* Per-slot chrnum snapshot, sized to SWARM_GPU_MAX (= 4096).
 *
 * The async readback ring delivers last frame's positions, but slot i
 * may have been refilled by death_poll_and_respawn since the dispatch
 * (a new chr with a different chrnum). Applying last frame's position
 * to a fresh chr would teleport it. To detect this, we record the
 * chrnum we dispatched for at write time and re-check it at read time;
 * any slot where snap_chrnum != chrs[i]->chrnum skips the apply.
 *
 * Two slots in parallel with the GL ring so we can keep the GPU-side
 * boid record at exactly 80 bytes (the std430 mirror). Stored as
 * int16 since chrnum is s16; -1 sentinel means "no chr was dispatched
 * for this slot at write time" (i.e. consumer must skip even if chrs[i]
 * is non-NULL at read time, because we'd be applying garbage). */
static s16     s_ReadbackRingChrnum[SWARM_READBACK_RING_DEPTH][SWARM_GPU_MAX];
/* Active chrnum snapshot for the data currently in s_BoidScratch.
 * Set by the consumer when copying from the ring; used by the apply /
 * sample / AI loops to skip slots whose chr identity has changed. */
static s16     s_ScratchChrnum[SWARM_GPU_MAX];

/* ------------------------------------------------------------------
 * c029 Slice 3 (2026-05-16): per-bot floor cache (Strategy 1A hybrid).
 *
 * Problem: the pre-Slice-3 swarm tick raycasted the BG floor THREE times
 * per active bot per frame: (1) the pre-pass chrSurfaceLocoSampleFloorNormal
 * call before the SSBO upload, plus (2) two cdFindGroundInfoAtCyl calls
 * buried inside chrSetPos (one in chrMoveToPos's pre-flight, one inline).
 * At 512 bots that's 1536 BG raycasts per frame -- the dominant non-GL
 * cost of the GPU swarm path.
 *
 * Insight: most bots in a swarm aren't moving very far per frame. The
 * surface normal under a bot that drifted < 60 cm in the last frame is
 * essentially the same surface; resampling it is wasted work. A pure
 * cell-bucketed share (one raycast per XZ cell per frame) has correctness
 * risk on stairs / overlapping floor tiles where two bots in the same
 * cell sit on different tiles. A pure stationary cache fails when most
 * bots ARE moving.
 *
 * Solution: per-bot last-sample-pos + last-surface-up + last-ground +
 * last-frame-stamp arrays. Reuse the cached values when:
 *   - |pos - s_LastSamplePos[i]| < SWARM_FLOOR_SAMPLE_DRIFT_CM, AND
 *   - (current_frame - s_LastSampleFrame[i]) < SWARM_FLOOR_SAMPLE_MAX_AGE_FRAMES
 *
 * Otherwise call the sampler + cdFindGroundInfoAtCyl and refresh the
 * cache. Per-frame perf-stats counters expose the hit rate so we can
 * tune the thresholds from playtest log.
 *
 * Identity hygiene: the cache is indexed by slot. When death_poll_and_
 * respawn swaps a fresh chr into a slot, the existing s_ScratchChrnum
 * identity guard already skips the apply loop for that slot. We also
 * invalidate this floor cache via swarmGpuInvalidateFloorCache(), which
 * the cycler calls alongside swarmGpuInvalidateReadback. Stale-but-
 * matching-chrnum data would still be detected by the drift check (a
 * respawn moves the chr by more than 60 cm in nearly every case), but
 * the explicit invalidate is the belt-and-braces guard. */
#define SWARM_FLOOR_SAMPLE_DRIFT_CM        60.0f
#define SWARM_FLOOR_SAMPLE_MAX_AGE_FRAMES  10

struct swarm_floor_cache_entry {
	f32      last_x, last_y, last_z;
	f32      sux, suy, suz;
	f32      ground;
	u16      floorcol;
	u8       floortype;
	RoomNum  floorroom;
	s32      last_frame;
	s32      valid;          /* 0 = never sampled; 1 = cached data present */
};
static struct swarm_floor_cache_entry s_FloorCache[SWARM_GPU_MAX];
static s32 s_FloorFrameCounter = 0;   /* monotonic; increments per dispatch */

/* Per-frame stats reset to 0 at the top of each dispatch; surfaced via
 * the BENCHMARK.SWARM.GPU.PERF log line at the 60-frame interval. */
static s32 s_PerfSampled = 0;
static s32 s_PerfReused  = 0;

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

	s_glDispatchCompute   = (swarm_glDispatchCompute_t)
		SDL_GL_GetProcAddress("glDispatchCompute");
	s_glMemoryBarrier     = (swarm_glMemoryBarrier_t)
		SDL_GL_GetProcAddress("glMemoryBarrier");
	s_glBindBufferBase    = (swarm_glBindBufferBase_t)
		SDL_GL_GetProcAddress("glBindBufferBase");
	/* Slice 2 c029 (2026-05-16): async readback ring symbols. Required
	 * for the new code path; if any fail to load we silently keep the
	 * blocking fallback. glCopyBufferSubData is core in GL 3.1+; the
	 * fence/sync trio is core in GL 3.2+. Both are guaranteed by the
	 * GL 4.3 minimum we already checked above, but we still verify
	 * each symbol load to be defensive against driver quirks. */
	s_glCopyBufferSubData = (swarm_glCopyBufferSubData_t)
		SDL_GL_GetProcAddress("glCopyBufferSubData");
	s_glFenceSync         = (swarm_glFenceSync_t)
		SDL_GL_GetProcAddress("glFenceSync");
	s_glClientWaitSync    = (swarm_glClientWaitSync_t)
		SDL_GL_GetProcAddress("glClientWaitSync");
	s_glDeleteSync        = (swarm_glDeleteSync_t)
		SDL_GL_GetProcAddress("glDeleteSync");
	if (!s_glDispatchCompute || !s_glMemoryBarrier || !s_glBindBufferBase) {
		sysLogPrintf(LOG_WARNING,
			"BENCHMARK.SWARM.GPU: GL 4.3 advertised but compute symbol load "
			"failed (dispatch=%p barrier=%p bindbase=%p)",
			(void *)s_glDispatchCompute, (void *)s_glMemoryBarrier,
			(void *)s_glBindBufferBase);
		return 0;
	}
	if (!s_glCopyBufferSubData || !s_glFenceSync || !s_glClientWaitSync
			|| !s_glDeleteSync) {
		/* Async-readback symbols missing -- not fatal. The dispatch loop
		 * detects NULL function pointers and falls back to the synchronous
		 * glGetBufferSubData path. Pre-Slice-2 perf in that case (still
		 * functional, just slower at 256+ bots). */
		sysLogPrintf(LOG_WARNING,
			"BENCHMARK.SWARM.GPU: async-readback symbols missing "
			"(copy=%p fence=%p wait=%p delete=%p); using blocking readback",
			(void *)s_glCopyBufferSubData, (void *)s_glFenceSync,
			(void *)s_glClientWaitSync, (void *)s_glDeleteSync);
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
	/* Slice 2 c029: allocate the readback ring buffers. Skip if the
	 * async symbols failed probe; the blocking path doesn't need them. */
	if (s_glCopyBufferSubData && s_glFenceSync && s_glClientWaitSync
			&& s_glDeleteSync && s_ReadbackRingBuf[0] == 0) {
		glGenBuffers(SWARM_READBACK_RING_DEPTH, s_ReadbackRingBuf);
		for (s32 i = 0; i < SWARM_READBACK_RING_DEPTH; i++) {
			/* Bind as COPY_WRITE so the driver knows the intended usage;
			 * we'll bind it as COPY_READ when reading. Either binding
			 * point is fine for the underlying GL buffer object -- the
			 * binding semantics are just hints. */
			glBindBuffer(GL_COPY_WRITE_BUFFER, s_ReadbackRingBuf[i]);
			glBufferData(GL_COPY_WRITE_BUFFER,
				sizeof(boid_record) * SWARM_GPU_MAX,
				NULL, GL_DYNAMIC_DRAW);
		}
		glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
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

/* Slice 2 c029 (2026-05-16): invalidate the async readback ring.
 *
 * Called from swarm_test.c whenever the chr-slot identity has changed
 * (cycler tick after respawn_swarm, session start/end, stage transition).
 * Drops any in-flight fences and zeroes the per-slot count so the next
 * 2 frames produce consumed_count=0 in swarmGpuStepAndApply, meaning
 * downstream apply / sample / AI loops bound by apply_count skip
 * entirely. The freshly dispatched compute fills the ring on those
 * frames; by frame N+2 the consumer has valid current-identity data.
 *
 * If the async ring isn't armed (driver failed probe / first call
 * before ensure_resources()), the function is a safe no-op. */
void swarmGpuInvalidateReadback(void)
{
	if (!s_glDeleteSync) return;
	for (s32 i = 0; i < SWARM_READBACK_RING_DEPTH; i++) {
		if (s_ReadbackRingFence[i]) {
			s_glDeleteSync(s_ReadbackRingFence[i]);
			s_ReadbackRingFence[i] = NULL;
		}
		s_ReadbackRingCount[i] = 0;
		/* Wipe the per-slot chrnum snapshot. Defensive: even if a future
		 * code path reads s_ReadbackRingChrnum without re-stamping, the
		 * -1 sentinel guarantees the consumer's identity check fails
		 * cleanly (i.e. no false-match against a brand-new chr that
		 * happens to share a chrnum with one freed during the cycler). */
		memset(s_ReadbackRingChrnum[i], 0xff,
			sizeof(s_ReadbackRingChrnum[i]));
	}
	/* And the active scratch chrnum array. The fence path normally
	 * refills this only on a successful consume; an invalidate just
	 * before a tick should treat the existing scratch as garbage. */
	memset(s_ScratchChrnum, 0xff, sizeof(s_ScratchChrnum));
	/* s_ReadbackWriteIdx intentionally not reset -- the next dispatch
	 * advances it normally. The XOR-1 read slot will simply have its
	 * fence NULL so the consumer skips. */
}

/* c029 Slice 3 (2026-05-16): wipe the per-bot floor cache.
 *
 * Called from swarm_test.c at the same junction points as
 * swarmGpuInvalidateReadback -- session start / end, cycler tick,
 * despawn_all. After a respawn the slot's chr identity has shifted to
 * a fresh chr at a (possibly) different XYZ. Clearing the cache forces
 * the merged-walk loop to take the full sample path on the next frame
 * rather than reusing a sample that was taken under the prior chr's
 * position. The drift threshold alone would catch this most of the
 * time (respawn distance is usually >> 60 cm), but explicit invalidation
 * is correct-by-construction; the dropped 1 frame of cache hits is
 * imperceptible.
 *
 * Safe no-op if called before any dispatch has populated the cache. */
void swarmGpuInvalidateFloorCache(void)
{
	memset(s_FloorCache, 0, sizeof(s_FloorCache));
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
	 * pre-Slice-6 XZ seek behaviour.
	 *
	 * c029 Slice 3 (2026-05-16): NOT a fresh sample every frame. We now
	 * consult s_FloorCache[i] -- if the chr drifted < DRIFT_CM AND the
	 * cache entry is younger than MAX_AGE_FRAMES, reuse the cached
	 * surface_up. This is the upload side of the merged walk; the apply
	 * side below uses the same cache state (refreshed by this loop) to
	 * call chrSetPosWithCachedGround with cached ground / floorcol /
	 * floortype / floorroom, eliminating the chrSetPos-internal raycast
	 * pair. The cache is invalidated by swarmGpuInvalidateFloorCache()
	 * at cycler-tick junctions so identity hygiene is preserved. */
	s_FloorFrameCounter++;
	s_PerfSampled = 0;
	s_PerfReused = 0;

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

			struct swarm_floor_cache_entry *ce = &s_FloorCache[i];
			const f32 dx = chr->prop->pos.x - ce->last_x;
			const f32 dy = chr->prop->pos.y - ce->last_y;
			const f32 dz = chr->prop->pos.z - ce->last_z;
			const f32 drift2 = dx * dx + dy * dy + dz * dz;
			const f32 max_drift2 = SWARM_FLOOR_SAMPLE_DRIFT_CM
				* SWARM_FLOOR_SAMPLE_DRIFT_CM;
			const s32 age = s_FloorFrameCounter - ce->last_frame;

			if (ce->valid && drift2 < max_drift2
					&& age < SWARM_FLOOR_SAMPLE_MAX_AGE_FRAMES) {
				/* Cache hit. Skip BOTH BG raycasts (the surface_up sample
				 * AND the cdFindGroundInfoAtCyl call for the apply path).
				 * Reuse the cached vector + ground info. */
				s_BoidScratch[i].sux = ce->sux;
				s_BoidScratch[i].suy = ce->suy;
				s_BoidScratch[i].suz = ce->suz;
				s_PerfReused++;
			} else {
				/* Cache miss / stale. Take the full sample path: one
				 * raycast for surface_up + one for ground / floorcol /
				 * floortype / floorroom. Cache the lot for next frame. */
				f32 sup[3];
				(void)chrSurfaceLocoSampleFloorNormal(chr, sup);
				s_BoidScratch[i].sux = sup[0];
				s_BoidScratch[i].suy = sup[1];
				s_BoidScratch[i].suz = sup[2];

				u16 fc = 0;
				u8  ft = 0;
				RoomNum fr = -1;
				const f32 g = cdFindGroundInfoAtCyl(&chr->prop->pos,
					chr->radius, chr->prop->rooms, &fc, &ft, NULL,
					&fr, NULL, NULL);

				ce->last_x = chr->prop->pos.x;
				ce->last_y = chr->prop->pos.y;
				ce->last_z = chr->prop->pos.z;
				ce->sux       = sup[0];
				ce->suy       = sup[1];
				ce->suz       = sup[2];
				ce->ground    = g;
				ce->floorcol  = fc;
				ce->floortype = ft;
				ce->floorroom = fr;
				ce->last_frame = s_FloorFrameCounter;
				ce->valid     = 1;
				s_PerfSampled++;
			}
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
	/* c029 (2026-05-16): tuned from 27.0 -> 5.0 per Mike playtest "GPU
	 * bots are insanely fast." See the sprint report at
	 * .claude/sprint-reports/sprint-2026-05-16T160500-gpu-bot-speed.md
	 * for the full triage path; the math summary is inline below.
	 *
	 * Effective per-frame motion math
	 * -------------------------------
	 * The compute kernel computes:
	 *   step  = max_speed * dt * 60.0 = max_speed * 1.0  (dt = 1/60)
	 *   vel   = (to_player / d) * step                   (|vel| = step)
	 *   pos  += vel * dt * 60.0      = vel * 1.0
	 *
	 * Both `dt * 60.0` factors collapse to identity at the 60 Hz target,
	 * so `max_speed` is functionally `units of position per dispatch`
	 * NOT `units per second`. One dispatch happens per rendered frame.
	 *
	 * 1 game unit ~ 1 cm in this engine. The prior 27.0 produced
	 *  ~1620 cm/sec straight-line beelines -- 5-9x faster than the
	 *  Perfect Dark "running enemy" feel and visibly teleporty.
	 *
	 * Why 5.0:
	 *  - CPU swarm bots cap at botCalculateMaxSpeed = 7.5 (the swarm
	 *    CHRHFLAG-gated ceiling in bot.c). Their per-frame motion runs
	 *    through the moverate accumulator + cosf/sinf heading + navnet
	 *    face-turn time + obstacle avoidance, so the effective beeline
	 *    velocity is well under 7.5 / frame. The GPU shader has no such
	 *    throttle -- it always moves the full max_speed per dispatch in
	 *    a perfect line at the player.
	 *  - 5.0 units/frame at 60 Hz = 300 cm/sec straight-line, which
	 *    sits comfortably in the OG "running enemy" 180-360 cm/sec
	 *    band the player feels in regular MP combat.
	 *  - The prior 27.0 was sourced from 18.0 * 1.5 (S593d, 2026-05-01)
	 *    where "18.0" was the original GPU constant and "1.5" was a
	 *    BOTDIFF_PERFECT difficulty bump intent. Both turned out to be
	 *    units-per-frame, NOT units-per-second, so the *1.5 stacked on
	 *    a value that was already too high.
	 *
	 * Tunable. If 5.0 feels too sluggish in the next playtest, bump in
	 * 1.0 increments and re-check the GPU.AI summary log range_min /
	 * avg / max values for "bots stop closing on the player" behaviour.
	 * The shader's d-clamp at line ~327 below already prevents
	 * overshoot when range is smaller than one step. */
	s_Params.max_speed     = 5.0f;
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

	/* ------------------------------------------------------------------
	 * Slice 2 c029 (2026-05-16): async readback ring.
	 *
	 * The pre-Slice-2 code did a single synchronous glGetBufferSubData
	 * here, which pipeline-drained the GPU every frame and dominated
	 * frame time at 256+ bots (Mike's playtest showed 12.5 ticks/sec at
	 * 512 bots / 80ms/frame). The new path uses a 2-deep PBO-style ring:
	 *
	 *   write_idx = current ring slot (where we copy THIS frame's data)
	 *   read_idx  = write_idx ^ 1 (where LAST frame's data lives)
	 *
	 * Steps per frame:
	 *   1. Consume ring[read_idx]: if its fence is signaled, read into
	 *      s_BoidScratch (= last frame's results). The fence has had a
	 *      full frame to land, so glGetBufferSubData here does NOT
	 *      stall -- the data is already on the CPU-visible side.
	 *   2. Copy this frame's BoidSsbo -> ring[write_idx] via
	 *      glCopyBufferSubData (GPU-side memcpy, no sync).
	 *   3. Insert a fence after the copy; record count snapshot.
	 *   4. Advance write_idx.
	 *
	 * Bootstrap: first 2 frames have no consumable data; downstream
	 * chr position application and AI dispatch skip if the consumed
	 * count is 0.
	 *
	 * Fallback: if probe_compute() failed to load the async symbols
	 * (s_glCopyBufferSubData / s_glFenceSync / s_glClientWaitSync /
	 * s_glDeleteSync NULL), we fall back to the synchronous path so
	 * the GPU swarm still works on quirky drivers. */
	s32 consumed_count = 0;
	const bool ring_armed =
		(s_glCopyBufferSubData != NULL && s_glFenceSync != NULL
		 && s_glClientWaitSync != NULL && s_glDeleteSync != NULL
		 && s_ReadbackRingBuf[0] != 0);

	if (ring_armed) {
		const s32 read_idx = s_ReadbackWriteIdx ^ 1; /* depth=2 */
		void *fence = s_ReadbackRingFence[read_idx];
		if (fence) {
			/* Non-blocking poll. Timeout=0 means "check if signaled and
			 * return immediately." Driver returns ALREADY_SIGNALED if
			 * the fence was already done, CONDITION_SATISFIED if it
			 * just signaled during the call, or TIMEOUT_EXPIRED if
			 * still pending. We treat the first two as "data is ready,"
			 * the third as "skip this frame's consumer" (the GPU is
			 * still working, don't drain it). The GL spec guarantees
			 * fences become signaled when ALL prior commands complete;
			 * since we issued the copy a full frame ago, it should
			 * essentially always be signaled by now. If we DO hit
			 * TIMEOUT_EXPIRED that's a stronger perf signal than the
			 * blocking get -- the GPU is genuinely behind. */
			const GLenum wait = s_glClientWaitSync(fence, 0, 0);
			if (wait == GL_ALREADY_SIGNALED
					|| wait == GL_CONDITION_SATISFIED) {
				consumed_count = s_ReadbackRingCount[read_idx];
				if (consumed_count > 0) {
					glBindBuffer(GL_COPY_READ_BUFFER,
						s_ReadbackRingBuf[read_idx]);
					glGetBufferSubData(GL_COPY_READ_BUFFER, 0,
						sizeof(boid_record) * consumed_count,
						s_BoidScratch);
					glBindBuffer(GL_COPY_READ_BUFFER, 0);
					/* Mirror the per-slot chrnum snapshot into the
					 * apply-side array so the consumer can detect chr
					 * identity changes (death_poll_and_respawn refills
					 * one slot per frame; the cycler can swap all
					 * slots in one tick -- swarmGpuInvalidateReadback
					 * covers the latter, this covers the former). */
					memcpy(s_ScratchChrnum,
						s_ReadbackRingChrnum[read_idx],
						sizeof(s16) * consumed_count);
				}
				s_glDeleteSync(fence);
				s_ReadbackRingFence[read_idx] = NULL;
				s_ReadbackRingCount[read_idx] = 0;
			}
			/* If wait == TIMEOUT_EXPIRED, leave the fence in place;
			 * we'll try again next frame. consumed_count stays 0 so
			 * the downstream consumer skips this frame. */
		}

		/* Queue THIS frame's copy + fence into the write slot. */
		const s32 write_idx = s_ReadbackWriteIdx;
		/* Drop any stale fence in the write slot (shouldn't happen with
		 * depth-2 ring, but defensive against double-fence leak). */
		if (s_ReadbackRingFence[write_idx]) {
			s_glDeleteSync(s_ReadbackRingFence[write_idx]);
			s_ReadbackRingFence[write_idx] = NULL;
		}
		glBindBuffer(GL_COPY_READ_BUFFER, s_BoidSsbo);
		glBindBuffer(GL_COPY_WRITE_BUFFER, s_ReadbackRingBuf[write_idx]);
		s_glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
			0, 0, (GLsizeiptr)(sizeof(boid_record) * count));
		glBindBuffer(GL_COPY_READ_BUFFER, 0);
		glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
		s_ReadbackRingFence[write_idx] =
			s_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		s_ReadbackRingCount[write_idx] = count;
		/* Stamp the chrnum the GPU just computed for. The consumer (next
		 * frame's pass through this function) checks this against the
		 * live chrs[i]->chrnum and skips the apply if death_poll_and_
		 * respawn swapped a fresh chr into the slot. NULL chr -> -1
		 * sentinel; the consumer never matches -1 so those slots also
		 * get skipped, which is correct (no position to apply). */
		for (s32 i = 0; i < count; i++) {
			struct chrdata *c = chrs[i];
			s_ReadbackRingChrnum[write_idx][i] =
				(c && c->chrnum >= 0) ? (s16)c->chrnum : (s16)-1;
		}
		s_ReadbackWriteIdx = (s_ReadbackWriteIdx + 1) % SWARM_READBACK_RING_DEPTH;
	} else {
		/* Fallback: blocking readback (pre-Slice-2 behaviour). The data
		 * is current-frame so chr identity is trivially consistent;
		 * stamp s_ScratchChrnum from the live chrs so the downstream
		 * identity check matches and the apply proceeds normally. */
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, s_BoidSsbo);
		glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
			sizeof(boid_record) * count, s_BoidScratch);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		for (s32 i = 0; i < count; i++) {
			struct chrdata *c = chrs[i];
			s_ScratchChrnum[i] = (c && c->chrnum >= 0)
				? (s16)c->chrnum : (s16)-1;
		}
		consumed_count = count;
	}

	/* From here on, `consumed_count` is the authoritative number of
	 * bots whose data lives in s_BoidScratch[]. In the async ring path
	 * that may be 0 (bootstrap or fence not yet signaled) or last-frame's
	 * count (which could differ from THIS frame's count if the cycler
	 * just changed the ladder). We clamp downstream loops to the smaller
	 * of (consumed_count, count) so we never read past the snapshot OR
	 * past the live chr array. */
	const s32 apply_count = (consumed_count < count) ? consumed_count : count;

	/* c029 Slice 3 + Track 5 merged-walk (2026-05-16).
	 *
	 * One walk of chrs[0..apply_count) does all post-readback work:
	 *   1. Identity check (chr + chrnum + model + snapshot identity match)
	 *   2. Read shader's per-bot position + velocity from s_BoidScratch
	 *   3. Compute face_deg from velocity (idle keeps yaw)
	 *   4. Call chrSetPosWithCachedGround using s_FloorCache[i].ground +
	 *      floorcol/floortype/floorroom. The cache was refreshed in the
	 *      pre-pass upload loop above; here we just consume it. Zero
	 *      BG raycasts (vs the pre-Slice-3 chrSetPos(findground=true)
	 *      path which did 2 internal raycasts per call).
	 *   5. Accumulate velocity stats for the 1 Hz BENCHMARK.SWARM.GPU.VEL
	 *      log (was its own walk pre-c029-Slice-3).
	 *   6. If do_ai, accumulate AI stats AND dispatch the per-bot
	 *      swarmTestApplyAiDecision side-effect call (was its own walk
	 *      pre-c029-Slice-3).
	 *
	 * Net: pre-Slice-3 had 4 walks (pre-pass + apply + vel + AI) doing
	 * 3 BG raycasts per active bot per frame (1 sampler + 2 chrSetPos
	 * internal). Post-Slice-3 has 2 walks (cache-driven pre-pass + this
	 * merged post-walk) doing at most 2 BG raycasts per active bot on a
	 * cache miss, and 0 on a cache hit. Expected hit rate at 512+ bot
	 * tiers is 70%+ since most bots cluster around the player and barely
	 * drift per frame.
	 *
	 * Slice 2 c029 still holds: loop bound is `apply_count`, not `count`.
	 * apply_count may be 0 (bootstrap or fence pending) or last frame's
	 * count (cycler-tick ladder change); we never read past the snapshot
	 * or past the live chr array. */
	static s32 s_VelLogTick = 0;
	static s32 s_AiLogTick = 0;
	f32 max_v2 = 0.0f;
	double sum_v = 0.0;
	s32 v_samples = 0;
	s32 n_idle = 0, n_seek = 0, n_attack = 0, n_fire = 0;
	f32 sum_range = 0.0f;
	f32 min_range = 1.0e30f, max_range = 0.0f;
	s32 ai_sample_count = 0;

	for (s32 i = 0; i < apply_count; i++) {
		struct chrdata *chr = chrs[i];
		if (!chr || !chr->prop || chr->chrnum < 0 || chr->model == NULL) {
			continue;
		}
		/* Identity guard: skip slots that were refilled by
		 * death_poll_and_respawn since the snapshot was taken. */
		if (s_ScratchChrnum[i] != (s16)chr->chrnum) {
			continue;
		}

		/* Position apply via the cached-ground helper -- 0 raycasts. */
		struct coord newpos;
		newpos.x = s_BoidScratch[i].px;
		newpos.y = s_BoidScratch[i].py;
		newpos.z = s_BoidScratch[i].pz;

		const float vx = s_BoidScratch[i].vx;
		const float vz = s_BoidScratch[i].vz;
		f32 face_deg = 0.0f;
		if (vx * vx + vz * vz > 0.001f) {
			face_deg = atan2f(vx, vz) * (180.0f / 3.14159265f);
			if (face_deg < 0.0f) face_deg += 360.0f;
		}

		RoomNum rooms[8];
		for (s32 r = 0; r < 8; r++) {
			rooms[r] = chr->prop->rooms[r];
		}

		const struct swarm_floor_cache_entry *ce = &s_FloorCache[i];
		/* c029 Slice 3 (2026-05-16, B-332 mitigation):
		 *
		 * Initial implementation called chrSetPosWithCachedGround when
		 * ce->valid was true, eliminating the chrSetPos-internal raycast
		 * pair. Smoke test surfaced a regression at the 8->16 cycle
		 * transition: 3 of 8 newly-spawned chrs ended up in a state
		 * where chr->prop was set but chr->prop->chr was NULL,
		 * triggering AV in despawn_all->chrRemove on the next cycle.
		 * Root cause not yet identified; the difference vs legacy
		 * chrSetPos is the omitted chrMoveToPos pre-flight
		 * (chrAdjustPosForSpawn + chr0f0220ac room re-register at
		 * spawn-adjusted rooms2), so the prop's room state is one step
		 * behind what the chr code expects.
		 *
		 * Conservative mitigation: keep the chrSetPos call (both
		 * internal raycasts) for the apply path. The floor cache still
		 * earns its keep on the surface_up side (chrSurfaceLocoSampleFloorNormal
		 * skipped for cache hits in the pre-pass above). At 70%+ hit
		 * rate at 512 bots the surface_up sampler is skipped for ~360
		 * bots/frame; the chrSetPos raycasts remain at 2 per active bot.
		 *
		 * Net raycast budget:
		 *   - Pre-c029-Slice-3: 3 per bot (sampler + 2 inside chrSetPos)
		 *   - Slice-3 with cache + cached helper (regressed):
		 *       miss=2, hit=0
		 *   - Slice-3 with cache only (this conservative path):
		 *       miss=2 (sampler + cached g/fc/ft/fr), hit=2 (chrSetPos)
		 *
		 * The conservative path saves at most 1 raycast per bot on a
		 * cache hit (the surface_up sample). At 70% hit rate / 512 bots
		 * that's ~360 raycasts/frame saved vs original 1536 = 23% cut.
		 * Less than the Slice-3 target but the regression class is
		 * eliminated; the deeper helper-vs-chrSetPos investigation can
		 * land in a follow-up sprint with the right shape.
		 *
		 * The cache write-through still happens in the pre-pass above
		 * so that a future re-introduction of the cached helper has
		 * the values ready. The unused cached g/fc/ft/fr fields are
		 * not currently consumed but cost only a few bytes per slot. */
		(void)ce;
		chrSetPos(chr, &newpos, rooms, face_deg, true);

		/* Velocity stats accumulator -- emitted at the 60-frame interval
		 * below. Was its own walk pre-c029-Slice-3. */
		const f32 v2 = vx * vx + vz * vz;
		if (v2 > max_v2) max_v2 = v2;
		sum_v += sqrt((double)v2);
		v_samples++;

		/* AI side-effect + stats. Was its own walk pre-c029-Slice-3. */
		if (do_ai) {
			const s32 act  = s_BoidScratch[i].action_class;
			const s32 fire = s_BoidScratch[i].fire_request;
			const s32 anim = s_BoidScratch[i].anim_key;
			const s32 tgt  = s_BoidScratch[i].target_propnum;
			const f32 r    = s_BoidScratch[i].range_to_target;

			(void)swarmTestApplyAiDecision(chr, i, tgt, act, fire, anim, r);

			if      (act == 2) n_attack++;
			else if (act == 1) n_seek++;
			else               n_idle++;
			if (fire) n_fire++;
			sum_range += r;
			if (r < min_range) min_range = r;
			if (r > max_range) max_range = r;
			ai_sample_count++;
		}
	}

	/* c029 Slice 3 perf-stats line. Once per 60 frames; reports the cache
	 * hit/miss ratio + derived raycast load.
	 *
	 * B-332 mitigation: with the chrSetPos refactor backed out for the
	 * apply path, each active bot still incurs 2 raycasts inside
	 * chrSetPos(findground=true). The floor cache saves the surface_up
	 * sampler raycast: HIT = 0 raycasts in pre-pass; MISS = 2 raycasts
	 * in pre-pass (sampler + cdFindGroundInfoAtCyl, written through to
	 * the cache for the next frame even though the apply path doesn't
	 * read them in this mitigation). Total raycasts per bot per frame:
	 *   - HIT  in pre-pass:  0 sampler + 2 chrSetPos    = 2
	 *   - MISS in pre-pass:  2 sampler + 2 chrSetPos    = 4
	 *
	 * `pre_pass_raycasts` is what the cache directly saves; the
	 * chrSetPos-internal raycasts are not reflected in this counter. */
	{
		static s32 s_PerfLogTick = 0;
		s_PerfLogTick++;
		if (s_PerfLogTick >= 60) {
			s_PerfLogTick = 0;
			const s32 total = s_PerfSampled + s_PerfReused;
			const s32 pre_pass_raycasts = s_PerfSampled * 2;
			const s32 chrsetpos_raycasts = total * 2;
			const s32 total_raycasts = pre_pass_raycasts + chrsetpos_raycasts;
			const double hit_pct = (total > 0)
				? (100.0 * (double)s_PerfReused / (double)total) : 0.0;
			sysLogPrintf(LOG_NOTE,
				"BENCHMARK.SWARM.GPU.PERF: count=%d sampled=%d reused=%d "
				"hit_pct=%.1f pre_pass_raycasts=%d chrsetpos_raycasts=%d "
				"total_raycasts=%d drift_cm=%.1f max_age_frames=%d",
				count, s_PerfSampled, s_PerfReused, hit_pct,
				pre_pass_raycasts, chrsetpos_raycasts, total_raycasts,
				(double)SWARM_FLOOR_SAMPLE_DRIFT_CM,
				SWARM_FLOOR_SAMPLE_MAX_AGE_FRAMES);
		}
	}

	/* Velocity log (1 Hz). Was its own walk pre-c029-Slice-3; stats now
	 * accumulated inline in the merged walk above. */
	s_VelLogTick++;
	if (s_VelLogTick >= 60) {
		s_VelLogTick = 0;
		const double max_v = sqrt((double)max_v2);
		const double avg_v = (v_samples > 0)
			? (sum_v / (double)v_samples) : 0.0;
		sysLogPrintf(LOG_NOTE,
			"BENCHMARK.SWARM.GPU.VEL: count=%d active=%d "
			"max_unit_per_frame=%.2f avg_unit_per_frame=%.2f "
			"max_cm_per_sec=%.0f avg_cm_per_sec=%.0f "
			"max_speed_cap=%.1f",
			count, v_samples,
			max_v, avg_v,
			max_v * 60.0,
			avg_v * 60.0,
			(double)s_Params.max_speed);
	}

	/* AI summary log (1 Hz). Was its own walk pre-c029-Slice-3; stats +
	 * dispatch now inline in the merged walk above. The drain on the
	 * "fires actually applied" counter must run every tick (it resets
	 * the per-tick accumulator in swarm_test.c), independent of whether
	 * we emit a log line; the captured value is consumed only when the
	 * 60-frame throttle fires. */
	if (do_ai) {
		const s32 fires_applied = swarmTestGetAndResetGpuAiFireCount();
		s_AiLogTick++;
		if (s_AiLogTick >= 60) {
			s_AiLogTick = 0;
			const f32 avg_range = (ai_sample_count > 0)
				? (sum_range / (f32)ai_sample_count) : 0.0f;
			sysLogPrintf(LOG_NOTE,
				"BENCHMARK.SWARM.GPU.AI: count=%d idle=%d seek=%d attack=%d "
				"fire_req=%d fire_applied_lastframe=%d range_min=%.0f avg=%.0f "
				"max=%.0f fire_range=%.0f target_propnum=%d",
				count, n_idle, n_seek, n_attack, n_fire,
				fires_applied,
				(double)((min_range > 1.0e29f) ? 0.0f : min_range),
				(double)avg_range,
				(double)max_range,
				(double)SWARM_AI_FIRE_RANGE,
				player_propnum);

			/* Burst-log up to SWARM_AI_LOG_MAX individual firing-bot
			 * decisions. Cheap (apply_count walk, log only on
			 * fire_request) and fires once per 60 frames, so it stays
			 * as a separate small walk rather than threading log-quota
			 * state through the merged walk above. */
			s32 logged = 0;
			for (s32 i = 0; i < apply_count && logged < SWARM_AI_LOG_MAX; i++) {
				struct chrdata *chr = chrs[i];
				if (!chr || !chr->prop || chr->chrnum < 0
						|| chr->model == NULL) {
					continue;
				}
				if (s_ScratchChrnum[i] != (s16)chr->chrnum) {
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
		} else {
			/* fires_applied is read once per 60-frame log throttle but
			 * the GetAndReset call must still happen each tick so the
			 * counter never accumulates indefinitely. */
			(void)fires_applied;
		}
	}
}

} /* extern "C" */
