# Mod/Catalog System: Features & TODOs Audit

**Date**: 2026-03-29
**Scope**: Complete inventory of mod loader features and all outstanding TODO/stub/disabled code
**Files Audited**: assetcatalog.h/c, assetcatalog_load.h/c, assetcatalog_cache.h/c, assetcatalog_resolve.h/c, assetcatalog_scanner.h/c, assetcatalog_base.c, assetcatalog_base_extended.c, modmgr.h/c, mod.h/c, modpack.h/c, modelcatalog.h/c, romdata.c, snd.c, pdgui_menu_room.cpp, catalog-activation-plan.md, asset-reference-audit.md

---

## PART 1: COMPLETE FEATURE INVENTORY

### Core Hash Table & Storage (`assetcatalog.c`)

- **FNV-1a hash table** with linear probing — 2048 initial slots, dynamic growth
- **CRC32 network identity hashing** for asset synchronization across clients
- **Dynamic entry pool** — 512 initial entries, grows on demand
- **Load factor management** — rehash at 70% load
- **O(1) average lookup** by string ID via hash table

### Asset Type System (23 types)

```
ASSET_NONE, ASSET_MAP, ASSET_CHARACTER, ASSET_SKIN, ASSET_BOT_VARIANT,
ASSET_WEAPON, ASSET_TEXTURES, ASSET_SFX, ASSET_MUSIC, ASSET_PROP,
ASSET_VEHICLE, ASSET_MISSION, ASSET_UI, ASSET_TOOL, ASSET_ARENA,
ASSET_BODY, ASSET_HEAD, ASSET_ANIMATION, ASSET_TEXTURE, ASSET_GAMEMODE,
ASSET_AUDIO, ASSET_HUD, ASSET_EFFECT
```

### Registration API

- `assetCatalogRegister()` — base registration with FNV-1a + CRC32 hash computation
- Per-type helpers: `assetCatalogRegisterMap()`, `RegisterCharacter()`, `RegisterSkin()`, `RegisterBotVariant()`, `RegisterArena()`, `RegisterBody()`, `RegisterHead()`, `RegisterWeapon()`, `RegisterAnimation()`, `RegisterTexture()`, `RegisterProp()`, `RegisterTextures()`, `RegisterSfx()`, `RegisterGameMode()`, `RegisterAudio()`, `RegisterHud()`
- All return pointer to entry for caller to populate type-specific fields

### Resolution API

- `assetCatalogResolve(id)` — const lookup by string ID, returns pointer or NULL
- `assetCatalogResolveBodyIndex(id)` — returns runtime_index for character asset
- `assetCatalogResolveStageIndex(id)` — returns runtime_index for map asset
- `assetCatalogResolveByNetHash(u32)` — linear scan by CRC32 for network sync

### Lifecycle API

- `assetCatalogInit()` — allocates 2048-slot hash table and 512-entry pool
- `assetCatalogClear()` — flush all entries, reuse memory for next population
- `assetCatalogClearMods()` — remove non-bundled entries only, rehash
- `assetCatalogGetCount()` — total entries (base + mods)
- `assetCatalogGetCountByType(type)` — count by asset type
- `assetCatalogGetByIndex(idx)` — direct pool access for reverse-index iteration
- `assetCatalogGetMutable(id)` — mutable resolve (used by lifecycle layer only)

### Iteration API

- `assetCatalogIterateByType(type, fn, userdata)` — callback per type
- `assetCatalogIterateByCategory(category, fn, userdata)` — callback by category string

### Query API

- `assetCatalogHasEntry(id)` — registered (enabled or not)
- `assetCatalogIsEnabled(id)` — registered AND enabled
- `assetCatalogGetSkinsForTarget(target_id, out[], maxout)` — get all skins for a character
- `assetCatalogGetUniqueCategories(out[][], maxout)` — enumerate mod categories for UI
- `assetCatalogSetEnabled(id, bool)` — only write operation exposed to callers

### Asset Entry Fields

**Identity:** `id[64]`, `id_hash` (FNV-1a), `net_hash` (CRC32)
**Classification:** `type` (asset_type_e), `category[64]` (mod ID or "base")
**Filesystem:** `dirpath[260]` — absolute component directory
**Metadata:** `model_scale` (default 1.0f), `enabled`, `temporary`, `bundled` flags, `runtime_index`
**ROM intercept:** `source_filenum`, `source_texnum`, `source_animnum`, `source_soundnum` (−1 = N/A)
**Load state (MEM-1):** `load_state` (enum), `loaded_data` (void*), `data_size_bytes`, `ref_count`

