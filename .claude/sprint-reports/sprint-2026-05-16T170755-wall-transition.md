# Sprint c3738 — Wall-Transition Climb Trigger (Slice 5 Follow-up)

**Date:** 2026-05-16
**Branch:** dev
**Pillar:** Physics-collision (surface locomotion)
**Card:** c3738
**Predecessor:** c3679 / Slice 5 (commit `e5771cda`) — added the -surface_up
raycast direction and the drop heuristic, but left the climb trigger TBD.

## Problem

After Slice 5 a Skedar on a floor still bunches against a wall and never
transitions. Root cause: `chrSurfaceLocoSampleFloorNormal` raycasts along
`-chr->surface_up`. For a floor-walking bot `surface_up == (0,1,0)` so the
ray points straight down and only ever sees floor geometry. Walls the bot
is approaching laterally are invisible to the sampler.

## Fix

Added a **forward-projected wall sampler** that runs in `chrSurfaceLocoTick`
BEFORE the existing floor sample. Cast direction = bot velocity projected
onto the current surface plane; ray length = `radius*1.5 + height*0.5`;
geoflags = `GEOFLAG_WALL | GEOFLAG_BLOCK_SIGHT`.

On wall hit, the wall normal becomes the new target for the existing
8-frame blend (`chrSurfaceLocoTick:151-163` pattern, copied above the
floor-sample branch). The bot smoothly re-orients onto the wall over
~133ms; subsequent ticks' floor sampler — now pointing perpendicular to
the wall — keeps the bot aligned. Outside-corner case (walking off a
ledge into open space) was already handled by the Slice 5 drop heuristic.

## Files Changed

- `src/include/game/surface_loco.h`
  - Declared `chrSurfaceLocoSampleWallAhead`.
  - Added 4 tuning constants:
    - `SURFACE_LOCO_WALL_LOOKAHEAD_MULT_RADIUS = 1.5f`
    - `SURFACE_LOCO_WALL_LOOKAHEAD_MULT_HEIGHT = 0.5f`
    - `SURFACE_LOCO_WALL_NORMAL_THRESHOLD = 0.3f`
    - `SURFACE_LOCO_WALL_MIN_VEL = 2.0f`

- `src/game/surface_loco.c`
  - Added `#include "system.h"` for `sysLogPrintf`.
  - Inserted wall-ahead probe at top of `chrSurfaceLocoTick`. Mirrors the
    canonical edge-blend block (save prev / set target / kick countdown).
    Logs `SURFACE_LOCO.WALL_BLEND: chrnum=%d wall_normal=(x,y,z)` on each
    kick (LOG_NOTE, ungated -- can be quieted later if it floods).
  - Implemented `chrSurfaceLocoSampleWallAhead` at end of file:
    1. Disabled-chr early-out (same gate as `chrSurfaceLocoSampleFloorNormal`).
    2. Velocity source: caller-supplied `vel_hint` or fallback to
       `prop->pos - prevpos`. Chose prev-pos delta as the canonical
       path per the design memo's "easier" recommendation -- no plumbing
       through swarm_gpu.cpp needed.
    3. Project velocity onto the surface plane (perpendicular to
       current surface_up): keeps the cast strictly lateral on
       floor/wall/ceiling regardless of vertical drift.
    4. Skip below `SURFACE_LOCO_WALL_MIN_VEL` (2.0 units/frame ~= slow walk).
    5. `cdExamLos08` with `GEOFLAG_WALL | GEOFLAG_BLOCK_SIGHT`. FLOOR
       flags deliberately omitted -- including them would fire on every
       adjoining floor tile.
    6. Flip the hit normal toward the chr (the canonical pattern).
    7. Gate on `dot(wall_normal, surface_up) < 0.3` -- 0.3 ~= 72.5deg.
       If the wall normal is too close to the current up the geometry
       is treated as a slope-blend and the floor sampler handles it.

## Why prev_pos and not a plumbed velocity hint

The design memo offered two paths:
- Plumb a vel_hint through `chrSurfaceLocoTick` from swarm_gpu.cpp.
- Read `prop->pos - prevpos` inside the helper.

