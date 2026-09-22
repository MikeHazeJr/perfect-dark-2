/**
 * scenario_save.c -- Transactional public-source Combat Simulator scenarios.
 *
 * A saved match JSON file is parsed into an isolated document, resolved into a
 * complete match candidate, and only then published. No parser, migration, or
 * catalog-resolution failure is allowed to mutate live match state.
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <PR/ultratypes.h>

#include "game/mplayer/participant.h"
#include "game/mplayer/mplayer.h"

#include "scenario_save.h"
#include "scenario_codec.h"
#include "asset_runtime.h"
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "catalog_activation_ledger.h"
#include "catalog_stage_ownership.h"
#include "constants.h"
#include "fs.h"
#include "options_forced.h"
#include "player_identity.h"
#include "save_atomic.h"
#include "scenario_source_runtime.h"
#include "system.h"

#define SCENARIO_SAVE_VERSION 3
#define SCENARIO_FILE_EXTENSION ".json"

typedef struct scenario_source_ownership_snapshot_t {
	/* Source-derived resident products are rebuildable cache. This snapshot
	 * covers the mutable ownership truth that a candidate must restore. */
	char id[CATALOG_ID_LEN];
	s32 valid;
	s32 stage_owned;
	s32 entry_ref_count;
	s32 ledger_present;
	asset_type_e ledger_type;
	s32 ledger_references;
	s32 ledger_stage_owned;
	s32 ledger_bundled;
} scenario_source_ownership_snapshot_t;

typedef struct scenario_load_plan_t {
	struct matchconfig config;
	scenario_source_ownership_snapshot_t source_ownership;
	s32 source_stage_ref_acquired;
	s32 source_version;
	s32 migrated;
	s32 legacy_v3_hybrid;
	s32 bot_count;
	s32 resolved_weapon_set;
} scenario_load_plan_t;

static void scenarioCopyId(char out[CATALOG_ID_LEN], const char *id)
{
	strncpy(out, id, CATALOG_ID_LEN - 1);
	out[CATALOG_ID_LEN - 1] = '\0';
}

static s32 scenarioSetDetail(char *detail, size_t detail_size,
		const char *message, const char *value)
{
	if (detail && detail_size > 0) {
		snprintf(detail, detail_size, "%s%s%s", message,
			value && value[0] ? ": " : "", value && value[0] ? value : "");
		detail[detail_size - 1] = '\0';
	}
	return 0;
}

static void getSaveDir(char *out, s32 size)
{
	fsFullPath("$S", out, (size_t)size);
}

static void getScenarioDir(char *out, s32 size)
{
	char savedir[SCENARIO_PATH_MAX];
	getSaveDir(savedir, sizeof(savedir));
	snprintf(out, (size_t)size, "%s/scenarios", savedir);
}

static void sanitizeName(const char *in, char *out, s32 maxlen)
{
	s32 i;
	s32 j = 0;
	s32 start;
	s32 len;

	for (i = 0; in[i] && j < maxlen - 1; i++) {
		unsigned char c = (unsigned char)in[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
				|| (c >= '0' && c <= '9') || c == ' ' || c == '-'
				|| c == '_' || c == '.') {
			out[j++] = (char)c;
		} else {
			out[j++] = '_';
		}
	}
	out[j] = '\0';
	start = 0;
	while (out[start] == ' ') start++;
	if (start > 0) memmove(out, out + start, (size_t)(j - start + 1));
	len = (s32)strlen(out);
	while (len > 0 && out[len - 1] == ' ') out[--len] = '\0';
	if (!out[0]) {
		strncpy(out, "scenario", (size_t)maxlen - 1);
		out[maxlen - 1] = '\0';
	}
}

static void scenarioJsonWriteEscaped(FILE *fp, const char *text)
{
	const unsigned char *p = (const unsigned char *)text;
	for (; *p; p++) {
		switch (*p) {
		case '"': fputs("\\\"", fp); break;
		case '\\': fputs("\\\\", fp); break;
		case '\b': fputs("\\b", fp); break;
		case '\f': fputs("\\f", fp); break;
		case '\n': fputs("\\n", fp); break;
		case '\r': fputs("\\r", fp); break;
		case '\t': fputs("\\t", fp); break;
		default:
			if (*p >= 0x20) fputc((int)*p, fp);
			break;
		}
	}
}

static s32 scenarioTextSerializable(const char *text)
{
	const unsigned char *p = (const unsigned char *)text;
	for (; *p; p++) {
		if (*p < 0x20 && *p != '\b' && *p != '\f' && *p != '\n'
				&& *p != '\r' && *p != '\t') return 0;
	}
	return 1;
}

static s32 scenarioEntryUsable(const asset_entry_t *entry, asset_type_e type)
{
	return entry && entry->occupied && entry->enabled && entry->type == type;
}

static const catalog_activation_root_t *scenarioFindActivationRoot(
		const char *id)
{
	const catalog_activation_root_t *match = NULL;
	size_t i;

	for (i = 0; i < catalogActivationLedgerActiveCount(); i++) {
		const catalog_activation_root_t *root =
			catalogActivationLedgerActiveAt(i);
		if (!root || strcmp(root->id, id) != 0) continue;
		if (match) return NULL;
		match = root;
	}
	return match;
}

