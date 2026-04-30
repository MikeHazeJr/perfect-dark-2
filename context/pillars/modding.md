# Modding

> Component-based mod system. `.pdmod` archive format. JSON manifest + INI components. VFS over archive handles. Asset catalog is the single registration target. Network distribution with SHA-256 integrity. Reserved-name discipline at two layers.

---

## What it is

A mod is a component bundle distributed as a `.pdmod` archive (zip with `mod.json` manifest) or a folder under `mods/`. Each asset inside the bundle (map, character, skin, audio, etc.) is an independent component with its own `.ini` manifest (or JSON content entry in `mod.json`) and gets registered as an `asset_entry_t` in the catalog. Mods are not loaded as monolithic units; individual components are.

Code:

- Archive reader/writer: [port/src/modarchive.c](../../port/src/modarchive.c) (1006 lines).
- Manifest parser + scanner: [port/src/modmgr.c](../../port/src/modmgr.c) (3136 lines).
- VFS over archives: [port/src/modvfs.c](../../port/src/modvfs.c) (362 lines).
- Network manifest: [port/src/net/netmanifest.c](../../port/src/net/netmanifest.c) (2172 lines).
- Network distribution: [port/src/net/netdistrib.c](../../port/src/net/netdistrib.c) (1579 lines).
- Catalog scanner for INI components: [port/src/assetcatalog_scanner.c](../../port/src/assetcatalog_scanner.c).
- Property handler DLL (Windows Shell, external tooling): [tools/pdmod_prophandler/](../../tools/pdmod_prophandler/).

---

## `.pdmod` archive format

Standard zip central-directory layout. Contents:

- `mod.json` (required) - manifest with id, name, version, author, description, base_fallback, dependencies, content (bodies/heads/arenas arrays), template, tags, requires_restart.
- Component `.ini` files (maps, characters, skins, weapons, audio, hud) at known sub-paths.
- Assets: `.tga`, `.bin`, `.pdmap`, `.wav`, `.mp3`, `.ogg`, `.ttf`, etc.

Path-traversal sanitization at parse time ([modarchive.c:13-17](../../port/src/modarchive.c:13)). One `FILE*` per archive for lazy per-entry extraction. Atomic write path: `modArchiveBegin / AddFileMem / AddFileDisk / Finish` streams to `.tmp` and renames on success.

---

## Manifest parsing

[modmgr.c:122-283](../../port/src/modmgr.c:122) hand-written token-by-token JSON parser sufficient for the `mod.json` schema. Same pattern as [savefile.c](../../port/src/savefile.c), [updater.c](../../port/src/updater.c). Falls back to filename-slug when `id` field missing. `audio.ini` fallback path for legacy audio mods.

`mod.json` content array supports embedded body / head / arena declarations alongside the file-based INI scan. Both paths converge on the same catalog registration API.

---

## Asset registration paths

Two paths, both ending in `assetCatalogRegister*`:

1. **INI scan path.** [port/src/assetcatalog_scanner.c:205-253](../../port/src/assetcatalog_scanner.c:205) walks component directories, parses `.ini` files, calls typed register functions (`assetCatalogRegisterBody`, `RegisterHead`, `RegisterArena`, `RegisterWeapon`, `RegisterAudio`, `RegisterHud`).
2. **JSON content path.** `modmgrRegisterModJsonContentBuf` parses `mod.json` content entries and calls the same `assetCatalogRegister*` functions.

Both paths use the typed catalog registration API; both flow into the same catalog rows. See [pillars/catalog.md](catalog.md) for the catalog model.

---

## VFS

[port/src/modvfs.c](../../port/src/modvfs.c) implements a 256 MB LRU cache over open archive handles. `modVfsMount` at [modmgr.c:1895](../../port/src/modmgr.c:1895) registers archive contents for reading via `fsFileLoad` and `fsFileSize`. No on-disk extraction at runtime: `.pdmod` archives are read directly through the VFS handle layer.

---

## Mod enumeration

[modmgr.c:1395-1586](../../port/src/modmgr.c:1395) handles 4 candidate roots with dedup:

- `$E/../mods` (parent of executable dir)
- `./mods` (current working dir)
- `$E/mods` (sibling of executable dir)
- base-dir mods (Mike-installed, system-wide)

**Reserved names** (`shared`, `inbox`, `untrusted`, `.legacy_backup`) enforced at two layers:

- Scan walker at [modmgr.c:1478](../../port/src/modmgr.c:1478)
- `modmgrArchivePathIsTrusted` at [modmgr.c:1252-1262](../../port/src/modmgr.c:1252)

**Auto-migration** from folder-mod to `.pdmod` runs via `modMigrateRun` at [modmgr.c:1460](../../port/src/modmgr.c:1460).

