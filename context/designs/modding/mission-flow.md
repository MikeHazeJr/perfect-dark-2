# Mission flow: phase, outcome and end from the mission graph

Status: CORE, 2026-11-16. Design for the week note's "Mission flow (phases,
end, fail)" row; slice 1 is CORE's to build on `week-pd/core`. Decision 1
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

1. **Required objectives are data.** A `mission.objective.source` node may
   carry `"required": false`: the objective is shown and evaluated as now but
   is left out of the outcome (its failure does not fail the mission, the
   mission completes without it). Absent means required, the original rule.
   The extractor writes no `required` field for the base missions.
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

1. The outcome seam (`scenarioSourceMissionOutcome`, `required` read from the
   graph), the runtime-owned phase state with `event.mission.phase_changed`,
   the activation refusals, the outcome parity probe, conformance and runner
   compile rules, the mission cache kind (`_outcome`, SP-86). No authored
   change to the base data beyond the kind.
2. `og.mission.end` and `gate.mission.phase`, with a smoke that ends a
   mission from its graph (a test mission graph, not base content).
3. Remove the native loop from the scenario-source path once the outcome
   parity has held on all four campaigns; it stays the ROM stage's.

## Not in scope

- Death, abort and the co-op partner's death stay the engine's end triggers;
  a graph sees them as the phase moving to `failed` and `end`.
- The end screen's layout and text; what a completed mission leads to stays
  the mission endings' events.
- The co-op client's copy of status messages (`netmsgSvcObjStatusRead`) is
  MULTIPLAYER's (mission-graph-runner.md, "Not yet").
