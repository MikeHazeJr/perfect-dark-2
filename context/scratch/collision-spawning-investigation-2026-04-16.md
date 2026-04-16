# Collision, Jump, and Spawning Ecosystem — Deep Investigation

> Date: 2026-04-16
> Scope: Five playtest-reported issues. Investigation only — no code changes.
> Worktree: `infallible-turing` (branch `claude/infallible-turing`). Report will be committed on `dev`.
> Inputs: five parallel diagnostic agents over `src/game/`, `src/lib/`, `port/src/`.

---

## TL;DR — Root Causes

| # | Issue | Root cause | Severity |
|---|-------|-----------|----------|
| 1 | Jump clips through ceilings | Mesh ceiling probe not wired; legacy probes have coverage gaps (`cdTestVolume` is WALL-only, point-probe has no radius) | High |
| 2 | CI tables solid only at rim | Tables have rim `GEOTYPE_TILE_F` walls but no top-face geo; PC auto-floor is bypassed because model has a (useless) `MODELPART_BASIC_0065` part | High |
| 3 | Jump suppressed on slopes | `grounded` heuristic in `bondwalk.c:924-928` requires `bdeltapos.y < 2.0f`; slope-ascent residual velocity exceeds this | High |
| 4 | Pickups act as floors | `OBJFLAG3_WALKTHROUGH` not set on weapon / ammocrate creation paths (SP/host); net-received path has it correctly | High |
| 5 | Spawn ecosystem | L4 radial layer skips validation; ground sentinel unguarded; ray directions asymmetric; no same-tick reservation; pool only tests BG (misses props) | Medium-High |

None of these are fixed by this investigation — this is diagnosis and a menu of proposed fixes for Mike to choose from.

---

## Issue 1 — Player can jump through ceilings

### Current integration points in `src/game/bondwalk.c::bwalkUpdateVertical()` (starts at line 848)

| Stage | Lines | Purpose |
|---|---|---|
| Floor detection (primary + prop fallback) | 994-1072 | `cdFindGroundInfoAtCyl` → `cdTestVolume` probe → `capsuleFindFloor` |
| Airborne branch entry | 1200 | `if (vv_manground > vv_ground)` |
| Pre-move capsule sweep | 1227-1275 | `capsuleSweep` (catches wall hits correctly) |
| Pre-move "geo ceiling" clamp | 1281-1315 | `cdFindCeilingRoomYColourFlagsAtPos` + headheight |
| `bwalkTryMoveUpwards` actual move | 1317 | wraps `cdTestVolume` at new pos |
| Post-move `capsuleFindCeiling` | 1337-1373 | only runs inside `if (fallspeed > 0.0f)` |

### Why ceilings fail to stop jumps

Four independent gaps stack, so the hole the player exploits differs per-level:

1. **Mesh ceiling system is not wired.** `meshFindCeiling` (`src/lib/meshcollision.c:885`) and `meshSweepCapsuleWorld` (`meshcollision.c:646`) exist, are declared in the header, but **have zero call sites in `src/game/`**. `context/collision.md` describes the mesh system as "primary for player movement" — in practice, for vertical motion, it is offline.
2. **`capsuleSweep` is blind to FLOOR-flagged ceilings.** It uses `cdTestVolume` internally, which filters on `GEOFLAG_WALL` only (`src/lib/CLAUDE.md`). PD's BG ceilings are FLOOR1/FLOOR2 tiles classified via normal direction. Upward sweep misses them.
3. **Pre-move ceiling clamp is a point-probe.** `cdFindCeilingRoomYColourFlagsAtPos(&ceilpos, ...)` at `bondwalk.c:1288` takes a single `ceilpos` (player XZ center), no radius. A player clipping an edge-of-ceiling diagonally misses the probe.
4. **Post-move `capsuleFindCeiling` is a correction, not a prevention.** `bwalkTryMoveUpwards` (`bondwalk.c:294`) already wrote `prop->pos.y` up before the probe runs. Worse, the probe's upward binary search itself uses `cdTestVolume` (`capsule.c:247`) — same WALL-only blindness.

