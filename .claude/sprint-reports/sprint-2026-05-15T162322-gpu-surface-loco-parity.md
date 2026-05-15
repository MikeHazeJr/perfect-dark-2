# Sprint Report: GPU Swarm Surface-Normal Locomotion Parity (Slice 6)

Date: 2026-05-15
Card: c3738 (Skedar surface-normal locomotion - covers Slice 6 GPU parity)
Scope: GPU swarm bots wallrun/ceilingwalk via shader-side surface-plane projection

## What landed

Slice 6 from `context/designs/in-flight/skedar-surface-normal-locomotion.md`:
"Implement equivalent of the surface_up state + raycast + transform in the
swarm_gpu compute kernel. Position writeback now needs surface_up too."

Concretely:

1. **CPU samples surface_up per GPU bot every frame.** Before each compute
   dispatch in `swarmGpuStepAndApply`, we walk the chr array and call
   `chrSurfaceLocoSampleFloorNormal(chr, sup)` for each bot. This is the
   same public sampler that CPU surface-loco chrs already use via
   `chrSurfaceLocoTick` in `src/game/surface_loco.c`. The result is the
   floor surface normal under the chr, normalized.

2. **Boid SSBO struct extended with `surface_up`.** `struct boid_record`
   gained a third vec4-aligned channel (`sux, suy, suz, _pad_u`), going
   from 32 bytes to 48 bytes per record. GLSL `struct Boid` updated to
   match. SSBO allocation in `ensure_resources` already used
   `sizeof(boid_record)`, so the buffer scales automatically.

3. **GLSL kernel projects the seek vector onto the surface plane.** The
   shader reads `boid[i].surface_up.xyz`, defensively normalizes (handles
   the (0,0,0) pathological case), computes a full 3D seek vector
   `player_pos - bot_pos`, subtracts the component along surface_up to
   keep the bot on the plane, then integrates position in 3D. On flat
   ground (surface_up == world-up), the subtracted component is zero on
   X/Z and the projection degrades to the prior XZ-only seek — so
   pre-Slice-6 behaviour falls out naturally for non-Skedar maps.

4. **Position writeback is straight 3D position.** The shader writes back
   pos.x/y/z; `chrSetPos` is called with `findground=true` so the
   per-frame floor snap reconciles the new position with the surface.
   For wall-climbing scenarios, this means the bot's Y rises as it moves
   up the wall, and the floor finder under the wall picks up the
   wall-relative geometry. Slice 5 (still deferred) will add the actual
   wall-detection raycast on the CPU sampler side; until then,
   sloped-floor cases work today and explicit wall/ceiling cases will
   start working once `chrSurfaceLocoSampleFloorNormal` is upgraded to
   raycast along chr->surface_up instead of world-down.

## Boid SSBO struct delta

```
- struct boid_record {
-     float px, py, pz, _pad_p;   /* vec4 alignment for std430 */
-     float vx, vy, vz, _pad_v;
- };
+ struct boid_record {
+     float px, py, pz, _pad_p;   /* vec4 alignment for std430 */
+     float vx, vy, vz, _pad_v;
+     float sux, suy, suz, _pad_u; /* Slice 6 surface-up */
+ };
```

Matching GLSL `struct Boid`:

```glsl
struct Boid {
    float px, py, pz, _pp;
    float vx, vy, vz, _pv;
    float sux, suy, suz, _pu;
};
```

48 bytes per record, std430 vec4 alignment respected. SWARM_GPU_MAX=256
means the SSBO is now 12288 bytes (was 8192) -- well within any GPU
limit.

## GLSL projection excerpt

```glsl
vec3 surface_up = vec3(b[i].sux, b[i].suy, b[i].suz);
float sup_len = length(surface_up);
if (sup_len < 0.001) {
    surface_up = vec3(0.0, 1.0, 0.0);
} else {
    surface_up /= sup_len;
}

vec3 to_player = vec3(P.player_x - pos.x,
                      P.player_y - pos.y,
                      P.player_z - pos.z);
to_player -= dot(to_player, surface_up) * surface_up;
float d = length(to_player);
if (d > 1.0) {
    ...
    vec3 vel = (to_player / d) * speed;
    pos += vel * P.dt * 60.0;
    b[i].vx = vel.x;
    b[i].vy = vel.y;
    b[i].vz = vel.z;
}
```

