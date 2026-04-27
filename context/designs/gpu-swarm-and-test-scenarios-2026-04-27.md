# GPU Swarm Benchmark + Test Scenarios

> Design doc, 2026-04-27. Phase 1 deliverable. Phase 2 implementation gated on Mike's
> approval of (a) the GL compute path choice in Section F.1, (b) the chr-pool ceiling
> in Section F.2, (c) the hit-detection model in Section F.3, and (d) the empty-map
> source in Section F.4.

## Possibility framing

The directive proposes a Test Scenarios system in Settings > Debug with three options:
The Grid Empty Map, Swarm CPU Bots, Swarm GPU Boids. The two swarm scenarios share
a runtime that benchmarks "many Skedar bots converging on an invincible player" while
the underlying simulation is swapped between the existing CPU bot system and a new
GPU compute boid system. Bot count cycles through 4, 8, 16, 32, 64, 128, 256 with
D-pad-down or 0 to drive comparable A/B numbers per count.

This is a benchmark harness, not a gameplay rework. Existing bot handling stays
intact. Decisions about real-game adoption come after the numbers.

The GPU path needs three things the codebase does not yet have: a GL 4.3+ context
(or an alternative GPU compute strategy on the existing GL 3.0 baseline), a compute
shader pipeline inside `fast3d`, and a separate chr-shaped renderable that tracks
GPU positions without paying CPU AI cost. None of these are blocked, but each has
a scope decision that must be made before code is written.

## A. Test Scenarios system

### A.1 Surface

New section in Settings > Debug, gated by `PD_DEV_BUILD` (the entire Debug tab is
already dev-only at [pdgui_menu_mainmenu.cpp:4191](port/fast3d/pdgui_menu_mainmenu.cpp:4191)).
Section sits below the existing `Memory` block and above `Shortcuts` in
`renderSettingsDebug` ([pdgui_menu_mainmenu.cpp:3486](port/fast3d/pdgui_menu_mainmenu.cpp:3486)).

Layout:

```
Test Scenarios
--------------
Scenario:  [The Grid - Empty Map           v]
Map:       [Skedar Ruins                    v]   (only shown for Swarm scenarios)
                                                 [ Launch ]
```

Widgets:
- `ImGui::Combo("##testscenario", ...)` over scenario enum.
- `ImGui::Combo("##testmap", ...)` over MP arenas filtered from the catalog
  (same source as The Grid: catalog entries of type `ASSET_ARENA`). Default
  selection is `base:mp_skedar`. Only visible when scenario is one of the swarm
  variants.
- `Launch` button calls into `port/src/testscenarios.c` (new module).

### A.2 Scenarios

```c
typedef enum {
    TESTSCEN_NONE             = 0,
    TESTSCEN_EMPTY_MAP        = 1,  /* "The Grid - Empty Map"  */
    TESTSCEN_SWARM_CPU        = 2,  /* "Swarm - CPU Bots"      */
    TESTSCEN_SWARM_GPU        = 3,  /* "Swarm - GPU Boids"     */
    TESTSCEN_COUNT
} test_scenario_t;
```

The Empty Map scenario does not accept the map selector; it loads a single empty
session (see Section F.4 for the source choice).

### A.3 Launch flow

`port/src/testscenarios.c`:

```c
s32 testScenarioLaunch(test_scenario_t scen, const char *map_id);
```

Launch flow piggybacks on the existing Grid path
([pdgui_menu_forge.cpp:41 `pdguiForgeStartSessionOn`](port/fast3d/pdgui_menu_forge.cpp:41)).
That function already does the right thing: resets `g_MatchConfig`, writes
`stage_id`, calls `mainChangeToStage` via the catalog-resolved stagenum.
We add a sibling helper that writes a `g_TestScenarioActive` flag before the
stage transition so the per-frame swarm runtime can latch on at session start.

```c
static struct {
    test_scenario_t scen;
    s32             swarm_method;     /* SWARM_METHOD_CPU / SWARM_METHOD_GPU */
    s32             pending_count;    /* armed by cycler; consumed at apply  */
    s32             active_count;     /* current bot count                   */
} g_TestScenario;
```

