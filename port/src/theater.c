/**
 * theater.c -- production authoritative adapter for the v2 Theater codec.
 */

#include "theater.h"

#include "assetcatalog.h"
#include "bss.h"
#include "constants.h"
#include "data.h"
#include "fs.h"
#include "game/lv.h"
#include "game/mplayer/participant.h"
#include "game/objectives.h"
#include "game/prop.h"
#include "modmgr.h"
#include "net/matchsetup.h"
#include "net/net.h"
#include "net/netmanifest.h"
#include "net/net_manifest_type.h"
#include "scenario_source_runtime.h"
#include "system.h"

#include <SDL.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

#define THEATER_CHECKPOINT_INTERVAL_TICKS 12u

static s32 theaterFloatIsFinite(f32 value)
{
	return value == value && value >= -FLT_MAX && value <= FLT_MAX;
}

_Static_assert(THEATER_FORMAT_MANIFEST_BODY == MANIFEST_TYPE_BODY,
	"Theater v2 manifest body type drift");
_Static_assert(THEATER_FORMAT_MANIFEST_STAGE == MANIFEST_TYPE_STAGE,
	"Theater v2 manifest stage type drift");
_Static_assert(THEATER_FORMAT_MANIFEST_GENERIC_ASSET == MANIFEST_TYPE_ASSET,
	"Theater v2 generic manifest type drift");
_Static_assert(THEATER_FORMAT_CATALOG_TYPE_MISSION == ASSET_MISSION,
	"Theater v2 mission catalog type drift");
_Static_assert(THEATER_FORMAT_CATALOG_TYPE_GAMEMODE == ASSET_GAMEMODE,
	"Theater v2 game-mode catalog type drift");
_Static_assert(THEATER_FORMAT_CATALOG_TYPE_BOT_PROFILE == ASSET_BOT_PROFILE,
	"Theater v2 bot-profile catalog type drift");
_Static_assert(THEATER_FORMAT_CATALOG_TYPE_MAX == ASSET_THEME,
	"Theater v2 catalog type ceiling drift");
_Static_assert(THEATER_FORMAT_WEAPON_SLOTS == NUM_MPWEAPONSLOTS,
	"Theater v2 weapon-slot count drift");

typedef struct theater_slot_state_s {
	u32 stable_id;
	u64 lifecycle_generation;
	u8 prop_type;
	u16 entity_kind;
	s32 active;
	s32 spawned;
	char asset_id[THEATER_FORMAT_ID_MAX];
} theater_slot_state_t;

typedef struct theater_recorder_s {
	theater_format_writer_t *writer;
	theater_format_match_t match;
	theater_format_entity_t *entities;
	theater_format_view_t views[THEATER_FORMAT_MAX_VIEWS];
	theater_slot_state_t *slots;
	u32 entity_capacity;
	u32 next_entity_id;
	u64 start_ticks;
	u64 last_checkpoint_tick;
	u64 last_record_tick;
	u64 previous_stage_flags;
	u64 previous_objective_flags;
	s32 has_checkpoint;
	s32 checkpoint_requested;
	char final_path[512];
} theater_recorder_t;

static theater_recorder_t s_Recorder;
static theater_replay_entry_t s_List[THEATER_REPLAY_LIST_MAX];
static s32 s_ListCount;

/* The shared prop allocator supplies a never-zero process-lifetime generation
 * for every allocation. Zero remains an invalid/unallocated sentinel: the
 * checkpoint path rejects it before a stable file ID can be assigned, and the
 * initial rejection aborts the unpublished candidate. */
static s32 theaterEntityLifecycleIdentityAvailable(void)
{
	return 1;
}

static u64 theaterPropLifecycleGeneration(const struct prop *prop)
{
	return propGetLifecycleGeneration(prop);
}

static void clearRecorderStorage(void)
{
	free(s_Recorder.entities);
	free(s_Recorder.slots);
	memset(&s_Recorder, 0, sizeof(s_Recorder));
}

static const char *replayDir(void)
{
	static char dir[512];
	if (dir[0]) return dir;
	char home[400];
	sysGetHomePath(home, sizeof(home));
	snprintf(dir, sizeof(dir), "%s/replays", home);
	fsCreateDir(dir);
	return dir;
}

static s32 replayFilenameValid(const char *filename)
{
	if (!filename || !filename[0]) return 0;
	const size_t length = strlen(filename);
	if (length < 6 || length >= THEATER_REPLAY_NAME_MAX) return 0;
	if (strcmp(filename + length - 5, ".pdth") != 0) return 0;
	for (size_t i = 0; i < length; i++) {
		const unsigned char c = (unsigned char)filename[i];
		if (c == '/' || c == '\\' || c == ':' || c < 0x20) return 0;
	}
	return 1;
}

static s32 buildReplayPath(const char *filename, char *out, u32 out_size)
{
	if (!replayFilenameValid(filename) || !out || out_size == 0) return 0;
	const int length = snprintf(out, out_size, "%s/%s", replayDir(), filename);
	return length > 0 && (u32)length < out_size;
}

static s32 copyCatalogId(char dst[THEATER_FORMAT_ID_MAX], const char *src,
	s32 required)
{
	if (!src || !src[0]) {
		dst[0] = '\0';
		return required ? 0 : 1;
	}
	const size_t length = strlen(src);
	if (length >= THEATER_FORMAT_ID_MAX) return 0;
	memcpy(dst, src, length + 1);
	return 1;
}

static const match_manifest_t *authoritativeManifest(void)
{
	if (g_NetMode == NETMODE_SERVER && !g_NetDedicated
			&& g_ServerManifest.num_entries > 0) return &g_ServerManifest;
	return g_CurrentLoadedManifest.num_entries > 0
		? &g_CurrentLoadedManifest : NULL;
}

static s32 copyBounded(char *dst, u32 capacity, const char *src, s32 required)
{
	if (!dst || capacity == 0 || !src || (!src[0] && required)) return 0;
	const size_t length = strlen(src);
	if (length >= capacity) return 0;
	memcpy(dst, src, length + 1);
	return 1;
}

static s32 manifestInventoryCompare(const void *left, const void *right)
{
	const theater_format_manifest_entry_t *a =
		(const theater_format_manifest_entry_t *)left;
	const theater_format_manifest_entry_t *b =
		(const theater_format_manifest_entry_t *)right;
	const int by_id = strcmp(a->catalog_id, b->catalog_id);
	if (by_id) return by_id;
	if (a->type != b->type) return a->type < b->type ? -1 : 1;
	return a->slot < b->slot ? -1 : a->slot > b->slot;
}

static s32 fillManifestInventoryEntry(theater_format_manifest_entry_t *output,
	const char *catalog_id, u16 type, u16 slot, const u8 *available_digest)
{
	const asset_entry_t *asset = assetCatalogResolve(catalog_id);
	memset(output, 0, sizeof(*output));
	output->type = type;
	output->slot = slot;
	output->flags = THEATER_FORMAT_MANIFEST_REQUIRED;
	if (!copyBounded(output->catalog_id, sizeof(output->catalog_id),
			catalog_id, 1)) return 0;
	const char *category = asset && asset->category[0]
		? asset->category : NULL;
	char derived_category[THEATER_FORMAT_CATEGORY_MAX] = {0};
	if (!category) {
		const char *separator = strchr(catalog_id, ':');
		const size_t length = separator
			? (size_t)(separator - catalog_id) : strlen(catalog_id);
		if (length == 0 || length >= sizeof(derived_category)) return 0;
		memcpy(derived_category, catalog_id, length);
		category = derived_category;
	}
	if (!copyBounded(output->category, sizeof(output->category), category, 1)) {
		return 0;
	}
	modinfo_t *mod = strcmp(category, "base") != 0
		? modmgrFindMod(category) : NULL;
	const u8 *digest = available_digest;
	s32 digest_present = 0;
	if (digest) {
		for (u32 byte = 0; byte < 32; byte++) {
			if (digest[byte]) digest_present = 1;
		}
	}
	if (!digest_present && mod) {
		digest = mod->sha256;
		for (u32 byte = 0; byte < 32; byte++) {
			if (digest[byte]) digest_present = 1;
		}
	}
	if (digest_present) {
		memcpy(output->content_digest, digest,
			sizeof(output->content_digest));
		output->flags |= THEATER_FORMAT_MANIFEST_HAS_DIGEST;
	}
	if (mod && mod->version[0]) {
		if (!copyBounded(output->version_id, sizeof(output->version_id),
				mod->version, 1)) return 0;
		output->flags |= THEATER_FORMAT_MANIFEST_HAS_VERSION;
	}
	return 1;
}