**Type-specific union fields (selected):**
- Map: stagenum, mode, music_file
- Character: bodyfile, headfile paths
- Skin: target_id (soft reference)
- Bot variant: base_type, accuracy, reaction_time, aggression
- Arena: stagenum, requirefeature unlock, name_langid
- Body/Head: global indices, unlock requirement
- Weapon: weapon_id, name, model_file, damage, fire_rate, ammo_type, dual_wieldable
- Animation: anim_id, name, frame_count, target_body
- Texture: texture_id, width, height, format, file_path
- Prop: prop_type, name, model_file, flags, health
- GameMode: mode_id, name, description, player limits, team_based
- Audio: sound_id, name, category (sfx/music/voice), duration_ms, file_path
- HUD: hud_id, name, element_type, texture_file
- Effect: name, effect_type, target, shader_id, intensity, params[4]

---

### Load State Lifecycle (`assetcatalog_load.h/c`) — MEM-1 through MEM-3

**Load states:** REGISTERED → ENABLED → LOADED → ACTIVE

- `assetCatalogGetLoadState(id)` / `assetCatalogSetLoadState(id, state)`
- Bundled entries start at LOADED with ref_count = ASSET_REF_BUNDLED (0x7FFFFFFF) — never evicted

**Asset memory API (MEM-2):**
- `catalogLoadAsset(assetId)` — load into memory, increment ref_count; bundled return immediately; non-bundled: fsFileLoad() into entry->loaded_data
- `catalogUnloadAsset(assetId)` — decrement ref_count, free when 0; non-bundled only
- `catalogRetainAsset(assetId)` — increment ref_count without loading (shared assets, stage diff)

**Stage transition diff (MEM-3 / C-9):**
- `catalogComputeStageDiff(newStageId, toLoad[], loadCount, toUnload[], unloadCount, maxItems)`
- Separates currently loaded non-bundled assets from needed-for-new-stage
- Respects load order: ASSET_MAP first, then ASSET_CHARACTER, rest in pool order
- newStageId=NULL → all loaded non-bundled go to unload list (transition to base-game stage)
- Caller drives actual load/unload calls

---

### Catalog Intercept Layer (`assetcatalog_load.c`) — C-4 through C-7

**Reverse-index maps:**
- `s_FilenumOverride[2048]` — ROM filenum → pool index for C-4 file intercept
- `s_TexnumOverride[4096]` — ROM texnum → pool index for C-5 texture intercept
- `s_AnimnumOverride[2048]` — ROM animnum → pool index for C-6 animation intercept
- `s_SoundnumOverride[4096]` — ROM soundnum → pool index for C-7 sound intercept

**CatalogResolveResult struct:** `path` (NULL = use ROM), `catalog_id` (pool index or −1), `is_mod_override` (1 = non-bundled)

**Primary intercept queries:**
- `catalogResolveFile(filenum)` — C-4
- `catalogResolveTexture(texnum)` — C-5
- `catalogResolveAnim(animnum)` — C-6
- `catalogResolveSound(soundnum)` — C-7

**Legacy wrappers (backward compat):**
- `catalogGetFileOverride()`, `catalogGetTextureOverride()`, `catalogGetAnimOverride()`, `catalogGetSoundOverride()` — return path or NULL

**Initialization:**
- `catalogLoadInit()` — builds all 4 reverse-index maps from populated catalog; mod overrides win (higher pool indices overwrite bundled); must be called after scan + on mod enable/disable

**Stats:**
- `catalogLoadLogStats()` — logs query counts for C-4/C-5/C-6/C-7

---

### Catalog Cache (`assetcatalog_cache.h/c`) — C-1 ROM Integrity

- `catalogCacheVerifyRom(romPath, romHashHexOut)` — SHA-256 hash of ROM file
- Stores result in `$S/catalog-hash-cache.json`
- First run: creates cache, returns 1
- Subsequent runs: verifies hash match, returns 1 (success) or 0 (changed)
- I/O error: returns −1
- Detects ROM corruption or swapped ROMs at startup

---

### Catalog Scanner (`assetcatalog_scanner.h/c`) — D3R-3 & D3R-4

