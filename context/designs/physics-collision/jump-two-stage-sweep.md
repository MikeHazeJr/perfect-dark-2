# Jump Two-Stage Capsule Sweep -- Design

Status: DESIGN -- pending Mike's approval. Date: 2026-05-13.

---

## Status / Premise

This document scaffolds the upgrade path for the player jump collision query so it stops missing rendered-only display-list geometry. Mike's framing for the choice, captured verbatim from the 2026-05-13 incompleteness audit Track 4 dialogue: "use the colliders the Laptop Gun uses OR full normal rendered geometry". The proposal below threads the needle by keeping the existing `cdTestVolume` cheap pre-cull AND adding a per-triangle validator drawn from the Laptop Gun's stick path. No live jump code is modified by this commit; this is the design + scaffolding that must land before implementation slices begin. Kanban card c038 (re-pillared to physics-collision in the same change) tracks the implementation arc once Mike approves.

## Problem statement

The player jump's collision query runs through `capsuleSweep` -> `cdTestVolume`, whose substrate is `GEOFLAG_WALL`-flagged BG tiles plus prop AABB blocks plus chr cylinders. That substrate is a **flagged collider mesh**, not the actually-rendered triangles. Any geometry rendered as a `G_TRI1`/`G_TRI4` triangle without a matching `GEOFLAG_WALL` collision skin is invisible to the jump sweep: the capsule walks straight through it. This is the structural explanation for the wall-jump glitch class tracked as kanban c038 (currently mis-pillared as "vehicles"). The Laptop Gun's sticky-projectile path proves the renderer-faithful geometry test already exists in the codebase via `bgTestHitInRoom` -- it just was never reused for player movement.

## Current call path

Jump (player vertical step) -- substrate = flagged tiles + AABB props + chr cylinders:

- `src/game/bondwalk.c:1255` -- `capsuleSweep(&sweep)` is the entry inside `bwalkUpdateVertical` (`bondwalk.c:849`).
- `src/lib/capsule.c:37` -- `capsuleSweep` iterates 16 sub-step positions along the move vector.
- `src/lib/capsule.c:73` -- each sub-step calls `cdTestVolume(...)`.
- `src/lib/collision.c:2402` -- `cdTestVolume` defers to `cdCollectGeoForCyl` with `GEOFLAG_WALL` (`collision.c:2407`). It returns a binary collide / no-collide. It collects: BG `geotilei` / `geotilef` tiles flagged `GEOFLAG_WALL`, prop `geoblock` AABBs flagged `GEOFLAG_WALL`, prop auto-generated floor tiles, and chr `geocyl` cylinders (when `cdtypes & CDTYPE_CHR`).
- `src/lib/capsule.c:122-124` -- the returned `hitnormal` is the negated, normalized movement direction. The comment at `capsule.c:113-118` explicitly calls this an approximation -- "We don't need a precise normal -- just the negated, normalized movement direction".

Laptop Gun (sticky projectile per-tick) -- substrate = actually-rendered triangles:

- `src/game/propobj.c:7195-7199` -- branches on `PROJECTILEFLAG_STICKY`.
- `src/game/propobj.c:3530` -- sticky branch calls `func0f06cd00(obj, &sp5dc, &sp5e8, &sp5f4)`.
- `src/game/propobj.c:3575` -- per traversed room calls `bgTestHitInRoom(&prop->pos, &sp1c4, spcc[i], &hitthing)`.
- `src/game/bg.c:4471` -- `bgTestHitInRoom` walks `g_Rooms[roomnum].vtxbatches[]` (`bg.c:4512`).
- `src/game/bg.c:4162` -- `bgTestHitInVtxBatch` walks `G_TRI1` / `G_TRI4` GBI commands and tests the actual rendered triangles, returning a per-triangle face normal in `hitthing.unk0c`.
- `src/game/bg.c:3295` -- `bgPopulateVtxBatchType` lazily populates `vtxbatches` from BG `G_VTX` display lists on first room entry. Each batch carries `bbmin` / `bbmax` AABB for early-out.
- Note `propobj.c:7222-7226` ("Thrown laptops can stick to the BG but not props") -- the laptop's sticky filter is intentional and is the only reason its path excludes props and chrs.

## Proposed two-stage design

Two-stage capsule sweep:

1. **Stage 1 (existing) -- pre-cull.** Run the current `cdTestVolume`-based sub-step iteration. Cheap. If no sub-step ever returns `CDRESULT_COLLISION`, return `safefrac = 1.0f` exactly as today. No regression for the common no-collision frame.
2. **Stage 2 (new) -- per-triangle validate.** At the sub-step where Stage 1 reported `CDRESULT_COLLISION`, fire a small bundle of rays (capsule center + lateral skin samples at top / mid / bottom = approximately 15 rays per step) through `bgTestHitInRoom` against `g_Rooms[].vtxbatches`. Take the closest hit as the authoritative safefrac. Surface normal is `hitthing.unk0c` (real per-triangle face normal), replacing the `-move/|move|` approximation at `capsule.c:122-124`.

