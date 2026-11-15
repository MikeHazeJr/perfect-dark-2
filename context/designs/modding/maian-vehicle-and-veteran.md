# Maian vehicle and Maian veteran: approved concepts and plan (week of 2026-11-15)

Owner: MODS week session, branch `week-pd/mods`. Mike approved the concepts on
2026-11-15: "Concepts approved. Build with the in-game mod tools, commit and
push as you go, no merges to dev." This note holds his brief, what exists in the
engine, and the plan. Everything below is marked READ (seen in source), RUN
(proven by a named run) or OPEN.

## 1. Mike's brief (2026-11-15, his words, lightly condensed)

### Weapon

The crystal pistol session is done (`week-pd/crystal-pistol`, draft PR #6).
Use its output as the weapon mod (`mod_crystal_pistol:crystal_pistol`; see
`maian-crystal-pistol-handoff.md` section 11).

### Vehicle

Maian two-seat hover vehicle: a driver plus a gunner on a rotating turret. The
gunner sits in an armoured cowl and is hittable only through a front opening of
about 70 degrees.

- Boost ramps to full over about 1 s. After about 4 s (a node variable) the
  crystals turn black: boost continues above base but below full speed,
  steering is briefly limited, and the turret cannot fire until the crystals
  are their normal colour again. Recharge is faster when stopped and scales
  with speed.
- Turret: 18 crystals, larger versions of the pistol's, each regrowing per
  shot. Full regrow = fire interval x 18, plus about 10 % scaled by speed; at
  10 % of max speed regrow keeps pace with steady fire, and faster driving
  causes a delay. Normal driving lowers the fire rate, normalised to current
  versus max non-boost speed.
- Lock-on: vehicles only. Shots track until they hit, the gunner releases
  tracking/zoom, or boost ramps. The boost curve weakens tracking; at full
  boost the turret cannot fire. A shot that loses tracking keeps its heading.
- Intent: firing while stopped is strongest, so the crew become sitting ducks.

### Character

Maian veteran: no traits; normal hitbox, speed and health. An older Skedar-war
veteran: weathered grey skin, scars, a worn harness with crystal inlays, a
holstered crystal pistol, three crystal chevrons on one pauldron. Male and
female variants on the Maian rig. Reuse the Maian animations; add driver and
gunner seats and get in/out; a calmer idle if easy. The simulant prefers Maian
weapons, holds cover and engages at range. A single-player ally can drive or
gun.

### Level

Waits for Forge mode. Mike, 2026-11-15: "Regarding levels, we need to finish
authoring the Forge mode first." The Maian arena is not part of this plan; it
is a later item that depends on Forge mode authoring being finished. Keep
going on the vehicle, the veteran and the crystal pistol.

## 2. What exists (survey, 2026-11-15)

READ, by a read-only survey of `week-pd/mods` at 27dc9204 (file:line as it
reported them; not re-read line by line).

### 2a. Vehicles

- `.pdvehicle`: `vehicle.ini` (`catalog_id`, `name`, `model_file`,
  `physics_file`, `behavior_graph`; `assetcatalog_scanner.c` 2734-2752),
  `physics.json` (`pd2.vehicle.physics.v2`: the 24 hoverbike tunables plus
  `hover[13]`, all required, `asset_runtime.c` 1202-1302; the conformance tool
  requires `archetype = hoverbike`), and `behavior.graph.json`
  (`pd2.vehicle.behavior.v2`), which is not a graph: three booleans,
  `allow_mount`, `allow_drive`, `allow_dismount`. The validator already allows
  `dependencies/assets/weapons/*.pdweapon` in a vehicle; nothing loads it.
- No vehicle graph schema or runtime exists. The graph schemas are weapon,
  projectile, entity, effect and prop (`weapon_graph_runtime.c` 446-460); the
  only runtime that runs a graph every tick is `prop_graph_runtime.c` 214-303.
- Runtime binding: only `base:vehicle_hoverbike` is bound (to MODEL_HOVBIKE,
  `assetcatalog_base_extended.c` 318, 1177). Scanned mod vehicles keep
  `runtime_index` -1, so no mod vehicle can be placed or driven. Scenario
  objects name a model id, not a vehicle id; Forge has no vehicles.
- Seats: driver only (`propobj.c` 18108-18346 mount and dismount from the
  sides; eye offset fixed in `bondbike.c` 295-302). `SVC_PROP_VEHICLE_STATE`
  has no seat field (`netmsg.h` 28-50).
- Input: accelerate, brake, steer, exit, look, handbrake, use; no boost and no
  fire action. `LAYER_VEHICLE_TURRET` exists with no actions
  (`inputlayer.c` 290-301); `SCENE_VEHICLE_KIND_TURRET` exists.
- Camera: first person only. Damage: the bike is an ordinary damageable object
  and the rider is hit as a normal player who keeps a hand weapon.
- Turrets and homing: the autogun (yaw/pitch, tracks characters only) and the
  hovercopter gun are references. `projectile.homing` steering works on any
  prop; the target is the sight's tracked prop at launch; the homing keys
  `target_filter`, `target_source`, `retarget_policy`, `lost_target_behavior`
  are parsed and unused; `gate.target_lock` is a name only.
- Boost: only the original hold-full-throttle bonus (`propobj.c` 7354-7376).
  Unit trap: its counter runs at 240 Hz although the key is
  `boost_time_ticks60` (2400 is 10 s, not 40).
- No Vehicles tool in the Modding Hub (vehicles appear only in the INI
  Editor); `asset_mod_utility_contract.c` 36 points vehicles at a "Props" tool
  that does not exist.
- Design notes that plan vehicles: `asset-archive-clean-formats.md` (hull,
  seats, cameras, hardpoints, damage states, mounted weapons), the September
  goal closure plan (a hover vehicle and a turret vehicle), and T-VEHICLES-002
  (full hoverbike operation, missing).
- Mike's October work (read only, `.claude/pd-maian-models-20261013/`): a
  Maian tandem hover vehicle model study (`vehicle/builder_v5.py`, driver
  saddle, gunner on a mast with a crystal turret, a cool/hot crystal colour)
  and `vehicle-policy-v2/` (a standalone, never integrated motion and turret
  policy: gunner fire slows the vehicle with a smooth envelope, boost breaks
  lock and refuses new shots, a lost shot keeps its heading).

| Requirement | State | Where the work goes |
|---|---|---|
| Place and drive a mod vehicle | missing | a model slot and runtime index for mod vehicles, placement by vehicle id |
| Two seats | missing | a seat table in the format; occupants beside `struct hoverbikeobj`; mount and dismount per seat; a seat field on the wire |
| Turret aim and fire | missing | turret actions on `LAYER_VEHICLE_TURRET`; yaw and pitch parts; fire the vehicle's `.pdweapon` |
| Boost and heat | partial (hold bonus only) | a boost action; ramp, heat, steering limit and fire lockout in the vehicle movement |
| Crystal looks (normal, black) | missing | object part visibility, as the pistol's seat parts |
| Lock-on at vehicles only | partial (steering works) | a vehicle-only target filter; track until hit, release or boost; a lost shot keeps its heading |
| Gunner cowl hit only from the front | missing | an angle test against the vehicle's forward vector where the gunner is hit |
| AI drives or guns | missing | no bot code touches vehicles |
| Vehicle behavior graph with node variables | missing | a new vehicle graph schema (CORE owns schemas), runtime modelled on the prop graph runtime |

### 2b. Characters and simulants

OPEN: the character half of the survey is still running.

## 3. Plan

OPEN: written after the survey.
