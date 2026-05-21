# Base Weapon Behavior Coverage Audit

Status: active first-pass audit for Kanban `c3814-s2`.

Scope: base-game weapon behavior as currently expressed by authored weapon data, local generated weapon archives, and runtime behavior in `bondgun.c` and `propobj.c`.

Important: local `.pdwpn` files were used only as a pre-release generated snapshot to speed inspection. `.pdwpn` is fully deprecated, was never released, and must be removed from current code paths. The target asset type is `.pdweapon`; no `.pdwpn` alias, migration source, or compatibility path should be added.

## Current Result

The first pass shows that the base game can be represented by the planned graph asset model without arbitrary script nodes, as long as the graph runtime includes named modules for the hard-coded physical behaviors that already exist in C.

What is covered now:

- The base inventory shape: 86 weapon records, two function slots each.
- The current function families and their data fields.
- The runtime homes for fired projectiles, thrown physical objects, deployed entities, mines, proxy detection, remote detonation, Slayer fly-by-wire, and Laptop Gun autogun behavior.
- The first set of `.pdprojectile` and `.pdentity` candidates that need to be cataloged or embedded by weapon archives.
- The weapon-by-weapon current parameter matrix: [base-weapon-parameter-matrix.md](base-weapon-parameter-matrix.md).

What is not complete yet:

- Final file/archive names for every nested payload.
- Final authored graph/module parameter names for the named runtime modules.
- Runtime emitter/scanner changes from `.pdwpn` to `.pdweapon`.
- Validation tests for canonical graph JSON and compiled deterministic IR.

## Source Evidence

Primary sources:

- `port/src/weapondata_authored.c`
- `port/include/weapondata_authored.h`
- `src/include/types.h`
- `src/include/constants.h`
- `src/game/bondgun.c`
- `src/game/propobj.c`

Inspection-only generated snapshot:

- `Build/data/ntsc-final/weapons/*.pdwpn`

The generated snapshot reported 86 weapon archives. Across both weapon function slots, the current local data contains:

| Function kind | Count | Graph home |
| --- | ---: | --- |
| `shoot_auto` | 18 | weapon fire graph with automatic cadence |
| `shoot_projectile` | 14 | weapon fire graph plus `.pdprojectile` payload |
| `shoot_single` | 20 | weapon fire graph with hitscan shot module |
| `throw` | 14 | weapon throw graph plus `.pdprojectile` or `.pdentity` payload |
| `melee` | 10 | weapon melee graph |
| `special` | 6 | weapon special graph |
| `device` | 8 | weapon/device activation graph |
| `none` | 7 | explicit empty function |
| `null` | 75 | missing second slot or no payload |

## Source-To-Graph Mapping

| Current source/data | New asset location | Notes |
| --- | --- | --- |
| `struct weapon` model refs, ammo defs, animations, aim settings, flags | `.pdweapon` root metadata and presentation graph | Must also carry part visibility and gun visual commands. |
| `weaponfunc_shoot` | `.pdweapon` graph nodes | Covers single shot, burst flags, recoil, spread, damage, penetration, noise, recovery, and shot sound. |
| `weaponfunc_shootauto` | `.pdweapon` graph nodes | Adds initial RPM, max RPM, vibration, and turret acceleration/deceleration parameters. |
| `weaponfunc_shootprojectile` | `.pdweapon` graph trigger plus `.pdprojectile` payload | Covers fired rockets, homing rockets, fly-by-wire Slayer rockets, bolts, and grenade rounds. |
| `weaponfunc_throw` | `.pdweapon` graph trigger plus `.pdprojectile` or `.pdentity` payload | Covers grenades, mines, thrown knife, thrown Dragon, thrown Laptop Gun, and sticky devices. |
| `weaponfunc_melee` | `.pdweapon` melee node | Covers damage and range. |
| `weaponfunc_special` | `.pdweapon` special action node | Covers remote mine detonation, combat boost/revert, crouch/lower weapon, uplink, cloak, and zoom-like actions. |
| `weaponfunc_device` | `.pdweapon` device node | Covers night vision, X-Ray, IR scanner, R-Tracker, EyeSpy, cloak device, and suicide pill. |
| `struct projectile` | `.pdprojectile` runtime state schema | Runtime instance data, not authored asset data, but it defines the behavior parameters the asset must feed. |
| `struct autogunobj` | `.pdentity` runtime state schema | Runtime entity instance for deployed Laptop Gun autogun behavior. |
| Proxy mine globals and trigger lists | `.pdentity` behavior module | The asset should describe trigger policy; runtime can keep its optimized list implementation. |

