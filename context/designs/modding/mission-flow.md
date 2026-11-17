# Mission flow: phase, outcome and end from the mission graph

Status: CORE, 2026-11-16. Design for the week note's "Mission flow (phases,
end, fail)" row; slice 1 built on `week-pd/core` (see Slices), slices 2 and 3
are next. Decision 1
(players can recreate any existing behavior with the nodes) applied to how a
mission is judged and ended. Builds on `mission-graph-runner.md` (events,
gates and `og.*` modules, D-006 option A).

## What decides a mission today

| Part | Where | Graph or native |
|---|---|---|
| One objective's status | `objectiveCheck` (`src/game/objectives.c`) | Graph only, since decision 1. An objective the mission graph cannot evaluate stays incomplete and is logged once; `objectiveCheckNative` survives only as the campaign runner's parity oracle (`objectiveGraphParity`, "compared=N not_graph=0" on every base mission). |
| The outcome rule | `objectiveIsAllComplete` | Native: every objective whose difficulty bit matches is required; asked in 23 places (end screen title, unlocks and best times, the end menus, `if_all_objectives_complete` 0x00f7, `prop.c`, the campaign runner). |
| Phase (`load`, `active`, `complete`, `failed`, `end`) | `scenarioSourceMissionGraphRecordPhase`, from `objectiveIsAllComplete`, `objectivesCheckAll`, `lvTick` and `mainEndStage` | Native decides, the graph records: the mission graph carries five `mission.phase.source` data nodes and the runtime logs each transition. |
| End of the stage | `mainEndStage` from the AI's `end_level` fade (`TICKMODE_GE_FADEOUT`), the player's death (not on the hub), the pause menu's abort, a co-op partner's death | The AI lists are public source; death and abort are the engine's. The end screen then shows failed when the player died, aborted or not every required objective is complete. |
| What a completed mission leads to | `event.mission.continue`, `event.mission.completed` | Graph (mission endings, 2026-11-15); Defense's unknown outcome is `stage.json`'s `ending` rule. |

## What players need to recreate

- The original rule: every objective active at the difficulty is required;
  death or abort fails the mission.
- An optional (bonus) objective that neither completes nor fails the mission.
- A mission that ends at once, failed, when a given objective fails (the
  hostage dies), or completed when a condition holds without walking to an
  exit.
- Something that happens when the mission's phase changes (a HUD line, music,
  an alarm) without an AI list polling objective statuses.

## The design

1. **Required objectives are data.** A row of kind `objective` in the
   mission's `objectives.json` (`pd2.mission.objectives.v1`, the table the
   runtime reads) may carry `"required": false`: the objective is shown and
   evaluated as now but is left out of the outcome (its failure does not fail
   the mission, the mission completes without it). Absent means required,
   the original rule. One source: conformance refuses `required` on a
   mission graph node and on the scenario's own objectives table. The
   extractor writes no `required` field for the base missions, so the
   extracted data and its cache kinds do not change.
2. **One seam for the outcome.** `objectiveIsAllComplete` asks the mission
   graph runtime on a stage with an active mission graph
   (`scenarioSourceMissionOutcome`), which aggregates the graph's statuses of
   the required objectives active at the difficulty; the 23 callers do not
   change. A ROM stage keeps the native loop. The co-op client aggregates the
   cached statuses (`SVC_OBJ_STATUS`) the same way, as it does today.
3. **The runtime owns the phase.** The recorder becomes the phase state of
   the active mission graph: it moves `load` -> `active` -> `complete` or
   `failed` -> `end` from the outcome above and from the stage's start and
   end, and each change fires `event.mission.phase_changed` (payload: the new
   phase) through the mission graph runner, so a graph can wire
   `gate.mission.phase` -> any `og.*` module. A transition the five phase
   nodes do not name is refused at activation, not at run time.
4. **A graph can end the mission.** `og.mission.end` (params `outcome`:
   `"failed"` or `"completed"`) ends the stage now through the same path as
   the AI's `end_level` (the fade, then `mainEndStage`; the host only in
   co-op and counter-op), and the outcome seam returns that outcome from then
   on. Allowed on `event.objective.status_changed` and
   `event.mission.phase_changed`; at most one per event walk.
5. **Fails closed.** On a stage built from scenario source:
   - a mission graph without the five phase nodes, with a `required` that is
     not a boolean, or with `og.mission.end` on another event is refused when
     it activates (the stage does not start, as for any refused mission
     graph);
   - a required objective the graph cannot evaluate stays incomplete, so the
     mission cannot complete; the outcome is never the native loop's answer;
   - a stage whose scenario declares objectives but has no mission graph is
     refused at activation (today its objectives stay incomplete, so it can
     never be completed, with one log line per objective).
6. **Proof.** The campaign parity probe gains the outcome: at every probe and
   at the end, the graph's outcome equals the native loop's
   (`outcome parity compared=N differ=0`); the release, Perfect Agent,
   special-assignment and soak campaigns require it. Since no base objective
   is optional, both must agree on every base mission.

## Slices

1. **Built on `week-pd/core` (2026-11-16):** `required` read from the
   mission's objective rows (`s_missionObjectiveReadRequired`; a bad value
   refuses the table, so the mission graph does not activate), the accessor
   `scenarioSourceMissionObjectiveRequired`, the two aggregates in
   `objectives.c` (the outcome in `objectiveIsAllComplete`, the phase in
   `objectivesCheckAll`) leaving an optional objective out, the campaign
   runner's outcome parity probe (`outcome parity probe stage_id=...
   outcome=...`, a failure on a difference), conformance, and pd-tests pins
   (`tests/test_mission_outcome_static.cpp`). The campaign fixtures require
   one outcome line per mission. No base data change.
2. **Built on `week-pd/core` (2026-11-16):** `event.mission.phase_changed`
   and `gate.mission.phase` in the runner (`"phase"`: `active`, `complete`
   or `failed`; load and end are not delivered), delivered by
   `scenarioSourceMissionGraphDeliverPhaseEvent` from `objectivesCheckAll`
   once per change, after the objectives' own events and never inside
   another walk. `og.mission.end` became two modules without params,
   `og.mission.end_failed` and `og.mission.end_completed` (the module table
   takes no params): from an objective or phase event they force the
   outcome (`objectiveIsAllComplete` returns it from then on, the phase holds
   it) and set a pending end that `player.c` takes after the objectives
   check, through the AI's `end_level` path (`func0000e990`). The first
   request wins. Refused on a network client (the host decides) and, for a
   forced completion, in co-op and counter-op until the stage end carries
   the outcome (the clients judge it from their cached statuses: MULTIPLAYER).
   A program that reaches either module from `event.mission.completed` or
   `event.mission.continue` is refused at activation, and conformance says
   the same. Proof: the runner's routing in pd-tests
   (`tests/test_mission_graph_phase.cpp`), the phase deliveries in the
   campaign logs (every base mission delivers `active` and `complete`). The
   in-game proof of `og.mission.end_*` and of `required: false` needs a
   playable test mission, which is custom-level work and waits for Forge
   (Mike, 2026-11-15).
3. Remove the native loop from the scenario-source path once the outcome
   parity has held on all four campaigns; it stays the ROM stage's.

## Not in scope

- Death, abort and the co-op partner's death stay the engine's end triggers;
  a graph sees them as the phase moving to `failed` and `end`.
- The end screen's layout and text; what a completed mission leads to stays
  the mission endings' events.
- The co-op client's copy of status messages (`netmsgSvcObjStatusRead`) is
  MULTIPLAYER's (mission-graph-runner.md, "Not yet").
