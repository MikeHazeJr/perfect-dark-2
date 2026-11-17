/*
 * scenario_global_ailists.c -- step 5 of the global AI lists design: the
 * binary's global lists, written as public rows and compiled back through the
 * scenario AI list reader and compiler, must equal the binary's bytes.
 *
 * Kept apart from scenario_source_runtime.c, which reads no game globals
 * outside its source-gated helpers; this file reads g_GlobalAilists.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ultra64.h>
#include "constants.h"
#include "types.h"
#include "data.h"
#include "bss.h"
#include "game/chrai.h"
#include "romextract_pd.h"
#include "scenario_global_ailists.h"
#include "ai_edge_shadow.h"
#include "assetcatalog.h"
#include "fs.h"
#include "scenario_source_runtime.h"
#include "system.h"

/* The binary's list with this id, or NULL. */
static u8 *s_nativeListById(s32 id)
{
	for (s32 i = 0; g_GlobalAilists[i].list != NULL && i < 4096; i++) {
		if ((s32)g_GlobalAilists[i].id == id) {
			return g_GlobalAilists[i].list;
		}
	}
	return NULL;
}

/* Compares one compiled list with the binary's: every command's opcode,
 * operand bytes and length (chraiGetCommandLength on the binary list), and
 * the same end. Returns the first differing byte offset, or -1 when equal. */
static s32 s_compareList(const scenario_ai_compiled_list_t *compiled, u8 *native)
{
	u32 offset = 0;

	while (offset < compiled->size) {
		u32 native_len;
		u32 compiled_len;
		u16 opcode;

		if (offset + 2u > compiled->size) {
			return (s32)offset;
		}
		opcode = (u16)(((u16)compiled->bytes[offset] << 8) | compiled->bytes[offset + 1]);
		if (native[offset] != compiled->bytes[offset]
				|| native[offset + 1] != compiled->bytes[offset + 1]) {
			return (s32)offset;
		}
		native_len = chraiGetCommandLength(native, offset);
		compiled_len = chraiGetCommandLength(compiled->bytes, offset);
		if (native_len < 2u || native_len != compiled_len
				|| offset + native_len > compiled->size) {
			return (s32)offset;
		}
		if (memcmp(native + offset, compiled->bytes + offset, native_len) != 0) {
			for (u32 b = 0; b < native_len; b++) {
				if (native[offset + b] != compiled->bytes[offset + b]) {
					return (s32)(offset + b);
				}
			}
		}
		offset += native_len;
		if (opcode == AICMD_END) {
			/* The compiled list must end here too. */
			return offset == compiled->size ? -1 : (s32)offset;
		}
	}
	return (s32)offset;
}

/* Compiles the rows and compares every list with the binary's list of the
 * same id. Logs one AI.GLOBAL line per mismatch. Returns the number of
 * mismatches, or -1 when the rows cannot be read. */
static s32 s_roundTripRows(const char *json, const char *source, s32 native_count,
	u32 *out_bytes, s32 *out_count)
{
	char error[192];
	scenario_ai_compiled_list_t *lists = NULL;
	s32 count = 0;
	s32 mismatches = 0;
	u32 bytes = 0;

	if (!scenarioSourceAiListsCompile(json, &lists, &count, error, sizeof(error))) {
		sysLogPrintf(LOG_ERROR, "AI.GLOBAL: the global AI list rows of %s could not be read: %s",
			source, error);
		return -1;
	}
	if (count != native_count) {
		sysLogPrintf(LOG_ERROR, "AI.GLOBAL: %d lists in the rows of %s, %d in the binary",
			count, source, native_count);
		mismatches++;
	}
	for (s32 i = 0; i < count; i++) {
		u8 *native = s_nativeListById(lists[i].id);
		s32 at;

		if (!native) {
			sysLogPrintf(LOG_ERROR, "AI.GLOBAL: list 0x%04x is in the rows of %s but not in the binary",
				(unsigned)lists[i].id, source);
			mismatches++;
			continue;
		}
		at = s_compareList(&lists[i], native);
		if (at >= 0) {
			sysLogPrintf(LOG_ERROR, "AI.GLOBAL: list 0x%04x of %s differs at byte %d (%u commands, %u bytes compiled)",
				(unsigned)lists[i].id, source, at, lists[i].commands, lists[i].size);
			mismatches++;
			continue;
		}
		bytes += lists[i].size;
	}
	scenarioSourceAiListsFree(lists, count);
	if (out_bytes) *out_bytes = bytes;
	if (out_count) *out_count = count;
	return mismatches;
}