Hook points (no protocol changes; this is local-only and never crosses the
wire):
- `lvSetupComplete` end-of-init: if `g_TestScenario.scen != TESTSCEN_NONE`,
  call `swarmTestOnSessionStart()`.
- `mainTick` once per frame: `swarmTestTick()` runs the cycler input check,
  the GPU readback (if applicable), and the per-frame benchmark logging.
- `netDisconnect` / stage transition out: `swarmTestReset()` clears state.

## B. GPU boid system

### B.1 Required GL features

Compute shaders require GL 4.3+ (or `GL_ARB_compute_shader` on a 4.2 context).
The current loader does not include 4.3+ symbols
([port/fast3d/glad/glad.h:2433](port/fast3d/glad/glad.h:2433) is the last
`GL_VERSION_4_1`), and the SDL context probe asks for 3.0 compat first
([port/fast3d/gfx_sdl2.cpp:164](port/fast3d/gfx_sdl2.cpp:164)). See Section F.1
for the required scope decision before this section is buildable.

Assuming Section F.1 lands as Option A (regenerate glad with 4.3 core, prepend
the version probe), the rest of B.2-B.5 follows.

### B.2 Data layout

SoA, fixed-capacity, double-buffered for ping-pong. Cap at 256 (same as the
top of the cycle).

```c
typedef struct {
    float pos_x[256];
    float pos_y[256];
    float pos_z[256];
    float vel_x[256];
    float vel_y[256];
    float vel_z[256];
    float target_offset_x[256]; /* per-boid jitter so swarm does not converge
                                   to one point */
    float target_offset_y[256];
    float target_offset_z[256];
    u32   state[256];           /* bit 0: alive, bit 1: just-killed,
                                   bits 8..15: hp (max 15) */
} swarm_boid_data_soa_t;
```

GPU storage:
- 2x SSBO, ping-pong: `g_BoidStateA`, `g_BoidStateB`.
- 1x SSBO uniform-style for "swarm params": player position, count, dt,
  separation/alignment/cohesion weights, target seek weight.
- Layout matches the SoA above so the compute shader and the CPU readback can
  both treat it as plain `float[N]` arrays.

Memory footprint at 256: 256 boids x (9 floats + 1 u32) = 40 bytes per boid =
10 KiB per buffer x 2 = 20 KiB GPU. Negligible.

### B.3 Compute shader

Single dispatch per tick. One thread per boid, work-group size 64.

```glsl
#version 430 core
layout(local_size_x = 64) in;

layout(std430, binding = 0) readonly  buffer InState  { /* SoA fields */ } s_in;
layout(std430, binding = 1) writeonly buffer OutState { /* SoA fields */ } s_out;
layout(std430, binding = 2) readonly  buffer Params   {
    vec3  player_pos;
    float dt;
    int   count;
    float w_sep;
    float w_align;
    float w_cohere;
    float w_seek;
    float max_speed;
    float neighbor_radius;
} P;

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(P.count)) return;
    if ((s_in.state[i] & 1u) == 0u) return; /* dead */

    vec3 p = vec3(s_in.pos_x[i], s_in.pos_y[i], s_in.pos_z[i]);
    vec3 v = vec3(s_in.vel_x[i], s_in.vel_y[i], s_in.vel_z[i]);

    /* O(N) neighbor walk. At 256 this is 65k ops on the GPU which is free.
     * Skip a spatial hash for now. */
    vec3 sep = vec3(0), align = vec3(0), cohere = vec3(0);
    int n_align = 0, n_cohere = 0;
    for (int j = 0; j < P.count; ++j) {
        if (uint(j) == i || (s_in.state[j] & 1u) == 0u) continue;
        vec3 q  = vec3(s_in.pos_x[j], s_in.pos_y[j], s_in.pos_z[j]);
        vec3 d  = p - q;
        float r = length(d);
        if (r < P.neighbor_radius && r > 0.001) {
            sep    += d / (r * r);
            align  += vec3(s_in.vel_x[j], s_in.vel_y[j], s_in.vel_z[j]);
            cohere += q;
            n_align++; n_cohere++;
        }
    }
    if (n_align > 0)  align  /= float(n_align);
    if (n_cohere > 0) cohere = cohere / float(n_cohere) - p;

    vec3 target = P.player_pos +
                  vec3(s_in.target_offset_x[i],
                       s_in.target_offset_y[i],
                       s_in.target_offset_z[i]);
    vec3 seek = target - p;

    vec3 a = P.w_sep    * sep
           + P.w_align  * align
           + P.w_cohere * cohere
           + P.w_seek   * seek;

    v += a * P.dt;
    float speed = length(v);
    if (speed > P.max_speed) v *= P.max_speed / speed;
    p += v * P.dt;

    s_out.pos_x[i] = p.x; s_out.pos_y[i] = p.y; s_out.pos_z[i] = p.z;
    s_out.vel_x[i] = v.x; s_out.vel_y[i] = v.y; s_out.vel_z[i] = v.z;
    s_out.state[i] = s_in.state[i];
    /* target_offset is constant per boid life, written at spawn */
}
```

