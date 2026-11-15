# Global AI lists as public source (design, CORE, 2026-11-15)

Status: proposed, not started. Checklist row "Global AI lists" (week note,
CORE's behavior conversion checklist). Decision 1: "Players should be able to
recreate any existing game behavior with the nodes."

## Where they are today

`src/game/gailists.c` (6,152 lines) holds the 46 global AI lists as C byte
arrays, indexed by `g_GlobalAilists[]` (ids below 0x0401: idle, the unalerted
and alerted guard loops, wake-up, the end-cinema list and the rest). Every
stage's characters fall back to them; stage AI lists jump into them. Unlike the
per-stage lists, they are compiled into the binary, so no player can read or
change them.

The per-stage lists are already public: each scenario carries
`ai/ailists.json` (`pd2.scenario.ai.lists.v1`), one row per command
(`list_id`, `command_index`, `offset`, `opcode`, `opcode_name`, `operands`,
the `model`/`weapon`/`body`/`head` catalog ids, a `graph_node` name), and the
runtime compiles those rows into the interpreter's byte lists at stage load
(`scenario_source_runtime.c`, "AI list source").

## Proposal

1. **Same schema, same compiler.** The global lists are written as
   `pd2.scenario.ai.lists.v1` rows with their own ids (below 0x0401). Nothing
   new to learn for a player, and one compiler for both.
2. **One base asset, written from the game's own table.** The extractor
   serialises `g_GlobalAilists` into one public file in a base asset (working
   name `base:ai_global`, a typed archive holding `ai/global_ailists.json`),
   the same way `stage.json` is written from the game's tables. A mod may ship
   its own copy; the catalog picks it over the base one, as it picks `.pdlang`
   banks.
3. **Per-level override.** A scenario's own `ai/ailists.json` may also hold
   rows with a global list id; for that level they replace the global list of
   that id. This lets a custom level change a shared behaviour (say, how its
   guards wake up) without touching every other level.
4. **Fail closed.** On a stage built from scenario source, the global lists
   come from the source: a missing or invalid global asset refuses the stage
   with the same fatal path as other required members
   (`scenarioSourceFatalRuntimeFallbackForStage`), never a silent fall back to
   the compiled table. A ROM stage keeps the compiled table.
5. **Proof.** The extracted rows must compile back to the exact bytes of
   `gailists.c` for every list (a unit test, like the setup round trips), and
   the release campaign must run unchanged.

## Open questions

- The asset type: a new typed archive (`.pdai`) or a member of an existing
  base archive. A new type keeps mod overrides simple and is the
  recommendation.
- Whether the 5 interpreter control-flow handlers (`aiGoToNext`,
  `aiGoToFirst`, `aiLabel`, `aiYield`, `aiEndList`) become graph edges first
  (checklist row "AI commands"); the rows above do not need them to.