Secondary: `meshcollision.c::classifyTriFlags` (line 149-162) aliases ceilings to `GEOFLAG_FLOOR2` — even if the mesh probe were wired up, consumers would need an honest ceiling flag, and `meshFindCeiling` itself doesn't filter on normal direction (it returns any triangle above the player, which includes the floor of the room above in multi-story maps).

### Proposed fixes (no implementation)

- **A. Minimal (~5 lines).** Make the pre-move probe radius-aware: sample `cdFindCeilingRoomYColourFlagsAtPos` at center + 4 points at ±radius in X and Z, take the min. Closes the diagonal edge gap.
- **B. Architectural.** Finish the mesh-collision migration:
  1. Fix `classifyTriFlags` to emit a real `GEOFLAG_CEILING` (not alias FLOOR2).
  2. Fix `meshFindCeiling` to filter on `normal.y < -0.7` so it only returns actual ceilings.
  3. Call `meshFindCeiling` at the pre-move clamp site; use `min(meshCeilY, legacyCeilY)`.
  4. Call `meshSweepCapsuleWorld` inside `capsuleSweep` so upward sweeps see FLOOR-normal ceilings.

Recommended: ship A now; schedule B as a dedicated session.

---

## Issue 2 — Carrington tables: solid at rim, player falls through middle

### Geometry primitives involved

`src/include/types.h:886` — `geoblock`: header + `ymax, ymin` + `vertices[8][2]` (XZ polygon, not "up to 4" as `context/collision.md:75` says). Vertical prism; no explicit top-face primitive.

`src/include/types.h:893` — `geocyl`: header + `ymax, ymin, x, z, radius`. Same story.

### How floors under a standing player are found

- **`cdFindGroundInfoAtCyl`** (`src/lib/collision.c:2193`) queries with `GEOFLAG_FLOOR1|FLOOR2`. In `cdCollectGeoForCylFromList` (`collision.c:1229`), the `GEOTYPE_BLOCK` branch is gated on `(geoflags & (GEOFLAG_WALL | GEOFLAG_BLOCK_SIGHT | GEOFLAG_BLOCK_SHOOT))`. With FLOOR1|FLOOR2 only, **geoblocks are skipped entirely by the primary ground probe**. Same for geocyl at line 1247.
- **`capsuleFindFloor`** (`src/lib/capsule.c:135`) binary-searches using `cdTestVolume`, which is WALL-only. So a capsule floating above a block's top center — not overlapping any SIDE wall — never hits anything → falls through to bgGround far below.

### The CI-table mechanism

1. Tables do **not** set `OBJFLAG_00000100` (`src/game/propobj.c:2166`), so no AABB geoblock/geocyl is allocated. All collision comes from the model parts.
2. Table models contain a `MODELPART_BASIC_0066` "wall" part → `func0f069b4c` emits it as `GEOTYPE_TILE_F` flagged `GEOFLAG_WALL|BLOCK_SIGHT|BLOCK_SHOOT` (`propobj.c:1937`). These are the vertical panels around the rim.
3. The PC auto-top-face synthesizer at `propobj.c:2179-2225` fires only when `MODELPART_BASIC_0065` is **absent** and extents ≥ 30 and not `OBJFLAG3_WALKTHROUGH`. Tables typically have a (non-covering) `MODELPART_BASIC_0065` part → `hasFloorParts = true` → auto-floor **suppressed**.
4. Result: the table's only collision geometry is a ring of vertical wall panels.
5. In `capsuleFindFloor`'s binary search (`cd000272f8FltTile`, `collision.c:1060`), the XZ projection of each wall panel is a near-degenerate line segment. A player at table center is ~50u from every panel — outside the capsule's ~30u radius — so no collision → no prop floor → fall through.
6. Near the rim the capsule overlaps a panel's XZ projection → collision → binary search converges to `panel.ymax` ≈ tabletop. Hence "solid at edges".

Mesh collision path has the same hole: `meshWorldAddRoomGeo` (`src/lib/meshcollision.c:424-448`) generates top-face triangles for BG room geoblocks, but **per-prop mesh extraction is still pending** (`context/collision.md:133`).

### Proposed fixes (no implementation)