static s32 ensureTypedMatchIdentity(theater_format_manifest_entry_t *entries,
	u32 *count, u32 capacity, const char *catalog_id, asset_type_e expected_type)
{
	const asset_entry_t *asset = assetCatalogResolve(catalog_id);
	if (!asset || asset->type != expected_type) return 0;
	const u16 manifest_type = netManifestTypeForCatalogAsset(asset->type);
	const u16 slot = manifest_type == THEATER_FORMAT_MANIFEST_GENERIC_ASSET
		? (u16)asset->type : THEATER_FORMAT_MANIFEST_SLOT_MATCH;
	for (u32 i = 0; i < *count; i++) {
		if (strcmp(entries[i].catalog_id, catalog_id) != 0) continue;
		if (entries[i].type == manifest_type && entries[i].slot == slot) return 1;
		if (entries[i].slot == slot) return 0;
	}
	if (*count >= capacity) return 0;
	if (!fillManifestInventoryEntry(&entries[*count], catalog_id,
			manifest_type, slot, NULL)) return 0;
	(*count)++;
	return 1;
}

static s32 ensureTypedStageIdentity(theater_format_manifest_entry_t *entries,
	u32 *count, u32 capacity, const char *catalog_id)
{
	const asset_entry_t *stage = assetCatalogResolve(catalog_id);
	if (!stage || (stage->type != ASSET_MAP && stage->type != ASSET_ARENA)) {
		return 0;
	}
	return ensureTypedMatchIdentity(entries, count, capacity, catalog_id,
		stage->type);
}

static s32 ensureTypedParticipantIdentity(
	theater_format_manifest_entry_t *entries, u32 *count, u32 capacity,
	const char *catalog_id, asset_type_e expected_type, u16 participant_slot)
{
	const asset_entry_t *asset = assetCatalogResolve(catalog_id);
	if (!asset || asset->type != expected_type
			|| (expected_type != ASSET_BODY && expected_type != ASSET_HEAD)) {
		return 0;
	}
	const u16 manifest_type = netManifestTypeForCatalogAsset(asset->type);
	for (u32 i = 0; i < *count; i++) {
		if (strcmp(entries[i].catalog_id, catalog_id) != 0
				|| entries[i].slot != participant_slot) continue;
		return entries[i].type == manifest_type;
	}
	if (*count >= capacity) return 0;
	if (!fillManifestInventoryEntry(&entries[*count], catalog_id,
			manifest_type, participant_slot, NULL)) return 0;
	(*count)++;
	return 1;
}

static s32 buildManifestInventory(const theater_format_match_t *match,
	const theater_format_roster_entry_t *roster, u32 roster_count,
	theater_format_manifest_entry_t **out_entries, u32 *out_count)
{
	const match_manifest_t *source = authoritativeManifest();
	if (!match || !roster || roster_count == 0
			|| roster_count > THEATER_FORMAT_MAX_ROSTER
			|| !source || !source->entries || source->num_entries == 0
			|| roster_count > (THEATER_FORMAT_MAX_MANIFEST_ENTRIES
				- 4u - THEATER_FORMAT_WEAPON_SLOTS) / 3u
			|| source->num_entries > THEATER_FORMAT_MAX_MANIFEST_ENTRIES
				- 4u - THEATER_FORMAT_WEAPON_SLOTS - roster_count * 3u) {
		return 0;
	}
	const u32 capacity = source->num_entries + 4u
		+ THEATER_FORMAT_WEAPON_SLOTS + roster_count * 3u;
	theater_format_manifest_entry_t *entries =
		(theater_format_manifest_entry_t *)calloc(capacity,
			sizeof(*entries));
	if (!entries) return 0;
	u32 count = 0;
	for (u16 i = 0; i < source->num_entries; i++) {
		const match_manifest_entry_t *input = &source->entries[i];
		const asset_entry_t *asset = input->id[0]
			? assetCatalogResolve(input->id) : NULL;
		if (input->type > MANIFEST_TYPE_ASSET
				|| (input->type != MANIFEST_TYPE_COMPONENT
					&& (!asset || !netManifestTypeAcceptsCatalogAsset(
						input->type, input->slot_index, asset->type)))) goto fail;
		const u16 slot = input->slot_index == MANIFEST_SLOT_MATCH
			? THEATER_FORMAT_MANIFEST_SLOT_MATCH : input->slot_index;
		if (!fillManifestInventoryEntry(&entries[count++], input->id,
				input->type, slot, input->sha256)) goto fail;
	}
	if (!ensureTypedStageIdentity(entries, &count, capacity,
			match->stage_id)) goto fail;
	if (match->mode == THEATER_FORMAT_MODE_CAMPAIGN) {
		if (!ensureTypedMatchIdentity(entries, &count, capacity,
				match->mission_id, ASSET_MISSION)) goto fail;
	} else if (!ensureTypedMatchIdentity(entries, &count, capacity,
			match->mode_id, ASSET_GAMEMODE)) goto fail;
	for (u32 i = 0; i < THEATER_FORMAT_WEAPON_SLOTS; i++) {
		if (match->weapon_ids[i][0]
				&& !ensureTypedMatchIdentity(entries, &count, capacity,
					match->weapon_ids[i], ASSET_WEAPON)) goto fail;
	}
	if (match->spawn_weapon_id[0]
			&& !ensureTypedMatchIdentity(entries, &count, capacity,
				match->spawn_weapon_id, ASSET_WEAPON)) goto fail;
	for (u32 i = 0; i < roster_count; i++) {
		if (!ensureTypedParticipantIdentity(entries, &count, capacity,
				roster[i].body_id, ASSET_BODY, roster[i].slot)
				|| !ensureTypedParticipantIdentity(entries, &count, capacity,
					roster[i].head_id, ASSET_HEAD, roster[i].slot)
				|| (roster[i].profile_id[0]
					&& !ensureTypedMatchIdentity(entries, &count, capacity,
						roster[i].profile_id, ASSET_BOT_PROFILE))) goto fail;
	}
	qsort(entries, count, sizeof(*entries),
		manifestInventoryCompare);
	for (u32 i = 1; i < count; i++) {
		if (manifestInventoryCompare(&entries[i - 1], &entries[i]) >= 0) goto fail;
	}
	*out_entries = entries;
	*out_count = count;
	return 1;
fail:
	free(entries);
	return 0;
}

static s32 theaterAuthorityKind(void)
{
	if (g_NetMode == NETMODE_NONE) return THEATER_FORMAT_AUTHORITY_OFFLINE;
	if (g_NetMode == NETMODE_SERVER && !g_NetDedicated) {
		return THEATER_FORMAT_AUTHORITY_LISTEN;
	}
	return 0;
}

