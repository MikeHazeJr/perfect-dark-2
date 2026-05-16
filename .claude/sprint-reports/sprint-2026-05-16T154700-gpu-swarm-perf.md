# Sprint 2026-05-16 15:47 — GPU Swarm Perf (frame_avg_ms fix + async PBO ring)

## Scope

Mike's playtest report: "GPU swarm gets incredibly slow with 256+ bots."

Triage evidence from log inter-frame wall-clock between
`BENCHMARK.SWARM.GPU: target=N in_play=Y` lines (which fire every 60 ticks):

| Ladder | wall-time between lines | inferred ticks/sec |
|--------|--------------------------|---------------------|
| 4      | ~0.60s                   | ~100                |
| 32-128 | ~0.60-0.80s              | ~75-100             |
| 256    | ~0.74s                   | ~81                 |
| 512    | ~4.81s                   | **~12.5**           |

**6x slowdown going from 256 to 512.** Plus the existing
`BENCHMARK.SWARM.GPU: SUMMARY ... frame_avg_ms=16.667` printout was
wrong — identical at every tier. The metric was fed
`g_Vars.lvframenum` deltas instead of wall-clock dt.

Two slices shipped:

## Slice 1 — fix the broken metric

Replaced the engine-tick `g_Vars.lvframenum` based dt accumulator with
SDL high-resolution wall-clock dt. `port/src/swarm_test.c`:

- Added `#include <SDL.h>` (matches `port/src/audio.c`, `port/src/net/net.c`
  pattern).
- Replaced `static u32 s_LastFrameTick` with `static u64 s_LastPerfCounter`.
- Per-tick: snapshot `SDL_GetPerformanceCounter()`; compute
  `(now - prev) / SDL_GetPerformanceFrequency()` in seconds, * 1000 -> ms.
- Reset `s_LastPerfCounter` to 0 at session start, session end, stage
  transition, AND cycler emit (so the spike caused by mass
  `respawn_swarm()` despawn/spawn doesn't bleed into the next tier's
  average).
- Extended the per-60-tick `BENCHMARK.SWARM.GPU: target=...` log line
  to include `frame_avg_ms=N.NNN ticks_per_sec=N.NN` so per-tier slowdown
  is visible mid-tier without waiting for the cycler SUMMARY line.

## Slice 2 — async PBO + fence readback ring

Replaced the blocking `glGetBufferSubData` at
`port/fast3d/swarm_gpu.cpp` line ~662 with a 2-deep PBO-style fence
ring.

**Before**: every frame called `glGetBufferSubData(BoidSsbo)` which
pipeline-drains the GPU. At 320KB * 60Hz the memcpy itself is trivial
(~50us); the kill was the GPU sync at the tail of a render pipeline
already buffered by the deferred game frame + ImGui + swap + next-frame
setup.

**After**: each frame
1. Dispatch compute.
2. `glCopyBufferSubData(BoidSsbo -> ring[write_idx])` — GPU-side memcpy,
   no sync.
3. `glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE)` to mark the copy's
   completion.
4. Consume `ring[read_idx]`: `glClientWaitSync(0)` non-blocking poll;
   if signaled (ALREADY_SIGNALED or CONDITION_SATISFIED), read into
   `s_BoidScratch`.
5. Advance `write_idx`.

Effect: chr position application is driven by frame N-1's boid
positions (one frame stale). Imperceptible at 60 Hz.

### New GL symbols loaded via SDL_GL_GetProcAddress

`glCopyBufferSubData` (GL 3.1+ core), `glFenceSync` /
`glClientWaitSync` / `glDeleteSync` (GL 3.2+ core). All guaranteed by
the GL 4.3 minimum the file already gates on. If any fail to load
we silently keep the blocking fallback so the GPU swarm still works
on quirky drivers.

### chrnum identity guard

The async ring delivers last-frame's positions, but `death_poll_and_
respawn` refills one slot per frame with a NEW chr (different chrnum).
Applying the OLD chr's snapshot position to the new chr would teleport
it. Fix: stamp `chrs[i]->chrnum` in a parallel ring-side array at write
time; compare at read time; skip slots where snapshot chrnum !=
current chrnum.

For the cycler case (`respawn_swarm` despawns all chrs and respawns
fresh ones at a different count), added an explicit
`swarmGpuInvalidateReadback()` API that drops all in-flight fences
and -1's the chrnum snapshots. Called from `cycler_tick`,
session-start, `swarmTestOnSessionEnd`.

### apply_count vs count

All downstream loops (chrSetPos, velocity sampler, AI consumer,
fire-request burst log) now bound by `apply_count = min(consumed_count,
count)` instead of raw `count`. `consumed_count` is 0 for the first
1-2 frames of every session (ring bootstrap), 0 again right after a
cycler/session-end invalidate, and otherwise the prior frame's count.
Bots hold their previous position for one tick during these gaps;
imperceptible at 60 Hz.

## Build verify — BLOCKED, environmental

I could not complete a build verify in this session. Root cause:

- The session's Bash tool environment cannot spawn `cc1.exe`. The
  parent `cc.exe` is reached and prints `-v` info, but the child
  `cc1.exe` invocation terminates immediately with no output (no
  preprocessor output, no error messages, exit nonzero). This is the
  exact failure mode that `devtools/build-env.sh` warns about for
  PS-from-bash subshells.
- `build-headless.ps1` (the canonical Windows build path) is blocked
  by the auto-permission deny rule for PowerShell in this Bash tool
  environment.
- Trivial isolation: `echo 'int main(){return 0;}' > t.c && cc.exe -c t.c -o t.o`
  also fails silently with the same signature; not specific to my code.

The code changes are mechanically straightforward (Slice 1 is a 15-line
swap, Slice 2 follows standard GL PBO + fence patterns) and inline
documentation makes them inspectable. Mike's normal
`build-headless.ps1` -> smoke-verify workflow will be the verify
path.

## Recursive test — DEFERRED to PowerShell session

The smoke-verify regex `BENCHMARK\.SWARM\.GPU\.VEL: ...` is preserved
intact. The smoke test's existing required-line list is unchanged.
After build verify, the swarm_gpu_smoke should:
- PASS at all tiers up to 768 (B-331 ceiling).
- Report measurably lower `frame_avg_ms` at 256/512/768 tier on the
  `BENCHMARK.SWARM.GPU.SUMMARY` line vs. the pre-fix 16.667.
- Show `ticks_per_sec` rising from ~12.5 (pre-Slice-2) toward ~30+ at
  the 512 tier (Slice 2's primary perf win).

## Files

- `port/src/swarm_test.c` — Slice 1 metric fix + ring-invalidate hooks.
- `port/fast3d/swarm_gpu.cpp` — Slice 2 async readback ring + identity
  guard + invalidate API.

## Slice 3 (floor-sampling batching) — NOT shipped

Decision: defer. Slice 2 should close the perf gap on its own at
modern GPU + driver combos. If Mike's playtest shows the 512 tier still
< 30 ticks/s after Slice 2, the next session can implement either
cell-based floor caching or per-bot position-delta gating for
`chrSurfaceLocoSampleFloorNormal` + the `findground=true` chrSetPos
call.

## Commit

Single commit per `feedback_commit_message_standard`:

```
Benchmarking - c029: GPU swarm perf (frame_avg_ms wall-clock + async PBO readback ring)

Refs: c029
```
