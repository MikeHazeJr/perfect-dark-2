# Universal Spawn System Architecture

> **Created**: 2026-04-13, Session S226
> **Status**: Design (no code changes)
> **Scope**: Guarantee that N players + M bots can spawn on ANY map -- base MP, base SP promoted to MP, mod MP, mod SP, imported foreign maps -- even with zero declared spawn points.
> **Constraint**: Deterministic. Same seed = same spawn pool on all clients.

---

## 1. Current State Audit

### 1.1 Spawn Point Sources (How g_SpawnPoints Gets Populated)

The global array `g_SpawnPoints[24]` (declared `player.c:123`) holds pad numbers used as spawn locations. `g_NumSpawnPoints` (`player.c:124`) tracks the count. Population happens in `playerReset()` (`playerreset.c:149-405`) during stage load:

**Primary source -- INTROCMD_SPAWN** (`playerreset.c:186-189`):
The setup file's intro command stream is iterated. Each `INTROCMD_SPAWN` with `param2 == 0` contributes `param1` (a pad number) to `g_SpawnPoints[]`, up to 24 entries. This is how all base-game MP arenas define spawn points -- they have hand-placed spawn commands in their setup files.

**Fallback 1 -- Waypoint sampling** (`playerreset.c:282-381`):
If `g_NumSpawnPoints == 0` AND the match is multiplayer (network or local Combat Sim), the system samples from `g_StageSetup.waypoints` -- the AI navigation graph. Uses rejection sampling with adaptive spacing (500 -> 250 -> 125 -> 60 -> 0 units), selecting up to 24 random waypoint pads with valid rooms. Includes a collapse-detection safety: if all selected pads are the same padnum, it falls back to sequential waypoint iteration.

**Fallback 2 -- Sequential pad scan** (`playerreset.c:385-404`):
If waypoint sampling found zero valid points AND `g_PadsFile` exists, iterates all pads sequentially, accepting any pad with `room >= 0`, up to 24.

### 1.2 Spawn Selection (How a Location Is Chosen)

