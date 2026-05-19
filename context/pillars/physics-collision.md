# Physics / Collision

> Capsule sweep over the existing N64 geometry infrastructure. Replaces the legacy `cdTestVolume` / `cdFindGroundInfoAtCyl` hacks where a real geometric solution is needed (jump, stair-step, ground detection, ceiling). Movement runs through `bondwalk.c` and `bondmove.c`. Five movement modes (walk, bike, grab, cutscene, with hidden values for vehicle).

---

## What it is

Three layers cooperate:

1. **Legacy collision primitives** ([src/lib/cd.c](../../src/lib/cd.c)) - N64-era functions like `cdTestVolume`, `cdFindGroundInfoAtCyl`, `cdFindCeilingRoomYColourFlagsAtPos`. Still present, still used as the underlying geometry-test substrate. Returns colliding geometry IDs, ground info, ceiling info, room IDs.
2. **Collision-owned mesh geometry** ([src/lib/meshcollision.c](../../src/lib/meshcollision.c)) - owns rendered room triangles in the static world mesh and local-space `prop->colmesh` arrays for movement-solid dynamic props. Dynamic queries build transforms from stable object state (`prop->pos`, `defaultobj.realrot`) and must not read render frame matrices.
3. **Capsule sweep** ([src/lib/capsule.c](../../src/lib/capsule.c)) - samples the legacy primitives at multiple points along a swept capsule volume, then runs a mesh-backed Stage 2 over top/mid/bottom + lateral capsule skin samples. Stage 1 remains authoritative when Stage 2 misses; Stage 2 can only add an earlier mesh hit. `capsuleSweep(cast)` is the entry point.

Movement is in [src/game/bondwalk.c](../../src/game/bondwalk.c) (2380 lines) and `bondmove.c`. The vertical pipeline (`bwalkUpdateVertical`) orchestrates jump initiation, gravity, ground re-acquisition, ceiling clamp, and ledge step-up using both layers.

---

## Capsule sweep

The capsule sweep is a swept volume test. The legacy `cdTestVolume` only tests a single static point; that misses cases where a fast-moving player passes through thin geometry. The sweep samples N intermediate positions along the path and runs the legacy test at each, producing a continuous collision result.

Per [src/lib/capsule.c](../../src/lib/capsule.c), the capsule sweep keeps `cdTestVolume` as the conservative broadphase and supplements it with collision-owned static world and dynamic prop mesh ray probes. Surface normals are oriented against movement and classified as floor, ceiling, or wall before consumers act on them. Movement/capsule code must not call `propobj.c::func0f0849dc()` and must not read `model->matrices`; those remain weapon/object-hit concerns only.

Capsule Stage 2 diagnostics are development-only and must stay quiet by default at runtime. `CAPSULE_LOG` honors `Debug.JumpLogging` / `g_JumpLoggingEnabled`, and `CAPSULE:` messages route through the normal game log channel instead of bypassing channel filtering.

Notable use sites in [capsule.c](../../src/lib/capsule.c):

- `capsuleSweep` - generic `selfprop`-aware movement sweep for player and bot props.
- `capsuleMeshSweepSamples` - mesh-backed Stage 2 top/mid/bottom + lateral skin sample bundle.
- `capsuleFindFloorForProp` / `capsuleFindCeilingForProp` - floor/ceiling probes that keep the moving prop explicit.
- `capsuleFindRenderedFloor` / `capsuleFindRenderedCeiling` - legacy-named mesh-backed top/ceiling probes with normal filtering.

---

## Movement modes

[src/include/constants.h:2630-2633](../../src/include/constants.h:2630):

```
MOVEMODE_WALK     0
MOVEMODE_BIKE     3
MOVEMODE_GRAB     4
MOVEMODE_CUTSCENE 5
```

Note: gaps at 1 and 2 are historical (legacy unused values from the N64 codebase).

