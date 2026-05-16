# Sprint Report: c029 Slice 3 GPU swarm perf + 4-loop merge

- Date: 2026-05-16
- Branch: dev
- Card: c029 (Benchmarking)
- Worktree: none (direct on dev per session directive)

## Goal

Reduce per-frame BG floor raycasts from ~1536/frame at 512 bots to ~150-300/frame,
AND collapse the four per-bot walks in `port/fast3d/swarm_gpu.cpp` into ONE pass.
Bundled Track 1 (perf cache) + Track 5 (function-merging refactor) from the
multi-track design memo.

## Shipped

Single coherent commit with three structural pieces:

1. `chrSetPosWithCachedGround` helper extracted from `chrSetPos` in
   [src/game/chraction.c](../../src/game/chraction.c). Takes pre-sampled
   `ground`, `floorcol`, `floortype`, `floorroom` so callers that already hold
   the ground info skip the embedded `cdFindGroundInfoAtCyl` raycast. Public
   declaration in [src/include/game/chraction.h](../../src/include/game/chraction.h).
   The legacy `chrSetPos` keeps full behavior (2 internal raycasts) and now
   delegates to the helper after running `chrMoveToPos` + raycasting once.

2. Per-bot floor-sample cache in
   [port/fast3d/swarm_gpu.cpp](../../port/fast3d/swarm_gpu.cpp) implementing
   Strategy 1A (hybrid drift + age cache). New static arrays
   `s_FloorCache[SWARM_GPU_MAX]` storing per-slot last sampled position, last
   surface_up, last ground info, and frame stamp. Hit criteria:
   `|pos - last_sample_pos| < SWARM_FLOOR_SAMPLE_DRIFT_CM (60.0)` AND
   `age < SWARM_FLOOR_SAMPLE_MAX_AGE_FRAMES (10)`. Miss path raycasts twice
   (surface_up sampler + cdFindGroundInfoAtCyl) and refreshes the cache.

3. Loop merge: collapsed the post-readback apply / velocity sampler / AI
   consumer (lines :999-1240 pre-refactor) into ONE walk of `chrs[0..apply_count)`.
   The pre-pass upload loop remains (must run before the SSBO upload + dispatch)
   but it now consults the cache and skips both raycasts on a hit.

4. New `swarmGpuInvalidateFloorCache()` API. Called from the same junction
   points as `swarmGpuInvalidateReadback` in
   [port/src/swarm_test.c](../../port/src/swarm_test.c) -- session start, cycler
   tick after `respawn_swarm`, session end. Drops the entire cache; the next
   dispatch repopulates via the miss path.

5. New `BENCHMARK.SWARM.GPU.PERF` log line (1 Hz) reporting
   `sampled / reused / hit_pct / pre_pass_raycasts / chrsetpos_raycasts /
   total_raycasts` so the cache hit rate is observable in the smoke / playtest
   log.

## Decisions

The smoke test surfaced a regression class that forced a partial back-off on
piece 1's intended use:

**chrSetPos refactor at the swarm apply site backed out (B-332 mitigation)**. The
initial implementation routed the apply loop through
`chrSetPosWithCachedGround` when `ce->valid` was true, dropping the chrSetPos
internal raycast pair. Smoke test went 4 -> 8 cleanly, then AVed at 8 -> 16
inside `despawn_all -> chrRemove` because 3 of the 8 newly-spawned bots ended
up in a state where `chr->prop` was set but `chr->prop->chr` was NULL.

The structural difference vs legacy `chrSetPos` is the omitted `chrMoveToPos`
pre-flight, which runs `chrAdjustPosForSpawn` + a `propDeregisterRooms` /
`roomsCopy` / `chr0f0220ac` cycle at spawn-adjusted rooms. The swarm path
called `chrSetPos(findground=true)` every frame, so this room re-registration
was happening unconditionally; skipping it left the prop's room state stale
in a way that broke a downstream invariant (root cause not yet identified --
it shows up only at the cycler-tick respawn boundary).

The conservative mitigation keeps the floor cache (surface_up sample skipped
on cache hits in the pre-pass) but reverts the apply path back to
`chrSetPos(findground=true)`. The cached `ground` / `floorcol` / `floortype` /
`floorroom` are still written to `s_FloorCache[i]` on miss so a future
re-introduction has the values ready, but the apply path does not currently
consume them. The unused-fields cost is a few bytes per slot in BSS.

`chrSetPosWithCachedGround` itself remains correct for callers that genuinely
have cached ground info AND don't need the chrMoveToPos pre-flight (e.g. an
authoritative position teleport with verified-current room registration). The
extraction is the right shape; only the swarm caller dropped the legacy path
because of the surfacing bug.

## Blockers

None blocking the lane close-out. The deferred work below is logged so a
future c029 follow-up sprint can resurrect the full Slice-3 target (3x perf
gain instead of the ~20% gain shipped).

## Follow-ups

- **Root-cause B-332 chrSetPos refactor regression.** The 3-of-8-bots NULL
  `prop->chr` divergence at the 8 -> 16 cycle transition is consistent with
  some downstream invariant maintained by `chrMoveToPos -> propDeregisterRooms
  / chr0f0220ac` even when the rooms didn't change. Likely fix shape: have the
  swarm pre-pass capture the spawn-adjusted rooms via a partial
  `chrAdjustPosForSpawn` call and pass them into the cached helper so the
  apply loop re-registers via `chr0f0220ac` even on cache hits. Or: add a
  `findroom` parameter to `chrSetPosWithCachedGround` that wraps the
  propDeregisterRooms / chr0f0220ac trio. Both keep the 2-raycast saving.

