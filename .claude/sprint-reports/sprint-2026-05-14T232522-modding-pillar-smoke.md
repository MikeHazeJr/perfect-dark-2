# Sprint Report - mod_load_smoke (modding pillar)

**Date:** 2026-05-14
**Card:** c115 (Tests)
**Branch:** dev
**Commit:** a9e531b5
**Result:** PASS 19/19 assertions, 15.5s elapsed

---

## Goal

Author a new in-client smoke test covering the modding pillar. Prove a
`.pdmod` placed in `<install>/mods/` at boot is detected by
`modmgrScanDirectory`, validated by `modmgrParseModJsonBuf`, and
registered into `g_ModRegistry`. Ship a minimal fixture `.pdmod`
alongside.

This uses the `fixtures` array support landed in commit fc9645a
(`run.ps1::Invoke-SmokeTest` calls `Copy-SmokeFixtures` after install
seeding and before launch).

## What landed

### Fixture: `tools/smoke-verify/fixtures/test_smoke_skin.pdmod` (500 bytes)

Built via Python's `zipfile` module (the canonical approach for
cross-platform reproducibility; PowerShell zip tooling is brittle and
locks on Windows). Single deflate-compressed entry:

- `mod.json` — UTF-8 JSON with eight headline fields:
  - `id: "pd2.smoke.testskin"` (the validator-required field)
  - `name`, `version`, `author`, `description`, `base_fallback`,
    `requires_restart`, `tags`
  - Zero `bodies` / `heads` / `arenas` content arrays — deliberately
    empty payload to keep the fixture independent of the catalog
    hot-load path (which only fires for `enabled=1` mods)

Plus an M-2.3 zip-comment mirror in the EOCD trailer:

```json
{"name": "PD2 Smoke Test Skin", "creator": "PD2 Smoke Verify", "version": "1.0.0"}
```

This matches the `pdmod-format.md` Section 4.5.5 defensive-mirror spec.
The mirror is bounded short JSON for tools that read zip metadata
without unpacking.

Round-trip verified post-write (Python re-opens, namelist, manifest
parse, comment readback all confirmed).

### Test: `tools/smoke-verify/tests/mod_load_smoke.json`

- **fixtures** entry: stages
  `tools/smoke-verify/fixtures/test_smoke_skin.pdmod` to
  `mods/test_smoke_skin.pdmod` inside the install dir.
- **boot_args**: `--no-update-check --no-sound --no-net --skip-intro`.
  Vanilla boot, no fast-path needed. No menu navigation; modmgr scan
  fires inside `gameInit()` before any UI input arrives.
- **timeout**: 60s. Scripted exit at 15s -- modmgr scan + initial
  asset catalog warm-up completes by ~3s on the shared install
  reference run; 12s headroom covers cold-start jitter.

### Assertions (chosen for canonical pillar coverage)

**required_lines (10):**

1. `SMOKE: scenario=mod_load_smoke` — harness boot sentinel
2. `BOOT: --no-net set; netInit\(\) skipped` — boot args reached main
3. `modmgr: initializing\.\.\.` — modmgrInit entry
4. `modmgr: scanning '.*mods' for mods\.\.\.` — scan walker fires
5. `modmgr: parsed mod\.json for 'pd2\.smoke\.testskin'.*archive=yes`
   — manifest validated AND archive flag set (proves the .pdmod path,
   not the folder path)
6. `modmgr: discovered mod \[\d+\] 'pd2\.smoke\.testskin'.*\[\.pdmod\]
   file=.*test_smoke_skin\.pdmod sha256=[0-9a-f]+\.\.` — registered
   into `g_ModRegistry` with SHA-256 prefix surfaced (M-1.6 trust
   echo)
7. `modmgr: scan complete -- \d+ mods found` — scan completed
8. `modmgr: initialized — \d+ mods, \d+ enabled` — modmgrInit finished
9. `Asset Catalog: \d+ entries registered` — boot reached catalog
   init, confirming the .pdmod scan didn't deadlock/crash boot
10. `SMOKE: result=scripted_exit` — clean exit reached

**forbidden_patterns (8):** Standard FATAL / ACCESS_VIOLATION / timeout
class + four mod-archive rejection classes targeted at the fixture:

- `modmgr: archive '.*test_smoke_skin\.pdmod' could not be opened`
  (broken zip)
