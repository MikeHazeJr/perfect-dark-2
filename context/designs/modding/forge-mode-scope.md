# Forge mode: scope for level authoring in the in-game mod tools

Status: SCOPED 2026-11-16 by MODS, at Mike's request ("Forge mode is yours. It
gates the level mod. Scope it and start on it once the merge and V4 are
moving, built so levels can be authored in the in-game mod tools, with level
data inside the level's .pd archive"). Nothing here is built yet. The older
design, `forge-level-editor.md`, describes the editor's features; this document
says what Forge must become for a level to be a mod like any other, in what
order, and what is out of the first version.

READ, by a read-only survey of `week-pd/mods` at e949cc48 (file:line as it
reported them; not re-read line by line). Runtime claims marked "inferred" in
the survey were not confirmed by a run.

## Where Forge is today

- Forge is "The Grid": a session laid over an arena that is already loaded,
  with two modes switched in place, free-fly editing and playtest
  (`src/game/forgemode.c`). The player enters from the main menu's The Grid
  button; there is no boot flag for it except `--launch-scenario empty_map`.
- The editor places objects from its own static list of 141 entries
  (`port/src/forge/forge_core.c`), which is not connected to the real asset
  catalog: its ids exist nowhere else, so placed props, pads and doors do not
  resolve at playtest (inferred). In practice only spawns, bots and zones
  appear.
- Editing is by lists and number fields. Placed objects are not drawn while
  editing, there is no picking and no gizmo, and rotation and scale are ignored
  at playtest. Undo covers place, delete and duplicate only.
- Save writes a loose folder, `mods/Forge Maps/<slug>/` with `mod.json` and
  `map.json` (`port/src/forge/forge_serialize.c`). Only the editor reads it
  back, and the load drops each object's properties, all logic, the game type
  and the settings, and gives every object a new id.
- A saved map is not a level: it has no catalog entry, does not appear in The
  Grid's list or in match setup, and cannot be sent to another player.
- No test or smoke covers Forge.

## What a level is

A base level is a typed archive: a `.pdarena` (`arena.ini`, its manifest) with
the whole `.pdscenario` embedded in it. The scenario holds the geometry
(`scene.glb`, collision, rooms, portals), the setup (`pads.json`,
`spawns.json`, `objects.json`, `intro.json`, `volumes.json`, `stage.json`,
`objectives.json`), navigation, AI lists and `level.graph.json`
(`port/src/romextract_pdarena.c`; readers in `port/src/scenario_source_runtime.c`).
The scanner gives a mod's arena a private stage number (32 slots, 0x60 to
0x7f), and the in-game mod author already saves a `.pdarena` mod from a
template with overrides that reach members of the embedded scenario. No game
run has loaded a mod level yet, and the survey below shows why none could: the
stage lookup accepts only map rows, and the scenario inside a mod arena is never
registered.

## The target

A level made in Forge is a `.pdarena` mod, saved by the in-game mod author,
with every piece of its data inside that archive in the formats base levels
use. It appears in The Grid and in match setup under its own name, loads
through the catalog on its own stage number, and travels to other players the
way any typed mod does. Forge edits the level's own data: what it opens is the
scenario's pads, spawns, objects and setup, and what it saves is those same
members. `map.json` and the `Forge Maps` folder go away.

## First version: placements on a level's geometry

The first version authors everything that sits on a level, not the level's
walls and floors. Geometry comes from a template:

- a base arena (a variant of an existing level; local only, because it embeds
  ROM-derived geometry, BYOR);
- the blank canvas;
- imported geometry: a scenario built from an external model by the standard
  source pipeline (step F5), which is how an original level such as the Maian
  arena gets its own walls and can be shared.

Building rooms and walls inside the game is out of the first version (see
"Later").

## Steps

Each step ends in something a run can prove. F1 and F2 are the foundation;
nothing else is worth building before them.

- F1. A mod level loads. The mod author saves a base arena as a named mod
  level; it shows in The Grid and in match setup under that name and a match
  starts on it. What stands in the way is in "F1 work list" below: no stage row
  and no registered scenario for a mod arena, a `load_mode` read as a number,
  and no name of its own. Proof: a smoke that builds the level from a recipe
  and spawns in a match on it.