- **A (targeted).** Tighten the auto-floor gate in `propobj.c:2189-2206`: don't skip auto-floor just because a `MODELPART_BASIC_0065` exists — require the existing floor part to actually cover ≥ 50% of the bbox XZ extent at ymax. If it doesn't, still emit the auto-floor tile. Contained, low-risk.
- **B (general).** Finish prop-mesh extraction into the world grid (`context/collision.md:133`): `meshWorldAddRoomGeo` already emits proper top faces for geoblocks; do the equivalent for per-prop colmesh. Fixes desks/crates/tables generally.
- **C (cheap but narrow).** Let `cdCollectGeoForCylFromList`'s BLOCK branch respect FLOOR1|FLOOR2 so geoblocks become findable floors. Doesn't fix this specific bug (table uses TILE_F, not BLOCK) but closes a class gap.

Recommended: A now, C opportunistically, B as a scheduled milestone.

---

## Issue 3 — Jump fails or is reduced while on a slope

### Jump pipeline (input → impulse)

- **Button → wantsjump**: `src/game/bondmove.c:1977-1995`. Only two gates: `controlmode == CONTROLMODE_PC && allowc1buttons`, and `!jumpconsumed`. **No slope/ground/mode check.** Comment in the code explicitly defers grounded-check to `bwalkUpdateVertical`.
- **Impulse site**: `src/game/bondwalk.c:901-966`. `FIXED_JUMP_IMPULSE = 8.2f`, overridden by `g_PlayerExtCfg[pidx].jumpheight`.

### The culprit: the `grounded` heuristic

```c
// bondwalk.c:924-928
const f32 groundgap = vv_manground - vv_ground;
const bool grounded =
    (groundgap < 3.0f
    || (groundgap < 10.0f && bdeltapos.y < 2.0f))
    && !onladder;
```

On flat terrain: `bdeltapos.y ≈ 0`, `groundgap ≈ 0` → both disjuncts pass.

On an **ascending slope**: the slope-tracking logic in the previous tick leaves a residual **upward `bdeltapos.y`**. If per-tick climb > 2.0 units, the second disjunct fails. The first disjunct (`groundgap < 3.0f`) also fails intermittently because `vv_manground` lags `vv_ground` by > 3 units during active slope climb. Both fail → `impulse` is not written (`bondwalk.c:961` "JUMP: BLOCKED") → jump eaten.

No explicit slope/normal test exists anywhere in the jump path. The only `GEOFLAG_SLOPE` references (`bondwalk.c:268, 345, 391`) toggle `g_Vars.enableslopes` for `bwalkTryMoveUpwards` — unrelated to jump gating. `chrCanJumpInDirection` in `chraction.c` is AI-only.

### Proposed fixes (no implementation)

- **A (one-liner).** Relax the velocity ceiling: change `bdeltapos.y < 2.0f` → `bdeltapos.y < 6.0f` (stays below `FIXED_JUMP_IMPULSE = 8.2f` so a mid-jump player still reads as airborne). Optionally widen `groundgap < 10.0f` → `groundgap < 20.0f`.
- **B (more correct).** Add a disjunct: consider the player grounded if `floorflags` includes `GEOFLAG_SLOPE|FLOOR1|FLOOR2` and `groundgap < ~20.0f`, regardless of `bdeltapos.y`.

Recommended: A first (minimum risk, maximum unlock).

---

## Issue 4 — Pickup items (ammo boxes, weapons) are standable

### Pickup types (`src/include/constants.h:3535-3542`)

- `PROPTYPE_OBJ = 1` — ammo crates via `OBJTYPE_AMMOCRATE=0x07`, `OBJTYPE_MULTIAMMOCRATE=0x14`.
- `PROPTYPE_WEAPON = 4` — ground weapons. All creation paths end at `func0f08adc8`/`func0f08ae0c` calling `objInit`, then overwrite `prop->type = PROPTYPE_WEAPON` (`propobj.c:18568,18580`).

### The flag that exists for exactly this purpose