static const char *currentStageCatalogId(void)
{
	return g_Vars.normmplayerisrunning
		? (g_MatchConfig.stage_id[0] ? g_MatchConfig.stage_id : g_MpSetup.stage_id)
		: (g_MissionConfig.stage_id[0] ? g_MissionConfig.stage_id
			: catalogStageIdByStagenum(g_Vars.stagenum));
}

static u8 currentCampaignVariant(void)
{
	if (g_Vars.coopplayernum >= 0 && g_Vars.antiplayernum >= 0) return 0;
	if (g_Vars.antiplayernum >= 0) {
		return THEATER_FORMAT_CAMPAIGN_COUNTER_OPERATIVE;
	}
	if (g_Vars.coopplayernum >= 0) {
		return THEATER_FORMAT_CAMPAIGN_COOPERATIVE;
	}
	return THEATER_FORMAT_CAMPAIGN_SOLO;
}

static const char *currentCombatModeCatalogId(void)
{
	return g_MatchConfig.scenario_id[0]
		? g_MatchConfig.scenario_id
		: catalogGameModeIdByScenarioIndex((s32)g_MpSetup.scenario);
}

static u8 currentSpawnWeaponMode(void)
{
	switch (g_MatchConfig.spawnWeaponMode) {
	case SPAWNWEAPON_MODE_SPECIFIC:
		return THEATER_FORMAT_SPAWN_WEAPON_SPECIFIC;
	case SPAWNWEAPON_MODE_RANDOM:
		return THEATER_FORMAT_SPAWN_WEAPON_RANDOM;
	case SPAWNWEAPON_MODE_FIESTA:
		return THEATER_FORMAT_SPAWN_WEAPON_FIESTA;
	default:
		return THEATER_FORMAT_SPAWN_WEAPON_NONE;
	}
}

static s32 currentMatchContextMatches(const theater_format_match_t *match)
{
	if (!match) return 0;
	const char *stage_id = currentStageCatalogId();
	if (!stage_id || strcmp(stage_id, match->stage_id) != 0) return 0;
	if (match->mode == THEATER_FORMAT_MODE_CAMPAIGN) {
		if (g_Vars.normmplayerisrunning) return 0;
		const char *mission_id = scenarioSourceActiveMissionId();
		return mission_id && strcmp(mission_id, match->mission_id) == 0
			&& currentCampaignVariant() == match->campaign_variant;
	}
	if (!g_Vars.normmplayerisrunning) return 0;
	const char *mode_id = currentCombatModeCatalogId();
	if (!mode_id || strcmp(mode_id, match->mode_id) != 0
			|| matchConfigGetUserOptions() != match->options
			|| g_MatchConfig.timelimit != match->time_limit
			|| g_MatchConfig.scorelimit != match->score_limit
			|| g_MatchConfig.teamscorelimit != match->team_score_limit
			|| currentSpawnWeaponMode() != match->spawn_weapon_mode
			|| strcmp(g_MatchConfig.spawn_weapon_id,
				match->spawn_weapon_id) != 0) return 0;
	for (u32 i = 0; i < THEATER_FORMAT_WEAPON_SLOTS; i++) {
		if (strcmp(g_MatchConfig.weapon_ids[i], match->weapon_ids[i]) != 0) {
			return 0;
		}
	}
	return 1;
}

static s32 buildMatchDescriptor(theater_format_match_t *match)
{
	memset(match, 0, sizeof(*match));
	match->authority = (u8)theaterAuthorityKind();
	if (!match->authority) return 0;
	match->tick_rate = 60;
	match->checkpoint_interval_ticks = THEATER_CHECKPOINT_INTERVAL_TICKS;
	match->start_time_unix = (u64)time(NULL);
	match->difficulty = (u8)lvGetDifficulty();

	if (g_Vars.normmplayerisrunning) {
		match->mode = THEATER_FORMAT_MODE_COMBAT_SIMULATOR;
		const char *stage_id = g_MatchConfig.stage_id[0]
			? g_MatchConfig.stage_id : g_MpSetup.stage_id;
		const char *mode_id = currentCombatModeCatalogId();
		if (!copyCatalogId(match->stage_id, stage_id, 1)
				|| !copyCatalogId(match->mode_id, mode_id, 1)) return 0;
		match->options = matchConfigGetUserOptions();
		match->time_limit = g_MatchConfig.timelimit;
		match->score_limit = g_MatchConfig.scorelimit;
		match->team_score_limit = g_MatchConfig.teamscorelimit;
		for (u32 i = 0; i < THEATER_FORMAT_WEAPON_SLOTS; i++) {
			if (!copyCatalogId(match->weapon_ids[i],
					g_MatchConfig.weapon_ids[i], 0)) return 0;
		}
		if (!copyCatalogId(match->spawn_weapon_id,
				g_MatchConfig.spawn_weapon_id, 0)) return 0;
		match->spawn_weapon_mode = currentSpawnWeaponMode();
		if (match->spawn_weapon_mode == THEATER_FORMAT_SPAWN_WEAPON_NONE) return 0;
	} else {
		match->mode = THEATER_FORMAT_MODE_CAMPAIGN;
		match->campaign_variant = currentCampaignVariant();
		if (!match->campaign_variant) return 0;
		const char *stage_id = g_MissionConfig.stage_id[0]
			? g_MissionConfig.stage_id
			: catalogStageIdByStagenum(g_Vars.stagenum);
		const char *mission_id = scenarioSourceActiveMissionId();
		if (!copyCatalogId(match->stage_id, stage_id, 1)
				|| !copyCatalogId(match->mission_id, mission_id, 1)) return 0;
	}
	return 1;
}

static u8 participantKindForSlot(s32 slot)
{
	MpParticipant *participant = mpGetParticipant(slot);
	if (!participant) {
		return g_NetMode == NETMODE_NONE && slot < MAX_PLAYERS
			? THEATER_FORMAT_PARTICIPANT_LOCAL
			: 0;
	}
	switch (participant->type) {
	case PARTICIPANT_LOCAL: return THEATER_FORMAT_PARTICIPANT_LOCAL;
	case PARTICIPANT_REMOTE: return THEATER_FORMAT_PARTICIPANT_REMOTE;
	case PARTICIPANT_BOT: return THEATER_FORMAT_PARTICIPANT_BOT;
	default: return 0;
	}
}

static s32 buildCombatSimRoster(theater_format_roster_entry_t *roster,
	u32 *out_count)
{
	u32 count = 0;
	for (s32 i = 0; i < g_MatchConfig.numSlots; i++) {
		const struct matchslot *slot = &g_MatchConfig.slots[i];
		if (slot->type == SLOT_EMPTY) continue;
		if (slot->type != SLOT_PLAYER && slot->type != SLOT_BOT) return 0;
		if (count >= THEATER_FORMAT_MAX_ROSTER) return 0;
		theater_format_roster_entry_t *entry = &roster[count++];
		memset(entry, 0, sizeof(*entry));
		entry->slot = (u16)i;
		entry->kind = slot->type == SLOT_BOT
			? THEATER_FORMAT_PARTICIPANT_BOT : participantKindForSlot(i);
		entry->team = slot->team;
		if (!entry->kind || (slot->type == SLOT_PLAYER
				&& entry->kind == THEATER_FORMAT_PARTICIPANT_BOT)
				|| !copyCatalogId(entry->body_id, slot->body_id, 1)
				|| !copyCatalogId(entry->head_id, slot->head_id, 1)
				|| !copyCatalogId(entry->profile_id, slot->profile_id,
					entry->kind == THEATER_FORMAT_PARTICIPANT_BOT)) return 0;
		if (slot->name[0]) {
			strncpy(entry->name, slot->name, sizeof(entry->name) - 1);
		} else {
			snprintf(entry->name, sizeof(entry->name), "Participant %d", i + 1);
		}
	}
	*out_count = count;
	return count > 0;
}