**`mods/` tree depth cap**: discovery scanners (`modmgrScanDirectory`, `s_scanModChromeStyles`, `scan_mods_for_themes`) recurse exactly one level into top-level entries that have no manifest. Supports category folders (`mods/UI Chrome/<slug>/`, `mods/Weapons/<slug>/`, `mods/MP Maps/<slug>/`). Do not deepen the recursion (S293 constraint).

---

## Network manifest

[port/src/net/netmanifest.c](../../port/src/net/netmanifest.c) builds a `match_manifest_t` from the host's `g_MpSetup`, `g_NetLocalClient`, `g_MatchConfig`, and `modmgrGetMod()` before `CLC_LOBBY_START`. The host then embeds the serialized manifest into the lobby-start packet so clients know what assets the match requires.

**Manifest types** ([port/include/net/netmanifest.h:72-81](../../port/include/net/netmanifest.h:72)):

```
MANIFEST_TYPE_BODY       0
MANIFEST_TYPE_HEAD       1
MANIFEST_TYPE_STAGE      2
MANIFEST_TYPE_WEAPON     3
MANIFEST_TYPE_COMPONENT  4
MANIFEST_TYPE_MODEL      5
MANIFEST_TYPE_ANIM       6
MANIFEST_TYPE_TEXTURE    7
MANIFEST_TYPE_LANG       8
MANIFEST_TYPE_AUDIO      9
```

**Status responses** ([netmanifest.h:84-86](../../port/include/net/netmanifest.h:84)):

- `MANIFEST_STATUS_READY 0` - all assets present
- `MANIFEST_STATUS_NEED_ASSETS 1` - request transfer
- `MANIFEST_STATUS_DECLINE 2` - spectate from lobby

`manifestCheck` ([netmanifest.c:2055-2172](../../port/src/net/netmanifest.c:2055)) iterates manifest entries, resolves via `assetCatalogResolve`, sends `CLC_MANIFEST_STATUS(READY)` or `NEED_ASSETS`. SHA-256 compared at [netmanifest.c:2085-2110](../../port/src/net/netmanifest.c:2085).

---

## Network distribution

[port/src/net/netdistrib.c](../../port/src/net/netdistrib.c) implements `SVC_DISTRIB_BEGIN / CHUNK / END`. Each missing component is shipped as a PDCA archive (zlib-compressed), chunked over the wire. Client decompresses, extracts, hot-registers via `assetCatalogRegister` ([netdistrib.c:1156-1232](../../port/src/net/netdistrib.c:1156)). Phase D-to-E manifest re-check at line 1258.

**v46 mandatory digest** (S507, 2026-04-28). `SVC_DISTRIB_BEGIN` carries a 32-byte SHA-256 digest of the exact compressed PDCA archive bytes. Clients reject zero/missing digests at BEGIN and verify accumulated compressed bytes before decompress/extract at END. Mixed v45/v46 play rejected at the ENet auth handshake.

---

## UI

- **Mod Manager** ([port/fast3d/pdgui_menu_modmgr.cpp](../../port/fast3d/pdgui_menu_modmgr.cpp)) - browse, toggle, validate, delete. Populates via `assetCatalogIterateByType` and `modmgrGetMod*` accessors.
- **Modding Hub** ([port/fast3d/pdgui_menu_moddinghub.cpp](../../port/fast3d/pdgui_menu_moddinghub.cpp)) - 9 tool tabs at line 129: Mod Manager, INI Editor, Scale Tool, Mod Pack, Audio Mods, Skin Editor, Map Import, Nine-Slice Chrome (Menu Style), Font Mod.

---

## Property Handler DLL

[tools/pdmod_prophandler/](../../tools/pdmod_prophandler/) - Windows Shell extension. Implements `IInitializeWithStream` to surface `mod.json` fields (name, author, version) in Explorer details pane on `.pdmod` files. Standalone, registered via `regsvr32`. Not loaded by the game (zero `LoadLibrary / dlopen` in `port/src/`).

---

## Active invariants

Per [constraints.md](../constraints.md):

- **`mods/` tree depth cap of 2 levels** (S293). Discovery scanners recurse exactly one level into top-level entries with no manifest.
- **Reserved names at two layers**: scan walker + archive-path-trusted check.
- **Catalog registers ALL assets**; mod content extends the catalog through the same typed registration API as base content.
- **Mod enablement policy: default-enabled** (per [designs/modding/mod-enablement-policy.md](../designs/modding/mod-enablement-policy.md)). Mod-declared UI assets (themes today; skins, audio, weapons in future) enable on import unless the user opts out.
- **GL texture upload caps + cache-scoped teardown** (SP-15). Any mod-supplied PNG / image upload must clamp dimensions against `GL_MAX_TEXTURE_SIZE` and free via `glDeleteTextures` on cache teardown.

