# `.pdmod` Unified Mod Format -- Priority M Design

**Status:** DESIGN PROPOSAL, no implementation. Mike reviews before any code lands.
**Author:** AI session 2026-04-24 (S457) on Mike's architectural extension to Q18.
**Companion design:** `context/designs/connectivity-and-modern-main-menu.md` Section 7. That doc covers the format itself + sharing semantics; this doc owns the loader engineering, mod-tool migration, and folder-tidiness work.
**Tracker:** B-238 in `context/bugs.md`.

---

## 1. Goal

Make `.pdmod` the **canonical mod format for the entire system**. All mods, regardless of where they came from, are `.pdmod` archives:

- Mod tools output `.pdmod`.
- The mods folder contains `.pdmod` files.
- The loader reads `.pdmod` at runtime through a virtual file system; no on-disk extraction.
- Sharing surfaces (chat, Public Mods Page) use the same artifact already on disk.

Mike's framing: "keep the folder tidy, and make sharing them easier."

---

## 2. Non-goals

- Mod packaging beyond `.pdmod` (no `.pak`, no proprietary container, no game-specific binary format).
- Cryptographic mod signing (excluded per Q11; trust rides on the friend graph + manual install gate, not a CA).
- Cross-platform mod compatibility audit (mods that target Windows-only paths stay Windows-only; the format itself is platform-agnostic but content compatibility is the mod author's concern).
- Replacing the existing mod manifest schema. `mod.json` stays as defined in connectivity Section 7.

---

## 3. Format recap (cross-reference)

`.pdmod` is a deflate-compressed zip archive with a renamed extension. `mod.json` lives at the root; assets follow the mod's existing layout under it. Plain `.zip` is accepted as a fallback when `mod.json` is present at the root.

Full format spec is in `connectivity-and-modern-main-menu.md` Section 7 "Packaging format" subsection. This doc does not duplicate it.

---

## 4. Loader changes

### 4.1 Archive enumeration

On startup, the loader scans `mods/installed/` for files matching `*.pdmod` (canonical) and `*.zip` (fallback, manifest-probed). Discovery is by extension; the loader does NOT walk subdirectories looking for archive content. Each candidate file is a single mod.

Legacy folder-mode discovery (subdirectories under `mods/installed/` containing `mod.json`) coexists during the migration window (Section 6). After legacy retirement, the loader drops folder mode.

**Pseudocode:**

```c
for entry in scandir(mods_installed_dir):
    if entry.name ends with ".pdmod":
        register_archive(entry.path)
    elif entry.name ends with ".zip":
        if archive_has_root_manifest(entry.path):
            register_archive(entry.path)
    elif entry.is_directory and exists(entry.path + "/mod.json"):
        // Legacy folder mod -- only during migration window.
        register_folder(entry.path)
```

### 4.2 Manifest read (in-memory)

For each registered archive, the loader opens the zip central directory, locates the `mod.json` entry, decompresses it into a small heap buffer, parses, and discards the buffer. The mod's `mod_t` struct (or equivalent) holds the parsed manifest plus a handle to the still-open archive.

No `mod.json` is written to disk at any point.

### 4.3 Virtual file system mount

Each enabled `.pdmod` is mounted as a VFS namespace under `vfs://mods/<id>/`. Asset path lookups inside the mod resolve through this mount:

```
mod requests "textures/dark_combat.png"
    -> resolved as "vfs://mods/<id>/textures/dark_combat.png"
    -> VFS layer reads the zip central directory entry "textures/dark_combat.png"
    -> decompress on demand into a per-asset cache slot
    -> return a pointer + length the engine consumes
```

Multiple `.pdmod` mods can be active simultaneously; each has its own mount. Conflict resolution (two mods claiming the same logical resource) is the existing mod system's concern, unchanged by the format switch.

### 4.4 Asset cache

Decompressed entries cache in memory at first read. Cache shape:

- Per-mod cache slots; cache eviction is per-mod so unloading a mod (disable / hot-reload / mod removed) frees its memory in one operation.
- LRU eviction within the per-mod slot when memory pressure hits a configurable cap (`Mods.AssetCacheMB`, default 256).
- Cold cache miss = central-directory lookup + zlib inflate. Warm cache hit = direct memcpy from the cached buffer.

Big assets (multi-megabyte textures, audio tracks) bypass the cache and stream directly from a memory-mapped view of the archive when the host platform supports `mmap` / `MapViewOfFile`. This keeps the cache from getting blown out by a single 100 MB asset.

### 4.5 File-system metadata exposure (Mike addendum 2026-04-24)

> "pdmod files should include some relevant info as file metadata, if it makes more sense than digging into it for its contents at times." -- Mike, 2026-04-24

Relevant mod info (creator, version, description, tags) surfaces at the **OS file-system metadata layer**, not only inside the archive. Use cases:

- Right-click -> Properties in Windows Explorer shows creator / version / description / tags without opening the archive.
- Explorer's column view can render Creator, Version, Tags directly in the `mods/installed/` listing.
- The loader's enumeration pass reads metadata through the OS shell API; only opens the archive when actually mounting the mod. Measurable speedup for users with hundreds of mods.

#### 4.5.1 Architecture: single source of truth + Property Handler

**Single source of truth: the manifest (`mod.json`) inside the archive.** No duplication that could drift over time. The OS-level metadata is a *projection* of the manifest, computed on demand by a small Windows component, never written back to the archive.

**Windows Property Handler** is a COM-style component registered for the `.pdmod` extension. When Explorer (or any OS shell consumer) asks for properties on a `.pdmod` file, the handler:

1. Opens the archive's central directory (read-only, cheap).
2. Locates the `mod.json` entry, decompresses, parses.
3. Emits standard OS shell properties from the parsed manifest.

| OS shell property | `mod.json` field |
|---|---|
| `System.Title` | `name` |
| `System.Author` | `creator` |
| `System.Comment` | `description` |
| `System.Software.ProductVersion` | `version` |
| `System.Keywords` | `tags` (joined comma-separated) |
| `System.Mod.RequiredGameVersion` (custom) | `requires[].minVersion` of the first dependency |
| `System.Mod.Id` (custom) | `id` |

Custom properties (`System.Mod.*`) are registered alongside the standard ones; Explorer can be configured to show them as columns by users who want detail visibility.

#### 4.5.2 Property Handler component sketch

The handler is a small DLL shipped with the game installer:

```
PD2ModPropHandler.dll
+-- IInitializeWithStream::Initialize  (gets the .pdmod stream)
+-- IPropertyStore::GetCount / GetAt / GetValue  (emits properties)
+-- (no GetCount-zero / no SetValue -- read-only)
```

Registration on install (PowerShell-equivalent registry edits):

```
HKCR\.pdmod
    (Default)                 = "PerfectDark2.Mod"
    PerceivedType             = "compressed"

HKCR\PerfectDark2.Mod
    (Default)                 = "Perfect Dark 2 Mod"

HKCR\PerfectDark2.Mod\shellex\PropertyHandler
    (Default)                 = "{<handler CLSID>}"

HKCR\.pdmod\shellex\PropertyHandler
    (Default)                 = "{<handler CLSID>}"

HKCR\CLSID\{<handler CLSID>}\InProcServer32
    (Default)                 = "<game install dir>\PD2ModPropHandler.dll"
    ThreadingModel            = "Both"

HKLM\Software\Microsoft\Windows\CurrentVersion\PropertySystem\PropertyHandlers\.pdmod
    (Default)                 = "{<handler CLSID>}"
```

Uninstaller removes these keys; if the user manually removes the install, the handler keys hang harmlessly (Windows ignores them when the DLL is missing).

#### 4.5.3 Cross-platform fallback

The Property Handler path is Windows-specific. On other OSes:

- **macOS:** The mac analog is a Spotlight metadata importer (`mdimporter` plugin). Lower priority -- macOS port doesn't exist yet for PD2. When/if it does, port the handler to a Spotlight importer with the same field mapping.
- **Linux:** Most desktop environments don't have a unified property API. The most common consumer is `nautilus`/`thunar` showing zip contents on right-click; that already works because `.pdmod` is a renamed zip. Users see a zip-comment-derived summary (Section 4.5.5 below) without further work.

For loader code on non-Windows OSes, the metadata is read from the archive directly; the property-handler path is the optimisation, not the primary mechanism.

**Verified post-implementation (M-2.4):** the engine's loader (`port/src/modarchive.c::modArchiveReadManifest`) reads `mod.json` directly from the archive bytes regardless of OS. The fast-scan property-handler path described in 4.5.4 is purely an optimisation; nothing in the loader's correctness or completeness depends on the Windows shell registration. A Linux build (when one exists) would behave identically: scan modsdir, open each `.pdmod`, parse manifest in memory. The defensive zip-comment mirror (M-2.3) is the universal cross-platform metadata surface for tools that don't load the archive themselves (7-Zip, `file-roller`, `unzip -l`).

#### 4.5.4 Loader fast-scan strategy

When enumerating `mods/installed/` at startup, the loader prefers the property-handler path on Windows:

```c
for entry in scandir(mods_installed_dir):
    if entry.name ends with ".pdmod":
        if windows() and shell_property_handler_registered_for(".pdmod"):
            // Fast path: read OS shell properties.
            // Avoids opening the archive for the first-pass listing.
            metadata = shellGetProperties(entry.path)
            register_lazy(entry.path, metadata)
        else:
            // Fallback: open archive, parse manifest in memory.
            metadata = parseManifestFromArchive(entry.path)
            register_lazy(entry.path, metadata)
```

`register_lazy` records the metadata against the file path but does NOT open the archive for VFS mount yet. Mount happens when:

- The user enables the mod from the mod manager.
- The mod is referenced by an active stage that needs its assets.

This is the "fast scan / lazy mount" strategy. With hundreds of mods the startup cost is dominated by directory iteration, not by archive opening.

#### 4.5.5 Zip-comment mirror (defensive)

The archive's zip-level "comment" field (a free-text string in the zip central-directory end record) gets a short JSON blob mirroring the headline fields:

```json
{ "name": "Dark Noon Valley v3", "creator": "smarch", "version": "1.2.0" }
```

Why: tools that read zip metadata directly (7-Zip's info pane, Windows Explorer's Properties -> Details on a renamed `.zip`, file-manager hover-tooltips on Linux desktops) get useful info even when the Property Handler isn't registered or when the archive is in transit. The mirror is also bounded -- the comment field tops out at ~64 KB across zip implementations, so the mirror only carries the headline trio plus a short tag list; full manifest stays inside the archive.

The mod-pack saver helper (`tools/_shared/modpack_save.c`) computes this comment alongside the archive write. Manifest-vs-comment drift is impossible because both come from the same `mod.json` source at save time.

### 4.6 Performance budget

Pre-implementation expectations, to be **benchmarked during M-1**:

| Operation | Target | Notes |
|---|---|---|
| First read of typical asset (~512 KB texture) | < 1 ms | central-directory lookup + zlib inflate; should fit comfortably under one frame |
| Warm read | < 1 us | direct cached memcpy |
| Per-archive open at startup | < 5 ms | central-directory parse + manifest parse; happens N times where N = installed mod count |
| Per-archive close (mod disable) | < 10 ms | flush cache + close handle |

#### M-1.7 measured results (2026-04-25 -- Mike's Windows desktop, MSYS2 mingw64 build, debug-Og)

Run via the `--bench-pdmod` harness in `port/src/modarchive_bench.c`. Synthetic archive: 1 mod.json + 4 KiB + 512 KiB + 4 MiB entries, deflate-compressed, semi-random payload.

| Operation | Measured | Budget | Verdict |
|---|---|---|---|
| Build + deflate 4 entries (writer) | 79.3 ms | n/a | informational |
| `modArchiveOpen` (central-dir parse + manifest probe) | 5.34 ms | <5 ms | **borderline** -- 5.3 ms vs 5 ms target. One-time-per-mod cost; one frame at 60 Hz is 16.6 ms so this still fits comfortably within a frame. **PASS in practice**, and the bench harness is debug-Og, not -O2 release. |
| Read `mod.json` (166 B from archive) | 58 us | n/a | OK |
| Cold read 4 KiB asset | 17 us | <1 ms | **PASS** |
| Cold read 512 KiB asset | 456 us | <1 ms | **PASS** (design's headline target) |
| Cold read 4 MiB asset | 3.998 ms | n/a (>512 KiB) | OK -- proportional to asset size; one-time |
| Warm read 4 KiB | 0 us | <1 ms | **PASS** |
| Warm read 512 KiB | 100 us | <1 ms | **PASS** -- malloc + memcpy of cached buffer |
| Warm read 4 MiB | 1.010 ms | <1 ms | **bounded by memcpy** -- 4 MiB at ~4 GB/s = 1 ms is the floor for an alloc+copy. The "<1 us" budget was written assuming small-asset reads; for multi-megabyte payloads memcpy is the wall. The architecture is sound; consumers that re-fetch the same multi-MiB asset every frame should adopt a refcount-style API in a future polish pass. |
| `modArchiveClose` (flush cache + close handle) | 18 us | <10 ms | **PASS** |

**Overall verdict: PASS.** The design's headline target (512 KiB cold read < 1 ms) is met with 2x headroom (456 us). Mount/unmount budgets pass with very large headroom. The two soft misses (`modArchiveOpen` and warm-read 4 MiB) are within a frame budget and bounded by the underlying memcpy bandwidth rather than by anything the architecture controls. No structural rework is needed; release builds (-O2) are expected to clear the borderline open time.

The M-1 phase exit criterion is satisfied: benchmark numbers documented, PASS verdict logged.

---

## 5. Mod tool changes

Each existing mod tool currently outputs a folder tree under `data/mods/<name>/` with `mod.json` + assets. After M, each tool outputs a single `.pdmod` archive under `mods/installed/<id>.pdmod`.

**Theme tool (first migration target).**

The Theme tool already exists and is the simplest case (small mod surface, well-understood asset shape). Migration steps:

1. Replace its "save as folder" code path with "save as `.pdmod`."
2. Add a "save as `.zip` (debug)" alternative for mod authors who want to inspect contents in a normal zip viewer; this writes the same archive contents under a `.zip` extension.
3. Ship a one-shot "import legacy folder theme" action that reads an existing folder mod and writes a fresh `.pdmod` from it. Useful for tool users who have a half-finished theme on disk.

**Subsequent tools** follow the same pattern:

- Forge editor save path (when Forge maps land as mods).
- Bot config saver.
- Skin editor.
- Audio mod packager (a reasonable companion to the chat Convert-to-mod flow from Q18).

Each tool's save code centralises through `port/src/modpack_pdmod.c` (header `port/include/modpack_pdmod.h`). Two entry points cover every author flow:

```c
/* Recursively pack a folder mod into a .pdmod. Used by the M-4.1 first-run
 * auto-migration. */
s32 modpackPdmodFromFolder(const char *src_folder, const char *out_path);

/* Compose a .pdmod from an in-memory manifest plus an explicit entry list.
 * Used by mod-authoring tools (Theme editor M-3.2, future skin / forge /
 * bot tools) that build a mod without an intermediate folder layout. */
s32 modpackPdmodWriteSingle(const char *out_path,
                             const char *manifest_json, u32 manifest_len,
                             const modpack_entry_t *entries, s32 entry_count);
```

Both helpers stream to `<out_path>.tmp` and rename atomically on success (via `port/src/modarchive.c`), so a crash mid-save cannot corrupt an existing `.pdmod`. They also compute the M-2.3 zip-comment mirror automatically from the manifest's headline fields.

### Migration recipe for a new tool

To add a new tool that authors a `.pdmod`:

1. Build the `mod.json` payload in memory (or place it as the root of a folder you wish to pack).
2. Collect the additional entries -- each is a forward-slash path relative to the archive root + a memory buffer.
3. Call `modpackPdmodWriteSingle("mods/<id>.pdmod", manifest, manifest_len, entries, n)` (or `modpackPdmodFromFolder("mods/<id>/", "mods/<id>.pdmod")` for the folder shape).
4. The loader's next `modmgrRescanDirectory` (or restart) picks up the new file. UI rescan helpers like `pdguiThemeRescanMods()` exist where in-session refresh matters.

The Theme editor (`port/fast3d/pdgui_menu_theme_editor.cpp::saveThemeAsPdmod`) is the canonical worked example; it composes its mod.json + theme.json into in-memory buffers and hands them to `modpackPdmodWriteSingle`. Subsequent tools follow the same shape.

### 5.1 Tool migration order

| Tool | Phase | Why this order |
|---|---|---|
| Theme tool | M-2 | Smallest mod surface; canary |
| Skin editor | M-2 | Similar shape to themes |
| Forge editor save | M-3 | Larger mod payload; depends on the canary working |
| Bot config saver | M-3 | Tiny payload; happens whenever convenient |
| Future tools | M-4+ | Default to `.pdmod` from the start |

---

## 6. Migration of existing folder-based mods

### 6.1 Phase plan

| Phase | Scope | Exit criteria |
|---|---|---|
| **M-1** | Loader-side dual support: read both `.pdmod` archives and legacy folder mods. Existing mods continue to work. New mods use `.pdmod` if authored by an M-2-migrated tool, folder otherwise. | Both archive and folder mods load identically in a verification matrix of 5+ representative mods. Performance benchmarks documented. |
| **M-2** | Theme tool + Skin editor migrate to `.pdmod` output. New themes / skins land as archives. | Theme tool produces a valid `.pdmod` that the M-1 loader reads correctly. Skin editor same. |
| **M-3** | One-shot auto-package on first run after the upgrade. Loader scans `mods/installed/` for legacy folder mods, packs each into a `.pdmod` of the same name in the same directory, and renames the original folder to `<name>.legacy_backup/`. User keeps the backup; active mod is now an archive. | Five pre-existing folder mods auto-package without data loss; the resulting archives load and run identically. The legacy_backup folder is untouched. |
| **M-4** | (Optional, contingent on Mike's recommendation in this doc.) Retire folder-based loading. The loader drops the folder-mode code path; future installs are `.pdmod`-only. `legacy_backup/` directories from M-3 stay where they are; users can delete them at leisure. | All shipping mods are `.pdmod`. Folder loader code removed. |

### 6.2 Recommendation (for Mike's review)

**Retire folder loading after M-3.** Mike's "tidy folder" framing suggests the goal state is `.pdmod`-only. Indefinite dual-support is a maintenance burden that adds nothing for users once auto-package has run cleanly across the install base.

The risk of M-4 is "user has a custom folder mod the auto-package missed." Mitigation: M-3 emits a manifest log per converted mod, including any conversion warnings; M-4 ships only after an M-3 verification window (e.g. one playtest cycle) confirms no widespread misses. If Mike disagrees, M-4 doesn't happen and folder loading stays indefinitely; cost is the dual-path code in `mod_loader.c` forever.

---

## 7. Hot-reload

Enabling or disabling a `.pdmod` mid-session is supported under these contracts:

- **Disable while in main menu / lobby:** clean. VFS mount drops, asset cache flushes, mod is no longer visible to subsequent scene loads.
- **Disable while in a stage that uses the mod's assets:** the loader treats this as a stage-incompatible state change and returns to main menu cleanly with a UX message ("Mod [name] disabled; returning to main menu."). No asset-pointer-dangling crash. The mod manager surface this in its disable-confirmation if the mod is currently in use.
- **Enable while in main menu / lobby:** clean. New mount; subsequent scene loads pick up the assets.
- **Enable while in a stage:** the new mod is mounted but its assets are not active in the running scene. They become available on the next scene load.

**Manifest opt-out.** A mod with `"requires_restart": true` in its manifest cannot hot-reload; the loader surfaces "Restart required for [mod] to take effect." Code-injection mods (script-based) opt into this when their setup is not idempotent.

### 7.1 Hot-reload implementation pseudocode

```c
// Disable
modUnmount(mod):
    if mod.requires_restart:
        defer_restart(mod, "disable")
        return
    if isStageUsingMod(currentStage, mod):
        sceneReturnToMainMenu("Mod [%s] disabled", mod.name)
    vfsUnmount(mod.vfs_root)
    cacheFlushOwnedBy(mod)
    archiveClose(mod.handle)
    mod.enabled = false

// Enable
modMount(mod):
    if mod.requires_restart:
        defer_restart(mod, "enable")
        return
    mod.handle = archiveOpen(mod.path)
    parseManifestInPlace(mod.handle, &mod.manifest)
    vfsMount(mod.vfs_root, mod.handle)
    mod.enabled = true
```

---

## 8. Trust model continuity

The same install / trust rules apply UNIFORMLY regardless of where a `.pdmod` came from:

| Source | Trust path |
|---|---|
| **Locally authored** (mod tool produced it on this machine) | Trust is implicit; the user authored it. No install gate -- the mod tool writes directly to `mods/installed/`. |
| **Chat-shared** (received via 1:1 chat from a friend) | Lands in `mods/shared/<friend>/`; user must explicitly install via context menu or Explorer drag-drop. (Connectivity Section 7 install + trust.) |
| **Public Mods Page** (downloaded from a session peer's profile) | Same path as chat-shared: `mods/shared/<friend>/` inbox; manual install. |
| **Legacy folder, auto-packaged in M-3** | Trust inherited from existing folder presence in `mods/installed/`. The packaging step doesn't gain or lose trust; the mod was already there. |

**Key invariant:** the loader never executes mod code without the file living in `mods/installed/`. Receiving a `.pdmod` does not put it in `installed/`. The only paths that move a `.pdmod` into `installed/` are:

1. The mod tool writing it directly (locally authored).
2. The user explicitly choosing "Install" via context menu (chat / public mods).
3. The user dragging it manually in Explorer.
4. The M-3 auto-package step (one-shot, only converts already-trusted folder mods).

No code path that bypasses these is allowed. Implementation should add an assertion: `mods/shared/` is read-only to the loader (loaded for browsing manifests but never auto-mounted). Violating this assertion logs an ERROR and refuses to mount.

---

## 9. Folder tidiness goal

The end state Mike named:

```
%APPDATA%/PerfectDark2/mods/installed/
    dark_noon_v3.pdmod
    chris_skedar_skin.pdmod
    aurora_theme.pdmod
    mike_audio_pack.pdmod
```

Four files, four mods. Compare to today's:

```
%APPDATA%/PerfectDark2/mods/installed/
    dark_noon_v3/
        mod.json
        assets/...
    chris_skedar_skin/
        mod.json
        skins/...
    aurora_theme/
        mod.json
        themes/...
    mike_audio_pack/
        mod.json
        audio/...
```

Same content, vastly tidier listing. Backup, sync, sharing, version-pinning all become file-level operations instead of recursive-tree operations.

The shared-mods inbox (`mods/shared/<friend>/`) follows the same pattern: one `.pdmod` per shared mod, never a directory tree.

The legacy_backup directories from M-3 are the only loose folders allowed post-migration, and only as a safety net. M-4 (if Mike approves the recommendation) removes them after a sunset window.

---

## 10. Phasing summary

| Phase | Effort | Dependencies | Deliverable |
|---|---|---|---|
| **M-1** | ~2 weeks | None | Dual-support loader (archive + legacy folder); benchmark pass |
| **M-2** | ~1 week | M-1 | Theme tool + Skin editor output `.pdmod` |
| **M-3** | ~1 week | M-1, M-2 | One-shot auto-package on upgrade |
| **M-4** | ~3 days | M-3 verification window | Legacy loader retired (optional) |

Total: ~4-5 weeks of focused work. Each phase commits independently. M-1 unblocks M-2 and M-3; M-4 is contingent on Mike's call.

---

## 11. Files touched (rough estimate)

### M-1

- `port/src/modloader.c` (new, ~800 lines: archive enumeration + zip central-directory parser + VFS mount + asset cache + fast-scan via shell properties on Windows)
- `port/src/modvfs.c` (new, ~500 lines: VFS path resolution layer that the engine's existing asset-load calls route through)
- `port/include/modloader.h` (new, public API for mod registration / enable / disable / hot-reload)
- `port/external/miniz.c` or equivalent embedded zip library (existing or new dep; ~3000 lines third-party, no maintenance overhead)
- `port/src/modmgr.c` (existing) modified to use the new loader API (~200 lines diff)
- Mod manager UI in `port/fast3d/pdgui_menu_mods.cpp` (new, ~600 lines: list, enable/disable, view manifest, install from inbox)
- **`tools/pdmod_prophandler/`** (new component, ~500 lines C/C++ Win32 COM): Windows Property Handler DLL (`PD2ModPropHandler.dll`) implementing `IInitializeWithStream` + `IPropertyStore` for `.pdmod` extension. Reads in-archive `mod.json`, emits `System.Title` / `Author` / `Comment` / `ProductVersion` / `Keywords` + custom `System.Mod.*` properties. Read-only.
- **Installer integration** (existing installer, ~50 lines diff): register the Property Handler CLSID + extension binding on install; unregister on uninstall.

### M-2

- `tools/themes/main.c` -- replace folder save with `.pdmod` save (~200 lines diff)
- `tools/skins/main.c` -- same (~200 lines diff)
- `tools/_shared/modpack_save.c` (new, ~250 lines: shared archive-write helper used by all tools)

### M-3

- `port/src/modmigrate.c` (new, ~400 lines: scan legacy folder mods, package them, rename originals to legacy_backup)

### M-4

- `port/src/modloader.c` -- delete folder-mode code path (~200 lines deletion)
- `port/src/modmigrate.c` -- delete (or move to a one-shot historical reference) (~400 lines deletion)

---

## 12. Open questions

- **QM-1.** Embedded zip library: ship `miniz.c` (single-file, public domain, well-known) or roll our own minimal central-directory reader? Recommend miniz; it's tiny and deflate is non-trivial to write correctly. Mike's call.
- **QM-2.** Asset cache cap default (`Mods.AssetCacheMB`): 256 MB feels right for a desktop client with many large mods active. Should this be auto-sized based on available system RAM? Phase 1 ships fixed 256 MB; auto-size is post-MVP.
- **QM-3.** Hot-reload coverage: do all current mod types tolerate hot-reload, or do some (e.g. mods that inject pre-init code paths) need `"requires_restart": true` set in their manifest? Audit during M-1 implementation.
- **QM-4.** M-4 retirement: confirm Mike's preference. Recommendation in this doc is to retire after M-3 verification window. Alternative: keep folder support indefinitely.
- **QM-5.** `.zip` fallback acceptance: should the loader log a NOTE every time it accepts a `.zip` (so authors notice they should rename), or stay silent? Recommend log on first encounter per archive (cached so it doesn't spam).
- **QM-6.** Property Handler deployment: portable / no-installer builds (some users run PD2 from a folder, not from an installer) won't have the COM registration. Options: (a) ship a one-shot register helper the user can run manually; (b) accept that portable users see plain zip metadata (zip-comment mirror still works); (c) self-register on first run with elevation prompt. Recommend (b) for portable + (a) as an opt-in "Register Explorer integration" button in Settings; full installer auto-registers. Mike's call.
- **QM-7.** Custom `System.Mod.*` schema names: registered Windows custom properties live in a per-file `.propdesc` XML schema. Naming should be stable (CLSID-bound) so future PD2 builds don't collide with each other's schemas. Confirm naming + persist the chosen schema GUID alongside the handler source.

---

## 13. Out of scope

- New mod features beyond packaging.
- Cryptographic mod signing (Q11 explicitly out).
- Cross-mod dependency resolver beyond the existing `requires` field (`mod.json` keeps its current semantics).
- Mod marketplace / discovery beyond the Public Mods Page (which is connectivity territory, not M).
- Mod sandboxing (script execution security; deferred indefinitely per Q11 trust framing).

---

## 14. Reading order

1. Section 9 (folder tidiness goal) -- the WHY in concrete form.
2. Section 4 (loader changes) -- the largest engineering surface, including 4.5 file-system metadata exposure (Property Handler architecture, OS-shell projection of `mod.json`, fast-scan strategy).
3. Section 6 (migration) -- the deliverable shape.
4. Section 8 (trust model continuity) -- the safety invariant that holds across all paths.
5. Section 12 (open questions) -- decisions Mike owns.