static s32 propIndex(const struct prop *prop)
{
	if (!prop || !g_Vars.props || g_Vars.maxprops <= 0) return -1;
	const uintptr_t address = (uintptr_t)prop;
	const uintptr_t begin = (uintptr_t)g_Vars.props;
	const uintptr_t bytes = (uintptr_t)g_Vars.maxprops * sizeof(*g_Vars.props);
	if (address < begin || address >= begin + bytes
			|| (address - begin) % sizeof(*g_Vars.props) != 0) return -1;
	return (s32)((address - begin) / sizeof(*g_Vars.props));
}

static s32 buildCampaignRoster(theater_format_roster_entry_t *roster,
	u32 *out_count, u8 campaign_variant)
{
	u32 count = 0;
	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		struct player *player = g_Vars.players[i];
		if (!player) continue;
		if (!player->prop || !player->prop->chr) return 0;
		if (count >= THEATER_FORMAT_MAX_ROSTER) return 0;
		theater_format_roster_entry_t *entry = &roster[count++];
		memset(entry, 0, sizeof(*entry));
		entry->slot = (u16)i;
		entry->kind = participantKindForSlot(i);
		if (!entry->kind || entry->kind == THEATER_FORMAT_PARTICIPANT_BOT) return 0;
		if (i == g_Vars.bondplayernum) {
			entry->role = THEATER_FORMAT_ROLE_CAMPAIGN_PRIMARY;
			strncpy(entry->name, "Primary Agent", sizeof(entry->name) - 1);
		} else if (campaign_variant == THEATER_FORMAT_CAMPAIGN_COOPERATIVE
				&& i == g_Vars.coopplayernum) {
			entry->role = THEATER_FORMAT_ROLE_CAMPAIGN_COOPERATIVE;
			strncpy(entry->name, "Cooperative Agent", sizeof(entry->name) - 1);
		} else if (campaign_variant
				== THEATER_FORMAT_CAMPAIGN_COUNTER_OPERATIVE
				&& i == g_Vars.antiplayernum) {
			entry->role = THEATER_FORMAT_ROLE_CAMPAIGN_COUNTER_OPERATIVE;
			strncpy(entry->name, "Counter-Operative", sizeof(entry->name) - 1);
		} else {
			return 0;
		}
		const char *body_id = catalogBodyIdByBodynum(player->prop->chr->bodynum);
		const char *head_id = catalogHeadIdByHeadnum(player->prop->chr->headnum);
		if (!copyCatalogId(entry->body_id, body_id, 1)
				|| !copyCatalogId(entry->head_id, head_id, 1)) return 0;
	}
	*out_count = count;
	return count > 0;
}

static s32 buildRoster(const theater_format_match_t *match,
	theater_format_roster_entry_t *roster, u32 *out_count)
{
	memset(roster, 0,
		THEATER_FORMAT_MAX_ROSTER * sizeof(theater_format_roster_entry_t));
	return match->mode == THEATER_FORMAT_MODE_COMBAT_SIMULATOR
		? buildCombatSimRoster(roster, out_count)
		: buildCampaignRoster(roster, out_count, match->campaign_variant);
}

static s32 entityAssetIdentity(struct prop *prop,
	theater_format_entity_t *entity)
{
	const char *primary = NULL;
	const char *secondary = NULL;
	if (prop->type == PROPTYPE_PLAYER || prop->type == PROPTYPE_CHR) {
		if (!prop->chr) return 0;
		primary = catalogBodyIdByBodynum(prop->chr->bodynum);
		secondary = catalogHeadIdByHeadnum(prop->chr->headnum);
	} else if (prop->type == PROPTYPE_WEAPON) {
		if (!prop->weapon) return 0;
		primary = catalogWeaponIdByRuntimeWeaponNum(prop->weapon->weaponnum);
	} else if (prop->type == PROPTYPE_OBJ || prop->type == PROPTYPE_DOOR
			|| prop->type == PROPTYPE_EYESPY) {
		if (!prop->obj) return 0;
		primary = catalogModelIdByModelnum(prop->obj->modelnum);
	}
	if (!copyCatalogId(entity->asset_id, primary, 1)
			|| !copyCatalogId(entity->secondary_asset_id, secondary, 0)) return 0;
	return 1;
}

static u16 entityKind(struct prop *prop)
{
	switch (prop->type) {
	case PROPTYPE_PLAYER: return THEATER_FORMAT_ENTITY_PLAYER;
	case PROPTYPE_CHR:
		return prop->chr && prop->chr->aibot
			? THEATER_FORMAT_ENTITY_BOT : THEATER_FORMAT_ENTITY_NPC;
	case PROPTYPE_DOOR: return THEATER_FORMAT_ENTITY_DOOR;
	case PROPTYPE_WEAPON:
		return prop->weapon && prop->weapon->base.projectile
			? THEATER_FORMAT_ENTITY_PROJECTILE : THEATER_FORMAT_ENTITY_WEAPON;
	case PROPTYPE_OBJ:
		return prop->obj && prop->obj->type == OBJTYPE_LIFT
			? THEATER_FORMAT_ENTITY_LIFT : THEATER_FORMAT_ENTITY_OBJECT;
	case PROPTYPE_EYESPY: return THEATER_FORMAT_ENTITY_OBJECT;
	/* Explosions, smoke and other effects are not represented by the v2
	 * foundation. T-THEATER-001 remains partial until a typed authoritative
	 * effect/lifecycle source exists; never guess from renderer state. */
	case PROPTYPE_EXPLOSION:
	case PROPTYPE_SMOKE:
	default: return 0;
	}
}

static s32 findPlayerIndexForProp(const struct prop *prop)
{
	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		if (g_Vars.players[i] && g_Vars.players[i]->prop == prop) return i;
	}
	return -1;
}

static const void *propBacking(const struct prop *prop)
{
	if (!prop) return NULL;
	switch (prop->type) {
	case PROPTYPE_PLAYER:
	case PROPTYPE_CHR: return prop->chr;
	case PROPTYPE_OBJ:
	case PROPTYPE_DOOR:
	case PROPTYPE_WEAPON:
	case PROPTYPE_EYESPY: return prop->obj;
	default: return NULL;
	}
}

static void setYawOrientation(theater_format_entity_t *entity, f32 yaw)
{
	const f32 sine = sinf(yaw);
	const f32 cosine = cosf(yaw);
	entity->orientation[0] = cosine;
	entity->orientation[2] = sine;
	entity->orientation[4] = 1.0f;
	entity->orientation[6] = -sine;
	entity->orientation[8] = cosine;
}

static u8 theaterDoorMotion(s32 runtime_mode)
{
	switch (runtime_mode) {
	case DOORMODE_IDLE: return THEATER_FORMAT_DOOR_IDLE;
	case DOORMODE_OPENING: return THEATER_FORMAT_DOOR_OPENING;
	case DOORMODE_CLOSING: return THEATER_FORMAT_DOOR_CLOSING;
	case DOORMODE_WAITING: return THEATER_FORMAT_DOOR_WAITING;
	default: return THEATER_FORMAT_DOOR_NONE;
	}
}

static s32 theaterCameraMode(s32 runtime_mode)
{
	switch (runtime_mode) {
	case CAMERAMODE_DEFAULT: return THEATER_FORMAT_CAMERA_FIRST_PERSON;
	case CAMERAMODE_THIRDPERSON: return THEATER_FORMAT_CAMERA_THIRD_PERSON;
	case CAMERAMODE_EYESPY: return THEATER_FORMAT_CAMERA_EYESPY;
	default: return 0;
	}
}

