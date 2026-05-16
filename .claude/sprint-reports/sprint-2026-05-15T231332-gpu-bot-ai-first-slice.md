# Sprint 2026-05-15: GPU bot AI first slice (B-308 partial, c3807)

Track: GPU swarm bot AI pipeline (Track A4)
Card anchor: c3807 ("GPU swarm benchmark debug tab Phase 2", closest open kanban card)
Bug: B-308 (LOW, was OPEN, now PARTIAL)
Status: SHIPPED — slice 1 scaffolding + heuristic + readback consumer; CPU side-effects deferred

## Slice scope shipped

1. **`swarm_method_t` expanded** in `port/include/testscenarios.h`:
   `{ SWARM_METHOD_CPU = 0, SWARM_METHOD_GPU_POS_ONLY = 1, SWARM_METHOD_GPU_FULL = 2, SWARM_METHOD_COUNT }`. `SWARM_METHOD_GPU` retained as backward-compat alias for `GPU_POS_ONLY` so unrelated call sites in `pdgui_backend.cpp` and `swarm_test.c` keep compiling.

2. **Per-bot readback struct grew 48 -> 80 bytes** (`port/fast3d/swarm_gpu.cpp::struct boid_record`):
   - vec4 0: `px, py, pz, _pad_p`   (position, existing)
   - vec4 1: `vx, vy, vz, _pad_v`   (velocity, existing)
   - vec4 2: `sux, suy, suz, _pad_u` (surface_up, existing)
   - **vec4 3 NEW**: `action_class (int), fire_request (int), target_propnum (int), anim_key (int)`
   - **vec4 4 NEW**: `range_to_target (float), _pad_r0..r2`

   At 4096 bots = 320 KB SSBO and 320 KB readback per frame. Within trivial GL 4.3 limits.

3. **GPU AI step** (`port/fast3d/swarm_gpu.cpp::kSwarmCs`):
   - One shader, branches on `Params.do_ai` uniform (0 -> POS_ONLY, 1 -> GPU_FULL).
   - When `do_ai != 0`: kernel writes per-bot decisions based on 3D range to player.
     - `range < FIRE_RANGE (500)` -> `action_class=ATTACK(2)`, `fire_request=1`, `anim_key=ATTACK(2)`
     - `FIRE_RANGE <= range < ATTACK_RANGE (1500)` -> `SEEK(1)`, no fire, `WALK(1)`
     - `range >= ATTACK_RANGE` -> `SEEK(1)` (still moving), `WALK(1)`
   - Heuristic intentionally over-fires (FIRE_RANGE = 500 game units = ~3x Skedar collision diameter) so the first GPU_FULL playtest log shows hundreds of bots requesting fire — that's the verification signal.

4. **CPU readback consumer** (`swarm_gpu.cpp::swarmGpuStepAndApply` AI block, 60-frame throttled):
   - Walks readback after the position-apply loop.
   - Counts action class distribution + fire requests + range stats.
   - Emits `BENCHMARK.SWARM.GPU.AI` summary at ~1 Hz with `count=N idle=X seek=Y attack=Z fire_req=N range_min/avg/max=N target_propnum=N`.
   - Bursts up to 8 `BENCHMARK.SWARM.GPU.AI.FIRE` lines per cycle for specific firing slots (slot, chrnum, range, target_propnum, anim_key).
   - **Projectile spawn / animation side effects are NOT done in this slice** (see Deferred below).

5. **Runtime cycler** (`ACTION_TESTSCEN_GPU_FULL_TOGGLE`, KEY_O):
   - New action in `port/include/actionmap.h` (= 116; ACTION_COUNT now 117).
   - Bind on gameplay IMC at `port/src/actionmap.cpp:2493` (KEY_O, SDL scan 18).
   - Name mapping entry at `port/src/actionmap.cpp:3187`.
   - Consumer in `port/src/swarm_test.c::swarmTestTick` calls `testScenarioCycleGpuSubmode()`.
   - `testScenarioCycleGpuSubmode` in `port/src/testscenarios.c` swaps `s_State.method` between `GPU_POS_ONLY` and `GPU_FULL` when the active scenario is `TESTSCEN_SWARM_GPU`; no-op + warning log otherwise. Does NOT respawn chrs (both modes share the same passive-prop chr layout).

6. **HUD label** (`port/fast3d/pdgui_backend.cpp:1042`):
   - "GPU Boids" -> "GPU Boids (pos-only)" or "GPU Boids (AI on)" based on method.
   - Hint line added: "O                GPU AI toggle".

