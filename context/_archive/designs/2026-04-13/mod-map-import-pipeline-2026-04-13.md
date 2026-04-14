# Mod Map Import Pipeline

> **Created**: 2026-04-13, Session S226
> **Status**: Design (no code changes)
> **Scope**: Pipeline for converting foreign map formats into valid PD2 catalog entries that load, spawn players/bots, and play matches without failure.
> **Depends on**: [spawn-system-architecture-2026-04-13.md](spawn-system-architecture-2026-04-13.md) for spawn point generation.

---

## 1. Foreign Format Survey

### 1.1 Formats In Scope

PD2 is built on the Perfect Dark decompilation, which already has four integrated mods (AllInOneMods) plus supports community map work. The realistic import targets are:

| Format | Source | Structure | Spawn Points? | Navmesh? | Maps to PD2? |
|---|---|---|---|---|---|
| **PD ROM setup files** | Base game SP missions | Binary setup file + pad file + BG geo | Sometimes (INTROCMD_SPAWN) | Waypoints in setup | Direct -- these ARE the native format |
| **GoldenEye 007 ROM setups** | GEX mod (already integrated) | Same binary format as PD (shared engine heritage) | Usually yes (GE spawn commands) | Waypoints | Direct -- GEX already handles this via its setup files |
| **AllInOneMods maps** | Kakariko, Dark Noon, GF64 | PD setup format with custom BG data, pads, textures | Varies by map | Varies | Direct -- already loaded as mod stages via g_Stages[] |
| **Community PD map packs** | Fan-made maps using PD level editors | PD setup + pad + BG format (same as base) | Should have INTROCMD_SPAWN | Should have waypoints | Direct -- same format, just different content |
| **Converted GE maps (external tools)** | Maps ported from GoldenEye using community tools | PD-format output from SubDrag's tools or equivalent | Typically missing | Typically missing | Needs spawn generation (Problem A) |
| **Raw geometry imports** | Future: OBJ/FBX -> PD collision tool | Vertex/triangle mesh, no game metadata | No | No | Needs full metadata generation |

### 1.2 Key Insight: PD2's Native Format IS the Import Target

Unlike a game with a proprietary compiled map format, PD2 loads maps from a well-understood decompiled structure:
- **BG file**: Room geometry, portals, collision data (binary, loaded by `bgReset()`)
- **Pad file**: Named locations in the level, each with position + room + flags (binary, loaded into `g_PadsFile`)
- **Setup file**: Entity placement -- props, characters, spawn commands, weapons, intro sequences (binary command stream, parsed by `playerReset()` and `setupCreateProps()`)
- **Waypoint data**: AI navigation graph embedded in setup (array of `struct waypoint`, each with padnum + neighbors)

All existing mod maps (GEX, Kakariko, Dark Noon, GF64) already use this format. The real import problem is:
1. Maps that have the geometry but are missing spawn/navmesh metadata
2. Maps from external conversion tools that produce valid BG data but incomplete setup files
3. Future maps from new level editors

### 1.3 Formats Explicitly Out of Scope (v1.0)

- Quake .bsp / Source .vmf / Unreal .umap: These would require a full geometry conversion pipeline. Massive scope. Deferred to post-1.0.
- Minecraft world data: Voxel-to-mesh conversion. Interesting but orthogonal.
- GoldenEye N64 ROM extraction: GEX already handles this. If someone has a new GE map, they port it with existing GE tools first, then PD2 imports the result.

---

## 2. Pipeline Stages

### 2.1 Overview

```
Source file(s)                     Output
     |                               ^
     v                               |
[1. PARSE] --> [2. NORMALIZE] --> [3. GENERATE] --> [4. EMIT] --> [5. VALIDATE] --> [6. REGISTER]
     |              |                  |                |              |                  |
  Read source   Map to PD2        Fill missing      Write to       Smoke test       Add to
  format        internal repr     metadata           disk           the map          catalog
```

### 2.2 Stage 1: PARSE -- Read Source Files

**Input**: Directory containing map files (BG, pads, setup, textures, etc.)