static s32 fillEntity(struct prop *prop, u32 stable_id, u32 parent_id,
	theater_format_entity_t *entity)
{
	memset(entity, 0, sizeof(*entity));
	entity->stable_id = stable_id;
	entity->parent_id = parent_id;
	entity->kind = entityKind(prop);
	if (!entity->kind) return 0;
	entity->room = prop->rooms[0];
	entity->position[0] = prop->pos.x;
	entity->position[1] = prop->pos.y;
	entity->position[2] = prop->pos.z;
	setYawOrientation(entity, prop->z);
	if (!entityAssetIdentity(prop, entity)) return 0;

	if (prop->type == PROPTYPE_PLAYER) {
		const s32 player_index = findPlayerIndexForProp(prop);
		struct player *player = player_index >= 0 ? g_Vars.players[player_index] : NULL;
		entity->health = player ? player->bondhealth : 0.0f;
		if (player && player->isdead) {
			entity->state_flags |= THEATER_FORMAT_ENTITY_STATE_DEAD;
		}
		if (player_index >= 0) {
			const s32 mpindex = g_Vars.playerstats[player_index].mpindex;
			if (mpindex >= 0 && mpindex < MAX_MPPLAYERCONFIGS) {
				entity->score = g_PlayerConfigsArray[mpindex].base.numpoints;
				entity->deaths = g_PlayerConfigsArray[mpindex].base.numdeaths;
			}
			if (player) {
				const char *weapon_id = catalogWeaponIdByRuntimeWeaponNum(
					player->gunctrl.weaponnum);
				if (weapon_id && weapon_id[0]
						&& !copyCatalogId(entity->secondary_asset_id, weapon_id, 1)) return 0;
			}
		}
	} else if (prop->type == PROPTYPE_CHR) {
		entity->health = prop->chr->maxdamage - prop->chr->damage;
		entity->velocity[0] = prop->chr->fallspeed.x;
		entity->velocity[1] = prop->chr->fallspeed.y;
		entity->velocity[2] = prop->chr->fallspeed.z;
		if (prop->chr->actiontype == ACT_DIE
				|| prop->chr->actiontype == ACT_DEAD) {
			entity->state_flags |= THEATER_FORMAT_ENTITY_STATE_DEAD;
		}
	} else if ((prop->type == PROPTYPE_OBJ || prop->type == PROPTYPE_DOOR
			|| prop->type == PROPTYPE_WEAPON || prop->type == PROPTYPE_EYESPY)
			&& prop->obj) {
		f32 object_orientation[9];
		f32 orientation_length = 0.0f;
		s32 orientation_finite = 1;
		for (u32 row = 0; row < 3; row++) {
			for (u32 column = 0; column < 3; column++) {
				const f32 value = prop->obj->realrot[row][column];
				object_orientation[row * 3 + column] = value;
				orientation_finite &= theaterFloatIsFinite(value);
				orientation_length += value * value;
			}
		}
		if (orientation_finite && orientation_length >= 0.75f) {
			memcpy(entity->orientation, object_orientation,
				sizeof(object_orientation));
		}
		entity->health = (f32)(prop->obj->maxdamage - prop->obj->damage);
		if (prop->type == PROPTYPE_DOOR && prop->door) {
			entity->door_fraction = prop->door->frac;
			entity->door_speed = prop->door->fracspeed;
			entity->door_motion = theaterDoorMotion(prop->door->mode);
			if (entity->door_motion == THEATER_FORMAT_DOOR_NONE) return 0;
		}
		if (prop->type == PROPTYPE_WEAPON && prop->weapon
				&& prop->weapon->base.projectile) {
			entity->velocity[0] = prop->weapon->base.projectile->speed.x;
			entity->velocity[1] = prop->weapon->base.projectile->speed.y;
			entity->velocity[2] = prop->weapon->base.projectile->speed.z;
		}
	}
	return 1;
}

static u64 objectiveStatusBits(void)
{
	u64 bits = 0;
	const s32 count = objectiveGetCount();
	for (s32 i = 0; i < count && i < 32; i++) {
		bits |= ((u64)(g_ObjectiveStatuses[i] & 3u)) << (i * 2);
	}
	return bits;
}

static s32 compareEntityStableId(const void *left, const void *right)
{
	const theater_format_entity_t *a = (const theater_format_entity_t *)left;
	const theater_format_entity_t *b = (const theater_format_entity_t *)right;
	return a->stable_id < b->stable_id ? -1 : a->stable_id > b->stable_id;
}

static theater_format_result_t appendObservedEvent(u64 tick, u16 kind,
	u32 actor_id, const s32 values[4], const char *catalog_id)
{
	theater_format_event_t event;
	memset(&event, 0, sizeof(event));
	event.tick = tick;
	event.kind = kind;
	event.actor_id = actor_id;
	if (values) memcpy(event.value, values, sizeof(event.value));
	if (catalog_id && !copyCatalogId(event.catalog_id, catalog_id, 0)) {
		return THEATER_FORMAT_INVALID_IDENTITY;
	}
	return theaterFormatAppendEvent(s_Recorder.writer, &event);
}

static s32 propRecordable(struct prop *prop)
{
	const u16 kind = entityKind(prop);
	return kind != 0 && propBacking(prop) != NULL;
}

static u32 captureParticipantViews(void)
{
	u32 count = 0;
	for (s32 i = 0; i < MAX_PLAYERS && count < THEATER_FORMAT_MAX_VIEWS; i++) {
		struct player *player = g_Vars.players[i];
		if (!player || !player->prop) continue;
		const s32 prop_index = propIndex(player->prop);
		if (prop_index < 0 || !s_Recorder.slots[prop_index].active) continue;
		theater_format_view_t *view = &s_Recorder.views[count++];
		memset(view, 0, sizeof(*view));
		view->slot = (u16)i;
		view->flags = THEATER_FORMAT_VIEW_ACTIVE;
		if (participantKindForSlot(i) == THEATER_FORMAT_PARTICIPANT_LOCAL) {
			view->flags |= THEATER_FORMAT_VIEW_LOCAL;
		}
		view->entity_id = s_Recorder.slots[prop_index].stable_id;
		view->camera_mode = theaterCameraMode(player->cameramode);
		if (!view->camera_mode) return 0;
		view->position[0] = player->cam_pos.x;
		view->position[1] = player->cam_pos.y;
		view->position[2] = player->cam_pos.z;
		view->forward[0] = player->cam_look.x;
		view->forward[1] = player->cam_look.y;
		view->forward[2] = player->cam_look.z;
		view->up[0] = player->cam_up.x;
		view->up[1] = player->cam_up.y;
		view->up[2] = player->cam_up.z;
		const f32 forward_length = view->forward[0] * view->forward[0]
			+ view->forward[1] * view->forward[1]
			+ view->forward[2] * view->forward[2];
		const f32 up_length = view->up[0] * view->up[0]
			+ view->up[1] * view->up[1]
			+ view->up[2] * view->up[2];
		if (!theaterFloatIsFinite(forward_length)
				|| !theaterFloatIsFinite(up_length)
				|| forward_length <= 0.0001f || up_length <= 0.0001f) {
			const f32 yaw = player->vv_theta * 0.017453292519943295f;
			const f32 pitch = player->vv_verta * 0.017453292519943295f;
			const f32 cos_pitch = cosf(pitch);
			view->forward[0] = -sinf(yaw) * cos_pitch;
			view->forward[1] = sinf(pitch);
			view->forward[2] = -cosf(yaw) * cos_pitch;
			view->up[0] = 0.0f;
			view->up[1] = 1.0f;
			view->up[2] = 0.0f;
			view->flags |= THEATER_FORMAT_VIEW_DERIVED_ORIENTATION;
		}
		view->fov_y_degrees = player->fovy > 1.0f && player->fovy < 179.0f
			? player->fovy : 60.0f;
		view->aspect = player->aspect > 0.1f && player->aspect < 10.0f
			? player->aspect : (4.0f / 3.0f);
		view->aim_yaw_degrees = player->vv_theta;
		view->aim_pitch_degrees = player->vv_verta;
	}
	return count;
}

