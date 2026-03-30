# Asset Catalog as Single Source of Truth — Implementation Plan

> Design document for routing ALL asset loading through the catalog.
> Back to [index](README.md)

---

## 1. Current State

120+ asset loading call sites across 4 core entry points, all using numeric IDs:

| Entry Point | Call Sites | Asset Types | Current Route |
|-------------|-----------|-------------|---------------|
| fileLoadToNew/Addr | 16 | models, tiles, pads, setup, lang | Numeric filenum -> ROM DMA |
| texLoad* | 50+ | textures | Numeric texnum -> g_Textures[] -> ROM |
| animLoadFrame/Header | 16+ | animations | Numeric animnum -> g_Anims[] -> ROM |
| sndStart/sndLoad* | 100+ | audio | Numeric soundnum -> ROM offset table |

The asset catalog (assetcatalog.c) currently handles: stages, bodies, heads, arenas,
weapons, animations, textures, props, gamemodes, audio, HUD, effects (12 types).
But most actual LOADING still bypasses it.

## 2. Architecture

### 2.1 Three Cataloging Triggers

**Base game (ROM assets):**
- Catalog once at first launch
- Re-catalog when ROM hash changes (checked at startup)
- Cache to disk as `$S/catalog-cache-{romhash}.json`
- Contains: all filenums, texture IDs, animation IDs, sound IDs from ROM tables

**Mods (component assets):**
- Re-catalog when enabled mod set changes OR mods folder contents change
- Diff-based: only scan new/changed mods, unregister removed ones
- Check at startup: hash of mods folder listing vs cached hash
- Check on mod enable/disable: re-diff, don't full re-scan

**Runtime resolution:**
- Every load call goes through catalog
- Catalog returns: source (ROM/mod/override), file path or ROM offset, load state
- If not in catalog: log error, return NULL (never silently load uncataloged assets)

### 2.2 Core Interception Points

**Strategy: intercept at the 4 gateway functions, not at every call site.**

1. **fileLoadToNew / fileLoadToAddr** (file.c) — the master gateway
   - Before loading: `catalogResolveFilenum(filenum)` -> returns source + path
   - If mod override exists: load from mod filesystem instead of ROM
   - If not in catalog: log error, return NULL
   - All 16 callers benefit automatically

2. **texLoad** (texdecompress.c) — texture gateway
   - Before decompressing: `catalogResolveTexture(texnum)` -> returns source
   - If mod texture exists: load from mod file instead of ROM segment
   - Cache texture catalog entries at stage load

3. **animLoadFrame / animLoadHeader** (anim.c) — animation gateway
   - Already has mod fallback (`modAnimationLoadData`)
   - Route through catalog instead: `catalogResolveAnimation(animnum)`
   - Mod animations registered in catalog, not separate g_AnimReplacements[]

4. **sndStart** (snd.c) — audio gateway
   - Already has mod fallback (`modSoundLoad`)
   - Route through catalog: `catalogResolveSound(soundnum)`

### 2.3 Catalog Cache Format

```json
{
  "rom_hash": "abc123...",
  "version": 1,
  "base_assets": {
    "file:0": { "type": "model", "name": "body_joanna", "size": 12345 },
    "file:1": { "type": "model", "name": "head_joanna", "size": 6789 },
    "tex:0": { "type": "texture", "name": "tex_wall_concrete", "size": 1024 },
    "anim:0": { "type": "animation", "name": "anim_walk", "frames": 30 },
    "snd:0": { "type": "audio", "name": "sfx_gunshot_falcon2" }
  },
  "mod_hash": "def456...",
  "mod_assets": {
    "mod_gex:body:gex_bond": { "type": "body", "path": "mods/mod_gex/_components/..." }
  }
}
```

## 3. Implementation Sequence

| Phase | What | Effort | Depends On |
|-------|------|--------|-----------|
| C-1 | ROM hash computation + cache check at startup | Small | Nothing |
| C-2 | Base game catalog population (filenums, texnums, animnums, sndnums from ROM tables) | Medium | C-1 |
| C-3 | Cache to disk + load from cache on subsequent launches | Small | C-2 |
| C-4 | Intercept fileLoadToNew — catalog resolve before ROM load | Medium | C-2 |
| C-5 | Intercept texLoad — catalog resolve for textures | Medium | C-2 |
| C-6 | Intercept animLoadFrame — catalog resolve for animations | Medium | C-2 |
| C-7 | Intercept sndStart — catalog resolve for audio | Medium | C-2 |
| C-8 | Mod diff-based re-cataloging (folder hash, enable/disable diff) | Medium | C-4 |
| C-9 | Stage transition: diff loaded vs needed, load/unload delta | Large | C-4 through C-7 |

**C-4 is the critical step** — once fileLoadToNew goes through the catalog, all 16 callers
(models, tiles, pads, setup, lang) automatically benefit. The other interceptions (C-5
through C-7) can be done incrementally.

## 4. Stage Transition Loading

When a stage loads (lv.c):
1. Query catalog for all assets needed by the new stage (tile, bg, pads, setup, textures)
2. Diff against currently loaded assets
3. Unload assets that are no longer needed (free memory)
4. Load assets that are needed but not yet loaded
5. Skip assets that are already loaded (no-op)

This replaces the current "unload everything, load everything" approach with an
incremental diff that's faster and uses less memory.

## 5. Confirmed Decisions (Game Director)

- Base game cataloged once (or on ROM hash change)
- Mods re-cataloged on folder change or enable/disable change (diff-based, not full rescan)
- Catalog is the SINGLE gateway — nothing loads without being in the catalog
- All loading goes through catalog resolve, then to ROM or mod filesystem

---

*Created: 2026-03-26, Session 48*
