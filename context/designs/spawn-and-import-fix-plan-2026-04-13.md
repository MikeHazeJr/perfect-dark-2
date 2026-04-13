# Implementation Punch List: Universal Spawn + Map Import

> **Created**: 2026-04-13, Session S226
> **Status**: Implementation plan (no code changes in this document)
> **Dependencies**: Build on designs in:
> - [spawn-system-architecture-2026-04-13.md](spawn-system-architecture-2026-04-13.md)
> - [mod-map-import-pipeline-2026-04-13.md](mod-map-import-pipeline-2026-04-13.md)

---

## Ordering Rationale

L4 first because it is the hard floor -- the guarantee that spawning never fails. Then L3 (grid sampling, depends on AABB infrastructure built for L4). Then L2 improvements (validation pipeline, deterministic seeding). Then the import pipeline that consumes all of them. Each step is independently testable and mergeable.

---

## Phase 1: L4 -- Ultimate Fallback (Radial Spawns)

**Goal**: Guarantee that `spawnPoolBuild()` ALWAYS returns >= needed points, even on a completely degenerate map.

### Tasks

| # | Task | File(s) | Est. Lines | Depends On |
|---|---|---|---|---|
| 1.1 | Create `src/game/spawnpool.c` and `src/include/game/spawnpool.h` with type definitions (`spawn_point_t`, `spawn_pool_t`) | NEW files | ~80 | -- |
| 1.2 | Implement `spawnPoolComputeAABB()` -- iterate `g_BgRooms[]` to compute stage bounding box. Fallback: if no rooms, AABB = (0,0,0)-(1000,1000,1000). | spawnpool.c | ~40 | 1.1 |
| 1.3 | Implement `spawnPoolL4Radial()` -- center + radial offsets with ground resolution attempt. Unconditionally produces `needed` points. | spawnpool.c | ~60 | 1.2 |
| 1.4 | Implement `spawnPoolBuild()` stub that calls L4 only (L1-L3 are pass-through for now). | spawnpool.c | ~30 | 1.3 |
| 1.5 | Wire `spawnPoolBuild()` into `playerreset.c` -- after existing spawn population, if `g_NumSpawnPoints < needed`, call L4 to fill the gap. | playerreset.c | ~20 | 1.4 |
| 1.6 | Expand `g_SpawnPoints[24]` to `g_SpawnPoints[MAX_MPCHRS]` (36). Update the `< 24` guard in playerreset.c:187. | player.c, playerreset.c | ~5 | -- |
| 1.7 | Add `match_seed` field to `SVC_STAGE_START` message. Server generates seed, clients store it. | netmsg.c, net.h | ~15 | -- |
| 1.8 | **Test**: Load a map with zero pads (create a test case or use a known-empty mod setup). Verify 8 spawns are generated, no crash, all players have valid rooms. | -- | -- | 1.5 |

### Verification
- `sysLogPrintf` confirms L4 activation on zero-pad maps
- 4P+4B match on empty map completes 30s without crash
- All spawned entities have `rooms[0] >= 0`

---

## Phase 2: L3 -- Grid Sampling on AABB Floor Plane

**Goal**: Better spawn quality than L4 by finding actual walkable surfaces.

### Tasks

| # | Task | File(s) | Est. Lines | Depends On |
|---|---|---|---|---|
| 2.1 | Implement `spawnPointValidate()` -- ground clearance, vertical clearance, room validity, not-inside-geometry, min spacing. | spawnpool.c | ~80 | 1.1 |
| 2.2 | Implement `spawnPoolL3Grid()` -- compute grid from AABB, raycast down at each grid point, validate, accept. | spawnpool.c | ~100 | 1.2, 2.1 |
| 2.3 | Integrate L3 into `spawnPoolBuild()` -- call after L1 (declared), before L4. | spawnpool.c | ~15 | 2.2 |
| 2.4 | Add adaptive `min_spacing` calculation based on AABB diagonal and needed count. | spawnpool.c | ~10 | 2.2 |
| 2.5 | **Test**: Load an SP mission map (e.g., Chicago) as MP arena with no INTROCMD_SPAWN. Verify L3 generates walkable spawns from room geometry. | -- | -- | 2.3 |

