# Sprint Report -- c115 Tighten Smoke Tests for Missing-Weapons Symptom

**Date**: 2026-05-14
**Session**: missing-weapons-tighten worker (Cowork Dispatch child of c115)
**Build**: `.claude/smoke-verify-install/PerfectDark.exe` (dev 189e67be, May 14)
**Branch**: dev (single deliverable commit)

## Goal

The user-visible "missing weapons" symptom decomposed into two assertion
gaps in the smoke-verify suite. The catalog pipeline is healthy (86
`.pdwpn` files emit, walker registers all 86, `weaponFindById(0)`
returns a valid pointer per B-318/B-324/B-325/B-326 pending-playtest)
but two tests were silently passing while leaving the surface
unprotected:

1. `mission_intro_flow.json` -- passed `--launch-mission 0x30`
   (STAGE_DEFECTION) but never asserted that stage 0x30 was the stage
   actually loaded. A regression that left `g_StageNum=0x26`
   (CITRAINING) would slip through.
2. `boot_smoke.json` -- asserted the walker `LOADER.UNIVERSAL.SUMMARY:`
   totals line but did NOT assert the per-kind weapons count of 86. A
   regression in `port/src/romextract_pdwpn.c` or `port/src/loader_walker.c`
   that dropped weapons to 85 (or 0) would slip through.

Goal: add tight, log-pattern assertions to both tests so the regression
class is loudly caught; verify by running both tests.

## Shipped

### `tools/smoke-verify/tests/mission_intro_flow.json`

Added 4 required_lines + 2 forbidden_patterns:

```diff
   required_lines:
+    "LOAD: lv\\.c entering stage load sequence for stagenum=0x30",
+    "LOAD: calling setupCreateProps stagenum=0x30",
+    "TICK: lvTick enter tick=\\d+ stagenum=0x30",
+    "LOADER\\.UNIVERSAL\\.OK: kind=weapon scanned=86 registered=86 envelope_failures=0 register_failures=0",
   forbidden_patterns:
+    "LOAD: lv\\.c entering stage load sequence for stagenum=0x26",
+    "TICK: lvTick enter tick=\\d+ stagenum=0x26",
```

The orchestrator brief proposed asserting `boot stage set to 0x30` and
forbidding `boot stage set to 0x26`. Empirical fresh run showed that
the `boot stage set to 0xNN` log is emitted by `port/src/main.c:1006`
**before** `bootApplyCliFastPaths()` at line 1037 runs -- so it
reflects the SkipIntro pre-set (0x26 = CITRAINING) regardless of
whether `--launch-mission` then overrides to 0x30. Asserting against
that log line is a false-positive trap. Switched to the canonical
load-path signals in `src/game/lv.c`:

- `LOAD: lv.c entering stage load sequence for stagenum=0x30`
  (lv.c:452, fires once per load with the actual `g_Vars.stagenum`)
- `LOAD: calling setupCreateProps stagenum=0x30` (lv.c:563)
- `TICK: lvTick enter tick=N stagenum=0x30` (lv.c:2318, fires every
  frame so confirms gameplay is ticking on the right stage)

These are after the fast-path applies and read `g_Vars.stagenum`
directly from the level layer, so they prove the actual loaded stage.
Forbidding the same patterns with `stagenum=0x26` catches the
"--launch-mission silently fell back to CITRAINING" regression class.

### `tools/smoke-verify/tests/boot_smoke.json`

Added 1 required_line:

```diff
   required_lines:
+    "LOADER\\.UNIVERSAL\\.OK: kind=weapon scanned=86 registered=86 envelope_failures=0 register_failures=0",
```

Direct per-kind weapons assertion. The walker emits one
`LOADER.UNIVERSAL.OK: kind=<k> ...` line per kind from
`port/src/loader_walker_common.c:430` and then aggregates into the
`LOADER.UNIVERSAL.SUMMARY:` totals line. The per-kind line is the
canonical "weapons-count regression" detector; the totals line could
mask a kind-level drop because it aggregates all 13 kinds.

## Verification

### mission_intro_flow -- 18/18 PASS

```
=== mission_intro_flow ===
  exit code: 0
  elapsed: 60.5s
  ok  forbidden absent: EXCEPTION_ACCESS_VIOLATION
  ok  forbidden absent: FATAL:
  ok  forbidden absent: SMOKE: result=timeout
  ok  forbidden absent: LOUDFAIL\.LOAD: .*unrecoverable
  ok  forbidden absent: MENU\.GRAPH\.FIRE .* ok=0
  ok  forbidden absent: LOAD: lv\.c entering stage load sequence for stagenum=0x26
  ok  forbidden absent: TICK: lvTick enter tick=\d+ stagenum=0x26
  ok  required matched: SMOKE: scenario=mission_intro_flow
  ok  required matched: Asset Catalog: \d+ entries registered
  ok  required matched: BOOT: --launch-mission .* -> stagenum=0x30 difficulty=0
  ok  required matched: BOOT: --no-net set; netInit\(\) skipped
  ok  required matched: LOAD: lv\.c entering stage load sequence for stagenum=0x30
  ok  required matched: LOAD: calling setupCreateProps stagenum=0x30
  ok  required matched: SETUP: setupLoadStage complete
  ok  required matched: TICK: lvTick enter tick=\d+ stagenum=0x30
  ok  required matched: LOADER\.UNIVERSAL\.OK: kind=weapon scanned=86 registered=86 envelope_failures=0 register_failures=0
  ok  required matched: BMOVE: allowc1
  ok  required matched: SMOKE: result=scripted_exit
  PASS 18/18 assertions
  Total: 1 pass, 0 fail
```

