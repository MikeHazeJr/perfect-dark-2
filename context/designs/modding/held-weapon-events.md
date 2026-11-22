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

## Smokes for Integration's finalization batch

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
| 5 | any smoke that charges the Mauler and fires it (none written yet: give `base:mauler`, switch to the secondary, wait two seconds, fire) | the press's `WEAPON.RULE` line shows `plan=charge start=while_selected time=100 steps=5 ... damage_per_step=3.33333325`; no `WEAPON.RULE differs` |
| 5 | `crystal_pistol_bot_charge_smoke`, `crystal_pistol_bot_embed_smoke` | the crystal pistol's committed charge still waits its time and launches (`BONDGUN.CHARGE: start ... release_in=`) |
| 5 | `projectile_events_smoke` | the timed grenade still cooks and throws (its fuse lines unchanged) |
| 5 (review) | the Mauler smoke above | the gun's tint and the shot's pitch still follow the charge (now `chargelevel`); a weapon change clears it |
| 5 (review) | `crystal_pistol_bot_charge_smoke`, `crystal_pistol_bot_embed_smoke` | the player's and the bot's `BONDGUN.CHARGE: start ... release_in=` both equal the function's plan time; the glow ramps over it |
| 5 (review) | `projectile_events_smoke` | a cooked grenade's fuse lines unchanged (a base grenade's fuse is its activation time) |
| 4 (review) | any Laser stream smoke (`held_weapon_cadence_smoke`) | the stream still hits at up to 300 |

## Where to look

- Runner: `port/src/mission_graph_runner.cpp`, `port/include/mission_graph_runner.h`.
- Records and held programs: `port/src/weapon_graph_runtime.c`.
- The profile: `port/src/weapon_graph_v2*.cpp`.
- The rule table, the plan, the cadence steps and the walk: `port/include/weapon_rules.h`.
- The seam side (hands' presses, states and plans; the parity probe; simulants' press plans): `port/src/weapon_events.c`.
- Seams: `src/game/bondgun.c`, `src/game/game_0b0fd0.c`, `src/game/prop.c`.
- The projectile pattern this follows: `context/designs/modding/projectile-events.md`.