### B.4 Render

Render path is one of two options:

- **Option B-R-1 (instanced draw)**: build an instanced draw call that submits
  the Skedar mesh once and pulls per-instance position from a vertex-attribute
  binding pointing at the GPU SoA. Fast and clean. Requires bypassing the GBI
  translator and submitting a hand-rolled GL draw between `rapi->end_frame`
  and `pdguiNewFrame` (where ImGui already does this).
- **Option B-R-2 (per-chr drive)**: keep using the existing chr render path.
  Each frame, copy the GPU position readback into the corresponding
  `chr->prop->pos` and let the existing `chrTickBg` + GBI submission render
  them as normal Skedars. Pays the per-chr CPU cost but keeps everything else
  working: collision, hit detection, score, kill animation, ragdoll, audio.

Recommendation: **B-R-2**. The benchmark is comparing CPU AI tick cost vs
GPU compute tick cost. Render cost is comparable in both modes and is not
the variable under test. This also keeps damage, scoring, kill credit, and
death animations on their existing paths without rewriting them. The render
optimization is a follow-on if the benchmark shows render is the next hotspot.

### B.5 Initialization, lifecycle, fail-soft

`port/fast3d/swarm_gpu.cpp`:

```cpp
bool swarmGpuAvailable(void);           /* true iff GL >= 4.3 */
void swarmGpuInit(s32 max_count);
void swarmGpuShutdown(void);
void swarmGpuRespawn(s32 count, const struct coord *player_pos);
void swarmGpuSimStep(const struct coord *player_pos, float dt);
void swarmGpuReadbackPositions(struct coord *out_positions, s32 count);
```

If `swarmGpuAvailable()` returns false, the Settings dropdown shows
`Swarm - GPU Boids` greyed out with a tooltip:

> Requires OpenGL 4.3 (compute shaders). Your context is GL 3.0.

The CPU scenario remains available regardless of GL version.

## C. Swarm test behavior (shared CPU and GPU)

### C.1 Player setup at session start

Triggered on `lvSetupComplete` when `g_TestScenario.scen` is one of the swarm
variants:

1. `g_Vars.players[0]->invincible = 1` (existing field at types.h:2675;
   set by `cheatActivate(CHEAT_INVINCIBLE)` and by F7 dev hotkey).
2. Give all weapons via the existing `CHEAT_*` weapon-grant path
   (cheats.c:923, list around `CHEAT_DKMODE`/`CHEAT_INVINCIBLE`). Use whatever
   "all weapons" cheat already exists.
3. Bottomless ammo: set `g_Vars.players[0]->bondammo[*]` to max or set the
   existing `CHEAT_INFINITE_AMMO` flag if available.
4. HUD overlay armed.

### C.2 Skedar bots: 1 HP, seek-player AI

CPU path:
- Allocate via existing `botmgrAllocateBot(chrnum, aibotnum)`
  ([botmgr.c:33](src/game/botmgr.c:33)).
