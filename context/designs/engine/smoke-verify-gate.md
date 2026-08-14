# Smoke Verify Gate (Phase 1)

> Pre-merge integration gate that boots a headless client, drives it through a
> scripted scenario, and asserts on the resulting log stream. Catches the
> class of bugs that survive unit tests but ship to playtest (boot AV at title
> screen, modal-stuck menus, missing weapon pool, stage-load access violation).

Lives at:

- Harness module: `port/src/smoke_harness.{h,c}` (client target only).
- Runner script: `tools/smoke-verify/run.ps1`.
- Test definitions: `tools/smoke-verify/tests/*.json`.
- Assertion helpers: `tools/smoke-verify/lib/Test-Assertions.ps1`.

---

## Why the gate exists

The 2026-05-06 super-audit scored project Stability 35/100 and Execution
Quality 65/100. A walk back through recent playtest blockers shows the same
shape repeating: each one shipped because the build artefact had no automated
boot/exit verification.

Concrete losses if the gate had existed:

- B-324 / B-325 (boot AV at `bgunCalculateBlend`, `weapon = NULL`) would have
  been caught the moment the clean-install boot smoke ran. The walker was
  ordered before the emitters, no `.pdwpn` file existed, `loaderPoolGetWeapon`
  returned NULL for every index for the entire boot. A `boot_smoke` test with
  `forbidden_patterns: ["EXCEPTION_ACCESS_VIOLATION", "FATAL: "]` and
  `required_lines: ["LOADER.UNIVERSAL.SUMMARY: .* active=1"]` catches both
  modes immediately.
- B-318 (modal-stuck Combat Sim, gate-removed walker still returning empty)
  catches in `combat_sim_entry` once that test's nav matures.
