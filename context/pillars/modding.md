# Modding

> Component-based mod system. Typed `.pd*` content files plus `.pdmod` transport archives. JSON manifest + INI components. VFS over archive handles. Asset catalog is the single registration target. Network distribution with SHA-256 integrity. Reserved-name discipline at two layers.

---

## What it is

A mod is made of typed content units (`.pdweapon`, `.pdprojectile`, `.pdentity`, `.pdcharacter`, `.pdhead`, `.pdbody`, `.pdarena`, `.pdmesh`, `.pdanim`, `.pdsfx`, `.pdvoice`, `.pdsong`, `.pdui`, `.pdfont`, `.pdlang`, etc.) and can be distributed as a `.pdmod` archive (zip with `mod.json` manifest) or a folder under `mods/`. Each asset inside the bundle (map, character, skin, audio, etc.) is an independent component with its own `.ini` manifest (or JSON content entry in `mod.json`) and gets registered as an `asset_entry_t` in the catalog. Mods are not loaded as monolithic units; individual components are. `.pdmod` is the package/transport envelope for sharing and online delivery; the typed `.pd*` files remain the content organization surface.

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
- External-format assets are standard authored files such as `.gltf`, `.glb`, `.obj`, `.png`, `.tga`, `.wav`, `.mp3`, `.ogg`, `.ttf`, `.otf`, and `.tsv`. Authored content packages and external-format `.pdmod` archives must not contain `.bin`; `.bin` is legacy-only and private generated cache stays outside shipped archives. Typed `.pd*` packages are the preferred content-unit surface, while `.pdmod` is the sharing/online transport envelope.

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
MANIFEST_TYPE_PROJECTILE 10
MANIFEST_TYPE_ENTITY     11
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
- **External-format content authoring + `.pdmod` transport** (2026-05-19, c3809). Typed `.pd*` files remain the preferred content-unit family, but modder-facing asset payloads are standard files plus grouped `.ini` / `.tsv` metadata. Authored content packages and `.pdmod` transport archives must not contain `.bin` files; any engine-native binary data must be generated as private runtime cache from external sources. `.pdmod` is used for packaging, sharing, Public Mods, and online-required content delivery. See [designs/modding/external-format-pdmod-pipeline.md](../designs/modding/external-format-pdmod-pipeline.md).
- **Fully self-contained typed asset archives** (2026-05-21, c3812/c3814). Each typed `*.pdxxx` content unit embeds its authored dependency closure on disk. Duplicate textures, materials, audio, meshes, animation targets, nested typed archives, and equivalent payloads across archives are acceptable for sharing and modding; catalog build/runtime dedupe by SHA-256 and catalog identity handles RAM waste after ingestion. Reference-only fields are metadata, not closure, unless the referenced authored payload is also embedded in the archive or carried as a contained nested dependency archive.
- **Weapon graph asset direction** (2026-05-21, c3814). The authored weapon extension is `.pdweapon` only; `.pdwpn` is fully deprecated, was never released, and must be removed rather than accepted, aliased, migrated, or supported for compatibility. Weapon behavior is graph JSON compiled to deterministic runtime IR. Physical projectile and deployed/thrown entity behavior are first-class `.pdprojectile` and `.pdentity` catalog assets, and weapon archives must embed all authored dependency payloads needed to edit, share, clone, and load the weapon, including nested projectile/entity behavior payloads. `.pdentity` is behavior/archetype data and does not replace `ASSET_PROP`. Exact duplicate embedded payloads share by SHA-256 over canonical archive content after ingestion. The base behavior audit is in [designs/modding/base-weapon-behavior-coverage.md](../designs/modding/base-weapon-behavior-coverage.md), with the current 86-weapon parameter matrix in [designs/modding/base-weapon-parameter-matrix.md](../designs/modding/base-weapon-parameter-matrix.md), module parameter spec in [designs/modding/weapon-graph-module-parameters.md](../designs/modding/weapon-graph-module-parameters.md), and runtime cutover split in [designs/modding/weapon-graph-runtime-cutover-plan.md](../designs/modding/weapon-graph-runtime-cutover-plan.md).

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