- Override `chr->maxdamage = 1.0f` and `chr->damage = 0.0f` post-allocation.
- Set `chr->myaction = MA_SWARM_TEST_SEEK` (new action). The action body is
  one helper that reads `g_Vars.players[0]->prop->pos`, computes a unit
  vector, drives `bwalkRunForwardThinking`-style velocity toward it. This
  is far cheaper than the full `MA_AIBOTMAINLOOP` and gives a deterministic
  baseline for the CPU number.
- body_id = `"base:skedar"` (catalog row 61 at
  [assetcatalog_base.c:307](port/src/assetcatalog_base.c:307)).
- head_id = catalog Skedar warrior head.

GPU path:
- Same chr allocation (per Section B.4 Option B-R-2).
- `chr->myaction = MA_SWARM_TEST_GPU_DRIVEN`. The action body is empty, the
  per-frame GPU readback writes `chr->prop->pos` directly before the next
  render. No CPU bot AI runs.
- Same 1 HP override.

### C.3 Cycler

D-pad-down (`SDL_CONTROLLER_BUTTON_DPAD_DOWN`) and `0` keyboard key trigger
the cycler. Sequence: 4 -> 8 -> 16 -> 32 -> 64 -> 128 -> 256 -> 4 (wrap).

On cycle:
1. Despawn all current swarm bots (CPU: `botmgrRemoveAll` and clear our
   ad-hoc swarm slot table; GPU: clear `state` bits in the SSBO and reset
   the swarm count param).
2. Spawn N new bots in a ring around the player at radius 600.0f units,
   evenly spaced in azimuth, with small random Y jitter so they spawn at
   ground level after the spawn-pool floor probe.

Input wire: new `ACTION_TESTSCEN_CYCLE_COUNT` action mapped to `KEY_0` and
`SDL_CONTROLLER_BUTTON_DPAD_DOWN` only when `g_TestScenario.scen` is a
swarm scenario.

### C.4 Scoring and death

Skedar deaths route through the existing chr-damage and kill-credit path
(`chraction.c` damage handler -> `mpHandleDeath` -> score increment). With
`maxdamage = 1.0f`, any successful hit kills. 1 point per kill. Existing
score path; no new scoring code.

### C.5 HUD overlay

New ImGui overlay rendered between the world frame and the menu pass via
the existing pdgui pre-frame entry (`pdguiNewFrame` at
[pdgui_backend.cpp](port/fast3d/pdgui_backend.cpp)). Hidden unless
`g_TestScenario.scen != TESTSCEN_NONE`.

Lines:

```
TEST SCENARIO   Swarm - GPU Boids
Map             Skedar Ruins
Method          GPU compute
Bot count       64           (next: 128)

CPU frame       3.42 ms
GPU frame       2.91 ms
Sim cost        0.18 ms      (GPU dispatch + readback)
Render          1.84 ms
Headroom        9.57 ms / 16.67 ms budget

[ D-pad Down or 0 ] Cycle bot count
[ Esc ]            Exit to main menu
```

Frame-time numbers are existing measurements from `mainTickProfileGet` if
that exists, or a small ad-hoc CPU/GPU timer wired here. GPU-frame is read
via `glQueryCounter(GL_TIMESTAMP)` with a 2-frame fence so the readback does
not stall.

### C.6 Logging

New log channel: `BENCHMARK.SWARM`, with sub-prefixes `BENCHMARK.SWARM.CPU`
and `BENCHMARK.SWARM.GPU` matching the directive's hierarchy. Plus
`TESTSCEN.*` for scenario lifecycle (start, stop, scenario change, cycle).

Per-frame line, throttled to once per 60 frames so the log stays small:

```
BENCHMARK.SWARM.GPU: count=64 cpu_ms=3.42 gpu_ms=2.91 sim_ms=0.18 render_ms=1.84 headroom=9.57
```

End-of-cycle summary (when count changes):

```
BENCHMARK.SWARM.GPU: SUMMARY count=64 frames=600 cpu_avg=3.41 cpu_p99=4.02 gpu_avg=2.89 gpu_p99=3.30 sim_avg=0.17 dropped=0
```

Channel registered alongside existing channels in
[pdgui_menu_mainmenu.cpp:3527](port/fast3d/pdgui_menu_mainmenu.cpp:3527)
so it shows in the Debug tab Log Channel Filters.