`MOVEMODE_CUTSCENE` is the hijacked movement mode used by the Forge / The Grid free-fly camera. The player chr is frozen; the freefly tick overrides `prop->pos`, `vv_theta`, and `vv_verta` directly while `MOVEMODE_CUTSCENE` is active.

---

## Vertical movement pipeline

`bwalkUpdateVertical` in [bondwalk.c](../../src/game/bondwalk.c) is the canonical vertical-axis driver. It performs:

1. Jump initiation if the jump button was just pressed and the player is on the ground.
2. Gravity integration on the airborne path.
3. Ceiling clamp if the upward sweep hits a ceiling.
4. Ground re-acquisition on the way down (capsule sweep + `cdFindGroundInfoAtCyl`).
5. Stair-step up if the horizontal motion is blocked by something at less than the step-up threshold.

Stationary jumping was the first capsule sweep validation case (D2b initial integration). Movement-during-jump and extended testing landed across S15-S40+ era.

---

## Crouch modes

Per [src/include/constants.h:4797+](../../src/include/constants.h:4797), crouch is a multi-state cycle (Hold / Analog / Toggle / Toggle+Analog), wired through the action map. See [pillars/input.md](input.md) for the binding side. The crouch cycle button was historically `BUTTON_CROUCH_CYCLE` (CONT_8000) on N64; on PC it routes through `ACTION_CROUCH` on the unified action map.

---

## Active invariants

Per [constraints.md](../constraints.md):

- **N64 collision workarounds removed** (since 2026-03-12). Legacy `cdTestVolume / cdFindGroundInfoAtCyl` ad-hoc hacks have been replaced by the capsule sweep system. Use proper geometric solutions, not the old hand-rolled fixes.
- **Spawn raycast-budget near-hit threshold = `SPAWNPOOL_CAPSULE_RADIUS` (30.0f)** (S249, B-134 fix). `spawnPoolRaycastBudget()` rejects any candidate with a per-ray hit distance below 30 units; the value MUST equal the player capsule radius. Prior threshold (5.0f) allowed players to spawn inside railings/thin geometry within the capsule volume. Declared in [src/include/game/spawnpool.h](../../src/include/game/spawnpool.h); do not shadow or localise.
- **Single local player only** (S188). Movement and physics arrays are sized for MAX_LOCAL_PLAYERS = 4 historically, but only Player 0 is initialized for IMCs and bindings on PC.

---

## What is done

- Capsule sweep system in production for jumping, stair-step, ground detection, ceiling clamp.
- Legacy geometry primitives retained as the substrate.
- Spawn pool capsule-radius threshold landed (B-134, S249).
- Crouch modes (Hold / Analog / Toggle / Toggle+Analog) wired through the action map.
- Forge / The Grid free-fly camera using `MOVEMODE_CUTSCENE` hijack pattern.

---

## What is in flight