- **Non-weapon self-contained archive closure done (2026-05-21, c3812-s8).** `.pdarena` embeds the corresponding `.pdscenario` under `scenario/`; `.pdhead` embeds `mesh.pdmesh`; `.pdbody` embeds `mesh.pdmesh` and optional `hand.pdmesh`; `.pdcharacter` refreshes nested body/head archives and marks `dependency_closure = embedded.v2`. `.pdmesh` extraction dedupes by `(filenum,hint)` rather than filenum alone so body/hand archive identities are not lost when the same source is also emitted for weapon hi/lo meshes. `Build/data/ntsc-final/chicago.zip` was refreshed and verified to contain `scenario/rooms.obj`, `scenario/setup.tsv`, and `scenario/scenario.ini`; the generated non-weapon archive audit found 0 missing closure entries across scenario-bearing arenas, heads, bodies, and characters. Weapon archive closure remains in `c3814`.
- **Blender-openable BG visual scenario payload done for non-weapons (2026-05-21, c3812-s9).** The previous neutral glTF/map_material placeholder is replaced by a proper BG visual display-list exporter. `.pdscenario` embeds `visual/scene.obj`, `visual/scene.mtl`, `visual/materials.tsv`, `visual/export_version.txt`, and decoded `visual/textures/*.tga`; `.pdarena` inherits the same payload under `scenario/`. Geometry comes from BG `G_VTX`, `G_TRI1`, and `G_TRI4`; materials come from C0/G_NOOP texture commands; textures are decoded from `textureslist`/`texturesdata`. `rooms.obj` remains the runtime/collision mesh, while `visual/scene.obj` is the plugin-free Blender import path with MTL texture paths relative to the `visual/` folder (`textures/...`). Old scenario archives without `visual/export_version.txt` are stale and regenerate. Clean extracted Chicago verification found 98 TGA textures in both embedded `.pdarena` and standalone `.pdscenario` forms.
- **External-format content + `.pdmod` transport pipeline (c3809) done 2026-05-20.** This supersedes the old "manifest.json + raw .bin payload" modder-facing shape while preserving typed `.pd*` files as the content-unit family. Typed `*.pdxxx` descriptors now point at standard authored files and grouped INI/TSV metadata; `.pdmod` is the bundle/transport envelope for sharing, Public Mods, and online-required content delivery. All c3809 subtasks are done: format contract, shared INI/archive scanner, metadata families, audio/music, UI/font/lang, models/maps/animations, packer/exporter integration, compatibility validation, Public Mods install, archive/folder digest alignment, and distributed-mod registry refresh. The hard boundary remains no authored `.bin` payloads; generated engine-native cache is private, readable, rebuildable, and never shipped inside content units or `.pdmod` archives.
- **Public Mods request-download enable policy done (2026-05-20, c3810).** Received `.pdmod` downloads still validate root `mod.json`, reject authored `.bin`, install into `mods/installed`, and refresh `g_ModRegistry`. If the sender is a friend, the installed mod is enabled and applied live by default. If the sender is not a friend, the mod remains disabled and the Social shell queues a modal so the user can choose `Enable now` or `Keep disabled` after download.
- **Typed `*.pdxxx` archive repair active (2026-05-20, c3812).** Mike corrected c3811's loose descriptor model: a `.pdcharacter`, `.pdarena`, `.pdanim`, etc. is itself a zip-openable typed asset archive. The archive root carries the readable descriptor (`character.ini`, `arena.ini`, `animation.ini`, etc.) plus authored source assets such as GLTF/GLB/OBJ, textures, audio, TSV, fonts, and supporting INI files as needed. Changing the extension to `.zip` should expose everything required to create a custom version of that asset. `.pdhead` and `.pdbody` remain lower-level dependency/runtime compatibility archives while the runtime still models those pieces separately. `.pdmod` remains only the sharing/Public Mods/online delivery transport wrapper; authored `.bin` payloads remain invalid.
- **Non-weapon base typed archive repair done (2026-05-21, c3812).** The ROM arena/head/body extractors now emit zip-openable `.pdarena`, `.pdhead`, and `.pdbody` archives with root descriptors plus compatibility `manifest.json`, and rewrite old plain-JSON outputs automatically. The already-zip non-weapon emitters now add their root descriptors too: `model.ini`, character `animation.ini`, `sound.ini`, `voice.ini`, `music.ini`, `ui.ini`, `font.ini`, `lang.ini`, and `scenario.ini`. The follow-up character extractor emits canonical `.pdcharacter` archives with `character.ini`, compatibility `manifest.json`, and nested `.pdbody`/`.pdhead` dependency archives, and scanner/packer/runtime suffix allowlists recognize `.pdcharacter`. The shared `.pdsfx`/`.pdvoice` extractor now writes decoded mono PCM16 `sample.wav` plus `sample.wav.sha256` instead of authored `sample.bin`, with descriptors/manifests referencing the WAV and retaining source-format provenance. The `.pdlang` extractor now writes editable `strings.tsv` plus `strings.tsv.sha256` instead of `data.bin`, decoded from the raw ROM language offset table. The `.pdfont` extractor now writes decoded `glyphs.pgm`, `metrics.tsv`, and `kerning.tsv` plus SHA-256 sidecars instead of authored `data.bin`. The `.pdsong` extractor now writes standard `sequence.mid` plus editable `sequence.tsv` event listings from the inflated N64 compressed-MIDI sequence, with SHA-256 sidecars and descriptors/manifests referencing those files instead of `data.bin`. Character `.pdanim` archives now expose `header.tsv` and `frames.tsv` instead of `frames.bin`. `.pdmesh` archives now promote the source `PD_MODELDEF`, traverse model display lists, and expose standard Wavefront `model.obj` plus `model.mtl` and SHA-256 sidecars instead of `geometry.bin` or a TSV byte table. `.pdscenario` archives now expose `rooms.obj`, `scenario.mtl`, `visual/scene.obj`, `visual/scene.mtl`, `visual/materials.tsv`, decoded `visual/textures/*.tga`, `tiles.tsv`, `pads.tsv`, `setup.tsv`, `mpsetup.tsv`, and `visual_segments.tsv` with SHA-256 sidecars instead of raw `geometry.bin`, `tiles.bin`, `pads.bin`, `setup.bin`, or `mpsetup.bin`. Descriptor-less, WAV-less, TSV-less, bitmap-font-file-less, MIDI/event-file-less, animation-TSV-less, OBJ-less, rooms-OBJ-less, or visual-export-marker-less existing zips are considered stale so the next extraction pass regenerates them. Weapon graph work is tracked separately by c3814.
- **Non-weapon extractor crash hardening done (2026-05-21, B-357/c3812).** First-launch `.pdmesh` extraction no longer feeds compressed or ROM-format model bytes into `modelPromoteOffsetsToPointers`. It RareZip-inflates, preprocesses as model/gun, promotes skeleton type, validates promotable offsets, and only then walks the node/display-list tree for `model.obj`. `.pdarena` / `.pdscenario` stage-file extraction now also inflates before tiles/pads/setup preprocessing. Both extractor passes keep preprocessor-dependent work serial because the preprocessors use process-global scratch state. Focused c3812 static tests pass at 229 assertions / 11 cases, the queued all-target build passes, and `boot_smoke` passes from a clean smoke install.
- **Extraction/fallback audit misses fixed (2026-05-21, B-358/c3812).** The Combat Sim crash audit proved the summary counters were not enough: core ROM/segment extraction reported `failed=0`, but archive validation and runtime logs still exposed loose-JSON weapon/inventory `.pdanim` files, no-triangle `.pdmesh` outputs, a raw menu HUD piece catalog miss, SP-manifest late-add patches during local Combat Simulator, and an SP-only random bot body. Fixes landed in the asset and match-start architecture: weapon/inventory `.pdanim` now emits ZIP archives with `animation.ini` / `manifest.json` / `opcodes.json`; `.pdmesh` display-list decoding now matches runtime GDL/vertex marker and `G_VTX` semantics; `FILE_GHUDPIECE` has a provider handle; local/offline Combat Simulator builds an MP manifest before stage change; and random bot bodies are filtered to MP-selectable rows. Latest isolated smoke reported ROM/segment failures at zero, `.pdmesh written=64 skipped=192 failed=0`, `.pdanim skipped=110 failed=0`, no `CATALOG.MISS`, no `MANIFEST-SP: late-add`, and no exception/fatal.
- **Weapon behavior graph asset design active (2026-05-21, c3814).** The canonical schema design is now in [designs/modding/weapon-behavior-graph-assets.md](../designs/modding/weapon-behavior-graph-assets.md), with the source-backed behavior audit in [designs/modding/base-weapon-behavior-coverage.md](../designs/modding/base-weapon-behavior-coverage.md), current 86-weapon parameter matrix in [designs/modding/base-weapon-parameter-matrix.md](../designs/modding/base-weapon-parameter-matrix.md), named module parameter spec in [designs/modding/weapon-graph-module-parameters.md](../designs/modding/weapon-graph-module-parameters.md), and runtime cutover split in [designs/modding/weapon-graph-runtime-cutover-plan.md](../designs/modding/weapon-graph-runtime-cutover-plan.md). Implemented so far: `.pdweapon`, `.pdprojectile`, `.pdentity`, nested catalog IDs, canonical SHA-256 payload sharing, graph JSON shape, validation gates, 86-weapon function inventory, behavior-family mapping, first physical payload candidates, current parameter matrix, weapon/projectile/entity module parameters, runtime mapping for Slayer rockets, grenades, proxy mines, mines, Dragon proxy behavior, thrown Laptop Gun, deployed Laptop Gun autogun behavior, concrete runtime implementation slices, the live `.pdweapon` emitter/scanner/walker/packer/example/test cutover, first-class `.pdprojectile`/`.pdentity` catalog/manifest metadata plumbing, shared graph archive helper APIs, base graph emission, graph validation, deterministic IR compilation, Debug Settings UI/config storage for the graph runtime toggle, the Mods > Weapons browser, and a partial template/save editor. `.pdprojectile` and `.pdentity` now have catalog enum values, descriptor templates, scanner/packer/distribution recognition, manifest type codes, UI listing/editing support, and projectile-to-entity dependency registration; `.pdentity` remains behavior/archetype data, not an `ASSET_PROP` replacement. `weapon_graph_archive` owns descriptor/text reads, root validation, derived nested projectile/entity IDs, duplicate-ID collision checks, canonical archive-content SHA-256 over sorted uncompressed entries, nested payload inventory scans/formatting, and base `.pdweapon` `nested_payloads.json` scaffolding. The base `.pdweapon` emitter now writes `base_weapon_graph_v1` graphs with named action modules and embeds generated `.pdprojectile`/`.pdentity` payload archives under `projectiles/` and `entities/`, with SHA-256 inventory rows and stale temporary legacy graph self-heal. `weapon_graph_runtime` validates schema/module/unit/catalog/cycle rules, rejects arbitrary script-like modules and ambiguous units, and compiles graph JSON to stable opcode IDs, sorted parameter blocks, `source_sha256`, and `ir_sha256`. Active next: `c3814-s15` held weapon runtime adapter using the Debug Settings toggle as the gameplay callsite gate. Partial now: held weapon IR registers during the weapon walker pass and feeds shared gameplay accessors plus first direct held shooting helpers; the Weapons tab can clone a selected weapon into a new `.pdweapon` mod with catalog refs, file imports, save-time graph validation, copied template payloads, `mod.json`, rescan, and enable. Not implemented yet: remaining held-weapon callsites, projectile runtime adapter, entity runtime adapter, true embedding of all selected catalog assets, projectile/entity file import affordances, saved custom weapon parity/hot-register coverage, and parity closure.
- **Base-content validate/rebuild pinned (2026-05-20, c3811-s3).** Base game content remains validated and regenerated by the startup self-heal path, not shipped as authored `.pdmod` examples. Static coverage pins the boot order: `catalogCacheVerifyRom`, file extraction, file SHA-256 verify/re-extract, segment extraction, segment SHA-256 verify/re-extract, `DATA INTEGRITY` aggregate report, then `romdataReleaseRom`. This keeps examples in `examples/modding/typed-pdxxx-basic` and keeps base content in the data integrity/rebuild lane.
- **Typed example discoverability done (2026-05-20, c3811-s4; wording corrected under c3812).** The Modding Hub Pack `.pdmod` tool now exposes the sample folder through `Use Sample Folder`, pre-fills output to `mods/typed-pdxxx-basic.pdmod`, and labels `.pdmod` output as sharing/Public Mods/online transport while the editable content lives inside the typed `.pdxxx` asset archives. `release.ps1` ships `examples/modding` into release packages. Static coverage pins the Hub surface and release inclusion.
- **Archive INI component scanning foundation landed (2026-05-19, c3809-s2).** `.pdmod` archives now have `assetCatalogScanComponentsFromArchive()`, shared memory INI parsing/writing, canonical descriptor recognition, archive-relative path qualification, VFS-backed load integration, and modmgr validation that marks archives with authored `.bin` payloads invalid. Next: broaden the descriptor fields/templates for each metadata family.
- **Grouped metadata-family INI support landed (2026-05-19, c3809-s3).** The INI parser keeps the first section as the asset type and allows later sections for organization. The scanner now supports external weapon/head/body/arena/animation metadata fields, commented templates via `modiniTemplateForKind()`, model/geometry/animation file references, body/head dependency lists, and animation-to-body reverse dependencies. Legacy JSON and `.pd*` walkers remain accepted.
- **Audio/music externalization landed (2026-05-19, c3809-s4).** External audio descriptors use `audio_category` plus standard `file_path` sources. SFX/voice templates point at WAV, music templates support OGG/MP3/WAV plus optional `midi_file` sidecars. Runtime WAV SFX and WAV/MP3/OGG mod music loaders now try `fsFileLoad` first, so archive-mounted audio can play through VFS without extracting into the mod folder.
- **UI/font/lang externalization landed (2026-05-19, c3809-s5).** UI descriptors support PNG/TGA texture files, font descriptors support TTF/OTF sources, and lang descriptors support UTF-8 `strings.tsv` banks. PDGUI catalog UI entries apply PNG/TGA and TTF/OTF paths through VFS-capable loaders, language banks load TSV data into the existing `langGet()` offset-table format, and network-distributed `ui.ini`/`font.ini`/`lang.ini` hot-registration keeps type and source metadata.
- **Models/maps/animations runtime backends landed (2026-05-20, c3809-s6 done).** `modasset_compiler` validates GLTF/GLB/OBJ sources, hashes source bytes loaded through `fsFileLoad`/VFS, and writes private readable `.pdmc` cache descriptors under `$S/mod-cache`. OBJ map/scenario sources normalize to `.pdmesh.json`, activate as catalog-owned `struct colmesh` data, and stage load merges loaded matching catalog meshes into `g_WorldMesh`. Model/head/body/weapon/prop GLTF/GLB/OBJ sources normalize to readable `.pdmodel.json` where applicable and activate as generated in-memory `modeldef` payloads with a root position node plus DL node; raw external sources are never handed to `modeldefLoadToNewFromHandle`, and `.gltf` sidecar binary buffers are rejected to keep authored `.bin` out of the new contract. Animation descriptors normalize to `.pdanimation.json`, activate as catalog-owned `ASSET_PAYLOAD_ANIMATION_CLIP` payloads, and install at the existing `animtableentry` plus header/frame byte-stream boundary. Static/empty GLTF/GLB clips and skeletal translation/rotation/scale channels are packed into the engine byte stream; unsupported weights or unsupported interpolation fail with clear validation errors instead of raw-source or `.bin` fallback, while legacy weapon `.pdanim` gunscript opcodes remain compatible. Loose folder mods scan the canonical external layout through `assetCatalogScanExternalLayoutFolder()` after `mod.json` registration, matching the archive descriptor registration path; editable fixtures cover folder and archive-entry layouts for OBJ arenas, embedded-data-URI GLTF heads, animation descriptors, skeletal channels, map sidecars, and no authored `.bin`. Live packed `.pdmod` fixture coverage exercises the production archive writer/reader plus VFS mount without extraction, and scanner templates expose optional `catalog_id`/`id` overrides for repeated canonical leaves such as weapon/character `idle` animations.
- **Packer/exporter integration landed (2026-05-20, c3809-s7 done).** `modpackPdmodFromFolder()` now validates typed `*.pdxxx` content descriptors plus compatibility external-layout folders before archive writing, rejects authored `.bin` payloads, generates missing commented INI templates for canonical descriptor families plus map/scenario sidecars, validates referenced source files and unsafe paths, and exposes detailed failure text through `modpackPdmodLastError()` for the Modding Hub packer UI. The in-memory `.pdmod` writer also rejects authored `.bin` entries. Voice/music templates now use category-specific defaults.
- **c3814-s15 held adapter progress (2026-05-21).** The `.pdweapon` walker now compiles/registers held IR during load, shared gameplay accessors read graph damage, impact force, fire-slot duration, numeric shoot sound, penetration, function flags, and max_rpm cadence, and direct held shooting helpers use graph-backed burst flags, ammo slot, spin-up/spin-down, muzzle flash flag, initial/max RPM, and ammo consumption behind the Debug Settings graph runtime toggle. Remaining held-weapon work: cooldown, trigger state, full hitscan execution, charge/release, beam, melee, specials, devices, presentation, reticles, overlays, zoom, model visibility, and parity tests. Legacy behavior remains default until parity is proven.
- **c3814-s20 Mods Weapons browser done (2026-05-21).** The Modding Hub has a Weapons tab that lists catalog weapon assets, marks Base vs Mod entries, shows catalog/model/runtime/archive details, and previews `weapon.ini`, `behavior.graph.json`, and `nested_payloads.json` when the `.pdweapon` archive is resolvable. The original browser slice was inspection-only for base entries; template/import/save work is now partially active under `c3814-s21` and `c3814-s22`.
- **c3814-s21/s22 weapon template/save progress (2026-05-21).** The Weapons tab now has a Template flow: `Use as Template` clones the selected base or mod weapon, catalog pickers select model/texture/animation/audio/projectile/entity refs, the in-engine file browser imports model/texture/animation/audio/graph files, edited graph JSON is validated before save, and `Save Weapon Mod` writes `mods/Weapons/<slug>/<slug>.pdweapon` with root descriptors, copied non-root template payloads, imported files, `mod.json`, mod rescan, and enable. Remaining: selected catalog assets that are not already copied from the template/import payload are stored as refs rather than embedded payloads; this is incomplete because the universal archive contract requires the saved `.pdweapon` to embed its authored dependency closure. Projectile/entity typed-archive file import buttons and stronger saved custom weapon hot-register/parity tests still need to land.
- **Compatibility + validation done (2026-05-20, c3809-s8; weapon extension corrected 2026-05-21 under c3814).** Static/build coverage pins typed content descriptors and the current `.pd*` walker matrix (`.pdweapon`, `.pdhead`, `.pdbody`, `.pdarena`, `.pdmesh`, `.pdanim`, `.pdsfx`, `.pdvoice`, `.pdsong`, `.pdui`, `.pdfont`, `.pdlang`, plus `.pdscenario`). Mike's 11:03 Build log validated boot, legacy `.pd*` registration, `.pdui` auto-emit/reload, and installed `.pdmod` discovery with zero `.bin` entries. Runtime smokes validate loose typed `*.pdxxx` folder content, the same typed content wrapped in real `.pdmod` transport, and installed Public Mods `.pdmod` discovery/load. GLTF head sources activate as modeldef payloads, OBJ arena sources activate as colmesh payloads, and GLTF skeletal animation sources activate as animation clip payloads through readable generated cache files. Public Mods received `.pdmod` downloads now validate root `mod.json`, reject authored `.bin`, install into `mods/installed`, refresh `g_ModRegistry`, and archive/folder mod comparison uses root `mod.json` digest bytes.