## D. Scope boundaries

- Not rewriting general bot handling. CPU bot system unchanged outside
  test mode.
- Test Scenarios menu surfaces in Settings > Debug, gated by `PD_DEV_BUILD`.
  Stable builds do not see this UI.
- Wire format unchanged. No `NET_PROTOCOL_VER` bump. Test mode is local-only;
  if the player is in a netplay session the dropdown is greyed out with a
  tooltip "Local sessions only".
- Save format unchanged. `g_TestScenario` is volatile, never persisted.
- Existing constraints respected:
  - Catalog ID strings used for body and arena resolution (no raw filenums).
  - `mainChangeToStage` reached via catalog stage resolve; no hardcoded
    stagenum.
  - Match-config init flows through existing helpers, not bespoke writes.
  - Capsule radius / spawn-pool invariants preserved (CPU mode bots come
    from `botmgrAllocateBot` which already routes through
    `scenarioChooseSpawnLocation` with the validated spawn pool).

## E. Testing

`pd-tests` cases (CMake adds them to `SRC_TESTS`):

- `tests/test_swarm_boid_sim.cpp`:
  - separation: two boids closer than `neighbor_radius` push each other apart
    over 1 step.
  - alignment: boid velocity converges toward neighbor mean velocity.
  - cohesion: boid velocity steers toward neighbor centroid.
  - target seek: boid with no neighbors moves toward `player_pos +
    target_offset` along the direction vector.
  - max-speed clamp: a boid with a large impulse never exceeds `max_speed`.
  - dead boids skipped: `state` bit 0 clear means the boid does not update.

  These cases run a CPU mock of the same math used inside the compute
  shader, so the test is a logic test of the algorithm, not a GL test.
  The compute shader gets the same algorithm, byte-for-byte if practical.

- `tests/test_testscenarios_launch.cpp`:
  - Launch with `TESTSCEN_EMPTY_MAP` writes the empty stage_id to
    `g_MatchConfig` and arms the scenario flag.
  - Launch with `TESTSCEN_SWARM_GPU` while `swarmGpuAvailable()` returns
    false short-circuits with a `LOG_WARNING` and does not start a stage.
  - Cycler advances 4 -> 8 -> 16 -> 32 -> 64 -> 128 -> 256 -> 4.

In-game verification (Mike runs):
- Each scenario launches without crash and arrives at the expected stage.
- Player is invincible, has full weapons, bottomless ammo.
- Skedars spawn in a ring, run at the player, die on first hit, score
  increments by 1 per kill.
- Cycler clears and respawns at each cap. HUD updates. Log records the
  per-count run.
- A/B comparison: same map, same bot count, swap method, eyeball the GPU
  vs CPU sim cost in the log summary.

## F. Decisions for Mike

### F.1 GL compute path (the big one)

**Stop condition surfaced.** The current GL context is 3.0 compatibility
([gfx_sdl2.cpp:164](port/fast3d/gfx_sdl2.cpp:164)) and glad does not export
GL 4.3+ symbols ([glad.h:2433](port/fast3d/glad/glad.h:2433) is the highest
version). Compute shaders need 4.3.

Three options:

- **Option A: regenerate glad with 4.3 core; prepend `(4, 3, CORE)` to the
  context probe array.**
  - Pro: real compute shaders, cleanest design, the SoA + binding code in
    Section B.3 ports unchanged.
  - Pro: PD2 is Windows-only x86_64 per CLAUDE.md; the macOS 4.1 ceiling
    in the current probe is dead weight.
  - Con: Tooling step (regen glad) and gfx_sdl2 probe edit. Renderer
    surgery is small but real. Estimate 0.5 day.
  - Con: Drops compatibility with hardware older than ~2012. Acceptable
    per the project's "modern hardware" stance in CLAUDE.md.

