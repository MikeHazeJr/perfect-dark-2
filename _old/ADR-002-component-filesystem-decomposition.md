# ADR-002: Component Filesystem Decomposition (D3R-1)

**Date**: 2026-03-23
**Status**: Proposed
**Scope**: Convert 5 bundled monolithic mods to component-based filesystem layout
**Depends on**: Nothing (first step in D3R chain)
**Blocks**: D3R-4 (scanner + loader), D3R-5 (callsite migration)
**Design doc**: [component-mod-architecture.md](component-mod-architecture.md) §3, §11

---

## Context

The current mod system stores each mod as a monolithic directory (`build/client/mods/mod_*/`) containing a flat pool of replacement files plus a `modconfig.txt` that patches `g_Stages[]` directly during load. Five bundled mods ship this way: `allinone`, `gex`, `kakariko`, `dark_noon`, `goldfinger_64`.

This monolithic layout has several problems that D3R is designed to solve:

1. **No per-asset granularity.** Enabling "goldfinger_64" enables all its maps, characters, textures, and props as a unit. Users can't enable the Temple map without also getting every GF64 character.

2. **File-override resolution is fragile.** `modmgrResolvePath()` iterates enabled mods in registry order, returning the first hit. Two mods providing the same filename create silent conflicts with no user visibility.

3. **Stage registration is a side effect of parsing.** `modConfigLoad()` directly mutates `g_Stages[]` — a global array — during load. The stage's identity is its array index, which shifts when mods change. Root cause of B-17 (wrong maps loading) and B-13 class (index-shift bugs).

4. **No metadata for individual assets.** The mod-level `mod.json` knows how many bodies/heads/arenas exist but not their names, scales, dependencies, or descriptions.

D3R-1 restructures the filesystem so every asset is an independent folder with a self-describing `.ini`, before any code changes.

---

## Decision

### What we're doing

Convert the 5 bundled mods from monolithic directories into the component filesystem layout defined in the architecture doc (§3). Each asset becomes its own folder under `mods/{category}/{asset_id}/` with an `{type}.ini` manifest.

This is a **filesystem-only** change. No game code is modified in D3R-1. The existing `modmgr.c` / `mod.c` system continues to function via a compatibility bridge (see Option A below).

### Layout

```
mods/
├── maps/
│   ├── gf64_temple/
│   │   ├── map.ini
│   │   └── files/           ← stage data files moved here
│   ├── gf64_dam/
│   ├── kakariko_village/
│   └── darknoon_saloon/
├── characters/
│   ├── gf64_bond/
│   │   ├── character.ini
│   │   └── files/           ← body/head model files
│   └── gf64_natalya/
├── skins/                   ← empty initially (future content)
├── bot_variants/            ← empty initially
├── textures/
│   ├── gf64_textures/
│   │   ├── textures.ini
│   │   └── files/           ← shared texture files
│   └── gex_textures/
└── .temp/                   ← session-only downloads (future)
```

### INI format

Per the architecture doc §4. Each `.ini` has a typed section header (`[map]`, `[character]`, `[skin]`, etc.) and common fields (`name`, `category`, `description`, `author`, `version`, `model_scale`, `depends_on`, `enabled`). Unknown fields are preserved.

Maps additionally carry `stagenum`, `mode`, and `depends_on` for shared texture packs. Characters carry `bodyfile` and `headfile` relative paths.

### Bundled flag

All converted components include `bundled = true` in their `.ini`. This prevents accidental deletion in the future mod manager and allows the UI to distinguish shipped content from user-installed content.

---

## Options Considered

### Option A: Component layout + shim loader (Recommended)

Decompose the filesystem now. Write a minimal **shim** in `modmgr.c` that scans the new `mods/{category}/` layout and synthesizes the old `modinfo_t` registry entries + shadow arrays so the existing load path still works. The game doesn't know the filesystem changed.