---

## Known gaps

- **Dead legacy manifest serializer.** `modmgrWriteManifest / modmgrReadManifest` ([modmgr.c:2456-2554](../../port/src/modmgr.c:2456)) implement a custom binary format that predates `match_manifest_t / manifestBuildForHost`. No live netplay caller. Content-hash compare uses CRC32 of `id:version` string at [modmgr.c:2534-2543](../../port/src/modmgr.c:2534), weaker than the SHA-256 path. Audit and remove or wire.
- **`manifest_pure.c` is hand-synced.** [tests/manifest_pure.c:1-31](../../tests/manifest_pure.c:1) documents manual extraction with `@SYNC` line-number comments pointing into `netmanifest.c`. As `netmanifest.c` evolves (over 2000 lines), this drifts. Replace with compile-boundary approach: factor container/hash/diff/serialise into a TU that imports no globals; link both production and test against it.

---

## Active design references

- [designs/modding/pdmod-format.md](../designs/modding/pdmod-format.md) - unified mod format spec (folded from `pdmod-unified-mod-format.md` per Section 7.6 of the rebuild proposal).
- [designs/modding/external-format-pdmod-pipeline.md](../designs/modding/external-format-pdmod-pipeline.md) - shipped external-format content plus `.pdmod` transport contract; standard files plus grouped INI/TSV metadata, no authored `.bin` payloads.
- [designs/modding/weapon-behavior-graph-assets.md](../designs/modding/weapon-behavior-graph-assets.md) - active `.pdweapon`, `.pdprojectile`, `.pdentity` graph asset schema.
- [designs/modding/base-weapon-behavior-coverage.md](../designs/modding/base-weapon-behavior-coverage.md) - active source-backed audit for base weapon behavior, physical projectile/entity payloads, and named graph module gaps.
- [designs/modding/base-weapon-parameter-matrix.md](../designs/modding/base-weapon-parameter-matrix.md) - current 86-weapon function parameter matrix for `.pdweapon` graph conversion.
- [designs/modding/weapon-graph-module-parameters.md](../designs/modding/weapon-graph-module-parameters.md) - named `.pdweapon`, `.pdprojectile`, and `.pdentity` graph modules and parameters.
- [designs/modding/weapon-graph-runtime-cutover-plan.md](../designs/modding/weapon-graph-runtime-cutover-plan.md) - runtime cutover slices from `.pdwpn` removal through weapon/projectile/entity IR parity.
- [designs/modding/mod-enablement-policy.md](../designs/modding/mod-enablement-policy.md) - default-enabled policy.
- [designs/modding/theme-bundle-and-per-agent-settings.md](../designs/modding/theme-bundle-and-per-agent-settings.md) - theme bundle plumbing + per-agent prefs sidecar.
- [designs/modding/forge-level-editor.md](../designs/modding/forge-level-editor.md) - in-game level editor (The Grid). Phase 0-1 implemented; future phases scoped.

---

## Where to look

- For catalog registration model: [pillars/catalog.md](catalog.md).
- For wire protocol, `SVC_DISTRIB_*`, `SVC_CATALOG_INFO` flow: [pillars/connectivity.md](connectivity.md).
- For mod-supplied themes/chrome/fonts: [pillars/menus.md](menus.md) Theme System section.
- For host vs client manifest discipline: [pillars/server.md](server.md).
