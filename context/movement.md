# Movement & Jump System

## Status: ACTIVE DEVELOPMENT
Capsule collision system integrated. Stationary jumping confirmed working. Movement-during-jump and extended testing pending.

## Architecture

### Vertical Movement Pipeline (bondwalk.c — bwalkUpdateVertical)
The vertical movement system processes gravity, jumping, floor/ceiling detection, and vertical translation each frame.

**Flow** (simplified):
1. Compute `ground` from `cdFindGroundInfoAtCyl` (BG floor)
2. Capsule floor probe: `capsuleFindFloor()` catches prop surfaces missed by legacy system
3. Apply gravity: `bdeltapos.y -= gravityamt` per frame
4. Accumulate vertical delta: `newmanground = vv_manground + bdeltapos.y`
5. Pre-move capsule sweep: `capsuleSweep()` along vertical delta, clamp on collision
6. Execute move: `bwalkTryMoveUpwards(verticalDelta)`
7. Post-move ceiling check: `capsuleFindCeiling()` for headroom enforcement

### Jump Physics (bondmove.c + bondwalk.c)
- **Input**: `BUTTON_JUMP` (CONT_4000) → spacebar via `joyGetButtonsPressedOnSample()` in bondmove.c:1863
- **Impulse**: `DEFAULT_JUMP_IMPULSE = 6.5f`, overridable by:
  - `g_MpSetup.jumpimpulse` (match-level limit)
  - `PLAYER_EXTCFG().jumpheight` (per-player profile config)
- **Network**: `UCMD_JUMP` (1 << 11) in `netplayermove.ucmd` — transmitted over network
- **Gravity**: Applied each frame in `bwalkUpdateVertical`, decelerating upward velocity then pulling down

### Ground Detection
- **Legacy**: `cdFindGroundInfoAtCyl` — tile plane equations for BG floors only
- **Capsule**: `capsuleFindFloor()` — overlays binary search with `cdTestVolume` to catch prop surfaces (desks, crates, tables) that have wall geometry but no floor geometry
- **Guard**: Only runs when `bdeltapos.y <= 0.5f` (not jumping upward) to prevent false floors

### Ceiling Detection
- **Legacy**: `cdFindCeilingRoomYColourFlagsAtPos` — BG ceilings only, doesn't write output when no geo found (init to 99999.0f required)
- **Capsule**: `capsuleFindCeiling()` — overlays upward binary search to catch prop/wall geometry acting as ceiling
- **Headroom enforcement**: If `headY > ceilY`, clamp `vv_manground` down

### Capsule Integration Points in bondwalk.c
See [collision.md](collision.md) for full details on the three integration points:
1. Floor detection (~line 994-1028)
2. Pre-move sweep (~line 1204-1248)
3. Ceiling detection (~line 1259-1302)

## Player Parameters
From `playerGetBbox()` in player.c:
- **radius**: `bond2.radius` (~30 units)
- **ymin**: `vv_manground + 30` (feet + 30)
- **ymax**: `vv_manground + vv_headheight + crouchoffsetrealsmall` (min 80 above manground)

## Bugs Fixed
- **ceilY=0.0 killing jumps**: Legacy cdFindCeiling only writes output when ceiling geo exists. Changed init from 0.0f to 99999.0f
- **Prop surface false floors during jumps**: Old binary search hack created "staircase to the sky" — each jump launched from a higher false floor. REPLACED by capsule system
- **Jump height config not persisted**: `jumpheight` field was never registered with config system (FIX-3). Added `configRegisterFloat("Game.Player%d.JumpHeight", ...)` in main.c

## Dependencies
- **Collision system** (collision.md): capsuleSweep, capsuleFindFloor, capsuleFindCeiling
- **Player data**: playerGetBbox provides capsule dimensions
- **Room system**: roomsCopy, bmoveFindEnteredRoomsByPos, func0f065e74 for room tracking
- **Input system**: joyGetButtonsPressedOnSample for BUTTON_JUMP detection
- **Network**: UCMD_JUMP in netplayermove.ucmd for multiplayer jump sync

## Key Files
- `src/game/bondwalk.c` — Vertical movement, gravity, floor/ceiling detection, capsule integration
- `src/game/bondmove.c` — Jump input detection, horizontal movement, movement mode dispatch
- `src/game/player.c` — playerGetBbox, player tick, death/respawn
- `src/game/playermgr.c` — Player allocation and management

## Planned Upgrades
- **Sprint mechanic**: Tighter, more responsive movement with sprint toggle
- **Horizontal capsule sweep**: Extend capsuleSweep to XZ movement for wall sliding and fast-move clipping
- **Animation-driven movement**: Replace fixed velocity constants with animation-synchronized movement speeds
- **Bot jump AI**: Reactive jumping for obstacles, evasive jumping under fire, pathfinding jump nodes (Phase D2)
