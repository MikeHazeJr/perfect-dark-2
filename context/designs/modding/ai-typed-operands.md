# AI commands with typed operands

Status: CORE, 2026-11-16. Design for the remaining target of the week note's
"AI commands and cutscene triggers" row ("typed operands, parsed nodes,
trigger-volume events"), under decision 1 (players can recreate any existing
behavior with the nodes). Not built. The facts below come from a read-only
survey of `week-pd/core` (0d2022b8) and the extracted base 7 data.

## Where it stands

- Every AI command is a row of `ai/ailists.json` (`pd2.scenario.ai.lists.v1`;
  the global lists use the same rows in `base:ai_global`) and a
  `scenario.ai.command` node of `level.graph.json`. A row carries its
  parameters as raw bytes: `if_chr_dead` is `"operands": ["0x24", "0x06"]`
  (chr 0x24, label 6), `try_equip_weapon` eight bytes. Four optional fields
  (`model_catalog_id`, `weapon_catalog_id`, `body_catalog_id`,
  `head_catalog_id`) name catalog entries whose runtime numbers the compiler
  writes over the matching bytes (`s_aiApplyCatalogOperands`). Command nodes
  carry the opcode, its name and `semantic_kind`, no parameters.
- The runtime knows the parameters: 436 of the 441 handlers pass decoded
  operands to a graph twin whose parameters are named. Survey of the twin
  calls: 539 arguments read one operand byte (`cmd[n]`), 20 combine bytes in
  the call (`cmd[n] | (cmd[m] << 8)`, a signed byte, a mask), 242 come from a
  local the handler decoded first; 176 distinct parameter names (`label` 205,
  `chrnum` 97, `tag_id` 48, `flags` 34, `distance` 20, ...), with synonyms
  (`pad`, `padnum`, `pad_id`). 88 handlers read some operand bytes some other
  way or not at all (`aiLabel`, padding bytes, no-ops, struct overlays).
- Lengths are fixed per opcode (`g_CommandLengths`), except `print` (0x00b5),
  whose text ends at a NUL.

A player editing a list today edits bytes against the source code. Forge's
AI editing and hand-written lists need named, typed parameters.

## Proposal

1. **One schema for every command.** `pd2.scenario.ai.command_schema.v1`:
   per opcode its name, length (or `variable` for `print`) and fields in byte
   order, each `{name, offset, width (1, 2 or 4), signed, kind}`. Kinds:
   `label`, `chr` (with the special ids `self`, `bond`, `target` and the
   rest by name), `pad`, `object` (tag), `room`, `ai_list`, `anim`, `sound`,
   `text`, `team`, `squadron`, `flags`, `number`, `bool`, `string`, and the
   catalog kinds `model`, `weapon`, `body`, `head`. Every operand byte belongs
   to exactly one field; unused bytes are fields of kind `padding` (written
   as zero, never shown). Names follow one vocabulary (`pad`, not `padnum`).
   The schema is a C table (`port/src/ai_command_schema.c`, pure, in the
   client and pd-tests) with a copy in the conformance tool, as the edge
   rules are (slice 1 of ai-list-graphs.md): pd-tests compares the copy and
   re-derives each field's offset and width from the handler that reads it.
2. **Rows carry arguments.** A row gains `"args": {"chr": 36, "label": 6}`;
   catalog kinds take catalog ids (`"weapon": "base:cmp150"`), so the four
   `*_catalog_id` fields fold into `args`. The compiler encodes `args`
   through the schema into the same bytes it writes today; `operands` stays
   only while both forms ship (slice 2) and must agree, then goes (slice 3).
3. **Nodes carry the same arguments** (parsed nodes): each command node gets
   the row's `args`, and conformance checks node against row, so a graph
   tool reads one self-describing file.
4. **Fails closed.** A missing, unknown or out-of-range argument, a catalog
   id that does not resolve, or `args` that disagree with `operands` refuses
   the list, and with it the stage (as a refused level graph).
5. **Proof.** Every base row (69,335 stage commands and the 4,268 of the
   bank) round-trips: decoded through the schema and encoded back, the bytes
   are unchanged; in the client, every list compiled from `args` equals the
   list compiled from `operands` (a check like `--check-global-ailists`);
   the campaigns and the shadow walk show play unchanged.

## Slices

1. The schema table, its conformance copy and the pd-tests checks (every
   operand byte covered once; offsets and widths re-derived from the
   handlers; the copy equal), plus a conformance pass that decodes every base
   row with it. No data change.
2. The extractor writes `args` in rows and nodes; the compiler encodes
   `args` and refuses a row whose `args` and `operands` disagree; the client
   check compares both compiles for every list. Scenario and `.pdai` kinds
   move (SP-86).
3. `operands` and the `*_catalog_id` fields leave the rows (schema
   `pd2.scenario.ai.lists.v2`); a v1 row is refused with a message naming v2.
   Kinds move again. A schema notice in the week note first.
4. Trigger-volume events: `event.volume.entered` and `event.volume.left` on
   `volumes.json` rows, delivered by the level graph runtime, so a list (or
   a module) can react without polling `if_chr_in_room`. Its own design; the
   base game keeps polling, which the AI lists already express.

## Not in scope

- Changing what a command does; the twins stay as they are.
- Label semantics: a label argument is a number, found forward from the
  command as today; the edges of ai-list-graphs.md show where it lands.
