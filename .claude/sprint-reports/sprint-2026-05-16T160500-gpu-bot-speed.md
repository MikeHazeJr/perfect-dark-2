# Sprint: GPU swarm bot speed tune (1620 -> ~300 cm/sec)

- **Date**: 2026-05-16
- **Card**: c029 (Benchmarking)
- **Branch**: dev (no worktree per task rules)
- **Status**: FIXED-PENDING-PLAYTEST. Smoke verifies BENCHMARK.SWARM.GPU.VEL max_unit_per_frame=5.00 at every ladder step (4 -> 8 -> 16 -> 32 -> 48 -> 64 -> 128).

## Task framing (from caller triage)

Mike's playtest report: "GPU bots are insanely fast." Triage at
`port/fast3d/swarm_gpu.cpp:595` flagged `s_Params.max_speed = 27.0f` (1.5x
BOTDIFF_PERFECT bump from 2026-05-01 S593d). Per-dispatch math at lines 329-335:
- `step = max_speed * dt * 60.0 = 27.0 * (1/60) * 60 = 27.0` units per dispatch
- `vel = (to_player/d) * 27.0`, `pos += vel * dt * 60.0 = vel * 1.0`
- => +27 units per shader dispatch, ~1620 cm/sec straight-line beeline at 60Hz
- OG walking speed ~180-360 cm/sec; GPU bots running 5-9x too fast

Recommended default: 5.0. Verify recursively.

## Math confirmed

Both `dt * 60.0` factors collapse to identity at the 60 Hz target, so `max_speed`
is functionally `units per dispatch` not `units per second`. The 27.0 value came
from S593d (2026-05-01) where 18.0 (the original constant) was bumped by 1.5x
for the BOTDIFF_PERFECT intent. Both interpretations were wrong: the GPU
applies max_speed raw per frame, with no AI throttle layer to soften it.

CPU swarm bots run through `botCalculateMaxSpeed` (BOTTYPE_SPEED * 14.0 capped
at 7.5 via the CHRHFLAG 0x00040000 swarm-lock at `src/game/bot.c:1800`), then
through the moverate accumulator + navnet face-turn + obstacle avoidance, so
their effective straight-line velocity sits well under 7.5/frame. GPU bots had
no such throttle pipeline.

## The fix

### `port/fast3d/swarm_gpu.cpp::s_Params.max_speed` (primary)
- 27.0f -> 5.0f. 5.0 units/frame at 60 Hz = 300 cm/sec, the OG running-enemy band.
- Multi-paragraph docblock explaining the units math + CPU comparison + 27.0 history.

### `port/src/swarm_test.c::SWARM_MAX_SPEED` (fallback)
- 27.0f -> 5.0f. Keeps the GL <4.3 / compute-compile-failure fallback path
  visually-matching the compute path.

### `port/fast3d/swarm_gpu.cpp` velocity sampler (new, ~50 lines)
Per Mike's "test recursively" directive: added BENCHMARK.SWARM.GPU.VEL log line
that fires every 60 dispatches (~1 Hz). Walks the readback, finds
max + average XZ-speed across active bots, emits both units-per-frame and
cm-per-sec. Fires in BOTH POS_ONLY and FULL modes so the smoke test (which uses
GPU_POS_ONLY default) gets the signal. Cost: ~16 KB/frame L1 traffic at 4096
bots, dwarfed by the 320 KB readback.

Also surfaced a stale `<math.h>` include order: `include/PR/gu.h` declares
`sqrtf` without `extern "C"`, so calling sqrtf from a .cpp file with project
headers already in scope fails to link against C-linkage libm. Fixed by moving
`<cmath>` to the top of the file (same pattern as gfx_pc.cpp / actionmap.cpp /
pdgui_theme.cpp).

### `tools/smoke-verify/tests/swarm_gpu_smoke.json`
- Added required_lines regex for `BENCHMARK.SWARM.GPU.VEL ... max_speed_cap=5.0`.
- Added forbidden_patterns regex catching `max_unit_per_frame >= 6` or
  `max_cm_per_sec >= 400` (catches any future regression toward 27.0).
- Added required_counts: VEL line must appear at least once.

## Verification

Built clean (all 4 targets): PerfectDark.exe, PerfectDarkServer.exe,
logview.exe, PD2ModPropHandler.dll. Direct invocation of
`PerfectDark.exe --smoke <swarm_gpu_smoke.json> --no-update-check --no-sound
--no-net --launch-scenario swarm_gpu` showed the velocity sampler emitting:

```
BENCHMARK.SWARM.GPU.VEL: count=4 active=4 max_unit_per_frame=5.00 \
  avg_unit_per_frame=5.00 max_cm_per_sec=300 avg_cm_per_sec=300 \
  max_speed_cap=5.0
```

at every ladder step (4 -> 8 -> 16 -> 32 -> 48 -> 64 -> 128). After bots reach
the player (`d < 1.0` shader clamp), velocity drops to 0.00 as expected. No
FATAL / EXCEPTION_ACCESS_VIOLATION / LOUDFAIL in the smoke log. Smoke exit
code 0, "result=scripted_exit".

Frame time stayed at 16.667 ms (60 FPS) at every cycle step in the SUMMARY
log. The velocity reduction does not change per-frame dispatch cost (still one
compute pass per frame).

## Notes / follow-ups

- 5.0 is tunable. If next playtest reports "too slow", bump by 1.0 and
  re-run the smoke. Going up is safer than the prior down-direction tune
  because the d-clamp at swarm_gpu.cpp:331-333 prevents overshoot when range
  is smaller than one step.
- Per-state speed (close-attack vs distant-seek) is plumbed via the kernel's
  three-band AI but currently uses a single max_speed for all bands. The
  triage suggested this as optional; deferred.
- The sqrtf-linkage hazard in `include/PR/gu.h` (extern decl without `extern
  "C"`) is a latent footgun. Only swarm_gpu.cpp is currently affected because
  the other .cpp files include `<cmath>` first by convention.
- The smoke test's `required_lines` line for max_speed_cap=5.0 acts as a
  silent-bump guard: any future change to s_Params.max_speed will fail the
  smoke until the smoke JSON is updated, mirroring the
  `g_TestExpectedNetProtocolVer` pin pattern.

## Files touched

- port/fast3d/swarm_gpu.cpp (max_speed tune + velocity sampler + cmath
  include reorder + sqrtf-decl-comment removal)
- port/src/swarm_test.c (SWARM_MAX_SPEED tune)
- tools/smoke-verify/tests/swarm_gpu_smoke.json (assertion update)
- .claude/sprint-reports/sprint-2026-05-16T160500-gpu-bot-speed.md (this file)