### Verification
- Spawns on Chicago/Pelagic/G5 (SP maps) are on walkable floors, not in walls or void
- Log shows "L3 grid: generated N points from M candidates"
- Compare spawn quality vs current waypoint-only fallback

---

## Phase 3: L2 Improvements -- Validation + Deterministic Seeding

**Goal**: Current waypoint sampling becomes robust and network-deterministic.

### Tasks

| # | Task | File(s) | Est. Lines | Depends On |
|---|---|---|---|---|
| 3.1 | Create `spawnPoolDeterministicRNG()` -- seeded PRNG (LCG or xorshift) using `hash(stage_id) ^ match_seed`. Separate from game RNG. | spawnpool.c | ~20 | 1.7 |
| 3.2 | Refactor existing waypoint sampling in `playerreset.c:282-381` into `spawnPoolL2Waypoints()` in spawnpool.c. Use deterministic RNG. Apply `spawnPointValidate()` to each candidate. | spawnpool.c, playerreset.c | ~120 (move + enhance) | 2.1, 3.1 |
| 3.3 | Integrate L2 into `spawnPoolBuild()` -- call after L1, before L3. | spawnpool.c | ~10 | 3.2 |
| 3.4 | Remove the old waypoint sampling code from `playerreset.c` (replaced by L2 in spawnpool.c). | playerreset.c | ~-100 (removal) | 3.3 |
| 3.5 | **Test**: Two clients connect to the same server. Both print their spawn pools. Verify pools are identical (deterministic). | -- | -- | 3.3, 1.7 |

### Verification
- Network test: two clients produce identical spawn pools from same match_seed
- Waypoint-based maps (all base MP arenas) still spawn correctly
- Log shows which layer provided each spawn point

---

## Phase 4: Spawn Selection Improvements

**Goal**: Better initial placement using farthest-point-first and team awareness.

### Tasks

| # | Task | File(s) | Est. Lines | Depends On |
|---|---|---|---|---|
| 4.1 | Implement `spawnPoolSelect()` -- farthest-from-occupied greedy assignment for FFA. | spawnpool.c | ~40 | 1.1 |
| 4.2 | Implement team-aware sector partitioning for team modes. | spawnpool.c | ~60 | 4.1 |
| 4.3 | Wire initial match spawn through `spawnPoolSelect()` instead of direct `scenarioChooseSpawnLocation()`. Respawns continue to use the existing enemy-distance algorithm. | playerreset.c, bot.c | ~30 | 4.1 |
| 4.4 | Preserve CTC's `spawnpadsperteam` as an L1 override for team-base spawning. | spawnpool.c | ~15 | 4.2 |
| 4.5 | **Test**: 4-team CTC on Felicity. Verify teams spawn near their bases. FFA on Complex: verify spread-out initial placement. | -- | -- | 4.3 |

---

## Phase 5: Map Import Pipeline

**Goal**: User can import a directory of map files into PD2's mod system and play matches on it.

### Tasks

| # | Task | File(s) | Est. Lines | Depends On |
|---|---|---|---|---|
| 5.1 | Create `port/src/mapimport.c` and `port/include/mapimport.h` with `import_context_t` struct and `mapImportParse()`. | NEW files | ~150 | -- |
| 5.2 | Implement `mapImportNormalize()` -- validate BG header, pad bounds, setup command stream. | mapimport.c | ~80 | 5.1 |
| 5.3 | Implement `mapImportGenerate()` -- call `spawnPoolBuild()` for spawn generation, generate minimal setup file if missing, generate mod.json. | mapimport.c | ~120 | 5.2, Phase 1-3 |
| 5.4 | Implement `mapImportEmit()` -- atomic directory write to `mods/imported_<name>/`. | mapimport.c | ~80 | 5.3 |
| 5.5 | Implement `mapImportValidate()` -- smoke test: load BG, verify pads, build spawn pool, collision test at each spawn, 30s headless match sim. | mapimport.c | ~150 | 5.4 |
| 5.6 | Implement `mapImportRegister()` -- trigger `modmgrReload()`, verify catalog entry created. | mapimport.c | ~30 | 5.5 |
| 5.7 | Wire import into `mapImport()` top-level function that runs all stages in sequence. | mapimport.c | ~40 | 5.1-5.6 |
| 5.8 | Error reporting: populate `import_context_t.error` at each stage, write `import_metadata.json`. | mapimport.c | ~40 | 5.7 |
| 5.9 | **Test**: Import a base-game SP map (Chicago) as an MP arena. Verify it appears in Combat Sim, spawns 4P+4B, plays 30s match. | -- | -- | 5.7 |