- **Tighten SWARM_FLOOR_SAMPLE_DRIFT_CM.** At 256 bots the hit rate is 61%; at
  512 bots ~80%; at 768 bots ~84%. The hit rate increases with count
  (more stationary bots packed around the player). Bumping the drift threshold
  from 60 cm to e.g. 120 cm would push hit rates higher at low counts at the
  cost of slightly more stale ground data. Worth a playtest cycle once the
  follow-up above lands.

- **Re-measure ticks/sec at each tier in a non-smoke playtest.** The smoke
  harness caps frame_avg_ms at ~10 ms (SDL pacing in headless mode) so the
  numbers don't reflect the actual GPU swarm tick. Mike's interactive playtest
  at 512 / 768 bots with live perf overlay will tell us the real wall-clock
  saving from the cache alone.

## Kanban Changes

c029 lane stays open. Slice 3 mitigation ships; the deeper helper-vs-chrSetPos
investigation lands in a follow-up slice. No new cards filed; B-332 documented
inline above and in the swarm_gpu.cpp comment block.

## Files Touched

- `src/game/chraction.c` -- extracted `chrSetPosWithCachedGround`; legacy
  `chrSetPos` rewritten as the cached helper + the chrMoveToPos pre-flight +
  one cdFindGroundInfoAtCyl wrapper.
- `src/include/game/chraction.h` -- declared `chrSetPosWithCachedGround`.
- `port/fast3d/swarm_gpu.cpp` -- pre-pass floor cache (drift + age hybrid);
  merged post-readback walk consolidating apply / velocity / AI; new
  `swarmGpuInvalidateFloorCache()` public; new `BENCHMARK.SWARM.GPU.PERF` log
  line at 1 Hz. Conservative mitigation comment block documents the helper
  back-off rationale at the apply call-site.
- `port/src/swarm_test.c` -- `swarmGpuInvalidateFloorCache()` extern + 3 call
  sites paralleling `swarmGpuInvalidateReadback` (session-arm,
  cycler-tick-post-respawn, despawn_all-on-session-end).

## Verification Notes

Build verify (all 4 targets):

```
$ PATH=/c/msys64/mingw64/bin:$PATH ninja -C Build pd pd-server pd-tests Updater.exe
[1/3] Building CXX object CMakeFiles/pd.dir/port/fast3d/swarm_gpu.cpp.obj
[2/3] Linking CXX executable PerfectDark.exe
...
ninja: no work to do.
```

Final binary sizes:
- PerfectDark.exe = 55.7 MB
- PerfectDarkServer.exe = 22.5 MB
- pd-tests.exe = 24.9 MB
- Updater.exe = 12.3 MB

Smoke verify (`swarm_gpu_smoke.json`, 4 -> 768 ladder, 240 s scripted exit):

Pre-mitigation (full chrSetPos refactor): FAILED with
`FATAL: ACCESS_VIOLATION` in `chrRemove(prop=...)` at the 8 -> 16 cycle
transition (frame 2904). 3 of 8 bots had `chr->prop->chr == NULL` despite
`chr->prop != NULL`.

Post-mitigation (conservative path, cache only on surface_up): PASSED. All 9
cycle events fired (4 -> 8 -> 16 -> 32 -> 48 -> 64 -> 128 -> 256 -> 512 ->
768); `SMOKE: result=scripted_exit ... code=0`; no FATAL / EXCEPTION;
`BENCHMARK.SWARM.GPU: SUMMARY count=768` line emitted.

Cache hit rate (steady state per tier) from `BENCHMARK.SWARM.GPU.PERF` line:

| Tier  | sampled | reused | hit_pct | pre_pass_raycasts | chrsetpos_raycasts | total |
|-------|--------:|-------:|--------:|------------------:|-------------------:|------:|
|   8   |    0    |   8    | 100.0%  |        0          |        16          |   16  |
|  16   |  varies |        |  ~95%   |                   |                    |       |
|  64   |  varies |        |  ~85%   |                   |                    |       |
| 256   |   99    |  157   |  61.3%  |       198         |       512          |  710  |
| 512   |  ~95    |  ~415  |  ~82%   |      ~190         |      1024          | 1214  |
| 768   |  ~125   |  ~643  |  ~83%   |      ~250         |      1536          | 1786  |

Pre-Slice-3 baseline at 512 bots: 1536 raycasts/frame (512 * 3). Post-Slice-3
conservative: 1214 raycasts/frame (~21% reduction). Pre-Slice-3 at 768 bots:
2304 raycasts/frame. Post-Slice-3 conservative: 1786 (~22% reduction).

The hit rate climbs with count (more stationary bots clustered around the
player), so the absolute raycast saving scales favorably. The cache pays for
itself everywhere from 8 bots up; even the 8-bot stationary case shows 100%
hit rate after the first dispatch refreshes the cache.

`frame_avg_ms` from the SUMMARY line is not a meaningful perf signal in the
smoke harness (SDL pacing caps it at ~10 ms in headless mode). Mike's
interactive playtest with the perf overlay is needed to read the actual
wall-clock saving.

Smoke verify run path:

```
$ cd .claude/smoke-verify-install && rm -f pd-client.log && \
  ./PerfectDark.exe --smoke ../../tools/smoke-verify/tests/swarm_gpu_smoke.json \
    --no-update-check --no-sound --no-net --launch-scenario swarm_gpu
...
[04:00.38] SMOKE: result=scripted_exit scenario=swarm_gpu_smoke elapsed_ms=240014 events_fired=14/14 code=0
```
