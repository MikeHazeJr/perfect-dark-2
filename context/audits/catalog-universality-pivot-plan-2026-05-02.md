# Catalog Universality Pivot Plan (Phase 1 Audit)

> Phase 1 audit for the .pdbase to .pdwpn / .pdui / .pdmesh / .pdhead / .pdbody / .pdarena (and friends) pivot. Mike's universality model: project ships only the executables; on first launch the user's ROM is extracted into per-asset compound files in the same format that mods use, and the catalog at every startup walks `data/<romid>/` and `mods/` and treats both equally. This doc is the audit + plan only; no code lands in this session.
>
> **Date:** 2026-05-02. **Author:** AI session `determined-banach-137a18`. **Scope:** Phase 1 audit. The implementation is multi-session work that follows.
>
> **Methodology gates** per [procedures.md](../procedures.md) and [working-preferences.md](../working-preferences.md):
> - Possibility-framed hypotheses, never near-conclusions.
> - Concrete file:line evidence for every current-state claim.
> - No em-dashes anywhere.
> - No code shipped in this session.
>
> **Companion docs:**
> - [audits/rom-extraction-audit-2026-04-30.md](rom-extraction-audit-2026-04-30.md) Phase 3 extension taxonomy (`.pdwpn`, `.pdui`, `.pdmesh`, etc.) and base-vs-mod symmetry directives.
> - [audits/ui-asset-pipeline-investigation-2026-04-30.md](ui-asset-pipeline-investigation-2026-04-30.md) Phase 2 `.pdui` schema sketch.
> - [pillars/catalog.md](../pillars/catalog.md) live catalog state (Pass A through Pass D shipped 2026-05-02).
> - [designs/catalog/catalog-full-pipeline-weapons.md](../designs/catalog/catalog-full-pipeline-weapons.md) F1 to F13 weapons gate that produced the current `.pdbase` shape.

---

## Executive summary

**Where we are.** The catalog migration just shipped its data + runtime layer (heads + bodies + arenas + weapons F1 to F13) plus the Phase 3 ROM-once-then-disk pivot (Pass A through Pass D). The runtime never touches `g_RomFile` after extraction; SHA-256 sidecars + self-heal loop guard `data/<romid>/` against corruption. Four `.pdbase` archives at the repo root carry project-curated metadata: [base/weapons.pdbase](../../base/weapons.pdbase) 12,823 lines / 86 weapons + 110 animation scripts; [base/heads.pdbase](../../base/heads.pdbase) 930 lines / 84 head records; [base/bodies.pdbase](../../base/bodies.pdbase) 890 lines / 68 body records; [base/arenas.pdbase](../../base/arenas.pdbase) 476 lines / 47 arena records. ROM extraction writes raw byte slabs to `data/<romid>/files/<name>.bin` + `data/<romid>/segs/<segname>.bin` with `.sha256` sidecars per file ([port/src/romextract.c:86-99](../../port/src/romextract.c:86)).

**What Mike asked for.** "Wasn't the idea to extract them and convert them to basegame `.pdxxx` files, so they are in the same universal format all our base game and mod assets use, universality you called it." Concretely: project ships `PerfectDark.exe` + `Updater.exe` + README; first launch extracts the user's ROM into per-asset compound `.pdwpn` / `.pdhead` / `.pdmesh` / `.pdsong` / `.pdvoice` / etc. files; same parser handles both `data/<romid>/*.pd*` and `mods/<modname>/*.pd*`; no `.pdbase` files committed to the repo; project's curation logic (balance choices, schema decisions, default fallbacks, bot preferences, etc.) lives in the EXTRACTOR'S C code and is applied during extraction.

**Where we drift.** The extractor today is build-time Python ([devtools/extract_weapons_pdbase.py](../../devtools/extract_weapons_pdbase.py) 1,557 lines + three siblings totalling 1,296 more) that runs on a developer machine and produces 4 monolithic `.pdbase` archives committed to the repo. The runtime extractor (`romExtractAllFiles` + `romExtractAllSegments`) does emit per-file output but in raw `.bin` form, not `.pd*` compound form. `loaderPdbaseScan` is hardcoded to read 4 fixed archive names from `base/` ([port/src/loader_pdbase.c:1882-2035](../../port/src/loader_pdbase.c:1882)). Mod content is consumed through a parallel pipeline (`.pdmod` ZIP archives parsed by [port/src/modarchive.c](../../port/src/modarchive.c) and [port/src/modmgr.c](../../port/src/modmgr.c)) that does not share parser code with the `.pdbase` loader. Universality is unrealized: base content and mod content have different shapes today.

**What this doc plans.** A multi-session migration to internalize the four Python extractors into runtime C, produce per-asset compound files in the universal `.pd*` taxonomy, switch the loader to a directory-walk pattern that handles `data/<romid>/` and `mods/` identically, and retire the four `.pdbase` archives from the repo. The doc covers six chunks: current state inventory; target state design; numbered migration sequence with risk and test strategy per step; backwards compat / transition strategy; risk assessment; rough session-count estimate.

**Stop conditions.** If during implementation a step's complexity grows past the bounded scope listed in Section 5, halt and re-plan rather than push through (rabbit-hole protocol per [procedures.md](../procedures.md)). The .pdbase files are the current source of truth for weapon DATA; do not delete them until the per-asset compound files are validated against them.

---

## 1. Current state inventory

### 1.1 The four `.pdbase` archives in repo

Every `.pdbase` file at repo root, sized and structured ([Glob `base/*.pdbase`](../../base/)):

| Archive | Size (bytes) | Lines | Records | Top-level keys |
|---|---|---|---|---|
| [base/weapons.pdbase](../../base/weapons.pdbase) | 287,876 | 12,823 | 86 weapons + 110 animations | `pdbase_version`, `type`, `animations`, `weapons` |
| [base/heads.pdbase](../../base/heads.pdbase) | 20,559 | 930 | 84 (75 named + 9 SP fallback) | `pdbase_version`, `type`, `heads` |
| [base/bodies.pdbase](../../base/bodies.pdbase) | 22,049 | 890 | 68 (63 named + 5 SP fallback) | `pdbase_version`, `type`, `bodies` |
| [base/arenas.pdbase](../../base/arenas.pdbase) | 11,871 | 476 | 47 | `pdbase_version`, `type`, `arenas` |

Schema envelope is uniform: each file is a JSON object with `"pdbase_version": 1`, a `"type"` discriminator (`"weapons"` / `"heads"` / `"bodies"` / `"arenas"`), and one top-level array carrying records. Validated by reading first 100 lines of each and parsing with `python3 -c "import json; ..."`.

### 1.2 Representative records

**Weapon record (Falcon 2)** at [base/weapons.pdbase](../../base/weapons.pdbase). Per-weapon shape:

```json
{
  "id": "base:falcon2",
  "weapon_id": 2,
  "symbol": "invitem_falcon2",
  "hi_model": "FILE_GFALCON2",
  "lo_model": "FILE_GFALCON2LOD",
  "equip_animation": "invanim_falcon2_equip",
  "unequip_animation": "invanim_falcon2_unequip",
  "pritosec_animation": null,
  "sectopri_animation": null,
  "functions": [
    { "_symbol": "invfunc_falcon2_singleshot",
      "_struct": "weaponfunc_shootsingle",
      "type": 1, "name": "L_GUN_085", "ammoindex": 0,
      "noisesettings": {"_symbol": "invnoisesettings_default", "minradius": 0,
                        "maxradius": 14, "incradius": 2, "decbasespeed": 1,
                        "decremspeed": 6},
      "fire_animation": "invanim_falcon2_shoot",
      "flags": 0,
      "recoilsettings": {"_symbol": "invrecoilsettings_default",
                         "xrange": 0.6, "yrange": 0.6, "zrange": 0.6,
                         "unk0c": 0.2, "unk10": 1},
      "recoverytime60": 16, "damage": 1, "spread": 1,
      "shootsound": "SFX_804D", "penetration": 1 },
    { "_symbol": "invfunc_falcon2_pistolwhip",
      "_struct": "weaponfunc_melee", ... }
  ],
  "ammos": [{"_symbol": "invammo_falcon2", "type": 1, "casingeject": 0,
             "clipsize": 8, "reload_animation": "invanim_falcon2_reload",
             "flags": 0}, null],
  "aimsettings": {"_symbol": "invaimsettings_default", "zoomfov": 0, ...},
  "muzzlez": 2, "posx": 9, "posy": -15.7, "posz": -23.8, "sway": 1,
  "gunviscmds": [["sethidden", 42], ["sethidden", 43], ..., ["end"]],
  "partvisibility": [[90, 0], [42, 0], ..., [255]],
  "shortname": "L_GUN_007", "name": "L_GUN_007",
  "manufacturer": "L_GUN_150", "description": "L_GUN_156",
  "flags": 702076,
  "bot_pref": {"unk00": 56, "unk01": 60, "unk02": 84, ...}
}
```

**Animation record** (one of 110 in `weapons.pdbase`). Path B data-driven opcode arrays per [designs/catalog/catalog-full-pipeline-weapons.md](../designs/catalog/catalog-full-pipeline-weapons.md):

```json
{ "id": "invanim_falcon2_reload_singlewield",
  "opcodes": [["playanimation", "ANIM_GUN_FALCON2_RELOAD", 0, 10000],
              ["waittime", 7, 2],
              ["end"]] }
```

**Head record** ([base/heads.pdbase:1-12](../../base/heads.pdbase:1)):

```json
{ "id": "base:head_dark_combat",
  "headnum": 4, "ismale": 0, "unk00_01": 1,
  "type": "HEADBODYTYPE_FEMALE", "height": 13,
  "filenum": "FILE_CHEADDARK_COMBAT",
  "scale": 1.0, "animscale": 1.0 }
```

**Body record** ([base/bodies.pdbase:1-12](../../base/bodies.pdbase:1)):

```json
{ "id": "base:djbond",
  "bodynum": 0, "ismale": 1, "unk00_01": 0, "canvaryheight": 0,
  "type": "HEADBODYTYPE_DEFAULT", "height": 167,
  "filenum": "FILE_CDJBOND",
  "scale": 1.0, "animscale": 1.0446009635925,
  "handfilenum": "FILE_GHAND_DDSECURITY" }
```

**Arena record** ([base/arenas.pdbase:1-9](../../base/arenas.pdbase:1)):

```json
{ "id": "base:arena_mp_skedar",
  "arena_index": 0, "slug": "mp_skedar",
  "category": "Dark", "stagenum": 50,
  "requirefeature": 0, "name_langid": 20599,
  "load_mode": "ARENA_LOADMODE_PLAYABLE" }
```

### 1.3 Build-time extractors (Python)

Four scripts under `devtools/`. They are developer-machine tools that parse C source and emit `.pdbase` JSON; they are not invoked at runtime:

| Script | Lines | Source it parses | Output |
|---|---|---|---|
| [devtools/extract_weapons_pdbase.py](../../devtools/extract_weapons_pdbase.py) | 1,557 | `src/game/invitems.c` (retired at F13), `src/game/botinv.c`, `src/include/constants.h`, `src/include/gunscript.h`, `src/include/types.h` | `base/weapons.pdbase` |
| [devtools/extract_heads_pdbase.py](../../devtools/extract_heads_pdbase.py) | 411 | `src/game/modeldata/robot.c`, `src/game/mplayer/mplayer.c`, `port/src/assetcatalog_base.c` | `base/heads.pdbase` |
| [devtools/extract_bodies_pdbase.py](../../devtools/extract_bodies_pdbase.py) | 436 | (parallel to heads, body slots in `g_HeadsAndBodies[]`) | `base/bodies.pdbase` |
| [devtools/extract_arenas_pdbase.py](../../devtools/extract_arenas_pdbase.py) | 449 | `g_MpArenas[]`, `s_ArenaNames[]`, `s_ArenaGroupMap[]` | `base/arenas.pdbase` |

The weapon extractor handles the heaviest lift: tokenize C, resolve `#define` constants iteratively to fixed point, walk `g_Weapons[]` + 75+ `invitem_*` + 150+ `invfunc_*` + 80+ `invammo_*` + 13+8+4 settings records + 110 `invanim_*` opcode arrays + 14 `gunviscmds_*` + 14 `invpartvisibility_*`, decode `gunscript_*` macros into JSON opcode lists, and emit the inlined record format above. Determinism is byte-stable (per [extract_weapons_pdbase.py:49](../../devtools/extract_weapons_pdbase.py:49) "same source -> same output bytes").

`src/game/invitems.c` was reduced to a 43-line grep-trail header comment at S484 F13 ([src/game/invitems.c](../../src/game/invitems.c)); the static records it used to hold now live exclusively in `base/weapons.pdbase`.

### 1.4 Runtime ROM extractor (C, just shipped 2026-05-02)

[port/src/romextract.c](../../port/src/romextract.c) (945 lines) and [port/include/romextract.h](../../port/include/romextract.h) (168 lines) implement the Phase 3 Pass A through Pass D pipeline. Public surface:

- `romExtractAllFiles()` ([romextract.c:86-99](../../port/src/romextract.c:86)): walks ROM file table, writes each non-empty slot to `data/<romid>/files/<sanitized_name>.bin` (or `data/<romid>/files/G_<XXXX>.bin` for unnamed slots). Idempotent via size match.
- `romExtractAllSegments()` ([romextract.h:85](../../port/include/romextract.h:85)): writes loaded ROM segments (sfxctl, sfxtbl, seqctl, seqtbl, sequences, animations, fonts, mp* tables, textures, copyright) to `data/<romid>/segs/<segname>.bin`.
- `romExtractVerifyAll()` + `romExtractVerifyAllSegments()` ([romextract.h:46](../../port/include/romextract.h:46), [romextract.h:97](../../port/include/romextract.h:97)): hash-verify against `.sha256` sidecars; on mismatch, quarantine to `data/_quarantine/<romid>/<unixtime>_<basename>` and re-extract.
- Sidecar format ([romextract.c:129-155](../../port/src/romextract.c:129)): one line of 64 hex chars + newline. Matches `sha256sum` output minus the filename column.
- Pass D toast queue + boot integrity report ([romextract.h:124-146](../../port/include/romextract.h:124)).

Boot wiring ([port/src/main.c:283-341](../../port/src/main.c:283)):

```
romdataInit() -> romExtractAllFiles() -> romExtractVerifyAll() ->
romExtractAllSegments() -> romExtractVerifyAllSegments() ->
romExtractEmitBootIntegrityReport() -> romdataReleaseRom()
```

After `romdataReleaseRom()`, `g_RomFile` is freed and the runtime never touches the ROM mapping again ([pillars/catalog.md](../pillars/catalog.md) Pass C section).

Output shape on disk after first launch (NTSC build, `VERSION_ROMID = "ntsc-final"` per [CMakeLists.txt:34](../../CMakeLists.txt:34)):

```
data/
  ntsc-final/
    files/
      Gfalcon2.bin              <- raw ROM bytes for FILE_GFALCON2
      Gfalcon2.bin.sha256       <- 64-hex-char digest
      Gfalcon2lod.bin
      ...
      Cdjbond.bin               <- raw ROM bytes for FILE_CDJBOND
      ...
      G_004f.bin                <- ROM file slot 0x4f had no name
      ...
    segs/
      animations.bin
      animations.bin.sha256
      sfxctl.bin
      seqctl.bin
      ...
  _quarantine/
    ntsc-final/
      <unixtime>_<basename>     <- corrupted file evicted before re-extract
  ui/
    textures/
      <name>.tga                <- written by pdguiThemeExtractRomTextures
      <name>.png
      <name>.9slice.json
```

Note: the [audits/rom-extraction-audit-2026-04-30.md](rom-extraction-audit-2026-04-30.md) said the runtime UI extractor wrote to `mods/base-ui/textures/`; it now writes to `data/ui/textures/` ([pdgui_theme.cpp:2544](../../port/fast3d/pdgui_theme.cpp:2544)). The retirement of the legacy `mods/base-ui/` path appears to have happened during S593-era texture-deployment cleanup; the runtime emits TGA + PNG + nineslice JSON loose files, NOT a `.pdui` compound archive yet.

### 1.5 Catalog parse-time entry points

The `.pdbase` JSON archives are parsed by a single function [loaderPdbaseScan()](../../port/src/loader_pdbase.c:1869) at [port/src/loader_pdbase.c:1869-2038](../../port/src/loader_pdbase.c:1869). The function:

1. Loads `<dir>/weapons.pdbase` via `fsFileLoad` (line 1888), parses through `parseTopLevel` (custom JSON parser at the top of the file).
2. Loads `<dir>/heads.pdbase` (line 1934), same parser.
3. Loads `<dir>/bodies.pdbase` (line 1970), same parser.
4. Loads `<dir>/arenas.pdbase` (line 2006), same parser.

Each archive populates a typed pool inside `loader_pdbase.c`:

- [s_Weapons[CATALOG_MGR_WEAPON_COUNT]](../../port/src/loader_pdbase.c:96), [s_Guncmds[3000]](../../port/src/loader_pdbase.c:98), [s_Gunviscmds[500]](../../port/src/loader_pdbase.c:99), [s_Partvis[500]](../../port/src/loader_pdbase.c:100), [s_Ammos[120]](../../port/src/loader_pdbase.c:101), [s_AimSettings[120]](../../port/src/loader_pdbase.c:102), [s_NoiseSettings[120]](../../port/src/loader_pdbase.c:103), [s_RecoilSettings[120]](../../port/src/loader_pdbase.c:114), [s_WeaponFuncs[256]](../../port/src/loader_pdbase.c:117), [s_Animations[256]](../../port/src/loader_pdbase.c:120), [s_BotPrefs](../../port/src/loader_pdbase.c:97).
- `s_HeadsPool[CATALOG_MGR_HEAD_COUNT]` populated by Catalog Gate 3 F12 ([loader_pdbase.h:127](../../port/include/loader_pdbase.h:127)).
- `s_BodiesPool[CATALOG_MGR_BODY_COUNT]` ([loader_pdbase.h:162](../../port/include/loader_pdbase.h:162)).
- `s_ArenasPool[CATALOG_MGR_ARENA_COUNT]` ([loader_pdbase.h:198](../../port/include/loader_pdbase.h:198)).

Symbol-string resolution (e.g. `"FILE_GFALCON2"` -> integer filenum, `"ANIM_GUN_FALCON2_RELOAD"` -> integer anim id, `"SFX_804D"` -> integer SFX id, `"L_GUN_007"` -> integer lang id) lives in [port/src/loader_pdbase_enums.c](../../port/src/loader_pdbase_enums.c) (5,499 lines, 5,400+ entries across ANIM / SFX / FILE / L_GUN families). Generated from C headers by the F12 build pipeline.

Boot wiring ([main.c:412-445](../../port/src/main.c:412)) runs once per startup, AFTER `assetCatalogRegisterBaseGame()` and `assetCatalogScanComponents(modsdir)`:

```c
loader_pdbase_result_t pdb_result;
loaderPdbaseScan("base", &pdb_result);
if (pdb_result.weapons_registered > 0) {
    loaderPdbaseBuildWeaponManager();
    assetCatalogRegisterWeaponModelFiles();
}
if (pdb_result.heads_registered > 0)  loaderPdbaseBuildHeadManager();
if (pdb_result.bodies_registered > 0) loaderPdbaseBuildBodyManager();
if (pdb_result.arenas_registered > 0) {
    loaderPdbaseBuildArenaManager();
    loaderPdbaseRunParityCheckArenas();
}
```

Two parser-related observations:

- The directory argument is hardcoded to `"base"` ([main.c:414](../../port/src/main.c:414)). The function does not currently scan a directory pattern (e.g. `base/*.pdbase`); it concatenates four fixed filenames. Generalising to a glob is a small change but must coordinate with the loader-pool sizing (each pool size is hardcoded for the 86 / 84 / 68 / 47 record counts).
- `assetCatalogRegisterBaseGame()` runs BEFORE `loaderPdbaseScan`. This means the catalog row layer is populated from the legacy in-binary tables (`g_Stages[]`, `g_HeadsAndBodies[]`, `g_MpArenas[]`, etc.) FIRST, and the loader pool layer is populated SECOND. The two layers must agree at boot or the parity check warnings fire. Heads and bodies eliminated their parity period at F13; arenas still runs `loaderPdbaseRunParityCheckArenas()` ([main.c:438](../../port/src/main.c:438)).

### 1.6 Runtime consumers of `.pdbase` data

Per `Grep -c 'catalogManager' src/game/*.c port/src/...`:

| File | catalogManager calls |
|---|---|
| [src/game/bot.c](../../src/game/bot.c) | 16 |
| [src/game/botinv.c](../../src/game/botinv.c) | 14 |
| [src/game/body.c](../../src/game/body.c) | 3 |
| [src/game/bondgunreset.c](../../src/game/bondgunreset.c) | 1 |
| [src/game/game_0b0fd0.c](../../src/game/game_0b0fd0.c) | 3 |
| [src/game/invitems.c](../../src/game/invitems.c) | 1 |
| [src/game/modelmgrreset.c](../../src/game/modelmgrreset.c) | 1 |
| [src/game/player.c](../../src/game/player.c) | 1 |
| [src/game/playerreset.c](../../src/game/playerreset.c) | 1 |
| [src/game/mplayer/mplayer.c](../../src/game/mplayer/mplayer.c) | 3 |
| [port/src/catalog_mgr_*.c](../../port/src/) (4 files) | 60 (manager internals) |
| [port/src/assetcatalog_api.c](../../port/src/assetcatalog_api.c) | 18 |
| [port/src/assetcatalog_base_extended.c](../../port/src/assetcatalog_base_extended.c) | 4 |
| [port/src/assetcatalog_scanner.c](../../port/src/assetcatalog_scanner.c) | 1 |
| [port/src/main.c](../../port/src/main.c) | 3 |

Total: ~140 production call sites resolve weapon / head / body / arena data through the manager API (`catalogManagerGetWeaponByIndex`, `catalogManagerGetHeadByIndex`, `catalogManagerGetBodyByIndex`, `catalogManagerGetArenaByIndex`, `catalogManagerGetWeaponBotPref`, `catalogManagerWeaponDefaultAimSettings`, `catalogManagerWeaponDefaultNoiseSettings`, `catalogManagerWeaponSetEyespyForStage`).

The manager files ([catalog_mgr_weapons.c](../../port/src/catalog_mgr_weapons.c) 167 lines, [catalog_mgr_heads.c](../../port/src/catalog_mgr_heads.c) 383 lines, [catalog_mgr_bodies.c](../../port/src/catalog_mgr_bodies.c) 305 lines, [catalog_mgr_arenas.c](../../port/src/catalog_mgr_arenas.c) 274 lines) are thin routers: each accessor calls `loaderPdbaseGet<Type>(idx)` if the loader is active, otherwise a fallback to the legacy in-binary table for asset classes that still have a parity period. Weapons + heads + bodies retired their fallback at F13; arenas is in F12 parity territory.

Total loader binary surface: [loader_pdbase.c](../../port/src/loader_pdbase.c) 2,334 lines + [loader_pdbase_enums.c](../../port/src/loader_pdbase_enums.c) 5,499 lines + [loader_pdbase.h](../../port/include/loader_pdbase.h) 223 lines = 8,056 lines.

### 1.7 Project-curated metadata vs ROM-1:1 byte data

The .pdbase records carry project-CURATED metadata. The raw ROM bytes (the actual model geometry, animation keyframes, audio sample data, texture pixels) are NOT in `.pdbase`. They are written separately to `data/<romid>/files/*.bin` by `romExtractAllFiles` and to `data/<romid>/segs/*.bin` by `romExtractAllSegments`. The .pdbase points at the byte slabs by symbolic enum reference ("FILE_GFALCON2") which the loader resolves to a filenum which other engine code resolves to a path on disk.

What is project-curated (must internalize into the runtime extractor as constants if the build-time Python pipeline retires):

1. **Catalog ID strings** ("base:falcon2", "base:head_dark_combat", "base:djbond", "base:arena_mp_skedar"). We minted the namespace + slug; ROM has no concept of strings keying assets.
2. **Asset class taxonomy.** Which slot is a weapon vs a head vs a body vs an arena, and within each which integer slot (`weapon_id`, `headnum`, `bodynum`, `arena_index`) maps to which catalog ID.
3. **Animation opcode trees** (Path B). The 110 `invanim_*` records are gunscript opcode arrays that drive the legacy `invexec.c` interpreter. Today they live inline in `weapons.pdbase`. Path A (struct-encoded animations) was rejected at S484 design per [designs/catalog/catalog-full-pipeline-weapons.md](../designs/catalog/catalog-full-pipeline-weapons.md) Section J.
4. **Default fallback values** (`invaimsettings_default`, `invnoisesettings_silent`). Hardcoded sentinels at [loader_pdbase.c:2055-2070](../../port/src/loader_pdbase.c:2055).
5. **Bot AI preferences** (`g_AibotWeaponPreferences[]`). 24-byte per-weapon AI config (target ammo per priority class, has-priammo-goal flags, distance configs). This is gameplay tuning, not ROM data.
6. **Inlining decisions** (sub-records inlined; cross-references stay as named strings; `_symbol` field preserved for debugging round-trip).
7. **Hand-tuned weapon flags** (e.g. WEAPONFLAG_DETERMINER_S_AN, WEAPONFLAG_DETERMINER_F_AN at [catalog_mgr_weapons.c:128](../../port/src/catalog_mgr_weapons.c:128)). These are bitmask flags applied per weapon.
8. **Head / body type / scale / animscale / handfilenum mappings.** The integer fields like `headnum=4`, `height=13`, `scale=1.0`, `animscale=1.0446009635925`, `handfilenum=FILE_GHAND_DDSECURITY` are project-authored derivations. Some fields ARE in ROM (the body modeldef pointers); others (`canvaryheight`, `unk00_01`, the SP-fallback head IDs) are project-curated.
9. **Arena category strings** ("Dark"), `name_langid` (which language string to display), `requirefeature` flags, `load_mode` constants (`ARENA_LOADMODE_PLAYABLE`, etc.). These are arena-management taxonomy, not ROM byte data.
10. **EYESPY mutator variants** at [catalog_mgr_weapons.c:114-151](../../port/src/catalog_mgr_weapons.c:114). The catalog manager has runtime mutation logic that swaps EYESPY's display name + flags based on stage (Airbase / Chicago / MBR variants). This is not data in .pdbase; it is game logic that READS .pdbase data.