Why two-stage and not pure-rendered-triangle replacement:

- `cdTestVolume` is O(geo-in-room) cylinder-vs-tile bounding tests. `bgTestHitInRoom` is O(numbatches) AABB cull then per-tri test. For frames where the player is in open space neither query finds a hit, but the rendered-tri path does more bounding-volume math per call. Keeping Stage 1 as the gate avoids that cost on the dominant no-collision case.
- Stage 1's coverage of chr cylinders and props is a strict superset of the Laptop Gun's BG-only path; replacing wholesale would lose chr / prop coverage. Two-stage keeps the existing coverage AND adds the missing rendered-tri coverage.
- Stage 2 can run with a Mike-tunable bypass (Boot.ini flag or compile-time gate) during initial slices so a regression is one-flag reversible.

## API plan

Extend the existing `capsulecast` struct (live definition near `src/include/lib/capsule.h`):

- Add `u8 hitfromrendered;` -- 1 if Stage 2 produced the authoritative hit, 0 if Stage 1 only.
- Add `struct coord realnormal;` -- per-triangle normal from `bgTestHitInVtxBatch::hitthing.unk0c` when `hitfromrendered == 1`. The existing `hitnormal` field stays put for back-compat; consumers that need the real normal opt in by reading `realnormal` when `hitfromrendered == 1`.

Add a parallel entry point `f32 capsuleSweepTwoStage(struct capsulecast *cast)` that performs Stage 1 + Stage 2 and is functionally a superset of `capsuleSweep`. Consumers opt in at their own pace; the existing `capsuleSweep` is unchanged.

Initial consumer migration order:

1. `src/game/bondwalk.c::bwalkUpdateVertical` at `bondwalk.c:1255` -- the wall-jump glitch site. Highest player-visible impact, lowest API churn risk.
2. `src/game/bondwalk.c::bwalkTryMoveUpwards` (`bondwalk.c:258`) -- consumed by ladder / stair-step / ceiling clamp. Migrate after the first consumer ships and a playtest gate confirms zero regressions.
3. Horizontal-step sites in `bondwalk.c` / `bondmove.c` -- deferred to a follow-up slice; not required to close c038.

## Per-frame cost analysis

- Laptop's path runs per projectile per frame today. There are 0-16 sticky laptops live at any time, so 16 budget units are already absorbed elsewhere in the frame.
- Player jump runs `capsuleSweep` once per frame per local player; this PC port has a single local player by constraint.
- `vtxbatches` AABB cull is O(numbatches), bounded ~10-100 per room. Per-tri test loop body at `bg.c:4209-4262` is the SIMD-friendly axis-cull + plane test.
- Worst-case ray budget per Stage 2 invocation: 15 rays per failing step * up to 16 steps but only one step actually fails per sweep, so 15 rays per sweep. Estimate <50us per sweep on x86_64.
- Stage 2 only runs when Stage 1 reported `CDRESULT_COLLISION`. Open-space frames pay zero Stage 2 cost.

## Surface-normal unlock

Real per-triangle normals from Stage 2 unlock several deferred items:

- **Skedar surface locomotion Slice 5** -- `context/designs/in-flight/skedar-surface-normal-locomotion.md`. Slice 5 needs `surface_up` per chr; the real normal at the foot-contact triangle is the input it has been waiting for.
- **Wall-slide for player movement** -- horizontal step blocked by a wall currently produces a clamp; with the real normal it can resolve into slide along the wall plane.
- **Scary-jump landing alignment** -- per the Slice 5 plan, animation blend at touchdown needs `surface_up`. Today the capsule sweep returns `-move/|move|`, which is useless for alignment.
- **Proper edge-detection for surface-loco chrs** -- the same `bgTestHitInVtxBatch` plane test exposes triangle edges; surface-loco bots can use them to find walkable edges.

## Coverage parity

The Laptop Gun's path excludes props and chrs by intent (`propobj.c:7222-7226`). Player jump cannot exclude either: the player must still collide with prop AABBs (doors, breakables, weapons-on-floor) and chr cylinders (bots, players). The two-stage design preserves Stage 1's existing prop and chr coverage and adds Stage 2's per-triangle BG validate. Net coverage is a strict superset of the current jump:

| Surface | Today | Stage 1 (post) | Stage 2 (post) | Net |
|---|---|---|---|---|
| BG `geotilei` / `geotilef` flagged `WALL` | YES | YES | YES (redundant but cheap) | YES |
| Prop `geoblock` AABBs flagged `WALL` | YES | YES | NO (intentional) | YES via Stage 1 |
| Prop auto-generated floor tiles | YES | YES | NO | YES via Stage 1 |
| Chr `geocyl` cylinders | YES | YES | NO | YES via Stage 1 |
| Rendered display-list triangles with no matching `WALL` tile | NO | NO | YES | YES via Stage 2 |
| Real per-triangle hit normal | NO (approx) | NO (approx) | YES | YES via Stage 2 |

## Migration plan

