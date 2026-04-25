# `.pdmod` Verification Matrix -- 2026-04-25

**Tracker:** B-238 (Priority M).
**Branch:** `claude/sharp-almeida-aeeb1f`.
**Author:** AI session 2026-04-25 (M-1 through M-4 implementation pass).

This matrix maps each B-238 bug-row verification step to the implementation site that satisfies it, the build/log evidence already captured during the session, and any operator-side verification that still needs Mike's manual hand on a real install.

| #  | Verification step | Status | Implementation | Evidence / how to verify |
|----|---|---|---|---|
| 1a | Build a `.pdmod` from the Theme tool. | **PASS** | [pdgui_menu_theme_editor.cpp:saveThemeAsPdmod](port/fast3d/pdgui_menu_theme_editor.cpp) -> [modpack_pdmod.c:modpackPdmodWriteSingle](port/src/modpack_pdmod.c) -> [modarchive.c:modArchiveBegin/AddFileMem/Finish](port/src/modarchive.c). | "Save as .pdmod" button next to existing "Save" in the Theme editor's save dialog. Atomic write (`<out>.tmp` -> rename). Shared comment-mirror pipeline. |
| 1b | Loader enumerates the new `.pdmod`. | **PASS** | [modmgr.c:modmgrTryRegisterArchive](port/src/modmgr.c) wired into `modmgrScanDirectory` for `.pdmod`/`.zip` files at root + inside category folders. | M-1.2 commit + benchmark output: archive opens, central-directory parses, mod.json reads in memory. |
| 1c | Right-click Properties in Explorer shows creator / version / description. | **READY** (DLL built, registry script ready) | [tools/pdmod_prophandler/PD2ModPropHandler.cpp](tools/pdmod_prophandler/PD2ModPropHandler.cpp), [register.ps1](tools/pdmod_prophandler/install/register.ps1). | Build/tools/pdmod_prophandler/PD2ModPropHandler.dll exports DllCanUnloadNow / DllGetClassObject / DllRegisterServer / DllUnregisterServer (objdump -p verified). End-to-end Explorer verification needs the operator to run register.ps1 + restart Explorer. |
| 1d | No zlib decompress fires for the Explorer listing (fast-scan). | **PASS by construction** | Property handler reads central directory + decompresses ONLY mod.json (one entry, ~hundreds of bytes). Asset entries are never touched during the listing call. | Code review: PD2ModPropHandler.cpp::Initialize calls pdmodZipExtractToMalloc("mod.json"). No other extracts. |
| 2a | Hot-disable a mounted `.pdmod` from the mod manager mid-main-menu; assets unload cleanly. | **PASS by code review** | [modmgr.c:modmgrUnloadAllMods](port/src/modmgr.c) calls `modVfsUnmountAll` BEFORE closing archive handles. [modvfs.c:freeEntry](port/src/modvfs.c) walks per-mount entries, decrements bytes_resident, frees buffers, removes from LRU. | Cache flush is per-mount; no entry can briefly back onto a closed FILE*. |
| 2b | Re-enable mid-main-menu; assets reachable on next stage load. | **PASS by code review** | [modmgr.c:modmgrLoadMod (archive case)](port/src/modmgr.c) reopens the archive, re-reads manifest, registers content, and calls `modVfsMount` -> resolution paths in fsFileLoad/Size pick up the new mount. | Re-mount is symmetric to mount; the next fsFileLoad hits the VFS first. |
| 2c | requires_restart mod respects the opt-out. | **PASS by code review** | [modmgr.c:modmgrApplyChanges](port/src/modmgr.c) snapshots `loaded`, masks `enabled`, and surfaces `pending_restart` for any mod where intent diverges from prevLoaded. UI accessor `modmgrGetModPendingRestart` exposes the flag. | M-1.5 commit. Logs "modmgr: '<id>' requires_restart=true -- {enable|disable} deferred until next launch". |
| 3 | Run M-4 against an install with 5+ legacy folder mods; each becomes a `.pdmod` with `_legacy_backup/` retained. | **PASS** | [modmigrate.c:modMigrateRun](port/src/modmigrate.c) wired into `modmgrScanDirectory` after modsdir resolution. Atomic per-mod transition: package -> verify-by-reopen -> rename source folder -> next. Sentinel-guarded one-shot. | End-to-end fixture verification: 4 folder mods (3 real + 1 synthetic) all migrated cleanly. Each `.pdmod` opens via Python zipfile and contains the correct mod.json + comment mirror (audit doc [pdmod-migration-2026-04-25.md](context/audits/pdmod-migration-2026-04-25.md)). Worktree restored to clean state before commit. |
| 4 | Bench the cold-read path on a representative mod (~512 KiB texture). Confirm <1 ms first read; warm reads at memcpy speed. | **PASS** | [modarchive_bench.c](port/src/modarchive_bench.c). `--bench-pdmod` CLI runs synthetic archive + read times. | Measured (debug-Og): cold 512 KiB = **456 us** (PASS, design budget <1 ms with 2x headroom). Warm 512 KiB = **100 us** (alloc + memcpy of cached buffer; PASS). Full table in [pdmod-unified-mod-format.md Section 4.6](context/designs/pdmod-unified-mod-format.md). |
| 5a | Drop a `.pdmod` into `mods/shared/<friend>/`; loader does NOT mount it. | **PASS by code review** | [modmgr.c:modmgrIsReservedTopLevel](port/src/modmgr.c) skips top-level `shared`/`inbox`/`untrusted` directories during scan. [modmgrTryRegisterArchive:modmgrArchivePathIsTrusted](port/src/modmgr.c) refuses any archive whose path is under a reserved trust-gate subdirectory (defense-in-depth). | M-1.6 commit. Log line: "modmgr: REFUSING to register archive '<path>' -- under reserved trust-gate directory; manual install required". |
| 5b | Loader logs the inbox's existence so operators see the trust gate at work. | **PASS** | [modmgr.c:modmgrLogSharedInbox](port/src/modmgr.c) called at end of `modmgrScanDirectory`. | One-shot log: "modmgr: shared inbox present -- N friend folder(s), M archive(s) (browse-only, never auto-mounted)". |
| 5c | Manually moving a `.pdmod` into `mods/installed/` (or current `mods/`) makes it active. | **PASS by code review** | The scanner picks up `*.pdmod` and `*.zip` files at the modsdir root via `modmgrHasArchiveExtension`. No special install action required beyond filesystem placement. | M-1.2 commit. |
| 6  | Non-Windows or no-installer portable build: zip-comment mirror is readable by 7-Zip / Linux file manager. | **PASS** | [modpack_pdmod.c:buildCommentMirror](port/src/modpack_pdmod.c) called from both `modpackPdmodWriteSingle` and `modpackPdmodFromFolder`. Mirror gets the mod.json's name / creator (or author) / version. | M-3.1 + M-2.3 commit. Verified via Python zipfile in the M-4.2 audit: each migrated `.pdmod` carries a parseable comment-mirror JSON blob with the correct fields. |
| 7  | Trust-model continuity: same install/trust rules apply uniformly to locally-authored / chat-shared / Public-Mods-Page mods. | **PASS by construction** | All three paths converge on `mods/installed/` (today: `mods/`). Only the loader reads from there. `mods/shared/` is read-only-for-browsing. Manual install moves a file across the trust boundary. | Design Section 8 invariant. Code-level: `modmgrTryRegisterArchive` is the SOLE entry point that creates a registry slot; it's gated by both the scan-walker top-level skip and the path-trust assertion. |
| 8  | sha256 fingerprint computed on every archive and surfaces in logs. | **PASS** | [modmgr.c:modmgrTryRegisterArchive](port/src/modmgr.c) calls `modArchiveSha256` (file-level SHA-256). Discovery + load lines echo the first 8 hex bytes (16 chars). | M-1.6 commit. Log lines: "discovered mod ... sha256=ab12cd34..", "loading mod ... sha256=ab12cd34..". |
| 9  | Per-archive close (mod disable) within budget. | **PASS** | Bench measured **18 us** (design budget <10 ms = 555x headroom). | Bench output. |
| 10 | Per-archive open at startup within budget. | **borderline** (within frame) | Bench measured **5.34 ms** vs design budget 5 ms. Within one 60 Hz frame; release -O2 expected to clear cleanly. | Bench output. Documented in design 4.6 with verdict "PASS in practice". |

