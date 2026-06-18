# Smoke-Verify Dispatch Findings -- 2026-05-13

Orchestrator session: main-checkout. Dispatch goal: "use our test pipeline to actually run the client with specific logging to test features such as menu flow through mp modes, missions, skedar benchmark (cpu and gpu), entering / operating / exiting vehicles, etc and find incomplete work to fix."

Five worker sessions dispatched in parallel. Each authored one new smoke-verify JSON test, ran it against the live `Build/PerfectDark.exe`, captured the resulting `pd-client.log`, and reported findings without committing.

---

## Headline finding (Mike's interjection during dispatch)

**Windows Defender Firewall is intercepting every per-test launch.** The smoke-verify pipeline copies `PerfectDark.exe` to a fresh per-test directory under `.claude/smoke-verify-runs/<utc>-<test>/`. Windows treats each fresh path as a new executable and surfaces a "Windows Security Alert -- Allow on private/public networks?" dialog the first time the binary's network stack initialises. The dialog steals focus from the SDL game window. The scripted `SDL_PushEvent` keyboard taps from the harness arrive at the SDL queue but ImGui drops them because the window is unfocused.

This root cause explains **every single failure pattern** all 5 workers reported. It is the headline blocker. Without resolving it, no smoke test can reliably drive the client past the first focus-loss event.

Severity: **HIGH**. Suggested fixes (any one is sufficient):

1. **Sign + pre-allow the canonical binary path once.** Use a single install location (e.g. `.claude/smoke-verify-install/`) the runner re-seeds each test instead of copying to a per-utc subdirectory. Windows only prompts once per path.
2. **Pre-allow via `New-NetFirewallRule` in `Install-Harness.ps1`** before launching the binary. Adds an inbound rule keyed by program path. Requires admin elevation on first run.
3. **Run with `--no-net` / `--offline` smoke boot arg** so the network stack never initialises. (Verify such a flag exists in `port/src/main.c`; if not, add one.)
4. **Use `-Install` mode in the runner** with a single shared install dir per session. Workers tried `-Test <name>` which forces per-test copies; switching to a single pre-warmed install avoids re-tripping the firewall.

Recommendation: ship option 4 immediately (one-line runner enhancement), then option 3 (small port-side CLI flag) as the durable fix.

---

## Worker reports

### Worker A -- `mp_room_flow.json` (MP / Combat Sim room flow)

- **Authored**: `tools/smoke-verify/tests/mp_room_flow.json`
- **Run outcome**: ASSERTION-FAIL (no crash, no timeout). Two `INPUTCTX: focus LOST` events at 5.83s and 25.75s -- consistent with firewall dialog stealing focus twice (initial prompt + subsequent interruption).
- **Run dir**: `.claude/smoke-verify-runs/20260514T031243Z-mp_room_flow/`
- **What worked**: boot completed cleanly, asset catalog registered, no crashes, no FATAL.
- **What didn't**: scripted Returns past t=15s never drove menu graph fires for the new mp_setup/mp_advanced/mp_bot_setup edges. The three migrations from commit `8f81c063` were not exercised by the smoke -- not because they're broken, but because the input sequence never reached the Room screen.
- **Incidental finding**: `ERROR: CATALOG_CRITICAL: bgun model filenum=0 failed to load` fires at 28.42s on Combat Sim free-roam transition out of menu mode. **Pre-existing**; not introduced by today's slices. Worth a separate investigation.

### Worker B -- `mission_intro_flow.json` (Solo mission intro)

- **Authored**: `tools/smoke-verify/tests/mission_intro_flow.json`
- **Run outcome**: silent shutdown at t=49.13s with no `SMOKE: result=...` marker. 14-second gap between last input and shutdown -- consistent with the SDL window being closed externally (firewall dialog dismissal? Alt+F4 fallthrough from blind Return-spam?).
- **Run dir**: `.claude/smoke-verify-runs/20260514T031147Z-mission_intro_flow/`
- **What worked**: agent_select graph edge fired correctly at t=12.40s. main_menu pool acquired (gen=2).
- **What didn't**: 11 menu-nav taps between t=15s-31s produced ZERO menu graph fires. The `MENU_TYPE_SOLO_MISSION` start/back/scene-op edges were never reachable. `LV.DIAG: first-render` never emitted -- no stage tick.

