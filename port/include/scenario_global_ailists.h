/*
 * scenario_global_ailists.h -- the global AI lists as public source.
 *
 * The 46 global AI lists live in the binary (src/game/gailists.c,
 * g_GlobalAilists). Design: context/designs/modding/global-ai-lists.md. This
 * is its step 5: the lists, written as the same pd2.scenario.ai.lists.v1 rows
 * a scenario's ai/ailists.json holds, compile back through the scenario
 * reader and compiler to the exact bytes of the binary's lists.
 */

#ifndef SCENARIO_GLOBAL_AILISTS_H
#define SCENARIO_GLOBAL_AILISTS_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Writes every global list as rows (romExtractGlobalAiListsJson), compiles
 * the rows (scenarioSourceAiListsCompile) and compares each compiled list
 * with the binary's list of the same id, command by command. Logs one
 * AI.GLOBAL line per mismatch and a total. Returns the number of lists that do
 * not match (0: every list is exact), or -1 when the rows cannot be written
 * or read. Run with --check-global-ailists. */
s32 scenarioGlobalAiListsRoundTrip(void);

/* Global AI lists slice 2, step 3 (global-ai-lists.md): points every entry of
 * g_GlobalAilists at the list of the same id compiled from the AI list bank
 * (base:ai_global's public rows), so a stage built from scenario source runs
 * the public lists. The bank is compiled once and again only when the catalog
 * entry or its rows member changes; ids and order are untouched, so
 * ailistFindById and the pointer-to-id lookup in chrai.c work as before.
 * Returns 1, or 0 with the reason when the bank is missing, unreadable, lacks
 * one of the game's list ids or names a list id the game does not have (the
 * caller refuses the stage). */
s32 scenarioGlobalAiListsBindBank(char *reason, u32 reason_size);

#ifdef __cplusplus
}
#endif

#endif
