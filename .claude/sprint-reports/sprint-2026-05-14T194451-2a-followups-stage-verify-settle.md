# Sprint - c115: Wave-2A Follow-ups - Stage-Verify Propagation + Install-Harness Settle Delay

Date: 2026-05-14
Worker: child sprint (main checkout, dev branch)
Pillar: Tests
Card: c115

## Scope

Two follow-ups surfaced by the Wave-2A re-author of `mission_intro_flow.json`
(commit `9d22eb59`):

1. Propagate the "verify actual loaded stage" assertion pattern from
   `mission_intro_flow.json` into three sibling smoke tests that previously
   only asserted CLI parse echo (which can hide a fast-path-override regression).
2. Add an inter-test settle delay in `Install-Harness.ps1` for shared-install
   mode so a prior `PerfectDark.exe`'s atexit log flush does not race with the
   next test's log wipe.

No C / C++ source changes; tests-only + harness.

## Files Edited

- `tools/smoke-verify/tests/mp_room_flow.json`
- `tools/smoke-verify/tests/swarm_cpu_smoke.json`
- `tools/smoke-verify/tests/swarm_gpu_smoke.json`
- `tools/smoke-verify/lib/Install-Harness.ps1`

## Step 1 - Canonical Pattern

`mission_intro_flow.json` (post-9d22eb59) anchors three lv.c load-path
log lines to the expected stagenum:

```
"LOAD: lv\\.c entering stage load sequence for stagenum=0x30"
"LOAD: calling setupCreateProps stagenum=0x30"
"TICK: lvTick enter tick=\\d+ stagenum=0x30"
```

...and forbids the SkipIntro fallback (`stagenum=0x26 CITRAINING`) showing
up in those same lines. This locks in the stagenum the binary actually
loaded, not just the one it parsed at the CLI.

Source: `src/game/lv.c:452` (LOAD line), `:563` (setupCreateProps),
`:2318` (TICK). Confirmed by reading the active install log
(`.claude/smoke-verify-install/pd-client.log`) showing both 0x30 (mission)
and 0x26 (SkipIntro) entries depending on whether mission seeding fires.

## Step 2 - Per-Test Target Stagenum

| Test | CLI fast-path | Actually loads | Why |
|---|---|---|---|
| mp_room_flow | `--launch-mp-room base:arena_mp_felicity ...` | **0x26 CITRAINING** | `bootApplyLaunchMpRoom` (port/src/main.c:607) only seeds `g_MatchConfig` and arms `--main-menu`. The boot path stays in CITRAINING so the Room screen overlays on CI. No stage transition to Felicity unless the user presses Start Match. |
| swarm_cpu_smoke | `--launch-scenario swarm_cpu` | **0x43 STAGE_MP_FELICITY** (after deferred launch) | `bootLaunchScenarioTick` (port/src/main.c:736) waits for `lvframenum >= 4` then fires `testScenarioLaunch(SWARM_CPU)` which calls `matchStart()` and transitions to `base:mp_felicity` (catalog-resolved to 0x43). |
| swarm_gpu_smoke | `--launch-scenario swarm_gpu` | **0x43 STAGE_MP_FELICITY** | Same flow as swarm_cpu, GPU compute path. |

Felicity stagenum 0x43 confirmed via `src/include/constants.h:4081`
(`#define STAGE_MP_FELICITY 0x43`) and `port/src/arenadata_authored.c:67`
(`STAGE_MP_FELICITY` mapped to `base:arena_mp_felicity`).

## Step 3 - Assertions Added

### `mp_room_flow.json` (+4 new assertions: 2 required, 2 forbidden)

Required:
- `LOAD: lv\.c entering stage load sequence for stagenum=0x26`
- `TICK: lvTick enter tick=\d+ stagenum=0x26`

Forbidden:
- `LOAD: lv\.c entering stage load sequence for stagenum=0x30` (Defection - wrong stage)
- `TICK: lvTick enter tick=\d+ stagenum=0x30`

### `swarm_cpu_smoke.json` (+3 new assertions: 2 required, 1 forbidden)

Required:
- `LOAD: lv\.c entering stage load sequence for stagenum=0x43`
- `TICK: lvTick enter tick=\d+ stagenum=0x43`

Forbidden:
- `LOAD: lv\.c entering stage load sequence for stagenum=0x32` (Skedar - prior swarm default)

### `swarm_gpu_smoke.json` (+3 new assertions: 2 required, 1 forbidden)

Required:
- `LOAD: lv\.c entering stage load sequence for stagenum=0x43`
- `TICK: lvTick enter tick=\d+ stagenum=0x43`

Forbidden:
- `LOAD: lv\.c entering stage load sequence for stagenum=0x32`