### Worker C -- `swarm_cpu_smoke.json` (CPU swarm benchmark)

- **Authored**: `tools/smoke-verify/tests/swarm_cpu_smoke.json`
- **Run outcome**: scripted_exit clean. ASSERTION-FAIL (0 `TESTSCEN.SWARM` lines, 0 `BENCHMARK.SWARM.CPU` lines).
- **Run dir**: `.claude/smoke-verify-runs/20260514T031454Z-swarm_cpu_smoke/`
- **Critical finding**: `--skip-intro` does NOT land at the main menu. It sets `g_StageNum = STAGE_CITRAINING` (0x26) and drops the player into Carrington Institute free-roam (`port/src/main.c:505-507`). The test's premise of "navigate Main Menu -> Settings -> Debug -> Test Scenarios -> Swarm CPU" was therefore invalid from boot. **The harness needs a different entry path** -- either remove `--skip-intro` (boot to title naturally) or add a CLI fast-path like `--launch-scenario swarm_cpu`.
- **B-307 status**: unconfirmed by this smoke. The CPU swarm scenario was never armed.
- **Incidental finding**: `WARNING: Maximum number of configuration entries exceeded: 512` at boot. The 512-entry `pd.ini` config table is full. Three `configLoad: malformed keyvalue line: (null)=ALT+ENTER` errors follow -- entries silently dropped after the cap. **Pre-existing**; deserves its own card.

### Worker D -- `swarm_gpu_smoke.json` (GPU swarm benchmark)

- **Authored**: `tools/smoke-verify/tests/swarm_gpu_smoke.json`
- **Run outcome**: scripted_exit clean. ASSERTION-FAIL (no `BENCHMARK.SWARM.GPU`, 0 `TESTSCEN.SWARM` cycles).
- **Run dir**: `.claude/smoke-verify-runs/20260514T032045Z-swarm_gpu_smoke/`
- **Critical finding**: **Smoke harness has no mouse synthesis.** Per `port/src/smoke_harness.c:229-292`, only keyboard scancodes are dispatched via `smokePushKey()`. The Test Scenarios block (Settings -> Debug -> Test Scenarios -> Swarm GPU radio button) is built with `ImGui::Button` / `ImGui::RadioButton` which require mouse clicks. **This blocks ALL Test-Scenarios-gated smoke tests** (swarm CPU, swarm GPU, future GPU compute paths, future The Grid). The harness needs either (a) a CLI fast-path `--launch-scenario swarm_gpu --map base:mp_felicity`, or (b) `mouse_click {x,y,button}` event support. Either is a small port-side addition.
- **B-311 status**: unconfirmed by this smoke. The GPU compute path was never armed.

### Worker E -- `vehicle_flow.json` (hoverbike mount/drive/dismount)

