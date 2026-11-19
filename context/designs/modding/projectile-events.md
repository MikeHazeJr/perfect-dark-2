# Projectiles, mines and autoguns as behavior graphs

Status: CORE, 2026-11-17. Draft for the week note's "Projectiles, mines,
autoguns" row ("the graph selects the projectile and entity modules for base
weapons too"), under decision 1 ("Players should be able to recreate any
existing game behavior with the nodes"). B-1261 and slice 1 are written on
`week-pd/core-s6` (78072e83, 772f0497; see "Slice 1 as written"), not yet
run in the game; slices 2 to 6 are plans. Facts from a read-only survey of `week-pd/core-s5` (a51e12bc);
`pc` is `src/game/propobj.c`, `bg` is `src/game/bondgun.c`.

## Where it stands

- **Records, not programs.** Each base weapon function that throws or fires
  something has a `pd.projectile_graph.v1` archive (28 on shared base 7)
  and, for mines, the Laptop gun and devices, a `pd.entity_graph.v1`
  archive. The runtime folds every node into one record of numbers and
  `has_*` booleans (`weapon_graph_runtime.c`); no event, gate or edge
  decides anything.
- **Custom weapons read the records; base weapons mostly do not.**
  `weaponGetCustomProjectileGraph` and `weaponGetEntityGraphForGameplay`
  (pc:1571, 1588) return nothing for a base weapon, so the base timers,
  contacts, mines and autoguns run native branches keyed by weapon number
  (about 67 weapon-specific behaviors: spawn 17, airborne 8, wall and floor
  contact 9, chr and prop contact 5, timer and arming 7, proximity 3, remote
  2, damage taken 4, deploy and autogun 5, pickup 3, free 4).
- **But some base paths read records already:** the spawn getters (bg:1976
  to 2178: model, speed, scale, timer, activation, reflect, sound),
  `projectileApplyGraphRuntime` at every spawn site (bg:5765, 6068, 6338,
  6433; chraction.c:10582), the proximity radius in `weaponTick` and the
  Laptop's entity record in `laptopDeploy`. Those values come from the same
  weapon function, so they agree with the original, except B-1261 (the
  record's function flags were copied into the projectile's flags; fixed
  on `week-pd/core-s6`).
- **Authority.** Spawning is the host's (bg:6127, 5819; SVC_PROP_SPAWN); damage
  is the host's (pc:17859); `weaponTick`, the airborne tick, contacts,
  proximity and remote mines run on host and client alike, with no gate.
  The autogun is the host's only.
- **Parsed but unused fields** in the v1 records (homing targets, fly-by-wire
  routing, sticky filters, timed pause, detonation policy, owner filters)
  and **behaviors with no field at all** (the bolt beam and wobble, knife
  poison, the grenade's prime carry-over, the rocket's speed cap and power
  limit, landing-stall detonation, the Dragon's double range, damage-class
  immunity, the mine flags) are listed in the survey; a v1 record cannot say
  them, which is the reason for programs.

## Proposal

1. **Programs per archive, as vehicles have them.** A projectile or entity
   archive's graph becomes a program compiled by a runner built on the
   mission graph runner's rules (events, gates, `og.*` native modules, each
   node at most once per event, bounded follow-up events), the way MODS
   built the vehicle runner. The schema moves to `pd.projectile_graph.v2`
   and `pd.entity_graph.v2`, D-006A's "new executable version, never a
   silent reinterpretation of v1": the launch numbers stay one
   `projectile.spawn_state` node, and the lifecycle is events wired to
   modules. The extractor writes v2 for every base archive behind a weapon
   export version; mods made after the re-extract copy v2 templates; then a
   v1 archive is refused with a message naming v2 (the vehicle migration's
   order, vehicle-graph-v1.md Rule 5).
2. **Events at the native seams**, each carrying the projectile or entity
   instance (its prop, the weapon object, the owner player or chr, the
   weapon function), fired where the native branch decides today:
   `event.projectile.spawned`, `event.projectile.timer_expired`,
   `event.projectile.hit_surface` (wall or floor, with the surface normal),
   `event.projectile.hit_target` (a chr or prop), `event.projectile.stopped`
   (came to rest), `event.projectile.damaged`, `event.projectile.freed`,
   `event.mine.proximity` (the prop that set it off), `event.mine.remote`,
   `event.entity.deployed`, `event.autogun.target_seen`.
