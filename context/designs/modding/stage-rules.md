# Stage-wide rules: `stage.json`

Status: first family (weather) built on `week-pd/core`, 2026-11-15. Owner: CORE.

Mike's decision 3 (2026-11-15): stage-wide rules may live anywhere "as long as
the rules end up packed into the level's .pd* archive file so a level ships as
one file". They live in one member of the stage's `.pdscenario`, `stage.json`
(schema `pd2.scenario.stage.v1`). A `.pdarena` carries its scenario nested, so
an arena ships its rules too.

Code: `port/include/scenario_stage_rules.h` (format), `port/src/scenario_stage_rules.cpp`
(strict reader), `scenario_source_runtime.c` (`scenarioSourceLoadStageRulesForStage`,
called from `bg.c` with the other scenario tables; dropped by
`scenarioSourceStageRulesReset`), the game code that reads a family, and
`romextract_pdarena.c` (`s_buildStageJson`, written for every scenario).
Checks: `tools/asset_archive_conformance.py` (`validate_stage_json`), tests in
`tests/test_stage_rules.cpp` (`[stagerules]`, sweep `[stagerules-sweep]` with
`PD2_STAGE_RULES_ROOT`).

## Rules of the file

- Each family is one optional top-level key; an absent key means the stage has
  no such rule.
- A key the reader does not know, or a value it cannot use, refuses the whole
  file (logged: `SCENARIO.SOURCE: stage.json of '<scenario>' refused`), and the
  stage runs without stage-wide rules. Nothing is half applied. A newer family
  therefore needs a client that knows it.
- A stage built from the ROM background keeps the game's own tables and checks.

## `atmosphere`

The Grid can author this optional family in Level File. The game applies it
when the saved stage loads.

| Field | Meaning |
|---|---|
| `sky_rgb` | Three integer color channels from 0 to 255 |
| `fog` | `start` and a larger `end`, from 0 to 1000 thousandths of view distance |
| `ambient_light` | Default room brightness from 0 to 255. A room's explicit `base_brightness` takes precedence. |
| `primary_music` | Catalog ID of a sequenced song to use as the stage's primary track |

Each setting is optional. The editor preserves the stage's other rules.

## `weather`

Where and how the stage's rain or snow falls. Without a startup rule, the
stage's AI commands select the kind and intensity (`configure_rain`,
`configure_snow`, which already have graph twins). Written from the game's
`g_WeatherConfig` (`src/game/weather.c`), so base stages are unchanged.

| Field | Meaning |
|---|---|
| `wind_speed` | number |
| `y_min`, `y_max` | the height band the particles fill, around the camera; `y_min` below `y_max` |
| `z_max` | 0 for no limit, otherwise no weather while the camera is below this z (Air Base: -2000) |
| `cutscene_only` | weather only during cutscenes (G5) |
| `weather_rooms` or `dry_rooms` | exactly one: the only rooms with weather, or the rooms without it; room numbers 1..32767, at most 127 |
| `wind_direction` | optional `{angle, speed_x, speed_z}`: a fixed wind instead of the changing one (Air Base) |

Base stages with a rule: Chicago, G5 Building, Air Base, Crash Site.
`weatherTick` used to check Air Base and G5 by stage; on the scenario-source path
it reads `z_max` and `cutscene_only` instead, which hold the same values, and the
draw code already did.

## `weather_start`

The Grid can choose weather at stage start in Level File's Atmosphere section.
The setting takes effect on draft Playtest or when the saved level loads.

`"weather_start": { "kind": "rain", "intensity": 2 }` starts native rain.
Rain has intensity 1 (light), 2 (medium), or 3 (heavy). Snow uses
`"kind": "snow"` and intensity 1. Stage default removes this override so the
stage's AI commands can start its existing weather as before.

The separate `weather` object's room list, cutscene/camera limits, bounds, and
wind still apply. If that object is absent, startup uses wind speed 20, a
camera-relative vertical range of -800 to 800, and precipitation in all rooms.
Later AI commands can change the weather. Particle placement uses the engine's
existing random stream, so exact particle positions can differ across clients.