- `modmgr: archive '.*test_smoke_skin\.pdmod' has no root mod\.json`
  (missing manifest)
- `modmgr: REFUSING to register archive '.*test_smoke_skin\.pdmod'`
  (M-1.6 trust gate refused it — would fire if dst path landed under
  a reserved subdir)
- `modmgr: mod\.json validation failed for 'pd2\.smoke\.testskin'`
  (id field missing or malformed JSON)

**required_counts (1):**

- `modmgr: discovered mod \[\d+\] 'pd2\.smoke\.testskin'` — exactly 1
  match (mod is discovered once and only once; dedupe guard intact)

## Verification

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/smoke-verify/run.ps1 `
  -Test mod_load_smoke -VerboseAssertions
```

Result:

```
[mod_load_smoke] PASS 19/19 assertions, 15.5s elapsed, exit_code=0
```

Log excerpt confirming discovery:

```
[00:02.49] modmgr: scanning './mods' for mods...
[00:02.49] modmgr: parsed mod.json for 'pd2.smoke.testskin' (PD2 Smoke
           Test Skin v1.0.0 by PD2 Smoke Verify) — 0 bodies, 0 heads,
           0 arenas, fallback=base-game, template=no, tags=3,
           archive=yes, requires_restart=no
[00:02.49] modmgr: discovered mod [1] 'pd2.smoke.testskin' (PD2 Smoke
           Test Skin) [.pdmod] file=./mods/test_smoke_skin.pdmod
           sha256=<8 hex chars>..
[00:02.49] modmgr: scan complete -- 2 mods found
[00:02.49] modmgr: initialized — 2 mods, 0 enabled
```

(2 mods = base.ui-chrome from the pre-existing `mods/base-game/` folder
plus our `.pdmod` fixture.)

## Design notes

### Why no catalog hot-load assertion

The dispatch suggested asserting `MOD: catalog rebuild — N
component(s) re-registered`. That log line only fires from
`modmgrRebuildCatalogFromCurrentSelection`, which is called from
`modmgrReload`, `modmgrSyncCatalogToRegistry`, or the apply path — NOT
from `modmgrInit` at boot. At boot, `main.c:307` calls
`assetCatalogScanComponents(modsdir)` directly; that only registers
INI components in `mod_*/` directories on disk (the open gap called
out in `pillars/modding.md`: `.pdmod` archives cannot deliver INI
components today).

The mod's mod.json `content` array (bodies/heads/arenas) WOULD
register through `modmgrLoadMod` -> `modmgrRegisterModJsonContentBuf`
-> `assetCatalogRegisterBody/Head/Arena`, but only if `enabled=1`,
which requires a matching entry in `%APPDATA%/PerfectDark/
mods-enabled.json`. Modifying that file from the smoke harness would
pollute user state across tests.

The chosen assertion set proves the three documented pillar
contracts: **detection** (scan walker fires + discovers the file),
**validation** (modarchive opens EOCD + central dir + manifest, AND
modmgrParseModJsonBuf accepts the id field), **registration**
(slot consumed in `g_ModRegistry`, surfaced via the discovered/scan-
complete log lines).

### Why a payloadless fixture

`pdmod-format.md` Section 3 lists no required entries beyond
`mod.json`. The validator path (modarchive open + manifest parse +
modmgrParseModJsonBuf) requires only:

- A valid zip EOCD (deflate-compressed entries OK)
- `mod.json` at the archive root
- `mod.json` contains a parseable `id` field

A payloadless mod IS a valid `.pdmod`; the codebase even uses
filename-slug fallback when `id` is missing (modmgr.c:485). Our
fixture has an explicit `id` so we get a deterministic regex anchor
in the discovered/parsed log lines.

Smallest possible envelope = smallest possible regression surface for
the fixture itself. If the modarchive parser ever tightens
(e.g., refuses zero-content-array manifests, mandates SHA-256 in
manifest, etc.) the fixture can grow accordingly with a one-line
Python edit.

## Files

- `tools/smoke-verify/fixtures/test_smoke_skin.pdmod` (new, 500 bytes)
- `tools/smoke-verify/tests/mod_load_smoke.json` (new, 58 lines)

## Out of scope (per dispatch)

- No C/C++ source edits
- No kanban / session-log edits
- No sub-agents
- Worktrees disabled — committed directly on `dev`

## Commit

```
a9e531b5 Tests - c115: mod_load_smoke (modding pillar)
```