static s32 scenarioCaptureSourceOwnership(const asset_entry_t *entry,
		scenario_source_ownership_snapshot_t *snapshot, char *detail,
		size_t detail_size)
{
	const catalog_activation_root_t *root;
	s32 stage_owned;

	if (!scenarioEntryUsable(entry, ASSET_SCENARIO) || !snapshot) {
		return scenarioSetDetail(detail, detail_size,
			"invalid public ASSET_SCENARIO ownership candidate", NULL);
	}
	memset(snapshot, 0, sizeof(*snapshot));
	root = scenarioFindActivationRoot(entry->id);
	stage_owned = catalogStageOwnershipHasRef(entry);
	if (entry->stage_ref_count < 0 || entry->stage_ref_count > 1
			|| (root && (root->type != ASSET_SCENARIO
				|| root->references < 1
				|| root->bundled != (entry->bundled ? 1 : 0)))
			|| stage_owned != (root && root->stage_owned ? 1 : 0)) {
		return scenarioSetDetail(detail, detail_size,
			"public ASSET_SCENARIO ownership ledger is inconsistent", entry->id);
	}
	scenarioCopyId(snapshot->id, entry->id);
	snapshot->valid = 1;
	snapshot->stage_owned = stage_owned;
	snapshot->entry_ref_count = entry->ref_count;
	snapshot->ledger_present = root != NULL;
	if (root) {
		snapshot->ledger_type = root->type;
		snapshot->ledger_references = root->references;
		snapshot->ledger_stage_owned = root->stage_owned;
		snapshot->ledger_bundled = root->bundled;
	}
	return 1;
}

static s32 scenarioSourceOwnershipMatches(
		const scenario_source_ownership_snapshot_t *snapshot)
{
	const asset_entry_t *entry;
	const catalog_activation_root_t *root;

	if (!snapshot || !snapshot->valid) return 1;
	entry = assetCatalogResolve(snapshot->id);
	if (!scenarioEntryUsable(entry, ASSET_SCENARIO)
			|| catalogStageOwnershipHasRef(entry) != snapshot->stage_owned
			|| entry->ref_count != snapshot->entry_ref_count) return 0;
	root = scenarioFindActivationRoot(snapshot->id);
	if ((root != NULL) != snapshot->ledger_present) return 0;
	return !root || (root->type == snapshot->ledger_type
		&& root->references == snapshot->ledger_references
		&& root->stage_owned == snapshot->ledger_stage_owned
		&& root->bundled == snapshot->ledger_bundled);
}

static s32 scenarioReleasePlanSource(scenario_load_plan_t *plan,
		char *detail, size_t detail_size)
{
	const asset_entry_t *entry;

	if (!plan->source_ownership.valid) return 1;
	entry = assetCatalogResolve(plan->source_ownership.id);
	if (plan->source_stage_ref_acquired) {
		if (!scenarioEntryUsable(entry, ASSET_SCENARIO)
				|| !catalogStageOwnershipHasRef(entry)) {
			return scenarioSetDetail(detail, detail_size,
				"temporary ASSET_SCENARIO stage ownership was lost",
				plan->source_ownership.id);
		}
		catalogReleaseStageAsset(ASSET_SCENARIO,
			plan->source_ownership.id);
		plan->source_stage_ref_acquired = 0;
	}
	if (!scenarioSourceOwnershipMatches(&plan->source_ownership)) {
		return scenarioSetDetail(detail, detail_size,
			"temporary ASSET_SCENARIO ownership did not restore exactly",
			plan->source_ownership.id);
	}
	plan->source_ownership.valid = 0;
	return 1;
}

static const asset_entry_t *scenarioFindLegacyUnique(asset_type_e type,
		s32 legacy_value)
{
	const asset_entry_t *match = NULL;
	s32 i;

	for (i = 0; i < assetCatalogGetPoolSize(); i++) {
		const asset_entry_t *entry = assetCatalogGetByIndex(i);
		s32 value;
		if (!scenarioEntryUsable(entry, type)) continue;
		switch (type) {
		case ASSET_ARENA: value = entry->ext.arena.stagenum; break;
		case ASSET_GAMEMODE: value = entry->ext.gamemode.mode_id; break;
		case ASSET_WEAPON:
		case ASSET_BODY:
		case ASSET_HEAD: value = entry->mp_index; break;
		default: return NULL;
		}
		if (value != legacy_value) continue;
		if (match) return NULL;
		match = entry;
	}
	return match;
}

