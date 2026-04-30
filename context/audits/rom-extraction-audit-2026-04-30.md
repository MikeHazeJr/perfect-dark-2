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

**Final extension list** after consolidation:

| Extension | Purpose |
|---|---|
| `.pdwpn` | Weapon |
| `.pdui` | UI texture bundle plus theme metadata |
| `.pdmesh` | Mesh |
| `.pdanim` | Animation |
| `.pdsong` | Music track |
| `.pdsfx` | Sound effect |
| `.pdvoice` | Voice line |
| `.pdscenario` | Stage / mission / firing range / MP map (carries scenario config inline) |
| `.pdtiles` | Stage walkable-surface data |
| `.pdseg` | Stage geometry segment |
| `.pdfont` | Font face |
| `.pdlang` | Language strings (any context: SP missions, MP UI, system messages) |
| `.pdmod` | Mod archive (existing, ZIP container of `.pdXXX` files plus `mod.json`) |
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

**`.pdscenario`** sketch:

```json
{
    "catalog_id": "scenario_villa",
    "display_name": "Villa",
    "stage_num": "SOLO_VILLA",
    "kind": "solo",                     // "solo" | "mp" | "firingrange" | "coop"
    "geometry_id": "bg_villa",          // ref to .pdseg
    "tiles_id": "tiles_villa",          // ref to .pdtiles
    "props": [...],                     // objects, lifts, doors
    "ai": [...],                        // spawn points, paths
    "events": [...],                    // objective triggers, scripted events
    "music_id": "song_villa",           // ref to .pdsong
    "ambient_sfx_ids": ["sfx_villa_amb"],
    "lang_id": "lang_villa",
    "mp_config": null                   // populated for "mp" stages
}
```

The exact schema for each is a follow-on design task. The audit's recommendation is to commit to the principle and design the schemas one asset class at a time, starting with `.pdwpn` (the F11-F13 catalog work just shipped weapons; this is the natural next step).

### 3.4 Directory taxonomy

Three top-level content trees. Same loader, same schema. The directory split tracks redistribution status (and authoring path), not loading mechanism:

| Tree | Purpose | Contents | Ships with release? | Authoring source |
|---|---|---|---|---|
| `base/` | Project-authored, redistributable, decompiled-source-derived | Per-asset `.pdXXX` files for content the project authors and is licensed to ship | Yes | PD2 contributors (in repo) |
| `data/` | BYOR runtime-extracted from user's ROM | Per-asset `.pdXXX` files extracted at first launch from the user's pd.<romid>.z64 | No (gitignored) | First-launch extractor on user machine |
| `mods/` | User-installed mods, bundled mods, and modpack-extracted contents | `.pdmod` archives, `.pdmodpack` archives, and (during install) extracted `.pdXXX` content | Sometimes (bundled mods like `bot-names.pdmod`) | Modders; PD2 (for bundled defaults) |

**Catalog discovery:** the catalog walks all three trees on startup, builds a single asset registry, and applies override rules (Section 3.5).

**Naming policy:** content in `mods/` cannot reuse names already present in `base/` or `data/` unless the mod explicitly declares the override (Section 3.5). This makes the override path a deliberate modder choice, not a side effect of file collision.

**Multi-ROM consideration (extrapolation E-3):** PD2 supports six ROM versions in `tools/extract:321` `vals[]` (currently only NTSC-final at runtime per `port/src/romdata.c:42-62`). Per directive 15, expanding runtime support to all six is approved as future work. Recommendation: under the multi-ROM model, `data/` becomes `data/<romid>/` (or a top-level `data/_active/` symlink that the catalog points at after determining the active ROM via SHA-256 hash check at startup). Avoids cross-version contamination if a user swaps ROMs.

Implication for existing content: `base/weapons.pdbase` (the F11-F13 monolith) is a transitional shape. Splitting into `base/weapons/weapon_*.pdwpn` is the convergent target.

### 3.5 Mod override semantics

Per directive 4, base content and mod content are effectively equal at the loader level; they differ only in location. The override mechanism makes mods a first-class overlay.

**Naming-disallow rule (D-1).** Content in `mods/` cannot reuse a name that already exists in `base/` or `data/` unless the mod explicitly declares the override. Example: a modder shipping `mods/<mymod>/weapons/weapon_falcon2.pdwpn` without an override declaration is a load-time error. The modder must either rename to `weapon_falcon2-mybuild.pdwpn` (a new asset, distinct catalog ID) or declare the override in the mod's manifest.

**Explicit override declaration (D-2).** The mod's `mod.json` (or the modpack's `pack.json`) carries an `overrides` block listing which base / data assets the mod intends to replace:

```json
{
    "id": "mymod_total_falcon_overhaul",
    "version": "1.0.0",
    "overrides": [
        { "catalog_id": "weapon_falcon2", "with": "weapons/weapon_falcon2.pdwpn" },
        { "catalog_id": "sfx_falcon2_fire", "with": "audio/sfx/sfx_falcon2_fire.pdsfx" },
        { "catalog_id": "sfx_falcon2_reload", "with": "audio/sfx/sfx_falcon2_reload.pdsfx" }
    ]
}
```

The override block makes intent explicit and auditable (the catalog logs every active override at startup so modders can verify their work loaded).

**Multiple simultaneous overrides (D-3).** A single mod can declare overrides for many assets at once. Mike's example: "all characters get the same skin" is one mod with N character-mesh overrides; "specific missions get this music track" is one mod with M scenario-music-id overrides. The override list has no cap.

**Multi-mod precedence (D-4, extrapolation E-2).** When two or more enabled mods declare overrides for the same `catalog_id`, the catalog needs a deterministic precedence rule. Possible models:

- **Mod load order (recommended default).** The mod manager already orders mods (`port/src/modmgr.c`); the last-loaded mod wins. Existing UI surface (mod ordering in the modding hub) doubles as override-precedence control.
- **Modder-declared priority.** Each override entry carries an optional `priority: <int>`; higher wins. Falls back to load order on tie. Allows finer control without forcing modders to reorder mods globally.
- **Hard-fail.** Two mods overriding the same asset is a load-time error; the user resolves manually. Strict but high-friction.

Recommendation for default: load-order precedence (matches existing mod manager UX), with optional priority field for advanced cases. Open question for Mike's call (Section 3.15).

**Variant overrides do not need overrides declarations (D-5).** Per directive 5, `weapon_falcon2-mybuild.pdwpn` is a distinct asset, not an override of `weapon_falcon2`. Modders shipping variants do not need an `overrides` block; their content is additive.

**Adding new assets (D-6).** Mods adding entirely new content (e.g. `weapon_my_custom_thing.pdwpn`) require no override declaration. The naming-disallow rule does not apply because the name does not collide.

**Override audit log (extrapolation E-?).** The catalog should log every active override at startup with format: `CATALOG: override <catalog_id> base=<path> mod=<modname> at <path>`. Modders verify their overrides loaded; conflicts (multi-mod) are explicit; LOUDFAIL channel (Section 3.7) hits when an override declaration cannot be resolved (file missing, schema mismatch).

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

Direct consequences of the symmetric-schema principle plus Mike's directives:

**M-1. Mod tools work without a ROM.** Because mods author against the same schema as extracted content, a modder can develop and test against community-shared extracted bundles without owning a ROM. (This is the single largest mod-ergonomics improvement of the entire architectural shift.)

**M-2. Variant naming convention is human-readable (per directive 5).** `weapon_falcon2-silenced.pdwpn` reads as "Falcon 2 silenced variant" at a glance. `weapon_plasmarifle-brute.pdwpn` reads as "Plasma Rifle, brute variant." Modders can identify content from the file tree alone. Variant assets are independent catalog IDs, not flag toggles on a shared base (per directive 5).

**M-3. Mod-from-extract round-trip enables reverse engineering.** A modder who wants to understand how the base game configures a weapon opens `data/weapons/weapon_falcon2.pdwpn` in a text editor (JSON surface schema) and reads the fields directly. No need to know ROM offsets, segment tables, or `preprocessGunFile`. The contract is the schema.

**M-4. Modarchive (`.pdmod`) and modpack (`.pdmodpack`) become the bundling formats.** A standalone mod ships as `mod_name.pdmod` (ZIP archive of `.pdXXX` files plus `mod.json`). A modpack ships as `pack_name.pdmodpack` (ZIP archive of `.pdmod` files plus `pack.json`). Mod authors just package by directory.

**M-5. Override declarations are explicit and audit-loggable (per directive 4 plus 9).** Every active override prints to log on startup; conflicts route to LOUDFAIL.CATALOG. Modders can diagnose "why is my mod not loading" without reading source.

**M-6. Quarantine preserves accidental edits (per directive 7).** A modder who edited `data/weapons/weapon_falcon2.pdwpn` in place (despite read-only) gets the file moved to `data/.quarantine/` rather than overwritten. The project is a safe haven.

**M-7. INI for flat configs, JSON for nested (per directive 17).** `.9slice` (4 numbers) is INI; `.pdwpn` (nested fields) is JSON. Templates with all-fields-commented surface modder-discoverable capabilities without docs.

