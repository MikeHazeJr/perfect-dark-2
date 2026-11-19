# Trigger-volume events

Status: CORE, 2026-11-16. The last target of the week note's "AI commands
and cutscene triggers" row ("trigger-volume events"), slice 4 of
ai-typed-operands.md. Built in the mission graph runner and the scenario
runtime; pd-tests prove the routing. No base mission uses them, so their
in-game proof needs a playable test mission (custom content, after Forge),
as `og.mission.end_*` does.

## Why

The original reacts to a player reaching a place by polling from an AI list
(`if_chr_in_room`, `if_player_in_room_range` and kin), and the lists are
public source with typed arguments, so that behavior is already
recreatable. What a level author does not have is the other way round: "when
a player enters this box, do X" without writing a polling list. The stage
already describes boxes: every scenario's `volumes.json` row
(`pd2.scenario.volumes.v1`: `volume_id`, `kind`, `shape` `aabb`, `min`,
`max`, `room_ref`) is a `scenario.trigger.volume.source` node of
`level.graph.json`, and the AI room checks already resolve pads through them.

## Design

- Two event kinds in `mission.graph.json`, executable in the mission graph
  runner: `event.volume.entered` and `event.volume.left`, each with a
  required `"volume"`: the `volume_id` of a row of the stage's
  `volumes.json` (no "any": a stage has hundreds of pad volumes). An
  `"objective"` filter on them is refused. They run gates and `og.*` modules
  like any other event; objective and phase gates never pass for them.
- The event carries the volume id and the player (index). The runner lists
  the distinct volumes its program names (`missionGraphProgramVolumeCount`,
  `missionGraphProgramVolumeAt`), at most `MISSION_GRAPH_MAX_VOLUMES`.
- When the mission graph activates, the runtime binds every named volume to
  its row (an axis-aligned box); a name with no row, or a row that is not an
  `aabb`, refuses the program, as an unknown module does.
- Delivery: in `objectivesCheckAll`, after the phase event (while objective
  checks run, never inside another walk), the runtime tests each bound
  volume against each player's position and fires `entered` when a player
  goes from outside to inside and `left` for the reverse, logging each
  delivery (`MISSION.GRAPH: event.volume.entered '<mission>' volume=<id>
  player=<n> modules=<m>`). A player starts outside, so one who spawns inside
  enters on the first check. A networked client delivers none (the host's
  graph decides), as for the end modules.
- Conformance knows the kinds and requires `"volume"`.

## Not in scope

- Chrs other than players, and shapes other than boxes.
- Authoring the boxes (Forge).