static s32 scenarioResolveArena(const scenario_document_t *document,
		scenario_load_plan_t *plan, char out_id[CATALOG_ID_LEN],
		u8 *out_stagenum, char *detail, size_t detail_size)
{
	const asset_entry_t *entry;
	const asset_entry_t *scenario;
	const asset_runtime_binding_t *runtime;
	catalog_stage_result_t stage = {0};

	if ((document->present & SCENARIO_DOC_ARENA_ID) != 0) {
		if (!document->arena_id[0]
				|| !catalogResolveStageForType(document->arena_id,
					ASSET_ARENA, &stage)
				|| !scenarioEntryUsable(stage.entry, ASSET_ARENA)
				|| stage.stagenum < 0 || stage.stagenum > 255) {
			return scenarioSetDetail(detail, detail_size,
				"invalid ASSET_ARENA catalog ID", document->arena_id);
		}
		entry = stage.entry;
	} else {
		entry = scenarioFindLegacyUnique(ASSET_ARENA,
			document->legacy_arena);
		if (!entry
				|| !catalogResolveStageForType(entry->id, ASSET_ARENA, &stage)
				|| !scenarioEntryUsable(stage.entry, ASSET_ARENA)
				|| stage.entry != entry
				|| stage.stagenum != document->legacy_arena
				|| stage.stagenum != entry->ext.arena.stagenum
				|| stage.net_hash != entry->net_hash
				|| stage.stagenum < 0 || stage.stagenum > 255) {
			return scenarioSetDetail(detail, detail_size,
				"legacy arena cannot produce a complete canonical stage result",
				NULL);
		}
	}
	/* A saved arena is publishable only when its MP stage resolves to the
	 * public .pdscenario source that stage activation will consume. The
	 * runtime binding is parsed/cache metadata; it cannot replace or fall
	 * back from the accessible public source member. */
	scenario = scenarioSourceFindEntryForStage(&stage, 1);
	if (!scenarioEntryUsable(scenario, ASSET_SCENARIO)
			|| scenario->ext.scenario.stagenum != stage.stagenum) {
		return scenarioSetDetail(detail, detail_size,
			"public ASSET_SCENARIO source does not match arena", entry->id);
	}
	if (!scenarioCaptureSourceOwnership(scenario, &plan->source_ownership,
			detail, detail_size)) return 0;
	if (!plan->source_ownership.stage_owned
			&& !catalogCanDeactivateTypedAsset(ASSET_SCENARIO, scenario->id)) {
		return scenarioSetDetail(detail, detail_size,
			"public ASSET_SCENARIO source cannot balance stage activation",
			scenario->id);
	}
	{
		s32 activated = catalogLoadStageAsset(ASSET_SCENARIO, scenario->id);
		if (!plan->source_ownership.stage_owned
				&& catalogStageOwnershipHasRef(scenario)) {
			plan->source_stage_ref_acquired = 1;
		}
		if (!activated || !catalogStageOwnershipHasRef(scenario)) {
			return scenarioSetDetail(detail, detail_size,
				"public ASSET_SCENARIO source activation failed", scenario->id);
		}
	}
	runtime = assetRuntimeFindByTypeAndId(ASSET_SCENARIO, scenario->id);
	if (!runtime || !runtime->active || runtime->type != ASSET_SCENARIO
			|| strcmp(runtime->id, scenario->id) != 0
			|| runtime->runtime_id != stage.stagenum
			|| !assetRuntimePrimaryFileAccessible(runtime)) {
		return scenarioSetDetail(detail, detail_size,
			"activated public ASSET_SCENARIO source is not runtime-ready",
			scenario->id);
	}
	scenarioCopyId(out_id, entry->id);
	*out_stagenum = (u8)stage.stagenum;
	return 1;
}

static s32 scenarioResolveGameMode(const scenario_document_t *document,
		char out_id[CATALOG_ID_LEN], u8 *out_mode, char *detail,
		size_t detail_size)
{
	const asset_entry_t *entry;
	const asset_runtime_binding_t *runtime;

	if ((document->present & SCENARIO_DOC_SCENARIO_ID) != 0) {
		entry = assetCatalogResolve(document->scenario_id);
		if (!document->scenario_id[0]
				|| !scenarioEntryUsable(entry, ASSET_GAMEMODE)) {
			return scenarioSetDetail(detail, detail_size,
				"invalid ASSET_GAMEMODE catalog ID", document->scenario_id);
		}
	} else {
		entry = scenarioFindLegacyUnique(ASSET_GAMEMODE,
			document->legacy_scenario);
		if (!entry) {
			return scenarioSetDetail(detail, detail_size,
				"legacy game-mode index is absent or ambiguous", NULL);
		}
	}
	if (entry->ext.gamemode.mode_id < 0 || entry->ext.gamemode.mode_id > 255) {
		return scenarioSetDetail(detail, detail_size,
			"game-mode index is outside the runtime domain", entry->id);
	}
	/* The public typed gamemode source is game-facing authority. Its hydrated
	 * runtime binding may cache parsed fields, but a catalog-only or missing
	 * source entry is not a loadable match mode. */
	runtime = assetRuntimeFindByTypeAndId(ASSET_GAMEMODE, entry->id);
	if (!runtime || !runtime->active || !runtime->source_hydrated
			|| runtime->type != ASSET_GAMEMODE
			|| strcmp(runtime->id, entry->id) != 0
			|| runtime->runtime_id != entry->ext.gamemode.mode_id
			|| !assetRuntimePrimaryFileAccessible(runtime)) {
		return scenarioSetDetail(detail, detail_size,
			"ASSET_GAMEMODE public source is not runtime-ready", entry->id);
	}
	scenarioCopyId(out_id, entry->id);
	*out_mode = (u8)entry->ext.gamemode.mode_id;
	return 1;
}

static s32 scenarioResolveWeaponId(const char *id,
		char out_id[CATALOG_ID_LEN], u8 *out_mp_weapon, char *detail,
		size_t detail_size)
{
	const asset_entry_t *entry;

	if (!id[0]) {
		out_id[0] = '\0';
		*out_mp_weapon = MPWEAPON_NONE;
		return 1;
	}
	entry = assetCatalogResolve(id);
	if (!scenarioEntryUsable(entry, ASSET_WEAPON)
			|| entry->mp_index <= MPWEAPON_NONE || entry->mp_index > 255
			|| entry->runtime_index <= 0
			|| catalogGetMpWeaponNum(entry->mp_index) != entry->runtime_index) {
		return scenarioSetDetail(detail, detail_size,
			"invalid ASSET_WEAPON catalog ID", id);
	}
	scenarioCopyId(out_id, entry->id);
	*out_mp_weapon = (u8)entry->mp_index;
	return 1;
}

