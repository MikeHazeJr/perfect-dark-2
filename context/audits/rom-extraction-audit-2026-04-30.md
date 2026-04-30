# ROM Extraction Audit (2026-04-30)

> Two-phase audit of how PD2 extracts content from a user-supplied N64 ROM, plus a research-and-compare pass against the wider N64 / classic-game decomp ecosystem, plus a third phase of concrete recommendations incorporating Mike's directive on per-asset-class file extensions and the `base/` vs `data/` directory split.

Methodology gates per `context/procedures.md`: possibility framing on subjective judgments, file:line + URL evidence for every claim, no em-dashes, no code shipped in this session, recommendations only.

---

## Executive summary

**Headline architectural principle (Mike, 2026-04-30):** ROM is a one-time bootstrap input. Extracted base content is the runtime source of truth. The catalog routes through extracted `.pdXXX` files plus mod overlays; ROM offsets and decoders only live inside the bootstrap extractor.

Today PD2 partially satisfies this principle in one place (`pdguiThemeExtractRomTextures` writes UI textures to disk on first launch and the runtime reads from there) and violates it everywhere else (the runtime ROM model in `port/src/romdata.c` keeps the full 32 MB ROM mapped and re-touches it on every gameplay file load). The `tools/extract` Python script handles build-time / developer-side extraction; there is no end-user runtime extractor in the SoH / Starship style.

Three architectural mismatches the audit surfaces:

1. **Directory vs archive drift.** The runtime UI texture extractor writes loose `.tga` / `.png` / `.9slice.json` files into `mods/base-ui/textures/`, while the modding pipeline migrated the rest of `base-ui` into a single ZIP archive (`mods/base-ui.pdmod`). The archive does not contain the extracted textures; they live alongside it on disk.
2. **End-user vs developer workflow conflation.** `tools/extract` (frozen upstream since 2022-12-04) runs only on developer machines. End users get C-side ROM parsing at startup with hardcoded per-region offsets and no validation feedback beyond a "wrong ROM" fatal.
3. **No bundled output format aligned with mods.** Build-time extracts land as flat directories of raw `.bin` files plus JSON manifests. Mike's emerging `.pdwep` / `.pdui` / `.pdmesh` per-asset-class extension taxonomy plus base-vs-mod schema symmetry directive is a PD2 innovation that does not have a direct precedent in the surveyed N64 decomp ecosystem (closest: SoH's libultraship loose-files-overlay-OTR).

Recommendations cascade from the architectural principle:

- Adopt the `.pdXXX` per-asset-class extension taxonomy. One canonical schema per extension. Same schema for extractor output and mod-tool output (round-trip clean).
- Add a `data/` directory tier alongside `base/` and `mods/` for BYOR-extracted runtime content. Catalog precedence: `mods/` then `data/` then `base/`.
- Realign the existing UI texture extractor to emit `data/ui/pd-original.pdui` (a single ZIP archive structurally identical to a modder-authored `.pdui`).
- Migrate runtime asset loading from "live-mapped ROM" to "first-launch extract then read disk" one asset class at a time using the same Catalog Gate cadence the F1-F13 weapons work used.
- Populate the SHA-256 known-good ROM hash table that already exists at `port/src/romdata.c:227-246` (currently `NULL`-only).

Stop conditions: none triggered. PD2 has functional extraction for what runtime actually needs today (audio and meshes are loaded directly from in-memory ROM via segment / file pointers, no extraction step required); the only runtime asset-to-disk extraction is for UI textures, and that path works.

---

## Phase 1: Functional audit of PD2's existing extraction

### 1.1 Architecture overview

PD2 has two distinct ROM-extraction layers:

- **Build-time / developer-facing** (Python tools): `tools/extract`, `tools/extract-segment`, `tools/mktextures`, `tools/assetmgr/mk*`. These run on a developer machine to reconstruct asset trees from a ROM, producing inputs for the build (`src/assets/<romid>/`) and reference dumps (`extracted/<romid>/`). These never run on an end user's machine.
- **Runtime / end-user-facing** (C/C++ in the binary): `port/src/romdata.c` loads the full ROM into memory once at startup. `port/src/preprocess/*` runs format conversion at load time. `port/fast3d/pdgui_theme.cpp` runs the ONE runtime extract-to-disk path for ImGui UI textures.

