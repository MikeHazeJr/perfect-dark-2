# ROM Extraction Audit (2026-04-30)

> Two-phase audit of how PD2 extracts content from a user-supplied N64 ROM, plus a research-and-compare pass against the wider N64 / classic-game decomp ecosystem, plus a third phase of concrete recommendations incorporating Mike's directive on per-asset-class file extensions and the `base/` vs `data/` directory split.

Methodology gates per `context/procedures.md`: possibility framing on subjective judgments, file:line + URL evidence for every claim, no em-dashes, no code shipped in this session, recommendations only.

---

## Executive summary

**Two canonical organizing principles (Mike, 2026-04-30):**

1. **ROM is a one-time bootstrap input. Extracted base content is the runtime source of truth.** The catalog routes through extracted `.pdXXX` files plus mod overlays; ROM offsets and decoders only live inside the bootstrap extractor.
2. **`base/` and `data/` are organizational, not hierarchical.** `base/` holds project-authored, redistributable content that ships with releases (decompiled-source-derived). `data/` holds BYOR ROM-extracted content that is never shipped (populated on the user's machine at first launch). Both are read by the catalog at runtime via the same loader. The directory split tracks redistribution status, not loading mechanism. Mods ride alongside both with the same loader; base content and mod content are effectively equal, the game knows no difference, they just live in different locations.

Today PD2 partially satisfies the bootstrap principle in one place (`pdguiThemeExtractRomTextures` writes UI textures to disk on first launch and the runtime reads from there) and violates it everywhere else: the runtime ROM model in `port/src/romdata.c` keeps the full 32 MB ROM mapped and re-touches it on every gameplay file load. The `tools/extract` Python script handles build-time / developer-side extraction; there is no end-user runtime extractor in the SoH / Starship style.

Architectural mismatches the audit surfaces:

1. **Directory vs archive drift.** The runtime UI texture extractor writes loose `.tga` / `.png` / `.9slice.json` files into `mods/base-ui/textures/`, while the modding pipeline migrated the rest of `base-ui` into a single ZIP archive (`mods/base-ui.pdmod`). The archive does not contain the extracted textures; they live alongside it on disk. Note: `mods/base-ui.pdmod` is broken legacy retirement target, not architectural reference.
2. **End-user vs developer workflow conflation.** `tools/extract` (frozen upstream since 2022-12-04) runs only on developer machines. End users get C-side ROM parsing at startup with hardcoded per-region offsets and no validation feedback beyond a "wrong ROM" fatal.
3. **No bundled output format aligned with mods.** Build-time extracts land as flat directories of raw `.bin` files plus JSON manifests. Mike's `.pdwpn` / `.pdui` / `.pdmesh` per-asset-class extension taxonomy plus base-vs-mod schema symmetry directive is a PD2 innovation that does not have a direct precedent in the surveyed N64 decomp ecosystem (closest: SoH's libultraship loose-files-overlay-OTR).
4. **Silent procedural fallbacks.** When ROM extraction fails or is incomplete (chrome frame today, individual UI textures on extraction error), the runtime substitutes synthetic art with no loud signal. Should be loud-fail-then-recover, not silent-fail-then-recover.
5. **No test coverage for catalog-critical extraction.** Catalog is THE pillar that owns single-source-of-truth for all assets; extraction having zero `pd-tests` coverage is structurally unacceptable. Severity high.

Recommendations cascade from the two canonical principles:

- Adopt the `.pdXXX` per-asset-class extension taxonomy. One canonical schema per extension. Same schema for extractor output, base content, and mod-tool output (round-trip clean).
- Add a `data/` directory tier for BYOR-extracted runtime content. Catalog reads from `mods/` plus `data/` plus `base/` with explicit override semantics (mods cannot reuse base names; mods declare overrides via a flag; multiple simultaneous overrides supported).
- Realign the existing UI texture extractor to emit `data/ui/pd-original.pdui` (a single ZIP archive structurally identical to a modder-authored `.pdui`).
- Migrate runtime asset loading from "live-mapped ROM" to "first-launch extract then read disk" one asset class at a time using the same Catalog Gate cadence the F1-F13 weapons work used.
- Populate the SHA-256 known-good ROM hash table at `port/src/romdata.c:227-246` (currently `NULL`-only). Use it both to validate ROM integrity AND to select correct extraction offsets per ROM version.
- Add a hash-verify-on-launch self-heal loop with corruption quarantine. `data/` is read-only at the OS level after extraction completes; the self-heal path acquires writable mode temporarily; corrupted-and-replaced files move to `data/.quarantine/` (one snapshot retained per file) before re-extraction. The project is a safe haven for modders; experimentation does not lose work.
- Introduce a `LOUDFAIL` log channel (`CATALOG.LOUDFAIL.*`, `EXTRACT.LOUDFAIL.*`, `LOAD.LOUDFAIL.*`, or a top-level `LOUDFAIL.*`) so silent recoveries become visible. Procedural fallback hits this channel before substituting.
- Bump severity on the procedural chrome generator: comment at `pdgui_theme.cpp:2845` is incomplete-implementation, not aspirational. High priority to identify the real ROM addresses for PD's dialog chrome source art and replace the synthetic reconstruction with authentic extraction.
- Bump severity on extraction test coverage: high (catalog is the most-load-bearing pillar; extraction is its bootstrap input).

Stop conditions: doc length expanded substantially in the second pass to accommodate Mike's 20 directives plus extrapolations; final length is documented at the bottom of this file. The original `< 800 lines` ceiling no longer applies under the expanded scope.

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

- `s_generateProceduralTexture()` (referenced from `:2602`) generates noise / haze / solid pixel buffers. Used as silent fallback when ROM extraction fails.
- `s_generateModernUiTextures()` at `:2638` generates 4 alternate UI textures for the `pd-modern-ui` mod. Triggered only by `--generate-modern-ui`.
- `pdguiChromeInitializeBaseMod()` (called from `:3295`) and `s_generateChromeFrameBgra()` at `:2856` generate the 64x64 nineslice chrome frame procedurally. Comment at `:2845` describes this as aspirational ("Replace with real ROM texture extraction once the specific ROM addresses for PD's dialog chrome source art are identified") but Mike has reframed it: this is incomplete implementation, not aspirational. The synthetic chrome diverges from the OG-PD look; the real chrome is assembled by `menugfxRenderDialogBackground()` compositing palette colors plus textured strips. Severity: high. Identifying the actual ROM addresses for the source art and implementing real extraction is a priority gap.

These are not extraction; they are deterministic synthesis. Two issues:

- **Silent fallback.** When `s_generateProceduralTexture` fires because ROM extraction failed, no loud diagnostic is logged. Modder or end-user has no way to tell the difference between authentic and synthetic textures without inspecting the output. Recommendation in Phase 3: route this through a `LOUDFAIL` channel.
- **Synthetic chrome is incomplete, not done.** Per directive 11 above, the project author is invested in OG-looking menus; the procedural chrome is a stopgap and needs replacement.

### 1.7 Architectural mismatches surfaced by the audit

**1. Runtime extractor writes to a directory; mod system reads from an archive.** Severity: medium (works today, drift-prone).

Evidence:
- `pdgui_theme.cpp:2525` writes to `mods/base-ui/textures/<name>.tga`
- `mods/base-ui.pdmod` exists (4420 bytes, ZIP archive) per `context/audits/pdmod-verification-matrix-2026-04-25.md:51`
- ZIP listing of `mods/base-ui.pdmod` shows ONLY `mod.json` + `themes/theme_*.json` (7 themes). No textures inside the archive.
- `mods/base-ui/` directory does not exist on a clean checkout.
- `mods/base-ui.legacy_backup/` exists but holds only `mod.json` + `themes/`, no textures.

Effect: on first launch the extractor creates `mods/base-ui/` and writes 14 TGAs + 14 PNGs + N nineslice JSONs there. The `.pdmod` archive does not get those textures. The VFS layer (`port/src/modvfs.c`) intercepts `fsFileLoad("mods/base-ui/textures/...")` first against the archive (miss, not present) then falls through to the on-disk directory (hit if extracted). Both paths exist alongside each other.

This works but it is fragile: a user who deletes `mods/base-ui/` thinking the `.pdmod` is canonical will lose their textures until the next launch re-extracts. A user who edits a TGA on disk will see edits applied (because disk hits before archive), but if textures were ever moved into the archive, disk edits would be silently overridden.

Per directive 19: `mods/base-ui.pdmod` is broken legacy and should be a retirement target. The right shape is `data/ui/pd-original.pdui` (a `.pdui` archive in the BYOR `data/` tree) that is regenerated on first launch by the extractor and that the mod system can override via a parallel `mods/<mymod>/ui.pdui`.

**2. Modder-friendly output format, but only one mod.**

The runtime extractor produces a complete, self-describing `base-ui` mod with `mod.json`, `textures/*.tga`, `textures/*.9slice.json`, ready for the user to edit and ship. This is a good idea applied to one place. None of the other ROM-derived assets (audio, gun models, character models, stage geometry, animations, fonts) get the same treatment at runtime.

**3. Dev-tool extractor in an end-user runtime.**

`tools/extract` is a Python script invoked by developers. End users never run it. The C-side runtime parsing in `romdata.c` is the closest thing PD2 has to an end-user extractor and it does not extract; it parses-in-place. There is no SoH-style "first launch detected, here is your ROM, generating asset cache, please wait" UX. Mike's directive to introduce `data/ui/pd-original.pdui` is the seed of an end-user runtime extractor.

**4. ROM hash validation is implemented but disabled.** Severity: medium (becomes high once extraction is the runtime source of truth).

`port/src/romdata.c:227-246` declares per-region SHA-256 known-good hash tables but they are commented out (the array is `{ NULL }`). The ROM is logged but not validated. A wrong-version ROM with a matching size and header (`NPDE`/`NPDP`/`NPDJ`) will load and silently corrupt offsets that drift between sub-versions. The Phase 2 research shows hash-keyed validation is universal in modern PC ports; PD2 has the scaffolding and chose not to populate it.

Per directive 13: enabling validation is not just hygiene; it is the gate that lets the extractor pick the right per-version offset table. Wrong ROM detected at hash time means abort extraction with a user-facing dialog ("This is the JPN ROM but the build expects NTSC; please supply the correct version"), not extract garbage offsets.

**5. Test coverage gap is structural, not cosmetic.** Severity: high (per directive 12).

Catalog is the single-source-of-truth pillar for ALL assets at runtime. Extraction is its bootstrap input. Zero `pd-tests` cases exercise `pdguiThemeCheckExtract` or `pdguiThemeExtractRomTextures`. Verified by absence in `context/pillars/tests.md` and grep of test source. A regression in extraction does not surface until a user reports broken UI textures. This is structurally unacceptable for the most load-bearing pillar in the codebase.

**6. Procedural fallback is silent.** Severity: medium (per directive 10).

When `s_generateProceduralTexture` fires (because ROM extraction returned bad dims, unsupported format, or NULL pointer), the runtime substitutes synthetic art and emits only a `LOG_WARNING` line. There is no loud signal in the user-facing logs, no diagnostics surface, no in-game banner. A modder or end-user looking at procedural chrome cannot tell at a glance whether they are seeing authentic ROM-derived art or a procedural stand-in.

**7. CLI extract flags are documented in code but not in any user-visible help.** Severity: low (becomes irrelevant under directive 14, which automates extraction triggering).

`--extract-ui-textures` and `--generate-modern-ui` exist (`pdgui_theme.cpp:3284-3290`). They are not listed in `--help` output (verified by absence of these strings outside `pdgui_theme.cpp` and a few archived design docs).

Per directive 14, end-user discoverability is the wrong abstraction: extraction should fire automatically on launch, gated by hash check, with a loading-screen modal during the first-launch run. Subsequent launches bypass when hashes match. CLI flags become developer / power-user surface, not end-user surface.

**8. Mod-override branch in `romdataFileLoad` is AllInOne residue.** Severity: low (cleanup target).