static s32 scenarioResolveLegacyWeapon(s32 legacy_value,
		char out_id[CATALOG_ID_LEN], u8 *out_mp_weapon, char *detail,
		size_t detail_size)
{
	const asset_entry_t *entry;
	if (legacy_value == MPWEAPON_NONE) {
		out_id[0] = '\0';
		*out_mp_weapon = MPWEAPON_NONE;
		return 1;
	}
	entry = scenarioFindLegacyUnique(ASSET_WEAPON, legacy_value);
	if (!scenarioEntryUsable(entry, ASSET_WEAPON)
			|| entry->mp_index <= MPWEAPON_NONE || entry->mp_index > 255
			|| entry->runtime_index <= 0
			|| catalogGetMpWeaponNum(entry->mp_index) != entry->runtime_index) {
		return scenarioSetDetail(detail, detail_size,
			"legacy weapon index is absent or ambiguous", NULL);
	}
	scenarioCopyId(out_id, entry->id);
	*out_mp_weapon = (u8)entry->mp_index;
	return 1;
}

static s32 scenarioResolveIdentity(const char *typed_id, s32 typed_present,
		s32 legacy_value, asset_type_e type, char out_id[CATALOG_ID_LEN],
		char *detail, size_t detail_size)
{
	const asset_entry_t *entry;

	if (typed_present) {
		entry = assetCatalogResolve(typed_id);
		if (!typed_id[0] || !scenarioEntryUsable(entry, type)) {
			return scenarioSetDetail(detail, detail_size,
				type == ASSET_BODY ? "invalid ASSET_BODY catalog ID"
					: "invalid ASSET_HEAD catalog ID", typed_id);
		}
	} else {
		entry = scenarioFindLegacyUnique(type, legacy_value);
		if (!entry) {
			return scenarioSetDetail(detail, detail_size,
				type == ASSET_BODY
					? "legacy body index is absent or ambiguous"
					: "legacy head index is absent or ambiguous", NULL);
		}
	}
	scenarioCopyId(out_id, entry->id);
	return 1;
}

static const asset_runtime_binding_t *scenarioResolveBotProfile(
		const char *profile_id)
{
	const asset_entry_t *entry = assetCatalogResolve(profile_id);
	const asset_runtime_binding_t *profile =
		assetRuntimeFindByTypeAndId(ASSET_BOT_PROFILE, profile_id);

	if (!scenarioEntryUsable(entry, ASSET_BOT_PROFILE)
			|| !profile || !profile->active || !profile->source_hydrated
			|| profile->type != ASSET_BOT_PROFILE
			|| strcmp(profile->id, profile_id) != 0
			|| !assetRuntimePrimaryFileAccessible(profile)
			|| profile->bot_profile_type < 0
			|| profile->bot_profile_type > 255
			|| profile->bot_profile_difficulty < 0
			|| profile->bot_profile_difficulty > 255) {
		return NULL;
	}
	return profile;
}

static u8 scenarioChooseBotTeam(const struct matchconfig *candidate)
{
	s32 count[2] = { 0, 0 };
	s32 i;
	for (i = 0; i < candidate->numSlots; i++) {
		const struct matchslot *slot = &candidate->slots[i];
		if (slot->type != SLOT_EMPTY && slot->team < 2) count[slot->team]++;
	}
	return count[1] < count[0] ? 1 : 0;
}

static s32 scenarioPrepareWeapons(const scenario_document_t *document,
		scenario_load_plan_t *plan, char *detail, size_t detail_size)
{
	s32 typed_count = 0;
	s32 legacy_count = 0;
	s32 i;

	for (i = 0; i < NUM_MPWEAPONSLOTS; i++) {
		typed_count += document->weapon_id_present[i] ? 1 : 0;
		legacy_count += document->legacy_weapon_present[i] ? 1 : 0;
	}
	if (typed_count != 0 && typed_count != NUM_MPWEAPONSLOTS) {
		return scenarioSetDetail(detail, detail_size,
			"typed weapon authority is incomplete", NULL);
	}
	if (typed_count == NUM_MPWEAPONSLOTS) {
		for (i = 0; i < NUM_MPWEAPONSLOTS; i++) {
			if (!scenarioResolveWeaponId(document->weapon_ids[i],
					plan->config.weapon_ids[i], &plan->config.weapons[i],
					detail, detail_size)) return 0;
		}
	} else if (legacy_count != 0) {
		if (legacy_count != NUM_MPWEAPONSLOTS) {
			return scenarioSetDetail(detail, detail_size,
				"legacy weapon migration input is incomplete", NULL);
		}
		for (i = 0; i < NUM_MPWEAPONSLOTS; i++) {
			if (!scenarioResolveLegacyWeapon(document->legacy_weapons[i],
					plan->config.weapon_ids[i], &plan->config.weapons[i],
					detail, detail_size)) return 0;
		}
	} else {
		u8 prepared[NUM_MPWEAPONSLOTS];
		s32 resolved = func0f188f9c(document->weaponset);
		if (resolved == WEAPONSET_RANDOM || resolved == WEAPONSET_RANDOMFIVE
				|| resolved == WEAPONSET_CUSTOM
				|| mpPrepareWeaponSet(document->weaponset, NULL,
					prepared, &resolved) != 0) {
			return scenarioSetDetail(detail, detail_size,
				"v1 weapon set cannot be migrated unambiguously", NULL);
		}
		for (i = 0; i < NUM_MPWEAPONSLOTS; i++) {
			if (!scenarioResolveLegacyWeapon(prepared[i],
					plan->config.weapon_ids[i], &plan->config.weapons[i],
					detail, detail_size)) return 0;
		}
	}
	plan->resolved_weapon_set = WEAPONSET_CUSTOM;
	plan->config.weaponSetIndex = (s8)document->weaponset;
	return 1;
}