- B-326 (Dev Window ROM placement at `<BuildDir>\data\` instead of install
  root) catches because the boot smoke fails to find the ROM and the LOUDFAIL
  log line fires before the gate trips on `forbidden_patterns`.
- B-323 (post-boot AV in `challengesInit` from N64-vs-PC mpstrings stride)
  catches because the AV fires inside `challengesInit`, well before
  `mainProc` ticks the game loop, and the gate trips on
  `EXCEPTION_ACCESS_VIOLATION` before timeout.

Cost to add: ~700 lines of harness/runner/tests + 1 line per CMake-discovered
file. Cost per merge: 60-180 seconds wall clock for the boot test alone, 5-10
minutes for the full Phase 1 matrix. The full matrix runs only when paths
touched in the merge intersect catalog/load/render/menu/input/weapon code; a
docs-only change skips entirely.

---

## Phase 1 scope (this ship)

### Capabilities

1. `--smoke <test.json>` CLI flag on the existing client binary
   (`PerfectDark.exe`). No new binary. No subprocess shim. The harness:
   - Parses the test JSON with the in-process minimal JSON tokenizer (same
     style as `modmgr.c`'s parser).
   - Applies test-declared environment (log channel mask, verbose flag, extra
     CLI flags) before main subsystems start.
   - Runs the normal `mainProc` loop with a `smokeHarnessTick` callback
     wired into the per-frame path.
   - Pushes scheduled SDL events at the JSON-declared `at_ms` offsets.
   - Times out after `timeout_seconds`; emits a `SMOKE: result=...` line so
     the runner can detect natural completion vs forced exit.
   - Exits with code 0 (clean) or 1 (forced by harness because of timeout or
     SMOKE-side assertion). Real assertions run in the PowerShell runner
     after the binary has exited.

2. Test runner `tools/smoke-verify/run.ps1`:
   - Discovers `tools/smoke-verify/tests/*.json`.
   - Filters by tag (`-Tag boot`, `-Tag catalog`), by name (`-Name <stem>`),
     or by changed-paths heuristic (`-AutoSelect`).
   - Creates a fresh install staging directory under
     `.claude/smoke-verify-runs/<timestamp>-<test_name>/`. Each run gets a
     clean copy of `PerfectDark.exe`, a known-good `pd.ini` (or none, for
     first-launch behaviour), the ROM (`pd.<romid>.z64`) at install root,
     and an empty `data/<romid>/` so the harness exercises the first-launch
     extract path.
   - Invokes `PerfectDark.exe --smoke <test.json> ...`.
   - Reads `pd-client.log` from the install dir, applies the JSON-declared
     assertions, returns pass/fail.
   - Optional `-Keep` to retain the run dir for debugging; default cleans
     successful runs to keep the disk small.
   - Optional `-Build` to chain the queued build before discovery, so the
     gate works from a freshly checked-out tree.

3. Initial test matrix (3 tests):
   - `boot_smoke.json`: no scripted input. Boot to title screen. Channel
     mask covers BOOT/CATALOG/SYSTEM. Asserts no AV, all 13 emitter kinds
     report `LOADER.UNIVERSAL.SUMMARY: ... registered=N` with N greater than
     zero, walker active flag transitions to 1.
   - `stage_load_paradox.json`: uses `--boot-stage 0x26` to jump straight
     into a stage. 30-second tick budget. Channel mask covers
     SCENARIO/LOAD/CATALOG. Asserts the stage init completed without AV at
     the B-318 / B-323 / B-324 family addresses.
   - `combat_sim_entry.json`: scripted input nav. From title screen taps
     Confirm (Return) repeatedly to advance through intro slides, navigates
     down to Combat Simulator, enters arena, attempts a weapon-equip motion,
     fires once, exits. Channel mask covers MENU/INPUT/WEAPON. Asserts no
     modal-stuck, no AV. Phase 1 tolerance: scripted timings are starting
     values; Mike will tune after the first live run. The schema is the
     deliverable in this ship; the exact frame budgets are tuning input.

4. Build-gate integration: `run.ps1 -AutoSelect` examines `git diff` against
   the merge base and runs only the tests whose `paths_of_interest` glob
   matches a changed path. This is wired into the auto-merge step in
   `devtools/build-headless.ps1` as a non-blocking advisory pass for the
   first week, then promoted to blocking once Mike has confirmed the noise
   floor.

### Out of scope (Phase 2)

These appear in the schema but the harness does not implement them yet:

- Online init smoke (lobby join, listen-host smoke).
- Mod load smoke (.pdmod scan + activation).
- Skin editor smoke.
- Forge level-editor smoke.
- Screenshot capture verb (would require GL readback during the harness
  tick).
- Named verbs (Confirm, Back, NextTab) mapped through the actionmap rather
  than raw SDL scancodes. The schema accepts `"key": "Return"` strings
  already, but the table is small in Phase 1; expanding it is mechanical.

The Phase 2 expansion plan lives in the closing section of this doc.

---

## Schema

```jsonc
{
  // Free-form name. Filename stem doubles as the canonical id.
  "scenario_name": "boot_smoke",

  // Single-paragraph human description. Shown in the runner output.
  "description": "Boot to title screen, verify catalog summary, no AV.",

  // Tag list. Selective runs (-Tag) and path-based auto-select use these.
  "tags": ["boot", "catalog", "stability"],

  // Glob patterns that, if intersected with changed paths in a merge,
  // make this test eligible under -AutoSelect. Empty = always eligible.
  "paths_of_interest": [
    "port/src/main.c",
    "port/src/romextract*.c",
    "port/src/loader_*.c",
    "port/src/assetcatalog*.c",
    "port/src/catalog_*.c",
    "port/src/loader_walker.c",
    "src/game/bondgun*.c",
    "src/game/invitems.c"
  ],

  // Bitmask string (hex or decimal) or "all" / "none". Maps to
  // sysLogSetChannelMask. Default: "all".
  "log_channel_mask": "all",

  // 0 or 1. Sets LOG_VERBOSE via sysLogSetVerbose. Default: 0.
  "verbose": 0,

  // Max wall-clock budget. Harness force-exits on timeout, runner marks
  // the test failed unless the assertions explicitly accept timeout.
  "timeout_seconds": 90,

  // Extra CLI flags passed to PerfectDark.exe. The runner always passes
  // --smoke <path> --no-update-check --no-crash-handler --no-sound on top
  // of these. Use this for --boot-stage, --profile, --skip-intro, etc.
  "boot_args": ["--skip-intro"],

  // Optional. Repo-relative source files copied into the install dir
  // before the binary launches. Each entry shape:
  //   { "src": "<repo-relative path>", "dst": "<install-relative path>" }
  // Used by mod_load_smoke (pre-stage a `.pdmod` under mods/) and
  // save_roundtrip_smoke (pre-stage a v1 agent file under
  // data/<romid>/saves/). Parent dirs are created. Missing src emits a
  // warning and continues; the binary launch decides whether the missing
  // fixture is fatal. Fixtures live under tools/smoke-verify/fixtures/.
  "fixtures": [
    { "src": "tools/smoke-verify/fixtures/test_skin_redmund.pdmod",
      "dst": "mods/test_skin_redmund.pdmod" }
  ],

  // Scripted event stream. Times are wall-clock milliseconds since
  // smokeHarnessInit returned. Events fire in JSON order; the harness
  // does NOT reorder by at_ms (intentional, so authoring stays linear).
  "input_sequence": [
    { "at_ms": 0,     "type": "wait",   "comment": "let boot complete" },
    { "at_ms": 8000,  "type": "key",    "key": "Return", "action": "tap" },
    { "at_ms": 9000,  "type": "key",    "scancode": 81,  "action": "tap" },
    { "at_ms": 12000, "type": "exit",   "comment": "clean shutdown marker" }
  ],

  // Assertions evaluated by the runner after the binary exits.
  "assertions": {
    // Each regex must match at least once in the log. Anchored with ^/$
    // is allowed and recommended.
    "required_lines": [
      "Asset Catalog: \\d+ entries registered",
      "LOADER\\.UNIVERSAL\\.SUMMARY:"
    ],

    // No log line may match any of these regexes. Use this for AV / FATAL.
    "forbidden_patterns": [
      "EXCEPTION_ACCESS_VIOLATION",
      "FATAL: ",
      "LOUDFAIL\\.LOAD: .* unrecoverable",
      "SMOKE: result=timeout"
    ],

    // Pattern must match at least min times, at most max times. Use to
    // assert positive event counts (weapon equips, stage transitions).
    // max < 0 means unbounded.
    "required_counts": [
      { "pattern": "memp heap at",        "min": 1, "max": 1 },
      { "pattern": "BOOT_PHASE_WALKER",   "min": 1, "max": -1 }
    ]
  }
}
```

### Boot-time CLI fast paths

The harness lives next to a small family of CLI flags parsed by
`bootApplyCliFastPaths` in `port/src/main.c`. Tests reach for these via
`boot_args` when they need deterministic state before the scripted
input stream takes over:

- `--no-net` -- skip `netInit()` (closes the Windows Firewall dialog).
- `--main-menu` -- skip intro, drop straight into the main menu.
- `--launch-scenario <n>` -- arm a test-scenario launch, dispatched on
  the first frame where the player prop exists.
- `--launch-mission <stage_id> [--difficulty agent|special|perfect]` --
  seed `g_MissionConfig` and boot into the resolved stagenum.
- `--launch-mp-room <arena_id> <scenario_id> <bot_count>` -- seed
  `g_MatchConfig` and boot into Combat Sim with the Room screen ready.
- `--debug-mount-bike` -- post-setupCreateProps hook: mount player 0
  on the first OBJTYPE_HOVERBIKE prop after stage load.
- `--debug-spawn-at x,y,z,room` -- post-setupCreateProps hook: teleport
  player 0 to the given world coord + room id on the first frame where
  `g_Vars.lvframenum >= 4` and `g_Vars.players[0]->prop->chr` exists.
  Four comma-separated tokens (x,y,z parsed as floats; room as s16
  RoomNum). Calls `chrMoveToPos(chr, &target, [room,-1], 0.0f, true)`
  -- the canonical AI-script teleport path, force flag on so
  bg-collision near the target does not refuse the move. One-shot per
  boot. Logs `BOOT: --debug-spawn-at consumed: prop=<addr>
  stagenum=<hex> result=<OK|FAILED> pos=<x,y,z> room=<id>`. Used by
  future `wall_jump_capsule_smoke` and any physics-collision regression
  that needs deterministic positioning.

### Key event types

`type` field on each entry in `input_sequence`:

- `wait`: no-op marker, used to keep authoring linear or to anchor a
  comment. Harness consumes the event and moves on.
- `wait_until`: bounded typed production-state barrier. It pauses only the
  current process's virtual script time while the real watchdog continues.
  `condition` is one exact readiness predicate and `timeout_ms` is mandatory;
  expiration wins at the exact boundary. Optional `stable_ms` requires one
  continuous true window. The complete `assist_action`,
  `assist_condition`, and `assist_hold_ms` tuple may press one production
  action while the target is false, never re-arms it, and releases ownership on
  hold expiry and every terminal path. Assisted waits require
  `stable_ms > 0`; one-frame OR predicates are not transition proof. The
  terminal receipt exposes `stable_elapsed_ms`, and fixture assertions must
  prove it met the configured lower bound. Player, cutscene, and control facts
  resolve through `playermgrGetLocalPlayerNum()` and fail closed when committed
  receiver-local identity is missing or ambiguous. Runtime slot zero and a
  peer's stable wire ID are never substitutes for receiver-local identity.
  Multi-process host fixtures use `network_listen_ready` before a
  peer-dependent stage barrier, so cold startup cannot consume the deadline
  while the runner is correctly withholding the dependent client.
- `key`: keyboard event. Either `"scancode": <int>` or `"key": "<name>"`.
  `action` is `"press"` (KEYDOWN only), `"release"` (KEYUP only), or
  `"tap"` (KEYDOWN then a KEYUP 1 frame later). Tap is the most common.
- `action`: unconditional semantic action-map press, release, or tap. The
  harness owns only successfully injected state and explicit or assisted
  releases are mandatory; readiness-gated one-shot actions are represented by
  the stateful `wait_until` tuple above.
- `exit`: end the test cleanly. Harness emits `SMOKE: result=ok` and
  calls `exit(0)`. Use this when the scripted scenario has reached its
  goal before the timeout.

Phase 2 adds: `mouse_move`, `mouse_click`, `mouse_wheel`, `screenshot`,
`controller_button`, `controller_axis`, `console` (run a console command).

Before runtime parsing, the harness validates the entire JSON-like document
(including the supported `//` comments and hexadecimal-number extension).
Truncation, trailing data, malformed delimiters, unknown/duplicate event
fields, and fields incompatible with the declared event type fail closed.
After stable event ordering, a fixture is also rejected if an explicit
key/action/mouse press would remain held across a `wait_until`; bounded assist
ownership is the only input allowed to live inside a paused interval.

### Named keys (Phase 1)

Strings accepted in `"key"`. Mirrors the SDL scancode table; the smoke
harness keeps the table in sync with `port/src/actionmap.cpp` VKL_ names.
Adding a new key is one-line.

```
Return Escape Tab Space Backspace
Up Down Left Right
A B C D E F G H I J K L M N O P Q R S T U V W X Y Z
1 2 3 4 5 6 7 8 9 0
F1 F2 F3 F4 F5 F6 F7 F8 F9 F10 F11 F12
LShift RShift LCtrl RCtrl LAlt RAlt
```

Anything else gets a hard error at harness init (so typos in JSON fail
loudly rather than silently doing nothing).

---

## Harness lifecycle

The harness runs entirely inside the client binary. No subprocess, no
remote control surface. The PowerShell runner only sets up the install
dir, launches the binary, and inspects the log file after exit.

```
main()
+- sysInitArgs(argc, argv)
+- updaterApplyPending()
+- crashInit() / chrTickStackInit()
+- sysArgCheck("--dedicated") -> g_NetDedicated = 1
+- conInit() / sysInit() / fsInit()
+- actionmapInit() / configInit()
|       <-- smokeHarnessInit() runs here.  pd.ini is loaded.
|           Harness reads --smoke <path>, parses JSON, applies channel
|           mask + verbose flag via sysLogSet*().  Returns 0 (no smoke)
|           or 1 (smoke active).
+- ...subsystems init as usual...
+- mainProc()
   +- mainInit()
   +- while (1) mainLoop()
      <-- smokeHarnessTick() runs once per frame at the same point as
          input dispatch.  Pushes scheduled SDL events, decrements
          timeout budget, may call smokeHarnessExit(0|1, reason).
```

The harness lives in module-static state. There is no global handle; the
public API is:

```c
/* Returns 1 if --smoke <path> was set and parsing succeeded.  Returns 0
 * if --smoke was absent (game runs normally).  Aborts with a clear log
 * line + exit(2) on parse error or missing required field. */
int smokeHarnessInit(void);

/* Per-frame tick.  Cheap no-op if not in smoke mode. */
void smokeHarnessTick(void);

/* Forced exit path.  Emits SMOKE: result=<reason> then calls exit(code).
 * Code 0 means the harness believes the scenario completed; 1 means the
 * harness believes it failed (timeout, parse failure that survived init,
 * unhandled assertion).  The runner re-evaluates assertions after exit
 * either way -- the binary's exit code is advisory, not authoritative. */
void smokeHarnessExit(int code, const char *reason);

/* Test if harness is active without referencing internals (used by
 * crash handler stubs to behave more deterministically). */
int  smokeHarnessIsActive(void);
```

### Log channel filter timing

`sysLogSetChannelMask` is callable any time after `sysInit()` returns.
The harness sets the mask after `configInit()` so the user's
`Debug.LogChannelMask` pd.ini value is overridden cleanly (configInit
loads pd.ini and registers the int, so setting the mask after config
load is the right point). Verbose flag follows the same rule.

### Input event injection

Events use `SDL_PushEvent`. The harness fills an `SDL_Event` struct with
synthesised `SDL_KEYDOWN` / `SDL_KEYUP` data (keysym.scancode + keysym.sym
+ keysym.mod set from the JSON), then calls `SDL_PushEvent`. SDL routes
the event through the normal event queue; the input layer (`inputLayer`,
`actionmap`, ImGui backend) consumes it exactly as if it had come from
a real keyboard.

`tap` is encoded as a KEYDOWN scheduled at `at_ms`, followed by a KEYUP
queued for `at_ms + 16` (one frame at 60Hz). This avoids the held-key
class of bugs where the harness presses a key and never releases it.

### Crash handler interaction

`smokeHarnessIsActive() == 1` causes the crash handler's stack-dump path
to emit the dump as a single LOG_ERROR line tagged `SMOKE_CRASH:` instead
of opening a Windows error dialog. This keeps the harness deterministic
under AV / divide-by-zero / segfault.

---

## Runner contract

`tools/smoke-verify/run.ps1` is the only entry point. Flags:

```
-Test <name>            Run a single test by file stem.
-Tag <tag>              Run all tests carrying <tag>. Repeatable.
-AutoSelect             Run only tests whose paths_of_interest matches
                        a path changed since the merge base (default: dev).
-MergeBase <ref>        Reference base for -AutoSelect. Default: dev.
-Install <path>         Use an existing install dir instead of a fresh
                        per-test copy. Implies -Keep. Skips the clean
                        harness; primarily for local debugging.
-Build                  Run the queued build first (build-session.ps1).
-Session <id>           Session id passed to the queued build. Default:
                        smoke-<timestamp>.
-Keep                   Do not delete the per-run dir on success.
-Verbose                Print every assertion check, not just failures.
-Timeout <seconds>      Override timeout_seconds in the test definitions.
                        Useful for CI environments where the budget needs
                        a higher floor.
```

Output:

- Per-test summary line: `[PASS|FAIL] boot_smoke (8.3s) assertions=12/12`.
- On FAIL: assertion summary, log tail, run dir path (auto-kept on fail).
- Aggregate exit code: 0 if all tests pass, 1 otherwise.

Per-run directory layout:

```
.claude/smoke-verify-runs/<utc-stamp>-<test_name>/
+- PerfectDark.exe
+- pd.<romid>.z64
+- pd.ini                       (may be absent -- first-launch case)
+- data/                        (may be empty -- first-launch case)
+- pd-client.log                (written by the harness run)
+- smoke-verify-result.json     (written by run.ps1 after assertions)
```

### Focus-sensitive multi-process gates

Launch order and `GetForegroundWindow` are not input proof. A test that needs a
real focus handoff declares `window_focus_transition` with named source/target
processes plus `source_wait_for` and `wait_for` regexes. The runner enumerates
visible unowned top-level HWNDs by exact PID and fails unless each process has
exactly one candidate. It uses checked `AttachThreadInput`,
`SetForegroundWindow`, `SetActiveWindow`, and `SetFocus` calls, verifies
`GUITHREADINFO.hwndActive` and `hwndFocus` before and after detaching, then
requires fresh complete source-log witnesses for SDL `focus GAINED` followed by
`focus LOST`. The target must still own active keyboard focus at the lost
witness. Only `required_sequences` explicitly tagged with
`anchor: window_focus_transition` receive that exact loss-witness line; untagged
pre-transition sequences retain whole-log semantics. A missing named anchor
fails closed. Any operational failure assigns the named anchor an unreachable
line and emits a structured result diagnostic, so earlier log lines can never
satisfy the causal gate without invalidating unrelated evidence.

### Clean-install matrix

The runner supports three install modes via `install_state` in the test
definition (default: `clean`):

- `clean`: fresh dir, no `data/<romid>/`, no `pd.ini`. Exercises the
  first-launch extract + verify path. Catches B-324 family bugs.
- `prefilled`: fresh dir with a known-good `data/<romid>/` populated
  from a prior canonical run. Skips the first-boot extract path. Faster
  by about 8 seconds on cold-cache; useful for tests where boot is not
  the focus.
- `current`: symlinks the user's existing install dir directly. Only
  via `-Install` flag, debugging only, never run in CI.

The canonical prefilled `data/<romid>/` is built once per ROM and stashed
under `.claude/smoke-verify-cache/<romid>/`. The runner regenerates it
when its mtime is older than the `PerfectDark.exe` it was built against.

### Assertion engine

PowerShell-side, no game integration. The runner reads `pd-client.log`
as text, iterates assertions, returns the result. Forbidden patterns are
checked first (single match anywhere fails the test). Required lines
must each match at least once. Required counts match in the documented
range.

Regex flavour is .NET regex (PowerShell `-match`); the test definitions
intentionally avoid PCRE-only features so the same patterns work in
other tools.

---

## Build-gate integration

Step into the existing flow without disrupting the queued build wrapper:

1. `devtools/build-headless.ps1` finishes its build.
2. Post-build, if `-RunSmokeVerify` is set OR the env var
   `PD_SMOKE_VERIFY=1` is set, the script invokes:
   ```
   .\tools\smoke-verify\run.ps1 -AutoSelect -MergeBase dev
   ```
3. Smoke verify failure sets the build script's exit code to a distinct
   non-zero value (e.g., 23) so downstream tooling can distinguish a
   compile failure (already returns 1) from a smoke failure.

For the first week after this ship, the build-headless flag defaults OFF
and we run the gate manually. Once Mike confirms the noise floor is low
enough, the default flips to ON and the auto-merge step blocks on it.

---

## Bug-regression tests (first-class category)

The gate hosts two test categories that share the same schema and run
through the same runner:

- **Scenario tests** at `tools/smoke-verify/tests/*.json`. Boot smokes,
  stage loads, menu walks, anything that exercises a code path we want
  to keep green. Identified by the absence of a `bug_id` field.
- **Bug-regression tests** at `tools/smoke-verify/tests/bugs/*.json`,
  one per fixed bug. Identified by the presence of a `bug_id` field
  matching a row in the bug tracker (e.g. `tools/bugs/state.json`).

The runner discovers both via `Get-ChildItem -Filter "*.json" -Recurse`
so adding a bug-regression test is mechanical -- drop the JSON in
`tests/bugs/` and the gate picks it up on the next run. The relative
parent directory (`scenario` or `bugs`) is captured on each result row
as `Category` so the bug tracker can route by it without re-parsing
the JSON.

### Schema additions for bug-regression tests

In addition to the base scenario schema (above), bug-regression tests
declare two optional fields:

- `bug_id` (string, e.g. `"B-323"`): canonical bug id. The runner
  surfaces this in the per-test result row as `BugId`, so the bug
  tracker's auto-flip path can read it without parsing JSON.
- `regression_for` (string, one line): prose summary of what the bug
  looked like pre-fix. Goes into the run summary and the result row's
  `RegressionFor`.

Tags carry `"regression"` and `"bug:B-NNN"` so the suite can be sliced:

```powershell
.\tools\smoke-verify\run.ps1 -Tag regression       # all regressions
.\tools\smoke-verify\run.ps1 -Tag "bug:B-323"      # one specific bug
.\tools\smoke-verify\run.ps1                       # everything
```

### Result JSON shape (per-test row)

The runner writes its aggregate to
`.claude/smoke-verify-runs/results-<utc>.json`. Each row carries:

```jsonc
{
  "Name": "B-323_challenge_stride",   // file stem
  "Category": "bugs",                 // "scenario" or "bugs"
  "BugId": "B-323",                   // null when not a regression test
  "RegressionFor": "challenges...",   // null when not a regression test
  "Passed": true,
  "ExitCode": 0,
  "ElapsedSeconds": 8.7,
  "InstallDir": "...",
  "AssertionsTotal": 5,
  "AssertionsMet": 5,
  "Failures": [],
  "OperationalFailures": []   // phase/message/native state for runner failures
}
```

The bug tracker reads the `BugId` field. A passing row whose `BugId`
matches a tracker entry's `linked_test` triggers the status flip; a
failing row whose `BugId` matches an already-fixed entry reverts it to
`open` with a note pointing at the failing run dir.

### Test definition (bug-regression)

Same schema as scenario tests with the additions described above. A
filled-in example for a hypothetical fix to B-323:

```jsonc
{
  "scenario_name": "B-323_challenge_stride",
  "bug_id": "B-323",
  "regression_for": "challengesInit() AVed because PC sizeof(struct mpconfig) was used as on-disk stride; N64 stride is smaller.",
  "description": "Reproduce by booting any build that doesn't apply the N64-stride fix to src/game/challenge.c::challengeLoadConfig. AV fires inside bcopy before mainProc enters its tick loop.",
  "tags": ["regression", "bug:B-323", "stability"],
  "paths_of_interest": [
    "src/game/challenge.c"
  ],
  "log_channel_mask": "all",
  "verbose": 0,
  "timeout_seconds": 60,
  "install_state": "clean",
  "boot_args": ["--no-update-check", "--no-sound"],
  "input_sequence": [
    { "at_ms": 0,     "type": "wait" },
    { "at_ms": 30000, "type": "exit" }
  ],
  "assertions": {
    "required_lines": [
      "SMOKE: scenario=B-323_challenge_stride",
      "Asset Catalog: \\d+ entries registered"
    ],
    "forbidden_patterns": [
      "EXCEPTION_ACCESS_VIOLATION",
      "FATAL: ",
      "SMOKE: result=timeout"
    ],
    "required_counts": []
  }
}
```

### Lifecycle

- A bug-regression test stays in the suite **forever** after the fix
  lands. Removing it would lose the regression guard.
- When `fix_commit` lands and the linked test passes for the first
  time, the bug tracker flips the entry to `fixed` and records the
  `fix_commit` SHA.
- If the linked test ever fails again (typically because a later
  change reintroduced the broken behaviour), the bug tracker flips
  the entry back to `open` and notes the failing build.
- The test author's responsibility ends at "the test reliably fails
  on the broken build and reliably passes on the fixed build."
  Maintenance after that is the tracker's job.

### What goes in Phase 1 vs Phase 2

Phase 1 (this ship) provides:

- Recursive test discovery in the runner.
- Optional `bug_id` and `regression_for` fields tolerated by the
  harness (harness ignores them; they are runner-side metadata).
- `BugId` / `RegressionFor` / `Category` columns in the result JSON
  for the future bug tracker to read.
- Tag conventions documented.

Phase 1 does NOT ship any bug-regression test yet. The three Phase 1
tests are scenario tests (`boot_smoke`, `stage_load_paradox`,
`combat_sim_entry`). The first bug-regression tests land alongside
the bug tracker spec when it lands; this gate is ready to host them.

---

## Phase 2 expansion targets

The schema and the harness module already accommodate these. Each is a
small follow-up rather than a redesign.

- **Online init smoke**: scripted input that creates a listen-host room,
  reads the connect code, then attempts a second-instance join. Two
  `PerfectDark.exe --smoke` processes coordinated by the runner.
- **Mod load smoke**: drops a known-good `.pdmod` into the smoke install
  and asserts that the catalog log line counts the new asset entries.
- **Skin editor smoke**: scripted input that opens the skin editor,
  applies a colour, saves a preset, exits. Asserts no AV inside the
  skin-editor render loop.
- **Forge smoke**: spawn a prop, save the level, reload, assert the
  prop is present. The Forge serializer's round-trip test already
  exists at unit-test layer; the smoke variant exercises the full
  Forge UI + render path.
- **Named-verb input grammar**: expose the 10-rule universal grammar
  (`Confirm`, `Back`, `NextTab`, `NavUp`, `NavDown`, `NavLeft`,
  `NavRight`, `SectionPrev`, `SectionNext`, `PageJump`) as input verbs
  that the smoke harness resolves through `actionmap` so the test
  authoring works against semantic actions rather than raw scancodes.

---

## Files in this ship

```
port/include/smoke_harness.h
port/include/smoke_fixture_schema.h
port/include/smoke_readiness.h
port/include/smoke_transition.h
port/src/smoke_harness.c
port/src/smoke_fixture_schema.c
port/src/smoke_readiness.c
port/src/smoke_transition.c
port/src/main.c                 (init + tick hook, ~10 lines)
tools/smoke-verify/run.ps1
tools/smoke-verify/lib/Test-Assertions.ps1
tools/smoke-verify/lib/Install-Harness.ps1
tools/smoke-verify/tests/boot_smoke.json
tools/smoke-verify/tests/stage_load_paradox.json
tools/smoke-verify/tests/combat_sim_entry.json
context/designs/engine/smoke-verify-gate.md     (this file)
tools/kanban/state.json                         (new card)
context/tasks.md                                (new lane entry)
context/session-log.md                          (this session)
```

---

## Where to look

- For the existing log channel infrastructure that the harness leans on:
  `port/include/system.h` (LOG_CH_* bitmask, prefix-classifier) and
  `port/src/system.c` (sysLogClassifyMessage prefix table).
- For the SDL event pipeline the harness pushes into:
  `port/src/input.c::inputEventFilter` and
  `port/fast3d/gfx_sdl2.cpp::gfx_sdl_handle_events`.
- For the existing CLI arg pattern the `--smoke` flag follows:
  `port/src/main.c` and `port/src/system.c::sysArgCheck/sysArgGetString`.
- For how the queued build wrapper is composed (so the gate integration
  reads naturally): `devtools/build-session.ps1` and
  `devtools/build-headless.ps1`.
