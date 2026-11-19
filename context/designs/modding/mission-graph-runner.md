# Mission graph runner

Status: first unit built on `week-pd/core`, 2026-11-15 (Mike's decision 2:
"build the mission-graph runner now"). Owner: CORE.

## What it is

The first executable layer of the mission graph (`pd2.mission.graph.v1`,
`mission.graph.json` in every `.pdmission`), on the D-006 option A model:
authored events, connections and gates drive named native modules. Before this,
the runtime loaded the mission graph, counted its phase nodes and read its
objective rows, but never walked an edge.

Code: `port/include/mission_graph_runner.h` (the contract),
`port/src/mission_graph_runner.cpp` (compile and fire, no game state),
`port/src/mission_graph_modules.c` (the game's `og.*` module table),
`scenario_source_runtime.c` (compiles the program when the mission graph
activates, frees it at reset, fires the events), `src/game/objectives.c` (the
first event seam). Tests: `tests/test_mission_graph_runner.cpp` (`[runner]`).

## Graph rules

- A mission graph keeps its data nodes (phases, objectives, criteria) and the
  structural edges between them. Structural edges are checked (both ends must
  exist) and never walked.
- An execution edge has `"input": "exec"`. It may start at an executable event,
  a gate or a native module, and must end at a gate or a native module.
- Output ports: a gate's edges need `"output": "pass"` or `"fail"`; an event's
  or a module's edges have `"output": "out"` or no output.
- One event activation walks every matching event node's execution edges
  breadth first, in authored order. Each reached node runs at most once per
  event, so fan-in and cycles cannot repeat work. A gate follows its `pass`
  edges when it passes and its `fail` edges when it does not. A module that
  refuses the event does not continue to its own successors; the others run,
  and the event reports the refusal.
- A graph that breaks a rule is refused when it compiles (logged once:
  `MISSION.GRAPH: refused the executable program`). A refused program still owns
  its events, so they run nothing; nothing falls back to native code.
- Limits: 4,096 nodes and 16,384 edges, refused above that, not truncated.
- Conformance (`tools/asset_archive_conformance.py`,
  `validate_mission_graph_execution`) applies the same rules to archives, and a
  test keeps its kind lists equal to the runner's.

## Kinds

| Kind | What | Params |
|---|---|---|
| `event.objective.status_changed` | An objective that applies at the current difficulty changed status (`objectivesCheckAll`). | `objective`: `"any"` (default) or a `mission.objective.source` node id |
| `gate.objective.status` | Passes when the event's status is `status`. | `status`: `"complete"`, `"incomplete"` or `"failed"`; optional `objective` |
| `og.hud.objective_status_message` | The original "Objective N: Completed / Incomplete / Failed" message to every player (`objectivesShowStatusMessage`). | none |
| `event.mission.completed` | A solo mission was completed without cheats; the game has just recorded the completion (best time, unlocks), before its end screen shows (`endscreenPrepare`). | none |
| `event.mission.continue` | What a completed mission leads to. Seam `endscreen`: the player continues from its end screen (on a first completion it continues by itself); a mission with a handler skips the next-mission menu. Seam `coop_stage_end`: a co-op stage whose continue reaches `og.mission.start_next_stage` has just ended; its co-op end screen is skipped (`menutick.c`). | none |
| `og.mission.start_next_stage` | The next solo stage starts at once (the original Deep Sea rule); from the end screen it closes instead when that stage is not unlocked at this difficulty. | none |
| `og.mission.start_credits` | The credits start, from the end screen (the original Skedar Ruins rule). The PC end screen labels its action Credits when the continue event reaches this module. | none |
| `og.progress.unlock_alt_title` | The alternate title screen unlocks once and the boss file is saved (the original Skedar Ruins rule). | none |

Mission events carry no objective: `objective` on their nodes is refused and an
objective gate never passes for them. Any other `event.*` kind
(`event.mission.load` today) may only have structural edges until the runner
executes it. An `og.*` kind the module table does not
know refuses the graph.

The event carries the runtime objective index, its number among the objectives
that apply (the "N" of the message), the new and the previous status, and the
objective's node id.

## Base content

The mission extractor (`romextract_pdmeta.c`, `s_buildMissionGraph`) writes into
every base mission graph:

```json
{ "id": "objective.status_changed", "kind": "event.objective.status_changed", "objective": "any" },
{ "id": "objective.status_message", "kind": "og.hud.objective_status_message" },
...
{ "from": "objective.status_changed", "output": "out", "to": "objective.status_message", "input": "exec" }
```

so the base game shows the same messages as before, now because its graph says
so. A mission graph that leaves the edge out shows no objective messages; one
that adds a gate can show the message for completions only. The pdmeta cache
kind gained `_objective_status_event` and a mission archive without an
execution edge is not clean, so installs re-extract their missions once.

## Mission endings (CORE, 2026-11-15)

What a completed mission leads to and what its completion unlocks run from its
mission graph on a scenario-source stage; a ROM stage keeps the original stage
checks. The extractor writes them from the stages the game checks:

| Mission | Nodes |
|---|---|
| Deep Sea | `event.mission.continue` -> `og.mission.start_next_stage` |
| Skedar Ruins | `event.mission.continue` -> `og.mission.start_credits`; `event.mission.completed` -> `og.progress.unlock_alt_title` |

Seams: `endscreenContinue` (the original menus) fires the continue event when
the mission graph handles it, else the default next-mission flow; the PC end
screen (`pdgui_bridge.c`) asks whether the continue event reaches
`og.mission.start_credits` for its Credits action and otherwise keeps its own
Next Mission advance, which already starts the next stage; the co-op stage end
(`menutick.c`) asks whether it reaches `og.mission.start_next_stage`. That
co-op branch sits in menuTick's root -6 ("match is ending") path, which only
`mpEndMatch` requests (`mplayer.c`); a co-op mission ends through
`mainEndStage`, which pushes the co-op end screens directly, in the original
(`src/lib/main.c`) as in this port (`port/src/pdmain.c`). So the branch and its
`coop_stage_end` seam are never reached, and co-op Deep Sea continues from its
end screen, as the original does (MULTIPLAYER's question, 2026-11-16). When the
continue event runs nothing (a module refused), the end screen closes rather
than leaving the player on it. A mission without these nodes ends as any other.
Defense's unknown outcome is a presentation rule of the stage, so it is the
`ending` family of `stage.json` (`stage-rules.md`). The pdmeta cache kind gained
`_mission_endings`.

## Not yet

- The co-op client shows its own copy of the message when the host's status
  update arrives (`netmsgSvcObjStatusRead`, `port/src/net/netmsg.c`); it should
  fire the same event. MULTIPLAYER owns that file; asked in the week note.
- More events (mission phase changes, mission load and end), gates and modules
  (mission end, generic HUD text, stage flags) follow the same contract; each
  new kind needs its row in the module table, the runner's compile rules, the
  conformance lists and this table.
- Per-instance state, waiting modules and continuations from the D-006A design
  (`executable-behavior-graphs-2026-09.md`) are not needed by the first event
  and are not built.