3. **The native branches become named modules**, one per behavior family,
   each running exactly today's code for the instance it is given:
   `og.projectile.explode` (the weapon's explosion type), `og.projectile.stick`,
   `og.projectile.bounce`, `og.projectile.slide`, `og.projectile.land`
   (knife and bolt landing), `og.projectile.damage_target`,
   `og.projectile.nbomb_storm`, `og.projectile.wall_hug`,
   `og.mine.arm`, `og.mine.detonate`, `og.entity.deploy_autogun`,
   `og.autogun.fire`, plus the gates the native code asks
   (`gate.projectile.fuse_armed`, `gate.projectile.target_kind`,
   `gate.mine.owner_filter`, `gate.mine.trigger_kind`). Module parameters
   carry what the original keys by weapon number (explosion type, radius,
   damage class), so a mod sets them instead of picking a weapon number.
4. **Base weapons wired by the extractor.** Every base archive's program wires
   its original behavior (the grenade: `timer_expired` to `explode`; the
   rocket: `hit_surface` and `hit_target` to `explode`; the throwing knife:
   `hit_target` to `damage_target`, `hit_surface` to `land`; the proximity
   mine: `hit_surface` to `stick`, `mine.proximity` through
   `gate.mine.trigger_kind` to `mine.detonate`), so rewiring one changes the
   game.
5. **Fail closed and prove it.** A base or v2 archive whose program does not
   compile refuses at registration, so its weapon cannot fire (as a vehicle
   with a refused graph cannot be mounted). A parity probe in the game counts,
   per seam, what the original branch would have done and what the program
   did (as the object rule probe does); the smokes require differ=0.
6. **Authority.** Rule events fire where the outcome is decided today. On a
   networked client, the native branches that run for presentation keep
   running (the host's explosions, damage and frees arrive by replication),
   so a base program and the client agree by parity; what a mod's rewired
   program changes is decided by the host and replicated. To confirm with
   MULTIPLAYER before slice 3: which client branches are presentation only.
7. **The guards go last.** When every seam of a family runs through the
   program, `weaponGetCustomProjectileGraph` and its siblings stop excluding
   base weapons, and base and custom weapons meet one contract (D-006A,
   migration step 5). The row is ticked then.

## Slices

1. **Proof tooling and record-only events.** B-1261 (done). The v2 schema,
   the projectile runner (compile rules, refusals, `[projectile-graph]` unit
   cases), the extractor writing v2 programs whose events are wired to
   nothing yet, and the record-only events `spawned`, `hit_surface`,
   `hit_target`, `stopped`, `timer_expired` and `freed` with per-stage
   totals. A smoke, `projectile_events_smoke`, on Felicity with
   `--debug-spawn-weapon` and the fire actions: the grenade, the N-bomb,
   the rocket launcher (both modes), the Slayer's fly-by-wire rocket, the
   Devastator (both modes), the crossbow, the throwing knife, the three mines
   and the Laptop gun; it checks one spawn and one landing or detonation line
   each, and the Slayer rocket flying (B-1261 in play).
2. **Timers and detonation** as rule events with the parity probe
   (`og.projectile.explode`, `og.projectile.nbomb_storm`,
   `gate.projectile.fuse_armed`).
3. **Contacts:** surface and target (stick, bounce, slide, land, damage,
   wall hug, explode on contact).
4. **Mines and damage taken:** proximity, remote and timed, a projectile set
   off by a shot.
5. **Deployment and autoguns:** the Laptop sentry and `autogunTick` as
   modules.
6. **One contract:** the base guards go, the v1 path is refused, the row
   ticks.

## Slice 1 as written (CORE, 2026-11-17)

- `port/src/projectile_events.c`: `projectileEventRecord(kind, obj)` counts
  the host's events per stage and logs the first of each kind for a weapon
  and function; `projectileEventsReport(why)` logs the totals at stage end
  (`mainEndStage`) and at a smoke's scripted exit, then starts again.
- Seams, each after the native decision: `spawned` after
  `projectileApplyGraphRuntime` at the player's throw
  (`bgunCreateThrownProjectile2`), both fired paths
  (`bgunCreateFiredProjectile`) and an AI shot (`chrTickShoot`); `bounced`
  at the bounce count; `hit_target` where a weapon projectile struck a prop
  (`g_EmbedProp`); `landed` at the top of `objLand`; `ended` in
  `objFreeProjectile`; `detonated` in `propExplodeWithSound` for a weapon
  prop. Not covered yet: the AI's thrown grenades (`chrTickThrowGrenade`
  through `objDrop`) and the simulants' throws and Slayer rockets
  (`botact.c`); slice 2 moves `spawned` to one place that every spawn
  passes.
- The smoke harness's `give_weapon` (player 0 gets a weapon with full
  ammunition for both functions and draws it) and `projectile_events_smoke`
  (every base projectile weapon on Felicity; the Slayer's fly-by-wire rocket
  without SLIDING, B-1261).

