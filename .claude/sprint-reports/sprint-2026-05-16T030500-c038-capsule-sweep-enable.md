# Sprint Report - c038 Track G: Enable Two-Stage Capsule Sweep

**Date**: 2026-05-15 (Track G activation)
**Card**: c038 (physics-collision)
**Scope**: Flip `PC_CAPSULE_ENABLED` from 0 to 1, verify no regression, author `wall_jump_capsule_smoke.json`.
**Branch**: dev (main checkout; worktrees disabled)

---

## Summary

The c038 design (`context/designs/physics-collision/jump-two-stage-sweep.md`)
proposes a two-stage capsule sweep upgrade. Stage 1 is the existing
`cdTestVolume`-based substep iteration in `src/lib/capsule.c`. Stage 2 is
a per-triangle BG validate via `bgTestHitInRoom` (rendered-only geometry
coverage). Stage 1's code structure already exists in `capsule.c` but
was gated dormant via `PC_CAPSULE_ENABLED=0`; the three call sites in
`bondwalk.c` (lines 1047, 1240, 1358) sit inside `#if PC_CAPSULE_ENABLED`
blocks.

This sprint ships the **activation gate** of Track G: flip the flag to
1 so Stage 1 of the design (the existing dormant infrastructure) becomes
live on the player movement path. Stage 2 (per-tri BG validate; new
`capsuleSweepTwoStage` entry point with `realnormal` / `hitfromrendered`
fields) is a follow-up slice -- the design doc tracks it as migration
steps 2-4. The dispatch directive explicitly allows shipping the largest
coherent slice; activating Stage 1 + authoring the activation-gate test
is the natural unit because Stage 2 has a substantial API surface
(new entry point, new struct fields, ray-bundle integration, per-tri
test fixtures) that wants a separate dispatch.

---

## Design-doc interpretation

Per `context/designs/physics-collision/jump-two-stage-sweep.md`:

- **Stage 1**: existing `cdTestVolume`-based substep iteration (16 steps,
  16-unit chunks of move vector). Cheap. Covers BG `GEOFLAG_WALL` tiles,
  prop AABBs, chr cylinders. Pre-existing in `capsule.c`. Just dormant.
- **Stage 2**: new per-triangle BG validate using `bgTestHitInRoom` and
  `bgTestHitInVtxBatch`. Fires only when Stage 1 reports
  `CDRESULT_COLLISION`. Catches rendered-only triangles without a matching
  `GEOFLAG_WALL` tile (the wall-jump glitch substrate). Surface normal
  from `hitthing.unk0c`, replacing the `-move/|move|` approximation in
  `capsule.c:144-146`.

The design's migration plan:

1. Land the new API alongside the old `capsuleSweep` -- no consumer swap.
2. Swap `bwalkUpdateVertical` at `bondwalk.c:1255` to `capsuleSweepTwoStage`.
3. Update Skedar surface-loco to consume the real normal.
4. Migrate `bwalkTryMoveUpwards` (and horizontal-step sites) opportunistically.

**This sprint scope = step 0 (activation gate for the dormant Stage 1).**
Steps 1-4 are downstream slices once Stage 1 is proven live.

---

## Implementation

### 1. `src/include/lib/capsule.h`

Flipped `PC_CAPSULE_ENABLED` from `0` to `1` and updated the leading
comment to record the Track G activation status and explain what was
activated, what was deferred, and where the design doc lives:

```c
#define PC_CAPSULE_ENABLED 1
```

Comment additions document the three call sites in `bondwalk.c` and
state that Stage 2 is a follow-up slice per the design doc.

### 2. `src/lib/capsule.c`

No code changes. The file already has the complete `capsuleSweep` /
`capsuleFindFloor` / `capsuleFindCeiling` implementations plus the five
`CAPSULE_LOG` markers added in 6a6425bb (PD_DEV_BUILD-gated). Flipping
the include flag is sufficient to activate.

### 3. `src/game/bondwalk.c`