7. **`testScenarioLaunch` for TESTSCEN_SWARM_GPU now sets method = SWARM_METHOD_GPU_POS_ONLY** (was just SWARM_METHOD_GPU which now aliases to POS_ONLY anyway — explicit for clarity). Existing smoke tests fire the legacy POS_ONLY path unchanged.

8. **`swarmGpuStepAndApply` signature extended** from `(player_pos, chrs, count)` to `(player_pos, chrs, count, method, player_propnum)`. Call site in `port/src/swarm_test.c::swarmTestTick:1547` updated; method now matches `GPU_POS_ONLY || GPU_FULL`.

## Slice scope deferred (next dispatch pickup)

Concrete file:line pickup points:

- **(a) Fire-request -> projectile spawn**.
  - Pickup: `port/fast3d/swarm_gpu.cpp::swarmGpuStepAndApply` AI block (the `if (s_BoidScratch[i].fire_request) { sysLogPrintf(...); }` log loop at ~line 660). Replace the log emission with a real CPU-side fire dispatch.
  - **Structural blocker**: the existing fire path (`bot.c::botTick` -> `aibotSelectTarget` -> `bgunFire`) all consume `chr->aibot`, which is NULL for GPU bots by design. The next slice needs to either (i) invent a "fire one shot now" entry point that takes `chr*` + `target_propnum` + `weapon_id` and skips the bot AI state machine, or (ii) flip GPU_FULL to give each chr an aibot anyway and surgically route only target/aim from GPU while CPU does projectile spawn. Option (i) is the cleaner Tier 3 architecture per the design doc; option (ii) is faster.

- **(b) anim_key -> chr animation**.
  - Pickup: `port/fast3d/swarm_gpu.cpp::swarmGpuStepAndApply` AI block, after fire dispatch. Translate `anim_key` (0=stand 1=walk 2=attack) to `chr->myaction` / `chr->actiontype` / animlist key.
  - **Structural blocker**: `chr->myaction = MA_NONE` for GPU bots; the engine's animation tick consumes this. Switching to `MA_AIBOTMAINLOOP` requires the full bot AI which we're not running. Need to research whether `MA_GENERIC_RIDE` or `MA_PATROL` can be driven by an externally-set animlist without re-routing into bot AI. Cheap alternative: just `modelSetAnim(chr->model, ANIM_KEY)` directly, bypassing the action machinery.

- **(c) Async readback (PBO + fence)**.
  - Pickup: `port/fast3d/swarm_gpu.cpp::swarmGpuStepAndApply::glGetBufferSubData` call. Replace with a ring of 2-3 GL_PIXEL_PACK_BUFFER PBOs and a `glClientWaitSync` fence. Adds 1-2 frame latency to AI decisions; acceptable for a "GPU bot wants to fire" because the fire request can lag a frame. At 4096 bots * 80 B = 320 KB readback per frame — blocking is OK today, will stall the pipeline once GPU_FULL becomes the default-on path.

