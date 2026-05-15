# Sprint Report - save_init_smoke (save-wire-format pillar)

**Date:** 2026-05-14
**Card:** c115 (Tests)
**Branch:** dev
**Commit:** 9a3f306e
**Result:** PASS 18/18 assertions, 15.7s elapsed

---

## Goal (per dispatch)

Author a new in-client smoke test covering the **save-wire-format
pillar**. Dispatch wording: "prove that an existing agent save file is
detected and loaded at boot, with the canonical SAVE: log markers
asserting the deeper save-load path."

Default to Option A (single canonical SAVE_VERSION=2 agent fixture).
Honor hard rule: do not ship a fixture that doesn't validate.

## Investigation findings

Five facts dictated scope:

1. **Save dir is AppData by default.** `port/src/fs.c:233-256` resolves
   `saveDir = $H` (= `AppData/Roaming/perfectdark` via
   `SDL_GetPrefPath` on Windows) unless `--portable` or `--savedir` is
   passed. Fixtures staged inside the install dir are invisible to the
   save loader without one of those flags.

2. **`saveInit()` fires deterministically at boot.** `port/src/main.c:957`
   calls it after `fsInit()`. Emits canonical line:
   `SAVE: initialized -- save dir: <path>` (`savefile.c:280`).

3. **`saveMigrateInit()` fires deterministically before `saveInit()`.**
   `port/src/main.c:956`. Emits:
   `SAVEMIGRATE: Initialized (0 migrations registered, current save version: 2)`
   (`savemigrate.c:276-277`).

4. **The deeper save-load path does NOT auto-fire at boot.**
   `saveLoadAgent` is only called from
   `port/fast3d/pdgui_menu_agentselect.cpp:116` (Agent Select UI
   accept). `saveListAgents` is similarly UI-driven. A vanilla
   `--no-net` boot does NOT emit `SAVE: agent <name> loaded from <path>`
   without scripted menu nav to Solo Play -> Agent Select -> File pick.
   Empirically confirmed: the test run's pd-client.log contains no
   `SAVE: agent` lines and no `SAVE: found` lines despite the
   `MENUPOOL: acquired agent_select gen=1` line firing at
   ~8.84s (the Agent Select menu legacy `filemgr` path is used, not
   the new `saveListAgents`).

5. **Smoke harness does not currently support scripted Agent Select
   nav.** The only test that drives Agent Select-ish nav
   (`combat_sim_entry`) just taps Return repeatedly past the title;
   the agent select file picker requires arrow + accept on a specific
   file row that is keyed by `saveListAgents` results, which has no
   stable boot-time enumeration order to assert against.

## Decision

Scope to **honest boot-time-deterministic** coverage. Drop the dispatch
goal's `SAVE: agent <name> loaded` required-line (would fail without
scripted nav). Name the test `save_init_smoke` instead of
`save_load_smoke` / `save_roundtrip_smoke` to reflect what is actually
asserted: the save subsystem initialises cleanly with a v2 fixture
pre-staged at the canonical save-dir path.

This is per the dispatch's explicit fallback ("Naming: avoid the word
'roundtrip' if no write-back is asserted... `save_load_smoke` is more
honest. Pick based on what you actually exercise."). Even
`save_load_smoke` overstates the coverage; `save_init_smoke` is the
correct floor.

The fixture's value is still real: it proves that
(a) `Copy-SmokeFixtures` stages save-pillar files correctly,
(b) `--portable` re-targets `saveDir` to the install dir as expected,
(c) the loader subsystem boot is silent against the staged fixture
(no `SAVE: refusing to load`, `failed to load`, `corrupt`, or
`saveListAgents: failed to open`).

## What landed

### Fixture: `tools/smoke-verify/fixtures/agent_smoke.json` (228 bytes)

Minimal v2 agent JSON matching `saveSaveAgent`'s canonical write
format (`port/src/savefile.c:309-365`). Fields:

```json
{
  "version": 2,
  "name": "smoke",
  "totaltime": 0,
  "autodifficulty": 0,
  "autostageindex": 0,
  "thumbnail": 0,
  "coopcompletions": [0, 0, 0],
  "firingrangescores": [0, 0, 0, 0, 0, 0, 0, 0, 0],
  "weaponsfound": [0, 0, 0, 0, 0, 0]
}
```

The `besttimes[60][3]` array is deliberately omitted -- the loader's
forward-compat fallthrough (`s_skip_value` in
`savefile.c:117-141`) accepts missing keys. Including the full
60x3 = 180 zero array would bloat the fixture without adding loader
coverage.

The `version: 2` matches the current `SAVE_VERSION` constant
(`savefile.h:41`). A v=2 file is the canonical-current case --
`saveCheckFileVersion(2, ...)` returns 0 without logging
(`savefile.c:230-243`). v=1 would have triggered the forward-compat
log line if `saveLoadAgent` ever fired; v=3 would have triggered the
refuse-to-load path. Neither fires at boot.

