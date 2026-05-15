# Sprint Report: Slice 5 surface_up raycast (wallrun via sampler upgrade)

Date: 2026-05-15
Card: c3738 (Skedar surface-normal locomotion, Slices 4-5)
Pillar: Physics-collision
Branch: dev (main checkout, no worktree)

## Goal

Upgrade `chrSurfaceLocoSampleFloorNormal` so surface-loco chrs (Skedars by
default) raycast along their own current `-surface_up` instead of world-down.
This is the critical-path change that unlocks visible wallrun behaviour on
both CPU surface-loco chrs (chrTick path) and GPU swarm bots (Slice 6 SSBO
upload).

## What changed

### `src/game/surface_loco.c`

#### `chrSurfaceLocoSampleFloorNormal`

Split into two branches gated by `chrSurfaceLocoIsEnabled(chr)`:

- **Disabled (humans, civilians, all non-Skedars by default):** unchanged
  legacy world-down sampler using `cdFindFloorRoomYColourNormalPropAtPos`.
  Bit-exact behaviour preserved for every existing non-Skedar caller. This
  protects flat-ground human behaviour, which the dispatch explicitly
  required.

- **Enabled (Skedars + per-chr/scenario opt-ins):** new raycast along
  `-chr->surface_up`. Implementation:
  1. Read `chr->surface_up`, normalize defensively (handles degenerate
     stored values, falls back to world-up rather than firing a
     zero-length ray).
  2. Compute ray endpoint at `chr->prop->pos - surface_up * ray_len`
     where `ray_len = max(chr->height * 1.5f, 200.0f)`. The 1.5x factor
     mirrors Slice 3 design guidance (hit < chr->height = on surface,
     extra margin so we detect surfaces the chr is approaching). The
     200 unit floor stops very small chrs from undersampling.
  3. Call `cdExamLos08(from, rooms, to, CDTYPE_ALL,
     GEOFLAG_WALL | GEOFLAG_FLOOR1 | GEOFLAG_FLOOR2 | GEOFLAG_BLOCK_SIGHT)`.
     This is BG + props in one sweep, catching the wall geometry that
     `cdFindFloorRoomYColourNormalPropAtPos` (floor-only) misses.
  4. On `CDRESULT_COLLISION`, fetch the hit normal via
     `cdGetObstacleNormal`, flip it if `dot(normal, surface_up) < 0` so
     the normal always faces the chr (catches double-sided hits where
     the geo's stored normal points the "wrong" way).
  5. Normalize, write to `out_up`, return true.
  6. On miss, return false with `out_up = world-up` so downstream
     consumers (chrSurfaceLocoTick, swarm_gpu.cpp) see the drop signal.

The signature `bool (*)(struct chrdata *, f32 *out_up)` is preserved
verbatim. `port/fast3d/swarm_gpu.cpp` and `chrSurfaceLocoTick` continue
to call it through the same prototype.

Excerpt of the new surface-loco branch:

```c
struct coord from = chr->prop->pos;
struct coord to;
to.x = from.x - sux * ray_len;
to.y = from.y - suy * ray_len;
to.z = from.z - suz * ray_len;

const u16 geoflags = GEOFLAG_WALL | GEOFLAG_FLOOR1 | GEOFLAG_FLOOR2 | GEOFLAG_BLOCK_SIGHT;
const s32 los = cdExamLos08(&from, chr->prop->rooms, &to, CDTYPE_ALL, geoflags);

if (los != CDRESULT_COLLISION) {
    return false;  /* drop heuristic: tick layer blends to world-up */
}

struct coord normal = { { 0.0f, 1.0f, 0.0f } };
cdGetObstacleNormal(&normal);

const f32 ndotu = normal.x * sux + normal.y * suy + normal.z * suz;
if (ndotu < 0.0f) {
    normal.x = -normal.x;
    normal.y = -normal.y;
    normal.z = -normal.z;
}
```

#### `chrSurfaceLocoTick`

Added the Slice 5 **drop-back-to-world-up heuristic** for the raycast-miss
case. Previously the tick just `return`ed early on miss (Slice 3 placeholder
"hold steady"). Now:

- Mark `SURFACE_LOCO_FLAG_AIRBORNE` so consumers can read the airborne
  state.
- If `chr->surface_up.y >= 0.99` (cosine threshold, already effectively
  world-up): snap to (0,1,0) and clear `BLENDING`.
- Else: save current as `surface_up_prev`, set target to world-up
  (0,1,0), arm `surface_blend_frames = SURFACE_LOCO_BLEND_FRAMES`,
  set `BLENDING`. Renderer lerps via `chrSurfaceLocoGetRenderUp`.

On surface re-acquisition (next tick the ray hits): clear `AIRBORNE`.

### Edge / corner blend factor

The existing `SURFACE_LOCO_BLEND_FRAMES = 8` (8 frames at 60Hz = ~133ms)
from Slice 3 is the blend window for edge / corner transitions. It is
already kicked when `dot(current_up, sampled_up) < 0.99` (~8 degrees).
A wall-to-floor transition would yield a 90-degree delta, easily over the
threshold, so it kicks the blend correctly. Reasoning: 8 frames is short
enough to feel responsive on a 60Hz tick yet long enough to avoid the
visible "snap" on edge crossings. Matches Slice 3 design doc tuning,
which was the playtest baseline. No change to this constant in Slice 5.

### `src/include/game/surface_loco.h`

Updated the docstring of `chrSurfaceLocoSampleFloorNormal` to describe
the new gated dual behaviour (disabled = world-down legacy; enabled =
surface_up raycast). Signature unchanged.

## Compatibility

- **Humans / non-Skedars:** unchanged. The new surface-loco branch is gated
  by `chrSurfaceLocoIsEnabled`, which still defaults to "RACE_SKEDAR only".
- **GPU swarm:** `port/fast3d/swarm_gpu.cpp` calls the sampler with the
  same signature it always has. For Skedar bots it now uploads the
  surface_up normal that follows the bot's local up (walls, ceilings)
  instead of always world-down. The Slice 6 compute kernel projects the
  seek vector onto that plane, completing the wallrun behaviour the
  benchmark exercises.
- **Chr tick path:** `chrSurfaceLocoTick` now handles the drop heuristic
  inline. `chr->surface_loco_flags` gains `AIRBORNE` semantics that
  consumers (none right now beyond the doc string) can read.

## Hard-rule compliance

- Non-Skedar humans MUST not get the new direction: enforced by the
  `chrSurfaceLocoIsEnabled` gate inside the sampler.
- No new state on the chr struct, no new net protocol bumps. AIRBORNE
  was already a pre-allocated bit in `surface_loco_flags`.
- Scope stayed inside Slice 5 - no Slice 4 aim-projection changes
  hitched on.

## Build verify

`devtools/build-headless.ps1` (default target=all) and `-Target server`:

| Target | Result | Time | Output |
|--------|--------|------|--------|
| Client (pd) | PASS | 3s | Build/PerfectDark.exe 55.5 MB |
| Updater    | PASS | 0s  | Build/Updater.exe 12.3 MB |
| Server (pd-server) | PASS | 2s | Build/PerfectDarkServer.exe 22.4 MB |

No warnings, no errors. Stderr logs empty.

## Smoke verify

| Test | Result | Assertions | Elapsed |
|------|--------|------------|---------|
| swarm_cpu_smoke | PASS | 19/19 | 96.2s |
| swarm_gpu_smoke | PASS | 19/19 | 150.5s |
| physics_capsule_basic_smoke | PASS | 19/19 | 55.8s |

All three exercise different code paths:
- `swarm_cpu_smoke` runs CPU surface-loco chrs through `chrSurfaceLocoTick`.
- `swarm_gpu_smoke` runs GPU swarm bots that consume the sampler directly
  from `swarm_gpu.cpp` for SSBO upload (Slice 6 path).
- `physics_capsule_basic_smoke` exercises non-Skedar player movement to
  confirm flat-ground human behaviour is unchanged.

Result JSONs in `.claude/smoke-verify-runs/results-20260515T*.json`.

## Manual visual check

Not performed. The dispatch noted the game director (Mike) validates
visually in-game. The code change ships per dispatch step 7.

## Files touched

- `src/game/surface_loco.c` (sampler split + tick drop heuristic)
- `src/include/game/surface_loco.h` (sampler docstring update)

## Commit

`Physics-collision - c3738: Slice 5 surface_up raycast (wallrun via sampler upgrade)`