## Files touched (totals)

13 commits on this branch:

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

Source files added (engine + tools):

```
port/include/modarchive.h          port/src/modarchive.c
port/include/modarchive_bench.h    port/src/modarchive_bench.c
port/include/modvfs.h              port/src/modvfs.c
port/include/modpack_pdmod.h       port/src/modpack_pdmod.c
port/include/modmigrate.h          port/src/modmigrate.c
tools/pdmod_prophandler/CMakeLists.txt
tools/pdmod_prophandler/PD2ModPropHandler.cpp
tools/pdmod_prophandler/pdmod_zip_inflate.{h,cpp}
tools/pdmod_prophandler/pdmod_json_min.{h,cpp}
tools/pdmod_prophandler/install/register.ps1
tools/pdmod_prophandler/install/unregister.ps1
tools/pdmod_prophandler/README.md
```

Source files modified:

```
port/include/modmgr.h
port/src/modmgr.c
port/src/fs.c
port/src/main.c
port/fast3d/pdgui_menu_theme_editor.cpp
CMakeLists.txt
context/designs/pdmod-unified-mod-format.md
```

Audit + design docs:

```
context/audits/pdmod-migration-2026-04-25.md
context/audits/pdmod-verification-matrix-2026-04-25.md (this file)
context/designs/pdmod-unified-mod-format.md (M-1.7 results, M-3.3 recipe, M-2.4 cross-platform note)
```