static theater_format_result_t captureCheckpointInner(void)
{
	if (!s_Recorder.writer || !g_Vars.props || !s_Recorder.slots
			|| g_Vars.maxprops <= 0
			|| (u32)g_Vars.maxprops != s_Recorder.entity_capacity) {
		return THEATER_FORMAT_INCOMPLETE;
	}
	const u64 tick = g_Vars.lvframenum >= 0 ? (u64)g_Vars.lvframenum : 0;

	/* First retire changed/disappeared slot occupants. Only the exact allocation
	 * generation may distinguish reuse; mutable state and addresses are never
	 * identity inputs. Production recording is gated while this returns zero. */
	for (s32 i = 0; i < g_Vars.maxprops; i++) {
		struct prop *prop = &g_Vars.props[i];
		theater_slot_state_t *slot = &s_Recorder.slots[i];
		const s32 recordable = propRecordable(prop);
		const u64 generation = recordable
			? theaterPropLifecycleGeneration(prop) : 0;
		if (recordable && generation == 0) {
			return THEATER_FORMAT_INVALID_IDENTITY;
		}
		const s32 replaced = slot->active && recordable
			&& (slot->prop_type != prop->type
				|| slot->lifecycle_generation != generation);
		if (slot->active && (!recordable || replaced)) {
			if (s_Recorder.has_checkpoint) {
				const s32 values[4] = { slot->entity_kind, 0, 0, 0 };
				theater_format_result_t result = appendObservedEvent(tick,
					THEATER_FORMAT_EVENT_ENTITY_RETIRE, slot->stable_id,
					values, slot->asset_id);
				if (result != THEATER_FORMAT_OK) return result;
			}
			memset(slot, 0, sizeof(*slot));
		}
	}

	/* Assign deterministic file-local identities in prop-slot order. Reused
	 * slots receive new IDs, so old lifecycle references never alias. */
	for (s32 i = 0; i < g_Vars.maxprops; i++) {
		struct prop *prop = &g_Vars.props[i];
		theater_slot_state_t *slot = &s_Recorder.slots[i];
		slot->spawned = 0;
		if (!propRecordable(prop) || slot->active) continue;
		if (s_Recorder.next_entity_id == 0xffffffffu) {
			return THEATER_FORMAT_LIMIT_EXCEEDED;
		}
		slot->stable_id = ++s_Recorder.next_entity_id;
		slot->lifecycle_generation = theaterPropLifecycleGeneration(prop);
		if (slot->lifecycle_generation == 0) {
			return THEATER_FORMAT_INVALID_IDENTITY;
		}
		slot->prop_type = prop->type;
		slot->entity_kind = entityKind(prop);
		slot->active = 1;
		slot->spawned = 1;
	}

	u32 count = 0;
	for (s32 i = 0; i < g_Vars.maxprops; i++) {
		struct prop *prop = &g_Vars.props[i];
		theater_slot_state_t *slot = &s_Recorder.slots[i];
		if (!slot->active) continue;
		if (count >= s_Recorder.entity_capacity) return THEATER_FORMAT_LIMIT_EXCEEDED;
		const s32 parent_index = propIndex(prop->parent);
		const u32 parent_id = parent_index >= 0
			&& s_Recorder.slots[parent_index].active
			? s_Recorder.slots[parent_index].stable_id : 0;
		if (!fillEntity(prop, slot->stable_id, parent_id,
				&s_Recorder.entities[count])) {
			return THEATER_FORMAT_INVALID_IDENTITY;
		}
		if (!copyCatalogId(slot->asset_id,
				s_Recorder.entities[count].asset_id, 1)) {
			return THEATER_FORMAT_INVALID_IDENTITY;
		}
		if (slot->spawned && s_Recorder.has_checkpoint) {
			const s32 values[4] = { s_Recorder.entities[count].kind, 0, 0, 0 };
			theater_format_result_t result = appendObservedEvent(tick,
				THEATER_FORMAT_EVENT_ENTITY_SPAWN, slot->stable_id,
				values, slot->asset_id);
			if (result != THEATER_FORMAT_OK) return result;
		}
		count++;
	}
	qsort(s_Recorder.entities, count, sizeof(*s_Recorder.entities),
		compareEntityStableId);
	if (count == 0) return THEATER_FORMAT_INCOMPLETE;

	const u64 stage_flags = g_StageFlags;
	const u64 objective_flags = objectiveStatusBits();
	if (s_Recorder.has_checkpoint
			&& objective_flags != s_Recorder.previous_objective_flags) {
		const s32 values[4] = {
			(s32)s_Recorder.previous_objective_flags,
			(s32)(s_Recorder.previous_objective_flags >> 32),
			(s32)objective_flags, (s32)(objective_flags >> 32)
		};
		theater_format_result_t result = appendObservedEvent(tick,
			THEATER_FORMAT_EVENT_OBJECTIVE, 0, values,
			"builtin:objective_status");
		if (result != THEATER_FORMAT_OK) return result;
	}
	if (s_Recorder.has_checkpoint
			&& stage_flags != s_Recorder.previous_stage_flags) {
		const s32 values[4] = {
			(s32)s_Recorder.previous_stage_flags,
			(s32)(s_Recorder.previous_stage_flags >> 32),
			(s32)stage_flags, (s32)(stage_flags >> 32)
		};
		theater_format_result_t result = appendObservedEvent(tick,
			THEATER_FORMAT_EVENT_STAGE_FLAGS, 0, values,
			"builtin:stage_flags");
		if (result != THEATER_FORMAT_OK) return result;
	}

	theater_format_checkpoint_t checkpoint;
	memset(&checkpoint, 0, sizeof(checkpoint));
	checkpoint.tick = tick;
	checkpoint.elapsed_ms = (u64)SDL_GetTicks64() - s_Recorder.start_ticks;
	checkpoint.stage_flags = stage_flags;
	checkpoint.objective_flags = objective_flags;
	checkpoint.match_elapsed_ticks = g_Vars.lvframe60 >= 0
		? (u32)g_Vars.lvframe60 : 0;
	checkpoint.entity_count = count;
	checkpoint.entities = s_Recorder.entities;
	checkpoint.view_count = captureParticipantViews();
	checkpoint.views = s_Recorder.views;
	if (checkpoint.view_count == 0) return THEATER_FORMAT_INCOMPLETE;
	const theater_format_result_t result = theaterFormatAppendCheckpoint(
		s_Recorder.writer, &checkpoint);
	if (result == THEATER_FORMAT_OK) {
		s_Recorder.previous_stage_flags = stage_flags;
		s_Recorder.previous_objective_flags = objective_flags;
		s_Recorder.has_checkpoint = 1;
	}
	return result;
}

static theater_format_result_t captureCheckpoint(void)
{
	theater_format_mark_t mark;
	theater_format_result_t result = theaterFormatMark(s_Recorder.writer, &mark);
	if (result != THEATER_FORMAT_OK) return result;
	result = captureCheckpointInner();
	if (result != THEATER_FORMAT_OK) {
		const theater_format_result_t rollback = theaterFormatRollback(
			s_Recorder.writer, &mark);
		if (rollback != THEATER_FORMAT_OK) return rollback;
	}
	return result;
}

