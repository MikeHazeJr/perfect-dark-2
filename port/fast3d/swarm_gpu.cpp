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
 *    (c3807, 2026-05-15) the per-boid record is 96 bytes -- pos + vel +
 *    surface_up (the legacy 48-byte block) plus 48 bytes of GPU-AI
 *    fields (action class, anim key, fire request, target propnum,
 *    range to target, jump request, and surface request). The legacy
 *    block is filled and consumed in both
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
 * v1 shipped pure seek-player so the GPU side was byte-for-byte
 * equivalent to swarm_test.c::cpu_seek_tick. Track 4 v0 (c3807,
 * 2026-05-16) extends the kernel with classical Reynolds (1987) boids
 * steering -- separation, alignment, cohesion -- accumulated naively
 * over all peers in the same SSBO (O(N^2) inner loop) and blended with
 * the seek vector. Final blended steering is normalised + scaled to
 * max_speed so the velocity magnitude bound the smoke test asserts on
 * (max_unit_per_frame < 6.0, max_speed_cap=5.0) still holds. The
 * surface-plane projection from Slice 6 wraps the BLENDED steering, so
 * boids respects sloped-floor locomotion.
 *
 * Spatial-grid v1 (k-nearest broad-phase) is a follow-up slice; v0 just
 * ships the math + weights so behaviour can be observed and tuned.
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
/* Track 2d (c3807, 2026-05-16): listen-host GPU swarm state sync. The
 * post-dispatch tail of swarmGpuStepAndApply calls netSendGpuSwarmState
 * to broadcast the state texture to connected peers. g_NetMode +
 * g_NetDedicated checks gate the call to listen-host only. The helper
 * itself is also pd-server-aware via #if !defined(PD_SERVER).
 *
 * net.h / netmsg.h are C headers with no extern "C" wrappers. Wrap the
 * includes locally so their function declarations get C linkage and
 * resolve against the C-side definitions in net.c / netmsg.c. */
extern "C" {
#include "net/net.h"
#include "net/netmsg.h"
} /* extern "C" -- net.h/netmsg.h */

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
extern s32 swarmTestApplyMovementIntent(struct chrdata *chr,
                                        s32 slot_index,
                                        s32 target_propnum,
                                        s32 jump_request,
                                        s32 surface_request,
                                        f32 range_to_target,
                                        const f32 *vel_hint);
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
/* Track 2a (c3807, 2026-05-16): GL constants for the ping-pong RGBA32F
 * state textures. GL_RGBA32F is already in glad.h (0x8814) but the
 * image-binding / barrier constants are NOT, so we forward-declare them
 * here to avoid bringing in extra GL headers that may not be present on
 * every MinGW SDK. Values per OpenGL 4.2+ spec; image-binding entry
 * points are core GL since 4.2 and accept these tokens unchanged. */
#ifndef GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#endif
#ifndef GL_TEXTURE_2D
#define GL_TEXTURE_2D                     0x0DE1
#endif
#ifndef GL_TEXTURE_MIN_FILTER
#define GL_TEXTURE_MIN_FILTER             0x2801
#endif
#ifndef GL_TEXTURE_MAG_FILTER
#define GL_TEXTURE_MAG_FILTER             0x2800
#endif
#ifndef GL_NEAREST
#define GL_NEAREST                        0x2600
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
/* Track 2a (c3807, 2026-05-16): glBindImageTexture is core GL 4.2+ but
 * not exposed by the project's pinned glad gen. We load it dynamically
 * via SDL_GL_GetProcAddress like the rest of the compute symbol pack;
 * if the load fails we silently disable the texture mirror and the
 * dispatch path continues to write only the SSBO (bit-exact pre-2a). */
typedef void   (APIENTRY *swarm_glBindImageTexture_t)(GLuint, GLuint, GLint,
                                                       GLboolean, GLint,
                                                       GLenum, GLenum);

static swarm_glDispatchCompute_t    s_glDispatchCompute    = NULL;
static swarm_glMemoryBarrier_t      s_glMemoryBarrier      = NULL;
static swarm_glBindBufferBase_t     s_glBindBufferBase     = NULL;
static swarm_glCopyBufferSubData_t  s_glCopyBufferSubData  = NULL;
static swarm_glFenceSync_t          s_glFenceSync          = NULL;
static swarm_glClientWaitSync_t     s_glClientWaitSync     = NULL;
static swarm_glDeleteSync_t         s_glDeleteSync         = NULL;
static swarm_glBindImageTexture_t   s_glBindImageTexture   = NULL;

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
 *  - SSBO    = 4096 * sizeof(boid_record=96) = 393,216 B (~384 KB) once
 *    at first dispatch via glBufferData. OpenGL 4.3 spec requires
 *    GL_MAX_SHADER_STORAGE_BLOCK_SIZE >= 128 MB; nVidia/AMD typically
 *    advertise GB-class limits. Trivial.
 *  - s_BoidScratch BSS = 4096 * 96 = ~384 KB. Static, no allocation.
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
	 * - jump_request / surface_request: compact movement-intent bits
	 *   consumed by the shared benchmark movement helper after readback.
	 */
	int   action_class;
	int   fire_request;
	int   target_propnum;
	int   anim_key;
	float range_to_target, _pad_r0, _pad_r1, _pad_r2;
	int   jump_request;
	int   surface_request;
	int   jump_kind;
	int   _pad_m0;
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
	/* Track 4 v0 (c3807, 2026-05-16): classical Reynolds boids steering
	 * (separation / alignment / cohesion) blended with the seek-player
	 * vector. Naive O(N^2) inner loop reads peer positions and velocities
	 * from the same SSBO. Two vec4-packed groups so std140/std430 padding
	 * is well-defined:
	 *   group A (16 B): boid_sep_radius / boid_align_radius /
	 *                   boid_coh_radius / _pad
	 *   group B (16 B): boid_seek_weight / boid_sep_weight /
	 *                   boid_align_weight / boid_coh_weight
	 * Spatial-grid v1 (k-nearest, broad-phase) is its own follow-up; v0
	 * just ships the steering math so behaviour can be measured. */
	float boid_sep_radius, boid_align_radius, boid_coh_radius, _pad_b0;
	float boid_seek_weight, boid_sep_weight, boid_align_weight, boid_coh_weight;
	/* Track 2a (c3807, 2026-05-16): per-frame gate for the imageStore
	 * mirror to the RGBA32F state texture. 1 = shader writes both SSBO
	 * and texture; 0 = SSBO only (bit-exact pre-2a). Set from the
	 * dispatch site based on whether s_StateTexArmed is non-zero. The
	 * three pads keep the std430 layout vec4-aligned. */
	int   state_tex_enable, _pad_s0, _pad_s1, _pad_s2;
};

