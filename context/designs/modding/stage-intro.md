# The stage intro: `intro.json`

Status: built on `week-pd/core`, 2026-11-15 (B-1175, B-1176). Owner: CORE.

A stage's setup file starts with an intro script. Besides the spawn pads it
gives the player's starting weapons and ammo, the outfit, the watch time, and
for multiplayer the briefcases, their respawn pads and the King of the Hill
hills. Scenario source carried only the spawn pads (`spawns.json`), so a stage
built from it started the player unarmed and in the default outfit (B-1175),
and the arenas, which were also built from the wrong setup file (B-1176), had
none of their placements.

The rest of the script now lives in one member of each `.pdscenario`,
`intro.json` (schema `pd2.scenario.intro.v1`). The runtime rebuilds the
intro as the spawn pads, then these rows in their authored order, then the
end, so `playerReset`, the inventory reset (`player.c`), `scenarioReset`
(`mplayer/scenarios.c`) and the network manifest read the same commands the
game read from the ROM setup.

Code: `port/include/scenario_intro.h` (format), `port/src/scenario_intro.cpp`
(strict reader), `scenarioSourceLoadSetupForStage` and
`s_setupBuildStageBlock` (`port/src/scenario_source_runtime.c`), and
`s_buildIntroJson` (`port/src/romextract_pdarena.c`). Checks:
`tools/asset_archive_conformance.py` (`validate_intro_json`), tests in
`tests/test_scenario_intro.cpp` (`[intro]`, sweep `[intro-sweep]` with
`PD2_INTRO_ROOT`).

## Rows

| `cmd` | Fields | Game command |
|---|---|---|
| `weapon` | `weapon_catalog_id` (or `weapon_selector: "none"`), optional `left_weapon_catalog_id`, `player` (0 = the player) | `INTROCMD_WEAPON` |
| `ammo` | `ammo_type` 0..32, `quantity` 0..1000000, `player` | `INTROCMD_AMMO` |
| `outfit` | `outfit`, a name from the `stage.json` outfit list | `INTROCMD_OUTFIT` |
| `watch_time` | `hours` 0..23, `minutes` 0..59 | `INTROCMD_WATCHTIME` |
| `case` | `team` 0..3, `pad_ref` (`pad_NNNN`) | `INTROCMD_CASE` |
| `case_respawn` | `team` 0..3, `pad_ref` | `INTROCMD_CASERESPAWN` |
| `hill` | `pad_ref` | `INTROCMD_HILL` |
| `legacy_3`, `legacy_4`, `legacy_6` | `words`: the 7, 1 or 9 parameters as they are | `INTROCMD_3`, `_4`, `_6` (no PD code reads them; GoldenEye leftovers, carried so nothing is lost) |

Rules: an absent `intro.json` means no such commands (a custom level may
have none). A row or key the reader does not know, or a value it cannot use,
refuses the file, and the stage does not load, as with an unreadable
`spawns.json`: starting a mission silently unarmed is worse than a loud stop.
A credit offset (`INTROCMD_CREDITOFFSET`) points into setup data the
scenario does not carry, so the extractor refuses a setup that has one; no
base setup does.

## Which setup a scenario comes from

A scenario of kind `mp` (the Dark, Classic and Bonus arenas, and the
multiplayer test stages) is built from the stage's multiplayer setup,
`mpsetupfileid`, the file `setupLoadFiles` reads when a match runs; every
other kind from the solo setup. The manifest records it as `setup_source`
(`mpsetup` or `setup`). The Solo Missions arenas keep the solo setup.

Census of the 121 ROM setup files (read-only): 241 spawns, 59 weapon, 59
ammo and 79 outfit commands, 64 cases, 384 case respawns and 75 hills; no
watch time, credit offset or legacy command.

## Open

- In a match on a Solo Missions arena the original reads that stage's
  multiplayer setup, which is empty, while scenario source loads the solo
  setup with its guards. That is MULTIPLAYER's call (asked in the week note).
- Forge-made levels: Mike, 2026-11-15, Forge mode authoring comes before any
  custom level work. When Forge writes levels it should write `intro.json`
  for a starting loadout and objective-mode placements; until then a level
  without one loads with spawns only.