**For PD-native format** (all current cases):
- Verify BG file exists and has valid header (magic bytes, room count > 0)
- Verify pad file exists and has `numpads > 0`
- Check for setup file; if absent, create a minimal one (see Stage 3)
- Check for texture data; if absent, map uses base-game textures only
- Record file sizes, checksums

**For converted maps** (future external-tool output):
- Same as above, but expect incomplete metadata
- Log what's missing for Stage 3

**Output**: `import_context_t` struct with pointers to parsed data and a bitmask of what's present/missing.

```c
typedef struct {
    char source_dir[FS_MAXPATH];
    char map_name[64];

    // What we found
    bool has_bg;          // BG geometry file
    bool has_pads;        // Pad location file
    bool has_setup;       // Setup/entity file
    bool has_waypoints;   // Navmesh/waypoint data
    bool has_spawns;      // INTROCMD_SPAWN entries in setup
    bool has_textures;    // Custom texture data
    bool has_modjson;     // mod.json manifest

    // Parsed counts
    s32 num_rooms;
    s32 num_pads;
    s32 num_waypoints;
    s32 num_spawn_cmds;

    // File references
    char bg_path[FS_MAXPATH];
    char pad_path[FS_MAXPATH];
    char setup_path[FS_MAXPATH];

    // Error state
    bool parse_ok;
    char error[256];
} import_context_t;
```

### 2.3 Stage 2: NORMALIZE -- Map to PD2 Internal Representation

**For PD-native maps**: Mostly a pass-through. Verify:
- Pad file format version compatibility
- BG room count within limits (current max: ~200 rooms in largest base maps)
- Setup command stream doesn't contain unrecognized command types (log warnings, don't fail)
- Waypoint neighbor indices are within bounds

**Normalization actions**:
- If map uses legacy mod identifiers (g_ModNum-era), strip them. Map is identified solely by catalog ID.
- If pad positions use coordinates outside reasonable bounds (> 50000 units from origin), warn but don't reject. Some creative maps are large.
- Compute map AABB from room data for spawn generation metadata.

### 2.4 Stage 3: GENERATE -- Fill Missing Metadata

This is where Problem A (spawn system) connects to Problem B (import pipeline).

**Missing spawn points**: If `has_spawns == false`:
1. **At import time**: Run L2 (waypoint sampling) and L3 (grid sampling) from the spawn system architecture to pre-compute a set of spawn locations.
2. **Emit as INTROCMD_SPAWN entries**: Write synthetic spawn commands into the setup file (or a supplementary setup overlay) so that the standard playerReset() path picks them up at match-load time.
3. **Why pre-compute at import vs match-load**: Pre-computing at import ensures the map is known-good before the player ever selects it. The spawn points are baked into the setup file, so the existing L1 path handles them at runtime. L4 (radial fallback) remains available at match-load as the hard guarantee, but should never be needed for properly imported maps.

**Missing waypoints/navmesh**: If `has_waypoints == false`:
1. Generate a basic waypoint graph from the pad file: each pad with a valid room becomes a waypoint. Neighbors are determined by room adjacency (using portal data from BG).
2. This produces a functional but non-optimal navmesh. Bots can navigate but won't path as well as hand-authored maps.
3. Write generated waypoints into the setup file.

**Missing setup file entirely**: If `has_setup == false`:
1. Generate a minimal setup file containing:
   - Generated INTROCMD_SPAWN entries (from above)
   - Default weapon set (INTROCMD_WEAPON for Falcon 2, Laptop Gun, Dragon, etc.)
   - INTROCMD_END
2. No characters, no props, no scripted events. The map is playable in MP only.

**Missing mod.json**: If `has_modjson == false`:
1. Generate a mod.json manifest:
   ```json
   {
     "id": "imported:<map_name>",
     "name": "<Map Name> (Imported)",
     "version": "1.0.0",
     "author": "Unknown (Imported)",
     "description": "Imported map - <source_info>",
     "base_fallback": "base:area52",
     "content": {
       "arenas": [{
         "id": "imported:<map_name>",
         "name": "<Map Name>",
         "stagenum": "<auto-assigned>",
         "mode": 1
       }]
     }
   }
   ```

