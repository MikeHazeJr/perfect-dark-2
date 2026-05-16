# Sprint Report - c038 Capsule.c Instrumentation for Smoke Assertions

- Date: 2026-05-15 (UTC)
- Branch: dev
- Worktree: none (main checkout, worktrees disabled)
- Commit: `6a6425bb` Physics-collision - c038: capsule.c instrumentation for smoke assertions
- Refs: c038 (wall-jump glitch + two-stage capsule sweep design)

## Summary

Added five canonical `CAPSULE:` log markers to `src/lib/capsule.c` so the
physics-collision pillar can be asserted directly by smoke-verify instead
of inferred by chain-of-evidence ("no AV + ticks survived" -> "must have
been called correctly"). Unblocks Track G (wall-jump physics test) which
needs to assert "capsule sweep actually evaluated the geometry" -- not
just "the program didn't crash."

All markers gated on `#if defined(PD_DEV_BUILD)`: dev / prerelease /
local builds get the lines, stable release builds compile them out
entirely (production has zero printf cost).

Build verify PASS (`ninja -C Build pd pd-server`), no new warnings.

## What landed

File: `src/lib/capsule.c` (+49 / -1, 1 file)

Five markers added through a single `CAPSULE_LOG(...)` macro:

1. **`CAPSULE: sweep enter ...`** -- top of `capsuleSweep` after the
   trivial no-movement early-out. Emits start pos, move delta, radius,
   ymin/ymax offsets. This is the "I was actually called" sentinel.

2. **`CAPSULE: sweep result=BLOCKED type=<FLOOR|CEILING|WALL|PROP|NONE>
   dist=<f> geoflags=0x<hex> step=<i>/<N>`** -- the single canonical
   collision exit. Includes the classified hit type (decoded from
   `cast->hittype` after the FLOOR/CEILING/WALL/PROP/fallback ladder),
   the safe fraction, the raw geo flags from `cdGetGeoFlags()`, and
   which sub-step out of `CAPSULE_SWEEP_STEPS` (16) tripped the hit --
   the step index is the closest proxy to "distance along the sweep"
   without a real swept distance.

3. **`CAPSULE: sweep result=CLEAR dist=1.000`** -- canonical no-hit
   exit when all 16 sub-steps clear.

4. **`CAPSULE: findFloor result=<PROP|BG> y=<f> ...`** -- single canonical
   exit of `capsuleFindFloor`, after the `propFloor > bgGround + 1.0f`
   decision. Logs both candidates and the geo flags of the floor.

5. **`CAPSULE: findCeiling result=<PROP|BG> y=<f> ...`** -- single
   canonical exit of `capsuleFindCeiling` showing winner + both
   candidates.

The classification switch inside `capsuleSweep` is itself gated on
`#if defined(PD_DEV_BUILD)` (not just the macro) so the `hitclass`
local doesn't become an unused-variable warning in stable builds.
`capsuleFindFloor`'s second marker reuses `floorflags` -- a variable
that already exists in the function -- so no new locals were
introduced for the gated path.

## Markers vs. the task spec

The task spec asked for 3-5 strategic log lines and called out
`STAIR-STEP` / `tri-hit prim=<id>` as optional. I deliberately did
not add either:

- The current `capsuleSweep` body has **no stair-step branch** -- it
  is a flat sub-step sweep with a single classification ladder. Adding
  a fake `step result=` line would be a placeholder, not a marker. The
  two-stage sweep design at `context/designs/physics-collision/jump-two-stage-sweep.md`
  is the future home for that marker; it lands when stage-2 lands.
- The current `capsuleSweep` does not have a tri-vs-primitive inner
  loop -- it delegates everything to `cdTestVolume`. A meaningful
  `tri-hit prim=<id>` marker would have to live inside `cd.c` /
  `meshcollision.c`, which is out of scope for this task.

Five markers is the natural ceiling for the current code shape.

## Gate convention

The codebase already uses `#if defined(PD_DEV_BUILD)` (see e.g.
`port/fast3d/pdgui_backend.cpp:654`). `PD_DEV_BUILD` is set by the
CMakeLists.txt target_compile_definitions for the `pd` target whenever
`PD_STABLE_RELEASE=OFF` (the default). It is NOT defined on `pd-server`.

`src/lib/capsule.c` links into `pd` only (it sits in `SRC_LIB`, which
the pd-server build deliberately omits -- pd-server links no
collision code). So the gate is sound: stable release pd builds
compile the markers out; pd-server never sees them at all.

I considered a runtime `g_DebugCapsuleLog` flag as well, but rejected
it: capsule sweep is hot (called every frame from `bondwalk.c`) and a
runtime flag would still cost a branch + cmp on the production path.
Compile-time gating is the cleaner answer and matches precedent.

## Why no JSON tightening

The task spec offered to optionally tighten
`tools/smoke-verify/tests/physics_capsule_basic_smoke.json` with a
required-patterns entry for `CAPSULE: sweep result=`. I did NOT do
this, because:

```c
// src/include/lib/capsule.h
#define PC_CAPSULE_ENABLED 0
```

The whole capsule pipeline is currently gated off via call-site
`#if PC_CAPSULE_ENABLED` checks in `bondwalk.c` (lines 1047, 1240,
1358). `capsuleSweep`, `capsuleFindFloor`, and `capsuleFindCeiling`
are **not called by the live game today**. Track G is the work that
flips `PC_CAPSULE_ENABLED` and wires the calls in.

Adding a `CAPSULE: sweep result=` required-patterns entry today would
actively fail the green `physics_capsule_basic_smoke` test, because
no `CAPSULE:` lines would be emitted at all. The markers are dormant
infrastructure -- ready for Track G to consume them.

When Track G wires in the capsule pipeline, the harness will start
seeing `CAPSULE:` lines and the assertion can land in that commit.

## Build verify

```
$ source devtools/build-env.sh && ninja -C Build pd pd-server
... [3/3] Linking CXX executable PerfectDark.exe
... Linking CXX executable PerfectDarkServer.exe
```

Both targets built clean. No new warnings introduced by the diff.
A transient ccache/ninja race reported `code=1` with no stderr on
the first attempt; rerunning ninja produced clean output (direct
ccache invocation confirmed exit=0 on the file). This is a known
Windows-side harness flake unrelated to the diff.

## Smoke verify

Not executed in this session: the smoke runner is PowerShell-only
(`tools/smoke-verify/run.ps1`) and PowerShell is denied in this
worker's tool permission set. Per the analysis above, the existing
`physics_capsule_basic_smoke.json` test status is unchanged -- the
markers are dormant under `PC_CAPSULE_ENABLED=0` so no `CAPSULE:`
lines would appear in the log today even if the test ran. The test's
existing assertion set (BOOT/LOAD/TICK/SETUP/SMOKE sentinels) does not
overlap with the new markers and is not affected.

## Hard-rule compliance

- No kanban / session-log edits made.
- No sub-agents invoked.
- Worktrees not used (main checkout).
- Build verify performed (PASS).
- Performance gate: `#if defined(PD_DEV_BUILD)` compile-time exclusion
  -- production builds incur zero cost.
- Single-file diff: `src/lib/capsule.c` only. Other dirty files in the
  worktree (`CMakeLists.txt`, `port/src/server_main.c`,
  `port/src/server_stubs.c` -- in-progress c115 server smoke-harness
  work) were deliberately not staged.

## Followups for Track G

When the capsule pipeline goes live:

1. Add the assertion `CAPSULE: sweep result=` to
   `physics_capsule_basic_smoke.json` (one occurrence is enough -- the
   pipeline fires often enough that smoke runs will see hundreds).
2. Consider a dedicated `LOG_CH_PHYSICS` channel registration so the
   `CAPSULE:` prefix is classified into a filter group rather than
   falling through the default pass. Volume warrants this if/when the
   marker count climbs (e.g. once the two-stage sweep + stair-step
   path lands).
3. Add a wall-jump-specific test that asserts
   `CAPSULE: sweep result=BLOCKED type=WALL` to prove the wall-jump
   geometry path is actually evaluated -- not just that the player
   physics stays alive.
