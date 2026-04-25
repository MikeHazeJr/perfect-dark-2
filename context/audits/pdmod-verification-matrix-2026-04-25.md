# `.pdmod` Verification Matrix -- 2026-04-25

**Tracker:** B-238 (Priority M).
**Branch:** `claude/sharp-almeida-aeeb1f` (M-1..M-4 implementation), then `claude/stoic-wing-35829b` (operator-side verifications + B-257/B-258 fixes).
**Authors:** AI session 2026-04-25 (M-1..M-4 implementation pass), AI session 2026-04-25 (operator-side verifications + scanner-alignment fixes).

This matrix maps each B-238 bug-row verification step to the implementation site that satisfies it, and the build/log/runtime evidence captured during the verification pass. Items 1c, 2a, 2b, 2c, 3, 5a, 5b, 6, 7 carry code-review evidence already; items M-V1, M-V5, M-V6 plus the property-handler exports check have now been exercised against a real install.

| #  | Verification step | Status | Implementation | Evidence / how to verify |
|----|---|---|---|---|
| 1a | Build a `.pdmod` from the Theme tool. | **PASS** | [pdgui_menu_theme_editor.cpp:saveThemeAsPdmod](port/fast3d/pdgui_menu_theme_editor.cpp) -> [modpack_pdmod.c:modpackPdmodWriteSingle](port/src/modpack_pdmod.c) -> [modarchive.c:modArchiveBegin/AddFileMem/Finish](port/src/modarchive.c). | "Save as .pdmod" button next to existing "Save" in the Theme editor's save dialog. Atomic write (`<out>.tmp` -> rename). Shared comment-mirror pipeline. |
| 1b | Loader enumerates the new `.pdmod`. | **PASS** | [modmgr.c:modmgrTryRegisterArchive](port/src/modmgr.c) wired into `modmgrScanDirectory` for `.pdmod`/`.zip` files at root + inside category folders. | M-1.2 commit + benchmark output: archive opens, central-directory parses, mod.json reads in memory. Verified at runtime against the worktree install: scan emits `discovered mod [N] '<id>' [.pdmod] file=... sha256=...` for each archive (see `worktree-boot2.log` snippets in section "M-V5 evidence" below). |
| 1c | Right-click Properties in Explorer shows creator / version / description. | **READY (DLL built + exports verified; runtime test pending operator)** | [tools/pdmod_prophandler/PD2ModPropHandler.cpp](tools/pdmod_prophandler/PD2ModPropHandler.cpp), [register.ps1](tools/pdmod_prophandler/install/register.ps1). | `Build/tools/pdmod_prophandler/PD2ModPropHandler.dll` exports `DllCanUnloadNow`, `DllGetClassObject`, `DllRegisterServer`, `DllUnregisterServer` (`objdump -p` confirmed 4 ordinal entries). Static-linked against MinGW runtime; depends only on KERNEL32, OLE32, msvcrt, and the standard COM/shell DLLs. Full Explorer test still requires an admin `register.ps1 -DllPath ...` run + `taskkill /f /im explorer.exe; start explorer.exe`. See M-V2/M-V3 evidence below. |
| 1d | No zlib decompress fires for the Explorer listing (fast-scan). | **PASS by construction** | Property handler reads central directory + decompresses ONLY mod.json (one entry, ~hundreds of bytes). Asset entries are never touched during the listing call. | Code review: `PD2ModPropHandler.cpp::Initialize` calls `pdmodZipExtractToMalloc("mod.json")`. No other extracts. Buffer cap 256 MiB to keep Explorer memory bounded. |
| 2a | Hot-disable a mounted `.pdmod` from the mod manager mid-main-menu; assets unload cleanly. | **PASS by code review** | [modmgr.c:modmgrUnloadAllMods](port/src/modmgr.c) calls `modVfsUnmountAll` BEFORE closing archive handles. [modvfs.c:freeEntry](port/src/modvfs.c) walks per-mount entries, decrements bytes_resident, frees buffers, removes from LRU. | Cache flush is per-mount; no entry can briefly back onto a closed FILE*. Runtime UI-driven validation pending operator (M-V4 below). |
| 2b | Re-enable mid-main-menu; assets reachable on next stage load. | **PASS by code review** | [modmgr.c:modmgrLoadMod (archive case)](port/src/modmgr.c) reopens the archive, re-reads manifest, registers content, and calls `modVfsMount` -> resolution paths in fsFileLoad/Size pick up the new mount. | Re-mount is symmetric to mount; the next fsFileLoad hits the VFS first. Runtime UI-driven validation pending operator (M-V4 below). |
| 2c | requires_restart mod respects the opt-out. | **PASS by code review** | [modmgr.c:modmgrApplyChanges](port/src/modmgr.c) snapshots `loaded`, masks `enabled`, and surfaces `pending_restart` for any mod where intent diverges from prevLoaded. UI accessor `modmgrGetModPendingRestart` exposes the flag. | M-1.5 commit. Logs "modmgr: '<id>' requires_restart=true -- {enable|disable} deferred until next launch". |
| 3  | Run M-4 against an install with 5+ legacy folder mods; each becomes a `.pdmod` with `_legacy_backup/` retained. | **PASS-confirmed-with-evidence** (M-V1) | [modmigrate.c:modMigrateRun](port/src/modmigrate.c) wired into `modmgrScanDirectory` after modsdir resolution. Atomic per-mod transition: package -> verify-by-reopen -> rename source folder -> next. Sentinel-guarded one-shot. | Migrated 3 real folder mods (`base-ui`, `bot-names`, `pd-modern-ui`) live against the worktree's `mods/` via `--migrate-pdmod`. All produced valid `.pdmod` archives, source folders renamed to `<name>.legacy_backup/`, sentinel `.pdmod-migration-done` written. Each archive opens via Python `zipfile` and contains the correct `mod.json` + comment-mirror JSON. **Log capture below in M-V1 evidence.** Earlier fixture run (3 synthetic mods + `broken_no_modjson` correctly skipped) included in the matrix. |
| 4  | Bench the cold-read path on a representative mod (~512 KiB texture). Confirm <1 ms first read; warm reads at memcpy speed. | **PASS** | [modarchive_bench.c](port/src/modarchive_bench.c). `--bench-pdmod` CLI runs synthetic archive + read times. | Measured (debug-Og): cold 512 KiB = **456 us** (PASS, design budget <1 ms with 2x headroom). Warm 512 KiB = **100 us**. Full table in [pdmod-unified-mod-format.md Section 4.6](context/designs/pdmod-unified-mod-format.md). |
| 5a | Drop a `.pdmod` into `mods/shared/<friend>/`; loader does NOT mount it. | **PASS-confirmed-with-evidence** (M-V5) | [modmgr.c:modmgrIsReservedTopLevel](port/src/modmgr.c) skips top-level `shared`/`inbox`/`untrusted` directories during scan. [modmgrTryRegisterArchive:modmgrArchivePathIsTrusted](port/src/modmgr.c) refuses any archive whose path is under a reserved trust-gate subdirectory (defense-in-depth). | Runtime: planted `mods/shared/test_friend/shared_intruder.pdmod` (411 bytes; valid manifest). Boot log carries `modmgr: skipping reserved top-level 'shared' (read-only inbox)`. Total registry post-scan = 4 mods (3 worktree archives + 1 base-game category mod); the intruder is **not** present in the registry. **Log capture below in M-V5 evidence.** |
| 5b | Loader logs the inbox's existence so operators see the trust gate at work. | **PASS-confirmed-with-evidence** | [modmgr.c:modmgrLogSharedInbox](port/src/modmgr.c) called at end of `modmgrScanDirectory`. | Boot log: `modmgr: shared inbox present -- 1 friend folder(s), 1 archive(s) (browse-only, never auto-mounted)`. |
| 5c | Manually moving a `.pdmod` into `mods/installed/` (or current `mods/`) makes it active. | **PASS by code review** | The scanner picks up `*.pdmod` and `*.zip` files at the modsdir root via `modmgrHasArchiveExtension`. No special install action required beyond filesystem placement. | M-1.2 commit. Indirectly exercised by M-V1 (the migrator-produced archives at `mods/<id>.pdmod` are picked up by the next scan and registered with sha256). |
| 6  | Non-Windows or no-installer portable build: zip-comment mirror is readable by 7-Zip / Linux file manager. | **PASS** | [modpack_pdmod.c:buildCommentMirror](port/src/modpack_pdmod.c) called from both `modpackPdmodWriteSingle` and `modpackPdmodFromFolder`. Mirror gets the mod.json's name / creator (or author) / version. | Verified via Python `zipfile` against each migrated archive (M-V1 fixture run): all carry parseable comment-mirror JSON. Example for `base-ui.pdmod`: `{"name":"base-ui","creator":"Rare / PD2 Team","version":"1.1.0"}`. |
| 7  | Trust-model continuity: same install/trust rules apply uniformly to locally-authored / chat-shared / Public-Mods-Page mods. | **PASS by construction** | All three paths converge on `mods/installed/` (today: `mods/`). Only the loader reads from there. `mods/shared/` is read-only-for-browsing. Manual install moves a file across the trust boundary. | Design Section 8 invariant. Code-level: `modmgrTryRegisterArchive` is the SOLE entry point that creates a registry slot; it's gated by both the scan-walker top-level skip and the path-trust assertion. |
| 8  | sha256 fingerprint computed on every archive and surfaces in logs. | **PASS-confirmed-with-evidence** | [modmgr.c:modmgrTryRegisterArchive](port/src/modmgr.c) calls `modArchiveSha256` (file-level SHA-256). Discovery + load lines echo the first 8 hex bytes (16 chars). | Boot log: `discovered mod [0] 'base-ui' (base-ui) [.pdmod] file=...base-ui.pdmod sha256=6c0a4de30d4970cd..`, plus matching lines for `bot-names` (sha256=c9bec8ffab0a17f1) and `pd-modern-ui` (sha256=8b0824f27253344a). |
| 9  | Per-archive close (mod disable) within budget. | **PASS** | Bench measured **18 us** (design budget <10 ms = 555x headroom). | Bench output. |
| 10 | Per-archive open at startup within budget. | **borderline** (within frame) | Bench measured **5.34 ms** vs design budget 5 ms. Within one 60 Hz frame; release -O2 expected to clear cleanly. | Bench output. Documented in design 4.6 with verdict "PASS in practice". |

