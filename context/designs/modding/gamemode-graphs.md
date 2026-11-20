# Combat Simulator modes as graphs

Status: CORE, 2026-11-19. A survey and a plan for the week note's "Combat
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
