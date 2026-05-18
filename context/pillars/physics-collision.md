# Physics / Collision

> Capsule sweep over the existing N64 geometry infrastructure. Replaces the legacy `cdTestVolume` / `cdFindGroundInfoAtCyl` hacks where a real geometric solution is needed (jump, stair-step, ground detection, ceiling). Movement runs through `bondwalk.c` and `bondmove.c`. Five movement modes (walk, bike, grab, cutscene, with hidden values for vehicle).

---

## What it is

Two layers cooperate:

1. **Legacy collision primitives** ([src/lib/cd.c](../../src/lib/cd.c)) - N64-era functions like `cdTestVolume`, `cdFindGroundInfoAtCyl`, `cdFindCeilingRoomYColourFlagsAtPos`. Still present, still used as the underlying geometry-test substrate. Returns colliding geometry IDs, ground info, ceiling info, room IDs.
2. **Capsule sweep** ([src/lib/capsule.c](../../src/lib/capsule.c), 281 lines) - samples the legacy primitives at multiple points along a swept capsule volume to produce a real geometric collision result for jumping / stair-step / ceiling-jump-through. `capsuleSweep(cast)` at line 37 is the entry point.

Movement is in [src/game/bondwalk.c](../../src/game/bondwalk.c) (2380 lines) and `bondmove.c`. The vertical pipeline (`bwalkUpdateVertical`) orchestrates jump initiation, gravity, ground re-acquisition, ceiling clamp, and ledge step-up using both layers.

---

## Capsule sweep

The capsule sweep is a swept volume test. The legacy `cdTestVolume` only tests a single static point; that misses cases where a fast-moving player passes through thin geometry. The sweep samples N intermediate positions along the path and runs the legacy test at each, producing a continuous collision result.

Per [src/lib/capsule.c:7-9](../../src/lib/capsule.c:7) the capsule sweep "uses the existing geometry collection and testing infrastructure (cdTestVolume, cdFindGroundInfoAtCyl, cdFindCeilingRoomYColourFlagsAtPos) but samples at" multiple capsule positions. This was the right shape for the integration: keep the N64 geometry pipeline, fix the algorithm.

Notable use sites in [capsule.c](../../src/lib/capsule.c):

- Line 73 - axis-aligned mid-sweep test.
- Line 156 - ground re-acquisition via `cdFindGroundInfoAtCyl`.
- Line 177 - mid-step volume test.
- Line 247 - probe-step volume test for stair-step climb.

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

- **D2c Bot Jump AI -- v1 SHIPPED 2026-05-17 (c038).** CPU-bot jumping in normal Combat Sim play with the existing `aibot` infrastructure. Toggle `MPOPTION_BOTJUMP` (default OFF) in CS Room setup. Decision = (target on higher platform) with difficulty-tiered reach threshold; execution writes `chr->fallspeed.y = 8.2f`. HARD+ bots get a +1.5 crouch-jump boost in the just-barely 30-60u zone. Wall-clock-budgeted scheduler (200 evals/sec across active bots, frame-rate independent). Telemetry: `BOT.JUMP: chrnum=N reason=X target_y=Y dy=N diff=N boost=N`. v1 trigger is reach-only; obstacle-jump (move-blocked-but-clear-above) deferred to a follow-up slice because the `aibot` struct doesn't currently expose a clean stuck-detection signal. Two-stage capsule sweep (the wall-jump-glitch lane this card originally tracked) still backlog.
- **Skedar swarm jump/surface parity -- VERIFIED 2026-05-18.** Benchmark-local helper now drives Skedar leap requests and wall/ceiling surface-transition requests for both CPU and GPU swarm paths. Verified with the focused static guard plus CPU/GPU `base:mp_skedar` behavior smokes. Normal Combat Sim bot jumping remains the D2c toggle above; this slice does not widen the general bot AI.
- **Slope-AABB adaptation.** Per the audit, slope handling on the AABB collision side was deferred. Players slide on steep inclines instead of being blocked.
- **Ceiling-jump-through.** Specific edge case where the upward sweep should pass through certain "passable" ceilings but currently does not. Deferred per Mike's directive.
- **Crouch-jump (player) SHIPPED 2026-05-17 (c036).** ACTION_JUMP press latches `g_BondCrouchJumpActive[pi]`; a fresh ACTION_CROUCH press while `bdeltapos.y > 0` (mid-jump) adds +1.5 to vertical velocity and consumes the latch. Clears surfaces slightly above the regular jump apex. Lives in [src/game/bondmove.c](../../src/game/bondmove.c) and pairs with the 3-state crouch model (STAND/DUCK/SQUAT) via the existing `crouchpos` field.

---

## Known gaps

- **Slope handling on AABBs is incomplete** (deferred work, per [audits/infrastructure-pillars-status-2026-04-27.md](../audits/infrastructure-pillars-status-2026-04-27.md)).
- **Ceiling-jump-through edge case** unhandled (same source).
- **Obstacle-driven bot jump AI** remains deferred; D2c v1 only jumps for target-height reach.
- **`bondwalk.c` is 2380 lines** and mixes vertical pipeline with horizontal pipeline with state machine bookkeeping. Inherited monolith; not new bloat. Splitting would clarify the capsule sweep call sites but is not scoped.

---

## Active design references

None today. Capsule + movement are in maintenance mode pending Bot Jump AI.

---

## Where to look

- For input action map bindings (jump, crouch, sprint): [pillars/input.md](input.md).
- For spawn pool that uses the capsule radius: see [systemic-bugs.md](../systemic-bugs.md) and the S249 / B-134 history in `_old/`.
- For Forge / The Grid free-fly that hijacks `MOVEMODE_CUTSCENE`: [designs/modding/forge-level-editor.md](../designs/modding/forge-level-editor.md).
- For the legacy collision primitives: [src/lib/cd.c](../../src/lib/cd.c) directly.
