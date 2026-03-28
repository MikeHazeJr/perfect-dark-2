# Catalog Activation Plan — Complete Architectural Blueprint

> **Created**: 2026-03-28, Session 74
> **Status**: PLAN ONLY — no implementation yet
> **Purpose**: Architectural blueprint for replacing all scattered direct asset loads with catalog infrastructure
> **Supersedes**: `context/catalog-loading-plan.md` (preserved for history — this doc is the implementation contract)
> **Back to**: [index](../README.md)

---

## 0. Current State Inventory

Before designing phases, understand exactly what exists and what doesn't.

### What Exists (Ready to Wire)

| Component | File | State |
|-----------|------|-------|
| Hash table + entry pool | `assetcatalog.c/h` | **Complete** — FNV-1a, CRC32, linear probing, dynamic growth |
| Load-state lifecycle | `assetcatalog.h` | **Complete** — REGISTERED→ENABLED→LOADED→ACTIVE + ref_count |
| Registration API | `assetcatalog.h` | **Complete** — 17 typed `catalogRegister*` wrappers |
| Resolution API | `assetcatalog.h` | **Complete** — by string ID, by net_hash, by body/stage index |
| Iteration + query API | `assetcatalog.h` | **Complete** — by type, by category, getSkinsForTarget |
| Base game registration | `assetcatalog_scanner.h` | **Complete API, NOT CALLED** — `assetCatalogRegisterBaseGame()` exists but is never called at startup |
| Component scanner | `assetcatalog_scanner.h` | **Complete API, NOT CALLED** — `assetCatalogScanComponents()` exists but is never called |
| Mod stage file resolve | `assetcatalog_resolve.c/h` | **Active (D3R-5)** — bgdata path redirect when active stage set |
| fs.c catalog hook | `fs.c:97` | **Active (D3R-5)** — calls `assetCatalogResolvePath()` for bgdata only |

### What's Missing (The Gaps)

| Gap | Effect |
|-----|--------|
| `assetCatalogRegisterBaseGame()` not called at startup | Catalog is empty — all 23 asset types unregistered |
| No filenum reverse-index | C-4 intercept can't map `romdataFileLoad(filenum)` → catalog entry |
| No texnum/animnum/soundnum reverse-index | C-5/C-6/C-7 intercepts can't redirect mod overrides |
| `assetCatalogScanComponents()` not called | Mod components never enter catalog |
| MEM-2/MEM-3 not implemented | No `catalogLoad/Unload` API — assets loaded but never tracked or evicted |
| C-4 through C-7 intercepts not wired | ROM loads bypass catalog entirely — mod overrides for base-game assets impossible |
| No ROM hash / cache system | Full re-registration every launch (slow) |
| Stage transition diff not implemented | "Unload everything, load everything" on every stage change |

---

## 1. Architecture Overview

### 1.1 The Three-Layer Intercept Model

```
┌─────────────────────────────────────────────────────────────────┐
│  Layer 0: Game Code (call sites — DO NOT MODIFY THESE)          │
│  fileLoadToNew(filenum)  texLoad(texnum)  sndStart(soundnum)    │
│  animLoadFrame(animnum)  … 100+ call sites                      │
└──────────────────┬──────────────────────────────────────────────┘
                   │  transparent intercept — callers unchanged
┌──────────────────▼──────────────────────────────────────────────┐
│  Layer 1: Catalog Intercept (NEW — assetcatalog_load.c)         │
│  C-4: romdataFileLoad()  intercept (all file types)             │
│  C-5: texLoad() intercept                                       │
│  C-6: animLoadFrame/Header() intercept                          │
│  C-7: sndStart() intercept                                      │
│                                                                 │
│  Each intercept: catalogGetOverride(id) → path OR null          │
│      ↓ hit: load from mod path                                  │
│      ↓ miss: fall through to Layer 2                            │
└──────────────────┬──────────────────────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────────────────┐
│  Layer 2: Platform Loaders (EXISTING — mostly unchanged)        │
│  romdataFileLoad() — files/ directory → ROM                     │
│  texDecompress()   — texturesdata segment                       │
│  animLoad*()       — animations segment                         │
│  snd*()            — sfxctl/sfxtbl segments                     │
└─────────────────────────────────────────────────────────────────┘
```

**Key principle**: Intercepts go into the gateway functions, not call sites. The 16 `fileLoadToNew` callers, 50+ texture callers, 16+ animation callers, and 100+ audio callers are all untouched. The intercept lives in the single function each group bottlenecks through.

### 1.2 Unified Load API — `assetcatalog_load.h`

New file: `port/include/assetcatalog_load.h` / `port/src/assetcatalog_load.c`

This module owns the reverse-index maps, the intercept queries, and the lifecycle API:

```c
/* === Initialization (call after catalogRegisterBaseGame + ScanComponents) === */

/* Build reverse-index maps from the populated catalog.
 * Must be called once after catalog is populated, before any asset loads.
 * Rebuilds automatically on catalogClearMods() + rescan. */
void catalogLoadInit(void);

/* === Intercept Queries (called from gateway functions) === */

/* C-4: Returns override path if a non-bundled mod entry overrides this filenum.
 * Returns NULL if no override — caller uses normal ROM/files path.
 * Path points to internal buffer, valid until next call. */
const char *catalogGetFileOverride(s32 filenum);

/* C-5: Returns override path if a non-bundled mod entry overrides this texnum.
 * NULL = no override, use ROM texture segment. */
const char *catalogGetTextureOverride(s32 texnum);

/* C-6: Returns override path if a non-bundled mod entry overrides this animnum.
 * NULL = no override, use ROM animation segment. */
const char *catalogGetAnimOverride(s32 animnum);

/* C-7: Returns override path if a non-bundled mod entry overrides this soundnum.
 * NULL = no override, use ROM audio. */
const char *catalogGetSoundOverride(s32 soundnum);

/* === Lifecycle API (MEM-2/MEM-3) === */

/* Load an asset to memory and advance its load state to LOADED.
 * Increments ref_count. Bundled assets are already LOADED — this is a no-op + retain.
 * Returns 1 on success, 0 on error. */
s32 catalogLoadAsset(const char *assetId);

/* Decrement ref_count. If ref_count reaches 0 and asset is not bundled:
 * free loaded_data, set load_state back to ENABLED.
 * Bundled assets (ASSET_REF_BUNDLED sentinel) are never evicted. */
void catalogUnloadAsset(const char *assetId);

/* Increment ref_count without loading (asset must already be LOADED). */
void catalogRetainAsset(const char *assetId);

/* === Stage Transition Diff (C-9) === */

/* Compute which assets to load and unload for a stage transition.
 * Fills toLoad[] and toUnload[] with asset IDs (up to maxItems each).
 * Returns total changes. Caller drives the actual load/unload calls.
 * Dynamic player count: toLoad set scales with catalog body/arena counts,
 * not hardcoded player limits. */
s32 catalogComputeStageDiff(const char *newStageId,
                            const char **toLoad,  s32 *loadCount,
                            const char **toUnload, s32 *unloadCount,
                            s32 maxItems);
```

### 1.3 Reverse-Index Design

The intercepts need O(1) lookup from numeric ID to catalog entry. Three separate arrays, one per numeric space:

```c
/* Inside assetcatalog_load.c — not exposed in header */

/* C-4: filenum → pool index (or -1 if no mod override) */
static s32 s_FilenumOverride[ROMDATA_MAX_FILES];   /* 2048 entries × 4 bytes = 8KB */

/* C-5: texnum → pool index (or -1 if no mod override) */
static s32 s_TexnumOverride[CATALOG_MAX_TEXTURES]; /* sized to ROM texture count */

/* C-6: animnum → pool index (or -1 if no mod override) */
static s32 s_AnimnumOverride[CATALOG_MAX_ANIMS];   /* sized to ROM anim count */

/* C-7: soundnum → pool index (or -1 if no mod override) */
static s32 s_SoundnumOverride[CATALOG_MAX_SOUNDS]; /* sized to ROM sound count */
```

`catalogLoadInit()` scans the entry pool and populates these arrays. Only non-bundled entries are entered (bundled = base game = no override needed).

The `asset_entry_t` structure needs two new common fields — add to the main struct (not the union):

```c
/* Source numeric ID for reverse-index (populated during base game registration) */
s32 source_filenum;   /* ROM filenum, or -1 if not a file-slot asset */
s32 source_texnum;    /* ROM texnum, or -1 */
s32 source_animnum;   /* ROM animnum, or -1 */
s32 source_soundnum;  /* ROM soundnum, or -1 */
```

These fields are set during `assetCatalogRegisterBaseGame()` (base game) and during component scanning (mod overrides set the filenum they replace).

### 1.4 Mod Override Path Resolution

When a mod component wants to override ROM file `FILE_CBOND_BODYZ`, the component's INI declares:

```ini
[character]
name = GF64 Bond
bodyfile = files/Cbond_bodyZ
headfile = files/Hbond_headZ
```

During scanning, `assetCatalogScanComponents()` resolves the component-relative path to an absolute path: `{component_dir}/files/Cbond_bodyZ`. This is stored in `asset_entry_t.ext.character.bodyfile` as an absolute path.

Then `catalogLoadInit()` asks: "what ROM filenum is named `Cbond_bodyZ`?" — scans `fileSlots[]` for a matching name, gets the filenum, writes it to `s_FilenumOverride[filenum] = poolIdx`.

**The ROM filename lookup**: `romdataGetFileName(filenum)` — we need this helper if it doesn't exist. Check `fileSlots[fileNum].name`.

### 1.5 Dynamic Player Count Design

All code that currently iterates `g_MpBodies[0..N]` or uses `g_MpBodiesCount` (hardcoded 63) must be replaced with catalog iteration:

```c
/* OLD — hardcoded */
for (int i = 0; i < g_MpBodiesCount; i++) {
    setup_body(g_MpBodies[i]);
}

/* NEW — catalog-driven, scales with mods */
assetCatalogIterateByType(ASSET_BODY, setup_body_callback, NULL);
/* or for count: */
int count = assetCatalogGetCountByType(ASSET_BODY);
```