`OBJFLAG3_WALKTHROUGH = 0x00000400` (`src/include/constants.h:3180`). Honored by:
- `objInit` auto-floor generator (`propobj.c:2193`): skips auto-floor if set.
- `objUpdateGeometry` (`propobj.c:16495`): hides unkgeo if set.

### Where the flag is (and isn't) set

- **Network-received weapons — correct.** `port/src/net/netmsg.c:2026` initializes `flags3 = OBJFLAG3_WALKTHROUGH` with a comment: *"weapon pickups must be walkthrough, not solid"*.
- **Local chr/player weapon drops — missing.** `weaponCreateForChr` at `propobj.c:19010` initializes `flags3 = 0`. The `weaponobj tmp = { … 0, // flags3 … }` initializer never got the matching change.
- **Setup-file weapons — missing.** The `weapon(...)` macro (`src/include/props.h:32`) and every `setup*.c` entry pass `flags3 = 0`.
- **Ammo crates — missing.** The `ammocrate(...)` macro (`src/include/props.h:28`) passes `flags3 = 0`.

Any weapon whose model bbox x/z ≥ 30 (rifles/launchers/sniper definitely) gets a `GEOTYPE_TILE_F` floor tile from `objInit`'s PC auto-floor generator. `propUpdateGeometry` currently skips PROPTYPE_WEAPON (`prop.c:3510`), which is a latent second issue (see "Defense-in-depth" below) — but direct callers of `objUpdateGeometry` still return that geometry.

### Root-cause summary

Two distinct but congruent issues with the same fix:
- Weapons drop with `flags3 = 0` in the local-creation path; the network-receipt path already patched this.
- Ammocrates are placed in setup files with `flags3 = 0`.

Both become standable because the PC auto-floor synthesizer is unaware they're pickups.

### Proposed fixes (no implementation)

1. **Primary.** Set `OBJFLAG3_WALKTHROUGH` at creation:
   - `propobj.c:19010` (weaponCreateForChr initializer): `flags3 = OBJFLAG3_WALKTHROUGH`.
   - For setup-placed weapons/ammo: patch in `setupPlaceWeapon` (`setup.c:767-773`) and the `OBJTYPE_AMMOCRATE` branch at `setup.c:1882-1889` — low-surface option. Alternatively bake into `weapon(...)` / `ammocrate(...)` macros in `include/props.h` — broadest reach, touches one file.
2. **Defense-in-depth.** Add a walkthrough early-return to `propIsOfCdType` (`prop.c:3018-3053`) so any prop with `OBJFLAG3_WALKTHROUGH` is filtered out of `cdCollectGeoForCyl` regardless of which dispatcher runs downstream.
3. **Consistency.** Decide whether `propUpdateGeometry` should dispatch PROPTYPE_WEAPON to `objUpdateGeometry` (prop.c:3510) — currently asymmetric. Either include it (so `WALKTHROUGH` becomes single-source-of-truth) or skip weapon geometry allocation in `objInit` entirely. Document in `context/collision.md`.
4. **Audit.** `botinv.c:1144`, `bot.c:1645`, `chraicommands.c:9444,9452,9456`, `playermgr.c:819`, `player.c:2018` all route through `weaponCreateForChr` — fix #1 covers them.

---

## Issue 5 — Spawning ecosystem review

### File map

| File | Role |
|---|---|
| `src/include/game/spawnpool.h` | API + constants: `SPAWNPOOL_CAPSULE_RADIUS=30.0f`, `SPAWNPOOL_BUDGET_THRESHOLD=1500.0f`, `SPAWNPOOL_RAY_COUNT=14`, `SPAWNPOOL_MAX=MAX_MPCHRS`. |
| `src/game/spawnpool.c` | L1-L4 builder, validator, ray-budget scorer, farthest-point selector, deterministic RNG. |
| `src/game/playerreset.c` | Parses `INTROCMD_SPAWN` (:187), waypoint fallback (:282-405), `spawnPoolBuildGlobal()` (:450). Initial MP spawn via `spawnPoolSelect()` (:606-651). |
| `src/game/player.c` | `g_SpawnPoints`, `playerTrySelectPoolSpawn()` (:233-355), `playerChooseSpawnLocation()` (:357), `playerStartNewLife()` (:829). |
| `src/game/mplayer/scenarios.c` | `scenarioChooseSpawnLocation()` (:807) — scenario override or delegate. |
| `src/game/bot.c` | `botSpawn()` (:280) → `scenarioChooseSpawnLocation` → `chrMoveToPos`. Post-spawn room recovery (:314). Failsafe re-spawner (:1098). |
| `src/game/chraction.c` | `chrAdjustPosForSpawn()` (:15259) — legacy per-pad volume test. |
| `port/src/net/net.c`, `netmsg.c` | Match seed generation and wire (v35). |