- **(d) Wider AI scope** per `context/designs/in-flight/gpu-swarm-bot-pipeline.md` Tier 1:
  - Per-bot target selection (currently always the local player, hardcoded uniform).
  - LOS / visibility (currently always assumed visible, matches CPU's S594h-A2 ALWAYS_SEE mode).
  - Weapon-range tables per bot config (currently a single FIRE_RANGE uniform).
  - Dodge / cover / hill state.

## Readback struct field list

```c
struct boid_record {                       /* 80 bytes total, std430 layout */
    float px, py, pz, _pad_p;              /* offset  0: position */
    float vx, vy, vz, _pad_v;              /* offset 16: velocity */
    float sux, suy, suz, _pad_u;           /* offset 32: surface_up */
    int   action_class;                    /* offset 48: 0=idle 1=seek 2=attack */
    int   fire_request;                    /* offset 52: 0|1 */
    int   target_propnum;                  /* offset 56: prop index in g_Vars.props[] */
    int   anim_key;                        /* offset 60: 0=stand 1=walk 2=attack */
    float range_to_target;                 /* offset 64: 3D distance */
    float _pad_r0, _pad_r1, _pad_r2;       /* offset 68-76: padding to 80 */
};
```

## GPU_FULL heuristic (slice 1)

```glsl
float range = length(player_pos - bot_pos);  /* full 3D, NOT plane-projected */

if (range < FIRE_RANGE)  {              /* default 500 game units */
    action_class = ATTACK;              /* 2 */
    fire_request = 1;
    anim_key     = ATTACK;              /* 2 */
} else if (range < ATTACK_RANGE) {      /* default 1500 game units */
    action_class = SEEK;                /* 1 */
    fire_request = 0;
    anim_key     = WALK;                /* 1 */
} else {
    action_class = SEEK;                /* still seeking, no fire */
    fire_request = 0;
    anim_key     = WALK;
}
target_propnum    = P.player_propnum;
range_to_target   = range;
```

`FIRE_RANGE` is intentionally generous (3x Skedar collision diameter) so the first GPU_FULL playtest produces hundreds of `BENCHMARK.SWARM.GPU.AI.FIRE` log lines — that's the round-trip success signal.

## Build verification

```text
build-headless.ps1 -Target all     -> CLIENT PASS 18s, UPDATER PASS 0s
build-headless.ps1 -Target server  -> SERVER PASS 11s
build-headless.ps1 -Target tests   -> TESTS PASS 0s (no test source changed)
```

All four targets clean. No new warnings.

## Smoke verification

```text
swarm_cpu_smoke    PASS 19/19  95.5s
swarm_gpu_smoke    PASS 19/19  150.6s
```

The GPU smoke runs through `BENCHMARK.SWARM.GPU` 247 times across ladder 4 -> 128. Zero `BENCHMARK.SWARM.GPU.AI` lines, which is correct — the smoke does NOT press KEY_O so GPU_FULL is never armed; the default POS_ONLY behaviour is byte-for-byte preserved.

Caveat: prior smoke runs in this dispatch failed initially because stale rotated log files (`pd-client.1.log`, `pd-client.3.log`, etc.) from earlier test invocations confused the assertion engine. Cleaning `.claude/smoke-verify-install/pd-*.log` between runs fixed it. Not a code regression. The smoke harness should arguably wipe rotated logs too — filed as an environment note, not a bug.

## Manual visual check

Not performed in this dispatch. Mike (game director) will verify by:
1. Boot Perfect Dark, enter Settings -> Debug -> Test Scenarios.
2. Pick "Swarm - GPU Boids", launch.
3. HUD shows "GPU Boids (pos-only)".
4. Press O — HUD swaps to "GPU Boids (AI on)" and log starts emitting `BENCHMARK.SWARM.GPU.AI: count=4 idle=0 seek=4 attack=0 fire_req=0 ...` every second.
5. Walk close to a bot (range < 500). The summary line should show `attack=N fire_req=N` non-zero. Burst log shows specific firing slots.
6. Cycle ladder via PgUp to 128 bots. Summary count tracks. Per-frame log volume stays bounded (60-frame throttle).

If the slice 2 work (projectile spawn) lands, the visible check becomes "bots actually shoot at me, dealing damage". This slice's check is "log lines confirm round trip".

## Performance note

At 4096 bots:
- SSBO upload: 4096 * 80 B = 320 KB per frame, `glBufferSubData`. Trivial.
- Compute dispatch: 4096 / 64 = 64 groups. Trivial.
- Readback: 4096 * 80 B = 320 KB per frame, **blocking `glGetBufferSubData`**. This is a 5-15 ms stall depending on driver — fine at 60 Hz today, NOT fine if GPU_FULL becomes the always-on path. Filed as deferred item (c) above.

## Files touched

- `port/include/testscenarios.h` (enum expansion + cycle accessor proto)
- `port/src/testscenarios.c` (launch path + `testScenarioCycleGpuSubmode`)
- `port/include/actionmap.h` (`ACTION_TESTSCEN_GPU_FULL_TOGGLE`)
- `port/src/actionmap.cpp` (bind + name mapping)
- `port/include/swarm_test.h` (no change — extern decls stay in swarm_test.c)
- `port/src/swarm_test.c` (extern + call-site + input handler + GPU label fixes)
- `port/fast3d/swarm_gpu.cpp` (struct + shader + new params + consumer)
- `port/fast3d/pdgui_backend.cpp` (HUD label + hint line)
- `context/bugs.md` (B-308 PARTIAL status note)

No CMakeLists, no header generator output, no smoke JSON changes.

## Risk

LOW. All changes are additive:
- New enum value (GPU_FULL); existing comparisons against SWARM_METHOD_GPU still resolve via the alias.
- New action (id=116); ACTION_COUNT bump is a pure-additive count change.
- SSBO grew 48->80 bytes per bot — `s_BoidScratch[SWARM_GPU_MAX]` is BSS, no runtime allocation change. The legacy 48-byte block sits at offset 0..47, byte-identical to before; the new 32 bytes at offset 48..79 are only consumed when `do_ai != 0` (which happens only after KEY_O is pressed on a GPU scenario).
- Method default at launch unchanged (TESTSCEN_SWARM_GPU -> POS_ONLY).
- KEY_O is otherwise unbound in the gameplay IMC (vacant scan code in the test scenarios block, adjacent to KEY_I for vis toggle).

The deferred items (projectile spawn, anim writeback, async readback) are independent and can each be tackled in isolation in future slices.