Matchmaking code that distributes slots: use `assetCatalogGetCountByType(ASSET_ARENA)` for map counts, ASSET_BODY/ASSET_HEAD for character counts. Nothing assumes 63 bodies, 76 heads, 87 stages. The catalog is the authoritative count.

### 1.6 Proper Load Order

The ROM load order (documented in `romdata.c`'s segment table and stage loading code) must be respected:

```
Boot:
  1. Segments loaded (fonts, animations table, textures list, audio ctl/tbl, sequences)
     → These are BUNDLED, always LOADED, ref_count = ASSET_REF_BUNDLED

Stage transition:
  2. Tile file      (LOADTYPE_TILES)   — fileLoadToNew
  3. BG seg files   (LOADTYPE_BG)      — fileLoadPartToAddr (multi-part)
  4. Setup file     (LOADTYPE_SETUP)   — fileLoadToNew
  5. Pads file      (LOADTYPE_PADS)    — fileLoadToNew
  6. Lang file      (LOADTYPE_LANG)    — fileLoadToNew
  7. Models         (LOADTYPE_MODEL)   — fileLoadToNew (on demand during setup)
  8. Textures       — texLoad* (on demand during render)

Per-frame:
  9. Dynamic props  — fileLoadToNew (on demand)
  10. Audio          — sndStart (on demand)
```

The catalog lifecycle mirrors this: stage assets advance to LOADED in the stage preparation phase (lvReset / lv.c), then to ACTIVE when referenced during gameplay. The `catalogComputeStageDiff` operates at step boundary 2-7.

---

## 2. Dependency Graph

```
╔═══════════════════════════════════════════════════════════╗
║  FOUNDATION (exists, just needs calling)                   ║
║  assetCatalogInit()  →  assetcatalog.c                    ║
╚═══════════════════════════════════════════════════════════╝
              │
              ▼
╔═══════════════════════════════════════════════════════════╗
║  C-1: ROM Hash Computation                                 ║
║  pdmain.c / main.c: compute SHA-256 of ROM file            ║
║  Compare to cached hash in catalog-cache.json              ║
╚═══════════════════════════════════════════════════════════╝
              │
              ▼ (cache miss or first launch)
╔═══════════════════════════════════════════════════════════╗
║  C-2: Base Game Catalog Population                         ║
║  assetCatalogRegisterBaseGame()                            ║
║  assetCatalogRegisterBaseGameExtended()                    ║
║  assetCatalogScanComponents(modsdir)                       ║
║  assetCatalogScanBotVariants(modsdir)                      ║
╚═══════════════════════════════════════════════════════════╝
              │
    ┌─────────┴──────────────────────────┐
    ▼                                    ▼
╔══════════════════╗            ╔═════════════════════╗
║  C-3: Disk Cache  ║            ║  catalogLoadInit()   ║
║  Write populated  ║            ║  Build 4 reverse-   ║
║  catalog to JSON  ║            ║  index arrays        ║
║  Cache next boot  ║            ║  (filenum/tex/anim/  ║
╚══════════════════╝            ║   sound → poolIdx)   ║
    (on cache miss)              ╚═════════════════════╝
                                          │
              ┌───────────────────────────┤
              │                           │
              ▼                           │
╔══════════════════════════════╗          │
║  C-4: fileLoadToNew Intercept ║          │
║  In romdataFileLoad():        ║          │
║  catalogGetFileOverride()     ║          │
║  → load from mod path, OR    ║          │
║  → fall through to files/ROM  ║          │
╚══════════════════════════════╝          │
              │                           │
    ┌─────────┴──────────────┐            │
    ▼                        ▼            │
╔══════════════╗   ╔══════════════════╗   │
║ C-5: texLoad ║   ║ C-6: animLoad    ║   │
║   Intercept  ║   ║   Intercept      ║   │
╚══════════════╝   ╚══════════════════╝   │
          │                  │            │
          └────────┬─────────┘            │
                   ▼                      │
         ╔══════════════════╗             │
         ║  C-7: sndStart   ║             │
         ║   Intercept      ║             │
         ╚══════════════════╝             │
                   │                      │
    ┌──────────────┘                      │
    │                                     │
    ▼                                     ▼
╔══════════════════════╗        ╔═══════════════════════╗
║  C-8: Mod Diff       ║        ║  MEM-2: catalogLoad/  ║
║  Re-Cataloging       ║        ║  Unload API           ║
║  Folder hash check   ║        ║  ref_count lifecycle  ║
║  Delta scan on       ║        ║  Eviction policy      ║
║  enable/disable      ║        ╚═══════════════════════╝
╚══════════════════════╝                  │
                                          ▼
                               ╔═══════════════════════╗
                               ║  MEM-3/C-9: Stage     ║
                               ║  Transition Diff      ║
                               ║  catalogComputeStage  ║
                               ║  Diff() in lvReset()  ║
                               ║  Incremental load/    ║
                               ║  unload per stage     ║
                               ╚═══════════════════════╝
```

### 2.1 What Unlocks What

| Completes | Unlocks |
|-----------|---------|
| C-1 (ROM hash) | Reliable cache invalidation; startup speed |
| C-2 (base game registered) | Everything — catalog is empty without this |
| `catalogLoadInit()` | C-4/C-5/C-6/C-7 intercepts (need reverse-index) |
| C-4 (file intercept) | Mod model/tile/pads/setup/lang overrides; all 16 fileLoadToNew callers |
| C-5 (tex intercept) | Mod texture replacement |
| C-6 (anim intercept) | Mod animation replacement |
| C-7 (snd intercept) | Mod audio replacement |
| C-4 + C-5 + C-6 + C-7 | C-8 (mod enable/disable is only meaningful once intercepts are live) |
| C-4 through C-7 + MEM-2 | C-9 (stage transition diff needs all loads and lifecycle tracking) |

---

## 3. Implementation Phases

### Phase C-0: Wire the Existing APIs (Prerequisite)

**What it changes**: `port/src/pdmain.c` (or wherever startup sequence lives)
**Files touched**: `pdmain.c`, `assetcatalog_scanner.c`
**Dependencies**: Nothing — just calling APIs that exist
**What it unlocks**: Catalog is populated; all iteration/query APIs become functional
**Effort**: Small — 10-20 lines of startup wiring

**Work:**
1. In `pdmain.c` startup sequence (after ROM loads, before menus):
   ```c
   assetCatalogInit();
   assetCatalogRegisterBaseGame();
   assetCatalogRegisterBaseGameExtended();
   assetCatalogScanComponents(fsGetModsDir());
   assetCatalogScanBotVariants(fsGetModsDir());
   ```
2. Verify `assetCatalogGetCount()` returns > 0 at startup
3. Wire `assetCatalogClearMods()` + re-scan to the mod enable/disable path in `modmgr.c`

**Testing criteria**: After startup, `assetCatalogGetCountByType(ASSET_BODY)` returns 63+ (base bodies). Log the count at startup.

**Risk**: Low. `assetCatalogRegisterBaseGame()` was written to be called here.

---

### Phase C-1: ROM Hash + Cache Validation

**What it changes**: `port/src/pdmain.c`, new `assetcatalog_cache.c/h`
**Files touched**: `pdmain.c`, `assetcatalog_cache.c` (new), `assetcatalog_cache.h` (new)
**Dependencies**: C-0 (catalog populated, so there's something to cache)
**What it unlocks**: Fast startup on repeat launches; forces re-catalog on ROM change
**Effort**: Medium — JSON serialization + SHA-256 hash check

**Work:**
1. At startup, SHA-256 hash the ROM file (sha256.c already exists in port/)
2. Check `$S/catalog-cache-{hash8}.json` — if it exists and is current, load from it
3. If cache miss: run C-0 registration, then serialize catalog to JSON
4. Cache invalidation: any change to ROM hash invalidates the cache
5. Mod hash: separately track the enabled mod set hash; catalog cache stores both

**Cache JSON format** (same as `catalog-loading-plan.md §2.3`):
```json
{
  "rom_hash": "abc123ef",
  "mod_hash": "def456ab",
  "version": 1,
  "entries": [...]
}
```

**Testing criteria**: First launch: cache file created. Second launch: "loaded from cache" log message. Delete ROM → hash changes → cache invalidated → re-registration runs.

**Risk**: Medium. JSON serialization of 500+ entries needs to be fast. Consider binary format if too slow — but start with JSON for debuggability.

**Implementation note**: `sha256.c/h` already exists (`port/src/sha256.c`, `port/include/sha256.h`) — use it.

---

### Phase C-2-ext: Add Numeric Source IDs to `asset_entry_t`

**What it changes**: `port/include/assetcatalog.h`, `port/src/assetcatalog_scanner.c`
**Files touched**: `assetcatalog.h`, `assetcatalog_scanner.c`, `assetcatalog.c`
**Dependencies**: C-0 (must understand what gets registered before adding fields)
**What it unlocks**: Required for `catalogLoadInit()` to build reverse-index maps
**Effort**: Small — struct field addition + population in scanner

**Work:**
1. Add to `asset_entry_t` common section (before the union):
   ```c
   /* Source numeric IDs for reverse-index (C-4 through C-7).
    * -1 means "not applicable to this asset type". */
   s32 source_filenum;    /* ROM fileSlots[] index */
   s32 source_texnum;     /* ROM textures table index */
   s32 source_animnum;    /* ROM animations table index */
   s32 source_soundnum;   /* ROM sounds table index */
   ```
2. Initialize all four to -1 in `assetCatalogRegister()`
3. In `assetCatalogRegisterBaseGame()`: set `source_filenum` for model/tile/pads/setup/lang assets; set `source_texnum` for texture assets; etc.
4. In `assetCatalogScanComponents()`: when a mod component declares an override file, resolve the filename to a ROM filenum and set `source_filenum`.
5. Add helper: `s32 romdataFilenumByName(const char *name)` — scan `fileSlots[]` for matching `.name`, return filenum. Location: `port/src/romdata.c` + `port/include/romdata.h`.

**Testing criteria**: After C-0 + C-2-ext: iterate ASSET_CHARACTER entries, all base characters should have `source_filenum >= 1`. Log any that don't.

---

### Phase `catalogLoadInit()`: Build Reverse-Index Maps

**What it changes**: New `port/src/assetcatalog_load.c`, new `port/include/assetcatalog_load.h`
**Files touched**: New files; `pdmain.c` (call `catalogLoadInit()` after catalog population)
**Dependencies**: C-0 + C-2-ext (catalog populated with source numeric IDs)
**What it unlocks**: C-4/C-5/C-6/C-7 intercepts
**Effort**: Small — scan entry pool, fill 4 arrays

**Work:**
1. Create `assetcatalog_load.c` with static arrays:
   ```c
   static s32 s_FilenumOverride[ROMDATA_MAX_FILES];   /* all -1 initially */
   static s32 s_TexnumOverride[4096];
   static s32 s_AnimnumOverride[2048];
   static s32 s_SoundnumOverride[4096];
   ```
2. `catalogLoadInit()` iterates entry pool: for each non-bundled enabled entry with source_filenum >= 0, set `s_FilenumOverride[source_filenum] = poolIdx`.
3. Expose query functions as documented in §1.2 above.
4. Wire to mod enable/disable: `catalogLoadInit()` must be called after any `assetCatalogClearMods()` + re-scan sequence.

**Testing criteria**: After wiring a test mod component: `catalogGetFileOverride(filenum)` returns the component's file path. For base game filenums with no mod override: returns NULL.

---

### Phase C-4: fileLoadToNew Intercept

**What it changes**: `port/src/romdata.c`
**Files touched**: `romdata.c`, `assetcatalog_load.c` (query function)
**Dependencies**: `catalogLoadInit()` (reverse-index ready)
**What it unlocks**: All 16 `fileLoadToNew` callers benefit automatically — mod models, tiles, pads, setup, lang files
**Effort**: Small — 10 lines in one function

**Current `romdataFileLoad` (simplified):**
```c
u8 *romdataFileLoad(s32 fileNum, u32 *outSize) {
    // ...range check...
    if (fileSlots[fileNum].source == SRC_UNLOADED) {
        char tmp[FS_MAXPATH];
        snprintf(tmp, sizeof(tmp), ROMDATA_FILEDIR "/%s", fileSlots[fileNum].name);
        if (fsFileSize(tmp) > 0 && ...) {
            out = fsFileLoad(tmp, &size);
            // ...cache in slot...
        }
        // if still unloaded: source = SRC_ROM (use ROM bytes)
    }
    // ...
}
```

**With C-4 intercept:**
```c
u8 *romdataFileLoad(s32 fileNum, u32 *outSize) {
    // ...range check...
    if (fileSlots[fileNum].source == SRC_UNLOADED) {

        /* C-4: catalog override check — mod file takes priority */
        const char *modPath = catalogGetFileOverride(fileNum);
        if (modPath) {
            u32 size = 0;
            u8 *out = fsFileLoad(modPath, &size);
            if (out && size) {
                fileSlots[fileNum].data = out;
                fileSlots[fileNum].size = size;
                fileSlots[fileNum].source = SRC_EXTERNAL;
                fileSlots[fileNum].numpatches = 0; /* mod file — no ROM patches */
                sysLogPrintf(LOG_NOTE, "C-4: file %d (%s) loaded from catalog mod",
                             fileNum, fileSlots[fileNum].name);
                if (outSize) *outSize = size;
                return out;
            }
        }

        /* existing: check files/ directory, then fall back to ROM */
        char tmp[FS_MAXPATH];
        snprintf(tmp, sizeof(tmp), ROMDATA_FILEDIR "/%s", fileSlots[fileNum].name);
        // ...unchanged...
    }
    // ...unchanged...
}
```

**Why here, not in `fileLoadToNew`**: `romdataFileLoad` is the single chokepoint — it's called by `romdataFileGetData()` → `fileGetRomAddress()` → `fileLoadToNew/fileLoadToAddr`, AND by direct callers. One intercept covers everything.

**Testing criteria**:
- Install a mod that overrides one model file (e.g., Joanna's body)
- Load a stage containing that character
- Log "C-4: file N loaded from catalog mod" appears
- Model displays with mod's version

**Why `catalogGetFileOverride` returns NULL for base game**: `s_FilenumOverride[filenum]` is only set for non-bundled (mod) entries. Base game filenums return -1, making `catalogGetFileOverride` return NULL — existing behavior unchanged.

---

### Phase C-5: texLoad Intercept

**What it changes**: `src/game/texdecompress.c` (or `src/game/tex.c`)
**Files touched**: `texdecompress.c`, `assetcatalog_load.c`
**Dependencies**: `catalogLoadInit()`
**What it unlocks**: Mod texture replacement packs; per-texture overrides
**Effort**: Medium — need to understand texture loading path first

**Work:**
1. Locate the texture load bottleneck (likely `texLoad()` in `texdecompress.c`)
2. Add `catalogGetTextureOverride(texnum)` to the intercept chain
3. If override exists: load texture from mod file path, skip ROM segment decompression
4. Mod texture components declare texnums they override via INI or filename matching

**Testing criteria**: Mod texture replaces one stage texture. The replaced texture appears in-game. ROM texture path untouched for non-overridden texnums.

**Risk**: Medium. Texture loading is complex (segmented, compressed, multiple formats). Must not break the existing texture pipeline. Intercept must be narrow: only redirects if there's an explicit catalog override, never changes the decompression logic.

---

### Phase C-6: animLoadFrame/animLoadHeader Intercept

**What it changes**: Animation loading code (anim.c or equivalent)
**Files touched**: anim.c, `assetcatalog_load.c`
**Dependencies**: `catalogLoadInit()`
**What it unlocks**: Mod animation replacements; extended animation sets
**Effort**: Medium

**Work:**
1. Locate `animLoadFrame` and `animLoadHeader` (or equivalent in anim.c)
2. Both already have a mod fallback (`modAnimationLoadData` per `catalog-loading-plan.md §2.2`)
3. Replace that existing mod fallback with catalog resolution: `catalogGetAnimOverride(animnum)` → path → load
4. This is a refactor of the existing mod path, not a new intercept from scratch

**Testing criteria**: A mod animation file overrides one animation. The animation plays correctly in-game.

---

### Phase C-7: sndStart Intercept

**What it changes**: Sound system (snd.c or equivalent)
**Files touched**: snd.c, `assetcatalog_load.c`
**Dependencies**: `catalogLoadInit()`
**What it unlocks**: Mod audio replacement (SFX, music, voice)
**Effort**: Medium — audio pipeline is complex (sfxctl/sfxtbl segment, sequence system)

**Work:**
1. Locate `sndStart` / `sndLoad*` entry point
2. Already has a mod fallback (`modSoundLoad` per `catalog-loading-plan.md §2.2`)
3. Replace with `catalogGetSoundOverride(soundnum)` → load from mod file
4. Mod audio entries in catalog carry `ext.audio.file_path` for non-ROM audio

**Testing criteria**: Mod audio file replaces one SFX. The replacement plays when triggered.

---

### Phase C-8: Mod Diff-Based Re-cataloging

**What it changes**: `port/src/modmgr.c`, `pdmain.c`
**Files touched**: `modmgr.c`, `assetcatalog_scanner.c`, `assetcatalog_load.c`
**Dependencies**: C-4 through C-7 (intercepts live — re-cataloging is only useful if overrides work)
**What it unlocks**: Hot mod enable/disable without restart; correct behavior when mod set changes at runtime
**Effort**: Medium

**Work:**
1. Track "enabled mod hash": hash of currently-enabled component ID list
2. On mod enable/disable: if hash changes:
   a. `assetCatalogClearMods()` — removes non-bundled entries
   b. `assetCatalogScanComponents()` — re-scans with new enabled set
   c. `catalogLoadInit()` — rebuilds reverse-index arrays
   d. Any currently LOADED mod assets must be unloaded before ClearMods
3. For startup: check stored mod hash vs current → if changed, skip cache and re-register
4. Expose progress: log "Re-cataloging: N components registered" when diff-scan completes

**Delta scan**: Full re-scan is acceptable for now (mod sets are small). True delta (only scan new/changed) can be added later if needed.

**Testing criteria**:
- Enable a mod → new catalog entries appear, intercepts active
- Disable a mod → catalog entries gone, file loads fall back to ROM
- Enable → play → disable → play: both states work correctly

---

### Phase MEM-2: catalogLoadAsset / catalogUnloadAsset

**What it changes**: `port/src/assetcatalog_load.c`
**Files touched**: `assetcatalog_load.c`, `assetcatalog_load.h`
**Dependencies**: C-4 (at minimum — assets must be intercept-able to load/unload them)
**What it unlocks**: Explicit asset lifecycle management; required for C-9 (stage diff)
**Effort**: Medium

**Work:**
1. Implement `catalogLoadAsset(const char *assetId)`:
   - Resolve entry
   - If ENABLED: call the appropriate load function based on `entry.type` (file, texture, anim, audio)
   - Set `entry.loaded_data`, `entry.data_size_bytes`, `entry.load_state = ASSET_STATE_LOADED`
   - Increment `entry.ref_count`
2. Implement `catalogUnloadAsset(const char *assetId)`:
   - Decrement `ref_count`
   - If `ref_count <= 0` AND `!bundled`: free `loaded_data`, set state to ENABLED
   - Never evict bundled assets (ASSET_REF_BUNDLED sentinel)
3. Implement `catalogRetainAsset` / `catalogReleaseAsset` for pure ref-count management

**Testing criteria**: Load asset → verify `load_state == LOADED`. Unload (ref=0) → verify `loaded_data == NULL`. Unload bundled asset → no effect, still LOADED.

---

### Phase MEM-3 / C-9: Stage Transition Diff Load/Unload

**What it changes**: `src/game/lv.c` (stage loader), `assetcatalog_load.c`
**Files touched**: `lv.c`, `assetcatalog_load.c`
**Dependencies**: MEM-2 (lifecycle API), all intercepts C-4 through C-7
**What it unlocks**: Incremental stage loading; faster transitions; proper memory management
**Effort**: Large

**Work:**
1. Implement `catalogComputeStageDiff(newStageId, toLoad, loadCount, toUnload, unloadCount, max)`:
   - Query catalog for all assets associated with `newStageId` (tiles, bg, pads, setup, models)
   - Compare against currently ACTIVE assets
   - Populate toLoad (needed but not LOADED) and toUnload (LOADED but not needed for new stage)
2. In `lvReset()` (stage setup):
   - Call `catalogComputeStageDiff(newStageId, ...)`
   - Loop toUnload: `catalogUnloadAsset(id)` for each
   - Loop toLoad: `catalogLoadAsset(id)` for each
   - Set all loaded assets to ACTIVE
3. Dynamic player count integration: the diff includes character assets proportional to the current match player+bot count, not hardcoded to 4. The match config (`g_MatchConfig.slots[]`) drives which character assets to include in the toLoad set.

**Testing criteria**:
- Stage A → Stage B: assets unique to A are unloaded, assets unique to B are loaded, shared assets skipped
- Verify "toLoad: N, toUnload: M, shared: K" log at each transition
- Memory usage: confirm no double-load of shared assets

**Load ordering respected**: `catalogComputeStageDiff` returns toLoad in the correct order (tile → bg → setup → pads → models). The caller (lvReset) processes them in order.

---

## 4. Risk Assessment

### R-1: ROM Filename Matching for Override Map (MEDIUM)

**Risk**: Building the reverse-index map requires matching mod component filenames against ROM fileSlot names. If filenames differ (case, extension, convention), matches fail silently — mod loads ROM version instead of mod version.

**Mitigation**:
- Normalize filenames during matching (lowercase, strip path prefix)
- Log every entry added to the reverse-index: "C-4 override registered: filenum N → mod path"
- For first implementation: exact match is fine, add fuzzy match only if needed
- Detect miss at load time: if catalogGetFileOverride returns NULL but mod has component for this stage, log warning

### R-2: Texture Loading Path Complexity (MEDIUM)

**Risk**: Texture loading is multi-stage (segment decompression, format conversion, GPU upload). Intercepting at the wrong level could decompress ROM textures and then try to replace them, wasting time and causing artifacts.

**Mitigation**:
- Intercept before decompression, not after
- Mod textures must be pre-processed (same format as ROM expects, or in a format the PC renderer handles natively)
- C-5 is lower priority than C-4 — defer until C-4 is stable

### R-3: fileSlots Caching Conflicts with Override (MEDIUM)

**Risk**: `fileSlots[fileNum].source` is set to `SRC_ROM` or `SRC_EXTERNAL` after first load. If a mod is enabled mid-session, the slot is already cached and the override won't apply.

**Mitigation**:
- On mod enable (C-8): `romdataFileFree(filenum)` for any affected filenums, then reset `source = SRC_UNLOADED`. This forces re-load through the new override path.
- Add `catalogClearFileOverrideCache()` called from C-8 after re-cataloging.

### R-4: Audio Intercept Segment Complexity (HIGH)

**Risk**: PD audio uses the N64 audio library format (sfxctl/sfxtbl/seqctl/seqtbl segments, ALBankFile format). Mod audio in a modern format (MP3/OGG) requires the mixer, not the N64 audio system. The intercept architecture differs from C-4.

**Mitigation**:
- C-7 defers to last. The existing `modSoundLoad` already handles this for the modmgr path.
- Migration path: replace `modSoundLoad` call with `catalogGetSoundOverride` — the load mechanism itself doesn't change, just the source resolution.
- For new mod audio formats (MP3/OGG): that's a separate mixer concern, not catalog activation.

### R-5: Cache Invalidation Correctness (LOW)

**Risk**: ROM hash or mod hash not recalculated when it should be, leading to stale catalog state.

**Mitigation**:
- Hash check is cheap (SHA-256 of ROM, already read into memory). Always check at startup.
- Mod hash: hash the list of enabled component IDs + their versions. O(N) where N is mod count.
- If in doubt, add a `--fresh-catalog` CLI flag that skips cache loading.

### R-6: `asset_entry_t` Struct Layout Change (LOW)

**Risk**: Adding 4 new `s32` fields to `asset_entry_t` breaks any serialized cache.

**Mitigation**:
- Bump cache `version` field from 1 → 2 when struct layout changes
- Cache version mismatch → invalidate and re-register
- The struct is only serialized to the cache file, not to the network or save files

---

## 5. Mod Integration Points

### 5.1 Where Mods Hook Into the Load Path

```
Mod component installed in mods/{category}/{id}/
    ↓
C-8: assetCatalogScanComponents() scans it, registers catalog entry
    ↓
C-2-ext: scanner sets source_filenum/texnum/animnum/soundnum on entry
    ↓
catalogLoadInit(): reverse-index updated — s_FilenumOverride[N] = this entry
    ↓
Game loads stage → fileLoadToNew(N) → romdataFileLoad(N)
    ↓
C-4: catalogGetFileOverride(N) returns mod component file path
    ↓
fsFileLoad(mod_path) — mod file loaded into memory
    ↓
Existing preprocessing (byteswap, endian) applied normally
```

### 5.2 Override Priority

When multiple entries could override the same filenum (base + multiple mods), the **last writer wins** rule from `assetCatalogRegister` applies: the last component scanned with that override wins. Scan order = filesystem order within category, deterministic within a session.

For user-controlled priority (mod A should beat mod B for the same file): not in scope for initial activation. Add explicit priority field to component INI in a later revision.

### 5.3 Base Game "Override" Is a No-Op

Base game entries have `bundled = 1`. `catalogGetFileOverride` only returns a path for non-bundled entries. Base game loads are never redirected — they always use the existing `files/` → ROM fallback chain. This is intentional and correct.

### 5.4 Soft Dependencies Between Mods

If a character mod references a texture pack via `depends_on = gf64_textures`:
- If `gf64_textures` is installed and enabled: catalog has both entries, both overrides active
- If `gf64_textures` is missing: texture loads fall through to ROM textures (graceful degradation, not crash)
- At C-8 time: dependency check can log a warning but must not block loading

### 5.5 Network Sync

Asset requirements are sent as CRC32 hashes of string IDs (`entry.net_hash`). Already implemented in `netdistrib.c:288` via `assetCatalogResolveByNetHash`. The intercept activation does not change this — network sync is catalog-level, not load-level.

### 5.6 Dedicated Server

The dedicated server stub (`port/src/server_stubs.c:256`) already stubs `assetCatalogResolvePath`. The C-4 intercept must be guarded by `#ifndef DEDICATED_SERVER` or equivalent — the server loads no visual assets. The catalog itself (registration + net_hash resolution) does run on the server for mod validation.

---

## 6. Files Created / Modified Summary

| File | Change |
|------|--------|
| `port/include/assetcatalog.h` | Add 4 `source_*` fields to `asset_entry_t` |
| `port/src/assetcatalog.c` | Initialize new fields to -1 in `catalogRegister()` |
| `port/src/assetcatalog_scanner.c` | Populate `source_filenum` etc. during base game registration |
| `port/include/assetcatalog_load.h` | **NEW** — unified load API (catalogLoadInit, intercept queries, lifecycle) |
| `port/src/assetcatalog_load.c` | **NEW** — reverse-index arrays, all intercept query functions, load/unload lifecycle |
| `port/include/assetcatalog_cache.h` | **NEW** — cache load/save API |
| `port/src/assetcatalog_cache.c` | **NEW** — JSON serialize/deserialize catalog state |
| `port/src/romdata.c` | C-4 intercept (6 lines in `romdataFileLoad`) + `romdataFilenumByName()` helper |
| `port/include/romdata.h` | Expose `romdataFilenumByName()` |
| `src/game/texdecompress.c` | C-5 intercept |
| Anim loading file (TBD) | C-6 intercept |
| Sound loading file (TBD) | C-7 intercept |
| `src/game/lv.c` | C-9: call `catalogComputeStageDiff` in stage transition |
| `port/src/pdmain.c` | Wire startup sequence: init → register → scan → `catalogLoadInit` |
| `port/src/modmgr.c` | Wire mod enable/disable to `catalogClearMods` + re-scan + `catalogLoadInit` |
| `port/src/server_stubs.c` | Stub new `catalogGetFileOverride` etc. (return NULL) |

---

## 7. Recommended Implementation Order

Given the dependency graph and risk profile, implement in this sequence:

| Step | Phase | Rationale |
|------|-------|-----------|
| 1 | C-0: Wire startup APIs | Zero risk, immediately useful — catalog becomes populated |
| 2 | C-2-ext: Add source_* fields | Needed for everything downstream; small struct change |
| 3 | `catalogLoadInit()` | Builds reverse-index; enables all intercepts |
| 4 | **C-4: fileLoadToNew intercept** | Highest value — 16 callers, all file types, critical gateway |
| 5 | C-8: Mod diff re-cataloging | Makes enable/disable work correctly with intercept live |
| 6 | C-1: ROM hash + cache | Performance improvement; requires C-0 complete |
| 7 | C-5: texLoad intercept | Next most impactful — 50+ callers |
| 8 | MEM-2: catalogLoad/Unload API | Lifecycle management; precondition for C-9 |
| 9 | C-6: animLoad intercept | Medium value — 16+ callers |
| 10 | C-7: sndStart intercept | Lower priority, highest complexity |
| 11 | C-9: Stage transition diff | Final piece — incremental load/unload at stage boundaries |

---

*Created: 2026-03-28, Session 74*
*Next action: Implement Step 1 (C-0: wire startup APIs) — zero risk, enables all validation steps*