Because the ROM is held in memory live for the whole process, almost no runtime "extraction" is needed: gameplay code reads from `g_RomFile` plus already-converted segment tables. The "extraction to disk" pattern only applies where the consumer wants files (ImGui's image loader, mod authors, future tooling).

### 1.2 Build-time extractor: `tools/extract` (Python)

Single-file Python extractor at `tools/extract`, 700 lines, frozen upstream since 2022-12-04 (verified via Phase 2 web research: https://github.com/fgsfdsfgs/perfect_dark/blob/port/tools/extract). Writes to two destinations:

- `extracted/<romid>/` via `write_extracted()` at `tools/extract:292`. Build inputs not committed back.
- `src/assets/<romid>/` via `write_asset()` at `tools/extract:296`. Committed-back content used by the build.

Per-asset extraction methods documented at `tools/extract:18` (`extract_all`):

| Asset | Method | Input | Output |
|---|---|---|---|
| Animations | `extract_animations` (`tools/extract:45`) | Walks `animations` segment offset table | `src/assets/<romid>/animations/<idx>.bin` |
| Audio (SFX/seq) | `extract_audio` (`tools/extract:62`) | `sfxctl`, `sfxtbl`, `seqctl`, `seqtbl`, `sequences` segments | `extracted/<romid>/sfx.{ctl,tbl}`, `seq.{ctl,tbl}`, `sequences/<name>.seq` |
| BG / props / chrs / guns / lang / setup | `extract_files` (`tools/extract:98`) | File table walk via offset prefix character (`A=audio`, `C=chrs`, `G=guns`, `L=lang`, `P=props`, `U=setup`, `.seg=bgdata`) | Mix of `src/assets/<romid>/files/<cat>/...` and `extracted/<romid>/files/<cat>/...` |
| Fonts | `extract_fonts` (`tools/extract:226`) | 10 font segments (`bankgothic`, `zurich`, `tahoma`, `numeric`, `handelgothic*`, `ocra*`, `jpn*`) | `src/assets/<romid>/fonts/<name>.bin` |
| Textures | `extract_textures` (`tools/extract:251`) | Texture segment + descriptor table | `src/assets/<romid>/textures/<idx>.bin` (raw, not decoded) |
| Texture config | `extract_textureconfig` (`tools/extract:268`) | `textureconfig` segment | `extracted/<romid>/textureconfig.bin` |
| MP configs / strings | `extract_mpconfigs`, `extract_mpstrings` (`tools/extract:200`) | `mpconfigs` segment | `extracted/<romid>/mpconfigs.bin`, `mpstrings<lang>.bin` (E/J/P/G/F/S/I) |
| Firing range | `extract_firingrange` (`tools/extract:217`) | `firingrange` segment, 0x1550 bytes | `extracted/<romid>/firingrange.bin` |
| Copyright / accessing pak | `extract_copyright`, `extract_accessingpak` (`tools/extract:38`, `:90`) | `copyright` segment | `extracted/<romid>/copyright.bin`, `src/assets/<romid>/accessingpak.bin` |
| Bootloader / preamble / lib / game / garbage | various (`tools/extract:169-249`) | Hardcoded ROM offsets | `extracted/<romid>/{bootloader,preamble,lib,game,garbage}.bin` |
| Get-it-tile (ge_intro) | `extract_getitle` (`tools/extract:272`) | `textureconfig + 0xb50`, 0x65d0 bytes | `extracted/<romid>/getitle.bin` |

Decompressor: `decompress()` at `tools/extract:280`. Asserts 0x1173 zlib header magic, raises AssertionError on mismatch (developer-side fail-loud).

ROM version table: `vals[]` keyed by `['ntsc-beta','ntsc-1.0','ntsc-final','pal-beta','pal-final','jpn-final']` at `tools/extract:321`. No SHA validation. No friendly error on unknown ROM.

Conservative-write semantics: `write()` at `tools/extract:300` skips overwriting an existing file with different content; emits warning text only. This protects committed-back content but masks a corrupt-input failure mode.

### 1.3 Build-time compilers: `tools/assetmgr/mk*`

Six Python scripts at `tools/assetmgr/`. Driven by CMake's `generate_asset_headers()` macro (CMakeLists.txt:575-580):

- `mklang` reads `src/assets/<romid>/lang/<stage>.json`, emits language string headers
- `mkpads` reads `src/assets/<romid>/pads/<stage>.json`, emits collision pad headers
- `mktiles` reads `src/assets/<romid>/tiles/<stage>.json`, emits stage tile headers
- `mkanims` reads `src/assets/<romid>/animations.json` and `animations/*.bin`, emits animation headers
- `mksequences` reads `src/assets/<romid>/sequences.json`, emits sequence headers
- `mktextures` reads `src/assets/<romid>/textures.json` (driven by `textures.py` listing in the same dir, see `src/assets/ntsc-final/textures/textures.py`), emits texture headers and binary bundles

Output goes to `src/generated/<romid>/`, included in the binary at compile time. This is the canonical "ROM data baked into the executable" path.

`tools/assetmgr/assetmgr.py` is a shared library imported by each `mk*` script; not invoked directly.

### 1.4 Runtime ROM loading: `port/src/romdata.c`

Single source of truth for runtime ROM access. Key entry points:

- `romdataInit()` at `port/src/romdata.c:601` runs once at startup. Sequence:
  1. `romdataLoadRom()` at `:366` reads `data/pd.<romid>.z64`, validates size (32 MB) and header (`NPDE` / `NPDP` / `NPDJ`), inflates the compressed data segment via `rzipInflate()` to a heap-allocated `romDataSeg`, and logs SHA-256 (`romdataVerifyRomHash()` at `:248`).
  2. Iterates `romSegs[]` calling `romdataInitSegment()` at `:446` for each. Each segment either points into `g_RomFile` at the version-keyed offset, or loads from `data/segs/<segname>` if a replacement exists.
  3. `romdataInitFiles()` at `:535` reads the file offset table from the data segment (offset `ROMDATA_FILES_OFS`), populates `fileSlots[]` with `(data, size, name)` tuples.

Segment table: 24 segments declared by `ROMSEG_LIST()` macro at `port/src/romdata.c:121-148`. Each row has NTSC / PAL / JPN offsets and an optional preprocess function:

| Segment | Preprocess function |
|---|---|
| `fontjpnsingle`, `fontjpnmulti`, `fontjpn` | `preprocessJpnFont` (`port/src/preprocess/segfonts.c`) |
| `animations` | `preprocessAnimations` |
| `mpconfigs` | `preprocessMpConfigs` |
| `mpstrings[E,J,P,G,F,S,I]`, `firingrange`, `texturesdata`, `copyright` | none (raw access) |
| `fonttahoma`, `fontnumeric`, `fonthandelgothic[sm,xs,md,lg]` | `preprocessFont` |
| `sfxctl`, `seqctl` | `preprocessALBankFile` (`port/src/preprocess/segaudio.c`) |
| `sfxtbl`, `seqtbl` | none (raw sample tables) |
| `sequences` | `preprocessSequences` |
| `textureslist` | `preprocessTexturesList` |

File preprocess functions, dispatched via `g_LoadType` at file load (`port/src/romdata.c:180-189`):

| Load type | Function | File category |
|---|---|---|
| LOADTYPE_BG | none (loaded in parts via `filebg.c`) | bg_*.seg |
| LOADTYPE_TILES | `preprocessTilesFile` | bg_*_tilesZ |
| LOADTYPE_LANG | `preprocessLangFile` | L*Z |
| LOADTYPE_SETUP | `preprocessSetupFile` | U*Z |
| LOADTYPE_PADS | `preprocessPadsFile` | bg_*_padsZ |
| LOADTYPE_MODEL | `preprocessModelFile` | C*Z, P*Z |
| LOADTYPE_GUN | `preprocessGunFile` | G*Z |

These are not extraction in any meaningful sense: they read N64-endian / N64-pointer data in place and rewrite it as host-endian / host-pointer data in a freshly-allocated buffer (`sysMemZeroAlloc` then `convertX()` walking pointer markers via `port/src/preprocess/common.c:6` `ptrAdd()` + `ptrFind()`). The buffer replaces the ROM-pointer slot; no disk write.

Catalog routing: `romdataFileLoad()` at `:691` consults `catalogResolveFile()` first. Three branches:
- `is_mod_override` and `r.path` set: load from disk path (mod override).
- `r.catalog_id >= 0`: load from ROM (cataloged base game asset).
- `r.catalog_id < 0`: load from ROM (uncataloged, legacy fallback).

The mod-override branch at `:710` is the only path where a disk file replaces ROM data at runtime.

### 1.5 Runtime extraction: `pdguiThemeExtractRomTextures()`

The single runtime extract-to-disk surface. At `port/fast3d/pdgui_theme.cpp:2405`. Triggered by `pdguiThemeCheckExtract()` at `:3162` which fires once per process from `pdguiRender()` (`port/fast3d/pdgui_backend.cpp:738`).

Trigger conditions (`pdgui_theme.cpp:3162-3296`):
- `s_baseUiTexturesExist()` returns false (any of 13 expected TGAs missing in `mods/base-ui/textures/`), OR
- `--extract-ui-textures` CLI flag (manual re-extract), OR
- `--generate-modern-ui` CLI flag (procedural alternate theme generation)

Source: `g_TexGeneralConfigs[]` is the in-memory texture descriptor array populated by `texReset()`. The extractor calls `texLoadFromConfig()` per index (`pdgui_theme.cpp:2422-2428`) to ensure ROM bytes are decompressed into the texture cache. Then for 14 indices (`pdgui_theme.cpp:2419` `k_IndicesToLoad[]`) it dispatches by GBI format:

- `PD_G_IM_FMT_RGBA` 16b: `decodeRgba16(src, w, h, dst)` (color5551 to RGBA32)
- `PD_G_IM_FMT_RGBA` 32b: direct `memcpy`
- `PD_G_IM_FMT_IA` 16b: `decodeIa16`
- `PD_G_IM_FMT_IA` 8b: `decodeIa8`
- `PD_G_IM_FMT_IA` 4b: `decodeIa4`
- Anything else: skipped with warning

Output: per texture, three files (`pdgui_theme.cpp:2525-2552`):

- `mods/base-ui/textures/<name>.tga` (uncompressed 32-bit, top-down) via `s_writeTga` at `:2369`
- `mods/base-ui/textures/<name>.png` via `s_writePng` at `:2205` (uses miniz)
- `mods/base-ui/textures/<name>.9slice.json` (default 16-pixel inset definition) via `s_writeNinesliceJson` at `:2344`

Plus `mods/base-ui/mod.json` (a hand-rolled JSON literal at `:3201-3239`).

Procedural fallback pass: `pdgui_theme.cpp:2563-2632`. For any of 14 expected textures whose ROM extraction failed (zero dims, unsupported format, or ROM not loaded), generate a procedural substitute via `s_generateProceduralTexture("noise_sm" / "solid" / "haze" / etc.)` and write a TGA. This gives the theme system a valid file even when ROM extraction breaks, preventing a missing-texture cascade.

Failure modes:
- Expected count mismatch logged with `mask=0x...` bitfield naming each missing file (`pdgui_theme.cpp:3187-3191`)
- Re-runs extraction; if textures are still missing afterward, logs `STILL MISSING: <path>` per file (`pdgui_theme.cpp:3267-3272`)
- Reloads theme via `s_ThemeLateInitDone = false; pdguiThemeLateInit();` (`:3278-3279`) so the texture cache picks up the new files

Test coverage: zero. No `pd-tests` cases exercise `pdguiThemeExtractRomTextures` or `pdguiThemeCheckExtract`. Verified by absence in `context/pillars/tests.md` and grep of test source.

### 1.6 Procedural generators

Three procedural-asset generators, all in `port/fast3d/pdgui_theme.cpp`, none of which read from ROM:

- `s_generateProceduralTexture()` (referenced from `:2602`) generates noise / haze / solid pixel buffers. Used as fallback when ROM extraction fails.
- `s_generateModernUiTextures()` at `:2638` generates 4 alternate UI textures for the `pd-modern-ui` mod. Triggered only by `--generate-modern-ui`.
- `pdguiChromeInitializeBaseMod()` (called from `:3295`) and `s_generateChromeFrameBgra()` at `:2856` generate the 64x64 nineslice chrome frame procedurally. Comment at `:2845` notes the eventual goal: "Replace with real ROM texture extraction once the specific ROM addresses for PD's dialog chrome source art are identified." Currently a synthetic reconstruction.

These are not extraction; they are deterministic synthesis. Worth noting because the comment at `pdgui_theme.cpp:2845` flags one place where future ROM extraction work could replace procedural output with authentic source art.

### 1.7 Architectural mismatches surfaced by the audit

**1. Runtime extractor writes to a directory; mod system reads from an archive.**

Evidence:
- `pdgui_theme.cpp:2525` writes to `mods/base-ui/textures/<name>.tga`
- `mods/base-ui.pdmod` exists (4420 bytes, ZIP archive) per `context/audits/pdmod-verification-matrix-2026-04-25.md:51`
- ZIP listing of `mods/base-ui.pdmod` shows ONLY `mod.json` + `themes/theme_*.json` (7 themes). No textures inside the archive.
- `mods/base-ui/` directory does not exist on a clean checkout.
- `mods/base-ui.legacy_backup/` exists but holds only `mod.json` + `themes/`, no textures.

Effect: on first launch the extractor creates `mods/base-ui/` and writes 14 TGAs + 14 PNGs + N nineslice JSONs there. The `.pdmod` archive does not get those textures. The VFS layer (`port/src/modvfs.c`) intercepts `fsFileLoad("mods/base-ui/textures/...")` first against the archive (miss, not present) then falls through to the on-disk directory (hit if extracted). Both paths exist alongside each other.

This works but it is fragile: a user who deletes `mods/base-ui/` thinking the `.pdmod` is canonical will lose their textures until the next launch re-extracts. A user who edits a TGA on disk will see edits applied (because disk hits before archive), but if textures were ever moved into the archive, disk edits would be silently overridden.

**2. Modder-friendly output format, but only one mod.**

The runtime extractor produces a complete, self-describing `base-ui` mod with `mod.json`, `textures/*.tga`, `textures/*.9slice.json`, ready for the user to edit and ship. This is a good idea applied to one place. None of the other ROM-derived assets (audio, gun models, character models, stage geometry, animations, fonts) get the same treatment at runtime.

**3. Dev-tool extractor in an end-user runtime.**

`tools/extract` is a Python script invoked by developers. End users never run it. The C-side runtime parsing in `romdata.c` is the closest thing PD2 has to an end-user extractor and it does not extract; it parses-in-place. There is no SoH-style "first launch detected, here is your ROM, generating asset cache, please wait" UX. Mike's directive to introduce `data/ui/pd-original.pdui` is the seed of an end-user runtime extractor.

**4. ROM hash validation is implemented but disabled.**

`port/src/romdata.c:227-246` declares per-region SHA-256 known-good hash tables but they are commented out (the array is `{ NULL }`). The ROM is logged but not validated. A wrong-version ROM with a matching size and header (`NPDE`/`NPDP`/`NPDJ`) will load and silently corrupt offsets that drift between sub-versions. The Phase 2 research shows hash-keyed validation is universal in modern PC ports; PD2 has the scaffolding and chose not to populate it.

**5. CLI extract flags are documented in code but not in any user-visible help.**

`--extract-ui-textures` and `--generate-modern-ui` exist (`pdgui_theme.cpp:3284-3290`). They are not listed in `--help` output (verified by absence of these strings outside `pdgui_theme.cpp` and a few archived design docs). End users who break their `mods/base-ui/textures/` directory have no manual recourse other than launching the game and waiting for auto-extraction to fire on the missing-files mask.

---

## Phase 2: Research and compare

The N64 / classic-game decomp community has converged on two dominant patterns for ROM-derived content extraction. PD2 currently sits inside Pattern A (build-time, developer-facing) but is gradually growing Pattern B surfaces (the `pdguiThemeExtractRomTextures` runtime path is one). Mike's `.pdwep` / `.pdui` format direction is a third innovation that does not have a direct precedent in the surveyed projects.

### 2.1 Pattern A: build-time decomp extractors (developer-only)

Trigger is `make` or a shell driver script. Output is a flat tree of decoded files plus JSON / XML manifests, consumed as build inputs. Failure mode is a developer-side error (assertion failure, missing-file warning).

Representative projects:

- **`fgsfdsfgs/perfect_dark`** (PD2's upstream parent): Single `tools/extract` Python script, frozen since 2022-12-04. Hardcoded per-region offset table. Hand-written per-asset extraction methods. Output split between `extracted/<romid>/` (build inputs) and `src/assets/<romid>/` (committed-back). PD2 mirrors this layout exactly. Source: https://github.com/fgsfdsfgs/perfect_dark/blob/port/tools/extract.
- **`zeldaret/oot`**: Two-stage. First `tools/decompress_baserom.py` does Yaz0/zlib decompression and recomputes IPL3 CIC checksum. Then `tools/assets/extract/__main__.py` walks XML resource descriptors using an internal `extase` framework plus `n64texconv` for textures. 10 supported versions, each with its own `baseroms/<ver>/checksum.md5`, `segments.csv`, `config.yml`. Per-asset handlers in `z64_resource_handlers.py`. Source: https://github.com/zeldaret/oot/tree/main/tools.
- **`zeldaret/mm`**: Uses the C++ tool **ZAPD** (Zelda Asset Processor) at `tools/ZAPD/ZAPD.out`, driven by per-room and per-scene XML descriptors. Output tree at `extracted/<version>/`, with `assets/audio/{samples,samplebanks,soundfonts,sequences}` as the canonical layout the rest of the build keys off of. Source: https://github.com/zeldaret/mm.
- **`n64decomp/sm64`**: `extract_assets.py` reads a single `assets.json` manifest mapping filename to ROM offset, length, format, and version-list. Supports `jp/us/eu/sh/cn`. Uses a numeric `new_version` revision counter and a `.assets-local.txt` cache to skip re-extraction when the manifest has not changed. Per-asset format dispatch by file extension (`.aiff`, `.m64`, `.rgba16.png`, etc.). Sister script `--clean` reverses extraction. Source: https://github.com/n64decomp/sm64/blob/master/extract_assets.py.
- **`n64decomp/mk64`**: `tools/new_extract_assets.py` plus `tools/blender/extract_models.py` (Blender CLI for course geometry). Hybrid: scripted asset extraction plus Blender-based 3D extraction. Now also calls **Torch** (the shared HarbourMasters extractor library). Source: https://github.com/n64decomp/mk64.
- **`n64decomp/banjo-kazooie`**: `tools/bk_asset_tool` and `tools/bk_rom_compressor` (Rare's custom rzip codec, same family PD uses). splat-driven, very bespoke per-game. Source: https://github.com/n64decomp/banjo-kazooie/tree/master/tools.

Common traits across Pattern A: build-time only, treats a missing or wrong ROM as a developer error (not user-facing), tolerates Python and ad-hoc decoders, no UI, no end-user error messaging, no validation of extracted output beyond fail-on-assertion semantics.

### 2.2 Pattern B: runtime first-launch extractors (end-user facing)

The Harbour Masters family invented this for PC ports of N64 decomps and it has become the reference design. Trigger is the game executable launching, detecting a missing archive next to the binary, and prompting the user for their ROM via a native file picker. Output is a single archive container.

Representative projects:

- **Ship of Harkinian** (OoT): `soh/soh/Extractor/`. SHA1 hash table of supported ROMs (NTSC 1.0/1.1/1.2, PAL 1.0/1.1, GC, MQ). On hash match, runs `OTRExporter` over the ROM. Status notification: "Processing OTR" then "OTR Successfully Generated". Output: `oot.o2r` (ZIP-based) or `oot.otr` (legacy MPQ-based). Source: https://github.com/HarbourMasters/Shipwright/tree/develop/soh/soh/Extractor.
- **2 Ship 2 Harkinian** (MM): same engine, same pattern. MM-specific hashes published at https://2ship.equipment/ and `docs/supportedHashes.json`. Source: https://github.com/HarbourMasters/2ship2harkinian.
- **Starship** (Star Fox 64), **Ghostship**, **SpaghettiKart** (MK64): all use a near-identical [`GameExtractor.cpp`](https://github.com/HarbourMasters/Starship/blob/main/src/port/extractor/GameExtractor.cpp), a SHA1 to friendly-name `mGameList` map, and `Companion::Init(ExportType::Binary)` to dispatch to **Torch** (the shared extractor library, ex-`Companion`). EU/JP versions can layer voice replacement onto a US base.
- **Decoder library**: lives in **libultraship** plus **Torch** / **Companion**. GBI display-list translation, RGBA16/CI4/CI8/IA16 texture decoders, soundfont/sequence parsers, MPQ/ZIP archive writers. **fast64** (Blender plugin) and **retro** (community OTR/O2R packer) round out the modder ecosystem. Source: https://github.com/Kenix3/libultraship.

Failure modes are user-facing: unsupported hash produces a modal saying "ROM not recognized"; corrupt produces a checksum-mismatch dialog; missing produces a file-picker re-prompt. Switch ports require a PC run first to generate the `.o2r`, then the file is copied to SD.

Common traits: runtime, GUI prompts, container-archive output, hash-validated, all decoders embedded in the game binary (not Python-side), end-user-grade error UX.

### 2.3 Non-N64 parallels

Three notable approaches that do not transcode at all:

- **OpenRCT2**: detects an existing RCT2 install path or prompts. Loads `.DAT` objects directly at runtime through its `ObjectRepository`. No transcoding, no archive synthesis. Closer to ScummVM than to SoH.
- **ScummVM**: per-engine MD5 / file-table detectors, no transcoding. Reads original game data files in place. "First-launch detection" is signature matching, not extraction.
- **fheroes2** (HoMM2): points at a HoMM2 install directory, validates with file-presence plus checksums. No transcoding.

Lesson for PD2: not every BYOR project has to transcode. PD2's current `g_RomFile`-in-memory design is closer to ScummVM's "read original files in place" than to SoH's "transcode to container archive." That's a defensible architecture.

The encoder side: **libdragon** (`DragonMinded/libdragon/tools/`) standardizes the *forward* asset pipeline (`mksprite`, `mkfont`, `mkasset`, `mkdfs`, `audioconv64`) so homebrew authors generate engine-native blobs at build time. No ROM extraction concept; it is the encoder side of the same formats N64 decomps decode. PD2's `tools/assetmgr/mk*` family is a smaller-scale equivalent.

### 2.4 Extension and format conventions across community

PD2's emerging `.pdwep` / `.pdui` / `.pdmesh` taxonomy slots into a third axis the surveyed community has not converged on. Surveying the conventions:

| Convention | Used by | Examples | Trait |
|---|---|---|---|
| Format-extension (file shows underlying data shape) | OoT decomp, MM decomp, SM64 decomp, MK64 decomp, libdragon | `.aiff`, `.m64`, `.rgba16.png`, `.bin`, `.json`, `.xml`, `.c` | Polyglot; reader knows what to do from the extension; one extension per data shape regardless of asset class |
| Container-archive-extension (single file holds many assets) | Ship of Harkinian, 2S2H, Starship, Ghostship, SpaghettiKart | `.otr` (MPQ-based, legacy), `.o2r` (ZIP-based, current) | Monolithic per game; index inside; opaque without the right tool |
| Asset-class-extension (file shows what content is for) | PD2 (emerging: `.pdwep`, `.pdui`, `.pdmesh`, `.pdmod`) | `.pdwep`, `.pdui`, `.pdmod`, `.pdbase` | Self-documenting at the directory level; loader dispatches by extension |

PD2's emerging convention is closer to game-engine-native pipelines (Unity `.prefab`, Unreal `.uasset`) than to either decomp pattern. The advantage: a glance at `base/weapons/weapon_farsight.pdwep` tells a modder exactly what they are looking at, what other files in the tree are similar, and what tool they need. The disadvantage: the loader has to register every extension; new asset categories need new code paths; the parser cannot multi-purpose a single shape across asset classes.

The community's `.otr` / `.o2r` archives are container-formats internally indexed; extracting one yields a tree of format-extension files. Mike's `.pdui` archive at `data/ui/pd-original.pdui` is in the same spirit (a single file containing many assets) but at a finer granularity (per asset class, not per game).

### 2.5 Base-vs-mod schema symmetry across community

Surveying whether other decomps maintain "one canonical schema for both base content and mods":

- **Ship of Harkinian / Starship**: yes, by construction. The OTR / O2R format is the only format. Base extraction produces an OTR; mods produce loose-file overlays or another OTR; both go through libultraship's `ResourceManager` which dispatches by virtual path. A modder authoring a custom enemy creates exactly the same shape libultraship reads from the base OTR. Reference: https://github.com/Kenix3/libultraship.
- **OoT decomp / MM decomp**: partial symmetry. The decomp's *source tree* (XML descriptors plus binary blobs in `assets/`) is what mods author against, but the build pipeline transforms it into a different runtime shape that mods do not see. So base XML and mod XML look the same in dev workflow, but at runtime the binary is monolithic and mods are not a first-class concept (mods = ROM patches).
- **SM64 decomp**: low symmetry. Base assets are auto-extracted at build time and live in `assets/` as PNGs / m64s / aiffs. Mods authoring custom assets use the same shapes (PNG textures, m64 sequences) but the build pipeline is a strict input format; runtime is just compiled C, no overlay system. "Modding" is rebuilding from a forked tree.
- **OpenRCT2**: full symmetry, by construction. Mods are `.DAT` objects, and so are base assets. The runtime makes no distinction beyond load order.

PD2's principle from Mike's third directive (one canonical shape across base and mods, same parser and loader) lines up most closely with the SoH and OpenRCT2 models. The OoT / SM64 build-time-transform model is the opposite: source format and runtime format are different shapes, mods author against the source side, and there is no real overlay at runtime.

PD2 already has the catalog routing layer (`port/src/romdata.c:709` `catalogResolveFile()`) implementing precedence (mod overrides win, then base ROM). Extending that to `data/`-extracted base content plus `mods/` mod overlays in the same format is a natural fit.

### 2.6 Concrete observations for PD2

- **`tools/extract` upstream is frozen.** Last touched 2022-12-04 (`Disassemble graphics microcode`). PD2 has not missed extractor changes from `fgsfdsfgs/perfect_dark`. PD2's local copy mirrors upstream byte-for-byte (verified by Phase 2 web fetch).
- **PD2's runtime ROM-in-memory model differs from both Pattern A and Pattern B.** It does not transcode at startup; it just keeps the ROM mapped and lets gameplay code read pointers into it. Closer to ScummVM than SoH. This is a deliberate architectural choice to support quick startup and easy ROM-replacement at runtime, but it means PD2 has not built the container-archive infrastructure SoH uses.
- **Mike's `.pdmod` archive format already exists** (`port/src/modarchive.c`) as a ZIP container with a root `mod.json`. Extending this to per-asset-class `.pdwep`, `.pdui`, `.pdmesh` archives reuses the same VFS, modarchive, and modvfs infrastructure. The work is mostly schema design and loader plumbing, not new container code.
- **Hash-keyed validation is universal in modern ports** (SHA1 in SoH/Starship, MD5 in older code). PD2 has SHA-256 logging at `port/src/romdata.c:248-272` but the known-good hash table is empty and validation is opt-in. Filling the table is a 4-line edit per ROM version once a known-good hash is captured.
- **No surveyed project pre-extracts to disk at runtime AND uses the same format as mods.** Mike's directive that `data/ui/pd-original.pdui` is structurally identical to a modder-authored `.pdui` is a PD2 innovation. The closest analog is libultraship's "loose files override OTR" overlay behavior, which is similar in spirit but operates on a different unit of granularity.

### 2.7 Convergence vs anti-pattern map (PD2 surfaces today)

Reframing the Phase 1 inventory through the architectural principle "ROM is a one-time bootstrap input; extracted base content is the runtime source of truth":

| Surface | Pattern | ROM read at runtime? | Status |
|---|---|---|---|
| `tools/extract` Python | Build-time, developer-only | No | Convergent. Single ROM pass, output committed-back |
| `tools/assetmgr/mk*` Python | Build-time, headers compiled in | No | Convergent. Single ROM-derived input pass, baked into binary |
| `pdguiThemeExtractRomTextures` runtime | Runtime, first-launch-only | Yes (texture cache load), but writes to disk and runtime reads from disk | Convergent. ROM is bootstrap; future runs read disk |
| `pdguiChromeInitializeBaseMod` runtime | Runtime synthesis (not ROM-derived) | No | Convergent (procedural, not extraction) |
| `port/src/romdata.c` runtime model | Live-mapped ROM, on-demand reads | Yes, every gameplay file load | Anti-pattern under Mike's principle |
| `port/src/preprocess/*` runtime | Endian / pointer fix at file-load time | Yes, every file load triggers conversion | Anti-pattern under Mike's principle |

The runtime model in `romdata.c` and `preprocess/*` is the architectural target for the long-term shift. Today the catalog routing at `port/src/romdata.c:709` (`catalogResolveFile`) is the seed that mediates "go to mod / go to ROM" but it still falls through to ROM for the majority of assets. Mike's directive is to flip the default: extracted `.pdXXX` becomes the runtime source of truth; ROM is touched only by the first-launch extractor.

This is the through-line for Phase 3.

---

## Phase 3: Recommendations

### 3.1 Architectural principle (organizing this entire section)

> "Our extracted base content gets used by the catalog instead of hitting the rom itself all the time; it's a bit more freeing." (Mike, 2026-04-30)

**ROM is a one-time bootstrap input. Extracted base content is the runtime source of truth.**

Concretely:

1. The first-launch extractor reads the ROM ONCE and writes `.pdXXX` files into `data/<category>/`.
2. The catalog reads only from `data/` (BYOR-extracted) and `base/` (project-authored, redistributable). It never reads the ROM directly at runtime.
3. Mods overlay extracted content using the same `.pdXXX` format. A modder's `weapon_farsight.pdwep` is structurally identical to the extracted version; only the load path differs.
4. The runtime loader has zero ROM-specific code beyond the bootstrap extraction subsystem.

What this unlocks:

- **Catalog stays clean.** No ROM offsets, no ROM-specific decoders downstream of extraction. Asset IDs map to extracted files map to loader. One path.
- **Hot-reload and live-edit become possible.** Since runtime touches files not ROM, an editor can rewrite a `.pdwep` and the catalog re-loads it. The Forge level editor (per `context/designs/modding/forge-level-editor.md`) gets this for free.
- **Mods are first-class symmetric.** Drop a `weapon_farsight.pdwep` into `mods/<modname>/weapons/` with the same catalog ID and the catalog precedence rule overlays it. No "is this a base or mod weapon" branch in the loader.
- **ROM dependency is bounded.** First-launch extraction is the only ROM read path. Long-term a ROM-free mode (community-shared extracted bundles, validated by signature) becomes architecturally feasible. Not a near-term goal; the option is preserved.
- **Decompression happens once, not per-access.** The extractor decompresses ROM-side `1173`-zipped data once, writes raw `.pdXXX`, and the runtime never re-decompresses.
- **Modder development loop works without a ROM.** Tools target the extracted-content shape. Modders testing against community-shared extracted content have the full dev loop with no ROM file.

### 3.2 Per-asset-class extension taxonomy

Each ROM-derived asset category gets its own extension. The schema for each is identical between extractor output and modder-authored content (Mike's symmetry directive).

Proposed extensions and their corresponding existing extractor feed:

| Extension | Asset class | Extractor source today | Proposed `data/` location |
|---|---|---|---|
| `.pdwep` | Weapons (per-weapon) | `tools/assetmgr/mktextures` plus weapon defs in `g_Weapons[]` (catalog F11-F13) | `data/weapons/weapon_<name>.pdwep` |
| `.pdui` | UI texture bundle plus theme metadata | `pdguiThemeExtractRomTextures` (`port/fast3d/pdgui_theme.cpp:2405`) | `data/ui/pd-original.pdui` |
| `.pdmesh` | Character / weapon / prop models | `tools/extract:128` (`C*` chrs, `G*` guns, `P*` props files) | `data/meshes/<category>/<name>.pdmesh` |
| `.pdanim` | Animation file | `tools/extract:45` (`extract_animations`) | `data/animations/anim_<idx>.pdanim` (or grouped per character) |
| `.pdaudio` | Audio bank plus samples plus sequences | `tools/extract:62` (`extract_audio`) | `data/audio/sfxbank.pdaudio`, `data/audio/seqbank.pdaudio`, `data/audio/sequences/<name>.pdaudio` |
| `.pdscenario` | Stage setup (props, lifts, doors, AI) | `tools/extract:138` (`U*` setup files) | `data/scenarios/setup_<stage>.pdscenario` |
| `.pdpads` | Collision pads per stage | `tools/assetmgr/mkpads` plus runtime `preprocessPadsFile` | `data/pads/<stage>.pdpads` |
| `.pdtiles` | BG tiles per stage | `tools/assetmgr/mktiles` plus runtime `preprocessTilesFile` | `data/tiles/<stage>.pdtiles` |
| `.pdseg` | BG geometry segment per stage | `tools/extract:122` (`bg_*.seg` and `bgdata/`) | `data/segments/bg_<stage>.pdseg` |
| `.pdfont` | Font (per face plus per size) | `tools/extract:226` (10 fonts) | `data/fonts/<face>_<size>.pdfont` |
| `.pdlang` | Language strings per stage per locale | `tools/extract:134` (`L*Z` lang files) plus `tools/assetmgr/mklang` | `data/lang/<stage>_<locale>.pdlang` |
| `.pdmpstrings` | MP UI strings per locale | `tools/extract:204` (`extract_mpstrings`) | `data/mpstrings/<locale>.pdmpstrings` |
| `.pdmpconfigs` | MP weapon / scenario configs | `tools/extract:200` (`extract_mpconfigs`) | `data/mpconfigs/mp_configs.pdmpconfigs` |
| `.pdtexconfig` | Texture descriptor table | `tools/extract:268` (`extract_textureconfig`) | `data/textures/texture_config.pdtexconfig` |
| `.pdfiringrange` | Firing range setup | `tools/extract:217` | `data/scenarios/firingrange.pdscenario` (folds into `.pdscenario`) |

Granularity decisions per type are guided by the prevailing usage shape:

- **Per-asset granularity** for weapons (`weapon_farsight.pdwep`), models (`chr_carrington.pdmesh`), scenarios (`setup_villa.pdscenario`). Modders typically swap one of these at a time; per-file granularity gives clean diffs and clean overrides.
- **Per-bundle granularity** for UI textures (`pd-original.pdui`), audio banks (`sfxbank.pdaudio`). These cohere as a unit; modders usually replace the whole theme or the whole bank.
- **Per-stage granularity** for collision (`<stage>.pdpads`), tiles (`<stage>.pdtiles`), language (`<stage>_<locale>.pdlang`). Stage is the natural axis.
- **Per-language granularity** for MP strings. One `.pdmpstrings` per locale.

Variant suffixes via name (Mike's directive): `weapon_falcon2-silenced.pdwep` reads as "Falcon 2, silenced variant." Catalog ID is the lookup key; suffix is human-readable disambiguator.

### 3.3 Schema design principles per `.pdXXX`

Each extension defines a single canonical schema used by both extractor and mod tools. To meet Mike's symmetry directive:

1. **One schema per extension.** `.pdwep` describes a weapon record regardless of origin. Loader has one parsing path.
2. **Mod-tool output and extractor output are byte-identical for the same content.** Round-trip clean: extract weapon X then save unchanged via mod tool yields the same file.
3. **Schema accommodates both extracted and mod-authored content.** Where extraction produces fields modders cannot author (engine internals, hash signatures), they must be optional. Where mods add fields extraction cannot produce (mod author, license, version), they must be part of the schema as optional.
4. **References are by ID, not by file path.** A weapon referencing `"SFX_FALCON_RELOAD"` resolves through the catalog regardless of whether the SFX is base-extracted or mod-authored.
5. **Override semantics: parent field for partial overrides.** A mod's `.pdwep` with `parent: "weapon_farsight"` and only some fields set inherits the rest from the base. Total replacement is the default when no parent is declared and the catalog ID matches an existing entry.

Concrete schema sketches per type (full design out of scope for this audit; outlining shape only):

**`.pdwep`** sketch:

```
catalog_id: "weapon_farsight"        # primary key, namespace-prefixed
display_name: "FarSight XR-20"
parent: null                         # or "weapon_farsight" for overrides
class: "rifle"
slot: SLOT_PRIMARY
mesh_id: "weapon_farsight_mesh"      # ref to .pdmesh
firing:
  rate: 3
  ammo: 20
  damage: 100
  sfx_fire: "SFX_FARSIGHT_FIRE"      # ref to .pdaudio entry
  sfx_reload: "SFX_FARSIGHT_RELOAD"
  recoil_pattern: "rifle_heavy"
visuals:
  hud_icon: "ui_icon_farsight"       # ref to .pdui entry
flags: { silenced: false, scope: true, wallbang: true }
```

**`.pdui`** sketch (ZIP container, identical shape to current `mods/base-ui.pdmod`):

```
mod.json                             # manifest (display name, version, dependencies)
textures/<name>.tga                  # extracted RGBA32 textures
textures/<name>.png                  # PNG mirror for modder editing
textures/<name>.9slice.json          # per-texture nineslice definition
themes/<theme>.json                  # theme palette / scanlines / tint
```

**`.pdmesh`** sketch:

```
catalog_id: "chr_carrington"
display_name: "Daniel Carrington"
parent: null
gbi:
  segments: [...]                    # display list segments
  vertices: [...]                    # vertex array
materials: [...]                     # texture references by .pdui catalog id
skeleton: { ... }                    # bone tree
collision: { ... }                   # bounding box, capsule
animations: ["chr_carrington_idle", ...]   # refs to .pdanim entries
```

**`.pdaudio`** sketch (one per bank):

```
catalog_id: "audio_sfxbank"
display_name: "SFX Bank"
samples: [...]                       # ALADPCM / raw16 samples
instruments: [...]                   # ALInstrument records
banks: [...]                         # ALBank records
```

**`.pdscenario`** sketch:

```
catalog_id: "scenario_villa"
display_name: "Villa"
parent: null
stage_num: SOLO_VILLA
geometry_ref: "bg_villa"             # ref to .pdseg
pads_ref: "villa_pads"               # ref to .pdpads
tiles_ref: "villa_tiles"             # ref to .pdtiles
props: [...]                         # objects, lifts, doors
ai: [...]                            # spawn points, paths
events: [...]                        # objective triggers, scripted events
```

The exact schema for each is a follow-on design task. The audit's recommendation is to commit to the principle and design the schemas one asset class at a time, starting with whichever delivers fastest user value (probably `.pdwep` since the F11-F13 catalog work just shipped).

### 3.4 Directory taxonomy

Three top-level data trees, each with a clear purpose:

| Directory | Purpose | Contents | Source |
|---|---|---|---|
| `base/` | Project-authored, redistributable, ships-with-release content | Per-asset `.pdXXX` files for content the project authors and is licensed to ship | Decompiled-source-derived; in the repo |
| `data/` | BYOR runtime-extracted content | Per-asset `.pdXXX` files for content extracted from the user's ROM at first launch | First-launch extractor; user machine; gitignored |
| `mods/` | User-installed and bundled mods | `.pdmod` archives plus `.pdXXX` files (loose or in archives) | Mod manager; user machine; gitignored |

Catalog precedence (highest to lowest): `mods/<active>` then `data/` then `base/`. This is the SoH-style overlay model: mods top, then BYOR-extracted, then ship-with-release.

This implies `base/weapons.pdbase` (the F11-F13 monolith from the recent catalog work) is a transitional shape. Splitting into `base/weapons/weapon_*.pdwep` is the convergent target.

### 3.5 Functional gaps in PD2's current extraction

Listed in priority order under the architectural principle:

**G-1. Runtime extractor's output directory is misaligned with the `.pdmod` archive convention.** Severity: low (works today, but architecturally drift-prone).

`pdguiThemeExtractRomTextures()` writes loose files to `mods/base-ui/textures/`, while the mod system migrated to `mods/base-ui.pdmod` archive form. Recommendation under Mike's principle: redirect output to `data/ui/pd-original.pdui` (a single ZIP archive). The existing `.pdmod` infrastructure (`port/src/modarchive.c`, `port/src/modvfs.c`) handles ZIP archive read; extraction-time write needs a small new path that builds a ZIP archive instead of loose files.

**G-2. Audio extraction is build-time only.** Severity: depends on goals.

`tools/extract:62` (`extract_audio`) writes `extracted/<romid>/sfx.{ctl,tbl}` and `seq.{ctl,tbl}`. At runtime, `port/src/preprocess/segaudio.c:316` (`preprocessALBankFile`) re-reads the same bytes from the in-memory ROM and runs endian / pointer fix. Convergent under the principle would require a runtime extractor that produces `data/audio/sfxbank.pdaudio` once. Today this is bypassed: ROM stays mapped, gameplay reads bytes through the segment pointer. Migration path: build an `.pdaudio` writer that emits the post-`preprocessALBankFile` form once, point the audio loader at `data/audio/`.

**G-3. Mesh / model extraction is build-time only.** Severity: medium (the existing model loader runs `preprocessModelFile` per file load).

Same shape as audio: `tools/extract:130` writes per-character `.bin` blobs; runtime `preprocessModelFile` (`port/src/preprocess/filemodel.c`) re-converts on each load. Convergent migration: emit `data/meshes/chr_*.pdmesh` once at first launch, in post-conversion shape.

**G-4. Stage / scenario extraction is build-time only.** Severity: medium.

`tools/extract:138` writes per-stage `setup` blobs; runtime `preprocessSetupFile` (`port/src/preprocess/filesetup.c`) re-converts. Migration: emit `data/scenarios/setup_<stage>.pdscenario` once.

**G-5. Procedural chrome generator hides a future ROM extraction job.** Severity: low (today's procedural output is acceptable).

Comment at `port/fast3d/pdgui_theme.cpp:2845`: "Replace with real ROM texture extraction once the specific ROM addresses for PD's dialog chrome source art are identified." When the source art is identified, the chrome generator should be retrofitted to extract from ROM and bundle into `data/ui/pd-original.pdui` instead of synthesizing.

**G-6. ROM hash validation is implemented but disabled.** Severity: low (logging works; validation is opt-in).

`port/src/romdata.c:227-246` declares per-region SHA-256 known-good arrays but they hold only `NULL`. Filling each is a 4-line per-version edit once a known-good hash is captured (the game logs its hash on first launch; copy from log into source). Recommendation: capture hashes from a known-good ROM dump for each of NTSC, PAL, JPN final, populate the arrays, ship with the next release. Out of scope for this audit; mentioned because Phase 2 surfaced it as universal practice.

**G-7. CLI extract flags are not user-discoverable.** Severity: low.

`--extract-ui-textures` and `--generate-modern-ui` exist but are not in `--help` output. Recommendation: add a `--help` listing of these flags, plus add `--extract-all` and `--reextract-<category>` flags as the per-asset extractors are added.

**G-8. No test coverage for extraction.** Severity: low (the path is fail-safe due to procedural fallbacks).

Zero `pd-tests` cases for `pdguiThemeCheckExtract` / `pdguiThemeExtractRomTextures`. Per `context/pillars/tests.md` no extraction-related test scope alias exists. Recommendation: add a `pd-tests` scope `extraction` covering the missing-files mask logic, fallback substitution, and round-trip via mock ROM blob.

### 3.6 Architectural improvements (the model shift)

**A-1. Migrate the runtime asset path from "live-mapped ROM" to "first-launch extract then read disk."**

Today: every gameplay file load goes through `romdataFileLoad` (`port/src/romdata.c:691`), which in the no-mod-override case reads from `g_RomFile` and runs `preprocessXxxFile` on the bytes. Tomorrow: every gameplay file load goes through the catalog, which resolves to a `.pdXXX` file in `data/` or `mods/`, and the loader parses pre-converted bytes.

Migration sequence (one asset class at a time, mirroring the F11-F13 catalog gate model):

1. Define the `.pdXXX` schema for the class (e.g. `.pdwep`).
2. Build the runtime extractor that emits the `.pdXXX` from ROM at first launch.
3. Build the loader that reads the `.pdXXX` and produces the engine's runtime structure.
4. Switch the catalog to route the class through the new path; legacy ROM path stays as fallback.
5. After verification, remove the legacy ROM path for that class.
6. Move to next class.

This is the Catalog Gate 3 / Gate 4 / etc. cadence the project already uses; the audit recommends formalizing it as the architectural plan and putting it in `context/roadmap.md`.

**A-2. Make the bootstrap extractor a single in-binary subsystem.**

Today the only runtime extractor is `pdguiThemeExtractRomTextures` plus its call site. Tomorrow there's `romBootstrap*` covering all asset classes. Recommendation: a single `port/src/rombootstrap.c` with a switch statement across asset classes, called once on first launch. Each class has its own writer; all writers output to `data/<category>/`. CLI flag `--reextract-<category>` re-runs one writer. CLI flag `--extract-all` re-runs all.

**A-3. Catalog gets an extracted-content provider.**

The catalog already routes through `assetprovider_rom.c` for ROM-backed reads (`port/src/assetprovider_rom.c`). Add a sibling `assetprovider_data.c` for `data/`-backed reads (loading `.pdXXX` files). Once all classes are migrated, the rom provider goes away and the data provider plus mod provider are the only two.

**A-4. Mod tooling becomes the canonical authoring surface.**

Once the schema is symmetric, the mod tools (theme editor, skin editor, future weapon editor, future scenario editor) author exactly the shape the extractor produces. A modder iterating on a custom weapon does not need a ROM. The pdgui_skin_editor, pdgui_menu_theme_editor, pdgui_menu_moddinghub already exist (`port/fast3d/pdgui_*.cpp`); their output should converge on `.pdXXX` rather than the older bespoke formats.

**A-5. ROM-free mode becomes a future option, not a near-term goal.**

Once `data/` is the runtime source, a community-shared `data-bundle.zip` with hash-validated extracts could ship without the ROM. Out of scope today but the architecture preserves the option. Document in `context/roadmap.md` as a long-term capability gate.

### 3.7 Tooling gaps

Recommendations for tooling that does not exist today but would benefit from the architectural shift:

**T-1. `data/` integrity verifier.** A standalone command (or hidden CLI flag `--verify-data`) that walks `data/` and validates each `.pdXXX` file's checksum against an expected manifest. Detects partial extraction, file corruption, version drift after a game update.

**T-2. `data/` re-extraction ergonomics.** Today the only way to force re-extraction is to delete the directory. Add `--reextract-<category>` (per asset class) and `--reextract-all` (full nuke and rebuild). Make these visible in `--help`.

**T-3. Mod-from-extract scaffolder.** A modder creating a custom weapon should be able to run a tool that copies `data/weapons/weapon_farsight.pdwep` into `mods/<modname>/weapons/weapon_falcon2-myvariant.pdwep` with the catalog ID rewritten and a `parent:` set. Bootstraps the mod-creation loop. Could live in the existing `pdgui_menu_moddinghub.cpp`.

**T-4. Schema validator.** Each `.pdXXX` schema gets a JSON Schema (or equivalent) that the loader validates against on load. Catches mod authoring errors at load time with clear messages.

**T-5. CI extraction smoke test.** A test ROM (could be a synthetic fixture, not a real Rare ROM) and a check that all `.pdXXX` extractors produce the expected files with the expected schema. Lives in `pd-tests` under the new `extraction` scope.

### 3.8 Mod-friendliness improvements

Direct consequences of the symmetric-schema principle:

**M-1. Mod tools work without a ROM.** Because mods author against the same schema as extracted content, a modder can develop and test against community-shared extracted bundles without owning a ROM. (This is the single largest mod-ergonomics improvement of the entire architectural shift.)

**M-2. `parent:` field enables partial-override mods.** A mod that wants only to change Falcon 2's reload sound writes a 5-line `.pdwep` with `parent: "weapon_falcon2"` and `firing.sfx_reload: "SFX_MY_RELOAD"`. The catalog merges the parent's fields with the override's. No need to copy the entire weapon definition.

**M-3. Variant naming convention is human-readable.** `weapon_falcon2-silenced.pdwep` reads as "Falcon 2 silenced variant" at a glance. `weapon_carrington_tux.pdmesh` reads as "Carrington in tuxedo." Modders can identify content from the file tree alone.

**M-4. Mod-from-extract round-trip enables reverse engineering.** A modder who wants to understand how the base game configures a weapon opens `data/weapons/weapon_farsight.pdwep` in a text editor (assuming JSON-or-similar surface schema) and reads the fields directly. No need to know ROM offsets, segment tables, or `preprocessGunFile`. The contract is the schema.

**M-5. Modarchive (`.pdmod`) becomes the bundling format.** A mod ships as `mod_name.pdmod` (a ZIP archive) containing `.pdXXX` files at canonical paths. The mod manager already knows how to read ZIP archives. Mod authors just package by directory.

### 3.9 Priority order

Ranked by leverage (impact / cost) for the next 1 to 4 sessions:

1. **Document the architectural principle in `context/roadmap.md` and `context/pillars/catalog.md`.** Cost: zero code, one doc edit. Locks in the direction; future sessions cascade from it. (This audit is the first half of that documentation.)
2. **Migrate the existing UI texture extractor's output from loose files to `data/ui/pd-original.pdui`.** Cost: small (~1 session). Zero schema work (use the existing mod.json + textures/ shape). Validates the principle on the simplest existing surface and aligns the runtime extractor with the rest of the mod system.
3. **Define the `.pdwep` schema and migrate the F11-F13 weapons content (currently `base/weapons.pdbase` monolith) to per-weapon `base/weapons/weapon_*.pdwep`.** Cost: medium (~2 sessions). Highest user value because weapons are the most active modding surface. Establishes the canonical schema-design pattern and the per-asset granularity convention. The existing `.pdbase` parser can be reused with minor tweaks.
4. **Populate ROM SHA-256 known-good hashes (`port/src/romdata.c:227-246`).** Cost: trivial (one-time, capture from log on each ROM version). Closes the validation gap surfaced by Phase 2.
5. **Add `--help` listing for `--extract-ui-textures`, `--generate-modern-ui`, plus new `--reextract-<category>` and `--extract-all`.** Cost: trivial. Improves end-user diagnostics.
6. **Add a `pd-tests` scope `extraction` covering existing UI texture extractor behavior.** Cost: small. Locks in regression coverage before the larger migrations.

Items 1 to 6 stage a multi-session program. Item 7+ is the asset-class-by-asset-class migration (mesh, audio, scenario, animation, etc.) which can happen in parallel with other work and does not need to be batch-scheduled.

---

## Where to look

**Existing extractors**
- Build-time Python: `tools/extract`, `tools/assetmgr/{assetmgr.py,mklang,mkpads,mktiles,mkanims,mksequences,mktextures}`
- Runtime UI textures: `port/fast3d/pdgui_theme.cpp:2405` (`pdguiThemeExtractRomTextures`), `:3162` (`pdguiThemeCheckExtract`)
- Runtime ROM model: `port/src/romdata.c:601` (`romdataInit`), `:691` (`romdataFileLoad`)
- Runtime preprocess hooks: `port/src/preprocess/{common,filebg,filelang,filemodel,filepads,filesetup,filetiles,gbi,misc,segaudio,segfonts}.c`

**Catalog routing**
- Asset providers: `port/src/assetprovider_rom.c`, `port/include/assetprovider.h`
- Catalog resolution: `port/src/assetcatalog_load.c`, `port/src/assetcatalog_scanner.c`
- Catalog file routing: `port/src/romdata.c:709` (`catalogResolveFile`)

**Mod system**
- Mod manager: `port/src/modmgr.c`
- Mod archive (.pdmod ZIP): `port/src/modarchive.c`, `port/src/modvfs.c`
- Mod scanner / VFS: `port/src/fs.c:18` (`modvfs.h` include), `port/src/modvfs.c`

**Pillar docs touched by these changes**
- `context/pillars/catalog.md` (catalog routing changes)
- `context/pillars/modding.md` (`.pdmod` plus `.pdXXX` extension family)
- `context/pillars/build-dev-tooling.md` (extractor tooling)
- `context/pillars/menus.md` (UI texture extractor lives here)
- `context/pillars/rendering.md` (texture decoders consumed by UI extractor)

**Phase 2 reference URLs**
- fgsfdsfgs/perfect_dark/tools/extract: https://github.com/fgsfdsfgs/perfect_dark/blob/port/tools/extract
- OoT decomp tools: https://github.com/zeldaret/oot/tree/main/tools
- MM decomp / ZAPD: https://github.com/zeldaret/mm/blob/main/extract_assets.py , https://github.com/zeldaret/mm/tree/main/tools/ZAPD
- SM64 decomp extract_assets.py: https://github.com/n64decomp/sm64/blob/master/extract_assets.py
- MK64 decomp tools: https://github.com/n64decomp/mk64/blob/master/Makefile
- BK decomp tools: https://github.com/n64decomp/banjo-kazooie/tree/master/tools
- Ship of Harkinian Extractor: https://github.com/HarbourMasters/Shipwright/tree/develop/soh/soh/Extractor
- Starship GameExtractor: https://github.com/HarbourMasters/Starship/blob/main/src/port/extractor/GameExtractor.cpp
- libultraship: https://github.com/Kenix3/libultraship
- libdragon (encoder side): https://github.com/DragonMinded/libdragon/tree/trunk/tools
- 2 Ship 2 Harkinian: https://github.com/HarbourMasters/2ship2harkinian , https://2ship.equipment/

**Audit cross-references**
- `context/audits/pdmod-verification-matrix-2026-04-25.md` (current `.pdmod` archive state)
- `context/audits/catalog-universality-sweep-2026-04-27.md` (catalog routing maturity)
- `context/audits/post-implementation-audit-2026-04-25.md` (recent shipped state)
- `context/designs/modding/pdmod-format.md` (`.pdmod` archive format spec)
- `context/designs/catalog/catalog-full-pipeline-weapons.md` (F1-F10 plus F11-F13 weapons catalog work)

End of audit.