static s32 scenarioHasV3HybridCompanions(
		const scenario_document_t *document)
{
	return document->version == 3
		&& (document->present & SCENARIO_DOC_LEGACY_ARENA) != 0;
}

static s32 scenarioValidateTypedCompanions(
		const scenario_document_t *document,
		const scenario_load_plan_t *plan, char *detail, size_t detail_size)
{
	s32 i;
	if ((document->present & SCENARIO_DOC_ARENA_ID) != 0
			&& (document->present & SCENARIO_DOC_LEGACY_ARENA) != 0
			&& document->legacy_arena != (s32)plan->config.stagenum) {
		return scenarioSetDetail(detail, detail_size,
			"numeric arena companion contradicts typed ASSET_ARENA", NULL);
	}
	if ((document->present & SCENARIO_DOC_SCENARIO_ID) != 0
			&& (document->present & SCENARIO_DOC_LEGACY_SCENARIO) != 0
			&& document->legacy_scenario != (s32)plan->config.scenario) {
		return scenarioSetDetail(detail, detail_size,
			"numeric scenario companion contradicts typed ASSET_GAMEMODE", NULL);
	}
	for (i = 0; i < NUM_MPWEAPONSLOTS; i++) {
		if (document->weapon_id_present[i]
				&& document->legacy_weapon_present[i]
				&& document->legacy_weapons[i]
					!= (s32)plan->config.weapons[i]) {
			return scenarioSetDetail(detail, detail_size,
				"numeric weapon companion contradicts typed ASSET_WEAPON", NULL);
		}
	}
	return 1;
}