**Base game registration:**
- `assetCatalogRegisterBaseGame()` — iterates g_Stages[], g_MpBodies[], g_MpHeads[], g_MpArenas[]; creates "base:{name}" entries with bundled=1, enabled=1
- `assetCatalogRegisterBaseGameExtended()` — partial registration of weapons (~35), animations (10 rep), textures (5 stub), props (8 categories), gamemodes (6 scenarios), audio (10 rep); see S46b TODOs below

**Component scanner (mod discovery):**
- `assetCatalogScanComponents(modsdir)` — D3R-4; walks modsdir for `mod_*/` → `_components/{maps,characters,textures,...}/`; parses .ini manifest per component; returns count or −1
- `assetCatalogScanBotVariants(modsdir)` — scans `modsdir/bot_variants/{slug}/bot.ini`; hot-registers new variants via botVariantSave()

**INI parser:**
- `iniParse(filepath, out_section)` — one section per file [type], key=value pairs, hash/semicolon comments
- `iniGet()`, `iniGetInt()`, `iniGetFloat()` — string/int/float lookup with defaults

---

### Catalog Resolver (`assetcatalog_resolve.h/c`) — D3R-5 File Resolution

- `assetCatalogFindModMapByStagenum(stagenum)` — find non-bundled ASSET_MAP by numeric ID
- `assetCatalogActivateStage(stagenum)` — set active catalog component; scans component bgdata/ directory; discovers files by role suffix (.seg, _padsZ, _tilesZ, _setupZ, _mpsetupZ); populates s_BgFilePaths[] and s_BgFileValid[] for smart redirect; pass −1 to deactivate
- `assetCatalogDeactivateStage()` — clear active stage context (called on menu return or stage transition)
- `assetCatalogResolvePath(relPath)` — smart bgdata redirect by role suffix; exact match fallback; returns full path or NULL; static buffer valid until next call

---

### Model Catalog (`modelcatalog.h/c`)

- Pre-scans all character models at startup; validates model pointers; caches metadata (name, gender, type, validity)
- Exception-safe: Windows VEH + POSIX SIGSEGV handler
- Bad scale values clamped (not rejected)
- Populates g_HeadsAndBodies[].modeldef with validated pointers
- `catalogGetBody(index)` / `catalogGetHead(index)` — safe accessors with lazy validation
- `catalogRequestThumbnail(index)` — queue thumbnail render (charpreview FBO integration pending — see TODOs)

---

### Mod Manager (`modmgr.h/c`)

**Lifecycle:** `modmgrInit()`, `modmgrShutdown()`, `modmgrReload()`

**Registry:** `modmgrGetCount()`, `modmgrGetMod(index)`, `modmgrFindMod(id)`

**Enable/disable:** `modmgrSetEnabled(index, bool)`, `modmgrIsDirty()`, `modmgrApplyChanges()` (save + reload + return to title)

**Config persistence:** `modmgrSaveConfig()`, `modmgrLoadConfig()`, `modmgrSaveComponentState()`, `modmgrLoadComponentState()` (state persisted to mods/.modstate)

**Network:** `modmgrGetManifestHash()` (CRC32 of enabled mod IDs+versions), `modmgrWriteManifest()`, `modmgrReadManifest()`

**Filesystem:** `modmgrResolvePath(relPath)` (resolve through enabled mods in load order), `modmgrGetModDir(index)`

**Dynamic asset table accessors (catalog-backed):**
- `modmgrGetTotalBodies()` / `modmgrGetBody(index)`
- `modmgrGetTotalHeads()` / `modmgrGetHead(index)`
- `modmgrGetTotalArenas()` / `modmgrGetArena(index)`
- `modmgrGetModsDir()`, `modmgrCatalogChanged()` (signal catalog mutations, rebuild caches lazily)

---

### Texture & Animation Intercepts (`mod.c`)

**C-5 texture intercept:**
- `modTextureLoad(num, dst, dstSize)` — checks g_NotLoadMod flag; calls `catalogResolveTexture()`; falls back to legacy textures/ directory

**C-6 animation intercept:**
- `modAnimationLoadData(num)` — calls `catalogResolveAnim()`; loads from animations/ directory; fatal if no data file found
- `modAnimationLoadDescriptor(num, animtableentry)` — parses numframes, bytesperframe, headerlen, framelen, flags from .txt metadata

**Sequence loading (no catalog intercept):**
- `modSequenceLoad(num, outSize)` — legacy sequences/ directory loader

---

### Mod Pack System (`modpack.h/c`) — D3R-10