## Slice 2 plan: timers and detonation (CORE, 2026-11-17)

- **Programs.** `weaponGraphCompileJson`'s callers also compile the archive's
  text with the mission graph runner in a projectile domain:
  `projectile.*` and `entity.*` nodes are data (skipped, as level data is),
  `event.projectile.*`, `gate.projectile.*` and `og.projectile.*` are the
  program, and a mission or level event in a projectile program is refused
  (and the other way round). The program lives in the projectile or entity
  runtime record and is freed with it.
- **Schema.** The extractor writes `pd.projectile_graph.v2` and
  `pd.entity_graph.v2`: the v1 data nodes unchanged, plus the program. The
  weapon export version moves, so every install re-extracts its weapon
  archives once.
- **The rule event.** `event.projectile.timer_expired` at each fuse in
  `weaponTick` (grenade, N-bomb, Devastator round, rocket, timed mine,
  N-bomb storm), fired instead of the native branch when the record's
  program handles it; the branch becomes `og.projectile.fuse_expired`, the
  original code for that weapon, so every base program wires
  `timer_expired` to it. A v2 archive whose program does not handle the
  event refuses at registration (fail closed); a v1 custom archive keeps the
  custom arm it has today.
- **Parity.** The probe counts each expiry the native rule would have acted on
  and what the program ran; the campaigns, `projectile_events_smoke` and the
  soak require differ=0, as the object rule probe does.

## Slice 2 as written (CORE, 2026-11-17 to 2026-11-19; not run in the game yet)

- **Runner** (aa3d21f4): `missionGraphProgramCompileProjectile` compiles a
  projectile domain: the seven projectile events (kinds 19 to 25) and no
  other event; data nodes skipped; a projectile event in a mission or level
  graph is refused. An event carries its instance (`event.instance`).
- **Registration** (da375c94): a v2 graph keeps its text
  (`program_json`), and `projectileProgramRegister` compiles it with the
  module list `og.projectile.fuse_expired`. A program that does not compile
  refuses its archive, so its weapon cannot fire. A v1 graph has no program.
- **The fuse rule** (54ab7092): `projectileFuseRule(obj)` at `weaponTick`'s
  four fuses, on the host. With a program, `event.projectile.timer_expired`
  decides: the fuse runs when the program reaches
  `og.projectile.fuse_expired`, and does not when it does not (this differs
  from the plan above, which refused such an archive at registration: an
  author may leave a fuse unwired on purpose, and the probe counts it). With
  no program the native rule decides, so a mod made before this slice (v1:
  the crystal pistol, the needler, the example weapon) behaves as it did.
  Parity: `fuse.compared` and `fuse.differ` in the stage totals.
- **Extractor** (e41fc471): every base projectile and entity archive is v2
  with `fuse` wired to `fuse_expired`; the weapon export version moved.