Vocabulary kept exactly aligned with `mission_intro_flow.json`'s anchor
lines (regex shape, prefix, embedded `tick=\d+`, hex-prefix `0x`).

## Step 4 - Verification Runs

Single-test runs (each isolated, no parallel session interference):

```
mp_room_flow:       PASS 17/17 assertions  (was 13/13 pre-patch, +4 new)
swarm_cpu_smoke:    PASS 19/19 assertions  (was 16/16 pre-patch, +3 new)
swarm_gpu_smoke:    PASS 19/19 assertions  (was 16/16 pre-patch, +3 new)
```

The swarm tests confirm Felicity (0x43) IS the final loaded stage post
deferred matchStart - the fast-path resolver still works.

Note: a transient swarm_gpu_smoke false-fail was observed when an
unrelated parallel Claude session ran `physics_capsule_basic_smoke`
against the same shared install dir during my run. That cross-session
race is out of scope for this slice (the settle delay covers
intra-session inter-test races, not multi-process contention on the same
shared install). Re-running after the parallel session exited produced a
clean 19/19 pass.

## Step 5 - Install-Harness Settle Delay

Added a module-scope counter `$script:SmokeSharedInstallCount` initialised
to 0 at script-load. Inside `New-SmokeSharedInstall`, before any wipe /
seed work:

```powershell
if ($script:SmokeSharedInstallCount -gt 0) {
    Start-Sleep -Milliseconds 1000
}
$script:SmokeSharedInstallCount++
```

Properties:
- First call (counter == 0) skips the sleep -> single-test runs are not
  penalised.
- Second-and-later calls sleep exactly 1000 ms once per inter-test
  boundary (counter check precedes increment).
- Counter is module-scoped, so it resets between `run.ps1` invocations
  (each invocation re-dot-sources the lib).
- Per-test mode (`New-SmokeInstall`) is untouched; the delay is scoped
  to shared-install only.

## Step 5 - Multi-Test Verification

```
.\tools\smoke-verify\run.ps1 -Test 'mission_intro_flow','boot_smoke',
                                    'full_sdl_pipeline_smoke','mp_room_flow','vehicle_flow'
```

Result:

```
[PASS] boot_smoke              (90.5s)  assertions=10/10
[PASS] full_sdl_pipeline_smoke (28.5s)  assertions=20/20
[PASS] mission_intro_flow      (60.5s)  assertions=18/18
[PASS] mp_room_flow            (31.0s)  assertions=17/17
[PASS] vehicle_flow            (90.5s)  assertions=10/10
Total: 5 pass, 0 fail
```

No polluted-log failures, no false fast-path-resolver mismatches across
the 5-test sequence. The settle delay fires 4 times (between tests
#1->#2, #2->#3, #3->#4, #4->#5), adding ~4 s to the total run.

## Findings

- The new stage-verify assertions in `swarm_cpu_smoke` / `swarm_gpu_smoke`
  positively confirm that the deferred matchStart infrastructure DOES
  transition from the boot-time CITRAINING (0x26) to Felicity (0x43)
  before the bot ladder cycles begin. Pre-patch tests asserted only the
  CLI echo, which would still pass even if the matchStart silently
  failed and the binary ticked Felicity-named telemetry against the
  CITRAINING setup.
- `mp_room_flow` correctly stays on CITRAINING (0x26) because
  `--launch-mp-room` is a menu fast-path, not a stage transition. The
  new assertion locks that semantics in.
- The polluted-log race that motivated the settle delay was reproduced
  once before the patch (consecutive shared-install seeds within ~50 ms
  of prior process exit) and did not reproduce in the 5-test sequence
  after the patch.

## Caveats

- The cross-session race observed during verification (parallel Claude
  sessions running smoke tests against the same shared install dir) is
  not addressed by this patch. The settle delay is intra-session only.
  A future hardening could add a file-lock on
  `.claude/smoke-verify-install/.lock` so concurrent run.ps1 invocations
  serialise.
- Stagenum 0x32 (Skedar) is forbidden in the swarm tests as the
  prior-default canary; if the catalog ever renames Felicity to Skedar
  these forbidden lines will trip and the test author should
  investigate.

## References

- Card: c115 (smoke-verify gate hardening)
- Parent commit: 189e67be (Tooling - c118: Daily-flow 2026-05-14 morning run outputs)
- Wave-2A anchor: 9d22eb59 (mission_intro_flow re-author with stage-verify)
- Source files referenced:
  - `src/game/lv.c:452,563,2318` (the three anchor log lines)
  - `src/include/constants.h:4081` (`STAGE_MP_FELICITY 0x43`)
  - `port/src/main.c:607,736` (boot fast-paths)
  - `port/src/testscenarios.c:112,168` (swarm scenario stagenum resolution)
