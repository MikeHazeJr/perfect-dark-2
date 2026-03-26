# Collision System

## Status: ACTIVE DEVELOPMENT
The swept capsule collision system has been implemented and is in initial testing.

## Architecture

### Legacy System (N64-era, still partially in use)
The original collision system uses simplified bounding boxes and flag-based tests:

- **cdTestVolume**: Tests if a bounding box at a position collides with geometry matching
  specified CDTYPE_* and GEOFLAG_* filters. Returns CDRESULT_COLLISION or CDRESULT_NOCOLLISION.
  Only tests GEOFLAG_WALL geometry — floor/ceiling surfaces flagged as FLOOR are invisible to it.
- **cdFindGroundInfoAtCyl**: Finds floor height below a cylinder using tile plane equations.
  Handles BG floor geometry properly but cannot find prop surfaces that lack floor geometry.
- **cdFindCeilingRoomYColourFlagsAtPos**: Finds ceiling height above a point. Only finds BG
  ceilings — misses prop/wall geometry acting as ceiling. Only writes output when ceiling geo
  exists (important: caller must initialize output to safe default).
- **cdCollectGeoForCyl / cdCollectGeoForCylFromList**: Iterates room geometry buffers, collecting
  tiles that overlap a given cylinder. Tests AABB overlap + optional vertical range.

### New System: Swept Capsule Collision (PC-only)
Replaces N64 workarounds with proper capsule volume sweeps.

**Files**:
- `src/include/lib/capsule.h` — API header
- `src/lib/capsule.c` — Implementation
- `CMakeLists.txt` — capsule.c added to build

**Core Functions**:

1. **capsuleSweep(struct capsulecast *cast)** — Projects the player capsule along a movement
   vector in 16 sub-steps, testing cdTestVolume at each step. Returns safe fraction [0..1].
   Populates: hittype (CAPSULE_HIT_NONE/FLOOR/CEILING/WALL/PROP), hitfrac, hitpos,
   hitnormal (approximate), hitprop, hitgeoflags.

2. **capsuleFindFloor(pos, radius, ymin_off, ymax_off, rooms, cdtypes, out_prop, out_flags)** —
   Combines cdFindGroundInfoAtCyl (BG floor) with 12-iteration binary search using capsule
   volume tests. Catches prop surfaces with wall geometry but no floor geometry (desks, tables,
   crates). Returns highest of BG floor or prop floor.

3. **capsuleFindCeiling(pos, radius, ymin_off, ymax_off, rooms, cdtypes, out_prop)** —
   Combines cdFindCeilingRoomYColourFlagsAtPos (BG ceiling) with capsule volume binary search
   upward. Catches prop/wall geometry acting as ceiling. Returns lowest of BG ceiling or prop ceiling.

**Capsule Parameters** (from playerGetBbox):
- radius: `bond2.radius` (~30 units)
- ymin: `vv_manground + 30` (feet + 30)
- ymax: `vv_manground + vv_headheight + crouchoffsetrealsmall` (min 80 above manground)

## Integration Points

### bondwalk.c — bwalkUpdateVertical()
Three integration points replace legacy workarounds:

1. **Floor detection** (~line 994-1028): Replaced old prop surface binary search hack with
   `capsuleFindFloor()`. Only runs when NOT jumping upward (bdeltapos.y <= 0.5f guard).
   Log tag: `CAPSULE_FLOOR:`

2. **Pre-move sweep** (~line 1204-1248): Before `bwalkTryMoveUpwards`, sweeps capsule along
   vertical movement vector with `capsuleSweep()`. Clamps movement to safe fraction on collision.
   Kills upward velocity on ceiling hits. Log tag: `CAPSULE_SWEEP:`

3. **Ceiling detection** (~line 1259-1302): Replaced old cdFindCeilingRoomYColourFlagsAtPos-only
   approach with `capsuleFindCeiling()`. Tests full capsule volume upward.
   Log tags: `CAPSULE_CEIL:`, `CAPSULE_CEIL_CLAMP:`

## Geometry Data Structures

```
struct geo { u8 type; u8 numvertices; u16 flags; }

struct geotilei  — BG geometry, s16 vertices, AABB via byte offsets into tile
struct geotilef  — Lift geometry, float vertices, same AABB scheme
struct geoblock  — Props/objects, XZ polygon (up to 4 verts) + ymin/ymax
struct geocyl    — Cylinders (chrs, hoverbike), center + radius + ymin/ymax

Room → geo mapping: g_TileFileData.u8 + g_TileRooms[roomnum]
Prop geometry: propUpdateGeometry(prop, &start, &end)
Geo buffer iteration: walk by type-specific stride (geotilei: numverts*6+0xe, etc.)
```