s32 theaterStartRecording(const char *filename)
{
	if (s_Recorder.writer) return -1;
	if (!theaterEntityLifecycleIdentityAvailable()) {
		sysLogPrintf(LOG_WARNING,
			"THEATER.V2.GAP: recording_blocked=prop_lifecycle_generation_missing");
		return -1;
	}
	theater_format_match_t match;
	if (!buildMatchDescriptor(&match)) {
		sysLogPrintf(LOG_WARNING,
			"THEATER.V2: recording rejected authority=%d dedicated=%d stage=%d",
			g_NetMode, g_NetDedicated, g_Vars.stagenum);
		return -1;
	}
	theater_format_roster_entry_t roster[THEATER_FORMAT_MAX_ROSTER];
	u32 roster_count = 0;
	if (!buildRoster(&match, roster, &roster_count)) {
		sysLogPrintf(LOG_WARNING, "THEATER.V2: authoritative roster rejected");
		return -1;
	}
	char path[512];
	if (!buildReplayPath(filename, path, sizeof(path))) {
		sysLogPrintf(LOG_WARNING, "THEATER.V2: invalid replay filename");
		return -1;
	}
	theater_format_manifest_entry_t *manifest = NULL;
	u32 manifest_count = 0;
	if (!buildManifestInventory(&match, roster, roster_count,
			&manifest, &manifest_count)) {
		sysLogPrintf(LOG_WARNING,
			"THEATER.V2: authoritative manifest inventory unavailable");
		return -1;
	}

	memset(&s_Recorder, 0, sizeof(s_Recorder));
	if (g_Vars.maxprops <= 0
			|| (u32)g_Vars.maxprops > THEATER_FORMAT_MAX_ENTITIES) {
		sysLogPrintf(LOG_WARNING,
			"THEATER.V2: invalid prop capacity %d bounded_max=%u",
			g_Vars.maxprops,
			(unsigned)THEATER_FORMAT_MAX_ENTITIES);
		free(manifest);
		return -1;
	}
	s_Recorder.entity_capacity = (u32)g_Vars.maxprops;
	s_Recorder.entities = (theater_format_entity_t *)calloc(
		s_Recorder.entity_capacity, sizeof(theater_format_entity_t));
	s_Recorder.slots = (theater_slot_state_t *)calloc(
		s_Recorder.entity_capacity, sizeof(theater_slot_state_t));
	if (!s_Recorder.entities || !s_Recorder.slots) {
		free(manifest);
		clearRecorderStorage();
		return -1;
	}
	theater_format_result_t result = THEATER_FORMAT_OK;
	s_Recorder.writer = theaterFormatBegin(path, &match, manifest,
		manifest_count, roster, roster_count, &result);
	free(manifest);
	if (!s_Recorder.writer) {
		clearRecorderStorage();
		sysLogPrintf(LOG_WARNING, "THEATER.V2: begin failed reason=%s",
			theaterFormatResultString(result));
		return -1;
	}
	s_Recorder.match = match;
	s_Recorder.start_ticks = SDL_GetTicks64();
	s_Recorder.last_checkpoint_tick = (u64)-1;
	strncpy(s_Recorder.final_path, path, sizeof(s_Recorder.final_path) - 1);

	theater_format_event_t event;
	memset(&event, 0, sizeof(event));
	event.tick = g_Vars.lvframenum >= 0 ? (u64)g_Vars.lvframenum : 0;
	event.kind = THEATER_FORMAT_EVENT_STAGE_BEGIN;
	strncpy(event.catalog_id, match.stage_id, sizeof(event.catalog_id) - 1);
	result = theaterFormatAppendEvent(s_Recorder.writer, &event);
	if (result == THEATER_FORMAT_OK) result = captureCheckpoint();
	if (result != THEATER_FORMAT_OK) {
		sysLogPrintf(LOG_WARNING, "THEATER.V2: initial checkpoint rejected reason=%s",
			theaterFormatResultString(result));
		theaterFormatAbort(s_Recorder.writer);
		clearRecorderStorage();
		return -1;
	}
	s_Recorder.last_checkpoint_tick = g_Vars.lvframenum >= 0
		? (u64)g_Vars.lvframenum : 0;
	s_Recorder.last_record_tick = s_Recorder.last_checkpoint_tick;
	sysLogPrintf(LOG_NOTE,
		"THEATER.V2: recording started mode=%u variant=%u authority=%u stage='%s' mission='%s' roster=%u manifest=%u path=%s",
		(unsigned)match.mode, (unsigned)match.campaign_variant,
		(unsigned)match.authority, match.stage_id, match.mission_id,
		(unsigned)roster_count, (unsigned)manifest_count, path);
	sysLogPrintf(LOG_NOTE,
		"THEATER.V2.GAP: foundation_partial=1 effects_explosions_smoke_not_captured=1 animation_audio_cutscene_streams_not_captured=1 playback_connected=0");
	return 0;
}

s32 theaterIsRecording(void)
{
	return s_Recorder.writer ? 1 : 0;
}

s32 theaterRecordEvent(u16 kind, u32 actor_id, u32 target_id,
	const s32 values[4], const char *catalog_id)
{
	if (!s_Recorder.writer || theaterAuthorityKind() != s_Recorder.match.authority) {
		return -1;
	}
	theater_format_event_t event;
	memset(&event, 0, sizeof(event));
	event.tick = g_Vars.lvframenum >= 0 ? (u64)g_Vars.lvframenum : 0;
	event.kind = kind;
	event.actor_id = actor_id;
	event.target_id = target_id;
	if (values) memcpy(event.value, values, sizeof(event.value));
	if (catalog_id && !copyCatalogId(event.catalog_id, catalog_id, 0)) return -1;
	const theater_format_result_t result = theaterFormatAppendEvent(
		s_Recorder.writer, &event);
	if (result == THEATER_FORMAT_OK) s_Recorder.last_record_tick = event.tick;
	return result == THEATER_FORMAT_OK ? 0 : -1;
}

void theaterRequestCheckpoint(void)
{
	if (s_Recorder.writer) s_Recorder.checkpoint_requested = 1;
}

static void finalizeAcceptedRecording(const char *reason)
{
	if (!s_Recorder.writer) return;
	theater_format_event_t event;
	memset(&event, 0, sizeof(event));
	event.tick = s_Recorder.last_record_tick;
	event.kind = THEATER_FORMAT_EVENT_MATCH_END;
	theater_format_result_t result = theaterFormatAppendEvent(
		s_Recorder.writer, &event);
	if (result == THEATER_FORMAT_OK) {
		result = theaterFormatFinish(s_Recorder.writer);
	} else {
		theaterFormatAbandon(s_Recorder.writer);
	}
	sysLogPrintf(result == THEATER_FORMAT_OK ? LOG_NOTE : LOG_WARNING,
		"THEATER.V2: recording finalized result=%s reason=%s path=%s",
		theaterFormatResultString(result), reason ? reason : "unspecified",
		s_Recorder.final_path);
	clearRecorderStorage();
}

void theaterStopRecording(void)
{
	if (!s_Recorder.writer) return;
	const u64 current_tick = g_Vars.lvframenum >= 0
		? (u64)g_Vars.lvframenum : s_Recorder.last_checkpoint_tick;
	if (theaterAuthorityKind() == s_Recorder.match.authority
			&& currentMatchContextMatches(&s_Recorder.match)
			&& current_tick > s_Recorder.last_checkpoint_tick) {
		const theater_format_result_t capture = captureCheckpoint();
		if (capture == THEATER_FORMAT_OK) {
			s_Recorder.last_checkpoint_tick = current_tick;
			s_Recorder.last_record_tick = current_tick;
		} else {
			sysLogPrintf(LOG_WARNING,
				"THEATER.V2: final capture rejected reason=%s; finalizing last accepted prefix",
				theaterFormatResultString(capture));
		}
	}
	finalizeAcceptedRecording("user_stop");
}