### Test: `tools/smoke-verify/tests/save_init_smoke.json`

Schema matches `mod_load_smoke.json` (commit a9e531b5) -- uses
`fixtures` array.

**Boot args:** `--no-update-check --no-sound --no-net --portable`.
`--portable` is the load-bearing one: `port/src/fs.c:165-167` and
`233-256` together make `saveDir = exeDir = install dir`. Without it,
`saveDir = $H` and the staged fixture is in the wrong directory.

**Fixture:** stages `tools/smoke-verify/fixtures/agent_smoke.json`
into `<install>/agent_smoke.json` (canonical `agent_<name>.json`
format per `savefile.c:262`).

**Input sequence:** wait + exit at 15s. No nav. `saveInit` runs ~1s
in; 14s headroom for cold-start variance.

**Assertions (18 total):**

Required lines (6):
- `SMOKE: scenario=save_init_smoke` -- harness ack
- `BOOT: --no-net set; netInit\(\) skipped` -- proves boot path
- `SAVEMIGRATE: Initialized \(0 migrations registered, current save version: 2\)`
  -- pillar init line A
- `SAVE: initialized -- save dir: ` -- pillar init line B
- `Asset Catalog: \d+ entries registered` -- general boot health
- `SMOKE: result=scripted_exit` -- clean harness exit

Forbidden patterns (10): the standard `FATAL:` /
`EXCEPTION_ACCESS_VIOLATION` / `SMOKE: result=timeout` /
`LOUDFAIL.LOAD: ... unrecoverable` plus six save-specific:
- `SAVE: refusing to load agent` (v > SAVE_VERSION rejection path)
- `SAVE: failed to load agent 'smoke'` (file open or parse failure
  named for our fixture)
- `SAVE: agent 'smoke' .* corrupt` (preserved for future corrupt
  detection emitter; currently no such line exists but the test
  guards against future regressions)
- `SAVE: saveListAgents: failed to open` (directory-scan failure)
- `SAVEMIGRATE: Migration registry full` (32-slot cap exceeded)
- `SAVEMIGRATE: Invalid migration` (registration validation
  failure)

Required counts (2):
- `SAVE: initialized -- save dir: ` count exactly 1 (init is
  idempotent; the `s_Initialized` guard at `savefile.c:271` ensures
  this; the count assertion locks the guard)
- `SAVEMIGRATE: Initialized` count exactly 1 (same idempotency
  guarantee)

## Validation

Ran:
```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/smoke-verify/run.ps1 `
  -Test save_init_smoke -VerboseAssertions
```

Result: `PASS 18/18 assertions, 15.7s elapsed, exit code 0`.

Log excerpt confirming the pillar contract:
```
[00:00.01] SAVEMIGRATE: Initialized (0 migrations registered, current save version: 2)
[00:00.01] SAVE: initialized -- save dir: C:/Users/mikeh/Perfect-Dark-2/perfect_dark-mike/.claude/smoke-verify-install
```

The `save dir` is the install dir as expected under `--portable`. The
agent_smoke.json fixture sits at that path post-stage; no loader
error fires against it. Both init lines fire exactly once.

## What this does NOT cover

For transparency, the deeper save-pillar paths NOT exercised:

1. `saveLoadAgent` -- requires scripted Agent Select nav. Out of
   scope for this dispatch (no C++ edits allowed; menu nav fragile).
2. `saveLoadSystem` -- requires explicit invocation; not auto-called
   at boot today.
3. `saveLoadMpPlayer` / `saveLoadMpSetup` -- MP path; out of scope.
4. v1 -> v2 migration -- `saveMigrateInit` registers 0 migrations
   today (`savemigrate.c:245-278`); the framework is wired but the
   v1->v2 path is in `mpsetupfileLoadWad` only and requires loading
   an old MP setup WAD, not an agent JSON.

A future dispatch could expand this to drive Agent Select via
`ACTION_USE` / `ACTION_MENU_DOWN` once the menu-nav infrastructure
stabilises -- the `mp_room_flow` test demonstrates the pattern but
also notes a ~5s post-Combat-Simulator crash class still present in
deeper Room sub-screens. Same risk applies to deeper Agent Select
nav until that crash class is fixed.

## Bug surface

None. Test passes on the current `Build/PerfectDark.exe` (SHA
inherited from current `dev` HEAD pre-commit).

## Files

- `tools/smoke-verify/fixtures/agent_smoke.json` (new, 228 bytes)
- `tools/smoke-verify/tests/save_init_smoke.json` (new)

No C / C++ source touched. No kanban / session-log edits. No
sub-agents. Worktrees disabled per dispatch.