## Behavior Families

### Hitscan And Automatic Fire

Normal pistols, automatics, shotguns, burst weapons, charge-like weapons, and the Laptop Gun primary all live in `.pdweapon`. Their graph needs these node categories:

- Input trigger and hold state.
- Ammo gate and ammo consumption.
- Cadence and recovery.
- Shot pattern: single, burst, auto, shotgun spread, or charged release.
- Damage, penetration, impact force, noise, recoil, spread, and animation.
- Presentation side effects: muzzle flash, shot sound, casing/eject effects, and weapon model visibility changes.

Open detail:

- The Mauler secondary is data-shaped like a normal `shoot_single` function, but its behavior is charge-like in runtime. This needs a named charge/release graph module before runtime conversion.

### Fired Projectiles

Fired projectile functions are still weapon actions, but their physical behavior belongs in `.pdprojectile` payloads.

Required modules:

- Spawn from muzzle or adjusted forward point.
- Aim trajectory correction when `FUNCFLAG_CALCULATETRAJECTORY` is set.
- Initial velocity, scale, travel distance, timer, reflect angle, sound, and owner velocity inheritance.
- Rocket explosion and smoke trail behavior.
- Homing target acquisition and steering.
- Slayer fly-by-wire owner control and bot fallback route behavior.
- Crossbow bolt sticky hit behavior.
- Devastator wall-hugger secondary behavior.

### Thrown Physicals

Throw functions need a graph path for priming, release, recovery, spawn position, throw velocity, owner velocity inheritance, pickup delay, and sticky/resting behavior.

Runtime findings:

- Grenade primary can be held too long. When the held timer crosses `activatetime60`, runtime forces the throw/fumble path and waits through the in-hand explosion window.
- Grenade secondary sets a different bounce scalar, making it the pinball/proxy behavior.
- Thrown mines and sticky devices arm after their timer reaches the runtime activation threshold.
- Combat knife uses deterministic thrown rotation and sets thrown-knife object flags.
- Dragon secondary discards the weapon from inventory and creates a proxy explosive entity.
- Remote Mine secondary is a special detonator action, not another throw.

### Deployed And Armed Entities

Some weapon behavior is best represented as an entity archetype even if the object is born from a weapon graph.

Required `.pdentity` families:

- Armed timed mine.
- Armed remote mine.
- Armed proximity mine.
- Armed Dragon proxy explosive.
- N-Bomb storm trigger.
- Deployed Laptop Gun autogun.
- Sticky mission devices where the runtime behavior is entity-like after attachment.

Laptop Gun correction:

- The current runtime creates the `OBJTYPE_AUTOGUN` first through `laptopDeploy()` and then applies projectile flight/stick behavior to that entity. The asset model should still describe this as a thrown physical payload that transitions into a deployed autogun entity on attach/rest; the runtime adapter may instantiate the entity early as an implementation detail.

### Special And Device Actions

Special/device functions stay in `.pdweapon` and call named runtime modules:

- Remote Mine detonator.
- Combat Boost activate/revert.
- Crouch/lower weapon.
- RCP120 cloak.
- Uplink action.
- Night vision, X-Ray, IR scanner, R-Tracker, EyeSpy, cloak device, and suicide pill.

These should not require `.pdprojectile` unless the action spawns a physical object.

## Physical Asset Candidates

These are first-pass candidate payloads. Exact canonical IDs can be normalized during emitter implementation.

