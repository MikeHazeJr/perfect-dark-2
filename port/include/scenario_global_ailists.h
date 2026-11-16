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

#ifdef __cplusplus
}
#endif

#endif
