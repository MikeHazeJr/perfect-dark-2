# Object rules: optional fields on `objects.json` rows

Status: CORE, 2026-11-16. Built on `week-pd/core` for the hover cars, Enemy
Rockets and the chopper (`port/include/scenario_object_rules.h`); the lifts
rule (B-228's single-player-in-multiplayer list) is not converted yet. The
"Object rules" row of the week note's
conversion checklist. Decision 3 (stage-wide rules travel in the level's
archive) and decision 1 (players can recreate any behavior) applied to the
stage checks that are about one object, not the whole stage. Stage-wide
rules live in `stage.json` (`stage-rules.md`); room rules on `rooms.json`
rows; these go on the object's own row in `objects.json`
(`pd2.scenario.objects.v1`), so they move with the object when a level is
edited.

## The stage checks (from CORE's survey)

| Where | Original check | Base rows it touches |
|---|---|---|
| `src/game/setup.c` (`withhovercars`) | hover cars are not created on Extraction or Defection when the stage runs in co-op or counter-op | Defection's 10 `hover_car` rows (Extraction has none) |
| `src/game/setup.c` (Enemy Rockets) | the cheat swaps guards' guns for rocket launchers, except the K7 Avenger on Investigation at Perfect Agent ("it would make an objective impossible") | Investigation's K7 Avenger row (`setup_0265`) |
| `src/game/setup.c` (B-228, a port addition) | when a single-player stage runs in multiplayer, its lifts and escalator steps are kept (CI, Chicago, Villa, Infiltration, G5 Building, Pelagic) | the `lift` and escalator rows of those stages |
| `src/game/propobj.c` (`chopperTick*`) | the chopper on Extraction hums with `SFX_810D` and `SFX_SHIP_HUM` instead of `SFX_8110` and `SFX_810F`; when shot down it falls with no collision checks and is removed below y -30000, where the Area 51 interceptors collide and explode | Extraction's `chopper` row |

The `sight.c` and `lv.c` training targets (they react to the sight) belong to
the hub's training content, which stays with that stage (`stage-rules.md`,
`hub`), so they are not object rules.

## Row fields (all optional; absent means the default behavior)

| Field | Rows | Meaning |
|---|---|---|
| `coop` | any object | `false`: left out when the stage runs in co-op or counter-op |
| `multiplayer` | `lift`, escalator step | `true`: kept when a single-player stage runs in multiplayer |
| `enemy_rockets_exempt` | weapon rows | difficulties (`agent`, `special_agent`, `perfect_agent`, `perfect_dark`) at which the Enemy Rockets cheat leaves this weapon alone |
| `engine_hum`, `thrust_hum` | `chopper` | sound effect catalog ids for the two hums (defaults: the interceptors' `SFX_810F` and `SFX_8110`) |
| `fall` | `chopper` | `"crash"` (default: collision checks, explodes on the ground) or `"drop"` (no collision, removed below y -30000) |

## How the game reads them

- The scenario runtime reads the fields into its setup record (port-side,
  it may grow); a stage built from scenario source asks
  `scenarioSourceSetupRecordRules(index, &rules)` with the setup command
  index `setup.c` already walks. A ROM stage keeps the original checks.
- Creation-time rules (`coop`, `multiplayer`, `enemy_rockets_exempt`) are
  read where `setup.c` creates or arms the object.
- The chopper's hums and fall act every tick, so its rule is copied at
  creation into a small port-side table keyed by the object (no growth of
  `struct chopperobj`, B-323 stride class) and cleared at stage unload.
- The extractor writes the fields from the original's own conditions, the
  conformance tool knows them, and the scenario kind moves with them (SP-86).
