#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bss.h"
#include "data.h"
#include "fs.h"
#include "asset_runtime.h"
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "catalog_activation_ledger.h"
#include "catalog_stage_ownership.h"
#include "player_identity.h"
#include "scenario_codec.h"
#include "scenario_save.h"
#include "scenario_source_runtime.h"
#include "system.h"
#include "v006_scenario_harness.h"

extern s32 g_MpWeaponSetNum;

typedef struct v006_scenario_snapshot_t {
	struct matchconfig match;
	struct mpsetup setup;
	struct mpplayerconfig players[MAX_PLAYERS];
	struct mpbotconfig bots[MAX_BOTS];
	u64 rng_seed;
	u64 rng2_seed;
	s32 weapon_set;
	s32 bondplayernum;
	s32 coopplayernum;
	s32 antiplayernum;
	s32 mpquickteam;
} v006_scenario_snapshot_t;

typedef struct v006_source_owner_snapshot_t {
	char id[CATALOG_ID_LEN];
	s32 stage_owned;
	s32 entry_ref_count;
	s32 ledger_present;
	asset_type_e ledger_type;
	s32 ledger_references;
	s32 ledger_stage_owned;
	s32 ledger_bundled;
} v006_source_owner_snapshot_t;

static void v006ScenarioCapture(v006_scenario_snapshot_t *snapshot)
{
	snapshot->match = g_MatchConfig;
	snapshot->setup = g_MpSetup;
	memcpy(snapshot->players, g_PlayerConfigsArray, sizeof(snapshot->players));
	memcpy(snapshot->bots, g_BotConfigsArray, sizeof(snapshot->bots));
	snapshot->rng_seed = g_RngSeed;
	snapshot->rng2_seed = g_Rng2Seed;
	snapshot->weapon_set = g_MpWeaponSetNum;
	snapshot->bondplayernum = g_Vars.bondplayernum;
	snapshot->coopplayernum = g_Vars.coopplayernum;
	snapshot->antiplayernum = g_Vars.antiplayernum;
	snapshot->mpquickteam = g_Vars.mpquickteam;
}

static s32 v006ScenarioUnchanged(const v006_scenario_snapshot_t *snapshot)
{
	return memcmp(&snapshot->match, &g_MatchConfig,
			sizeof(snapshot->match)) == 0
		&& memcmp(&snapshot->setup, &g_MpSetup,
			sizeof(snapshot->setup)) == 0
		&& memcmp(snapshot->players, g_PlayerConfigsArray,
			sizeof(snapshot->players)) == 0
		&& memcmp(snapshot->bots, g_BotConfigsArray,
			sizeof(snapshot->bots)) == 0
		&& snapshot->rng_seed == g_RngSeed
		&& snapshot->rng2_seed == g_Rng2Seed
		&& snapshot->weapon_set == g_MpWeaponSetNum
		&& snapshot->bondplayernum == g_Vars.bondplayernum
		&& snapshot->coopplayernum == g_Vars.coopplayernum
		&& snapshot->antiplayernum == g_Vars.antiplayernum
		&& snapshot->mpquickteam == g_Vars.mpquickteam;
}

static const catalog_activation_root_t *v006ScenarioActivationRoot(
		const char *id)
{
	size_t i;
	for (i = 0; i < catalogActivationLedgerActiveCount(); i++) {
		const catalog_activation_root_t *root =
			catalogActivationLedgerActiveAt(i);
		if (root && strcmp(root->id, id) == 0) return root;
	}
	return NULL;
}

static s32 v006ScenarioCaptureSourceOwner(const asset_entry_t *entry,
		v006_source_owner_snapshot_t *snapshot)
{
	const catalog_activation_root_t *root;
	if (!entry || !snapshot || !entry->occupied || !entry->enabled
			|| entry->type != ASSET_SCENARIO) return 0;
	memset(snapshot, 0, sizeof(*snapshot));
	strncpy(snapshot->id, entry->id, sizeof(snapshot->id) - 1);
	snapshot->stage_owned = catalogStageOwnershipHasRef(entry);
	snapshot->entry_ref_count = entry->ref_count;
	root = v006ScenarioActivationRoot(entry->id);
	snapshot->ledger_present = root != NULL;
	if (root) {
		snapshot->ledger_type = root->type;
		snapshot->ledger_references = root->references;
		snapshot->ledger_stage_owned = root->stage_owned;
		snapshot->ledger_bundled = root->bundled;
	}
	return 1;
}