static int           s_Probed       = 0;
static int           s_Available    = 0;
static GLuint        s_Program      = 0;
static GLuint        s_BoidSsbo     = 0;
static GLuint        s_ParamsSsbo   = 0;
static boid_record   s_BoidScratch[SWARM_GPU_MAX];
static struct swarm_params s_Params;

/* ------------------------------------------------------------------
 * Track 2a (c3807, 2026-05-16): ping-pong RGBA32F texture state.
 *
 * Two textures sized (SWARM_GPU_MAX wide, 5 tall) hold a mirror of the
 * boid record in a layout amenable to vertex-texture-fetch and to
 * extraction / network sync work in slices 2b-2d. Each bot occupies a
 * 5-texel column:
 *   row 0: pos.xyz, _pad
 *   row 1: vel.xyz, _pad
 *   row 2: surface_up.xyz, _pad
 *   row 3: action_class, fire_request, target_propnum, anim_key
 *          (packed via intBitsToFloat in the shader)
 *   row 4: range_to_target, _pad, _pad, _pad
 *
 * Movement-intent fields are SSBO/readback-only for now; the texture
 * protocol remains 5 rows so existing extraction and sync readers keep
 * their shape.
 * Memory budget: 2 * 4096 * 5 * 16 B = 640 KB. Trivial.
 *
 * Compute writes BOTH the SSBO (existing CPU readback path) AND the
 * texture every frame; bit-exact behaviour parity with pre-2a is
 * preserved because the SSBO is the only thing the CPU side consumes.
 * The texture mirror exists to feed future slices: 2b (kernel reads
 * from texture instead of SSBO), 2c (extraction primitive), 2d (ENet
 * sync). Sub-2a only adds the WRITE path and the ping-pong swap so
 * 2b can land cleanly.
 *
 * 2026-05-16 update: Track 2b INTENTIONALLY SKIPPED. After 2c and 2d
 * shipped (390ae5c7), the texture proved sufficient as a write-only
 * side-channel: CPU consumers (extraction primitive, ENet broadcast)
 * sample it via glGetTexImage; the kernel keeps reading state from the
 * SSBO. Folding the read side into the kernel would have eliminated
 * one SSBO->texture redundancy but added a real texelFetch -> int
 * round trip per bot per frame with no measurable win for the
 * benchmark workload. The named follow-ups in the Track 2d sprint
 * report jump straight to 2e (client prediction), 2f (range-relative
 * pos quantization), 2g (dedicated-server Mode A). The ping-pong
 * swap stays wired so a future 2b can land without breaking the
 * frame-N reads-frame-N-1 invariant.
 *
 * Ping-pong: s_StateTexReadIdx names the texture the NEXT slice will
 * READ from; the kernel writes to the OTHER one. Frame N+1 writes to
 * the texture frame N read from. With 2b deferred indefinitely, the
 * read side stays unused -- we still wire the swap so a future 2b's
 * first run is identical to a steady-state run.
 *
 * Gating: the texture path is armed only after ensure_resources()
 * succeeds AND s_glBindImageTexture loaded. On any failure the shader
 * runs with state_tex_enable = 0 and the imageStore calls are skipped;
 * SSBO behaviour is unchanged.
 *
 * pd-server: swarm_gpu.cpp is NOT linked into pd-server (CMakeLists.txt
 * lists port/fast3d sources explicitly for the server target). So no GL
 * symbols from this file leak into the dedicated-server binary; the
 * texture state is dead code on the server target by construction.
 */
static GLuint        s_StateTexA      = 0;
static GLuint        s_StateTexB      = 0;
static int           s_StateTexReadIdx = 0;
static int           s_StateTexArmed   = 0;

/* ------------------------------------------------------------------
 * Slice 2 c029 (2026-05-16): async readback ring.
 *
 * Problem: pre-Slice-2 we called glGetBufferSubData(BoidSsbo) immediately
 * after the compute dispatch every frame. That call is a SYNCHRONOUS
 * pipeline drain -- it blocks the CPU until ALL queued GL commands
 * complete, then copies the data. At 4096 bots * 96 bytes = 384 KB the
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
 *   - Memory cost: 2 * 384 KB = 768 KB. Trivial.
 *
 * Fence handle: GLsync. We use void* (see typedef above) to avoid GL
 * header coupling. NULL = "no fence inserted yet" / "previous fence
 * already consumed".
 *
 * If the compute symbols failed to load (probe_compute) we keep the
 * pre-Slice-2 behaviour by NOT initializing ring buffers and falling
 * back to the blocking readback at the call-site. */
#define SWARM_READBACK_RING_DEPTH 2
#define SWARM_READBACK_LATE_WAIT_NS 2000000ull
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
 * boid record at exactly 96 bytes (the std430 mirror). Stored as
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
 * 1:1 to the C struct.  96 B per boid. */
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
    int   jump_request;               /* offset 80: movement intent */
    int   surface_request;
    int   jump_kind;
    int   _pad_m0;
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
    /* Track 4 v0 (c3807, 2026-05-16) boids uniforms. */
    float boid_sep_radius, boid_align_radius, boid_coh_radius, _pad_b0;
    float boid_seek_weight, boid_sep_weight, boid_align_weight, boid_coh_weight;
    /* Track 2a (c3807, 2026-05-16) state-texture mirror gate.
     * 1 = imageStore writes the per-bot column into StateOut alongside
     * the SSBO write; 0 = imageStore skipped (SSBO-only, pre-2a parity).
     */
    int   state_tex_enable, _pad_s0, _pad_s1, _pad_s2;
} P;

