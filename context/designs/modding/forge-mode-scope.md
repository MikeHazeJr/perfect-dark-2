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

First run, 2026-11-17 03:28 UTC (`forge_mod_level_load_smoke`, 11/16, on
`week-pd/mods-v4` 15837054 and base 7; RUN, the lines are from the client
log). Everything up to the stage load works: the mod author saves Complex as
`mod:arena_forge_test` (two archives, two ids), the catalog registers it
(`type=14 enabled=1 bundled=0 category='Classic'`, the template's category),
the match start's preflight passes (`stage='mod:arena_forge_test'
scenario='base:combat'`), and the arena's stage number is minted and
activated (`CATALOG RESOLVE: activated stage 0x60`). Then items 1 and 2 stop
it, in this order:

```
WARNING: [CATALOG-ERROR] catalogResolveStageForType: 'mod:arena_forge_test' not found or wrong type (expected=1)
LOAD: lv.c entering stage load sequence for stagenum=0x60
ERROR: [CATALOG-FATAL] catalogGetStageResultByIndex: stageindex=87 not in catalog
ERROR: FATAL: ASSET.FALLBACK: Scenario stage '?' cannot use legacy tiles id=0 after public .pdscenario source activation failed for '(missing scenario source)' (tile source compile failed)
```

Item 6 was not reached. The smoke stays red until CORE's two decisions are
built; it is the proof for F1 when it passes.

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

## F2 back end: the level placements document (WRITTEN 2026-11-17)

F1 waits for CORE's two decisions. The part of F2 that needs neither the game
nor CORE is the data Forge edits, as a module of its own with unit tests:
`port/src/forge_level_doc.cpp`, interface `port/include/forge_level_doc.h`,
cases `[forge-level-doc]` in `tests/test_forge_level_doc.cpp`. Text in, text
out: no game state, no file access. First compiled and run by hosted CI; the
header's comment is the reference for its rules. Not wired to the editor yet.

What it does:

- Reads five members of a level's scenario by schema name: `pads.json`
  (`pd2.scenario.pads.v1`), `spawns.json` (`pd2.scenario.spawns.v1`),
  `objects.json` (`pd2.scenario.objects.v1`), `volumes.json`
  (`pd2.scenario.volumes.v1`, each pad's bounds in the world) and
  `setup.fields.json` (`pd2.scenario.setup.fields.v1`). A wrong schema,
  a row with no id, a repeated id, a pad out of sequence, or a spawn, object or
  bounds row naming a pad the level does not have is refused with the reason.
  Nothing is repaired.
- Rows keep their bytes. A row nobody touched is written back exactly as it
  was read, with every key the module does not know; a document with no edit
  gives back the texts it was given. One value inside a row is replaced in
  place (a spawn's `pad_ref`), so the rest of that row keeps its order and
  layout too.
- Adds a spawn, moves a spawn, removes a spawn, and hands the mod author one
  override per changed member (`forgeLevelDocOverrides`: `<scenario
  member>::pads.json` and so on). A new pad comes with its bounds row, and a
  pad moved in place takes its bounds with it. A unit case saves the example
  arena with a new spawn through `modAuthorSave`, reads the saved level back,
  and checks its navigation record against its files.
- Moves an object and duplicates one (written after the survey of
  `setup.fields.json` below; `setup.fields.json` is the document's fifth
  member). A move changes the object's pad in `objects.json` and in its pad
  field (`base.pad` or `character.pad`), so the two files never disagree; the
  document refuses a level in which they already do. A duplicate is the next
  command: every field row of the original is copied under the next id, whose
  number is its place in the command list, on a pad of its own. There is no
  remove for objects (see below).

What a save must keep true, found on 2026-11-17 by running the archive
conformance tool on a level with one pad added and by reading the runtime
(`scenario_source_runtime.c`, near "source counts mismatch"):

- **The scenario's navigation record must match its files.**
  `_meta/generated-navmesh.json` repeats the row count of six tables (pads,
  volumes, waypoints, waygroups, covers, paths) and a hash of eleven members
  (`scene.glb`, `collision.obj`, `navigation.ini`, `portals.json`, `pads.json`,
  `spawns.json`, `volumes.json` and the four navigation tables). Before it uses
  a level's navigation the game looks for both lists as exact one-line text
  and refuses the level when either differs. A `_meta` member cannot be
  overridden, so the archive rebuild derives it: `cloneNavRefresh` in
  `asset_archive_clone.c` rewrites the two lists from the members the rebuilt
  scenario holds, in the extractor's form, whenever they are no longer true.
  A plain clone changes none of those members (none holds a catalog id), so
  its record stays as it was.
- **Every pad has a bounds row.** In every base level `volumes.json` has one
  `volume_pad_NNNN` row per pad (the pad's place, a hundred units each way
  when the pad has no box of its own), and the extractor writes the two counts
  as the same number.
- **Four digits.** The conformance tool wants a spawn's id and a spawn's pad
  written `spawn_NNNN` and `pad_NNNN` with exactly four digits, so the document
  stops at 10,000 pads and at spawn number 9,999. The largest base level has
  under 2,000 pads.

What the base levels taught (a read-only survey of all 132 extracted levels of
base 7: 41,102 pads, 704 spawns, 15,940 objects; `survey_placements.py` in the
session scratchpad), and what the game's readers require
(`scenario_source_runtime.c`, the pad rows near line 6283 and the spawn rows
near line 2910):

- Every file has one key order and the extractor's layout: pads and spawns one
  row per line, objects one key per line, line feeds, a final newline.
- **A pad's number is its place in the file.** The reader refuses a file whose
  n-th row is not `pad_n`. So pads are only ever added at the end and never
  removed; a removed spawn's pad stays.
- A pad row needs `pad_ref`, `room_ref`, `liftnum`, `flags`, `position`, `up`,
  `look` and a box (`min`, `max`). `room_ref` is empty in every base row, so a
  new pad's is empty too and the caller does not supply a room. The flags of
  base pads are the old axis-alignment bits; a new pad is written with no
  flags (`0x00000`) and its facing as a free direction.
- A spawn row needs `spawn_id` and `pad_ref`; `team` is `any`, `default` or
  `team_N`. Base rows carry `"position": null, "look": null`: the pad holds the
  place.
- 299 pads are shared by more than one spawn or object. Moving something that
  stands on a template pad therefore gives it a new pad and leaves the old one;
  only a pad the document added is moved in place.
- 4,470 objects stand on no pad (an empty `pad_ref`). They are kept and cannot
  be moved.
- Spawn ids are `spawn_NNNN` and object ids `setup_NNNN` with no repeats. A new
  spawn takes the number above the highest the document has ever held.

Not done:

- Removing objects, and adding one of a kind the level does not have yet. An
  object's kind and fields are rows of `setup.fields.json` (37 rows for one
  weapon pickup of Complex). Removal has to renumber every later record and
  rewrite every reference to them, or switch the record off where it stands.
  A new kind needs that kind's rows written from nothing (a duplicate only
  copies what a level already has), and `level.graph.json` events name objects
  by `record_id`, which the module does not read. A first version of the
  module moved objects by changing `objects.json` alone; the game applies that
  file after `setup.fields.json`, so it would have worked, but it left the two
  files disagreeing and was replaced.
- What the next piece (objects) has to respect, from a survey of the 132 base
  levels' `setup.fields.json` (15,940 objects, up to 21,240 field rows in one
  level) and from the runtime, READ 2026-11-17:
  - `objects.json` and the fields file agree about every object's pad (no
    disagreement in base data). The pad field is `base.pad` for 10,053 objects
    and `character.pad` for the 1,750 character spawns; other pad-typed fields
    (`lift.stops[n].pad`, `pad_effect.pad`, `autogun.target_pad`,
    `cctv.look_at_pad`, `camera_position.pad`) are other places an object
    names, not where it stands.
  - **A record's id number is its place in the command list.** `command.order`
    runs 0 to n-1 in every level and equals the number in `setup_NNNN`, and the
    runtime turns a reference to another record into a relative offset from it
    (`s_setupRecordRefOffset`: the referenced id's number minus the referring
    record's order). Records refer to each other a lot (`tag.target` 2,724
    times, `monitor.owner`, lift and door links, `rename_object.target`,
    `conditional_scenery.trigger`). So a new object can be added at the end
    (id and order n) without touching anything else, and an object can be
    moved, but removing one from the middle would shift every later record
    under the references that point at them. Removal needs renumbering with
    every reference rewritten, or a way to leave the record in place and
    switch it off; it is not in the next piece.
  - In a multiplayer arena a weapon pickup does not name a weapon: its
    `pickup.weapon` is a slot of the match's weapon set (`mp_location_00`),
    with ammo crates beside it. "Add a weapon" in such a level means another
    spot for a slot, which is a copy of an existing pickup record at a new
    pad. So the first way to add an object is to duplicate one.
- A run that shows a new spawn used in a match. READ in the runtime, it is
  enough to add the row: `s_setupBuildStageBlock` (`scenario_source_runtime.c`)
  builds the stage's intro script from `spawns.json`, one `INTROCMD_SPAWN pad
  team` per row, then the commands of `intro.json`. So `intro.json` does not
  have to name a new spawn. F2's smoke proves it once a level loads (F1).
- The editor: opening a level fills Forge from this document, and Save calls
  the mod author with its overrides. The two calls exist (next section); the
  editor's own lists and buttons are not connected to them yet.
- The local sweep over every base level (`[forge-level-doc]` with
  `PD2_BASE_TEMPLATE_DIR` set) runs in a game turn, not in hosted CI.

## F2 game side: open a level, save a level (WRITTEN 2026-11-17, not run)

`port/src/forge_level_io.c`, interface `port/include/forge_level_io.h`. This is
the part that knows the game; the document above does not.

- `forgeLevelOpen(arena id)` finds the archive the arena in the catalog came
  from (`modAuthorRuntimeTemplatePath`, the lookup the other creators use),
  takes the scenario member's name from the arena's `scenario_archive` (the
  text after the last `::`), reads the five members out of the scenario inside
  the archive and hands them to the document. It refuses an arena that is not
  in the catalog, one whose archive is not a file on disk (a level inside a
  packed `.pdmod` cannot be a template yet, as for every other family), and
  files the document refuses.
- `forgeLevelSaveAs(document, name)` saves a level mod made from the archive
  the level was opened from plus one override per changed member, through
  `modAuthorRuntimeSave`: save, rescan, enable, apply. The level is in the
  catalog when it returns.
- `--debug-forge-level <arena id>` with `--debug-forge-level-name <name>`
  (`main.c`, right after the character tool's flags) makes those two calls
  with a fixed edit between them: a player start 40 units in front of the
  level's first one, and a copy of its first weapon pickup 60 units in front
  of that pickup. Every step logs a `FORGE.LEVEL.*` line.
- `forge_level_edit_smoke` is F2's proof: it edits base Complex (226 pads, 19
  starts, 35 objects, read from base 7) that way, saves it as `Forge_Edit`,
  starts a match on `mod:arena_forge_edit` in the same launch, and requires
  the game's own lines for the level it loaded: a navigation record of 228
  pads and 228 volumes, and a setup of 36 records and 20 spawns. It does not
  look at the world, so it does not show that the copied pickup is drawn where
  it was put or that anyone started on the new spot.

Not known until a run: whether a pad the document added (no room, no flags)
gets its room the way a base pad does (READ: a player start with no room has
its room found from its position, `player.c` near "Final room validation"),
and whether a save over a level that is its own template (editing a Forge
level and keeping its name) is safe; the smoke's template is a base arena.

## F2 in the editor: the Level File section (WRITTEN 2026-11-17, not run)

The open level is a session owned by `forge_level_io.c`, one document at a
time, so the editor, the debug flags and later the Modding Hub's Levels tool
work on the same one: `forgeLevelSessionBegin` (a Forge session started on
this arena; any open document is closed), `forgeLevelSessionOpen`,
`forgeLevelSessionDoc`, `forgeLevelSessionSaveAs`, `forgeLevelSessionClose`.

- **Which level.** The Grid's Enter names the arena after a start succeeds
  (`gridCommitEnter` in `pdgui_menu_mainmenu.cpp`); every session start first
  clears the name (`pdgui_menu_forge.cpp`), so a session started any other way
  (the blank map, a random pick, the CI Training default) has no level file.
- **The section** is at the top of the editor's Setup tab
  (`forgeDrawLevelFileSection` in `pdgui_forge_editor.cpp`, sidebar row "Level
  File"): Open Level File; Add Start Here, Move Start Here, Remove Start; Move
  Object Here, Copy Object Here; a name and Save As Level; Close Without
  Saving (asks twice when there are changes). Everything is placed where the
  editor camera is, facing the way it faces, flat (a pad's look becomes a
  player's turn angle as `atan2f(look.x, look.z)`, the camera's own
  convention). The two lists show each start's team and place and each
  object's kind, model or weapon, and pad.
- **What it is not yet.** Nothing in the document is drawn in the world, so
  the lists are the only view of where things stand. The editor's older parts
  (its own object list, `map.json`) are untouched and still separate; the old
  save now says that only the editor reads it.
- **Starting a session from the command line.** `--launch-forge-level <arena
  id>` starts a Forge session on a level as The Grid does
  (`forgeLevelStartForge`: a clean match setup on the level, then the stage
  change). `--debug-forge-session-save <name>` then waits two seconds into the
  level, adds a start where the camera is, saves the level under the name,
  lets the game run three more seconds and ends a smoke run. Both are driven
  from `pdguiForgeEditorTick` (`bootForgeLevelTick` in `main.c`).
- `forge_session_save_smoke` is the proof of the part no run has done: the
  mod author's save, rescan, enable and apply while the level it was made
  from is loaded and the editor is up. It requires the save's lines, the new
  level's scenario registered (`PDARENA.SCENARIO`), and the game still running
  afterwards. It does not use the mouse, so the section's widgets themselves
  are unproven until someone opens the editor.

Not known until a run: whether a level loaded for Forge straight from the boot
stage (no agent chosen, no match started) has a player for the session to
attach to, which is what `--launch-scenario empty_map` relies on for CI
Training; and whether applying mods with a level loaded disturbs it.
