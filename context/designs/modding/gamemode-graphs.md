# Combat Simulator modes as graphs

Status (2026-11-21): slices 1 to 5 and 7 written by the Multiplayer and Rules
session (see the "as written" sections); slice 6 waits for decision D-055. Originally CORE, 2026-11-19: a survey and a plan for the week note's "Combat
Simulator rules" row ("a mode graph calling native mode modules"), under
decision 1 ("Players should be able to recreate any existing game behavior
with the nodes"). Nothing here is built. MULTIPLAYER owns the native mode
rules and their network state this week, so CORE adds no graph entry point
until their host and presentation split has landed; this document is what
CORE will build then, and the questions at the end are for them. Facts from
a read-only survey of `week-pd/core-s7` (9f4c3f72); `sc` is
`src/game/mplayer/scenarios.c`, and each mode's code is
`src/game/mplayer/scenarios/<mode>.inc`, included into it.

## Where it stands

- **Six modes, one table.** `MPSCENARIO_COMBAT` 0, `HOLDTHEBRIEFCASE` 1,
  `HACKERCENTRAL` 2, `POPACAP` 3, `KINGOFTHEHILL` 4, `CAPTURETHECASE` 5; the
  chosen one is `g_MpSetup.scenario`. `struct mpscenario` (sc:86, private to
  the file) has 18 callbacks and `g_MpScenarios[]` (sc:630) fills them per
  mode; a field left out is NULL and its dispatcher does the default.
- **The callbacks and when they run:**

  | Callback | Dispatcher | Called from | When |
  |---|---|---|---|
  | `initfunc` | `scenarioInit` | the mode pick in a menu; `mpStartMatch`; `mpApplyConfig`; a setup file load | the mode is chosen, and every match start |
  | `numpropsfunc` | `scenarioNumProps` | `setupLoadFiles` | stage load, counting extra objects |
  | (a switch, not the table) | `scenarioReset` | `lvReset` | stage load: clears the mode's state and reads the stage intro's case and hill pads |
  | `initpropsfunc` | `scenarioInitProps` | `setupCreateProps` | stage load: makes the token, the terminal, the hill |
  | `tickfunc` | `scenarioTick` | `lvTick` | every frame, paused or not |
  | `tickchrfunc` | `scenarioTickChr` | `lvRender` per local player; `botTick` per simulant (host only) | every frame |
  | `calcscorefunc` | `scenarioCalculatePlayerScore` | rankings, team score, the score limit, the match result, the Grid scorecard | whenever a score is asked |
  | `spawnfunc` | `scenarioChooseSpawnLocation` | `playerReset`, `playerStartNewLifeAt`, `botSpawnAttempt` | a spawn or respawn |
  | `maxteamsfunc` | `scenarioGetMaxTeams` | team menus, the client team check, Capture the Case | menus and joins |
  | `hudfunc`, `radarextrafunc`, `radarchrfunc`, `highlightpropfunc`, `highlightroomfunc` | their dispatchers | HUD, radar, chr and object render, room lighting | drawing |
  | `readsavefunc`, `writesavefunc` | `scenarioReadSave`, `scenarioWriteSave` | the setup file | load and save (King of the Hill's hill time) |
  | `optionsdialog` | `menuhandlerMpOpenOptions` | the setup menus | the mode's options page |

- **Much of a mode is not behind the table.** Token pickups
  (`scenarioPickUpBriefcase`, `scenarioPickUpUplink`: a Capture the Case
  capture scores inside the pickup), a dropped token
  (`scenarioHandleDroppedToken`), the Hacker Central terminal
  (`scenarioHandleActivatedProp`), a Pop a Cap death (`pacHandleDeath`,
  called from `mpstats.c`), the spawn anchor of Capture the Case, the token
  weapon and model by mode, what a carrier may pick up
  (`objPickupRulesAllow`), the lowered eye height while carrying, the forced
  team options (King of the Hill and Capture the Case turn teams on;
  `matchConfigApplyScenarioRules`), which highlight options a mode honours,
  the challenge score limit scaling, and the simulants: the team commands a
  mode offers and how a simulant carries them out are about 500 lines of
  `bot.c` keyed by mode (4138 to 4628), with more in `botFindPickup`.
- **State.** `g_ScenarioData` is a union of the five modes' structs
  (`types.h` 4337), so two modes cannot hold state at once; more lives in
  file statics of each `.inc`, in `mpchrconfig.numpoints` (the points of
  Hold the Briefcase, King of the Hill and Capture the Case),
  `g_Vars.mphilltime`, `playerstats.tokenheldtime` and eight `aibot` fields.
- **Networking: the host decides, the client presents.**
  `scenarioNetIsAuthority` guards every mode's tick and scoring; the host
  captures the mode's state into `SVC_SCENARIO_STATE` (a union per mode, a
  revision that moves only on a change, a resend every 30 ticks when only a
  timer moved, a baseline at stage start and at reconnect) and each mode's
  `...NetPresent` applies it on the client. Token pickups and drops travel
  as prop messages, and the client runs the pickup body, the Capture the
  Case score included; simulant AI runs on the host only.
- **The archive names a mode and nothing more.** `gamemodes/<slug>.pdgamemode`
  holds `rules.json` (`pd2.gamemode.rules.v2`): the catalog id, a
  `mode_key` that must be one of the six, name, description, players and
  whether teams are required. It is read once, at load. No field drives
  behavior; everything stays behind the mode number. A mode key outside
  the six fails to load, so a mod cannot add a mode today.

## Proposal

The pattern of the level programs (object-events.md) and the projectile
programs (projectile-events.md), in the same order.

1. **A mode archive carries a program.** A `mode.graph.json` member, schema
   `pd.gamemode_graph.v1`, compiled by the graph runner in a mode domain:
   `event.mode.*` events, gates, and `og.mode.*` native modules joined by
   execution edges. `rules.json` stays the data (names, players, teams).
2. **Events at the dispatcher seams**, fired where the table is asked today,
   each carrying what its callback is given:

   | Event | Seam | Carries |
   |---|---|---|
   | `event.mode.chosen` | `scenarioInit` | nothing: the mode sets up its options |
   | `event.mode.stage_reset` | `scenarioReset` | the stage; then one `event.mode.intro_pad` per case or hill pad the stage intro lists |
   | `event.mode.props_init` | `scenarioInitProps` | nothing |
   | `event.mode.tick` | `scenarioTick` | the frame |
   | `event.mode.chr_tick` | `scenarioTickChr` | the player or simulant |
   | `event.mode.score` | `scenarioCalculatePlayerScore` | the player or simulant whose score is asked |
   | `event.mode.spawn` | `scenarioChooseSpawnLocation` | who spawns |
   | `event.mode.token_picked_up` | the pickup routines | the token, who took it |
   | `event.mode.token_dropped` | `scenarioHandleDroppedToken` | the token, who dropped it |
   | `event.mode.prop_activated` | `scenarioHandleActivatedProp` | the object, who used it |
   | `event.mode.player_died` | `mpstatsRecordDeathByRuntimeIndex` | the victim and the killer |

   Drawing (HUD, radar, highlights, room lights) is presentation: it reads
   the state the rules made, on host and client alike, and is chosen by
   presenter modules on `event.mode.chosen`, not by per-frame events.
3. **The native callbacks become named modules**, one per mode and seam,
   each running exactly today's code: `og.mode.htb.tick`,
   `og.mode.koh.score`, `og.mode.ctc.spawn`, `og.mode.pac.player_died`,
   and the presenters `og.mode.htb.hud`, `og.mode.koh.room_lights`. As with
   projectiles, a module is a choice the seam acts on: the dispatcher fires
   the event, the program chooses, and the dispatcher runs the callbacks
   chosen. The default each dispatcher has today (kills minus suicides for
   the score, the general spawn chooser) is a module too
   (`og.mode.default.score`), so Combat's program is not empty.
4. **The extractor wires each base mode to its own callbacks**, read from
   one table that replaces `g_MpScenarios[]` (which callbacks a mode has),
   so a base program and the original agree by construction and a parity
   probe proves it in play: per seam, the callback the table names and the
   modules the program chose (`mode.compared`, `mode.differ`). The mode
   smokes (`mp_<mode>_local_smoke`, `_net_smoke`, `_net_rematch_smoke`,
   `_reconnect_smoke`) require differ=0.
5. **Fail closed.** A mode archive whose program does not compile refuses at
   registration, so the mode cannot be chosen (as a vehicle with a refused
   graph cannot be mounted). A match whose host and client hold different
   mode archives is refused at the content check that Grid matches already
   make.
6. **Authority.** Rule events fire on the host only, behind
   `scenarioNetIsAuthority`, where the outcome is decided today; a client
   runs presenters and applies the host's state. What a mod's rewired mode
   changes is decided by the host and replicated, with no new message as
   long as it uses the six modes' state.
7. **Then the pieces come apart.** Three things stand between "a base mode
   wired to its own callbacks" and "a mod's own mode":
   - *State.* The union becomes a struct of the five states, so a program
     may run the hill of one mode and the token of another. The network
     state follows: one section per state in use, not one per mode number.
   - *The mode number.* A mod's mode needs an identity that is not one of
     six numbers. Matches already carry the mode's catalog id at stage and
     lobby start; the setup file (3 bits), the room settings byte and the
     wire state's leading byte still carry the number. A mod mode takes a
     private number above 5 for the session, mapped from its catalog id on
     each peer, the way custom weapons take private slots.
   - *The code outside the table.* Token pickups and drops, the terminal,
     the death hook and the forced options move behind their events (the
     table above already lists them); the carrier rules (what a carrier may
     pick up, the eye height) become module parameters of the token.
8. **Simulants last.** The team commands a mode offers and what a simulant
   does when it has none are the largest block of mode code outside the
   table. They become modules a mode names (`og.mode.bot.hold_hill`), fired
   from `botTickUnpaused`'s command choice; until then a mod mode's
   simulants fight as in Combat.

## Slices

1. **Record-only events.** The eleven events, counted per match and logged
   once per kind and mode, at the dispatchers and the out-of-table routines.
   No behavior change. The mode smokes gain their lines.
2. **Programs.** The schema, the runner's mode domain, the extractor writing
   the six base programs, registration (fail closed), the archive validator
   and its tests.
3. **The table is the program's.** The dispatchers fire rule events and run
   the callbacks chosen; the parity probe; `g_MpScenarios[]` becomes the
   rule table the extractor and the probe read.
4. **The code outside the table** behind its events: token pickup and drop,
   the terminal, the death hook, `scenarioReset`'s switch, the forced
   options.
5. **State and wire by state**, not by mode number.
6. **A mod's mode.** The private mode number, the picker, a worked example
   (the hill of King of the Hill with the token of Hold the Briefcase).
7. **Simulant commands** as modules.

Slices 1 to 3 change no file that the network state lives in; slice 4
touches the pickup and drop routines MULTIPLAYER fixed this week (B-1209,
B-1272); slice 5 changes the wire format and belongs to them or is done
with them.

## Slice 1 as written (Multiplayer and Rules, 2026-11-21; not run in the game yet)

Mike assigned this plan to the Multiplayer and Rules session on 2026-11-21, which
owns the native mode rules and their network state, so the questions below are
answered in place as the slices land.

- `port/src/mode_events.c` / `port/include/mode_events.h`:
  `modeEventRecord(kind, who)` counts a match's events and logs the first of each
  kind for a mode (`MODE.EVENT: <kind> mode=<catalog id> scenario=<n> who=<n or ->`);
  `modeEventsReport(why)` logs the totals (`MODE.EVENTS: <why> mode=... chosen=N
  stage_reset=N intro_pad=N props_init=N tick=N chr_tick=N score=N spawn=N
  token_picked_up=N token_dropped=N prop_activated=N player_died=N`) at the stage
  end (`mainEndStage`) and at a smoke's scripted exit, then starts again; a match
  with no events logs nothing. Only the peer that decides records
  (`scenarioNetIsAuthority`: offline or the listen host), as rule events will.
- Seams, each after the native decision: `chosen` in `scenarioInit`;
  `stage_reset` in `scenarioReset` after its per-mode switch, and one `intro_pad`
  per case, case-respawn or hill pad the stage intro lists; `props_init` in
  `scenarioInitProps`; `tick` in `scenarioTick` while a match runs; `chr_tick` in
  `scenarioTickChr` (a simulant, or the current player); `score` in
  `scenarioCalculatePlayerScore` (who is the Combat Simulator index);
  `spawn` in `scenarioChooseSpawnLocation`; `token_picked_up` in
  `scenarioPickUpBriefcase` (Hold the Briefcase, Capture the Case) and
  `scenarioPickUpUplink` (Hacker Central); `token_dropped` in
  `scenarioHandleDroppedToken`; `prop_activated` in `scenarioHandleActivatedProp`
  for the Hacker Central terminal; `player_died` in
  `mpstatsRecordDeathByRuntimeIndex` (who is the victim's index).
- The six local mode smokes (`mp_combat_local_scorelimit_smoke` and
  `mp_<mode>_local_smoke`) require the mode's first `chosen` and `tick` lines and a
  stage-end total with ticks and spawns. Static contract:
  `gamemode-graphs slice 1` in `tests/test_match_scenario_start_static.cpp`.

## Slice 2 as written (Multiplayer and Rules, 2026-11-21; not run in the game yet)

- **The program.** A `.pdgamemode` carries `mode.graph.json`, schema
  `pd.gamemode_graph.v1`: `catalog_id` (the archive's own), `nodes` and `edges`
  as in the other programs (an execution edge has `"input": "exec"`; a node
  that is not `event.*`, `gate.*` or `og.*` is data). `gamemode.ini` and the
  manifest name it (`mode_graph_file`), and the catalog carries it from every
  ingress (base registration, the archive walker, the INI scanner, received
  mods) as `ext.gamemode.mode_graph_file`; the pdmod packer keeps it.
  `rules.json` stays the data.
- **The runner's mode domain** (`missionGraphProgramCompileMode`): the twelve
  events of slice 1 as `event.mode.*`, runner kinds 64 to 75 (a block above the
  other programs' kinds, so theirs can grow below it). A mode program refuses
  every other event, every other program refuses these, and they filter
  nothing.
- **The modules** (`port/include/mode_rules.h`, `port/src/mode_rules.c`, no game
  state): `og.mode.<mode>.<seam>` for each callback `g_MpScenarios[]` gives a
  mode (`htb`, `htm`, `pac`, `koh`, `ctc`; Combat has none of its own) and the
  dispatchers' defaults `og.mode.default.score` and `og.mode.default.spawn`, 50
  in all. One event chooses each seam's modules:

  | Event | Chooses |
  |---|---|
  | `event.mode.chosen` | `init`, and what every peer needs to know about the mode: `num_props`, `max_teams`, `save` (King of the Hill's hill time in the setup file) and the presenters `hud`, `radar_extra`, `radar_chr`, `highlight_prop`, `room_lights` (the room pair of King of the Hill and Capture the Case) |
  | `event.mode.props_init` | `init_props` |
  | `event.mode.tick` | `tick` |
  | `event.mode.chr_tick` | `tick_chr` |
  | `event.mode.score` | the mode's `score`, else `og.mode.default.score` |
  | `event.mode.spawn` | `og.mode.ctc.spawn`, then `og.mode.default.spawn` (the original falls back to the general chooser when a mode's own finds no place) |

  The six other events choose nothing until slice 4 moves the code outside the
  table behind them. The options dialog belongs to the legacy menu and is not
  a module.
- **Base programs.** The extractor writes each base mode's program from the
  table (`modeRulesBaseProgramJson`; node ids `mode.event.<event>` and
  `mode.<seam>`, `mode.spawn.default` for Capture the Case's fallback): Combat
  4 nodes and 2 execution edges, Hold the Briefcase and Hacker Central 17 and
  11, Pop a Cap and King of the Hill 14 and 9, Capture the Case 19 and 13. The
  meta kind gains `_modegraph`, so an install re-extracts its meta archives
  once. pd-tests read `scenarios.c` and hold the table to `g_MpScenarios[]`,
  so a base program and the original agree by construction.
- **Registration fails closed.** Hydrating a mode (`s_hydrateGamemode`,
  `asset_runtime.c`) compiles its program and checks it for the archive's mode
  (`modeRulesCheckProgram`): every mode module is the mode's own or a default
  (the five modes share one state, `g_ScenarioData`, until slice 5), every
  module an event reaches (both branches of a gate) is one the event chooses,
  and `event.mode.spawn` reaches a spawn module. A mode without a program, or
  with a refused one, does not hydrate, so the catalog does not activate it
  (`MODE.GRAPH: refused the program of '<id>': <why>`). A registered mode logs
  `MODE.GRAPH: program '<id>' mode=<n> nodes=N exec_edges=E modules=M`, and its
  binding keeps the compiled program for slice 3 (`assetRuntimeModeProgram`).
  Known gap, B-1292: a refused mod mode stays enabled in the catalog, so the
  legacy picker would meet it; slice 6's picker skips refused modes.
- **The validator** (`tools/asset_archive_conformance.py`, `validate_mode_graph`)
  requires and allows the member, checks the program as the game does, and
  holds a base archive (`base:`) to exactly the original table's wiring; the
  example mod's `tri_gamemode` carries Combat's program. Tests:
  `tests/test_mode_graph_program.cpp` (`[gamemode-graphs][slice2]`),
  `tools/tests/test_mode_graph_conformance.py` (in CI), the validator's
  self-test (`GAMEMODE-PROGRAM`).
- **Nothing runs from the program yet**: the dispatchers still call
  `g_MpScenarios[]`; slice 3 makes them fire the events and run the callbacks
  chosen. In the game, any smoke of the finalization batch on a re-extracted
  base should show six `MODE.GRAPH: program 'base:...'` lines at boot, and the
  mode smokes should pass as before.

## Slice 3 as written (Multiplayer and Rules, 2026-11-21; not run in the game yet)

- **The plan.** `modeRulesPlanFromProgram` (`mode_rules.c`) fires each choosing
  event into a mode's program once and keeps, per seam, the owners of the
  modules it reaches in walk order (a mode, or the default), each once and at
  most two. A mode program's choice reads no game state (none of the mission
  gates passes for a mode event), so one walk per event is its whole answer.
  The runner now accepts mode events when they are fired (`eventValid`).
- **The dispatchers run it** (`scenarios.c`). The plan is made when the mode
  is chosen (`scenarioInit`, logged `MODE.PLAN: chosen ...`) and at each stage
  reset (`MODE.PLAN: stage_reset mode=<id> scenario=<n> modules=N`), and every
  dispatcher runs the `g_MpScenarios[]` callback of each owner its seam names,
  in order: the score from the first score module (`og.mode.default.score` is
  the dispatcher's own kills-minus-suicides code; no score module scores
  nothing), the spawn from each chooser in turn until one places the player
  (`og.mode.default.spawn` is the general chooser, and an empty plan falls back
  to it so players still spawn), the setup file's mode data from the save
  module (without one, the dispatcher's own byte). Nothing calls a mode's
  callback past its plan any more; the table is read by mode only for the
  legacy options dialog and two log lines.
- **A mode with no registered program runs nothing**: its stage reset fails
  loudly (`LOUDFAIL.MODE.PLAN`); made earlier (a menu pick before the catalog
  is ready), the plan is only a warning and is made again at the stage reset.
- **The parity probe.** Every dispatcher call compares the plan's seam with
  what the original table runs, read from `g_MpScenarios[]` itself (the mode's
  callback when it has one, the default score without one, the general spawn
  chooser after the mode's own), on every peer; the first difference of each
  seam logs both answers (`MODE.PARITY: differ scenario=.. seam=.. native=..
  program=..`) and the totals go out with the mode events
  (`MODE.PARITY: <why> mode=<id> compared=N differ=M`, stage end and smoke
  exit). The six local mode smokes require the stage-reset plan and
  `differ=0`, and forbid a difference and the loud failure.
- Base programs are the original table by construction (slice 2), so players
  see no change; a mod that rewires a mode changes it here (slice 6 lets a mod
  ship one). Tests: `[gamemode-graphs][slice3]` in
  `tests/test_mode_graph_program.cpp`.

## Slice 4 as written (Multiplayer and Rules, 2026-11-21; not run in the game yet)

The code outside the table is callbacks of its rows now, so the six remaining
events choose modules too (64 in all):

| Event | Chooses | Was |
|---|---|---|
| `event.mode.stage_reset` | `og.mode.{htb,htm,koh,ctc}.stage_reset` | `scenarioReset`'s switch (`htbReset`, `htmReset`, the hill count, Capture the Case's bases) |
| `event.mode.intro_pad` | `og.mode.{htb,htm,koh,ctc}.intro_pad` | the intro loop's mode checks; each module takes its own pad kind (case pads, or King of the Hill's hills) |
| `event.mode.token_picked_up` | `og.mode.{htb,ctc,htm}.token_picked_up` | the per-mode bodies of `scenarioPickUpBriefcase` and `scenarioPickUpUplink` |
| `event.mode.token_dropped` | `og.mode.ctc.token_dropped` | `scenarioHandleDroppedToken`'s Capture the Case body (the case goes home) |
| `event.mode.prop_activated` | `og.mode.htm.prop_activated` | `scenarioHandleActivatedProp`'s terminal |
| `event.mode.player_died` | `og.mode.pac.player_died` | `mpstats.c`'s Pop a Cap check (`pacHandleDeath`), now `scenarioHandlePlayerDied` |

- `g_MpScenarios[]` is written with designators (`[MPSCENARIO_CAPTURETHECASE] =
  { .tickfunc = ctcTick, ... }`), and the six new fields come last in `struct
  mpscenario`; the per-mode bodies moved verbatim into their own functions.
- The two token routines run only a module whose mode uses their token
  (`scenarioTokenWeaponNum`: the briefcase for Hold the Briefcase and Capture
  the Case, the uplink for Hacker Central), as the original did.
- No `g_MpSetup.scenario == MPSCENARIO_*` check is left in `scenarios.c`. Still
  checked by mode, and not part of this slice: which highlight options a mode
  honours (`scenarioHighlightProp`, Combat), Capture the Case's spawn anchor,
  the token weapon and model by mode, what a carrier may pick up and the
  carrier's eye height (the token's module parameters, plan item 7), and the
  simulants' commands (slice 7).
- The base programs gain the new modules, so the meta kind moves to
  `_modegraph_outside` (installs re-extract their meta archives once); the
  validator and its tests know the 64 modules. The forced team option needed
  no move: `matchConfigApplyScenarioRules` already reads it from the mode's
  rules (`teams.required`).

## Slice 5 as written (Multiplayer and Rules, 2026-11-21; not run in the game yet)

State and wire by state, not by mode number (protocol v67).

- **State.** `struct scenariodata` holds the five modes' states side by side
  instead of in one union, so a mode's program may run the hill of one mode and
  the token of another; every `g_ScenarioData.<mode>.*` access stays as it was.
- **The plan's states** (`modeRulesPlanStates`): one bit per mode whose modules
  the plan runs; the defaults keep no state, so Combat uses none.
- **Wire.** `SVC_SCENARIO_STATE` carries a count and one section per state the
  running mode's plan uses that has a wire body (`net_scenario_state_list_t`,
  `netScenarioStateListWrite` and `Read` in `net_scenario_state_wire.c`); each
  section is the old payload, keyed by its state. Sections are distinct states
  on one revision, 1 to 5 of them; a bad count, a repeated state or two
  revisions are refused whole (`NET_SCENARIO_STATE_BAD_SECTIONS`), and the
  caller's list is not touched. A base mode sends its own state alone, so the
  bytes are the old ones behind a count of 1.
- **Host and client.** The host captures each state its plan uses
  (`scenarioNetCaptureState`) and moves the shared revision when any section
  changed outside its timers; a client takes the list only when it carries
  exactly the states its own plan uses (both peers hold the same mode archive,
  so anything else is dropped and logged as before), and each mode's presenter
  takes its own section (`scenarioNetTakePending(state, ...)`). The log lines
  the network smokes require keep their text, naming the first section's state
  (`SCENARIO.STATE: sent scenario=N rev=R reason=baseline sections=1`).
- Tests: the section list's round trip and refusals in
  `tests/test_net_scenario_state_wire.cpp`, the states and the wiring in
  `tests/test_mode_graph_program.cpp` (`[slice5]`), the protocol pins. In the
  game: the network mode smokes (`mp_<mode>_net_smoke`, `_net_rematch_smoke`,
  `_reconnect_smoke`) in the finalization batch.

## Slice 6: waiting for decision D-055 (2026-11-21)

A mod's own mode (its private mode number, the picker, a worked example) needs
three product calls first: what the worked example plays like (the hill of
King of the Hill with the briefcase of Hold the Briefcase: whose score counts,
or whether they add up), whether saved setups remember a mod mode, and how the
Room offers mod modes in a networked room. They are decision D-055 in the
Workbench (my pick: scores add up, Room only, no setup file change). Slices 5
and 7 do not depend on it.

## Slice 7 as written (Multiplayer and Rules, 2026-11-21; not run in the game yet)

The simulants' mode commands become modules, chosen by two new events (runner
kinds 76 and 77; 73 modules in all):

| Event | Chooses | Was |
|---|---|---|
| `event.mode.bot_command` | `og.mode.{htb,htm,pac,koh,ctc}.bot_command` | `botTickUnpaused`'s per-mode command choice for a simulant whose team has no humans (every 20 to 60 seconds) |
| `event.mode.bot_idle` | `og.mode.{htb,htm,pac,ctc}.bot_idle` | its per-mode action for a simulant with no command (follow a teammate while carrying the briefcase or being Pop a Cap's victim, take a case home, go to the terminal with the uplink) |

- The per-mode bodies moved verbatim into `bot.c` functions
  (`htbBotChooseCommand`, `ctcBotIdleAction`, ...), named by the
  `g_MpScenarios[]` rows (`botcommandfunc`, `botidlefunc`); `bot.c` calls
  `scenarioBotChooseCommand` and `scenarioBotIdleAction`, which run the plan's
  modules and record `bot_command` and `bot_idle` in the mode events. A mode
  with no idle module, or whose modules find nothing, leaves the simulant to
  fight as in Combat, as before.
- The meta kind moves to `_modegraph_outside_bots` (the base programs gain the
  modules; installs re-extract meta archives once).
- Still keyed by mode number in `bot.c` (17 checks): how each mode's commands
  are carried out once given (`AIBOTCMD_GETCASE` and the others, a human can
  give them too), which pickups a carrier goes for, King of the Hill's hill
  checks while aiming and the Hacker Central uplink checks. They are the
  commands themselves rather than the mode's choice of them, so they can
  become modules with parameters once a mod mode (slice 6) needs them.

## Questions for MULTIPLAYER

1. Is `scenarioNetIsAuthority` the one test of "this peer decides"? The
   client still runs the pickup body (`propPickupByPlayer`,
   `botPickupProp`), and the Capture the Case capture scores inside it. For
   rule events to be the host's alone, that score would move to the host's
   state. Is that already planned?
2. Which of the mode smokes are stable enough to carry a parity line each?
3. When your host and presentation split lands, does it move the
   `...NetPresent` code out of the `.inc` files? Slice 3 names presenters
   by the functions that exist then.

## Not in scope

- The match setup menus and the Room's mode picker: they already read the
  mode list from the catalog.
- Challenges and presets (`mpconfigs.c`): they name a mode number; they
  follow when a mode has a catalog id everywhere.
- New rules that no base mode has (a round system, a shop): a later row,
  once the base modes are programs.