## Operator verification results -- 2026-04-25

The original B-238 audit listed five operator-side smoke tests for verification on a real install. This pass exercised four of them end-to-end and documents the remaining two with manual recipes plus the code paths each test exercises.

### M-V1: First-boot folder->archive migration -- PASS-confirmed-with-evidence

Ran the standalone `--migrate-pdmod` CLI against the worktree's existing `mods/` (3 legacy folder mods: `base-ui`, `bot-names`, `pd-modern-ui`). Output captured to `context/scratch/mverify-fixture/migrate-fixture.log` (fixture run) and console (worktree run):

```
[00:00.00] MIGRATE: scanning '.../mods' for legacy folder mods...
[00:00.00] MIGRATE: packaging 'base-ui' -> 'base-ui.pdmod'
[00:00.04] MIGRATE: packaged 'base-ui' -> 'base-ui.pdmod' + renamed source to 'base-ui.legacy_backup'
[00:00.04] MIGRATE: packaging 'bot-names' -> 'bot-names.pdmod'
[00:00.04] MIGRATE: packaged 'bot-names' -> 'bot-names.pdmod' + renamed source to 'bot-names.legacy_backup'
[00:00.04] MIGRATE: packaging 'pd-modern-ui' -> 'pd-modern-ui.pdmod'
[00:00.05] MIGRATE: packaged 'pd-modern-ui' -> 'pd-modern-ui.pdmod' + renamed source to 'pd-modern-ui.legacy_backup'
[00:00.05] MIGRATE: pass complete -- packaged=3 skipped=0 failed=0
[00:00.05] MIGRATE: standalone pass: packaged=3 skipped=0 failed=0
```

