# Vehicle behavior graph `pd.vehicle_graph.v1` (draft for CORE's review)

Status: draft by MODS, 2026-11-16. Not implemented. CORE asked for it on
2026-11-15 (19:15 UTC, week note schema table, row "MAIAN VEHICLE 1": "please
draft it for CORE's review: `pd.vehicle_graph.v1` with `$name` variables as the
weapon graphs have, each behavior a native module wired from events and gates
(decision 1), no per-statement nodes"). CORE owns the schema and the runner; MODS
builds the Maian vehicle on whatever CORE accepts. First user: the Maian hover
vehicle (`dev-mods/maian_hover_vehicle/`, steps V3 to V7 in
`maian-vehicle-and-veteran.md`).

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
| `physics.json` | `pd2.vehicle.physics.v2` | the hover and drive tunables, read by `og.vehicle.hover_drive` (see question 3 for the boost keys) |

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
  and fired after the current walk, in order, at most 8 per vehicle per
  simulation update (refused and logged above that), so a module never re-enters
  the walk that called it.

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
| `og.vehicle.throttle_boost` | the original bonus: full throttle (at least 0.99) ramps extra forward speed up to `boost_speed` over `ramp_time`; at 0.8 or more it holds, from -0.1 it decays, below that it resets | `boost_speed`, `ramp_time` (base: 7.0 and `{1800, ticks240}`, 7.5 s) |
| `og.vehicle.boost_heat` | Mike's boost and heat, below | below |
| `og.vehicle.part_visible` | shows or hides a model part of this vehicle (toggle nodes; parts start visible) | `part` (model part number), `visible` |
| `og.vehicle.turret_lockout` | sets or clears a named lockout; the turret cannot fire while any is set (V5 reads it) | `reason`, `locked` |

### `og.vehicle.boost_heat`

Mike's words: boost ramps to full over about 1 s; after about 4 s the crystals
turn black; boost continues above base but below full speed; steering is briefly
limited; the turret cannot fire until the crystals are their normal colour again;
recharge is faster when stopped and scales with speed.

Input: the driver's boost action (a new `ACTION_VEHICLE_BOOST`, held; it needs a
saved binding name, B-1184) and the speed fraction (current over max non-boost
speed). State per vehicle: `boost` 0 to 1, `heat` 0 to 1, the phase and a steer
limit timer.

| Param | Meaning | Maian start value |
|---|---|---|
| `boost_speed` | extra forward speed at full boost | 7.0 (the base bonus's top) |
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
machine is written and unit-tested as `port/src/vehicle_boost_heat.c`
(`[vehicle-boost-heat]`); nothing calls it yet.
Choice to confirm with Mike: boosting again while `recovering` is allowed at the
capped level and does not lengthen the black-crystal time.

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
      "params": { "boost_speed": 7.0, "ramp_time": { "value": 1800, "unit": "ticks240" } } }
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