No code changes. The three call sites are pre-existing and correctly
guarded; flipping the flag activates them as written. Verified by
inspection of lines 1047 (`vv_manground` prop-surface probe), 1240
(vertical jump sweep), 1358 (post-tryMoveUpwards ceiling clamp).

### 4. `tools/smoke-verify/tests/wall_jump_capsule_smoke.json`

New file. Boots STAGE_CITRAINING via `--skip-intro`, teleports player 0
via `--debug-spawn-at 637.0,360.0,923.0,16` (matches
`physics_capsule_basic_smoke`), then dismisses CITRAINING's
agent_select + Main Menu overlays via ACTION_USE + ACTION_CANCEL_USE
(pattern lifted from `vehicle_flow.json`), then fires 5 scripted
ACTION_JUMP taps across 35-45s. Asserts:

- `CAPSULE: sweep enter start=... move=... radius=...` fires >=2 times.
- `CAPSULE: sweep result=(CLEAR|BLOCKED)` fires >=2 times (mirror pair).
- Standard SMOKE / LOAD / TICK sentinels.
- Forbidden: AV / FATAL / timeout / `CAPSULE: sweep result=FAILED`.

The test does NOT attempt to repro the specific wall-jump glitch. That
requires (a) a known glitch ledge with rendered-only geometry and
(b) Stage 2's per-tri validation; both are follow-up slices.

---

## Build verify

```
$ powershell -NoProfile -ExecutionPolicy Bypass -File devtools/build-headless.ps1
[PASS] CLIENT  4s -> PerfectDark.exe (55.5 MB)
[PASS] UPDATER 1s -> Updater.exe (12.3 MB)

$ powershell -NoProfile -ExecutionPolicy Bypass -File devtools/build-headless.ps1 -Target server
[PASS] SERVER  2s -> PerfectDarkServer.exe (22.4 MB)

$ powershell -NoProfile -ExecutionPolicy Bypass -File devtools/build-headless.ps1 -Target tests
[PASS] TESTS   0s -> pd-tests.exe (24.9 MB)
```

All 4 targets clean. Zero new warnings in the client compile stderr
log. Build flavor is dev (PD_STABLE_RELEASE not passed), so
`CAPSULE_LOG` markers are active.

---

## Regression smoke verify

| Test | Result | CAPSULE markers in log |
|---|---|---|
| physics_capsule_basic_smoke | 19/19 PASS | 0 (no scripted jump; player teleports to flat ground, stays still) |
| mission_intro_flow | 18/18 PASS | 0 (no scripted jump; cutscene-heavy boot path) |
| vehicle_flow | 10/10 PASS | 0 (no scripted jump; mount-bike scenario) |
| **wall_jump_capsule_smoke** | **21/21 PASS** | **495** |

The new test fired **495 sweep enters paired with 495 sweep results**
across 5 scripted jumps -- well above the conservative `min: 2` floor.
Sample lines from the log:

```
[00:35.38] CAPSULE: sweep enter start=(379.9,487.0,669.5) move=(0.0,4.1,0.0) radius=30.0 ymin=-132.0 ymax=10.0
[00:35.38] CAPSULE: sweep result=CLEAR dist=1.000
[00:35.38] CAPSULE: sweep enter start=(379.9,491.1,669.5) move=(0.0,6.0,0.0) radius=30.0 ymin=-129.0 ymax=13.0
[00:35.38] CAPSULE: sweep result=CLEAR dist=1.000
[00:35.39] CAPSULE: sweep enter start=(379.9,497.0,669.5) move=(0.0,3.9,0.0) radius=30.0 ymin=-129.0 ymax=13.0
[00:35.39] CAPSULE: sweep result=CLEAR dist=1.000
```

