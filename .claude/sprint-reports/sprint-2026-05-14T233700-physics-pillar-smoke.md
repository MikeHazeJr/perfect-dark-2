# Sprint Report - c115 Physics-Collision Pillar Smoke (--debug-spawn-at baseline)

- Date: 2026-05-14 (UTC)
- Branch: dev
- Worktree: none (main checkout, worktrees disabled)
- Commit: `a12327a7` Tests - c115: physics_capsule_basic_smoke (physics-collision pillar)
- Prereq commit: `fc9645aa` Tests - c115: smoke harness extensions (fixtures + debug-spawn-at)

## Summary

Authored a NEW in-client smoke test that exercises the new
`--debug-spawn-at x,y,z,room` one-shot latch added in fc9645a, proves
it parses + consumes correctly on STAGE_CITRAINING, and proves the
capsule sweep + chr tick pipeline keeps running for >=30 ticks after
the teleport without crashing or emitting collision-error log channels.

This is the harness-flag baseline test before the physics-collision
pillar builds richer scenarios (e.g. wall_jump_capsule_smoke) on top
of the spawn-at primitive.

PASS 19/19 assertions on dev @9d22eb59, no C/C++ source touched.

## Deliverable

File: `tools/smoke-verify/tests/physics_capsule_basic_smoke.json`

67 lines, declarative JSON. Tags:
`[physics, collision, smoke, pillar:physics-collision]`.

Boot args:
`--no-update-check --no-sound --no-net --skip-intro --debug-spawn-at 637.0,360.0,923.0,16`

Input sequence (pure waits + scripted exit):
- 0 ms wait (boot path arms `g_BootSpawnAt*` latches before mainProc)
- 15000 ms wait (settle past setupCreateProps; deferred tick fires by ~6s)
- 30000 ms wait (>=30 ticks of player tick post-teleport)
- 45000 ms wait (4+ BMOVE periodic samples)
- 55000 ms scripted exit

## (x,y,z,room) tuning notes

Used `(637.0, 360.0, 923.0, room=16)` lifted from
`.claude/smoke-verify-runs/20260514T031147Z-mission_intro_flow/pd-client.log`
line 992:

```
SPAWN: pre-ground pos=(637.0,360.0,923.0) room=16 angle=1.570
```

This is the canonical Combat Sim Training player spawn point on
STAGE_CITRAINING. Teleporting to it with force=true via
`chrMoveToPos` produces `result=OK`. No additional tuning needed -
first try.

## Run outcome (PASS)

```
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/smoke-verify/run.ps1 -Test physics_capsule_basic_smoke -VerboseAssertions
```

```
log:  .claude/smoke-verify-install/pd-client.log
exit: 0
elapsed: 55.5s

  ok forbidden absent: EXCEPTION_ACCESS_VIOLATION
  ok forbidden absent: FATAL:
  ok forbidden absent: SMOKE: result=timeout
  ok forbidden absent: LOUDFAIL\.LOAD: .*unrecoverable
  ok forbidden absent: BOOT: --debug-spawn-at consumed: .* result=FAILED
  ok forbidden absent: BOOT: --debug-spawn-at expects 4 comma-separated tokens

  ok required matched: SMOKE: scenario=physics_capsule_basic_smoke
  ok required matched: Asset Catalog: \d+ entries registered
  ok required matched: BOOT: --no-net set; netInit\(\) skipped
  ok required matched: BOOT: --debug-spawn-at armed: pos=\(637\.\d+,360\.\d+,923\.\d+\) room=16
  ok required matched: LOAD: lv\.c entering stage load sequence for stagenum=0x26
  ok required matched: LOAD: calling setupCreateProps stagenum=0x26
  ok required matched: SETUP: setupLoadStage complete
  ok required matched: TICK: lvTick enter tick=\d+ stagenum=0x26
  ok required matched: BOOT: --debug-spawn-at consumed: prop=(?:0x)?[0-9a-fA-F]+ stagenum=0x26 result=OK pos=\(637\.\d+,360\.\d+,923\.\d+\) room=16
  ok required matched: BMOVE: allowc1x=\d+ allowc1y=\d+ allowc1buttons=\d+
  ok required matched: SMOKE: result=scripted_exit

  ok count in range [30..unbounded]: LOG\.WPN\.DIAG: bgunTickGameplay enter player=0 frame= -> 87
  ok count in range [2..unbounded]: BMOVE: allowc1x=\d+ allowc1y=\d+ allowc1buttons=\d+ -> 41

PASS 19/19 assertions
```

A second confirmation run (without -VerboseAssertions) also PASS 19/19.

## Iteration notes

Two minor regex adjustments after the first run:

1. `prop=0x[0-9a-fA-F]+` -> `prop=(?:0x)?[0-9a-fA-F]+`. The `%p`
   format on this MinGW/MSVC build emits raw hex without `0x`
   prefix (e.g. `prop=00000230adb1cc80`). The optional `0x`
   matcher handles both conventions.

2. `BMOVE: allowc1` count min lowered from 4 to 2. Initial design
   assumed BMOVE fires every 120 frames @ 60fps == every 2s ==
   ~25 in 55s. Reality: the agent_select dialog is open by default
   on CITRAINING boot which pauses `lvframenum` increments (via
   `pausemode`/`lvIsPaused`), throttling BMOVE log fires to as few
   as 3 per run. The `bgunTickGameplay enter player=0` count >=30
   assertion carries the heavier "tick alive for 30+ frames"
   coverage; BMOVE >=2 is a supplemental "tick did not deadlock"
   sanity check. Saw count=41 on the stable runs and count=3 on
   one short run - min=2 absorbs that variance without losing the
   regression signal.

## Pillar coverage notes

The test does NOT assert wall-jump-specific or capsule-sweep-specific
log markers. `src/lib/capsule.c` has zero `sysLogPrintf` call sites
today, so direct "capsule sweep fired" assertions are not possible
without first instrumenting capsule.c. Coverage is via the
chain-of-evidence approach:

- `BOOT: --debug-spawn-at consumed: result=OK` proves `chrMoveToPos`
  ran on `players[0]->prop->chr` without aborting at force=true
  guard.
- `LOG.WPN.DIAG: bgunTickGameplay enter player=0 frame=` >=30 proves
  the player tick (which calls `playerTickBondMovement` which calls
  `bwalkUpdate` which uses the capsule sweep on every frame) survived
  the teleport for at least 30 ticks.
- Absence of EXCEPTION_ACCESS_VIOLATION proves no AV happened during
  those ticks.
- BMOVE periodic samples cross-validate the bondmove tick is
  actually advancing.

A future wall_jump_capsule_smoke sibling test should:

1. Teleport to a coordinate next to a known-wall in CITRAINING
   (different (x,y,z,room) tuple).
2. Inject ACTION_JUMP + ACTION_AXIS_MOVE_Y at known timings to
   produce a wall-jump trajectory.
3. Assert the resulting position via a logged player.pos sample.

That depends on adding instrumentation hooks to log player.pos at
gameplay-tick boundaries; out of scope for this baseline test.

## Files touched

- `tools/smoke-verify/tests/physics_capsule_basic_smoke.json` (new, 67 lines)

No C, C++, or other test files modified.

## Hard rule compliance

- Worktree disabled (main checkout): YES
- No kanban / session-log edits: YES
- No sub-agents: YES (single session)
- No C / C++ source edits: YES (harness-only)
- Concise return report under 400 words: YES (separate message)