**PDPK container format:**
```
[4]  magic "PDPK" (0x4B504450 LE)
[4]  version 1
[4]  component_count
[4]  manifest_len
[manifest_len] INI-style manifest text
--- per component ---
[2] id_len, [id_len] id
[2] cat_len, [cat_len] category
[2] ver_len, [ver_len] version
[4] raw_size (uncompressed PDCA bytes)
[4] stored_size (bytes stored on disk)
[1] compression (0=raw, 1=zlib)
[stored_size] PDCA data
```

**PDCA archive format (inner container):**
```
u32 magic "PDCA"
u16 file_count
per file: u16 path_len, char path[], u32 data_len, u8 data[]
```

**API:**
- `modpackExport(component_ids[], count, name, author, version, output_path, error_buf)` — export catalog components to .pdpack
- `modpackReadManifest(pack_path, out)` — read manifest only (no extraction)
- `modpackValidate(pack_path, out)` — check for conflicts against active catalog
- `modpackImport(pack_path, session_only, out)` — import and hot-register; session_only=1 installs to mods/.temp/

---

### Arena Picker Integration (`pdgui_menu_room.cpp`)

- Catalog-driven: builds s_Arenas[256] from ASSET_ARENA iteration
- Arena names via `langGet(e->ext.arena.name_langid)`
- ImGui combo dropdown → updates g_MatchConfig.stagenum → `mainChangeToStage(stagenum)`
- Stagenum validation: clamps picker if stagenum not in arena list
- Logging: `"CATALOG: arena selected \"%s\" stagenum=0x%02x"`
- **Status: PRODUCTION-READY** — no outstanding TODOs in arena picker

---

## PART 2: TODOs, STUBS, DISABLED CODE, PLACEHOLDERS

### TODO Table

| # | File | Line | Text | What It Takes | Priority |
|---|------|------|------|---------------|----------|
| T-1 | assetcatalog_scanner.c | ~257 | Parse mode string for map.mode field | ~~Parse "mp\|solo\|coop" from INI key into e->ext.map.mode; currently hardcoded to 0~~ **DONE (S79)** — `parseModeString()` added; MAP_MODE_MP/SOLO/COOP bitmask constants added to assetcatalog.h | Nice-to-have |
| T-2 | assetcatalog_base.c | ~42 | Full MPWEAPON_* enumeration | ~~35 of ~35+ MPWEAPON_* constants registered~~ **DONE (S79)** — verified: active table in assetcatalog_base_extended.c covers all 47 MPWEAPON_* (0x01-0x2f); base.c table is dead code (not iterated), comment updated to clarify | Nice-to-have |
| T-3 | assetcatalog_base_extended.c | ~99 | S46b: enumerate full animation table from animations.json (~1000 entries) | Read animations.json for each ROM variant; register one entry per anim ID with frame/byte metadata | Important |
| T-4 | assetcatalog_base_extended.c | ~127 | S46b: enumerate base texture table from ROM metadata | Enumerate ROM texture descriptors; register ASSET_TEXTURE entries with width/height/format | Important |
| T-5 | assetcatalog_base_extended.c | ~231 | S46b: enumerate broader SFX table from sfx.h (1545 entries total) | Iterate sfx.h constants; register ASSET_AUDIO entries for all SFX IDs | Important |
| T-6 | modelcatalog.c | ~668 | Implement thumbnail render via charpreview FBO system | Render each body/head to 64×128 FBO; cache in ce->thumbnailTexId; used by agent select screen | Nice-to-have |
| T-7 | modmgr.c | ~606 | D3b: Parse mod.json content sections (bodies, heads, arenas) and register into catalog | Read mod.json bodies[]/heads[]/arenas[] arrays; call RegisterBody/Head/Arena per entry; this is the main gap for mod-supplied characters appearing in multiplayer | **CRITICAL** |
| T-8 | modmgr.c | ~618 | D3e: Restore pristine base stage table after mod reload | ~~On modmgrReload(), re-copy g_Stages[] from ROM snapshot before re-populating with mod overrides~~ **DONE (S78)** — `stageTableReset()` added to stagetable.c; called in `modmgrUnloadAllMods()` | Important |
| T-9 | modmgr.c | ~735 | D3e: Flush texture cache and return to title after mod reload | ~~Call textureCacheFlush() (or equivalent) before rebuilding catalog; missing step causes stale texture refs~~ **DONE (S78)** — `videoResetTextureCache()` + `mainChangeToStage(TITLE)` added at end of `modmgrReload()` | Important |
| T-10 | modmgr.c | ~958 | D3f: Add size_bytes to manifest for download estimation | Add per-component size_bytes field to manifest serialization; used for download progress bars | Nice-to-have |