static s32 v006ScenarioSourceOwnerMatches(
		const v006_source_owner_snapshot_t *snapshot)
{
	const asset_entry_t *entry;
	const catalog_activation_root_t *root;
	if (!snapshot || !snapshot->id[0]) return 0;
	entry = assetCatalogResolve(snapshot->id);
	if (!entry || catalogStageOwnershipHasRef(entry) != snapshot->stage_owned
			|| entry->ref_count != snapshot->entry_ref_count) return 0;
	root = v006ScenarioActivationRoot(snapshot->id);
	if ((root != NULL) != snapshot->ledger_present) return 0;
	return !root || (root->type == snapshot->ledger_type
		&& root->references == snapshot->ledger_references
		&& root->stage_owned == snapshot->ledger_stage_owned
		&& root->bundled == snapshot->ledger_bundled);
}

static const asset_entry_t *v006ScenarioSourceForArena(const char *arena_id)
{
	catalog_stage_result_t stage = {0};
	if (!arena_id || !catalogResolveStageForType(arena_id, ASSET_ARENA,
			&stage)) return NULL;
	return scenarioSourceFindEntryForStage(&stage, 1);
}

static asset_entry_t *v006ScenarioFindInactiveArenaSource(
		char out_arena_id[CATALOG_ID_LEN])
{
	s32 i;
	if (out_arena_id) out_arena_id[0] = '\0';
	for (i = 0; i < assetCatalogGetPoolSize(); i++) {
		const asset_entry_t *arena = assetCatalogGetByIndex(i);
		const asset_entry_t *source;
		const asset_runtime_binding_t *runtime;
		catalog_stage_result_t stage = {0};
		if (!arena || !arena->occupied || !arena->enabled
				|| arena->type != ASSET_ARENA
				|| strcmp(arena->id, g_MatchConfig.stage_id) == 0
				|| !catalogResolveStageForType(arena->id, ASSET_ARENA,
					&stage)) continue;
		source = scenarioSourceFindEntryForStage(&stage, 1);
		if (!source || !source->occupied || !source->enabled
				|| source->type != ASSET_SCENARIO
				|| !source->ext.scenario.collision_file[0]
				|| source->loaded_data
				|| source->payload_kind != ASSET_PAYLOAD_NONE
				|| catalogStageOwnershipHasRef(source)) continue;
		runtime = assetRuntimeFindByTypeAndId(ASSET_SCENARIO, source->id);
		if (runtime && runtime->active) continue;
		if (out_arena_id) {
			strncpy(out_arena_id, arena->id, CATALOG_ID_LEN - 1);
			out_arena_id[CATALOG_ID_LEN - 1] = '\0';
		}
		return assetCatalogGetMutable(source->id);
	}
	return NULL;
}

static s32 v006ScenarioWrite(const char *path, const void *bytes, size_t size)
{
	FILE *stream = fopen(path, "wb");
	s32 ok;
	if (!stream) return 0;
	ok = fwrite(bytes, 1, size, stream) == size && fflush(stream) == 0;
	if (fclose(stream) != 0) ok = 0;
	return ok;
}

static void v006ScenarioWriteEscaped(FILE *stream, const char *value)
{
	for (; value && *value; value++) {
		if (*value == '"') {
			fputs("\\\"", stream);
		} else if (*value == '\\') {
			fputs("\\\\", stream);
		} else {
			fputc(*value, stream);
		}
	}
}

static const struct matchslot *v006ScenarioFirstBot(
		const struct matchconfig *source)
{
	s32 i;
	for (i = 1; i < source->numSlots && i < MATCH_MAX_SLOTS; i++) {
		if (source->slots[i].type == SLOT_BOT) return &source->slots[i];
	}
	return NULL;
}