**Pros**: Immediate testability — the game still boots and loads mods through the existing code path. No risk of breaking the build. The shim is throwaway code (replaced by D3R-4's proper scanner), but it validates the filesystem layout with real gameplay before building the full catalog.

**Cons**: Throwaway shim code (~150-200 LOC). Two scanning codepaths briefly coexist.

### Option B: Component layout, defer loading until D3R-4

Decompose the filesystem. Don't write a shim. Mods simply don't load until the scanner (D3R-4) is built.

**Pros**: No throwaway code.

**Cons**: The game runs without mods for potentially multiple sessions. Can't validate the filesystem layout against real gameplay. Riskier — layout mistakes discovered late require rework of both files and the scanner.

### Option C: Keep monolithic layout, build catalog first

Skip D3R-1 entirely. Build the Asset Catalog (D3R-2) and scanner (D3R-4) against the existing monolithic layout, then restructure the filesystem last.

**Pros**: No throwaway work at all.

**Cons**: The catalog and scanner would be built against a layout we know we're abandoning. Every design decision during D3R-2/3/4 would need to account for "this is temporary." Violates the principle of establishing the target data model first.

### Decision: Option A

The shim is cheap (~150-200 LOC, confined to `modmgr.c`) and buys us validated data before we build the catalog on top of it. Mike can play-test the decomposed mods immediately.

---

## Conversion Process

### Step 1: Audit existing modconfig.txt files

Parse each bundled mod's `modconfig.txt` to extract:
- Stage definitions (stagenum, file references, music, weather, allocation)
- Body/head registrations (file numbers, scale values)
- Arena registrations (stagenum, name)
- Shared file pools (textures, props used across multiple stages)

Cross-reference with `mod.json` for metadata (name, version, author, description).

### Step 2: Identify shared resources

Some mods share texture files across multiple maps. These become `textures/{pack_id}/` components referenced via `depends_on` in the map `.ini`. Specifically:
- GF64 has a shared texture pack used by Temple, Dam, and other maps
- GEX has shared textures across its stages
- Other mods (kakariko, dark_noon) are likely self-contained

### Step 3: Create component folders

For each asset, create the folder under `mods/{category}/{asset_id}/`:
- Move data files into a `files/` subdirectory within the component folder
- Generate the `.ini` from modconfig.txt fields + mod.json metadata
- Set `bundled = true`

### Step 4: Write compatibility shim

Extend `modmgrScanDirectory()` to recognize the component layout:
- If `mods/{category}/` directories exist, scan them instead of `mods/mod_*/`
- Parse each `.ini` to populate the existing `modinfo_t` registry and shadow arrays
- Map `.ini` fields back to `modconfig.txt` semantics for the existing loader

### Step 5: Document the conversion

Write `docs/MOD_CONVERSION_GUIDE.md` with:
- Field mapping table (modconfig.txt field → `.ini` field)
- Step-by-step walkthrough for converting a legacy mod
- Gotchas (shared textures, file number vs. filename references, allocation values)

### Step 6: Build test

Mike builds and tests: all 5 mods load correctly, stages play, characters appear, textures render. Same behavior as before the restructure.

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| File path breakage during move | Mods don't load | Shim validates paths; old directories kept as backup until verified |
| Shared textures misidentified | Texture fallback to base game (visual regression) | Audit file references in modconfig.txt before decomposing |
| modconfig.txt fields not fully captured in .ini | Lost stage configuration (music, weather, allocation) | Full field audit in Step 1; `hint_memory` preserves allocation as advisory |
| Shim complexity exceeds estimate | Schedule slip | Shim only needs to map .ini → existing structures; no new behavior |

---

## Success Criteria

1. All 5 bundled mods decomposed into component folders with `.ini` manifests
2. Game boots and loads all mod content through the shim
3. No gameplay regression vs. monolithic layout (same stages, characters, textures)
4. `docs/MOD_CONVERSION_GUIDE.md` exists with complete field mapping
5. No references to old `mods/mod_*/` layout remain in the build output

---

## Relationship to Other Work

- **D3R-2 (Asset Catalog)** can begin in parallel — it doesn't depend on the filesystem being restructured, only on the `.ini` format being finalized (which this ADR does).
- **D3R-4 (Scanner + Loader)** replaces the shim from this ADR. The shim is explicitly throwaway.
- **B-17 (wrong maps loading)** is a symptom of the monolithic stage-patching approach. D3R-1's `.ini`-per-map with explicit `stagenum` doesn't fix B-17 directly, but establishes the data model that D3R-5 (callsite migration) uses to eliminate the root cause.
