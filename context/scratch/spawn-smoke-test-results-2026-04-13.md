# Spawn Pool Smoke Test — M-7.x Retroactive Validation
**Date:** 2026-04-13  
**Session:** gap-closure-smoke-test  
**Status:** Infrastructure DONE — runtime results accumulate on play

---

## What Was Built

`spawnPoolSmokeAll()` + `spawnPoolSmokeWriteCSV()` added to `spawnpool.c`.  
"Run All" button added to Modding Hub → Map Import tab.  
CSV output: `Build/smoke-test-results.csv`

### How It Works

| Source | Label | How triggered |
|--------|-------|---------------|
| Live gameplay | `L` | Automatic — logged every time `spawnPoolBuildGlobal()` runs |
| Offline sweep | `O` | "Run All" button — iterates catalog arenas with zeroed pads, seed `0x5EC0BE45` |

Live results overwrite offline results for the same stage.  
Offline sweep confirms L2-L4 fallback guarantee even with zero declared pads.

**CSV columns:** `stage_id, needed, produced, max_layer_used, source, time_ms, flag`  
**Flags:** `OK` (max_layer ≤ 2), `L3L4_RISK` (max_layer ≥ 3)

---

## Base Dark MP Arenas (13)

These have declared INTROCMD_SPAWN pads; expected max_layer = L1.  
Offline sweep (no pads loaded) will show L4 until visited in gameplay.

| stage_id | Name | Expected (live) | Risk |
|----------|------|-----------------|------|
| mp_skedar | Skedar (MP) | L1 | low |
| mp_pipes | Pipes | L1 | low |
| mp_ravine | Ravine | L1 | low |
| mp_g5building | G5 Building | L1 | low |
| mp_sewers | Sewers | L1 | low |
| mp_warehouse | Warehouse | L1 | low |
| mp_grid | Grid | L1 | low |
| mp_ruins | Ruins | L1 | low |
| mp_area52 | Area 52 | L1 | low |
| mp_base | Base | L1 | low |
| mp_fortress | Fortress | L1 | low |
| mp_villa | Villa (MP) | L1 | low |
| mp_carpark | Car Park | L1 | low |

## Classic MP Arenas (5)

| stage_id | Name | Expected (live) | Risk |
|----------|------|-----------------|------|
| mp_temple | Temple | L1 | low |
| mp_complex | Complex | L1 | low |
| mp_felicity | Felicity | L1 | low |
| mp_grid6 | Grid 6 | L1 | low |
| mp_grid2 | Grid 2 | L1 | low |

---

## Mod Stages (GEX / Kakariko / Goldfinger 64 / Dark Noon)

**Not yet imported** — no mod stages in catalog at time of this session.  
These stages would be registered via the Map Import Pipeline.  
Once imported, the "Run All" offline sweep will test them automatically.

**For any mod stage that hits L3 or L4 in the CSV:**
1. Open the mod's source directory
2. Verify `.pad` file is present and contains INTROCMD_SPAWN entries
3. If missing spawn pads: add a declared MP pad set to the mod's manifest
4. Re-run import and verify spawn pool drops to L1 or L2 on next playtest

---

## L3/L4 Offline Sweep Notes

The offline sweep calls `spawnPoolBuild()` with:
- `g_NumSpawnPoints = 0` (no declared pads)
- Empty room geometry (AABB invalid → fallback center `{0, 100, 0}`)
- Test seed `0x5EC0BE45`

Expected offline result for ALL stages: **max_layer = L4** (geometry not loaded).  
This confirms the L4 guarantee fires and produces `needed=40` points in every case.  
The `L3L4_RISK` flag in offline rows is expected and does NOT indicate a real problem.

**Only live (`L`) rows with `L3L4_RISK` are actionable.**  
A live L3/L4 hit means the stage lacks declared pads and the fallback system engaged during real gameplay.

---

## Instructions for Mike

To get real per-map results:
1. Launch game, visit each MP arena (quickmatch or arena selection)
2. After visiting all arenas, open Modding Hub → Map Import → click "Run All"
3. Check `Build/smoke-test-results.csv` — filter `source=L` rows
4. Any `L3L4_RISK` in live rows = that map needs more declared spawn pads

Expected: all 13 base Dark MP arenas show `L1` live.  
If any base arena shows `L3+` live, file a new bug with the stage name.