static const char *v006ScenarioFindIncompatibleHead(const char *body_id)
{
	const asset_entry_t *body = assetCatalogResolve(body_id);
	s32 i;
	if (!body || body->type != ASSET_BODY || !body->ext.body.rig_class[0]) {
		return NULL;
	}
	for (i = 0; i < assetCatalogGetPoolSize(); i++) {
		const asset_entry_t *head = assetCatalogGetByIndex(i);
		player_identity_plan_t identity;
		if (!head || !head->occupied || !head->enabled
				|| head->type != ASSET_HEAD || !head->ext.head.rig_class[0]
				|| strcmp(body->ext.body.rig_class,
					head->ext.head.rig_class) == 0) continue;
		if (playerIdentityPrepare(body_id, head->id, &identity)
				== PLAYER_IDENTITY_INCOMPATIBLE_RIG_CLASS) return head->id;
	}
	return NULL;
}

static char *v006ScenarioRead(const char *path, size_t *out_size)
{
	FILE *stream = fopen(path, "rb");
	long length;
	char *bytes;
	if (out_size) *out_size = 0;
	if (!stream || fseek(stream, 0, SEEK_END) != 0
			|| (length = ftell(stream)) <= 0
			|| fseek(stream, 0, SEEK_SET) != 0) {
		if (stream) fclose(stream);
		return NULL;
	}
	bytes = (char *)malloc((size_t)length + 1);
	if (!bytes || fread(bytes, 1, (size_t)length, stream) != (size_t)length) {
		free(bytes);
		fclose(stream);
		return NULL;
	}
	fclose(stream);
	bytes[length] = '\0';
	if (out_size) *out_size = (size_t)length;
	return bytes;
}

static char *v006ScenarioReplace(const char *source, const char *needle,
		const char *replacement, size_t *out_size)
{
	const char *position = strstr(source, needle);
	size_t source_size;
	size_t needle_size;
	size_t replacement_size;
	size_t prefix_size;
	char *result;
	if (!position) return NULL;
	source_size = strlen(source);
	needle_size = strlen(needle);
	replacement_size = strlen(replacement);
	prefix_size = (size_t)(position - source);
	result = (char *)malloc(source_size - needle_size + replacement_size + 1);
	if (!result) return NULL;
	memcpy(result, source, prefix_size);
	memcpy(result + prefix_size, replacement, replacement_size);
	memcpy(result + prefix_size + replacement_size,
		position + needle_size, source_size - prefix_size - needle_size + 1);
	if (out_size) *out_size = source_size - needle_size + replacement_size;
	return result;
}

static s32 v006ScenarioReport(const char *name, s32 passed, s32 *count)
{
	sysLogPrintf(passed ? LOG_NOTE : LOG_ERROR,
		"V006.SCENARIO: case=%s result=%s", name, passed ? "PASS" : "FAIL");
	if (passed) (*count)++;
	return passed;
}

static s32 v006ScenarioRejectPreserves(const char *path, const void *bytes,
		size_t size)
{
	v006_scenario_snapshot_t before;
	v006ScenarioCapture(&before);
	return v006ScenarioWrite(path, bytes, size)
		&& scenarioLoad(path, matchConfigCountHumans()) != 0
		&& v006ScenarioUnchanged(&before);
}

static s32 v006ScenarioWriteVersion2(const char *path,
		const struct matchconfig *source)
{
	/* Exact early-v2 writer shape: numeric stage/mode authority, typed arena,
	 * selected weapon-set authority, and numeric+typed bot companions. */
	FILE *stream = fopen(path, "wb");
	s32 first = 1;
	s32 i;
	s32 ok;
	if (!stream) return 0;
	fprintf(stream, "{\n  \"version\": 2,\n  \"name\": \"V2 migration\",\n");
	fprintf(stream, "  \"arena\": %u,\n  \"arenaId\": \"",
		(unsigned)source->stagenum);
	v006ScenarioWriteEscaped(stream, source->stage_id);
	fprintf(stream, "\",\n  \"scenario\": %u,\n", (unsigned)source->scenario);
	fprintf(stream, "  \"timelimit\": %u,\n  \"scorelimit\": %u,\n",
		(unsigned)source->timelimit, (unsigned)source->scorelimit);
	fprintf(stream, "  \"teamscorelimit\": %u,\n  \"options\": %u,\n",
		(unsigned)source->teamscorelimit, (unsigned)source->options);
	fprintf(stream, "  \"weaponset\": %d,\n  \"bots\": [\n",
		(s32)source->weaponSetIndex);
	for (i = 1; i < source->numSlots && i < MATCH_MAX_SLOTS; i++) {
		const struct matchslot *bot = &source->slots[i];
		if (bot->type != SLOT_BOT) continue;
		if (!first) fprintf(stream, ",\n");
		first = 0;
		fprintf(stream, "    {\"name\": \"");
		v006ScenarioWriteEscaped(stream, bot->name);
		fprintf(stream, "\", \"difficulty\": %u, \"body\": %u, \"head\": %u, "
			"\"bodyId\": \"", (unsigned)bot->botDifficulty,
			(unsigned)bot->bodynum, (unsigned)bot->headnum);
		v006ScenarioWriteEscaped(stream, bot->body_id);
		fprintf(stream, "\", \"headId\": \"");
		v006ScenarioWriteEscaped(stream, bot->head_id);
		fprintf(stream, "\"}");
	}
	if (!first) fprintf(stream, "\n");
	fprintf(stream, "  ]\n}\n");
	ok = !ferror(stream) && fflush(stream) == 0;
	if (fclose(stream) != 0) ok = 0;
	return ok;
}

