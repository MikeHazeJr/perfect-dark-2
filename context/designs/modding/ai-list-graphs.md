# AI lists as graphs: control flow as edges

Status: CORE, 2026-11-16. Design for the next step of the week note's "AI
commands and cutscene triggers" row (decision 1: players can recreate any
existing behavior with the nodes). Slice 1 built (see "Slice 1 as built");
slices 2-4 not built. The facts below come from a read-only survey of
`week-pd/core` and the extracted base 7 data on 2026-11-16.

## Where it stands

- 434 of the 441 AI command handlers have a graph twin
  (`scenarioSourceAiGraphExecute*`) that runs instead of the native handler on
  a stage built from scenario source. Seven have none: the interpreter's own
  control flow (`aiGoToNext`, `aiGoToFirst`, `aiLabel`, `aiYield`,
  `aiEndList`) and two simulant commands (`aiIfBotRespawning` 0x0176,
  `aiMpInitSimulants` 0x0185).
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

## Not in scope

- Cutscene triggers and trigger volumes (their own row and nodes).
- AI actions and simulant behavior (`chraction.c`, `bot*.c`): native modules
  behind the commands; simulants are MULTIPLAYER's.
