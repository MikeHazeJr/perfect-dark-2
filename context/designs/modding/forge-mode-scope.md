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

- Reads six members of a level's scenario by schema name: `pads.json`
  (`pd2.scenario.pads.v1`), `spawns.json` (`pd2.scenario.spawns.v1`),
  `objects.json` (`pd2.scenario.objects.v1`), `volumes.json`
  (`pd2.scenario.volumes.v1`, each pad's bounds in the world),
  `setup.fields.json` (`pd2.scenario.setup.fields.v1`) and `level.graph.json`
  (`pd2.level.graph.v1`, kept whole; see the graph rule below). A wrong schema,
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
- **The level's graph has one trigger volume node for each bounds row**
  (found 2026-11-17 06:15 UTC by the F5 survey, before any run; READ in
  `scenario_source_runtime.c` near "trigger volume node count differs from
  volumes.json rows"). At activation the game counts the text `"kind":
  "scenario.trigger.volume.source"` in `level.graph.json` and refuses the
  level unless that is the number of rows in `volumes.json`. The first
  versions of the document added a bounds row with every pad and left the
  graph alone, so **every level Forge saved with a new pad would have been
  refused by the game**, and nothing short of a run would have shown it: the
  archive conformance tool does not check this rule, and hosted CI passed.
  `level.graph.json` is now the document's sixth member. A new bounds row
  comes with its node (`{ "id": "trigger.volume.NNNN", "kind":
  "scenario.trigger.volume.source", "table": "volumes.json", "volume": ...,
  "pad": ... }`, put after the last such node) and the link the extractor
  gives every such node (`{ "from": "scenario.load", "to": ... }`, with the
  `from` of the graph's own last such link). The graph's `counts` of pads,
  volumes and objects follow the tables when they were true to begin with.
  A level whose two counts already differ is refused when it is opened, as
  the game would refuse it. Every other byte of the graph is kept; a Python
  run of the same edit on the example's and Complex's real graphs gave valid
  JSON with nothing else moved.
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

- Removes an object by leaving it out, and puts it back
  (`forgeLevelDocSetObjectRemoved`, written 2026-11-17 after the survey and
  the read of `setup.c` below). An object's second flags word holds four
  bits, one per difficulty, that leave it out of the level
  (`OBJFLAG2_EXCLUDE_A`, `_SA`, `_PA`, `_PD`, 0x10 to 0x80). The setup loop
  (`setup.c`, `diffflag |= 1 << (lvGetDifficulty() + 4)`, then `(obj->flags2 &
  diffflag) == 0` in every object type's case) does not create an object whose
  word has the bit of the difficulty being played, in every mode. Its second
  pass tests for a prop before it links records (READ for objects inside
  another, lift doors, safe items, padlocked doors and conditional scenery;
  linked guns are paired without that test, as they are today when a
  difficulty leaves one of a pair out). Removing sets all
  four in both files (`flags2` in `objects.json`, `base.flags2` in
  `setup.fields.json`); the record keeps its place, so no reference shifts.
  Putting back restores the bits the object had, and the exact text when that
  is the word that was read. Not removable: a lift or an escalator step (the
  loop relaxes their filter on six levels in multiplayer), a character (its
  word is `character.flags2`, which that loop does not test), and a record
  whose word is missing from either file or differs between them. An object
  that is left out is not copied.
  - Survey behind it (66 arena and mission archives of base 7, 10,374 objects
    with a flags word): the two files agree about the word for every object
    (`base.flags2` 8,645 times, `character.flags2` 1,729, type `u32_hex`,
    written `0x%08x`), and **no base object has all four bits**. So "left out
    everywhere" is a state the game's code allows for but its levels never
    used; no run has shown it yet.

Not done:

- Adding an object of a kind the level does not have yet. An object's kind
  and fields are rows of `setup.fields.json` (37 rows for one weapon pickup of
  Complex). A new kind needs that kind's rows written from nothing (a
  duplicate only copies what a level already has), and `level.graph.json`
  events name objects by `record_id`, which the module does not read. A first
  version of the module moved objects by changing `objects.json` alone; the
  game applies that file after `setup.fields.json`, so it would have worked,
  but it left the two files disagreeing and was replaced.
- Deleting a record outright. It would have to renumber every later record
  and rewrite every reference to them; leaving it out does the job without
  that.
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
  level's first one, a copy of its first weapon pickup 60 units in front of
  that pickup, and its first ammo crate left out. Every step logs a
  `FORGE.LEVEL.*` line.
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
  Object Here, Copy Object Here, Remove Object and Put Object Back; a name and
  Save As Level; Close Without Saving (asks twice when there are changes).
  Everything is placed where the editor camera is, facing the way it faces,
  flat: the camera's yaw is the player's turn angle t, a player with turn
  angle t faces (-sin t, 0, cos t) (`bondmove.c`), and that is the look a new
  pad is given, which the game turns back into the same angle when someone
  starts there (`player.c`). The two lists show each start's team and place
  and each object's kind, model or weapon, and pad.
- **Markers in the world (the first piece of F3).** While the editor camera
  flies, the HUD marks each player start (a diamond) and each object that
  stands on a pad (a square) on the picture, named when within 2,500 units,
  the selected one in yellow, a removed one grey
  (`forgehudDrawLevelFile` in `pdgui_forge_hud.cpp`; a switch in the section).
  They are drawn over the game's picture and under every window, and they
  show through walls. The place comes from the game's own projection:
  `forgeProjectWorldPos` (`forgemode.c`) applies the current player's view
  matrix and `cam0f0b4d04`, exactly as the game's target boxes do (`prop.c`,
  `menu.c`), and answers in the game's framebuffer units; the HUD scales by
  window size over the renderer's native size (`videoGetNativeWidth` by
  `videoGetNativeHeight`), the same scale the renderer gives the 3D viewport
  (READ: `gfx_adjust_viewport_or_scissor` in `gfx_pc.cpp` multiplies the
  viewport by output size over the native size, with no aspect squeeze for
  the 3D view).
- **What it is not yet.** No picking in the view, no gizmo, and a marker is a
  flat sign, not the object's model. The editor's older parts (its own object
  list, `map.json`) are untouched and still separate; the old save now says
  that only the editor reads it.
- **Starting a session from the command line.** `--launch-forge-level <arena
  id>` starts a Forge session on a level as The Grid does
  (`forgeLevelStartForge`: a clean match setup on the level, then the stage
  change). `--debug-forge-session-save <name>` then waits two seconds into the
  level, adds a start where the camera is and saves the level under the name;
  twenty frames later it turns the camera level and to yaw 217 (away from
  every axis), and forty frames after that it logs where known points land.
  It takes no screenshot and ends nothing. Both are driven from
  `pdguiForgeEditorTick` (`bootForgeLevelTick` in `main.c`).
- `forge_session_save_smoke` is the proof of the part no run has done: the
  mod author's save, rescan, enable and apply while the level it was made
  from is loaded and the editor is up. It waits on two new wait conditions of
  the smoke harness (`smoke_readiness.c`, unit-tested): `forge_editor_ready`
  (a live offline stage with the editor camera flying) and then
  `forge_level_file_open`. The second exists because the save holds one frame
  for an unknown time while the script clock runs on: timed from before the
  save, a screenshot and an exit could come due in the same tick and the exit
  would win. The smoke requires the save's lines, the new level's scenario
  registered (`PDARENA.SCENARIO`), and six more seconds of the game running.
  It does not use the mouse, so the section's widgets themselves are unproven
  until someone opens the editor.
- The same smoke checks the markers' projection and the facing math by
  numbers, with nobody looking. At yaw 217 the flag logs
  `FORGE.MARKERS.PROBE`; the smoke requires the point straight ahead of the
  camera, by the reading above, within 5% of the middle of the picture, the
  one ahead and to the right at least 6% right of the middle, the one ahead
  and above at least 5% above it, and the one behind out of view. A mirrored
  projection or a wrong sign in the facing fails that line (at yaw 0 or 180,
  where the session starts on Complex, a wrong sign would not show). The line
  also carries, unjudged, where the other reading of the angle points: the
  one the editor's older fly and placement code uses (B-1238). The harness
  takes one screenshot with the markers on, for a person to look at; whether
  each marker sits on its object is not something the numbers show.

Not known until a run: whether a level loaded for Forge straight from the boot
stage (no agent chosen, no match started) has a player for the session to
attach to, which is what `--launch-scenario empty_map` relies on for CI
Training; and whether applying mods with a level loaded disturbs it.

### An independent read of this code, before any run (2026-11-17)

Everything from the open and save glue to the markers had been compiled by
hosted CI and run by nothing, so a second reader was given the diff
(a3775be7 to f88ff301) and asked for bugs only. It found five, each then
confirmed in the code by MODS, and all are fixed:

1. The session smoke would have been refused by the harness before it began:
   a wait of 200 s plus a screenshot listed at 225 s is past the 300 s
   watchdog, which the harness checks up front. And the runner collects a
   screenshot by its listed time, so one taken by a debug flag at a time of
   its own was the wrong design. The harness now owns the screenshot and the
   exit, timed from `forge_level_file_open`.
2. The world markers would never have been drawn, nor has anything else on
   the Forge HUD while the camera flies since 2026-05-01 (B-1237).
3. A start placed from the camera faced the camera's mirror image for every
   yaw but 0 and 180: the look was written (+sin t, 0, cos t). It is
   (-sin t, 0, cos t).
4. The probe's "right" was the camera's left, for the same reason. It now
   takes the view's own right, up cross back, and the measurement is made at
   yaw 217 so that the sign is tested.
5. A level saved again under its own name could keep an edit that had been
   taken back: the save sent only members that differ from what was read
   when the level was opened, and after the first such save the archive no
   longer is what was read. A save over the level's own archive now writes
   every member (`forgeLevelDocOverridesAll`; the game side asks the mod
   author whether the name gives the level's own id). A unit case does the
   sequence both ways.

The same reading says the editor's older fly and placement math uses the
mirrored angle too. That is recorded as B-1238 and left alone until the probe
has measured it.

Also read on the way (not a fault of this work, recorded with B-1237): the
mouse did not turn the editor camera. Its deltas are zeroed while the
editor's input context is on top (`input.c`), the aim axis the camera reads is
the right stick or the aim keys (`actionmap.cpp`), and the editor's own
bindings take the up, down and right arrow keys for its sidebar. So with a
keyboard and a mouse the camera could hardly be turned at all.

**Mouse look (WRITTEN 2026-11-17, seen by nobody):** hold the right mouse
button over the world, not over a window, and drag. The editor takes the
mouse's movement from ImGui, which still sees it, and turns the camera through
`forgeAddCameraAngles` (0.15 degrees a pixel; a drag that starts over a window
belongs to the window; the cursor stays visible). The footer hint says so. No
smoke can show this: it wants a hand on the mouse.

## F3 back end, first piece: what a pickup gives, what a prop looks like (WRITTEN 2026-11-17, unit-tested, not run)

F3 says the things to place come from the catalog. Copying an object (F2)
gives a level another weapon spot or prop of a kind it already has; these
three edits of the level document make the copy something else. They are text
edits, tested without the game.

How base levels write it (base 8, all 66 arena and mission archives, READ):

- What an object is drawn with is the `base.model` row of `setup.fields.json`
  (`type` `model_catalog_id`, the id in `catalog_id`), repeated in
  `objects.json` as `model_catalog_id`. Its size is `base.extra_scale`; 256 is
  life size (`setup.c`: scale = extrascale / 256).
- What a weapon pickup gives is the `pickup.weapon` row, written one of two
  ways. A weapon of the catalog: `type` `weapon_catalog_id`, the id in
  `catalog_id`, and the same id in `objects.json`'s `weapon_catalog_id`
  (1,627 pickups). Or one of the match's sixteen weapon places: `type`
  `weapon_selector`, `value` `mp_location_00` to `mp_location_15`, and no id
  in either file (260 pickups; every multiplayer arena's weapon spots).
- When a match starts the game gives a weapon place the weapon, the model and
  the size of that place in the match's weapon set (`setupPlaceWeapon`). A
  pickup of a catalog weapon keeps its own record's model and size, and its
  weapon is looked up in the catalog by id (`s_setupResolveWeaponnum`,
  `catalogResolveWeapon`), so a mod's weapon can be named.
- 64 of the 1,905 `pickup.weapon` rows disagree with `objects.json` about the
  weapon. Such a pickup is read and kept, and cannot be changed here.

The edits (`forge_level_doc.h`):

- `forgeLevelDocSetPickupWeapon(record, weapon id, model id, size)`: the
  pickup gives that catalog weapon. Three field rows and two values of the
  object's row change. The caller supplies the model and the size that go
  with the weapon; the document does not know them.
- `forgeLevelDocSetPickupSlot(record, 0 to 15)`: the pickup is a weapon place
  of the match. Its model and size are left, since a place does not use them.
- `forgeLevelDocSetObjectModel(record, model id)`: another model for a plain
  prop (kind `prop`). Other kinds are refused: a door, a lift or a monitor
  needs a model built for it.

An id must have the shape of a catalog id (lower-case letters, digits, `_`,
`-`, `.` and one `:`); every id in the base levels has. Whether the catalog
has the asset is the caller's to check, and the game refuses a level that
names one it does not have.

The game's side of the first one (`forge_level_io.h`, written, not run):
`forgeLevelPickupLook(weapon id)` gives the model and size a pickup of that
weapon is shown with. For a weapon the match's weapon sets offer it is that
weapon's row in the weapon set table (`g_MpWeapons`: what a weapon place
becomes when a match gives it the weapon); for any other weapon, the model it
is held with (`playermgrGetModelOfWeapon`, which knows mod weapons) at life
size. `forgeLevelSessionGivePickup(record, weapon id)` does the lookup and the
document edit on the open level.

Proof prepared: `--debug-forge-level-weapon <weapon id>` makes the scripted
level edit give its copied pickup that weapon, and `forge_level_weapon_smoke`
saves such a level from Complex with `base:dragon` and starts a match on it.
It requires the game's compile of the rewritten rows (a weapon or model id
the catalog cannot resolve refuses the level) and a new line `setup.c` writes
when a match makes a weapon pickup that is not one of its weapon places
(`SETUP: fixed weapon pickup cmd=35 ... prop=1`). It is a smoke of its own so
that a fault in it cannot hide the plain edit proof.

In the editor (written, seen by nobody yet): with a weapon pickup selected in
the Level File section's object list, "What It Gives" says whether it is a
weapon place of the match or always gives one weapon, and offers two changes:
a list of every weapon the catalog has enabled (by catalog id, a mod's
included) with Always Give This Weapon, and a place number with Make It This
Weapon Place.

**A new object where there is none to copy (WRITTEN 2026-11-17, unit-tested,
not run):** `forgeLevelDocAddObject(kind, weapon place, position, facing)`
makes a weapon place or an ammo crate from nothing. An empty level, which is
what a template made from a model starts as, has no object of any kind to
copy, so without this nothing could be placed on it. READ from base 8: every
weapon pickup of the base levels has the same list of 37 field rows (1,887
records, one list) and every multi-ammo crate the same 68 (524 records, one
list); the 180 weapon places of the 18 multiplayer arenas all carry flags
0x00000001 and the stand-in model `base:model_roofgun`, and every crate is
`base:model_multi_ammo_crate` with its nineteen slots empty. The new record is
that list with the values those objects share, life size, on a pad of its own
at the end of the command list. It can then be moved, copied, left out and,
for a place, given a weapon. Its two model ids are the game's own: a level
that names them holds nothing of them. The archive validator in hosted CI does
not know `base:model_*` ids without a base install beside it, so a level
saved with such an object is not handed to that step; the unit case reads the
rows back instead.

In the editor (written, seen by nobody): New Weapon Place Here and New Ammo
Crate Here, above the object list. A new place takes the lowest place number
the level does not use yet.

Not written yet: the same choice for a prop's model (the catalog's list of
models is long and wants a filter; how a mod's prop gets a model the level can
name is not read yet), and any run. A level with a pinned pickup of the crystal pistol in it is the
proof to aim for after this one.

## F5 survey: a level from a Blender model (READ 2026-11-17, nothing built)

A read-only survey of `week-pd/mods-v4` at f88ff301 (file and line as it
reported them; only the trigger volume rule above was re-read by MODS). It
answers what a level needs when its walls come from a `.glb` and nothing in it
comes from the original game.

What exists:

- **Collision and the navigation record need no new tool.** The game builds
  collision at catalog activation from `collision.obj` or, when the scenario
  names no collision file, from `scene.glb` itself (`modasset_compiler.c`
  near 9549, `assetcatalog_load.c` 1382). `_meta/generated-navmesh.json` is a
  stamp of counts and hashes, not a navmesh; the archive rebuild already
  re-makes it. `_meta/generated-collision.json` is read by nothing.
- **Rooms come from the model.** A per-vertex attribute `_PD_ROOM` (a whole
  number, 1 to 32767; 0 is "no room") gives each triangle its room when all
  three vertices agree; in an OBJ, an `o room_N` or `g room_N` line does
  (`modasset_compiler.c` 2758 and 1009). At least one triangle must be in a
  room above 0, or the load stops at `tilesreset.c:51`.
- **Simulants can move without a navmesh.** With empty waypoint, waygroup and
  cover tables the game makes a waypoint and a cover per pad and links them
  from `paths.json`, or pad 0 to pad 1 to pad 2 when there are no paths
  (`scenario_source_runtime.c` 34469, 6689). That ignores walls, so paths
  have to be authored.
- The scene is drawn whole, with textures embedded in the GLB, UVs, vertex
  colours and `alphaMode`; lighting is baked.
- `rooms.json`, `portals.json` (may be empty) and `collision.flags.json` are
  optional. Without `rooms.json` room boxes come from collision triangles,
  and a prop on a pad outside every box is not created (`bg.c` 1712).

What is missing:

1. Export conventions written down (Y up, units, triangles, embedded
   textures, how rooms are tagged) and a Blender step that writes `_PD_ROOM`
   or room-named OBJ objects. Whether Blender's glTF exporter writes that
   attribute in a form the game takes is not known.
2. Writers for `collision.obj`, `portals.json`, `rooms.json` and the seed
   tables (pads, spawns, volumes, paths) outside the extractor.
3. A level graph template that the game accepts: the extractor's full node
   list with the `scenario.navigation.generate` node, one trigger volume node
   per bounds row, and later CORE's `object.rules` wiring. The mod packer's
   template (`"nodes": []`) and the example level's graph are both refused by
   the game.
4. A packer that builds a `.pdscenario` inside a `.pdarena` from those
   sources. Rebuilding a template with member overrides exists, but **there
   is no playable template that is free of the original game's data**: the
   example `tri_arena` is not a playable level (its collision is all room 0
   and its graph lacks the navigation node; smokes only activate it as a
   catalog asset), `mapimport.c` writes a stub no stage can load, The Grid's
   Blank Map is the original game's stage 0x1a, and `--launch-scenario
   empty_map` is CI Training.
5. A smoke that starts a match on such a level.

So the first piece of F5 is a small original level that plays: one room, a
floor and walls from Blender, a few pads and starts, an accepted graph. It is
the "blank canvas" of this document's first version, the template every
shareable Forge level starts from, and the proof of the pipeline before the
Maian arena's geometry goes through it.

Also found: passing `tools/asset_archive_conformance.py` does not mean a level
loads. The tool requires neither the navigation node nor a `collision.obj`
member nor the trigger volume count, and the game requires all three.

Open: the Blender scale that matches game units (a mod's stage copies the
Skedar stage row, scale fields included), whether rooms and portals still hide
props and characters when the scene is drawn whole, whether a portal's vertex
order matters, and whether a pad's room is taken from `room_ref` or from the
room boxes.

## Proven in the game (2026-11-17, turn q_20261117041043360_545fad61)

Client 13dd514d on shared base 8, smoke storage on D:. Receipts and client
logs are under `D:\PD2-wt\pd2-smoke-mods\receipts\`.

- **F1, a mod level loads: PROVEN** (`forge_mod_level_load_smoke`, 17/17).
  The level saved from Complex got stage number 0x60, its embedded scenario
  was registered (`PDARENA.SCENARIO: owner=mod:arena_forge_test
  id=mod:arena_forge_test__scenario_scenario_mp_complex stagenum=0x60`),
  collision was built from its own archive, 45 rooms and 2,247 tiles were
  compiled, and the player spawned in a Combat Simulator match.
- **F2, an edited level plays: PROVEN** (`forge_level_edit_smoke`, 29/29).
  The game's own lines for the level Forge's document saved: `trigger volume
  nodes ... nodes=228 rows=228` (the rule the first survey caught), `SETUP.PADS:
  stage 0x60 pads=228 without_room=0` (the two pads Forge added, which name no
  room, were each found a room), 36 setup records and 20 spawns.
- **F3 back end, a pinned pickup: PROVEN** (`forge_level_weapon_smoke`,
  27/27). `FORGE.LEVEL.EDIT: setup_0035 now gives base:dragon (model
  base:model_chrdragon, size 256)`, then `SETUP: fixed weapon pickup cmd=35
  weapon=15 model=255 prop=1` in the match.
- **F2, a save from inside a Forge session: works; the smoke is red on one
  line** (`forge_session_save_smoke`, 33/34). The session came up from the
  command line, the mod author's save, rescan, enable and apply ran while the
  level was loaded with the editor up, the new level's scenario was
  registered, and the game ran on. The one missing line is the projection
  probe, which measured B-1240: the editor camera's yaw and pitch did not
  reach the view. The screenshot shows the Forge HUD and the level file's
  markers while the camera flies (B-1237 proven) and the markers where the
  objects are for the view the game actually drew.
- The base-level sweep of the level document read all 132 archives of base 8
  for the first time and failed one check on the 46 with no player start
  (B-1239, fixed).
- Not run: the four vehicle smokes. The MODS smoke archive on C: was at its
  2 GiB cap and every one was refused before launch.

Not proven by any of this: that a marker sits on its object to the pixel,
that the panel's widgets work under a hand, that a copied pickup is drawn and
can be collected, and anything about the camera fix (B-1240), the mouse look,
the new weapon place and ammo crate, and the Blank Room. Those are in the next
queued turn.

## F5: the smallest level the game accepts (READ 2026-11-17 by a second survey)

A second read-only survey went through the scanner, the release rules, the
graph activation, the scene compiler and renderer and the stage load, at
e62d5fd0, and wrote the rules below with the file and line of each (MODS did
not re-read them; the builder's checks mirror them, and a run decides). SSR is
`port/src/scenario_source_runtime.c`, SCAN `port/src/assetcatalog_scanner.c`,
POL `port/src/asset_archive_policy.c`, MAC `port/src/modasset_compiler.c`,
REN `port/fast3d/scenario_scene_renderer.cpp`.

Corrections to the first survey: `collision.obj` is needed even when
`scenario.ini` names no collision file (the navigation record hashes it,
SSR 8450); **every** collision triangle must be in a room above 0, not just
one (a room-0 triangle overruns the tile buffer, SSR 36624-36697); `scene.glb`
is parsed by the compiler at activation whatever the collision source
(MAC 4765); and with any row in `navigation/paths.json` a pad on no path gets
no links (SSR 7347).

The archive:

- **Mod folder.** One rejected archive rolls back the whole folder's scan
  (`CATALOG.SCAN.TRANSACTION.ROLLBACK`, SCAN 6086), so a level lives in a
  folder of its own.
- **`arena.ini`**: `[arena]`, `catalog_id`, `display_name` (an arena with no
  name is not listed), `load_mode = ARENA_LOADMODE_PLAYABLE`, `scenario`,
  `scenario_archive`. The scenario is silently not registered when either of
  the last two is missing, when the arena id begins `base:`, or when the two
  ids do not share the text before the first `:` (SCAN 5432). The scenario's
  own `catalog_id` must equal the arena's `scenario`.
- **`scenario.ini`**: fifteen file keys must be non-empty at activation
  (`scene_file`, `level_graph_file`, the tables, `navigation_file`,
  `asset_runtime.c` 864). `source_room_count` is 2 for one room numbered 1.
  Every `*_file` a descriptor names must be a member (POL 797), comment lines
  included, so no commented-out file key.
- **Members that must exist**, with `{ "schema": ..., "rows": [] }` being
  valid for each table: `scene.glb`, `collision.obj`, `portals.json`,
  `pads.json`, `spawns.json`, `volumes.json`, the four `navigation/*.json`,
  `objects.json`, `setup.fields.json`, `ai/ailists.json`, `objectives.json`,
  `navigation.ini`, `level.graph.json`, `_meta/generated-navmesh.json`.
  Optional: `collision.flags.json`, `rooms.json`, `room_lights.json`,
  `stage.json`, `intro.json`. The base blank arena `mp_grid2` has no pads, no
  starts, no AI list and no objects, and plays.
- **Tables are read as flat text**: a key is the first textual match in the
  row, integers are parsed base 0 (a leading zero is octal), vectors are
  `[a, b, c]`.
- **`level.graph.json`** is counted as text. `"kind": "x"` with one space; 427
  kinds exactly once each (five structural ones, 277 AI actions, 145 AI
  conditions), `scenario.trigger.volume.source` once per `volumes.json` row;
  the first `"scenario"` in the text is the id; the twelve table keys first
  occur inside `tables`, which therefore comes before `nodes` and `counts`;
  four exact navigation strings; and since dev fd4c1f03 the ten
  `object.rules` nodes and seven `edges` for pickups and doors. A minified
  graph passes the Python validator and fails the game.
- **`navigation.ini`** nine exact lines, and **the navigation record**'s
  counts and hashes lines character for character (SSR 8480-8574).
- **`scene.glb`**: one embedded buffer, no `extensionsRequired`, no
  `normalized` or sparse accessor, every accessor with a buffer view.
  `POSITION` float VEC3; `TEXCOORD_0` float VEC2; `COLOR_0` only as float
  VEC4; `_PD_ROOM` as an unsigned integer scalar for the renderer (a float
  one is ignored there). Node transforms are honoured; the renderer draws
  both sides; images must be embedded (PNG is the safe choice).
  **Blender's exporter cannot write `_PD_ROOM` as an unsigned integer, and
  its vertex colours are in forms the game refuses**, so a level from Blender
  is exported plain and the builder adds the room attribute and writes the
  collision.
- **`collision.obj`**: `o room_N` (N at least 1) before the first vertex; a
  floor counts only when wound so its normal points up (`meshFindFloor`
  wants normal.y of 0.5 or more); no `mtllib`. `collision.flags.json` has one
  flat row per triangle in file order (base floors `0x001b`, walls `0x001c`);
  without it floors and walls get no sight or shot blocking.
- **Pads**: `pad_ref` numbers equal the row's place; `room_ref` empty means
  "find the room from the room boxes" and `room_1` means that room; never
  `room_0`. Flags 0 for a start. A pad sits above its floor, not on it (base
  starts are about 52 above). `volumes.json` rows are world boxes.
- **Starts**: none are needed in code, 8 to 16 is sensible; the stage load
  aborts if a human cannot be placed, and points within 85 units of the
  origin are marked down.
- **Simulants** move along waypoint links only. With the waypoint tables
  empty every pad becomes a waypoint, linked along each path row in order.
  The list every base arena has (id `0x1000`: `mp_init_simulants`,
  `rebuild_teams`, `rebuild_squadrons`, `set_list`, `end`) starts them; a
  list id below `0x401` replaces one of the game's global lists.
- **Scale**: one unit is a centimetre. A player is 172 tall with radius 30
  and steps up 30; a simulant is 185 with radius 20.
- **What the Python validator does not check**: scene geometry, collision
  content, pad references across files, the navigation node, the exact text
  forms, the record's exact text, the trigger node count.

## F5 first piece: the Blank Room (BUILT 2026-11-17, PROVEN in the game the same day: see the last section)

`dev-mods/blank_room/`: an original level with nothing of the original game
in it, and the template a level that can be shared starts from.

- `build_blank_room.py` makes all of it: a room 24 by 24 metres and 4 high
  with four pillars (96 collision triangles in room 1, the ceiling drawn and
  not collided with, as base arenas do), a tile texture it draws itself,
  eight starts on a ring of radius 850 facing the middle, their bounds rows,
  one closed path round the ring, the `0x1000` list, `rooms.json` with the
  room's box, empty object tables, the graph and the navigation record.
- The graph's structure is taken from this repository's own example level
  (`tri_arena.pdarena`, which CORE's builder keeps in step with the game): its
  422 AI kind nodes and fixed links, the pickup and door rule nodes and edges,
  and the cache kind for `arena.ini`. The builder adds the three structural
  nodes the example lacks. When the level formats move: rebuild the example,
  then run this builder (`--check` says whether the built file is current).
- The builder checks its output against the rules above (the node kinds are
  read from the game's own source at build time: 428 of them) and then hands
  the archive to `tools/asset_archive_conformance.py`, which accepts it.
- It is a dev mod, off by default (`dev-mods.json`: `-DevMods blank_room`),
  in a folder of its own so that a fault in it cannot roll back another mod's
  scan. Ids `mod_blank_room:arena_blank_room` and
  `mod_blank_room:scenario_blank_room`.
- Unit case (hosted CI): the level document reads it, a start is added, the
  mod author saves it as a level, and that level goes to the archive
  validator; then a weapon place and a crate are made on the empty level.
- Smokes written for the next turn: `forge_blank_room_smoke` (a match on the
  Blank Room itself, with the game's own lines for collision, scene, rooms,
  graph, pads and spawn, and a screenshot) and `forge_blank_room_edit_smoke`
  (the scripted Forge edit on it: a start, a weapon place made from nothing
  and pinned to `base:dragon`, an ammo crate made from nothing, saved as a
  level and played; it requires the three pads Forge adds to be found a room).

Only a run settles: how the room looks, whether the spawn pool takes the
starts, whether simulants walk the ring, whether a match with no pickups is
acceptable, the minted stage's music, sky and fog, and wall winding in play.
After that: the same builder fed a Blender model (plain GLB in, room
attribute and collision added), which is what the Maian arena needs.

## F4 first slice: the Modding Hub's Levels tool (WRITTEN 2026-11-17; PROVEN in the game the same day, turn q_..ef485e13)

Level authoring joins the other in-game mod tools: a Levels tab in the
Modding Hub, after Characters.

What a player does there:

- Two lists, by name: Level Mods (levels made here, installed level mods and
  templates such as the Blank Room) and Game Levels (the game's own arenas
  and missions). A player with no level mods yet starts on Game Levels.
- The selected level's pane says what it holds, read from the level's own
  files by the document Forge edits (`forgeLevelOpen`): for example "8 starts,
  12 weapon places, 1 fixed weapon, 3 ammo crates, 40 props, 25 other objects",
  its pads and its archive. A level Forge cannot open says why there, before
  the player tries.
- Open in Forge starts a Forge session on the level (`forgeLevelStartForge`,
  the call The Grid's Enter makes) and closes the hub. It is off while a
  network game runs.
- New level from this one: a name, the id the name gives, then Create, or
  Create and Open in Forge. The new level is the template's archive under the
  new name, saved by the mod author with the active agent as its author and
  `levels-tool` as its tool. Forge's Save under that level's own name then
  updates it in place (F2's own-template save).
- A new level never replaces anything. The mod author updates a mod in place
  when a name gives the id of one the player already has; that is right for
  Forge's Save and wrong here, where it would put a fresh copy of the template
  over a level the player has worked on. Such a name is refused with the level
  that has it, and so is the template's own name.
- A level made from one of the game's levels holds data from the player's own
  game files; the pane says so and that it must stay on this computer (BYOR),
  in the words the Characters tool uses.

Where it is:

- Rules, unit-tested (`[level-tool]`): `port/src/level_tool.c`
  (`level_tool.h`): what a level's placements add up to (a record the level
  leaves out counts as left out, not under its kind), the one-line summary,
  and which names a new level may take.
- Window: `port/fast3d/pdgui_menu_leveltool.cpp` (`pdgui_leveltool.h`); the
  hub's table has one more row (`HUB_TOOL_LEVELS`). The mod author's answer
  for a name is asked again only when the name or the template changes,
  because it looks at the mods folder.
- `--debug-level-tool-new <arena id>` with `--debug-level-tool-name <name>`
  runs the Create button's function at boot, before `--debug-forge-level` and
  `--launch-forge-level`, so one launch can make a level, open it in Forge and
  save it. Lines: `LEVELTOOL.NEW.*`, `LEVELTOOL.FORGE`, `LEVELTOOL.DEBUG.NEW.*`.
- Smoke `forge_level_tool_smoke`: the Blank Room staged as a mod; a new level
  `Tool_Room` made from it by the tool's function; a Forge session started on
  the new level; a start added and the level saved under its own name from
  inside the session. That last step is new ground for the game too: the save
  replaces the archive of the level that is loaded and the mod manager rescans
  and applies while it runs (`own_template=1`, `updated=1`). Every later Save
  of a level being worked on is that step, so the smoke is there to find out
  whether it holds.

Not in this slice: a check of a level beyond "its files are read" (the game's
own checks run when it loads one: the graph's rules, the navigation record,
the scene and the collision; a Check Level button would run them without a
load); deleting a level (the Mod Manager turns one off); the author's name on
Forge's own Save (it is still the mod author's default, "Player"); a picture
of each level in the list.

## Proven in the game (2026-11-17, turn q_20261117080550197_5ee34c8d)

11:41 to 12:40 UTC, client 0f329f66, shared base 9, receipts under
`D:\PD2-wt\pd2-smoke-mods\receipts\20261117T12*`. pd-tests: 2,204 cases pass,
the base-level sweep included (B-1239 is closed by it), and the game's release
validator accepts the Blank Room.

- The editor camera (B-1240, B-1238). `forge_session_save_smoke` 35/35 and
  `forge_blank_room_session_smoke` 38/38. With the camera turned to yaw 217
  and 20 degrees down: `ahead=(0.500, 0.500) right=(0.646, 0.500) up=(0.500,
  0.240) behind=hidden`, `player_theta=217.0 player_verta=-20.0`. Flying
  forward for 1.2 s: `moved=(407, -246, -540) distance=720 along_look=1.000
  along_right=0.000`. The same numbers on Complex and on the Blank Room. The
  forward action a script injects is read while the camera flies, with the
  window unfocused.
- The Blank Room is a level. `forge_blank_room_smoke`: a Combat Simulator
  match loads it (collision 96 triangles with the flags sidecar, 2 rooms and
  96 tiles, 8 pads all with a room, 8 starts, the level program `nodes=11
  exec_edges=7 modules=4`), the player spawns and the scoreboard lists a
  simulant. 24 of 25 checks; the one miss was the fixture's (it expected
  `volumes=8` in the level program line, where every level says `volumes=0`;
  corrected). The screenshot shows the tiled floor, walls, ceiling and a
  pillar as built.
- The Blank Room edited through Forge plays. `forge_blank_room_edit_smoke`
  32/32: a start added at (810, 50, 0), a weapon place made from nothing at
  (690, 50, 0) and pinned to `base:dragon`, an ammo crate made from nothing at
  (570, 50, 0), `pads=11 spawns=9 objects=2`, saved as a level, and a match on
  it: all eleven pads find a room, setup makes the fixed weapon, and the
  screenshot shows the player holding the Dragon it gives and a simulant in
  the room, diving sideways.
- A Forge session on a mod level with a minted stage number, and a save from
  inside it (`forge_blank_room_session_smoke`).
- Again on base 9: `forge_mod_level_load_smoke` 17/17, `forge_level_edit_smoke`
  29/29, `forge_level_weapon_smoke` 27/27.

Seen in the pictures, not yet acted on:

- The two session screenshots were taken after the flight, which at 20
  degrees down ends under the floor, so they show the level from below (the
  markers and their labels are there). Both session smokes now take a picture
  before the flight as well.
- Every key in the editor's legend reads "Unbound" in these runs. Whether a
  player sees that too, or only a run whose window has no focus, is not known
  yet: the label comes from the bindings that are active at that moment
  (`actionmapGetActiveBindingVk`), which depends on the input layer on top and
  on the device last used.

Not proven: the Levels tool (F4, written after this turn was armed), and
anything under a hand: the panel's buttons, the mouse look, picking.

## F3: a level places a hoverbike (WRITTEN 2026-11-17, unit-tested, not run)

The level document makes a third kind from nothing,
`FORGE_LEVEL_NEW_HOVERBIKE`: the record all five hoverbikes of the base
levels share (63 field rows; `base:model_hovbike`; flag words 0x05120101,
0x00304300 and 0x02000003, so it falls to rest without turning over and
shots and explosions do not harm it; its hover state at rest). It faces its
pad's look, as every placed object does. The editor's Level File section has
New Hoverbike Here, which places one where the camera is, facing the way it
looks.

The first use is the vehicle smokes' own place to drive (B-1241): Escape's
script now ends a ride a second in. `--debug-forge-level-vehicle` adds a
hoverbike to the scripted level edit, and `maian_vehicle_room_drive_smoke`
and `maian_vehicle_room_boost_smoke` ride the Maian vehicle on the Blank Room
in a Combat Simulator match (the hoverbike is swapped for the mod vehicle at
setup, as on Escape). Which vehicle a record names is still the base
hoverbike: a record that names a mod vehicle's model is the next step, and
then a level needs no swap flag.

## F3: props from the catalog (WRITTEN 2026-11-17, unit-tested, not run)

A level can gain a plain prop drawn with any model of the catalog, made from
nothing: `forgeLevelDocAddProp` writes the record all 2,226 plain props of
the base levels share (30 field rows), life size, falling to the floor under
it, with no other flags. In the editor's Level File section a filter box and
a list of the catalog's models choose a model; New Prop Here places one where
the camera is, and What It Looks Like redraws a selected plain prop with the
chosen model (`forgeLevelDocSetObjectModel`). `--debug-forge-level-prop
<model id>` adds one to the scripted level edit (420 units in front of the
first start, after checking the catalog has that model), and
`forge_blank_room_edit_smoke` now makes a `base:model_crate2_cq` crate on the
Blank Room and requires the level to compile it as a third setup record.

Survey (structure only, shared base 9): of the 2,226 plain props, 1,837 are
life size, 1,595 are fitted to their pad's box in some way (flag bits
0x000000f0) and 616 only fall to the floor; the arenas use 117 distinct
models and the missions 127, all `base:model_*`. A prop made here is never
fitted to a pad's box: Forge's pads are 200 units each way, and a fitted prop
would take that size.

## F3: level edits can be undone (WRITTEN 2026-11-17, not run)

Every edit the editor's Level File section makes to the open level (a start
added, moved or removed; a weapon place, ammo crate, hoverbike or prop made;
an object moved, copied, left out or put back; a model or a weapon given) can
be undone and redone. The document is all values, so a copy shares nothing
with it (`forgeLevelDocClone`, unit-tested: the copy edits, saves and reports
changes as the document would have). The session keeps up to 32 copies from
before each edit (`forgeLevelSessionEditBegin`/`EditEnd` around the edit) and
Undo and Redo swap the open document with them; opening another level,
closing and starting a session forget them, a save does not. The section has
Undo Level Edit (n) and Redo (n). `--debug-forge-session-undo` undoes and
redoes the start the session debug flag adds, before it saves, and
`forge_blank_room_session_smoke` requires both steps (`FORGE.LEVEL.UNDO`,
`FORGE.LEVEL.REDO`) and the same saved level as before.

## F3: turning, and picking in the view (WRITTEN 2026-11-17, not run)

- Turn Left 45 and Turn Right 45 turn a selected player start or object
  where it stands (`forgeLevelTurnLook`, unit-tested: positive is to the
  right, the way the camera's yaw grows), as an edit that can be undone.
- The world markers can be clicked: a left click on the world, off the
  editor's windows, within a few pixels of a marker selects that start or
  object. The marker turns yellow at once and the Level File section takes
  the pick into its lists when it next draws
  (`pdguiForgeLevelMarkersTakePick`). The left button is not used over the
  world for anything else while the camera flies.

Still to come in F3: a move gizmo (dragging an object instead of flying the
camera to where it goes), drawing a selected object's outline, and size.

## Proven in the game (2026-11-17, turn q_20261117134802790_ef485e13)

14:49 to 15:30 UTC, client c88a828f, shared base 9, receipts under
`D:\PD2-wt\pd2-smoke-mods\receipts\20261117T15*`. pd-tests pass, the
base-level sweep included.

- The Levels tool (F4). `forge_level_tool_smoke` 43/43: a new level
  'Tool_Room' made from the Blank Room by the tool's own function, a Forge
  session started on it by the Open in Forge call, and a start added and the
  level saved over itself while it is the level that is loaded
  (`own_template=1`, `updated=1`).
- The Blank Room and Complex again, now with a picture from before the
  flight: `forge_blank_room_smoke` 25/25 (the corrected fixture),
  `forge_blank_room_session_smoke` 39/39 and `forge_session_save_smoke` 36/36.
  The pictures show the editor camera at yaw 217 and 20 degrees down, the
  HUD's readout, and on Complex the markers of starts and objects with their
  labels.
- A level places a hoverbike (F3). The scripted edit made one from nothing on
  the Blank Room (`made hoverbike setup_0002 at (250, 50, 0) facing (-1.00,
  0.00, 0.00)`), the edited level loaded with `pads=12 spawns=9 objects=3`,
  setup made the hoverbike, and the Maian vehicle mod took its model
  (`VEHICLE.SWAP: hoverbike modelnum 58 -> 441`). The two vehicle smokes that
  ride it failed after that and before the ride: the debug mount hook could not
  place the rider where the camera sees (B-1243, fixed in e4532d24; rerun in
  the next turn).

Seen in the pictures, not yet acted on (cosmetic): on Complex the markers'
labels show through the editor's translucent panel, and through walls.

## F3: the world follows the level file (WRITTEN 2026-11-17, unit-tested, not compiled)

Until now an edit showed only as the HUD's marker: a moved object stayed where
the stage had it, a new one was not there and a removed one did not go. The
world now follows the open level file while the camera flies
(`port/include/forge_level_live.h`):

- An object of the loaded level that the file moved, redrew (another model,
  size or weapon) or left out is hidden: not drawn, not ticked, no collision
  (`propDisable`; shown again with `propEnable` when the file no longer asks).
- An object the file added, and one it moved or redrew, is drawn by a
  stand-in: a plain object with the same model and size, set down the way
  setup sets down an object on a pad (the pad's look and up, then
  `func0f06a730`, which rests it on the floor under the pad), that anything
  passes through. An object only moved is drawn as the stage drew it, which
  for one of the match's weapon places is the weapon the match put there.
  Stand-ins live in the stage's memory (64 at most), are never freed while the
  stage lasts, and a stand-in no longer wanted is hidden and reused.
- The world never hides an object it cannot draw again: such an object stays
  where the stage has it, with the marker where the file puts it.
- Rows match the stage's objects by record id (setup_0012 is the thirteenth
  command, which the document checks when it reads a level), and each match is
  checked against the object's type before anything is hidden.
- Playtest plays the stage as loaded: the stage's objects come back and the
  stand-ins go when the camera stops flying, when the file closes and when the
  session ends. Playing the edits means saving the level and choosing it.
- It follows the file only when the file is the level the session is on, and
  catches up once per change of the file (the session's generation number:
  opened, edited, undone, redone, closed). A stage change forgets everything.

Choice for Mike to overrule: stand-ins rather than moving the stage's own
objects or reloading the stage. Moving a live object of any kind (weapons the
match placed, hoverbikes with physics, doors with portals) is engine surgery
per kind; reloading on every edit takes seconds and loses the session's
state. A stand-in is one plain object of one kind, and the stage's own objects
are only switched off and on.

The planning half is unit-tested (`[forge-level-live]`, five cases in
`tests/test_forge_level_doc.cpp`); the world half needs a game turn.
Kinds drawn: props, weapon pickups, ammo crates, hoverbikes, hover props,
keys, hats, shields, gas bottles, monitors, alarms, cameras, autoguns, safes
and glass. Doors, lifts and characters keep their markers only.

## F3: an object's size (WRITTEN 2026-11-17, unit-tested, not compiled)

`forgeLevelDocSetObjectScale` writes an object's `base.extra_scale` row (256
is life size), the one file that holds it; it refuses a record with no such
row and one of the match's weapon places, which the match's weapon set sizes
whatever the level says (`setupPlaceWeapon`). The Level File section has a
Size slider (25% to 400%) and Set Size, an edit that can be undone, and the
world's stand-in is drawn at the new size.

## F5: a level from a model (BUILT 2026-11-17, unit-tested, not run)

The Blank Room's builder is split: everything that does not depend on what a
level looks like is `tools/level_archive.py` (the tables, the AI list, the
navigation files and record, the graph, the archives, the scene writer and
the checks), and the Blank Room keeps its room, tile and starts. Its
`--check` says the split builds the identical level.

`tools/build_level_from_model.py MODEL.glb --id ID --name NAME --out DIR`
makes a level from a model, the way one exported from Blender comes:

- every mesh is drawn and collided with; a node named `collision...` is
  collided with and not drawn, `nocollide...` is drawn and not collided with;
  a face that looks down is never collided with (ceilings, as in the base
  arenas); a face that looks up (normal y 0.5 or more, the game's floor
  test) is a floor, the rest walls; a mirroring node's faces are turned back
  the right way round;
- each material's base colour texture (embedded; PNG as is, other kinds
  converted when Pillow is there) and factor become one textured, tinted
  part of the scene; a material with no texture is drawn plain in its colour;
- empties named `start...` are the starts: set 50 above the floor under them
  and facing where the empty's +Z points in glTF (Blender's -Y), walked in
  order round their middle by the simulants' path;
- `--scale` (default 100) turns metres into the game's centimetres;
- one room: every triangle and start is in room 1, the room's box is the
  model's. Several rooms need portals and are later work.

`tools/tests/test_level_from_model.py` (7 cases, local; hosted CI does not
run it yet) builds a model in memory and checks the collision kinds and
counts, the mirrored part's winding, the scene's parts, the starts' heights,
facings and order, and the refusals. `tools/simple_glb.py` is the small
writer it and the example use.

The example is the Model Room (`dev-mods/model_room`, a dev mod off by
default): `make_model_room.py` writes `model_room.glb` (a 24 m room, a
platform 1.2 m up at each end on a ramp, the west one a mirrored copy, four
pillars, two cover blocks, a ceiling, a strip along a wall that is not
collided with, eight starts) and builds it: 90 collision triangles, 14 of
them floors, starts at 50 and 170 high, accepted by the archive validator.
`forge_model_room_smoke` plays a match on it.

A model made in Blender itself (2026-11-17, Blender 5.2.2 in the background,
`dev-mods/model_room/blender_arena_example.py`, exported with Blender's glTF
exporter, Y up): the builder made 24 collision triangles, 8 of them floors
(the floor, a platform that is a scaled cube, a ramp and a copy of it scaled
-1 on X), left out the ceiling and a `nocollide_banner`, kept the textured
and the plain material, set the start on the platform at 150 (its top is at
100), and every start faced the middle: an empty's -Y in Blender is its
facing, through the exporter's axis change. Not played yet.

Next for F5: play a Blender-made level in a game turn, and then the Maian
arena when Forge's authoring is finished (Mike, 2026-11-15).

Later, so a level's geometry can come in through the in-game tools as well:
the Levels tool's "New level from a model file", doing in the client what
the builder does. The pieces exist: the client reads glTF
(`port/src/modasset_gltf_document.cpp`, the mod asset compiler's reader),
and a save from a template with replaced members already brings the pads,
volumes, graph nodes and the navigation record into step (the level document
and the archive rebuild). What it would add: the scene's conversion to the
game's form (unindexed, float colours, `_PD_ROOM`), `collision.obj` and its
flags, the room box, and the starts from the model's empties.