`port/src/romdata.c:710` has a `r.is_mod_override && r.path` branch that calls `fsFileLoad(r.path)` with arbitrary paths from the catalog. Per directive 18, this is likely AllInOne residue (the project's prior multi-mod merger predecessor). Under the new `data/` plus `base/` plus `mods/` model with canonical `.pdXXX` schema and explicit override semantics, this branch is not the right shape. Mark as cleanup target during the migration.

---

## Phase 2: Research and compare

The N64 / classic-game decomp community has converged on two dominant patterns for ROM-derived content extraction. PD2 currently sits inside Pattern A (build-time, developer-facing) but is gradually growing Pattern B surfaces (the `pdguiThemeExtractRomTextures` runtime path is one). Mike's `.pdwpn` / `.pdui` format direction is a third innovation that does not have a direct precedent in the surveyed projects.

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

PD2's emerging `.pdwpn` / `.pdui` / `.pdmesh` taxonomy slots into a third axis the surveyed community has not converged on. Surveying the conventions:

| Convention | Used by | Examples | Trait |
|---|---|---|---|
| Format-extension (file shows underlying data shape) | OoT decomp, MM decomp, SM64 decomp, MK64 decomp, libdragon | `.aiff`, `.m64`, `.rgba16.png`, `.bin`, `.json`, `.xml`, `.c` | Polyglot; reader knows what to do from the extension; one extension per data shape regardless of asset class |
| Container-archive-extension (single file holds many assets) | Ship of Harkinian, 2S2H, Starship, Ghostship, SpaghettiKart | `.otr` (MPQ-based, legacy), `.o2r` (ZIP-based, current) | Monolithic per game; index inside; opaque without the right tool |
| Asset-class-extension (file shows what content is for) | PD2 (emerging: `.pdwpn`, `.pdui`, `.pdmesh`, `.pdmod`) | `.pdwpn`, `.pdui`, `.pdmod`, `.pdmodpack` | Self-documenting at the directory level; loader dispatches by extension |

PD2's emerging convention is closer to game-engine-native pipelines (Unity `.prefab`, Unreal `.uasset`) than to either decomp pattern. The advantage: a glance at `base/weapons/weapon_falcon2.pdwpn` tells a modder exactly what they are looking at, what other files in the tree are similar, and what tool they need. The disadvantage: the loader has to register every extension; new asset categories need new code paths; the parser cannot multi-purpose a single shape across asset classes.

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
- **Mike's `.pdmod` archive format already exists** (`port/src/modarchive.c`) as a ZIP container with a root `mod.json`. Extending this to per-asset-class `.pdwpn`, `.pdui`, `.pdmesh` archives reuses the same VFS, modarchive, and modvfs infrastructure. The work is mostly schema design and loader plumbing, not new container code.
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

### 3.1 Two canonical principles

**Principle 1. ROM is a one-time bootstrap input. Extracted base content is the runtime source of truth.**

> "Our extracted base content gets used by the catalog instead of hitting the rom itself all the time; it's a bit more freeing." (Mike, 2026-04-30)

Concretely:

1. The first-launch extractor reads the ROM ONCE and writes `.pdXXX` files into `data/<category>/`.
2. The catalog reads only from `data/` (BYOR-extracted), `base/` (project-authored, redistributable), and `mods/` (user-installed plus bundled mods). It never reads the ROM directly at runtime.
3. The runtime loader has zero ROM-specific code beyond the bootstrap extraction subsystem.

What this unlocks:

- **Catalog stays clean.** No ROM offsets, no ROM-specific decoders downstream of extraction. Asset IDs map to extracted files map to loader. One path.
- **Hot-reload and live-edit become possible.** Since runtime touches files not ROM, an editor can rewrite a `.pdwpn` and the catalog re-loads it. The Forge level editor (per `context/designs/modding/forge-level-editor.md`) gets this for free.
- **ROM dependency is bounded.** First-launch extraction is the only ROM read path. Long-term a ROM-free mode (community-shared extracted bundles, validated by signature) becomes architecturally feasible. Not a near-term goal; the option is preserved.
- **Decompression happens once, not per-access.** The extractor decompresses ROM-side `1173`-zipped data once, writes raw `.pdXXX`, and the runtime never re-decompresses.
- **Modder development loop works without a ROM.** Tools target the extracted-content shape. Modders testing against community-shared extracted content have the full dev loop with no ROM file.

**Principle 2. `base/` and `data/` are organizational, not hierarchical. Mods are first-class equals.**

> "Base content and mod content are EFFECTIVELY EQUAL, the game knows no difference, they just live in different locations." (Mike, 2026-04-30)

The three top-level content trees and what distinguishes them:

| Tree | Source | Ships with release? | Authored by | Loaded via |
|---|---|---|---|---|
| `base/` | Decompiled-source-derived; project-authored | Yes (redistributable) | PD2 contributors | Catalog |
| `data/` | Extracted from user's ROM at first launch | No (BYOR; never shipped) | First-launch extractor | Catalog |
| `mods/` | User-installed `.pdmod` archives, bundled mods, or modpack-extracted contents | Sometimes (bundled mods like `bot-names.pdmod`) | Modders or PD2 | Catalog |

Critical clarifications:

- **No format difference between any of the three.** All three hold `.pdXXX` files following the same canonical schemas. The loader does not branch on tree; it dispatches by extension.
- **Naming-disallow rule (per directive 4).** Mod-authored content cannot reuse names that already exist in `base/` or `data/` without an explicit override declaration. This forces modder intent: a modder who actually wants to override `weapon_falcon2` declares the override in their mod's manifest; a modder who just wants a Falcon variant ships `weapon_falcon2-mybuild.pdwpn` with a distinct name.
- **Multiple simultaneous overrides are first-class.** A single mod can declare overrides for many assets at once ("all characters get the same skin"; "specific missions get this music track"). Override declarations live alongside the override content in the mod's manifest.
- **The directory split tracks redistribution status, not loading mechanism.** Anything in `base/` is something the project is licensed to ship. Anything in `data/` is BYOR-derived and never gets pushed to GitHub. Anything in `mods/` rides the same loader regardless of source.

What this unlocks:

- **Mods authored against base content are round-trip clean.** Open `data/weapons/weapon_falcon2.pdwpn` in a mod tool, edit, save as `mods/<myFalconRework>/weapons/weapon_falcon2-mybuild.pdwpn`, ship. No format conversion at any step.
- **Mod tools load any base content as a starting template** (per directive 20). A modder forking a mission opens `data/scenarios/setup_villa.pdscenario`, gets a starter, modifies, saves to their mod. Same for characters, weapons, audio, etc.
- **Future ROM-free distribution remains feasible.** A community-shared `data-bundle.zip` of validated extracted content could ship without the ROM. The architecture preserves the option without committing to it.

### 3.2 Per-asset-class extension taxonomy

Each ROM-derived asset category gets its own extension. The schema for each is identical between extractor output, base content, and mod-tool output (Mike's symmetry directive). The extension reveals what the content is for; the file naming reveals identity and variant.

**Confirmed extensions per Mike (directive 1):**

| Extension | Asset class | Granularity | Extractor source today | Proposed location |
|---|---|---|---|---|
| `.pdwpn` | Weapon | Per-weapon | weapon defs in `g_Weapons[]` (catalog F11-F13) plus `tools/assetmgr/mktextures` for weapon mesh refs | `data/weapons/weapon_<name>.pdwpn` |
| `.pdui` | UI texture bundle plus theme | Per-bundle | `pdguiThemeExtractRomTextures` (`port/fast3d/pdgui_theme.cpp:2405`) | `data/ui/pd-original.pdui` |
| `.pdmesh` | Mesh (character / weapon / prop / accessory) | Per-mesh | `tools/extract:128` (`C*` chrs, `G*` guns, `P*` props files) | `data/meshes/<category>/<name>.pdmesh` |
| `.pdanim` | Animation | Per-animation (or grouped) | `tools/extract:45` (`extract_animations`) | `data/animations/anim_<idx>.pdanim` |
| `.pdsong` | Music track | Per-song | `tools/extract:62` (subset: `sequences/*.seq`) | `data/audio/music/song_<name>.pdsong` |
| `.pdsfx` | Sound effect | Per-effect | `tools/extract:62` (subset: SFX bank entries split per ID) | `data/audio/sfx/sfx_<name>.pdsfx` |
| `.pdvoice` | Voice line | Per-line | `tools/extract:62` (subset: voice samples split per actor / line) | `data/audio/voice/voice_<actor>_<id>.pdvoice` |
| `.pdscenario` | Stage / mission (Mike open on `.pdmission`; recommend `.pdscenario` for genre neutrality across SP and MP and firing range) | Per-stage | `tools/extract:138` (`U*` setup files) | `data/scenarios/setup_<stage>.pdscenario` |
| `.pdfont` | Font face | Per-face plus per-size | `tools/extract:226` (10 fonts) | `data/fonts/<face>_<size>.pdfont` |
| `.pdlang` | Language strings | Per-stage per-locale | `tools/extract:134` (`L*Z` lang files) plus `tools/assetmgr/mklang` | `data/lang/<stage>_<locale>.pdlang` |
| `.pdmodpack` | Mod pack (container of multiple mods) | Per-pack | n/a (modder-authored) | `mods/<packname>.pdmodpack` |

**Asked-about extensions, with definitions and recommendations (directive 1):**

- **`.pdtiles`** (proposed): BG tile data per stage. Defines walkable surfaces, height map, footstep audio class, slipperiness flags. Today extracted by `tools/assetmgr/mktiles` from `src/assets/<romid>/tiles/<stage>.json` plus runtime `preprocessTilesFile` in `port/src/preprocess/filetiles.c`. **Recommendation**: keep separate from `.pdscenario` because modders may swap tiles independently of props/AI (e.g. retexture a stage with new floor types without touching the mission flow). Folder: `data/scenarios/<stage>/tiles.pdtiles` (or `data/tiles/<stage>.pdtiles` if you prefer flat layout).
- **`.pdseg`** (proposed): BG geometry segment per stage. Defines the GBI display lists, vertex arrays, texture atlas references for the visual mesh of a stage. Today extracted by `tools/extract:122` (`bg_*.seg`, `bgdata/`) and loaded at runtime as a raw segment. **Recommendation**: keep separate from `.pdtiles` and `.pdscenario`. A modder retexturing a stage's visuals (`.pdseg`) is a different change from retiling its walkable floor (`.pdtiles`) is a different change from rewiring its props and AI (`.pdscenario`). Folder: `data/scenarios/<stage>/geometry.pdseg`.
- **`.pdmpconfig`** (proposed): MP weapon set / scenario default config. Today extracted by `tools/extract:200` (`extract_mpconfigs`). **Recommendation**: fold into `.pdscenario` for MP stages. The MP-specific config (which weapons spawn, default time / score limits, default loadouts) is a property of the stage, not a separate asset. A `.pdscenario` for an MP map carries its weapon set inline. Saves a tier of indirection.
- **`.pdtexconfig`** (proposed): Texture descriptor table (offset, dims, format) baked into ROM. Today extracted by `tools/extract:268`. **Recommendation**: retire under the new model. The descriptor table is a ROM-internal index for the runtime texture cache; once textures are extracted into `.pdui` / `.pdmesh` archives, each carries its own format metadata inline. The table becomes obsolete. No `.pdtexconfig` extension.
- **`.pdfiringrange`** (proposed): Firing range setup. Today extracted by `tools/extract:217`. **Recommendation**: roll into `.pdscenario`. Firing range is a stage; treating it as such removes a special case. `data/scenarios/setup_firingrange.pdscenario`.
- **`.pdmpstrings`** (proposed): MP UI strings per locale. Today extracted by `tools/extract:204`. **Recommendation**: roll into `.pdlang`. MP strings are localized text; `.pdlang` already covers per-locale text. Use `data/lang/mp_<locale>.pdlang` (or fold into a global UI lang file).

**Final extension list** after consolidation (revised in Pass 3 to add `.pdprop` and `.pdcharacter`; revised in Pass 5 to add `.pdwepset`):

| Extension | Purpose |
|---|---|
| `.pdwpn` | Weapon |
| `.pdwepset` | Weapon set (compound: a curated list of weapon catalog IDs plus spawn metadata; surfaces in MP setup's weapon-set picker; see Section 3.16.11) |
| `.pdui` | UI texture bundle plus theme metadata |
| `.pdmesh` | Static mesh (visual geometry only; no skeleton, no behavior). Covers spawnable props that need only visual representation. |
| `.pdcharacter` | Compound character (skeletal mesh plus animations plus audio plus attach points plus behavior). Distinct catalog asset type from `.pdmesh`. See Section 3.16.4. |
| `.pdprop` | Spawnable prop with logic (visual geometry plus physics plus stats plus behavior block). Distinct from `.pdmesh` (which is visual-only). See Section 3.17 for the worked example. |
| `.pdanim` | Animation |
| `.pdsong` | Music track |
| `.pdsfx` | Sound effect |
| `.pdvoice` | Voice line |
| `.pdscenario` | Stage / mission / firing range / MP map (carries scenario config inline; modes block per Section 3.16.3; per-spawn-point weapon and pickup catalog IDs declared in scenario data per Section 3.3) |
| `.pdtiles` | Stage walkable-surface data |
| `.pdseg` | Stage geometry segment |
| `.pdfont` | Font face |
| `.pdlang` | Language strings (any context: SP missions, MP UI, system messages) |
| `.pdmod` | Mod archive (ZIP container of `.pdXXX` files plus `mod.json`) |
| `.pdmodpack` | Mod-pack archive (container of multiple `.pdmod`s, plus pack metadata) |

Also retained for completeness, not extension-class:
- `.pdmanifest` (extrapolation E-11; per-tree manifest with hash table; see Section 3.6)
- `.9slice` (existing UI sub-asset; see Section 3.3 INI usage)

**Granularity decisions per type:**

- **Per-asset granularity** for weapons (`weapon_falcon2.pdwpn`), meshes (`chr_carrington.pdmesh`), scenarios (`setup_villa.pdscenario`), songs (`song_pelagic.pdsong`), sfx (`sfx_falcon_reload.pdsfx`), voice lines (`voice_carrington_intro_01.pdvoice`). Modders typically swap one of these at a time; per-file granularity gives clean diffs and clean overrides.
- **Per-bundle granularity** for UI textures (`pd-original.pdui`). UI textures cohere as a theme; modders usually replace whole themes.
- **Per-stage-per-resource granularity** for stage geometry (`<stage>/geometry.pdseg`), tiles (`<stage>/tiles.pdtiles`), and scenario (`<stage>/setup.pdscenario`). Three sub-resources per stage; modders can swap independently.
- **Per-locale granularity** for `.pdlang`. One file per locale, or per (stage, locale) pair.

**Variant naming convention (per directive 5):** variant suffix is part of the asset identity, not a flag toggle on a shared base. `weapon_plasmarifle.pdwpn` and `weapon_plasmarifle-brute.pdwpn` are distinct catalog IDs with distinct property sets. The hyphen suffix is a human-readable disambiguator for sorting and discovery; the catalog treats them as independent assets. (See extrapolation E-4 in Section 3.15 for the catalog-ID treatment.)

### 3.3 Schema design principles per `.pdXXX`

Each extension defines a single canonical schema used by both extractor, base content, and mod tools. To meet Mike's symmetry directive:

1. **One schema per extension.** `.pdwpn` describes a weapon record regardless of origin. Loader has one parsing path.
2. **Extractor output, base content, and mod-tool output are byte-identical for the same content.** Round-trip clean: extract weapon X, then save unchanged via mod tool, yields the same file.
3. **Schema accommodates both extracted and mod-authored content.** Where extraction produces fields modders cannot author (engine internals, hash signatures), they must be optional. Where mods add fields extraction cannot produce (mod author, license, version), they must be part of the schema as optional.
4. **References are by catalog ID, not by file path.** A weapon referencing `"SFX_FALCON_RELOAD"` resolves through the catalog regardless of whether the SFX is base-extracted or mod-authored.
5. **Per-mod override semantics live in the mod's manifest, not in a `parent:` field on the asset.** (Correction from prior pass; see Section 3.5 for the full mechanism per directive 4.)

**JSON / INI usage convention (per directive 17):**

- **JSON for structured data** (`.pdwpn` with nested fields, `.pdscenario` with prop arrays and AI hooks, `.pdmesh` with skeleton trees). Trade-off: parser cost is higher, but schema is expressive enough for nested objects.
- **INI for flat data** (`.9slice` with four numbers, simple manifests). Trade-off: cheap to parse, easy for modders to edit by hand, no nesting.
- **Include all available fields with comments showing what's possible**, even when the field is unused or default. Helps modders discover capabilities without docs. JSON does not natively support comments; use a JSON5 parser or a `// comments` extension. Alternative: every `.pdXXX` ships with an adjacent `.pdXXX.template` that lists every available field with documentation comments; the runtime ignores templates.

Concrete schema sketches per type (full design out of scope for this audit; outlining shape only).

**`.pdwpn`** sketch (JSON):

```json
{
    "catalog_id": "weapon_falcon2",
    "display_name": "Falcon 2",
    "class": "pistol",
    "slot": "SLOT_SECONDARY",
    "mesh_id": "weapon_falcon2_mesh",
    "firing": {
        "rate": 4,
        "ammo": 8,
        "damage": 18,
        "sfx_fire_id": "sfx_falcon2_fire",
        "sfx_reload_id": "sfx_falcon2_reload",
        "recoil_pattern": "pistol_light"
    },
    "visuals": {
        "hud_icon_id": "ui_icon_falcon2"
    },
    "flags": {
        "silenced": false,
        "scope": false,
        "wallbang": false
    }
    // "variant_of": "weapon_falcon2"   // reserved field; left for variant tools
    // "tags": ["pistol", "sidearm"]    // reserved; modder-discoverable categorization
}
```

A variant: `weapon_falcon2-silenced.pdwpn` carries its own complete record with `flags.silenced: true`, distinct catalog ID, distinct display name. Not an inheritance from the base Falcon 2 (per directive 5).

**`.pdui`** sketch (ZIP container; structurally identical to a `.pdmod`):

```
mod.json                             # manifest with id, version, override declarations
textures/<name>.tga                  # extracted RGBA32 textures (top-down 32-bit)
textures/<name>.png                  # PNG mirror for modder editing
textures/<name>.9slice               # per-texture nineslice INI (4 numbers)
themes/<theme>.json                  # theme palette / scanlines / tint
```

**`.pdmesh`** sketch (JSON manifest plus binary blobs in a container):

```json
{
    "catalog_id": "chr_carrington",
    "display_name": "Daniel Carrington",
    "gbi": {
        "segments": "geometry.bin",        // sibling binary blob
        "vertices": "vertices.bin"
    },
    "materials": [
        { "slot": 0, "texture_id": "tex_carrington_diffuse" }
    ],
    "skeleton": {
        "bones": [
            { "name": "ROOT",     "parent": -1, "rest_pose": [0, 0, 0] },
            { "name": "PELVIS",   "parent": 0,  "rest_pose": [0, 1, 0] }
        ]
    },
    "attach_points": [
        { "name": "HEAD_TOP", "bone": "HEAD",  "transform": [0, 0.2, 0] },
        { "name": "FACE",     "bone": "HEAD",  "transform": [0, 0, 0.1] }
    ],
    "collision": { "capsule": [0, 0.9, 0, 0.4] },
    "animations": ["chr_carrington_idle", "chr_carrington_run"]
}
```

`attach_points` is reserved for the future accessories system (forward-looking note 1; see Section 3.14). Modders adding custom characters declare attach points; accessories reference by name.

**`.pdsong`**, **`.pdsfx`**, **`.pdvoice`** sketches (audio):

```json
{
    "catalog_id": "song_pelagic",
    "display_name": "Pelagic II",
    "format": "ALSEQ",                  // or "OGG_REPLACEMENT" for mod-side replacement
    "tempo_bpm": 120,
    "loops": true,
    "data": "song_pelagic.bin"          // sibling binary; format depends on `format`
}
```

```json
{
    "catalog_id": "sfx_falcon2_reload",
    "display_name": "Falcon 2 reload",
    "format": "ALADPCM",
    "sample_rate_hz": 22050,
    "data": "sfx_falcon2_reload.bin"
}
```

```json
{
    "catalog_id": "voice_carrington_intro_01",
    "display_name": "Carrington intro line 1",
    "actor": "carrington",
    "format": "ALADPCM",
    "sample_rate_hz": 22050,
    "transcript": "Joanna, your first mission is critical.",
    "language": "en",
    "data": "voice_carrington_intro_01.bin"
}
```

**`.pdscenario`** sketch (Pass 5: explicit per-spawn-point weapon and pickup declarations):

```json
{
    "catalog_id": "scenario_villa",
    "display_name": "Villa",
    "stage_num": "SOLO_VILLA",
    "kind": "solo",                     // "solo" | "mp" | "firingrange" | "coop"
    "geometry_id": "bg_villa",          // ref to .pdseg
    "tiles_id": "tiles_villa",          // ref to .pdtiles
    "props": [
        {
            "spawn_id": "prop_001",
            "asset_id": "base:prop_crate_metal",  // ref to .pdprop
            "transform": {"pos": [10, 0, 5], "rot": [0, 90, 0]}
        }
    ],
    "weapon_spawns": [                  // per-location weapon pickups
        {
            "spawn_id": "wpn_001",
            "asset_id": "base:weapon_falcon2",     // ref to .pdwpn (catalog ID)
            "transform": {"pos": [12, 0, 8], "rot": [0, 0, 0]},
            "ammo_count": 24,
            "respawn_seconds": 30
        },
        {
            "spawn_id": "wpn_002",
            "asset_id": "base:weapon_cmp150",
            "transform": {"pos": [-5, 0, 12], "rot": [0, 180, 0]},
            "ammo_count": 60,
            "respawn_seconds": 45
        }
    ],
    "pickup_spawns": [                  // per-location item pickups (ammo, armor, etc.)
        {
            "spawn_id": "pkup_001",
            "asset_id": "base:pickup_armor_full",
            "transform": {"pos": [0, 0, 0], "rot": [0, 0, 0]},
            "respawn_seconds": 60
        }
    ],
    "ai": [...],                        // AI spawn points, patrol paths
    "events": [...],                    // objective triggers, scripted events
    "music_id": "song_villa",           // ref to .pdsong
    "ambient_sfx_ids": ["sfx_villa_amb"],
    "lang_id": "lang_villa",
    "modes": {                          // per-mode initialization (Section 3.16.3)
        "campaign": {...},
        "combat_sim": {
            "spawn_points": [...],
            "default_weapon_set_id": "base:wepset_classic",   // ref to .pdwepset
            "supported_modes": ["combat", "kingofthehill"]
        }
    }
}
```

Note on the `weapon_spawns` and `pickup_spawns` arrays: these are per-location declarations using catalog IDs. Modders can replace the spawn list (via a custom scenario for the same map, or via per-location overrides through a future scenario-edit tool); the schema explicitly carries asset references so the data is portable.

**`.pdwepset`** sketch (Pass 5: weapon set as a compound mod type):

```json
{
    "catalog_id": "modder:halo_power_weapons",
    "type": "weapon_set",
    "display_name": "Halo Power Weapons",
    "description": "High-tier Halo weaponry",
    "weapons": [
        "modder:halo_rocket_launcher",
        "modder:halo_sniper_rifle",
        "modder:halo_energy_sword"
    ],
    "spawn_density": "low",             // "low" | "medium" | "high"; affects MP map distribution
    "respawn_seconds": 60,              // default respawn for set members in MP
    "tags": ["mp", "power"]             // discoverable categorization in MP setup picker
}
```

A `.pdwepset` registers as `ASSET_WEAPON_SET` in the catalog. It is a compound asset (carries references to other catalog IDs, not raw bytes). MP setup's weapon-set picker lists all enabled `.pdwepset` entries; selecting one establishes the spawn pool for the match. The `random_source:` field on MP weapon-setup config (Section 3.16.3) accepts `weapon_set:<catalog_id>` to scope random selection to a specific set.

Validation at registration: each `weapons:` entry must resolve to an enabled `.pdwpn` catalog ID. If any reference is unresolvable or disabled (Section 3.16.11), the set fails registration with `LOUDFAIL.CATALOG.WEPSET_INVALID` listing the unresolvable references. (Extrapolation E-34: this validates at registration time, after all enabled mods have registered, so the set author sees clear errors when their referenced weapons are not present.)

The exact schema for each asset class is a follow-on design task. The audit's recommendation is to commit to the principle and design the schemas one asset class at a time, starting with `.pdwpn` (the F11-F13 catalog work just shipped weapons; this is the natural next step).

### 3.4 Directory taxonomy

Three top-level content trees. Same loader, same schema. The directory split tracks redistribution status (and authoring path), not loading mechanism. Per-tree on-disk shape differs:

| Tree | Purpose | On-disk shape | Ships with release? | Authoring source |
|---|---|---|---|---|
| `base/` | Project-authored, redistributable, decompiled-source-derived | Per-asset `.pdXXX` files (per-asset granularity) | Yes | PD2 contributors (in repo) |
| `data/` | BYOR runtime-extracted from user's ROM | Per-asset `.pdXXX` files (per-asset granularity) | No (gitignored) | First-launch extractor on user machine |
| `mods/` | User-installed mods plus bundled mods plus extracted modpack contents | Compound-only: `.pdmod` archives and `.pdmodpack` archives at top level; extracted modpack contents in subdirs (`mods/<packname>/<inner>.pdmod`) | Sometimes (bundled mods like `bot-names.pdmod`) | Modders; PD2 (for bundled defaults) |

**On-disk shape distinction (Pass 4, per Section 3.16.0):**

- `base/` and `data/` keep per-asset granularity. Files like `base/weapons/weapon_falcon2.pdwpn`, `data/audio/sfx/sfx_falcon2_fire.pdsfx`. They are not subject to the "sharing" complexity that drives the compound-only model in mods/.
- `mods/` contains compound-only files. Atomic assets (mesh, textures, audio, animations, behavior) are bundled INSIDE compound `.pdmod` archives, not loose. Per Section 3.16.0, this enforces "one file equals one mod" and eliminates user-facing dependency management for atomic assets.

**Catalog discovery:** the catalog walks all three trees on startup. For `base/` and `data/` it loads each per-asset file directly. For `mods/` it parses each compound's `mod.json` and registers the declared internal atomic assets (Section 3.16.0). All registrations enter a single asset registry keyed by catalog ID.

**Catalog ID uniqueness invariant (Section 3.5 D-2):** every catalog ID is unique across base, data, and all enabled mods. Duplicate IDs LOUDFAIL at registration (`LOUDFAIL.CATALOG.DUPLICATE_ID`). The naming-disallow rule (mods cannot reuse names already in base or data) is a special case of this invariant; the broader rule extends to mods cannot share IDs with each other either.

**Multi-ROM consideration (extrapolation E-3):** PD2 supports six ROM versions in `tools/extract:321` `vals[]` (currently only NTSC-final at runtime per `port/src/romdata.c:42-62`). Per directive 15, expanding runtime support to all six is approved as future work. Recommendation: under the multi-ROM model, `data/` becomes `data/<romid>/` (or a top-level `data/_active/` symlink that the catalog points at after determining the active ROM via SHA-256 hash check at startup). Avoids cross-version contamination if a user swaps ROMs.

Implication for existing content: `base/weapons.pdbase` (the F11-F13 monolith) is a transitional shape. Splitting into `base/weapons/weapon_*.pdwpn` is the convergent target.

### 3.5 Mods are additive (no overrides)

> **Pass 4 architectural shift (2026-04-30):** Mike eliminated mod overrides as a first-class mechanism. Mods are strictly additive. The base game is permanent canonical content; mods extend it but never replace it.

The original draft of this section described an override flag and multi-mod precedence rules. Those mechanisms are removed. The new model:

**Additive-only invariant (D-1).** A mod's content is added to the catalog alongside base and data content. Total conversions are achieved by collections of additive mods (typically bundled in a `.pdmodpack`, see Section 3.16.1). Custom campaigns appear in the campaign menu alongside the base campaign; retextures appear as alternate selectable variants alongside the base; the user picks via existing curation UI surfaces (campaign picker, weapon picker, character selection, etc.).

**Catalog ID uniqueness invariant (D-2).** Every catalog ID is unique across the entire registered set: base, data, and all enabled mods. No two registrations share an ID. Concrete consequences:

- A mod that ships a retextured Falcon 2 names it `weapon_falcon2-hd-retexture` (distinct ID), not `weapon_falcon2` (which is base's ID).
- A mod that adds an entirely new weapon names it however the modder likes (`weapon_my_custom_thing`) provided the ID is not already taken.
- Variants per directive 5 (`weapon_plasmarifle-brute`) are independent assets with independent IDs; the suffix is human-readable provenance, not catalog hierarchy (extrapolation E-4).

**LOUDFAIL on duplicate ID (D-3).** When two mods register the same catalog ID, the catalog hits `LOUDFAIL.CATALOG.DUPLICATE_ID` with a clear message:

```
LOUDFAIL.CATALOG.DUPLICATE_ID: catalog ID 'weapon_falcon2-hd' is registered by both mod 'mymod_falcon_pack' and mod 'othermod_hd_textures'. IDs must be unique. Rename one before re-enabling.
```

Both mods fail to register their conflicting entries (audit recommendation: the catalog reads them in load order, the first to register wins, the second LOUDFAILs and fails to register the conflicting asset; the rest of the second mod's content registers normally). Open question for Mike's call: hard-fail-both vs first-wins-and-warn-second; audit defaults to first-wins-and-warn-second since that lets the user keep both mods enabled and only the conflicting item is dropped.

**No naming-disallow rule against base.** The earlier draft's rule "mods cannot reuse names that already exist in base or data" stays in effect: it is just a special case of D-2. A mod registering `weapon_falcon2` collides with base's `weapon_falcon2` and LOUDFAILs.

**Total conversions become modpacks of additive compounds.** A "Halo total conversion" is a `.pdmodpack` bundling N compound `.pdmod` files that together form a cohesive experience: a custom campaign (selectable in campaign menu), custom weapons (selectable in loadout pickers), custom characters (selectable in character pickers), custom UI theme (toggleable in Settings). The modpack ships them all; the user installs the pack; each constituent mod surfaces in its appropriate UI list.

**Load-order complexity collapses.** Because no two mods target the same catalog ID, the load-order-as-override-precedence question (which the prior draft treated as Q-2) goes away. Load order matters only for `requires:` dependency resolution between compound mods (Section 3.16.5 to 3.16.7). The `priority:` field is kept for forward compatibility but documented as rarely needed (Section 3.16.2).

**Override audit log retained as duplicate-ID audit log.** The earlier extrapolation about per-startup catalog logging applies in modified form: the catalog logs every registered ID at startup with `CATALOG: register <catalog_id> from <source>` (where source is `base`, `data:<romid>`, or `mod:<mod_id>`). Conflicts surface as `LOUDFAIL.CATALOG.DUPLICATE_ID` per D-3.

**UX implications (extrapolation E-28).** The campaign menu, weapon-loadout picker, character selector, and other curation UIs become enriched with mod-introduced variants. Audit recommends: a "Built-in" header for base content, then a list of each enabled modpack's contributions grouped by source modpack. Out of scope for this audit; flagged for the future modding-hub UX pass.

**AllInOneMods legacy implication (extrapolation E-27).** The historical AllInOneMods (GEX, Kakariko, Goldfinger 64, Dark Noon) replaced base content. Under the additive-only model, those need to be reauthored as additive collections that appear as separate selectable options. Concretely: GEX becomes "GEX Campaign" in the campaign menu; the GEX weapons appear as `weapon_gex_pistol`, `weapon_gex_<x>` in the loadout picker; the user picks GEX as their campaign. The conversion is non-trivial (existing AllInOne content currently rebinds base IDs); flagged as a future migration pillar.

**Pass 5 refinement: presentation-layer disable for total conversions.** Pass 4 established mods are strictly additive; Pass 5 adds a complementary mechanism that lets total-conversion mods hide base content from selectors WITHOUT replacing it. See Section 3.16.11 for the full mechanism. Summary:

- Each catalog entry has an `enabled: true / false` flag (default true).
- Mods declare `disable_base: [list_of_catalog_ids]` in their metadata; when the mod is enabled, those base IDs are filtered from selectors.
- User UI can flip `enabled` independently for per-content visibility control.
- Direct catalog lookup by ID still works regardless of `enabled` (so cross-references from other mods do not break).

This is NOT an override. Base content stays canonical and untouched. The catalog never rewrites a base entry; it just hides it from UI selectors when a filter says so. A "Halo total conversion" mod declares `disable_base:` listing all base PD weapons / characters / vehicles / maps and adds Halo content as additive entries; when the mod is enabled, the user sees only Halo content; when disabled, base PD returns. Both can coexist (`enabled: false` on PD content + Halo additive content = pure Halo experience).

The presentation-layer mechanism preserves the additive-only invariant (no two mods register the same catalog ID) while still enabling the total-conversion UX. Both AllInOne-style total conversions and surgical "use my variant instead" selective replacements compose cleanly under this model.

### 3.6 Hash-verify, self-heal, and corruption quarantine

Per directive 7, `data/` is the runtime source of truth, but it lives on the user's filesystem and can be corrupted (intentionally by a modder editing a base file in-place, or unintentionally by disk error / crash mid-write / tooling bug). The hash-verify-and-self-heal model:

**Manifest file (extrapolation E-11).** Each `data/` tree carries a `data/<romid>/manifest.pdmanifest` listing every file with its expected SHA-256. The first-launch extractor produces this manifest. The runtime verifies against it on each launch.

**Verify on launch (D-7).** During catalog startup, walk the manifest, hash each file, compare. Three outcomes per file:

1. **Match**: load normally.
2. **Mismatch**: corrupted-or-edited. Trigger self-heal.
3. **Missing**: file is gone. Trigger self-heal.

**Self-heal flow (D-8).**

1. Acquire writable mode for `data/` (Section 3.7 read-only model).
2. If the file exists but is mismatched, copy it to `data/.quarantine/<romid>/<asset_path>.<timestamp>.bak`. Retain ONE quarantine snapshot per asset path (latest wins; older snapshots evicted).
3. Re-extract the missing or replaced file from the ROM into `data/`.
4. Verify the re-extract against the manifest. Loud-fail (LOUDFAIL channel) if it still does not match.
5. Drop writable mode; `data/` returns to read-only.

**Quarantine framing (D-9).** Mike's framing per directive 7: "the project as a safe haven for modders." A modder who accidentally edited `data/weapons/weapon_falcon2.pdwpn` in place (because they did not understand the read-only-with-extraction-write model) does not lose their changes. The corrupted-but-modder-authored file is preserved in `.quarantine/`; the modder can copy it out, rename, and ship as a mod proper.

**Quarantine retention (extrapolation E-?):** one snapshot per file. New corruption of the same path overwrites the prior snapshot. Recommendation: log a LOUDFAIL when a quarantine entry is overwritten (the modder's prior accidental edit is now lost; surface that loudly).

**Manifest signature (forward-looking).** Optional: sign the manifest with a project-side key so users can detect manifest tampering (someone replacing manifest.pdmanifest with one that lists their corrupted hashes as expected). Out of scope for v1; track as future work.

### 3.7 Read-only `data/` with writable-during-extraction

Per directive 8: under steady state, `data/` is read-only at the filesystem level. The first-launch extractor and the self-heal path acquire writable mode temporarily.

**Permissions model (D-10):**

1. After the first-launch extraction completes, the extractor calls `chmod` (or Windows ACL equivalent) to mark every file in `data/<romid>/` as read-only. Manifest is written last and also marked read-only.
2. Subsequent launches verify hashes (Section 3.6); the read-only state is honored. The catalog reads but does not write.
3. Self-heal acquires writable mode by `chmod`-ing the affected file or directory back to writable, performing the move-to-quarantine and re-extract, then dropping back to read-only.
4. Re-extraction triggered by `--reextract-all` or `--reextract-<category>` or first-launch-after-update similarly toggles writable for the duration.

**Why read-only matters (extrapolation E-?):**

- A modder cannot accidentally edit `data/weapons/weapon_falcon2.pdwpn` thinking it is their mod's file. The OS rejects the write.
- A buggy mod tool cannot corrupt base content by writing to a `data/` path; the OS rejects the write.
- A user investigating their content with a text editor sees "read-only" in the title bar and is reminded to save-as into `mods/` instead of overwriting in place.

**Implementation considerations:**

- Cross-platform: Windows ACLs and POSIX `chmod` express read-only differently; the extractor needs platform-specific code. SDL2 already handles file ops abstraction; `fs.c` is the right place for a `fsSetReadOnly(path)` helper.
- `data/.quarantine/` should NOT be read-only (the self-heal path writes to it). Skip it during the read-only sweep.
- The manifest file specifically: read-only is critical (manifest tampering undermines the whole verify chain). Write last, then mark read-only, then write the "extraction complete" sentinel that the catalog uses to skip re-extraction on subsequent launches.

### 3.8 LOUDFAIL log channel

Per directive 9, silent recoveries undermine debuggability. Introduce a `LOUDFAIL` log channel for cases where the runtime detected a problem and recovered, where the recovery is invisible to the user but should be visible to a developer or to a modder triaging their work.

**Channel taxonomy (D-11).** Recommendation: top-level `LOUDFAIL.*` namespace with subsystem suffixes:

- `LOUDFAIL.CATALOG`: asset registry conflicts, override resolution failures, missing referenced catalog IDs.
- `LOUDFAIL.EXTRACT`: ROM-extraction errors (bad offset, decoder failure, write failure) that caused the extractor to skip or substitute.
- `LOUDFAIL.LOAD`: file-load failures the runtime recovered from (corrupted `.pdXXX`, schema mismatch, broken reference).
- `LOUDFAIL.HEAL`: self-heal events (file detected corrupted, quarantined, re-extracted).
- `LOUDFAIL.PROC`: procedural fallback fired (substitute synthesized because expected asset was missing or invalid).

**Log format suggestion:**

```
LOUDFAIL.PROC: ui_chrome_frame: ROM extraction unavailable (no ROM addresses identified for chrome source); fell back to procedural synthesis
LOUDFAIL.HEAL: weapon_falcon2.pdwpn hash mismatch (expected abc12345..., got def67890...); quarantined as data/.quarantine/.../weapon_falcon2.pdwpn.20260430-153022.bak; re-extracted from ROM
LOUDFAIL.CATALOG: override declaration in mod 'mymod_total_falcon_overhaul' references catalog_id 'weapon_farsight_v2' which does not exist in base or data
```

**LOUDFAIL UI surface (extrapolation E-6).** A diagnostics panel in the dev menus (`pdgui_menu_moddinghub.cpp` is the existing surface) showing the most recent LOUDFAIL events with timestamps and context. Modders triaging "why is my mod not loading" check this panel first. Out of scope for the audit but flagged for the larger logging-pipeline-cleanup pass.

**Logging-pipeline cleanup (D-12).** Per directive 9, the LOUDFAIL channel naturally folds into a broader logging-pipeline-cleanup pass (similar in scale to the context-system rebuild that just shipped). Track as a future architectural pillar; not in scope for this audit.

### 3.9 `.pdmodpack` mod-pack architecture

Per directive 6, modpacks are containers for multiple mods. Enabling / disabling a modpack affects all linked mods; per-component disable also possible.

**Storage model recommendation (extrapolation E-5; Mike's open question).**

Two possible models:

1. **Contain (recommended).** `.pdmodpack` is a ZIP archive of `.pdmod` files plus a `pack.json` metadata file. On install, the user's mod manager extracts the `.pdmod` files into `mods/<packname>/<modname>.pdmod` and writes a `mods/<packname>/pack.json` that records the constituent mod IDs.
2. **Reference.** `.pdmodpack` is a manifest only, with URLs or version declarations pointing at separately-distributed `.pdmod` files. Installer fetches each component on install; user has the responsibility to verify provenance.

Recommendation: contain. Reasons:
- Self-contained distribution: the user downloads one file and the modpack works offline.
- Reference model has dependency-resolution failure modes (linked mod taken down, version mismatch, fetch failure mid-install) that a containment model avoids.
- Installation is reversible by deleting `mods/<packname>/`.
- Containment matches user expectations from `.zip`-based workflows everywhere else.

The cost is duplicated bytes if a user has the same mod installed standalone and inside a pack. Acceptable trade-off for v1; if duplication becomes a real problem, a hybrid model (contain by default, optional reference for "this pack requires the latest live version of mod X") can layer on later.

**Hierarchical UI grouping (D-13).** The modpack's `pack.json` records the constituent mod IDs. The mod manager UI displays modpacks as collapsible parent rows with the constituent mods nested underneath. Toggling the pack toggles all constituents; toggling a constituent toggles only that mod (the pack becomes a "partially enabled" parent).

**Storage path layout:**

```
mods/
  <packname>.pdmodpack             // the original archive (kept for reinstall)
  <packname>/                       // extracted on install
    pack.json                       // pack metadata (id, version, constituent_mod_ids)
    <mod1>.pdmod
    <mod2>.pdmod
    <mod3>.pdmod
  mymod_solo.pdmod                  // standalone mod, sibling to packs
```

**Bundled-with-release modpacks** (extrapolation E-?): a `.pdmodpack` could ship as a release-time bundling of project-author-curated mods (e.g. "PD2 starter pack" with bot-names + base-ui + modern-ui). Same shape as user-installed modpacks; lives in `mods/` directly.



### 3.10 Functional gaps in PD2's current extraction

Listed by priority order under the architectural principle. Severities reflect Mike's directives:

**G-1. Runtime extractor's output directory is misaligned with the `.pdmod` archive convention.** Severity: medium.

`pdguiThemeExtractRomTextures()` writes loose files to `mods/base-ui/textures/`, while the mod system migrated to `mods/base-ui.pdmod` archive form. Per directive 19, `mods/base-ui.pdmod` is broken legacy and should be retired. Recommendation: redirect runtime extractor output to `data/ui/pd-original.pdui` (a single `.pdui` archive in the BYOR `data/` tree, structurally identical to a modder-authored `.pdui`). The existing `.pdmod` infrastructure (`port/src/modarchive.c`, `port/src/modvfs.c`) handles ZIP archive read; extraction-time write needs a small new path that builds a ZIP archive instead of loose files.

**G-2. Audio extraction is build-time only and not split by category.** Severity: medium.

`tools/extract:62` (`extract_audio`) writes monolithic `extracted/<romid>/sfx.{ctl,tbl}` and `seq.{ctl,tbl}`. At runtime, `port/src/preprocess/segaudio.c:316` (`preprocessALBankFile`) re-reads the same bytes from the in-memory ROM and runs endian / pointer fix. Convergent under the principle requires a runtime extractor that produces `data/audio/music/song_*.pdsong`, `data/audio/sfx/sfx_*.pdsfx`, and `data/audio/voice/voice_*.pdvoice` once at first launch. Per directive 2, the three categories are organized into separate folders with separate extensions. Migration path: build per-category audio writers (separate the SFX bank into per-effect `.pdsfx`, separate the sequence bank into per-song `.pdsong`, identify and separate voice samples from SFX as `.pdvoice`), point the audio loader at `data/audio/`.

**G-3. Mesh / model extraction is build-time only.** Severity: medium.

Same shape as audio: `tools/extract:130` writes per-character `.bin` blobs; runtime `preprocessModelFile` (`port/src/preprocess/filemodel.c`) re-converts on each load. Convergent migration: emit `data/meshes/chr_*.pdmesh` once at first launch in post-conversion shape, with attach-point declarations for the future accessories system.

**G-4. Stage / scenario extraction is build-time only.** Severity: medium.

`tools/extract:138` writes per-stage `setup` blobs; runtime `preprocessSetupFile` (`port/src/preprocess/filesetup.c`) re-converts. Migration: emit `data/scenarios/setup_<stage>.pdscenario` once. Companion files for the same stage: `data/scenarios/<stage>/geometry.pdseg`, `data/scenarios/<stage>/tiles.pdtiles`.

**G-5. Procedural chrome generator is incomplete implementation, not aspirational.** Severity: HIGH (per directive 11).

Comment at `port/fast3d/pdgui_theme.cpp:2845` describes the procedural chrome as a stopgap awaiting ROM-address identification. Mike's directive: this is a high priority gap. The synthetic chrome diverges from OG-PD look; the real chrome is assembled by `menugfxRenderDialogBackground()` compositing palette colors plus textured strips. Recommendation: investigate the ROM addresses, extract the source art into `data/ui/pd-original.pdui`, retire the procedural generator. Project author Chris will be happy when this lands.

**G-6. ROM hash validation is implemented but disabled.** Severity: medium (becomes high once extraction is the runtime source of truth, per directive 13).

`port/src/romdata.c:227-246` declares per-region SHA-256 known-good arrays but they hold only `NULL`. Per directive 13: enabling validation is the gate that lets the extractor pick the right per-version offset table. Recommendation: capture hashes from a known-good ROM dump for each of NTSC, PAL, JPN final (and beta variants if Mike approves), populate the arrays, ship with the next release. Wire the validation result into the offset-table selection so a misidentified ROM aborts extraction with a user-facing dialog rather than extracting garbage.

**G-7. CLI extract flags discoverability is the wrong abstraction.** Severity: low (subsumed by directive 14).

Per directive 14, extraction should fire automatically on launch (gated by hash check), with a loading-screen modal during the first-launch run. Subsequent launches bypass when hashes match. CLI flags become developer / power-user surface, not end-user-facing. Recommendation: implement the auto-extraction-on-launch model; CLI flags can be added as power-user override but are no longer the primary trigger.

**G-8. Zero test coverage on the most load-bearing pillar's bootstrap.** Severity: HIGH (per directive 12).

Catalog is THE pillar that owns single-source-of-truth for all assets at runtime. Extraction is its bootstrap input. Zero `pd-tests` cases exercise `pdguiThemeCheckExtract` / `pdguiThemeExtractRomTextures` or any of the build-time extractors. A regression in extraction does not surface until a user reports broken UI textures. This is structurally unacceptable for the most load-bearing pillar.

Recommendation: add a `pd-tests` scope `extraction` covering:
- Missing-files mask logic (reset-and-reextract on partial extraction)
- Procedural fallback substitution with LOUDFAIL emission
- Schema validation per `.pdXXX` (well-formed, schema-conforming, references resolve)
- Round-trip via mock ROM blob (synthetic test ROM, not a real Rare ROM)
- Hash-mismatch detection plus self-heal flow (Section 3.6)
- Read-only-with-writable-during-extraction permission flips (Section 3.7)

**G-9. Procedural fallback is silent.** Severity: medium (per directive 10).

`s_generateProceduralTexture` substitutes synthetic art when ROM extraction returns bad data. The substitution emits `LOG_WARNING` lines but no diagnostic surface; users and modders cannot tell at a glance whether they are seeing authentic or synthetic textures. Recommendation: route through `LOUDFAIL.PROC` channel (Section 3.8) with specific failure mode (`file-not-found` / `rom-offset-invalid` / `decoder-error`); substitute as fallback, but loud.

**G-10. AllInOne mod-override branch in `romdataFileLoad`.** Severity: low (cleanup target).

`port/src/romdata.c:710` has a `r.is_mod_override && r.path` branch that calls `fsFileLoad(r.path)` with arbitrary paths from the catalog. Per directive 18, this is likely AllInOne residue. Under the new `data/` plus `base/` plus `mods/` model with canonical `.pdXXX` schema and explicit override semantics, this branch is not the right shape. Recommendation: deprecate during the per-asset-class migration; remove once all asset classes route through the new model.

**G-11. `src/generated/<romid>/` is source-tracked build artifact.** Severity: low (per directive 16).

Once extraction matures, generated content should not be source-tracked. Either:
- Extract directly into runtime `data/` with no source-tree intermediate, or
- Run extraction in memory only during the build with no on-disk intermediate.

Recommendation: TODO. Migration depends on the asset-class-by-asset-class shift to the new model; `tools/assetmgr/mk*` retires as each class moves.

### 3.11 Architectural improvements (the model shift)

**A-1. Migrate the runtime asset path from "live-mapped ROM" to "first-launch extract then read disk."**

Today: every gameplay file load goes through `romdataFileLoad` (`port/src/romdata.c:691`), which in the no-mod-override case reads from `g_RomFile` and runs `preprocessXxxFile` on the bytes. Tomorrow: every gameplay file load goes through the catalog, which resolves to a `.pdXXX` file in `data/`, `base/`, or `mods/`, and the loader parses pre-converted bytes.

Migration sequence (one asset class at a time, mirroring the F11-F13 catalog gate model):

1. Define the `.pdXXX` schema for the class (e.g. `.pdwpn`).
2. Build the runtime extractor that emits the `.pdXXX` from ROM at first launch.
3. Build the loader that reads the `.pdXXX` and produces the engine's runtime structure.
4. Switch the catalog to route the class through the new path; legacy ROM path stays as fallback.
5. After verification, remove the legacy ROM path for that class.
6. Move to next class.

This is the Catalog Gate cadence the project already uses; the audit recommends formalizing it as the architectural plan and putting it in `context/roadmap.md`.

**A-2. Make the bootstrap extractor a single in-binary subsystem.**

Today the only runtime extractor is `pdguiThemeExtractRomTextures` plus its call site. Tomorrow there is `romBootstrap*` covering all asset classes. Recommendation: a single `port/src/rombootstrap.c` (or per-category `rombootstrap_<category>.c` family) with a dispatch table across asset classes, called once on first launch. Each class has its own writer; all writers output to `data/<romid>/<category>/`. CLI flag `--reextract-<category>` re-runs one writer; `--extract-all` re-runs all.

The bootstrap also writes the `data/<romid>/manifest.pdmanifest` (Section 3.6 hash table) and acquires / drops the read-only filesystem state (Section 3.7).

**A-3. Catalog gets an extracted-content provider.**

The catalog already routes through `assetprovider_rom.c` for ROM-backed reads (`port/src/assetprovider_rom.c`). Add a sibling `assetprovider_data.c` for `data/`-backed reads (loading `.pdXXX` files). Once all classes are migrated, the ROM provider goes away and the data provider plus mod provider are the only two.

**A-4. Mod tooling becomes the canonical authoring surface.**

Once the schema is symmetric, the mod tools (theme editor, skin editor, future weapon editor, future scenario editor) author exactly the shape the extractor produces. A modder iterating on a custom weapon does not need a ROM. The pdgui_skin_editor, pdgui_menu_theme_editor, pdgui_menu_moddinghub already exist (`port/fast3d/pdgui_*.cpp`); their output should converge on `.pdXXX` rather than the older bespoke formats. Per directive 20, mod tools should also be able to load any base content as a starting template for modder forks (open `data/scenarios/setup_villa.pdscenario` to start a new mission).

**A-5. ROM-free mode becomes a future option, not a near-term goal.**

Once `data/` is the runtime source, a community-shared `data-bundle.zip` with hash-validated extracts could ship without the ROM. Out of scope today but the architecture preserves the option. Document in `context/roadmap.md` as a long-term capability gate.

**A-6. Multi-ROM runtime support (per directive 15).**

The `tools/extract:321` `vals[]` table already keys six ROM versions; runtime is currently NTSC-final-only per `port/src/romdata.c:42-62`. Mike approved expanding runtime support to all six. Recommendation: data layout becomes `data/<romid>/` with the catalog selecting the active ROM via SHA-256 hash check at startup. Build target stays per-ROM (NTSC-final binary still differs from PAL binary because game-code constants differ across regions), but the extractor and runtime support whichever ROM the user provided. Track as future work in `context/roadmap.md`.

**A-7. Loading-screen modal during first-launch extraction (per directive 14).**

Extraction fires automatically on launch when hash check fails (first launch, ROM swap, or self-heal cascade). During extraction, a loading-screen modal blocks gameplay startup with progress per category. Subsequent launches bypass when manifest verification passes.

The existing pdgui infrastructure can host this; recommend adding a `pdguiBootstrapModal()` surface that renders a centered ImGui window with a progress bar and per-category status. Hooks into the LOUDFAIL channel for any extraction errors so they surface in the user's first-time experience.

### 3.12 Tooling gaps

Recommendations for tooling that does not exist today but would benefit from the architectural shift:

**T-1. `data/` integrity verifier.** Implicit in the hash-verify-on-launch flow (Section 3.6). The catalog already does this on every launch; surfaces as a CLI flag `--verify-data` for power users.

**T-2. `data/` re-extraction ergonomics.** `--reextract-<category>` (per asset class) and `--reextract-all` (full nuke and rebuild). Map to the same in-binary subsystem (A-2). Visible in `--help`.

**T-3. Mod-from-extract scaffolder.** A modder creating a custom weapon should be able to open `data/weapons/weapon_falcon2.pdwpn` from inside the mod tool, edit, and save into their mod folder with the catalog ID rewritten and the override declaration auto-populated. Bootstraps the mod-creation loop. Lives in the existing `pdgui_menu_moddinghub.cpp` family per directive 20.

**T-4. Schema validator.** Each `.pdXXX` schema gets a JSON Schema (or equivalent) that the loader validates against on load. Catches mod authoring errors at load time with clear LOUDFAIL messages.

**T-5. CI extraction smoke test.** A synthetic test ROM fixture (not a real Rare ROM) plus a check that all `.pdXXX` extractors produce the expected files with the expected schema. Lives in `pd-tests` under the new `extraction` scope. Validates round-trip from extractor to schema validator.

**T-6. Manifest signing (forward-looking).** Per Section 3.6, optional signing of the manifest with a project-side key. Out of scope for v1; track as future work.

### 3.13 Mod-friendliness improvements

Direct consequences of the symmetric-schema principle plus Mike's directives, updated for Pass 4 (compound-only plus additive-only):

**M-1. Mod tools work without a ROM.** Because mods author against the same schema as extracted content, a modder can develop and test against community-shared extracted bundles without owning a ROM. (This is the single largest mod-ergonomics improvement of the entire architectural shift.)

**M-2. Variant naming convention is human-readable (per directive 5).** `weapon_falcon2-silenced` reads as "Falcon 2 silenced variant" at a glance. `weapon_plasmarifle-brute` reads as "Plasma Rifle, brute variant." Modders can identify content by name alone. Variant assets are independent catalog IDs (Section 3.5 D-2), not flag toggles on a shared base.

**M-3. Mod-from-extract round-trip enables reverse engineering.** A modder who wants to understand how the base game configures a weapon opens `data/weapons/weapon_falcon2.pdwpn` in a text editor (JSON surface schema) and reads the fields directly. No need to know ROM offsets, segment tables, or `preprocessGunFile`. The contract is the schema.

**M-4. One-file-equals-one-mod (Pass 4, Section 3.16.0).** Sharing a mod transmits everything it needs in a single `.pdmod` archive. No external dep-resolution at install time for atomic assets (the catalog dedupes by hash at registration). Modder ships one file; user installs one file; everything works.

**M-5. Catalog ID uniqueness is explicit and audit-loggable.** Every registered ID prints to log on startup with `CATALOG: register <id> from <source>`. Duplicates fire `LOUDFAIL.CATALOG.DUPLICATE_ID` with a clear rename instruction (Section 3.5 D-3). Modders diagnose "why is my mod not loading" without reading source.

**M-6. Quarantine preserves accidental edits (per directive 7).** A modder who edited `data/weapons/weapon_falcon2.pdwpn` in place (despite read-only) gets the file moved to `data/.quarantine/` rather than overwritten. The project is a safe haven.

**M-7. INI for flat configs, JSON for nested (per directive 17).** `.9slice` (4 numbers) is INI; `.pdwpn` (nested fields) is JSON. Templates with all-fields-commented surface modder-discoverable capabilities without docs.

**M-8. Mod tools load any base content as a starting template (per directive 20).** Missions, arenas, characters, skins, weapons, audio, vehicles all forkable from base content. The in-client tool COPIES the cataloged asset's bytes into the new compound's internal storage (Section 3.16.0 mod authoring workflow), so the result is fully self-contained. Lowers the barrier from "study the format spec then author from scratch" to "open this thing, change two fields, save."

**M-9. Additive-only architecture removes override-precedence complexity (Pass 4).** Modders never have to worry about "will my mod conflict with another mod that also targets Falcon 2?" because no two mods can target the same catalog ID. A mod's content is added alongside base; the user picks via existing curation UIs. Modders focus on content, not conflict resolution.

**M-10. Hash-dedupe at registration removes duplicate-asset penalty (Pass 4, Section 3.16.0).** A modder who self-contains everything (the default per Section 3.17.3) does not pay a runtime memory cost for redundancy with other mods. If two mods bundle the same texture, the catalog stores it once. The on-disk cost is duplicated; the runtime cost is not. Modders can ship self-contained without architecturally penalizing the user.

**M-11. Provenance audit (extrapolation E-23).** When the in-client tool copies a cataloged asset into a new compound, it stamps an optional `origin:` field on the entry recording the source catalog ID. Lets collaborators trace lineage; supports credit attribution. Modder can opt out if they prefer not to disclose lineage.

**M-12. Total conversions become genuinely composable (Pass 5).** The presentation-layer disable mechanism (Section 3.16.11) plus additive-only invariant (Section 3.5) plus modpack architecture (Section 3.16.1) compose cleanly: a Halo total conversion enables when the user installs the pack, hides base content from selectors, exposes Halo content additively. When the user wants to play base PD again, they disable the modpack and base content reappears. Coexistence is trivial. AllInOne-style total conversions and surgical-replacement variants both compose under this model without conflicting.

### 3.14 Forward-looking work (track but not in scope)

Mike mentioned several features in flight or aspirational that align with the architectural direction. Document in this audit so they are recorded; design out of scope.

**FW-1. Accessories system.** Future feature: characters can have accessories (hats, cigars, glasses, masks) that attach to a default root point on the body. Modders define attach points for custom characters via the `attach_points` field in `.pdmesh` (Section 3.3 sketch). Shooting an accessory does not damage health but knocks it off and triggers audio plus animation. The accessories themselves are independent `.pdmesh` files plus an `attach_to:` metadata field referencing a target attach-point name. Aligns with the symmetric-schema principle: accessories are first-class assets.

**FW-2. Mod-driven character behavior.** Future Banjo-Kazooie example: a custom character could pair with custom run animation (`.pdanim`) plus custom run audio (`.pdsfx`) and integrate with a future sprint feature. The mod system architecture should accommodate behavior-shaped mods, not just visual mods. (Extrapolation E-7: a behavioral schema may want to live in a `.pdcharacter` extension separate from `.pdmesh`. Implied: state machines, animation triggers, sprint integration, accessory attach points all belong to the character behavior layer, not the visual layer. Surface for Mike's call.)

**FW-3. Terrain / mesh editor in-client.** Inspired by `n64decomp/mk64`'s Blender hybrid. Modders create custom content directly in-client using controller or mouse-and-keyboard. Future Forge-for-content-creation pillar. Aligns with hot-reload / live-edit (Principle 1, what-this-unlocks point 2): the editor writes directly to `.pdXXX` files in the user's mod folder, no intermediate format, round-trip clean with extraction.

**FW-4. Bundled-with-release modpacks.** Project author can ship a `.pdmodpack` of curated mods alongside the binary (e.g. "PD2 starter pack" with bot-names + base-ui + modern-ui). Same shape as user-installed modpacks; lives in `mods/` directly.

**FW-5. Logging-pipeline cleanup pass (per directive 9).** Comparable in scale to the recently-shipped context-system rebuild. Fold the LOUDFAIL channel introduction into a broader logging refactor (channel taxonomy, levels, in-game LOUDFAIL UI surface, log file rotation, telemetry export). Track as a future architectural pillar; not in scope for this audit.

**FW-6. ROM-free distribution (long-term, per Principle 1 unlocks).** A community-shared `data-bundle.zip` with hash-validated extracts could ship without the ROM. Architecturally feasible once `data/` is the runtime source. Legal / policy questions out of scope here; flag as a future capability gate.

**FW-7. The Grid as Forge-extensible (Pass 5).** The Grid's observer character (currently Dr Carroll) becomes a `.pdcharacter` with a flag `observer_capable: true`. Modders authoring replacement observer characters (e.g. Halo Monitor) register `.pdcharacter` entries with the flag set; The Grid's setup picker filters by the flag. When The Grid loads, the player picks which registered observer character to use. Extends the player-character-selection plumbing already in place to The Grid context.

**FW-8. Catalog-driven prop palette in The Grid (Pass 5).** The Grid's prop palette is auto-populated from the catalog: every entry of type `ASSET_PROP` (i.e. every `.pdprop` registration, base + data + mods) appears in the prop palette UI, filtered by `enabled` and any context filter The Grid applies. Mods adding props extend the palette automatically, no Grid-side code change. Search and category-tag filtering already supported by the catalog universality sweep.

**FW-9. Logic-system mods extending The Grid (Pass 5).** When the logic system pillar (Section 3.17.5 forward-looking note) lands, mods can register custom triggers (events the engine fires) and custom actions (operations the engine can execute) into the trigger / action registry. The Grid's interaction-tool palette consumes the registry to expose the available triggers and actions for users to compose into Forge-style interactive scenes. The combination (FW-7 plus FW-8 plus FW-9) makes The Grid effectively Forge-from-Halo with PD's renderer. Substantial future pillar; track separately from this audit.

### 3.15 Open questions and self-extrapolations

This section surfaces points where Mike's directives left a question open or where the audit author extrapolated something Mike did not explicitly say. Mike to review and confirm or correct.

**Open questions (resolved in Pass 3 walkthrough; see Section 3.16):**

- **Q-1. `.pdmodpack` storage model.** RESOLVED: contain (Section 3.16.1).
- **Q-2. Multi-mod override precedence.** RESOLVED: positional defaults via `load_after:` / `load_before:` plus optional `priority:` field plus drag-reorder UI (Section 3.16.2).
- **Q-3. `.pdscenario` vs `.pdmission` extension name.** RESOLVED: `.pdscenario` confirmed; SP-MP unified via `modes:` block in the schema, with map variants as suffix-named siblings (Section 3.16.3).
- **Q-4. `.pdcharacter` as a separate extension from `.pdmesh`.** RESOLVED: yes, distinct extension and distinct catalog asset type. `.pdmesh` is now strictly static-mesh-visual-only (Section 3.16.4). The Q-4 walkthrough also produced refinements on dependency manifests, optional + fallback deps, and circular-dependency prevention (Sections 3.16.5 / 3.16.6 / 3.16.7).
- **Q-5. Quarantine retention deeper than 1 snapshot.** RESOLVED: counter-based LOUDFAIL with session reset; one snapshot retained but distinct LOUDFAIL message when overwritten, with per-asset counter that resets when the file stops being touched (Section 3.16.8).
- **Q-6. Multi-ROM data layout.** RESOLVED: per-romid subdirs with priority-list ROM selection at extraction time (NTSC-final preferred), plus session-cache for cross-region multiplayer (Sections 3.16.9, 3.16.10).

**Self-extrapolations (independent additions; Mike to review):**

- **E-1. Atomic extraction transaction.** Read-only `data/` plus writable-during-extraction (Section 3.7) implies the extractor needs temp-then-rename or full-replacement-of-`data/` semantics to avoid partial-write corruption mid-extraction. Otherwise a crash mid-extract leaves `data/` in a half-state that the next launch flags as corrupted, retriggers extraction, may crash again (infinite loop risk). Recommendation: extractor writes to `data-tmp-<romid>/`, validates the manifest, then atomically renames `data-tmp-<romid>/` to `data/<romid>/` (or appropriate platform-specific atomic dir-replace). On any failure mid-extraction, `data-tmp-<romid>/` is discarded; the previous `data/<romid>/` (if any) survives.
- **E-2. Multi-mod override precedence default = load order.** See Q-2. Default to mod load order; allow optional `priority:` field for advanced cases. Matches existing modding hub UX.
- **E-3. `data/<romid>/` subdir layout.** See Q-6. Avoids cross-version contamination if user swaps ROMs.
- **E-4. Variant catalog IDs are flat strings.** `weapon_falcon2` and `weapon_falcon2-silenced` are independent IDs. The hyphen suffix is a UI / discovery convention, not a catalog hierarchy. The catalog does not need new structure.
- **E-5. Modpack storage = contain.** See Q-1.
- **E-6. LOUDFAIL UI surface.** A diagnostics panel in `pdgui_menu_moddinghub.cpp` showing recent LOUDFAIL events with timestamps and context. Modders triaging "why did this not work" check this panel first. Out of scope for the audit; track for the logging cleanup pass (FW-5).
- **E-7. `.pdcharacter` schema split from `.pdmesh`.** See Q-4. Behavioral layer (state machines, animation triggers, voice cues) may want its own extension separate from the visual mesh.
- **E-8. Accessories as attachable mini-meshes.** Per FW-1: each accessory is a `.pdmesh` with an `attach_to:` metadata field referencing a target character's attach-point name. Same loader, no new extension.
- **E-9. In-client editor save path.** Per FW-3: the editor writes directly to `.pdXXX` files in the user's mod folder. No intermediate format; round-trip with extraction is symmetric.
- **E-10. `tools/assetmgr/mk*` retirement.** Per G-11 (directive 16): once the runtime extractor matures, the build no longer needs to produce headers from JSON because the runtime reads `.pdXXX` files directly. The `mk*` family retires.
- **E-11. Per-tree manifest with hash table.** `data/<romid>/manifest.pdmanifest` is the audit's recommended location. Lists every file with expected SHA-256. First-launch extractor writes; runtime verifies.
- **E-12. `.pdwpn` references `.pdmesh`, not contains it.** Schema-wise, `.pdwpn` IS a weapon record that REFERENCES a `.pdmesh` for visuals. Same pattern as Mike's catalog model. Means both the weapon record AND the mesh file get extracted; `.pdwpn` is small (config), `.pdmesh` carries bulk.
- **E-13. Mods adding NEW assets vs overriding EXISTING.** Adding `weapon_my_custom_thing.pdwpn` does not need an override declaration. Overriding `weapon_falcon2.pdwpn` does. Naming-disallow rule (no same name as base) plus explicit override flag together implement this distinction.
- **E-14. `.pdtexconfig` retires under the new model.** The texture descriptor table baked into ROM is a ROM-internal index; once textures are extracted into `.pdui` / `.pdmesh` archives, each carries its own format metadata inline. No `.pdtexconfig` extension.
- **E-15. `.pdmpconfig` rolls into `.pdscenario`.** MP configs are stage-shaped; each `.pdscenario` for an MP map carries its weapon set inline.
- **E-16. `.pdfiringrange` rolls into `.pdscenario`.** It is a stage. No reason for its own extension.
- **E-17. `.pdtiles` and `.pdseg` are split sub-resources of `.pdscenario`.** Geometry (`.pdseg`) and walkable surfaces (`.pdtiles`) are sub-resources of a stage. Keep separate so they can be swapped independently. Each `.pdscenario` references both by catalog ID.
- **E-18. Override audit log on startup.** Catalog logs every active override at startup with `CATALOG: override <catalog_id> base=<path> mod=<modname> at <path>`. Audit-loggable; LOUDFAIL when an override declaration cannot be resolved.

### 3.16 Mod architecture refinements (Pass 3, 2026-04-30)

After Mike walked through a Halo fusion-coil prop-mod authoring flow, the open questions Q-1 through Q-6 were resolved with a set of architectural refinements. The compound-mod plus dependency-graph plus session-cache framing emerged from that walkthrough. Recording the resolutions here as authoritative; the prior open-question list (Section 3.15) has been updated to point here.

#### 3.16.0 Compound-only on disk; internal catalog granularity (Pass 4 architectural shift)

> **Pass 4 architectural shift (2026-04-30):** the user's `mods/` folder contains only compound files. Atomic assets (mesh, textures, audio, animations, behavior) are bundled inside compound `.pdmod` archives, not loose. One file equals one mod. No user-facing dependency management for atomic assets. Sharing a mod transmits everything it needs.

This is the foundational refinement that the rest of Section 3.16 cascades from. The prior atomic-vs-compound packaging matrix (Pass 3 Section 3.17.4) is collapsed: every mod is a compound; the on-disk surface in `mods/` is `.pdmod` (or `.pdmodpack`) only.

**On-disk shape:**

```
mods/
  halo_fusion_coil.pdmod                    # compound mod, ZIP archive
  modern_ui.pdmod                           # compound mod, ZIP archive
  goldfinger64.pdmodpack                    # modpack, ZIP archive of compounds
    -> after install:
       goldfinger64/                         # extracted directory
         pack.json
         goldfinger64-campaign.pdmod
         goldfinger64-weapons.pdmod
         goldfinger64-characters.pdmod
         goldfinger64-music.pdmod
```

The mod manager scans only `.pdmod` files (top-level and inside extracted modpack directories) plus `.pdmodpack` files. No loose atomic files; no cross-mod sharing on disk.

**Atomic assets remain a first-class concept inside the catalog.** A compound's manifest declares its internal atomic contents. Each contained atomic registers as its own catalog entry with its own ID. Other mods (or the gameplay engine) reference the atomic by its catalog ID; the catalog resolves to the bytes inside the compound.

Compound manifest sketch (revised for Pass 4; updated for Pass 5 with optional `disable_base:` field):

```json
{
    "id": "modder:halo_fusion_coil",
    "version": "1.0.0",
    "display_name": "Halo Fusion Coil",
    "type": "compound",
    "internal_assets": [
        {
            "catalog_id": "modder:prop_fusion_coil",
            "type": "prop",
            "path": "props/fusion_coil.pdprop"
        },
        {
            "catalog_id": "modder:mesh_fusion_coil",
            "type": "mesh",
            "path": "meshes/fusion_coil.pdmesh"
        },
        {
            "catalog_id": "modder:tex_fusion_coil_diffuse",
            "type": "texture",
            "path": "textures/coil_albedo.tga"
        },
        {
            "catalog_id": "modder:sfx_fusion_coil_boom",
            "type": "sfx",
            "path": "audio/sfx/fusion_coil_boom.pdsfx"
        }
    ],
    "requires": [],
    "disable_base": []                     // Pass 5: optional list of base catalog IDs to hide from selectors when this mod is enabled (Section 3.16.11)
}
```

The `disable_base:` field is empty for this compound (the fusion-coil mod is purely additive). For a Halo total-conversion compound, the field would list all base PD weapons / characters / vehicles / maps it wants to hide from selectors:

```json
{
    "id": "modder:halo_total_conversion",
    "version": "1.0.0",
    "type": "compound",
    "internal_assets": [...],
    "requires": [...],
    "disable_base": [
        "base:weapon_falcon2",
        "base:weapon_cmp150",
        "base:weapon_dy357magnum",
        // ... full base weapon list
        "base:character_carrington",
        // ... full base character list
        "base:scenario_villa",
        // ... full base scenario list
    ]
}
```

When this mod is enabled, the catalog flips `enabled: false` on each listed base entry. The base entries are still present in the catalog (lookups by ID still resolve, so cross-references from other mods do not break); they are just filtered from selector UIs. When the mod is disabled, the catalog flips `enabled` back to true and the base content reappears in selectors.

The compound's archive holds the per-asset-class files (`.pdprop`, `.pdmesh`, `.pdsfx`, raw textures, etc.) at their declared paths. Each is registered into the catalog under its declared ID. Cross-asset references inside the compound use catalog IDs (the `.pdprop` references `modder:tex_fusion_coil_diffuse` for its texture; the catalog resolves to the bytes at `textures/coil_albedo.tga` inside this compound).

**Hash-based deduplication at registration (Pass 4 mechanism):**

When a compound's atomic asset registers into the catalog, the loader hashes the asset's bytes (SHA-256). The catalog maintains two structures:

- An "asset bytes pool" keyed by hash. The bytes are stored once.
- A "catalog ID to hash" mapping. Every registered ID points at a hash.

If two compounds bundle the same texture (same SHA-256), the catalog stores the bytes once. Both compounds' declared IDs point at the single entry. Disk space is duplicated (each compound is self-contained on disk); runtime memory and catalog space dedupe.

If two compounds declare different IDs for assets with the same hash, both IDs register and both point at the shared hash entry. This is the expected case when modder A and modder B independently bundle the same default explosion VFX; they each ship it inside their compound, and at runtime the catalog represents it once.

If two compounds declare the same ID with different hashes, the catalog hits `LOUDFAIL.CATALOG.DUPLICATE_ID` (Section 3.5 D-3): the IDs collide; both fail to register their conflicting entries. The first-loaded compound wins by audit recommendation (load-order resolution), the second LOUDFAILs.

**Mod authoring workflow (Pass 4 mechanism):**

When the in-client mod tool authors a new compound mod, it can use any cataloged asset (from base, from data, from another enabled mod) as a starting point. Workflow:

1. Modder opens the tool, picks "New compound mod."
2. Tool scans the catalog and presents existing assets browseable by type (weapons, meshes, props, characters, audio, etc.).
3. Modder selects assets to fork or extend (e.g. "use the base Falcon 2 mesh as my starting point; use this texture from another mod as my emissive layer").
4. Tool COPIES the selected assets' bytes into the new compound's internal storage. Assigns new catalog IDs (the modder names them or accepts auto-suggested names with the modder's namespace prefix).
5. Modder edits the copies in-place (rebinding texture references, adjusting numbers, etc.).
6. Tool saves the result as `mods/<modname>.pdmod`. The compound is fully self-contained; sharing it transmits everything.

Provenance metadata (extrapolation E-23): each copied asset's manifest entry carries an optional `origin:` field naming the source catalog ID (`base:weapon_falcon2`, `modder:other_mod:tex_diffuse`). For audit and credit, not enforcement. Modders can omit if they want their work to look entirely original; tool defaults to populating origin so collaborators can trace lineage.

**Loading flow (Pass 4 sequence):**

1. Mod manager walks `mods/*.pdmod` (top-level) and `mods/*/<inner>.pdmod` (extracted modpack contents).
2. For each compound: parse `mod.json` manifest; for each entry in `internal_assets`, register the declared `catalog_id` into the catalog as that asset type, hash the bytes, dedupe per the mechanism above.
3. Compound manifest also declares external dependencies (`requires:` block, see Section 3.16.5 / 3.16.6) on other compound mods.
4. Topological sort across the compound dependency graph (Section 3.16.7); cycle detection fires `LOUDFAIL.CATALOG.CYCLE` if violated; missing deps fire `LOUDFAIL.CATALOG.MISSING_DEP`.
5. Catalog is ready. Gameplay code looks up assets by ID; the catalog resolves to bytes by hash; bytes resolve from the relevant compound's archive (or `base/` / `data/` for non-mod content).

**Implications for prior sections:**

- The atomic-vs-compound packaging decision matrix (Pass 3 Section 3.17.4) collapses to a single recommendation: always compound. The mod-author choice becomes "self-contained vs depends-on-other-compound" (still real, see Sections 3.16.5 to 3.16.7), not "atomic file vs compound file."
- The reverse-dependency manifest (Section 3.16.5) operates at the compound level only. Atomic deps are internal to a compound and the catalog handles them via hash-dedupe; they are not user-facing.
- The mod manager UI never shows atomic-level mods (because they do not exist on disk). It shows compounds and modpacks only.

#### 3.16.1 Modpack storage = contain (Q-1 resolved; updated for Pass 4)

Confirmed default. `.pdmodpack` is a ZIP archive of compound `.pdmod` files plus a `pack.json` metadata file. On install, the mod manager extracts the constituent `.pdmod` files into `mods/<packname>/<modname>.pdmod` and writes a `mods/<packname>/pack.json` recording the constituent mod IDs. Reference model is rejected for v1.

Under the Pass 4 additive-only-compound model, every mod inside a `.pdmodpack` is a compound that contributes additive content. A "Halo total conversion" pack contains compound mods like `halo-campaign.pdmod`, `halo-weapons.pdmod`, `halo-characters.pdmod`, `halo-ui.pdmod`. After install, each of those compounds surfaces in the mod manager and contributes its catalog entries; the user sees Halo content alongside base content (Halo campaign in the campaign picker, Halo weapons in the weapon list, etc.).

Reasons restated: self-contained distribution (works offline), reversible install, no fetch-at-install fragility, matches `.zip`-based workflows everywhere else. Acceptable trade-off: duplicated bytes if a user has the same compound installed standalone and inside a pack (runtime hash-dedupe per Section 3.16.0 mitigates the memory cost; only the disk cost duplicates).

#### 3.16.2 Load order: vestigial under additive-only model (Q-2 resolved; rewritten for Pass 4)

Under the Pass 4 additive-only-compound model, load order matters only for `requires:` dependency resolution (compound A depends on compound B; B must register before A). The catalog handles this via topological sort over the dependency graph (Section 3.16.7); manual ordering is rarely needed.

Vestigial fields kept for forward compatibility:

```json
{
    "id": "modder:my_compound",
    "version": "1.0.0",
    "load_after": ["modder:other_compound"],
    "load_before": ["modder:third_compound"],
    "priority": 100
}
```

When useful:

- `load_after: [other_compound]` is a stronger declaration than `requires: [other_compound]`: it says "I want to load after this compound but I do not actually depend on it." Useful for ordering mods that interact via runtime conventions rather than catalog references.
- `priority:` field is documented as rarely needed under additive-only. Reserved for future use cases where load order has a non-dependency-driven significance.

The original Pass 3 framing of `priority:` as a tie-breaker for override-precedence is moot: there are no overrides under Pass 4, so no precedence ties to break.

User-facing override: drag-reorder UI in the modding hub remains useful for the small minority of cases where load order matters. The override persists across launches.

Cycle detection (Section 3.16.7) applies if `load_after` / `load_before` or `requires:` declarations form a cycle.

#### 3.16.3 SP-MP unification via game-mode block (Q-3 resolved)

`.pdscenario` schema gains a `modes:` block. Each scene declares per-mode initialization. Loader picks the active mode's block at scene init.

```json
{
    "catalog_id": "scenario_skedar_temple",
    "display_name": "Skedar Temple",
    "geometry_id": "geo_skedar_temple",
    "tiles_id": "tiles_skedar_temple",
    "modes": {
        "campaign": {
            "enemies": [...],
            "objectives": [...],
            "ai_paths": [...]
        },
        "combat_sim": {
            "spawn_points": [...],
            "default_weapon_set": "wepset_classic",
            "supported_modes": ["combat", "kingofthehill", "capturetheflag"]
        },
        "forge": {
            "editor_bounds": [...],
            "default_player_position": [0, 0, 0]
        }
    }
}
```

Hardcoded MP spawn points for campaign maps live in the canonical scenario's `modes.combat_sim` block. One scenario file, multiple game-mode initializations.

**Map variants are siblings via suffix naming (per directive 5).** A zombies-mode variant of Skedar Temple is `scenario_skedar_temple-zombies.pdscenario`, a distinct catalog ID. The variant inherits geometry / tiles / textures from the canonical scenario by reference (catalog ID lookups), but carries its own `modes:` block (or only the relevant mode entries). Treat as independent assets in the catalog; the suffix is human-readable provenance.

**MP weapon-source scoping (Pass 5 addition).** MP setup config gains a `random_source:` field that scopes random-weapon selection to a specific pool. Schema sketch for the MP setup config (lives in the host's match-setup state, not in the scenario itself):

```json
{
    "match_id": "mp_session_001",
    "scenario_id": "scenario_skedar_temple",
    "mode": "combatsim_combat",
    "weapon_set_id": "base:wepset_classic",     // ref to .pdwepset (Section 3.16.11)
    "random_source": "all_enabled",              // see options below
    "fiesta_enabled": false,
    "score_limit": 25,
    "time_limit_minutes": 10
}
```

`random_source:` accepted values:

- `"all_enabled"` (default; current behavior) - random from any enabled weapon registered in the catalog.
- `"base_only"` - random from base game weapons only. Useful for purist matches.
- `"modpack:<modpack_id>"` - random from weapons registered by mods belonging to the named modpack. e.g. `"modpack:halo-tc-1.0"` for a Halo-only match.
- `"weapon_set:<catalog_id>"` - random from a specific weapon set. e.g. `"weapon_set:modder:halo_power_weapons"`.

Random selection iterates the scope, applies the universal selector filter (`catalog ∩ enabled ∩ unlocked ∩ context_filter`; Section 3.16.11), picks uniformly. Existing Random and Fiesta semantics still apply; `random_source:` just narrows the pool.

If the resolved random pool is empty (all candidates are disabled or context-filtered out), the runtime fires `LOUDFAIL.RANDOM.EMPTY_POOL` and falls back to a default base weapon (extrapolation E-35: base entries are always present in the catalog even when `enabled: false`, so direct lookup still resolves and the fallback always works).

Weapon set assets (`.pdwepset`, Section 3.16.11) referenced by `random_source: "weapon_set:..."` are themselves catalog entries; their members must resolve to enabled `.pdwpn` entries at registration time, or the set itself fails to register.

#### 3.16.4 `.pdcharacter` and `.pdprop` as catalog asset types (Q-4 resolved)

`.pdcharacter` is a confirmed distinct catalog asset type from `.pdmesh`. `.pdprop` is a third asset type for spawnable props with logic. Definitions:

| Extension | Asset type | Contains |
|---|---|---|
| `.pdmesh` | Static mesh (visual geometry only) | Vertices, materials, texture references. No skeleton, no behavior. Spawnable as a passive visual prop. |
| `.pdcharacter` | Compound character | Skeletal mesh + animation references + audio references + attach points + behavior block. The full character, not just the geometry. |
| `.pdprop` | Spawnable prop with logic | Geometry plus physics plus stats plus behavior block. See Section 3.17 worked example. |

Important Pass 4 clarification: these extensions describe the SHAPE of files inside compound `.pdmod` archives (and inside `base/` and `data/` per-asset-granularity trees). They are not user-facing files in `mods/` (only `.pdmod` and `.pdmodpack` are visible there per Section 3.16.0). When a modder unzips a compound `.pdmod` for inspection, they see these per-asset-class extensions in the inner directory structure (`weapons/foo.pdwpn`, `meshes/foo.pdmesh`, etc.) and the catalog registers each as an atomic entry per the compound's manifest.

Schema sketch for `.pdcharacter` (extrapolation from Mike's framing):

```json
{
    "catalog_id": "character_carrington",
    "display_name": "Daniel Carrington",
    "skeletal_mesh": "mesh_carrington_skeletal.pdmesh",
    "skeleton": {
        "bones": [...],
        "rest_pose": {...}
    },
    "animations": [
        { "ref": "anim_carrington_idle", "alias": "idle" },
        { "ref": "anim_carrington_run",  "alias": "run" }
    ],
    "audio": {
        "voice_lines": ["voice_carrington_intro_01", "voice_carrington_intro_02"],
        "footstep_sfx": "sfx_footstep_default"
    },
    "attach_points": [
        { "name": "HEAD_TOP", "bone": "HEAD",  "transform": [0, 0.2, 0] }
    ],
    "behavior": {
        "default_state": "idle",
        "states": [
            { "id": "idle", "loop_anim": "idle" },
            { "id": "running", "loop_anim": "run", "audio_loop": "sfx_running" }
        ],
        "transitions": [
            { "from": "idle", "to": "running", "on": "input_sprint_pressed" }
        ]
    }
}
```

Differences from `.pdmesh` enforced by the catalog: a `.pdcharacter` registers as `ASSET_CHARACTER`, a `.pdmesh` as `ASSET_MESH`, a `.pdprop` as `ASSET_PROP`. Spawn code paths differ (character spawn instantiates skeletal animator + AI hooks; mesh spawn instantiates visual-only; prop spawn instantiates physics + behavior).

The behavior block uses the logic system (state machines: states, transitions, triggers, actions). Authoring tools in the client expose this declaratively. Logic system itself is out of scope for this audit; track as a future architectural pillar.

#### 3.16.5 Reverse dependency manifest (Q-4 follow-up; updated for Pass 4)

Under the Pass 4 compound-only model, `requires:` blocks declare external dependencies on OTHER COMPOUND MODS, not on atomic assets (atomic assets live inside compounds and are deduplicated by hash per Section 3.16.0). The reverse manifest applies at the compound level.

Use case: a custom map compound (`modder:halo_blood_gulch.pdmod`) requires a custom-prop compound (`modder:halo_props_pack.pdmod`) for the warthogs and weapons that spawn in the map. The map's `mod.json` declares:

```json
{
    "id": "modder:halo_blood_gulch",
    "version": "1.0.0",
    "type": "compound",
    "internal_assets": [...],
    "requires": [
        { "id": "modder:halo_props_pack", "kind": "compound" }
    ]
}
```

The catalog computes a reverse manifest at load time by walking all enabled compound mods' `requires:` blocks. Each compound mod's runtime state acquires a derived `required_by:` list:

```
modder:halo_props_pack (compound):
  required_by: [modder:halo_blood_gulch, modder:halo_assault_pack, modder:halo_modpack]
```

This is a runtime-derived field, not a stored manifest field. It cannot be authored; only consumed.

UX consequence: disabling or removing a compound with a non-empty `required_by:` list triggers a user prompt:

> "This mod is required by:
>   - modder:halo_blood_gulch
>   - modder:halo_assault_pack
>   - modder:halo_modpack
> Disabling it will break those mods. Continue?"

User cannot accidentally pull a foundational compound that other compounds depend on without an explicit confirm.

Note: atomic-level dependency management does NOT exist as a user-facing concept under Pass 4. If two compounds happen to bundle the same texture (same SHA-256), the catalog dedupes silently per Section 3.16.0. Neither compound declares the other as a dep; each is self-contained. The user can disable either without breaking the other.

#### 3.16.6 Optional plus fallback dependencies (Q-4 follow-up; updated for Pass 4)

Compound mods can declare a dependency as optional, with a fallback. Schema sketch:

```json
{
    "id": "modder:halo_blood_gulch",
    "type": "compound",
    "requires": [
        { "id": "modder:halo_props_pack", "kind": "compound" },
        { "id": "modder:halo_vehicles_pack", "kind": "compound", "optional": true,
          "fallback": null }
    ]
}
```

Loader rule for compound-on-compound deps:

- **Required dep missing:** the compound fails to register entirely. LOUDFAIL.CATALOG.MISSING_DEP.
- **Optional dep missing, no fallback:** the compound loads. Asset references inside the compound that point at the missing dep's catalog IDs resolve to `null` (or to a placeholder); gameplay code handles missing references gracefully (typically: skip the spawn, log warning).
- **Optional dep missing, fallback provided:** asset references that would resolve through the missing dep instead resolve to the fallback ID. Audit recommendation: fallback IDs should resolve to base content, since base is always present.

Use case: a Halo Blood Gulch map requires the props pack (warthogs are not optional for this map), but optionally uses the vehicles pack for additional spawnables. Without the vehicles pack, the map still loads with a degraded set of spawnables.

LOUDFAIL hits at `LOUDFAIL.CATALOG.MISSING_DEP` when a required dep is missing. Optional deps that resolve to fallback log at `LOG_NOTE` (not LOUDFAIL): the modder declared the fallback intentionally; the substitution is expected behavior.

#### 3.16.7 Circular dependency prevention (Q-4 follow-up)

The catalog build performs topological sort on the compound dependency graph at startup:

1. Build adjacency list from every enabled compound's `requires:` block (and `load_after:` / `load_before:` from Section 3.16.2).
2. Run topological sort (Kahn's algorithm or DFS with white / grey / black coloring).
3. On cycle detection, fire `LOUDFAIL.CATALOG.CYCLE` with the cycle path: "Cycle detected: A requires B, B requires C, C requires A. Mods refused to register: A, B, C."
4. The cycle members fail to register entirely. The user sees a clear error and the catalog continues to build with the remaining (non-cyclic) compounds.

Cycles are pathological in the dependency graph (a normal mod ecosystem is a DAG). The detection exists to prevent silent infinite-loop or stack-blowout failure modes when a malformed mod ships.

Hash-dedupe (Section 3.16.0) is independent of the dependency graph: deduping happens per-asset at registration time and does not generate cycles.

#### 3.16.8 Consecutive-streak counter for self-heal events (Q-5 resolved; updated for Pass 4)

The counter is for SUBSEQUENT SESSIONS, not within a session. It tracks PERSISTENT corruption (an architectural alarm signal that something is structurally wrong with extraction or storage), not one-off user accidents (intentional in-place edits that the user knows about and accepts).

**Mechanics:**

- Per-asset counter persisted in `data/.session-state.json` (or platform-equivalent persistent state file).
- Counter starts at 0.
- Counter increments by 1 each time the asset triggers self-heal (hash mismatch detected, quarantine, re-extraction) on a launch where the previous launch ALSO triggered self-heal for the same asset. In other words: only consecutive runs.
- Counter resets to 0 when the streak breaks: a launch that loads the asset cleanly (hash verifies, no self-heal needed) resets the counter for that asset.
- Logging is gated by streak: while counter > 0, every increment fires `LOUDFAIL.HEAL.PERSISTENT_CORRUPTION` with the count and the asset path. When the streak breaks, logging for that asset disables until the issue resumes.

```
LOUDFAIL.HEAL.PERSISTENT_CORRUPTION: data/weapons/weapon_falcon2.pdwpn re-extracted on 4 consecutive launches; previous quarantine from 2026-04-28 has been overwritten on each launch. This indicates persistent corruption in extraction or storage; investigate.
```

**Distinction Mike drew (Pass 3 baseline, refined Pass 4):**

This only matters if it can ruin Base Game experience. A user intentionally editing files in `data/` to mod the base game is not the alarm; the alarm is when self-heal repeatedly has to overwrite older quarantines because the user is unintentionally re-corrupting (or extraction is producing different bytes each run, or storage is flaky). One-off edits are below the radar.

The streak-reset behavior is the key architectural distinction:

- **One session of corruption:** logged at LOG_NOTE level (informational); no LOUDFAIL.
- **Two or more consecutive sessions of corruption on the same asset:** LOUDFAIL.HEAL.PERSISTENT_CORRUPTION fires with the count.
- **Streak breaks (clean launch):** counter for that asset resets; logging for that asset goes silent until the issue resumes.

This way the alarm fires only on PERSISTENT architectural problems (a flaky extractor, a flaky storage layer, a botched migration). Modder activity does not generate noise; one-off corruption recovers silently.

**Persistence file format (extrapolation E-?):** `data/.session-state.json` is a small JSON file recording per-asset counter state plus a "last clean launch timestamp" for bookkeeping:

```json
{
    "version": 1,
    "last_clean_launch": "2026-04-30T14:22:00Z",
    "self_heal_streaks": {
        "data/weapons/weapon_falcon2.pdwpn": { "count": 3, "last_event": "2026-04-30T15:02:00Z" }
    }
}
```

The state file is itself read-only with writable-during-extraction-or-self-heal semantics (Section 3.7), excluded from the manifest hash check (since its purpose is to track manifest events).

**UI consequence:** the Settings panel can show a "self-heal streaks active: <N>" indicator when any counters are nonzero. When all counters are zero (no active streaks), no indicator. Click-through to a list of active streaks for diagnosis.

#### 3.16.9 Priority-list ROM selection (Q-6 resolved)

When the user has multiple PD ROM files in `data/` (e.g. `pd.ntsc-final.z64` plus `pd.pal-final.z64`), the extractor uses an internal priority list to pick which one to extract from:

| Priority | ROM ID |
|---|---|
| 1 | ntsc-final |
| 2 | pal-final |
| 3 | ntsc-1.0 |
| 4 | jpn-final |
| 5 | pal-beta |
| 6 | ntsc-beta |

Recommendation rationale: NTSC-final is the primary decompilation target; PAL-final is the next-most-tested; betas are last because they have the most quirks.

Client uses the first detected match for extraction; ignores the others. Player UI in Settings can override the auto-selection (force a specific ROM ID even if a higher-priority ROM is present). Avoids extracting from multiple ROMs simultaneously, which would waste disk and create cross-version contamination.

Implementation: at startup, the bootstrap extractor scans `data/` for `pd.<romid>.z64` files (or whatever per-ROM file naming the project uses), hashes each against `s_KnownRomHashes`, and picks the highest-priority validated match.

#### 3.16.10 Cache-only base distribution to mismatched peers (Q-6 follow-up)

Networking integration for the multi-ROM model. When a player joins a host whose extracted base content differs from the joining peer's (different region quirks, modded asset variants, different ROM version selected), the host can stream the differing base content into the joining peer's `data/.session-cache/` for the duration of the session.

Mechanics:

- Catalog computes the diff at session-join handshake (host sends asset-ID-plus-hash list; peer responds with which it has matching, which it lacks).
- Host streams the lacking assets over the existing ENet protocol channel (or a dedicated "asset transfer" channel; out of scope for this audit).
- Peer writes received assets to `data/.session-cache/<host_session_id>/`. This directory is NOT marked read-only (it is session-scratch, not extracted base).
- Catalog routes asset lookups to `data/.session-cache/` first for the active host's content, falls back to local `data/` extracts otherwise.
- On disconnect, `data/.session-cache/<host_session_id>/` is evicted entirely.
- Session cache is ephemeral and local. Player does NOT accumulate ROM-derived content from other players' ROMs across sessions (that would be redistribution, not BYOR).

Why this matters: a US-region player joins an EU-region host's match. Their ROMs differ subtly (PAL voice samples, different localized text, region-specific level tweaks). Without session-cache, the game has either silent desync (peer plays with their NTSC content while host plays PAL) or a hard ban on cross-region play. Session-cache lets the peer experience the host's content for that session without polluting their permanent extracts.

Privacy / legal framing: the peer is not retaining or redistributing the host's content. The session-cache is functionally identical to streaming video: bits cross the wire for playback, then evict. Compare to OpenRCT2 cross-version play, which behaves similarly.

Implementation surface: `port/src/net/` integration with the catalog. The catalog gets a third asset-provider (alongside `assetprovider_rom.c` and the future `assetprovider_data.c`): `assetprovider_session_cache.c`. Out of scope for this audit; flagged as a future networking-layer integration.

#### 3.16.11 Disabling base content (presentation-layer; Pass 5 architectural mechanism)

> **Pass 5 architectural extension (2026-04-30):** total-conversion mods need a way to hide base content from selectors without overriding it. The mechanism is a per-catalog-entry `enabled` flag plus a `disable_base:` field on compound mods. Base content stays canonical and is never replaced; only its visibility in selector UIs is filtered.

This is the complement to Pass 4's no-overrides invariant. Pass 4 says no two registrations share a catalog ID; Pass 5 says base entries can be hidden from selectors via a presentation filter without violating the no-overrides invariant. Both compose cleanly.

**Catalog entry shape (extrapolation E-31):**

Every registered catalog entry has a runtime `enabled: true / false` flag (default true). The flag is mutated by:

1. **Mod-driven disable.** A compound mod declares `disable_base: [catalog_id, ...]` in its `mod.json`. When the mod is enabled, the catalog flips `enabled: false` on each listed ID. When the mod is disabled, the catalog flips `enabled: true` back (releasing the filter).
2. **User-driven disable.** The mod manager UI (or a dedicated content-visibility panel) lets the user toggle `enabled` per-content independently of any mod. Persists across launches.
3. **Multiple flippers stack.** If two mods both list the same base ID in their `disable_base:`, the catalog tracks both flippers; the entry stays disabled until BOTH mods are disabled (or the user manually overrides). Logged via `CATALOG: <id> disabled by [mod_a, mod_b]` for audit clarity.

**Direct lookup vs selector filter:**

- **Direct lookup by ID** (e.g. catalog API `assetCatalogGet("base:weapon_falcon2")`) returns the entry regardless of `enabled` state. Cross-references from other mods or scenarios resolve cleanly.
- **Selector filter** (e.g. CS weapon picker, campaign menu, character selection) applies the universal filter: `selector_pool = catalog ∩ enabled ∩ unlocked ∩ context_filter`. Disabled entries are absent from the picker UI.

This split is the architectural payoff: the catalog stays a single source of truth (every base ID always exists), while selector UIs respect the filter (modder controls what the user sees in the picker).

**Selector pool composition (Pass 5 formalization):**

The selector pool concept already partially shipped via the catalog universality sweep (`context/audits/catalog-universality-sweep-2026-04-27.md`). Pass 5 grows it to include the `enabled` filter:

```
selector_pool(context) = {
    entry in catalog
    where entry.enabled == true
    AND entry.unlocked_for(context)
    AND entry.matches(context_filter)
}
```

The four filters compose by intersection. Disabling any one excludes the entry. Examples:

- CS weapon picker for player 1 in a casual match: filter is `(entry.type == weapon) AND (entry.context_includes("mp")) AND entry.enabled AND entry.unlocked`. A disabled base weapon does not appear; an enabled mod weapon does.
- Campaign menu: filter is `(entry.type == scenario) AND (entry.context_includes("campaign")) AND entry.enabled`. A disabled base campaign mission does not appear; an enabled Halo campaign mission does.

**`disable_base:` field semantics:**

```json
{
    "id": "modder:halo_total_conversion",
    "type": "compound",
    "internal_assets": [...],
    "disable_base": [
        "base:weapon_falcon2",
        "base:weapon_cmp150",
        "base:weapon_dy357magnum"
    ]
}
```

Validation (extrapolation E-32): each entry in `disable_base:` must resolve to an existing catalog ID at registration time. Misnamed or unknown IDs fire `LOUDFAIL.CATALOG.UNKNOWN_DISABLE_TARGET` with a clear message:

```
LOUDFAIL.CATALOG.UNKNOWN_DISABLE_TARGET: mod 'modder:halo_total_conversion' declares disable_base entry 'base:weapon_xxxxxxx' which is not present in the catalog. Check spelling.
```

The mod still registers; only the unknown disable entry is dropped. Other valid entries still apply.

**Total conversion use case (Mike's Halo example):**

A "Halo total conversion" `.pdmodpack` contains compound mods that:

1. List all base PD weapons / characters / vehicles / maps in `disable_base:` arrays.
2. Add Halo content as additive entries via `internal_assets:`.

When the modpack is enabled, the catalog has Halo content visible in selectors and base PD content hidden. When the modpack is disabled, base PD reappears. Both can coexist (`enabled: false` on PD content + Halo additive content = pure Halo experience).

**User UI for content visibility (extrapolation E-?):**

The mod manager UI gains a "Content visibility" panel listing all catalog entries with per-entry toggles. Users can:

- Disable individual base content (e.g. "I never want to see the firing range in the campaign menu").
- Re-enable base content that a mod hid (override the mod's `disable_base:` list for specific IDs).
- View which mods are currently flipping each base entry's `enabled` state.

Out of scope for the audit; tracked as future modding-hub UX work. The architecture supports it; the UI surface is the implementation question.

**Interaction with `.pdwepset` (Section 3.3 / extension table):**

A `.pdwepset` references weapon catalog IDs in its `weapons:` array. When the set registers, each reference must resolve to an enabled `.pdwpn`. If a referenced weapon is disabled (by a `disable_base:` from any other mod), the set fails registration with `LOUDFAIL.CATALOG.WEPSET_INVALID`. (Extrapolation E-34: validation runs after all enabled mods have registered, so the modder sees the actual conflict.)

This means: a modder authoring a weapon set that references base weapons should be aware that any total-conversion mod the user enables alongside their set may invalidate the set. Audit recommendation: document this clearly in the mod-authoring guide; weapon set authors should reference content from the same modpack (or with explicit `requires:` declarations) so the set composes well.

**Interaction with `random_source:` (Section 3.16.3):**

`random_source: "all_enabled"` automatically respects the `enabled` filter (random selection iterates the catalog and filters by `enabled`). `random_source: "base_only"` selects from base entries that are currently enabled (so a disabled base weapon does not appear in random). `random_source: "weapon_set:<id>"` selects from the set's members, filtered by enabled.

Empty random pool (all candidates filtered out) fires `LOUDFAIL.RANDOM.EMPTY_POOL` with the resolved scope and falls back to a default base weapon (which is always reachable via direct lookup even if `enabled: false`).

### 3.17 Worked example: authoring a Halo fusion-coil prop mod

To illustrate the per-asset-class extension model plus compound-mod plus dependency-graph framing in concrete terms, here is the end-to-end shape of a modder authoring a Halo-style fusion-coil prop mod.

#### 3.17.1 Goal

The modder wants to add a destructible fusion coil prop:

- A pulsing-orange-emissive cylinder that spawns as a placeable object in arenas and missions.
- Shootable. Damageable.
- Below 25 percent health, the coil shakes and tints redder.
- At zero health, explodes with VFX, spawns a custom boom audio cue, applies area-of-effect damage to nearby actors, removes itself.

#### 3.17.2 Schema (the `.pdprop` file)

```json
{
    "id": "modder:fusion_coil",
    "type": "prop",
    "geometry": {
        "mesh": "fusion_coil.glb",
        "skeleton": null
    },
    "textures": {
        "diffuse": "coil_albedo.tga",
        "emissive": {
            "frames": ["coil_emit_0.tga", "coil_emit_1.tga", "coil_emit_2.tga"],
            "fps": 12,
            "loop": true
        }
    },
    "physics": {
        "shape": "cylinder",
        "mass_kg": 8.0,
        "shootable": true
    },
    "stats": {
        "max_health": 50,
        "spawn_health": 50
    },
    "behavior": {
        "on_health_below": [
            {
                "threshold_pct": 25,
                "tint": [1.0, 0.3, 0.2],
                "loop_anim": "shake_warning"
            }
        ],
        "on_destroyed": {
            "spawn_vfx": "vfx:explosion_medium",
            "spawn_audio": "modder:fusion_coil_boom",
            "aoe_damage": {
                "radius": 4.0,
                "amount": 75,
                "falloff": "linear"
            },
            "remove_self": true
        }
    }
}
```

Field anatomy:

- **`id`**: catalog ID, namespaced under the modder's namespace (`modder:`). Distinct from base namespace (`base:`) and ROM-extracted namespace (`data:` if needed; see Section 3.4).
- **`type`**: `"prop"` registers the asset as `ASSET_PROP` in the catalog. Distinguishes from `ASSET_MESH` (visual-only) and `ASSET_CHARACTER` (skeletal plus behavior).
- **`geometry`**: references the bundled `fusion_coil.glb` (binary mesh asset shipped inside the `.pdprop` archive alongside this manifest).
- **`textures.emissive`**: animated texture with frame list and FPS, demonstrating that the schema accommodates non-trivial visual effects without engine-side hardcoding.
- **`physics`**: shape, mass, shootable flag for the physics-collision pillar to ingest.
- **`stats`**: simple key-value record for the gameplay layer. Mod author's design choice; engine consumes generic field names.
- **`behavior`**: state-machine block. `on_health_below` is a watcher that fires when the prop's health drops below a threshold; `on_destroyed` is an event handler firing when health hits zero. Actions inside (`spawn_vfx`, `spawn_audio`, `aoe_damage`, `remove_self`) are vocabulary the logic system understands.

#### 3.17.3 Packaging: a single self-contained compound (Pass 4 default)

Under Pass 4 (Section 3.16.0), every mod in `mods/` is a compound `.pdmod` archive. The fusion-coil mod ships as a single self-contained compound:

```
modder_fusion_coil.pdmod                     # ZIP archive
  mod.json                                    # compound manifest
  props/fusion_coil.pdprop                    # the prop record (manifest from 3.17.2)
  meshes/fusion_coil.pdmesh                   # bundled mesh
  textures/coil_albedo.tga                    # bundled diffuse texture
  textures/coil_emit_0.tga                    # bundled emissive frames
  textures/coil_emit_1.tga
  textures/coil_emit_2.tga
  audio/sfx/fusion_coil_boom.pdsfx            # bundled audio record
  audio/sfx/fusion_coil_boom.bin              # bundled audio sample data
```

The compound's `mod.json` declares its internal atomic contents (each registers into the catalog with its own ID) and any external compound dependencies:

```json
{
    "id": "modder:halo_fusion_coil",
    "version": "1.0.0",
    "display_name": "Halo Fusion Coil",
    "type": "compound",
    "author": "MyModder",
    "internal_assets": [
        { "catalog_id": "modder:prop_fusion_coil",
          "type": "prop",
          "path": "props/fusion_coil.pdprop" },
        { "catalog_id": "modder:mesh_fusion_coil",
          "type": "mesh",
          "path": "meshes/fusion_coil.pdmesh" },
        { "catalog_id": "modder:tex_coil_diffuse",
          "type": "texture",
          "path": "textures/coil_albedo.tga" },
        { "catalog_id": "modder:tex_coil_emit_0",
          "type": "texture",
          "path": "textures/coil_emit_0.tga" },
        { "catalog_id": "modder:tex_coil_emit_1",
          "type": "texture",
          "path": "textures/coil_emit_1.tga" },
        { "catalog_id": "modder:tex_coil_emit_2",
          "type": "texture",
          "path": "textures/coil_emit_2.tga" },
        { "catalog_id": "modder:sfx_coil_boom",
          "type": "sfx",
          "path": "audio/sfx/fusion_coil_boom.pdsfx" }
    ],
    "requires": []
}
```

Why this is the default packaging:

- **Self-contained.** Modder ships one file; user installs one file; everything works without external coordination.
- **Round-trip clean** (per Pass 1 directive 3): the modder edits in the in-client tool, saves; the file is the canonical compound shape.
- **Hash-dedupe at registration** (Section 3.16.0) handles any incidental duplication with other compounds at runtime. If the modder's emissive textures happen to match another mod's textures, both compounds keep them on disk but the catalog stores them once in memory.
- **No external dep-resolution at install time.** Pass 4 architecture eliminates atomic-level dependencies. The optional VFX referenced in the prop's behavior block resolves via Section 3.16.6 (optional plus fallback) at load time.
- **Additive-only invariant (Section 3.5)** is naturally satisfied: this compound declares unique catalog IDs (`modder:prop_fusion_coil`, etc.) and adds them alongside base content. The fusion coil appears in the prop picker as a new selectable entry.

#### 3.17.4 Packaging variation: compound depending on another compound

When a modder is publishing a coordinated set of related mods (e.g. a Halo modpack), they may split into multiple compounds with declared dependencies. Example:

- `modder:halo_shared_assets.pdmod` (compound): bundles shared VFX + audio + materials used across multiple Halo mods.
- `modder:halo_fusion_coil.pdmod` (compound): the fusion-coil prop. Declares `requires: [modder:halo_shared_assets]`.
- `modder:halo_warthog.pdmod` (compound): the warthog vehicle. Declares `requires: [modder:halo_shared_assets]`.

The shared compound carries assets used by both downstream compounds. Reverse-dep manifest (Section 3.16.5) lets the user know they cannot disable `modder:halo_shared_assets` without breaking the mods that depend on it.

Distribution: the modder bundles all three compounds in a `.pdmodpack` (Section 3.16.1) so users install everything in one operation.

When to use this variation vs the default self-contained packaging:

- **Self-contained (3.17.3):** default. The mod is small, ships standalone, no coordinated set.
- **Compound-on-compound (3.17.4):** the modder is publishing a coordinated set and wants to factor out shared content. Typical for total-conversion modpacks.

Hash-dedupe (Section 3.16.0) softens this trade-off: a self-contained compound that happens to bundle the same textures as another self-contained compound dedupes at runtime. So even a "duplicate everything" approach is acceptable from a memory standpoint; the cost is disk, not RAM. Modders who do not want to manage compound-on-compound dependencies can simply self-contain everything.

#### 3.17.5 Behavior block: the logic system

The `behavior` block uses the logic system: a state machine with triggers and actions. Triggers are events the engine fires (`on_health_below`, `on_destroyed`, `on_spawn`, `on_player_proximity`, etc.). Actions are operations the engine knows how to execute (`spawn_vfx`, `spawn_audio`, `aoe_damage`, `tint`, `loop_anim`, `remove_self`, etc.).

Authoring tools in the client expose this declaratively. The modder picks triggers and actions from a UI palette; no code is required from the modder.

Implementation surface: the logic system itself is a future architectural pillar (logic engine + trigger registry + action registry). Out of scope for this audit; flagged so that the schema sketch above is understood as forward-looking. The behavior block's exact vocabulary is a follow-on design task.

#### 3.17.6 What this example demonstrates

- **Compound-only on disk** (Section 3.16.0): the modder ships one `.pdmod` file; the user sees one file; sharing the file transmits everything needed.
- **Internal catalog granularity**: the compound's manifest declares each contained atomic asset with its own catalog ID; gameplay code references them by ID; the catalog resolves to bytes inside the compound.
- **Hash-dedupe at registration** (Section 3.16.0): if another mod bundles the same textures, the catalog stores the bytes once and points multiple IDs at the shared entry.
- **Catalog-mediated references** (`modder:tex_coil_diffuse`, `modder:sfx_coil_boom`, `base:vfx_explosion_default`) decouple authoring from filesystem layout. The modder writes IDs, not paths; the catalog resolves at load time.
- **Additive only** (Section 3.5): the fusion coil adds a new prop alongside base content. No override; no precedence ambiguity.
- **Optional plus fallback dependencies** (Section 3.16.6) at the compound-on-compound level let coordinated sets gracefully degrade.
- **Symmetric schema** (per Pass 1 directive 3): the same `.pdprop` shape describes a base-extracted prop (e.g. base PD's destructible barrels) and a modder-authored one. Loader has one parsing path.
- **Behavior via state machine, not code**: the modder declares triggers and actions; the logic system executes. No programming required.

### 3.18 Priority order

Ranked by leverage (impact / cost) for the next 1 to 4 sessions, updated to reflect Mike's directives plus the Pass 3 refinements (Section 3.16) and worked example (Section 3.17):

1. **Document the architectural principle in `context/roadmap.md` and `context/pillars/catalog.md`.** Cost: zero code, one doc edit. Locks in the direction; future sessions cascade from it. (This audit is the first half of that documentation.)
2. **Populate ROM SHA-256 known-good hashes (`port/src/romdata.c:227-246`).** Cost: trivial (one-time, capture from log on each ROM version). Per directive 13, this is the gate for correct offset selection in the extractor.
3. **Migrate the existing UI texture extractor's output from loose files to `data/ui/pd-original.pdui`.** Cost: small. Validates the principle on the simplest existing surface and aligns the runtime extractor with the rest of the mod system. Existing code path is small; the work is the ZIP archive writer plus reroute.
4. **Add the LOUDFAIL channel and route procedural fallback through it.** Cost: small. Per directive 10. Existing log infrastructure can subdivide a new channel; the procedural-fallback site (`pdgui_theme.cpp:2563-2632`) is one place to start.
5. **Add a `pd-tests` scope `extraction` covering existing UI texture extractor behavior.** Cost: small. Per directive 12, this is structural for the most load-bearing pillar; should land before larger migrations.
6. **Investigate ROM addresses for PD's dialog chrome source art and replace the procedural chrome with real extraction.** Cost: medium (research-heavy). Per directive 11, severity high.
7. **Define the `.pdwpn` schema and migrate the F11-F13 weapons content from `base/weapons.pdbase` monolith to per-weapon `base/weapons/weapon_*.pdwpn`.** Cost: medium. Highest user value because weapons are the most active modding surface.
8. **Implement the hash-verify-and-self-heal flow plus quarantine.** Cost: medium. Per directives 7 plus 8. Depends on the manifest design landing first.
9. **Implement read-only `data/` with writable-during-extraction.** Cost: small (cross-platform `chmod` / ACL plumbing). Per directive 8. Pairs with item 8.
10. **Loading-screen modal during first-launch extraction.** Cost: small. Per directive 14. Pairs with item 8 (auto-extraction-on-launch behavior).

Items 11+ are the asset-class-by-asset-class migration (mesh, audio split into music / sfx / voice, scenarios with sub-resources, animations, etc.) which can happen in parallel with other work and does not need to be batch-scheduled. The `.pdmodpack` storage decision (Q-1) is needed before mod-pack tooling lands but does not block any of items 1 to 10.



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
- `context/audits/pdmod-verification-matrix-2026-04-25.md` (current `.pdmod` archive state; treat as retirement target per directive 19)
- `context/audits/catalog-universality-sweep-2026-04-27.md` (catalog routing maturity)
- `context/audits/post-implementation-audit-2026-04-25.md` (recent shipped state)
- `context/designs/modding/pdmod-format.md` (`.pdmod` archive format spec)
- `context/designs/catalog/catalog-full-pipeline-weapons.md` (F1-F10 plus F11-F13 weapons catalog work, F12-F13 lane CLOSED 2026-04-30)
- `context/designs/modding/forge-level-editor.md` (FW-3 terrain editor reference)

**Mike's directives applied (Pass 2 plus Pass 3 plus Pass 4 plus Pass 5, 2026-04-30)**

Twenty directives logged from Pass 2 plus six Q-resolution refinements from Pass 3 (Section 3.16) plus the worked example (Section 3.17) plus the compound-only-plus-no-overrides architectural shift from Pass 4 plus the presentation-layer disable mechanism plus weapon-set extension plus Grid forward-looking notes from Pass 5:

1. Extension naming (`.pdwpn` over `.pdwep`); definitions for `.pdtiles`, `.pdseg`, `.pdmpconfig`, `.pdtexconfig`, `.pdfiringrange`. (Sections 3.2)
2. Audio extraction by category (music / sfx / voice). (Sections 3.2, 3.10 G-2)
3. `data/` vs `base/` canonical distinction. (Sections 3.1, 3.4)
4. Mods first-class symmetric with override-flag plus naming-disallow plus multi-override. (Section 3.5)
5. Variant naming (per-asset, not flag toggle). (Sections 3.2, 3.13 M-2)
6. `.pdmodpack` architecture. (Section 3.9; Q-1 open question)
7. Hash-verify plus self-heal plus quarantine. (Section 3.6)
8. Read-only `data/` with writable-during-extraction. (Section 3.7)
9. LOUDFAIL log channel. (Section 3.8)
10. Procedural fallback as loud failure. (Section 3.10 G-9; Section 3.8)
11. Procedural chrome severity bumped to high. (Section 3.10 G-5)
12. Test coverage severity bumped to high. (Section 3.10 G-8)
13. ROM hash validation enable plus offset selection. (Section 3.10 G-6)
14. CLI extraction discoverability auto via launch flow. (Section 3.10 G-7; Section 3.11 A-7)
15. Multi-ROM support expansion approved. (Section 3.11 A-6; Q-6)
16. `src/generated/` retirement TODO. (Section 3.10 G-11)
17. JSON / INI usage with commented-out unused tags. (Section 3.3)
18. Mod-override branch in `romdataFileLoad` is AllInOne residue. (Section 3.10 G-10)
19. `mods/base-ui.pdmod` is broken legacy retirement target. (Sections 1.7 mismatch 1; 3.10 G-1)
20. Mod tools load any base content as template. (Section 3.11 A-4; 3.13 M-8)

Forward-looking notes tracked: accessories system (FW-1), mod-driven character behavior (FW-2), terrain editor (FW-3), bundled-with-release modpacks (FW-4), logging-pipeline cleanup pass (FW-5), ROM-free distribution (FW-6).

**Pass 3 Q-resolutions (Section 3.16):**

- Q-1 modpack storage = contain (3.16.1).
- Q-2 load order with `load_after:` / `load_before:` positional defaults plus `priority:` plus drag-reorder UI (3.16.2).
- Q-3 SP-MP unified via `modes:` block in `.pdscenario`; map variants are siblings via suffix naming (3.16.3).
- Q-4 `.pdcharacter` distinct extension and distinct catalog asset type from `.pdmesh`; `.pdprop` introduced as third asset type (3.16.4). Plus reverse-dependency manifest (3.16.5), optional + fallback dependencies (3.16.6), circular-dependency prevention via topological sort (3.16.7).
- Q-5 counter-based LOUDFAIL with session reset for quarantine overwrites (3.16.8).
- Q-6 priority-list ROM selection at extraction time (3.16.9) plus session-cache for cross-region multiplayer (3.16.10).

**Pass 3 worked example (Section 3.17):** end-to-end Halo fusion-coil prop mod authoring flow demonstrating per-asset-class extensions, catalog-mediated references, optional + fallback dependencies, packaging trade-offs, and the behavior-as-state-machine model. Updated in Pass 4 for compound-only packaging (the atomic-vs-compound matrix collapsed; everything is compound now).

**Pass 4 architectural shift (Section 3.16.0 plus Section 3.5 rewrite):**

- Compound-only on disk in `mods/` (Section 3.16.0). Atomic assets live INSIDE compound `.pdmod` archives, not loose. One file equals one mod. No user-facing dependency management for atomic assets.
- Internal catalog still gets per-asset granularity. Each compound's `internal_assets` block declares its contents; each registers as its own catalog entry with its own ID; gameplay code references by ID; catalog resolves to bytes inside the compound.
- Hash-based deduplication at registration. Two compounds bundling the same texture (same SHA-256) cause the catalog to store the bytes once but register both declared IDs pointing at the shared entry. Disk space duplicates; runtime memory and catalog space dedupe.
- No overrides. Mods are strictly additive. Catalog ID uniqueness invariant: every ID is unique across base, data, and all enabled mods. Duplicates LOUDFAIL at registration. Total conversions become modpacks of additive compounds. (Section 3.5 rewritten to reflect this.)
- Q-2 load-order/priority becomes vestigial under additive-only (Section 3.16.2 rewritten).
- Reverse-dep manifest (Section 3.16.5) and optional+fallback deps (Section 3.16.6) operate at the compound-on-compound level only; atomic-level dep tracking happens internally and via hash-dedupe.
- Q-5 counter clarified (Section 3.16.8): tracks consecutive sessions of self-heal on the same asset; persists in `data/.session-state.json` across launches; resets on streak break (a clean launch).
- Halo worked example rewritten (Section 3.17.3 / 3.17.4 / 3.17.6) for compound-only packaging.

**Pass 3 self-extrapolations beyond Mike's explicit text:**

- E-19. `.pdprop` introduced as a third asset class distinct from `.pdmesh` (visual-only) and `.pdcharacter` (skeletal compound). Required by the worked example; folded into Section 3.2 extension table.
- E-20. Logic system (state machine: triggers + actions) flagged as a future architectural pillar (Section 3.17.5). The behavior block schema sketches assume vocabulary the logic system understands; the system itself is out of scope for this audit.
- E-21. `assetprovider_session_cache.c` as the third asset provider alongside ROM and data providers (Section 3.16.10). Networking-layer integration with the catalog. Out of scope; flagged for future networking-layer work.
- E-22. ROM priority list ordering recommendation (NTSC-final > PAL-final > NTSC-1.0 > JPN-final > PAL-beta > NTSC-beta) per Section 3.16.9. Mike said "recommend" so I picked an order; flag if a different priority is preferred.

**Pass 4 self-extrapolations (E-23 through E-29):**

- E-23. **Provenance metadata in copied assets.** Section 3.16.0 mod authoring workflow says the in-client tool copies cataloged assets into the new compound. I added that each copied entry carries an optional `origin:` field naming the source catalog ID for audit and credit. Mike did not specify; I chose this because it lowers the friction of collaboration and credit-trail.
- E-24. **Compound archive layout convention.** I prescribed a specific layout for `.pdmod` archives (top-level `mod.json` plus inner directories `weapons/`, `meshes/`, `audio/`, etc., with per-asset-class extensions on inner files). Mike did not specify; I chose this because a modder unzipping a compound for inspection sees a self-documenting tree.
- E-25. **First-loaded wins on duplicate ID.** Section 3.5 D-3 specifies the catalog reads in load order and the first-to-register wins on conflict; the second LOUDFAILs and fails to register only the conflicting asset (the rest of its content registers normally). Mike said "LOUDFAIL on duplicate ID" but did not specify the resolution. I chose first-wins-and-warn-second because it lets the user keep both mods enabled and only the colliding item drops. Open question: hard-fail-both vs first-wins-and-warn-second.
- E-26. **Hash-dedupe pool architecture.** Section 3.16.0 specifies the catalog maintains a "bytes by hash" pool plus a "catalog ID to hash" mapping. Mike said "hash-based deduplication at registration." I chose the two-level lookup as the implementation shape because it minimizes memory and keeps catalog operations independent of bytes storage.
- E-27. **AllInOneMods migration framing.** Section 3.5 notes that the historical AllInOneMods (GEX, Kakariko, Goldfinger 64, Dark Noon) replaced base content and need to be reauthored as additive collections under Pass 4. I called this out as a non-trivial future migration pillar. Mike did not explicitly mention it in Pass 4; I extrapolated from his "no overrides" directive that this becomes a real migration concern.
- E-28. **UX implication for additive curation.** Section 3.5 notes that the campaign menu, weapon-loadout picker, character selector, etc. become enriched with mod-introduced variants under additive-only. I sketched a "Built-in" header followed by per-modpack groupings as the modding-hub UX pattern. Mike said "user picks" but did not specify the curation UI; I chose this grouping as a reasonable default.
- E-29. **`data/.session-state.json` persistence shape.** Section 3.16.8 specifies the streak counter file format. Mike said "store in `data/.session-state.json` or similar." I chose JSON with a `version`, `last_clean_launch`, and `self_heal_streaks` map structure. The file is itself read-only with writable-during-extraction-or-self-heal semantics, excluded from manifest hash check. Mike did not specify the exact format; I picked a shape consistent with the rest of the data tier.

**Pass 5 changes:**

- Added Section 3.16.11 (Disabling base content; presentation-layer mechanism) with full architectural detail: catalog `enabled` flag, `disable_base:` field on compound mods, multi-flipper stacking, validation, selector pool composition, interaction with `.pdwepset` and `random_source:`.
- Updated Section 3.5 (Mods are additive) with a Pass 5 refinement subsection bridging to 3.16.11.
- Updated Section 3.16.0 compound-manifest sketch with the `disable_base:` field and a Halo total-conversion example.
- Updated Section 3.16.3 (SP-MP unification) with the `random_source:` field on MP setup config (`all_enabled`, `base_only`, `modpack:<id>`, `weapon_set:<catalog_id>` options).
- Added `.pdwepset` to Section 3.2 extension table and Section 3.3 schema sketches.
- Updated `.pdscenario` schema sketch in Section 3.3 with explicit `weapon_spawns` and `pickup_spawns` arrays carrying per-location catalog IDs.
- Added M-12 (Total conversions become genuinely composable) to Section 3.13.
- Added FW-7 (Grid as Forge-extensible via observer character flag), FW-8 (catalog-driven prop palette), FW-9 (logic-system mods extending Grid) to Section 3.14.

**Pass 5 self-extrapolations (E-30 through E-37):**

- E-30. **Selector pool composition formalized.** Section 3.16.11 formalizes `selector_pool = catalog ∩ enabled ∩ unlocked ∩ context_filter` as a four-way intersection. Mike said the universal selector filter pattern "is already shipped via the catalog universality sweep; now grows to include the `enabled` filter." I formalized the four-way intersection in the doc to make the pattern explicit for future implementers.
- E-31. **Catalog entry `enabled` flag and disable-reason tracking.** Section 3.16.11 specifies the catalog tracks WHICH mod or user setting flipped `enabled` so the audit-log line can list the flipper(s). When multiple mods disable the same base ID, the entry stays disabled until both are disabled. Mike said `enabled` flag with default true; I extrapolated multi-flipper stacking and audit-log shape.
- E-32. **`disable_base:` validation with `LOUDFAIL.CATALOG.UNKNOWN_DISABLE_TARGET`.** Mike said `disable_base:` declares base IDs to filter; I added that misnamed IDs in the list LOUDFAIL with a clear message and the mod still registers (only the unknown disable entry is dropped).
- E-33. **Compound mods can disable AND add simultaneously.** A compound mod's `disable_base:` and `internal_assets:` are independent mechanisms; the Halo total-conversion example uses both. Mike's framing implied this; I made it explicit.
- E-34. **`.pdwepset` validation against currently-disabled weapons.** A weapon set referencing a base weapon that any total-conversion mod has disabled fails registration with `LOUDFAIL.CATALOG.WEPSET_INVALID`. Validation runs after all enabled mods have registered. Mike said sets validating disabled weapons fail; I added the timing detail (post-registration validation).
- E-35. **Empty random pool fallback.** Section 3.16.3 specifies `LOUDFAIL.RANDOM.EMPTY_POOL` plus fallback to a default base weapon (always reachable via direct lookup even if `enabled: false`). Mike did not specify the empty-pool case; I picked LOUDFAIL plus base-weapon fallback as the safe default.
- E-36. **`.pdwepset` registers as `ASSET_WEAPON_SET` in the catalog.** Sets are catalog entries with their own IDs; they can be referenced from `.pdscenario` `default_weapon_set_id` and from MP setup `random_source: weapon_set:<id>`. Mike said sets surface in MP setup picker; I extended to mean they are catalog-registered in the same way as other compound assets.
- E-37. **Per-spawn-point weapon and pickup arrays in `.pdscenario`.** Mike asked the schema sketch to "explicitly document per-location weapon/pickup spawn declarations." I added `weapon_spawns` and `pickup_spawns` arrays with per-spawn `asset_id` (catalog ID), transform, ammo count, and respawn timer. Mike did not specify the field names; I picked these for clarity.

End of audit.