- F2. Forge opens and saves the level's own data. Opening a level fills the
  editor from the scenario's pads, spawns, objects and intro setup; Save writes
  those members back through the mod author as a new mod level or an update of
  one. A new object gets a pad with its position, facing and room (the stage is
  loaded while editing, so the room is looked up there). Ids are stable across
  save and load, so nothing is lost on a round trip. Proof: pd-tests for the
  round trip on base scenarios, and a smoke that adds a spawn and a weapon,
  saves, restarts and finds both in a match.
- F3. Real things to place, placed in the world. The placeable list comes from
  the asset catalog (props, weapons and ammo, spawn points, the game modes'
  markers such as hills and cases), so what is placed is what spawns. Objects
  are drawn while editing, picked in the view and moved with a gizmo; rotation
  and scale are honoured; transforms can be undone.
- F4. The Modding Hub's Levels tool: choose a template, open it in Forge, name
  it, save, validate, and see the levels made so far. This is where level
  authoring joins the other in-game mod tools.
- F5. Imported geometry: a level template from an external model (scene,
  collision, rooms, generated navigation), then placements in Forge. The Maian
  arena level needs this step.

## Later, not in the first version

- Logic, zones and game types: Forge's 44 logic operations, waves and
  objectives become nodes of `level.graph.json` and rows of the scenario's
  files, run by the scenario runtime. Today they are editor data with a thin
  runtime; carrying them over is a design of its own with CORE.
- Building geometry in the game (rooms, floors, walls), with collision, rooms,
  portals and navigation generated from it.
- Editing a level together over the network. Playing a Forge level over the
  network needs nothing new once F1 holds, because typed archives already
  travel with a match.

## Choices made here, for Mike to overrule

1. The first version places things on existing or imported geometry and does
   not build geometry in the game. It is the shortest path to a real level mod,
   and the Maian arena's geometry is better made as a model, as the vehicle's
   was.
2. A level is always a `.pdarena` with its scenario inside, never a second
   format. Old `Forge Maps` folders are not converted: the format lost data on
   every load, so there is nothing reliable to convert.
3. Logic, zones and waves wait until the placements are solid.

## Rules this work keeps

- BYOR: a level cloned from a base arena is local only; nothing ROM-derived is
  ever shipped. An original level (imported geometry) can be shared.
- Members a Forge level does not need stay optional, so a level that leaves
  them out still loads (Mike, 2026-11-15, `constraints.md`).
- The scenario formats are CORE's. Forge writes the existing schemas
  (`pd2.scenario.pads.v1`, `spawns.v1`, `objects.v1`, `intro.v1`, `volumes.v1`)
  and asks CORE before any change to them.
- Every object Forge writes keeps a `record_id` in `objects.json` (CORE,
  2026-11-16 22:53 UTC, `object-events.md` on week-pd/core). A level graph is
  gaining an executable part (`event.*`, `gate.*` and `og.*` nodes with an
  `edges` array, compiled for every stage, arenas included), and its events on
  pickups, doors and used objects name an object by that id. A stage whose
  level program does not compile is refused, so a Forge level's
  `level.graph.json` must stay a valid one (a level with no program is valid).