static s32 scenarioBuildPlanCandidate(const scenario_document_t *document,
		s32 human_count, scenario_load_plan_t *plan, char *detail,
		size_t detail_size)
{
	/* The legacy mutating matchConfigAddBotWithProfile( path is intentionally
	 * excluded: saved "profileId" authority is resolved into plan->config and
	 * every bot slot is published with the rest of the match exactly once. */
	s32 actual_humans = 0;
	s32 max_bots;
	s32 i;

	memset(plan, 0, sizeof(*plan));
	if (human_count < 1 || human_count > MATCH_PARTICIPANT_CAP) {
		return scenarioSetDetail(detail, detail_size,
			"human count is outside the participant domain", NULL);
	}
	for (i = 0; i < g_MatchConfig.numSlots && i < MATCH_MAX_SLOTS; i++) {
		if (g_MatchConfig.slots[i].type == SLOT_PLAYER) {
			if (actual_humans >= human_count
					|| actual_humans >= MATCH_PARTICIPANT_CAP) {
				return scenarioSetDetail(detail, detail_size,
					"live human slots exceed declared human count", NULL);
			}
			plan->config.slots[actual_humans++] = g_MatchConfig.slots[i];
		}
	}
	if (actual_humans < 1) {
		return scenarioSetDetail(detail, detail_size,
			"live match has no human slot to preserve", NULL);
	}
	if (actual_humans != human_count) {
		return scenarioSetDetail(detail, detail_size,
			"live human slots do not match declared human count", NULL);
	}
	plan->config.numSlots = (u8)actual_humans;
	if (!scenarioResolveArena(document, plan, plan->config.stage_id,
			&plan->config.stagenum, detail, detail_size)
			|| !scenarioResolveGameMode(document, plan->config.scenario_id,
				&plan->config.scenario, detail, detail_size)) return 0;
	plan->config.timelimit = (u8)document->timelimit;
	plan->config.scorelimit = (u8)document->scorelimit;
	plan->config.teamscorelimit = (u16)document->teamscorelimit;
	plan->config.options = document->options;
	plan->config.options_engine_forced = 0;
	if (!scenarioPrepareWeapons(document, plan, detail, detail_size)
			|| !scenarioValidateTypedCompanions(document, plan, detail,
				detail_size)) return 0;

	plan->config.spawnWeaponMode = (u8)(((document->present
		& SCENARIO_DOC_SPAWN_WEAPON_MODE) != 0)
		? document->spawn_weapon_mode
		: (((document->present & SCENARIO_DOC_SPAWN_WEAPON_ID) != 0)
			&& document->spawn_weapon_id[0]
				? SPAWNWEAPON_MODE_SPECIFIC : SPAWNWEAPON_MODE_RANDOM));
	if (plan->config.spawnWeaponMode == SPAWNWEAPON_MODE_SPECIFIC) {
		u8 ignored;
		if ((document->present & SCENARIO_DOC_SPAWN_WEAPON_ID) == 0
				|| !document->spawn_weapon_id[0]
				|| !scenarioResolveWeaponId(document->spawn_weapon_id,
					plan->config.spawn_weapon_id, &ignored, detail,
					detail_size)) return 0;
	} else if (plan->config.spawnWeaponMode == SPAWNWEAPON_MODE_RANDOM
			|| plan->config.spawnWeaponMode == SPAWNWEAPON_MODE_FIESTA) {
		if ((document->present & SCENARIO_DOC_SPAWN_WEAPON_ID) != 0
				&& document->spawn_weapon_id[0]) {
			return scenarioSetDetail(detail, detail_size,
				"random/fiesta spawn mode must not carry a weapon ID", NULL);
		}
		plan->config.spawn_weapon_id[0] = '\0';
	} else {
		return scenarioSetDetail(detail, detail_size,
			"spawn mode is outside its enum domain", NULL);
	}
	plan->config.spawnWeaponNum = 0;

	max_bots = matchConfigMaxBotsForHumans(human_count);
	if (document->bot_count > max_bots
			|| actual_humans + document->bot_count > MATCH_PARTICIPANT_CAP) {
		return scenarioSetDetail(detail, detail_size,
			"saved bot roster exceeds live participant capacity", NULL);
	}
	for (i = 0; i < document->bot_count; i++) {
		const scenario_document_bot_t *source = &document->bots[i];
		struct matchslot *slot = &plan->config.slots[plan->config.numSlots];
		const asset_runtime_binding_t *profile;
		const char *profile_id;
		char body_id[CATALOG_ID_LEN];
		char head_id[CATALOG_ID_LEN];
		player_identity_plan_t identity;
		player_identity_status_e identity_status;

		if ((source->present & SCENARIO_BOT_PROFILE_ID) != 0) {
			if (!source->profile_id[0]) {
				return scenarioSetDetail(detail, detail_size,
					"typed bot profile ID is empty", NULL);
			}
			profile_id = source->profile_id;
		} else {
			if (source->difficulty < 0 || source->difficulty > 5) {
				return scenarioSetDetail(detail, detail_size,
					"legacy bot difficulty cannot be migrated", NULL);
			}
			profile_id = mpBotProfileIdForTraits(0, source->difficulty);
		}
		profile = profile_id ? scenarioResolveBotProfile(profile_id) : NULL;
		if (!profile || profile->bot_profile_difficulty != source->difficulty) {
			return scenarioSetDetail(detail, detail_size,
				"invalid or mismatched ASSET_BOT_PROFILE", profile_id);
		}
		if (!scenarioResolveIdentity(source->body_id,
				(source->present & SCENARIO_BOT_BODY_ID) != 0,
				source->legacy_body, ASSET_BODY, body_id, detail, detail_size)
				|| !scenarioResolveIdentity(source->head_id,
					(source->present & SCENARIO_BOT_HEAD_ID) != 0,
					source->legacy_head, ASSET_HEAD, head_id, detail,
					detail_size)) return 0;
		identity_status = playerIdentityPrepare(body_id, head_id, &identity);
		if (identity_status != PLAYER_IDENTITY_OK) {
			return scenarioSetDetail(detail, detail_size,
				"bot identity preflight rejected",
				playerIdentityStatusString(identity_status));
		}
		if ((source->present & SCENARIO_BOT_BODY_ID) != 0
				&& (source->present & SCENARIO_BOT_LEGACY_BODY) != 0
				&& source->legacy_body != identity.mp_body_index) {
			return scenarioSetDetail(detail, detail_size,
				"numeric bot body companion contradicts typed ASSET_BODY", NULL);
		}
		if ((source->present & SCENARIO_BOT_HEAD_ID) != 0
				&& (source->present & SCENARIO_BOT_LEGACY_HEAD) != 0
				&& source->legacy_head != identity.mp_head_index) {
			return scenarioSetDetail(detail, detail_size,
				"numeric bot head companion contradicts typed ASSET_HEAD", NULL);
		}

		slot->type = SLOT_BOT;
		slot->team = (plan->config.options & MPOPTION_TEAMSENABLED)
			? scenarioChooseBotTeam(&plan->config) : 0;
		scenarioCopyId(slot->profile_id, profile_id);
		scenarioCopyId(slot->body_id, identity.body_id);
		scenarioCopyId(slot->head_id, identity.head_id);
		slot->botType = (u8)profile->bot_profile_type;
		slot->botDifficulty = (u8)profile->bot_profile_difficulty;
		slot->bodynum = identity.mp_body_index >= 0
			? (u8)identity.mp_body_index : 0xFF;
		slot->headnum = identity.mp_head_index >= 0
			? (u8)identity.mp_head_index : 0xFF;
		strncpy(slot->name, source->name, sizeof(slot->name) - 1);
		slot->name[sizeof(slot->name) - 1] = '\0';
		plan->config.numSlots++;
	}
	plan->source_version = document->version;
	plan->legacy_v3_hybrid = scenarioHasV3HybridCompanions(document);
	plan->migrated = document->version < SCENARIO_SAVE_VERSION
		|| plan->legacy_v3_hybrid;
	plan->bot_count = document->bot_count;
	return 1;
}

static s32 scenarioBuildPlan(const scenario_document_t *document,
		s32 human_count, scenario_load_plan_t *plan, char *detail,
		size_t detail_size)
{
	s32 candidate_ok = scenarioBuildPlanCandidate(document, human_count, plan,
		detail, detail_size);
	s32 ownership_ok = scenarioReleasePlanSource(plan, detail, detail_size);

	return candidate_ok && ownership_ok;
}

static void scenarioCommitPlan(const scenario_load_plan_t *plan)
{
	g_MatchConfig = plan->config;
	mpCommitPreparedWeaponSet(plan->resolved_weapon_set, plan->config.weapons);
}

