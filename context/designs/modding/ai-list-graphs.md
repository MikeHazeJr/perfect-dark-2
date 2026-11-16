# AI lists as graphs: control flow as edges

Status: CORE, 2026-11-16. Design for the next step of the week note's "AI
commands and cutscene triggers" row (decision 1: players can recreate any
existing behavior with the nodes). Slice 1 built (see "Slice 1 as built");
slice 2 built but not yet run in play and without its fixtures (see "Slice 2
as built"); the two simulant twins built (see "Simulant twins as built");
slices 3-4 not built. The facts below come from a read-only survey of
`week-pd/core` and the extracted base 7 data on 2026-11-16.

## Where it stands

- 434 of the 441 AI command handlers had a graph twin
  (`scenarioSourceAiGraphExecute*`) that runs instead of the native handler on
  a stage built from scenario source. Seven had none: the interpreter's own
  control flow (`aiGoToNext`, `aiGoToFirst`, `aiLabel`, `aiYield`,
  `aiEndList`) and two simulant commands (`aiIfBotRespawning` 0x0176,
  `aiMpInitSimulants` 0x0185). The two simulant commands have one now (436
  of 441); the five left are the control flow the edges carry.
- The data has the commands as graph nodes but no control flow:
  `level.graph.json` holds one `scenario.ai.command` node per row
  (69,335 across the 87 scenarios) and links each to its list; no link
  leaves a command node. Labels, gotos, yields and ends exist only as
  `semantic_kind: scenario.ai.control.*` on command nodes.
- At run time nothing per-command survives loading: the rows compile to
  bytes, `graph_node` and `offset` are dropped, and the interpreter
  (`chraiExecute`) walks byte offsets. Twins move `g_Vars.aioffset` with
  literal command lengths and call `chraiGoToLabel` themselves.
- The global lists have rows (the `.pdai` bank) but no graph.

## The native control flow (what the edges must reproduce)

- A command's normal successor is the next command in the list.
- A branching command (158 `if_*`, 56 act-then-branch such as `try_*`,
  `find_cover*`, `spawn_chr_*`, and `if_bot_respawning`) jumps to the first
  `label` with its number at or after its own position (`chraiGoToLabel`
  scans forward). `go_to_next` does the same; `go_to_first` scans from the top
  of the list. Label numbers are not unique (720 of 1,644 lists repeat one),
  so the target is positional, and static: it can be computed once.
- A label that is not found restarts the list at offset 0 (real cases: two
  `if_chr_has_object` in Investigation 0x101f, `go_to_first` in Deep Sea
  0x0406 and Pelagic 0x0416).
- `yield` saves `(list, next command)` and ends the entity's tick; it is the
  only save point. `end` and a failed `set_camera_animation` end the tick
  without saving: the next tick resumes at the last yield (or wherever another
  entity or the engine last pointed the entity). 495 lists never yield; they
  end by switching list.
- Lists switch from outside: `set_list` on another chr (618 of 2,546), the
  engine's shot, dodge and dark-room lists at the start of a tick, the buddy
  stealth list, the aibot dead list, the cutscene background chr. Engine code
  compares `chr->ailist` against `ailistFindById(id)`.
- `return` jumps to the single `aireturnlist` slot at its top (no call stack).
- A refused or "handled without moving" command is skipped by the B-949
  guard: its branch is not taken.
- Loops are normal (6,023 of 6,031 `go_to_first` jump backwards) and some
  commands are unreachable (after a final `go_to_first`).

## Proposal

1. **Edges in the data.** The extractor writes control-flow edges between
   command nodes in `level.graph.json`, computed from the rows with the native
   rules above: `next` (to the following command), `branch` (a branching
   command's label target), `goto` (`go_to_next`, `go_to_first`), `restart`
   (a label that is not found: to the list's first command), and the
   `yield`, `end` and `return` and `set_list` exits as node properties, not
   edges. The same for the global lists: the `.pdai` bank gains its graph
   (`ai/global_ailists.graph.json`). Edges are `{from, to, kind}`; command
   nodes keep their ids. Unreachable commands stay (they are the original's)
   and are marked `reachable: false`.
2. **One source of truth, two forms.** The rows (bytes) and the edges must
   agree: conformance recomputes every edge from the rows and fails on a
   difference; the runtime does the same when a list compiles. Until the
   runtime walks edges (step 4), the bytes are the authority and the edges
   are their public, checked form.
3. **A shadow walk proves the edges in play.** The runtime builds a
   `(list, offset) -> node` table when a list compiles and, on a stage built
   from scenario source, follows each executed command's edge alongside the
   interpreter: after every command it predicts the next node from the edge
   the twin's outcome selects (next, branch taken or not, goto, restart,
   yield/end) and compares it with the native next offset. A mismatch is
   logged once per (list, command) and counted; the campaign runner reports
   `ai edge parity compared=N differ=0` per mission, and the campaign
   fixtures require it, as the objective and outcome probes do.
4. **The runtime walks edges.** Twins report an outcome instead of moving
   `g_Vars.aioffset` (`next`, `branch(taken)`, `stop`, `switch(list)`); the
   interpreter takes the edge. The cursor stays `(list, offset)` so the
   engine, the other readers of the bytes (`stageLoadAllAilistModels`, the net
   manifest scan, `chraiGetAilistLength`) and the pointer comparisons keep
   working; the offset is the node's, looked up through the table. A refused
   command takes its `next` edge (today's skip, made explicit).
5. **Authoring from edges.** With 1-4 in place, a list may be authored as
   nodes and edges: the compiler writes the bytes (labels and gotos) from the
   edges, so a player edits control flow in a graph editor and the game runs
   exactly what the graph says. The rows remain an equivalent form.

## Fail closed

- On a stage built from scenario source, a list whose edges do not match its
  rows refuses the stage at activation (like a refused level graph).
- In step 4, a node without the edge its outcome needs (a branch with no
  `branch` edge) refuses the list at compile time, not at run time.
- The two simulant commands without a twin get one before step 4 (they run
  only in multiplayer, on the host). MULTIPLAYER's semantics (week note,
  2026-11-16 12:44 UTC): `if_bot_respawning(label)` (0x0176) jumps to the label
  and clears the flag when the current chr is a simulant whose
  `aibot->respawning` is set, and goes on otherwise (no chr or not a simulant
  is false; the original reads `aibot` regardless and would crash);
  `mp_init_simulants` (0x0185) calls `botSpawnAll()` and, while the wave is not
  complete, stays on its command so the next pass retries (the twin ends the
  tick without moving: only a handler that goes on without moving is skipped
  by B-949), and goes on once it is. A networked client runs neither (its
  simulants are the host's).

## Slices

1. Edges written by the extractor (stage lists and the global bank),
   conformance recompute, the scenario and `.pdai` cache kinds move (SP-86).
2. The runtime's edge table and the shadow walk, the campaign parity line and
   fixtures (no change to play).
3. Twins report outcomes; the interpreter takes edges (play must not change:
   the shadow walk becomes the check that the walker and the bytes agree).
4. The compiler writes bytes from edges (authoring).

## Slice 1 as built

- `port/src/ai_list_edges.c` (pure, in the client and pd-tests): one list's
  commands (opcode, operand bytes) in, edges `next` / `branch` / `goto` /
  `restart` and flags `yields`, `ends`, `returns`, `switches_list`,
  `reachable` out. A command's edges come next first, then its jump. The
  branching table is 215 opcodes (every `g_CommandPointers` handler whose
  native path calls `chraiGoToLabel(g_Vars.ailist, g_Vars.aioffset, cmd[n])`,
  go_to_next left to the interpreter rules): label byte = the command's last
  byte except `if_chr_teleport_full_white` 0x01c0 (cmd[2]). pd-tests
  re-derives the table from `chrai.c`, `chraicommands.c` and
  `mpaicommands.c` and compares the conformance tool's copy with it.
- Stage lists: `level.graph.json` command nodes gain the flags (written only
  when true; `reachable` only when false) and each list's edges follow its
  `scenario.ai.lists` links as `{from, to, kind}` links. Base 7 rows give
  79,408 edges (58,325 next, 13,657 branch, 7,422 goto, 4 restart) and 2,578
  unreachable commands. Scenario kind gains `_aiedges` (SP-86).
- Global bank: `ai/global_ailists.graph.json` (`pd2.scenario.ai.graph.v1`,
  `{schema, nodes, links}`, node ids = the rows' `graph_node`), named by
  `ai.ini ai_lists_graph_file`; extract version and kind
  `global_ailists_rows_v2_aiedges` / `pdai_global_ailists_rows_v2_aiedges`.
  The meta walker ignores the extra key and member; the runtime reads only
  the rows.
- Conformance recomputes the edges from the rows (`ai_list_control_flow`)
  and reports every missing, extra or different edge or flag by list and
  command. A scenario must carry them when its arena's
  `scenario_graph_cache` is the new kind; a graph with any edge or flag is
  always checked; one with none and no such kind (older data) is not. A bank
  must carry its graph from the new extract version; a graph present is
  always checked.

## Slice 2 as built

- `port/src/ai_edge_shadow.c` (pure, in the client and pd-tests): one table
  per compiled list (its bytes, list id, each command's offset, opcode and
  flags, its edges by command), built with `aiListEdgesCompute` from the same
  commands the bytes were written from, in one lookup keyed by the list's
  bytes. Two owners, each following its bytes: a stage's lists
  (`s_setupBuildStageBlock` registers each under its bytes in the setup
  block; the main loop clears them just before it resets the stage pool) and
  the AI list banks (`scenarioSourceAiListsCompile` builds each list's table
  into `scenario_ai_compiled_list_t.edges`; `scenarioGlobalAiListsBindBank`
  hands them over after a whole bank compiles; `s_freeBanks` clears them
  before it frees the bytes; the round trip frees them unregistered). A
  stage's override of a global list runs the stage list, whose table is the
  stage's. A list without a table is logged at compile time and runs
  untracked.
- `chraiExecute`, on a stage built from scenario source only: every
  dispatched command is reported after its handler, a stop before the loop
  ends and any other command after the B-949 guard (marked when the guard
  skipped it). `aiEdgeShadowClassify` (pure; rules in the header): a stop
  agrees for a yield at its next edge's target and for end and
  set_camera_animation where they stand; leaving the list agrees for set_list
  on CHR_SELF and return (so does offset 0 of the same list); a skip on a
  node without a next edge is `refused_fallthrough`; anything else must land
  on one of the node's edge targets. A list or offset without a table, or
  bytes that are not the table's command, is `untracked`. Counters
  `compared`, `differ`, `refused_fallthrough`, `untracked`; a differ or
  fall-through is logged once per (list, command), the first untracked
  commands too, within caps (`AI.EDGES:` lines).
- The campaign runner zeroes the counters as it queues each mission and,
  after the objective and outcome parity probes, logs `ai edge parity
  stage_id='...' compared=N differ=N refused_fallthrough=N untracked=N` and
  fails the run (`ai edge parity: ...`, kind objectives) on any differ or
  untracked.
- Not yet: no campaign has run with the walk, and the campaign fixtures do
  not require the line.

## Simulant twins as built

- Every level graph carries two more module nodes, linked from
  `scenario.ai.lists`: `scenario.ai.condition.if_bot_respawning` (opcode
  0x0176, target `chr.aibot.respawning`) and
  `scenario.ai.action.mp_init_simulants` (0x0185, target
  `simulants.spawn_wave`). The runtime requires exactly one of each (the
  "simulant" family; a level graph without them refuses the stage) and logs
  `SCENARIO.GRAPH: AI simulant commands '...' if_bot_respawning=1
  mp_init_simulants=1`. Conformance and the typed examples carry them; the
  scenario kind gains `_simtwins` (SP-86).
- Where they run in base 7: `if_bot_respawning` once, in the global list
  0x0028 (the simulant dead list: yield, then jump to the respawn when it is
  pending, else loop); `mp_init_simulants` in the background list 0x1000 of
  each of the 24 multiplayer stages, before `rebuild_teams`,
  `rebuild_squadrons` and the switch to list 0x0000.
- `if_bot_respawning(label)`: MULTIPLAYER's semantics. The twin jumps to the
  label and clears the flag when the running chr is a simulant whose respawn
  is pending, and goes on otherwise; no chr, or a chr without an aibot, is
  false. The native handler read `aibot` through a null pointer in that case
  (the original's missing return); it now returns too (B-1254). Each branch
  outcome is logged once per stage (`... branch=0` and `... branch=1`).
- `mp_init_simulants`: MULTIPLAYER's semantics, which are also what the
  B-174 spawn wave was written for (`tests/test_combat_sim_defect_cohort.cpp`
  pins it). The twin calls `botSpawnAll()`; once the wave is complete the
  command goes on, and while it is not the entity's tick ends with the
  cursor on the command, so the next pass retries the retained wave
  cursor. The native handler does the same. B-1255: it used to return
  false without moving ("leave the opcode in place"), which the B-949
  guard skips, so the retry never ran from the AI list and
  `rebuild_teams` and `rebuild_squadrons` ran before the wave completed;
  only the host's botTick failsafe retried. The shadow walk accepts a stop
  in place for 0x0185, as for `set_camera_animation`. Each wave result is
  logged once per stage (`wave=complete`, `pending`, `invalid`). For
  MULTIPLAYER: a networked Combat Simulator client runs list 0x1000 too
  (its background chrs run their AI; only co-op and counter-op suppress
  non-simulant AI on a client), so the client calls `botSpawnAll()` and
  now holds its `rebuild_teams` until its own wave completes; its botTick
  failsafe never runs. If a client should not spawn at all, the twin can
  go on without spawning on `NETMODE_CLIENT`.
- Pins: `tests/test_ai_simulant_twins_static.cpp`.

## Not in scope

- Cutscene triggers and trigger volumes (their own row and nodes).
- AI actions and simulant behavior (`chraction.c`, `bot*.c`): native modules
  behind the commands; simulants are MULTIPLAYER's.