## Constants
```
CDRESULT_COLLISION = 0, CDRESULT_NOCOLLISION = 1
CDTYPE_ALL = 0x003f (objs+doors+players+chrs+pathblocker+bg)
GEOFLAG_FLOOR1 = 0x0001, GEOFLAG_FLOOR2 = 0x0002, GEOFLAG_WALL = 0x0004
GEOTYPE_TILE_I = 0, GEOTYPE_TILE_F = 1, GEOTYPE_BLOCK = 2, GEOTYPE_CYL = 3
```

## Bugs Fixed
- **ceilY=0.0 killing jumps**: cdFindCeilingRoomYColourFlagsAtPos only writes output when
  ceiling geo exists. Changed init from 0.0f to 99999.0f.
- **Prop surface false floors during jumps**: Old binary search hack created "staircase to the sky"
  where each jump launched from a higher false floor. REPLACED by capsule system.
- **cdTestVolume missing ceiling surfaces**: cdTestVolume only checks GEOFLAG_WALL, so ceiling
  surfaces flagged as FLOOR were invisible. Capsule ceiling probe now catches these.

## Dependencies
- **Movement system** (movement.md): Capsule system is called from bwalkUpdateVertical
- **Player data**: playerGetBbox provides capsule dimensions
- **Room system**: roomsCopy, bmoveFindEnteredRoomsByPos, func0f065e74 for room tracking

## Mesh Collision System (S48 -- ACTIVE)

New files: `src/lib/meshcollision.c` + `src/include/lib/meshcollision.h`
Modified: `src/lib/capsule.c`, `src/game/lv.c`, `src/include/types.h`

**Architecture**: mesh collision is PRIMARY for player movement. Legacy is fallback only.
- **Mesh** (`meshSweepCapsuleWorld`, `meshFindFloor`, `meshFindCeiling`): all player movement
- **Legacy** (`cdTestVolume`, `cdFindGroundInfoAtCyl`): damage/weapons only, fallback if mesh empty

**Triangle extraction**: walks model node tree, finds DL nodes (type 0x18), parses GBI
display lists (G_TRI1, G_TRI4) to extract vertex positions from Vtx arrays. Handles
PC port vertex index convention (divide by 10). Auto-classifies triangles as floor/wall/ceiling.

**Room geometry extraction**: `meshWorldAddRoomGeo()` reads packed tile data from
g_TileFileData/g_TileRooms per room. Handles geotilei (s16 BG), geotilef (float lifts),
geoblock (prop bounding boxes). Fan-triangulates polygon tiles.

**Static world mesh**: all room geometry merged into one spatial grid (256-unit cells).
Built at stage load after `bgBuildTables()`. All rooms iterated. Skedar Ruins: 7,110 tris,
33x52 grid. Lifecycle: `meshWorldInit()` -> add rooms -> `meshWorldFinalize()` -> gameplay -> `meshWorldShutdown()`.

**Dynamic meshes**: `struct colmesh *colmesh` field added to `struct prop` in types.h.
`meshAttachToProp()` / `meshGetFromProp()` for per-prop mesh data. Dynamic objects
keep their own triangle array, transformed by world matrix at query time.

**Capsule integration** (capsule.c):
- `capsuleSweep()`: mesh sweep only when world mesh is built, legacy only when not
- `capsuleFindFloor()`: runs BOTH mesh and legacy, returns the higher floor. Mesh covers
  level geometry (BG tiles), legacy covers prop surfaces (desks, crates, walls) until
  prop meshes are extracted into the world grid.
- `capsuleFindCeiling()`: runs BOTH mesh and legacy, returns the lower ceiling. Same
  rationale -- props acting as ceilings need legacy until prop mesh extraction.

**Stage coverage**: all stages except TITLE/BOOTPAKMENU/CREDITS/4MBMENU get mesh collision.
Carrington Institute menu map is included.

**Verified**: Skedar Ruins extracted 7,110 tris successfully. Compiles clean (496 objects, 0 errors).

## Remaining Work
- Extract static prop meshes at prop spawn and add to world grid
- Extract dynamic prop meshes (doors, lifts) and attach to props
- Horizontal capsule sweep for wall sliding
- BVH acceleration for very large levels (if grid becomes bottleneck)