No legacy standalone `spawn.c` — all spread across above.

### Flow summary (initial spawn vs respawn)

- **Initial MP**: `playerReset` → `spawnPoolBuildGlobal(stage_id, match_seed, needed)` (:450) → `spawnPoolSelect(pool, occupied, my_team, num_teams, center)` (:636) → direct write of `pos`, `rooms[0]`, `turnanglerad=0` → Y-snap via `cdFindGroundInfoAtCyl` (:724).
- **Respawn**: `playerStartNewLife` (:829) → `scenarioChooseSpawnLocation` → `playerChooseGeneralSpawnLocation` → `playerChooseSpawnLocation` (:357) → tries `playerTrySelectPoolSpawn` first; otherwise legacy shortlist with `chrAdjustPosForSpawn` validation → Y-snap (:870).
- **Bot**: `botSpawn` (:306) → same scenarios path → `chrMoveToPos` (:308).

### Raycast budget (`spawnPoolRaycastBudget`, `spawnpool.c:220-264`)

- 14 rays: 6 axes, 4 horizontal diagonals, 4 **upper** diagonals — **no lower diagonals**.
- Range 2000u. Near-hit reject at `< SPAWNPOOL_CAPSULE_RADIUS (30.0f)` — this is the B-134 fix from S249 (was 5.0f).
- Pass threshold: `sum(distances) ≥ 1500.0f`.
- Raycast is `bgTestHitInRoom` — **BG only**, no face normals, no props.

### Validation (`spawnPoolValidateCandidate`, `spawnpool.c:270-333`)

L1-L3 candidates run through:
1. Room valid (`bgFindRoomsByPos`).
2. Ground ≤ 500u below (`cdFindGroundInfoAtCyl`).
3. Vertical clearance: single upward cylinder sweep, radius 30, height 180 (`cdExamCylMove01`).
4. `bgTestPosInRoom`.
5. XZ spacing vs existing entries.
6. Ray budget.

**L4 radial candidates skip steps 1-5** (spawnpool.c:681). Only ray budget is applied. If no dilation pass succeeds, highest-budget candidates are accepted as last-resort **with no validation at all**.

### Known gaps / failure modes

1. **L4 mid-air spawn**: `bgFindRoomsByPos` failure causes `resolved = 0` (spawnpool.c:677) and candidate is still accepted. Runtime Y-snap may inherit the `-100000` sentinel.
2. **L4 inside-geometry**: ray-budget can pass if all 14 rays exit > 30u, even when center is inside a thick wall. No `bgTestPosInRoom` safety net at L4.
3. **Ray asymmetry**: 4 upper diagonals, 0 lower. Overhangs below the candidate are never diagonally sampled.
4. **Props invisible to pool**: the validator uses `CDTYPE_BG`; the raycasts use `bgTestHitInRoom`. Crates, pickup piles, props on the pad are not tested. Legacy respawn path uses `CDTYPE_ALL` via `chrAdjustPosForSpawn` and catches this.
5. **Ground sentinel unguarded**: at `player.c:870` and `playerreset.c:724`, no check that `cdFindGroundInfoAtCyl` returned a real floor (e.g. `groundy > -99000`).
6. **Orientation**: pool path sets `turnanglerad = 0` unconditionally (playerreset.c:642). Only the zero-pad legacy path does 8-direction wall-probe to face away from walls. Declared pads use `pad.look`.
7. **Same-tick duplicate**: `spawnPoolSelect` marks "used" only by proximity to **already-placed** props (10u XZ). Two decisions on the same tick before positions commit can both pick the same entry.
8. **Single-room ground check**: validator passes `{ room, -1 }` to `cdFindGroundInfoAtCyl` (spawnpool.c:295). Pads on room boundaries where the floor mesh belongs to the adjacent room fail spuriously.
9. **Crouch/prone clearance not tested** (only 180u standing).
10. **Pool determinism**: rebuilt from seed on each client. Any FP non-determinism in `cdFindGroundInfoAtCyl` or waypoint load order diverges pools.
11. **`spawn_needed` clamp**: `playerreset.c:432` forces ≥ 4. On tiny 2-player maps, over-builds into L3/L4 unnecessarily.
12. **Crosses Issue 4**: because weapons/ammo produce auto-floor tiles (Issue 4 above), some spawn pads landing on top of a dropped weapon can be "validated" by the legacy path testing `CDTYPE_ALL`. Fixing Issue 4 removes this category of false-positive.

