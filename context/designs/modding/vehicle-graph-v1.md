# Vehicle behavior graph `pd.vehicle_graph.v1`

Status: IMPLEMENTED and proven in game on `week-pd/mods` (2026-11-16). CORE
accepted the design at 05:50 UTC ("CORE's answers" at the end; they override the
draft text where they differ), reviewed the code at 09:58 UTC and approved the
fixes at 10:35 UTC ("CORE's review"). The Maian hover vehicle runs its mount,
dismount, drive, boost and heat through its graph; since Rule 5 (CORE,
`week-pd/core-s5`, 2026-11-17) the base hoverbike runs its own graph too, and
every vehicle mounts, dismounts and drives only through a graph (no v2 path is
left in the code). Sections marked (history) describe a step that is over.

How it came about: CORE asked for the draft on 2026-11-15 (19:15 UTC, week note
schema table, row "MAIAN VEHICLE 1": "please draft it for CORE's review:
`pd.vehicle_graph.v1` with `$name` variables as the weapon graphs have, each
behavior a native module wired from events and gates (decision 1), no
per-statement nodes"). CORE owns the schema; MODS built the runner from the
mission runner's compile rules (`port/src/mission_graph_runner.cpp`). First
user: the Maian hover vehicle (`dev-mods/maian_hover_vehicle/`, steps V3 to V7
in `maian-vehicle-and-veteran.md`).

## Where the code is

| What | File |
|---|---|
| Compile and run a graph (kinds, values, compile rules, the walk, the C API) | `port/src/vehicle_graph_runner.cpp`, `port/include/vehicle_graph_runner.h` |
| One instance per vehicle (keyed by prop and its lifetime serial) | `port/src/vehicle_graph_runtime.c`, `port/include/vehicle_graph_runtime.h` |
| The state machine of `og.vehicle.boost_heat` | `port/src/vehicle_boost_heat.c`, `port/include/vehicle_boost_heat.h` |
| The game side: the events fired and what the modules ask of the game | `src/game/vehiclegraph.c`, `src/include/game/vehiclegraph.h` |
| The seams that fire the events | `src/game/propobj.c` (`currentPlayerTryMountHoverbike`, `hoverbikeUpdateMovement`, the idle tick), `src/game/bondbike.c` (both dismounts, the boost input) |
| The lifetime of instances | `src/game/lv.c` (cleared at stage load), `src/game/prop.c` (`propFree` releases a vehicle's record) |
| Reading a vehicle's files and compiling its graph when the catalog hydrates it | `port/src/asset_runtime.c` (`s_hydrateVehicle`, `s_hydrateVehicleGraph`) |
| The same rules for packaged mods | `tools/asset_archive_conformance.py` (`validate_vehicle_graph_execution`, `validate_vehicle_variables`) |
| Tests | `tests/test_vehicle_graph_runner.cpp`, `test_vehicle_boost_heat.cpp`, `test_vehicle_graph_conformance.py`, `test_asset_runtime_adapters.cpp`, `test_maian_hover_vehicle.cpp`, `test_vehicle_observer_layer.cpp` |
| In-game proof | `tools/smoke-verify/tests/maian_vehicle_drive_smoke.json`, `maian_vehicle_boost_smoke.json`, `mod_vehicle_drive_smoke.json` |

What is a graph and what is not: a v1 vehicle's decisions (who may mount, what a
tick does, what a heat phase shows and locks) are graph nodes and edges. The
work inside a node is a native module (`og.vehicle.*`): the original mount,
dismount and movement code, and the boost and heat machine. Handling numbers
are data in `physics.json`, and the boost numbers are the graph's params.

## Why

- A `.pdvehicle`'s `behavior.graph.json` today is `pd2.vehicle.behavior.v2`:
  three booleans (`allow_mount`, `allow_drive`, `allow_dismount`), not a graph.
  Movement and the only boost (the hold-full-throttle bonus) are native code in
  `hoverbikeUpdateMovement` (`src/game/propobj.c`), tuned by `physics.json`.
- Mike wants the Maian vehicle's boost and heat timings as node variables ("after
  about 4 s (a node variable) the crystals turn black"), and decision 1 says
  players must be able to recreate any existing game behavior with the nodes. So
  the base hoverbike's own behavior goes into the same graph as named native
  modules (the D-006A model), and the Maian vehicle is a different wiring of
  modules plus new ones.

## Archive members

| Member | Schema | What |
|---|---|---|
| `vehicle.ini` | unchanged | `behavior_graph = behavior.graph.json` (the existing key) |
| `behavior.graph.json` | `pd.vehicle_graph.v1` | the graph (this document) |
| `behavior/variables.json` | `pd.vehicle_variables.v1` | optional; named values the graph's params use as `"$name"` |
| `physics.json` | `pd2.vehicle.physics.v3` | the hover and drive tunables, read by `og.vehicle.hover_drive`. v3 is v2 without the boost keys (the graph holds them); a v1 graph with v2 physics, or the reverse, is refused |

## Graph rules (the mission graph runner's, `mission-graph-runner.md` on week-pd/core)

- Nodes `{ "id", "kind", "params" }`. Execution edges
  `{ "from", "output", "to", "input": "exec" }`: a gate's edges have `"output":
  "pass"` or `"fail"`, an event's or a module's have `"out"` or no output. An
  execution edge starts at an event, a gate or a module and ends at a gate or a
  module.
- One event activation walks breadth first in authored order; each reached node
  runs at most once per activation, so fan-in and cycles cannot repeat work. A
  module that refuses does not continue to its own successors; the others run.
- A graph that breaks a rule is refused when it compiles, logged once
  (`VEHICLE.GRAPH: refused the executable program`). A refused program runs
  nothing and nothing falls back to native code; since mounting is a graph edge,
  a vehicle with a refused graph cannot be mounted, so the failure shows at once.
- Limits as the runner (4,096 nodes, 16,384 edges), refused above, not truncated.
- One immutable program per catalog id and source digest; state belongs to each
  vehicle instance (below).
- Follow-up events that a module raises (`event.vehicle.heat_changed`) are queued
  and fired after the current walk, in order, so a module never re-enters the
  walk that called it. How many one fired event can lead to is proved when the
  graph compiles (at most 64; an event that can raise itself again while it is
  being delivered is refused), and the queue is sized from that proof.
- One event may reach at most one `og.vehicle.mount_seat` and one
  `og.vehicle.dismount_seat` per seat, both branches of a gate counted.
- Comments: a graph, a node, an edge and a variables file may each carry one
  `"comment"` string, for the people who read and edit it. The runner and
  conformance check that it is a string and never read it for behavior; in a
  variables file it is reserved, so `$comment` is not a variable. The Maian
  graph comments every node.

## Values and units

- Time params are `{ "value": N, "unit": "ticks60" | "ticks240" | "centiseconds" }`
  (the weapon module rule); speeds are world units per 60 Hz tick; fractions are
  0 to 1; angles are degrees.
- Any param value may be `"$name"`: the variable's value (a number, a bool, a
  string or a `{value, unit}` object) replaces it whole. An unknown name or a
  value of the wrong type refuses the graph.
- `pd.vehicle_variables.v1` is the flat shape of the crystal pistol's
  `behavior/variables.json`: `{ "schema": "pd.vehicle_variables.v1",
  "<name>": <value>, ... }`, names `[a-z0-9_]+`, the keys `schema` and
  `catalog_id` reserved.

## Events (fired by native seams)

| Kind | When | Seam | Carries |
|---|---|---|---|
| `event.vehicle.mount_requested` | a player asks to get on | `currentPlayerTryMountHoverbike` (`propobj.c`), today's driver mount | player, seat (`driver` until V4) |
| `event.vehicle.dismount_requested` | an occupant asks to get off | the bike exit path (`bondbike.c`) | player, seat |
| `event.vehicle.tick` | once per simulation update while the vehicle exists | the `hoverbikeUpdateMovement` call site | the update's 60 and 240 Hz deltas, occupied seats, the driver's inputs |
| `event.vehicle.heat_changed` | raised by `og.vehicle.boost_heat` when its phase changes | the module (follow-up queue) | phase, previous phase |

## Gates

| Kind | Passes when | Params |
|---|---|---|
| `gate.vehicle.seat` | the event's seat is `seat` | `seat`: `driver` or `gunner` |
| `gate.vehicle.seat_free` | `seat` has no occupant | `seat` |
| `gate.vehicle.heat_phase` | the vehicle's heat phase is one of `phases` | `phases`: list of `cool`, `ramping`, `full`, `overheated`, `recovering` |

## Modules (native, `og.vehicle.*`)

| Kind | What | Params |
|---|---|---|
| `og.vehicle.mount_seat` | the original side mount (proximity, facing, commit; `propobj.c` mount path) into `seat` | `seat` |
| `og.vehicle.dismount_seat` | the original dismount to the sides | `seat` |
| `og.vehicle.hover_drive` | the original hover and drive (`hoverbikeUpdateMovement` without its throttle bonus) from `physics.json`; hovers always, drives while the driver seat is occupied | `drive` (bool, default true; `allow_drive: false` in v2 terms) |
| `og.vehicle.throttle_boost` | the original bonus: full throttle (at least 0.99) ramps extra forward speed up to `boost_speed` over `ramp_time`; at 0.8 or more it holds, from -0.1 it decays, below that it resets | `boost_speed`, `ramp_time` (base: 5.0 and `{2400, ticks240}`, 10 s) |
| `og.vehicle.boost_heat` | Mike's boost and heat, below | below |
| `og.vehicle.part_visible` | shows or hides a model part of this vehicle (toggle nodes; parts start visible) | `part` (model part number), `visible` |
| `og.vehicle.turret_lockout` | sets or clears a named lockout; the turret cannot fire while any is set (V5 reads it) | `reason`, `locked` |

### `og.vehicle.boost_heat`

Mike's words: boost ramps to full over about 1 s; after about 4 s the crystals
turn black; boost continues above base but below full speed; steering is briefly
limited; the turret cannot fire until the crystals are their normal colour again;
recharge is faster when stopped and scales with speed.

Input: the driver's boost action (`ACTION_VEHICLE_BOOST`, held; Shift and
controller A by default, mappable) and the speed fraction (current over max non-boost
speed). State per vehicle: `boost` 0 to 1, `heat` 0 to 1, the phase and a steer
limit timer.

| Param | Meaning | Maian start value |
|---|---|---|
| `boost_speed` | extra forward speed at full boost | 7.0 (a new value; the base hoverbike's bonus tops out at 5.0) |
| `ramp_time` | 0 to full boost while held | `{60, ticks60}` (1 s) |
| `overheat_after` | time boost is held until overheated (heat rises at a constant rate while held, so about 4 s from pressing) | `{240, ticks60}` (4 s) |
| `overheated_boost` | the boost cap from overheating until cool again (0 base, 1 full) | 0.5 |
| `steer_limit` | steering scale right after overheating | 0.5 |
| `steer_limit_time` | how long the steering limit lasts | `{45, ticks60}` |
| `recover_time_stopped` | full heat to cool, standing still, boost released | `{120, ticks60}` |
| `recover_speed_scale` | recovery time multiplier at full non-boost speed (linear in the speed fraction) | 2.0 |

Phases: `cool` (heat 0) -> `ramping` (held, boost below 1) -> `full` -> after
`overheat_after` of holding boost, `overheated` (boost falls to
`overheated_boost` and stays there while held, the heat stays full, the
steering limit starts) -> boost released, `recovering` (heat falls at the
speed-scaled rate; boost stays capped if pressed again) -> heat 0, `cool`.
Releasing before overheating goes back to `cool` as the boost ramps down, and
heat falls at the same rate; heat left from a short break counts towards the
next overheat. Each change raises `event.vehicle.heat_changed`. The state
machine is `port/src/vehicle_boost_heat.c` (`[vehicle-boost-heat]`), run by the
graph runner for the graph's `og.vehicle.boost_heat` node. It holds no vehicle's
numbers and does not decide what a phase shows or locks: those are the graph's
params and its `gate.vehicle.heat_phase` gates.
Choice to confirm with Mike: boosting again while `recovering` is allowed at the
capped level and does not lengthen the black-crystal time.

## Interim, until CORE answers (history: removed in 5aae6799)

As CORE agreed (keep the values in a file in the vehicle archive until the
graph exists), the game reads `behavior/variables.json` directly: a vehicle
with the file gets `og.vehicle.boost_heat` wired as in the Maian graph below
(no full-throttle bonus), with the same value names plus `cool_crystal_part`,
the part that the graph's `og.vehicle.part_visible` nodes would hide. Moving to
the graph changes the wiring only; the values file stays as it is.

## The base hoverbike's graph

```json
{
  "schema": "pd.vehicle_graph.v1",
  "catalog_id": "base:vehicle_hoverbike",
  "nodes": [
    { "id": "mount", "kind": "event.vehicle.mount_requested" },
    { "id": "dismount", "kind": "event.vehicle.dismount_requested" },
    { "id": "tick", "kind": "event.vehicle.tick" },
    { "id": "driver_free", "kind": "gate.vehicle.seat_free", "params": { "seat": "driver" } },
    { "id": "mount_driver", "kind": "og.vehicle.mount_seat", "params": { "seat": "driver" } },
    { "id": "dismount_driver", "kind": "og.vehicle.dismount_seat", "params": { "seat": "driver" } },
    { "id": "drive", "kind": "og.vehicle.hover_drive" },
    { "id": "throttle_boost", "kind": "og.vehicle.throttle_boost",
      "params": { "boost_speed": 5.0, "ramp_time": { "value": 2400, "unit": "ticks240" } } }
  ],
  "edges": [
    { "from": "mount", "output": "out", "to": "driver_free", "input": "exec" },
    { "from": "driver_free", "output": "pass", "to": "mount_driver", "input": "exec" },
    { "from": "dismount", "output": "out", "to": "dismount_driver", "input": "exec" },
    { "from": "tick", "output": "out", "to": "drive", "input": "exec" },
    { "from": "tick", "output": "out", "to": "throttle_boost", "input": "exec" }
  ]
}
```

The v2 booleans map onto it: `allow_mount: false` leaves out the mount edges,
`allow_dismount: false` the dismount edge, `allow_drive: false` sets `drive` to
false.

## The Maian vehicle's graph (V3)

Part 140 is the seafoam crystals and part 141 the black ones, which sit just
inside the seafoam ones (`build_vehicle_mesh.py`), so hiding 140 shows black.

```json
{
  "schema": "pd.vehicle_graph.v1",
  "catalog_id": "mod:vehicle_maian_hover",
  "nodes": [
    { "id": "mount", "kind": "event.vehicle.mount_requested" },
    { "id": "dismount", "kind": "event.vehicle.dismount_requested" },
    { "id": "tick", "kind": "event.vehicle.tick" },
    { "id": "heat", "kind": "event.vehicle.heat_changed" },
    { "id": "driver_free", "kind": "gate.vehicle.seat_free", "params": { "seat": "driver" } },
    { "id": "mount_driver", "kind": "og.vehicle.mount_seat", "params": { "seat": "driver" } },
    { "id": "dismount_driver", "kind": "og.vehicle.dismount_seat", "params": { "seat": "driver" } },
    { "id": "drive", "kind": "og.vehicle.hover_drive" },
    { "id": "boost", "kind": "og.vehicle.boost_heat", "params": {
        "boost_speed": "$boost_speed", "ramp_time": "$boost_ramp",
        "overheat_after": "$overheat_after", "overheated_boost": "$overheated_boost",
        "steer_limit": "$steer_limit", "steer_limit_time": "$steer_limit_time",
        "recover_time_stopped": "$recover_time_stopped",
        "recover_speed_scale": "$recover_speed_scale" } },
    { "id": "crystals_black", "kind": "gate.vehicle.heat_phase",
      "params": { "phases": ["overheated", "recovering"] } },
    { "id": "hide_seafoam", "kind": "og.vehicle.part_visible", "params": { "part": 140, "visible": false } },
    { "id": "show_seafoam", "kind": "og.vehicle.part_visible", "params": { "part": 140, "visible": true } },
    { "id": "turret_blocked", "kind": "gate.vehicle.heat_phase",
      "params": { "phases": ["full", "overheated", "recovering"] } },
    { "id": "lock_turret", "kind": "og.vehicle.turret_lockout", "params": { "reason": "heat", "locked": true } },
    { "id": "unlock_turret", "kind": "og.vehicle.turret_lockout", "params": { "reason": "heat", "locked": false } }
  ],
  "edges": [
    { "from": "mount", "output": "out", "to": "driver_free", "input": "exec" },
    { "from": "driver_free", "output": "pass", "to": "mount_driver", "input": "exec" },
    { "from": "dismount", "output": "out", "to": "dismount_driver", "input": "exec" },
    { "from": "tick", "output": "out", "to": "drive", "input": "exec" },
    { "from": "tick", "output": "out", "to": "boost", "input": "exec" },
    { "from": "heat", "output": "out", "to": "crystals_black", "input": "exec" },
    { "from": "crystals_black", "output": "pass", "to": "hide_seafoam", "input": "exec" },
    { "from": "crystals_black", "output": "fail", "to": "show_seafoam", "input": "exec" },
    { "from": "heat", "output": "out", "to": "turret_blocked", "input": "exec" },
    { "from": "turret_blocked", "output": "pass", "to": "lock_turret", "input": "exec" },
    { "from": "turret_blocked", "output": "fail", "to": "unlock_turret", "input": "exec" }
  ]
}
```

`behavior/variables.json`:

```json
{
  "schema": "pd.vehicle_variables.v1",
  "boost_speed": 7.0,
  "boost_ramp": { "value": 60, "unit": "ticks60" },
  "overheat_after": { "value": 240, "unit": "ticks60" },
  "overheated_boost": 0.5,
  "steer_limit": 0.5,
  "steer_limit_time": { "value": 45, "unit": "ticks60" },
  "recover_time_stopped": { "value": 120, "unit": "ticks60" },
  "recover_speed_scale": 2.0
}
```

The Maian vehicle has no `og.vehicle.throttle_boost` node, so it has no
full-throttle bonus; a mod that wants both wires both.

## State, authority and the wire

- Module state (boost, heat, phase, timers, lockouts, part visibility) lives per
  vehicle instance in a side table keyed by a lifetime-checked prop id, cleared at
  creation and freed with the object; nothing in catalog-global records.
- The host (or the single-player game) runs the graph for every vehicle. Clients
  get the heat phase and the visible parts through `SVC_PROP_VEHICLE_STATE`
  (MULTIPLAYER's message; it needs a seat field for V4 anyway) and predict no
  heat. Asked of MULTIPLAYER with the seat field when V4 starts.

## Migration (CORE's call, question 4)

Proposal: the vehicle extractor writes the base hoverbike graph above, the
vehicle cache kind changes so installs re-extract once, and after that a v2
behavior file is refused with a message naming `pd.vehicle_graph.v1`. The
in-game mod author copies the base hoverbike, so mods made after the base change
are v1 already; the conformance tool reports v2 as not clean.

## Conformance and tests

- `tools/asset_archive_conformance.py`: `validate_vehicle_graph_execution` with
  the same rules, and a test that keeps its kind lists equal to the runner's (as
  for the mission graph).
- Unit cases `[vehicle-graph]`: the compile rules and refusals, `$name`
  resolution and type checks, the follow-up event bound, and the boost and heat
  phases on a fixed tick sequence (the phase change times match the variables).
- In game: the base hoverbike drives as before (`vehicle_flow`, once it handles
  Escape's grab-mode start, B-1159), and the Maian vehicle smoke boosts until the
  crystals turn black and checks the part and lockout lines.

## Not in v1 (same contract, later)

- Gunner seat and turret (V4, V5): `gate.vehicle.seat` with `gunner`,
  `og.vehicle.turret_aim` (yaw part 1, pitch part 2, rates and limits),
  `og.vehicle.fire_weapon` (the vehicle's embedded `.pdweapon`, muzzle part 50),
  per-shot crystal regrow through the crystal pistol's mod magazine on ring seats
  110 to 127, fire rate lowered by the speed fraction.
- Lock-on at vehicles only (V6): a target filter on the turret weapon's
  projectile graph (`projectile.homing`'s `target_filter`), tracking weakened by
  the boost level; a projectile graph question, not this schema.
- The gunner's cowl (V7): an `event.vehicle.occupant_hit` with the hit direction
  and `gate.vehicle.hit_arc` (70 degrees about the gunner's forward).
- AI drivers and gunners (V8).

## Questions for CORE

1. Per-instance state and module-raised events: the mission runner has neither
   yet. Is a module-owned side table plus the bounded follow-up queue fine for
   v1, or do you want the general D-006A instance (`graphBind`, `graphDeliver`)
   first?
2. `event.vehicle.tick` walks the graph once per simulation update per vehicle.
   Fine, or should continuous modules bind once and tick natively?
3. The boost keys: move `boost_speed` and `boost_time_ticks60` out of
   `physics.json` into the base graph (a physics v3; it also ends B-1230's
   misleading name), or keep physics v2 and let `og.vehicle.throttle_boost` read
   them when its params are absent?
4. v2 behavior files: refuse after the base re-extract (proposal above), or keep
   reading them?
5. Names: `event.vehicle.*`, `gate.vehicle.*` and `og.vehicle.*`, as the
   mission graph's `og.hud.*` and `og.mission.*`?

## Implementation (MODS, for CORE's review before it merges)

Step A, written 2026-11-16: the runner, `port/include/vehicle_graph_runner.h` and
`port/src/vehicle_graph_runner.cpp`, unit cases `[vehicle-graph]`
(`tests/test_vehicle_graph_runner.cpp`). Not called by the game yet.

- Compile: the mission runner's rules (exec edges, `pass`/`fail`/`out`, data nodes
  and structural edges never walked, 4,096 nodes and 16,384 edges), plus: every
  `event.*`, `gate.*` and `og.*` kind must be one of v1's, with only its own params;
  `"$name"` takes the variable whole; times are `{value, unit}` only; a module
  reachable from an event it is not made for refuses the graph (mount and dismount
  modules on their events, `hover_drive`, `throttle_boost` and `boost_heat` on the
  tick, `part_visible` and `turret_lockout` on any); at most one `hover_drive` and
  one `boost_heat`; lockout reasons get bits when the graph compiles (at most 32);
  `catalog_id` must match the archive's.
- The raised-event bound: for each event kind, the raising modules its walk can
  reach (both gate branches); a raised kind already being delivered refuses the
  graph; the bound is the sum of 1 plus the raised kind's own bound over those
  nodes, at most 64. Each instance's queue has exactly that room. The Maian graph's
  bound is 1, the base graph's 0.
- The instance (one per vehicle): the program, per-node state (each
  `throttle_boost` node's timer; the one `boost_heat` state), the lockout bits, the
  pending queue, and the walk's reached flags and queue, sized when it is made, so
  a fire allocates nothing.
- The tick: its modules only record this update's drive (hover, drive, extra
  forward speed as a sum, steering scale as a product), reset at each tick; the
  host's movement reads it after the walk. So `throttle_boost` before or after
  `hover_drive` in the authored order gives the same physics, and the original
  order (bonus, then integration) holds. `throttle_boost` is the original bonus with
  its timer in node state; the unit case checks it bit for bit against the original
  code over 2,000 uneven updates. `vehicleGraphInstanceRest` is the original's
  rest reset (an empty bike that stopped zeroes `maxspeedtime240`).
- Gates: `seat` passes on mount and dismount events for its seat (never on tick or
  heat events, as the mission runner's objective gate never passes on mission
  events); `seat_free` asks the host; `heat_phase` reads the instance's phase (cool
  without a `boost_heat`).

B to D, written 2026-11-16 (5aae6799; pd-tests pass). The client is built on e23f6ea7 (with
`week-pd/extract-6`, shared base 7); the in-game proof is the three vehicle smokes on base 7
(q_20261116090706870_9fada84f). The game:

- `asset_runtime.c`: a `pd.vehicle_graph.v1` behavior file compiles with its
  `behavior/variables.json` when the catalog hydrates the vehicle; a refused graph
  logs `VEHICLE.GRAPH: refused the executable program` with the reason and the
  vehicle does not hydrate (nothing falls back). The v2 booleans become derived for
  v1 (`mount` = the mount event reaches `og.vehicle.mount_seat`, and so on), so
  `assetRuntimeVehicleAllows` keeps answering the committed-state and network
  paths. v2 files keep today's reader until the base re-extract.
- One instance per vehicle prop, keyed by the prop and its `lifecycle_generation`,
  replacing `vehicle_boost_runtime.c`'s table; cleared at stage load. The parts the
  graph shows and hides are checked against the vehicle's model when its instance
  is made (the model is not built when the catalog registers the vehicle); a missing
  toggle part refuses the instance, logged.
- Seams: the tick fires where `hoverbikeUpdateMovement` runs (its drive replaces
  the original bonus and scales the steering; no hover means no movement);
  `og.vehicle.mount_seat` is today's mount candidate and commit
  (`currentPlayerTryMountHoverbike`'s range, facing and apply), fired from the same
  callers; `og.vehicle.dismount_seat` is today's side dismount in
  `bbikeHandleActivate`, network intent paths unchanged. Part toggles go through the
  model's toggle nodes, as the V3 interim does now.
- The interim reader is gone: `behavior/variables.json` belongs to a v1 graph only, and a
  v1 graph pairs with `pd2.vehicle.physics.v3` (the v2 keys minus the boost keys); each
  refusal names its reason (`VEHICLE.SOURCE`, `VEHICLE.GRAPH`). Conformance has
  `validate_vehicle_graph_execution` with the runner's rules, and a pd-tests case checks
  its kind sets against the runner's own list (`vehicleGraphKnownKind`). The Maian
  recipe ships its v1 graph and physics v3 (0.4.0, c3d09fda): 15 nodes, 11 exec edges, one
  raised event pending at most, one part (140); the recipe test compiles the built
  archive's graph with the runner, and the boost smoke counts one boost cycle's part lines
  (shown at ramping, full and cool, hidden at overheated and recovering).
- Also found and fixed on the way: B-1231 (the PC exit key dismounted without the
  vehicle's permission); both dismount paths now go through the graph.

## CORE's review of steps B to D (2026-11-16 09:58 UTC) and what MODS did

CORE reviewed 5aae6799 and 22a2126b: merge after these fixes. Done on week-pd/mods the same
morning (afd8739b); the client built with them (and C3) and the three vehicle smokes passed on
base 7 at 11:08 UTC: mod drive 32/32, Maian drive 34/34, Maian boost 47/47 (ramp peak 24.63 over
the ground against a top of 21; part 140 hidden once and shown once):

- (1) A crash: two `og.vehicle.dismount_seat` nodes on one event compiled, and the second ran
  `bbikeDismountToSides` after the first had set `player->hoverbike` to NULL (a remote client's
  dismount reaches it on a listen host). Now one event may reach at most one `mount_seat` and one
  `dismount_seat` per seat, wherever they sit (both branches of a gate count); the runner and
  conformance refuse a second ("reaches a second ... for the driver seat"), and the game's
  seat action (`hoverbikeGraphSeatAction`, `src/game/vehiclegraph.c`) does nothing once one has acted in that
  request (`seat_done`). Runner cases: a second dismount, a second mount on a gate's other branch,
  and one per seat allowed.
- (2) Records: `propFree` now releases the vehicle's record by prop and generation
  (`vehicleGraphRuntimeRelease`), and only a vehicle with a graph records the boost input, so
  destroyed and respawned vehicles never fill the 64. Freeing a program forgets only the vehicles
  made from it (`vehicleGraphRuntimeForgetProgram`, which `s_freeVehicleGraph` uses instead of a
  reset of every vehicle), and releasing a vehicle's catalog entry frees its program at once.
- Smaller: the throttle bonus reads no throttle when the graph's `hover_drive` has
  `drive: false` (the program records it at compile time; there is one hover_drive); a part
  toggle logs only when it changes (the boost smoke's counts become 1 shown and 1 hidden after the
  09:40 UTC turn); conformance checks a variables file's `catalog_id` against the vehicle's.

Follow-up tasks (to become Workbench items at the merge, as agreed for the week):

- Rule 6 (CLOSED 2026-11-16, was a recorded deviation): the parts a graph toggles are checked when
  the catalog registers the vehicle. `assetRuntimeVehicleGraphAdmits` (`port/src/asset_runtime.c`),
  called by the scanner's `registerVehicleModel`, compiles the graph with its variables and asks
  `port/src/vehicle_model_parts.cpp` whether each part is a toggle node, read from the embedded
  mesh's `model.parts.json` and `model.nodes.json` the way the model compiler reads them. A part
  the model lacks, a part that is not a toggle, or a mesh with no part tables refuses the vehicle
  with `PDVEHICLE.MODEL.REJECT ... behavior graph refused: ...`; any other compile refusal does too.
  The same compile runs again when the vehicle hydrates, and the built model is still checked when
  the instance is made, as a guard. A base vehicle (a ROM model, v2 today) is not checked at
  registration; that follows the extractor task below.
- Rule 5 (task, EXTRACT/CORE): the extractor writes the base hoverbike's `pd.vehicle_graph.v1`
  graph (the base graph in this doc) and `pd2.vehicle.physics.v3` physics behind a vehicle export
  version; then the runtime refuses v2 behavior and physics and conformance reports them. B-1230
  ends with it; until then the v2 path is the second path.
  **Done on `week-pd/core-s5` (CORE, 2026-11-17):** `romextract_pdmeta.c` writes this doc's base
  graph and v3 physics (the meta export version moves to `_vehiclegraph`, so every install
  re-extracts its meta archives once); `asset_runtime.c` refuses a vehicle whose behavior is not
  a v1 graph (the message names `pd.vehicle_graph.v1` and the retired v2 schemas); conformance
  reports v2 as an error and its self-test checks a v1 vehicle; the typed example vehicle is v1
  (boost 7.0 over 1800 ticks240). The Maian recipe already replaced its template's v2 files.
  The v2 branches of the mount, dismount and movement code went with it
  (`currentPlayerTryMountHoverbike`, `bbikeHandleActivate`, the PC exit in
  `bbikeApplyMoveData`, `hoverbikeUpdateMovement`) and the two physics v2 boost
  fields of the runtime binding: a vehicle without a graph cannot be mounted,
  left or driven, so there is no native fallback.
- B-1232 (MULTIPLAYER's wire, MODS's choice offered): a remote rider's boost never reaches the
  host. MODS's preference: a boost bit in the move message, so the host runs `boost_heat` with the
  client's input as for its own rider, and the phase (with the toggled parts) in the vehicle state
  for remote clients, which predict no heat, as this design says.

## CORE's answers (2026-11-16 05:50 UTC, week note To CORE)

1. State: no module-owned side tables. The runner keeps one instance record per
   vehicle, keyed by the prop and its lifetime serial (as `prop_graph_runtime.c`
   keeps its instances); each module's state is a slot of that record, sized per
   node by the compiler. The record has the D-006A shape (program, per-node
   state, pending events), so moving to `graphBind`/`graphDeliver` later is a
   rename. Follow-up events: the bound is proved when the graph compiles, not at
   run time. A graph is refused when an event's walk can reach a module that
   raises an event already being delivered (directly or through other raised
   events); with each node once per activation the queue depth is then at most
   the number of raising nodes, and the runner sizes the queue from that. This
   replaces "State, authority and the wire"'s side table.
2. Tick: walk `event.vehicle.tick` every simulation update. Compile the tick walk
   to a flat node list once, allocate nothing per update, and fire it where
   `hoverbikeUpdateMovement` is called today so the physics order stays the
   original's. Modules use the 60 and 240 Hz deltas the event carries, never
   frame time or frame counts (host, recordings and the Theater agree).
3. Boost keys: into the graph. Physics v3 has no boost keys, and
   `og.vehicle.throttle_boost` requires both params with no fallback to
   `physics.json`. Correction: the base values are 5.0 and `{2400, ticks240}`, a
   10 s ramp (the original caps at `TICKS(2400)`; the base
   `vehicles/base_vehicle_hoverbike.pdvehicle` holds `boost_speed` 5.0 and
   `boost_time_ticks60` 2400, checked on shared base 7). The 7.0 and 1800 first
   quoted here and in B-1230 are the example template's
   (`template/tri_vehicle.pdvehicle`); the Maian 7.0 is a new value.
4. v2 behavior and v2 physics: refused after the base re-extract, with a
   message naming `pd.vehicle_graph.v1` and the mapping; no fallback reader, and
   conformance reports v2 as an error. The extractor writes the v1 graph and v3
   physics behind one vehicle export version (if the vehicle extractor ever gets
   a folder fast cache, its kind comes from that version, SP-86). Mods built from
   the template before then need a rebuild; the notice says so.
5. Names: accepted (`<role>.<domain>.<name>`, like `event.objective.status_changed`,
   `gate.objective.status`, `og.hud.objective_status_message`). Also:
   `og.vehicle.part_visible` refuses at catalog registration a `part` the
   vehicle's model does not have, and `turret_lockout` reasons are fixed strings
   counted when the graph compiles.