static s32 v006ScenarioWritePriorVersion3(const char *path,
		const struct matchconfig *source)
{
	/* Exact field order and complete companion shape emitted by the shipping
	 * fadf9ff6 scenarioSave writer. Values are taken from the live candidate. */
	FILE *stream = fopen(path, "wb");
	s32 first = 1;
	s32 i;
	s32 ok;
	if (!stream) return 0;
	fprintf(stream, "{\n");
	fprintf(stream, "  \"version\": 3,\n");
	fprintf(stream, "  \"name\": \"Prior shipping v3\",\n");
	fprintf(stream, "  \"arena\": %u,\n", (unsigned)source->stagenum);
	fprintf(stream, "  \"arenaId\": \"");
	v006ScenarioWriteEscaped(stream, source->stage_id);
	fprintf(stream, "\",\n");
	fprintf(stream, "  \"scenario\": %u,\n", (unsigned)source->scenario);
	fprintf(stream, "  \"scenarioId\": \"");
	v006ScenarioWriteEscaped(stream, source->scenario_id);
	fprintf(stream, "\",\n");
	fprintf(stream, "  \"timelimit\": %u,\n", (unsigned)source->timelimit);
	fprintf(stream, "  \"scorelimit\": %u,\n", (unsigned)source->scorelimit);
	fprintf(stream, "  \"teamscorelimit\": %u,\n",
		(unsigned)source->teamscorelimit);
	fprintf(stream, "  \"options\": %u,\n", (unsigned)source->options);
	fprintf(stream, "  \"weaponset\": %d,\n", (s32)source->weaponSetIndex);
	for (i = 0; i < NUM_MPWEAPONSLOTS; i++) {
		fprintf(stream, "  \"weapon_id%d\": \"%s\",\n", i,
			source->weapon_ids[i]);
		fprintf(stream, "  \"weapon%d\": %u,\n", i,
			(unsigned)source->weapons[i]);
	}
	fprintf(stream, "  \"spawnWeaponId\": \"");
	v006ScenarioWriteEscaped(stream, source->spawn_weapon_id);
	fprintf(stream, "\",\n");
	fprintf(stream, "  \"spawnWeaponMode\": %u,\n",
		(unsigned)source->spawnWeaponMode);
	fprintf(stream, "  \"bots\": [\n");
	for (i = 1; i < source->numSlots && i < MATCH_MAX_SLOTS; i++) {
		const struct matchslot *bot = &source->slots[i];
		if (bot->type != SLOT_BOT) continue;
		if (!first) fprintf(stream, ",\n");
		first = 0;
		fprintf(stream, "    {\"name\": \"");
		v006ScenarioWriteEscaped(stream, bot->name);
		fprintf(stream, "\", \"profileId\": \"");
		v006ScenarioWriteEscaped(stream, bot->profile_id);
		fprintf(stream, "\", \"difficulty\": %u, \"bodyId\": \"",
			(unsigned)bot->botDifficulty);
		v006ScenarioWriteEscaped(stream, bot->body_id);
		fprintf(stream, "\", \"headId\": \"");
		v006ScenarioWriteEscaped(stream, bot->head_id);
		fprintf(stream, "\"}");
	}
	if (!first) fprintf(stream, "\n");
	fprintf(stream, "  ]\n");
	fprintf(stream, "}\n");
	ok = !ferror(stream) && fflush(stream) == 0;
	if (fclose(stream) != 0) ok = 0;
	return ok;
}