static s32 scenarioDocumentFromLive(const char *name,
		scenario_document_t *document, char *detail, size_t detail_size)
{
	scenario_load_plan_t validation;
	s32 i;

	memset(document, 0, sizeof(*document));
	if (!name || !name[0] || strlen(name) >= sizeof(document->name)
			|| !scenarioTextSerializable(name)) {
		return scenarioSetDetail(detail, detail_size,
			"saved Scenario name is empty or too long", NULL);
	}
	document->version = SCENARIO_SAVE_VERSION;
	document->present = SCENARIO_DOC_VERSION | SCENARIO_DOC_NAME
		| SCENARIO_DOC_ARENA_ID | SCENARIO_DOC_SCENARIO_ID
		| SCENARIO_DOC_TIMELIMIT | SCENARIO_DOC_SCORELIMIT
		| SCENARIO_DOC_TEAMSCORELIMIT | SCENARIO_DOC_OPTIONS
		| SCENARIO_DOC_WEAPONSET | SCENARIO_DOC_SPAWN_WEAPON_ID
		| SCENARIO_DOC_SPAWN_WEAPON_MODE | SCENARIO_DOC_BOTS;
	strncpy(document->name, name, sizeof(document->name) - 1);
	scenarioCopyId(document->arena_id, g_MatchConfig.stage_id);
	scenarioCopyId(document->scenario_id, g_MatchConfig.scenario_id);
	document->timelimit = g_MatchConfig.timelimit;
	document->scorelimit = g_MatchConfig.scorelimit;
	document->teamscorelimit = g_MatchConfig.teamscorelimit;
	document->options = matchConfigGetUserOptions();
	document->weaponset = g_MatchConfig.weaponSetIndex;
	for (i = 0; i < NUM_MPWEAPONSLOTS; i++) {
		document->weapon_id_present[i] = 1;
		scenarioCopyId(document->weapon_ids[i], g_MatchConfig.weapon_ids[i]);
	}
	document->spawn_weapon_mode = g_MatchConfig.spawnWeaponMode;
	scenarioCopyId(document->spawn_weapon_id, g_MatchConfig.spawn_weapon_id);
	for (i = 0; i < g_MatchConfig.numSlots && i < MATCH_MAX_SLOTS; i++) {
		const struct matchslot *slot = &g_MatchConfig.slots[i];
		scenario_document_bot_t *bot;
		if (slot->type == SLOT_PLAYER) continue;
		if (slot->type != SLOT_BOT
				|| document->bot_count >= SCENARIO_CODEC_MAX_BOTS) {
			return scenarioSetDetail(detail, detail_size,
				"live match contains an invalid scenario slot", NULL);
		}
		bot = &document->bots[document->bot_count++];
		if (!slot->name[0] || !scenarioTextSerializable(slot->name)) {
			return scenarioSetDetail(detail, detail_size,
				"live bot name is empty or not serializable", NULL);
		}
		bot->present = SCENARIO_BOT_NAME | SCENARIO_BOT_PROFILE_ID
			| SCENARIO_BOT_DIFFICULTY | SCENARIO_BOT_BODY_ID
			| SCENARIO_BOT_HEAD_ID;
		strncpy(bot->name, slot->name, sizeof(bot->name) - 1);
		scenarioCopyId(bot->profile_id, slot->profile_id);
		scenarioCopyId(bot->body_id, slot->body_id);
		scenarioCopyId(bot->head_id, slot->head_id);
		bot->difficulty = slot->botDifficulty;
	}
	return scenarioBuildPlan(document, matchConfigCountHumans(), &validation,
		detail, detail_size);
}

s32 scenarioSave(const char *name)
{
	scenario_document_t document;
	char detail[192];
	char scenario_dir[SCENARIO_PATH_MAX];
	char safe_name[128];
	char filepath[SCENARIO_PATH_MAX];
	save_atomic_file_t transaction;
	FILE *fp;
	s32 i;

	if (!scenarioDocumentFromLive(name, &document, detail, sizeof(detail))) {
		sysLogPrintf(LOG_WARNING,
			"SCENARIO.ROLLBACK save candidate rejected: %s", detail);
		return -1;
	}
	getScenarioDir(scenario_dir, sizeof(scenario_dir));
	fsCreateDir(scenario_dir);
	sanitizeName(name, safe_name, sizeof(safe_name));
	snprintf(filepath, sizeof(filepath), "%s/%s%s", scenario_dir, safe_name,
		SCENARIO_FILE_EXTENSION);
	if (saveAtomicBegin(&transaction, filepath) != 0) {
		sysLogPrintf(LOG_WARNING, "SCENARIO.ROLLBACK cannot open '%s'", filepath);
		return -1;
	}
	fp = saveAtomicStream(&transaction);
	fprintf(fp, "{\n  \"version\": %d,\n  \"name\": \"", document.version);
	scenarioJsonWriteEscaped(fp, document.name);
	fprintf(fp, "\",\n  \"arenaId\": \"");
	scenarioJsonWriteEscaped(fp, document.arena_id);
	fprintf(fp, "\",\n  \"scenarioId\": \"");
	scenarioJsonWriteEscaped(fp, document.scenario_id);
	fprintf(fp, "\",\n  \"timelimit\": %d,\n  \"scorelimit\": %d,\n"
		"  \"teamscorelimit\": %u,\n  \"options\": %u,\n"
		"  \"weaponset\": %d,\n", document.timelimit,
		document.scorelimit, (unsigned)document.teamscorelimit,
		(unsigned)document.options, document.weaponset);
	for (i = 0; i < NUM_MPWEAPONSLOTS; i++) {
		fprintf(fp, "  \"weapon_id%d\": \"", i);
		scenarioJsonWriteEscaped(fp, document.weapon_ids[i]);
		fprintf(fp, "\",\n");
	}
	fprintf(fp, "  \"spawnWeaponId\": \"");
	scenarioJsonWriteEscaped(fp, document.spawn_weapon_id);
	fprintf(fp, "\",\n  \"spawnWeaponMode\": %d,\n  \"bots\": [\n",
		document.spawn_weapon_mode);
	for (i = 0; i < document.bot_count; i++) {
		const scenario_document_bot_t *bot = &document.bots[i];
		if (i) fprintf(fp, ",\n");
		fprintf(fp, "    {\"name\": \"");
		scenarioJsonWriteEscaped(fp, bot->name);
		fprintf(fp, "\", \"profileId\": \"");
		scenarioJsonWriteEscaped(fp, bot->profile_id);
		fprintf(fp, "\", \"difficulty\": %d, \"bodyId\": \"",
			bot->difficulty);
		scenarioJsonWriteEscaped(fp, bot->body_id);
		fprintf(fp, "\", \"headId\": \"");
		scenarioJsonWriteEscaped(fp, bot->head_id);
		fprintf(fp, "\"}");
	}
	if (document.bot_count) fprintf(fp, "\n");
	fprintf(fp, "  ]\n}\n");
	if (ferror(fp)) {
		saveAtomicAbort(&transaction);
		sysLogPrintf(LOG_WARNING,
			"SCENARIO.ROLLBACK write failed; prior '%s' preserved", filepath);
		return -1;
	}
	if (saveAtomicCommit(&transaction) != 0) {
		sysLogPrintf(LOG_WARNING,
			"SCENARIO.ROLLBACK commit failed; prior '%s' preserved", filepath);
		return -1;
	}
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.COMMIT saved public source '%s' version=%d", filepath,
		document.version);
	return 0;
}

