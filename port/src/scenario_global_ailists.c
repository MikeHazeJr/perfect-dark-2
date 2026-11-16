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

/* Step 3: the bank the stage lists come from, compiled once per change. */
#define GLOBAL_AILISTS_MAX 128

static struct {
	const asset_entry_t *entry;
	char lists_file[FS_MAXPATH + 1];
	scenario_ai_compiled_list_t *lists;
	s32 count;
	u32 bytes;
} s_Bank;

/* Writes the reason the bank cannot be used and returns 0. */
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

s32 scenarioGlobalAiListsBindBank(char *reason, u32 reason_size)
{
	const asset_entry_t *bank = assetCatalogResolve("base:ai_global");
	s32 native_count = 0;

	if (reason && reason_size) {
		reason[0] = '\0';
	}
	if (!bank || bank->type != ASSET_AI_LISTS || !bank->ext.ai_lists.lists_file[0]) {
		return s_bindFail(reason, reason_size,
			"base:ai_global is not in the catalog as an AI list bank");
	}
	if (bank != s_Bank.entry || strcmp(bank->ext.ai_lists.lists_file, s_Bank.lists_file) != 0
			|| !s_Bank.lists) {
		char error[192];
		u32 size = 0;
		char *text = (char *)fsFileLoad(bank->ext.ai_lists.lists_file, &size);
		scenario_ai_compiled_list_t *lists = NULL;
		s32 count = 0;

		if (!text || !size) {
			free(text);
			return s_bindFail(reason, reason_size,
				"base:ai_global's rows could not be read (%s)", bank->ext.ai_lists.lists_file);
		}
		if (!scenarioSourceAiListsCompile(text, &lists, &count, error, sizeof(error))) {
			free(text);
			return s_bindFail(reason, reason_size, "base:ai_global's rows do not compile: %s", error);
		}
		free(text);
		/* Nothing runs an AI list while a stage loads, so the previous
		 * compile can go before the table points at the new one. */
		if (s_Bank.lists) {
			scenarioSourceAiListsFree(s_Bank.lists, s_Bank.count);
		}
		s_Bank.entry = bank;
		snprintf(s_Bank.lists_file, sizeof(s_Bank.lists_file), "%s", bank->ext.ai_lists.lists_file);
		s_Bank.lists = lists;
		s_Bank.count = count;
		s_Bank.bytes = 0;
		for (s32 i = 0; i < count; i++) {
			s_Bank.bytes += lists[i].size;
		}
		sysLogPrintf(LOG_NOTE, "AI.GLOBAL: compiled the AI list bank '%s': %d lists, %u bytes (%s)",
			bank->id, count, s_Bank.bytes, s_Bank.lists_file);
	}

	/* Every list the bank carries must be one of the game's, and every one
	 * of the game's must be in the bank. */
	for (s32 b = 0; b < s_Bank.count; b++) {
		s32 found = 0;
		for (s32 i = 0; g_GlobalAilists[i].list != NULL && i < GLOBAL_AILISTS_MAX; i++) {
			if ((s32)g_GlobalAilists[i].id == s_Bank.lists[b].id) {
				found = 1;
				break;
			}
		}
		if (!found) {
			return s_bindFail(reason, reason_size,
				"base:ai_global has list 0x%04x, which is not one of the game's global lists",
				(unsigned)s_Bank.lists[b].id);
		}
	}
	for (s32 i = 0; g_GlobalAilists[i].list != NULL && i < GLOBAL_AILISTS_MAX; i++) {
		u8 *list = NULL;
		for (s32 b = 0; b < s_Bank.count; b++) {
			if (s_Bank.lists[b].id == (s32)g_GlobalAilists[i].id) {
				list = s_Bank.lists[b].bytes;
				break;
			}
		}
		if (!list) {
			return s_bindFail(reason, reason_size,
				"base:ai_global has no global list 0x%04x", (unsigned)g_GlobalAilists[i].id);
		}
		native_count++;
	}
	for (s32 i = 0; g_GlobalAilists[i].list != NULL && i < GLOBAL_AILISTS_MAX; i++) {
		for (s32 b = 0; b < s_Bank.count; b++) {
			if (s_Bank.lists[b].id == (s32)g_GlobalAilists[i].id) {
				g_GlobalAilists[i].list = s_Bank.lists[b].bytes;
				break;
			}
		}
	}
	sysLogPrintf(LOG_NOTE, "AI.GLOBAL: stage 0x%02x runs the %d global lists of '%s'",
		(unsigned)g_Vars.stagenum, native_count, s_Bank.entry->id);
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