static s32 v006ScenarioWriteVersion1(const char *path,
		const struct matchconfig *source)
{
	FILE *stream = fopen(path, "wb");
	const struct matchslot *bot = NULL;
	s32 i;
	s32 ok;
	if (!stream) return 0;
	for (i = 0; i < source->numSlots; i++) {
		if (source->slots[i].type == SLOT_BOT) {
			bot = &source->slots[i];
			break;
		}
	}
	fprintf(stream,
		"{\"version\":1,\"name\":\"V1 migration\","
		"\"arena\":%u,\"scenario\":%u,\"timelimit\":%u,"
		"\"scorelimit\":%u,\"teamscorelimit\":%u,\"options\":%u,"
		"\"weaponset\":%d,", (unsigned)source->stagenum,
		(unsigned)source->scenario, (unsigned)source->timelimit,
		(unsigned)source->scorelimit, (unsigned)source->teamscorelimit,
		(unsigned)source->options, (s32)source->weaponSetIndex);
	fprintf(stream, "\"bots\":[");
	if (bot) {
		fprintf(stream,
			"{\"name\":\"V1Normal\",\"difficulty\":%u,"
			"\"body\":%u,\"head\":%u}",
			(unsigned)bot->botDifficulty, (unsigned)bot->bodynum,
			(unsigned)bot->headnum);
	}
	fprintf(stream, "]}");
	ok = !ferror(stream) && fflush(stream) == 0;
	if (fclose(stream) != 0) ok = 0;
	return ok;
}