1. **Land the new API alongside the old `capsuleSweep`** -- no consumer swap. Add unit tests under `tests/` that exercise the two-stage path against a hand-rolled `vtxbatches` fixture covering the wall-jump glitch repro (rendered triangle, no matching `GEOFLAG_WALL` tile, capsule sweep enters the triangle volume). Acceptance: Stage 2 returns `safefrac < 1.0f` for the rendered tri, Stage 1 alone returns `safefrac == 1.0f`.
2. **Swap `bwalkUpdateVertical`** at `bondwalk.c:1255` to `capsuleSweepTwoStage`. Build verify (`build-headless.ps1`). Playtest gate per `context/procedures.md`. Acceptance: wall-jump glitch repro now produces a `CAPSULE_SWEEP: hit ...` log line with `hittype=WALL` at the jump apex.
3. **Update Skedar surface-loco to consume the real normal** -- closes the Slice 5 dependency. Wire `realnormal` into the `surface_up` sample.
4. **Migrate `bwalkTryMoveUpwards`** and the horizontal-step sites as opportunity arises. Each migration is its own slice with its own playtest gate.

After all consumers migrate, decide whether to retire `capsuleSweep` (see Open Question 3 below).

## Risks

(a) **Prop-bbox vs rendered-triangle disagreement.** Some props have a `GEOFLAG_WALL` AABB skin that is more generous than the rendered triangles (a thin door modeled with a fat collider). Stage 2 might say "no hit" against the rendered triangle while Stage 1 says "hit" against the AABB. The proposal as written takes Stage 2 as authoritative; if Stage 2 misses by triangle, the player passes through the AABB skin. Mitigation: only let Stage 2 OVERRIDE Stage 1 if Stage 2 reports a closer hit; otherwise keep Stage 1's safefrac. This is the conservative default.

(b) **Hit normal sign convention.** `bgTestHitInVtxBatch` returns a normal whose sign convention may not match capsule sweep consumer expectations. Mike, audit consumers (Skedar Slice 5, wall-slide candidate sites, scary-jump landing) before flipping. The current approximation `-move/|move|` is by convention "toward the player", so any consumer that flipped to compensate will need re-checking once the real normal lands.

(c) **`vtxbatches` lazy population.** `bgPopulateVtxBatchType` (`bg.c:3295`) runs on first room entry; rooms entered for the first time mid-jump would have a NULL `vtxbatches` until population completes. Audit confirms the jump path is post-population in practice (room entry happens during `bmoveFindEnteredRoomsByPos` before `capsuleSweep`), but pin it with a Stage 2 test that asserts `g_Rooms[roomnum].vtxbatches != NULL` before walking.

(d) **`cdGetObstacleNormal` coordinate space.** `bgTestHitInVtxBatch` writes normals in world-space N64 units. Capsule sweep's existing `hitnormal` is also world-space, so the orientation should match. Pin it with a test that throws a known-orientation triangle and checks the normal direction.

## Open questions for Mike

1. **Two-stage vs alternative.** Approve the two-stage approach as scoped here, or do you want to consider retiring `cdTestVolume` entirely in favor of a single rendered-triangle test plus a separate chr / prop pass?
2. **First-consumer slice.** Migrate `bwalkUpdateVertical` first as the c038 fix, or wait to bundle it with `bwalkTryMoveUpwards` and the horizontal-step sites in a single larger slice?
3. **Old API retirement.** After all consumers migrate, retire the old `capsuleSweep` entry point, or keep it as a fallback for emergency rollback?
4. **Horizontal step scope.** Extend the new API to player walk (`bwalkTryMoveHoriz` and related sites in `bondwalk.c` / `bondmove.c`) in the same arc, or defer the horizontal pipeline to a separate sprint after the vertical pipeline ships?
5. **Skedar dependency tracking.** Track Skedar Slice 5's dependency on this work in `tools/kanban/state.json`, or let Skedar wait until its own playtest gate fires first (which would expose the dependency naturally)?

## Where to look

- `src/game/bondwalk.c:849, 1255` -- jump impulse and the capsule sweep call site.
- `src/lib/capsule.c:37, 73, 122` -- `capsuleSweep`, the per-step `cdTestVolume` call, and the normal approximation.
- `src/lib/collision.c:2402` -- `cdTestVolume`.
- `src/game/propobj.c:3530, 3575, 7195-7226` -- Laptop Gun sticky path and the "Thrown laptops can stick to the BG but not props" comment.
- `src/game/bg.c:3295, 4162, 4471` -- `bgPopulateVtxBatchType`, `bgTestHitInVtxBatch`, `bgTestHitInRoom`.
- `context/pillars/physics-collision.md` -- live pillar status.
- `context/audits/incompleteness-sweep-input-context-extraction-jump-2026-05-13.md` Track 4 sections 4.1-4.11 -- audit findings that grounded this design.
- `context/designs/in-flight/skedar-surface-normal-locomotion.md` -- the consumer that unblocks once real normals land.
- `tools/kanban/state.json` -- card c038 (re-pillared to physics-collision in this commit).