Note the player position drift on Y (487 -> 491 -> 497) confirms the
ascent arc -- the sweep is firing once per airborne tick of the jump,
exactly as designed. All 495 results were `CLEAR` (open-air ascent on
CITRAINING's main floor); no `BLOCKED`. This is consistent with a
benign jump-and-land cycle on flat ground -- no ceiling, no wall above
the player.

The three existing tests stayed green with **zero capsule markers**
because none of them produce vertical movement (`vv_manground >
vv_ground`), which is the precondition for the airborne branch at
`bondwalk.c:1205` to fire. This confirms the gating is correct: the
sweep does not fire spuriously on grounded frames.

---

## Findings

- **Activation is structurally a one-character flip.** The dormant Stage
  1 infrastructure is complete: `capsuleSweep` plus the three
  `#if PC_CAPSULE_ENABLED` call sites in `bondwalk.c` are pre-existing,
  correctly guarded, and immediately functional once the flag flips.
  No additional API plumbing was required.
- **Test harness flow.** ACTION_JUMP injection requires the gameplay IMC
  (priority 0) to be the top of the IMC stack. CITRAINING opens with a
  brief cutscene IMC (priority 4, ~1s duration) followed by a sticky
  Main Menu / agent_select overlay (priority 10). The
  `vehicle_flow.json` dismiss pattern (ACTION_USE @ 25s,
  ACTION_CANCEL_USE @ 28s) cleanly transitions to free-roam by 32s.
  Documented inline in the test JSON for future authors.
- **Marker fire rate.** A single jump fires the sweep ~100 times (once
  per airborne tick of ascent + descent across ~1-1.5s). The `min: 2`
  floor leaves substantial headroom against tick-throttle variability.

---

## Followups (out of scope this dispatch)

1. **Stage 2 per-tri BG validate** -- new `capsuleSweepTwoStage` entry
   point with `realnormal` / `hitfromrendered` fields, ray-bundle
   integration with `bgTestHitInRoom`. Per design doc migration step 1.
2. **Swap `bwalkUpdateVertical` to Stage 2** at `bondwalk.c:1255` once
   the API lands. Design doc migration step 2; this is the actual
   wall-jump glitch fix.
3. **Skedar surface-loco real-normal consumption** -- unblocks Slice 5
   of `context/designs/in-flight/skedar-surface-normal-locomotion.md`.
   Design doc migration step 3.
4. **Migrate `bwalkTryMoveUpwards` + horizontal-step sites** to Stage 2.
   Design doc migration step 4.
5. **Wall-jump-specific assertion test** -- a sibling smoke test that
   targets a known glitch ledge and asserts `CAPSULE: sweep
   result=BLOCKED type=WALL` to prove Stage 2 catches rendered-only
   geometry. Needs a known repro position; deferred until Stage 2 lands.
6. **Pillar doc update.** `context/pillars/physics-collision.md`
   currently reads as if the capsule sweep is in active use. Update to
   reflect the Track G activation date + design-doc cross-link.
7. **Stage 1 redundancy retirement.** Once Stage 2 is the authoritative
   path, decide whether to retire the old `capsuleSweep` entry point
   (design doc Open Question 3).

---

## Hard-rule compliance

- No kanban / session-log edits.
- No sub-agents.
- Worktrees not used (main checkout).
- Build verify performed: all 4 targets PASS, no new warnings.
- Smoke verify performed: 4/4 tests PASS (3 regression + 1 new).
- Rabbit-hole protocol: not triggered. Activation was structural; no
  dependency surfaced as missing. Movement feel preserved (regression
  tests green, no `CAPSULE: sweep result=BLOCKED` or `JUMP_COLLIDED`
  markers in the post-spawn jump-cycle frames of the new test, which
  would indicate a regression in the airborne pipeline).
- PD_DEV_BUILD gating: `CAPSULE_LOG` markers are `PD_DEV_BUILD`-gated
  (compile-time `((void)0)` in stable release). `CAPSULE_SWEEP:` /
  `JUMP_COLLIDED:` / `PRE_CEIL_CLAMP:` / `CAPSULE_CEIL_CLAMP:` markers
  inside `bondwalk.c` are unconditional `sysLogPrintf` (active in all
  build flavors). `build-headless.ps1` default flavor is dev so both
  classes fire in smoke runs.

---

## Files changed

- `src/include/lib/capsule.h` -- flag flip + comment update.
- `tools/smoke-verify/tests/wall_jump_capsule_smoke.json` -- new test.

That's the complete diff. No code changes to `capsule.c`, `bondwalk.c`,
or any other source file.