What is 1:1 with ROM (no curation; bytes flow through unchanged):

1. **Model file bytes** (the file `Gfalcon2.bin` content equals ROM bytes at `FILE_GFALCON2`'s offset).
2. **Animation file bytes** (raw animation frame data in `data/<romid>/segs/animations.bin`).
3. **SFX bank entries** (raw ALADPCM samples from `sfxtbl` segment).
4. **Texture pixel data** (RGBA16 / IA16 / IA8 / IA4 bytes from texture segment, decoded once at extraction by `pdguiThemeExtractRomTextures` for UI textures only; other classes still re-decode at every load).
5. **Sequence music** (MIDI-style sequence data in `seqtbl` + `sequences` segments).
6. **Stage geometry** (`bg_*.seg` files, raw GBI display lists + vertex arrays).

The pivot Mike wants assumes: the project-curated metadata moves from "live in committed JSON files" to "live in the extractor's C code as constants the extractor writes into the per-asset compound files at extraction time." The 1:1 ROM bytes flow into the per-asset compounds as embedded payloads.

---

## 2. Target state design

This section sketches the target shape. Schema details for each per-asset compound are in [audits/rom-extraction-audit-2026-04-30.md](rom-extraction-audit-2026-04-30.md) Section 3.2 and [audits/ui-asset-pipeline-investigation-2026-04-30.md](ui-asset-pipeline-investigation-2026-04-30.md). This audit reuses those references rather than redoing them.

### 2.1 The taxonomy (Mike + 04-30 audit, this audit confirms)

One extension per asset class. The 2026-04-30 ROM extraction audit (Section 3.2) settled on the following extension list, which this plan adopts unchanged:

| Extension | Asset class | Replaces (today) |
|---|---|---|
| `.pdwpn` | Weapon record | One row of `base/weapons.pdbase` `weapons[]` |
| `.pdhead` | Character head record | One row of `base/heads.pdbase` `heads[]` |
| `.pdbody` | Character body record | One row of `base/bodies.pdbase` `bodies[]` |
| `.pdarena` | MP arena record | One row of `base/arenas.pdbase` `arenas[]` |
| `.pdmesh` | Mesh / model (visual geometry; weapon model, prop model, etc.) | The bytes of `data/<romid>/files/Gfalcon2.bin` plus a JSON metadata sidecar |
| `.pdanim` | Animation script (gunscript opcodes today) | One row of `base/weapons.pdbase` `animations[]` for weapon anims; eventually character anims too |
| `.pdsfx` | Sound effect | One entry inside the SFX bank, today fused in `data/<romid>/segs/sfxtbl.bin` |
| `.pdsong` | Music track | One entry inside `seqtbl` + `sequences` segments |
| `.pdvoice` | Voice line | Subset of SFX bank, separated at extraction by symbolic actor + index |
| `.pdscenario` | Stage / mission / firing range / MP map | Today: a stage table row + bg / pads / setup / mpsetup / tile file IDs all crossed-referenced through `g_Stages[]` |
| `.pdtiles` | Stage walkable surfaces | Today: `data/<romid>/files/<stage>_padsZ.bin` |
| `.pdseg` | Stage geometry segment | Today: `data/<romid>/files/bg_<stage>.bin` |
| `.pdfont` | Font face | Today: a row inside `data/<romid>/segs/font<face>.bin` |
| `.pdlang` | Language string bank | Today: `data/<romid>/files/L<stage>Z.bin` plus `data/<romid>/segs/mpstrings*.bin` |
| `.pdui` | UI texture bundle + theme metadata | Today: loose files at `data/ui/textures/<name>.tga + .png + .9slice.json` |
| `.pdmod` | Mod compound (zip archive of `.pd*` files plus `mod.json`) | Already exists; will be augmented with the new per-asset extensions |
| `.pdmodpack` | Mod pack (zip of `.pdmod` archives) | New |

The audit's Pass 4 architectural shift (mods are strictly additive; no two registrations share a catalog ID; presentation-layer `enabled: false` for total conversions) is incorporated by reference.

### 2.2 Storage layout

**Hypothesis A: per-asset directories under `data/<romid>/`.**

```
data/
  ntsc-final/
    weapons/
      base_falcon2.pdwpn          <- one weapon record + its mesh refs
      base_cmp150.pdwpn
      ...
    heads/
      base_dark_combat.pdhead
      base_djbond_head.pdhead
      ...
    bodies/
      base_djbond.pdbody
      ...
    arenas/
      base_arena_mp_skedar.pdarena
      ...
    meshes/
      base_falcon2_hi.pdmesh       <- raw model bytes + metadata
      base_falcon2_lo.pdmesh
      base_cdjbond.pdmesh
      ...
    animations/
      base_invanim_falcon2_reload.pdanim
      ...
    audio/
      sfx/
        base_sfx_804d.pdsfx
        ...
      music/
        base_song_pelagic.pdsong
        ...
      voice/
        base_voice_carrington_intro_01.pdvoice
        ...
    scenarios/
      base_scenario_villa.pdscenario
      ...
    ui/
      base_pd_original.pdui        <- UI texture bundle as a single .pdui
    fonts/
      base_handelgothic.pdfont
      ...
    lang/
      base_villa_en.pdlang
      ...
    manifest.pdmanifest            <- top-level: every file + SHA-256
```

Filename convention: `<namespace>_<slug>.pd<ext>`. Catalog IDs ("base:falcon2") map to filenames by replacing the colon with an underscore. Reverse mapping is unambiguous because the underscore is a valid colon-replacement (the namespace cannot itself contain underscores by convention).

**Hypothesis B: flat per-class directories.**

Same directories but no `<romid>/` tier. `data/weapons/`, `data/heads/`, etc. Argument: the ROM is a one-time bootstrap; users have one ROM at a time; the multi-ROM tier complicates path resolution and is unrealized today (`tools/extract:321` `vals[]` keys six versions but runtime is NTSC-final-only). Argument against: re-introduces ROM-version contamination if the user swaps ROMs.

**Recommendation: Hypothesis A.** Per-romid tier matches the existing `data/<romid>/files/` + `data/<romid>/segs/` layout. Cross-version isolation is cheap. Multi-ROM runtime support (NTSC + PAL + JPN) is a forward-looking goal per [audits/rom-extraction-audit-2026-04-30.md](rom-extraction-audit-2026-04-30.md) A-6 (tracked in `roadmap.md`).

**Mods directory layout (per [pillars/modding.md](../pillars/modding.md)):** unchanged. `mods/<modname>/` for folder mods, `mods/<modname>.pdmod` for archive mods. Mods can ship `.pdwpn` / `.pdhead` / `.pdmesh` / etc. files alongside or inside their archive; the catalog walks both.

### 2.3 Schema design per `.pd*` extension

Each `.pd*` file is either a JSON document (for metadata-heavy classes like `.pdwpn`, `.pdhead`, `.pdbody`, `.pdarena`, `.pdscenario`) or a compound container (ZIP archive holding a `manifest.json` plus binary blobs for byte-heavy classes like `.pdmesh`, `.pdsfx`, `.pdui`).

**`.pdwpn` schema sketch.** A direct lift from the `weapons[]` per-row shape in `weapons.pdbase`, plus the schema header that the loader uses to disambiguate file class:

```json
{
  "pd_kind": "weapon",
  "pd_schema_version": 1,
  "id": "base:falcon2",
  "weapon_id": 2,
  "symbol": "invitem_falcon2",
  "hi_model": "base:falcon2_hi",
  "lo_model": "base:falcon2_lo",
  "equip_animation": "base:invanim_falcon2_equip",
  "unequip_animation": "base:invanim_falcon2_unequip",
  "functions": [...],
  "ammos": [...],
  "aimsettings": {...},
  "muzzlez": 2, "posx": 9, "posy": -15.7, "posz": -23.8, "sway": 1,
  "gunviscmds": [...],
  "partvisibility": [...],
  "shortname": "L_GUN_007", "name": "L_GUN_007",
  "manufacturer": "L_GUN_150", "description": "L_GUN_156",
  "flags": 702076,
  "bot_pref": {...}
}
```

Differences from today's per-row shape inside `weapons.pdbase`:

- **`pd_kind` discriminator** at the top. Lets the universal loader route to the per-class parser without filename inspection.
- **Cross-references promoted to catalog IDs** ("FILE_GFALCON2" symbolic enum becomes "base:falcon2_hi" catalog ID; "ANIM_GUN_FALCON2_RELOAD" stays as a symbolic enum because the underlying engine indexes it by integer but the lookup is symbol-table-mediated).
- **Animations move to sibling `.pdanim` files**, not embedded under `animations[]` at archive root. Each `.pdanim` is one record. A weapon's `equip_animation` field references the catalog ID of its anim. Cross-cuts: today the `weapons.pdbase` envelope colocates animations because the F11 extractor needed to keep cross-references resolvable in one parser pass; in the universal model the catalog ID layer mediates.

**`.pdhead` / `.pdbody` schemas.** Direct lift from per-row shape. `filenum` symbolic enum becomes a catalog ID reference to the `.pdmesh` for that head/body model. `handfilenum` likewise.

**`.pdarena`.** `stagenum` integer kept (the arena-to-stage mapping is Mike's curation, not engine-derived); `name_langid` integer kept; `category` string kept; `load_mode` enum-string kept. Cross-reference to the actual stage `.pdscenario` by catalog ID, not stagenum.

**`.pdmesh` schema (compound).** ZIP archive containing:

```
manifest.json     <- "pd_kind": "mesh", id, source_filenum_symbol (provenance only),
                     materials, attach_points (forward-looking accessories),
                     skeleton (forward-looking)
geometry.bin      <- raw ROM bytes of the model file (was data/<romid>/files/<name>.bin)
geometry.bin.sha256  <- digest for self-heal
```

The byte payload (`geometry.bin`) is the same bytes that today live at `data/<romid>/files/Gfalcon2.bin`. The compound just packages those bytes with an explicit per-asset manifest, removing the implicit "filename equals provenance" coupling and making the asset self-describing.

**`.pdanim` schema.** JSON; one opcode-array record per file:

```json
{
  "pd_kind": "animation",
  "id": "base:invanim_falcon2_reload",
  "category": "weapon_animation",
  "opcodes": [["playanimation", "ANIM_GUN_FALCON2_RELOAD", 0, 10000],
              ["waittime", 7, 2],
              ["end"]]
}
```

The `ANIM_GUN_*` symbol stays a string because the legacy `invexec.c` interpreter resolves it via the same enum table the runtime currently uses ([loader_pdbase_enums.c](../../port/src/loader_pdbase_enums.c)).

**`.pdscenario` schema.** Open question: today the stage data is split across multiple files (bg, pads, setup, mpsetup, tile). One option is a compound `.pdscenario` ZIP containing all five payloads + a `manifest.json`. Another is a thin `.pdscenario` JSON that references five sibling `.pdseg` / `.pdtiles` / etc. files by catalog ID. Per [audits/rom-extraction-audit-2026-04-30.md](rom-extraction-audit-2026-04-30.md) Section 3.2 paragraph "Per-stage-per-resource granularity," modders can swap geometry separately from tiles separately from setup; that argues for multiple sibling files joined by reference. Recommendation: split, with cross-reference. This is the same pattern as `.pdwpn` referring to `.pdmesh` and `.pdanim`.

**`.pdsfx` / `.pdsong` / `.pdvoice` schemas.** Compound. Manifest fields: `pd_kind`, `id`, format (`ALADPCM`, `OGG_REPLACEMENT`, etc.), sample_rate_hz, transcript (voice only), data file reference. Audio extraction needs to split today's monolithic SFX bank into per-effect files; that decoder logic lives in [port/src/preprocess/segaudio.c](../../port/src/preprocess/segaudio.c) today and can be invoked by the new extractor.

**`.pdui` schema.** Compound ZIP. The 2026-04-30 audit Section 3.16 contains the full sketch. Today's loose `data/ui/textures/*.tga + .png + .9slice.json` files become a single `.pdui` archive. The current `pdguiThemeExtractRomTextures` ([pdgui_theme.cpp:2424](../../port/fast3d/pdgui_theme.cpp:2424)) writes loose files; under the pivot it would write into a `.pdui` ZIP.

**`.pdfont` / `.pdlang`.** Compound. Font files have format-specific metadata (face, sizes). Lang files have per-locale string banks plus locale identifier.

**Universal envelope.** Every `.pd*` file (whether plain JSON or compound) has a top-level `pd_kind` discriminator + `pd_schema_version` integer. The loader inspects these before dispatching to the per-class parser. Schema bumps are explicit; an old build reading a new file fails the version gate cleanly.

### 2.4 Loader change

The `loaderPdbaseScan` function ([loader_pdbase.c:1869](../../port/src/loader_pdbase.c:1869)) has to grow into a directory walker that handles both `data/<romid>/` and `mods/<modname>/`. Sketch:

```
catalogLoadAllAssets(romid) {
    for each tier in [data/<romid>/, mods/]:
        for each subdir [weapons/, heads/, bodies/, arenas/, meshes/,
                         animations/, audio/sfx/, audio/music/, audio/voice/,
                         scenarios/, ui/, fonts/, lang/]:
            for each file in subdir matching *.pd<ext>:
                load file
                inspect pd_kind
                dispatch to per-class parser
                register catalog row
                populate manager pool
}
```

Two parser strategies:

- **Strategy A: one parser per `.pd*` extension.** Each parser owns the schema for its kind. New asset class = new parser. Maintains parity with today's `loaderPdbaseScan` (which has a per-archive parseTopLevel branch).
- **Strategy B: one universal parser, schema-driven.** Each `.pd*` file declares its schema; the loader looks up the registered schema by `pd_kind`, validates, and emits typed records. Adding a new asset class = registering a new schema, not writing a new parser.

Recommendation: Strategy A for the first pass (matches existing per-class manager structure); revisit Strategy B in a polish pass once all classes are migrated. The audit at [audits/rom-extraction-audit-2026-04-30.md](rom-extraction-audit-2026-04-30.md) Section 3.16 Tooling Gap T-4 mentions schema validators; that work converges with Strategy B.

### 2.5 Extractor change

Today: four Python scripts at `devtools/extract_*_pdbase.py` (1,557 + 411 + 436 + 449 = 2,853 lines) parse C source AT BUILD TIME and emit JSON archives that ship with the binary.

Target: one runtime extractor in C ([port/src/romextract.c](../../port/src/romextract.c) is the natural home; it already owns ROM file + segment extraction) augmented with per-asset-class generators. Each generator:

1. Reads ROM bytes for the asset's source file (via the existing `g_RomFile + ofs` mapping during the boot extraction phase).
2. Applies project-curated metadata (the constants the Python script applied at build time) baked into the C generator as static data tables.
3. Writes a `.pd*` file at `data/<romid>/<class>/<filename>.pd<ext>`.
4. Writes a `.sha256` sidecar (existing pattern).

The project curation that today lives in Python migrates to C constants. Sources:

- Weapon flags / damage / bot prefs / animation opcode arrays: embed as static `const struct weapon_curation_t s_BaseWeapons[86]` table generated from the current `base/weapons.pdbase` content (one-time conversion). The generator combines ROM bytes + curation table to produce `.pdwpn`.
- Head / body / arena tables: same pattern. Static `const head_curation_t s_BaseHeads[84]`, `const body_curation_t s_BaseBodies[68]`, `const arena_curation_t s_BaseArenas[47]`.
- Animation opcodes: static `const guncmd_curation_t s_BaseAnimations[110][maxlen]`.

This shifts ~9,000 lines of metadata from `base/*.pdbase` (committed JSON) to `port/src/romextract_curation_*.c` (committed C tables), but the binary footprint is similar (the JSON is text; the C is binary const data) and the user-facing output (the actual `.pd*` files) is identical to what a modder authoring against schema would produce.

**Round-trip property.** A modder opening `data/<romid>/weapons/base_falcon2.pdwpn` in a text editor sees a real, modifiable schema-conforming JSON file. They can save it as `mods/<mymod>/weapons/<myslug>.pdwpn` with a renamed catalog ID and ship it. The catalog loader handles their authored `.pdwpn` identically to the extracted one. Universality realized.

### 2.6 Mod system relationship

Today `mods/` content goes through [port/src/modarchive.c](../../port/src/modarchive.c) + [port/src/modmgr.c](../../port/src/modmgr.c) + [port/src/assetcatalog_scanner.c](../../port/src/assetcatalog_scanner.c). The INI scanner reads `.ini` component manifests; the JSON content path reads `mod.json` content arrays.

Under the pivot, mods can ship `.pd*` files directly. Two compatibility models:

- **Model 1: `.pdmod` archives stay an umbrella.** A `.pdmod` is a ZIP that contains `.pdwpn`, `.pdhead`, `.pdmesh`, etc. files at known sub-paths. The mod loader extracts (or VFS-mounts) the `.pdmod`, then dispatches each interior `.pd*` to the universal `.pd*` loader. Backwards compatible: existing INI-based mods continue to work; new mods can use `.pd*` files.
- **Model 2: `.pdmod` retires; mods are loose `.pd*` files.** Each mod is a folder of `.pd*` files plus a `mod.json` at folder root. ZIP archives are not used at the mod level. Simpler conceptually; loses `.pdmod` distribution UX (one file = one mod for sharing).

Recommendation: Model 1. The existing `.pdmod` archive infrastructure (network distribution per [pillars/modding.md](../pillars/modding.md), VFS LRU cache, atomic write, SHA-256 integrity, property handler DLL) is significant and works; the per-asset extension change happens INSIDE the `.pdmod` archive. The loose-folder mod alternative is preserved through the existing folder-mod path.

### 2.7 Scope guardrails

Out of scope for the core pivot (each may become a follow-up lane):

- Multi-ROM runtime support (NTSC + PAL + JPN) per [audits/rom-extraction-audit-2026-04-30.md](rom-extraction-audit-2026-04-30.md) A-6.
- ROM-free distribution mode (`data-bundle.zip` of validated extracts) per [audits/rom-extraction-audit-2026-04-30.md](rom-extraction-audit-2026-04-30.md) A-5.
- Schema validators with per-`pd_kind` JSON Schema files (Tooling Gap T-4).
- In-client mod authoring tools that round-trip from `data/<romid>/` content (Mod-friendliness M-1, M-3, M-8).
- Loading-screen modal during first-launch extraction per [audits/rom-extraction-audit-2026-04-30.md](rom-extraction-audit-2026-04-30.md) A-7.
- Extending the catalog to all 86 WEAPON slots (today only 41 MPWEAPON entries; per [pillars/catalog.md](../pillars/catalog.md) Known Gaps).
- Bot profiles + bot variants migration (per [pillars/catalog.md](../pillars/catalog.md) "What is in flight").

---

## 3. Migration sequence

Numbered steps, each a coherent unit of implementation work. Sequenced to minimize cross-step merge conflicts (the catalog code surface is heavily contested, so steps that touch the manager files are spaced out).

### Step 0: schema lock-down

**Goal:** lock the `pd_kind` taxonomy, schema versions, and per-class JSON shapes in a design doc before any code lands.

**What:**

- Land `context/designs/catalog/universality-pivot-schemas.md` documenting all `.pd*` schemas (one section per kind). Reference 2026-04-30 audit's sketch as starting point; add `pd_kind` discriminator + `pd_schema_version: 1` envelope; resolve open questions (`.pdscenario` split vs combine; mesh compound layout; cross-reference convention in JSON).
- Lock the directory layout (Hypothesis A: per-romid tier).
- Lock the parser strategy (Strategy A: per-class parser).

**Risk assessment:**

- Bounded. Doc-only. No code, no merge conflicts.
- Cross-cuts: must agree with [audits/rom-extraction-audit-2026-04-30.md](rom-extraction-audit-2026-04-30.md) Section 3.2 to avoid drift.

**Test strategy:** N/A (doc-only).

**Rollback:** delete the design doc; no committed code.

**Deliverable:** the schema doc, signed off by Mike.

---

### Step 1: extractor C-port for one asset class (weapons proving ground)

**Goal:** prove the runtime-extract-to-`.pdwpn`-files pattern with weapons. The 86 weapons are the heaviest class (most curation, most complex schema). Proving with weapons de-risks all subsequent classes.

**What:**

- Add `port/src/romextract_curation_weapons.c` (new file): static `const struct weapon_curation_t s_BaseWeapons[86]` derived from [base/weapons.pdbase](../../base/weapons.pdbase). One-time conversion via a new throwaway Python script `devtools/pdbase_to_c_curation.py` that emits a `#include`-able C file. The curation tables hold every project-authored field (catalog ID, weapon_id, symbolic enum names for refs, gunviscmds, partvis, bot_pref, flags, etc.).
- Add `port/src/romextract_pdwpn.c` (new file): `romExtractPdwpnEmit(weapon_index, out_dir)` writes one `.pdwpn` JSON file at `out_dir/weapons/<id>.pdwpn`. Combines `s_BaseWeapons[idx]` with the ROM bytes (pulled via `romdataFileGet*` for the model files referenced by `hi_model` / `lo_model`).
- Add `port/src/romextract_pdmesh.c` (new file): `romExtractPdmeshEmit(filenum, out_dir)` writes one `.pdmesh` compound at `out_dir/meshes/<id>.pdmesh`. Compound is a ZIP archive built via existing `port/src/modarchive.c` (or its writer subset).
- Add `port/src/romextract_pdanim.c` (new file): `romExtractPdanimEmit(anim_index, out_dir)` writes one `.pdanim` JSON file.
- Wire `romExtractAllPdwpn()` into the boot sequence at [main.c:296](../../port/src/main.c:296) (after `romExtractAllFiles` + `Verify`, BEFORE `romdataReleaseRom`). Idempotent: skip if `data/<romid>/weapons/<id>.pdwpn` exists with valid sidecar.
- DO NOT change the loader yet. The new `.pdwpn` files coexist with `base/weapons.pdbase`; the loader keeps reading `.pdbase` until Step 4.

**Risk assessment:**

- Moderate. New code; no callsite changes to consumers.
- Cross-cuts: shares `port/src/modarchive.c` for ZIP writing (read-only against existing modarchive consumers).
- Bounded scope: weapons only. Each new `.pdwpn` produced is byte-comparable against the matching row of `weapons.pdbase` JSON for parity validation.

**Test strategy:**

- New test: `tests/test_pdwpn_emitter.cpp` with cases for: emit one weapon, parse it back through a small JSON reader, assert all fields match the curation table.
- Manual verification: extract on a clean `data/`, diff each generated `.pdwpn` JSON against the matching row in `base/weapons.pdbase` (semantic diff via the round-trip helper at [loader_pdbase.h:97](../../port/include/loader_pdbase.h:97) `loaderPdbaseEncodeOpcode`).
- Build verify per [procedures.md](../procedures.md): `.\devtools\build-session.ps1 -Session pivot1 -Target all`.
- LOUDFAIL channel: `LOUDFAIL.EXTRACT.PDWPN.<reason>` for emission failures.

**Rollback:** revert the four new C files + the boot wiring line. No load-path consumer change to revert.

**Deliverable:** `data/<romid>/weapons/base_*.pdwpn` (86 files) + `data/<romid>/meshes/base_*.pdmesh` (~172 files for hi/lo variants) + `data/<romid>/animations/base_invanim_*.pdanim` (110 files) all generated cleanly on first launch.

---

### Step 2: heads + bodies + arenas extractors

**Goal:** extend the runtime extractor to cover the remaining metadata-bearing classes.

**What:**

- `port/src/romextract_curation_heads.c` (84 records).
- `port/src/romextract_curation_bodies.c` (68 records).
- `port/src/romextract_curation_arenas.c` (47 records).
- `port/src/romextract_pdhead.c`, `_pdbody.c`, `_pdarena.c` emitters.
- Boot wiring: `romExtractAllPdhead()`, `romExtractAllPdbody()`, `romExtractAllPdarena()` at [main.c:296](../../port/src/main.c:296) area.
- Each emits to `data/<romid>/heads/`, `data/<romid>/bodies/`, `data/<romid>/arenas/`.

**Risk assessment:**

- Lower than Step 1. Same pattern, smaller surface per class.
- Cross-cuts: shares the JSON writer subset from Step 1.

**Test strategy:** parallel to Step 1. Per-class emitter test pinned in `tests/test_pdhead_emitter.cpp`, etc.

**Rollback:** revert the new files + boot wiring; loader untouched.

**Deliverable:** 84 + 68 + 47 = 199 new `.pd*` files generated on first launch.

---

### Step 3a: character animations (REQUIRED per Q-3)

**Goal:** break the monolithic character animation lump (`data/<romid>/segs/animations.bin`) into per-animation `.pdanim` files. This is required within catalog scope per Mike's 2026-05-02 directive ("Catalog is not complete unless it is COMPLETE. IT IS FOUNDATIONAL TO EVERYTHING.").

**What:**

- Walk the character animation index (today consumed via `preprocessAnimations` at [port/src/preprocess/](../../port/src/preprocess/)) and identify each animation's symbolic name (`ANIM_*` constants).
- `port/src/romextract_pdanim_chr.c` (sibling to `_pdanim.c` from Step 1 which handles weapon anims): for each character animation, emit `data/<romid>/animations/base_<anim_name>.pdanim`.
- Curation table `port/src/romextract_curation_chr_anims.c`: per-anim metadata (frame rate, loop flag, transition rules) sourced from existing animation index.
- Loader extension: the existing `.pdanim` parser from Step 1 must accept a `category` field distinguishing `weapon_animation` from `character_animation` so the right pool slot fills.
- Boot wiring: `romExtractAllPdanimChr()` after `romExtractAllPdanim()`.

**Risk assessment:**

- Moderate. Character animations are large (the segment file is multi-MB) and the index is dense. Possibility: per-anim splitting reveals seams where animations share underlying data.
- Cross-cuts: model code that loads animations at runtime (chraction.c, body.c, etc.) reads through filenum; provided the catalog router stays the same, consumers should not notice.

**Test strategy:**

- Per-anim emitter test: extract one character animation, parse it back, assert byte-equivalent reconstruction.
- Manual smoke: spawn a character, observe idle / run / death animations play correctly.
- LOUDFAIL: `LOUDFAIL.EXTRACT.PDANIM_CHR.<reason>`.

**Rollback:** revert the new files + boot wiring; character animations continue loading from the segment file.

**Deliverable:** every character animation is a per-asset `.pdanim` file. The segment file `data/<romid>/segs/animations.bin` may stay alongside as bytes-of-record (the underlying frame data is still ROM-derived); the per-anim file is the catalog entry point.

---

### Step 3: byte-payload classes (audio, scenarios, ui, fonts, lang)

**Goal:** extend extraction to the classes that today flow through `data/<romid>/files/*.bin` + `data/<romid>/segs/*.bin` directly without going through `.pdbase`. These are the ROM-1:1 byte payloads listed in Section 1.7.

**What:**

- `port/src/romextract_pdsfx.c`: split `data/<romid>/segs/sfxctl.bin` + `sfxtbl.bin` into per-effect `.pdsfx` compounds.
- `port/src/romextract_pdsong.c`: split `seqctl.bin` + `seqtbl.bin` + `sequences/*` into per-track `.pdsong` compounds.
- `port/src/romextract_pdvoice.c`: identify voice samples within the SFX bank by symbolic actor + index; emit per-line `.pdvoice` compounds.
- `port/src/romextract_pdscenario.c`: walk `g_Stages[]`, for each stage emit a `.pdscenario` JSON referencing sibling `.pdseg` (geometry), `.pdtiles`, `.pdlang` etc. by catalog ID.
- `port/src/romextract_pdseg.c`: for each `bg_*.seg` ROM file, emit `data/<romid>/scenarios/<stage>/geometry.pdseg` (compound: bytes + manifest).
- `port/src/romextract_pdtiles.c`: parallel for tile data.
- `port/src/romextract_pdlang.c`: parallel for lang strings.
- `port/src/romextract_pdfont.c`: per-face font extraction.
- `port/src/romextract_pdui.c`: replaces `pdguiThemeExtractRomTextures` ([pdgui_theme.cpp:2424](../../port/fast3d/pdgui_theme.cpp:2424)) with a `.pdui` ZIP compound emit. Today's loose `data/ui/textures/*.tga` becomes `data/<romid>/ui/base_pd_original.pdui`.

**Risk assessment:**

- Moderate per class. Each requires understanding the legacy preprocess function that decoded the segment ([port/src/preprocess/segaudio.c](../../port/src/preprocess/segaudio.c), `preprocessSetupFile`, `preprocessLangFile`, etc.).
- Highest risk class: `.pdscenario`. Stages cross-reference geometry, tiles, setup, mpsetup, AI scripts, lang banks. Splitting cleanly without semantic drift requires careful schema work.
- Cross-cuts: `.pdui` retires the existing `pdguiThemeExtractRomTextures` path, which is a loose-files writer. Risk: theme system reads loose files; if `.pdui` ships before the theme reader migrates, theme breaks.

**Test strategy:**

- Per-class emitter test in `tests/test_pd<class>_emitter.cpp`.
- Manual UI smoke: extract `.pdui`, then verify theme system loads textures equivalently to the loose-files path.
- LOUDFAIL channels per class.

**Rollback:** per-class. Each emitter is independent; can ship them in any order or skip individual classes if blocked.

**Deliverable:** `data/<romid>/audio/sfx/`, `audio/music/`, `audio/voice/`, `scenarios/`, `ui/`, `fonts/`, `lang/` all populated with per-asset `.pd*` files.

---

### Step 4: loader directory walker (the universality switch)

**Goal:** flip the loader from "read 4 fixed `.pdbase` archives" to "walk per-class directories under `data/<romid>/` and `mods/`."

**What:**

- Replace [loaderPdbaseScan()](../../port/src/loader_pdbase.c:1869) body with a directory walker that:
  - Walks `data/<romid>/weapons/*.pdwpn`, parses each, populates the weapons pool.
  - Walks `data/<romid>/heads/*.pdhead`, populates heads pool.
  - Walks `data/<romid>/bodies/*.pdbody`, populates bodies pool.
  - Walks `data/<romid>/arenas/*.pdarena`, populates arenas pool.
  - Walks `data/<romid>/meshes/*.pdmesh`, registers ASSET_MODEL rows.
  - Walks `data/<romid>/animations/*.pdanim`, populates animations pool.
  - Walks `mods/<modname>/<class>/*.pd<ext>` for each enabled mod, identical dispatch.
- Rename the function to `catalogUniversalScan(out)` to reflect that it no longer reads `.pdbase`.
- Keep the four pool layouts (`s_Weapons[86]`, `s_HeadsPool`, `s_BodiesPool`, `s_ArenasPool`) and the parser pipeline (just route per-`pd_kind` instead of per-archive-key).
- Boot wiring: rename `loaderPdbaseScan("base", ...)` to `catalogUniversalScan(...)` at [main.c:414](../../port/src/main.c:414).
- Catalog row registration moves AFTER the universal scan: today `assetCatalogRegisterBaseGame` runs before `loaderPdbaseScan`; under the new model, the catalog row layer is built FROM the loaded pools, not in a separate pass. This is a meaningful order shift; coordinate with [main.c:357-388](../../port/src/main.c:357).

**Risk assessment:**

- High. Touches the boot order. Touches the catalog row registration.
- Cross-cuts: every consumer of `catalogManagerGet*` must continue to work. The manager pools fill the same way; only the source path changes (filesystem walk instead of archive read).
- Boot order shift could break `assetCatalogRegisterStageSceneFiles` ([main.c:379](../../port/src/main.c:379)) if it depends on `g_Stages` populated before the universal scan.

**Test strategy:**

- Build verify per [procedures.md](../procedures.md).
- All existing manager-API tests (`tests/test_catalog_mgr_weapons_api.cpp`, `_heads_api.cpp`, `_bodies_api.cpp`, `_arenas_api.cpp`) must continue to pass.
- New test: `tests/test_universal_loader.cpp` exercising the directory-walk pipeline end-to-end with synthetic fixture files.
- Manual smoke: full game playtest. Spawn each weapon, verify head + body + arena pickers populate, verify SP missions load.
- LOUDFAIL: `LOUDFAIL.LOAD.UNIVERSAL.<reason>` for parse failures.

**Rollback:** revert the loader change + boot wiring. The Step 1 to Step 3 emitters keep producing `.pd*` files in parallel without harm.

**Deliverable:** runtime reads `data/<romid>/<class>/*.pd<ext>` exclusively. `base/*.pdbase` is unread, unreferenced.

---

### Step 5: retire `base/*.pdbase` archives

**Goal:** delete the four `.pdbase` archives from the repo. Symbolic completion.

**What:**

- Delete `base/weapons.pdbase`, `base/heads.pdbase`, `base/bodies.pdbase`, `base/arenas.pdbase`.
- Delete the four `devtools/extract_*_pdbase.py` scripts (no longer needed; curation tables are the new source).
- Update CMake to stop copying `base/` into the release directory (or to copy a different subset).
- Update [pillars/catalog.md](../pillars/catalog.md) "Where to look" + "What is done" sections.
- Update [audits/rom-extraction-audit-2026-04-30.md](rom-extraction-audit-2026-04-30.md) to mark the .pdbase paragraphs as obsolete.

**Risk assessment:**

- Low (the .pdbase files are unread after Step 4) but visible (release shape changes; users who downloaded a previous release with `base/*.pdbase` will see the next release omit them).
- Cross-cuts: the release pipeline at [reference_release_assets.md](../../C--Users-mikeh-Perfect-Dark-2-perfect-dark-mike/memory/reference_release_assets.md) currently lists `base/` content (with `.pdbase`) as a release asset. Update accordingly.

**Test strategy:**

- Build verify; no `.pdbase` referenced in source.
- Grep guard test: `tests/test_no_pdbase_in_repo.cpp` asserting zero `.pdbase` files exist under `base/` and zero references in source.

**Rollback:** restore from git; no functional impact since runtime ignores them.

**Deliverable:** repo has zero `.pdbase` files. Curation lives in `port/src/romextract_curation_*.c`. Extraction lives in `port/src/romextract_pd*.c`. Loading lives in `port/src/loader_universal.c` (formerly `loader_pdbase.c`).

---

### Step 6: mod system uplift

**Goal:** extend the mod loader to consume `.pd*` files inside `.pdmod` archives.

**What:**

- [port/src/assetcatalog_scanner.c](../../port/src/assetcatalog_scanner.c) walks INI components today. Add a parallel walker that finds `<modname>/weapons/*.pdwpn`, etc., and dispatches to the universal `.pd*` parser.
- For archive mods (`.pdmod` ZIP), extend [modarchive.c](../../port/src/modarchive.c) iteration to surface entries by `.pd<ext>` extension and route to the universal parser.
- Preserve INI components for backwards compatibility (per [pillars/modding.md](../pillars/modding.md) "What is in flight" lists INI delivery from archive as a known gap; this step happens to address it).

**Risk assessment:**

- Moderate. Mod system is significant code (~3,000 lines across modmgr.c + modarchive.c + modvfs.c).
- Cross-cuts: network distribution (`.pdmod` shipped over wire) must continue to work.

**Test strategy:**

- Author one test mod with a `.pdwpn` inside, verify it registers and overrides UI.
- Verify network distribution still works (`SVC_DISTRIB_*` flow per [pillars/connectivity.md](../pillars/connectivity.md)).

**Rollback:** revert the new walker. Mods continue with INI-only.

**Deliverable:** modders can author with `.pd*` files; the mod system handles them identically to base content.

---

### Step 7: in-client mod authoring round-trip

**Goal:** in-client mod tools can copy a `data/<romid>/<class>/<id>.pd<ext>` file as a starter template into a new mod folder. Per [audits/rom-extraction-audit-2026-04-30.md](rom-extraction-audit-2026-04-30.md) M-3, M-8.

**What:**

- Extend [port/fast3d/pdgui_menu_moddinghub.cpp](../../port/fast3d/pdgui_menu_moddinghub.cpp) tab "Mod-from-extract" or similar: presents a list of base content; clicking copies + renames into the user's mod folder; opens the new file in the relevant editor.
- Out of scope for the core pivot but flagged as the natural follow-up.

**Risk assessment:** out of scope for the core lane; track separately.

**Deliverable:** N/A this lane.

---

## 4. Backwards compatibility / transition strategy

### 4.1 Cohabitation during the migration

Steps 1 to 4 produce `.pd*` files alongside the existing `.pdbase` files. The runtime loader still reads `.pdbase` until Step 4 flips. Mike's local install can run any intermediate state without breaking, because the new emitters write to new paths (`data/<romid>/weapons/`, etc.) and do not touch `base/*.pdbase`.

This means a partial implementation (Steps 1, 2, 3 shipped but Step 4 not) is a fully working build. The user's `data/` accumulates the new `.pd*` files but the runtime ignores them. Useful for staged rollout: ship Step 1 to 3 first, validate the on-disk output by hand, ship Step 4 in a separate session.

### 4.2 Save data + network protocol

No save-format change. Catalog IDs at all interface boundaries (per [constraints.md](../constraints.md) "Catalog ID strings at all interface boundaries") stay the same; the source of truth for those IDs shifts from `base/*.pdbase` archives to `data/<romid>/*.pd*` directory walks but the IDs themselves do not change.

No wire-protocol change. The catalog ID strings traversing the wire (`SVC_CATALOG_INFO`, `CLC_LOBBY_START`, etc., per [pillars/save-wire-format.md](../pillars/save-wire-format.md) NET_PROTOCOL_VER=46) reference the same catalog IDs.

The match manifest (`MANIFEST_TYPE_WEAPON`, `MANIFEST_TYPE_BODY`, `MANIFEST_TYPE_HEAD`, etc., per [pillars/modding.md](../pillars/modding.md)) continues to enumerate catalog entries by ID. The fact that those entries are sourced from `.pdwpn` instead of `weapons.pdbase` is invisible at the wire layer.

### 4.3 Mod compatibility

Existing INI-based mods continue to work after Step 6 lands (the INI scanner stays alongside the `.pd*` walker). Mods authored with `.pd*` files require Step 6 or later. Folder mods (per [pillars/modding.md](../pillars/modding.md) `mods/<modname>/`) continue to work; archive mods (`.pdmod`) continue to work. The `.pdmod` distribution path (network sync, SHA-256 digest at SVC_DISTRIB_BEGIN, etc.) is unchanged.

### 4.4 Clean cut vs gradual

Mike's directive ("dynamically and validated, populated at runtime each time the game boots") is compatible with both a clean cut (Step 4 lands all asset classes at once) and a gradual cut (Step 4 lands per-class with the loader handling mixed source for a transition window).

Recommendation: clean cut at Step 4 for the four `.pdbase`-resident classes (weapons, heads, bodies, arenas). Reasoning: the loader pools (`s_Weapons[86]`, `s_HeadsPool`, etc.) are populated at boot in a single pass; making the loader pull SOME slots from `.pdwpn` and OTHER slots from `weapons.pdbase` adds complexity for no shipping value. Either the .pdbase loads or the per-asset files load; pick one.

For Step 3 byte-payload classes (audio, scenarios, ui, fonts, lang), gradual is fine. Each class has independent storage today (separate `.bin` files); switching one class to `.pd*` while another stays as `.bin` is structurally clean.

### 4.5 Rollback per step

Each migration step is reversible by reverting its commit. Step 1 to 3 add code without removing it; reverting them removes the new emitters but keeps the runtime path working. Step 4 swaps the loader; reverting it returns to .pdbase reading. Step 5 deletes `.pdbase`; reverting it restores them from git.

The riskiest reversal is Step 4 (loader swap). Mitigation: keep the .pdbase reading code as an unused-but-present alternative path until Step 5 ships, gated by a build flag or env var (`PD_USE_LEGACY_PDBASE_LOADER=1`). Removed at Step 5.

### 4.6 Migration order rationale

The numbered sequence is ordered to:

1. Prove the pattern on the heaviest class first (Step 1 weapons), because if weapons works the rest is mechanical.
2. Lighter metadata classes follow (Step 2: heads + bodies + arenas).
3. Character animations (Step 3a) prove the `.pdanim` parser works for both weapon and character contexts. Required per Q-3 within catalog scope.
4. Byte-payload classes (Step 3) follow because they touch different code (preprocess functions) and shouldn't block the metadata work.
5. Loader switch (Step 4) gates the universality realization. Comes after all emitters exist so the on-disk content is complete.
6. Cleanup (Step 5) once Step 4 has stuck for at least one playtest cycle.
7. Mod uplift (Step 6) is independent of the base content migration; can run in parallel with Steps 4-5 if a parallel session is available.
8. Mod authoring tools (Step 7) is the polish lane.

---

## 5. Risk assessment

### 5.1 What could break

**Boot order regressions (Step 4 high risk).** [main.c:357-445](../../port/src/main.c:357) has eight calls in a specific order: stageTableInit, assetCatalogInit, assetCatalogRegisterBaseGame, assetCatalogRegisterStageSceneFiles, assetCatalogScanComponents, catalogManagerHeadInit, catalogManagerBodyInit, catalogManagerArenaInit, then the loaderPdbaseScan block. The universal scan must slot into this sequence cleanly. Possibility: the catalog row layer expects to be populated before the manager pools (parity-period assumption). Today this works because `assetCatalogRegisterBaseGame()` reads in-binary tables (`g_HeadsAndBodies`, `g_MpArenas`) that exist independently of `.pdbase`. Under the universal model, if base in-binary tables are retired (e.g. `g_HeadsAndBodies[]` deleted), the catalog row registration must happen FROM the loaded pools, which inverts the order.

Mitigation: keep `g_HeadsAndBodies` etc. as compatibility shims through the migration; only retire after the order shift is validated. Or: prove the order shift on heads alone (smallest legacy table) before extending to bodies + arenas.

**Schema drift between extractor and consumer (Step 1 high risk).** A `.pdwpn` produced by the extractor must round-trip cleanly through the loader and produce identical pool entries to what `loaderPdbaseScan` produces today from `weapons.pdbase`. Possibility: a field is dropped, mistyped, or normalized differently (float precision, integer width, enum-string vs enum-integer choice). Caught only by playtest if not by parity test.

Mitigation: Step 1 ships with a parity test (`tests/test_pdwpn_parity.cpp`) that loads `weapons.pdbase` AND `data/<romid>/weapons/*.pdwpn`, populates two parallel pools, and asserts every field matches. F12 of the original weapons gate ran this exact pattern via `loaderPdbaseRunParityCheck()` (retired at F13); reintroduce a temporary parity check during the pivot's parity period.

**Mod compatibility regressions (Step 6 moderate risk).** The mod system has 3,000+ lines and active features: network distribution, VFS LRU cache, atomic write, SHA-256 integrity, property handler DLL, INI scanner, JSON content path, theme bundle. Any one of these could miss a corner case under the new `.pd*` walker.

Mitigation: Step 6 is independent of Steps 1-5; can defer until the base content pivot is fully proven, then ship with extensive smoke testing.

**Network manifest surface (low risk).** [pillars/modding.md](../pillars/modding.md) network manifest lists MANIFEST_TYPE_BODY, _HEAD, _STAGE, _WEAPON, _COMPONENT, _MODEL, _ANIM, _TEXTURE, _LANG, _AUDIO. Each maps to catalog rows by ID. After the pivot, these still resolve through `assetCatalogResolve(id)`, which still returns rows; the rows just got their data from a `.pdwpn` instead of a `weapons.pdbase` parse. Possibility: net-distrib expects file bytes for some classes (e.g. character mesh distribution); if the storage shape shifts (loose `data/<romid>/files/<name>.bin` becomes a compound `data/<romid>/meshes/<id>.pdmesh`), the distribution code may not find what it expects.

Mitigation: audit `port/src/net/netdistrib.c` (1,579 lines) for path assumptions before Step 4. Specifically the `manifestCheck` SHA-256 compare path at [netmanifest.c:2085](../../port/src/net/netmanifest.c:2085).

**Build pipeline regressions (low risk).** CMake `file(GLOB_RECURSE)` for `port/src/*.c` auto-discovers new files (per [CLAUDE.md](../../CLAUDE.md)). Adding `port/src/romextract_curation_*.c` etc. is automatic. But the release pipeline at `devtools/release.ps1` may copy `base/*.pdbase` explicitly; that path needs an audit at Step 5.

Mitigation: grep release scripts for `pdbase` references before Step 5.

**Test rot (low risk).** Existing tests assume `loaderPdbaseScan` reads four named archives. Tests like `tests/test_loader_pdbase_scan.cpp` (per [pillars/catalog.md](../pillars/catalog.md) Known Gaps "F10 tests are static-text shape only") may break in informative or uninformative ways.

Mitigation: update test names + assertions as part of Step 4. Treat existing test changes as part of the same commit.

### 5.2 Bounded vs unbounded scope

**Bounded:**

- Step 0 (schema doc): bounded. Doc-only.
- Step 1 (weapons emitter): bounded. ~1,500 lines of new C across 3-4 files. Curation table is mechanical conversion of existing JSON.
- Step 2 (heads + bodies + arenas emitters): bounded. ~1,500 lines parallel to Step 1.
- Step 4 (loader swap): bounded. ~500 lines of loader rewrite, mostly directory iteration.
- Step 5 (cleanup): bounded. Deletions + doc edits.

**Unbounded if not fenced:**

- Step 3 (byte-payload classes). Specifically `.pdscenario`. Stages have many cross-references (geometry, tiles, setup, mpsetup, AI scripts, lang banks, music, ambient SFX). The schema decomposition is non-trivial. Possibility: scope balloons into a multi-session lane on its own.
  
  Fence: ship one stage class at a time (`.pdseg` first, `.pdtiles` second, `.pdscenario` last). Or, defer `.pdscenario` to a follow-up lane and migrate the simpler stage sub-classes first.

- Step 6 (mod system uplift). Possibility: the mod system has hidden coupling (theme bundle, audio mod, font mod) that resists clean uplift.
  
  Fence: explicitly defer to a follow-up lane; the universality of base content is the priority, modding-side parity follows.

### 5.3 Rollback summaries

| Step | Reversibility | Cost of revert |
|---|---|---|
| 0 | Trivial: delete doc | Zero functional impact |
| 1 | Easy: revert new files + boot wiring | Zero (unused output) |
| 2 | Easy: revert | Zero (unused output) |
| 3 | Easy per class: revert that class's emitter | Zero per class |
| 4 | Moderate: revert loader change + boot wiring; legacy pdbase reading restored | Step 1-3 emitters keep working; consumers still go through manager API |
| 5 | Easy: restore `.pdbase` from git | Zero functional impact (runtime ignores them post-Step 4) |
| 6 | Easy: revert walker | Mods authored with `.pd*` files become unloadable until re-revert |
| 7 | Easy: revert tool extension | No impact on runtime |

### 5.4 Open questions, RESOLVED 2026-05-02

These were the open questions surfaced for Mike's call. All five were answered and folded into the Step 0 schema lock-down doc at [designs/catalog/universality-pivot-schemas.md](../designs/catalog/universality-pivot-schemas.md).

- **Q-1: schema for `.pdscenario` (RESOLVED: unified).** A stage is a single ZIP archive holding all five components (geometry, walkable surfaces / tiles, mission setup, MP setup, tile data) plus a manifest. Modders load one file; in-client UI handles the loading smoothly. Mike's call: "Modding will mostly occur in-client, and so users will load one and it will load it smoothly."
- **Q-2: `pd_kind` granularity for audio (RESOLVED: separate kinds, type-tolerant references).** Voice gets its own `.pdvoice` kind distinct from `.pdsfx`. PLUS: any audio-consumption site in the engine accepts either kind at the reference. A modder making "weapon fire sound = a voice line" works because the weapon's `fire_sound` field accepts a `.pdvoice` catalog ID just as readily as a `.pdsfx` one. Document this as an architectural invariant in the schema doc: audio refs are type-tolerant.
- **Q-3: character animations (RESOLVED: REQUIRED within catalog scope).** Mike verbatim: "DO NOT DEFER beyond the scope of the catalog work. Catalog is not complete unless it is COMPLETE. IT IS FOUNDATIONAL TO EVERYTHING." Weapon anims still go first in Step 1 to prove the pattern; character animations follow as a required Step 3a sub-step before catalog universality is declared done.
- **Q-4: catalog ID for unnamed ROM slots (RESOLVED: investigate first, then drop or name).** Three buckets:
  1. Zero references found by grep audit. Bucket as junk; do not extract.
  2. Has references but purpose obscure. Investigate the consumer code, infer the asset class from how it's loaded, name appropriately (e.g. `base:tex_unknown_004f` if loaded as a texture).
  3. Has references with clear purpose. Name properly (`base:tex_carrington_logo` or similar).
  
  Each unnamed slot's disposition is documented in an appendix produced as part of Step 1 emitter work.
- **Q-5: parity period duration (RESOLVED: parity only, retired with `.pdbase` deletion).** Same shape as the F12 to F13 weapons migration: parity check runs while both formats coexist; gets removed at the step where `.pdbase` is deleted from the repo (Step 5). Mike validates after each migration step before the parity check retires.

---

## 6. Estimate

### 6.1 Per-step session count (rough)

Each session is approximately one focused work block (3-6 hours of effort, one design + implementation + test + verify cycle).

| Step | Session count (rough) | Critical path | Notes |
|---|---|---|---|
| 0. Schema lock-down | 1 | Yes | Doc + sign-off |
| 1. Weapons emitter | 2-3 | Yes | Heaviest class; one session for curation table generation, one for emitter, possibly one for parity test |
| 2. Heads + bodies + arenas emitters | 2 | Yes | Lighter; pattern from Step 1 |
| 3a. Character animations (Q-3 required) | 1-2 | Yes | Reuses `.pdanim` parser from Step 1. Adds chr-anim curation table + per-anim emitter. Catalog completeness gate. |
| 3. Byte-payload extractors | 4-6 | Partial | Each class is a separate sub-step. SFX + music + voice in one. Scenarios + tiles + segments in another (unified `.pdscenario` ZIP per Q-1). UI + fonts + lang in another. |
| 4. Loader directory walker | 2 | Yes | Loader rewrite + boot order shift + parity test |
| 5. Retire `.pdbase` archives | 1 | No | Cleanup + doc updates. Parity check retires here per Q-5. |
| 6. Mod system uplift | 2-3 | No | Independent lane |
| 7. In-client mod authoring | 3+ | No | Polish lane; out of scope for the core pivot |

**Critical path total: 9-14 sessions** (Mike's Q-3 expansion adds 1-2 sessions for character anims to the prior 8-12). Steps 0, 1, 2, 3a, 4, 5 in sequence. Step 3 byte-payload classes can run in parallel with Step 4 if a parallel session is available (different files + functions); recommendation is sequential to avoid cross-cuts.

**Full-pivot total (including Steps 6-7): 14-20 sessions.**

**Catalog completeness gate (per Q-3):** the catalog is not declared complete until Steps 0 through 5 ship. Character anims (Step 3a) is REQUIRED, not deferrable. Byte-payload classes (Step 3) are required for `.pdscenario` / `.pdsfx` / `.pdvoice` / `.pdsong` because those classes have catalog rows that must populate. Mod system uplift (Step 6) is universality-relevant but is outside catalog completeness because mods extend an already-complete catalog.

### 6.2 Critical path dependencies

```
Step 0 (schema lock-down)
   |
   v
Step 1 (weapons emitter)
   |
   v
Step 2 (heads+bodies+arenas emitters)
   |
   v
Step 3a (character animations, REQUIRED per Q-3)
   |
   +---> Step 3 byte-payloads (audio / scenarios / ui / fonts / lang)
   |        (can run in parallel with Step 4 if scope allows)
   v
Step 4 (loader directory walker; the universality switch)
   |
   v
Step 5 (retire .pdbase + parity check)
   |
   v
[CATALOG UNIVERSALITY COMPLETE]
   |
   v
Step 6 (mod system uplift; independent lane)
   |
   v
Step 7 (in-client mod authoring; polish lane)
```

Step 4 is the lock-in. After Step 4 ships, `data/<romid>/<class>/*.pd<ext>` is the runtime source of truth. Before Step 4, the new emitters produce output that the runtime ignores.

### 6.3 Recommendation (per Mike's catalog completeness directive)

Mike's Q-3 directive: catalog is not complete unless it is COMPLETE. The "ship metadata only and defer byte-payloads" path the prior version of this section recommended is OBSOLETE. Steps 0 through 5 (including 3a) are the catalog completeness lane and run as a single sequenced campaign. Steps 6 and 7 are the modding-side complement and follow.

Sequencing strategy:

1. Step 0 schema lock-down (this session is producing the schema doc).
2. Steps 1 and 2 prove the metadata pipeline (weapons, heads, bodies, arenas).
3. Step 3a extends the `.pdanim` parser to character animations (catalog completeness).
4. Step 3 brings the byte-payload classes (audio, scenarios, ui, fonts, lang) into the universal model. Unified `.pdscenario` ZIP per Q-1; type-tolerant audio refs per Q-2.
5. Step 4 flips the loader; parity check runs alongside per Q-5.
6. Step 5 retires `.pdbase` and the parity check after Mike's playtest validates the new path.
7. Steps 6 and 7 follow as the modding-side track.

### 6.4 Comparison to prior catalog work

Per [tasks.md](../tasks.md):

- **F1-F13 weapons gate (S484 + S591):** 13 substeps over the same number of sessions plus a few iterations. Single asset class, parity-period bridge through F12, retire at F13.
- **Heads gate (S596):** F1-F13 in roughly 1-2 sessions because the pattern was proven on weapons.
- **Bodies + Arenas (S598 + S599):** further accelerated; 1 session each.
- **Phase 3 Pass A through D (Pass C: `b15cc701`, Pass D: S604):** 4 passes over ~5-7 sessions; included extraction + verify + ROM release + self-heal.

The pivot is roughly comparable to "do the heads + bodies + arenas pattern again, but with extraction-side curation tables instead of a one-shot Python pass." The per-class effort drops with experience; first class is heaviest. Estimate 8-12 critical-path sessions reflects expected per-class velocity given prior gate cadence.

### 6.5 Build verify cadence

Per [procedures.md](../procedures.md): every step that touches code must `.\devtools\build-session.ps1 -Session <id> -Target all` clean before merging. Session IDs by step:

- Step 1: `pivot-wpn`
- Step 2: `pivot-meta`
- Step 3a: `pivot-anim-chr`
- Step 3 sub-steps: `pivot-audio`, `pivot-stage`, `pivot-ui`, etc.
- Step 4: `pivot-load`
- Step 5: `pivot-clean`
- Step 6: `pivot-mod`

---

## 7. Where to look

For this audit's claims:

- `.pdbase` archives + content: [base/weapons.pdbase](../../base/weapons.pdbase), [base/heads.pdbase](../../base/heads.pdbase), [base/bodies.pdbase](../../base/bodies.pdbase), [base/arenas.pdbase](../../base/arenas.pdbase).
- Loader: [port/src/loader_pdbase.c](../../port/src/loader_pdbase.c), [port/include/loader_pdbase.h](../../port/include/loader_pdbase.h), [port/src/loader_pdbase_enums.c](../../port/src/loader_pdbase_enums.c).
- Catalog managers: [port/src/catalog_mgr_weapons.c](../../port/src/catalog_mgr_weapons.c), [port/src/catalog_mgr_heads.c](../../port/src/catalog_mgr_heads.c), [port/src/catalog_mgr_bodies.c](../../port/src/catalog_mgr_bodies.c), [port/src/catalog_mgr_arenas.c](../../port/src/catalog_mgr_arenas.c).
- Build-time extractors: [devtools/extract_weapons_pdbase.py](../../devtools/extract_weapons_pdbase.py), [devtools/extract_heads_pdbase.py](../../devtools/extract_heads_pdbase.py), [devtools/extract_bodies_pdbase.py](../../devtools/extract_bodies_pdbase.py), [devtools/extract_arenas_pdbase.py](../../devtools/extract_arenas_pdbase.py).
- Runtime ROM extractor (Pass A through D): [port/src/romextract.c](../../port/src/romextract.c), [port/include/romextract.h](../../port/include/romextract.h).
- Boot wiring: [port/src/main.c:280-445](../../port/src/main.c:280).
- Catalog identity API: [port/include/assetcatalog.h](../../port/include/assetcatalog.h), [port/src/assetcatalog.c](../../port/src/assetcatalog.c).
- Mod system: [port/src/modarchive.c](../../port/src/modarchive.c), [port/src/modmgr.c](../../port/src/modmgr.c), [port/src/modvfs.c](../../port/src/modvfs.c), [port/src/assetcatalog_scanner.c](../../port/src/assetcatalog_scanner.c).
- UI texture extractor: [port/fast3d/pdgui_theme.cpp:2424](../../port/fast3d/pdgui_theme.cpp:2424).

For the architectural backdrop:

- [pillars/catalog.md](../pillars/catalog.md) live state, what is done, known gaps.
- [audits/rom-extraction-audit-2026-04-30.md](rom-extraction-audit-2026-04-30.md) extension taxonomy, base-vs-mod symmetry, extrapolations referenced throughout.
- [audits/ui-asset-pipeline-investigation-2026-04-30.md](ui-asset-pipeline-investigation-2026-04-30.md) `.pdui` schema sketch.
- [designs/catalog/catalog-full-pipeline-weapons.md](../designs/catalog/catalog-full-pipeline-weapons.md) F1-F13 gate that produced the current `.pdbase` shape.
- [pillars/modding.md](../pillars/modding.md) `.pdmod` archive format + network distribution.
- [constraints.md](../constraints.md) catalog identity invariants (catalog ID strings at all interface boundaries, etc.).

For decisions that have been made before implementation begins:

- Q-1 through Q-5 RESOLVED 2026-05-02; see Section 5.4 above.
- Step 0 schema lock-down doc lands at [designs/catalog/universality-pivot-schemas.md](../designs/catalog/universality-pivot-schemas.md) and is the binding reference for all subsequent steps.

---

---

## Step 3 audio half SHIPPED 2026-05-03 (worktree `frosty-antonelli-fd537f`)

**What landed:** the audio side of Step 3. Three new emitters + three matching parity checks for the byte-payload audio classes:

- `port/src/romextract_pdsfx.c` -- shared SFX-bank walker. Iterates the post-preprocess `ALBankFile` via the disk-migrated `sfxctl` segment (instrument 0's `soundArray`), reads sample bytes from `sfxtbl`, emits one `.pdsfx` ZIP per leaf SFX index NOT classified as voice. Manifest carries envelope + `format` (`ALADPCM` / `PCM16`) + `sample_rate_hz` + `data_size` + loop info + `source_index` provenance.
- `port/src/romextract_pdvoice.c` -- thin wrapper around the same walker with `PDAUDIO_WALK_VOICE`. Emits `.pdvoice` ZIPs for leaf SFX indices that match the Slice 10 voice predicate (`g_AudioRussMappings` slot in `{1, 2, 3, 47, 48, 60, 62}`). Manifest adds `actor` / `transcript` / `language` / `context` placeholder fields per Section 2.8; curation lands in a follow-up.
- `port/src/romextract_pdsong.c` -- walks the byte-swapped `struct seqtable` at the head of the disk-migrated `sequences` segment, slices `binlen` (or `ziplen` if compressed) bytes from `entry.romaddr`, emits one `.pdsong` ZIP per slot. Manifest carries envelope + `format` (`ALSEQ` / `ALSEQ_ZIP`) + `binlen` + `ziplen` provenance.
- `port/src/romextract_parity_pdsfx.c` -- Q-5 structural parity for both `.pdsfx` and `.pdvoice` (mode-flag selector). Re-opens each emitted ZIP, parses `manifest.json`, asserts envelope + `id` + `source_index` + `data_size` + `sample_rate_hz` round-trip the source `ALSound`. Failures emit `LOADER.UNIVERSAL.PARITY_FAIL`.
- `port/src/romextract_parity_pdvoice.c` -- thin wrapper.
- `port/src/romextract_parity_pdsong.c` -- Q-5 structural parity for `.pdsong`. Re-opens each emitted ZIP, asserts `pd_kind="song"` + `id` + `source_index` + `binlen` + `ziplen` round-trip, plus `data.bin` size matches the source slice.

Internal glue:

- `port/src/romextract_pdaudio_internal.h` -- private header exposing `romextract_pdaudio_walkBank(mode, force_rewrite)` and `romextract_pdaudio_parityCheck(mode)`. Lets `romextract_pdsfx.c` and `romextract_pdvoice.c` share the bank walker so the byte-format interpretation is in a single place. Header lives under `port/src/` (not `port/include/`) because no out-of-tree consumer needs it.

Public API: `port/include/romextract_pd.h` gains six new prototypes (`romExtractAllPdsfx` / `Pdvoice` / `Pdsong` plus `romExtractParityCheckPdsfx` / `Pdvoice` / `Pdsong`) inside a Step 3 audio half block.

Boot wiring: `port/src/main.c` gets a Step 3 audio block immediately after Step 3a. Emit + parity calls follow the established pattern (idempotent on subsequent boots, return-value-discarded with `(void)cast`).

**Catalog ID convention** (per `feedback_human_readable_ids` + Q-4 buckets):

- `.pdsfx`: `base:sfx_<lowered_symbol>` when `loaderPdbaseNameForSfxEnum` returns a symbolic name (e.g. `base:sfx_launch_rocket` from `SFX_LAUNCH_ROCKET`). Falls back to `base:sfx_<NNNN>` 4-digit hex.
- `.pdvoice`: `base:voice_<NNNN>` always; symbolic SFX names map to non-actor labels so per-line actor curation lands later without ID churn.
- `.pdsong`: `base:song_<NNNN>` 4-digit hex; the 43 catalog-registered `MUSIC_*` tracks (slugs like `track_dark_combat`) map to seqtable slots via runtime indirection that curation will fold in as a Step 5 cleanup.

**Voice classification heuristic** (Slice 10 predicate, mirrored from `port/src/assetcatalog_base_extended.c::s_audioConfigIsVoice`):

A leaf SFX index `i` is voice if some entry in `g_AudioRussMappings[]` has `soundnum == i` AND `audioconfig_index` in `{AUDIOCONFIG_01, _02, _03, _47, _48, _60, _62}`. The walker builds a u8 bitset cache at start of walk so the per-sound check is O(1). The same predicate gates the `.pdvoice` walk so every leaf goes to exactly one emitter.

Per Q-2 type-tolerance, misclassification stays recoverable: the audio playback layer reads `pd_kind` at resolve time and routes to the right decoder. Voice retag at extract time is a hint, not a contract.

**Server build (PD_SERVER):** all six functions short-circuit to 0. The walker depends on `g_AudioRussMappings` from `snd.c` which isn't linked into `pd-server`; the russ table is reachable only client-side. Server registers all audio rows as SFX (per Slice 10) and the extractors mirror that contract.

**Counts emitted (NTSC final ROM, expected):**

- `.pdsfx`: ~1401 (1545 leaf SFX minus 144 voice-classified entries per Slice 10).
- `.pdvoice`: ~144 (the Slice 10 retag count).
- `.pdsong`: count varies by ROM (sequence table is dynamic; runtime walks `g_SeqTable->count`).

**Build verify:** clean four-target build via `devtools/build-session.ps1`. PASS for client (54.8-55.1 MB), updater (12.3 MB), server (22.4 MB), tests (24.7-24.9 MB). No new compile warnings on the six new files. Headless boot path unchanged (Step 3 audio block sits between Step 3a and `catalogBuildRuntimeCaches`).

**Files added** (7 new files, ~1100 lines):

- `port/src/romextract_pdaudio_internal.h` (~70 lines)
- `port/src/romextract_pdsfx.c` (~480 lines, includes the shared walker)
- `port/src/romextract_pdvoice.c` (~30 lines, wrapper)
- `port/src/romextract_pdsong.c` (~270 lines)
- `port/src/romextract_parity_pdsfx.c` (~360 lines, includes shared parity walker)
- `port/src/romextract_parity_pdvoice.c` (~20 lines, wrapper)
- `port/src/romextract_parity_pdsong.c` (~210 lines)

**Files modified:**

- `port/include/romextract_pd.h` -- 6 new prototypes + Step 3 audio block docblock (~95 new lines).
- `port/src/main.c` -- Step 3 audio block (~35 new lines).

**State after this commit:** 10 of 13 universality kinds emitted. Remaining: `.pdui` / `.pdfont` / `.pdlang` (Step 3b -- planned for follow-up worktree). `.pdscenario` already shipped in Step 2.

**Step 3b queue (next ship):**

- `port/src/romextract_pdui.c` -- replaces `pdguiThemeExtractRomTextures` ([port/fast3d/pdgui_theme.cpp:2424](../../port/fast3d/pdgui_theme.cpp:2424)) with a `.pdui` ZIP compound emit. Risk: theme system reads loose files today; `.pdui` ship coordinates with the theme reader migration.
- `port/src/romextract_pdfont.c` -- per-face font extraction from the 10 font segments (`bankgothic`, `zurich`, `tahoma`, `numeric`, `handelgothic*`, `ocra*`, `jpn*`).
- `port/src/romextract_pdlang.c` -- per-language string-table extraction from the lang segments.

**Sizing call for orchestrator:** Step 3b should ship as a separate coherent unit. The audio half shared the audio-decoder lineage (segaudio.c); the "other" half spans different lump shapes (ImGui texture pool for ui, font glyph metrics, language string tables). Recommend routing Step 3b to a fresh worktree.

---

## Step 3b part 1 SHIPPED 2026-05-03 (worktree `frosty-antonelli-fd537f` cont)

**What landed:** the raw-payload byte-wrapper side of Step 3b. Two new emitters + two matching parity checks bring catalog universality to **12 of 13 kinds emitted**.

- `port/src/romextract_pdfont.c` -- walks 10 NTSC font face segments (`bankgothic` / `zurich` / `tahoma` / `numeric` / `handelgothic{xs,sm,md,lg}` / `ocra{md,lg}`; + JPN `fontjpn` / `fontjpnsingle` on JPN builds) and emits one `.pdfont` ZIP per face. Reads raw bytes from `data/<romid>/segs/<face>.bin` (Pass A) and wraps with manifest envelope (`pd_kind="font"` / `face` / `data` / `data_size` / `source_segment`). Catalog ID `base:font_<facename>`. The Step 4 universal loader runs `preprocessFont` on the raw bytes at load time so the emitter does not duplicate the preprocess pass.
- `port/src/romextract_pdlang.c` -- walks `g_LangFiles[1..68]` and emits one `.pdlang` ZIP per bank. Reads raw bytes from `data/<romid>/files/<sanitized>.bin` (Pass A). Bank name extracted from the `FILE_L<NAME>E` enum string via `loaderPdbaseNameForFileEnum` (e.g. `FILE_LGUNE` -> `gun`); falls back to hex bank index for unknown shapes per Q-4 Bucket 2. Stage category derived from bank ID range (banks <= 0x25 are `"stage"`; `GUN`/`PROPOBJ`/`MPWEAPONS` are `"system"`; rest are `"mp_ui"`). Catalog ID `base:lang_<bank>_en` for NTSC; PAL/JPN locale extension folds in as a Step 5 cleanup or follow-up worktree (the catalog ID includes the locale suffix so follow-up IDs do not collide).
- `port/src/romextract_parity_pdfont.c` and `_pdlang.c` -- Q-5 structural parity per the audio-half pattern. Re-opens each emitted ZIP, parses `manifest.json`, asserts envelope + key scalar fields round-trip the source. `data.bin` entry size matches source file size on disk.

Public API: `port/include/romextract_pd.h` gains four new prototypes inside a Step 3b part 1 block, with full docblocks per the established pattern.

Boot wiring: `port/src/main.c` gets a Step 3b part 1 block immediately after the Step 3 audio block. Emit + parity calls follow the established pattern.

**Build verify:** clean four-target build via `devtools/build-session.ps1`. PASS for client (55.x MB), updater (12.3 MB), server (22.4 MB), tests (24.9 MB). All four new `.obj` files compiled into the client. Server build short-circuits per `PD_SERVER` guards (no font / lang / disk-extracted source files server-side).

**Counts emitted (NTSC final ROM, expected):**

- `.pdfont`: 10 NTSC face segments expected.
- `.pdlang`: 68 banks * 1 locale (English) = 68 expected.

**Files added (4 new files, ~1131 lines new):**

- `port/src/romextract_pdfont.c` (~230 lines)
- `port/src/romextract_pdlang.c` (~310 lines)
- `port/src/romextract_parity_pdfont.c` (~230 lines)
- `port/src/romextract_parity_pdlang.c` (~280 lines)

**Files modified:**

- `port/include/romextract_pd.h` -- 4 new prototypes + Step 3b part 1 block.
- `port/src/main.c` -- Step 3b part 1 block (~30 new lines).

**State after this commit:** 12 of 13 universality kinds emitted (weapon, mesh, animation, head, body, arena, scenario, sfx, voice, song, font, lang). Remaining: `.pdui` (Step 3b part 2).

**Why .pdui is split off as Step 3b part 2:**

Per `feedback_complete_unit_shipping`, the `.pdui` emitter is sized as its own coherent unit because the consumer migration (rewriting `pdguiThemeLateInit` to read from `.pdui` ZIPs instead of `data/ui/textures/<name>.tga` loose files) cross-cuts the GL render path. UI bugs are silent at build time and surface only at runtime; bundling that cross-cut with `.pdfont` + `.pdlang` (which are pure raw-bytes-to-ZIP wrappers with zero consumer cross-cut) would conflate two risk classes.

The split mirrors the Step 3 audio half / Step 3b part 1 split: ship coherent risk-class chunks. Audio decoder lineage shipped together; raw-payload wrappers shipped together; the cross-cut piece ships in its own unit.

**Step 3b part 2 queue (next ship):**

- `port/fast3d/pdgui_theme.cpp` rewrite of `pdguiThemeExtractRomTextures` to emit per-texture `.pdui` ZIPs at `data/<romid>/ui/<id>.pdui` instead of the current loose-files writer (TGA + PNG + 9slice.json under `data/ui/textures/`).
- `port/fast3d/pdgui_theme.cpp` rewrite of `pdguiThemeLateInit` to read texture bytes from `.pdui` ZIPs via `modArchiveOpen` + `modArchiveExtractAlloc`, replacing the current `s_loadTgaTexture(disk_path)` calls. Add memory-variant TGA helper (`s_loadTgaFromMem`) since `s_loadTgaTexture` currently reads from a `FILE *`.
- `port/src/romextract_pdui.c` thin C wrapper that calls the new C++ emitter via an `extern "C"` API exposed from `pdgui_theme.cpp`.
- Bake nineslice metadata into the per-texture manifest (deprecate the standalone `.9slice.json` per the schema doc Section 2.11; the existing `parse_theme_json` `nineslice` array reader stays as the authoritative consumer).
- 14 textures expected (one `.pdui` per entry in `pdguiThemeExtractRomTextures::k_Extracts[]`).

After `.pdui`: 13 of 13 emitted. Step 4 (universal directory walker) is the universality switch.

---

## Step 3b part 2 SHIPPED 2026-05-03 (worktree `gifted-benz-cad936`)

**What landed:** the cross-cut UI texture half of Step 3b. Closes catalog universality at **13 of 13 kinds emitted**. The `.pdui` emitter + reader migration ship together per the no-half-measures directive (the consumer migration in `pdguiThemeLateInit` lands in lockstep with the emitter, not piecewise).

- `port/fast3d/pdgui_theme.cpp` -- new memory-variant TGA helpers `s_writeTgaToMem` / `s_loadTgaFromMem` (mirror `s_writeTga` / `s_loadTgaTexture` but operate on heap buffers instead of `FILE*`); new canonical `k_PduiEntries[]` table (14 textures) folding the prior `k_Extracts[]` / `k_UiTextures[]` / `k_Fallbacks[]` triplet into one source of truth; new `pdguiThemeEmitPduiZips(int force)` extern "C" emitter walking the table and writing per-texture `.pdui` ZIP compounds at `data/<romid>/ui/<slug>.pdui` (manifest envelope + `texture.tga` + `texture.tga.sha256`).
- `port/fast3d/pdgui_theme.cpp` -- `pdguiThemeLateInit` rewritten to read texture bytes from `.pdui` ZIPs via `modArchiveOpen` + `modArchiveExtractAlloc("texture.tga")` + `s_loadTgaFromMem`, replacing the prior `s_loadTgaTexture(disk_path)` loose-TGA reader. Procedural fallback (`s_registerProceduralTexture`) is unchanged.
- `port/fast3d/pdgui_theme.cpp` -- `pdguiThemeExtractRomTextures` body collapsed to a single delegate call to `pdguiThemeEmitPduiZips(0)`. Legacy loose-files writers (`s_writeTga` for ROM extract path, `s_writePng`, `s_writeNinesliceJson`, `s_initCrc32` and friends) now unreferenced from the extract path; `s_writeTga` remains live for `s_generateModernUiTextures` (CLI `--generate-modern-ui` flag); the rest are dead code retained for Step 5 cleanup. Compiler unused-function warnings are suppressed at the project level (`-Wno-unused-function` for CXX per CMakeLists.txt:276).
- `port/fast3d/pdgui_theme.cpp` -- `pdguiThemeCheckExtract` updated to detect missing `.pdui` ZIPs (via new `s_countMissingBaseUiPdui`) instead of missing loose TGAs; on missing-ZIP detection the extract auto-runs and lateInit re-runs to swap procedural fallbacks for the freshly-emitted textures.
- `port/src/romextract_pdui.c` -- thin C wrapper exposing `romExtractAllPdui(s32 force_rewrite)` that delegates to the C++ emitter via the extern "C" API. Server build returns 0 immediately.
- `port/src/romextract_parity_pdui.c` -- Q-5 structural parity check. Re-walks the canonical 14-texture mirror table, opens each `.pdui` ZIP, parses `manifest.json`, asserts envelope (`pd_kind="ui"`, `id`, `texture_count=1`, `source_index`) round-trip the source descriptor. Missing files are treated as skip (not failure) because the emitter is deferred to the render-loop trigger on first launch. Failures emit `LOADER.UNIVERSAL.PARITY_FAIL`.

Public API: `port/include/romextract_pd.h` gains a Step 3b part 2 block with two prototypes + full docblock explaining the texture-init ordering wrinkle.

Boot wiring: `port/src/main.c` gets a Step 3b part 2 block immediately after the part 1 block, calling `romExtractAllPdui(0)` + `romExtractParityCheckPdui()`. The block is the structural placeholder that mirrors the part 1 / Step 3 / Step 2 / Step 1 emit + parity convention; on first boot at this point `g_TexGeneralConfigs` is null (texInit runs later in pdmain.c::mainInit), so the emitter returns 0 cleanly and the actual emit fires from `pdguiThemeCheckExtract` in the render-loop fallback. Subsequent boots find the `.pdui` files already on disk and the call is an idempotent skip.

**Why .pdui ships separately from .pdfont / .pdlang (recap):** the `.pdfont` + `.pdlang` emitters wrap raw bytes that are already on disk after Pass A (zero render-path involvement). The `.pdui` emitter must decode N64 textureconfigs through the GL texture system, which depends on `g_TexGeneralConfigs` (populated by `texInit`/`texReset` in `pdmain.c::mainInit`). UI bugs are silent at build time and surface only at runtime; bundling the cross-cut with the raw-payload wrappers would conflate two risk classes per `feedback_complete_unit_shipping`.

**Universality model under .pdui:**

```
data/<romid>/ui/
  ui_bg_haze.pdui          ZIP: manifest.json + texture.tga + texture.tga.sha256
  ui_particles.pdui        same
  ui_noise_sm.pdui
  ui_noise_lg.pdui
  ui_grad_bar.pdui
  ui_mirror_tile.pdui
  ui_dot_tile.pdui
  ui_nuke.pdui
  ui_bg_alt.pdui
  ui_icon_a.pdui
  ui_icon_b.pdui
  ui_icon_c.pdui
  ui_deco.pdui
  ui_stars.pdui
```

Each `manifest.json` carries the envelope (`pd_kind="ui"`, `pd_schema_version=1`, `id="base:ui_<name>"`), `texture_count=1`, `theme_count=0`, and a `texture` object with `name` / `file` / `width` / `height` / `format="rgba32_top_down"` / `data_size` plus baked-in `nineslice` insets (`left` / `right` / `top` / `bottom` / `edgeMode` / `centerMode`). `source_index` records the `g_TexGeneralConfigs[]` index for round-trip provenance.

**Counts emitted (NTSC final ROM, expected after first launch):**

- `.pdui`: 14 textures expected, one ZIP per entry in `k_PduiEntries[]`.

**Files added (2 new files):**

- `port/src/romextract_pdui.c` (~50 lines, wrapper)
- `port/src/romextract_parity_pdui.c` (~240 lines)

**Files modified:**

- `port/fast3d/pdgui_theme.cpp` -- new memory-variant TGA helpers + canonical entries table + `s_pduiRelPath` + `s_decodeUiTexToRgba` + `s_pduiNinesliceInsets` + `s_pduiBuildManifest` + `s_emitOnePduiZip` + `pdguiThemeEmitPduiZips` extern "C" emitter; `pdguiThemeLateInit` rewritten to read `.pdui` ZIPs; `pdguiThemeExtractRomTextures` body collapsed to delegate; `pdguiThemeCheckExtract` updated to check `.pdui` paths; `s_countMissingBaseUiPdui` replaces the loose-TGA missing-checkers. Net diff is roughly +400 / -250 lines (legacy walk machinery removed; new universality machinery added).
- `port/include/romextract_pd.h` -- 2 new prototypes + Step 3b part 2 block (~70 new lines).
- `port/src/main.c` -- Step 3b part 2 block (~25 new lines) immediately after the Step 3b part 1 block.

**Build verify:** clean four-target via `devtools/build-session.ps1`. PASS for client (55.2 MB), updater (12.3 MB), server (22.4 MB), tests (24.9 MB). Two new `.obj` files (`romextract_pdui.c.obj`, `romextract_parity_pdui.c.obj`) compile into the client; server build short-circuits per `PD_SERVER` guard (no GL context, no UI rendering server-side); tests link without complaint.

**State after this commit:** **13 of 13 universality kinds emitted** (weapon, mesh, animation, head, body, arena, scenario, sfx, voice, song, font, lang, ui). Catalog universality writer-side is COMPLETE.

**Step 4 (next ship):** universal directory walker. `loaderPdbaseScan` retires; replaced with a generic walker that recursively enumerates `data/<romid>/`, `mods/`, and `base/` (whatever subset is appropriate per tier), reads each `.pd*` file's `pd_kind` from its envelope, and dispatches to the right typed registrar. After Step 4 the catalog reads from the same per-asset compound format whether the source is BYOR-extracted or modder-supplied -- universality is realized end-to-end.

---

## 8. Sentinel

This audit ends here. If a future reader sees content past this line, the document was modified after the original draft.

DOC_END_2026-05-02_CATALOG_UNIVERSALITY_PIVOT_PLAN
