# Held weapons as behavior graphs

Status: Core Weapons (claude-pd-core-weapons-20261121), started 2026-11-21.
Workbench T-MODDING-025. For the week note's "Held weapons" row, under
decision 1 ("Players should be able to recreate any existing game behavior
with the nodes"). Source: the gun-code audit, Claude Doc "PD2 held weapons:
gun-code audit and node design" (https://claude.ai/artifact/7fgSVVLGzHGRddzXoUJ1Xz),
read at dev e648546. Mike's answers of 2026-11-20 and 2026-11-21 are in the
audit's last section and in "Mike's answers" below. `bg` is
`src/game/bondgun.c`, `pc` is `src/game/propobj.c`.

## Where it stands (before slice 1)

- **Records.** Every base weapon is a `.pdweapon` whose two function graphs
  (`behavior/primary.graph.json`, `behavior/secondary.graph.json`) are
  `pd.weapon_graph.v1`: an `event.trigger_pressed` node and one action node
  (`fire.hitscan`, `melee.strike`, ...) whose params are the whole function
  definition. The runtime composes both into one record IR and reads its
  values first (`weapon_graph_runtime.c`); nothing in it decides anything.
- **The v2 profile.** `pd.weapon_graph.v2` with `"profile":
  "held_single_shot.v1"` is an offline custom-weapon executor (D-006A):
  press and held-ready events, ammo, cooldown and boolean state gates,
  `fire.hitscan` actions with their own function definitions, routed by
  outcome ports. It runs only for a private custom slot and is refused in
  netplay, Theater and by bots (`weapon_graph_v2_catalog.cpp`).
- **Weapon numbers.** About 15 behaviours are still decided by weapon number
  in `bg` (charge, spin-up extras, pellets, wall pass, beam, the spawn
  if-chain, deploy, cook, melee ammo, the cloak), and targeting is decided in
  eight places with no shared filter (the audit's tables).

## The model (every slice)

The projectile slices' pattern (projectile-events.md), for held weapons:

1. **Programs on the shared runner.** A held function graph of
   `pd.weapon_graph.v2` (no profile) is the v1 record, unchanged, plus a
   program of `event.weapon.*`, `gate.*` and `og.*` nodes joined by execution
   edges, compiled by the mission graph runner's weapon domain
   (`missionGraphProgramCompileWeapon`). The record still feeds the native
   pipeline; the program decides at the seams.
2. **Events at the native seams**, each carrying the hand, fired where `bg`
   decides today, for the player's hands and a simulant's alike.
3. **Modules are choices.** The seam asks the program which modules its event
   runs and then runs the original code for the modules chosen, where it has
   always run. A module's settings (node params) carry what the original keys
   by weapon number (the Shotgun's six pellets are its shot node's default
   pattern).
4. **The original rules are one table** (`port/src/weapon_rules.c`, no game
   state, linked into pd-tests): the extractor writes every base program from
   it, the seams compare a program's choice with it (the parity probe,
   `rules.compared` and `rules.differ`), and a function with no program (a v1
   archive) is answered by it.
5. **Bots use the same programs.** The simulant fire path (`bg` near 6792,
   `game_0b0fd0.c` near 683) asks at the same seams, so players and bots
   cannot drift apart. Custom bot types may define their own behaviour
   separately; bots must be able to use PD2's jump (Mike, 2026-11-21; the AI
   lane's, recorded here because it is a stated requirement).
6. **Guards kept.** Dual wield doubles damage and widens spread in shared code,
   so every parity case has a dual-wield twin. Netplay and Theater keep their
   guards: a base program equals the table by construction, so every peer and
   a replay agree; what a mod's rewired program changes is decided by the host.
7. **Weapon-number guards go last**, after every slice has proved parity.

## Mike's answers

- The target filter offers both "any vehicle" and "occupied vehicles only".
- Spread gets a visible or defined pattern: a set of points shown over the
  reticle, draggable or picked from presets.
- Base guns' quirks are node settings (the Shotgun's 6 pellets).
- Custom bot types can have their own behaviour; bots must be able to use our
  jump.
- Base nodes are more abstract than "int + int", while still allowing those
  (the value nodes of slice 8).
- The Crystal Pistol goes through slice 5 (one charge module with the
  Mauler's).
- All 8 slices count for 1.0. Order: slices 1 to 5, then projectile slice 7,
  then weapon slice 6, then 7 and 8. Each slice merges to dev when it builds
  and passes unit tests; smokes wait for Integration's finalization batch.

## Slices

| # | Slice | Done when |
|---|---|---|
| 1 | The held v2 graph on the shared runner; the v2 archive conflict settled | today's single-shot profile runs unchanged on the shared runner |
| 2 | Events and checks at the native seams: trigger, ready, clip empty, reload done; Has ammo, Ready to fire, State is | fire decisions match native with differ = 0 |
| 3 | Trigger nodes Fire once, Burst, Automatic; one Use ammo node | all 86 base weapons extractor-wired for cadence; burst flags and burst_count retired into Burst |
| 4 | Shot nodes: Hitscan shot (pellets, through walls), Beam, Launch projectile, Throw, Melee strike, Deploy, Mode toggle; Reload styles | the Shotgun, Farsight, Laser, spawn if-chain, Laptop, cloak and Tranquilizer special cases are node settings |
| 5 | Charge then release and Hold to cook, one module with the crystal pistol's charge | the Mauler, the grenade and the crystal pistol run from it |
| P7 | Projectile slice 7: flight presentation, spawn and free as rule events | projectile-events.md |
| 6 | Target filter: the candidate prop on events, homing re-checks in flight, vehicles lockable | Mike's rocket example runs; the eight pickers use the filter |
| 7 | Feel nodes, recoil jitter, slide and casings for mod guns | mod guns feel like base guns |
| 8 | Value nodes and the creator: the palette shows designer nodes with named inputs, writes v2 | a modder builds both examples in game with no raw JSON |

## Slice 1 as written (Core Weapons, 2026-11-21)

**The runner's weapon domain** (`port/src/mission_graph_runner.cpp`,
`port/include/mission_graph_runner.h`):

- `missionGraphProgramCompileWeapon`: like a projectile program, only the
  `event.*`, `gate.*` and `og.*` nodes are the program; every other node is
  the function record, skipped. The record's own `event.trigger_pressed` (and
  `_held`, `_released`) nodes are inert and may only have structural edges. Its
  events are the weapon events (`event.weapon.trigger_pressed` 35,
  `event.weapon.ready_while_held` 36), each carrying the hand's executor as
  the event's instance; a weapon program refuses every other program's events
  and an unknown `event.weapon.*`, and every other program refuses the weapon
  events.
- **Modules that finish later.** A shot is accepted now and delivered ticks
  later, so a weapon program's module may be asynchronous: `module_known`
  answers `MISSION_GRAPH_MODULE_ASYNC`, the module leaves by its outcome
  (`completed`, `blocked`, `failed`, `cancelled`, never `out`), and its run
  returns the outcome or `MISSION_GRAPH_RUN_PENDING`. A weapon program's gate
  may also leave by `failed` when it cannot be answered (its run returns
  `MISSION_GRAPH_RUN_GATE_FAILED`), and then neither `pass` nor `fail` is
  followed.
- **Activations.** `missionGraphActivationCreate`, `Begin`, `Route`, `Walk`
  and `SetTrace`: one event's walk is kept, so a pending module's outcome joins
  the same breadth-first walk later and every node still runs at most once per
  activation. `missionGraphProgramFire` is now Begin and Walk on a fresh
  activation: one walk for every program kind (mission, level, projectile,
  weapon), and the existing programs walk exactly as before (their tests are
  unchanged).

**The profile on the runner** (`port/src/weapon_graph_v2.cpp`): the
`held_single_shot.v1` grammar is still checked there (the same keys, types,
ports, exports, acyclicity and reachability, and the same messages), then the
graph is compiled node for node as a weapon program (`event.weapon.*`,
`gate.weapon.has_ammo`, `gate.weapon.ready_to_fire`, `gate.weapon.state_is`,
`og.weapon.set_state`, and `og.fire.hitscan` as an asynchronous module) and
walked by the runner's activation. The executor keeps only what the profile
owns: the per-hand boolean state, the native jobs of its actions (tickets,
start, tick, wake, cancel, cleanup) and its result. Its 24 unit cases pass
unchanged, trace strings included, which is the slice's "runs unchanged";
the in-game proof is the existing `weapon_mesh_ingress_smoke` and
`graph_v2_world_hit_smoke` in the finalization batch.

**The archive conflict, settled by the v2 record.** The audit's conflict
(v2 refused the variables, shared-context and presentation members that
conformance requires) belonged to the profile, which reads none of them. A
held function graph of `pd.weapon_graph.v2` without a profile is now the v1
record plus a program, read by the v1 path, so its variables (`$name`), shared
contexts, settings and presentation work as a v1 weapon's do:

- `weaponGraphTextIsHeldProgram` tells it from the profile;
  `wgV2ArchiveInspect` sends it down the v1 path (only the profile is the
  profile executor's).
- The composers (`composeWeaponGraphFromAuthoringFiles`,
  `weaponGraphCompileWeaponSourceJson`) copy a v2 function graph's record
  members only (`builderAppendRecordMembers`: no program node, no execution
  edge), and the function graph's text becomes that function's program
  (`weapon_graph_ir_t.held_program_json[2]`). A single v2 function graph
  compiled directly names its function in `graph_id`; a profile graph is
  refused there ("run by the profile executor").
- The IR digest covers a function's program, so editing a program changes the
  weapon's identity; a v1 weapon's digest is unchanged.
- `weaponGraphRuntimeRegisterHeldIr` compiles each function's program with the
  held module table and keeps it beside the records
  (`weaponGraphRuntimeHeldProgram(weaponnum, func)`); a program that does not
  compile refuses the whole weapon (fail closed). Slice 1's table knows no
  module yet: slices 2 to 8 add the vocabulary.

Not yet: `$name` in a program node's params (the record's are substituted; a
program's params are read as text by the module table, which slice 2 resolves
from the weapon's variables), the conformance validator for weapon programs
(slice 2, with the first base programs), and no base archive is v2 yet (slice
2's extractor change).

Tests: `tests/test_mission_graph_runner.cpp` ("weapon programs take the weapon
events, and only they do"; "a weapon program's asynchronous modules leave by
their outcome, within one activation"; "a v2 held function graph registers its
record and its program"), `tests/test_weapon_graph_v2.cpp` (the 24 profile
cases, and "v2 single-shot profile compiles and walks on the shared graph
runner").

## Slice 2 as written (Core Weapons, 2026-11-21)

What an idle hand does (fire, click on an empty clip, reload) was decided in
`bgunTickIncIdle` by the native ammunition check (`bgun0f098ca0`) and the
trigger. It is now the hand's function's program's choice, at the same place,
and the native branches that follow it (the reload request, the out-of-ammo
switches) run when nothing was started, as before.

| Event (kind) | Fired | Modules it applies |
|---|---|---|
| `event.weapon.trigger_pressed` (35) | an idle hand's trigger is down and a press arrived since it last acted | `fire`, `dry_fire`, `reload`, `set_state` |
| `event.weapon.ready_while_held` (36) | an idle hand's trigger is down and was already down | `fire`, `dry_fire`, `reload`, `set_state` |
| `event.weapon.clip_empty` (37) | an idle hand's trigger is up and its clip is below what a shot needs while there is reserve | `fire`, `dry_fire`, `reload`, `set_state` |
| `event.weapon.trigger_released` (38) | the trigger came up (`bgunSetTriggerOn`), a record | `set_state` |
| `event.weapon.reload_done` (39) | the hand left its reload for idle (`bgunSetState`), a record | `set_state` |

| Module or gate | What it selects or reads |
|---|---|
| `og.weapon.fire` | the hand starts its attack with its function (`HANDSTATE_ATTACK`) |
| `og.weapon.dry_fire` | the empty click (`HANDSTATE_ATTACKEMPTY`) |
| `og.weapon.reload` | the hand reloads its function's clip (`HANDSTATE_RELOAD`) |
| `og.weapon.set_state` | the hand's named state `key` becomes `value` (true or false), as the walk reaches it |
| `gate.weapon.has_ammo` | the clip holds `rounds` (default 1; a full clip or a function with no ammunition always passes) |
| `gate.weapon.has_reserve` | there is ammunition to reload from, or the clip loads itself |
| `gate.weapon.ready_to_fire` | the hand is idle |
| `gate.weapon.state_is` | the hand's named state `key` is `equals` |

- **The table** is `port/include/weapon_rules.h`, header-only (static
  inline) so every target has it without a CMake line: the kinds, what each
  event applies, the original's idle rule (`weaponRulesNativeIdle`), its
  rounds (`weaponRulesNativeRounds`: the Shotgun's double blast 2, the
  Tranquilizer's sedative shot 4, else 1), and the gate answers. The
  Shotgun's and Tranquilizer's minimum-ammunition weapon tests became the
  base programs' `rounds` setting.
- **The base programs** (`romextract_pdweapon.c`, `s_emitHeldProgramNodes`):
  every function that exists is v2 with the original's idle rule: pressed and
  held go to `has_ammo` (the table's rounds), its pass to `fire`, its fail to
  `has_reserve`, whose pass is `dry_fire`; `clip_empty` goes to `reload`. An
  empty function has no program. pd-tests fire this wiring through the runner
  against the table for every hand state (both ammunition uses, clips of 1 to
  8, every load, with and without reserve, rounds 1, 2 and 4), so a base
  program and the original agree by construction. The closure marker is
  `embedded.v17` and the weapon cache kind gains `_heldprograms`, so an
  install rewrites its weapon archives once.
- **The seam** (`weapon_events.c`): a press waits for the idle seam and a
  release drops it; with a program the walk chooses (gates answered from the
  hand as the native check reads it, `set_state` writing the hand's states,
  which reset when the hand takes another weapon or function), with none the
  table answers. The parity probe counts each choice a program made where the
  original has a rule (`rules.compared`, `rules.differ`, the first eight
  logged as `WEAPON.RULE differs:`), and the first choice for a weapon,
  function and event logs `WEAPON.RULE: <event> weapon=... func=...
  program=yes modules=... native=...`. The totals go out with the projectile
  totals (`WEAPON.EVENTS: ...`, at the stage end and a smoke's scripted exit).
  A mod weapon's number has no original rule: its choices are logged, not
  counted.
- **Registration** (`weapon_graph_runtime.c`): a module wired to an event
  that does not apply it refuses the weapon ("og.weapon.reload has no effect
  on event.weapon.trigger_released"), and so does a setting the table cannot
  read (rounds outside 1 to 255, a state node without a key or a boolean,
  more than eight state keys). A "$name" setting reads the weapon's
  variables; one that names none refuses the weapon.
- **The Weapons creator** keeps a program: `weapon_graph_editor_io.cpp` keeps
  each program node's settings and each execution edge's ports through
  compose and split, and writes v2 when a graph has a program; a joined v2
  document's program nodes name their function in `subgraph`, and
  `weaponGraphValidateJson` checks each function's program as registration
  would (a "$name" setting is left to registration there).
- **Conformance** (`tools/asset_archive_conformance.py`,
  `validate_weapon_function_graph`): the same rules for every `.pdweapon`
  function graph; v1 carries no program node; the profile is left to its own
  executor. All 184 function graphs in the repo and on shared base 14 pass.

**Left native, on purpose** (later slices): the reload request
(`modenext`), the out-of-ammo switches, the function change, and everything
after an attack starts (cadence: slice 3; the shot: slice 4). Bots fire
through `chrTickShoot`, not the idle seam; their cadence and spread are
slices 3 and 4.

Tests: `tests/test_mission_graph_runner.cpp` ("the held weapon rule table
names its modules, gates and events"; "the base idle wiring chooses what the
original does, for every hand state"; "a held program registers only with
modules its events apply and settings it can read"),
`tests/test_weapon_graph_editor_io.cpp` ("weapon editor keeps a function's
program through compose and split"),
`tools/tests/test_projectile_program_conformance.py` (`HeldWeaponPrograms`).

## Slice 3 as written (Core Weapons, 2026-11-21)

An attack's cadence (one round, a burst, an automatic with its rate, spin and
burst) and what each shot spends were read from the function record's burst
flag bits, its rates and spin, and three weapon-number tests (the Shotgun's
13-tick burst gap, the Laser's free single shot, and `bondgun.c`'s
automatic-by-rate rule). They are now the attack's **plan**: the trigger node
its program started, with that node's settings, and `og.weapon.use_ammo`'s
rounds.

| Module | Settings (unit, range; a setting left out is the record's) | What the attack does |
|---|---|---|
| `og.weapon.fire_once` | none | one round, then the attack ends |
| `og.weapon.burst` | `count` (rounds, 2 to 50), `gap` (60 Hz ticks between rounds, 0 to 255) | `count` rounds from one press, released or not |
| `og.weapon.automatic` | `rate`, `start_rate` (rounds a minute, above 0 to 3600), `spin_up`, `spin_down` (ticks, 0 to 10000), `burst` (rounds it fires once started, released or not, 0 to 255), `aimed_burst` (whether that burst holds while aiming) | fires at `rate` while held, speeding up from `start_rate` over `spin_up` ticks and slowing over `spin_down` after release |
| `og.weapon.use_ammo` | `rounds` (rounds a shot takes, 1 to 255) | each shot takes `rounds` from the clip; without it the attack is free |

The three triggers replace slice 2's `og.weapon.fire`. They and `use_ammo`
apply at the idle seam's three rule events; on a record event registration
refuses them.

- **The table** (`port/include/weapon_rules.h`): the plan
  (`weapon_rule_trigger_t`), the original's plan for a record
  (`weaponRulesNativeTrigger`: a function that does not shoot fires once and
  spends nothing here; an automatic is `automatic` at the record's rates and
  spin with the burst its flags give, the 3-round one giving way while
  aiming; a shooting function with a 2-, 3- or 5-round flag is `burst`, 3
  ticks apart, 13 for the Shotgun; a shot takes a round unless the function
  has no clip or is the Laser's single shot), the settings table
  (`weaponRulesSettingAt`, which registration, conformance and the creator
  read), `weaponRulesApplySettings` (defaults from the record), and the
  cadence itself as plain functions: `weaponRulesCadenceStep` (the original
  `bgun0f09a3f8`, reading the plan), `weaponRulesRateAt`,
  `weaponRulesPayShots` and `weaponRulesTicksPerShot`. The program walk is
  in the table too (`weaponRulesWalk`, `weaponRulesWalkModule`): the gates,
  `set_state`, the first trigger reached planning the attack, `use_ammo`
  naming the rounds. The game's seam and pd-tests run that one walk.
- **The base programs** (`romextract_pdweapon.c`): `has_ammo`'s pass goes to
  the function's trigger node (`<mode>_attack`), written with every setting of
  the original's plan, then to `og.weapon.use_ammo` (rounds 1), which a
  function that spends nothing here has none of. So the base guns' quirks are
  visible settings: the AR34 is `automatic` 750 a minute with `burst` 3 and
  `aimed_burst` false; the Reaper `automatic` 1800 from `start_rate` 60 over
  88 ticks each way; the Cyclone's dump `burst` 50; the MagSec 4's secondary
  `burst` 3 rounds 3 ticks apart; the Shotgun's double blast `burst` 2 rounds
  13 ticks apart; the Laser's single shot has no `use_ammo`. The record no
  longer writes `burst_count` (the runtime still reads it from older archives
  and still ignores it); its flags keep the bits for the parity probe. Marker
  `embedded.v18`, cache kind `_heldtriggers`.
- **The seam** (`weapon_events.c`, `bondgun.c`): when the idle seam starts an
  attack the hand keeps its plan (the program's, or the original's for a
  function with no program); `bgun0f09a3f8` and `bgun0f09a6f8` read it
  (`weaponHandTrigger`) in place of the flags, the rates, the spin, the
  Shotgun's gap and the Laser's number. The parity probe now compares the
  plans too: `WEAPON.RULE: ... plan=... native_plan=...`, and a different
  plan counts in `rules.differ`.
- **Simulants and guards** fire the same programs: `weaponGetNumTicksPerShot`
  (`game_0b0fd0.c`, read by `bot.c` and `chraction.c`) is the rate of the
  automatic a press starts, walked for a ready hand with a loaded clip and
  every state false (`weaponProgramPressPlan`, cached until a program is
  registered or dropped); a base function whose answer differs logs
  `WEAPON.RULE differs: ticks_per_shot ...`. A simulant's single-shot burst
  in `bot.c` (2 or 3 rounds, 5 ticks apart, from the flags) is Multiplayer &
  Rules' file: `weaponProgramBurstRounds` is ready for it, and the request is
  in the hub.
- **Registration and conformance** refuse a trigger or `use_ammo` setting
  that is not a value of its kind ("og.weapon.burst needs count from 2 to
  50, got 1"), and a program node that takes a record node's id (the runner
  checks it now: "node id 'primary_action' is used twice").

**Not exact, on purpose:** a v1 record whose flags give an automatic both the
3-round and the 2-round burst keeps the 3-round burst while aiming (the
original kept 2). No base weapon has two burst flags.

**With a program, the trigger node's settings are the cadence.** A
`fire_cadence` tunable (`settings.json`) sets the record's rate, which is a
default: an automatic node that names its own `rate` keeps it. A mod that
wants a tunable rate names it as a variable (`"rate": "$fire_rate"`). The mods
that use `fire_cadence` today (the crystal pistol, the triangle fixtures) are
v1 graphs and unchanged.

Tests: `tests/test_mission_graph_runner.cpp` ("the trigger table plans each
base function's attack as the original does"; "the trigger's cadence is the
original's, for every base function", which runs a copy of the original
`bgun0f09a3f8` and `bgun0f09a6f8` beside the plan's steps over held, tapped
and released triggers, aiming or not, at 1 to 3 ticks a frame and four clip
loads, comparing every return and every hand field; "a trigger node's
settings default to the function's record and refuse what they cannot read";
"the base wiring starts the original's attack, for players and for
simulants"; "a held program's trigger settings are checked when its weapon
registers"), `tests/test_weapon_graph_editor_io.cpp`,
`tools/tests/test_projectile_program_conformance.py`.

## Slice 4a as written (Core Weapons, 2026-11-21)

The Shotgun, Farsight, Laser, spawn if-chain, Laptop, cloak and
Tranquilizer special cases are node settings. The shot joins the attack's
plan (slice 3): the walk that chooses the trigger also chooses the **shot
module**, the first one reached after it, with its settings; the native code
of each attack family runs it, reading the plan where it read a weapon
number. One shot module per family:

| Module | Family (record's function type) | Settings (default the record's) | Replaces |
|---|---|---|---|
| `og.weapon.hitscan` | shoot (single, automatic) | `pellets` (1 to 32; Shotgun 6), `through_walls` (bool; Farsight), `shield_damage` (factor; Farsight 10) | `handTickAttack`'s six Shotgun `shotCreate` calls; `prop.c` Farsight wall skip and kept distance; `chraction.c` Farsight shield x10 and a simulant's Farsight shot through walls |
| `og.weapon.beam` | shoot | `range` (world units; Laser stream 300) | `prop.c` laser-stream test (Laser secondary) |
| `og.weapon.launch` | shoot projectile | `item` (catalog id of the item fired, or `own`), `item_function` (`gun`, 0, 1 or 2, or left out) | `bgunCreateFiredProjectile`'s if-chain (rocket launcher and Slayer fire `base:rocket`, crossbow `base:bolt` with the gun's function, Devastator `base:grenaderound` with the gun's, Super Dragon `base:grenaderound` function 2) |
| `og.weapon.throw` | throw | `give_up_gun` (bool; Laptop gun) | `bgunCreateThrownProjectile`'s Laptop `bgunFreeWeapon` |
| `og.weapon.melee` | melee | none this slice | (the Tranquilizer's cost moves to `use_ammo`) |
| `og.weapon.special` | special | `action` (`detonate`, `boost`, `revert_boost`, `crouch`, `uplink`) | the record's `specialfunc` |
| `og.weapon.mode_toggle` | special | `mode` (`cloak`), `drain` (rounds a second; RCP120 24) | the RCP120 cloak toggle and its per-tick drain (`bondgun.c`, `0.4` a tick) |

`og.weapon.use_ammo` spends for every family: a throw and a special take 1
round when the function has a clip, the Tranquilizer's sedative strike 4
(a strike takes up to its rounds, emptying a shorter clip, as the original),
any other melee none. The base programs wire `has_ammo` → trigger →
`use_ammo` → shot. A shot module whose family is not the function's is
refused (the attack's state machine is still the record's family). Devices
have no shot module. Left native for later slices: wall-hit decals and spark
colours per weapon (feel, slice 7), throw arcs, the grenade's prime (slice
5), the Reaper's grind, the alternate-button cloak control and the
function-switch rules (input). Reload styles (magazine, one round at a time,
regrow) follow as slice 4b.

- **Where it runs:** `weaponRulesNativeShot` and `weaponRulesApplyShotSettings`
  (`weapon_rules.h`) plan the shot; the walk takes the first shot reached
  (a shot left out is the function's own). `bondgun.c`: the launcher's item
  (`bgunCreateFiredProjectile`), the Laptop gun's `bgunFreeWeapon`, the
  special's action, the throw's, special's and strike's spending
  (`weaponRulesSpendRounds`), the cloak's drain and clip. `prop.c`: the
  pellets in `handTickAttack`, the beam and the wall pass in
  `shotCalculateHitsSource` (`bgunHandPlan`). `chraction.c`: shield damage and a
  simulant's shot through walls read `weaponFunctionPlan` (the press plan, or
  the original's), so simulants, guards and players agree.
- **Extractor:** the base programs end `has_ammo` → trigger → `use_ammo` →
  `<mode>_shot`, every setting written; marker `embedded.v19`, cache kind
  `_heldshots`.
- **Parity probe:** `WEAPON.RULE` plans carry ` shot=...` with its settings.
- **Registration and conformance** refuse a shot that does not fit its
  function ("og.weapon.launch does not fit this function") and settings that
  are not values of their kinds.

Tests: `tests/test_mission_graph_runner.cpp` ("the shot table plans each base
function's shot as the original does"; "a shot node's settings default to
the function's own shot and refuse what they cannot read"; "the base
wiring's shots are the original's, for players and for simulants"; "a held
program's shot is checked against its function when its weapon
registers"), `tests/test_projectile_rules.cpp` (the launcher's item agrees
with the projectile rule's launched object for every base launcher),
`tools/tests/test_projectile_program_conformance.py`.

## Slice 4b as written (Core Weapons, 2026-11-21)

A reload's style is `og.weapon.reload`'s: `style` (`magazine`, `one_at_a_time`
or `regrow`) and, for a regrow, `regrow_time` (60 Hz ticks a round, 1 to
10000). The record's own is the default: a mod magazine whose reload is
`regrow` (T-MODDING-008) regrows at its `regrow_ticks60`, a clip whose ammo
carries `AMMOFLAG_INCREMENTALRELOAD` (the Shotgun's shells, the crossbow's
bolts) loads a round each time the animation loops, the rest load the
magazine. When the idle seam chooses a reload, the hand keeps its reload plan
(`weaponHandReload`), and `bgunTickIncReload` reads it where it read the ammo
flag and the mod magazine: a regrow on a base gun regrows with no seat visuals
and no sound. The base programs write each reload's style (marker
`embedded.v20`, cache kind `_heldreloads`); the parity probe's `WEAPON.RULE`
line names ` reload=... native_reload=...` for a reload decision, and a
different style counts in `rules.differ`. Registration and conformance refuse
a style they do not know.

Tests: `tests/test_mission_graph_runner.cpp` ("a reload's style is the
function's own unless its program names one"),
`tools/tests/test_projectile_program_conformance.py` (`test_reload_styles`).

## Slice 5 as written (Core Weapons, 2026-11-21)

Charge then release and Hold to cook, one charge module shared with the
crystal pistol. What the three behave like today:

- **Mauler secondary** (`fire.charge_release`, shoot single): the charge
  builds while the secondary is *selected*, not while the trigger is held
  (`bgunTickMaulerCharge`, every frame; the audit's recorded hazard): `matmot1`
  rises 0.05 a 60 Hz tick to 5 (five steps in 100 ticks), and each whole step
  takes a round while the clip holds 2 or more; on the primary it falls 0.005 a
  tick; a reload empties it. The shot carries it: `gsetPopulateFromCurrentPlayer`
  sets `unk063a` to `matmot1 * 10` and `gsetGetDamage` multiplies the damage by
  `unk063a / 3 + 1`; the shot sound's pitch (`bondgun.c` near 2474) and the
  gun's red tint (near 12991) follow it; firing resets it (near 2746).
  Simulants charge the same way in `bot.c` (Multiplayer & Rules' file:
  `maulercharge`).
- **Crystal pistol secondary** (mod, `has_charge`, T-MODDING-008): a
  committed charge launch: a press with a loaded clip starts it
  (`bgunModTickIncAttackCharge`), the hand stays busy for
  `charge_time_ticks60`, then `bgunModChargeRelease` launches up to its round
  count of crystals in a fan (`launch_speed`, `launch_fan_deg`, its parts);
  simulants through `bgunModChargeForChr`.
- **Grenade primary** (throw): the prime timer runs while the trigger is held
  in the throw (`primetimer60`); held past the function's activation time the
  grenade is forced out and goes off in the hand
  (`HANDSTATEMINOR_ATTACK_THROW_GRENADEWAIT`); a thrown grenade's fuse is
  shortened by the prime (`timer240 -= primetimer60 * 4`).

**What slice 5 does.** Both are trigger modules (the
audit's Trigger group: Fire once, Burst, Automatic, Charge then release, Hold
to cook), so they live in the attack's plan beside the other triggers:

| Module | Settings (the original's) | Replaces |
|---|---|---|
| `og.weapon.charge` | `start` (`while_selected`: the Mauler; `on_press`: the crystal pistol), `time` (ticks empty to full; Mauler 100, crystal pistol its `charge_time_ticks60`), `steps` (Mauler 5), `ammo_per_step` (Mauler 1) and `min_clip` (Mauler 2), `fall_time` (ticks a step falls back while the other function is held; Mauler 200), `release` (`on_press`: the trigger fires the charged shot; `on_full`: it goes by itself), `damage_per_step` (Mauler 10/3) | `bgunTickMaulerCharge`'s weapon test, secondary test, clip 0, the 0.05 and 0.005 a tick, the five steps; `gsetPopulateFromCurrentPlayer` and `gsetGetDamage`'s Mauler tests; the shot sound's pitch and the gun's tint; the reset after a shot; the crystal pistol's `has_charge` dispatch |
| `og.weapon.cook` | `fuse` (ticks the throw may be held; the record's activation time) | the grenade primary's forced throw and the fuse shortened by the prime |

The charge level stays the hand's (`matmot1`, in steps) and the damage reads
it as before (`unk063a` is tenths of a step): with the Mauler's 10/3 the
multiplier is the original's own expression, any other `damage_per_step` is
`1 + level * damage_per_step`. A charge trigger's attack is one round (as
Fire once). Simulants' Mauler charge stays in `bot.c` (Multiplayer & Rules'
file), noted like the burst.

- **Where it runs:** `weaponWeaponCharge` (`weapon_events.c`) finds a
  weapon's while-selected charge; `bgunTickMaulerCharge` runs it from the
  plan (steps over time, ammo per step from the clip of its function, the
  fall while the other function is held), and the shot sound's pitch, the
  reset after a shot, the gun's tint, the charge the shot carries
  (`gsetPopulateFromCurrentPlayer`) and the damage (`gsetGetDamage`) follow
  the weapon's charge where they followed the Mauler's number. The committed
  charge (`bgunTickIncAttack`) is the plan's `on_press` charge, for its
  `time` (`bgunModTickIncAttackCharge`); the crystal's glow still ramps over
  the record's time. The grenade's forced throw and its shortened fuse read
  the plan's `og.weapon.cook`.
- **Extractor:** the Mauler's secondary is written as `og.weapon.charge`
  with every setting (its damage with nine digits, so 10/3 reads back the
  same), the grenade's primary as `og.weapon.cook` with its activation time;
  marker `embedded.v21`, cache kind `_heldcharges`.
- **Not exact, on purpose:** none for base weapons; a mod's
  `damage_per_step` other than 10/3 multiplies as `1 + level x
  damage_per_step` in float.

Tests: `tests/test_mission_graph_runner.cpp` ("the charge and cook triggers
are the Mauler's, the crystal pistol's and the grenade's": the native plans,
the settings, and the original's charge arithmetic beside the plan's over held
and switched functions at 1 to 3 ticks a frame),
`tools/tests/test_projectile_program_conformance.py` (`test_charge_and_cook`).

### PR 31 review (slices 2 to 5), two rounds

Round 1 (45204a1ad): a module's settings read through "$name" variables each
get a buffer of their own (a ring of 12, each as long as a catalog id), so an
item is no longer overwritten by its item_function nor a long id cut; a launch
whose item is not the rocket frees the held rocket and fires its own;
`item_function` applies to every launched object, the gun's own included; a
player's shield damage is the firing hand's plan; idle admission counts the
plan's rounds, so a clip below a shot's reloads; the Weapons creator keeps a
program node's settings and an edge's ports and writes v2 when the graph has a
program.

Round 2:

- **Only the charges the runtime runs.** `start: while_selected` with
  `release: on_press` (the Mauler; on any weapon) and `start: on_press` with
  `release: on_full` (a custom weapon's committed launch, the crystal pistol).
  Registration (`heldProgramParamVisit`, `weaponRulesChargeUnsupported`) and
  conformance refuse every other pair, so no setting is accepted and then
  ignored: a while-selected charge that would go by itself, a pressed charge
  that would wait for a press, and a base weapon's pressed charge (it has no
  committed launch to make). A start left out is the function's (on_press for a
  custom weapon's committed record), a release left out its start's. Other
  shapes (hold to charge and let go, a selected charge that fires itself) are
  new behaviour, not slice 5's: they wait for Mike.
- **The committed charge is its plan's alone.** The player's attack
  (`bgunTickIncAttack`: a custom weapon whose attack plan is an on_press
  charge), bots (`bgunModBotChargeShot`, the function choice and the fire
  bookkeeping) and the gun's look (`bgunModChargeFunction`; the glow ramps over
  the charge's own time) ask `bgunModFunctionCharges`, the function's plan, for
  its time; a node needs no `charge_time_ticks60` besides, which is only a v1
  record's native plan. Bots no longer wait the record's time when the node
  says another.
- **The level has its own field.** `hand->chargelevel` (types.h's unused
  `fspare3`, so the layout is unchanged), not `matmot1`, which the Shotgun's
  flash, the Laser's stream, the Reaper's blades and a cloak's drain use for
  their own effects; a weapon change clears it with them.
- **Every step is paid.** Each whole step a frame crosses takes
  `ammo_per_step` while the clip holds `min_clip` and the step's whole cost
  (round 3: `weaponRulesChargeStepNeeds`, so two rounds never buy a
  three-round step), and the charge stops at the last step paid (the Mauler
  crosses at most one a frame and needs 2 for a 1-round step, so its numbers
  are the original's).
- **The cooked fuse is the plan's.** The thrown grenade's timer is the plan's
  `fuse` less the prime (it was the record's activation time less the prime),
  and the wait after it goes off in the hand follows that fuse. Round 3: the
  240 Hz timer is an s16, so the fuse is worked out wide and clamped
  (`bgunGraphClampTicks240`); a thrown object's own activation time saturates
  the same way.
- **The beam's reach is its range.** `shotCalculateHitsSource`'s hit and aim
  checks use the plan's `range` where they used 300.
- **Round 4.** A charge has at most 25 steps (`WEAPON_RULE_MAX_CHARGE_STEPS`):
  the shot carries the level as tenths of a step in a byte
  (`gset->unk063a`), and 26 steps would not fit. A graph-authored cloak
  (`og.weapon.mode_toggle`'s cloak on any weapon) now cloaks the player
  (`chrTickCloak` asks `weaponCloakToggle` where it tested the RCP120) and goes
  off with a dropped gun; netplay's drop handler (`netmsg.c`, another lane's
  file) still tests the RCP120 and is noted for its owner. Not changed: a
  pellet shot delivers one shot's pellets a frame however many rounds the
  frame paid, because the original delivers one shot a frame for every
  automatic (`shotCreate`'s count only gates the shot): a decision for Mike if
  fire should not depend on the frame rate.
- **Round 5 (after PR 31 merged; fixed in PR 40).** A special carries out only
  a special's actions (`weaponRulesSpecialActionRuns`: detonate, boost,
  revert_boost, crouch, uplink, every base special's): shoot, launch, throw and
  melee read their own families' data, so a special that named one launched
  nothing or read past its function. A cloak needs rounds: a function with no
  ammunition is no cloak (`weaponCloakToggle` no longer takes its slot -1 for
  slot 0), registration and conformance refuse `og.weapon.mode_toggle` on one,
  and the drain never reads the reserve of ammunition type -1.
- **Reloads follow the program.** A manual reload (no reload planned by the
  idle seam: Reload pressed with rounds in the clip) is the reload the
  function's program plans for an empty clip (`weaponProgramReloadWalk`), so
  an authored `style` holds there too; a clip the program regrows needs no
  reserve: the native ammunition check (`bgun0f098ca0`), the gates' reserve
  and so the idle seam's clip_empty admit it (`weaponProgramRegrows`), as they
  admit a regrowing mod magazine. Simulants' self-loading check (`botact.c`)
  still reads only mod ammo records: Multiplayer & Rules' (a base weapon's
  regrow for bots needs the regrow's timing, not unlimited rounds).

Tests: `tests/test_mission_graph_runner.cpp` ("a charge runs as its node says,
or its weapon is refused": the pairs, the defaults, registration's refusals,
the seams; "the charge and cook triggers ...": the per-step paying beside the
Mauler's arithmetic and a five-steps-a-frame charge),
`tests/test_weapon_graph_editor_io.cpp` (validation by asset id),
`tools/tests/test_projectile_program_conformance.py` (`test_charge_and_cook`).

## Slice 6 plan: the target filter (Core Weapons, 2026-11-22, before code)

Source: the audit's "A rocket that tracks vehicles but not players" and its
eight pickers, read again at dev b66065f8 (every picker still exists; only
their lines moved).

**Mike's example.** A launcher whose held graph locks one target through the
filter "Enemy vehicles" (kinds: vehicle, any or occupied only; relation
enemy; alive; visible; line of sight; up to the launcher's lock range) while
aiming, and hands the lock to its rocket; the rocket homes on it through the
same filter, re-checked in flight, and flies straight when it fails. The
rider is a separate player prop, so the bike is locked, never the rider;
under "any" a bike its rider left stays a target, under "occupied only" the
rocket drops it.

**The pickers today** (file:line at b66065f8):

| # | Picker | What it filters on |
|---|---|---|
| 1 | Auto-aim (`autoaimTick` prop.c:3245, `chrCalculateAutoAim` chr.c:5889, `func0f06438c` prop.c:3053) | on-screen chr or other player, not a friend, armed or force-flagged, not dying, drugged, dead or no-autoaim, cloaked only with IR, coop partners out, central box, line of sight; no range, no objects |
| 2 | Farsight auto-target (`farsightChooseTarget` prop.c:3191) | every chr: not unexplodable, friend, dying, drugged or cloaked; best facing; no range or line of sight |
| 3 | What you look at (`lvRender` lv.c:1912, `propFindAimingAt`) | chrs and players unless cloaked without IR; objects only with react-to-sight, not destroyed |
| 4 | Lock-on (`sightCanTargetProp` sight.c:135, `sightTick` sight.c:222, `lvUpdateTrackedProp` lv.c:1141) | what you look at, the rocket launcher 1 slot, follow-lock 4; drops self, dead chrs, doors, eyespies |
| 5 | Homing (`bgunCreateFiredProjectile` bondgun.c:6446, `projectileTick` propobj.c:8724) | the lock at fire time; never re-checked |
| 6 | The bot Slayer (`rocketTickFbw` propobj.c:7955, 8096) | MP enemy of the owner, alive, not invisible, within 250 |
| 7 | Proximity (`weaponTick` propobj.c:6095, `coordTriggerProxiesFor` 21817, `bgunModProxyDecision` bondgun_mod.c:1826) | the player of the tick within 250, or a moving object; mod mines: owner, team, hostile only |
| 8 | Sentries and autoguns (`autogunTick` propobj.c:10869, `laptopDeploy` 22056) | team mask, chrs and players, range, 140-degree wake cone, yaw limits, line of sight |

**The plan, in the slices' pattern:**

- **6a. One filter, native for every picker.** A header-only table
  (`port/include/target_filter.h`, linked into pd-tests): `target_filter_t`
  (kinds: player, simulant, guard, vehicle with any or occupied only, sentry,
  object with react-to-sight; relation to the owner: enemy, friend, anyone,
  owner only, not owner; alive; visibility: visible, cloaked, cloaked with
  IR; line of sight; range; cone) and `targetFilterAccepts` over a
  candidate's facts (its kind, its relation, alive, cloaked, the viewer's IR,
  line of sight, distance, angle, occupied). Each picker keeps its own search
  (the screen box, the wake cone's yaw limits, the facing score) and asks
  the filter where it tested kinds, teams and states inline, with the
  original's filter as its native one; pd-tests compare each native filter
  with a copy of the picker's inline conditions over every combination of
  facts.
- **6b. The filter on programs.** `og.weapon.lock_on` (held: lock type,
  locks, filter, while aiming) replaces the rocket launcher's and the
  CMP150's lock by weapon number; `og.projectile.home` takes `filter` and
  `lost` (fly straight, keep the last point, re-target) and re-checks while
  it flies (the record's `target_filter` and `retarget_policy`, parsed and
  read nowhere today, become its settings); the proximity event carries the
  candidate prop (`projectile_rule_context_t`) so a program's filter can
  read it. The module word widens to 64 bits first (it is full).
- **6c. Vehicles lockable.** The lock accepts a vehicle when its filter's
  kinds have one (a hoverbike has no react-to-sight flag), the seat read
  through `vehicleGraphRuntimeSeatOccupant`; Mike's example becomes a sample
  weapon and a smoke for Integration.
- **An empty vehicle's relation.** A vehicle's relation is its rider's; an
  empty one is nobody's friend, so it passes "enemy" and "anyone" and fails
  "friend" and "owner only" (Mike's "the bike stays a target under any").

**6a as written so far (2026-11-22, `week-pd/core-weapons-w6`):** the filter
(`port/include/target_filter.h`: kinds, things that must react to sight, the
CI training targets, occupied vehicles, relation, alive, cloaks) and its
first two pickers. `lvRender`'s look and `sightCanTargetProp`'s lock read a
prop's facts (`sightTargetFacts`, sight.c) and ask their native filters; the
rocket launcher's "locks whatever is looked at" is the lock table's answer
for its weapon number. pd-tests compare both with copies of the inline tests
as they stood, over 160 combinations of facts (the comparison caught a
sentry that does not react to sight, which the first draft let through).
Then the Farsight (`farsightChooseTarget`): its auto-target asks the
Farsight's native filter (an enemy character or another player, alive and
awake, not cloaked even through the IR scanner, not unexplodable; every other
gun has none, so the Farsight's number is the table's), with the facts
gaining the relation (owner, friend through `chrCompareTeams`, else enemy),
alive, drugged and unexplodable; compared over 576 combinations. Homing's
native filter takes anything (it never re-checks), so it waits for 6b.
Then the bot Slayer (`rocketTickFbw`): what sets off a bot's rocket asks its
native filter (an enemy of the bot, alive, not invisible to bots) over the
facts as the bot reads them (`rocketFbwTargetFacts`: `COMPARE_ENEMIES`,
`chrIsDead`, `botIsTargetInvisible` as the cloak; the distance and the
re-route stay the picker's). Each picker builds the facts as it reads them,
and the filter is one. Then sentries (`autogunTick`, an autogun with a
team mask): its search still picks the team (the mask, the owner left out in
a match, a model, an enabled prop), and its take asks the native filter (a
character, alive, neither hidden nor cloaked) over `autogunTargetFacts`; the
re-checks of a held target and the aim distance are next with the laptop's
filters. Then auto-aim (`autoaimTick`'s standard auto-aim): its candidates ask
the native filter (a character or another player who is not a friend and is
armed: holding or carrying a gun, or flagged for auto-aim), and
`chrCalculateAutoAim` keeps testing the state, the cloak and the screen (its
state and cloak tests join the filter with the CMP150's lock-on branch).
Proximity stays as it is in 6a: a base mine already decides through its
program's gates (`trigger_close`, `trigger_character`, projectile slice 4),
and a mod mine's filters (`bgunModProxyDecision`) are its record's (owner
excluded or only, enemies only, objects ignored), not weapon numbers, with
three answers (trigger, ignore, or the rule's) and one quirk to keep (with
"enemies only" and no owner filter, the owner sets off its own mine). They
move with 6b, when the proximity event carries its candidate and a program
chooses its filter. So 6a ends with six of the eight on the filter: look,
lock, the Farsight, the bot Slayer, sentries and auto-aim; homing and
proximity are 6b's.

**6b, as planned now.** No new module bits are needed: the filter is
settings. `og.projectile.home` takes the filter's settings (`kinds`,
`vehicles` any or occupied, `relation`, `alive`, `cloaked`, `line_of_sight`,
`range`) and `lost` (fly straight, as Mike's example; the original never
re-checks, so a base program's home writes none and keeps that), parsed by
one function in `target_filter.h` that the held lock uses too; a held
`og.weapon.lock_on` (weapon module bit 16: `style` rocket or follow, `locks`,
the filter) replaces the aim settings' track type and the rocket
launcher's number; the launch hands the lock to its object as today.

**6b part 1 as written (2026-11-22, `week-pd/core-weapons-w6b`):** the filter
as settings (`target_filter.h`: `kinds` as names with commas, `vehicles`,
`relation`, `alive`, `cloaked`; parsed by `targetFilterFromSettings`, written
by `targetFilterSettingsText`, a setting left out takes the "anything"
filter's). `og.projectile.home` takes them and `lost: straight`
(`projectile_flight_t.home_filter`, `home_filtered`), and `projectileTick`
re-checks a filtered home's target each tick it homes, with the facts as the
rocket's owner reads them (`sightTargetFactsFor`, the IR scanner not
counted for a rocket); a target that fails is dropped and the rocket flies
straight. The original's home names no filter and writes none, so base
rockets and their archives are unchanged (no marker bump). Registration and
conformance refuse a malformed setting with what it takes. Mike's "Enemy
vehicles" passes an occupied enemy bike and refuses its rider; vehicles
cannot be locked until 6c.

**6b part 2 as written (2026-11-22, `week-pd/core-weapons-w6b`): the held
lock.** A new held event, `event.weapon.aiming` (kind 41, numbered after the
flight event; `weaponRulesEventAt` and `weaponRulesEventIndex` keep the six
weapon events in one order), walked for the right hand each tick the sight is
on (`weaponSightLock`, from `sightTick`), applies one module,
`og.weapon.lock_on` (weapon module bit 16): `locks` (1 to 4, the targets the
sight holds; with more than one each box shows its number), `aim_follows`
(the aim follows the held targets, the CMP150 secondary's follow lock-on) and
the target filter's settings, which gain `sight_targets_only` (a vehicle,
sentry or object only if the stage marks it for the sight; the CMP150's).
Gates work there too (a scope that locks only while a state is on). The hand
keeps the lock while it holds the function, for the sight between walks and
for auto-aim (`autoaimTick` reads `aim_follows` instead of the CMP150's
number); a program that handles the event and reaches no lock has none (the
box follows the aim). A function with no program, or whose program does not
handle the event, has the original's (`weaponRulesNativeLock`): its weapon's
aim settings' track type (the rocket launcher's one target, the CMP150's
four), with the rocket launcher's "whatever the player looks at" and the
CMP150 secondary's follow kept by number in the table, so a mod weapon whose
descriptor names `rocket_launcher` keeps today's filter. The extractor writes
the rocket launcher's and the CMP150's lock into their base programs with
every setting (`embedded.v23`, cache kind `_heldlocks`: installs re-extract
base weapons once), and the parity probe compares each walked base lock with
the original's (`rules.compared`, `rules.differ`; `WEAPON.EVENTS` ends with
`aiming=N`). `targetFilterSame` now ignores the CI training targets when a
filter's things need not react (they cannot matter then). Bots have no sight:
a simulant's rocket homes on its own target, checked in flight by the home's
filter (part 1).

**6b part 3 as written (2026-11-22, `week-pd/core-weapons-w6b`): what sets a
mine off.** A filter on what came near needs no new gate (the projectile word
is full, and widening it is D-057's option B): it is the arm's setting,
because the arm already chooses what a mine listens for.
`og.projectile.arm_proximity` takes the target filter's settings
(`projectile_arm_t`, `projectileRulesArmSettings`; kept with the object's arm
in its projectile record, `projectileArmProximityFilter`), and the proximity
sweep (`coordTriggerProxiesFor`) now names the candidate prop (a character,
a moving object, an eyespy or a firing-range target: `coordTriggerProxiesBy`)
and judges it as the mine's owner sees it (`sightTargetFactsFor`) before the
proximity rule's gates; a candidate the sweep cannot name does not set off a
filtered mine. The filter's relations gain `not_owner` and `not_friend`. A
mod mine's record filters (owner excluded or only, hostile only, objects
ignored) are its native filter (`targetFilterModMine`, over the facts as the
record reads them: the owner by player number, a friend by
`COMPARE_ENEMIES`), compared with a copy of the record's inline tests over
every combination; the record's quirk stays (hostile only with no owner
filter is `not_friend`: the owner still sets off its own mine). When a mod
mine's program names an arm filter it replaces the record's filters within
the record's radius. Base programs name none (no re-extract). Slice 6b is
done; 6c makes vehicles lockable.

**6c part 1 as written (2026-11-22, `week-pd/core-weapons-w6b`): vehicles
lockable.** A vehicle's facts come from its rider (`sightVehicleRider`: the
player whose hoverbike it is in bike mode, the driver before the gunner, the
gunner's seat read through `vehicleGraphRuntimeSeatOfPlayer`): occupied
while someone rides it (or it is mounted), and its rider's friend or enemy;
an empty one is nobody's. A thing is alive until it is destroyed. A lock
seeks vehicles when it holds targets and names its kinds, vehicles among
them (`weaponRulesLockSeeksVehicles`); for such a lock the look offers a
vehicle its filter takes, though no vehicle reacts to sight, and sees a
rider the lock does not take as the vehicle they ride (`sightRiderVehicle`),
so Mike's "Enemy vehicles" locks the bike when the aim is on its rider. A
lock that takes every kind (the rocket launcher's, written `kinds: all`)
takes what the look offers, as the original's did, so no base weapon locks a
vehicle it could not before.

**6c part 2 as written (2026-11-22): the lock's evidence, and the sample.**
Each target the sight takes is counted (`WEAPON.EVENTS ... locks=N`) and the
stage's first sixteen are logged: `SIGHT.LOCK: player=0 weapon=... func=...
slot=1 of 1 kind=vehicle occupied=1 relation=enemy`. The sample weapon is
Authored Mods' to package (recipes, `dev-mods.json` and staging are theirs,
and a sample made from a base weapon may not carry ROM data), to this spec:

- A recipe from the `tri_weapon` template, as `maian_crystal_pistol` is,
  with descriptor `track_type = default`.
- Primary (`pd.weapon_graph.v2`): its record (`spawn.fired_projectile`,
  `shoot_projectile`, its own rocket) and the program
  `event.weapon.trigger_pressed -> og.weapon.fire_once -> og.weapon.launch
  {item: own}` and `event.weapon.aiming -> og.weapon.lock_on {locks: 1,
  kinds: vehicle, vehicles: occupied, relation: enemy, alive: true,
  cloaked: never}` (Mike's "Enemy vehicles").
- Secondary: the same with `vehicles: any`, launching
  `{item: base:homingrocket}`: the base homing rocket never re-checks, so a
  bike its rider left stays its target.
- Its rocket (the primary projectile archive, `pd.projectile_graph.v2`):
  `event.projectile.in_flight -> og.projectile.thrust, og.projectile.home
  {kinds: vehicle, vehicles: occupied, relation: enemy, alive: true,
  cloaked: never, lost: straight}, og.projectile.trail`, with the contact
  and armed rules a rocket needs (the base homing rocket's wiring is the
  reference).

## Slice 7 plan: feel (Core Weapons, 2026-11-22, before code)

What a gun's shots feel like, from the audit's "Recoil and feel" and
"Presentation" rows and a survey of the code at `week-pd/core-weapons-w6b`.
The audit's done-when: mod guns feel like base guns.

| Feel | Where it is decided and runs | What it reads | A mod gun today |
|---|---|---|---|
| Recoil kick and return | `bgun0f09aba4`, each frame of the attack's second shoot state (once per attack for an automatic, after its last round) | the held record's `recoil_anim_unk24..27`, recovery time, `recoildist`, `recoilangle`, else the function's | its graph's (the crystal pistol's 4/6/3/0) |
| Slide | `bgunStartSlide` each shot, `bgunUpdateSlide` each frame (models with `MODELPART_GUN_SLIDE`) | the function's `slidemax` only; the held one is never read | the adapter copies its graph's |
| Jitter | `bgun0f0a5550`, each firing frame | the function's `recoilsettings` (x, y, z ranges), times the Reaper's spin multiplier (`vibrationstart`/`vibrationmax`, element 0 only) | none (`recoilsettings` NULL) |
| Casings | `bgunCreateFx` each firing frame; the casing model loaded at equip | the ammo's `casingeject` | none (`CASING_NONE`) |
| Muzzle flash | `bgun0f09a6f8` each shot | `FUNCFLAG_NOMUZZLEFLASH` (held flags first) | from its flags |
| Shot sound | each firing tick | the held shot sound and duration, else the function's | from its graph |
| Fire animation | each attack | the function's `fire_animation` | none (never set; the adapter's weapon lacks the animation flag) |

The recoil's four frame counts: `unk24` frames of kick, `unk25` of return, and
a press after `unk26` frames cuts the recoil short with an `unk27`-frame
return (negative: no cut); then `recoverytime60` frames of recovery before the
attack ends. They also set a semi-automatic's fire rate, and simulants use
kick plus return as their shot interval (`botactGetShootInterval60`).

**The plan, in the slices' pattern:**

- **7a. `og.weapon.recoil`** (weapon module bit 17), reached on the attack's
  walk with its shot: `kick`, `return`, `recovery`, `cut_after`,
  `cut_return` (frames), `distance`, `angle`, `slide`, `shake_x`, `shake_y`,
  `shake_z`, `spin_shake` (the shake's multiplier at full spin; the Reaper's).
  A setting left out is the function's own, so a program without the node
  feels as today; base programs write every one (`embedded.v24`). The plan
  carries them, and the recoil, the slide and the jitter read the hand's
  plan. The held record gains `recoilsettings` and the spin shake (base
  archives already carry both). Mod guns get jitter.
- **7b. `og.weapon.casing`** (`type`: none, standard, reaper, rifle,
  shotgun), **`og.weapon.muzzle_flash`** (`on`), **`og.weapon.shot_sound`**
  (`sound`, `duration`): the plan carries them, and the casing model loads
  from the press plan when the gun is equipped. Mod guns get casings.
- **7c. `og.weapon.play_animation`**: the fire animation by name. A mod gun
  plays one only on a model that animates (the adapter sets the animation
  flag when its program names one); to be checked against the generated
  models' static matrices before it is built.
- Simulants: their shot interval reads the press plan's kick and return
  (`botact.c`, Multiplayer & Rules' file: an accessor and a note to them).
- Weapon-number cases stay, as the rule says (guards go last): the Shotgun's
  hold, the Reaper's eject parts and velocities, the DY357's casings,
  tracers, smoke, the Falcon 2's laser sight.

**7a as written (2026-11-22, `week-pd/core-weapons-w7`):**
`og.weapon.recoil` (weapon module bit 17, the first feel module; the idle
rule events apply the feel modules with the shots) takes `kick`, `return`,
`recovery`, `cut_after`, `cut_return` (frames, 0 to 127), `refire_early`,
`distance`, `slide` (0 to 1000), `angle` (0 to 360), `shake_x`, `shake_y`,
`shake_z`, `spin_shake_from`, `spin_shake_to` (0 to 100). The attack's plan
carries the recoil; the original's comes from the record
(`weaponRulesNativeRecoil`): `unk26` at or below 0 is no cut, `unk27` below 0
no early refire, read back as the seam read them
(`weaponRulesRecoilCutAfter`, `weaponRulesRecoilCutReturn`). The seams read
the hand's plan: `bgun0f09aba4` (kick, return, cut, recovery, distance,
angle), `bgunUpdateSlide`, the jitter and the spin multiplier. The held
record now reads the jitter (`recoilsettings`) and the Reaper's spin
(`vibration_start`, `vibration_max`), which base archives always carried, so
a mod gun's graph gives it jitter. Base programs write every recoil setting
after their shot (`embedded.v24`, cache `_heldfeel` since 7b); the parity probe
compares the recoil as part of the plan, and the first `WEAPON.RULE` line
ends with it (` recoil=6/18/0 refire=yes cut=2/8 ...`). Registration and
conformance refuse a recoil on a function that does not shoot, and
malformed settings. A number setting from 0 now reads `0.0` too, the
charge's `damage_per_step` included. Simulants' shot interval has an
accessor for Multiplayer & Rules' `botact.c` (`weaponProgramRecoilFrames`:
the press plan's kick plus return, the original's with no program).

**7b as written (2026-11-22, `week-pd/core-weapons-w7`):** `og.weapon.casing`
(bit 18, `type`: none, standard, reaper, rifle, shotgun) and
`og.weapon.muzzle_flash` (bit 19, `on`). The attack's plan carries both, the
casing as its `CASING_*` value plus one so a zeroed plan ejects none; the
original's casing is the function's ammo's `casingeject`, its flash on unless
the function is flagged `FUNCFLAG_NOMUZZLEFLASH`. The per-shot hub reads the
plan's flash; the casing spawn (`casingCreateForHand`) reads the plan's
casing, and a function with no clip ejects one too when its program names
one; when the gun is equipped it loads the model of the casing its
function's press plan names (`weaponFunctionCasing`), so a mod gun with a
casing node has its casing's model ready. The profile executor's weapons
keep their own casing path. Base programs write both after the recoil
(still `embedded.v24`: nothing was extracted at v24 before this). So a mod
gun gets casings from its program.

**7b part 2 as written (2026-11-22, `week-pd/core-weapons-w7`):**
`og.weapon.shot_sound` (bit 20, a feel module: `sound`, none or an audio
catalog id such as `base:sfx_fire_shotgun`; `duration`, the frames a fire
slot keeps it, 0 to 255). The attack's plan carries both; the original's are
the record's, which reads them as `gsetGetSingleShootSound` and
`gsetGetFireslotDuration` do (the held graph's `shoot_sound_catalog_id` and
`duration_ticks60` first, else the shooting function's `shootsound` and
`duration60`). A walk resolves the id through the audio catalog
(`walk.sound_number`, `weaponSoundNumber` at every walk site); an id that
does not resolve keeps the function's own, and registration refuses one that
does not resolve when its weapon registers (compile-time validation checks
only its form, as for the record's audio references). A player's shot plays
the attack's plan's sound and keeps its fire slot for its frames
(`bgunPlanShotSound`, with the plan the shot reads for its flash and its
rounds; PAL still converts 4 frames and up, now in `gsetFireslotTicks`); a simulant's and a guard's shots read their function's
press plan through the original's two accessors
(`weaponProgramShotSound`, `weaponProgramShotDuration`), so a program's
sound reaches every shooter. The profile executor's weapons keep their
accessors. Base programs write the node after the flash with the same
catalog id as the record (still `embedded.v24`, cache `_heldfeel`), and the
first `WEAPON.RULE` line ends with ` sound=N slot=N`.

**7c as written (2026-11-22, `week-pd/core-weapons-w7`):**
`og.weapon.play_animation` (bit 21, `animation`: none or an animation
catalog id such as `base:invanim_falcon2_shoot`). It is an attack's, not a
shot's: it fits a shooting, throwing or melee function (the three attack
ticks that play a fire animation), so it sits beside the feel modules in
the idle events' presentation set (`WEAPON_MODULE_PRESENTATION`) rather
than in the feel set. The attack's plan carries the gun commands; the
original's is the function's `fire_animation`. A walk resolves the id
through the loader pool (`walk.animation_ref`, `weaponAnimationRef`, at
every walk site); one that does not resolve keeps the function's own, and
registration checks only its form (animation references are not checked
anywhere else either). The shot, the throw and the strike start the plan's
animation and wait on that same one (`bgunPlanFireAnimation`). The model
check from the plan: a generated mesh (a mod gun's `.pdmesh` with no
`skeleton_symbol`) has no skeleton, and its root node is part 0, so a base
animation would move the whole gun by that animation's first channel. An
animation the function does not have therefore plays only on a held model
with a skeleton; otherwise the attack goes on as one with no animation and
`BONDGUN.ANIM: weapon=... plays no fire animation: its held model has no
skeleton` is logged (first 8). A mod gun animates once its mesh names a
skeleton its animation was made for. The dry fire's weapon-number list
keeps the function's own animation. Base programs write the node last in
every attack's chain (after the sound for a shot, after the throw or the
strike otherwise) with the record's catalog id (still `embedded.v24`, cache
`_heldfeel`), and the first `WEAPON.RULE` line ends with
` animation=<name>`.

## Slice 8 plan: value nodes and the creator (Core Weapons, 2026-11-22, before code)

The audit's slice 8: "Value nodes and the creator: palette shows designer
nodes with named inputs, writes v2", done when "a modder builds both
examples in-game with no raw JSON" (the pistol that fires like a machine
gun, and the rocket that tracks vehicles but not players). Mike's rule for
the vocabulary: base nodes more abstract than "int + int", while still
allowing those.

**What the creator has today** (Modding Hub, Weapons tool,
`pdgui_menu_moddinghub.cpp` and `pdgui_weapon_graph_node_editor.cpp`): a
node canvas (imgui-node-editor) with a palette, an inspector and templates
(open any weapon, a base one included, and save a mod in the clean layout,
T-MODINFRASTRUCTURE-002). It already round-trips v2 programs: a program
node's settings ride in `extra`, execution edges keep their ports, and the
writer (`weapon_graph_editor_io.cpp`) names `pd.weapon_graph.v2` when a
program node is present. What it lacks: the palette lists only the v1 record
modules (`fire.hitscan` with record params), the inspector edits only record
params (a program node's settings are not editable outside raw JSON), and a
weapon's projectile is picked by catalog id, never edited.

- **8a. Designer palette and named inputs.** The palette gains the program
  nodes, generated from the rule tables (weapon events, gates, modules;
  `weapon_rules.h`), with designer labels ("On trigger pressed", "Has ammo",
  "Automatic", "Hitscan shot", "Recoil", ...). The inspector shows a program
  node's settings as named, typed inputs from `weaponRulesSettingAt` (whole
  numbers within their range, rates, numbers, switches, the named choices:
  casing types, reload styles, charge start and release, special actions,
  the cloak; items, sounds and animations as catalog pickers) and the
  target filter's settings for the lock. A setting left out shows the
  function's own value and stays out until it is edited; a Reset puts it
  back. Registration's own refusal text shows under the node. Done when the
  machine-gun pistol is built from the Falcon 2 template by swapping Fire
  once for Automatic (rate 600, spin-up 0) and saved, with no raw JSON.
- **8b. The weapon's projectile in the creator.** The rocket example needs
  the rocket's own program (its home filter, slice 6b). The Weapons tool
  opens the weapon's projectile dependency (a base one as a template) with
  the same canvas, a palette from `projectile_rules.h` and named inputs, and
  saves it in the mod beside the weapon. Done when the vehicle rocket is
  built: the lock (rocket style, one lock, Enemy vehicles), the launch, and
  the rocket's home with the same filter.
- **8c. Value nodes.** A setting read at a seam can take a value input from
  value nodes, evaluated each shot from the hand's live state, so the
  audit's "wilder the longer you hold it" spread is one wire. Designer value
  nodes: Per shot (start, step, limit: over the shots of the attack), With
  spin (from, to: over the automatic's spin), With charge (from, to),
  Random between (from, to; the game's seeded random, so replays and peers
  agree); and the plain ones (add, multiply, min, max, clamp). First
  settings that take an input: the hitscan's spread and damage, the
  automatic's rate, the recoil's kick. The plan keeps a compiled expression
  per wired setting; the seam evaluates it; registration and conformance
  check the data edges; the creator draws data pins. Base programs wire
  none, so parity is unchanged.

Order: 8a, 8c, 8b. Files: the Weapons tool's part of
`pdgui_menu_moddinghub.cpp` (shared with other tools: claimed in the
Workbench before editing), the node editor, the editor IO, the rule
headers, the runtime seams for 8c.

**8a as written (2026-11-22, `week-pd/core-weapons-w8`):** the designer
list lives in `weapon_graph_editor_io.cpp` (`weaponGraphDesignerNodeAt`,
`weaponGraphDesignerNodeFor`; a test holds it to exactly the rule tables'
events, gates and modules), with the node ports
(`weaponGraphNodeTakesExec`, `weaponGraphNodeOutputName`: an event has no
input, a gate leaves by pass or fail, a module by out, a record node keeps
its one unnamed link). The palette lists the designer nodes by group, then
the function records under "Function record"; the canvas draws each node's
own pins (a gate's fail has its own), carries a new link's port to the hub,
and refuses a link between a program node and a record node. The inspector
shows a program node's settings as named inputs from
`weaponGraphDesignerSettings` (the rule table's settings with designer
labels, has_ammo's rounds, state_is's and set_state's key and value, the
lock's target filter); a test checks every bound and choice against the
runtime's own validator. A setting left out reads "the function's own" with
Set; Reset leaves it out; "$name" settings stay text. Replace With swaps a
designer node for another of its group, keeping its links and place and
dropping its settings; Duplicate keeps a program node's settings. Mike's
first example is a pd-tests case: the Falcon-like program with Fire once
replaced by Automatic (rate 600, spin-up 0), as the hub writes it,
validates, splits into a v2 primary file and compiles.

**PR 49 review (2026-11-22):** the homing rocket's re-check and the proximity
arm's filter now read the owner's IR scanner (`sightOwnerUsesIr`: a living
player, not in a cutscene or an eyespy, using the scanner; anyone else
none), so `"cloaked": "with_ir"` there means what it means for the sight;
they had built their facts with it off. The inspector's kinds checkboxes
start from `weaponGraphDesignerKindsMask`, which expands `characters`,
`things` and `all` as the filter reads them, so an edit keeps a group.

**PR 49 review, casings (2026-11-22):** a program weapon now loads a casing
model for each casing its two functions name (`bgunQueueCartModels`: the
selected function's, or the other's when it names none, and a second when
they differ), and each attack ejects its own type's model
(`bgunGetCartModeldefForCasing`, never another type's). Before, the model
was the selected function's at equip only, so a second casing type ejected
the first's model, and a gun equipped on a function with none ejected no
casings after a switch. The base guns' own quirk of that kind is fixed with
it: the Super Dragon equipped in grenade mode now ejects its rifle casings
once switched to its primary. The profile executor's weapons keep their
path.

**PR 49 review, casings by state (2026-11-22):** one function can also eject
more than one casing type: its program can hold `og.weapon.casing` nodes on
the two branches of a `gate.weapon.state_is`, and the press plan the gun
took its models from is walked with no states, so it named only the casing
of the branch taken with none set; a hand with the state set asked for the
other and found no model. The gun now loads a model for every casing either
function can eject (`weaponFunctionCasings`: each casing node the program
holds, wherever it sits, plus the function's own, which a branch with no
casing node ejects; one node's casings are `weaponRulesCasingNodeMask`), one
model a type and at most the four casing models, with `cartmodeldef` still
the selected function's press casing, loaded first. Casings still load in
one-player games only, as in the original.

**PR 49 review, locks (2026-11-22):** a weapon whose lock takes targets
(`weaponSightLock(0)->locks > 0`) now picks them in multiplayer and has
their bounds and deaths refreshed, as the aim-tracking base weapons do;
those (the CMP150 and the rocket launchers) are the only base weapons with
`WEAPONFLAG_AIMTRACK` and the only ones with a lock, and a mod weapon's
adapter never sets the flag. And what a weapon's authored lock takes
(`weaponSightLockAuthored`: its lock is not the original's for the weapon)
passes the look's native filter too, so a lock that sees through cloaks or
takes objects that do not react to sight can acquire them; an unmodified
weapon keeps the native look.

**8b as written (2026-11-22, `week-pd/core-weapons-w8`):** done before 8c,
since it completes the done-when (8c's value nodes are the advanced
extra). The rocket example needs only settings on nodes the base rocket
launcher already has (the secondary's `og.weapon.lock_on`, the homing
rocket's `og.projectile.home`), so the creator edits a projectile's program
by its nodes' settings rather than on a second canvas. The template
editor's Projectile Programs tab lists each projectile the template carries
(the rocket launcher's rocket and homing rocket) with its program nodes that
have settings, each with the inspector's named inputs
(`pdguiWeaponGraphRenderNodeSettings`). An edit rewrites that node's
settings in the program's text (`weaponGraphEditorSetNodeSettings`; the
rest is kept), the program is checked as the game checks it, and Save
writes it over the mod's copy of the projectile
(`<projectile>::behavior.graph.json`), refusing with the reason while it
does not check out. The projectile designer nodes (17 events, 4 gates, 27
modules; Events, Checks, Arm, Flight, Impact) and the projectile settings
(thrust, trail, hit sound, bounce smoke, the explosion, the home's and the
proximity arm's target filter) are tested against the projectile rule
tables and parsers. Adding or rewiring a projectile's nodes in the creator
is not in this part.

## Smokes for Integration's finalization batch

**The `aim_at` smoke event (2026-11-22, with Integration and MP-RULES).** The lock smokes need the view held
on a moving target, which `mouse_move` cannot do. `{ "type": "aim_at", "target": "simulant" | "simulant:<i>" | "vehicle", "duration_ms": N }` (`port/src/smoke_harness.c`, validated by
`port/src/smoke_fixture_schema.c`) picks its target once (the live simulant or vehicle nearest player 0,
or bot slot i) and holds player 0's view on its body centre, from the camera, for `duration_ms`. It is
applied by `smokeHarnessAimApply` right after `bmoveProcessInput` in `bmoveTick`, only for player 0's own
tick and on foot (a bike turns through `speedtheta` in `bbikeTick` right after the hook, so a bike, a grab
or a cutscene pauses the aim; `paused` and `resumed` are logged, and the view a pause left is not an
override), so the look input of the frame cannot fight it and the movement tick refreshes the trig. Its logs say why a lock did not come: `SMOKE.AIM: begin` (los, the eye and the
point), `override` (look input it cleared, the turn and pitch speeds it zeroes while it holds because
`bwalkUpdateTheta` adds the turn after the hook, or a view change the game made, such as Look Ahead
re-centring the pitch while walking, which aim mode turns off; a smoke that forbids it keeps the player
still or aiming), `lost` (the target died or went, or its stage was swapped out: an aim is bound to the
stage it began in, so it ends before the old stage's pointers are read), and `end` (`held_ms`,
`on_target_ms`: the time the look picker, which the lock takes from, chose the target). The B-1135
schedule bound counts an aim to `at_ms + duration_ms`.

Listed as each slice lands; none is run by Core Weapons.

| Slice | Smoke | What it shows |
|---|---|---|
| 1 | `weapon_mesh_ingress_smoke` | the profile's alternating-state custom weapon fires as before |
| 1 | `graph_v2_world_hit_smoke` | the profile's world-hit delivery as before |
| 1 | `boot_smoke` | every base weapon still registers (v1 records, no program refused) |
| 2 | `boot_smoke` on a freshly extracted install | every base weapon re-extracts once as v2 (`embedded.v17`) and registers its programs; no `program refused` line |
| 2 | `mod_author_weapon_create_smoke`, `mod_author_weapon_play_smoke` | a weapon made from a base one keeps its program and fires |
| 2 | any campaign or combat smoke that fires, reloads and clicks (for example `projectile_events_smoke`) | `WEAPON.EVENTS: stage_end ... rules.differ=0`, `WEAPON.RULE:` lines for trigger_pressed, ready_while_held and clip_empty |
| 3 | `held_weapon_cadence_smoke` (new, `tools/smoke-verify/tests/`) | each automatic, burst and single shot's first press logs its program's plan equal to the original's (AR34, Reaper, Cyclone and its dump, MagSec 4 burst, Shotgun double blast, CMP150, Laser shot and stream, Falcon 2); `WEAPON.EVENTS: smoke_exit ... rules.differ=0`; no `WEAPON.RULE differs` |
| 3 | `boot_smoke` on a freshly extracted install | every base weapon re-extracts once (`embedded.v18`) and registers its programs with their trigger nodes; no `program refused` line |
| 3 | a Combat Simulator match with simulants holding automatics (for example `mp_combat_local_scorelimit_smoke` or `combat_sim_simulant_twins`) | no `WEAPON.RULE differs: ticks_per_shot` line: simulants fire base guns at the original rates |
| 3 | `mod_author_weapon_create_smoke`, `mod_author_weapon_play_smoke` | a weapon made from a base one keeps its trigger node and fires |
| 4 | `held_weapon_shot_smoke` (new) | the rocket launcher, crossbow, Devastator and Super Dragon launch the original's items; the Farsight's, the sedative strike's, the RCP120 cloak's and the Laptop gun's plans equal the original's; `rules.differ=0`; no `WEAPON.SHOT: launch` line |
| 4 | `held_weapon_cadence_smoke` | its plan lines now end with the shot (`shot=hitscan pellets=6` for the Shotgun, `shot=beam range=300` for the Laser stream) |
| 4 | `boot_smoke` on a freshly extracted install | `embedded.v20`; every base program registers with its shot node and its reload's style |
| 4b | any smoke that empties a Shotgun's or crossbow's clip | the clip_empty `WEAPON.RULE` line shows `reload=one_at_a_time native_reload=one_at_a_time`; no `WEAPON.RULE differs` |
| 4b | `crystal_pistol_bot_charge_smoke` | the crystal pistol (a v1 graph, its record's regrow) still regrows its crystals (`BONDGUN.REGROW` lines) |
| 5 | `held_weapon_charge_smoke` (written 2026-11-22, `tools/smoke-verify/tests/`: gives `base:mauler`, fires the primary, switches to the secondary, waits 3.5 s while it charges, fires) | the press's `WEAPON.RULE` line shows `plan=charge start=while_selected time=100 steps=5 ... damage_per_step=3.33333325`; no `WEAPON.RULE differs` |
| 5 | `crystal_pistol_bot_charge_smoke`, `crystal_pistol_bot_embed_smoke` | the crystal pistol's committed charge still waits its time and launches (`BONDGUN.CHARGE: start ... release_in=`) |
| 5 | `projectile_events_smoke` | the timed grenade still cooks and throws (its fuse lines unchanged) |
| 5 (review) | the Mauler smoke above | the gun's tint and the shot's pitch still follow the charge (now `chargelevel`); a weapon change clears it |
| 5 (review) | `crystal_pistol_bot_charge_smoke`, `crystal_pistol_bot_embed_smoke` | the player's and the bot's `BONDGUN.CHARGE: start ... release_in=` both equal the function's plan time; the glow ramps over it |
| 5 (review) | `projectile_events_smoke` | a cooked grenade's fuse lines unchanged (a base grenade's fuse is its activation time) |
| 4 (review) | any Laser stream smoke (`held_weapon_cadence_smoke`) | the stream still hits at up to 300 |
| 6b | `boot_smoke` on a freshly extracted install | `embedded.v23`: every base weapon re-extracts once; no `program refused` line (the rocket launcher's and the CMP150's programs register with `event.weapon.aiming`) |
| 6b | `held_weapon_cadence_smoke`, `held_weapon_shot_smoke` | their `WEAPON.EVENTS` lines end with `aiming=N`; `rules.differ=0` as before |
| 6b | `projectile_events_smoke` | the proximity mine, the Dragon and the proximity grenade go off as before (their proximity lines unchanged) |
| 6b | `crystal_pistol_bot_charge_smoke`, `crystal_pistol_bot_embed_smoke` | the crystal pistol's mine keeps its record's radius and filters (`BONDGUN.CRYSTAL: mine triggered` as before; the owner still sets off its own mine) |
| 7a | `boot_smoke` on a freshly extracted install | `embedded.v24`: every base weapon re-extracts once; no `program refused` line (every shooting function's program carries `og.weapon.recoil`) |
| 7a | `held_weapon_cadence_smoke`, `held_weapon_shot_smoke` | their first `WEAPON.RULE` lines end with ` recoil=...`; `rules.differ=0`; the guns kick, slide and jitter as before (the Falcon 2's slide, the Reaper's shake rising with its spin) |
| 7b | `held_weapon_cadence_smoke` (or any smoke that fires a Falcon 2, a CMP150 and a Shotgun) | casings fly as before (standard, rifle, shotgun) and the flash shows; no `WEAPON.RULE differs` line |
| 7b (sound) | `held_weapon_cadence_smoke`, `held_weapon_shot_smoke` | the guns sound as before (the Falcon 2, the CMP150's stream, the Shotgun); their first `WEAPON.RULE` lines end with ` sound=N slot=N`, N above 0; no `WEAPON.RULE differs` line; simulants' shots sound as before (any simulant smoke) |
| 7c | `held_weapon_cadence_smoke`, `held_weapon_shot_smoke`, any smoke that punches or throws a grenade | fire animations play as before (the Falcon 2's, the Shotgun's pump, the punch, a grenade's throw), and the attacks keep their timing; first `WEAPON.RULE` lines end with ` animation=invanim_...`; no `WEAPON.RULE differs` line |
| 8a | a creator smoke (none written yet: Modding Hub, Weapons, open the base Falcon 2 as a template, right-click Fire once, Replace With, Automatic, Set rate to 600 and spin-up to 0, save the mod; then a match with the saved weapon, holding the trigger). How it runs is Mike's call, **D-059**: (A, Core Weapons' pick) a `graph_edits` step in the `--mod-author` recipe (`port/src/mod_author_recipe.cpp`, Authored Mods' format) that applies the creator's own Replace With and node settings to the install's real template and saves it the way the creator does, with Replace With's rules moved into `port/src/weapon_graph_editor_io.cpp` so the creator and the recipe share them; (B) a hand-written program in a fixture, which drops the extracted record (sounds, fire animation, recoil settings, timings), so it plays a stripped gun and goes stale at the next `embedded.vNN`; or (C) a person makes and plays both examples. | the palette lists the designer groups; the saved weapon's first `WEAPON.RULE: trigger_pressed ... modules=automatic` and a held trigger keeps firing; no `program refused` line |
| 8b | a creator smoke (none written yet: Modding Hub, Weapons, the base rocket launcher as a template; Secondary Graph, the lock: kinds vehicle, relation enemy, vehicles any; Projectile Programs, homing rocket, Home toward target: the same filter; save; then the 6c vehicle lock smoke with the saved launcher). Under D-059 A, the same recipe step, on the weapon's lock and on its projectile's member (`<projectile>::behavior.graph.json`). | the saved mod's copy of the homing rocket carries the filter; the lock takes the empty bike (`SIGHT.LOCK: ... kind=vehicle`) and never a player; no `program refused` line |
| 4, 6b (cloak) | `held_weapon_cloak_smoke` (written 2026-11-22, `tools/smoke-verify/tests/`) | the alternate-function button toggles the RCP120's cloak on and off: `BONDGUN.CLOAK: on player=0 weapon=base:rcp120 drain=24 slot=N`, then `off`; no `WEAPON.RULE differs` or `program refused` line |
| 6c | a vehicle lock smoke (not written yet: it needs a launcher whose lock seeks vehicles, Mike's Enemy vehicles sample asked of Authored Mods or the 8b creator example, D-059. The base rocket launcher's lock takes every kind, so it does not seek vehicles (`weaponRulesLockSeeksVehicles`), and the native look drops an empty Forge hoverbike, which does not react to sight; Codex on PR 56. With one: the Blank Room with a hoverbike made at boot as `hoverbike_room_flow_smoke` does, `aim_at` vehicle, aim held on the secondary, and a primary hold that takes no lock and no look time) | aiming the secondary at the empty bike logs `SIGHT.LOCK: ... kind=vehicle occupied=0 relation=none`; the primary takes no lock on it; with a second player riding (split screen or a second client) the primary logs `occupied=1 relation=enemy`, and its rocket flies straight once the rider bails; `rules.differ=0` |
| 6a, 6b | `held_weapon_lock_smoke` (written 2026-11-22 with `aim_at`: two simulants that hold their fire, the first placed in sight; the CMP150's primary and its follow lock-on, then the targeted rocket, each lock the placed one with aim held, and the rocket is fired at it; the second simulant is aimed at for the record only, so two locks on one function are not required) | `WEAPON.RULE: aiming` lines for both weapons and functions with `lock` equal to `native_lock` (`locks=1` for the rocket launcher; `locks=4 ... sight_targets_only: true` for the CMP150, `aim_follows=true` on its secondary); `aiming=N` above 0 with `rules.differ=0`; the homing rocket flies to the locked simulant |

## Where to look

- Runner: `port/src/mission_graph_runner.cpp`, `port/include/mission_graph_runner.h`.
- Records and held programs: `port/src/weapon_graph_runtime.c`.
- The profile: `port/src/weapon_graph_v2*.cpp`.
- The rule table, the plan, the cadence steps and the walk: `port/include/weapon_rules.h`.
- The seam side (hands' presses, states and plans; the parity probe; simulants' press plans): `port/src/weapon_events.c`.
- Seams: `src/game/bondgun.c`, `src/game/game_0b0fd0.c`, `src/game/prop.c`.
- The projectile pattern this follows: `context/designs/modding/projectile-events.md`.