### 2.5 Stage 4: EMIT -- Write to Disk

**Output directory**: `mods/imported_<map_name>/`

**Files written**:
- `mod.json` (generated or validated original)
- BG file (copied or linked from source)
- Pad file (copied, possibly augmented with generated pads)
- Setup file (original + synthetic spawn/waypoint commands, or fully generated)
- Texture files (copied from source if present)
- `import_metadata.json` (diagnostic: what was generated, source checksums, import timestamp)

**Atomic write**: Write to a temp directory first, then rename. If any step fails, the temp directory is deleted and no partial import is left behind.

### 2.6 Stage 5: VALIDATE -- Smoke Test

Every imported map must pass a smoke test before being available to players. The smoke test runs in-engine (not a separate tool) and verifies:

| Test | Method | Pass Criteria |
|---|---|---|
| **Load test** | Call `bgReset(stagenum)` with the imported BG | No crash, rooms loaded > 0 |
| **Pad test** | Load pad file, iterate all pads | At least 1 pad with valid room |
| **Spawn pool test** | Run `spawnPoolBuild()` with needed=8 (4P + 4B) | Pool count >= 8 |
| **Collision test** | `cdFindGroundInfoAtCyl()` at each spawn point | All return valid ground Y |
| **Room traversal test** | `bgFindRoomsByPos()` at each spawn point | All resolve to valid rooms |
| **30-second match simulation** | Start a headless 4P+4B match, tick for 30s (1800 frames) | No crash, no SIGABRT, all participants have valid rooms[0] at end |

**On failure**: Import is rejected. The error is reported to the user with:
- Which test failed
- Specific coordinates/values that failed
- Suggested remediation (e.g., "Map has no collision data below spawn points. The BG file may be incomplete.")

The imported directory is NOT deleted on failure -- it's moved to `mods/_failed_imports/` with the error log, so the user can inspect and retry.

### 2.7 Stage 6: REGISTER -- Add to Catalog

On smoke test success:
1. Call `modmgrReload()` or equivalent to re-scan the mods directory
2. The new `mod.json` is picked up by `modmgrScanDirectory()`
3. The arena is registered in the Asset Catalog via `catalogRegisterArena()`
4. The map appears in the Combat Simulator stage select
5. `modmgrSaveConfig()` persists the enabled state

---

## 3. Spawn Point Generation at Import vs Match-Load

### 3.1 Recommendation: Pre-compute at Import, Verify at Match-Load

| Concern | Import-time | Match-load-time |
|---|---|---|
| **User experience** | Map is known-good before selection | Player might hit spawn failure in-game |
| **Performance** | One-time cost at import | Cost every match start |
| **Determinism** | Baked into setup file, always consistent | Depends on match_seed |
| **Flexibility** | Fixed spawn set | Can adapt to player count |

**Hybrid approach** (recommended):
- **Import-time**: Run L2+L3 to generate 24 spawn points. Bake into setup file as INTROCMD_SPAWN.
- **Match-load-time**: If the baked set has fewer than `needed` points (e.g., 36-player match on a map that was imported with 24), the runtime L2-L4 pipeline supplements.
- **Net effect**: Imported maps always have at least 24 declared spawns (L1 path at runtime). The full L1-L4 pipeline is still available as a safety net.

---

## 4. Error Handling

### 4.1 Error Taxonomy

| Error Class | Example | User Message | Recovery |
|---|---|---|---|
| **Missing required file** | No BG geometry file | "Import failed: No map geometry file found in <dir>. Expected a .bg or .bin file." | User provides correct file |
| **Corrupt file** | BG header has invalid magic | "Import failed: Map geometry file is corrupt or in an unsupported format." | User re-exports from editor |
| **Empty map** | BG has 0 rooms | "Import failed: Map contains no rooms. The geometry file may be empty." | User fixes in editor |
| **No valid pads** | Pad file exists but all rooms < 0 | "Warning: All map locations have invalid rooms. Spawn points were generated from geometry bounds." | Auto-remediated by L3/L4 |
| **Smoke test crash** | SIGSEGV during load test | "Import failed: Map crashed during test load. This usually means the collision data is incompatible." | User re-exports with correct settings |
| **Spawn validation fail** | All generated spawns failed ground check | "Warning: No walkable surfaces found. Spawns placed at map center. Map may have rendering-only geometry with no collision." | L4 guarantees spawns exist; warn user about quality |
| **Duplicate ID** | Map with same catalog ID already registered | "A map with ID '<id>' already exists. Overwrite?" | User confirms or renames |