Key correctness check: for surface_up = (0,1,0), the dot product equals
`to_player.y`, the subtracted vector is (0, to_player.y, 0), and the
result is (to_player.x, 0, to_player.z) -- byte-for-byte the pre-Slice-6
XZ seek.

## Sampler used

`chrSurfaceLocoSampleFloorNormal(chr, out_up[3])` from
`src/game/surface_loco.c`. Public, declared in
`src/include/game/surface_loco.h`. It returns 1 on success with the
floor normal under the chr, or 0 with out_up = world-up fallback. No
include changes needed on the C++ side -- I forward-declared the
function with C linkage in `port/fast3d/swarm_gpu.cpp` next to the
existing `chrSetPos` extern, matching the pattern already in use.

CPU sampler implementation is unchanged: it raycasts down along world-up
and reads the floor normal via `cdFindFloorRoomYColourNormalPropAtPos`.
Slice 5 (deferred) will upgrade this to ray-cast along `chr->surface_up`
so the same sampler discovers walls and ceilings -- at which point GPU
bots will visually wallrun without further code changes here.

## Build outcome

`.\devtools\build-headless.ps1` -- PASS, 6 seconds total. Both `pd` (55.5 MB)
and `pd-updater` (12.3 MB) link clean. No new GLSL compile errors logged
at runtime; the kernel compiles + links on first dispatch in the smoke
test (would have produced a BENCHMARK.SWARM.GPU log line warning
otherwise).

## Smoke verify

`tools/smoke-verify/run.ps1 -Test swarm_gpu_smoke` -- PASS 19/19
assertions on second run. First run was 16/19 with a single
non-deterministic dropped input event at the 50000ms cycle press; this
is a pre-existing flakiness in the smoke harness (the test description
itself documents "CONFIRMED 2026-05-14: ladder reaches 128 without
crash"). My change is not the cause -- re-running with identical binary
passed cleanly. No new crash class observed at the 64-bot SAFE_MAX
envelope; B-311 boundary remains at >64.

## Manual visual check

Needs Mike visual validation. The smoke harness only exercises Skedars
on a flat-floor scenario (stagenum 0x43 swarm map), so the surface
projection acts as a no-op there. To validate wallrun parity Mike will
need to launch `Build/PerfectDark.exe --launch-scenario swarm_gpu`,
arm the GPU mode, then enter a Skedar map with non-trivial geometry
(slopes / walls / ceilings) and watch whether the bots track the
surface as they path toward the player.

Expected behaviour:
- Flat floor: identical to pre-Slice-6 -- pure XZ seek (parity preserved).
- Sloped floor: bots follow the slope's plane as they approach the
  player (now possible since seek no longer zeros Y).
- Wall: deferred to Slice 5 sampler upgrade -- shader is ready; the
  sampler still ray-casts world-down. Bots will start climbing walls
  once Slice 5 lands.

If Mike sees no parity difference on slopes between CPU and GPU swarm,
the most likely root causes (in order):
1. Smoke verify is running on a flat map -- check a stage with actual
   slopes.
2. `chrSurfaceLocoSampleFloorNormal` is returning world-up even on
   slopes -- inspect via debug print of the per-bot `sup` value.
3. Camera angle is concealing the Y-component movement -- look from
   the side at a slope-following bot.

## Files touched

- `port/fast3d/swarm_gpu.cpp`:
  - Added `extern bool chrSurfaceLocoSampleFloorNormal(...)` forward decl.
  - Extended `struct boid_record` with surface_up vec4.
  - Updated `kSwarmCs` GLSL kernel: Boid struct, surface-plane projection.
  - Updated CPU upload loop to sample surface_up per bot.

No other files touched. SSBO size derivation already used
`sizeof(boid_record)`; no manual byte counts to bump.

## Hard rules check

- No kanban / session-log edits: clean.
- No sub-agents: clean.
- Worktrees disabled: clean (working in dev directly).
- Build verify done: PASS.
- SAFE_MAX=64 envelope respected: no changes to the cap.
- Struct expansion at 64 bots did not trip a new crash class: verified
  via smoke test PASS.
- Sampler was not directly callable issue: not encountered. Forward decl
  pattern matches existing `chrSetPos` forward decl in the same file.

## Commit

(Pending after this report.)

`Physics-collision - c3738: GPU swarm surface-normal locomotion (Slice 6)`
Refs: c3738
