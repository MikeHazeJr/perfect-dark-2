# src/game/ — Game Logic

Core game systems. All files are C, decompiled from original N64 ROM with PC extensions.

## Key Files

### Player Movement & Physics
- **bondwalk.c** — Player ground detection, jump physics, gravity, capsule sweeps
  - `FIXED_JUMP_IMPULSE 8.2f` — Jump strength constant
  - Ground detection: `cdFindGroundInfoAtCyl()` → capsule fallback → mesh floor
  - Grounded check: groundgap < 10, bdeltapos.y < 2, not on ladder
  - Airborne gravity multiplier: 0.277777777f with average velocity
- **bondmove.c** — Player lateral movement, camera-relative input

### Props & Objects
- **propobj.c** — Prop initialization, collision geometry, object types
  - `objInit()` — Allocates collision geo from MODELPART_0065/0066 nodes
  - PC: Auto-generates floor tiles from bbox for props without custom collision
  - `func0f069b4c()` — Updates collision geometry when props move
  - `objBuildTopFaceTile()` — PC-only: transforms bbox ymax face to world-space floor tile
  - `func0f070ca0()` — Builds geotilef from bbox/type19 data (uses ymin)
  - `modelFindBboxRodata()` — Finds bbox node in model tree
- **prop.c** — Prop lifecycle, room assignment, `propUpdateGeometry()`

### Multiplayer
- **mplayer/setup.c** — MP character select, game setup
- **bot.c** — Bot AI (server-authoritative in netplay)

## Collision System Integration
Props with `unkgeo != NULL` get collision checked via `propUpdateGeometry()` →
`cdCollectGeoForCyl()`. Geometry types: GEOTYPE_TILE_F (float tiles), GEOTYPE_BLOCK (AABB),
GEOTYPE_CYL (cylinder). Flags: GEOFLAG_FLOOR1|FLOOR2 for walkable, GEOFLAG_WALL for walls.

## Conventions
- **New code**: Standard C types (`bool`, `int`, `float`, `<stdbool.h>`) are fine.
- **Existing code**: Uses `s32` for bool, `f32` for float — legacy typedefs, mix safely.
- **No platform guards needed**: PC-only target, no N64/Switch. Write code unconditionally.
- Function naming: decompiled names preserved (e.g., `func0f069b4c`)
- Struct access: `g_Vars.currentplayer->` for player state