The helper signature still accepts a `vel_hint` so the GPU swarm path
can pass its readback velocity later if measurements show the prevpos
delta is too noisy (GPU bots don't always update prevpos through the
chr anim path). For now the NULL hint path uses `chr->prevpos`. Spawn
sets prevpos = pos at chrTickPrep (chr.c:1483) so first-tick velocity
is 0, which is benign -- the MIN_VEL gate skips the raycast.

## Plumbing GPU swarm vel later (TBD)

`port/fast3d/swarm_gpu.cpp` already has per-bot `s_BoidScratch[i].vx/vy/vz`
from the readback (line 1194). If playtest shows GPU swarm bots aren't
triggering wall climbs because their prevpos isn't tracking, the
minimal hook is to call `chrSurfaceLocoSampleWallAhead(chr, vel_arr, out)`
directly from the apply loop with `vel_arr = {vx, 0, vz}` (or actual vy
if a vertical component is meaningful), then stash the result onto
chr->surface_up* before chr.c::chrTick fires `chrSurfaceLocoTick`. The
helper API was intentionally designed to accept the vel hint to keep
that future plumbing trivial. Not done in this slice because the helper
already gracefully degrades when prevpos delta is small (MIN_VEL gate).

## Build verify

`PATH="/c/msys64/mingw64/bin:/usr/bin:/bin" ninja -C Build pd pd-server pd-updater pd-tests` clean.

- `Build/PerfectDark.exe` rebuilt 2026-05-16 17:06.
- `Build/PerfectDarkServer.exe`, `Build/Updater.exe`, `Build/pd-tests.exe`
  unaffected (surface_loco.c is pd-only target -- server stubs out chr logic).
- The bloated PATH (devkitPro, etc.) caused cc1.exe to fail silently
  with no diagnostic; reproduced with a minimal hello-world. Fix: prepend
  `/c/msys64/mingw64/bin` and run with a trimmed PATH. Not a code issue.

## Smoke tests

`tools/smoke-verify/run.ps1` is PowerShell-only and `Execution Policy
Bypass` is denied in this sandbox -- I could not invoke
`swarm_gpu_smoke` or `physics_capsule_basic_smoke` from this session.
The last green run was `results-20260516T191830Z.json` (swarm_gpu_smoke
26/26 assertions, pre-c3738). Recommend Mike re-run both as the next
step:

- `swarm_gpu_smoke` -- exercises GPU swarm pipeline w/ 4 -> 768 bots.
  Wall-ahead sampler is in the chrSurfaceLocoTick hot path for every
  Skedar, so any regression in the existing floor sample or blend
  logic will surface here.
- `physics_capsule_basic_smoke` -- exercises capsule sweeps; orthogonal
  to surface_loco but a safety net against unrelated breakage.

## Visual playtest plan

Mike to spawn Skedars in `mp_skedar_ruins` or any interior with vertical
walls. Walk a swarm group at a wall:

- Expected: bots blend their facing toward the wall normal over ~8
  frames (~133ms), then climb. The new log line
  `SURFACE_LOCO.WALL_BLEND: chrnum=N wall_normal=(x,y,z)` fires once
  per kick.
- If wall transitions don't trigger: try raising
  `SURFACE_LOCO_WALL_NORMAL_THRESHOLD` from 0.3 -> 0.5 (~60deg). 0.5
  catches shallower walls but increases false-positives on steep slopes.
- If transitions trigger but bots don't climb after rotation: that's a
  later-slice integration bug (velocity integration onto the wall),
  not a sampler issue. Slice 6+ scope.

## Hard rules compliance

- No kanban / session-log edits. (Confirmed.)
- No sub-agents. (None used.)
- Worktrees disabled. (Not used.)
- Build verify REQUIRED. (Done -- four targets clean.)
- Non-Skedar callers gated identically to existing
  `chrSurfaceLocoIsEnabled` check. (Confirmed -- the helper early-outs
  on non-enabled chrs.)
- Velocity plumbing via prev_pos delta only. (Confirmed -- no struct
  field added; vel_hint parameter exists but is NULL at the only
  in-tree call site.)
