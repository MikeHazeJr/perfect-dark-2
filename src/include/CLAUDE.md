# src/include/ — Game Headers

Type definitions, constants, and function declarations for all game code.

## Critical Files

### types.h — Core Data Structures
- `struct prop` (line ~279): `pos`, entity union (obj/chr/weapon/etc), `rooms[8]`
- `struct defaultobj` (line ~1453): `model`, `realrot[3][3]`, `unkgeo` (collision), `geocount`
- `struct model` (line ~613): `definition`, `matrices`, `scale`
- `struct modeldef` (line ~597): `rootnode`, `parts`, `numparts`
- `struct modelnode` (line ~588): `type`, `rodata`, parent/child/next/prev tree
- `struct modelrodata_dl` (line ~553): `vertices` (Vtx*), `numvertices`, `opagdl`/`xlugdl`
- `struct modelrodata_gundl` (line ~444): `vertices` (Vtx*), `numvertices`
- `struct modelrodata_bbox` (line ~471): `xmin/xmax/ymin/ymax/zmin/zmax`
- `struct coord` (line ~41): union of `{x,y,z}` and `f[3]`
- Geo types: `struct geo` (header), `struct geotilef` (float floor tiles, up to 64 vertices),
  `struct geoblock` (AABB), `struct geocyl` (cylinder)

### constants.h — All Game Constants
- GEO flags: GEOFLAG_FLOOR1 (0x0001), FLOOR2 (0x0002), WALL (0x0004), SLOPE (0x0100)
- Model node types: MODELNODETYPE_BBOX (0x0a), DL (0x18), GUNDL (0x04)
- Object flags: OBJFLAG3_WALKTHROUGH (0x00000400), OBJH2FLAG_08 (0x08)
- Prop types: PROPTYPE_OBJ (1), DOOR (2), CHR (3), WEAPON (4), PLAYER (6)
- GEOTYPE_TILE_F (1), GEOTYPE_BLOCK (2), GEOTYPE_CYL (3)

### bss.h — Global Variable Declarations
- `g_Vars` — Main game state (players, props, frame counters)
- `g_RoomPropListChunks` — Room-to-prop spatial index

## Type Conventions
- **New code**: Standard C types (`bool`, `int`, `float`, `<stdbool.h>`) are fine.
- **Legacy types** (in existing decompiled code): `s8/s16/s32` = signed, `u8/u16/u32` = unsigned,
  `f32` = float, `bool` = `s32`. These are typedefs from `PR/ultratypes.h`, mix safely with standard types.
- `RoomNum` = room index type (s16)
- `Mtxf` = 4x4 float matrix (union of f32[4][4] and u32[4][4])
