# Global AI lists as public source (design, CORE, 2026-11-15)

Status: plan written 2026-11-16 (below). Slice 1 built on `week-pd/core-gai`: step 5's proof (rows, compile, byte comparison with `--check-global-ailists`), no asset type or runtime switch yet. Checklist row "Global AI lists" (week note,
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

## Implementation plan (CORE, 2026-11-16; recommendation taken, not started)

Asset type: a new typed archive `.pdai` (`ASSET_AI_LISTS`), one base
asset `base:ai_global` in `data/<romid>/ai/` holding `ai.ini` and
`ai/global_ailists.json` (the same `pd2.scenario.ai.lists.v1` rows; ids
below 0x0401). A new type touches about twenty places, the ones
`ASSET_LANG` is wired through: the enum (`assetcatalog.h`), type names
(`assetcatalog_load.c`, `asset_archive_writer.c`,
`asset_fallback_telemetry.c`, `asset_source_debug.c`, `main.c` both
ways), the scanner's folder, section and descriptor tables
(`assetcatalog_scanner.c`), the archive policy row (`asset_archive_policy.c`),
the mod contract row (`asset_mod_utility_contract.c`), the runtime switches
(`asset_runtime.c`), a loader walker (`loader_walker_ai.c`, like
`loader_walker_lang.c`), the mod manager and modpack lists
(`modmgr_component_catalog.cpp`, `modpack.c`), plus the conformance family
and its tests.

1. Extraction: a writer next to `s_buildAiListsTable` that walks
   `g_GlobalAilists` (pointer and id per list, commands up to `endlist` by
   `chraiGetCommandLength`) into the same rows, with its own fast-cache kind
   and a boot phase in `main.c`.
2. Proof, in the client (the writer and the row compiler live in
   game-dependent files, so pd-tests cannot link them): a boot check that
   compiles the extracted rows back and compares them byte for byte with
   every list of `g_GlobalAilists`, logging one line per list on a mismatch
   and a total (`AI.GLOBAL: 46 lists round-trip exact`). The release
   campaign must run unchanged.
3. Runtime: in `setupLoadFiles` (`setup.c`), after the global lists are
   sorted, a stage built from scenario source points each
   `g_GlobalAilists[i].list` at the list compiled from the source (ids and
   order unchanged, so `ailistFindById` and the pointer-to-id lookup in
   `chrai.c` keep working); a ROM stage keeps the compiled table. A missing
   or invalid global asset refuses the stage through
   `scenarioSourceFatalRuntimeFallbackForStage`. The graph twins of the AI
   commands already run on any list's commands, so global lists need no new
   graph handlers.
4. Per-level override: rows in a scenario's own `ai/ailists.json` with a
   global id replace that global list for that level only.
5. Mods: the catalog picks a mod's `.pdai` over the base one, as it picks
   `.pdlang` banks.

## Open questions

- The asset type: a new typed archive (`.pdai`) or a member of an existing
  base archive. A new type keeps mod overrides simple and is the
  recommendation.
- Whether the 5 interpreter control-flow handlers (`aiGoToNext`,
  `aiGoToFirst`, `aiLabel`, `aiYield`, `aiEndList`) become graph edges first
  (checklist row "AI commands"); the rows above do not need them to.