void theaterTick(void)
{
	if (!s_Recorder.writer) return;
	if (theaterAuthorityKind() != s_Recorder.match.authority) {
		finalizeAcceptedRecording("authority_changed");
		return;
	}
	const char *stage_id = currentStageCatalogId();
	if (!stage_id || strcmp(stage_id, s_Recorder.match.stage_id) != 0) {
		finalizeAcceptedRecording("stage_changed_without_recapture");
		return;
	}
	if (!currentMatchContextMatches(&s_Recorder.match)) {
		finalizeAcceptedRecording("match_context_changed_without_recapture");
		return;
	}
	const u64 tick = g_Vars.lvframenum >= 0 ? (u64)g_Vars.lvframenum : 0;
	if (s_Recorder.last_checkpoint_tick != (u64)-1
			&& tick <= s_Recorder.last_checkpoint_tick) {
		s_Recorder.checkpoint_requested = 0;
		return;
	}
	if (!s_Recorder.checkpoint_requested
			&& s_Recorder.last_checkpoint_tick != (u64)-1
			&& tick - s_Recorder.last_checkpoint_tick
				< s_Recorder.match.checkpoint_interval_ticks) return;
	s_Recorder.checkpoint_requested = 0;
	const theater_format_result_t result = captureCheckpoint();
	if (result != THEATER_FORMAT_OK) {
		sysLogPrintf(LOG_WARNING,
			"THEATER.V2: checkpoint rejected reason=%s; finalizing last accepted prefix",
			theaterFormatResultString(result));
		finalizeAcceptedRecording("checkpoint_rejected");
		return;
	}
	s_Recorder.last_checkpoint_tick = tick;
	s_Recorder.last_record_tick = tick;
}

s32 theaterStartReplay(const char *filename)
{
	char path[512];
	if (!buildReplayPath(filename, path, sizeof(path))) return -1;
	theater_format_info_t info;
	const theater_format_result_t result = theaterFormatValidate(path, &info);
	if (result != THEATER_FORMAT_OK) {
		sysLogPrintf(LOG_WARNING, "THEATER.V2: replay rejected reason=%s path=%s",
			theaterFormatResultString(result), path);
		return -1;
	}
	sysLogPrintf(LOG_NOTE,
		"THEATER.V2: replay validated but world playback unit is not connected stage='%s' checkpoints=%u live_state_mutated=0",
		info.match.stage_id, (unsigned)info.checkpoint_count);
	return -1;
}

void theaterStopReplay(void)
{
}

s32 theaterIsReplaying(void)
{
	return 0;
}

static void recoverInterruptedPath(const char *part_name)
{
	const size_t length = strlen(part_name);
	if (length <= 5 || strcmp(part_name + length - 5, ".part") != 0) return;
	char final_name[THEATER_REPLAY_NAME_MAX];
	if (length - 5 >= sizeof(final_name)) return;
	memcpy(final_name, part_name, length - 5);
	final_name[length - 5] = '\0';
	char final_path[512];
	if (!buildReplayPath(final_name, final_path, sizeof(final_path))) return;
	const theater_format_result_t result = theaterFormatRecoverInterrupted(final_path);
	sysLogPrintf(result == THEATER_FORMAT_OK ? LOG_NOTE : LOG_WARNING,
		"THEATER.V2: interrupted candidate result=%s file=%s",
		theaterFormatResultString(result), part_name);
}

static void recoverInterruptedFiles(void)
{
#ifdef _WIN32
	char glob[600];
	snprintf(glob, sizeof(glob), "%s\\*.pdth.part", replayDir());
	WIN32_FIND_DATAA data;
	HANDLE handle = FindFirstFileA(glob, &data);
	if (handle == INVALID_HANDLE_VALUE) return;
	do {
		recoverInterruptedPath(data.cFileName);
	} while (FindNextFileA(handle, &data));
	FindClose(handle);
#else
	DIR *dir = opendir(replayDir());
	if (!dir) return;
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		const size_t length = strlen(entry->d_name);
		if (length > 10 && strcmp(entry->d_name + length - 10, ".pdth.part") == 0) {
			recoverInterruptedPath(entry->d_name);
		}
	}
	closedir(dir);
#endif
}

static s32 readReplayEntry(const char *filename, theater_replay_entry_t *entry)
{
	char path[512];
	if (!buildReplayPath(filename, path, sizeof(path))) return 0;
	theater_format_info_t info;
	if (theaterFormatValidate(path, &info) != THEATER_FORMAT_OK) return 0;
	memset(entry, 0, sizeof(*entry));
	strncpy(entry->filename, filename, sizeof(entry->filename) - 1);
	entry->start_time_unix = info.match.start_time_unix;
	entry->checkpoint_count = info.checkpoint_count;
	entry->frame_count = info.checkpoint_count;
	entry->record_count = info.record_count;
	entry->size_bytes = info.file_size <= 0xffffffffu ? (u32)info.file_size : 0xffffffffu;
	entry->format_flags = info.flags;
	entry->mode = info.match.mode;
	entry->authority = info.match.authority;
	entry->campaign_variant = info.match.campaign_variant;
	entry->recovered = (info.flags & THEATER_FORMAT_FLAG_RECOVERED) != 0;
	strncpy(entry->stage_id, info.match.stage_id, sizeof(entry->stage_id) - 1);
	strncpy(entry->mode_id, info.match.mode_id, sizeof(entry->mode_id) - 1);
	strncpy(entry->mission_id, info.match.mission_id,
		sizeof(entry->mission_id) - 1);
	return 1;
}

s32 theaterRefreshList(void)
{
	recoverInterruptedFiles();
	memset(s_List, 0, sizeof(s_List));
	s_ListCount = 0;
#ifdef _WIN32
	char glob[600];
	snprintf(glob, sizeof(glob), "%s\\*.pdth", replayDir());
	WIN32_FIND_DATAA data;
	HANDLE handle = FindFirstFileA(glob, &data);
	if (handle == INVALID_HANDLE_VALUE) return 0;
	do {
		if (s_ListCount >= THEATER_REPLAY_LIST_MAX) break;
		if (readReplayEntry(data.cFileName, &s_List[s_ListCount])) s_ListCount++;
	} while (FindNextFileA(handle, &data));
	FindClose(handle);
#else
	DIR *dir = opendir(replayDir());
	if (!dir) return 0;
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL && s_ListCount < THEATER_REPLAY_LIST_MAX) {
		const size_t length = strlen(entry->d_name);
		if (length < 6 || strcmp(entry->d_name + length - 5, ".pdth") != 0) continue;
		if (readReplayEntry(entry->d_name, &s_List[s_ListCount])) s_ListCount++;
	}
	closedir(dir);
#endif
	return s_ListCount;
}

const theater_replay_entry_t *theaterListAt(s32 index)
{
	if (index < 0 || index >= s_ListCount) return NULL;
	return &s_List[index];
}

void theaterInit(void)
{
	memset(&s_Recorder, 0, sizeof(s_Recorder));
	memset(s_List, 0, sizeof(s_List));
	s_ListCount = 0;
	(void)replayDir();
	recoverInterruptedFiles();
}

void theaterShutdown(void)
{
	if (s_Recorder.writer) finalizeAcceptedRecording("shutdown_without_recapture");
}
