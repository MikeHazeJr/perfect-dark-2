# Sprint 2026-05-16: GPU bot AI fire + anim (B-308 finish, c3807-finish)

Track: GPU swarm bot AI pipeline (Track A4)
Card anchor: c3807 ("GPU swarm benchmark debug tab Phase 2")
Bug: B-308 (LOW, was PARTIAL, now FIXED-PENDING-PLAYTEST)
Status: SHIPPED -- slice 2 (fire + anim side effects). Async PBO readback deferred.

## Goal

Finish the GPU bot AI parity work started in commit `0edf90cb` (slice 1). Slice 1 had the GPU compute kernel writing per-bot AI decisions (action_class, fire_request, anim_key, target_propnum) into an expanded 80-byte boid SSBO, and the CPU readback consumer just logged the decisions for verification. This slice wires those decisions through to actual CPU-side game state: damage events (chrDamageByImpact) and model animation (modelSetAnimation).

## Resolution paths chosen

Per the dispatch directive's default ("Option A for both -- minimizes engine surface area and keeps CPU/GPU paths separate"):

### Fire: aibot-free direct damage call

Added a thin C function `swarmTestApplyAiDecision` in `port/src/swarm_test.c`. The function builds a minimal `gset = {WEAPON_UNARMED, 0, 0, FUNC_PRIMARY}` -- structurally identical to the gset used by `chrPunchInflictDamage` (chraction.c:7867) for any chr that wants to deal melee damage without an aibot weapon. The vector to the target is computed inline (XZ unit vector from chr->prop to target_prop). `chrDamageByImpact(target_prop->chr, 1.0f, &vector, &gset, chr->prop, HITPART_GENERAL)` is called.

`chrDamageByImpact` is the canonical entry the AI script command opcode 0x018d (chraicommands.c:850) uses to make a chr "punch" a target -- it does not require an aibot on the attacker, only a valid attacker prop and a victim chr. The player's `invincible = 1` short-circuit in `chrDamage` (chraction.c:4425) absorbs the damage cleanly, but the call still walks the canonical damage / MP-stats path so any future score / kill-counter / sound integration sees real-shaped events.

Throttling: per-bot cooldown `SWARM_GPU_FIRE_COOLDOWN_60 = 30` ticks (0.5 s). At 4096 bots fully in range this caps total damage-events at ~8 K/s across the whole pool. The per-bot last-fire timestamp lives in `s_SwarmLastFire60[TESTSCEN_SWARM_MAX_COUNT]` (16 KB BSS, trivial).

### Anim: direct modelSetAnimation, bypass action machinery

`chr->myaction = MA_NONE` on GPU bots is unchanged. The dispatcher calls `modelSetAnimation(chr->model, ANIM_*, 0, 0.0f, speed, 16.0f)` directly. The anim-table map:

- anim_key=0 (stand)  -> ANIM_STANDING_TYPE_ONE_HAND
- anim_key=1 (walk)   -> ANIM_SKEDAR_RUNNING (the canonical Skedar run anim used by `chrRunPos` at chraction.c:2207)
- anim_key=2 (attack) -> ANIM_034C (first entry in `g_SkedarPunchAnims`, chraction.c:7920 -- Skedar frontal punch, visually reads as "swing")

Transition guard: skip the `modelSetAnimation` call when `chr->model->anim->animnum == desired_anim`. This drops 4096 redundant "still walking" calls per frame to zero. The action machinery (chr->myaction tick) doesn't run for GPU bots, so there's no conflict between the directly-set animation and any background state machine.

### PBO async readback: deferred

The blocking `glGetBufferSubData` (320 KB / frame at 4096 bots) is documented in code as a follow-up. Not regressed in the smoke runs at 4096 bots; worth doing before GPU_FULL becomes the default-on path. Adding a PBO + fence ring is straightforward (allocate N>=2 GL_PIXEL_PACK_BUFFER objects, glFenceSync after dispatch, check on the next frame) but introduces 1-2 frame latency to AI decisions; not required for correctness.

## Plumbing

The CPU-side dispatcher is in C (game-land); the GPU-side readback consumer is in C++ (port/fast3d). The boundary is a single extern "C" pair in swarm_gpu.cpp:

