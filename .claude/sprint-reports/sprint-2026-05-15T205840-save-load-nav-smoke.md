# sprint-2026-05-15T205840-save-load-nav-smoke

**Card:** c115 (save-wire-format pillar — deeper coverage)
**Outcome:** **B (blocker — NO test shipped)**
**Branch:** dev (main checkout)
**Worker scope:** JSON-only authoring of `tools/smoke-verify/tests/save_load_nav_smoke.json`

---

## TL;DR

The blocker is structural, not nav-related. **`saveLoadAgent` has zero
production call-sites** in the current codebase. The Agent Select UI's
Load action still routes through the legacy `gamefileLoad()` path, not
the PC-native `saveLoadAgent`. No menu nav from main menu -> Solo ->
Agent Select -> Accept can possibly emit
`SAVE: agent 'smoke' loaded from <path>`, because that log line lives
inside an unreachable function.

The prior worker's sprint report
(`.claude\sprint-reports\sprint-2026-05-14T235300-save-pillar-smoke.md`)
stated `saveLoadAgent` is called from `pdgui_menu_agentselect.cpp:116`.
That was a misread: line 116 is `prefsAgentLoad(name)` -- the per-agent
**preferences sidecar** loader (`port/include/prefs_agent.h`), which is
a completely different subsystem from the save wire-format engine
(`port/src/savefile.c`).

The task hard rules forbid C / C++ edits, so I cannot add the missing
call site. Per the task's outcome-B directive, I shipped no test.

---

## Evidence

### saveLoadAgent definition (the ONLY site)

`port\src\savefile.c:367` defines `saveLoadAgent`.
`port\include\savefile.h:243` declares it.

The success log line is at `port\src\savefile.c:476`:

```
sysLogPrintf(LOG_NOTE, "SAVE: agent '%s' loaded from %s", name, path);
```

### saveLoadAgent call-site search (canonical)

```
Grep pattern: "saveLoadAgent\s*\(" across entire repo
```

Result (3 hits, none are actual callers):

- `port\PHASE_D5_PLAN.md:89` -- planning doc ("Replace `gamefileLoad()`
  call with `saveLoadAgent()`"). The plan was never executed.
- `port\src\savefile.c:367` -- the function definition itself.
- `port\include\savefile.h:243` -- the function declaration.

Zero call-sites. Confirmed by two separate Grep passes (`saveLoadAgent`
substring; `saveLoadAgent\s*\(` for invocation form).

### What the Agent Select Load action actually does

`port\fast3d\pdgui_menu_agentselect.cpp:158-174`
(`agentSelectGraphLoad`, the callback that runs when the user presses
A / Enter on a non-"New Agent" row):

```c
g_GameFileGuid.fileid       = payload->file->fileid;
g_GameFileGuid.deviceserial = payload->file->deviceserial;
filemgrSaveOrLoad(&g_GameFileGuid, FILEOP_LOAD_GAME, 0);
prefsLoadForFile(payload->file);
```

- `filemgrSaveOrLoad(..., FILEOP_LOAD_GAME, 0)` routes into
  `src\game\filemgr.c:824 errnum = gamefileLoad(device);`. That's the
  **legacy** save engine -- bit-packed pak-format reader in
  `src\game\gamefile.c:268`. It does not log "SAVE: agent ... loaded".
- `prefsLoadForFile(payload->file)` ends at
  `pdgui_menu_agentselect.cpp:116 prefsAgentLoad(name);` -- the per-
  agent visual-prefs sidecar, declared at
  `port\include\prefs_agent.h`. Different subsystem; no overlap with
  `port\src\savefile.c::saveLoadAgent`.

### What the existing save_init_smoke proves

Per the prior session's run output (cited from
`sprint-2026-05-14T235300-save-pillar-smoke.md`), the boot-time markers
that DO fire today are:

- `SAVEMIGRATE: Initialized (0 migrations registered, current save version: 2)`
  -- `port\src\savemigrate.c:276-277`
- `SAVE: initialized -- save dir: <path>` -- `port\src\savefile.c:269`
  area.

Neither of those touches `saveLoadAgent`. The Agent Select "Load" path
does not emit any line from `port\src\savefile.c` at all -- because
the Agent Select Load path does not call savefile.c.

### Why this isn't fixable from JSON alone

Three independent reasons:

1. **No call-site exists.** A scripted nav cannot summon a function
   from nothing; the C++ code must be edited to route the Agent Select
   Load action (or some boot-time pathway) through `saveLoadAgent`.