| Weapon | `.pdprojectile` payload | `.pdentity` payload | Notes |
| --- | --- | --- | --- |
| Rocket Launcher | rocket, homing rocket | none | Homing behavior is selected by function flags. |
| Slayer | powered rocket, fly-by-wire rocket | none | Secondary needs fly-by-wire player and bot control module. |
| SuperDragon | grenade round | none | Projectile fire, not throw. |
| Devastator | grenade round, wall-hugger grenade round | none | Secondary sticks, waits, falls, then explodes. |
| Crossbow | sedative bolt, lethal bolt | none | Sticky bolt impact behavior. |
| Combat Knife | thrown knife | none | Sticky thrown physical with knife hit handling. |
| Grenade | timed grenade, proxy/pinball grenade | armed proxy grenade trigger | Primary timed, secondary proxy-style behavior. |
| N-Bomb | timed N-Bomb, proxy N-Bomb | N-Bomb storm trigger | Storm creation differs between timed and proxy activation. |
| Timed Mine | thrown timed mine | armed timed mine | Timer activates then explodes. |
| Proximity Mine | thrown proximity mine | armed proximity mine | Uses proxy registration and proximity checks. |
| Remote Mine | thrown remote mine | armed remote mine | Secondary special detonates owned mines. |
| Dragon | thrown Dragon explosive | armed Dragon proxy explosive | Discards weapon and doubles proxy trigger radius in one path. |
| Laptop Gun | thrown Laptop Gun carrier | deployed Laptop Gun autogun | Runtime creates autogun before projectile flight. |
| ECM Mine | sticky thrown device | armed ECM device | Needs final mission behavior audit. |
| Comms Rider | sticky thrown device | attached comms device | Needs final mission behavior audit. |
| Tracer Bug | sticky thrown device | attached tracer device | Needs final mission behavior audit. |
| Target Amplifier | sticky thrown device | attached target amplifier | Needs final mission behavior audit. |

## Named Runtime Modules Needed

These modules should be explicit graph node/module types rather than hidden weapon-specific branches:

| Module | Needed for | Reason |
| --- | --- | --- |
| `fire.hitscan` | Most firearms | Normal shot data already fits current weapon function fields. |
| `fire.auto_cadence` | Automatics and Laptop autogun | Uses initial/max RPM and special autogun tick cadence. |
| `fire.burst` | Burst-capable guns | Existing burst flags need graph-level expression. |
| `fire.charge_release` | Mauler secondary and any future charge weapons | Runtime behavior is not fully represented by current data fields. |
| `spawn.fired_projectile` | Rockets, bolts, grenade rounds | Bridges weapon graph to `.pdprojectile`. |
| `spawn.thrown_physical` | Grenades, mines, thrown knife, Dragon, Laptop | Bridges weapon graph to `.pdprojectile` or `.pdentity`. |
| `projectile.homing` | Homing rockets | Needs target prop policy and steering parameters. |
| `projectile.fly_by_wire` | Slayer secondary | Needs owner control, bot route fallback, static/vision handling, and self-destruct rules. |
| `projectile.wall_hugger` | Devastator secondary | Needs stick, wait, fall, and explode phases. |
| `projectile.sticky_attach` | Mines, bolts, knives, sticky devices, Laptop | Needs allowed attachment classes and detach/pickup behavior. |
| `entity.proxy_trigger` | Proxies, Dragon, proxy grenade, proxy N-Bomb | Needs trigger radius, owner/team policy, registration, and activation effect. |
| `entity.remote_detonatable` | Remote Mines | Needs owner broadcast and detonator action binding. |
| `entity.autogun` | Laptop Gun deployed mode | Needs target policy, ammo reserve, fire cadence, beam effects, pickup/recall, and net authority. |
| `presentation.weapon_visibility` | Laptop Gun and weapons with visible ammo/parts | Needed to keep authored presentation editable instead of hard-coded. |

## Runtime Implementation Implications

The first runtime slice after the design/audit work should be a replacement, not a compatibility layer:

- Rename the emitted weapon archive extension to `.pdweapon`.
- Remove `.pdwpn` assumptions from scanner/emitter code before expanding behavior coverage.
- Do not add `.pdwpn` migration, alias, or compatibility behavior.
- Add first-class catalog registration for `.pdprojectile` and `.pdentity`.
- Let `.pdweapon` archives embed nested `.pdprojectile` and `.pdentity` payloads for self-contained weapon archives.
- Share duplicate embedded payloads by SHA-256 over canonical archive content.
- Compile graph JSON into deterministic runtime IR before gameplay code consumes it.
- Regenerate base archives through the boot/self-heal path.

## Audit Closure

`c3814-s2` is complete enough to close: the behavior families, runtime ownership, physical payload candidates, and the 86-weapon parameter matrix are checked in.

The named module parameter pass is in [weapon-graph-module-parameters.md](weapon-graph-module-parameters.md). It turns the matrix into authored graph module names for:

- Primary and secondary graph function type.
- Any nested `.pdprojectile` and `.pdentity` payloads.
- Special named runtime modules required.
- Flags and constants that must become authored graph parameters.
- Any behavior still hard-coded in C after the mapping.

The next subtask is `c3814-s9`: translate the design and audit set into scanner/emitter/catalog/runtime implementation tasks.