/* Track 2a (c3807, 2026-05-16): ping-pong state texture (write side).
 * One bot occupies a 5-texel column at x = bot_index, y in [0, 5):
 *   y=0 pos.xyz / _pad
 *   y=1 vel.xyz / _pad
 *   y=2 surface_up.xyz / _pad
 *   y=3 action_class / fire_request / target_propnum / anim_key
 *       (packed via intBitsToFloat so the integer bit pattern survives
 *        the RGBA32F round trip; readers decode via floatBitsToInt.)
 *   y=4 range_to_target / _pad / _pad / _pad
 * Movement-intent fields remain SSBO/readback-only to keep the texture
 * protocol at 5 rows.
 *
 * writeonly so the driver can skip read-tracking; image binding 2 is
 * free (0 and 1 are the boid + params SSBOs already bound above).
 */
layout(rgba32f, binding = 2) uniform writeonly image2D StateOut;

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
        /* ---- Track 4 v0 (c3807, 2026-05-16) classical Reynolds boids ----
         * Three-rule accumulator over peers in the SSBO. Naive O(N^2):
         * every invocation reads every other invocation's pos+vel from
         * the SAME boid buffer this dispatch is writing to. std430
         * defines this as a write-after-read race for a given index j,
         * but the only writes per invocation are to b[i] (px/py/pz/vx/vy/vz
         * + AI block). Peers' fields read here (px/py/pz/vx/vy/vz) are
         * the START-of-frame snapshot the CPU just uploaded; the
         * compute kernel doesn't reissue between reads for different i,
         * so we get pre-step positions for everyone. Equivalent to a
         * double-buffer in effect at this dispatch frequency.
         *
         * Spatial-grid v1 (k-nearest broad phase) is the next slice; v0
         * just demonstrates that the steering math is wired and bots
         * separate / align / cohere visually.
         */
        vec3  sep_sum    = vec3(0.0);
        int   sep_n      = 0;
        vec3  align_sum  = vec3(0.0);
        int   align_n    = 0;
        vec3  coh_sum    = vec3(0.0);
        int   coh_n      = 0;

        vec3  vel_current = vec3(b[i].vx, b[i].vy, b[i].vz);

        for (int j = 0; j < int(P.count); j++) {
            if (j == int(i)) continue;
            vec3 p_j = vec3(b[j].px, b[j].py, b[j].pz);
            vec3 offset = pos - p_j;
            float dist = length(offset);

            if (dist > 0.001 && dist < P.boid_sep_radius) {
                /* Inverse-distance push: closer peers push harder. */
                sep_sum += offset / (dist * dist);
                sep_n++;
            }
            if (dist < P.boid_align_radius) {
                align_sum += vec3(b[j].vx, b[j].vy, b[j].vz);
                align_n++;
            }
            if (dist < P.boid_coh_radius) {
                coh_sum += p_j;
                coh_n++;
            }
        }

        vec3 seek_dir = to_player / d;

        vec3 sep_steer = vec3(0.0);
        if (sep_n > 0) {
            float sl = length(sep_sum);
            if (sl > 0.0001) sep_steer = sep_sum / sl;
        }

        vec3 align_steer = vec3(0.0);
        if (align_n > 0) {
            vec3 avg_v = align_sum / float(align_n);
            vec3 diff = avg_v - vel_current;
            float al = length(diff);
            if (al > 0.0001) align_steer = diff / al;
        }

        vec3 coh_steer = vec3(0.0);
        if (coh_n > 0) {
            /* Note: 'centroid' is a reserved GLSL qualifier (used in
             * fragment shaders for centroid sampling), so we use
             * 'group_center' as the local variable name instead. */
            vec3 group_center = coh_sum / float(coh_n);
            vec3 diff = group_center - pos;
            float cl = length(diff);
            if (cl > 0.0001) coh_steer = diff / cl;
        }

        /* Blend the four steering terms. Each component is a unit
         * vector (or zero); weighted sum is then normalized so the
         * final velocity magnitude is gated entirely by max_speed (the
         * smoke test asserts max_unit_per_frame stays below 6.0 and
         * max_speed_cap=5.0). */
        vec3 steer = P.boid_seek_weight  * seek_dir
                   + P.boid_sep_weight   * sep_steer
                   + P.boid_align_weight * align_steer
                   + P.boid_coh_weight   * coh_steer;

        /* Project steering onto the surface plane to preserve Slice-6
         * surface-walk behaviour (sloped floors / future walls). */
        steer = steer - dot(steer, surface_up) * surface_up;

        float steer_len = length(steer);
        if (steer_len > 0.0001) {
            steer /= steer_len;
        } else {
            /* Boids cancelled the seek entirely (e.g. trapped between
             * sep + coh pulls). Fall back to plain seek so bots still
             * make progress and the velocity doesn't latch at zero. */
            steer = seek_dir;
        }

        float step = P.max_speed * P.dt * 60.0;
        float speed = P.max_speed;
        /* Clamp speed so we never overshoot the player in one step --
         * this preserves the pre-v0 d-clamp guard that keeps the
         * BENCHMARK.SWARM.GPU.VEL max_unit_per_frame assertion
         * (< 6.0) satisfied at all bot tiers. */
        if (step > d) {
            speed = d / (P.dt * 60.0);
        }
        vec3 vel = steer * speed;
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
     *                                    jump_request = range-gated Skedar leap intent,
     *                                    surface_request = wall-ahead transition intent.
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
        int   jump = 0;
        int   surface = 1;
        if (range_full < P.fire_range) {
            act  = 2;             /* attack */
            fire = 1;
            anim = 2;             /* attack-melee */
        }
        if (range_full >= 200.0 && range_full <= 550.0) {
            jump = 1;
        }
        b[i].action_class    = act;
        b[i].fire_request    = fire;
        b[i].target_propnum  = P.player_propnum;
        b[i].anim_key        = anim;
        b[i].range_to_target = range_full;
        b[i].jump_request    = jump;
        b[i].surface_request = surface;
        b[i].jump_kind       = 1;
    }

    /* ---- Track 2a (c3807, 2026-05-16) state-texture mirror ----
     * Mirror the SSBO record into a 5-texel column in StateOut. The
     * texture writes are AFTER all SSBO writes so the texture and SSBO
     * agree on per-bot state at end-of-dispatch. Reading b[i] back here
     * is safe because std430 ordering within a single invocation is
     * sequential (no cross-invocation race on a given index).
     *
     * The AI block (row 3) packs four ints into a single RGBA32F texel
     * via intBitsToFloat. The bit pattern survives the texel round trip
     * untouched on any GL 4.2+ driver; future readers decode via the
     * matching floatBitsToInt. Range row (4) keeps the float in .r and
     * leaves the rest zero so a reader can treat it as a plain scalar.
     *
     * Slices 2b (kernel reads from texture), 2c (extraction primitive),
     * and 2d (ENet sync) build on this layout. 2a's job is to land the
     * write side with bit-exact SSBO parity (CPU consumer reads SSBO,
     * texture is silent / observe-only).
     */
    if (P.state_tex_enable != 0) {
        int x = int(i);
        imageStore(StateOut, ivec2(x, 0),
            vec4(b[i].px, b[i].py, b[i].pz, 0.0));
        imageStore(StateOut, ivec2(x, 1),
            vec4(b[i].vx, b[i].vy, b[i].vz, 0.0));
        imageStore(StateOut, ivec2(x, 2),
            vec4(b[i].sux, b[i].suy, b[i].suz, 0.0));
        imageStore(StateOut, ivec2(x, 3), vec4(
            intBitsToFloat(b[i].action_class),
            intBitsToFloat(b[i].fire_request),
            intBitsToFloat(b[i].target_propnum),
            intBitsToFloat(b[i].anim_key)));
        imageStore(StateOut, ivec2(x, 4),
            vec4(b[i].range_to_target, 0.0, 0.0, 0.0));
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
	/* Track 2a (c3807, 2026-05-16): glBindImageTexture is core GL 4.2+
	 * but our pinned glad gen does not expose it. Load dynamically. If
	 * the load fails (driver quirk or stub loader), we leave the texture
	 * mirror disarmed and the dispatch falls back to SSBO-only behaviour
	 * -- bit-exact pre-2a. NOT fatal. */
	s_glBindImageTexture = (swarm_glBindImageTexture_t)
		SDL_GL_GetProcAddress("glBindImageTexture");
	if (!s_glBindImageTexture) {
		sysLogPrintf(LOG_WARNING,
			"BENCHMARK.SWARM.GPU: glBindImageTexture symbol missing -- "
			"state-texture mirror disabled (SSBO-only path)");
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
	/* Track 2a (c3807, 2026-05-16): allocate the ping-pong RGBA32F state
	 * textures. Texture is (SWARM_GPU_MAX wide, 5 tall); one bot occupies
	 * a 5-texel column. Two textures so 2b can ping-pong (read from one,
	 * write to the other). Memory: 2 * 4096 * 5 * 16 B = 640 KB.
	 *
	 * Implementation note: we use glTexImage2D rather than glTexStorage2D
	 * because the project's pinned glad gen loads glTexStorage2D only
	 * inside load_GL_ES_VERSION_3_0() -- gated on GLAD_GL_ES_VERSION_3_0,
	 * which is false for our desktop GL 4.3 context. Calling the unloaded
	 * (NULL) glTexStorage2D would segfault. glTexImage2D is core GL 1.0
	 * and unconditionally loaded; for our purpose (allocate immutable
	 * sized storage, no data upload) the two are interchangeable. The
	 * format/internalformat pair (GL_RGBA32F / GL_RGBA / GL_FLOAT) matches
	 * the OpenGL 4.3 spec entry for sized internal formats and is what
	 * the imageStore writes target.
	 *
	 * Arming: only proceeds if glBindImageTexture loaded. On any failure
	 * we leave s_StateTexArmed = 0 and the shader falls back to SSBO-only
	 * via the state_tex_enable uniform gate. Bit-exact pre-2a parity is
	 * preserved either way. */
	if (s_glBindImageTexture && s_StateTexA == 0 && s_StateTexB == 0) {
		GLuint tex[2] = {0, 0};
		glGenTextures(2, tex);
		if (tex[0] && tex[1]) {
			for (s32 i = 0; i < 2; i++) {
				glBindTexture(GL_TEXTURE_2D, tex[i]);
				/* Allocate mip level 0 only; NULL data means
				 * driver leaves contents undefined (we overwrite
				 * via imageStore before any reader looks). */
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F,
					SWARM_GPU_MAX, 5, 0, GL_RGBA, GL_FLOAT, NULL);
				glTexParameteri(GL_TEXTURE_2D,
					GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D,
					GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			}
			glBindTexture(GL_TEXTURE_2D, 0);
			s_StateTexA = tex[0];
			s_StateTexB = tex[1];
			s_StateTexReadIdx = 0;
			s_StateTexArmed = 1;
			sysLogPrintf(LOG_NOTE,
				"BENCHMARK.SWARM.GPU: state-texture mirror armed "
				"(2 x RGBA32F %dx5 = %d KB)",
				SWARM_GPU_MAX,
				(2 * SWARM_GPU_MAX * 5 * 16) / 1024);
		} else {
			sysLogPrintf(LOG_WARNING,
				"BENCHMARK.SWARM.GPU: glGenTextures returned 0 "
				"(tex[0]=%u tex[1]=%u) -- state-texture mirror disabled",
				tex[0], tex[1]);
		}
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

/* ------------------------------------------------------------------
 * Track 2c (c3807, 2026-05-16): state-texture extraction primitive.
 * ------------------------------------------------------------------
 *
 * Read N rows from the READ-side state texture into the caller's
 * float buffer. Used by:
 *   - the --dump-swarm-state CLI fast-path (see port/src/main.c) for
 *     ad-hoc inspection / replay capture;
 *   - future Track 2d ENet sync, which encodes a row range into a
 *     network message per frame.
 *
 * READ-side selection: swarmGpuStepAndApply() swaps s_StateTexReadIdx
 * post-dispatch (line ~1394), so AFTER a dispatch the texture indexed
 * by s_StateTexReadIdx holds the MOST RECENT dispatch's per-bot
 * column. When read from a frame-deferred consumer (typical), that's
 * "the texture just written this frame". Reading mid-dispatch is not
 * supported (and the memory barrier guarantees the writes are visible
 * before the consumer runs).
 *
 * Implementation: glGetTextureSubImage (GL 4.5+) would give us a true
 * row-slice read, but the project's pinned glad gen does not export
 * it -- and the desktop GL minimum probe (GL >= 4.3, line ~899) does
 * not guarantee 4.5. We use the compatibility fallback: glGetTexImage
 * (core GL 1.0, unconditionally in glad) reads the WHOLE 4096x5 RGBA32F
 * texture into a static scratch buffer, then memcpy's the requested
 * row range into the caller's buffer. Total readback cost is 320 KB
 * per call (4096 * 5 * 16 B); negligible vs the dispatch itself, and
 * the caller controls call rate (the --dump CLI throttles to 1 dump
 * per second). No additional dynamic allocations.
 *
 * Failure modes (all return 0):
 *   - swarmGpuAvailable() == 0 (GL < 4.3 or compute symbols missing)
 *   - ensure_resources() failed (compile error / glGenBuffers fail)
 *   - s_StateTexArmed == 0 (glBindImageTexture missing, or the
 *     ensure_resources texture-alloc path failed)
 *   - row_start / row_count / out_capacity out of range
 *   - out_buf NULL
 *
 * Thread safety: caller must invoke from the same thread that owns
 * the GL context (i.e. the render / main thread). All other entry
 * points on this module obey the same rule.
 */
#define SWARM_GPU_STATE_TEX_ROWS    5
#define SWARM_GPU_STATE_TEX_TEXELS (SWARM_GPU_MAX * SWARM_GPU_STATE_TEX_ROWS)

/* Static scratch sized to the full texture. 4096 * 5 * 4 floats = 320 KB.
 * BSS, no allocation; static lifetime matches the rest of the file's
 * state. */
static float s_StateTexReadScratch[SWARM_GPU_STATE_TEX_TEXELS * 4];

int swarmGpuReadbackTextureRows(int row_start, int row_count,
                                float *out_buf, int out_capacity)
{
	if (!swarmGpuAvailable())  return 0;
	if (!ensure_resources())   return 0;
	if (!s_StateTexArmed)      return 0;
	if (out_buf == NULL)       return 0;
	if (row_start < 0 || row_count <= 0)                              return 0;
	if (row_start + row_count > SWARM_GPU_STATE_TEX_ROWS)             return 0;
	const int needed = SWARM_GPU_MAX * 4 * row_count;
	if (out_capacity < needed)                                        return 0;

	/* Read-side texture is the one s_StateTexReadIdx points at after
	 * the post-dispatch swap (= the texture the most recent dispatch
	 * wrote into). See the swap logic at the bottom of
	 * swarmGpuStepAndApply for the ping-pong invariant. */
	const GLuint tex = (s_StateTexReadIdx == 0) ? s_StateTexA : s_StateTexB;
	if (tex == 0) return 0;

	/* Compatibility fallback: glGetTexImage reads the entire mip 0
	 * into the static scratch (320 KB), then we memcpy the requested
	 * row range into the caller's buffer. glGetTextureSubImage would
	 * be a single-step row-slice read but is GL 4.5+ and not exposed
	 * by the project's pinned glad gen (which only loads up to 3.3 +
	 * compute selectively). The GL spec requires glGetTexImage to
	 * return packed pixels for GL_RGBA / GL_FLOAT; row width = 4096
	 * texels * 4 floats = 16384 floats per row. */
	glBindTexture(GL_TEXTURE_2D, tex);
	/* Ensure tight packing (4-byte aligned floats are tight anyway, but
	 * be explicit: some drivers default GL_PACK_ALIGNMENT to 4 which
	 * is correct for float, but a non-default GL_PACK_ROW_LENGTH from
	 * an earlier draw could break the read. Reset both to defaults. */
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
#ifdef GL_PACK_ROW_LENGTH
	glPixelStorei(GL_PACK_ROW_LENGTH, 0);
#endif
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT,
		s_StateTexReadScratch);
	glBindTexture(GL_TEXTURE_2D, 0);

	const size_t row_floats = (size_t)SWARM_GPU_MAX * 4;
	const size_t src_offset = (size_t)row_start * row_floats;
	const size_t copy_floats = (size_t)row_count * row_floats;
	memcpy(out_buf, s_StateTexReadScratch + src_offset,
		copy_floats * sizeof(float));

	return SWARM_GPU_MAX * row_count;
}

/* ------------------------------------------------------------------
 * Track 2d (c3807, 2026-05-16): remote-state apply.
 * ------------------------------------------------------------------
 *
 * Receiver-side counterpart to swarmGpuReadbackTextureRows. The caller
 * (port/src/net/netmsg.c::netmsgSvcGpuSwarmStateRead) decoded a chunk of
 * the listen-host's broadcast into a row-major RGBA32F float buffer:
 *   row 0:  pos.xyz, 0      (count texels)
 *   row 1:  vel.xyz, 0
 *   row 2:  surface_up.xyz, 0
 *   row 3:  action_class, fire_request, target_propnum, anim_key
 *           (packed via intBitsToFloat-style memcpy in the decoder)
 *   row 4:  range_to_target, 0, 0, 0  (zeroed by the decoder; remote
 *                                       clients don't need this)
 *
 * This function uploads `count` columns starting at `chunk_start` into
 * the READ-side state texture so the rendering / animation paths can
 * sample it as if the local compute had produced it. Pong: we DON'T
 * advance s_StateTexReadIdx -- the local compute, if it ran, would have
 * already swapped; remote clients skip the compute dispatch entirely
 * (see swarmTestTick in port/src/swarm_test.c) so the ping-pong index
 * stays whatever it was at the last invalidate. Uploads always target
 * whichever texture s_StateTexReadIdx currently points at.
 *
 * Failure modes (silent return):
 *   - swarmGpuAvailable() == 0
 *   - ensure_resources() failed
 *   - s_StateTexArmed == 0 (compute symbols / texture-alloc fail)
 *   - count <= 0 or chunk_start < 0 or chunk_start + count >
 *     SWARM_GPU_MAX
 *   - raw_rgba32f_buf == NULL
 *
 * Thread safety: same as the rest of the module -- main / GL thread only.
 *
 * pd-server: NOT linked into pd-server (swarm_gpu.cpp is not in
 * SRC_SERVER). The netmsg receiver's call site is `#if !defined(PD_SERVER)`-
 * guarded so the pd-server link stays clean.
 */
void swarmGpuApplyRemoteState(const float *raw_rgba32f_buf, int count,
                              int chunk_start)
{
	if (raw_rgba32f_buf == NULL) return;
	if (count <= 0)              return;
	if (chunk_start < 0)         return;
	if (chunk_start + count > SWARM_GPU_MAX) return;
	if (!swarmGpuAvailable()) return;
	if (!ensure_resources())  return;
	if (!s_StateTexArmed)     return;

	const GLuint tex = (s_StateTexReadIdx == 0) ? s_StateTexA : s_StateTexB;
	if (tex == 0) return;

	/* Texture is SWARM_GPU_MAX wide, 5 tall. RGBA32F. We upload each row
	 * as a separate glTexSubImage2D call -- 5 calls per apply, but the
	 * payload of each is small (count texels * 16 B) so the overhead is
	 * dominated by the per-call driver chatter, which is fine at the
	 * 10 Hz broadcast rate (5 * 10 = 50 calls/sec, trivial vs the per-
	 * frame draw + compute load).
	 *
	 * Source buffer layout matches the encode/decode contract:
	 *   row 0 floats: count * 4
	 *   row 1 floats: count * 4 (starts at count*4 into the buffer)
	 *   ...
	 *
	 * Target subrect: x = chunk_start, y = row, width = count, height = 1.
	 */
	glBindTexture(GL_TEXTURE_2D, tex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
#ifdef GL_UNPACK_ROW_LENGTH
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif

	for (int row = 0; row < 5; row++) {
		const float *src = raw_rgba32f_buf + (size_t)row * count * 4;
		glTexSubImage2D(GL_TEXTURE_2D, /*level=*/0,
			/*xoffset=*/chunk_start, /*yoffset=*/row,
			/*width=*/count, /*height=*/1,
			GL_RGBA, GL_FLOAT, src);
	}

	glBindTexture(GL_TEXTURE_2D, 0);
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
	/* Track 4 v0 (c3807, 2026-05-16) Reynolds boids defaults.
	 *
	 * Radii (units = cm; 1 game unit ~ 1 cm):
	 *   sep    = 60.0  -- ~1.5x Skedar collision radius (30 * scale ~40)
	 *                     so neighbours actively push when overlapping
	 *                     spawn slop or piling into the player.
	 *   align  = 200.0 -- a few bot-widths; matches velocity within a
	 *                     local clump rather than the whole swarm.
	 *   coh    = 300.0 -- attract toward a slightly wider neighbourhood
	 *                     so isolated bots find the pack again.
	 *
	 * Weights (steering blend; final vector renormalised to unit then
	 * scaled to max_speed so magnitudes stay smoke-bounded):
	 *   seek  = 1.0   -- primary goal: chase the player.
	 *   sep   = 1.5   -- separation strongest so bots don't overlap.
	 *   align = 0.4   -- mild flocking.
	 *   coh   = 0.3   -- mild cohesion.
	 *
	 * If playtest shows the swarm trails the player too lazily, bump
	 * seek to 1.5 and trim sep to 1.2. The CPU side can override these
	 * later through a debug uniform without touching the shader. */
	/* Mike playtest 2026-05-17: pre-tune bots were floating slowly and
	 * merging at the player position. Seek vs sep at 1.0/1.5 left the
	 * bots locally cancelled near the player, dance-stuck in a cluster.
	 * Bumping seek=1.6 (was 1.0) and trimming sep=1.0 (was 1.5) gives
	 * the seek vector primary authority on approach; separation still
	 * acts but no longer dominates near-radius. Bumped sep_radius to
	 * 90 (was 60) so the personal-space bubble is wider -- bots fan out
	 * around the player rather than clumping into a single point. */
	s_Params.boid_sep_radius   = 90.0f;
	s_Params.boid_align_radius = 200.0f;
	s_Params.boid_coh_radius   = 300.0f;
	s_Params.boid_seek_weight  = 1.6f;
	s_Params.boid_sep_weight   = 1.0f;
	s_Params.boid_align_weight = 0.4f;
	s_Params.boid_coh_weight   = 0.3f;
	/* Track 2a (c3807, 2026-05-16): gate the imageStore mirror to the
	 * state texture. Armed only when ensure_resources() successfully
	 * allocated both textures AND glBindImageTexture loaded; otherwise
	 * the shader sees state_tex_enable = 0 and runs the SSBO-only path
	 * (bit-exact pre-2a). _pad fields stay zero for std430 hygiene. */
	s_Params.state_tex_enable = s_StateTexArmed ? 1 : 0;
	s_Params._pad_s0 = 0;
	s_Params._pad_s1 = 0;
	s_Params._pad_s2 = 0;
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, s_ParamsSsbo);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
		sizeof(swarm_params), &s_Params);

	/* Dispatch */
	glUseProgram(s_Program);
	s_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s_BoidSsbo);
	s_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, s_ParamsSsbo);
	/* Track 2a (c3807, 2026-05-16): bind the WRITE-side state texture as
	 * image binding 2. The shader's layout(rgba32f, binding=2) gates on
	 * P.state_tex_enable so a 0-bound image is harmless when armed=0
	 * (glBindImageTexture with id=0 unbinds), but it's cleaner to skip
	 * the call entirely in that case. */
	GLuint state_tex_write = 0;
	if (s_StateTexArmed && s_glBindImageTexture) {
		/* Write to the texture OPPOSITE of the one the next slice (2b)
		 * will read from. Pre-2b nothing reads the texture; we still
		 * advance the ping-pong index so 2b's first run is identical
		 * to a steady-state run. Layer 0 + level 0, full RGBA32F access.
		 * GL_FALSE for layered (this is not a 3D / array / cubemap),
		 * 0 layer (ignored when layered=GL_FALSE). */
		state_tex_write = (s_StateTexReadIdx == 0)
			? s_StateTexB : s_StateTexA;
		s_glBindImageTexture(2, state_tex_write, 0, GL_FALSE, 0,
			GL_WRITE_ONLY, GL_RGBA32F);
	}

	GLuint groups = (GLuint)((count + SWARM_GPU_LOCAL_X - 1) / SWARM_GPU_LOCAL_X);
	s_glDispatchCompute(groups, 1, 1);
	/* Track 2a (c3807, 2026-05-16): expand the post-dispatch barrier to
	 * cover image stores as well as SSBO writes. The barrier ensures
	 * subsequent CPU readback (glGetBufferSubData) and any image-sampling
	 * consumer (none today; slated for 2b) see the dispatch's writes.
	 * Bit cost is negligible vs the SSBO bit alone. */
	s_glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT
		| GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	/* Track 2a (c3807, 2026-05-16): ping-pong swap. Next dispatch writes
	 * to the texture this dispatch just consumed as read (= started as
	 * the "current read side"). For 2a there is no read path yet, but
	 * swapping unconditionally keeps the state machine identical between
	 * 2a (write-only) and 2b (write + read), so 2b lands cleanly. */
	if (s_StateTexArmed) {
		s_StateTexReadIdx ^= 1;
	}

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
			GLenum wait = s_glClientWaitSync(fence, 0, 0);
			/* At small live-debug counts, a fence that is barely late is
			 * more visible as jitter than as perf savings. Give it a short
			 * bounded wait so 128/256-bot GPU_FULL runs keep applying
			 * movement instead of reporting active=0 for entire seconds. */
			if (wait == GL_TIMEOUT_EXPIRED && count <= 256) {
				wait = s_glClientWaitSync(fence,
					GL_SYNC_FLUSH_COMMANDS_BIT,
					(GLuint64)SWARM_READBACK_LATE_WAIT_NS);
			}
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
	s32 n_jump_req = 0, n_surface_req = 0;
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
		const float vy = s_BoidScratch[i].vy;
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
		/* c029 Slice 3 (2026-05-16, B-332 fix):
		 *
		 * Initial implementation called chrSetPosWithCachedGround when
		 * ce->valid was true, eliminating the chrSetPos-internal raycast
		 * pair. Smoke test surfaced a regression at the 8->16 cycle
		 * transition: 3 of 8 newly-spawned chrs ended up in a state
		 * where chr->prop was set but chr->prop->chr was NULL, triggering
		 * AV in despawn_all->chrRemove on the next cycle.
		 *
		 * Root cause (B-332, 2026-05-16): chrSetPosWithCachedGround gated
		 * its propDeregisterRooms + roomsCopy + chr0f0220ac trio behind
		 * an `if (newrooms)` check that compared the caller's rooms array
		 * against chr->prop->rooms. Since this loop passes chr->prop->rooms
		 * back to itself, newrooms was ALWAYS false, so the room re-register
		 * was ALWAYS skipped -- in contrast to chrMoveToPos which does the
		 * trio UNCONDITIONALLY. Bots drifting across room boundaries had
		 * stale prop->rooms entries, the propsTickPlayer room-walks lost
		 * track of them, and downstream invariants broke (newly-spawned
		 * bots ended up with chr->prop->chr NULLed by an engine free path
		 * that fired because the bot was effectively unreachable through
		 * room registration).
		 *
		 * Fix: chrSetPosWithCachedGround now runs the room sync trio
		 * UNCONDITIONALLY (see chraction.c::chrSetPosWithCachedGround for
		 * the patched body). With that fix in place the cached helper is
		 * safe to call from this apply loop -- net effect is BOTH chrSetPos-
		 * internal raycasts skipped on cache hit (vs the prior conservative
		 * fallback which kept chrSetPos and only saved the surface_up
		 * sampler).
		 *
		 * Cache-miss path: the cdFindGroundInfoAtCyl already ran in the
		 * pre-pass and wrote g/fc/ft/fr into ce. The miss-path entry has
		 * all the cached fields populated; we can feed them straight into
		 * chrSetPosWithCachedGround. This means even on a miss we save the
		 * second cdFindGroundInfoAtCyl that the legacy chrSetPos(findground)
		 * path would have made internally.
		 *
		 * Net raycast budget per active bot per frame:
		 *   - Pre-c029-Slice-3:        3 (sampler + 2 inside chrSetPos)
		 *   - Slice-3 post-B-332-fix:  miss=2 (sampler + 1 in pre-pass)
		 *                              hit=0  (both pre-pass raycasts + the
		 *                                      former 2 chrSetPos-internal
		 *                                      raycasts skipped)
		 *
		 * Bootstrap: if ce->valid is 0 (first frame after invalidate, or
		 * a slot that never went through the pre-pass), the pre-pass
		 * MISS path above always populates the cache before the apply
		 * loop runs. So when we reach this point with apply_count > 0,
		 * ce->valid is guaranteed true for every i in [0, apply_count).
		 * We assert that via the conditional below for safety, falling
		 * back to legacy chrSetPos if a future refactor breaks the
		 * invariant. */
		if (chr->actiontype != ACT_SKJUMP) {
			if (ce->valid) {
				chrSetPosWithCachedGround(chr, &newpos, rooms, face_deg,
					ce->ground, ce->floorcol, ce->floortype, ce->floorroom);
			} else {
				/* Defensive fallback. Should not happen in steady state:
				 * the pre-pass always populates ce on a MISS, and HIT
				 * implies ce was already valid. Counted by s_PerfCacheMiss
				 * external to this site so it shows up in playtest logs. */
				chrSetPos(chr, &newpos, rooms, face_deg, true);
			}
		}

		/* Velocity stats accumulator -- emitted at the 60-frame interval
		 * below. Was its own walk pre-c029-Slice-3. */
		const f32 v2 = vx * vx + vy * vy + vz * vz;
		if (v2 > max_v2) max_v2 = v2;
		sum_v += sqrt((double)v2);
		v_samples++;

		/* AI side-effect + stats. Was its own walk pre-c029-Slice-3.
		 *
		 * Mike playtest 2026-05-17: pressing O to flip GPU_POS_ONLY ->
		 * GPU_FULL caused FATAL: Unknown GBI opcode 0x80 at high bot
		 * counts. Root cause: on the very first GPU_FULL frame ALL
		 * active bots transition from their spawn-time anim
		 * (ANIM_SKEDAR_RUNNING) to whatever the GPU picked (typically
		 * ANIM_034C = punch), simultaneously firing 4096
		 * modelSetAnimation calls into the chrvtxstore in a single
		 * frame. Pool pressure corrupted a downstream display-list
		 * emission.
		 *
		 * Mitigation: round-robin throttle. Each bot's AI decision is
		 * applied at most once every 4 frames; the wave of simultaneous
		 * anim switches spreads across ~4 frames at 60 Hz (~67 ms,
		 * imperceptible). Per-bot stats still update every frame so the
		 * BENCHMARK.SWARM.GPU.AI summary remains accurate; only the
		 * side-effect (modelSetAnimation + chrDamageByImpact) is gated. */
		if (do_ai) {
			const s32 act  = s_BoidScratch[i].action_class;
			const s32 fire = s_BoidScratch[i].fire_request;
			const s32 anim = s_BoidScratch[i].anim_key;
			const s32 tgt  = s_BoidScratch[i].target_propnum;
			const f32 r    = s_BoidScratch[i].range_to_target;
			const s32 jump = s_BoidScratch[i].jump_request;
			const s32 surface = s_BoidScratch[i].surface_request;

			/* Local frame phase: increments once per readback pass.
			 * g_Vars isn't visible in this TU so we use a static
			 * function-local counter; it's only a phase signal for the
			 * round-robin stride so any monotonic source works. */
			static u32 s_AiApplyPhase = 0;
			if (i == 0) s_AiApplyPhase++;
			if (((u32)i & 3u) == (s_AiApplyPhase & 3u)) {
				const f32 vel_hint[3] = { vx, vy, vz };
				(void)swarmTestApplyMovementIntent(chr, i, tgt, jump,
					surface, r, vel_hint);
				(void)swarmTestApplyAiDecision(chr, i, tgt, act, fire, anim, r);
			}

			if      (act == 2) n_attack++;
			else if (act == 1) n_seek++;
			else               n_idle++;
			if (fire) n_fire++;
			if (jump) n_jump_req++;
			if (surface) n_surface_req++;
			sum_range += r;
			if (r < min_range) min_range = r;
			if (r > max_range) max_range = r;
			ai_sample_count++;
		}
	}

	/* c029 Slice 3 perf-stats line. Once per 60 frames; reports the cache
	 * hit/miss ratio + derived raycast load.
	 *
	 * B-332 fix (2026-05-16): apply loop now routes through
	 * chrSetPosWithCachedGround (room-sync-safe per the unconditional
	 * propDeregisterRooms + chr0f0220ac trio inside the helper). Every
	 * raycast in the pipeline is now controlled by the pre-pass:
	 *
	 *   - HIT  in pre-pass:  0 sampler + 0 ground sample = 0 raycasts
	 *   - MISS in pre-pass:  1 sampler + 1 ground sample = 2 raycasts
	 *
	 * chrsetpos_raycasts is now always 0 (the apply path consumes the
	 * cached ground info and skips chrSetPos's internal raycast pair).
	 * Total raycasts per frame = 2 * s_PerfSampled (miss path only). */
	{
		static s32 s_PerfLogTick = 0;
		s_PerfLogTick++;
		if (s_PerfLogTick >= 60) {
			s_PerfLogTick = 0;
			const s32 total = s_PerfSampled + s_PerfReused;
			const s32 pre_pass_raycasts = s_PerfSampled * 2;
			const s32 chrsetpos_raycasts = 0;
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
				"fire_req=%d jump_req=%d surface_req=%d "
				"fire_applied_lastframe=%d range_min=%.0f avg=%.0f "
				"max=%.0f fire_range=%.0f target_propnum=%d",
				count, n_idle, n_seek, n_attack, n_fire,
				n_jump_req, n_surface_req, fires_applied,
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

	/* Track 2d (c3807, 2026-05-16): listen-host GPU swarm state broadcast.
	 *
	 * At this point the compute dispatch has completed and the post-
	 * dispatch swap has moved the just-written texture into the READ
	 * slot (line ~1493). The broadcast helper reads it via
	 * swarmGpuReadbackTextureRows and fans out to all connected peers.
	 *
	 * Gate: listen-host (NETMODE_SERVER && !g_NetDedicated). The helper
	 * also checks NETMODE_SERVER internally; we still do an extra check
	 * here to skip the call cheaply when offline / client-mode. Internal
	 * throttle drops 5 of every 6 frames so the wire load is 10 Hz.
	 *
	 * pd-server: this function (swarmGpuStepAndApply) is not linked into
	 * pd-server, so the send is naturally absent from headless builds.
	 * The g_NetDedicated check is for the listen-host case where a player
	 * has --dedicated but is somehow still running compute (defensive). */
	if (g_NetMode == NETMODE_SERVER && !g_NetDedicated) {
		netSendGpuSwarmState();
	}
}

} /* extern "C" */