2. **The fixture format mismatch.** `tools/smoke-verify/fixtures/agent_smoke.json`
   is a v2 JSON shape that `saveLoadAgent` parses. But the legacy
   `gamefileLoad` reads a bit-packed binary pak body, not JSON.
   The fixture cannot reach `gamefileLoad` regardless of nav -- the
   on-disk filelist contains no entry pointing at it.
3. **`g_FileLists[0]` is empty in a vanilla --portable --no-net boot.**
   No pak EEPROM is present, so the Agent Select dialog's
   `fl->numfiles == 0` and the only selectable row is "+ New Agent...".
   Even if `saveLoadAgent` were wired in, there is no agent for the
   user to select; nav into the dialog reaches a list with zero
   loadable rows.

Point (3) alone would block any save-load nav test until either a
pak-format fixture is generated and copied to a pak-readable location,
or `gamefileLoad` is replaced with `saveLoadAgent` so the .json fixture
becomes the source.

---

## What this task tried to accomplish vs. what's actually needed

**Task scope:** "Single-launch scripted nav test that drives Agent
Select UI to trigger `saveLoadAgent`."

**Actual blocker:** Phase D5 step 2 from `port\PHASE_D5_PLAN.md`
("Replace `gamefileLoad()` call with `saveLoadAgent()`") was never
executed. Until that wiring is added, the Agent Select Load action
goes through the wrong save engine and no menu-driven test can exercise
`saveLoadAgent`.

The path forward is **engine work**, not test authoring:

1. Wire `saveLoadAgent` into `agentSelectGraphLoad` (replace or
   parallel-call alongside `filemgrSaveOrLoad`), AND ensure the
   filelist surfaces the JSON fixture as a selectable row, OR
2. Add a CLI fast-path (`--launch-load-agent <name>`) that calls
   `saveLoadAgent("smoke")` directly during boot, bypassing the menu
   entirely. This is the cheapest unlock for the smoke pillar and
   keeps menu wiring untouched.

Either choice is out of scope here (no C / C++ edits permitted by
task rules).

---

## Pillar coverage state — unchanged

Before this dispatch:

- save-wire-format pillar smoke = `save_init_smoke.json` only
  (boot-init markers; commit `9a3f306e`).

After this dispatch:

- save-wire-format pillar smoke = `save_init_smoke.json` only.
  No change. Outcome B per the task's directive ("better to leave the
  pillar at save_init_smoke than to ship a half-working nav test").

## Recommended follow-up cards (c115 lineage)

- **c115-save-pillar-fp**: tiny CLI fast-path
  `--launch-load-agent <name>` that calls `saveLoadAgent(name)` at
  boot. Unlocks single-launch save-load smoke coverage without any
  menu-nav work. Smallest viable engine change. Owner: engine session.
- **c115-save-pillar-wire**: complete PHASE_D5_PLAN step 2 -- route
  `agentSelectGraphLoad` through `saveLoadAgent`. Larger change
  (affects filelist surfacing, format conversion, pak compatibility).
  Owner: save-wire-format engine session, blocked on design call about
  legacy compat.

Once either lands, this dispatch's smoke test JSON becomes trivially
authorable: it would just be a boot-only `save_init_smoke` clone with
the extra `--launch-load-agent smoke` flag (option 1) or a real key-
event nav sequence into Agent Select (option 2).

---

## Files inspected (read-only)

- `tools\smoke-verify\tests\save_init_smoke.json`
- `tools\smoke-verify\tests\full_sdl_pipeline_smoke.json`
- `tools\smoke-verify\fixtures\agent_smoke.json`
- `port\fast3d\pdgui_menu_agentselect.cpp` (all 824 lines)
- `port\src\smoke_harness.c` (vocabulary + event schema)
- `port\src\savefile.c` lines 362-490 (`saveLoadAgent`)
- `port\include\savefile.h` lines 235-255
- `src\game\gamefile.c` lines 265-378 (`gamefileLoad`)
- `src\game\filemgr.c` lines 455-830 (`filemgrSaveOrLoad` switch)
- `.claude\sprint-reports\sprint-2026-05-14T235300-save-pillar-smoke.md`
- `port\PHASE_D5_PLAN.md` line 89

## Files authored

None. Outcome B.

## Run output

No smoke run was attempted -- the static-analysis blocker
(zero `saveLoadAgent` call-sites) makes any run a pre-determined fail.
Running the harness would only have produced a missing-required-line
failure for `SAVE: agent 'smoke' loaded from .*`, with no diagnostic
value beyond what static analysis already proved.

## Commit

None. Outcome B -> no test, no commit.
