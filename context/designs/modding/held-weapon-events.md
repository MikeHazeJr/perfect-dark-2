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

## Where to look

- Runner: `port/src/mission_graph_runner.cpp`, `port/include/mission_graph_runner.h`.
- Records and held programs: `port/src/weapon_graph_runtime.c`.
- The profile: `port/src/weapon_graph_v2*.cpp`.
- Seams: `src/game/bondgun.c`, `src/game/game_0b0fd0.c`, `src/game/prop.c`.
- The projectile pattern this follows: `context/designs/modding/projectile-events.md`.