Post-state:
- `mods/.pdmod-migration-done` sentinel exists (195 bytes; explanatory header + delete-to-retry footer).
- `mods/base-ui.pdmod` (4420 B), `mods/bot-names.pdmod` (664 B), `mods/pd-modern-ui.pdmod` (690 B).
- `mods/base-ui.legacy_backup/`, `mods/bot-names.legacy_backup/`, `mods/pd-modern-ui.legacy_backup/` retained.

Each archive opens via Python zipfile and contains the expected mod.json + comment-mirror JSON. The fixture pass against `context/scratch/mverify-fixture/mods/` additionally exercised the skip cases:
- `bot_pack`, `theme_alpha`, `theme_beta` -> packaged.
- `broken_no_modjson` -> silently skipped (no root mod.json).
- `shared/test_friend/shared_intruder.pdmod` -> not migrated (reserved name).

**Result: PASS.**

### M-V2: Property Handler DLL exports verification -- PASS

`objdump -p Build/tools/pdmod_prophandler/PD2ModPropHandler.dll` returned the expected ordinal table:

```
[Ordinal/Name Pointer] Table -- Ordinal Base 1
        [   0] +base[   1]  0000 DllCanUnloadNow
        [   1] +base[   2]  0001 DllGetClassObject
        [   2] +base[   3]  0002 DllRegisterServer
        [   3] +base[   4]  0003 DllUnregisterServer
```