- **c038 Jump surface collision fix -- IMPLEMENTED + BUILD/TEST VERIFIED 2026-05-18; pending playtest.** Replaced the incomplete single-center-ray Stage 2 with a generic multi-sample capsule sweep, then replaced the fragile render-owned prop/model substrate with collision-owned mesh data. Stage load now builds `g_WorldMesh` from rendered room triangles; movement-solid props attach local-space `prop->colmesh`; Forge pickups/weapon pads/pass-through/projectile-only objects are movement-pass-through. Dynamic prop collision meshes detach through `meshDetachAllStageProps()` before `MEMPOOL_STAGE` reset, not from stale props in `lvReset()`; the detach pass walks only `g_Vars.activeprops` and prop reset nulls `colmesh` for the pool. Bug ledger: B-335 + B-339 + B-343. Verification: queued isolated `jumpfix` client/tests/focused `[physics][jump]` PASS for the first solver; queued isolated `meshcol` client/updater PASS, tests target PASS, focused `[physics][jump]` PASS (64 assertions / 4 cases), server PASS for the mesh ownership fix; queued isolated `meshdn` client/updater PASS, tests target PASS, focused `[physics][jump]` PASS (85 assertions / 4 cases), server PASS for the detach-before-stage-reset fix; recursive session `smkmis` focused `[physics][jump]` PASS (92 assertions / 4 cases), client PASS, server PASS, `mission_intro_flow` PASS (18/18), and `auto_campaign_first_cycle` PASS (20/20) after reproducing/fixing the Defection -> Investigation stale-free-tail crash.
- **D2c Bot Jump AI -- v1 SHIPPED 2026-05-17 (c038), expanding in current c038 slice.** CPU-bot jumping in normal Combat Sim play with the existing `aibot` infrastructure. Toggle `MPOPTION_BOTJUMP` (default OFF) in CS Room setup. Reach trigger remains live; current c038 work adds the deferred obstacle trigger using solver probes: low forward sweep blocked, raised sweep clear, landing floor exists. Telemetry remains `BOT.JUMP: chrnum=N reason=X target_y=Y dy=N diff=N boost=N`.
- **Skedar swarm jump/surface parity -- VERIFIED 2026-05-18.** Benchmark-local helper now drives Skedar leap requests and wall/ceiling surface-transition requests for both CPU and GPU swarm paths. Verified with the focused static guard plus CPU/GPU `base:mp_skedar` behavior smokes. Normal Combat Sim bot jumping remains the D2c toggle above; this slice does not widen the general bot AI.
- **Slope-AABB adaptation.** Per the audit, slope handling on the AABB collision side was deferred. Players slide on steep inclines instead of being blocked.
- **Ceiling-jump-through.** Specific edge case where the upward sweep should pass through certain "passable" ceilings but currently does not. Deferred per Mike's directive.
- **Crouch-jump (player) SHIPPED 2026-05-17 (c036).** ACTION_JUMP press latches `g_BondCrouchJumpActive[pi]`; a fresh ACTION_CROUCH press while `bdeltapos.y > 0` (mid-jump) adds +1.5 to vertical velocity and consumes the latch. Clears surfaces slightly above the regular jump apex. Lives in [src/game/bondmove.c](../../src/game/bondmove.c) and pairs with the 3-state crouch model (STAND/DUCK/SQUAT) via the existing `crouchpos` field.

---

## Known gaps

- **Slope handling on AABBs is incomplete** (deferred work, per [audits/infrastructure-pillars-status-2026-04-27.md](../audits/infrastructure-pillars-status-2026-04-27.md)).
- **Ceiling-jump-through edge case** unhandled (same source).
- **Coordinate-specific wall-jump smoke** remains pending; the activation smoke is live, but repro-grade coverage needs Mike-captured bad-surface coordinates.
- **Static-vs-dynamic mesh baking is conservative in v1.** The current architecture gives every eligible solid prop a collision-owned dynamic mesh. Future performance work can bake proven-immovable static props/Grid structures into `g_WorldMesh`; do not reintroduce render-matrix movement collision to chase that optimization.
- **`bondwalk.c` is 2380 lines** and mixes vertical pipeline with horizontal pipeline with state machine bookkeeping. Inherited monolith; not new bloat. Splitting would clarify the capsule sweep call sites but is not scoped.

---

## Active design references

- [designs/physics-collision/jump-two-stage-sweep.md](../designs/physics-collision/jump-two-stage-sweep.md) - original c038 design and risk analysis. The live implementation has advanced beyond the design-only state.

---

## Where to look

- For input action map bindings (jump, crouch, sprint): [pillars/input.md](input.md).
- For spawn pool that uses the capsule radius: see [systemic-bugs.md](../systemic-bugs.md) and the S249 / B-134 history in `_old/`.
- For Forge / The Grid free-fly that hijacks `MOVEMODE_CUTSCENE`: [designs/modding/forge-level-editor.md](../designs/modding/forge-level-editor.md).
- For the legacy collision primitives: [src/lib/cd.c](../../src/lib/cd.c) directly.