---

## What is done (per [audits/infrastructure-pillars-status-2026-04-27.md](../audits/infrastructure-pillars-status-2026-04-27.md) Section 4)

- Production `.pdmod` reader and writer with atomic rename.
- Two-tier asset registration (INI components + JSON content) converging on catalog.
- VFS LRU cache eliminates on-disk extraction during normal play.
- Mod scanning enforces reserved-name discipline at two layers.
- Network manifest carries SHA-256 per entry.
- Netdistrib distributes missing components with zlib chunking.
- v46 added mandatory archive digest at BEGIN (S507).
- UI is wired to real data, not stubs.
- Property handler DLL correctly scoped to Explorer integration only.
- 9-tab Modding Hub with full content authoring tools (S288-S314 era: Skin Editor, Map Import, Audio Mod, Nine-Slice Chrome, Font Mod).

---

## What is in flight

Lower-priority compared to Catalog Gate 3 + Input Cohorts 5-8. Items per the audit Section 4:

- **`.pdmod` cannot deliver INI-based components.** When a `.pdmod` mounts via `modVfsMount`, the JSON content path registers bodies/heads/arenas/audio. But the INI scanner ([assetcatalog_scanner.c](../../port/src/assetcatalog_scanner.c)) uses `fopen / stat / opendir`, expecting on-disk files. A `.pdmod` containing a `maps/` subdir with `map.ini` and geometry would not appear in the catalog from the archive. Maps and characters via `.pdmod` are folder-only today. Fix: extend `modmgrLoadMod` for archive mods to drive the INI scanner over archive contents (new helper `assetCatalogScanComponentsFromArchive`).
- **SHA-256 mismatch between archive and folder mods.** [modmgr.c:1331](../../port/src/modmgr.c:1331) calls `modArchiveSha256(archivePath, ...)` for archive mods (file-level digest). Folder mods compute SHA-256 over `mod.json` only at [modmgr.c:1074](../../port/src/modmgr.c:1074). `manifestCheck` at [netmanifest.c:2090-2093](../../port/src/net/netmanifest.c:2090) compares them as if equal, so client-folder vs server-archive (same content) always disagrees, triggering unnecessary `NEED_ASSETS`. Fix: unify both digests to `mod.json` bytes only.
- **NetDistrib hot-register skips `g_ModRegistry`.** [netdistrib.c:1200](../../port/src/net/netdistrib.c:1200) calls `assetCatalogRegister` directly. Component appears in catalog but `modmgrGetCount() / modmgrGetMod()` iterate `g_ModRegistry` which is not updated. Mod Manager UI does not show distributed mods until next launch. Fix: call `modmgrRescanDirectory` after `SVC_DISTRIB_END`.

---

## Known gaps

- **Dead legacy manifest serializer.** `modmgrWriteManifest / modmgrReadManifest` ([modmgr.c:2456-2554](../../port/src/modmgr.c:2456)) implement a custom binary format that predates `match_manifest_t / manifestBuildForHost`. No live netplay caller. Content-hash compare uses CRC32 of `id:version` string at [modmgr.c:2534-2543](../../port/src/modmgr.c:2534), weaker than the SHA-256 path. Audit and remove or wire.
- **`manifest_pure.c` is hand-synced.** [tests/manifest_pure.c:1-31](../../tests/manifest_pure.c:1) documents manual extraction with `@SYNC` line-number comments pointing into `netmanifest.c`. As `netmanifest.c` evolves (over 2000 lines), this drifts. Replace with compile-boundary approach: factor container/hash/diff/serialise into a TU that imports no globals; link both production and test against it.

---

## Active design references

- [designs/modding/pdmod-format.md](../designs/modding/pdmod-format.md) - unified mod format spec (folded from `pdmod-unified-mod-format.md` per Section 7.6 of the rebuild proposal).
- [designs/modding/mod-enablement-policy.md](../designs/modding/mod-enablement-policy.md) - default-enabled policy.
- [designs/modding/theme-bundle-and-per-agent-settings.md](../designs/modding/theme-bundle-and-per-agent-settings.md) - theme bundle plumbing + per-agent prefs sidecar.
- [designs/modding/forge-level-editor.md](../designs/modding/forge-level-editor.md) - in-game level editor (The Grid). Phase 0-1 implemented; future phases scoped.

---

## Where to look

- For catalog registration model: [pillars/catalog.md](catalog.md).
- For wire protocol, `SVC_DISTRIB_*`, `SVC_CATALOG_INFO` flow: [pillars/connectivity.md](connectivity.md).
- For mod-supplied themes/chrome/fonts: [pillars/menus.md](menus.md) Theme System section.
- For host vs client manifest discipline: [pillars/server.md](server.md).