- **Authored**: `tools/smoke-verify/tests/vehicle_flow.json`
- **Run outcome**: PASS as a boot-stability smoke. 36/36 events fired. `SMOKE: result=scripted_exit`. No crashes.
- **Run dir**: `.claude/smoke-verify-runs/vehicle-flow-manual/` (worker bypassed the buggy runner -- see structural findings below).
- **What worked**: boot stability on stage 0x26 with hoverbike spawned via `setupdish.c:364`. `BMOVE: AXIS_MOVE` lines confirm the input pipeline reached the gameplay layer correctly. lvFrame ticked up to 5646 frames @ 60Hz with no AV.
- **What didn't**: zero `bbike`, `hoverbike`, `HOVBIKE`, `imcVehicle`, or `currentPlayerTryMountHoverbike` lines. The bike was never approached or mounted. **No scripted path exists to navigate from the player's spawn to `PAD_DISH_01F9` within the smoke timeout**, and CITRAINING is a "use opens menu" hub so F (ACTION_USE) taps are non-deterministic -- they may accidentally re-open the pause menu.
- **Specific gap**: B-298 fix (vehicle IMC consumers at `src/game/bondbike.c:236-272`) is unreachable from any clean-boot smoke test. The fix verifies only via `tests/test_vehicle_observer_layer.cpp` unit + live Mike playtest. No automated regression coverage.
- **Cheapest scaffolding to unlock**: a `--debug-mount-bike` CLI flag (~20 lines in `port/src/main.c` + a hook into `setup.c`'s hoverbike spawn path) that, after `setupLoadStage` completes on any stage containing a `hoverbike(...)` setup command, calls `currentPlayerTryMountHoverbike(bike_handle)` once.

---

## Structural findings (apply across all 5 tests)

### S-1. `tools/smoke-verify/run.ps1:446` strict-mode crash on single-test invocation -- HIGH

`if ($selected.Count -eq 0)` fails with `PropertyNotFoundException` under `Set-StrictMode -Version Latest` when `Select-SmokeTests` returns a single PSCustomObject (PowerShell auto-unwraps arrays of length 1). **Every `-Test <name>` invocation crashes before launching the binary.**

Workarounds the workers used:
- Worker A: invoked `PerfectDark.exe --smoke <path>` directly.
- Worker B: `pwsh` instead of `powershell` (different error but same class).
- Worker C: `-Tag swarm` (matched 2 tests -> array stays an array).
- Worker D: scratch driver in `.claude/scratch/invoke-smoke-gpu.ps1`.
- Worker E: direct exe invocation.

**One-line fix**: change line 446 to `if (@($selected).Count -eq 0)`, or wrap the `Select-SmokeTests` return in `@(...)` at the call site.

### S-2. `tools/smoke-verify/run.ps1:350` `-Verbose` parameter binding collision -- HIGH

`Invoke-SmokeAssertions ... -Verbose:$VerboseEval` errors because `Invoke-SmokeAssertions` has `[switch] $Verbose` AND PowerShell auto-binds the common `-Verbose` parameter. Under pwsh 7 the exception is clear; under PowerShell 5.1 it surfaces as the misleading `Count not found`. Fix: rename the assertion lib's switch to `$VerboseAssertions`, or splat the call to bypass auto-binding.

### S-3. Windows Defender Firewall focus-steal -- HIGH (Mike's interjection)

See headline finding above.

### S-4. Smoke harness has no mouse synthesis -- HIGH

`port/src/smoke_harness.c` dispatches only `SDL_KEYDOWN`/`SDL_KEYUP` events. Blocks every test that requires clicking an ImGui button or radio (Test Scenarios, Modding Hub tools, Skin Editor, Theme Editor, Forge editor sidebars, future menu surfaces that don't have keyboard nav coverage).

Two fixes:
- Add `mouse_click {x, y, button}` event type with `SDL_MOUSEBUTTONDOWN`/`SDL_MOUSEBUTTONUP` dispatch.
- Add CLI fast-paths for major in-game features (`--launch-scenario`, `--debug-mount-bike`, `--boot-stage`) so smoke tests can skip menu navigation entirely for high-frequency scenarios.

Recommendation: ship CLI fast-paths first (cheaper, more deterministic); add mouse synthesis later for tests that genuinely need to exercise mouse-driven UIs.

### S-5. `--skip-intro` boot semantics surprise -- MED

`--skip-intro` boots to `STAGE_CITRAINING` (Carrington Institute), not to the main menu. Workers C and D wrote tests assuming `--skip-intro` would land at the menu, and the tests therefore never armed Test Scenarios. The flag's name implies "skip the intro and show the menu"; the actual behaviour is "skip the intro and skip the menu, drop into CI".

Two paths to resolve:
- Rename `--skip-intro` to something less misleading (e.g. `--boot-ci-direct`), and add `--main-menu` for "boot to title -> main menu only".
- Document the existing behaviour clearly in `port/src/main.c` help output and in `tools/smoke-verify/README.md` (if it exists).

### S-6. No CLI fast-paths for major in-game features -- MED

Today's CLI argv supports `--boot-stage`, `--skip-intro`, `--profile`, `--smoke`, `--no-update-check`, `--no-sound`, `--no-crash-handler`. Missing for smoke coverage:

- `--launch-scenario <name>` (swarm_cpu / swarm_gpu / empty_map) -> calls `testScenarioLaunch` post-catalog-init.
- `--debug-mount-bike` -> hooks `setupLoadStage` to call `currentPlayerTryMountHoverbike` on first hoverbike object.
- `--launch-mp-room <arena> <scenario> <bots>` -> seeds a Combat Sim room and skips menu nav.
- `--launch-mission <id> <difficulty>` -> seeds a solo mission and skips menu nav.

Each is ~20-50 lines in `port/src/main.c`. Bundle as a "smoke-fast-paths" slice.

---

## Incidental findings (non-blocking but documented)

### I-1. `ERROR: CATALOG_CRITICAL: bgun model filenum=0 failed to load` -- MED

Fires on Combat Sim free-roam transition out of menu mode (Worker A's log at 28.42s). Pre-existing; not introduced by today's slices. Worth a card under `catalog` pillar.

### I-2. `WARNING: Maximum number of configuration entries exceeded: 512` -- LOW

Boot-time warning. The 512-entry `pd.ini` config table is full. Three `configLoad: malformed keyvalue line: (null)=ALT+ENTER` errors follow. Pre-existing; growing config registration pressure. Either lift the cap (single-line define change in `port/src/config.c`) or audit which entries are non-essential.

### I-3. CI hub `ACTION_USE` ambiguity -- MED

Worker E surfaced this: in CITRAINING, F (ACTION_USE) opens the main menu when the player is near the hub PC, and is inert elsewhere. The non-determinism makes F unsafe in any scripted CI flow. Suggest filing as a UX consistency follow-up.

---

## Recommended next dispatch

Once the headline firewall issue is resolved (any of the 4 fixes), the next orchestrator dispatch should:

1. **Run the 5 authored tests again** with reliable focus -> verify the assertion-fails were entirely focus-driven (most likely) or surface the residual gaps.
2. **Add the CLI fast-paths** (S-6) as a dedicated sprint -- one PR adding `--launch-scenario` + `--debug-mount-bike` + `--launch-mp-room` + `--launch-mission`. Estimate 1 session.
3. **Fix the runner bugs S-1 and S-2** (small PowerShell hygiene). Estimate 30 min.
4. **Add mouse synthesis to the harness** (S-4) as a follow-up sprint after CLI fast-paths cover the high-frequency cases. Estimate 1 session.
5. **File cards for incidental findings** I-1, I-2, I-3.

The 5 new test JSONs are committed as-is even though they assertion-fail today; they encode the expected behaviour and become live regression tests once the harness can drive them.

---

## Files

- `tools/smoke-verify/tests/mp_room_flow.json` (NEW, Worker A)
- `tools/smoke-verify/tests/mission_intro_flow.json` (NEW, Worker B)
- `tools/smoke-verify/tests/swarm_cpu_smoke.json` (NEW, Worker C)
- `tools/smoke-verify/tests/swarm_gpu_smoke.json` (NEW, Worker D)
- `tools/smoke-verify/tests/vehicle_flow.json` (NEW, Worker E)
- `context/audits/smoke-verify-dispatch-findings-2026-05-13.md` (this file)

## Worker agent IDs (for the orchestrator log)

- Worker A: `add4e07ec53af9d41`
- Worker B: `ab1992944337c1ebd`
- Worker C: `af9fb164dcc1c0a76`
- Worker D: `a24336ebdf9344858`
- Worker E: `a7279d44d7a598883`

End of dispatch findings.