### Proposed improvements (no implementation)

1. **L4 validation parity**: run at least the room/ground/ceiling subset of `spawnPoolValidateCandidate` before last-resort accept.
2. **Guard the ground sentinel**: `player.c:870`, `playerreset.c:724` — reject or widen room list if `groundy <= -99000`.
3. **Symmetric ray set**: add 4 lower diagonals or rebalance the 14.
4. **Prop-aware validation**: replace the BG-only raycast with `cdTestVolume(CDTYPE_ALL)` or at least `CDTYPE_OBJS` for candidates.
5. **Orientation**: reuse 8-direction wall-probe (playerreset.c:695-714) for pool spawns; today pool-sourced spawns face north.
6. **Same-tick reservation**: pass a caller-owned "reserved" bitset into `spawnPoolSelect` that persists across calls within a tick.
7. **Widen ground-check room list**: include `bgRoomGetNeighbours` in the room array passed to `cdFindGroundInfoAtCyl` inside the validator.
8. **Diagnostic**: flag L4 last-resort picks in the pool dump so `context/scratch/` logs show problem stages.
9. **Determinism audit**: confirm bit-identical results for `cdFindGroundInfoAtCyl` cross-target; otherwise ship pool in `SVC_STAGE_START` as contingency.

---

## Cross-Issue Interactions

- Issue 4 (pickups as floors) and Issue 5 (spawn pool ignores props) compound: a weapon dropped on a valid spawn pad can be standable AND spawn-passable → player spawns standing on a rifle.
- Issue 1 (ceiling clip) and Issue 2 (table tops missing) share the same root-category: the capsule system's reliance on `cdTestVolume`'s WALL-only filter, plus the not-yet-finished prop-mesh extraction in `meshcollision.c`.
- Issue 3 (slope jump) is isolated — a heuristic fix, no cross-impact.

---

## Recommended ordering of fixes

1. **Issue 3** — one-line heuristic relax. Immediate playtest win, zero regression risk.
2. **Issue 4** — set `OBJFLAG3_WALKTHROUGH` at pickup creation (primary + setup macro). Single-session work.
3. **Issue 1-A** — radius-aware pre-move ceiling probe. ~5 lines.
4. **Issue 2-A** — tighten auto-floor eligibility for props with non-covering `MODELPART_BASIC_0065`.
5. **Issue 5** — L4 validation parity + ground-sentinel guard + prop-aware raycast (three linked changes in one session).
6. **Issues 1-B / 2-B (architectural)** — wire mesh collision for vertical queries; finish per-prop mesh extraction. Dedicated milestone.

All fixes above are **proposals only** — no code has been edited during this investigation.

---

## Open questions for Mike

1. Issue 2 — should `propIsOfCdType` / `propUpdateGeometry` be made symmetric for PROPTYPE_WEAPON, or should `objInit` skip geometry allocation for weapons entirely? Both are valid; pick a canonical rule and document.
2. Issue 3 — prefer heuristic relax (A) or signal-driven redesign (B, uses `floorflags`)?
3. Issue 5 — is pool-sync via seed-only acceptable going forward, or should L3/L4 candidates be added to `SVC_STAGE_START`? The determinism audit would decide.