---

## Phase 6: Import UI

**Goal**: User-facing import interface in the Modding Hub.

### Tasks

| # | Task | File(s) | Est. Lines | Depends On |
|---|---|---|---|---|
| 6.1 | Add "Import Map" button to Modding Hub (`pdgui_menu_moddinghub.cpp`). | pdgui_menu_moddinghub.cpp | ~30 | Phase 5 |
| 6.2 | Implement import dialog: directory selection (SDL file dialog or ImGui path input), progress display, result summary. | pdgui_menu_moddinghub.cpp | ~100 | 6.1 |
| 6.3 | Add "Imported" tag display for maps with `imported:` namespace prefix. | pdgui_menu_moddinghub.cpp | ~20 | 6.1 |
| 6.4 | Add re-import and delete options for imported maps. | pdgui_menu_moddinghub.cpp | ~50 | 6.1 |
| 6.5 | **Test**: Full end-to-end: click Import, select directory, watch progress, see map in Combat Sim, play match. | -- | -- | 6.2 |

---

## Phase 7: Retroactive Validation

**Goal**: Verify all existing maps (base + mods) pass the new validation pipeline.

### Tasks

| # | Task | File(s) | Est. Lines | Depends On |
|---|---|---|---|---|
| 7.1 | Run smoke test on all 14 base MP arenas. Log results. | -- | -- | Phase 2 |
| 7.2 | Run smoke test on all GEX arenas (GoldenEye ports). | -- | -- | Phase 2 |
| 7.3 | Run smoke test on Kakariko, Dark Noon, GF64 arenas. | -- | -- | Phase 2 |
| 7.4 | Run smoke test on all SP missions promoted to MP. | -- | -- | Phase 2 |
| 7.5 | Fix any failures found. These are pre-existing spawn quality issues that the new system should handle gracefully. | Various | TBD | 7.1-7.4 |

---

## Summary of New/Changed Files

| File | Action | Est. Total Lines |
|---|---|---|
| `src/game/spawnpool.c` | NEW | ~400 |
| `src/include/game/spawnpool.h` | NEW | ~50 |
| `port/src/mapimport.c` | NEW | ~700 |
| `port/include/mapimport.h` | NEW | ~50 |
| `src/game/player.c` | MODIFY (expand array) | +12 |
| `src/game/playerreset.c` | MODIFY (wire spawnpool, remove old fallback) | -80, +40 |
| `src/game/bot.c` | MODIFY (use spawnpool for bot spawn) | +10 |
| `port/src/net/netmsg.c` | MODIFY (match_seed in SVC_STAGE_START) | +15 |
| `port/include/net/net.h` | MODIFY (match_seed field) | +2 |
| `port/fast3d/pdgui_menu_moddinghub.cpp` | MODIFY (import UI) | +200 |
| `CMakeLists.txt` | MODIFY (add new source files) | +2 |

**Total estimated new code**: ~1,200 lines (spawn system) + ~700 lines (import pipeline) + ~200 lines (UI) = ~2,100 lines across 7 phases.

---

## Risk Assessment

| Risk | Mitigation |
|---|---|
| L3 grid sampling is expensive on large maps | Budget: max 10,000 grid cells. Typical map: ~100x100 = 10,000. Runs once at import/match-start, not per-frame. |
| Deterministic RNG divergence between clients | Unit test: hash + seed -> pool comparison. Include pool checksum in SVC_STAGE_START for runtime verification. |
| Smoke test catches false positives (rejects valid maps) | Conservative thresholds. Log all rejections. User can force-import with a flag. |
| BG/pad format variations between PD versions | Parse defensively. Log warnings for unexpected values but don't reject unless structurally invalid. |
| L4 radial spawns produce bad gameplay (all in center) | L4 is the last resort. Any map with even basic geometry should resolve earlier. Log L4 activation prominently so it surfaces during testing. |