- Every level graph carries the level's pickup and door rules (CORE's notice,
  2026-11-17 01:37 UTC, week note schema table; object events slice 4, built on
  `week-pd/core-s4`, not merged yet): ten `object.rules.*` nodes and seven
  execution edges that handle `event.pickup.touched`, `event.door.use` and
  `event.door.approach` (the exact JSON is in `object-events.md`, "Slice 4
  design"; the mod packer's level graph template writes it). A level graph that
  does not handle all three is refused at activation and by conformance, so
  every level Forge or the mod author makes must carry that wiring. A level
  cloned from a base arena gets it with its template's graph once the base is
  extracted again with CORE's extractor; a level from the blank canvas or from
  imported geometry needs it written. An author changes a rule by changing the
  wiring.
- A level that fails a rule is refused when it is saved or registered, with the
  reason, never repaired silently or half loaded.

## First piece of work

F1: prove a mod level loads. Every later step stands on it. It is not small:
the survey below found that a mod arena cannot load today.

### F1 work list (read-only survey of `week-pd/mods` at 2fdbb7f8, 2026-11-16)

READ in code (file:line as the survey reported them; not re-read line by line):

1. No stage row. A custom stage number gets a cloned `g_Stages` row
   (`s_mintCustomStagenum`, `assetcatalog_scanner.c:1658`), but every stage
   lookup goes through `catalogResolveStage`, which accepts `ASSET_MAP` rows
   only (`assetcatalog_api.c:296`, `689`, `1440`; callers in `bg.c`, `setup.c`,
   `tilesreset.c`, `lv.c:187`). A mod arena has no map row, so the load stops at
   a catalog fatal.
2. The scenario is not registered. Nested registration exists for effects,
   weapons, themes and vehicles only (`assetcatalog_scanner.c:5425`); the arena
   row keeps just `scenario_id` and `scenario_archive`. The scenario runtime
   finds a stage's scenario by stage number among `ASSET_SCENARIO` rows
   (`s_findScenarioForStage`, `scenario_source_runtime.c:7342`) and never reads
   the arena's `scenario_id`; a miss is fatal. Its fallback id
   (`<ns>:scenario_<stage slug>`) cannot match the id the mod author gives a
   cloned scenario (`mod:<slug>__scenario_<name>`, `asset_archive_clone.c:388`).
3. `load_mode` is read as a number (`iniGetInt`), and the extractor writes its
   name, so every mod arena reads as playable, a canvas template included
   (`assetcatalog_scanner.c:2343`, `romextract_pdarena.c:9666`).
4. A mod arena has no name of its own. Arenas have no display-name field
   (`assetcatalog.h:255`), and the mod author's arena family has no name key
   (`mod_author.c:55`), so a saved level shows its template's name (a second
   "Complex") and category. The Grid and match setup skip an arena whose name
   is empty; the Room lists it as "???".
5. `--boot-stage` sends any stage number above 0x5f to the title screen
   (`main.c:4638`). `--launch-mp-room <arena id> <game mode id> <bots>` with
   `--debug-auto-start-match` is the way a smoke starts a match on an arena by
   catalog id, and it takes custom stage numbers.

INFERRED, to check in the first run:

6. `s_copyMemberPath` cuts a member path at its first `::`
   (`scenario_source_runtime.c:7242`), so `intro.json`, `stage.json`,
   `rooms.json` and `room_lights.json` of a scenario nested in an arena would be
   looked for in the outer `.pdarena`. `fs.c` itself reads nested chains.

Built since the survey, on `week-pd/mods-v4` (2026-11-17; compiled and unit
tested in hosted CI, not yet seen in the game because the level cannot load
until items 1 and 2 are done):

- Item 3 (B-1235): both readers of `arena.ini` read `load_mode` by name
  through `catalogArenaLoadModeFromIni`; a number is still read.
- Item 4: a level has its own name. The mod author's arena family writes
  `display_name` into `arena.ini` (its `name_key`), both readers of
  `arena.ini` copy it into `ext.arena.display_name`
  (`catalogArenaDisplayNameFromIni`), and the four arena lists show it in
  place of the template's language string: the Room (`catalogArenaCollect`),
  the match setup (`mpsetupArenaCollect`), and the Grid's two lists in
  `pdgui_menu_mainmenu.cpp`, all through `arenaEntryName` or the same test.
  Not done for the name: text shown during a match that is looked up by
  stage number and not from the catalog entry (to check once a level loads).

What already works: a save rescans, enables and rebuilds the catalog with no
restart (`modAuthorRuntimeSave`; the author never writes `requires_restart`),
and the Grid's list is invalidated; the pickers put non-bundled arenas in a Mod
section and do not filter them out.

Split proposed to CORE (items 1, 2 and 6 are in the catalog core and the
scenario runtime, which are CORE's): MODS registers the embedded scenario with
the arena's stage number and gives the arena its own name (a `display_name` key
the pickers read before the language id; the mod author writes it) and a named
`load_mode`; CORE decides how a custom stage resolves (a map row minted with the
stage number, or a lookup that accepts an arena) and how the runtime opens a
nested scenario's members. MODS writes the smoke: a recipe saves Complex as
"Forge Test", then `--launch-mp-room mod:arena_forge_test base:combat 3
--debug-auto-start-match`, requiring the scenario's intro line and the player's
spawn.