s32 scenarioLoad(const char *filepath, s32 humanCount)
{
	FILE *fp;
	long filesize;
	char *buffer;
	size_t nread;
	scenario_document_t document;
	scenario_load_plan_t plan;
	scenario_codec_status_e parse_status;
	char detail[192];

	if (!filepath || !filepath[0]) return -1;
	fp = fopen(filepath, "rb");
	if (!fp) {
		sysLogPrintf(LOG_WARNING, "SCENARIO.ROLLBACK cannot open '%s'", filepath);
		return -1;
	}
	if (fseek(fp, 0, SEEK_END) != 0 || (filesize = ftell(fp)) <= 0
			|| filesize > (long)SCENARIO_CODEC_MAX_BYTES
			|| fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		sysLogPrintf(LOG_WARNING,
			"SCENARIO.ROLLBACK invalid file extent for '%s'", filepath);
		return -1;
	}
	buffer = (char *)malloc((size_t)filesize + 1);
	if (!buffer) {
		fclose(fp);
		return -1;
	}
	nread = fread(buffer, 1, (size_t)filesize, fp);
	if (nread != (size_t)filesize || ferror(fp)) {
		fclose(fp);
		free(buffer);
		sysLogPrintf(LOG_WARNING,
			"SCENARIO.ROLLBACK truncated read for '%s'", filepath);
		return -1;
	}
	fclose(fp);
	buffer[filesize] = '\0';
	parse_status = scenarioDocumentParse(buffer, (size_t)filesize, &document,
		detail, sizeof(detail));
	free(buffer);
	if (parse_status != SCENARIO_CODEC_OK) {
		sysLogPrintf(LOG_WARNING,
			"SCENARIO.ROLLBACK parse=%s detail='%s' file='%s'",
			scenarioCodecStatusString(parse_status), detail, filepath);
		return -1;
	}
	if (!scenarioBuildPlan(&document, humanCount, &plan, detail,
			sizeof(detail))) {
		sysLogPrintf(LOG_WARNING,
			"SCENARIO.ROLLBACK plan rejected detail='%s' file='%s'",
			detail, filepath);
		return -1;
	}
	scenarioCommitPlan(&plan);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.COMMIT loaded '%s' version=%d migrated=%d legacy_v3_hybrid=%d bots=%d humans=%d",
		filepath, plan.source_version, plan.migrated, plan.legacy_v3_hybrid,
		plan.bot_count, humanCount);
	return 0;
}

s32 scenarioDelete(const char *filepath)
{
	if (!filepath || !filepath[0] || remove(filepath) != 0) {
		sysLogPrintf(LOG_WARNING, "SCENARIO: delete failed for '%s'",
			filepath ? filepath : "(null)");
		return -1;
	}
	sysLogPrintf(LOG_NOTE, "SCENARIO: deleted '%s'", filepath);
	return 0;
}

s32 scenarioListFiles(char (*outPaths)[SCENARIO_PATH_MAX], s32 maxCount)
{
	char scenario_dir[SCENARIO_PATH_MAX];
	DIR *dir;
	struct dirent *entry;
	s32 count = 0;
	const size_t extension_len = strlen(SCENARIO_FILE_EXTENSION);

	if (!outPaths || maxCount <= 0) return 0;
	getScenarioDir(scenario_dir, sizeof(scenario_dir));
	dir = opendir(scenario_dir);
	if (!dir) return 0;
	while ((entry = readdir(dir)) != NULL && count < maxCount) {
		const size_t name_len = strlen(entry->d_name);
		if (name_len > extension_len
				&& strcmp(entry->d_name + name_len - extension_len,
					SCENARIO_FILE_EXTENSION) == 0) {
			snprintf(outPaths[count], SCENARIO_PATH_MAX, "%s/%s",
				scenario_dir, entry->d_name);
			count++;
		}
	}
	closedir(dir);
	return count;
}
