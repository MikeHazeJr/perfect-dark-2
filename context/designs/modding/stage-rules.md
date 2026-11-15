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

## `weather`

Where and how the stage's rain or snow falls. Whether it rains or snows, and
how hard, is started by the stage's AI commands (`configure_rain`,
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

## `player`

What the stage dresses the player in. The game kept these as stage checks in
`src/game/player.c` (outfit choice) and `src/game/bondgun.c` (putting on the
disguise); the extractor writes the same conditions.

| Field | Meaning |
|---|---|
| `perfect_agent_outfit` | the outfit worn at Perfect Agent and up (Villa: `negotiator`) |
| `disguise` | `{outfit, untargetable}`: the outfit worn while disguised (Rescue: `lab`, Air Base: `stewardess`), and whether a disguised player cannot be targeted (Rescue) |
| `coop_spawn_distance` | how far the co-op buddy starts from the player, 1..1000 (60 when absent; Defection: 80, a port fix so the buddy does not block the intro) |

## `ai`

| Field | Meaning |
|---|---|
| `dark_room_lists` | `false`: guards never switch to their dark room AI list (`src/game/chrai.c`; Crash Site) |

Outfit names follow the `OUTFIT_*` constants in order: `default`, `elvis`,
`trent`, `trench`, `frock_ripped`, `frock`, `leather`, `deepsea`, `wetsuit`,
`snow`, `lab`, `stewardess`, `mrblonde`, `maian`, `negotiator` (a test keeps the
reader, the checker and `constants.h` equal).

## Next families (from CORE's stage-check survey, week note checklist)

Each becomes a key here unless it belongs to a room or an object:

- `stars`: count, grid and below-horizon rule (`stars.c`, the starfield
  allocation in `lv.c`, the draw in `bg.c`). B-1172 first: the scene renderer
  does not draw stars at all yet.
- `ending`: Defense's unknown-status end screen, and the stages whose completion
  goes straight to the next stage or to the credits (`endscreen.c`, `menu.c`,
  `menugfx.c`, `menutick.c`).
- `hub`: one flag for the training stage's ~25 special cases.
- Escape's gas (a stage-level sphere and door tags; its rooms on `rooms.json`).
- Animated textures and sparks (`dyntex.c`, `sparks.c`).
- The stage's text bank (`lang.c`), if it is needed before the scenario is read.

Room-level and object-level rules (Escape's gas rooms, the Extraction chopper
hum, objects left out of co-op) go on `rooms.json` and `objects.json` rows
instead, like the light rules.
