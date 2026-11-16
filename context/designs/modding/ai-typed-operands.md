# AI commands with typed operands

Status: CORE, 2026-11-16. Design for the remaining target of the week note's
"AI commands and cutscene triggers" row ("typed operands, parsed nodes,
trigger-volume events"), under decision 1 (players can recreate any existing
behavior with the nodes). Slices 1, 2 and 4 built (see "Slice 1 as built",
"Slice 2 as built" and trigger-volume-events.md); slice 3 not built. The facts below come from a read-only survey of `week-pd/core`
(0d2022b8) and the extracted base 7 data.

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
   `volumes.json` rows, so a mission graph can react without polling
   `if_chr_in_room`; the base game keeps polling, which the AI lists already
   express. Built in the mission graph runner (trigger-volume-events.md).

## Slice 1 as built

- `port/include/ai_command_schema.h` and `port/src/ai_command_schema.c`
  (pure, in the client and pd-tests): 441 commands, one per handler of
  `g_CommandPointers` (0x01b4 is the NTSC 1.0 side of its `#if`), 819
  fields; lookups by opcode and field name, kind names, and big-endian
  field reads and writes with range checks. Widths are 1 to 4 bytes (the
  timer comparisons hold a 24-bit tick count). Kinds as proposed, plus
  `portal` (`set_portal_flag`, an index into the stage's portals).
- How it was made: a generator read each handler's decoding (locals built
  from `cmd[]` bytes, bytes passed to `chrFindById`, `objFindByTagId` and
  `chraiGoToLabel`, bytes passed to the twin and the twin's parameter
  names), then two reviewers went through every handler by hand (struct
  overlays such as `try_damage`'s `gset` split into its members, `bytelist`
  reads, 24-bit values, signedness from the casts; 292 of the 441 entries
  changed from the draft). Every base row decodes and re-encodes to its
  bytes (73,603 rows).
- Choices worth knowing: a byte no handler reads is kind `unused`; six
  such fields hold nonzero bytes in 20 base rows (`set_team_orders`,
  `if_orders`, `recover_gun`, `release_object`, `clear_inventory`,
  `if_music_event_queue_is_empty`: mostly a chr id the PC handler ignores),
  so slice 2 writes an unused field into `args` only when it is not zero,
  which keeps those rows byte-exact while authors see only what acts. Two
  fields have two meanings: `chr_move_to_pad`'s pad (its low byte is a chr
  when the mode is 88) and `play_sound_from_entity`'s `entity` (a chr or an
  object tag by `entity_is_chr`, so kind `number`, as are the `entity`
  fields of the `try_attack_*` family). Sound ids are signed, as the
  handlers read them (bit 15 is a flag). `play_sound_from_prop` reads
  `cmd[10]` twice, so its byte 9 is unused (the handler's own quirk, kept).
- Checks: pd-tests `tests/test_ai_command_schema.cpp` (every handler has
  its command and length; the fields cover every operand byte once; every
  byte a handler reads is a used field, and the bytes passed to the label,
  chr and object lookups are fields of that kind, with the two dual-use
  fields as named exceptions; field reads and writes; the conformance copy
  equals the table). Conformance (`AI_COMMAND_SCHEMA`,
  `ai_row_operand_error`) checks every AI row of a scenario and of a
  `.pdai` bank: a known opcode, its name, its operand count, and print's
  single NUL. No data change.

## Slice 2 as built

- Pure helpers in `ai_command_schema.c`: `aiCommandArgsToJson` (a command's
  bytes to `{"chr": 36, "label": 6}`: fields in byte order, an unused field
  only when it is not zero, a bool field holding 0 or 1 as `false` or
  `true`, print's text as a JSON string) and `aiCommandArgsEncode` (back to
  bytes: any key order, every used field required, an unknown or repeated
  key, a value outside its field's range, a non-number or a malformed object
  refused with a reason).
- The extractor writes `"args"` after `"operands"` on every row of a
  scenario's `ai/ailists.json` and of the bank, and on every command node of
  `level.graph.json` and of `ai/global_ailists.graph.json`; a command the
  schema cannot type fails the archive. Scenario kind `_typedargs`; the bank's
  extract version and kind `global_ailists_rows_v3_typedargs` (SP-86).
- The compiler: a row with `args` encodes them and must get exactly its
  operand bytes, checked before the catalog ids patch them, or the list is
  refused (`AI row '<list> command <n>' args ...`). A row may also give
  `args` alone (no `operands`, no `*_catalog_id` fields): they are its
  bytes, so a list can be authored by name today; a row without `args` is
  the v1 form and compiles from its operands; a row with neither is
  refused. Every stage load and bank compile is the check that the two
  forms agree. The mod bank fixture (`tests/fixtures/modpipe/ai-lists-bank`)
  is written as args only.
- Conformance: `ai_command_args` and `ai_command_encode` (the same rules in
  Python); a row needs operands or args; per row, args equal to what the
  operands decode to (an args-only row's edges come from its encoded
  bytes); command nodes carry their
  row's args (`ai_args_node_errors`); every row must have them when the
  scenario's arena is the new kind or the bank's extract version is v3 (the
  version gate that requires the bank's graph takes v2 and v3).
- Checks: pd-tests round-trips every command through both helpers at the
  top and bottom of each field's range and pins the refusals, the call
  sites and their order; the conformance unit tests cover matching and
  mismatched args, nodes and the v3 requirement. All 73,603 base rows
  decode to args and back in Python.

## Not in scope

- Changing what a command does; the twins stay as they are.
- Label semantics: a label argument is a number, found forward from the
  command as today; the edges of ai-list-graphs.md show where it lands.