## `player`

What the stage dresses the player in and leaves them. The game kept these as
stage checks in `src/game/player.c` (outfit choice) and `src/game/bondgun.c`
(putting on the disguise, the ammo the unlimited ammo cheats refill); the
extractor writes the same conditions.

| Field | Meaning |
|---|---|
| `perfect_agent_outfit` | the outfit worn at Perfect Agent and up (Villa: `negotiator`) |
| `disguise` | `{outfit, untargetable}`: the outfit worn while disguised (Rescue: `lab`, Air Base: `stewardess`), and whether a disguised player cannot be targeted (Rescue) |
| `coop_spawn_distance` | how far the co-op buddy starts from the player, 1..1000 (60 when absent; Defection: 80, a port fix so the buddy does not block the intro) |
| `unlimited_ammo_excludes` | the weapons (catalog ids, 1 to 8) whose primary ammo the unlimited ammo cheats do not refill on the stage (Chicago: `base:remotemine`, Air Force One: `base:timedmine`, the original's `bgunAmmotypeAllowsUnlimitedAmmo`); every name must be a weapon in the catalog or the file is refused. The game's own list of ammo the cheats never refill (Psychosis darts, bugs, cameras, plastique, ECM mines and others) is not a stage rule and stays in that function |

## `ai`

| Field | Meaning |
|---|---|
| `dark_room_lists` | `false`: guards never switch to their dark room AI list (`src/game/chrai.c`; Crash Site) |

## `ending`

| Field | Meaning |
|---|---|
| `outcome` | `"unknown"`: the end screen reports the mission as Unknown and the agent as Missing, the end screen turns white instead of the success colour, the menus' particles are gray and slow (the pause menu too), and a completion plays the mission-unknown tune (Defense). `"known"` is the default |

Written from the game's `g_StageIndex == STAGEINDEX_DEFENSE` checks
(`endscreen.c`, `menu.c`, `menugfx.c`), which now all ask one function,
`endscreenStageOutcomeUnknown`: the rule on a scenario-source stage, the check
on a ROM stage. What a completed mission leads to (Deep Sea straight on, Skedar
Ruins to the credits and the alternate title) is mission behavior, so it lives
in the mission graph instead (`mission-graph-runner.md`, mission endings).

## `hub`

`"hub": true` marks the stage the player spends time in between missions (the
Carrington Institute). On it: cheats are not applied (`cheats.c`), no mission
timer is shown (`hudmsg.c`), corpses fade as in the original instead of
persisting (`chraction.c`, `corpsestore.c`), the player takes no damage outside
a training session (`chraction.c`), a death respawns the player instead of
ending the stage and co-op AI buddies do not spawn (`player.c`), and its end
screen offers no next mission (`endscreen.c`). All of these ask one function,
`lvStageIsHub` (`lv.c`): the rule on a scenario-source stage, the Carrington
Institute on a ROM stage.

Not part of the rule, and still checked by stage: the Institute's own training
content (the device and holo training, the firing range, its tagged objects and
target models, the tour), which belongs to that stage's objects and AI, and the
PC main menu, which runs on the Institute (file select, menu music, the menu
camera, the lobby): those are about the menu stage, not about a hub.

Outfit names follow the `OUTFIT_*` constants in order: `default`, `elvis`,
`trent`, `trench`, `frock_ripped`, `frock`, `leather`, `deepsea`, `wetsuit`,
`snow`, `lab`, `stewardess`, `mrblonde`, `maian`, `negotiator` (a test keeps the
reader, the checker and `constants.h` equal).

## `stars`

`"stars": { "count": 200, "grid": 2, "below_horizon": false }` gives the stage
a starfield (`stars.c`): `count` stars (1 to 8192) spread over a sky cube of
`grid` cells per face side (1 to 8), filling the whole sky when
`below_horizon` is true and only the upper half otherwise (`below_horizon`
defaults to false). A stage without the key has no starfield. On a stage built
from scenario source `lvReset` allocates the starfield, `starsReset` takes the
three values and `bgRenderScene` draws the stars (single player only, as
before) when the rule is present; a ROM stage keeps the stage lists. The
extractor writes it from those lists: Defection, Extraction, Infiltration and
Escape get 200 stars on a grid of 2; Attack Ship 1,200 and Test Old 1,600 on a
grid of 3, below the horizon too. Not carried: `starsRender`'s Defection and
Extraction tint, which computes a blend and discards it, so it has no effect.

## `sparks`

`"sparks": { "flash_lights": false }` keeps the stage's environmental sparks
(`sparks.c`'s `SPARKTYPE_ENVIRONMENTAL` types) from flashing the room lights;
`flash_lights` is required, and a stage without the key flashes them, as the
original does everywhere but Crash Site. The extractor writes it for Crash Site;
a ROM stage keeps the original check.

## `gas`

A gas cloud drawn over the screen (`nbomb.c` `gasRender`; Escape's gas):
`rooms` (room numbers where it shows), a `sphere` `{center, radius, door_tag}`
outside those rooms where it thins toward the edge and follows how far the
`door_tag` door is open, `door_rooms` `[{room, door_tags, add}]` (inside a gas
room that is also `room`, the gas follows the most open of `door_tags`, plus
`add`) and a `cutscene_fade` `{start_frame, full_frame}`. At least one of
`rooms` and `sphere`. The game keeps one rule struct for both paths: a ROM stage
uses `g_EscapeGasRule` (the original's constants) and the extractor writes the
same struct into Escape's `stage.json`, so the two cannot differ.

## Next families (from CORE's stage-check survey, week note checklist)

Each becomes a key here unless it belongs to a room or an object:

- Escape's gas went into `gas` above (its rooms listed there, as weather's are).
- Animated textures went into `scene.glb` materials (`animated-surfaces.md`)
  and sparks into `sparks` above.
- The stage's text bank (`lang.c`): built 2026-11-21 (Multiplayer and Rules,
  Mike's order "Stage text banks off lang.c onto graph data"), as a manifest
  field rather than a `stage.json` key, because the briefing needs it before
  the stage loads; see "Text bank" below.

## Text bank (the archive manifest, not `stage.json`)

The stage's language bank is named in the scenario archive's
`_meta/manifest.json` as `"text_bank"`: the locale-independent `ASSET_LANG`
catalog id (`base:lang_dam`; the catalog picks the bank's locale file as
before, `langmanifest.c`). The scenario walker (`loader_walker_scenario.c`)
and the INI scanner read it into `ext.scenario.text_bank` when the archive
registers, so the three places that asked `lang.c`'s stage table now ask
`langGetStageBank(stagenum, prefer_mp)`: the mission briefing before the stage
loads (`setupLoadBriefing`, the solo scenario), the stage's setup load and the
teardown (`lvStop`, the scenario the stage loaded). The extractor writes the id
from the game's table (`assetCatalogBaseLangBankId`) into every base scenario
and rebuilds an archive that lacks it (scenario kind `..._textbank`).

- A stage that names no bank keeps the table: a ROM stage, or a level written
  before the field (the `..._liftglass` kind still loads).
- A named id that is not a language bank fails loudly (`LOUDFAIL.LANG.STAGE`)
  and the table answers, so the stage keeps its text; conformance refuses such
  an archive (`text_bank` must look like `namespace:lang_name`).
- Every resolution logs once per stage: `LANG.STAGE: stage=0x.. bank=N
  source=archive|table scenario='...'`.
- Open (decision for Mike): a level that wants text of its own needs a bank it
  owns, while AI commands and objectives still address text as bank and index.

Room-level and object-level rules (Escape's gas rooms, the Extraction chopper
hum, objects left out of co-op) go on `rooms.json` and `objects.json` rows
instead, like the light rules; the object rules are in `object-rules.md`.