- **Option B: transform feedback on GL 3.0** (existing context).
  - Pro: zero renderer surgery. Works on every machine the game runs on.
  - Con: Less ergonomic for boid sim. No shared memory, no atomics,
    neighbor lookups via texture buffer object (TBO) sampling of a
    previous-frame state texture. Shader is more contorted. Maintenance
    cost when we extend the sim later.
  - Con: Fragment-shader-style GPGPU. Slightly slower than compute at
    256 boids; the difference is small but real.
  - Estimate: 1 day for the transform-feedback wiring + TBO neighbor
    sampling.

- **Option C: stage GL 4.3 only behind a feature gate.**
  - Try to create a 4.3 core context first; if it fails, fall back to the
    existing 3.0 context for everything else, but the GPU swarm scenario
    is greyed out.
  - Pro: zero risk to the rest of fast3d on machines without 4.3.
  - Con: Same surgery as Option A, plus a feature gate.
  - Estimate: 0.5-1 day.

**Recommendation: Option A.** Project is PC-Windows-only. GL 4.3 is from
2012. Glad regen is a one-time tooling step. The benchmark is pointless if
the GPU path can't actually run on Mike's machine; Option C reads like
Option A in practice.

**If A is approved**, Section B.3 lands as-written. **If B is approved**,
Section B.3 needs a rewrite of the shader (vertex shader writing to a
transform-feedback varying, with neighbor sampling via TBO), which I will
do as part of Phase 2 if Mike picks this.

### F.2 Bot count ceiling and chr pool size

`MAX_BOTS = 32` is an active constraint
([constraints.md "4-player bot limit" Removed entry](context/constraints.md))
and `g_MpBotChrPtrs[MAX_BOTS]` is hard-sized. The wire `participant active mask
caps at 64 slots` (MASTER-H3 in tasks-current.md) is also relevant, though
test mode is local-only and never crosses the wire.

The chr pool itself was expanded in Session 17:
NUMTYPE1=70 + NUMTYPE2=50 + NUMTYPE3=48 + NUMSPARE=80 = 248 chr slots total
across all types. 256 swarm bots **would exceed** the pool.

Three options:

- **F.2.A**: Cap the cycle at 128 instead of 256. Pool is fine. No code
  changes outside the cycler array.
- **F.2.B**: For the swarm test, allocate chrs but skip the
  `g_MpBotChrPtrs[]` and `g_MpAllChrPtrs[]` registration. This frees us
  from `MAX_BOTS = 32` for test mode, but still hits the 248 chr-pool
  ceiling at 256. Cap at 240 or so.
- **F.2.C**: Bump pool sizes. NUMTYPE2 from 50 to 100 takes us past 256
  comfortably. Cascade: any code that assumes pool sizes (none I'm aware
  of in test-mode-relevant paths). One-line memsizes change.

**Recommendation: F.2.B + F.2.C combined.** Use a separate test-mode
chr table (not `g_MpBotChrPtrs[]`) so bot allocation doesn't fight the
participant pool. Bump NUMTYPE2 to 100 so 256 fits with comfortable
headroom. Total memory cost is trivial and Mike asked for 256 as the top
of the cycle. Going to 128 just to stay inside the existing pool feels
like the wrong trade.

### F.3 Hit detection in GPU mode

Player bullets need to hit boids. Two options:

- **F.3.A: CPU-side broadphase using the GPU readback**, once per frame.
  After `swarmGpuReadbackPositions` updates each chr->prop->pos, the
  existing bullet/raycast code finds the chrs as normal and applies
  damage. Existing kill path triggers. This is the directive's "default
  option 1".
- **F.3.B: GPU-side hit detection**, reading bullet rays from a small SSBO
  the CPU writes per-frame, marking dead boids in the state array, CPU
  reads back kill events. Better for very high counts; complex to wire.

**Recommendation: F.3.A.** Matches the directive's default. Existing damage
infrastructure handles scoring, kill animation, audio, and ragdoll without
changes. Readback latency at 256 floats is microseconds. GPU-side hit
detection is the optimization to reach for if the benchmark shows CPU
bullet broadphase is the next hotspot.

### F.4 Empty Map source

"The Grid - Empty Map" needs an empty scene. Two options:

- **F.4.A: Reuse `STAGE_CITRAINING`.** Already used as the Grid editor
  canvas. It's not procedurally empty; it's the CI training map. But it
  does load fast and plays clean. Zero new content.
- **F.4.B: Procedural ground plane.** Generate a flat 1024x1024 textured
  quad at runtime, register it as a synthetic stage, load it as the Empty
  Map scenario. Real new content. Useful as a clean baseline for any
  future "no-map cost" benchmark.

**Recommendation: F.4.A for the first cut, F.4.B as a follow-on if the
empty-map number turns out to be muddied by CI Training's prop load.**
The directive itself says "test level if exists, or create one with just
a procedural ground plane" so this is the lighter-cost reading.

### F.5 CPU mode AI: full bot AI vs simple seek

The CPU mode could:

- **F.5.A**: Use the existing `MA_AIBOTMAINLOOP` action, which is the full
  MP bot AI (vision, target select, weapon, bwalk path follow, etc.).
  This gives a number representing "what 256 real bots cost today".
- **F.5.B**: Use a simplified seek-player action that mirrors the GPU
  shader logic. Comparable A/B against GPU.

**Recommendation: F.5.B for the headline benchmark, with a debug toggle to
flip CPU mode into full-AI**. The benchmark question is "is the GPU path
worth the build?" and F.5.B answers that cleanly: same algorithm, two
substrates. F.5.A answers a different (also useful) question: "how
expensive is the existing AI?". Both numbers are interesting; the simple
seek is the apples-to-apples comparison the directive seems to want.

## G. Decisions made during execution

(Placeholder. Phase 2 decisions go here as work progresses, with date and
rationale, so the doc tells the full story afterward.)

## Files (Phase 2 plan)

New:
- `port/include/testscenarios.h`
- `port/src/testscenarios.c`
- `port/include/swarm_test.h`
- `port/src/swarm_test.c`
- `port/include/swarm_gpu.h`            (Option A or C only)
- `port/fast3d/swarm_gpu.cpp`           (Option A or C only)
- `tests/test_swarm_boid_sim.cpp`
- `tests/test_testscenarios_launch.cpp`

Touched:
- `port/fast3d/pdgui_menu_mainmenu.cpp` (`renderSettingsDebug`: add Test
  Scenarios section).
- `port/fast3d/glad/glad.{c,h}` (Option A: regenerate at 4.3 core).
- `port/fast3d/gfx_sdl2.cpp` (Option A: prepend 4.3 core in the version
  probe array at line 164).
- `port/include/sysinclude.h` or similar (Option A: define
  `GFX_HAS_COMPUTE_SHADERS`).
- `src/include/game/chr.h` (`MA_SWARM_TEST_SEEK`,
  `MA_SWARM_TEST_GPU_DRIVEN` action enum entries).
- `src/game/chraction.c` (action handlers for the two new actions).
- `port/include/sysinclude.h` log channel registration (`LOG_CH_BENCHMARK`,
  `LOG_CH_TESTSCEN`).
- `CMakeLists.txt` (`SRC_TESTS` additions, `SRC_PORT` includes new files).
- `port/include/actionmap.h` + `port/src/actionmap.c`
  (`ACTION_TESTSCEN_CYCLE_COUNT`).
- Memsize bump (Option F.2.C): NUMTYPE2 50 -> 100.

Untouched:
- `port/src/net/*`. No wire change.
- `src/game/botmgr.c`. Existing entry points used.
- `src/game/bot.c::botSpawn`. Existing entry point used.
- Save files. No persistence.

## Stop condition outcomes (carried back to Mike's directive)

Three of Mike's listed stop conditions land on this doc:

- "fast3d can't compute-shader without major renderer surgery": confirmed
  partial. Glad regen + context-probe prepend is the surgery. Estimate
  0.5 day. Section F.1.
- "Hit detection design needs Mike's call (CPU readback vs full GPU)":
  Section F.3, recommendation F.3.A (CPU readback).
- "Save/wire format implications": none. Section D.

Two additional stop conditions discovered:

- Bot-count ceiling vs chr pool size at 256. Section F.2.
- CPU mode AI choice (full AI vs simple seek for fair A/B). Section F.5.
