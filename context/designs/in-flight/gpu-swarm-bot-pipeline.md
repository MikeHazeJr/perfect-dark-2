# GPU Swarm: bot behaviour on GPU compute (follow-up to S593)

> Status: SCOPED, not started. Filed 2026-05-01 as a follow-up after the
> S593 pass that brought CPU-mode swarm bots up to real-bot behaviour.

## Premise (from Mike, 2026-05-01)

> "The idea is to translate bot behaviour to the GPU so they can run in
> parallel better. The benchmark we have here is simply to see at what
> bot count or CPU load should we switch modes, or whether we should
> actually set bots to ALWAYS operate on the GPU."

The "Swarm - GPU Boids" test scenario today only drives **positions** on
the GPU. Bot behaviour (target selection, attack decisions, dodge,
animation choice, weapon firing) all stays on the CPU and is BYPASSED in
GPU mode. The result: GPU-mode chrs are visually "moving props", not
real bots. The CPU vs GPU benchmark crossover is therefore meaningless
because the workloads are not equivalent.

This doc scopes the work to make GPU mode run real bot behaviour on the
GPU side, so the benchmark crossover is meaningful and the long-term
"always run bots on GPU" question is answerable.

## Where things stand after S593 (2026-05-01)

- `port/src/swarm_test.c` -- CPU mode now uses `GAILIST_AIBOT_INIT` and
  a private 256-slot `aibot` pool, so each Skedar runs the real bot AI
  through `chraTick`/`chraiExecute`. CPU-mode bots seek/attack/collide
  like a normal MP simulant.
- `port/fast3d/swarm_gpu.cpp` -- GPU mode runs a tiny GLSL 4.3 compute
  shader (`kSwarmCs`) that does pure seek-toward-player position
  update. The shader has no notion of: target, weapon, animation,
  state machine, line-of-sight, room/floor info, other chrs.
- GPU-mode chrs are spawned with `GAILIST_IDLE` and **no** `aibot`, so
  the CPU side is doing the bare minimum (animation + render only).

## Work breakdown

### Tier 1: GPU-side state machine + target selection (largest piece)

The CPU-side `aibot` struct holds ~700 bytes per bot of state: target
indices, distance/visibility tables, weapon state, dodge state, hill
state, command queue, etc. To run on GPU we need:

1. **A GPU-friendly bot state struct** (SoA or AoS, std430 layout).
   Trim aggressively -- many fields are only used by SP cutscene paths
   or specific weapons that don't fire in benchmark.
2. **Per-tick state advancement in a compute kernel.** The current
   shader is ~20 lines; a real bot tick is hundreds of branches. The
   compute kernel either:
   - Encodes the state machine as a switch on `state` (simple but
     branchy across warps; OK on modern hw).
   - Splits into multiple kernels (one per state class) and dispatches
     a list per state. More efficient on warp coherence, more code.
3. **Target acquisition.** Each bot picks a target. In benchmark the
   target is always the player so this is one read of player state.
   Generalising to MP requires a chr table on the GPU.
4. **Visibility / line-of-sight.** Today `chr->aibot->chrsinsight[]` is
   filled by CPU-side raycasts against BG geometry. Doing this on GPU
   requires either:
   - Uploading BG primitives once and raycasting in compute (large data,
     non-trivial code).
   - Maintaining a coarse occupancy grid the bots query.
   - Skipping LOS entirely in benchmark mode (always assume visible) --
     valid for the apples-to-apples cost benchmark even though it
     diverges from gameplay realism.

### Tier 2: GPU -> CPU writeback shape

The CPU side still owns: rendering, weapon firing (projectile spawn,
damage application), animation state. The GPU compute output that the
CPU reads back per frame:

- `prop->pos` (already in v1).
- Heading angle (`face_deg`, already partially used in v1).
- Action class (`MA_*` enum or a benchmark-specific compact code).
- Animation key (`ANIM_*` enum or one-hot).
- Fire request (per-frame bool + target prop index).

Each bot's read-back is currently 32 bytes (boid_record). Expand to a
~64-byte block that carries the tier-2 data. At 256 bots that's 16 KB
read-back per frame, no problem.

### Tier 3: CPU does the side-effects

Once the GPU has decided "bot N wants to fire at player", the CPU
spawns the projectile, applies damage, plays the audio. The CPU still
owns the gameplay-visible event because:

- Projectile spawn touches the prop pool (chr/prop allocation).
- Damage application touches health, scoreboard, kill counters.
- Audio engine is CPU-only.

This is the GPGPU pattern: GPU decides, CPU executes side effects.

## Open questions for Mike before kicking off

1. **Behaviour fidelity.** Is "perfect parity with CPU bot AI" the
   target, or is "approximate parity that exposes the same per-bot
   cost shape" enough? The latter is dramatically simpler.
2. **MP vs benchmark scope.** Should the GPU bot pipeline be available
   in MP matches (the "always-on-GPU" path Mike hinted at), or just
   in the benchmark scenario? MP introduces wire-protocol concerns
   (positions/state need to sync between peers).
3. **Compute API floor.** GL 4.3 is the minimum we already require.
   Vulkan/DX12 offer better compute primitives but big lift. Stay on
   GL?
4. **AI difficulty / config.** Each `aibot` reads `mpbotconfig` for
   difficulty (BOTDIFF_MEAT/EASY/.../DARK). On the GPU this becomes
   a per-bot uniform. Per-bot or shared?

## Suggested first slice

If Mike wants this to start small and grow:

1. Add a "GPU mode with CPU bot AI" benchmark variant (so we have
   three modes: SWARM_METHOD_CPU, SWARM_METHOD_GPU_POS_ONLY, and a
   new SWARM_METHOD_GPU_FULL). The GPU_FULL variant is the WIP target.
2. Build the GPU state struct + the trivial state machine that just
   does seek + a simple "if in range, request fire" decision.
3. Validate the writeback shape with the simple decision before
   layering in target selection / visibility / weapon choice.

## Known precedent in the codebase

- `port/fast3d/swarm_gpu.cpp` -- existing GLSL 4.3 compute pipeline,
  SSBO management, GL symbol loading via `SDL_GL_GetProcAddress`.
- `src/game/bot.c` -- the canonical bot AI tick, structurally complex
  but readable. Use as the "what behaviour exactly do we replicate".
- `src/game/chraicommands.c` -- the AI script command set. Many
  commands compile down to GPU-friendly state transitions.

## Why this is filed, not started

- S593's CPU-mode fix gets the benchmark to a meaningful baseline
  first. Without that, comparing GPU to anything is comparing to
  garbage.
- The work is cross-pillar (port/fast3d + game/bot AI + benchmark
  pillar) and likely 3-5 sessions of focused effort.
- Mike's directive (2026-05-01) was explicit: "Don't try to make the
  GPU path do real bot behaviour in this session if the gap is large.
  Surface it as a follow-up."

The gap is large. This file is the surface.