- **Which archive a projectile belongs to** (B-1263, 2026-11-19): the launch
  binds it. A fired rocket, bolt or grenade round carries its own weapon
  number (WEAPON_ROCKET, WEAPON_BOLT, WEAPON_GRENADEROUND), not the gun's,
  and the thrown Laptop gun is an autogun object, so the object alone names
  another weapon's archive or none. `projectileApplyGraphRuntime` binds the
  object's prop (and its generation) to the archive whose record it applies;
  events and rules name the gun and its function (`weapon=base:rocketlauncher
  func=primary object=base:rocket`) and use that archive's program. An unbound
  object is identified by its own weapon and function, right for everything
  thrown. Launches that apply no record (a simulant's rocket, the chopper's,
  an AI's thrown grenade) stay unbound and keep the native rules until the
  spawn seam is one place.
- **Validator** (2026-11-19): `validate_projectile_entity_graph` in
  `tools/asset_archive_conformance.py`. A projectile or entity graph names
  one of its family's two schemas; a v1 graph carries no program node; a v2
  graph's program is held to the runner's rules (only projectile events,
  only the modules a projectile program has, unique ids, execution edges
  with the right ports), so a program the game would refuse fails the
  archive check first. A projectile event in a mission or level graph is an
  error there too. Test: `tools/tests/test_projectile_program_conformance.py`.

For slice 6 ("the v1 path is refused"): do not refuse v1 archives that mods
already hold. Slice 5 of the object events met the same question for levels
(object-events.md, "Levels authored before slice 5") and kept older content
loading with the rules it was made under; a v1 projectile keeps the native
rules the same way, and the guards that go are the ones that keep base
weapons off the program path.

## Slice 3 as written: contacts (CORE, 2026-11-19; on `week-pd/core-s7`, not run in the game yet)

What a thrown or fired projectile does when it strikes something was
decided in `projectileTick`, `projectileLaunch` and `objLand` by tests on
the object's weapon number (16 of them). Those tests are gone from the
contact code. Each seam asks the projectile's program which modules its
rule event runs, and then runs the original code for the modules chosen,
where that code has always run. A module is a choice, not a call: the
physics, the sweep and the order of operations stay the game's.

**The original rules are one table**, `port/src/projectile_rules.c`
(no game state, linked into pd-tests): which modules each event runs for a
weapon number and function. The extractor writes every base projectile
archive's wiring from it (the object a function launches, then that
object's rules), the seams compare a program's choice with it (the parity
probe), and a projectile with no program (a v1 archive, or an unbound
launch) is answered by it. So a base program and the original agree by
construction, and the probe proves it in play.

| Event (kind) | Fired | Modules it applies |
|---|---|---|
| `event.projectile.contact` (26) | the sweep of a sticky projectile (everything thrown or fired) struck the background, a character or an object, before anything is decided | `stick`, `wall_hug`, `pierce`, `impact` |
| `event.projectile.launch_blocked` (27) | its first step, from where it was made to the muzzle, struck something (`projectileLaunch`) | `spend_fuse` |
| `event.projectile.bounced` (20, was a record) | it bounced off a wall or floor | `spend_fuse` |
| `event.projectile.came_to_rest` (28) | it lies or all but lies still while still in flight (on the floor, falling, speed or movement under 0.1) | `spend_fuse` |
| `event.projectile.landed` (22, was a record) | it stuck or came down (`objLand`) | `shield_device`, `land_bolt`, `land_knife`, `poison_target` |
| `event.projectile.timer_expired` (25, slice 2) | a fuse ran out | `fuse_expired` |

`spawned`, `hit_target`, `ended` and `detonated` stay records with no
module. A module wired to an event that does not apply it would do nothing,
so registration refuses the program and the validator fails the archive,
both naming the pair ("og.projectile.stick has no effect on
event.projectile.bounced").

| Module | The original code it selects | Base wiring |
|---|---|---|
| `og.projectile.stick` | it sticks where it struck, unless the target is itself in flight, is glass, or is a shielded character | the three mines, the ECM mine, the comms rider, the tracer bug, the target amplifier, a bolt, a thrown knife, and any function flagged to stick to walls |
| `og.projectile.wall_hug` | it sticks, hangs for its wall timer (8 seconds), falls, and goes off where it lands | the Devastator's second function |
| `og.projectile.pierce` | a bolt's or thrown knife's contact: it does not stick into another weapon object, breaks glass it hits, wounds a character on its first flight (the weapon's damage; the sedative bolt's dizziness comes with that), leaves a spark, and counts in the owner's shot statistics | a bolt, a thrown knife |
| `og.projectile.impact` | a rocket's contact with a target: 2 damage to a character, 100 to an object, then it is consumed and its fuse is spent | a rocket, a homing rocket |
| `og.projectile.spend_fuse` | its fuse runs out now; the fuse rule (`timer_expired`) decides what follows | a rocket on `bounced` and `launch_blocked`; a grenade round on `came_to_rest`; the N-bomb's first function on `came_to_rest` behind `gate.projectile.fuse_armed` |
| `og.projectile.shield_device` | once planted it cannot be shot away or knocked off | the ECM mine, the comms rider, the tracer bug, the target amplifier |
| `og.projectile.land_bolt` | it lands as a crossbow bolt (pushed in along its flight, then quivers) | a bolt |
| `og.projectile.land_knife` | it lands as a thrown knife (set by the surface) | a thrown knife |
| `og.projectile.poison_target` | it poisons the character it sticks in | a thrown knife |

`gate.projectile.fuse_armed` passes while the weapon object's timer is not
idle (0 or more).

A hand grenade, a Dragon and anything else with no contact wiring bounces
and comes to rest, as before.

**Which archive**: B-1263's binding. A bolt's rules are the crossbow's
archive's, a rocket's the rocket launcher's or the Slayer's, a grenade
round's the Devastator's or the Super Dragon's. The bolt item's and the
grenade round item's own archives carry the same wiring for an object no
launch bound. The extractor wires an archive for the object its function
launches (`projectileRulesLaunchedObject`, pinned against `bondgun.c` and
`chraction.c`): the guards' rocket launcher (weapon 0x2c), which only a
character fires, makes a rocket too, so its archive carries the rocket's
wiring.

**Parity**: `rules.compared` and `rules.differ` in the stage totals; the
first choice for a weapon, function and event logs `PROJECTILE.RULE: contact
weapon=base:crossbow func=primary object=base:bolt program=... modules=stick+pierce
native=stick+pierce`; the first eight differences log `PROJECTILE.RULE
differs: ...`. `projectile_events_smoke` requires nine of the first-choice
lines and forbids a difference.

**Authority**: host and client alike choose by the program (a client's copy
of a thrown item is simulated locally, and a fired one it did not launch is
unbound there and keeps the original rules, which are the same for base
programs); damage stays the host's through its callees, and only the host
counts parity. This is the answer to "which client branches are presentation
only" for contacts: none of the contact decisions is, so the client runs the
same rule. A mod that rewires a fired projectile is decided on the host; the
client learns the launch's archive only when the spawn message carries it
(MULTIPLAYER's, with graph weapons over the network).

**Left native, on purpose** (each keyed by weapon number still): the flight
model (homing, the Slayer's fly-by-wire, the rocket's thrust cap and power
limit, the bot Slayer's waypoint steering in `rocketTickFbw`, which also
spends its own fuse); presentation (trails, the pinball grenade's smoke and
sounds, the knife's woosh and hit sound); the continuations of a module in
`weaponTick` (the wall hugger's countdown and fall, the bolt's quiver and
beam); the thrown Laptop gun, an autogun object that sticks to the
background only (slice 5); and the custom-weapon record clauses beside each
seam (`has_sticky_attach`, `has_impact`, `has_wall_hugger`, bounce limits),
which a v1 mod archive still drives.

**Not covered**: a projectile whose weapon has no fuse of its own in
`weaponTick` ignores `spend_fuse` (slice 4 gives every program a fuse).

Tests: `tests/test_projectile_rules.cpp` (the table, a program compiled and
fired through the runner, and the source pins of the seams, the extractor
and the validator), `tools/tests/test_projectile_program_conformance.py`
(the applicability rule), pins in `tests/test_custom_projectile_log_static.cpp`.
The weapon cache kind moved (`_contactrules`), so an install re-extracts its
weapon archives once.

## Not in scope

- Held weapons (the fire types in `bondgun.c`): the week note's Held weapons
  row, which waits for Mike's answer about the parked
  `week-pd/inflight-20261114` lane.
- Effects (`pd.effect_graph.v1` and v2 profiles): already programs.
- Vehicle weapons (V5): MODS's `og.vehicle.fire_weapon` uses these programs
  when they exist.