**M-8. Mod tools load any base content as a starting template (per directive 20).** Missions, arenas, characters, skins, weapons, audio, vehicles all forkable from base content. Lowers the barrier from "study the format spec then author from scratch" to "open this thing, change two fields, save."

### 3.14 Forward-looking work (track but not in scope)

Mike mentioned several features in flight or aspirational that align with the architectural direction. Document in this audit so they are recorded; design out of scope.

**FW-1. Accessories system.** Future feature: characters can have accessories (hats, cigars, glasses, masks) that attach to a default root point on the body. Modders define attach points for custom characters via the `attach_points` field in `.pdmesh` (Section 3.3 sketch). Shooting an accessory does not damage health but knocks it off and triggers audio plus animation. The accessories themselves are independent `.pdmesh` files plus an `attach_to:` metadata field referencing a target attach-point name. Aligns with the symmetric-schema principle: accessories are first-class assets.

**FW-2. Mod-driven character behavior.** Future Banjo-Kazooie example: a custom character could pair with custom run animation (`.pdanim`) plus custom run audio (`.pdsfx`) and integrate with a future sprint feature. The mod system architecture should accommodate behavior-shaped mods, not just visual mods. (Extrapolation E-7: a behavioral schema may want to live in a `.pdcharacter` extension separate from `.pdmesh`. Implied: state machines, animation triggers, sprint integration, accessory attach points all belong to the character behavior layer, not the visual layer. Surface for Mike's call.)

**FW-3. Terrain / mesh editor in-client.** Inspired by `n64decomp/mk64`'s Blender hybrid. Modders create custom content directly in-client using controller or mouse-and-keyboard. Future Forge-for-content-creation pillar. Aligns with hot-reload / live-edit (Principle 1, what-this-unlocks point 2): the editor writes directly to `.pdXXX` files in the user's mod folder, no intermediate format, round-trip clean with extraction.

**FW-4. Bundled-with-release modpacks.** Project author can ship a `.pdmodpack` of curated mods alongside the binary (e.g. "PD2 starter pack" with bot-names + base-ui + modern-ui). Same shape as user-installed modpacks; lives in `mods/` directly.

**FW-5. Logging-pipeline cleanup pass (per directive 9).** Comparable in scale to the recently-shipped context-system rebuild. Fold the LOUDFAIL channel introduction into a broader logging refactor (channel taxonomy, levels, in-game LOUDFAIL UI surface, log file rotation, telemetry export). Track as a future architectural pillar; not in scope for this audit.

**FW-6. ROM-free distribution (long-term, per Principle 1 unlocks).** A community-shared `data-bundle.zip` with hash-validated extracts could ship without the ROM. Architecturally feasible once `data/` is the runtime source. Legal / policy questions out of scope here; flag as a future capability gate.

### 3.15 Open questions and self-extrapolations

This section surfaces points where Mike's directives left a question open or where the audit author extrapolated something Mike did not explicitly say. Mike to review and confirm or correct.

**Open questions (Mike to call):**

- **Q-1. `.pdmodpack` storage model.** Audit recommends contain (extract on install). Reference is the alternative. Mike's open question per directive 6.
- **Q-2. Multi-mod override precedence.** When two enabled mods override the same `catalog_id`, who wins? Audit recommends load-order default plus optional `priority:` field. See Section 3.5 D-4.
- **Q-3. `.pdscenario` vs `.pdmission` extension name.** Mike open per directive 1. Audit recommends `.pdscenario` for genre neutrality (covers SP missions, MP maps, firing range, future modes).
- **Q-4. `.pdcharacter` as a separate extension from `.pdmesh`.** Extrapolation E-7. The visual layer (`.pdmesh`) and behavioral layer (animations, sprint integration, attach-point usage, voice-line triggers) may want a split. Surface for Mike's call.
- **Q-5. Quarantine retention deeper than 1 snapshot.** Directive 7 specifies "up to 1 version." Audit honors that. Extrapolation: should LOUDFAIL fire when an existing quarantine entry is overwritten by a new corruption (the modder's older accidental edit is now lost)? Recommend yes; Mike to confirm.
- **Q-6. Multi-ROM data layout: `data/<romid>/` subdirs vs single shared `data/`.** Extrapolation E-3 recommends per-romid subdirs. Mike's confirmation is needed for the runtime expansion (directive 15).

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

### 3.16 Priority order

Ranked by leverage (impact / cost) for the next 1 to 4 sessions, updated to reflect Mike's directives:

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

**Mike's directives applied (2026-04-30 Pass 2)**

Twenty directives logged in this audit:

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

End of audit.