**`scenarioChooseSpawnLocation()`** (`scenarios.c:805-820`):
Entry point for all spawn location selection. Checks if the active scenario has a custom spawn function (e.g., CTC's `ctcChooseSpawnLocation` which biases spawns near team bases). If no custom function or not in MP, falls through to `playerChooseGeneralSpawnLocation()`.

**`playerChooseGeneralSpawnLocation()`** (`player.c:698-700`):
Thin wrapper: calls `playerChooseSpawnLocation()` with `g_SpawnPoints` and `g_NumSpawnPoints`.

**`playerChooseSpawnLocation()`** (`player.c:232-696`):
The core dispersal algorithm. Categorizes each pad as good/bad/verybad based on:
- Distance to enemy players and bots (squared distance comparison)
- Room visibility (is the pad's room on any player's screen or standby set?)
- Room neighbor arrays for bot proximity

Builds a shortlist of 4 pads using priority tiers:
1. Good pads > 10m from enemies, not visible
2. Bad-but-not-verybad pads > 5m from enemies
3. Any remaining pads (verybad included)
4. Full random if nothing qualifies

Anti-repeat: `s_LastSpawnPad` prevents the same pad twice in a row when the shortlist has >1 entry.

**Zero-pad fallback** (`player.c:245-311`):
If `numpads <= 0`, scans up to 64 pads for the first with a valid room, uses that pad's position, resolves room via `bgFindRoomsByPos()` if needed, probes 8 directions for walls, and faces away from the nearest wall. Logs a warning.

### 1.3 Where Spawns Happen

| Callsite | Who | When |
|---|---|---|
| `playerreset.c:560-570` | Human player | Initial match spawn |
| `player.c:742` (in `playerStartNewLife`) | Human player | Respawn after death |
| `bot.c:306` (in `botSpawn`) | Bot | Spawn/respawn |
| `bot.c:1098-1126` | All bots | Failsafe: detect bots with rooms[0]==-1, call `botSpawnAll()` |
| `capturethecase.inc:463-471` | CTC scenario | Team-biased spawn using `spawnpadsperteam` |

### 1.4 Known Failure Modes

| Failure | Root Cause | Current Mitigation | Residual Risk |
|---|---|---|---|
| **Zero spawn points** | SP map with no INTROCMD_SPAWN and no waypoints | Pad scan fallback + zero-pad fallback in playerChooseSpawnLocation | If g_PadsFile is NULL (no pad data at all), spawn pos = origin (0,0,0). room = -1. Crash in cdFindGroundInfoAtCyl. |
| **All pads invalid** | Every pad has room < 0 | Zero-pad fallback scans first 64, returns padnum 0 if none valid | Position may be in void. Room resolved via bgFindRoomsByPos; if that also fails, room stays -1, player is invisible. |
| **Single-pad collapse** | Waypoint sampling selects the same pad repeatedly | Collapse detection + sequential fallback | If the sequential fallback also yields 1 pad, all players spawn on top of each other. Functional but poor gameplay. |
| **Degenerate geometry** | Map with no floor meshes (e.g., space-themed map, broken import) | Zero-pad fallback probes walls, not floors | cdFindGroundInfoAtCyl returns garbage if no collision geo exists below the spawn point. Player falls forever. |
| **Bot spawn before stage ready** | SVC_BOT_AUTHORITY arrives before pads load | Deferred activation (g_NetPendingBotAuthority) + failsafe re-check | Works. No known residual. |

### 1.5 Architectural Gaps

1. **No guaranteed spawn count**: Nothing ensures N+M points exist. The system tries, but can end up with 0 or 1.
2. **No spawn point validation**: Pads accepted without checking if position is above a floor, inside geometry, or has enough clearance.
3. **No deterministic seed**: Waypoint sampling uses `rngRandom()` which is the shared game RNG. In MP, clients run this independently -- spawn pool construction is not synchronized. The server sends authoritative positions for bots (via CLC_BOT_MOVE/SVC_CHR_MOVE), but player spawns are computed locally and may diverge on edge cases.
4. **Hardcoded limit of 24**: `g_SpawnPoints[24]` caps spawn diversity. With MAX_MPCHRS=36, maps with many participants cycle through the same 24 points.

---

## 2. Proposed Layered Fallback Hierarchy

The design principle: **each layer is a complete spawn-point generator**. Layers are tried in order. A layer "succeeds" if it produces >= N+M valid spawn points (where N = players, M = bots). If a layer produces fewer, the next layer supplements. Layer 4 is the hard guarantee -- it always succeeds.

### 2.1 Layer 1: Declared Spawn Points (Existing)

**Source**: `INTROCMD_SPAWN` entries from the stage setup file.

**Behavior**: Unchanged from current system. Parse intro commands, collect pad numbers.

**Success condition**: `g_NumSpawnPoints >= needed` where `needed = PLAYERCOUNT() + g_BotCount`.

**Enhancement**: Expand `g_SpawnPoints[]` from 24 to `MAX_MPCHRS` (36) to handle full-capacity matches without recycling.

### 2.2 Layer 2: Geometry-Aware Sampling from Navmesh/Waypoints

**Source**: `g_StageSetup.waypoints` (AI navigation graph).

**Behavior**: Enhanced version of the current waypoint sampling. Changes:

1. **Deterministic RNG**: Use a separate PRNG seeded with `hash(stage_id) ^ match_seed`. The `match_seed` is generated by the server at match start and distributed via `SVC_STAGE_START`. All clients use the same seed, so spawn pools are identical.

2. **Validation pipeline**: Each candidate waypoint pad is validated before acceptance:
   - **Ground check**: `cdFindGroundInfoAtCyl(padpos, 30, padrooms, ...)` must return a finite ground Y. Reject if ground Y is more than 500 units below pad Y (likely a void drop).
   - **Clearance check**: `cdExamCylMove01(padpos, padpos_up, 30, padrooms, CDTYPE_BG, true, padpos.y + 180, padpos.y)` must NOT return COLLISION. Ensures 180 units (standing height) of vertical clearance.
   - **Not-inside-geometry check**: `bgFindRoomsByPos(padpos, inrooms, ...)` must yield a valid room. Pad's stored room must match or be a neighbor of the resolved room.
   - **Minimum spacing**: At least 200 units from all previously accepted points (configurable by map size heuristic: `min_spacing = clamp(bbox_diagonal / (needed * 2), 60, 500)`).

3. **Adaptive fill**: If initial spacing is too tight (yields < needed), halve spacing and retry (same as current, but with validation at each step).

**Output**: Validated pad list appended to spawn pool.

### 2.3 Layer 3: Grid Sampling on Map Bounding Box Floor Plane

**When**: Layer 2 produced fewer than `needed` valid points (map has no waypoints, or waypoints are sparse/invalid).

**Algorithm**:

```
Compute map AABB from room bounding boxes:
  for each room r in stage:
    expand AABB by g_BgRooms[r].pos (center), room bbox (types.h struct room)

Grid step = max(AABB_width, AABB_depth) / ceil(sqrt(needed * 4))
  (oversample 4x because many grid points will fail validation)

For each grid point (x, z) on the AABB floor plane (y = AABB_max_y):
  1. Raycast down: cdFindGroundInfoAtCyl at (x, AABB_max_y, z) with radius 30
     - If no ground found (returns huge negative): skip
  2. Set candidate pos = (x, ground_y + 10, z)
  3. Resolve room: bgFindRoomsByPos(candidate, inrooms, ...)
     - If no valid room: skip
  4. Clearance check: cdExamCylMove01 upward 180 units
     - If blocked: skip
  5. Spacing check vs all accepted points
     - If too close: skip
  6. Accept candidate, store with resolved room
```

**Determinism**: Grid iteration order is fixed (row-major). No randomness. Same AABB = same grid = same candidates = same results.

**Output**: Grid-derived synthetic spawn points appended to pool.

### 2.4 Layer 4: Bounding Box Center + Radial Offsets (Ultimate Fallback)

**When**: All previous layers combined produced fewer than `needed` points. This layer ALWAYS produces exactly `needed` points.

**Algorithm**:

```
center = (AABB_center_x, AABB_center_y, AABB_center_z)
  If no AABB available (no rooms loaded): center = (0, 100, 0)

For i in 0..needed-1:
  angle = (2 * PI * i) / needed
  radius = min(200, AABB_diagonal / 4)  -- stay inside map
  candidate = center + (cos(angle) * radius, 0, sin(angle) * radius)

  Attempt ground resolution:
    ground_y = cdFindGroundInfoAtCyl(candidate, 30, ...)
    If valid: candidate.y = ground_y + 10
    Else: candidate.y = center.y  -- can't find floor, use center height

  Resolve room:
    bgFindRoomsByPos(candidate, inrooms, ...)
    If valid room found: use it
    Else if any room exists at all: use room 0 as last resort
    Else: room = 0 (degenerate map)

  Accept candidate unconditionally
```

**Guarantee**: This always produces `needed` points. Even if every candidate is in void geometry, they still have coordinates and a room number. The player/bot will exist in the world. They may clip through geometry, but they will not crash the game.

**Determinism**: Pure math from AABB + needed count. No RNG. Identical on all clients.

### 2.5 Layer Orchestration

```
spawnPoolBuild(stage_id, match_seed, needed):
  pool = []

  // L1: Declared spawn points
  pool += g_SpawnPoints[0..g_NumSpawnPoints-1]  (already populated by playerReset)
  pool = validateAndFilter(pool)  // apply validation pipeline

  if len(pool) >= needed: return pool

  // L2: Waypoint/navmesh sampling
  pool += sampleWaypoints(stage_id, match_seed, needed - len(pool))

  if len(pool) >= needed: return pool

  // L3: Grid sampling
  aabb = computeStageAABB()
  pool += gridSample(aabb, needed - len(pool))

  if len(pool) >= needed: return pool

  // L4: Radial fallback (guaranteed)
  pool += radialFallback(aabb, needed - len(pool))

  assert(len(pool) >= needed)  // MUST hold
  return pool
```

---

## 3. Validation Rules

Every spawn point, regardless of origin layer, is tested against these rules before final acceptance. L4 is exempt (it accepts unconditionally to preserve the guarantee).

| Rule | Test | Threshold | Reject Action |
|---|---|---|---|
| **Ground clearance** | `cdFindGroundInfoAtCyl(pos, 30, rooms, ...)` | ground_y must be within 500 units below pos.y | Skip candidate |
| **Vertical clearance** | `cdExamCylMove01(pos, pos+180y, 30, rooms, CDTYPE_BG, true, ...)` | Must NOT collide | Skip candidate |
| **Room validity** | `bgFindRoomsByPos(pos, inrooms, ...)` | At least one valid room (>= 0) | Skip candidate |
| **Not inside geometry** | `bgTestPosInRoom(pos, resolvedroom)` | Must return true | Skip candidate |
| **Minimum spacing** | Euclidean XZ distance to all accepted points | >= `min_spacing` (adaptive, 60-500 units) | Skip candidate |

### 3.1 Validation Budget

To prevent infinite loops on degenerate maps, each layer has a **candidate budget**:
- L2: `numwaypoints * 4` candidates max
- L3: `grid_cells * 1` (each cell tested once)
- L4: Exactly `needed` candidates, all accepted

If a layer exhausts its budget without filling the pool, it returns what it has and the next layer takes over.

---

## 4. Spawn Selection at Match Start

Given a valid pool of `P >= N+M` spawn points, assignment works as follows:

### 4.1 Spread-Out Rule (FFA / Non-Team)

```
assignSpawns(pool, participants):
  assigned = []
  remaining_pool = shuffle(pool, match_seed)  // deterministic shuffle

  for each participant p in participants:
    best = argmax over remaining_pool of: min_distance_to(assigned)
    assigned[p] = best
    remove best from remaining_pool
```

This is a greedy farthest-point-first algorithm. It maximizes the minimum distance between any two spawned entities. O(P * N) which is fine for P <= 36.

### 4.2 Team-Aware Rule

```
For team modes (2/3/4 teams):
  Partition pool into team_zones:
    Sort pool by angle from map center
    Divide into num_teams equal angular sectors
    Assign team T to sector T

  For each team T:
    Apply spread-out rule within team T's sector
    If sector has fewer points than team T's members:
      Overflow into adjacent sector (wrap around)
```

CTC's existing `spawnpadsperteam` system is preserved as a special case of L1 -- when CTC declares team spawn pads, those are used directly.

### 4.3 Respawn Selection

Respawns use the existing `playerChooseSpawnLocation()` algorithm (enemy-distance weighting, visibility avoidance, shortlist). The only change: the pool it draws from is the validated L1-L4 pool instead of raw `g_SpawnPoints[]`.

---

## 5. Network Synchronization

### 5.1 Recommendation: Server Computes, Clients Verify

**Server** (or host in listen mode):
1. At match start, generate `match_seed` (32-bit, from secure RNG or time-based hash).
2. Run `spawnPoolBuild(stage_id, match_seed, needed)`.
3. Include `match_seed` in `SVC_STAGE_START` (already has room for additional fields).
4. Include the pool size and which layer was used (for diagnostics) in `SVC_STAGE_START`.

**Clients**:
1. Receive `match_seed` from `SVC_STAGE_START`.
2. Run the same `spawnPoolBuild()` with the same inputs.
3. Pool will be identical because all layers are deterministic given the same seed.

**Why not ship the pool directly?** The pool can be up to 36 entries x (3 floats + 1 room) = 576 bytes. Shipping it works but adds message complexity. Since the algorithm is deterministic, shipping just the 4-byte seed is sufficient. However, shipping the pool is an acceptable fallback if determinism bugs appear -- the message size is manageable.

### 5.2 Seed Distribution

Add `u32 match_seed` to `SVC_STAGE_START`. This is a 4-byte addition to an existing message. The seed is generated once per match and does not change during the match. Respawns use the same pool (just different selection within it), so no re-seeding is needed.

### 5.3 Late Joiners

Late joiners receive `SVC_STAGE_START` with the same `match_seed`. They reconstruct the pool identically and use it for their initial spawn.

---

## 6. Key Files (Implementation Targets)

| File | Role | Changes Needed |
|---|---|---|
| `src/game/player.c:123-124` | `g_SpawnPoints[]`, `g_NumSpawnPoints` | Expand array to MAX_MPCHRS. Add validated pool structure. |
| `src/game/player.c:232-696` | `playerChooseSpawnLocation()` | Draw from validated pool instead of raw g_SpawnPoints. |
| `src/game/playerreset.c:149-405` | Spawn point population during stage load | Integrate L1-L4 pipeline. |
| `src/game/mplayer/scenarios.c:805-820` | `scenarioChooseSpawnLocation()` | Pass validated pool. |
| `src/game/bot.c:306` | `botSpawn()` | Use validated pool for bot spawn location. |
| `port/src/net/netmsg.c` | SVC_STAGE_START | Add match_seed field. |
| **NEW**: `src/game/spawnpool.c` | L2/L3/L4 generators, validation pipeline | New file, ~400 lines C. |
| **NEW**: `src/include/game/spawnpool.h` | Public API for spawn pool system | New file, ~40 lines. |

---

## 7. Pseudocode: spawnpool.c Core

```c
// spawnpool.h
#define SPAWNPOOL_MAX  MAX_MPCHRS  // 36

typedef struct {
    struct coord pos;
    RoomNum room;
    s16 source_pad;   // pad number if from L1/L2, -1 if synthetic (L3/L4)
    u8 layer;         // which layer generated this point (1-4)
} spawn_point_t;

typedef struct {
    spawn_point_t points[SPAWNPOOL_MAX];
    s32 count;
    u32 seed;
    u8 max_layer_used;  // highest layer that contributed points
} spawn_pool_t;

// Build the pool. Call after playerReset() has populated g_SpawnPoints.
void spawnPoolBuild(spawn_pool_t *pool, const char *stage_id, u32 match_seed, s32 needed);

// Select a spawn point for a participant (farthest-from-occupied).
s32 spawnPoolSelect(const spawn_pool_t *pool, const struct coord *occupied, s32 num_occupied, s32 team, s32 num_teams);

// Validate a single candidate point. Returns true if passes all checks.
bool spawnPointValidate(const struct coord *pos, RoomNum *rooms, const spawn_point_t *existing, s32 num_existing, f32 min_spacing);
```