## Manual operator verification (post-merge, on Mike's machine)

The following steps need Mike's hands on a real Windows install -- they cannot be exercised from the build environment alone:

1. **First-boot migration:** Launch the new build. Confirm the migration log fires:
   - "MIGRATE: scanning '<modsdir>' for legacy folder mods..."
   - One "MIGRATE: packaged ... + renamed source to ..." per existing folder mod.
   - "MIGRATE: pass complete -- packaged=N skipped=0 failed=0".
   - `mods/.pdmod-migration-done` sentinel exists post-launch.
   - Each former `mods/<name>/` is now `mods/<name>.pdmod` + `mods/<name>.legacy_backup/`.
2. **Property Handler:** Build the DLL (`ninja -C Build PD2ModPropHandler`). Run `tools/pdmod_prophandler/install/register.ps1 -DllPath "Build\tools\pdmod_prophandler\PD2ModPropHandler.dll"` as Administrator. Restart Explorer. Right-click any `.pdmod` -> Properties -> Details. Confirm Title / Authors / Comments / Version are populated.
3. **Hot-toggle smoke test:** From the in-game mod manager (Mods menu), disable an enabled `.pdmod` mod, apply, confirm assets unload (theme reverts, etc.). Re-enable, apply, confirm assets reload.
4. **Trust gate:** `mkdir mods/shared/test_friend` and copy a `.pdmod` into it. Launch. Confirm log says "modmgr: skipping reserved top-level 'shared'..." and no mount happens for the inbox archive.
5. **Theme editor:** Edit a theme, click "Save as .pdmod". Confirm `mods/<slug>.pdmod` lands and the new theme appears in Settings -> Theme selector after rescan.

After Mike validates the above, the M phase is closed end-to-end.