s32 v006ScenarioTransactionRun(void)
{
	char saved_path[FS_MAXPATH + 1];
	char candidate_path[FS_MAXPATH + 1];
	char *saved = NULL;
	char *prior_v3 = NULL;
	char *v2_source = NULL;
	char *changed = NULL;
	size_t changed_size = 0;
	char timelimit_key[48];
	char companion_key[64];
	char companion_value[64];
	char identity_key[SCENARIO_CODEC_ID_LEN + 32];
	char identity_value[SCENARIO_CODEC_ID_LEN + 32];
	struct matchconfig expected;
	const struct matchslot *expected_bot;
	const char *incompatible_head;
	s32 passed = 0;
	const s32 cases = 19;

	fsFullPath("$S/scenarios/v006_transaction.json", saved_path,
		sizeof(saved_path));
	fsFullPath("$S/scenarios/v006_candidate.json", candidate_path,
		sizeof(candidate_path));
	{
		const asset_entry_t *source =
			v006ScenarioSourceForArena(g_MatchConfig.stage_id);
		v006_source_owner_snapshot_t before;
		s32 ok = v006ScenarioCaptureSourceOwner(source, &before)
			&& scenarioSave("v006_transaction") == 0
			&& v006ScenarioSourceOwnerMatches(&before);
		v006ScenarioReport("lazy_activation_success_restores_ownership", ok,
			&passed);
		if (!ok) goto done;
	}
	saved = v006ScenarioRead(saved_path, NULL);
	if (!saved) goto done;
	v006ScenarioReport("v3_save_typed_only", strstr(saved, "\"arenaId\"")
		&& strstr(saved, "\"scenarioId\"")
		&& strstr(saved, "\"weapon_id0\"")
		&& !strstr(saved, "\"arena\":")
		&& !strstr(saved, "\"scenario\":")
		&& !strstr(saved, "\"weapon0\":"), &passed);
	v006ScenarioReport("v3_load_commit",
		scenarioLoad(saved_path, matchConfigCountHumans()) == 0, &passed);
	expected = g_MatchConfig;
	{
		const asset_entry_t *source =
			v006ScenarioSourceForArena(expected.stage_id);
		v006_source_owner_snapshot_t before;
		v006_source_owner_snapshot_t owned;
		s32 acquired = 0;
		s32 ok = v006ScenarioCaptureSourceOwner(source, &before);
		if (ok && !before.stage_owned) {
			ok = catalogLoadStageAsset(ASSET_SCENARIO, source->id);
			acquired = ok;
		}
		if (ok) ok = v006ScenarioCaptureSourceOwner(source, &owned);
		if (ok) {
			ok = scenarioLoad(saved_path, matchConfigCountHumans()) == 0
				&& v006ScenarioSourceOwnerMatches(&owned);
		}
		if (acquired) catalogReleaseStageAsset(ASSET_SCENARIO, source->id);
		ok = ok && v006ScenarioSourceOwnerMatches(&before);
		v006ScenarioReport("already_owned_source_preserved", ok, &passed);
	}
	{
		char failure_arena_id[CATALOG_ID_LEN];
		asset_entry_t *failure_source =
			v006ScenarioFindInactiveArenaSource(failure_arena_id);
		v006_source_owner_snapshot_t before;
		char *original_collision = NULL;
		s32 ok = failure_source
			&& v006ScenarioCaptureSourceOwner(failure_source, &before);
		if (ok) {
			original_collision = (char *)malloc(
				strlen(failure_source->ext.scenario.collision_file) + 1);
			ok = original_collision != NULL;
		}
		if (ok) {
			strcpy(original_collision,
				failure_source->ext.scenario.collision_file);
			changed = v006ScenarioReplace(saved, expected.stage_id,
				failure_arena_id, &changed_size);
			strncpy(failure_source->ext.scenario.collision_file,
				"Z:/__v006_missing_scenario_collision__.obj",
				sizeof(failure_source->ext.scenario.collision_file) - 1);
			failure_source->ext.scenario.collision_file[
				sizeof(failure_source->ext.scenario.collision_file) - 1] = '\0';
			ok = changed && v006ScenarioRejectPreserves(candidate_path,
				changed, changed_size);
			strncpy(failure_source->ext.scenario.collision_file,
				original_collision,
				sizeof(failure_source->ext.scenario.collision_file) - 1);
			failure_source->ext.scenario.collision_file[
				sizeof(failure_source->ext.scenario.collision_file) - 1] = '\0';
			ok = ok && v006ScenarioSourceOwnerMatches(&before);
		}
		v006ScenarioReport("lazy_activation_failure_rollback", ok, &passed);
		free(original_collision);
		free(changed);
		changed = NULL;
	}
	v006ScenarioReport("prior_v3_hybrid_migration",
		v006ScenarioWritePriorVersion3(candidate_path, &expected)
			&& scenarioLoad(candidate_path, matchConfigCountHumans()) == 0
			&& memcmp(&g_MatchConfig, &expected, sizeof(expected)) == 0,
		&passed);
	prior_v3 = v006ScenarioRead(candidate_path, NULL);
	if (!prior_v3) goto done;

	v006ScenarioReport("malformed_preserves_state",
		v006ScenarioRejectPreserves(candidate_path, "{\"version\":", 11),
		&passed);
	changed = v006ScenarioReplace(saved, "\"version\": 3",
		"\"version\": 4", &changed_size);
	v006ScenarioReport("future_version_preserves_state", changed
		&& v006ScenarioRejectPreserves(candidate_path, changed, changed_size),
		&passed);
	free(changed);
	snprintf(timelimit_key, sizeof(timelimit_key), "\"timelimit\": %u",
		(unsigned)expected.timelimit);
	changed = v006ScenarioReplace(saved, timelimit_key,
		"\"timelimit\": \"wrong\"", &changed_size);
	v006ScenarioReport("wrong_json_type_preserves_state", changed
		&& v006ScenarioRejectPreserves(candidate_path, changed, changed_size),
		&passed);
	free(changed);
	changed = v006ScenarioReplace(saved, expected.stage_id, "base:combat",
		&changed_size);
	v006ScenarioReport("wrong_catalog_type_preserves_state", changed
		&& v006ScenarioRejectPreserves(candidate_path, changed, changed_size),
		&passed);
	free(changed);
	changed = (char *)malloc(SCENARIO_CODEC_MAX_BYTES + 1);
	if (changed) memset(changed, 'x', SCENARIO_CODEC_MAX_BYTES + 1);
	v006ScenarioReport("oversized_preserves_state", changed
		&& v006ScenarioRejectPreserves(candidate_path, changed,
			SCENARIO_CODEC_MAX_BYTES + 1), &passed);
	free(changed);
	changed = NULL;

	snprintf(companion_key, sizeof(companion_key), "  \"arena\": %u,\n",
		(unsigned)expected.stagenum);
	snprintf(companion_value, sizeof(companion_value), "  \"arena\": %u,\n",
		(unsigned)(expected.stagenum ^ 1));
	changed = v006ScenarioReplace(prior_v3, companion_key, companion_value,
		&changed_size);
	v006ScenarioReport("prior_v3_contradiction_preserves_state", changed
		&& v006ScenarioRejectPreserves(candidate_path, changed, changed_size),
		&passed);
	free(changed);
	snprintf(companion_key, sizeof(companion_key), "  \"weapon5\": %u,\n",
		(unsigned)expected.weapons[5]);
	changed = v006ScenarioReplace(prior_v3, companion_key, "", &changed_size);
	v006ScenarioReport("prior_v3_partial_preserves_state", changed
		&& v006ScenarioRejectPreserves(candidate_path, changed, changed_size),
		&passed);
	free(changed);
	{
		v006_scenario_snapshot_t before;
		v006ScenarioCapture(&before);
		v006ScenarioReport("human_count_mismatch_preserves_state",
			scenarioLoad(saved_path, matchConfigCountHumans() + 1) != 0
				&& v006ScenarioUnchanged(&before), &passed);
	}
	expected_bot = v006ScenarioFirstBot(&expected);
	incompatible_head = expected_bot
		? v006ScenarioFindIncompatibleHead(expected_bot->body_id) : NULL;
	if (expected_bot && incompatible_head) {
		snprintf(identity_key, sizeof(identity_key), "\"headId\": \"%s\"",
			expected_bot->head_id);
		snprintf(identity_value, sizeof(identity_value), "\"headId\": \"%s\"",
			incompatible_head);
		changed = v006ScenarioReplace(saved, identity_key, identity_value,
			&changed_size);
	} else {
		changed = NULL;
	}
	v006ScenarioReport("incompatible_rig_preserves_state", changed
		&& v006ScenarioRejectPreserves(candidate_path, changed, changed_size),
		&passed);
	free(changed);
	changed = NULL;

	if (v006ScenarioWriteVersion2(candidate_path, &expected)) {
		v2_source = v006ScenarioRead(candidate_path, NULL);
	}
	if (expected_bot && v2_source) {
		snprintf(identity_key, sizeof(identity_key), "\"bodyId\": \"%s\"",
			expected_bot->body_id);
		snprintf(identity_value, sizeof(identity_value),
			"\"bodyId\": \"base:combat\"");
		changed = v006ScenarioReplace(v2_source, identity_key, identity_value,
			&changed_size);
	} else {
		changed = NULL;
	}
	v006ScenarioReport("v2_invalid_typed_identity_preserves_state", changed
		&& v006ScenarioRejectPreserves(candidate_path, changed, changed_size),
		&passed);
	free(changed);
	changed = NULL;

	v006ScenarioReport("v2_profile_migration",
		v006ScenarioWriteVersion2(candidate_path, &expected)
			&& scenarioLoad(candidate_path, matchConfigCountHumans()) == 0
			&& strcmp(g_MatchConfig.stage_id, expected.stage_id) == 0
			&& g_MatchConfig.numSlots == expected.numSlots, &passed);
	v006ScenarioReport("v1_numeric_migration",
		v006ScenarioWriteVersion1(candidate_path, &expected)
			&& scenarioLoad(candidate_path, matchConfigCountHumans()) == 0
			&& strcmp(g_MatchConfig.stage_id, expected.stage_id) == 0
			&& strcmp(g_MatchConfig.scenario_id, expected.scenario_id) == 0,
		&passed);
	v006ScenarioReport("v3_restore_for_match_start",
		scenarioLoad(saved_path, matchConfigCountHumans()) == 0
			&& memcmp(&g_MatchConfig, &expected, sizeof(expected)) == 0, &passed);
done:
	free(saved);
	free(prior_v3);
	free(v2_source);
	remove(candidate_path);
	remove(saved_path);
	sysLogPrintf(passed == cases ? LOG_NOTE : LOG_ERROR,
		"V006.SCENARIO: passed=%d cases=%d result=%s", passed, cases,
		passed == cases ? "PASS" : "FAIL");
	return passed == cases ? 0 : -1;
}