s32 scenarioGlobalAiListsRoundTrip(void)
{
	u32 written_lists = 0;
	u32 written_commands = 0;
	s32 native_count = 0;
	s32 mismatches;
	s32 count = 0;
	u32 bytes = 0;
	char *json = romExtractGlobalAiListsJson(&written_lists, &written_commands);
	const asset_entry_t *bank;

	if (!json) {
		sysLogPrintf(LOG_ERROR, "AI.GLOBAL: the global AI lists could not be written as rows");
		return -1;
	}
	for (s32 i = 0; g_GlobalAilists[i].list != NULL && i < 4096; i++) {
		native_count++;
	}

	/* The writer's rows (slice 1). */
	mismatches = s_roundTripRows(json, "the writer", native_count, &bytes, &count);
	free(json);
	if (mismatches < 0) {
		return -1;
	}
	if ((u32)count != written_lists) {
		sysLogPrintf(LOG_ERROR, "AI.GLOBAL: %d lists read back, %u written", count, written_lists);
		mismatches++;
	}
	sysLogPrintf(mismatches ? LOG_ERROR : LOG_NOTE,
		"AI.GLOBAL: round trip %d/%d lists exact, %u commands, %u bytes",
		count - (mismatches > count ? count : mismatches), native_count,
		written_commands, bytes);

	/* Slice 2, step 2: the rows of the extracted base asset base:ai_global
	 * (ai/base_ai_global.pdai), as the catalog registered them. */
	bank = assetCatalogResolve("base:ai_global");
	if (!bank || bank->type != ASSET_AI_LISTS || !bank->ext.ai_lists.lists_file[0]) {
		sysLogPrintf(LOG_ERROR, "AI.GLOBAL: base:ai_global is not in the catalog as an AI list bank");
		return mismatches + 1;
	}
	{
		u32 size = 0;
		char *text = (char *)fsFileLoad(bank->ext.ai_lists.lists_file, &size);
		s32 bank_mismatches;
		s32 bank_count = 0;
		u32 bank_bytes = 0;

		if (!text || !size) {
			sysLogPrintf(LOG_ERROR, "AI.GLOBAL: base:ai_global's rows could not be read (%s)",
				bank->ext.ai_lists.lists_file);
			free(text);
			return mismatches + 1;
		}
		bank_mismatches = s_roundTripRows(text, "base:ai_global", native_count,
			&bank_bytes, &bank_count);
		free(text);
		if (bank_mismatches < 0) {
			return mismatches + 1;
		}
		sysLogPrintf(bank_mismatches ? LOG_ERROR : LOG_NOTE,
			"AI.GLOBAL: base:ai_global round trip %d/%d lists exact, %u bytes",
			bank_count - (bank_mismatches > bank_count ? bank_count : bank_mismatches),
			native_count, bank_bytes);
		mismatches += bank_mismatches;
	}
	return mismatches;
}

/* Steps 3 and 5: the banks the stage lists come from, compiled once per
 * change. The base bank (base:ai_global, bundled) must carry every global
 * list; an enabled mod bank (a .pdai of a mod) may carry any of them, and its
 * lists replace the base's by id. When two mod banks carry the same list, the
 * smaller catalog id wins, the order .pdlang banks use. */
#define GLOBAL_AILISTS_MAX 128
#define GLOBAL_AILIST_BANKS_MAX 16
#define GLOBAL_AILIST_BANK_SIGNATURE (GLOBAL_AILIST_BANKS_MAX * (CATALOG_ID_LEN + FS_MAXPATH + 2))

typedef struct global_ailist_bank {
	const asset_entry_t *entry;
	scenario_ai_compiled_list_t *lists;
	s32 count;
} global_ailist_bank_t;

static struct {
	char signature[GLOBAL_AILIST_BANK_SIGNATURE];
	global_ailist_bank_t banks[GLOBAL_AILIST_BANKS_MAX]; /* [0] the base, then mods by id */
	s32 bank_count;
	u32 bytes;
} s_Bank;

/* Writes the reason the banks cannot be used and returns 0. */
static s32 s_bindFail(char *reason, u32 reason_size, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));
static s32 s_bindFail(char *reason, u32 reason_size, const char *fmt, ...)
{
	va_list args;

	if (reason && reason_size) {
		va_start(args, fmt);
		vsnprintf(reason, reason_size, fmt, args);
		va_end(args);
	}
	return 0;
}

static void s_freeBanks(void)
{
	/* AI lists as graphs, slice 2: the shadow walk's tables describe these
	 * bytes and go first. */
	aiEdgeShadowClearOwner(AI_EDGE_SHADOW_OWNER_BANK);
	for (s32 b = 0; b < s_Bank.bank_count; b++) {
		if (s_Bank.banks[b].lists) {
			scenarioSourceAiListsFree(s_Bank.banks[b].lists, s_Bank.banks[b].count);
		}
	}
	memset(&s_Bank, 0, sizeof(s_Bank));
}