```cpp
extern s32 swarmTestApplyAiDecision(struct chrdata *chr,
                                    s32 slot_index,
                                    s32 target_propnum,
                                    s32 action_class,
                                    s32 fire_request,
                                    s32 anim_key,
                                    f32 range_to_target);
extern s32 swarmTestGetAndResetGpuAiFireCount(void);
```

The readback consumer's existing loop over `s_BoidScratch[i]` now calls `swarmTestApplyAiDecision` once per active bot (after the chr+prop+model validity check that the AI stats block already did). The dispatcher gates internally on per-bot cooldown and anim transition state so the common idle/seek case is just a NULL-check + animnum compare.

A new per-frame counter `s_SwarmGpuAiFiresApplied` accumulates "fires that actually applied" (vs "GPU requested" which is just the bit count in `fire_request`). Drained via `swarmTestGetAndResetGpuAiFireCount` at each summary log emission so the log line reads:

```
BENCHMARK.SWARM.GPU.AI: count=N idle=X seek=Y attack=Z fire_req=N fire_applied_lastframe=M range_min/avg/max=N ...
```

Mike can correlate "GPU requested N fires" (fire_req) vs "CPU applied M fires" (fire_applied_lastframe) across runs. M < N is expected because of the cooldown.

## Fire entry signature

```c
s32 swarmTestApplyAiDecision(struct chrdata *chr,
                             s32 slot_index,           /* swarm slot 0..MAX-1 */
                             s32 target_propnum,       /* index into g_Vars.props[] */
                             s32 action_class,         /* 0=idle 1=seek 2=attack */
                             s32 fire_request,         /* 0|1 */
                             s32 anim_key,             /* 0=stand 1=walk 2=attack */
                             f32 range_to_target);     /* 3D distance, advisory */
```

Returns 1 if a fire side-effect was applied this call, 0 otherwise. Action_class and range_to_target are currently unused (could be consumed by future scope: e.g. action_class=idle could skip anim transitions entirely; range_to_target could feed a non-uniform fire cooldown). Kept on the ABI so callers don't need a signature bump when those are wired in.

## Anim mapping

```c
case 2:  desired_anim = ANIM_034C;                   break;  /* attack */
case 1:  desired_anim = ANIM_SKEDAR_RUNNING;         break;  /* walk */
default: desired_anim = ANIM_STANDING_TYPE_ONE_HAND; break;  /* stand */
```

Speed = 0.5f for walk (matches the chraction.c:2201 RACE_SKEDAR mult used by chrRunPos), 1.0f for stand and attack. Merge = 16.0f frames matches the existing chraction.c uses of modelSetAnimation.

## Build verify

All four targets clean, no new warnings:

```
build-headless.ps1 -Target all     -> CLIENT PASS 4s, UPDATER PASS 1s
build-headless.ps1 -Target server  -> SERVER PASS 2s
build-headless.ps1 -Target tests   -> TESTS PASS 0s
```

## Smoke verify

```
swarm_cpu_smoke    PASS 19/19   95.5s   (results-20260516T040001Z.json)
swarm_gpu_smoke    PASS 19/19  150.5s   (results-20260516T040241Z.json)
```

The smoke tests run TESTSCEN_SWARM_GPU in `SWARM_METHOD_GPU_POS_ONLY` (default at launch). KEY_O is not pressed by either smoke, so the new dispatcher is linked but never executes during the smoke run. The smoke pass therefore confirms (a) no link / build regression from the new function, (b) POS_ONLY behaviour is byte-for-byte unchanged.

Smoke env caveat: orphan `PerfectDark.exe` processes from earlier sessions corrupted the first smoke runs by writing to the same `pd-client.log`. Killing the orphans + clearing `pd-client*.log` resolved it. Same shape as the iter-3 sprint note (2026-05-15) about stale rotated logs -- worth filing as a smoke-harness hardening item separately.

## Visual playtest

Not performed in this dispatch. Mike (game director) will verify by:

1. Boot `PerfectDark.exe`, Settings -> Debug -> Test Scenarios.
2. Pick "Swarm - GPU Boids", launch.
3. HUD reads "GPU Boids (pos-only)". Press O.
4. HUD swaps to "GPU Boids (AI on)". Log starts emitting at ~1 Hz:
   ```
   BENCHMARK.SWARM.GPU.AI: count=4 idle=0 seek=X attack=Y fire_req=N fire_applied_lastframe=M ...
   ```
5. Walk close to a bot (range < 500). The summary should show `attack=N` non-zero and `fire_applied_lastframe=M` non-zero, with M roughly N/30 (cooldown is 30 ticks per bot). Visually, the close bots should animate ANIM_034C (Skedar attack swing); distant bots should run ANIM_SKEDAR_RUNNING.
6. Cycle ladder via PgUp to 128 / 256 / 1024 / 4096. Visual + log volume should stay bounded; per-frame summary is throttled to 1 / 60 frames.
7. Visual: bots should look like they're playing the same combat poses (run + swing) as the CPU swarm, just driven by the GPU shader's decisions rather than the CPU bot AI.

If GPU_FULL bots demonstrably fire AND animate, B-308 moves to CLOSED on Mike's playtest sign-off.

## Performance note

At 4096 bots in range (worst-case GPU_FULL frame):

- 4096 NULL checks + 4096 dispatcher calls. The chr+prop+model validity check is the same one the AI stats block already does, so the marginal cost is the dispatcher body.
- 4096 anim-compare branches; ~0 modelSetAnimation calls in steady state (all bots already at desired anim from the previous frame).
- ~8 K chrDamageByImpact calls / second (4096 bots * 2 Hz cooldown). The `currentplayer->invincible` short-circuit returns from chrDamage line 4425 -- ~20 lines of validation walked per call, no heap allocs.

Expected per-frame overhead vs POS_ONLY: low single-digit ms. Will measure on Mike's machine via the existing BENCHMARK.SWARM.GPU SUMMARY `frame_avg_ms` field.

## Files touched

- `port/src/swarm_test.c`       -- added `swarmTestApplyAiDecision` + `swarmTestGetAndResetGpuAiFireCount` + `s_SwarmLastFire60[]` BSS + `s_SwarmGpuAiFiresApplied` counter
- `port/include/swarm_test.h`   -- exported the two new functions
- `port/fast3d/swarm_gpu.cpp`   -- readback consumer now calls `swarmTestApplyAiDecision` per bot + drains the applied-fire count for the summary log line
- `context/bugs.md`             -- B-308 PARTIAL -> FIXED-PENDING-PLAYTEST 2026-05-16 (c3807-finish, dev)

No CMakeLists, no header generator output, no smoke JSON changes, no protocol bump, no MPSETUP / save-format change.

## Risk

LOW. Surface area:

- `swarmTestApplyAiDecision` is only called from the GPU_FULL readback path, which only runs after KEY_O is pressed on a GPU swarm scenario. Default-on POS_ONLY behaviour is byte-for-byte preserved (verified by smoke).
- `chrDamageByImpact` is called against the player who has `invincible = 1` set in the swarm setup. Damage is absorbed; the call walks the canonical damage path without externally visible mutations to player state. If the player loses invul somehow, they'd take 1.0 raw damage per bot per 0.5s.
- `modelSetAnimation` is the same call used by chraction.c::chrRunPos for CPU swarm bots. Direct call from the readback consumer is safe because GPU bots have `myaction = MA_NONE` (no background action tick that would clobber the anim).
- Per-bot cooldown table is 16 KB BSS; far below any pool budget.

## Slice scope deferred (next pickup)

- (c) Async PBO + fence ring readback. Pickup: `port/fast3d/swarm_gpu.cpp::swarmGpuStepAndApply::glGetBufferSubData` line.
- (d) Wider Tier 1 scope per `context/designs/in-flight/gpu-swarm-bot-pipeline.md`: per-bot target selection, LOS / visibility raycast, per-weapon range tables, dodge / cover / hill state. Pickup: shader `kSwarmCs` `if (P.do_ai != 0)` block.

The remaining work is structurally independent of slice 2. Slice 2's deliverable -- "GPU bots demonstrably fire AND animate in GPU_FULL mode" -- is what Mike's playtest will validate.