### 4.2 Error Reporting

All errors are:
1. **Logged** via `sysLogPrintf(LOG_WARNING, ...)` to the game log
2. **Stored** in `import_metadata.json` in the import directory
3. **Displayed** to the user in the import UI dialog with actionable text
4. **Never silent** -- every import attempt produces either a success confirmation or a visible error

---

## 5. UI Surface

### 5.1 Import Location

**Primary**: Dev Window > Modding Hub > "Import Map" button

Opens a file browser dialog (SDL file dialog or ImGui FileBrowser) for selecting the source directory. After selection:
1. Parse stage runs, progress bar shows
2. If warnings/errors, display them in a summary dialog before proceeding
3. If smoke test passes, confirmation dialog: "Map '<name>' imported successfully. Available in Combat Simulator."
4. If smoke test fails, error dialog with details

**Secondary**: Drag-and-drop support in the Modding Hub (future)

### 5.2 Import Status in Modding Hub

Imported maps appear in the Modding Hub with an "Imported" tag (similar to template mod tags from S196). The tag is set automatically based on the `imported:` namespace prefix in the catalog ID.

The Modding Hub shows:
- Map name, source info, import date
- Which metadata was auto-generated (spawn points, navmesh, etc.)
- Option to re-import (re-run pipeline from source)
- Option to delete (removes from mods/ and catalog)

---

## 6. Catalog Entry Schema for Imported Maps

An imported map produces a catalog entry of type `ASSET_ARENA` (same as base-game arenas and mod arenas):

```c
catalog_entry.id       = "imported:facility_port"
catalog_entry.type     = ASSET_ARENA
catalog_entry.name     = "Facility Port"
catalog_entry.bundled  = false
catalog_entry.enabled  = true
catalog_entry.ext.arena = {
    .stagenum      = <auto-assigned from available range>,
    .mode          = MAP_MODE_MP,
    .bg_path       = "mods/imported_facility_port/facility.bg",
    .pad_path      = "mods/imported_facility_port/facility.pad",
    .setup_path    = "mods/imported_facility_port/facility.setup",
}
```

The `stagenum` assignment uses the mod stage range (0x61-0x86, indices 61-86 in `g_Stages[]`). The existing `modmgrRegisterContent()` / `catalogRegisterArena()` path handles this.

---

## 7. Key Files (Implementation Targets)

| File | Role | Changes Needed |
|---|---|---|
| **NEW**: `port/src/mapimport.c` | Import pipeline: parse, normalize, generate, emit | New file, ~600 lines C |
| **NEW**: `port/include/mapimport.h` | Public API | New file, ~40 lines |
| `port/src/modmgr.c` | Re-scan after import, detect imported: namespace | Minor additions to modmgrScanDirectory |
| `port/fast3d/pdgui_menu_moddinghub.cpp` | "Import Map" button + UI flow | Add import dialog, ~100 lines |
| `src/game/spawnpool.c` (from spawn arch) | Called by import pipeline for spawn generation | Cross-dependency |
| `port/src/assetcatalog_base.c` | Register imported arenas | May need minor changes for imported: namespace |

---

## 8. Implementation Sequence

1. **spawnpool.c** first (Problem A) -- this is required by the import pipeline
2. **mapimport.c** parse + normalize + emit stages
3. **mapimport.c** generate stage (calls spawnpool for spawn generation)
4. **mapimport.c** validate stage (smoke test)
5. **Modding Hub UI** for import trigger
6. **Integration testing** with real mod maps (GEX, Kakariko, etc. -- verify they pass the smoke test retroactively)