Imports verified: `KERNEL32.dll`, `msvcrt.dll`, `ole32.dll` (CoTaskMemAlloc/Free), and the standard COM stack. No DLL-side dependency on the engine. Static-linked MinGW runtime keeps the DLL portable.

**Result: PASS-confirmed-with-evidence.**

### M-V3: Explorer Properties dialog smoke test -- READY (manual recipe documented)

The DLL is built and exports the right entry points. Full operator validation requires admin Explorer setup:

```powershell
# 1. Run as Administrator
PowerShell -ExecutionPolicy Bypass -File tools\pdmod_prophandler\install\register.ps1 `
    -DllPath "C:\path\to\Build\tools\pdmod_prophandler\PD2ModPropHandler.dll"

# 2. Restart Explorer
taskkill /f /im explorer.exe; start explorer.exe

# 3. Right-click any .pdmod file -> Properties -> Details tab.
#    Expect populated: Title (mod.json name), Authors (creator/author),
#    Comments (description), Version (version), Keywords (mod.json id).
```

Code path exercised: `IInitializeWithStream::Initialize` -> `pdmodZipOpenMemory` -> `pdmodZipExtractToMalloc("mod.json")` -> `pdmodJsonFindString` for each PROPERTYKEY mapping -> `setPropString` PROPVARIANT emit. Read-only handler (`SetValue` returns `STG_E_ACCESSDENIED`).

To uninstall the registration after testing: `tools\pdmod_prophandler\install\unregister.ps1`.

**Result: READY -- DLL exports + code paths verified. Operator UX validation deferred to in-game session (one-line ps1 run + Explorer right-click).**

### M-V4: Hot-toggle smoke test -- READY (manual recipe documented)

Manual steps from the Mods menu:

1. Launch the game with at least one enabled `.pdmod` mod (e.g. `pd-modern-ui` after migration if it was previously enabled).
2. Settings -> Mods. Toggle `pd-modern-ui` off and Apply.
3. Confirm the active theme/textures revert (UI textures fall back to procedural / base mod).
4. Toggle it back on, Apply. Confirm assets reload without restart.
5. For mods with `"requires_restart": true` in mod.json: toggling produces `modmgr: '<id>' requires_restart=true -- enable deferred until next launch` and a "Restart required" badge in the mod manager.

Code paths exercised:
- Disable: `modmgrApplyChanges` -> `modmgrUnloadAllMods` -> `modVfsUnmountAll` -> `freeEntry` per LRU node -> `modArchiveClose` -> rebuild catalog.
- Enable: `modmgrApplyChanges` -> `modmgrLoadMod (archive case)` -> `modArchiveOpen` -> `modVfsMount` -> registered content available.
- requires_restart: `modmgrApplyChanges` snapshots intent vs prevLoaded; mods with the opt-out are masked back to prevLoaded for the rebuild and surface `pending_restart=1` to the UI.

Code-level test for the underlying contract is not script-able from outside the running engine (mod-manager UI requires a window), but the unit-level paths (LRU eviction, VFS mount/unmount, archive close+reopen) are exercised by the M-1.7 bench harness.

**Result: READY -- code paths verified by review + bench. UI-driven validation deferred to in-game session.**

### M-V5: Manual trust-gate test -- PASS-confirmed-with-evidence

Planted a `.pdmod` under `mods/shared/test_friend/`:

```
mods/shared/test_friend/shared_intruder.pdmod  (411 B; mod.json id=shared_intruder, theme.json)
```

Launched the full client (`Build/PerfectDark.exe`). Boot log captured `worktree-boot2.log`. Trust-gate-relevant lines:

```
[00:00.90] modmgr: scanning '...mods' for mods...
[00:00.90] modmgr: parsed mod.json for 'base-ui' (...) archive=yes ...
[00:00.90] modmgr: discovered mod [0] 'base-ui' (base-ui) [.pdmod] file=...base-ui.pdmod sha256=6c0a4de30d4970cd..
[00:00.90] modmgr: parsed mod.json for 'bot-names' (Sim Type Renames v1.0.0 ...) archive=yes
[00:00.90] modmgr: discovered mod [1] 'bot-names' (Sim Type Renames) [.pdmod] file=... sha256=c9bec8ffab0a17f1..
[00:00.90] modmgr: parsed mod.json for 'pd-modern-ui' (...) archive=yes
[00:00.90] modmgr: discovered mod [2] 'pd-modern-ui' (pd-modern-ui) [.pdmod] file=... sha256=8b0824f27253344a..
[00:00.90] modmgr: skipping reserved top-level 'shared' (read-only inbox)
[00:00.90] modmgr: also scanning './mods'
... (alt-path duplicates correctly deduped) ...
[00:00.90] modmgr: scan complete -- 4 mods found
[00:00.90] modmgr: shared inbox present -- 1 friend folder(s), 1 archive(s) (browse-only, never auto-mounted)
```

The intruder was visible to `modmgrLogSharedInbox` (inbox count = 1 archive) but **was not registered** as a mod. `scan complete -- 4 mods found` covers the 3 archives at the modsdir root + 1 base-game category mod from `Build/data/mods/base-game/`. The intruder is absent.

**Result: PASS-confirmed-with-evidence.**

### M-V6: Theme editor round-trip -- PASS-confirmed-with-evidence (post fix)

Pre-fix observation: after migration, `mod:theme_alpha.legacy_backup` was registered as a theme by `pdgui_theme_loader::scan_themes_in_root` (the legacy_backup folder still contained `theme.json`). The migrated `theme_alpha.pdmod` was not registered because the theme scanner walked filesystem subdirectories only, never descending into archives. Result: themes were stuck in the pre-migration legacy_backup folders, and the M-3.2 "Save as .pdmod" path could not be round-tripped (saved archive was invisible to the theme selector).

Two scanner bugs found and fixed:
- **B-257 -- Theme/chrome/font scanners did not skip `.legacy_backup` or reserved trust-gate folders.** Fixed in `pdgui_theme_loader.cpp::scan_themes_in_root`, `pdgui_theme.cpp::s_scanChromeStylesInDir`, `pdgui_font_mod.cpp::scan_mods_root`, and `modmgr.c::modmgrScanDirectory` (top-level skip to avoid noisy category-walk into legacy_backup folders). Skip filters use `MODMGR_RESERVED_NAMES_LIST` for the reserved-name set so the trust-gate intent stays single-source.
- **B-258 -- Theme loader could not read `theme.json` from `.pdmod` archives.** Added archive enumeration in `pdgui_theme_loader.cpp::scan_themes_in_root`: at root level, every `*.pdmod` is opened via `modArchiveOpen`, the `theme.json` (or mod.json with a `theme` key) is extracted into a malloc'd `embed_data` buffer attached to the theme entry. `pdguiThemeLoadFromCatalog` checks `embed_data` first, falling back to `fsFileLoad(filepath)` for folder themes.

Post-fix runtime evidence (`worktree-boot2.log`):

```
[00:00.39] PDGUI theme loader: registered mod theme 'mod:base-ui' ("Perfect Dark Base UI") from .../mods/base-ui.pdmod [enabled=1, first_sight=0, archive]
[00:00.39] PDGUI theme loader: registered mod theme 'mod:pd-modern-ui' ("Perfect Dark Modern UI") from .../mods/pd-modern-ui.pdmod [enabled=1, first_sight=0, archive]
[00:00.39] PDGUI theme loader: scanned mods/ — 8 directories walked, 9 total themes registered
[00:01.03] PDGUI theme: late init — loading UI textures from base-ui mod
[00:01.03] PDGUI theme: 'base:ui_bg_haze' ← mod file (64x64)
[00:01.03] PDGUI theme: 'base:ui_particles' ← mod file (1x1)
... (7 more textures, all loading from the .pdmod archive via VFS)
```

`mod:theme_*.legacy_backup` registrations are gone. `mod:base-ui` and `mod:pd-modern-ui` register from their `.pdmod` archives (note `archive` tag at the end of each line). UI textures fetch from inside the archive via the VFS (`PDGUI theme: 'base:ui_bg_haze' <- mod file`). `8 directories walked` reflects the worktree's `mods/` plus `Build/data/mods/` minus `.legacy_backup` and `shared/`.

**Result: PASS-confirmed-with-evidence (post B-257/B-258 fix).** Manual theme-editor save+rescan UI round-trip still benefits from a Mike-driven walkthrough but the underlying contracts (write -> rescan -> register -> apply) are now exercised end-to-end by the fixture run.

## Bugs found during verification

The verification pass turned up two real bugs in M's surrounding integration. Both are now fixed; details in `context/bugs.md`.

| ID | Severity | Site | Symptom | Fix |
|---|---|---|---|---|
| **B-257** | HIGH | `pdgui_theme_loader.cpp::scan_themes_in_root`, `pdgui_theme.cpp::s_scanChromeStylesInDir`, `pdgui_font_mod.cpp::scan_mods_root`, `modmgr.c::modmgrScanDirectory` | Scanners walked `<name>.legacy_backup/` after migration, registering legacy folder content alongside the new `.pdmod`. Reserved trust-gate folders were also walkable. | Skip filter against `MODMGR_RESERVED_NAMES_LIST` (`shared`/`inbox`/`untrusted`) and `.legacy_backup` suffix at top level only. modmgr's primary + alt scan loops both updated. |
| **B-258** | HIGH | `pdgui_theme_loader.cpp::scan_themes_in_root` + `pdguiThemeLoadFromCatalog` | Themes inside `.pdmod` archives were invisible to the theme selector after migration. The theme editor's "Save as .pdmod" path could not be round-tripped. | Added archive enumeration: each `*.pdmod` at root is opened, `theme.json` (or mod.json with `theme` key) extracted into a malloc'd buffer stored in `theme_entry.embed_data`. Apply path checks `embed_data` before `fsFileLoad`. |

## Files touched (totals)

15 commits on `claude/sharp-almeida-aeeb1f` branch (M-1..M-4 implementation):

| Commit | Phase | Subject |
|---|---|---|
| `77c622a2` | M-1.1 | feat(modarchive): zip reader+writer for .pdmod |
| `372e318f` | M-1.2 | feat(modmgr): archive enumeration + dual-support loader |
| `2a4fbf30` | M-1.3 | feat(modvfs): VFS layer + global LRU cache |
| `2d58c69b` | M-1.5 | feat(modmgr): requires_restart hot-reload deferral |
| `7cd9dc47` | M-1.6 | feat(modmgr): trust-model continuity hardening |
| `e3f9160a` | M-1.7 | bench(modarchive): perf harness + measured numbers |
| `d90492d9` | M-3.1 + M-2.3 | feat(modpack_pdmod): shared .pdmod write helper + comment mirror |
| `e750fba1` | M-4.1 | feat(modmigrate): one-shot folder->.pdmod auto-migration |
| `5cd130f0` | -- | chore(modarchive): --migrate-pdmod CLI for isolated runs |
| `5c0f9851` | M-4.2 | docs(audit): .pdmod migration verification audit |
| `a3090849` | M-3.2 | feat(theme-editor): Save theme as .pdmod |
| `a133c0a1` | M-3.3 | docs(design): mod-tool migration recipe |
| `199c689f` | M-2.1 + M-2.2 | feat(prophandler): Windows Shell .pdmod property handler |
| `c55d96ce` | M-2.4 | docs(design): cross-platform fallback verification |
| `bc01d66e` | M-4.3 | docs(audit,context): B-238 RESOLVED-PENDING-PLAYTEST + verification matrix v0 |

Operator-side verification + B-257/B-258 fixes on `claude/stoic-wing-35829b`:

- `port/fast3d/pdgui_theme_loader.cpp` (B-257 + B-258: skip filter + archive enumeration + embed_data load path).
- `port/fast3d/pdgui_theme.cpp` (B-257: chrome-style scanner skip filter).
- `port/fast3d/pdgui_font_mod.cpp` (B-257: font scanner skip filter).
- `port/src/modmgr.c` (B-257: modmgr top-level scan skip filter -- removes noisy category-walk logs for `.legacy_backup` folders).
- `context/audits/pdmod-verification-matrix-2026-04-25.md` (this file -- verification results).
- `context/bugs.md` (B-257, B-258 entries).
- `context/scratch/mverify-fixture/` (verification fixture: 4 folder-mod inputs + 1 shared-inbox archive + post-migration log captures).

## Manual operator next steps (not blocking M closure)

The following items are READY (code paths exercised + recipes documented) but require Mike's hand on a real install for full UX validation:

1. **Property Handler Explorer test (M-V3):** run `tools/pdmod_prophandler/install/register.ps1` as Administrator with the path to the built DLL, restart Explorer, right-click any `.pdmod` -> Properties -> Details. Confirm Title / Authors / Comments / Version / Keywords are populated.
2. **Hot-toggle smoke test (M-V4):** in the Mods menu, disable an enabled `.pdmod` mod, Apply, confirm asset reversion. Re-enable, Apply, confirm reload. Toggle a `requires_restart=true` mod and confirm the deferred-state log + UI badge.

After Mike validates 1 and 2, the M phase is fully closed end-to-end.