static s32 s_isGameGlobalId(s32 id)
{
	for (s32 i = 0; g_GlobalAilists[i].list != NULL && i < GLOBAL_AILISTS_MAX; i++) {
		if ((s32)g_GlobalAilists[i].id == id) {
			return 1;
		}
	}
	return 0;
}

/* The enabled mod banks, by catalog id. Returns how many. */
static s32 s_modBanks(const asset_entry_t **out, s32 max)
{
	s32 count = 0;
	s32 pool = assetCatalogGetPoolSize();

	for (s32 i = 0; i < pool; i++) {
		const asset_entry_t *entry = assetCatalogGetByIndex(i);
		s32 at;

		if (!entry || !entry->occupied || entry->type != ASSET_AI_LISTS || !entry->enabled
				|| entry->bundled || !entry->ext.ai_lists.lists_file[0]) {
			continue;
		}
		if (count >= max) {
			sysLogPrintf(LOG_WARNING, "AI.GLOBAL: more than %d mod AI list banks; '%s' is not used",
				max, entry->id);
			continue;
		}
		at = count++;
		while (at > 0 && strcmp(entry->id, out[at - 1]->id) < 0) {
			out[at] = out[at - 1];
			at--;
		}
		out[at] = entry;
	}
	return count;
}

static s32 s_compileBank(global_ailist_bank_t *bank, const asset_entry_t *entry,
	char *reason, u32 reason_size)
{
	char error[192];
	u32 size = 0;
	char *text = (char *)fsFileLoad(entry->ext.ai_lists.lists_file, &size);

	bank->entry = entry;
	if (!text || !size) {
		free(text);
		return s_bindFail(reason, reason_size, "%s's rows could not be read (%s)",
			entry->id, entry->ext.ai_lists.lists_file);
	}
	if (!scenarioSourceAiListsCompile(text, &bank->lists, &bank->count, error, sizeof(error))) {
		free(text);
		return s_bindFail(reason, reason_size, "%s's rows do not compile: %s", entry->id, error);
	}
	free(text);
	for (s32 b = 0; b < bank->count; b++) {
		if (!s_isGameGlobalId(bank->lists[b].id)) {
			return s_bindFail(reason, reason_size,
				"%s has list 0x%04x, which is not one of the game's global lists",
				entry->id, (unsigned)bank->lists[b].id);
		}
	}
	return 1;
}

/* AI lists as graphs, slice 2 (ai-list-graphs.md): hands each compiled list's
 * edge table to the shadow walk's lookup under the list's bytes; the tables
 * go when s_freeBanks frees the bytes. Returns how many lists have one. */
static s32 s_registerEdgeTables(global_ailist_bank_t *bank)
{
	s32 registered = 0;

	for (s32 l = 0; l < bank->count; l++) {
		struct ai_edge_table *table = bank->lists[l].edges;

		bank->lists[l].edges = NULL;
		if (table && aiEdgeShadowRegister(AI_EDGE_SHADOW_OWNER_BANK, table)) {
			registered++;
		} else {
			aiEdgeShadowForget(bank->lists[l].bytes);
		}
	}
	return registered;
}