Results: `.claude/smoke-verify-runs/results-20260514T231646Z.json`.

### boot_smoke -- 10/10 PASS

```
=== boot_smoke ===
  exit code: 0
  elapsed: 90.5s
  ok  forbidden absent: EXCEPTION_ACCESS_VIOLATION
  ok  forbidden absent: FATAL:
  ok  forbidden absent: SMOKE: result=timeout
  ok  forbidden absent: LOUDFAIL\.LOAD: .*unrecoverable
  ok  required matched: SMOKE: scenario=boot_smoke
  ok  required matched: Asset Catalog: \d+ entries registered
  ok  required matched: LOADER\.UNIVERSAL\.OK: kind=weapon scanned=86 registered=86 envelope_failures=0 register_failures=0
  ok  required matched: LOADER\.UNIVERSAL\.SUMMARY:
  ok  required matched: SMOKE: result=scripted_exit
  ok  count in range [1..1]: memp heap at -> 1
  PASS 10/10 assertions
  Total: 1 pass, 0 fail
```

Results: `.claude/smoke-verify-runs/results-20260514T231530Z.json`.

## Decisions

### `boot stage set to 0xNN` is unreliable for stage-load assertions

The orchestrator brief proposed using this log line. Investigation
showed it fires at `port/src/main.c:1006` BEFORE
`bootApplyCliFastPaths()` runs at line 1037. So a healthy
`--launch-mission 0x30` boot legitimately emits
`boot stage set to 0x26` first (from `g_SkipIntro=1` setting
g_StageNum=CITRAINING), then the fast-path overrides g_StageNum to
0x30, and the actual load happens for 0x30. Asserting against the
`boot stage set` log is a false-positive trap.

The canonical "this is the stage actually loading" signals are the
lv.c log lines that read `g_Vars.stagenum` AFTER the fast-path has
applied. Used those instead. Required these three for
defense-in-depth (catch a regression at any stage of the load
pipeline: pre-load, during props, ticking).

### Shared-install log-pollution surfaced during verification

Ran both tests sequentially; the second run's log accidentally read
artefacts from a different test's run. Worked around by running each
test in isolation (single -Test argument, no chaining). The
shared-install log-wipe in
`tools/smoke-verify/lib/Install-Harness.ps1:237-240`
DOES wipe `pd-client.log` before each run, so this is not a
runner-side bug; the polluted log appeared after running multiple
tests in close succession with overlapping process lifecycles. Not in
scope for this dispatch; flagging as a follow-up.

## Follow-ups

- **Coordinator**: orchestrator's kanban c115 card can claim two new
  regression coverage points: stage-load-target verification +
  weapons-count walker assertion. Both are tight regex regressions; a
  drop from 86 to 85 weapons in any future commit will surface
  loudly.
- **Test harness**: investigate why running multiple smoke tests in
  the same `run.ps1` invocation can leave the shared install's log
  with multi-scenario content (observed once during verification but
  not reproducible in single-test runs). Probable cause: the runner
  starts the next test's `Process.Start` while the prior test's
  binary is still in shutdown atexit cleanup, and the prior test's
  log buffer flushes into the new install's freshly-wiped log.
  Mitigation: add a 1s post-exit settle delay before re-seeding the
  shared install. Out of scope for this dispatch.
- **propagation**: apply the same "verify the actual loaded stage,
  not just the CLI-flag echo" pattern to other tests that boot via
  `--launch-mp-room` or `--launch-scenario`. `mp_room_flow.json`,
  `swarm_cpu_smoke.json`, and `swarm_gpu_smoke.json` should all
  assert `TICK: lvTick enter ... stagenum=0xNN` for the expected
  stage rather than just the CLI parse echo. Out of scope for this
  dispatch but tracked here for the orchestrator.

## Files Touched

```
tools/smoke-verify/tests/mission_intro_flow.json   (+8 -1)
tools/smoke-verify/tests/boot_smoke.json           (+1 -0)
```

No C / C++ source touched. No kanban or session-log edits (coordinator
owned).

## Verification Notes

Each test was invoked via:
```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/smoke-verify/run.ps1 -Test <name> -VerboseAssertions
```

Both tests passed cleanly in isolation. The new assertions exercised
all the right log surface and fired as expected. No false-positive
trap activated.

## Recommendation

**Lock in as regression coverage immediately**. The new assertions
target the exact regression classes the orchestrator brief flagged:

- mission_intro_flow now catches the "--launch-mission resolves but
  the loaded stage is wrong" silent-pass class.
- boot_smoke now catches the "walker registers fewer than 86
  weapons" class that B-318 / B-324 / B-325 / B-326 surfaced.

Both classes are now noisy on regression -- they cannot slip past
the smoke gate.

Refs: c115