---

### Stub / Partial Implementations

These are **intentional partial registrations** waiting for S46b expansion — marked in source with TODO comments:

| Location | What's Partial | Current State | Full State |
|----------|----------------|---------------|------------|
| assetcatalog_base_extended.c ~99 | Base animations | 10 representative entries (idle, walk, run, jump, fall, attack, fire, reload, death, pain) | ~1000 entries from animations.json |
| assetcatalog_base_extended.c ~127 | Base textures | 5 minimal placeholder entries | Full ROM texture metadata table |
| assetcatalog_base_extended.c ~231 | Base SFX/audio | 10 representative entries | 1545 entries from sfx.h |
| assetcatalog_base.c ~42 | Base weapons | 35 MPWEAPON_* entries | Confirm full table is covered |

---

### Disabled Code (`#if 0` blocks)

**None found** in any catalog or mod implementation file.

---

### C-4 / C-5 / C-6 / C-7 Intercept Status

| Intercept | Function | Status | Notes |
|-----------|----------|--------|-------|
| **C-4** File | `catalogResolveFile(filenum)` in romdata.c | ✅ LIVE | Reverse map s_FilenumOverride[2048]; logging active; query counter tracked |
| **C-5** Texture | `catalogResolveTexture(texnum)` in mod.c | ✅ LIVE | Reverse map s_TexnumOverride[4096]; falls back to legacy textures/ dir |
| **C-6** Animation | `catalogResolveAnim(animnum)` in mod.c | ✅ LIVE | Reverse map s_AnimnumOverride[2048]; falls back to legacy animations/ dir |
| **C-7** Sound | `catalogResolveSound(soundnum)` in snd.c | ✅ API READY | Reverse map s_SoundnumOverride[4096]; snd.c includes header and is ready; integration call site TBD |

C-7 is the only intercept not yet wired at the call site. The resolver and reverse map exist; snd.c includes the header. What remains is inserting the `catalogResolveSound()` call at the actual sound load point inside `snd.c`.

---

### Catalog Activation Plan Phase Status

| Phase | ID | Status |
|-------|----|--------|
| Core catalog hash table | D3R-1 | ✅ Complete |
| Asset entry pool | D3R-2 | ✅ Complete |
| Base game registration | D3R-3 | ✅ Complete (S46b expansions pending: T-3/T-4/T-5) |
| Component scanner + INI parser | D3R-4 | ✅ Complete |
| Mod stage file resolution | D3R-5 | ✅ Complete |
| Component-level enable state | D3R-6 | ✅ Complete |
| Reverse-index initialization | D3R-7 | ✅ Complete |
| Network manifest serialization | D3R-8 | 🟡 Partial — size_bytes missing (T-10) |
| Mod pack export/import (.pdpack) | D3R-10 | ✅ Complete |
| Mod manager foundation | D3a | ✅ Complete |
| mod.json content parsing (body/head/arena) | D3b | ❌ TODO — T-7 (critical) |
| Cache flush on reload | D3e | ✅ Complete — T-8, T-9 done (S78) |
| Manifest download estimation | D3f | ❌ TODO — T-10 (nice-to-have) |

---

## Summary

| Metric | Value |
|--------|-------|
| Asset types defined | 23 |
| Registration API functions | 17 |
| C-4/C-5/C-6/C-7 intercepts live | 3/4 (C-7 wiring pending) |
| Total TODO items | 10 |
| Critical TODOs | 1 (T-7: mod.json body/head/arena parsing) |
| Important TODOs | 5 (T-3/T-4/T-5 S46b expansions, T-8/T-9 reload cleanup) |
| Nice-to-have TODOs | 4 |
| `#if 0` disabled blocks | 0 |
| Empty stub functions | 0 |
| Island arena status | PRODUCTION-READY |
| MEM-1/MEM-2/MEM-3 lifecycle | ✅ All implemented |
| Stage diff / transition | ✅ Implemented |
| PDPK mod pack format | ✅ Implemented |
| ROM hash/integrity cache | ✅ Implemented |

**Overall system completeness: ~95%.** The one critical gap is T-7 (mod.json body/head/arena registration — mods cannot add custom characters or arenas to multiplayer until this is wired). All other gaps are expansions (S46b base-game table population) or polish (cache flush, manifest size, thumbnail render).
