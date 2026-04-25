# src/lib/ — Engine Libraries

Low-level engine systems. Decompiled C code with PC extensions.

## Key Files

### Collision
- **collision.c** — Core collision detection
  - `cdFindGroundInfoAtCyl()` (line ~2183) — Primary ground detection. Collects GEOFLAG_FLOOR1|FLOOR2 geometry only.
  - `cdCollectGeoForCyl()` — Iterates BG rooms + props. Uses `roomGetProps()` for prop enumeration.
  - `cdTestVolume()` — Tests cylinder against GEOFLAG_WALL geometry.
  - `cdFindGroundFromList()` — Evaluates ground height from geo list. Steep inclines can give unrealistic values.
  - Props are iterated via `propUpdateGeometry()` which returns the geo byte range.

### Capsule Physics
- **capsule.c** — Capsule sweep and floor/ceiling probes
  - `capsuleFindFloor()` (line ~135) — Binary search (12 iterations) for prop surfaces
  - `capsuleFindCeiling()` (line ~230) — Binary search upward for ceiling
  - Both return higher of BG floor/ceiling vs prop floor/ceiling

## Collision Geometry Types
- `GEOTYPE_TILE_I` (0) — Integer vertices, BG tiles
- `GEOTYPE_TILE_F` (1) — Float vertices, lifts and auto-generated prop floors
- `GEOTYPE_BLOCK` (2) — AABB, most objects
- `GEOTYPE_CYL` (3) — Cylinder, characters and hoverbike

## Geo Flags (from constants.h)
- `GEOFLAG_FLOOR1` (0x0001), `GEOFLAG_FLOOR2` (0x0002) — Walkable surfaces
- `GEOFLAG_WALL` (0x0004) — Wall collision
- `GEOFLAG_SLOPE` (0x0100) — Slope surface
- `GEOFLAG_STEP` (0x2000) — Ascend regardless of steepness

## Subdirectories
- **ultra/audio/** — N64 audio sequence/bank loaders (all compiled)
- **ultra/gu/** — Graphics utility math: sin/cos tables, matrix ops, perspective (all compiled)
- **ultra/io/** — Only 4 vi mode table files remain (vimodentsclan1.c, vimodepallan1.c, vimodempallan1.c, vitbl.c)
- **mp3/** — MP3 decoder (C implementations only; assembly removed)
- **naudio/** — N64 audio driver

## Important Note
`cdFindGroundInfoAtCyl` only collects FLOOR-flagged geometry. WALL-flagged inclines are
invisible to ground detection — this is why mesh-based prop collision was added in Phase 8.