s32 scenarioGlobalAiListsBindBank(char *reason, u32 reason_size)
{
	const asset_entry_t *base = assetCatalogResolve("base:ai_global");
	const asset_entry_t *mods[GLOBAL_AILIST_BANKS_MAX - 1];
	char signature[GLOBAL_AILIST_BANK_SIGNATURE];
	s32 mod_count;
	s32 native_count = 0;
	s32 from_mods = 0;
	size_t used;

	if (reason && reason_size) {
		reason[0] = '\0';
	}
	if (!base || base->type != ASSET_AI_LISTS || !base->enabled || !base->ext.ai_lists.lists_file[0]) {
		return s_bindFail(reason, reason_size,
			"base:ai_global is not in the catalog as an enabled AI list bank");
	}
	mod_count = s_modBanks(mods, GLOBAL_AILIST_BANKS_MAX - 1);

	used = (size_t)snprintf(signature, sizeof(signature), "%s=%s;", base->id,
		base->ext.ai_lists.lists_file);
	for (s32 m = 0; m < mod_count && used < sizeof(signature); m++) {
		used += (size_t)snprintf(signature + used, sizeof(signature) - used, "%s=%s;",
			mods[m]->id, mods[m]->ext.ai_lists.lists_file);
	}

	if (strcmp(signature, s_Bank.signature) != 0 || s_Bank.bank_count == 0) {
		/* Nothing runs an AI list while a stage loads, so the previous
		 * compile can go before the table points at the new one. */
		s_freeBanks();
		if (!s_compileBank(&s_Bank.banks[0], base, reason, reason_size)) {
			s_Bank.bank_count = 1;
			s_freeBanks();
			return 0;
		}
		s_Bank.bank_count = 1;
		for (s32 m = 0; m < mod_count; m++) {
			s32 ok = s_compileBank(&s_Bank.banks[s_Bank.bank_count], mods[m], reason, reason_size);

			s_Bank.bank_count++;
			if (!ok) {
				s_freeBanks();
				return 0;
			}
		}
		snprintf(s_Bank.signature, sizeof(s_Bank.signature), "%s", signature);
		for (s32 b = 0; b < s_Bank.bank_count; b++) {
			s32 edge_tables;

			for (s32 l = 0; l < s_Bank.banks[b].count; l++) {
				s_Bank.bytes += s_Bank.banks[b].lists[l].size;
			}
			edge_tables = s_registerEdgeTables(&s_Bank.banks[b]);
			sysLogPrintf(LOG_NOTE, "AI.GLOBAL: compiled the AI list bank '%s': %d lists (%s)",
				s_Bank.banks[b].entry->id, s_Bank.banks[b].count,
				s_Bank.banks[b].entry->ext.ai_lists.lists_file);
			sysLogPrintf(LOG_NOTE, "AI.EDGES: %d of %d AI lists of the bank '%s' have edge tables",
				edge_tables, s_Bank.banks[b].count, s_Bank.banks[b].entry->id);
		}
	}

	/* The base must carry every one of the game's lists. */
	for (s32 i = 0; g_GlobalAilists[i].list != NULL && i < GLOBAL_AILISTS_MAX; i++) {
		s32 found = 0;

		for (s32 l = 0; l < s_Bank.banks[0].count; l++) {
			if (s_Bank.banks[0].lists[l].id == (s32)g_GlobalAilists[i].id) {
				found = 1;
				break;
			}
		}
		if (!found) {
			return s_bindFail(reason, reason_size, "base:ai_global has no global list 0x%04x",
				(unsigned)g_GlobalAilists[i].id);
		}
		native_count++;
	}

	/* Each list from the first mod bank (by id) that carries it, else the base. */
	for (s32 i = 0; g_GlobalAilists[i].list != NULL && i < GLOBAL_AILISTS_MAX; i++) {
		u8 *list = NULL;

		for (s32 b = 1; b < s_Bank.bank_count && !list; b++) {
			for (s32 l = 0; l < s_Bank.banks[b].count; l++) {
				if (s_Bank.banks[b].lists[l].id == (s32)g_GlobalAilists[i].id) {
					list = s_Bank.banks[b].lists[l].bytes;
					from_mods++;
					break;
				}
			}
		}
		if (!list) {
			for (s32 l = 0; l < s_Bank.banks[0].count; l++) {
				if (s_Bank.banks[0].lists[l].id == (s32)g_GlobalAilists[i].id) {
					list = s_Bank.banks[0].lists[l].bytes;
					break;
				}
			}
		}
		g_GlobalAilists[i].list = list;
	}
	if (from_mods) {
		sysLogPrintf(LOG_NOTE,
			"AI.GLOBAL: stage 0x%02x runs the %d global lists of '%s', %d of them from %d mod bank(s)",
			(unsigned)g_Vars.stagenum, native_count, s_Bank.banks[0].entry->id, from_mods,
			s_Bank.bank_count - 1);
	} else {
		sysLogPrintf(LOG_NOTE, "AI.GLOBAL: stage 0x%02x runs the %d global lists of '%s'",
			(unsigned)g_Vars.stagenum, native_count, s_Bank.banks[0].entry->id);
	}
	return 1;
}

s32 scenarioGlobalAiListsApplyStageOverrides(const struct ailist *stage_lists, s32 count,
	char *reason, u32 reason_size)
{
	s32 overrides = 0;

	if (reason && reason_size) {
		reason[0] = '\0';
	}
	for (s32 s = 0; stage_lists && s < count; s++) {
		s32 found = 0;

		if (stage_lists[s].id >= 0x401 || !stage_lists[s].list) {
			continue;
		}
		for (s32 i = 0; g_GlobalAilists[i].list != NULL && i < GLOBAL_AILISTS_MAX; i++) {
			if (g_GlobalAilists[i].id == stage_lists[s].id) {
				g_GlobalAilists[i].list = stage_lists[s].list;
				found = 1;
				overrides++;
				break;
			}
		}
		if (!found) {
			return s_bindFail(reason, reason_size,
				"the stage's ai/ailists.json has list 0x%04x, which is not one of the game's global lists",
				(unsigned)stage_lists[s].id);
		}
	}
	if (overrides) {
		sysLogPrintf(LOG_NOTE, "AI.GLOBAL: stage 0x%02x replaces %d global lists with its own (ai/ailists.json)",
			(unsigned)g_Vars.stagenum, overrides);
	}
	return 1;
}
