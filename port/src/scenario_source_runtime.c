#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "constants.h"
#include "types.h"
#include "platform.h"
#include "system.h"
#include "fs.h"
#include "asset_source_debug.h"
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "game/chraction.h"
#include "lib/meshcollision.h"
#include "lib/memp.h"
#include "scenario_source_runtime.h"

typedef struct scenario_source_pad_row {
	s32 room;
	s32 liftnum;
	u32 flags;
	f32 pos[3];
	f32 up[3];
	f32 look[3];
	f32 bbox[6];
} scenario_source_pad_row_t;

typedef struct scenario_source_segment_list {
	u32 *values;
	s32 count;
} scenario_source_segment_list_t;

typedef struct scenario_source_waypoint_row {
	s32 padnum;
	s32 groupnum;
	s32 step;
	scenario_source_segment_list_t neighbours;
} scenario_source_waypoint_row_t;

typedef struct scenario_source_waygroup_row {
	s32 step;
	scenario_source_segment_list_t waypoints;
	scenario_source_segment_list_t neighbours;
} scenario_source_waygroup_row_t;

typedef struct scenario_source_cover_row {
	f32 pos[3];
	f32 look[3];
	u32 flags;
} scenario_source_cover_row_t;

typedef struct scenario_source_path_row {
	s32 id;
	u32 flags;
	s32 *pads;
	s32 pad_count;
} scenario_source_path_row_t;

typedef struct scenario_source_path_table {
	scenario_source_path_row_t *rows;
	s32 count;
	s32 capacity;
	char error[192];
} scenario_source_path_table_t;

typedef struct scenario_source_volume_row {
	char id[32];
	char kind[32];
	char shape[16];
	s32 padnum;
	s32 room;
	f32 min[3];
	f32 max[3];
} scenario_source_volume_row_t;

typedef struct scenario_source_navigation {
	scenario_source_waypoint_row_t *waypoints;
	s32 waypoint_count;
	scenario_source_waygroup_row_t *waygroups;
	s32 waygroup_count;
	scenario_source_cover_row_t *covers;
	s32 cover_count;
} scenario_source_navigation_t;

typedef struct scenario_source_setup_record {
	char id[32];
	char kind[64];
	u8 type;
	s32 order;
	u8 *bytes;
	u32 len;
} scenario_source_setup_record_t;

typedef struct scenario_source_setup_table {
	scenario_source_setup_record_t *records;
	s32 count;
	s32 capacity;
	char error[192];
} scenario_source_setup_table_t;

typedef struct scenario_source_ai_command {
	u16 opcode;
	u8 *operands;
	u32 operand_count;
} scenario_source_ai_command_t;

typedef struct scenario_source_ai_list {
	char ref[32];
	s32 id;
	scenario_source_ai_command_t *commands;
	s32 count;
	s32 capacity;
	u32 byte_count;
} scenario_source_ai_list_t;

typedef struct scenario_source_ai_table {
	scenario_source_ai_list_t *lists;
	s32 count;
	s32 capacity;
	u32 byte_count;
	char error[192];
} scenario_source_ai_table_t;

typedef struct scenario_source_spawn_row {
	s32 padnum;
	s32 team;
} scenario_source_spawn_row_t;

typedef struct scenario_source_spawn_table {
	scenario_source_spawn_row_t *rows;
	s32 count;
	s32 capacity;
	char error[192];
} scenario_source_spawn_table_t;

typedef struct scenario_source_objective_node {
	char objective_id[32];
	char graph_node[96];
	char text_token[64];
	char difficulty_mask[32];
	u32 difficulty_bits;
	s32 criteria_start;
	s32 criteria_count;
	s32 inserted;
} scenario_source_objective_node_t;

typedef struct scenario_source_objective_criteria {
	char objective_id[32];
	char kind[64];
	char graph_node[96];
	char operand_kind[48];
	char target_ref[32];
	char target_record_ref[32];
	char pad_ref[32];
	char state_ref[64];
	u8 type;
	u32 stage_flag_mask;
	s32 tag_id;
	s32 pad;
	s32 match_value;
	s32 initial_status;
	s32 runtime_status;
	s32 runtime_status_valid;
	s32 runtime_object_state_valid;
	s32 runtime_object_present;
	s32 runtime_object_healthy;
	s32 runtime_object_held_by_player;
	s32 objective_index;
	s32 matched;
	const void *runtime_criteria;
} scenario_source_objective_criteria_t;

typedef struct scenario_source_setup_link {
	char record_id[32];
	char kind[64];
	u8 type;
	s32 order;
	s32 target[3];
	s32 aux[2];
	s32 matched;
} scenario_source_setup_link_t;

typedef struct scenario_source_match {
	const catalog_stage_result_t *stage;
	s32 desired_mode;
	const asset_entry_t *category_exact;
	const asset_entry_t *exact;
	const asset_entry_t *category_fallback;
	const asset_entry_t *fallback;
} scenario_source_match_t;

typedef struct scenario_source_graph_state {
	char scenario_id[CATALOG_ID_LEN];
	char level_graph_path[FS_MAXPATH + 1];
	char mission_id[CATALOG_ID_LEN];
	char mission_graph_path[FS_MAXPATH + 1];
	char mission_objectives_path[FS_MAXPATH + 1];
	char pads_path[FS_MAXPATH + 1];
	char spawns_path[FS_MAXPATH + 1];
	char objects_path[FS_MAXPATH + 1];
	char setup_fields_path[FS_MAXPATH + 1];
	char ai_lists_path[FS_MAXPATH + 1];
	char volumes_path[FS_MAXPATH + 1];
	char objectives_path[FS_MAXPATH + 1];
	char waypoints_path[FS_MAXPATH + 1];
	char waygroups_path[FS_MAXPATH + 1];
	char covers_path[FS_MAXPATH + 1];
	char paths_path[FS_MAXPATH + 1];
	u32 level_graph_size;
	u32 mission_graph_size;
	scenario_source_setup_link_t *setup_links;
	s32 setup_link_count;
	scenario_source_volume_row_t *level_volumes;
	s32 level_volume_count;
	s32 level_volume_node_count;
	s32 level_pad_node_count;
	s32 level_global_settings_node_count;
	s32 level_ai_list_node_count;
	s32 level_path_node_count;
	s32 level_ai_jog_to_pad_node_count;
	s32 level_ai_goto_pad_preset_node_count;
	s32 level_ai_walk_to_pad_node_count;
	s32 level_ai_run_to_pad_node_count;
	s32 level_ai_set_path_node_count;
	s32 level_ai_start_patrol_node_count;
	char level_global_settings_scenario[CATALOG_ID_LEN];
	char level_global_settings_kind[32];
	scenario_source_objective_node_t *mission_objectives;
	scenario_source_objective_criteria_t *mission_objective_criteria;
	s32 mission_objective_nodes;
	s32 mission_objective_criteria_nodes;
	s32 level_graph_active;
	s32 mission_graph_active;
	s32 mission_objective_runtime_active;
	s32 mission_objective_insert_logged;
	s32 mission_objective_evaluate_logged;
	s32 mission_objective_check_logged;
	s32 mission_objective_state_logged;
	s32 mission_objective_object_state_logged;
	s32 setup_link_logged;
	s32 ai_action_jog_to_pad_logged;
	s32 ai_action_goto_pad_preset_logged;
	s32 ai_action_walk_to_pad_logged;
	s32 ai_action_run_to_pad_logged;
	s32 ai_action_set_path_logged;
	s32 ai_action_start_patrol_logged;
	s32 level_volume_eval_logged;
	s32 level_volume_missing_logged;
	u32 mission_stage_flags;
	s32 mission_stage_flags_valid;
	s32 mission_stage_flags_logged;
	s32 mission_objective_range_warning_logged;
	s32 mission_phase_node_count;
	char mission_phase_state[32];
} scenario_source_graph_state_t;

typedef struct scenario_source_mission_match {
	const asset_entry_t *scenario;
	const asset_entry_t *archive_match;
	const asset_entry_t *graph_match;
	char scenario_slug[CATALOG_ID_LEN];
} scenario_source_mission_match_t;

#define SCENARIO_SEGMENT_FLAG_OUTWARD 0x4000u
#define SCENARIO_SEGMENT_FLAG_INWARD 0x8000u
#define SCENARIO_SEGMENT_ID_MASK 0x3fffu

static scenario_source_graph_state_t s_ActiveScenarioGraphs;

static s32 s_activeGraphPathForScenario(const asset_entry_t *scenario,
	const char *path, char *out, size_t out_n);
static char *s_loadGraphText(const char *path, u32 *out_size);

static s32 s_objectiveCriterionUsesGraphStatus(u8 type)
{
	return type == OBJECTIVETYPE_HOLOGRAPH ||
		type == OBJECTIVETYPE_ENTERROOM ||
		type == OBJECTIVETYPE_THROWINROOM;
}

static s32 s_objectiveCriterionUsesObjectState(u8 type)
{
	return type == OBJECTIVETYPE_DESTROYOBJ ||
		type == OBJECTIVETYPE_COLLECTOBJ ||
		type == OBJECTIVETYPE_THROWOBJ ||
		type == OBJECTIVETYPE_HOLOGRAPH;
}

static void s_resetActiveScenarioGraphs(void)
{
	free(s_ActiveScenarioGraphs.setup_links);
	free(s_ActiveScenarioGraphs.level_volumes);
	free(s_ActiveScenarioGraphs.mission_objectives);
	free(s_ActiveScenarioGraphs.mission_objective_criteria);
	memset(&s_ActiveScenarioGraphs, 0, sizeof(s_ActiveScenarioGraphs));
}

static char *s_nextField(char **cursor)
{
	char *start;
	char *p;

	if (!cursor || !*cursor) {
		return NULL;
	}

	start = *cursor;
	p = start;

	while (*p && *p != '\t' && *p != '\n' && *p != '\r') {
		p++;
	}

	if (*p) {
		*p++ = '\0';
		if (*(p - 1) == '\r' && *p == '\n') {
			p++;
		}
		*cursor = p;
	} else {
		*cursor = NULL;
	}

	return start;
}

static char *s_nextLine(char **cursor)
{
	char *start;
	char *p;

	if (!cursor || !*cursor) {
		return NULL;
	}

	start = *cursor;
	p = start;

	while (*p && *p != '\n') {
		p++;
	}

	if (*p == '\n') {
		*p++ = '\0';
		*cursor = p;
	} else {
		*cursor = NULL;
	}

	if (p > start && p[-1] == '\0' && p - start >= 2 && p[-2] == '\r') {
		p[-2] = '\0';
	}

	return start;
}

static s32 s_startsWith(const char *value, const char *prefix)
{
	size_t prefix_len;

	if (!value || !prefix) {
		return 0;
	}

	prefix_len = strlen(prefix);
	return strncmp(value, prefix, prefix_len) == 0;
}

static void s_copyString(char *out, size_t out_n, const char *value)
{
	if (!out || out_n == 0) {
		return;
	}
	out[0] = '\0';
	if (!value || !value[0]) {
		return;
	}
	strncpy(out, value, out_n - 1);
	out[out_n - 1] = '\0';
}

static s32 s_parseRoomRef(const char *value)
{
	const char *p;

	if (!value || !value[0]) {
		return -1;
	}

	p = value;
	while (*p && !isdigit((unsigned char)*p) && *p != '-' && *p != '+') {
		p++;
	}

	if (!*p) {
		return -1;
	}

	return (s32)strtol(p, NULL, 10);
}

static s32 s_parseFloat(const char *value, f32 *out)
{
	char *end;
	double parsed;

	if (!value || !out) {
		return 0;
	}

	parsed = strtod(value, &end);
	if (end == value) {
		return 0;
	}

	*out = (f32)parsed;
	return 1;
}

static s32 s_parseIndexedRef(const char *value)
{
	const char *p;

	if (!value || !value[0]) {
		return -1;
	}

	p = value;
	while (*p && !isdigit((unsigned char)*p) && *p != '-' && *p != '+') {
		p++;
	}

	if (!*p) {
		return -1;
	}

	return (s32)strtol(p, NULL, 10);
}

static s32 s_parseStageFlagRef(const char *value, u32 *out)
{
	const char *p;

	if (!value || !value[0] || !out) {
		return 0;
	}

	p = strstr(value, "0x");
	if (!p) {
		p = value;
		while (*p && !isxdigit((unsigned char)*p)) {
			p++;
		}
	}
	if (!p || !*p) {
		return 0;
	}

	*out = (u32)strtoul(p, NULL, 0);
	return 1;
}

static s32 s_setupRecordRefOffset(const scenario_source_setup_record_t *record,
	const char *ref_record_id, s32 *out)
{
	s32 target;

	if (!record || !ref_record_id || !ref_record_id[0] || !out) {
		return 0;
	}

	target = s_parseIndexedRef(ref_record_id);
	if (target < 0) {
		return 0;
	}

	*out = target - record->order;
	return 1;
}

static s32 s_parseInteger(const char *value, s32 *out)
{
	char *end;
	long parsed;

	if (!value || !out) {
		return 0;
	}

	parsed = strtol(value, &end, 0);
	if (end == value) {
		return 0;
	}

	*out = (s32)parsed;
	return 1;
}

static s32 s_parseU32Value(const char *value, u32 *out)
{
	char *end;
	unsigned long parsed;

	if (!value || !out) {
		return 0;
	}

	parsed = strtoul(value, &end, 0);
	if (end == value) {
		return 0;
	}

	*out = (u32)parsed;
	return 1;
}

static s32 s_setupKindToObjType(const char *kind, u8 *out_type)
{
	struct kind_map {
		const char *kind;
		u8 type;
	};
	static const struct kind_map maps[] = {
		{ "door", OBJTYPE_DOOR },
		{ "door_scale", OBJTYPE_DOORSCALE },
		{ "prop", OBJTYPE_BASIC },
		{ "key", OBJTYPE_KEY },
		{ "alarm", OBJTYPE_ALARM },
		{ "cctv", OBJTYPE_CCTV },
		{ "ammo_crate", OBJTYPE_AMMOCRATE },
		{ "weapon_pickup", OBJTYPE_WEAPON },
		{ "character_spawn", OBJTYPE_CHR },
		{ "single_monitor", OBJTYPE_SINGLEMONITOR },
		{ "multi_monitor", OBJTYPE_MULTIMONITOR },
		{ "hanging_monitors", OBJTYPE_HANGINGMONITORS },
		{ "autogun", OBJTYPE_AUTOGUN },
		{ "linked_guns", OBJTYPE_LINKGUNS },
		{ "debris", OBJTYPE_DEBRIS },
		{ "hat", OBJTYPE_HAT },
		{ "grenade_probability", OBJTYPE_GRENADEPROB },
		{ "lift_door_link", OBJTYPE_LINKLIFTDOOR },
		{ "multi_ammo_crate", OBJTYPE_MULTIAMMOCRATE },
		{ "shield", OBJTYPE_SHIELD },
		{ "tag", OBJTYPE_TAG },
		{ "objective_begin", OBJTYPE_BEGINOBJECTIVE },
		{ "objective_end", OBJTYPE_ENDOBJECTIVE },
		{ "objective_destroy_object", OBJECTIVETYPE_DESTROYOBJ },
		{ "objective_complete_flags", OBJECTIVETYPE_COMPFLAGS },
		{ "objective_fail_flags", OBJECTIVETYPE_FAILFLAGS },
		{ "objective_collect_object", OBJECTIVETYPE_COLLECTOBJ },
		{ "objective_throw_object", OBJECTIVETYPE_THROWOBJ },
		{ "objective_holograph", OBJECTIVETYPE_HOLOGRAPH },
		{ "objective_marker", OBJECTIVETYPE_1F },
		{ "objective_enter_room", OBJECTIVETYPE_ENTERROOM },
		{ "objective_throw_in_room", OBJECTIVETYPE_THROWINROOM },
		{ "objective_marker_22", OBJTYPE_22 },
		{ "briefing", OBJTYPE_BRIEFING },
		{ "gas_bottle", OBJTYPE_GASBOTTLE },
		{ "rename_object", OBJTYPE_RENAMEOBJ },
		{ "padlocked_door", OBJTYPE_PADLOCKEDDOOR },
		{ "truck", OBJTYPE_TRUCK },
		{ "heli", OBJTYPE_HELI },
		{ "tank", OBJTYPE_TANK },
		{ "camera_position", OBJTYPE_CAMERAPOS },
		{ "glass", OBJTYPE_GLASS },
		{ "safe", OBJTYPE_SAFE },
		{ "safe_item", OBJTYPE_SAFEITEM },
		{ "tinted_glass", OBJTYPE_TINTEDGLASS },
		{ "lift", OBJTYPE_LIFT },
		{ "conditional_scenery", OBJTYPE_CONDITIONALSCENERY },
		{ "blocked_path", OBJTYPE_BLOCKEDPATH },
		{ "hoverbike", OBJTYPE_HOVERBIKE },
		{ "hover_prop", OBJTYPE_HOVERPROP },
		{ "fan", OBJTYPE_FAN },
		{ "hover_car", OBJTYPE_HOVERCAR },
		{ "pad_effect", OBJTYPE_PADEFFECT },
		{ "chopper", OBJTYPE_CHOPPER },
		{ "mine", OBJTYPE_MINE },
		{ "escalator_step", OBJTYPE_ESCASTEP },
	};

	if (!kind || !out_type) {
		return 0;
	}

	for (u32 i = 0; i < ARRAYCOUNT(maps); i++) {
		if (strcmp(kind, maps[i].kind) == 0) {
			*out_type = maps[i].type;
			return 1;
		}
	}

	return 0;
}

static s32 s_setupObjTypeHasDefaultBase(u8 type)
{
	switch (type) {
	case OBJTYPE_DOOR:
	case OBJTYPE_BASIC:
	case OBJTYPE_KEY:
	case OBJTYPE_ALARM:
	case OBJTYPE_CCTV:
	case OBJTYPE_AMMOCRATE:
	case OBJTYPE_WEAPON:
	case OBJTYPE_SINGLEMONITOR:
	case OBJTYPE_MULTIMONITOR:
	case OBJTYPE_HANGINGMONITORS:
	case OBJTYPE_AUTOGUN:
	case OBJTYPE_DEBRIS:
	case OBJTYPE_HAT:
	case OBJTYPE_MULTIAMMOCRATE:
	case OBJTYPE_SHIELD:
	case OBJTYPE_GASBOTTLE:
	case OBJTYPE_TRUCK:
	case OBJTYPE_HELI:
	case OBJTYPE_GLASS:
	case OBJTYPE_SAFE:
	case OBJTYPE_TINTEDGLASS:
	case OBJTYPE_LIFT:
	case OBJTYPE_HOVERBIKE:
	case OBJTYPE_HOVERPROP:
	case OBJTYPE_FAN:
	case OBJTYPE_HOVERCAR:
	case OBJTYPE_CHOPPER:
	case OBJTYPE_MINE:
	case OBJTYPE_ESCASTEP:
		return 1;
	default:
		return 0;
	}
}

static u32 s_setupCommandLengthBytesForType(u8 type)
{
	switch (type) {
	case OBJTYPE_CHR:                return (u32)sizeof(struct packedchr);
	case OBJTYPE_DOOR:               return (u32)sizeof(struct doorobj);
	case OBJTYPE_DOORSCALE:          return (u32)sizeof(struct doorscaleobj);
	case OBJTYPE_BASIC:              return (u32)sizeof(struct defaultobj);
	case OBJTYPE_DEBRIS:             return (u32)sizeof(struct debrisobj);
	case OBJTYPE_GLASS:              return (u32)sizeof(struct glassobj);
	case OBJTYPE_TINTEDGLASS:        return (u32)sizeof(struct tintedglassobj);
	case OBJTYPE_SAFE:               return (u32)sizeof(struct safeobj);
	case OBJTYPE_GASBOTTLE:          return (u32)sizeof(struct gasbottleobj);
	case OBJTYPE_KEY:                return (u32)sizeof(struct keyobj);
	case OBJTYPE_ALARM:              return (u32)sizeof(struct alarmobj);
	case OBJTYPE_CCTV:               return (u32)sizeof(struct cctvobj);
	case OBJTYPE_AMMOCRATE:          return (u32)sizeof(struct ammocrateobj);
	case OBJTYPE_WEAPON:             return (u32)sizeof(struct weaponobj);
	case OBJTYPE_SINGLEMONITOR:      return (u32)sizeof(struct singlemonitorobj);
	case OBJTYPE_MULTIMONITOR:       return (u32)sizeof(struct multimonitorobj);
	case OBJTYPE_HANGINGMONITORS:    return (u32)sizeof(struct hangingmonitorsobj);
	case OBJTYPE_AUTOGUN:            return (u32)sizeof(struct autogunobj);
	case OBJTYPE_LINKGUNS:           return (u32)sizeof(struct linkgunsobj);
	case OBJTYPE_HAT:                return (u32)sizeof(struct hatobj);
	case OBJTYPE_GRENADEPROB:        return (u32)sizeof(struct grenadeprobobj);
	case OBJTYPE_LINKLIFTDOOR:       return (u32)sizeof(struct linkliftdoorobj);
	case OBJTYPE_SAFEITEM:           return (u32)sizeof(struct safeitemobj);
	case OBJTYPE_MULTIAMMOCRATE:     return (u32)sizeof(struct multiammocrateobj);
	case OBJTYPE_SHIELD:             return (u32)sizeof(struct shieldobj);
	case OBJTYPE_TAG:                return (u32)sizeof(struct tag);
	case OBJTYPE_RENAMEOBJ:          return (u32)sizeof(struct textoverride);
	case OBJTYPE_BEGINOBJECTIVE:     return (u32)sizeof(struct objective);
	case OBJTYPE_ENDOBJECTIVE:       return (u32)sizeof(u32);
	case OBJECTIVETYPE_DESTROYOBJ:
	case OBJECTIVETYPE_COMPFLAGS:
	case OBJECTIVETYPE_FAILFLAGS:
	case OBJECTIVETYPE_COLLECTOBJ:
	case OBJECTIVETYPE_THROWOBJ:     return (u32)(sizeof(u32) * 2u);
	case OBJECTIVETYPE_HOLOGRAPH:    return (u32)sizeof(struct criteria_holograph);
	case OBJECTIVETYPE_1F:           return (u32)sizeof(u32);
	case OBJECTIVETYPE_ENTERROOM:    return (u32)sizeof(struct criteria_roomentered);
	case OBJECTIVETYPE_THROWINROOM:  return (u32)sizeof(struct criteria_throwinroom);
	case OBJTYPE_22:                 return (u32)sizeof(u32);
	case OBJTYPE_BRIEFING:           return (u32)sizeof(struct briefingobj);
	case OBJTYPE_PADLOCKEDDOOR:      return (u32)sizeof(struct padlockeddoorobj);
	case OBJTYPE_TRUCK:              return (u32)sizeof(struct truckobj);
	case OBJTYPE_HELI:               return (u32)sizeof(struct heliobj);
	case OBJTYPE_TANK:               return (u32)(32u * sizeof(u32));
	case OBJTYPE_CAMERAPOS:          return (u32)sizeof(struct cameraposobj);
	case OBJTYPE_LIFT:               return (u32)sizeof(struct liftobj);
	case OBJTYPE_CONDITIONALSCENERY: return (u32)sizeof(struct linksceneryobj);
	case OBJTYPE_BLOCKEDPATH:        return (u32)sizeof(struct blockedpathobj);
	case OBJTYPE_HOVERBIKE:          return (u32)sizeof(struct hoverbikeobj);
	case OBJTYPE_HOVERPROP:          return (u32)sizeof(struct hoverpropobj);
	case OBJTYPE_FAN:                return (u32)sizeof(struct fanobj);
	case OBJTYPE_HOVERCAR:           return (u32)sizeof(struct hovercarobj);
	case OBJTYPE_CHOPPER:            return (u32)sizeof(struct chopperobj);
	case OBJTYPE_PADEFFECT:          return (u32)sizeof(struct padeffectobj);
	case OBJTYPE_MINE:               return (u32)sizeof(struct weaponobj);
	case OBJTYPE_ESCASTEP:           return (u32)sizeof(struct escalatorobj);
	default:                         return 0;
	}
}

static void s_setupTableSetError(scenario_source_setup_table_t *table,
	const char *fmt, const char *a, const char *b)
{
	if (!table || table->error[0]) {
		return;
	}

	snprintf(table->error, sizeof(table->error), fmt,
		a ? a : "", b ? b : "");
}

static void s_setupFreeTable(scenario_source_setup_table_t *table)
{
	if (!table) {
		return;
	}
	for (s32 i = 0; i < table->count; i++) {
		if (table->records[i].bytes) {
			free(table->records[i].bytes);
		}
	}
	free(table->records);
	memset(table, 0, sizeof(*table));
}

static void s_aiTableSetError(scenario_source_ai_table_t *table,
	const char *fmt, const char *a, const char *b)
{
	if (!table || table->error[0]) {
		return;
	}
	snprintf(table->error, sizeof(table->error), fmt,
		a ? a : "", b ? b : "");
}

static void s_aiFreeTable(scenario_source_ai_table_t *table)
{
	if (!table) {
		return;
	}
	for (s32 i = 0; i < table->count; i++) {
		for (s32 j = 0; j < table->lists[i].count; j++) {
			free(table->lists[i].commands[j].operands);
		}
		free(table->lists[i].commands);
	}
	free(table->lists);
	memset(table, 0, sizeof(*table));
}

static void s_spawnTableSetError(scenario_source_spawn_table_t *table,
	const char *fmt, const char *a, const char *b)
{
	if (!table || table->error[0]) {
		return;
	}
	snprintf(table->error, sizeof(table->error), fmt,
		a ? a : "", b ? b : "");
}

static void s_spawnFreeTable(scenario_source_spawn_table_t *table)
{
	if (!table) {
		return;
	}
	free(table->rows);
	memset(table, 0, sizeof(*table));
}

static void s_setupInitRecordBytes(scenario_source_setup_record_t *record)
{
	if (!record || !record->bytes) {
		return;
	}

	memset(record->bytes, 0, record->len);
	if (record->type == OBJTYPE_CHR) {
		struct packedchr *chr = (struct packedchr *)record->bytes;
		chr->typenum = OBJTYPE_CHR;
	} else if (s_setupObjTypeHasDefaultBase(record->type)) {
		struct defaultobj *obj = (struct defaultobj *)record->bytes;
		obj->type = record->type;
	} else {
		u32 word = PD_BE32((u32)record->type);
		memcpy(record->bytes, &word, sizeof(word));
	}
}

static scenario_source_setup_record_t *s_setupFindOrAddRecord(
	scenario_source_setup_table_t *table, const char *record_id,
	const char *kind, s32 default_order)
{
	u8 type;
	u32 len;
	scenario_source_setup_record_t *record;

	if (!table || !record_id || !record_id[0] || !kind || !kind[0]) {
		return NULL;
	}

	for (s32 i = 0; i < table->count; i++) {
		if (strcmp(table->records[i].id, record_id) == 0) {
			return &table->records[i];
		}
	}

	if (!s_setupKindToObjType(kind, &type)) {
		s_setupTableSetError(table,
			"unknown setup record kind '%s' for '%s'", kind, record_id);
		return NULL;
	}

	len = s_setupCommandLengthBytesForType(type);
	if (len == 0) {
		s_setupTableSetError(table,
			"unsupported setup record kind '%s' for '%s'", kind, record_id);
		return NULL;
	}

	if (table->count >= table->capacity) {
		s32 new_capacity = table->capacity ? table->capacity * 2 : 32;
		scenario_source_setup_record_t *new_records =
			(scenario_source_setup_record_t *)realloc(table->records,
				(size_t)new_capacity * sizeof(*new_records));
		if (!new_records) {
			s_setupTableSetError(table,
				"setup record allocation failed for '%s'", record_id, "");
			return NULL;
		}
		memset(new_records + table->capacity, 0,
			(size_t)(new_capacity - table->capacity) * sizeof(*new_records));
		table->records = new_records;
		table->capacity = new_capacity;
	}

	record = &table->records[table->count++];
	memset(record, 0, sizeof(*record));
	strncpy(record->id, record_id, sizeof(record->id) - 1);
	strncpy(record->kind, kind, sizeof(record->kind) - 1);
	record->type = type;
	record->order = default_order;
	record->len = len;
	record->bytes = (u8 *)malloc(len);
	if (!record->bytes) {
		s_setupTableSetError(table,
			"setup command allocation failed for '%s'", record_id, "");
		return NULL;
	}
	s_setupInitRecordBytes(record);
	return record;
}

static s32 s_setupWriteRaw(scenario_source_setup_record_t *record,
	u32 offset, const void *src, u32 size)
{
	if (!record || !record->bytes || !src || offset > record->len
			|| size > record->len - offset) {
		return 0;
	}
	memcpy(record->bytes + offset, src, size);
	return 1;
}

static s32 s_setupWriteS32(scenario_source_setup_record_t *record,
	u32 offset, s32 value)
{
	return s_setupWriteRaw(record, offset, &value, sizeof(value));
}

static s32 s_setupWriteU32(scenario_source_setup_record_t *record,
	u32 offset, u32 value)
{
	return s_setupWriteRaw(record, offset, &value, sizeof(value));
}

static s32 s_setupWriteS16(scenario_source_setup_record_t *record,
	u32 offset, s32 value)
{
	s16 v = (s16)value;
	return s_setupWriteRaw(record, offset, &v, sizeof(v));
}

static s32 s_setupWriteU16(scenario_source_setup_record_t *record,
	u32 offset, s32 value)
{
	u16 v = (u16)value;
	return s_setupWriteRaw(record, offset, &v, sizeof(v));
}

static s32 s_setupWriteU8(scenario_source_setup_record_t *record,
	u32 offset, s32 value);
static s32 s_setupWriteS8(scenario_source_setup_record_t *record,
	u32 offset, s32 value);
static s32 s_setupWriteF32(scenario_source_setup_record_t *record,
	u32 offset, f32 value);

static s32 s_setupWriteRecordRefS16(scenario_source_setup_record_t *record,
	u32 offset, const char *ref_record_id)
{
	s32 ivalue;

	if (!ref_record_id || !ref_record_id[0]) {
		return s_setupWriteS16(record, offset, 0);
	}
	if (!s_setupRecordRefOffset(record, ref_record_id, &ivalue)) {
		return 0;
	}
	return s_setupWriteS16(record, offset, ivalue);
}

static s32 s_setupWriteRecordRefS32(scenario_source_setup_record_t *record,
	u32 offset, const char *ref_record_id)
{
	s32 ivalue;

	if (!ref_record_id || !ref_record_id[0]) {
		return s_setupWriteS32(record, offset, 0);
	}
	if (!s_setupRecordRefOffset(record, ref_record_id, &ivalue)) {
		return 0;
	}
	return s_setupWriteS32(record, offset, ivalue);
}

static const char *s_setupFieldAfterPrefix(const char *field,
	const char *prefix)
{
	size_t len;

	if (!field || !prefix) {
		return NULL;
	}

	len = strlen(prefix);
	if (strncmp(field, prefix, len) != 0 || field[len] != '.') {
		return NULL;
	}

	return field + len + 1;
}

static s32 s_setupApplyTvScreenField(scenario_source_setup_record_t *record,
	u32 base_offset, const char *member, const char *value)
{
	s32 ivalue;
	f32 fvalue;

	if (!record || !member || !value) {
		return 0;
	}

#define TV_U16(name) \
	if (strcmp(member, #name) == 0 && s_parseInteger(value, &ivalue)) { \
		return s_setupWriteU16(record, base_offset + (u32)offsetof(struct tvscreen, name), ivalue); \
	}
#define TV_S16(name) \
	if (strcmp(member, #name) == 0 && s_parseInteger(value, &ivalue)) { \
		return s_setupWriteS16(record, base_offset + (u32)offsetof(struct tvscreen, name), ivalue); \
	}
#define TV_U8(name) \
	if (strcmp(member, #name) == 0 && s_parseInteger(value, &ivalue)) { \
		return s_setupWriteU8(record, base_offset + (u32)offsetof(struct tvscreen, name), ivalue); \
	}
#define TV_F32(name) \
	if (strcmp(member, #name) == 0 && s_parseFloat(value, &fvalue)) { \
		return s_setupWriteF32(record, base_offset + (u32)offsetof(struct tvscreen, name), fvalue); \
	}

	TV_U16(offset);
	TV_S16(pause60);
	TV_F32(rot);
	TV_F32(xscale);
	TV_F32(xscalefrac);
	TV_F32(xscaleinc);
	TV_F32(xscaleold);
	TV_F32(xscalenew);
	TV_F32(yscale);
	TV_F32(yscalefrac);
	TV_F32(yscaleinc);
	TV_F32(yscaleold);
	TV_F32(yscalenew);
	TV_F32(xmid);
	TV_F32(xmidfrac);
	TV_F32(xmidinc);
	TV_F32(xmidold);
	TV_F32(xmidnew);
	TV_F32(ymid);
	TV_F32(ymidfrac);
	TV_F32(ymidinc);
	TV_F32(ymidold);
	TV_F32(ymidnew);
	TV_U8(red);
	TV_U8(redold);
	TV_U8(rednew);
	TV_U8(green);
	TV_U8(greenold);
	TV_U8(greennew);
	TV_U8(blue);
	TV_U8(blueold);
	TV_U8(bluenew);
	TV_U8(alpha);
	TV_U8(alphaold);
	TV_U8(alphanew);
	TV_F32(colfrac);
	TV_F32(colinc);

#undef TV_U16
#undef TV_S16
#undef TV_U8
#undef TV_F32
	return 0;
}

static s32 s_setupApplyCoordField(scenario_source_setup_record_t *record,
	u32 base_offset, const char *prefix, const char *field,
	const char *value)
{
	const char *member = s_setupFieldAfterPrefix(field, prefix);
	f32 fvalue;

	if (!member) {
		return -1;
	}
	if (!s_parseFloat(value, &fvalue)) {
		return 0;
	}
	if (strcmp(member, "x") == 0) {
		return s_setupWriteF32(record, base_offset + (u32)offsetof(struct coord, x), fvalue);
	}
	if (strcmp(member, "y") == 0) {
		return s_setupWriteF32(record, base_offset + (u32)offsetof(struct coord, y), fvalue);
	}
	if (strcmp(member, "z") == 0) {
		return s_setupWriteF32(record, base_offset + (u32)offsetof(struct coord, z), fvalue);
	}
	return 0;
}

static s32 s_setupApplyF32ArrayField(scenario_source_setup_record_t *record,
	u32 base_offset, u32 count, const char *prefix, const char *field,
	const char *value)
{
	u32 index;
	size_t prefix_len;
	f32 fvalue;

	if (!prefix || !field) {
		return -1;
	}
	prefix_len = strlen(prefix);
	if (strncmp(field, prefix, prefix_len) != 0 ||
			sscanf(field + prefix_len, "[%u]", &index) != 1) {
		return -1;
	}
	if (index >= count || !s_parseFloat(value, &fvalue)) {
		return 0;
	}
	return s_setupWriteF32(record, base_offset + index * (u32)sizeof(f32), fvalue);
}

static s32 s_setupApplyU8ArrayField(scenario_source_setup_record_t *record,
	u32 base_offset, u32 count, const char *prefix, const char *field,
	const char *value)
{
	u32 index;
	size_t prefix_len;
	s32 ivalue;

	if (!prefix || !field) {
		return -1;
	}
	prefix_len = strlen(prefix);
	if (strncmp(field, prefix, prefix_len) != 0 ||
			sscanf(field + prefix_len, "[%u]", &index) != 1) {
		return -1;
	}
	if (index >= count || !s_parseInteger(value, &ivalue)) {
		return 0;
	}
	return s_setupWriteU8(record, base_offset + index, ivalue);
}

static s32 s_setupApplyF32Matrix3Field(scenario_source_setup_record_t *record,
	u32 base_offset, const char *prefix, const char *field,
	const char *value)
{
	u32 r;
	u32 c;
	size_t prefix_len;
	f32 fvalue;

	if (!prefix || !field) {
		return -1;
	}
	prefix_len = strlen(prefix);
	if (strncmp(field, prefix, prefix_len) != 0 ||
			sscanf(field + prefix_len, "[%u][%u]", &r, &c) != 2) {
		return -1;
	}
	if (r >= 3 || c >= 3 || !s_parseFloat(value, &fvalue)) {
		return 0;
	}
	return s_setupWriteF32(record,
		base_offset + ((r * 3u + c) * (u32)sizeof(f32)), fvalue);
}

static s32 s_setupApplyHoverField(scenario_source_setup_record_t *record,
	u32 base_offset, const char *prefix, const char *field,
	const char *value)
{
	const char *member = s_setupFieldAfterPrefix(field, prefix);
	s32 ivalue;
	f32 fvalue;

	if (!member) {
		return -1;
	}

#define HOV_U8(name) \
	if (strcmp(member, #name) == 0 && s_parseInteger(value, &ivalue)) { \
		return s_setupWriteU8(record, base_offset + (u32)offsetof(struct hov, name), ivalue); \
	}
#define HOV_S32(name) \
	if (strcmp(member, #name) == 0 && s_parseInteger(value, &ivalue)) { \
		return s_setupWriteS32(record, base_offset + (u32)offsetof(struct hov, name), ivalue); \
	}
#define HOV_F32(name) \
	if (strcmp(member, #name) == 0 && s_parseFloat(value, &fvalue)) { \
		return s_setupWriteF32(record, base_offset + (u32)offsetof(struct hov, name), fvalue); \
	}
	HOV_U8(type);
	HOV_U8(flags);
	HOV_F32(bobycur);
	HOV_F32(bobytarget);
	HOV_F32(bobyspeed);
	HOV_F32(yrot);
	HOV_F32(bobpitchcur);
	HOV_F32(bobpitchtarget);
	HOV_F32(bobpitchspeed);
	HOV_F32(bobrollcur);
	HOV_F32(bobrolltarget);
	HOV_F32(bobrollspeed);
	HOV_F32(groundpitch);
	HOV_F32(y);
	HOV_F32(ground);
	HOV_S32(prevframe60);
	HOV_S32(prevgroundframe60);
#undef HOV_U8
#undef HOV_S32
#undef HOV_F32
	return 0;
}

static s32 s_setupWriteS8(scenario_source_setup_record_t *record,
	u32 offset, s32 value)
{
	s8 v = (s8)value;
	return s_setupWriteRaw(record, offset, &v, sizeof(v));
}

static s32 s_setupWriteU8(scenario_source_setup_record_t *record,
	u32 offset, s32 value)
{
	u8 v = (u8)value;
	return s_setupWriteRaw(record, offset, &v, sizeof(v));
}

static s32 s_setupWriteF32(scenario_source_setup_record_t *record,
	u32 offset, f32 value)
{
	return s_setupWriteRaw(record, offset, &value, sizeof(value));
}

static s32 s_setupResolveModelnum(const char *catalog_id, s32 *out)
{
	catalog_model_result_t result;

	if (!catalog_id || !catalog_id[0] || !out) {
		return 0;
	}

	if (!catalogResolveModel(catalog_id, &result)) {
		return 0;
	}

	*out = result.modelnum;
	return 1;
}

static s32 s_setupResolveWeaponnum(const char *catalog_id, s32 *out)
{
	catalog_weapon_result_t result;

	if (!catalog_id || !catalog_id[0] || !out) {
		return 0;
	}

	if (!catalogResolveWeapon(catalog_id, &result)) {
		return 0;
	}

	*out = result.weapon_num;
	return 1;
}

static s32 s_setupResolveWeaponSelector(const char *value, s32 *out)
{
	s32 slot;

	if (!value || !out) {
		return 0;
	}

	if (strcmp(value, "none") == 0) {
		*out = WEAPON_NONE;
		return 1;
	}

	if (sscanf(value, "mp_location_%d", &slot) == 1 &&
			slot >= 0 && slot <= 15) {
		*out = WEAPON_MPLOCATION00 + slot;
		return 1;
	}

	return 0;
}

static s32 s_setupResolveBodyNum(const char *catalog_id, s32 *out)
{
	catalog_body_result_t result;

	if (!catalog_id || !catalog_id[0] || !out) {
		return 0;
	}

	if (!catalogResolveBody(catalog_id, &result) || !result.entry) {
		return 0;
	}

	*out = result.entry->runtime_index;
	return 1;
}

static s32 s_setupResolveHeadNum(const char *catalog_id, s32 *out)
{
	catalog_head_result_t result;

	if (!catalog_id || !catalog_id[0] || !out) {
		return 0;
	}

	if (!catalogResolveHead(catalog_id, &result) || !result.entry) {
		return 0;
	}

	*out = result.entry->runtime_index;
	return 1;
}

static scenario_source_ai_list_t *s_aiFindOrAddList(
	scenario_source_ai_table_t *table, const char *ref, s32 id)
{
	if (!table) {
		return NULL;
	}
	if (!ref || !ref[0]) {
		s_aiTableSetError(table, "missing AI list ref '%s'", "", "");
		return NULL;
	}
	for (s32 i = 0; i < table->count; i++) {
		if (strcmp(table->lists[i].ref, ref) == 0) {
			if (table->lists[i].id != id) {
				s_aiTableSetError(table,
					"AI list ref '%s' changed id", ref, "");
				return NULL;
			}
			return &table->lists[i];
		}
	}
	if (table->count >= table->capacity) {
		s32 new_capacity = table->capacity ? table->capacity * 2 : 16;
		scenario_source_ai_list_t *new_lists =
			(scenario_source_ai_list_t *)realloc(table->lists,
				(size_t)new_capacity * sizeof(*table->lists));
		if (!new_lists) {
			s_aiTableSetError(table, "out of memory adding AI list '%s'",
				"", "");
			return NULL;
		}
		memset(new_lists + table->capacity, 0,
			(size_t)(new_capacity - table->capacity) * sizeof(*new_lists));
		table->lists = new_lists;
		table->capacity = new_capacity;
	}
	scenario_source_ai_list_t *list = &table->lists[table->count++];
	memset(list, 0, sizeof(*list));
	snprintf(list->ref, sizeof(list->ref), "%s", ref);
	list->id = id;
	return list;
}

static s32 s_aiParseOperands(char *value, u8 **out_bytes, u32 *out_count)
{
	u8 *bytes;
	u32 count = 0;
	u32 capacity = 0;

	if (out_bytes) *out_bytes = NULL;
	if (out_count) *out_count = 0;
	if (!value || !out_bytes || !out_count || !value[0]) {
		return 1;
	}

	char *cursor = value;
	while (cursor && *cursor) {
		char *token = cursor;
		char *comma = strchr(cursor, ',');
		u32 parsed;
		if (comma) {
			*comma = '\0';
			cursor = comma + 1;
		} else {
			cursor = NULL;
		}
		while (*token && isspace((unsigned char)*token)) token++;
		if (!*token) {
			continue;
		}
		if (!s_parseU32Value(token, &parsed) || parsed > 0xffu) {
			free(*out_bytes);
			*out_bytes = NULL;
			*out_count = 0;
			return 0;
		}
		if (count >= capacity) {
			u32 new_capacity = capacity ? capacity * 2u : 16u;
			bytes = (u8 *)realloc(*out_bytes, new_capacity);
			if (!bytes) {
				free(*out_bytes);
				*out_bytes = NULL;
				*out_count = 0;
				return 0;
			}
			*out_bytes = bytes;
			capacity = new_capacity;
		}
		(*out_bytes)[count++] = (u8)parsed;
	}
	*out_count = count;
	return 1;
}

static s32 s_aiApplyCatalogOperands(scenario_source_ai_table_t *table,
	scenario_source_ai_command_t *cmd, const char *model_id,
	const char *weapon_id, const char *body_id, const char *head_id)
{
	s32 value;

	if (!cmd) {
		return 0;
	}

	if ((cmd->opcode == AICMD_DROPITEM || cmd->opcode == AICMD_EQUIPHAT) &&
			model_id && model_id[0]) {
		if (cmd->operand_count < 2 ||
				!s_setupResolveModelnum(model_id, &value)) {
			s_aiTableSetError(table, "bad AI model catalog id '%s'",
				model_id, "");
			return 0;
		}
		cmd->operands[0] = (u8)((value >> 8) & 0xff);
		cmd->operands[1] = (u8)(value & 0xff);
	}

	if (cmd->opcode == AICMD_EQUIPWEAPON) {
		if (model_id && model_id[0]) {
			if (cmd->operand_count < 2 ||
					!s_setupResolveModelnum(model_id, &value)) {
				s_aiTableSetError(table, "bad AI weapon model catalog id '%s'",
					model_id, "");
				return 0;
			}
			cmd->operands[0] = (u8)((value >> 8) & 0xff);
			cmd->operands[1] = (u8)(value & 0xff);
		}
		if (weapon_id && weapon_id[0]) {
			if (cmd->operand_count < 3 ||
					!s_setupResolveWeaponnum(weapon_id, &value)) {
				s_aiTableSetError(table, "bad AI weapon catalog id '%s'",
					weapon_id, "");
				return 0;
			}
			cmd->operands[2] = (u8)value;
		}
	}

	if ((cmd->opcode == AICMD_SPAWNCHRATPAD ||
			cmd->opcode == AICMD_SPAWNCHRATCHR)) {
		if (body_id && body_id[0]) {
			if (cmd->operand_count < 1 ||
					!s_setupResolveBodyNum(body_id, &value)) {
				s_aiTableSetError(table, "bad AI body catalog id '%s'",
					body_id, "");
				return 0;
			}
			cmd->operands[0] = (u8)value;
		}
		if (head_id && head_id[0]) {
			if (cmd->operand_count < 2 ||
					!s_setupResolveHeadNum(head_id, &value)) {
				s_aiTableSetError(table, "bad AI head catalog id '%s'",
					head_id, "");
				return 0;
			}
			cmd->operands[1] = (u8)value;
		}
	}

	return 1;
}

static s32 s_aiAppendCommand(scenario_source_ai_table_t *table,
	scenario_source_ai_list_t *list, scenario_source_ai_command_t *cmd)
{
	if (!table || !list || !cmd) {
		return 0;
	}
	if (list->count >= list->capacity) {
		s32 new_capacity = list->capacity ? list->capacity * 2 : 16;
		scenario_source_ai_command_t *commands =
			(scenario_source_ai_command_t *)realloc(list->commands,
				(size_t)new_capacity * sizeof(*list->commands));
		if (!commands) {
			s_aiTableSetError(table, "out of memory adding AI command '%s'",
				"", "");
			return 0;
		}
		memset(commands + list->capacity, 0,
			(size_t)(new_capacity - list->capacity) * sizeof(*commands));
		list->commands = commands;
		list->capacity = new_capacity;
	}
	list->commands[list->count++] = *cmd;
	list->byte_count += 2u + cmd->operand_count;
	table->byte_count += 2u + cmd->operand_count;
	memset(cmd, 0, sizeof(*cmd));
	return 1;
}

static s32 s_loadAiListSourceRows(char *text,
	scenario_source_ai_table_t *table)
{
	char *cursor;
	char *line;

	if (!text || !table) {
		return 0;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		char *line_cursor = line;
		char *ailist_ref = s_nextField(&line_cursor);
		char *list_id = s_nextField(&line_cursor);
		char *graph_node = s_nextField(&line_cursor);
		char *command_index = s_nextField(&line_cursor);
		char *offset = s_nextField(&line_cursor);
		char *opcode = s_nextField(&line_cursor);
		char *opcode_name = s_nextField(&line_cursor);
		char *operands = s_nextField(&line_cursor);
		char *model_id = s_nextField(&line_cursor);
		char *weapon_id = s_nextField(&line_cursor);
		char *body_id = s_nextField(&line_cursor);
		char *head_id = s_nextField(&line_cursor);
		u32 parsed;
		u32 parsed_opcode;
		scenario_source_ai_list_t *list;
		scenario_source_ai_command_t cmd;

		(void)graph_node;
		(void)command_index;
		(void)offset;
		(void)opcode_name;

		if (!ailist_ref || !list_id || !opcode) {
			continue;
		}
		if (strcmp(ailist_ref, "ailist_ref") == 0) {
			continue;
		}
		if (!s_parseU32Value(list_id, &parsed) ||
				!s_parseU32Value(opcode, &parsed_opcode) ||
				parsed_opcode > 0xffffu) {
			s_aiTableSetError(table, "bad AI row '%s'", ailist_ref, "");
			return 0;
		}

		list = s_aiFindOrAddList(table, ailist_ref, (s32)parsed);
		if (!list) {
			return 0;
		}

		memset(&cmd, 0, sizeof(cmd));
		cmd.opcode = (u16)parsed_opcode;
		if (!s_aiParseOperands(operands, &cmd.operands,
				&cmd.operand_count) ||
				!s_aiApplyCatalogOperands(table, &cmd, model_id,
				weapon_id, body_id, head_id) ||
				!s_aiAppendCommand(table, list, &cmd)) {
			free(cmd.operands);
			return 0;
		}
	}

	return table->error[0] == '\0';
}

static s32 s_parseSpawnTeam(const char *value)
{
	if (!value || !value[0] || strcmp(value, "any") == 0 ||
			strcmp(value, "default") == 0) {
		return 0;
	}

	return s_parseIndexedRef(value);
}

static s32 s_spawnAppendRow(scenario_source_spawn_table_t *table,
	s32 padnum, s32 team)
{
	scenario_source_spawn_row_t *rows;
	s32 new_capacity;

	if (!table || padnum < 0 || padnum > 0x7fff) {
		return 0;
	}
	if (table->count >= table->capacity) {
		new_capacity = table->capacity ? table->capacity * 2 : 32;
		rows = (scenario_source_spawn_row_t *)realloc(table->rows,
			(size_t)new_capacity * sizeof(*table->rows));
		if (!rows) {
			s_spawnTableSetError(table,
				"out of memory adding spawn '%s'", "", "");
			return 0;
		}
		table->rows = rows;
		table->capacity = new_capacity;
	}
	table->rows[table->count].padnum = padnum;
	table->rows[table->count].team = team >= 0 ? team : 0;
	table->count++;
	return 1;
}

static s32 s_loadSpawnSourceRows(char *text,
	scenario_source_spawn_table_t *table)
{
	char *cursor;
	char *line;

	if (!text || !table) {
		return 0;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		char *line_cursor = line;
		char *spawn_id = s_nextField(&line_cursor);
		char *pad_ref = s_nextField(&line_cursor);
		char *room_ref = s_nextField(&line_cursor);
		char *team = s_nextField(&line_cursor);
		char *profile = s_nextField(&line_cursor);
		s32 padnum;
		s32 teamnum;

		(void)room_ref;
		(void)profile;

		if (!spawn_id || !pad_ref || !team) {
			continue;
		}
		if (strcmp(spawn_id, "spawn_id") == 0) {
			continue;
		}

		padnum = s_parseIndexedRef(pad_ref);
		teamnum = s_parseSpawnTeam(team);
		if (padnum < 0 || padnum > 0x7fff || teamnum < 0) {
			s_spawnTableSetError(table, "bad spawn row '%s'",
				spawn_id, "");
			return 0;
		}
		if (!s_spawnAppendRow(table, padnum, teamnum)) {
			s_spawnTableSetError(table, "bad spawn row '%s'",
				spawn_id, "");
			return 0;
		}
	}

	return table->error[0] == '\0';
}

static s32 s_setupApplyDefaultField(scenario_source_setup_record_t *record,
	const char *field, const char *type, const char *value,
	const char *catalog_id)
{
	s32 ivalue;
	s32 applied;
	u32 uvalue;
	f32 fvalue;
	u32 r;
	u32 c;
	u32 index;

	if (!record || !field) {
		return 0;
	}

	if (strcmp(field, "base.extra_scale") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU16(record, (u32)offsetof(struct defaultobj, extrascale), ivalue);
	}
	if (strcmp(field, "base.hidden2") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU8(record, (u32)offsetof(struct defaultobj, hidden2), ivalue);
	}
	if (strcmp(field, "base.model") == 0) {
		if (!catalog_id || !catalog_id[0]) {
			return 1;
		}
		if (!s_setupResolveModelnum(catalog_id, &ivalue)) {
			return 0;
		}
		return s_setupWriteS16(record, (u32)offsetof(struct defaultobj, modelnum), ivalue);
	}
	if (strcmp(field, "base.pad") == 0) {
		ivalue = s_parseIndexedRef(value);
		return s_setupWriteS16(record, (u32)offsetof(struct defaultobj, pad), ivalue);
	}
	if (strcmp(field, "base.flags") == 0 &&
			s_parseU32Value(value, &uvalue)) {
		return s_setupWriteU32(record, (u32)offsetof(struct defaultobj, flags), uvalue);
	}
	if (strcmp(field, "base.flags2") == 0 &&
			s_parseU32Value(value, &uvalue)) {
		return s_setupWriteU32(record, (u32)offsetof(struct defaultobj, flags2), uvalue);
	}
	if (strcmp(field, "base.flags3") == 0 &&
			s_parseU32Value(value, &uvalue)) {
		return s_setupWriteU32(record, (u32)offsetof(struct defaultobj, flags3), uvalue);
	}
	if (sscanf(field, "base.rotation[%u][%u]", &r, &c) == 2 &&
			r < 3 && c < 3 && s_parseFloat(value, &fvalue)) {
		return s_setupWriteF32(record,
			(u32)offsetof(struct defaultobj, realrot)
				+ ((r * 3u + c) * (u32)sizeof(f32)),
			fvalue);
	}
	if (strcmp(field, "base.hidden") == 0 &&
			s_parseU32Value(value, &uvalue)) {
		return s_setupWriteU32(record, (u32)offsetof(struct defaultobj, hidden), uvalue);
	}
	if (strcmp(field, "base.damage") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteS16(record, (u32)offsetof(struct defaultobj, damage), ivalue);
	}
	if (strcmp(field, "base.max_damage") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteS16(record, (u32)offsetof(struct defaultobj, maxdamage), ivalue);
	}
	if (sscanf(field, "base.shade_color[%u]", &index) == 1 &&
			index < 4 && s_parseInteger(value, &ivalue)) {
		return s_setupWriteU8(record,
			(u32)offsetof(struct defaultobj, shadecol) + index, ivalue);
	}
	if (sscanf(field, "base.next_color[%u]", &index) == 1 &&
			index < 4 && s_parseInteger(value, &ivalue)) {
		return s_setupWriteU8(record,
			(u32)offsetof(struct defaultobj, nextcol) + index, ivalue);
	}
	if (strcmp(field, "base.floor_color") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU16(record, (u32)offsetof(struct defaultobj, floorcol), ivalue);
	}
	if (strcmp(field, "base.geo_count") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteS8(record, (u32)offsetof(struct defaultobj, geocount), ivalue);
	}

	(void)type;
	return 0;
}

static s32 s_setupApplyCharacterField(scenario_source_setup_record_t *record,
	const char *field, const char *type, const char *value,
	const char *catalog_id)
{
	s32 ivalue;
	u32 uvalue;

	if (strcmp(field, "character.index") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteS16(record, (u32)offsetof(struct packedchr, chrindex), ivalue);
	}
	if (strcmp(field, "character.kind") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteS8(record, (u32)offsetof(struct packedchr, typenum), ivalue);
	}
	if (strcmp(field, "character.spawn_flags") == 0 &&
			s_parseU32Value(value, &uvalue)) {
		return s_setupWriteU32(record, (u32)offsetof(struct packedchr, spawnflags), uvalue);
	}
	if (strcmp(field, "character.slot") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteS16(record, (u32)offsetof(struct packedchr, chrnum), ivalue);
	}
	if (strcmp(field, "character.pad") == 0) {
		ivalue = s_parseIndexedRef(value);
		return s_setupWriteU16(record, (u32)offsetof(struct packedchr, padnum), ivalue);
	}
	if (strcmp(field, "character.body") == 0) {
		if (!s_setupResolveBodyNum(catalog_id, &ivalue)) {
			return 0;
		}
		return s_setupWriteU8(record, (u32)offsetof(struct packedchr, bodynum), ivalue);
	}
	if (strcmp(field, "character.head") == 0) {
		if (type && strcmp(type, "head_selector") == 0 &&
				value && strcmp(value, "random") == 0) {
			return s_setupWriteS8(record, (u32)offsetof(struct packedchr, headnum),
				HEAD_RANDOM);
		}
		if (type && strcmp(type, "head_selector") == 0 &&
				value && strcmp(value, "embedded") == 0) {
			return s_setupWriteS8(record, (u32)offsetof(struct packedchr, headnum), 0);
		}
		if (!catalog_id || !catalog_id[0]) {
			return s_setupWriteS8(record, (u32)offsetof(struct packedchr, headnum), -1);
		}
		if (!s_setupResolveHeadNum(catalog_id, &ivalue)) {
			return 0;
		}
		return s_setupWriteS8(record, (u32)offsetof(struct packedchr, headnum), ivalue);
	}
	if (strcmp(field, "character.ai_list") == 0) {
		ivalue = s_parseIndexedRef(value);
		return s_setupWriteU16(record, (u32)offsetof(struct packedchr, ailistnum), ivalue);
	}
	if (strcmp(field, "character.pad_preset") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU16(record, (u32)offsetof(struct packedchr, padpreset), ivalue);
	}
	if (strcmp(field, "character.character_preset") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU16(record, (u32)offsetof(struct packedchr, chrpreset), ivalue);
	}
	if (strcmp(field, "character.hearing_scale") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU16(record, (u32)offsetof(struct packedchr, hearscale), ivalue);
	}
	if (strcmp(field, "character.view_distance") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU16(record, (u32)offsetof(struct packedchr, viewdist), ivalue);
	}
	if (strcmp(field, "character.flags") == 0 &&
			s_parseU32Value(value, &uvalue)) {
		return s_setupWriteU32(record, (u32)offsetof(struct packedchr, flags), uvalue);
	}
	if (strcmp(field, "character.flags2") == 0 &&
			s_parseU32Value(value, &uvalue)) {
		return s_setupWriteU32(record, (u32)offsetof(struct packedchr, flags2), uvalue);
	}
	if (strcmp(field, "character.team") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU8(record, (u32)offsetof(struct packedchr, team), ivalue);
	}
	if (strcmp(field, "character.squadron") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU8(record, (u32)offsetof(struct packedchr, squadron), ivalue);
	}
	if (strcmp(field, "character.chair") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteS16(record, (u32)offsetof(struct packedchr, chair), ivalue);
	}
	if (strcmp(field, "character.conversation_talk") == 0 &&
			s_parseU32Value(value, &uvalue)) {
		return s_setupWriteU32(record, (u32)offsetof(struct packedchr, convtalk), uvalue);
	}
	if (strcmp(field, "character.attitude") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU8(record, (u32)offsetof(struct packedchr, tude), ivalue);
	}
	if (strcmp(field, "character.natural_animation") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU8(record, (u32)offsetof(struct packedchr, naturalanim), ivalue);
	}
	if (strcmp(field, "character.visible_yaw_angle") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU8(record, (u32)offsetof(struct packedchr, yvisang), ivalue);
	}
	if (strcmp(field, "character.team_scan_distance") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU8(record, (u32)offsetof(struct packedchr, teamscandist), ivalue);
	}

	return 0;
}

static s32 s_setupApplyKnownField(scenario_source_setup_table_t *table,
	scenario_source_setup_record_t *record, const char *field,
	const char *type, const char *value, const char *catalog_id,
	const char *ref_record_id)
{
	s32 ivalue;
	s32 applied;
	u32 uvalue;
	f32 fvalue;

	if (!record || !field || !field[0]) {
		return 1;
	}

	if (strcmp(field, "command.order") == 0) {
		if (s_parseInteger(value, &ivalue)) {
			record->order = ivalue;
			return 1;
		}
		return 0;
	}

	if (s_setupObjTypeHasDefaultBase(record->type)
			&& strncmp(field, "base.", 5) == 0) {
		return s_setupApplyDefaultField(record, field, type, value,
			catalog_id);
	}

	if (record->type == OBJTYPE_CHR
			&& strncmp(field, "character.", 10) == 0) {
		return s_setupApplyCharacterField(record, field, type, value,
			catalog_id);
	}

	switch (record->type) {
	case OBJTYPE_DOOR:
#define DOOR_F32(name) \
		if (strcmp(field, "door." #name) == 0 && s_parseFloat(value, &fvalue)) { \
			return s_setupWriteF32(record, (u32)offsetof(struct doorobj, name), fvalue); \
		}
#define DOOR_U8(name) \
		if (strcmp(field, "door." #name) == 0 && s_parseInteger(value, &ivalue)) { \
			return s_setupWriteU8(record, (u32)offsetof(struct doorobj, name), ivalue); \
		}
#define DOOR_S8(name) \
		if (strcmp(field, "door." #name) == 0 && s_parseInteger(value, &ivalue)) { \
			return s_setupWriteS8(record, (u32)offsetof(struct doorobj, name), ivalue); \
		}
#define DOOR_S16(name) \
		if (strcmp(field, "door." #name) == 0 && s_parseInteger(value, &ivalue)) { \
			return s_setupWriteS16(record, (u32)offsetof(struct doorobj, name), ivalue); \
		}
#define DOOR_U16(name) \
		if (strcmp(field, "door." #name) == 0 && s_parseInteger(value, &ivalue)) { \
			return s_setupWriteU16(record, (u32)offsetof(struct doorobj, name), ivalue); \
		}
#define DOOR_S32(name) \
		if (strcmp(field, "door." #name) == 0 && s_parseInteger(value, &ivalue)) { \
			return s_setupWriteS32(record, (u32)offsetof(struct doorobj, name), ivalue); \
		}
		DOOR_F32(maxfrac);
		DOOR_F32(perimfrac);
		DOOR_F32(accel);
		DOOR_F32(decel);
		DOOR_F32(maxspeed);
		DOOR_U16(doorflags);
		DOOR_U16(doortype);
		if (strcmp(field, "door.key_flags") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteU32(record, (u32)offsetof(struct doorobj, keyflags), uvalue);
		}
		DOOR_S32(autoclosetime);
		DOOR_F32(frac);
		DOOR_F32(fracspeed);
		DOOR_S8(mode);
		DOOR_S8(glasshits);
		DOOR_S16(fadealpha);
		DOOR_S16(xludist);
		DOOR_S16(opadist);
		applied = s_setupApplyCoordField(record,
			(u32)offsetof(struct doorobj, startpos),
			"door.start_position", field, value);
		if (applied >= 0) {
			return applied;
		}
		applied = s_setupApplyF32Matrix3Field(record,
			(u32)offsetof(struct doorobj, mtx98),
			"door.matrix", field, value);
		if (applied >= 0) {
			return applied;
		}
		DOOR_S32(lastopen60);
		DOOR_S16(portalnum);
		DOOR_S8(soundtype);
		DOOR_S8(fadetime60);
		DOOR_S32(lastcalc60);
		DOOR_U8(laserfade);
		applied = s_setupApplyU8ArrayField(record,
			(u32)offsetof(struct doorobj, shadeinfo1),
			4, "door.shade_info_player1", field, value);
		if (applied >= 0) {
			return applied;
		}
		applied = s_setupApplyU8ArrayField(record,
			(u32)offsetof(struct doorobj, shadeinfo2),
			4, "door.shade_info_player2", field, value);
		if (applied >= 0) {
			return applied;
		}
		DOOR_U8(actual1);
		DOOR_U8(actual2);
		DOOR_U8(extra1);
		DOOR_U8(extra2);
#undef DOOR_F32
#undef DOOR_U8
#undef DOOR_S8
#undef DOOR_S16
#undef DOOR_U16
#undef DOOR_S32
		break;
	case OBJTYPE_DOORSCALE:
		if (strcmp(field, "door_scale.scale") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct doorscaleobj, scale), ivalue);
		}
		break;
	case OBJTYPE_KEY:
		if (strcmp(field, "key.flags") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteU32(record, (u32)offsetof(struct keyobj, keyflags), uvalue);
		}
		break;
	case OBJTYPE_CCTV:
		if (strcmp(field, "cctv.look_at_pad") == 0) {
			ivalue = s_parseIndexedRef(value);
			return s_setupWriteS16(record, (u32)offsetof(struct cctvobj, lookatpadnum), ivalue);
		}
		if (strcmp(field, "cctv.to_left") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct cctvobj, toleft), ivalue);
		}
		if (strcmp(field, "cctv.y_zero") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cctvobj, yzero), fvalue);
		}
		if (strcmp(field, "cctv.y_rot") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cctvobj, yrot), fvalue);
		}
		if (strcmp(field, "cctv.y_left") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cctvobj, yleft), fvalue);
		}
		if (strcmp(field, "cctv.y_right") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cctvobj, yright), fvalue);
		}
		if (strcmp(field, "cctv.y_speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cctvobj, yspeed), fvalue);
		}
		if (strcmp(field, "cctv.y_max_speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cctvobj, ymaxspeed), fvalue);
		}
		if (strcmp(field, "cctv.see_bond_time60") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct cctvobj, seebondtime60), ivalue);
		}
		if (strcmp(field, "cctv.max_distance") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cctvobj, maxdist), fvalue);
		}
		if (strcmp(field, "cctv.x_zero") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cctvobj, xzero), fvalue);
		}
		break;
	case OBJTYPE_AMMOCRATE:
		if (strcmp(field, "ammo_crate.ammo_kind") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct ammocrateobj, ammotype), ivalue);
		}
		break;
	case OBJTYPE_WEAPON:
	case OBJTYPE_MINE:
		if (strcmp(field, "pickup.weapon") == 0) {
			if (type && strcmp(type, "weapon_selector") == 0) {
				if (!s_setupResolveWeaponSelector(value, &ivalue)) {
					return 0;
				}
				return s_setupWriteU8(record, (u32)offsetof(struct weaponobj, weaponnum),
					ivalue);
			}
			if (!s_setupResolveWeaponnum(catalog_id, &ivalue)) {
				return 0;
			}
			return s_setupWriteU8(record, (u32)offsetof(struct weaponobj, weaponnum), ivalue);
		}
		if (strcmp(field, "pickup.unknown_5d") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS8(record, (u32)offsetof(struct weaponobj, unk5d), ivalue);
		}
		if (strcmp(field, "pickup.unknown_5e") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS8(record, (u32)offsetof(struct weaponobj, unk5e), ivalue);
		}
		if (strcmp(field, "pickup.fire_mode") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU8(record, (u32)offsetof(struct weaponobj, gunfunc), ivalue);
		}
		if (strcmp(field, "pickup.fadeout_timer60") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS8(record, (u32)offsetof(struct weaponobj, fadeouttimer60), ivalue);
		}
		if (strcmp(field, "pickup.dual_weapon") == 0) {
			if (type && strcmp(type, "weapon_selector") == 0 &&
					value && strcmp(value, "none") == 0) {
				return s_setupWriteS8(record, (u32)offsetof(struct weaponobj, dualweaponnum),
					-1);
			}
			if (type && strcmp(type, "weapon_selector") == 0) {
				if (!s_setupResolveWeaponSelector(value, &ivalue)) {
					return 0;
				}
				return s_setupWriteS8(record, (u32)offsetof(struct weaponobj, dualweaponnum),
					ivalue);
			}
			if (!catalog_id || !catalog_id[0]) {
				return s_setupWriteS8(record, (u32)offsetof(struct weaponobj, dualweaponnum), -1);
			}
			if (!s_setupResolveWeaponnum(catalog_id, &ivalue)) {
				return 0;
			}
			return s_setupWriteS8(record, (u32)offsetof(struct weaponobj, dualweaponnum), ivalue);
		}
		if (strcmp(field, "pickup.team_or_timer240") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct weaponobj, team), ivalue);
		}
		break;
	case OBJTYPE_AUTOGUN:
#define AUTOGUN_F32(name) \
		if (strcmp(field, "autogun." #name) == 0 && s_parseFloat(value, &fvalue)) { \
			return s_setupWriteF32(record, (u32)offsetof(struct autogunobj, name), fvalue); \
		}
#define AUTOGUN_S32(name) \
		if (strcmp(field, "autogun." #name) == 0 && s_parseInteger(value, &ivalue)) { \
			return s_setupWriteS32(record, (u32)offsetof(struct autogunobj, name), ivalue); \
		}
		if (strcmp(field, "autogun.target_pad") == 0) {
			ivalue = s_parseIndexedRef(value);
			return s_setupWriteS16(record, (u32)offsetof(struct autogunobj, targetpad), ivalue);
		}
		if (strcmp(field, "autogun.firing") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS8(record, (u32)offsetof(struct autogunobj, firing), ivalue);
		}
		if (strcmp(field, "autogun.fire_count") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU8(record, (u32)offsetof(struct autogunobj, firecount), ivalue);
		}
		AUTOGUN_F32(yzero);
		AUTOGUN_F32(ymaxleft);
		AUTOGUN_F32(ymaxright);
		AUTOGUN_F32(yrot);
		AUTOGUN_F32(yspeed);
		AUTOGUN_F32(xzero);
		AUTOGUN_F32(xrot);
		AUTOGUN_F32(xspeed);
		AUTOGUN_F32(maxspeed);
		if (strcmp(field, "autogun.aim_distance") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct autogunobj, aimdist), fvalue);
		}
		if (strcmp(field, "autogun.barrel_speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct autogunobj, barrelspeed), fvalue);
		}
		if (strcmp(field, "autogun.barrel_rot") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct autogunobj, barrelrot), fvalue);
		}
		if (strcmp(field, "autogun.last_see_bond60") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct autogunobj, lastseebond60), ivalue);
		}
		if (strcmp(field, "autogun.last_aim_bond60") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct autogunobj, lastaimbond60), ivalue);
		}
		if (strcmp(field, "autogun.allow_sound_frame") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct autogunobj, allowsoundframe), ivalue);
		}
		if (strcmp(field, "autogun.shot_bond_sum") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct autogunobj, shotbondsum), fvalue);
		}
		if (strcmp(field, "autogun.target_team") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU8(record, (u32)offsetof(struct autogunobj, targetteam), ivalue);
		}
		if (strcmp(field, "autogun.ammo_quantity") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU8(record, (u32)offsetof(struct autogunobj, ammoquantity), ivalue);
		}
		if (strcmp(field, "autogun.next_character_test") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct autogunobj, nextchrtest), ivalue);
		}
#undef AUTOGUN_F32
#undef AUTOGUN_S32
		break;
	case OBJTYPE_LINKGUNS:
		if (strcmp(field, "linked_guns.weapon_1") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS16(record,
				(u32)offsetof(struct linkgunsobj, offset1),
				ref_record_id);
		}
		if (strcmp(field, "linked_guns.weapon_2") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS16(record,
				(u32)offsetof(struct linkgunsobj, offset2),
				ref_record_id);
		}
		break;
	case OBJTYPE_GRENADEPROB:
		if (strcmp(field, "grenade_probability.character") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct grenadeprobobj, chrnum), ivalue);
		}
		if (strcmp(field, "grenade_probability.percent") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU16(record, (u32)offsetof(struct grenadeprobobj, probability), ivalue);
		}
		break;
	case OBJTYPE_SINGLEMONITOR: {
		const char *member = s_setupFieldAfterPrefix(field, "monitor.screen");
		if (member) {
			return s_setupApplyTvScreenField(record,
				(u32)offsetof(struct singlemonitorobj, screen),
				member, value);
		}
		if (strcmp(field, "monitor.owner") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS16(record,
				(u32)offsetof(struct singlemonitorobj, owneroffset),
				ref_record_id);
		}
		if (strcmp(field, "monitor.owner_part") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS8(record,
				(u32)offsetof(struct singlemonitorobj, ownerpart), ivalue);
		}
		if (strcmp(field, "monitor.image") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU8(record,
				(u32)offsetof(struct singlemonitorobj, imagenum), ivalue);
		}
		break;
	}
	case OBJTYPE_MULTIMONITOR: {
		u32 index;
		char member[64];

		if (sscanf(field, "monitor.screens[%u].%63s", &index, member) == 2
				&& index < 4) {
			return s_setupApplyTvScreenField(record,
				(u32)offsetof(struct multimonitorobj, screens)
					+ index * (u32)sizeof(struct tvscreen),
				member, value);
		}
		if (sscanf(field, "monitor.images[%u]", &index) == 1
				&& index < 4 && s_parseInteger(value, &ivalue)) {
			return s_setupWriteU8(record,
				(u32)offsetof(struct multimonitorobj, imagenums) + index,
				ivalue);
		}
		break;
	}
	case OBJTYPE_LINKLIFTDOOR:
		if (strcmp(field, "lift_door_link.door") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct linkliftdoorobj, door),
				ref_record_id);
		}
		if (strcmp(field, "lift_door_link.lift") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct linkliftdoorobj, lift),
				ref_record_id);
		}
		if (strcmp(field, "lift_door_link.stop") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct linkliftdoorobj, stopnum), ivalue);
		}
		break;
	case OBJTYPE_MULTIAMMOCRATE: {
		u32 index;
		char member[32];
		u32 slot_base;

		if (sscanf(field, "multi_ammo_crate.slots[%u].%31s",
				&index, member) != 2 ||
				index >= ARRAYCOUNT(((struct multiammocrateobj *)0)->slots)) {
			break;
		}

		slot_base = (u32)offsetof(struct multiammocrateobj, slots)
			+ index * (u32)sizeof(struct multiammocrateslot);
		if (strcmp(member, "model") == 0) {
			if (!s_setupResolveModelnum(catalog_id, &ivalue)) {
				return 0;
			}
			return s_setupWriteU16(record,
				slot_base + (u32)offsetof(struct multiammocrateslot, modelnum),
				ivalue);
		}
		if (strcmp(member, "quantity") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU16(record,
				slot_base + (u32)offsetof(struct multiammocrateslot, quantity),
				ivalue);
		}
		break;
	}
	case OBJTYPE_SHIELD:
		if (strcmp(field, "shield.initial_amount") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct shieldobj, initialamount), fvalue);
		}
		if (strcmp(field, "shield.amount") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct shieldobj, amount), fvalue);
		}
		if (strcmp(field, "shield.unknown_64") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteU32(record, (u32)offsetof(struct shieldobj, unk64), uvalue);
		}
		break;
	case OBJTYPE_TAG:
		if (strcmp(field, "tag.id") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU16(record, (u32)offsetof(struct tag, tagnum), ivalue);
		}
		if (strcmp(field, "tag.target") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS16(record,
				(u32)offsetof(struct tag, cmdoffset), ref_record_id);
		}
		break;
	case OBJTYPE_BEGINOBJECTIVE:
		if (strcmp(field, "objective.index") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct objective, index), ivalue);
		}
		if (strcmp(field, "objective.text_token") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteU32(record, (u32)offsetof(struct objective, text), uvalue);
		}
		if (strcmp(field, "objective.flags") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU8(record, (u32)offsetof(struct objective, flags), ivalue);
		}
		if (strcmp(field, "objective.difficulty_mask") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteS8(record, (u32)offsetof(struct objective, difficulties), (s32)uvalue);
		}
		break;
	case OBJECTIVETYPE_DESTROYOBJ:
	case OBJECTIVETYPE_COLLECTOBJ:
	case OBJECTIVETYPE_THROWOBJ:
		if (strcmp(field, "objective_step.target_tag") == 0) {
			u32 be;
			ivalue = s_parseIndexedRef(value);
			if (ivalue < 0) {
				return 0;
			}
			be = PD_BE32((u32)ivalue);
			return s_setupWriteU32(record, sizeof(u32), be);
		}
		if (strcmp(field, "objective_step.argument") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			u32 be = PD_BE32(uvalue);
			return s_setupWriteU32(record, sizeof(u32), be);
		}
		break;
	case OBJECTIVETYPE_COMPFLAGS:
	case OBJECTIVETYPE_FAILFLAGS:
		if (strcmp(field, "objective_step.stage_flag") == 0 &&
				s_parseStageFlagRef(value, &uvalue)) {
			u32 be = PD_BE32(uvalue);
			return s_setupWriteU32(record, sizeof(u32), be);
		}
		if (strcmp(field, "objective_step.argument") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			u32 be = PD_BE32(uvalue);
			return s_setupWriteU32(record, sizeof(u32), be);
		}
		break;
	case OBJECTIVETYPE_HOLOGRAPH:
		if ((strcmp(field, "objective_holograph.target_tag") == 0 ||
				strcmp(field, "objective_holograph.object") == 0) &&
				((ivalue = s_parseIndexedRef(value)) >= 0 ||
					s_parseInteger(value, &ivalue))) {
			return s_setupWriteU32(record, (u32)offsetof(struct criteria_holograph, obj), (u32)ivalue);
		}
		if (strcmp(field, "objective_holograph.status") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU32(record, (u32)offsetof(struct criteria_holograph, status), (u32)ivalue);
		}
		break;
	case OBJECTIVETYPE_ENTERROOM:
		if (strcmp(field, "objective_enter_room.pad") == 0) {
			ivalue = s_parseIndexedRef(value);
			return s_setupWriteS32(record, (u32)offsetof(struct criteria_roomentered, pad), ivalue);
		}
		if (strcmp(field, "objective_enter_room.status") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct criteria_roomentered, status), ivalue);
		}
		break;
	case OBJECTIVETYPE_THROWINROOM:
		if (strcmp(field, "objective_throw_in_room.match_value") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct criteria_throwinroom, unk04), ivalue);
		}
		if (strcmp(field, "objective_throw_in_room.pad") == 0) {
			ivalue = s_parseIndexedRef(value);
			return s_setupWriteS32(record, (u32)offsetof(struct criteria_throwinroom, pad), ivalue);
		}
		if (strcmp(field, "objective_throw_in_room.status") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct criteria_throwinroom, status), ivalue);
		}
		break;
	case OBJTYPE_BRIEFING:
		if (strcmp(field, "briefing.kind") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU32(record, (u32)offsetof(struct briefingobj, type), (u32)ivalue);
		}
		if (strcmp(field, "briefing.text_token") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteU32(record, (u32)offsetof(struct briefingobj, text), uvalue);
		}
		break;
	case OBJTYPE_RENAMEOBJ:
		if (strcmp(field, "rename_object.target") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct textoverride, objoffset), ref_record_id);
		}
		if (strcmp(field, "rename_object.weapon") == 0) {
			if (type && strcmp(type, "weapon_selector") == 0) {
				if (!s_setupResolveWeaponSelector(value, &ivalue)) {
					return 0;
				}
				return s_setupWriteS32(record,
					(u32)offsetof(struct textoverride, weapon), ivalue);
			}
			if (!s_setupResolveWeaponnum(catalog_id, &ivalue)) {
				return 0;
			}
			return s_setupWriteS32(record,
				(u32)offsetof(struct textoverride, weapon), ivalue);
		}
		if (strcmp(field, "rename_object.obtain_text") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteU32(record,
				(u32)offsetof(struct textoverride, obtaintext), uvalue);
		}
		if (strcmp(field, "rename_object.owner_text") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteU32(record,
				(u32)offsetof(struct textoverride, ownertext), uvalue);
		}
		if (strcmp(field, "rename_object.inventory_text") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteU32(record,
				(u32)offsetof(struct textoverride, inventorytext), uvalue);
		}
		if (strcmp(field, "rename_object.inventory2_text") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteU32(record,
				(u32)offsetof(struct textoverride, inventory2text), uvalue);
		}
		if (strcmp(field, "rename_object.pickup_text") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteU32(record,
				(u32)offsetof(struct textoverride, pickuptext), uvalue);
		}
		break;
	case OBJTYPE_TRUCK:
		if (strcmp(field, "vehicle.ai_offset") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU16(record, (u32)offsetof(struct truckobj, aioffset), ivalue);
		}
		if (strcmp(field, "vehicle.ai_return_list") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct truckobj, aireturnlist), ivalue);
		}
		if (strcmp(field, "vehicle.speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct truckobj, speed), fvalue);
		}
		if (strcmp(field, "vehicle.wheel_x_rot") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct truckobj, wheelxrot), fvalue);
		}
		if (strcmp(field, "vehicle.wheel_y_rot") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct truckobj, wheelyrot), fvalue);
		}
		if (strcmp(field, "vehicle.speed_aim") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct truckobj, speedaim), fvalue);
		}
		if (strcmp(field, "vehicle.speed_time60") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct truckobj, speedtime60), fvalue);
		}
		if (strcmp(field, "vehicle.turn_rot60") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct truckobj, turnrot60), fvalue);
		}
		if (strcmp(field, "vehicle.rot_y") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct truckobj, roty), fvalue);
		}
		if (strcmp(field, "vehicle.next_step") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct truckobj, nextstep), ivalue);
		}
		break;
	case OBJTYPE_HELI:
		if (strcmp(field, "vehicle.ai_offset") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU16(record, (u32)offsetof(struct heliobj, aioffset), ivalue);
		}
		if (strcmp(field, "vehicle.ai_return_list") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct heliobj, aireturnlist), ivalue);
		}
		if (strcmp(field, "vehicle.rotor_y_rot") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct heliobj, rotoryrot), fvalue);
		}
		if (strcmp(field, "vehicle.rotor_y_speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct heliobj, rotoryspeed), fvalue);
		}
		if (strcmp(field, "vehicle.rotor_y_speed_aim") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct heliobj, rotoryspeedaim), fvalue);
		}
		if (strcmp(field, "vehicle.rotor_y_speed_time") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct heliobj, rotoryspeedtime), fvalue);
		}
		if (strcmp(field, "vehicle.speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct heliobj, speed), fvalue);
		}
		if (strcmp(field, "vehicle.speed_aim") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct heliobj, speedaim), fvalue);
		}
		if (strcmp(field, "vehicle.speed_time60") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct heliobj, speedtime60), fvalue);
		}
		if (strcmp(field, "vehicle.rot_y") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct heliobj, yrot), fvalue);
		}
		if (strcmp(field, "vehicle.next_step") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct heliobj, nextstep), ivalue);
		}
		break;
	case OBJTYPE_HOVERCAR:
	case OBJTYPE_CHOPPER: {
		u32 common_base = 0;

		if (record->type == OBJTYPE_CHOPPER) {
			common_base = (u32)offsetof(struct chopperobj, base);
		} else {
			common_base = (u32)offsetof(struct hovercarobj, base);
		}

		(void)common_base;
		if (strcmp(field, "vehicle.ai_offset") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteU16(record, (u32)offsetof(struct chopperobj, aioffset), ivalue)
				: s_setupWriteU16(record, (u32)offsetof(struct hovercarobj, aioffset), ivalue);
		}
		if (strcmp(field, "vehicle.ai_return_list") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteS16(record, (u32)offsetof(struct chopperobj, aireturnlist), ivalue)
				: s_setupWriteS16(record, (u32)offsetof(struct hovercarobj, aireturnlist), ivalue);
		}
		if (strcmp(field, "vehicle.speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteF32(record, (u32)offsetof(struct chopperobj, speed), fvalue)
				: s_setupWriteF32(record, (u32)offsetof(struct hovercarobj, speed), fvalue);
		}
		if (strcmp(field, "vehicle.speed_aim") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteF32(record, (u32)offsetof(struct chopperobj, speedaim), fvalue)
				: s_setupWriteF32(record, (u32)offsetof(struct hovercarobj, speedaim), fvalue);
		}
		if (strcmp(field, "vehicle.speed_time60") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteF32(record, (u32)offsetof(struct chopperobj, speedtime60), fvalue)
				: s_setupWriteF32(record, (u32)offsetof(struct hovercarobj, speedtime60), fvalue);
		}
		if (strcmp(field, "vehicle.turn_y_speed60") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteF32(record, (u32)offsetof(struct chopperobj, turnyspeed60), fvalue)
				: s_setupWriteF32(record, (u32)offsetof(struct hovercarobj, turnyspeed60), fvalue);
		}
		if (strcmp(field, "vehicle.turn_x_speed60") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteF32(record, (u32)offsetof(struct chopperobj, turnxspeed60), fvalue)
				: s_setupWriteF32(record, (u32)offsetof(struct hovercarobj, turnxspeed60), fvalue);
		}
		if (strcmp(field, "vehicle.turn_rot60") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteF32(record, (u32)offsetof(struct chopperobj, turnrot60), fvalue)
				: s_setupWriteF32(record, (u32)offsetof(struct hovercarobj, turnrot60), fvalue);
		}
		if (strcmp(field, "vehicle.rot_y") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteF32(record, (u32)offsetof(struct chopperobj, roty), fvalue)
				: s_setupWriteF32(record, (u32)offsetof(struct hovercarobj, roty), fvalue);
		}
		if (strcmp(field, "vehicle.rot_x") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteF32(record, (u32)offsetof(struct chopperobj, rotx), fvalue)
				: s_setupWriteF32(record, (u32)offsetof(struct hovercarobj, rotx), fvalue);
		}
		if (strcmp(field, "vehicle.rot_z") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteF32(record, (u32)offsetof(struct chopperobj, rotz), fvalue)
				: s_setupWriteF32(record, (u32)offsetof(struct hovercarobj, rotz), fvalue);
		}
		if (strcmp(field, "vehicle.next_step") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteS32(record, (u32)offsetof(struct chopperobj, nextstep), ivalue)
				: s_setupWriteS32(record, (u32)offsetof(struct hovercarobj, nextstep), ivalue);
		}
		if (record->type == OBJTYPE_HOVERCAR) {
			if (strcmp(field, "vehicle.status") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteS16(record, (u32)offsetof(struct hovercarobj, status), ivalue);
			}
			if (strcmp(field, "vehicle.dead") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteS16(record, (u32)offsetof(struct hovercarobj, dead), ivalue);
			}
			if (strcmp(field, "vehicle.dead_timer60") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteS16(record, (u32)offsetof(struct hovercarobj, deadtimer60), ivalue);
			}
			if (strcmp(field, "vehicle.sparks_timer60") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteS16(record, (u32)offsetof(struct hovercarobj, sparkstimer60), ivalue);
			}
		} else {
			if (strcmp(field, "chopper.weapons_armed") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteS16(record, (u32)offsetof(struct chopperobj, weaponsarmed), ivalue);
			}
			if (strcmp(field, "chopper.on_target") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteS16(record, (u32)offsetof(struct chopperobj, ontarget), ivalue);
			}
			if (strcmp(field, "chopper.target") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteS16(record, (u32)offsetof(struct chopperobj, target), ivalue);
			}
			if (strcmp(field, "chopper.attack_mode") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteU8(record, (u32)offsetof(struct chopperobj, attackmode), ivalue);
			}
			if (strcmp(field, "chopper.clockwise") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteU8(record, (u32)offsetof(struct chopperobj, cw), ivalue);
			}
			if (strcmp(field, "chopper.vx") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, vx), fvalue);
			}
			if (strcmp(field, "chopper.vy") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, vy), fvalue);
			}
			if (strcmp(field, "chopper.vz") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, vz), fvalue);
			}
			if (strcmp(field, "chopper.power") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, power), fvalue);
			}
			if (strcmp(field, "chopper.origin_target_x") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, otx), fvalue);
			}
			if (strcmp(field, "chopper.origin_target_y") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, oty), fvalue);
			}
			if (strcmp(field, "chopper.origin_target_z") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, otz), fvalue);
			}
			if (strcmp(field, "chopper.bob") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, bob), fvalue);
			}
			if (strcmp(field, "chopper.bob_strength") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, bobstrength), fvalue);
			}
			if (strcmp(field, "chopper.target_visible") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteU8(record, (u32)offsetof(struct chopperobj, targetvisible), ivalue);
			}
			if (strcmp(field, "chopper.timer60") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteS32(record, (u32)offsetof(struct chopperobj, timer60), ivalue);
			}
			if (strcmp(field, "chopper.patrol_timer60") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteS32(record, (u32)offsetof(struct chopperobj, patroltimer60), ivalue);
			}
			if (strcmp(field, "chopper.gun_turn_y_speed60") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, gunturnyspeed60), fvalue);
			}
			if (strcmp(field, "chopper.gun_turn_x_speed60") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, gunturnxspeed60), fvalue);
			}
			if (strcmp(field, "chopper.gun_rot_y") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, gunroty), fvalue);
			}
			if (strcmp(field, "chopper.gun_rot_x") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, gunrotx), fvalue);
			}
			if (strcmp(field, "chopper.barrel_rot_speed") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, barrelrotspeed), fvalue);
			}
			if (strcmp(field, "chopper.barrel_rot") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, barrelrot), fvalue);
			}
			if (strcmp(field, "chopper.dead") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteU8(record, (u32)offsetof(struct chopperobj, dead), ivalue);
			}
		}
		break;
	}
	case OBJTYPE_GLASS:
		if (strcmp(field, "glass.portal") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct glassobj, portalnum), ivalue);
		}
		break;
	case OBJTYPE_PADLOCKEDDOOR:
		if (strcmp(field, "padlocked_door.door") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct padlockeddoorobj, door),
				ref_record_id);
		}
		if (strcmp(field, "padlocked_door.lock") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct padlockeddoorobj, lock),
				ref_record_id);
		}
		break;
	case OBJTYPE_SAFEITEM:
		if (strcmp(field, "safe_item.item") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct safeitemobj, item),
				ref_record_id);
		}
		if (strcmp(field, "safe_item.safe") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct safeitemobj, safe),
				ref_record_id);
		}
		if (strcmp(field, "safe_item.door") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct safeitemobj, door),
				ref_record_id);
		}
		break;
	case OBJTYPE_TINTEDGLASS:
		if (strcmp(field, "tinted_glass.xlu_distance") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct tintedglassobj, xludist), ivalue);
		}
		if ((strcmp(field, "tinted_glass.opaque_distance") == 0 ||
				strcmp(field, "tinted_glass.opa_distance") == 0) &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct tintedglassobj, opadist), ivalue);
		}
		if (strcmp(field, "tinted_glass.opacity") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct tintedglassobj, opacity), ivalue);
		}
		if (strcmp(field, "tinted_glass.portal") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct tintedglassobj, portalnum), ivalue);
		}
		if (strcmp(field, "tinted_glass.unknown_64") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct tintedglassobj, unk64), fvalue);
		}
		break;
	case OBJTYPE_LIFT: {
		u32 index;
		char member[32];

		if (sscanf(field, "lift.stops[%u].%31s", &index, member) == 2
				&& index < ARRAYCOUNT(((struct liftobj *)0)->pads)) {
			if (strcmp(member, "pad") == 0) {
				ivalue = s_parseIndexedRef(value);
				return s_setupWriteS16(record,
					(u32)offsetof(struct liftobj, pads) + index * (u32)sizeof(s16),
					ivalue);
			}
			if (strcmp(member, "door") == 0 &&
					type && strcmp(type, "record_ref") == 0) {
				return s_setupWriteRecordRefS32(record,
					(u32)offsetof(struct liftobj, doors)
						+ index * (u32)sizeof(struct doorobj *),
					ref_record_id);
			}
		}
		if (strcmp(field, "lift.distance") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct liftobj, dist), fvalue);
		}
		if (strcmp(field, "lift.speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct liftobj, speed), fvalue);
		}
		if (strcmp(field, "lift.accel") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct liftobj, accel), fvalue);
		}
		if (strcmp(field, "lift.max_speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct liftobj, maxspeed), fvalue);
		}
		if (strcmp(field, "lift.sound") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS8(record, (u32)offsetof(struct liftobj, soundtype), ivalue);
		}
		if (strcmp(field, "lift.current_level") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS8(record, (u32)offsetof(struct liftobj, levelcur), ivalue);
		}
		if (strcmp(field, "lift.target_level") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS8(record, (u32)offsetof(struct liftobj, levelaim), ivalue);
		}
		applied = s_setupApplyCoordField(record,
			(u32)offsetof(struct liftobj, prevpos),
			"lift.previous_position", field, value);
		if (applied >= 0) {
			return applied;
		}
		break;
	}
	case OBJTYPE_CONDITIONALSCENERY:
		if (strcmp(field, "conditional_scenery.trigger") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct linksceneryobj, trigger),
				ref_record_id);
		}
		if (strcmp(field, "conditional_scenery.unexploded") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct linksceneryobj, unexp),
				ref_record_id);
		}
		if (strcmp(field, "conditional_scenery.exploded") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct linksceneryobj, exp),
				ref_record_id);
		}
		break;
	case OBJTYPE_BLOCKEDPATH:
		if (strcmp(field, "blocked_path.blocker") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct blockedpathobj, blocker),
				ref_record_id);
		}
		if (strcmp(field, "blocked_path.waypoint_1") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct blockedpathobj, waypoint1), ivalue);
		}
		if (strcmp(field, "blocked_path.waypoint_2") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct blockedpathobj, waypoint2), ivalue);
		}
		break;
	case OBJTYPE_CAMERAPOS:
		if (strcmp(field, "camera_position.x") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cameraposobj, x), fvalue);
		}
		if (strcmp(field, "camera_position.y") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cameraposobj, y), fvalue);
		}
		if (strcmp(field, "camera_position.z") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cameraposobj, z), fvalue);
		}
		if (strcmp(field, "camera_position.theta") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cameraposobj, theta), fvalue);
		}
		if (strcmp(field, "camera_position.vertical_angle") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cameraposobj, verta), fvalue);
		}
		if (strcmp(field, "camera_position.pad") == 0) {
			ivalue = s_parseIndexedRef(value);
			return s_setupWriteS32(record, (u32)offsetof(struct cameraposobj, pad), ivalue);
		}
		break;
	case OBJTYPE_HOVERBIKE:
		applied = s_setupApplyHoverField(record,
			(u32)offsetof(struct hoverbikeobj, hov),
			"hover", field, value);
		if (applied >= 0) {
			return applied;
		}
		applied = s_setupApplyF32ArrayField(record,
			(u32)offsetof(struct hoverbikeobj, speed),
			ARRAYCOUNT(((struct hoverbikeobj *)0)->speed),
			"hoverbike.speed", field, value);
		if (applied >= 0) {
			return applied;
		}
		applied = s_setupApplyF32ArrayField(record,
			(u32)offsetof(struct hoverbikeobj, prevpos),
			ARRAYCOUNT(((struct hoverbikeobj *)0)->prevpos),
			"hoverbike.previous_position", field, value);
		if (applied >= 0) {
			return applied;
		}
		if (strcmp(field, "hoverbike.ex_real") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct hoverbikeobj, exreal), fvalue);
		}
		if (strcmp(field, "hoverbike.ez_real") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct hoverbikeobj, ezreal), fvalue);
		}
		if (strcmp(field, "hoverbike.ez_real2") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct hoverbikeobj, ezreal2), fvalue);
		}
		if (strcmp(field, "hoverbike.lean_speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct hoverbikeobj, leanspeed), fvalue);
		}
		if (strcmp(field, "hoverbike.lean_diff") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct hoverbikeobj, leandiff), fvalue);
		}
		if (strcmp(field, "hoverbike.max_speed_time240") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct hoverbikeobj, maxspeedtime240), ivalue);
		}
		applied = s_setupApplyF32ArrayField(record,
			(u32)offsetof(struct hoverbikeobj, rels),
			ARRAYCOUNT(((struct hoverbikeobj *)0)->rels),
			"hoverbike.relative", field, value);
		if (applied >= 0) {
			return applied;
		}
		applied = s_setupApplyF32ArrayField(record,
			(u32)offsetof(struct hoverbikeobj, speedabs),
			ARRAYCOUNT(((struct hoverbikeobj *)0)->speedabs),
			"hoverbike.absolute_speed", field, value);
		if (applied >= 0) {
			return applied;
		}
		applied = s_setupApplyF32ArrayField(record,
			(u32)offsetof(struct hoverbikeobj, speedrel),
			ARRAYCOUNT(((struct hoverbikeobj *)0)->speedrel),
			"hoverbike.relative_speed", field, value);
		if (applied >= 0) {
			return applied;
		}
		break;
	case OBJTYPE_HOVERPROP:
		applied = s_setupApplyHoverField(record,
			(u32)offsetof(struct hoverpropobj, hov),
			"hover", field, value);
		if (applied >= 0) {
			return applied;
		}
		break;
	case OBJTYPE_FAN:
		if (strcmp(field, "fan.y_rot") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct fanobj, yrot), fvalue);
		}
		if (strcmp(field, "fan.previous_y_rot") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct fanobj, yrotprev), fvalue);
		}
		if (strcmp(field, "fan.y_max_speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct fanobj, ymaxspeed), fvalue);
		}
		if (strcmp(field, "fan.y_speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct fanobj, yspeed), fvalue);
		}
		if (strcmp(field, "fan.y_accel") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct fanobj, yaccel), fvalue);
		}
		if (strcmp(field, "fan.on") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS8(record, (u32)offsetof(struct fanobj, on), ivalue);
		}
		break;
	case OBJTYPE_PADEFFECT:
		if (strcmp(field, "pad_effect.effect") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct padeffectobj, effect), ivalue);
		}
		if (strcmp(field, "pad_effect.pad") == 0) {
			ivalue = s_parseIndexedRef(value);
			return s_setupWriteS32(record, (u32)offsetof(struct padeffectobj, pad), ivalue);
		}
		break;
	case OBJTYPE_ESCASTEP:
		if (strcmp(field, "escalator_step.frame") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct escalatorobj, frame), ivalue);
		}
		applied = s_setupApplyCoordField(record,
			(u32)offsetof(struct escalatorobj, prevpos),
			"escalator_step.previous_position", field, value);
		if (applied >= 0) {
			return applied;
		}
		break;
	default:
		break;
	}

	if (type && strcmp(type, "record_ref") == 0 && ref_record_id
			&& ref_record_id[0]) {
		s_setupTableSetError(table,
			"setup record reference field '%s' is not compiled yet",
			field, "");
		return 0;
	}

	return 0;
}

static s32 s_setupParseFieldsTsv(char *text,
	scenario_source_setup_table_t *table)
{
	char *cursor;
	char *line;
	s32 default_order = 0;

	if (!text || !table) {
		return 0;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		char *line_cursor = line;
		char *record_id = s_nextField(&line_cursor);
		char *kind = s_nextField(&line_cursor);
		char *field = s_nextField(&line_cursor);
		char *type = s_nextField(&line_cursor);
		char *value = s_nextField(&line_cursor);
		char *catalog_id = s_nextField(&line_cursor);
		char *ref_record_id = s_nextField(&line_cursor);
		scenario_source_setup_record_t *record;

		if (!record_id || !kind || !field || !type) {
			continue;
		}
		if (strcmp(record_id, "record_id") == 0) {
			continue;
		}

		record = s_setupFindOrAddRecord(table, record_id, kind,
			default_order);
		if (!record) {
			return 0;
		}

		if (!s_setupApplyKnownField(table, record, field, type,
				value ? value : "", catalog_id ? catalog_id : "",
				ref_record_id ? ref_record_id : "")) {
			s_setupTableSetError(table,
				"unsupported setup source field '%s' on '%s'",
				field, record_id);
			return 0;
		}

		default_order++;
	}

	return table->error[0] == '\0';
}

static s32 s_setupApplyObjectsSummary(char *text,
	scenario_source_setup_table_t *table)
{
	char *cursor;
	char *line;
	s32 order = 0;

	if (!text || !table) {
		return 1;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		char *line_cursor = line;
		char *record_id = s_nextField(&line_cursor);
		char *kind = s_nextField(&line_cursor);
		char *pad_ref = s_nextField(&line_cursor);
		char *model_id = s_nextField(&line_cursor);
		char *weapon_id = s_nextField(&line_cursor);
		char *secondary_weapon_id = s_nextField(&line_cursor);
		char *body_id = s_nextField(&line_cursor);
		char *head_id = s_nextField(&line_cursor);
		char *ailist_ref = s_nextField(&line_cursor);
		char *flags = s_nextField(&line_cursor);
		char *flags2 = s_nextField(&line_cursor);
		char *flags3 = s_nextField(&line_cursor);
		scenario_source_setup_record_t *record;
		s32 ivalue;
		u32 uvalue;

		if (!record_id || !kind) {
			continue;
		}
		if (strcmp(record_id, "record_id") == 0) {
			continue;
		}

		record = s_setupFindOrAddRecord(table, record_id, kind, order);
		if (!record) {
			return 0;
		}

		if (pad_ref && pad_ref[0]) {
			ivalue = s_parseIndexedRef(pad_ref);
			if (record->type == OBJTYPE_CHR) {
				if (!s_setupWriteU16(record,
						(u32)offsetof(struct packedchr, padnum), ivalue)) {
					return 0;
				}
			} else if (s_setupObjTypeHasDefaultBase(record->type)) {
				if (!s_setupWriteS16(record,
						(u32)offsetof(struct defaultobj, pad), ivalue)) {
					return 0;
				}
			}
		}

		if (model_id && model_id[0]
				&& s_setupObjTypeHasDefaultBase(record->type)) {
			if (!s_setupResolveModelnum(model_id, &ivalue) ||
					!s_setupWriteS16(record,
						(u32)offsetof(struct defaultobj, modelnum), ivalue)) {
				s_setupTableSetError(table,
					"unknown model catalog id '%s' on '%s'",
					model_id, record_id);
				return 0;
			}
		}

		if (weapon_id && weapon_id[0]
				&& (record->type == OBJTYPE_WEAPON
					|| record->type == OBJTYPE_MINE)) {
			if (!s_setupResolveWeaponnum(weapon_id, &ivalue) ||
					!s_setupWriteU8(record,
						(u32)offsetof(struct weaponobj, weaponnum), ivalue)) {
				s_setupTableSetError(table,
					"unknown weapon catalog id '%s' on '%s'",
					weapon_id, record_id);
				return 0;
			}
		}

		if (secondary_weapon_id && secondary_weapon_id[0]
				&& (record->type == OBJTYPE_WEAPON
					|| record->type == OBJTYPE_MINE)) {
			if (!s_setupResolveWeaponnum(secondary_weapon_id, &ivalue) ||
					!s_setupWriteS8(record,
						(u32)offsetof(struct weaponobj, dualweaponnum), ivalue)) {
				s_setupTableSetError(table,
					"unknown secondary weapon catalog id '%s' on '%s'",
					secondary_weapon_id, record_id);
				return 0;
			}
		}

		if (body_id && body_id[0] && record->type == OBJTYPE_CHR) {
			if (!s_setupResolveBodyNum(body_id, &ivalue) ||
					!s_setupWriteU8(record,
						(u32)offsetof(struct packedchr, bodynum), ivalue)) {
				s_setupTableSetError(table,
					"unknown body catalog id '%s' on '%s'",
					body_id, record_id);
				return 0;
			}
		}

		if (head_id && head_id[0] && record->type == OBJTYPE_CHR) {
			if (!s_setupResolveHeadNum(head_id, &ivalue) ||
					!s_setupWriteS8(record,
						(u32)offsetof(struct packedchr, headnum), ivalue)) {
				s_setupTableSetError(table,
					"unknown head catalog id '%s' on '%s'",
					head_id, record_id);
				return 0;
			}
		}

		if (ailist_ref && ailist_ref[0] && record->type == OBJTYPE_CHR) {
			ivalue = s_parseIndexedRef(ailist_ref);
			if (!s_setupWriteU16(record,
					(u32)offsetof(struct packedchr, ailistnum), ivalue)) {
				return 0;
			}
		}

		if (flags && flags[0] && s_parseU32Value(flags, &uvalue)) {
			if (record->type == OBJTYPE_CHR) {
				if (!s_setupWriteU32(record,
						(u32)offsetof(struct packedchr, flags), uvalue)) {
					return 0;
				}
			} else if (s_setupObjTypeHasDefaultBase(record->type)) {
				if (!s_setupWriteU32(record,
						(u32)offsetof(struct defaultobj, flags), uvalue)) {
					return 0;
				}
			}
		}

		if (flags2 && flags2[0] && s_parseU32Value(flags2, &uvalue)) {
			if (record->type == OBJTYPE_CHR) {
				if (!s_setupWriteU32(record,
						(u32)offsetof(struct packedchr, flags2), uvalue)) {
					return 0;
				}
			} else if (s_setupObjTypeHasDefaultBase(record->type)) {
				if (!s_setupWriteU32(record,
						(u32)offsetof(struct defaultobj, flags2), uvalue)) {
					return 0;
				}
			}
		}

		if (flags3 && flags3[0] && s_parseU32Value(flags3, &uvalue)
				&& s_setupObjTypeHasDefaultBase(record->type)) {
			if (!s_setupWriteU32(record,
					(u32)offsetof(struct defaultobj, flags3), uvalue)) {
				return 0;
			}
		}

		order++;
	}

	return table->error[0] == '\0';
}

static int s_setupCompareRecords(const void *a, const void *b)
{
	const scenario_source_setup_record_t *ra =
		(const scenario_source_setup_record_t *)a;
	const scenario_source_setup_record_t *rb =
		(const scenario_source_setup_record_t *)b;

	if (ra->order < rb->order) {
		return -1;
	}
	if (ra->order > rb->order) {
		return 1;
	}
	return strcmp(ra->id, rb->id);
}

static u8 *s_setupBuildStageBlock(scenario_source_setup_table_t *table,
	scenario_source_spawn_table_t *spawn_table,
	scenario_source_ai_table_t *ai_table,
	scenario_source_path_table_t *path_table,
	const char *scenario_id, s32 *out_size)
{
	u32 intro_offset = (u32)sizeof(struct stagesetup);
	u32 spawn_count = spawn_table ? (u32)spawn_table->count : 0;
	u32 intro_size = spawn_count * 3u * (u32)sizeof(s32) + (u32)sizeof(s32);
	u32 props_offset = (intro_offset + intro_size + 3u) & ~3u;
	u32 props_size = (u32)sizeof(u32);
	u32 path_count = path_table ? (u32)path_table->count : 0;
	u32 path_words = 0;
	u32 paths_offset;
	u32 paths_size;
	u32 path_pads_offset;
	u32 path_pads_size;
	u32 ailists_offset;
	u32 ailists_size;
	u32 ailist_bytes_offset;
	u32 total_size;
	u8 *data;
	struct stagesetup *setup;
	u32 prop_end = PD_BE32((u32)OBJTYPE_END);
	u32 cursor;
	u32 align_mask = (u32)sizeof(uintptr_t) - 1u;

	if (!table || table->count < 0) {
		return NULL;
	}

	for (s32 i = 0; i < table->count; i++) {
		props_size += table->records[i].len;
	}
	if (path_table) {
		for (s32 i = 0; i < path_table->count; i++) {
			path_words += (u32)path_table->rows[i].pad_count + 1u;
		}
	}

	paths_offset = (props_offset + props_size + align_mask) & ~align_mask;
	paths_size = path_count ? (path_count + 1u) *
		(u32)sizeof(struct path) : 0u;
	path_pads_offset = (paths_offset + paths_size + 3u) & ~3u;
	path_pads_size = path_words * (u32)sizeof(s32);
	ailists_offset = (path_pads_offset + path_pads_size +
		align_mask) & ~align_mask;
	ailists_size = (u32)(((ai_table ? ai_table->count : 0) + 1) *
		(s32)sizeof(struct ailist));
	ailist_bytes_offset = (ailists_offset + ailists_size + 3u) & ~3u;
	total_size = ailist_bytes_offset + (ai_table ? ai_table->byte_count : 0);
	data = mempAlloc(total_size, MEMPOOL_STAGE);
	if (!data) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: setup allocation failed for '%s' bytes=%u",
			scenario_id ? scenario_id : "?", (unsigned)total_size);
		return NULL;
	}
	memset(data, 0, total_size);

	setup = (struct stagesetup *)data;
	setup->intro = (s32 *)(uintptr_t)intro_offset;
	setup->props = (u32 *)(uintptr_t)props_offset;
	setup->paths = path_count ? (struct path *)(uintptr_t)paths_offset : NULL;
	setup->ailists = (struct ailist *)(uintptr_t)ailists_offset;

	cursor = intro_offset;
	if (spawn_table) {
		for (s32 i = 0; i < spawn_table->count; i++) {
			s32 cmd[3];
			cmd[0] = INTROCMD_SPAWN;
			cmd[1] = spawn_table->rows[i].padnum;
			cmd[2] = spawn_table->rows[i].team;
			memcpy(data + cursor, cmd, sizeof(cmd));
			cursor += sizeof(cmd);
		}
	}
	{
		s32 intro_end = INTROCMD_END;
		memcpy(data + cursor, &intro_end, sizeof(intro_end));
	}
	cursor = props_offset;
	for (s32 i = 0; i < table->count; i++) {
		memcpy(data + cursor, table->records[i].bytes,
			table->records[i].len);
		cursor += table->records[i].len;
	}
	memcpy(data + cursor, &prop_end, sizeof(prop_end));
	cursor += sizeof(prop_end);

	if (path_count) {
		struct path *runtime_paths = (struct path *)(data + paths_offset);
		u32 path_cursor = path_pads_offset;
		for (s32 i = 0; i < path_table->count; i++) {
			scenario_source_path_row_t *row = &path_table->rows[i];
			s32 *runtime_pads = (s32 *)(data + path_cursor);
			runtime_paths[i].pads = (s32 *)(uintptr_t)path_cursor;
			runtime_paths[i].id = (u8)row->id;
			runtime_paths[i].flags = (u8)row->flags;
			runtime_paths[i].len = (u16)row->pad_count;
			for (s32 j = 0; j < row->pad_count; j++) {
				runtime_pads[j] = row->pads[j];
			}
			runtime_pads[row->pad_count] = -1;
			path_cursor += (u32)(row->pad_count + 1) *
				(u32)sizeof(s32);
		}
	}

	struct ailist *runtime_ailists = (struct ailist *)(data + ailists_offset);
	u32 list_cursor = ailist_bytes_offset;
	if (ai_table) {
		for (s32 i = 0; i < ai_table->count; i++) {
			runtime_ailists[i].id = ai_table->lists[i].id;
			runtime_ailists[i].list = (u8 *)(uintptr_t)list_cursor;
			for (s32 j = 0; j < ai_table->lists[i].count; j++) {
				scenario_source_ai_command_t *cmd =
					&ai_table->lists[i].commands[j];
				data[list_cursor++] = (u8)((cmd->opcode >> 8) & 0xff);
				data[list_cursor++] = (u8)(cmd->opcode & 0xff);
				if (cmd->operand_count) {
					memcpy(data + list_cursor, cmd->operands,
						cmd->operand_count);
					list_cursor += cmd->operand_count;
				}
			}
		}
	}

	if (out_size) {
		*out_size = (s32)list_cursor;
	}
	return data;
}

static const char *s_setupBehaviorLinkKindForType(u8 type)
{
	switch (type) {
	case OBJTYPE_LINKGUNS:           return "linked_guns";
	case OBJTYPE_LINKLIFTDOOR:       return "lift_door_link";
	case OBJTYPE_SAFEITEM:           return "safe_item";
	case OBJTYPE_PADLOCKEDDOOR:      return "padlocked_door";
	case OBJTYPE_CONDITIONALSCENERY: return "conditional_scenery";
	case OBJTYPE_BLOCKEDPATH:        return "blocked_path";
	default:                         return NULL;
	}
}

static s32 s_setupReadS16Field(const scenario_source_setup_record_t *record,
	u32 offset, s32 *out)
{
	s16 value;

	if (!record || !record->bytes || !out || offset > record->len ||
			sizeof(value) > record->len - offset) {
		return 0;
	}

	memcpy(&value, record->bytes + offset, sizeof(value));
	*out = value;
	return 1;
}

static s32 s_setupReadS32Field(const scenario_source_setup_record_t *record,
	u32 offset, s32 *out)
{
	s32 value;

	if (!record || !record->bytes || !out || offset > record->len ||
			sizeof(value) > record->len - offset) {
		return 0;
	}

	memcpy(&value, record->bytes + offset, sizeof(value));
	*out = value;
	return 1;
}

static s32 s_setupTargetOrderFromS16(
	const scenario_source_setup_record_t *record, u32 offset,
	s32 optional_zero, s32 *out)
{
	s32 rel;

	if (!s_setupReadS16Field(record, offset, &rel)) {
		return 0;
	}
	*out = (optional_zero && rel == 0) ? -1 : record->order + rel;
	return 1;
}

static s32 s_setupTargetOrderFromS32(
	const scenario_source_setup_record_t *record, u32 offset,
	s32 optional_zero, s32 *out)
{
	s32 rel;

	if (!s_setupReadS32Field(record, offset, &rel)) {
		return 0;
	}
	*out = (optional_zero && rel == 0) ? -1 : record->order + rel;
	return 1;
}

static void s_setupClearBehaviorLinks(void)
{
	free(s_ActiveScenarioGraphs.setup_links);
	s_ActiveScenarioGraphs.setup_links = NULL;
	s_ActiveScenarioGraphs.setup_link_count = 0;
	s_ActiveScenarioGraphs.setup_link_logged = 0;
}

static s32 s_setupFillBehaviorLink(
	const scenario_source_setup_record_t *record,
	scenario_source_setup_link_t *link)
{
	s32 value;

	if (!record || !link || !s_setupBehaviorLinkKindForType(record->type)) {
		return 0;
	}

	memset(link, 0, sizeof(*link));
	strncpy(link->record_id, record->id, sizeof(link->record_id) - 1);
	strncpy(link->kind, record->kind, sizeof(link->kind) - 1);
	link->type = record->type;
	link->order = record->order;
	link->target[0] = link->target[1] = link->target[2] = -1;
	link->aux[0] = link->aux[1] = -1;

	switch (record->type) {
	case OBJTYPE_LINKGUNS:
		return s_setupTargetOrderFromS16(record,
				(u32)offsetof(struct linkgunsobj, offset1),
				0, &link->target[0]) &&
			s_setupTargetOrderFromS16(record,
				(u32)offsetof(struct linkgunsobj, offset2),
				0, &link->target[1]);
	case OBJTYPE_LINKLIFTDOOR:
		if (!s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct linkliftdoorobj, door),
				0, &link->target[0]) ||
				!s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct linkliftdoorobj, lift),
				0, &link->target[1])) {
			return 0;
		}
		if (s_setupReadS32Field(record,
				(u32)offsetof(struct linkliftdoorobj, stopnum),
				&value)) {
			link->aux[0] = value;
		}
		return 1;
	case OBJTYPE_SAFEITEM:
		return s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct safeitemobj, item),
				0, &link->target[0]) &&
			s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct safeitemobj, safe),
				0, &link->target[1]) &&
			s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct safeitemobj, door),
				0, &link->target[2]);
	case OBJTYPE_PADLOCKEDDOOR:
		return s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct padlockeddoorobj, door),
				0, &link->target[0]) &&
			s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct padlockeddoorobj, lock),
				0, &link->target[1]);
	case OBJTYPE_CONDITIONALSCENERY:
		return s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct linksceneryobj, trigger),
				0, &link->target[0]) &&
			s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct linksceneryobj, unexp),
				1, &link->target[1]) &&
			s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct linksceneryobj, exp),
				1, &link->target[2]);
	case OBJTYPE_BLOCKEDPATH:
		if (!s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct blockedpathobj, blocker),
				0, &link->target[0])) {
			return 0;
		}
		if (s_setupReadS16Field(record,
				(u32)offsetof(struct blockedpathobj, waypoint1),
				&value)) {
			link->aux[0] = value;
		}
		if (s_setupReadS16Field(record,
				(u32)offsetof(struct blockedpathobj, waypoint2),
				&value)) {
			link->aux[1] = value;
		}
		return 1;
	default:
		return 0;
	}
}

static s32 s_setupCollectBehaviorLinkSource(
	const asset_entry_t *scenario, const char *source_path,
	const scenario_source_setup_table_t *table)
{
	s32 count = 0;
	s32 index = 0;
	char active_path[FS_MAXPATH + 1];

	if (!scenario || !table || !s_activeGraphPathForScenario(scenario,
			s_ActiveScenarioGraphs.setup_fields_path,
			active_path, sizeof(active_path))) {
		return 1;
	}

	s_setupClearBehaviorLinks();
	for (s32 i = 0; i < table->count; i++) {
		if (s_setupBehaviorLinkKindForType(table->records[i].type)) {
			count++;
		}
	}
	if (count <= 0) {
		return 1;
	}

	s_ActiveScenarioGraphs.setup_links =
		(scenario_source_setup_link_t *)calloc((size_t)count,
			sizeof(*s_ActiveScenarioGraphs.setup_links));
	if (!s_ActiveScenarioGraphs.setup_links) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.GRAPH: failed to allocate setup behavior link source for '%s' links=%d",
			scenario->id, count);
		return 0;
	}

	for (s32 i = 0; i < table->count; i++) {
		const scenario_source_setup_record_t *record = &table->records[i];
		if (!s_setupBehaviorLinkKindForType(record->type)) {
			continue;
		}
		if (!s_setupFillBehaviorLink(record,
				&s_ActiveScenarioGraphs.setup_links[index])) {
			sysLogPrintf(LOG_ERROR,
				"SCENARIO.GRAPH: invalid setup behavior link source '%s' record=%s kind=%s",
				source_path ? source_path : active_path,
				record->id, record->kind);
			s_setupClearBehaviorLinks();
			return 0;
		}
		index++;
	}

	s_ActiveScenarioGraphs.setup_link_count = count;
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: setup behavior link source '%s' links=%d backend=graph.setup.links+setup.fields.tsv",
		source_path && source_path[0] ? source_path : active_path,
		count);
	return 1;
}

static s32 s_parseSegmentToken(const char *value, const char *prefix,
	u32 *out)
{
	s32 id;
	u32 flags;

	if (!value || !out) {
		return 0;
	}

	id = s_parseIndexedRef(value);
	if (id < 0 || id > (s32)SCENARIO_SEGMENT_ID_MASK) {
		return 0;
	}

	if (prefix && prefix[0] && !s_startsWith(value, prefix)) {
		return 0;
	}

	flags = 0;
	if (strstr(value, "outward")) {
		flags |= SCENARIO_SEGMENT_FLAG_OUTWARD;
	}
	if (strstr(value, "inward")) {
		flags |= SCENARIO_SEGMENT_FLAG_INWARD;
	}

	*out = ((u32)id & SCENARIO_SEGMENT_ID_MASK) | flags;
	return 1;
}

static void s_freeSegmentList(scenario_source_segment_list_t *list)
{
	if (!list) {
		return;
	}
	if (list->values) {
		free(list->values);
	}
	list->values = NULL;
	list->count = 0;
}

static s32 s_parseSegmentList(char *field, const char *prefix,
	scenario_source_segment_list_t *out)
{
	s32 capacity;
	char *cursor;
	char *token;

	if (!out) {
		return 0;
	}
	out->values = NULL;
	out->count = 0;

	if (!field || !field[0]) {
		return 1;
	}

	capacity = 8;
	out->values = malloc((size_t)capacity * sizeof(*out->values));
	if (!out->values) {
		return 0;
	}

	cursor = field;
	while (cursor && *cursor) {
		u32 value;
		char *end = cursor;

		while (*end && *end != ';' && *end != ',') {
			end++;
		}
		if (*end) {
			*end++ = '\0';
		}
		token = cursor;
		while (*token && isspace((unsigned char)*token)) {
			token++;
		}
		cursor = end;

		if (!token[0]) {
			continue;
		}
		if (!s_parseSegmentToken(token, prefix, &value)) {
			s_freeSegmentList(out);
			return 0;
		}
		if (out->count >= capacity) {
			u32 *grown;
			capacity *= 2;
			grown = realloc(out->values,
				(size_t)capacity * sizeof(*out->values));
			if (!grown) {
				s_freeSegmentList(out);
				return 0;
			}
			out->values = grown;
		}
		out->values[out->count++] = value;
	}

	return 1;
}

static void s_pathTableSetError(scenario_source_path_table_t *table,
	const char *fmt, const char *a)
{
	if (!table || table->error[0]) {
		return;
	}
	snprintf(table->error, sizeof(table->error), fmt,
		a ? a : "");
}

static void s_freePathTable(scenario_source_path_table_t *table)
{
	if (!table) {
		return;
	}
	if (table->rows) {
		for (s32 i = 0; i < table->count; i++) {
			free(table->rows[i].pads);
		}
		free(table->rows);
	}
	memset(table, 0, sizeof(*table));
}

static s32 s_parsePathPadList(char *field, s32 **out_pads,
	s32 *out_count)
{
	s32 capacity;
	s32 count;
	s32 *pads;
	char *cursor;

	if (!out_pads || !out_count) {
		return 0;
	}
	*out_pads = NULL;
	*out_count = 0;

	if (!field || !field[0]) {
		return 0;
	}

	capacity = 8;
	count = 0;
	pads = (s32 *)malloc((size_t)capacity * sizeof(*pads));
	if (!pads) {
		return 0;
	}

	cursor = field;
	while (cursor && *cursor) {
		char *token = cursor;
		char *end = cursor;
		s32 padnum;

		while (*end && *end != ';' && *end != ',') {
			end++;
		}
		if (*end) {
			*end++ = '\0';
		}
		cursor = end;

		while (*token && isspace((unsigned char)*token)) {
			token++;
		}
		if (!token[0]) {
			continue;
		}
		padnum = s_parseIndexedRef(token);
		if (padnum < 0 || !s_startsWith(token, "pad_")) {
			free(pads);
			return 0;
		}
		if (count >= capacity) {
			s32 *grown;
			capacity *= 2;
			grown = (s32 *)realloc(pads,
				(size_t)capacity * sizeof(*pads));
			if (!grown) {
				free(pads);
				return 0;
			}
			pads = grown;
		}
		pads[count++] = padnum;
	}

	if (count <= 0) {
		free(pads);
		return 0;
	}

	*out_pads = pads;
	*out_count = count;
	return 1;
}

static s32 s_pathAppendRow(scenario_source_path_table_t *table,
	scenario_source_path_row_t *row)
{
	scenario_source_path_row_t *rows;
	s32 new_capacity;

	if (!table || !row || row->id < 0 || row->id > 0xff ||
			row->flags > 0xffu || !row->pads || row->pad_count <= 0) {
		return 0;
	}

	if (table->count >= table->capacity) {
		new_capacity = table->capacity ? table->capacity * 2 : 16;
		rows = (scenario_source_path_row_t *)realloc(table->rows,
			(size_t)new_capacity * sizeof(*rows));
		if (!rows) {
			s_pathTableSetError(table,
				"out of memory adding path '%s'", "");
			return 0;
		}
		table->rows = rows;
		table->capacity = new_capacity;
	}
	table->rows[table->count++] = *row;
	memset(row, 0, sizeof(*row));
	return 1;
}

static s32 s_loadPathSourceRows(char *text,
	scenario_source_path_table_t *table)
{
	char *cursor;
	char *line;

	if (!text || !table) {
		return 0;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		char *line_cursor = line;
		char *path_ref = s_nextField(&line_cursor);
		char *flags = s_nextField(&line_cursor);
		char *pads = s_nextField(&line_cursor);
		u32 parsed_flags;
		scenario_source_path_row_t row;

		if (!path_ref || !flags || !pads) {
			continue;
		}
		if (strcmp(path_ref, "path_ref") == 0) {
			continue;
		}

		memset(&row, 0, sizeof(row));
		row.id = s_parseIndexedRef(path_ref);
		if (row.id < 0 || row.id > 0xff ||
				!s_startsWith(path_ref, "path_") ||
				!s_parseU32Value(flags, &parsed_flags) ||
				parsed_flags > 0xffu ||
				!s_parsePathPadList(pads, &row.pads,
				&row.pad_count)) {
			free(row.pads);
			s_pathTableSetError(table, "bad path row '%s'", path_ref);
			return 0;
		}
		row.flags = parsed_flags;

		if (!s_pathAppendRow(table, &row)) {
			free(row.pads);
			return 0;
		}
	}

	return table->error[0] == '\0';
}

static s32 s_parsePadRow(char *line, scenario_source_pad_row_t *out)
{
	char *cursor;
	char *field;
	s32 i;

	if (!line || !out || !line[0] || s_startsWith(line, "pad_id")) {
		return 0;
	}

	memset(out, 0, sizeof(*out));
	out->room = -1;

	cursor = line;
	field = s_nextField(&cursor);
	if (!field || !field[0]) {
		return 0;
	}

	field = s_nextField(&cursor);
	out->room = s_parseRoomRef(field);

	field = s_nextField(&cursor);
	out->liftnum = field ? (s32)strtol(field, NULL, 0) : 0;

	field = s_nextField(&cursor);
	out->flags = field ? (u32)strtoul(field, NULL, 0) : 0;

	for (i = 0; i < 3; i++) {
		field = s_nextField(&cursor);
		if (!s_parseFloat(field, &out->pos[i])) return 0;
	}
	for (i = 0; i < 3; i++) {
		field = s_nextField(&cursor);
		if (!s_parseFloat(field, &out->up[i])) return 0;
	}
	for (i = 0; i < 3; i++) {
		field = s_nextField(&cursor);
		if (!s_parseFloat(field, &out->look[i])) return 0;
	}
	for (i = 0; i < 6; i++) {
		field = s_nextField(&cursor);
		if (!s_parseFloat(field, &out->bbox[i])) return 0;
	}

	return 1;
}

static u32 s_scenarioPadFlags(u32 flags)
{
	flags &= 0x3ffff;
	flags &= ~(u32)(PADFLAG_INTPOS
		| PADFLAG_UPALIGNTOX | PADFLAG_UPALIGNTOY | PADFLAG_UPALIGNTOZ
		| PADFLAG_UPALIGNINVERT
		| PADFLAG_LOOKALIGNTOX | PADFLAG_LOOKALIGNTOY | PADFLAG_LOOKALIGNTOZ
		| PADFLAG_LOOKALIGNINVERT);
	flags |= PADFLAG_HASBBOXDATA;
	return flags;
}

static void s_writeFloat(u8 **dst, f32 value)
{
	memcpy(*dst, &value, sizeof(value));
	*dst += sizeof(value);
}

static void s_writePadRecord(u8 *dst,
	const scenario_source_pad_row_t *row)
{
	u32 flags;
	u32 room_bits;
	u32 liftnum;
	u32 header;
	s32 i;
	u8 *p;

	flags = s_scenarioPadFlags(row->flags);
	room_bits = (u32)row->room & 0x3ff;
	liftnum = (u32)row->liftnum & 0x0f;
	header = (flags << 14) | (room_bits << 4) | liftnum;

	p = dst;
	memcpy(p, &header, sizeof(header));
	p += sizeof(header);

	for (i = 0; i < 3; i++) s_writeFloat(&p, row->pos[i]);
	for (i = 0; i < 3; i++) s_writeFloat(&p, row->up[i]);
	for (i = 0; i < 3; i++) s_writeFloat(&p, row->look[i]);
	for (i = 0; i < 6; i++) s_writeFloat(&p, row->bbox[i]);
}

static size_t s_alignSize(size_t value, size_t align)
{
	return (value + align - 1u) & ~(align - 1u);
}

static u8 *s_buildPadfile(const scenario_source_pad_row_t *rows,
	s32 row_count, const scenario_source_navigation_t *nav, s32 *out_size)
{
	size_t header_size;
	size_t offset_size;
	size_t record_size;
	size_t pad_records_end;
	size_t waypoint_offset;
	size_t waypoint_list_offset;
	size_t waygroup_offset;
	size_t waygroup_waypoints_offset;
	size_t waygroup_neighbours_offset;
	size_t cover_offset;
	size_t total_size;
	s32 waypoint_count;
	s32 waygroup_count;
	s32 cover_count;
	size_t waypoint_list_words;
	size_t waygroup_waypoint_words;
	size_t waygroup_neighbour_words;
	u8 *buf;
	struct padsfileheader *header;
	u16 *offsets;
	u8 *records;
	s32 i;

	if (out_size) {
		*out_size = 0;
	}
	if (!rows || row_count <= 0) {
		return NULL;
	}

	header_size = offsetof(struct padsfileheader, padoffsets);
	offset_size = (size_t)row_count * sizeof(u16);
	record_size = sizeof(u32) + (size_t)15 * sizeof(f32);
	pad_records_end = header_size + offset_size +
		(size_t)row_count * record_size;

	if (pad_records_end > 0xffff) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: pads.tsv pad records compile to %u bytes, exceeding u16 pad offsets",
			(unsigned)pad_records_end);
		return NULL;
	}

	waypoint_count = nav ? nav->waypoint_count : 0;
	waygroup_count = nav ? nav->waygroup_count : 0;
	cover_count = nav ? nav->cover_count : 0;
	waypoint_list_words = 0;
	waygroup_waypoint_words = 0;
	waygroup_neighbour_words = 0;

	if (nav) {
		for (i = 0; i < waypoint_count; i++) {
			waypoint_list_words +=
				(size_t)nav->waypoints[i].neighbours.count + 1u;
		}
		for (i = 0; i < waygroup_count; i++) {
			waygroup_waypoint_words +=
				(size_t)nav->waygroups[i].waypoints.count + 1u;
			waygroup_neighbour_words +=
				(size_t)nav->waygroups[i].neighbours.count + 1u;
		}
	}

	total_size = s_alignSize(pad_records_end, sizeof(uintptr_t));
	waypoint_offset = total_size;
	total_size += (size_t)(waypoint_count + 1) * sizeof(struct waypoint);
	waypoint_list_offset = total_size;
	total_size += waypoint_list_words * sizeof(u32);
	total_size = s_alignSize(total_size, sizeof(uintptr_t));
	waygroup_offset = total_size;
	total_size += (size_t)(waygroup_count + 1) * sizeof(struct waygroup);
	waygroup_waypoints_offset = total_size;
	total_size += waygroup_waypoint_words * sizeof(u32);
	waygroup_neighbours_offset = total_size;
	total_size += waygroup_neighbour_words * sizeof(u32);
	total_size = s_alignSize(total_size, sizeof(uintptr_t));
	cover_offset = total_size;
	total_size += (size_t)cover_count * sizeof(struct coverdefinition);

	buf = mempAlloc((u32)total_size, MEMPOOL_STAGE);
	if (!buf) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: could not allocate %u bytes for pads.tsv runtime buffer",
			(unsigned)total_size);
		return NULL;
	}
	memset(buf, 0, total_size);

	header = (struct padsfileheader *)buf;
	header->numpads = row_count;
	header->numcovers = cover_count;
	header->waypointsoffset = (uintptr_t)waypoint_offset;
	header->waygroupsoffset = (uintptr_t)waygroup_offset;
	header->coversoffset = cover_count > 0 ? (uintptr_t)cover_offset : 0;

	offsets = (u16 *)(buf + header_size);
	records = buf + header_size + offset_size;

	for (i = 0; i < row_count; i++) {
		offsets[i] = (u16)(records - buf);
		s_writePadRecord(records, &rows[i]);
		records += record_size;
	}

	{
		struct waypoint *waypoints = (struct waypoint *)(buf + waypoint_offset);
		u32 *neighbours = (u32 *)(buf + waypoint_list_offset);
		size_t list_offset = waypoint_list_offset;

		for (i = 0; i < waypoint_count; i++) {
			s32 j;
			waypoints[i].padnum = nav->waypoints[i].padnum;
			waypoints[i].neighbours = (s32 *)(uintptr_t)list_offset;
			waypoints[i].groupnum = nav->waypoints[i].groupnum;
			waypoints[i].step = nav->waypoints[i].step;
			for (j = 0; j < nav->waypoints[i].neighbours.count; j++) {
				*neighbours++ = nav->waypoints[i].neighbours.values[j];
				list_offset += sizeof(u32);
			}
			*neighbours++ = 0xffffffffu;
			list_offset += sizeof(u32);
		}
		waypoints[waypoint_count].padnum = -1;
		waypoints[waypoint_count].neighbours = NULL;
		waypoints[waypoint_count].groupnum = 0;
		waypoints[waypoint_count].step = 0;
	}

	{
		struct waygroup *waygroups = (struct waygroup *)(buf + waygroup_offset);
		u32 *group_waypoints = (u32 *)(buf + waygroup_waypoints_offset);
		u32 *group_neighbours = (u32 *)(buf + waygroup_neighbours_offset);
		size_t group_waypoints_list_offset = waygroup_waypoints_offset;
		size_t group_neighbours_list_offset = waygroup_neighbours_offset;

		for (i = 0; i < waygroup_count; i++) {
			s32 j;
			waygroups[i].waypoints = (s32 *)(uintptr_t)group_waypoints_list_offset;
			waygroups[i].neighbours = (s32 *)(uintptr_t)group_neighbours_list_offset;
			waygroups[i].step = nav->waygroups[i].step;
			for (j = 0; j < nav->waygroups[i].waypoints.count; j++) {
				*group_waypoints++ = nav->waygroups[i].waypoints.values[j];
				group_waypoints_list_offset += sizeof(u32);
			}
			*group_waypoints++ = 0xffffffffu;
			group_waypoints_list_offset += sizeof(u32);
			for (j = 0; j < nav->waygroups[i].neighbours.count; j++) {
				*group_neighbours++ = nav->waygroups[i].neighbours.values[j];
				group_neighbours_list_offset += sizeof(u32);
			}
			*group_neighbours++ = 0xffffffffu;
			group_neighbours_list_offset += sizeof(u32);
		}
		waygroups[waygroup_count].neighbours = NULL;
		waygroups[waygroup_count].waypoints = NULL;
		waygroups[waygroup_count].step = 0;
	}

	if (cover_count > 0) {
		struct coverdefinition *covers =
			(struct coverdefinition *)(buf + cover_offset);
		for (i = 0; i < cover_count; i++) {
			covers[i].pos.x = nav->covers[i].pos[0];
			covers[i].pos.y = nav->covers[i].pos[1];
			covers[i].pos.z = nav->covers[i].pos[2];
			covers[i].look.x = nav->covers[i].look[0];
			covers[i].look.y = nav->covers[i].look[1];
			covers[i].look.z = nav->covers[i].look[2];
			covers[i].flags = (u16)(nav->covers[i].flags & 0xffffu);
		}
	}

	if (out_size) {
		*out_size = (s32)total_size;
	}
	return buf;
}

static scenario_source_pad_row_t *s_parsePadsTsv(char *text,
	s32 *out_count)
{
	scenario_source_pad_row_t *rows;
	s32 capacity;
	s32 count;
	char *cursor;
	char *line;

	if (out_count) {
		*out_count = 0;
	}
	if (!text) {
		return NULL;
	}

	capacity = 64;
	count = 0;
	rows = malloc((size_t)capacity * sizeof(*rows));
	if (!rows) {
		return NULL;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		scenario_source_pad_row_t row;
		if (!s_parsePadRow(line, &row)) {
			continue;
		}
		if (count >= capacity) {
			scenario_source_pad_row_t *grown;
			capacity *= 2;
			grown = realloc(rows, (size_t)capacity * sizeof(*rows));
			if (!grown) {
				free(rows);
				return NULL;
			}
			rows = grown;
		}
		rows[count++] = row;
	}

	if (out_count) {
		*out_count = count;
	}
	return rows;
}

static s32 s_parseWaypointRow(char *line, scenario_source_waypoint_row_t *out)
{
	char *cursor;
	char *field;

	if (!line || !out || !line[0] || s_startsWith(line, "waypoint_id")) {
		return 0;
	}

	memset(out, 0, sizeof(*out));
	out->groupnum = -1;

	cursor = line;
	field = s_nextField(&cursor);
	if (!field || !field[0]) {
		return 0;
	}

	field = s_nextField(&cursor);
	out->padnum = s_parseIndexedRef(field);
	if (out->padnum < 0) {
		return -1;
	}

	field = s_nextField(&cursor);
	out->groupnum = s_parseIndexedRef(field);

	field = s_nextField(&cursor);
	out->step = field && field[0] ? (s32)strtol(field, NULL, 0) : 0;

	field = s_nextField(&cursor);
	if (!s_parseSegmentList(field, "waypoint_", &out->neighbours)) {
		return -1;
	}

	return 1;
}

static s32 s_parseWaygroupRow(char *line, scenario_source_waygroup_row_t *out)
{
	char *cursor;
	char *field;

	if (!line || !out || !line[0] || s_startsWith(line, "waygroup_id")) {
		return 0;
	}

	memset(out, 0, sizeof(*out));
	cursor = line;
	field = s_nextField(&cursor);
	if (!field || !field[0]) {
		return 0;
	}

	field = s_nextField(&cursor);
	out->step = field && field[0] ? (s32)strtol(field, NULL, 0) : 0;

	field = s_nextField(&cursor);
	if (!s_parseSegmentList(field, "waypoint_", &out->waypoints)) {
		return -1;
	}

	field = s_nextField(&cursor);
	if (!s_parseSegmentList(field, "waygroup_", &out->neighbours)) {
		s_freeSegmentList(&out->waypoints);
		return -1;
	}

	return 1;
}

static s32 s_parseCoverRow(char *line, scenario_source_cover_row_t *out)
{
	char *cursor;
	char *field;
	s32 i;

	if (!line || !out || !line[0] || s_startsWith(line, "cover_id")) {
		return 0;
	}

	memset(out, 0, sizeof(*out));
	cursor = line;
	field = s_nextField(&cursor);
	if (!field || !field[0]) {
		return 0;
	}

	field = s_nextField(&cursor);
	out->flags = field && field[0] ? (u32)strtoul(field, NULL, 0) : 0;

	for (i = 0; i < 3; i++) {
		field = s_nextField(&cursor);
		if (!s_parseFloat(field, &out->pos[i])) return -1;
	}
	for (i = 0; i < 3; i++) {
		field = s_nextField(&cursor);
		if (!s_parseFloat(field, &out->look[i])) return -1;
	}

	return 1;
}

static s32 s_parseVolumeRow(char *line, scenario_source_volume_row_t *out)
{
	char *cursor;
	char *field;
	s32 i;

	if (!line || !out || !line[0] || s_startsWith(line, "volume_id")) {
		return 0;
	}

	memset(out, 0, sizeof(*out));
	out->padnum = -1;
	out->room = -1;

	cursor = line;
	field = s_nextField(&cursor);
	if (!field || !field[0]) {
		return -1;
	}
	s_copyString(out->id, sizeof(out->id), field);

	field = s_nextField(&cursor);
	if (field && field[0]) {
		out->padnum = s_parseIndexedRef(field);
	}

	field = s_nextField(&cursor);
	if (!field || !field[0]) {
		return -1;
	}
	s_copyString(out->kind, sizeof(out->kind), field);

	field = s_nextField(&cursor);
	if (field && field[0]) {
		out->room = s_parseRoomRef(field);
	}

	field = s_nextField(&cursor);
	if (!field || strcmp(field, "aabb") != 0) {
		return -1;
	}
	s_copyString(out->shape, sizeof(out->shape), field);

	for (i = 0; i < 3; i++) {
		field = s_nextField(&cursor);
		if (!s_parseFloat(field, &out->min[i])) return -1;
	}
	for (i = 0; i < 3; i++) {
		field = s_nextField(&cursor);
		if (!s_parseFloat(field, &out->max[i])) return -1;
		if (out->max[i] < out->min[i]) return -1;
	}

	return 1;
}

static s32 s_loadLevelVolumeSourceRows(const char *path,
	scenario_source_volume_row_t **out_rows, s32 *out_count)
{
	scenario_source_volume_row_t *rows;
	s32 capacity;
	s32 count;
	char *text;
	char *cursor;
	char *line;

	if (out_rows) {
		*out_rows = NULL;
	}
	if (out_count) {
		*out_count = 0;
	}
	if (!path || !path[0]) {
		return 0;
	}

	text = s_loadGraphText(path, NULL);
	if (!text) {
		return 0;
	}

	capacity = 32;
	count = 0;
	rows = malloc((size_t)capacity * sizeof(*rows));
	if (!rows) {
		free(text);
		return 0;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		scenario_source_volume_row_t row;
		s32 parsed = s_parseVolumeRow(line, &row);
		if (parsed < 0) {
			free(rows);
			free(text);
			return 0;
		}
		if (parsed == 0) {
			continue;
		}
		if (count >= capacity) {
			scenario_source_volume_row_t *grown;
			capacity *= 2;
			grown = realloc(rows, (size_t)capacity * sizeof(*rows));
			if (!grown) {
				free(rows);
				free(text);
				return 0;
			}
			rows = grown;
		}
		rows[count++] = row;
	}

	free(text);
	if (count == 0) {
		free(rows);
		rows = NULL;
	}
	if (out_rows) {
		*out_rows = rows;
	}
	if (out_count) {
		*out_count = count;
	}
	return 1;
}

static void s_freeNavigation(scenario_source_navigation_t *nav)
{
	s32 i;

	if (!nav) {
		return;
	}

	if (nav->waypoints) {
		for (i = 0; i < nav->waypoint_count; i++) {
			s_freeSegmentList(&nav->waypoints[i].neighbours);
		}
		free(nav->waypoints);
	}
	if (nav->waygroups) {
		for (i = 0; i < nav->waygroup_count; i++) {
			s_freeSegmentList(&nav->waygroups[i].waypoints);
			s_freeSegmentList(&nav->waygroups[i].neighbours);
		}
		free(nav->waygroups);
	}
	if (nav->covers) {
		free(nav->covers);
	}
	memset(nav, 0, sizeof(*nav));
}

static s32 s_parseWaypointsTsv(char *text, scenario_source_navigation_t *nav)
{
	scenario_source_waypoint_row_t *rows;
	s32 capacity;
	s32 count;
	char *cursor;
	char *line;

	if (!text || !nav) {
		return 0;
	}

	capacity = 32;
	count = 0;
	rows = malloc((size_t)capacity * sizeof(*rows));
	if (!rows) {
		return 0;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		scenario_source_waypoint_row_t row;
		s32 parsed = s_parseWaypointRow(line, &row);
		if (parsed < 0) {
			s32 i;
			for (i = 0; i < count; i++) {
				s_freeSegmentList(&rows[i].neighbours);
			}
			free(rows);
			return 0;
		}
		if (parsed == 0) {
			continue;
		}
		if (count >= capacity) {
			scenario_source_waypoint_row_t *grown;
			capacity *= 2;
			grown = realloc(rows, (size_t)capacity * sizeof(*rows));
			if (!grown) {
				s32 i;
				s_freeSegmentList(&row.neighbours);
				for (i = 0; i < count; i++) {
					s_freeSegmentList(&rows[i].neighbours);
				}
				free(rows);
				return 0;
			}
			rows = grown;
		}
		rows[count++] = row;
	}

	nav->waypoints = rows;
	nav->waypoint_count = count;
	return 1;
}

static s32 s_parseWaygroupsTsv(char *text, scenario_source_navigation_t *nav)
{
	scenario_source_waygroup_row_t *rows;
	s32 capacity;
	s32 count;
	char *cursor;
	char *line;

	if (!text || !nav) {
		return 0;
	}

	capacity = 16;
	count = 0;
	rows = malloc((size_t)capacity * sizeof(*rows));
	if (!rows) {
		return 0;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		scenario_source_waygroup_row_t row;
		s32 parsed = s_parseWaygroupRow(line, &row);
		if (parsed < 0) {
			s32 i;
			for (i = 0; i < count; i++) {
				s_freeSegmentList(&rows[i].waypoints);
				s_freeSegmentList(&rows[i].neighbours);
			}
			free(rows);
			return 0;
		}
		if (parsed == 0) {
			continue;
		}
		if (count >= capacity) {
			scenario_source_waygroup_row_t *grown;
			capacity *= 2;
			grown = realloc(rows, (size_t)capacity * sizeof(*rows));
			if (!grown) {
				s32 i;
				s_freeSegmentList(&row.waypoints);
				s_freeSegmentList(&row.neighbours);
				for (i = 0; i < count; i++) {
					s_freeSegmentList(&rows[i].waypoints);
					s_freeSegmentList(&rows[i].neighbours);
				}
				free(rows);
				return 0;
			}
			rows = grown;
		}
		rows[count++] = row;
	}

	nav->waygroups = rows;
	nav->waygroup_count = count;
	return 1;
}

static s32 s_parseCoversTsv(char *text, scenario_source_navigation_t *nav)
{
	scenario_source_cover_row_t *rows;
	s32 capacity;
	s32 count;
	char *cursor;
	char *line;

	if (!text || !nav) {
		return 0;
	}

	capacity = 16;
	count = 0;
	rows = malloc((size_t)capacity * sizeof(*rows));
	if (!rows) {
		return 0;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		scenario_source_cover_row_t row;
		s32 parsed = s_parseCoverRow(line, &row);
		if (parsed < 0) {
			free(rows);
			return 0;
		}
		if (parsed == 0) {
			continue;
		}
		if (count >= capacity) {
			scenario_source_cover_row_t *grown;
			capacity *= 2;
			grown = realloc(rows, (size_t)capacity * sizeof(*rows));
			if (!grown) {
				free(rows);
				return 0;
			}
			rows = grown;
		}
		rows[count++] = row;
	}

	nav->covers = rows;
	nav->cover_count = count;
	return 1;
}

static void s_copyMemberPath(char *out, size_t out_n,
	const char *archive_member_path, const char *fallback_member)
{
	const char *sep;
	size_t archive_len;

	if (!out || out_n == 0) {
		return;
	}
	out[0] = '\0';

	if (!archive_member_path || !archive_member_path[0]
			|| !fallback_member || !fallback_member[0]) {
		return;
	}

	sep = strstr(archive_member_path, "::");
	if (!sep) {
		return;
	}

	archive_len = (size_t)(sep - archive_member_path);
	if (archive_len + 2 + strlen(fallback_member) >= out_n) {
		return;
	}

	memcpy(out, archive_member_path, archive_len);
	out[archive_len] = '\0';
	strcat(out, "::");
	strcat(out, fallback_member);
}

static void s_matchScenarioEntry(const asset_entry_t *entry, void *userdata)
{
	scenario_source_match_t *match;
	s32 mode;
	s32 category_match = 0;

	if (!entry || !userdata) {
		return;
	}

	match = (scenario_source_match_t *)userdata;
	if (!match->stage || entry->ext.scenario.stagenum != match->stage->stagenum) {
		return;
	}

	if (match->stage->entry && match->stage->entry->category[0]) {
		category_match = strncmp(entry->category,
			match->stage->entry->category, CATALOG_CATEGORY_LEN) == 0;
	}

	mode = entry->ext.scenario.mode;
	if (mode == match->desired_mode || mode == 0 || match->desired_mode == 0) {
		if (category_match && !match->category_exact) {
			match->category_exact = entry;
			return;
		}
		if (!match->exact) {
			match->exact = entry;
		}
		return;
	}

	if (category_match && !match->category_fallback) {
		match->category_fallback = entry;
		return;
	}

	if (!match->fallback) {
		match->fallback = entry;
	}
}

static const asset_entry_t *s_findScenarioByDerivedStageId(
	const catalog_stage_result_t *stage)
{
	const char *id;
	const char *colon;
	const char *slug;
	char scenario_id[CATALOG_ID_LEN];
	const asset_entry_t *candidate;
	size_t namespace_len;
	size_t slug_len;
	int written;

	if (!stage || !stage->entry || !stage->entry->id[0]) {
		return NULL;
	}

	id = stage->entry->id;
	colon = strchr(id, ':');
	if (!colon || !colon[1]) {
		return NULL;
	}

	namespace_len = (size_t)(colon - id);
	slug = colon + 1;
	slug_len = strlen(slug);
	if (namespace_len == 0 || slug_len == 0) {
		return NULL;
	}

	written = snprintf(scenario_id, sizeof(scenario_id),
		"%.*s:scenario_%s", (int)namespace_len, id, slug);
	if (written <= 0 || (size_t)written >= sizeof(scenario_id)) {
		return NULL;
	}

	candidate = assetCatalogResolve(scenario_id);
	if (candidate && candidate->type == ASSET_SCENARIO) {
		return candidate;
	}
	return NULL;
}

static const asset_entry_t *s_findScenarioForStage(
	const catalog_stage_result_t *stage, s32 prefer_mp)
{
	scenario_source_match_t match;
	s32 desired_mode;
	const asset_entry_t *derived;

	if (!stage) {
		return NULL;
	}

	desired_mode = prefer_mp ? MAP_MODE_MP : 0;
	if (!desired_mode && stage->entry && stage->entry->type == ASSET_MAP) {
		desired_mode = stage->entry->ext.map.mode;
	}

	memset(&match, 0, sizeof(match));
	match.stage = stage;
	match.desired_mode = desired_mode;

	assetCatalogIterateByType(ASSET_SCENARIO, s_matchScenarioEntry, &match);
	derived = s_findScenarioByDerivedStageId(stage);
	if (match.category_exact) {
		return match.category_exact;
	}
	if (match.exact) {
		return match.exact;
	}
	if (derived) {
		return derived;
	}
	if (match.category_fallback) {
		return match.category_fallback;
	}
	return match.fallback;
}

const asset_entry_t *scenarioSourceFindEntryForStage(
	const catalog_stage_result_t *stage, s32 prefer_mp)
{
	return s_findScenarioForStage(stage, prefer_mp);
}

static s32 s_activeGraphPathForScenario(const asset_entry_t *scenario,
	const char *path, char *out, size_t out_n)
{
	if (!scenario || !scenario->id[0] || !path || !path[0]
			|| !out || out_n == 0) {
		return 0;
	}

	if (!s_ActiveScenarioGraphs.level_graph_active
			|| strncmp(s_ActiveScenarioGraphs.scenario_id,
				scenario->id, CATALOG_ID_LEN) != 0) {
		return 0;
	}

	s_copyString(out, out_n, path);
	return out[0] != '\0';
}

static s32 s_scenarioPadsPath(const asset_entry_t *scenario,
	char *out, size_t out_n)
{
	if (!scenario || !out || out_n == 0) {
		return 0;
	}

	out[0] = '\0';
	if (s_activeGraphPathForScenario(scenario,
			s_ActiveScenarioGraphs.pads_path, out, out_n)) {
		return 1;
	}

	if (scenario->ext.scenario.pads_file[0]) {
		strncpy(out, scenario->ext.scenario.pads_file, out_n - 1);
		out[out_n - 1] = '\0';
		return out[0] != '\0';
	}

	s_copyMemberPath(out, out_n, scenario->ext.scenario.scene_file, "pads.tsv");
	return out[0] != '\0';
}

static s32 s_scenarioMemberPath(const asset_entry_t *scenario,
	const char *member, char *out, size_t out_n)
{
	if (!scenario || !member || !out || out_n == 0) {
		return 0;
	}

	out[0] = '\0';
	if (scenario->ext.scenario.pads_file[0]) {
		s_copyMemberPath(out, out_n, scenario->ext.scenario.pads_file,
			member);
	}
	if (!out[0]) {
		s_copyMemberPath(out, out_n, scenario->ext.scenario.scene_file,
			member);
	}
	return out[0] != '\0';
}

static void s_graphFailure(asset_type_e type, const char *asset_id,
	const char *path, const char *reason);

static s32 s_jsonStringValueForKey(const char *text, const char *key,
	char *out, size_t out_n)
{
	char quoted_key[96];
	const char *p;
	const char *colon;
	const char *value;
	size_t i;

	if (!text || !key || !key[0] || !out || out_n == 0) {
		return 0;
	}

	out[0] = '\0';
	if (strlen(key) + 2 >= sizeof(quoted_key)) {
		return 0;
	}

	snprintf(quoted_key, sizeof(quoted_key), "\"%s\"", key);
	p = strstr(text, quoted_key);
	if (!p) {
		return 0;
	}

	colon = strchr(p + strlen(quoted_key), ':');
	if (!colon) {
		return 0;
	}

	value = colon + 1;
	while (*value == ' ' || *value == '\t' || *value == '\r'
			|| *value == '\n') {
		value++;
	}
	if (*value != '"') {
		return 0;
	}
	value++;

	for (i = 0; value[i] && value[i] != '"' && i + 1 < out_n; i++) {
		if (value[i] == '\\' && value[i + 1]) {
			i++;
		}
		out[i] = value[i];
	}
	out[i] = '\0';
	return out[0] != '\0';
}

static s32 s_graphMemberPath(const asset_entry_t *scenario,
	const char *member, char *out, size_t out_n)
{
	if (!out || out_n == 0) {
		return 0;
	}
	out[0] = '\0';
	if (!member || !member[0]) {
		return 0;
	}

	if (strstr(member, "::")) {
		s_copyString(out, out_n, member);
		return out[0] != '\0';
	}

	return s_scenarioMemberPath(scenario, member, out, out_n);
}

static s32 s_bindLevelGraphTablePath(const asset_entry_t *scenario,
	const char *graph_text, const char *graph_path, const char *key,
	char *out, size_t out_n)
{
	char member[160];
	char reason[160];

	if (!s_jsonStringValueForKey(graph_text, key, member, sizeof(member))) {
		snprintf(reason, sizeof(reason), "missing level graph table '%s'",
			key);
		s_graphFailure(ASSET_SCENARIO,
			scenario && scenario->id[0] ? scenario->id : "?",
			graph_path, reason);
		return 0;
	}

	if (!s_graphMemberPath(scenario, member, out, out_n)) {
		snprintf(reason, sizeof(reason), "invalid level graph table '%s'",
			key);
		s_graphFailure(ASSET_SCENARIO,
			scenario && scenario->id[0] ? scenario->id : "?",
			graph_path, reason);
		return 0;
	}
	return 1;
}

static char *s_loadOptionalText(const char *path, u32 *out_size)
{
	u32 size;
	char *text;

	if (out_size) {
		*out_size = 0;
	}
	if (!path || !path[0]) {
		return NULL;
	}

	size = 0;
	text = (char *)fsFileLoad(path, &size);
	if (!text || size == 0) {
		if (text) {
			free(text);
		}
		return NULL;
	}

	if (out_size) {
		*out_size = size;
	}
	return text;
}

static char *s_loadGraphText(const char *path, u32 *out_size)
{
	u32 size;
	char *raw;
	char *text;

	if (out_size) {
		*out_size = 0;
	}
	if (!path || !path[0]) {
		return NULL;
	}

	size = 0;
	raw = (char *)fsFileLoad(path, &size);
	if (!raw || size == 0) {
		if (raw) {
			free(raw);
		}
		return NULL;
	}

	text = (char *)malloc((size_t)size + 1u);
	if (!text) {
		free(raw);
		return NULL;
	}

	memcpy(text, raw, size);
	text[size] = '\0';
	free(raw);

	if (out_size) {
		*out_size = size;
	}
	return text;
}

static s32 s_missionGraphMemberPath(const asset_entry_t *mission,
	const char *member, char *out, size_t out_n)
{
	if (!out || out_n == 0) {
		return 0;
	}
	out[0] = '\0';
	if (!member || !member[0]) {
		return 0;
	}

	if (strstr(member, "::")) {
		s_copyString(out, out_n, member);
		return out[0] != '\0';
	}

	if (mission && mission->ext.mission.mission_graph_file[0]) {
		s_copyMemberPath(out, out_n,
			mission->ext.mission.mission_graph_file, member);
	}
	return out[0] != '\0';
}

static s32 s_bindMissionGraphObjectivesPath(const asset_entry_t *mission,
	const char *graph_text, char *out, size_t out_n)
{
	char member[160];

	if (!mission || !out || out_n == 0) {
		return 0;
	}

	out[0] = '\0';
	if (s_jsonStringValueForKey(graph_text, "file", member,
			sizeof(member)) &&
			s_missionGraphMemberPath(mission, member, out, out_n)) {
		return 1;
	}

	if (mission->ext.mission.objectives_file[0]) {
		s_copyString(out, out_n, mission->ext.mission.objectives_file);
		return out[0] != '\0';
	}

	return s_missionGraphMemberPath(mission, "objectives.tsv", out, out_n);
}

static s32 s_parseDifficultyMask(const char *value, u32 *out)
{
	char buf[64];
	char *token;
	u32 bits = 0;

	if (!out) {
		return 0;
	}
	*out = 0;
	if (!value || !value[0]) {
		return 0;
	}
	if (s_parseU32Value(value, out)) {
		return 1;
	}

	strncpy(buf, value, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	for (size_t i = 0; buf[i]; i++) {
		buf[i] = (char)tolower((unsigned char)buf[i]);
		if (buf[i] == '|' || buf[i] == '+' || buf[i] == '/') {
			buf[i] = ',';
		}
	}

	if (strcmp(buf, "all") == 0) {
		*out = DIFFBIT_A | DIFFBIT_SA | DIFFBIT_PA | DIFFBIT_PD;
		return 1;
	}

	token = strtok(buf, ", ");
	while (token) {
		if (strcmp(token, "a") == 0 || strcmp(token, "agent") == 0) {
			bits |= DIFFBIT_A;
		} else if (strcmp(token, "sa") == 0
				|| strcmp(token, "special") == 0
				|| strcmp(token, "special_agent") == 0) {
			bits |= DIFFBIT_SA;
		} else if (strcmp(token, "pa") == 0
				|| strcmp(token, "perfect") == 0
				|| strcmp(token, "perfect_agent") == 0) {
			bits |= DIFFBIT_PA;
		} else if (strcmp(token, "pd") == 0
				|| strcmp(token, "dark") == 0
				|| strcmp(token, "perfect_dark") == 0) {
			bits |= DIFFBIT_PD;
		} else {
			return 0;
		}
		token = strtok(NULL, ", ");
	}

	if (!bits) {
		return 0;
	}
	*out = bits;
	return 1;
}

static void s_freeMissionObjectiveSourceRows(
	scenario_source_objective_node_t *objectives,
	scenario_source_objective_criteria_t *criteria)
{
	free(objectives);
	free(criteria);
}

static s32 s_loadMissionObjectiveSourceRows(const char *path,
	const char *graph_text,
	scenario_source_objective_node_t **out_objectives,
	s32 *out_objective_count,
	scenario_source_objective_criteria_t **out_criteria,
	s32 *out_criteria_count)
{
	char *text;
	char *cursor;
	char *line;
	scenario_source_objective_node_t *objectives = NULL;
	scenario_source_objective_criteria_t *criteria = NULL;
	s32 objective_count = 0;
	s32 objective_capacity = 0;
	s32 criteria_count = 0;
	s32 criteria_capacity = 0;
	s32 current_objective = -1;
	s32 header = 1;

	if (out_objectives) *out_objectives = NULL;
	if (out_objective_count) *out_objective_count = 0;
	if (out_criteria) *out_criteria = NULL;
	if (out_criteria_count) *out_criteria_count = 0;
	if (!path || !path[0]) {
		return 0;
	}

	text = s_loadGraphText(path, NULL);
	if (!text) {
		return 0;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		char *field_cursor;
		char *objective_id;
		char *kind;
		char *text_token;
		char *difficulty_mask;
		char *graph_node;
		char *scenario_source;
		char *operand_kind;
		char *target_ref;
		char *target_record_ref;
		char *pad_ref;
		char *state_ref;
		char *match_value;
		char *initial_status;
		s32 i;

		if (!line[0]) {
			continue;
		}
		if (header) {
			header = 0;
			continue;
		}

		field_cursor = line;
		objective_id = s_nextField(&field_cursor);
		kind = s_nextField(&field_cursor);
		text_token = s_nextField(&field_cursor);
		difficulty_mask = s_nextField(&field_cursor);
		graph_node = s_nextField(&field_cursor);
		scenario_source = s_nextField(&field_cursor);
		operand_kind = s_nextField(&field_cursor);
		target_ref = s_nextField(&field_cursor);
		target_record_ref = s_nextField(&field_cursor);
		pad_ref = s_nextField(&field_cursor);
		state_ref = s_nextField(&field_cursor);
		match_value = s_nextField(&field_cursor);
		initial_status = s_nextField(&field_cursor);
		(void)scenario_source;

		if (!objective_id || !objective_id[0] || !kind || !kind[0]
				|| !graph_node || !strstr(graph_node,
					"mission.objective")) {
			free(text);
			s_freeMissionObjectiveSourceRows(objectives, criteria);
			return 0;
		}
		if (graph_text && graph_text[0] && !strstr(graph_text, graph_node)) {
			free(text);
			s_freeMissionObjectiveSourceRows(objectives, criteria);
			return 0;
		}

		for (i = 0; objective_id[i]; i++) {
			if (!isprint((unsigned char)objective_id[i])) {
				free(text);
				s_freeMissionObjectiveSourceRows(objectives, criteria);
				return 0;
			}
		}

		if (strcmp(kind, "objective") == 0) {
			scenario_source_objective_node_t *grown;
			u32 difficulty_bits;

			if (!s_parseDifficultyMask(difficulty_mask,
					&difficulty_bits)) {
				free(text);
				s_freeMissionObjectiveSourceRows(objectives, criteria);
				return 0;
			}
			if (objective_count >= objective_capacity) {
				objective_capacity = objective_capacity
					? objective_capacity * 2 : 8;
				grown = realloc(objectives,
					(size_t)objective_capacity * sizeof(*objectives));
				if (!grown) {
					free(text);
					s_freeMissionObjectiveSourceRows(objectives,
						criteria);
					return 0;
				}
				objectives = grown;
			}
			memset(&objectives[objective_count], 0,
				sizeof(objectives[objective_count]));
			s_copyString(objectives[objective_count].objective_id,
				sizeof(objectives[objective_count].objective_id),
				objective_id);
			s_copyString(objectives[objective_count].graph_node,
				sizeof(objectives[objective_count].graph_node),
				graph_node);
			s_copyString(objectives[objective_count].text_token,
				sizeof(objectives[objective_count].text_token),
				text_token ? text_token : "");
			s_copyString(objectives[objective_count].difficulty_mask,
				sizeof(objectives[objective_count].difficulty_mask),
				difficulty_mask ? difficulty_mask : "");
			objectives[objective_count].difficulty_bits =
				difficulty_bits;
			objectives[objective_count].criteria_start =
				criteria_count;
			current_objective = objective_count;
			objective_count++;
		} else if (s_startsWith(kind, "objective_")) {
			scenario_source_objective_criteria_t *grown;
			u8 type;

			if (current_objective < 0 ||
					!s_setupKindToObjType(kind, &type)) {
				free(text);
				s_freeMissionObjectiveSourceRows(objectives, criteria);
				return 0;
			}
			if (criteria_count >= criteria_capacity) {
				criteria_capacity = criteria_capacity
					? criteria_capacity * 2 : 16;
				grown = realloc(criteria,
					(size_t)criteria_capacity * sizeof(*criteria));
				if (!grown) {
					free(text);
					s_freeMissionObjectiveSourceRows(objectives,
						criteria);
					return 0;
				}
				criteria = grown;
			}
			memset(&criteria[criteria_count], 0,
				sizeof(criteria[criteria_count]));
			s_copyString(criteria[criteria_count].objective_id,
				sizeof(criteria[criteria_count].objective_id),
				objective_id);
			s_copyString(criteria[criteria_count].kind,
				sizeof(criteria[criteria_count].kind), kind);
			s_copyString(criteria[criteria_count].graph_node,
				sizeof(criteria[criteria_count].graph_node),
				graph_node);
			s_copyString(criteria[criteria_count].operand_kind,
				sizeof(criteria[criteria_count].operand_kind),
				operand_kind ? operand_kind : "");
			s_copyString(criteria[criteria_count].target_ref,
				sizeof(criteria[criteria_count].target_ref),
				target_ref ? target_ref : "");
			s_copyString(criteria[criteria_count].target_record_ref,
				sizeof(criteria[criteria_count].target_record_ref),
				target_record_ref ? target_record_ref : "");
			s_copyString(criteria[criteria_count].pad_ref,
				sizeof(criteria[criteria_count].pad_ref),
				pad_ref ? pad_ref : "");
			s_copyString(criteria[criteria_count].state_ref,
				sizeof(criteria[criteria_count].state_ref),
				state_ref ? state_ref : "");
			criteria[criteria_count].type = type;
			criteria[criteria_count].tag_id =
				s_parseIndexedRef(target_ref);
			criteria[criteria_count].pad =
				s_parseIndexedRef(pad_ref);
			if (state_ref && state_ref[0] &&
					!s_parseStageFlagRef(state_ref,
						&criteria[criteria_count]
							.stage_flag_mask)) {
				free(text);
				s_freeMissionObjectiveSourceRows(objectives,
					criteria);
				return 0;
			}
			if (match_value && match_value[0]) {
				s32 parsed_match;
				if (!s_parseInteger(match_value, &parsed_match)) {
					free(text);
					s_freeMissionObjectiveSourceRows(objectives,
						criteria);
					return 0;
				}
				criteria[criteria_count].match_value =
					parsed_match;
			}
			criteria[criteria_count].initial_status = -1;
			if (initial_status && initial_status[0]) {
				s32 parsed_status;
				if (!s_parseInteger(initial_status,
						&parsed_status)) {
					free(text);
					s_freeMissionObjectiveSourceRows(objectives,
						criteria);
					return 0;
				}
				criteria[criteria_count].initial_status =
					parsed_status;
			}
			criteria[criteria_count].runtime_status =
				criteria[criteria_count].initial_status;
			if ((type == OBJECTIVETYPE_DESTROYOBJ ||
					type == OBJECTIVETYPE_COLLECTOBJ ||
					type == OBJECTIVETYPE_THROWOBJ ||
					type == OBJECTIVETYPE_HOLOGRAPH) &&
					criteria[criteria_count].tag_id < 0) {
				free(text);
				s_freeMissionObjectiveSourceRows(objectives,
					criteria);
				return 0;
			}
			if ((type == OBJECTIVETYPE_ENTERROOM ||
					type == OBJECTIVETYPE_THROWINROOM) &&
					criteria[criteria_count].pad < 0) {
				free(text);
				s_freeMissionObjectiveSourceRows(objectives,
					criteria);
				return 0;
			}
			if ((type == OBJECTIVETYPE_COMPFLAGS ||
					type == OBJECTIVETYPE_FAILFLAGS) &&
					criteria[criteria_count].stage_flag_mask == 0) {
				free(text);
				s_freeMissionObjectiveSourceRows(objectives,
					criteria);
				return 0;
			}
			if (s_objectiveCriterionUsesGraphStatus(type) &&
					criteria[criteria_count].initial_status < 0) {
				free(text);
				s_freeMissionObjectiveSourceRows(objectives,
					criteria);
				return 0;
			}
			criteria[criteria_count].objective_index =
				current_objective;
			objectives[current_objective].criteria_count++;
			criteria_count++;
		} else {
			free(text);
			s_freeMissionObjectiveSourceRows(objectives, criteria);
			return 0;
		}
	}

	free(text);
	if (objective_count <= 0) {
		s_freeMissionObjectiveSourceRows(objectives, criteria);
		return 0;
	}

	if (out_objectives) *out_objectives = objectives;
	if (out_objective_count) *out_objective_count = objective_count;
	if (out_criteria) *out_criteria = criteria;
	if (out_criteria_count) *out_criteria_count = criteria_count;
	return 1;
}

static void s_graphFailure(asset_type_e type, const char *asset_id,
	const char *path, const char *reason)
{
	if (assetSourceDebugIsEnabledFor(type)) {
		sysFatalError("ASSET.SOURCE_ONLY: %s graph source '%s' at %s is not "
			"usable (%s); refusing legacy mission/setup fallback.",
			assetSourceDebugTypeLabel(type),
			asset_id && asset_id[0] ? asset_id : "?",
			path && path[0] ? path : "(missing)",
			reason && reason[0] ? reason : "invalid graph");
	}

	sysLogPrintf(LOG_WARNING,
		"%s.GRAPH: graph source '%s' at %s is not usable (%s)",
		type == ASSET_MISSION ? "MISSION" : "SCENARIO",
		asset_id && asset_id[0] ? asset_id : "?",
		path && path[0] ? path : "(missing)",
		reason && reason[0] ? reason : "invalid graph");
}

static s32 s_validateGraphText(asset_type_e type, const char *asset_id,
	const char *path, const char *text, const char *schema,
	const char *required_ref)
{
	if (!text || !text[0]) {
		s_graphFailure(type, asset_id, path, "empty file");
		return 0;
	}
	if (!schema || !schema[0] || !strstr(text, schema)) {
		s_graphFailure(type, asset_id, path, "schema mismatch");
		return 0;
	}
	if (required_ref && required_ref[0] && !strstr(text, required_ref)) {
		s_graphFailure(type, asset_id, path, "missing scenario reference");
		return 0;
	}
	if (!strstr(text, "\"nodes\"")) {
		s_graphFailure(type, asset_id, path, "missing nodes");
		return 0;
	}
	if (strstr(text, "\"nodes\": []") || strstr(text, "\"nodes\":[]")) {
		s_graphFailure(type, asset_id, path, "empty nodes");
		return 0;
	}
	if (type == ASSET_MISSION && !strstr(text, "mission.objective")) {
		s_graphFailure(type, asset_id, path,
			"missing mission objective nodes");
		return 0;
	}
	if (type == ASSET_MISSION
			&& !strstr(text, "mission.objective.source")
			&& !strstr(text, "mission.objective.criteria.source")) {
		s_graphFailure(type, asset_id, path,
			"missing executable mission objective source nodes");
		return 0;
	}
	if (!strstr(text, "\"links\"") && !strstr(text, "\"edges\"")) {
		s_graphFailure(type, asset_id, path, "missing links or edges");
		return 0;
	}
	return 1;
}

static s32 s_countTextOccurrences(const char *text, const char *needle)
{
	const char *p;
	s32 count;

	if (!text || !needle || !needle[0]) {
		return 0;
	}

	p = text;
	count = 0;
	while ((p = strstr(p, needle)) != NULL) {
		count++;
		p += strlen(needle);
	}
	return count;
}

static void s_catalogIdToFilenameSlug(const char *id, char *out, size_t out_n)
{
	size_t i;
	size_t j;

	if (!out || out_n == 0) {
		return;
	}
	out[0] = '\0';
	if (!id || !id[0]) {
		return;
	}

	for (i = 0, j = 0; id[i] && j + 1 < out_n; i++) {
		char c = id[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
				|| (c >= '0' && c <= '9')) {
			out[j++] = c;
		} else {
			out[j++] = '_';
		}
	}
	out[j] = '\0';
}

static s32 s_missionGraphMentionsScenario(const asset_entry_t *mission,
	const char *scenario_id)
{
	char *text;
	u32 text_size;
	s32 match;

	if (!mission || !mission->ext.mission.mission_graph_file[0]
			|| !scenario_id || !scenario_id[0]) {
		return 0;
	}

	text = s_loadGraphText(mission->ext.mission.mission_graph_file, &text_size);
	if (!text) {
		return 0;
	}

	match = strstr(text, scenario_id) != NULL;
	free(text);
	return match;
}

static void s_matchMissionEntry(const asset_entry_t *entry, void *userdata)
{
	scenario_source_mission_match_t *match;

	if (!entry || !userdata) {
		return;
	}

	match = (scenario_source_mission_match_t *)userdata;
	if (!match->scenario || !match->scenario->id[0]) {
		return;
	}

	if (!match->archive_match && match->scenario_slug[0]
			&& strstr(entry->ext.mission.scenario_archive,
				match->scenario_slug)) {
		match->archive_match = entry;
	}

	if (!match->graph_match
			&& s_missionGraphMentionsScenario(entry,
				match->scenario->id)) {
		match->graph_match = entry;
	}
}

static const asset_entry_t *s_findMissionForScenario(
	const asset_entry_t *scenario)
{
	scenario_source_mission_match_t match;
	const asset_entry_t *exact;
	char exact_id[CATALOG_ID_LEN];
	const char *base_prefix = "base:scenario_";

	if (!scenario || !scenario->id[0]) {
		return NULL;
	}

	if (s_startsWith(scenario->id, base_prefix)) {
		snprintf(exact_id, sizeof(exact_id), "base:mission_%s",
			scenario->id + strlen(base_prefix));
		exact = assetCatalogResolve(exact_id);
		if (exact && exact->type == ASSET_MISSION) {
			return exact;
		}
	}

	memset(&match, 0, sizeof(match));
	match.scenario = scenario;
	s_catalogIdToFilenameSlug(scenario->id, match.scenario_slug,
		sizeof(match.scenario_slug));
	assetCatalogIterateByType(ASSET_MISSION, s_matchMissionEntry, &match);

	if (match.graph_match) {
		return match.graph_match;
	}
	return match.archive_match;
}

s32 scenarioSourceActivateGraphsForStage(const catalog_stage_result_t *stage,
	s32 prefer_mp)
{
	const asset_entry_t *scenario;
	const asset_entry_t *mission;
	char graph_path[FS_MAXPATH + 1];
	char mission_objectives_path[FS_MAXPATH + 1];
	char *text;
	u32 text_size;
	const char *stageid;
	s32 active;
	s32 mission_objectives;
	s32 mission_criteria;
	s32 mission_phase_node_count;
	s32 level_volume_count;
	s32 level_volume_node_count;
	s32 level_pad_node_count;
	s32 level_global_settings_node_count;
	s32 level_ai_list_node_count;
	s32 level_path_node_count;
	s32 level_ai_jog_to_pad_node_count;
	s32 level_ai_goto_pad_preset_node_count;
	s32 level_ai_walk_to_pad_node_count;
	s32 level_ai_run_to_pad_node_count;
	s32 level_ai_set_path_node_count;
	s32 level_ai_start_patrol_node_count;
	scenario_source_volume_row_t *level_volume_rows;
	scenario_source_objective_node_t *mission_objective_rows;
	scenario_source_objective_criteria_t *mission_criteria_rows;

	s_resetActiveScenarioGraphs();
	level_volume_rows = NULL;
	mission_objective_rows = NULL;
	mission_criteria_rows = NULL;

	scenario = s_findScenarioForStage(stage, prefer_mp);
	if (!scenario || !scenario->id[0]) {
		if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
			stageid = stage && stage->entry && stage->entry->id[0]
				? stage->entry->id : "?";
			s_graphFailure(ASSET_SCENARIO, stageid, NULL,
				"no scenario catalog entry");
		}
		return 0;
	}

	graph_path[0] = '\0';
	if (scenario->ext.scenario.level_graph_file[0]) {
		s_copyString(graph_path, sizeof(graph_path),
			scenario->ext.scenario.level_graph_file);
	} else {
		s_scenarioMemberPath(scenario, "level.graph.json",
			graph_path, sizeof(graph_path));
	}

	if (!graph_path[0]) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, NULL,
			"missing level.graph.json binding");
		return 0;
	}

	text_size = 0;
	text = s_loadGraphText(graph_path, &text_size);
	if (!text) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"load failed");
		return 0;
	}

	if (!s_validateGraphText(ASSET_SCENARIO, scenario->id, graph_path,
			text, "pd2.level.graph.v1", scenario->id)) {
		free(text);
		return 0;
	}
	level_volume_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.trigger.volume.source\"");
	level_pad_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.pads.source\"");
	level_global_settings_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.global.settings.source\"");
	level_ai_list_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.lists.source\"");
	level_path_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.navigation.paths.source\"");
	level_ai_jog_to_pad_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.jog_to_pad\"");
	level_ai_goto_pad_preset_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.go_to_pad_preset\"");
	level_ai_walk_to_pad_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.walk_to_pad\"");
	level_ai_run_to_pad_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.run_to_pad\"");
	level_ai_set_path_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_path\"");
	level_ai_start_patrol_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.start_patrol\"");
	if (level_global_settings_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable global settings source node");
		free(text);
		return 0;
	}
	if (level_ai_list_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI list source node");
		free(text);
		return 0;
	}
	if (level_pad_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable pad source node");
		free(text);
		return 0;
	}
	if (level_path_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable navigation path source node");
		free(text);
		return 0;
	}
	if (level_ai_jog_to_pad_node_count != 1 ||
			level_ai_goto_pad_preset_node_count != 1 ||
			level_ai_walk_to_pad_node_count != 1 ||
			level_ai_run_to_pad_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI pad action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_set_path_node_count != 1 ||
			level_ai_start_patrol_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI path action source nodes");
		free(text);
		return 0;
	}
	if (!s_jsonStringValueForKey(text, "scenario",
			s_ActiveScenarioGraphs.level_global_settings_scenario,
			sizeof(s_ActiveScenarioGraphs.level_global_settings_scenario)) ||
			strcmp(s_ActiveScenarioGraphs.level_global_settings_scenario,
				scenario->id) != 0) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"global settings source scenario mismatch");
		free(text);
		return 0;
	}
	s_jsonStringValueForKey(text, "kind",
		s_ActiveScenarioGraphs.level_global_settings_kind,
		sizeof(s_ActiveScenarioGraphs.level_global_settings_kind));

	s_copyString(s_ActiveScenarioGraphs.scenario_id,
		sizeof(s_ActiveScenarioGraphs.scenario_id), scenario->id);
	s_copyString(s_ActiveScenarioGraphs.level_graph_path,
		sizeof(s_ActiveScenarioGraphs.level_graph_path), graph_path);
	if (!s_bindLevelGraphTablePath(scenario, text, graph_path, "pads",
			s_ActiveScenarioGraphs.pads_path,
			sizeof(s_ActiveScenarioGraphs.pads_path)) ||
			!s_bindLevelGraphTablePath(scenario, text, graph_path, "spawns",
			s_ActiveScenarioGraphs.spawns_path,
			sizeof(s_ActiveScenarioGraphs.spawns_path)) ||
			!s_bindLevelGraphTablePath(scenario, text, graph_path, "objects",
			s_ActiveScenarioGraphs.objects_path,
			sizeof(s_ActiveScenarioGraphs.objects_path)) ||
			!s_bindLevelGraphTablePath(scenario, text, graph_path, "setup_fields",
			s_ActiveScenarioGraphs.setup_fields_path,
			sizeof(s_ActiveScenarioGraphs.setup_fields_path)) ||
			!s_bindLevelGraphTablePath(scenario, text, graph_path, "ai_lists",
			s_ActiveScenarioGraphs.ai_lists_path,
			sizeof(s_ActiveScenarioGraphs.ai_lists_path)) ||
			!s_bindLevelGraphTablePath(scenario, text, graph_path, "volumes",
			s_ActiveScenarioGraphs.volumes_path,
			sizeof(s_ActiveScenarioGraphs.volumes_path)) ||
			!s_bindLevelGraphTablePath(scenario, text, graph_path, "objectives",
			s_ActiveScenarioGraphs.objectives_path,
			sizeof(s_ActiveScenarioGraphs.objectives_path)) ||
			!s_bindLevelGraphTablePath(scenario, text, graph_path, "waypoints",
			s_ActiveScenarioGraphs.waypoints_path,
			sizeof(s_ActiveScenarioGraphs.waypoints_path)) ||
			!s_bindLevelGraphTablePath(scenario, text, graph_path, "waygroups",
			s_ActiveScenarioGraphs.waygroups_path,
			sizeof(s_ActiveScenarioGraphs.waygroups_path)) ||
			!s_bindLevelGraphTablePath(scenario, text, graph_path, "covers",
			s_ActiveScenarioGraphs.covers_path,
			sizeof(s_ActiveScenarioGraphs.covers_path)) ||
			!s_bindLevelGraphTablePath(scenario, text, graph_path, "paths",
			s_ActiveScenarioGraphs.paths_path,
			sizeof(s_ActiveScenarioGraphs.paths_path))) {
		s_resetActiveScenarioGraphs();
		free(text);
		return 0;
	}
	level_volume_count = 0;
	if (!s_loadLevelVolumeSourceRows(s_ActiveScenarioGraphs.volumes_path,
			&level_volume_rows, &level_volume_count)) {
		s_graphFailure(ASSET_SCENARIO, scenario->id,
			s_ActiveScenarioGraphs.volumes_path,
			"missing executable level volume source rows");
		s_resetActiveScenarioGraphs();
		free(text);
		return 0;
	}
	if (level_volume_node_count != level_volume_count) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"trigger volume node count differs from volumes.tsv rows");
		s_resetActiveScenarioGraphs();
		free(text);
		free(level_volume_rows);
		return 0;
	}
	s_ActiveScenarioGraphs.level_graph_size = text_size;
	s_ActiveScenarioGraphs.level_volumes = level_volume_rows;
	s_ActiveScenarioGraphs.level_volume_count = level_volume_count;
	s_ActiveScenarioGraphs.level_volume_node_count =
		level_volume_node_count;
	s_ActiveScenarioGraphs.level_pad_node_count =
		level_pad_node_count;
	s_ActiveScenarioGraphs.level_global_settings_node_count =
		level_global_settings_node_count;
	s_ActiveScenarioGraphs.level_ai_list_node_count =
		level_ai_list_node_count;
	s_ActiveScenarioGraphs.level_path_node_count =
		level_path_node_count;
	s_ActiveScenarioGraphs.level_ai_jog_to_pad_node_count =
		level_ai_jog_to_pad_node_count;
	s_ActiveScenarioGraphs.level_ai_goto_pad_preset_node_count =
		level_ai_goto_pad_preset_node_count;
	s_ActiveScenarioGraphs.level_ai_walk_to_pad_node_count =
		level_ai_walk_to_pad_node_count;
	s_ActiveScenarioGraphs.level_ai_run_to_pad_node_count =
		level_ai_run_to_pad_node_count;
	s_ActiveScenarioGraphs.level_ai_set_path_node_count =
		level_ai_set_path_node_count;
	s_ActiveScenarioGraphs.level_ai_start_patrol_node_count =
		level_ai_start_patrol_node_count;
	s_ActiveScenarioGraphs.level_graph_active = 1;

	stageid = stage && stage->entry && stage->entry->id[0]
		? stage->entry->id : "?";
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: activated level graph '%s' path=%s bytes=%u stage='%s'",
		scenario->id, graph_path, (unsigned)text_size, stageid);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: table refs '%s' pads=%s spawns=%s setup=%s ai=%s objects=%s volumes=%s objectives=%s waypoints=%s waygroups=%s covers=%s paths=%s",
		scenario->id,
		s_ActiveScenarioGraphs.pads_path,
		s_ActiveScenarioGraphs.spawns_path,
		s_ActiveScenarioGraphs.setup_fields_path,
		s_ActiveScenarioGraphs.ai_lists_path,
		s_ActiveScenarioGraphs.objects_path,
		s_ActiveScenarioGraphs.volumes_path,
		s_ActiveScenarioGraphs.objectives_path,
		s_ActiveScenarioGraphs.waypoints_path,
		s_ActiveScenarioGraphs.waygroups_path,
		s_ActiveScenarioGraphs.covers_path,
		s_ActiveScenarioGraphs.paths_path);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: global settings source '%s' nodes=%d scenario='%s' kind='%s' backend=graph.global.settings+level.graph.nodes",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_global_settings_node_count,
		s_ActiveScenarioGraphs.level_global_settings_scenario,
		s_ActiveScenarioGraphs.level_global_settings_kind[0]
			? s_ActiveScenarioGraphs.level_global_settings_kind : "(unknown)");
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: volume source '%s' volumes=%d backend=graph.trigger.volumes+volumes.tsv",
		s_ActiveScenarioGraphs.volumes_path,
		s_ActiveScenarioGraphs.level_volume_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: pad source '%s' nodes=%d backend=graph.pads+pads.tsv",
		s_ActiveScenarioGraphs.pads_path,
		s_ActiveScenarioGraphs.level_pad_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: trigger volume nodes '%s' nodes=%d rows=%d backend=graph.trigger.volumes+level.graph.nodes+volumes.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_volume_node_count,
		s_ActiveScenarioGraphs.level_volume_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI list source '%s' nodes=%d backend=graph.ai.lists+ai/ailists.tsv",
		s_ActiveScenarioGraphs.ai_lists_path,
		s_ActiveScenarioGraphs.level_ai_list_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: path source '%s' nodes=%d backend=graph.navigation.paths+navigation/paths.tsv",
		s_ActiveScenarioGraphs.paths_path,
		s_ActiveScenarioGraphs.level_path_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI pad actions '%s' walk_to_pad=%d run_to_pad=%d backend=graph.ai.action.pad+pads.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_walk_to_pad_node_count,
		s_ActiveScenarioGraphs.level_ai_run_to_pad_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI pad movement actions '%s' jog_to_pad=%d go_to_pad_preset=%d backend=graph.ai.action.pad+pads.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_jog_to_pad_node_count,
		s_ActiveScenarioGraphs.level_ai_goto_pad_preset_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI path actions '%s' set_path=%d start_patrol=%d backend=graph.ai.action.path+navigation/paths.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_set_path_node_count,
		s_ActiveScenarioGraphs.level_ai_start_patrol_node_count);
	free(text);
	active = 1;

	if (prefer_mp) {
		return active;
	}

	mission = s_findMissionForScenario(scenario);
	if (!mission || !mission->id[0]) {
		if (assetSourceDebugIsEnabledFor(ASSET_MISSION)) {
			s_graphFailure(ASSET_MISSION, scenario->id, NULL,
				"no mission catalog entry for scenario");
		}
		return active;
	}

	if (!mission->ext.mission.mission_graph_file[0]) {
		s_graphFailure(ASSET_MISSION, mission->id, NULL,
			"missing mission.graph.json binding");
		return active;
	}

	text_size = 0;
	text = s_loadGraphText(mission->ext.mission.mission_graph_file,
		&text_size);
	if (!text) {
		s_graphFailure(ASSET_MISSION, mission->id,
			mission->ext.mission.mission_graph_file, "load failed");
		return active;
	}

	if (!s_validateGraphText(ASSET_MISSION, mission->id,
			mission->ext.mission.mission_graph_file, text,
			"pd2.mission.graph.v1", scenario->id)) {
		free(text);
		return active;
	}
	mission_phase_node_count = s_countTextOccurrences(text,
		"\"kind\": \"mission.phase.source\"");
	if (mission_phase_node_count < 5) {
		s_graphFailure(ASSET_MISSION, mission->id,
			mission->ext.mission.mission_graph_file,
			"missing executable mission phase source nodes");
		free(text);
		return active;
	}

	mission_objectives_path[0] = '\0';
	if (!s_bindMissionGraphObjectivesPath(mission, text,
			mission_objectives_path, sizeof(mission_objectives_path))) {
		s_graphFailure(ASSET_MISSION, mission->id,
			mission->ext.mission.mission_graph_file,
			"missing mission objectives table binding");
		free(text);
		return active;
	}

	mission_objectives = 0;
	mission_criteria = 0;
	if (!s_loadMissionObjectiveSourceRows(mission_objectives_path, text,
			&mission_objective_rows, &mission_objectives,
			&mission_criteria_rows, &mission_criteria)) {
		s_graphFailure(ASSET_MISSION, mission->id,
			mission_objectives_path,
			"missing executable mission objective source rows");
		free(text);
		return active;
	}

	s_copyString(s_ActiveScenarioGraphs.mission_id,
		sizeof(s_ActiveScenarioGraphs.mission_id), mission->id);
	s_copyString(s_ActiveScenarioGraphs.mission_graph_path,
		sizeof(s_ActiveScenarioGraphs.mission_graph_path),
		mission->ext.mission.mission_graph_file);
	s_copyString(s_ActiveScenarioGraphs.mission_objectives_path,
		sizeof(s_ActiveScenarioGraphs.mission_objectives_path),
		mission_objectives_path);
	s_ActiveScenarioGraphs.mission_graph_size = text_size;
	s_ActiveScenarioGraphs.mission_objectives = mission_objective_rows;
	s_ActiveScenarioGraphs.mission_objective_criteria =
		mission_criteria_rows;
	s_ActiveScenarioGraphs.mission_objective_nodes = mission_objectives;
	s_ActiveScenarioGraphs.mission_objective_criteria_nodes =
		mission_criteria;
	s_ActiveScenarioGraphs.mission_phase_node_count =
		mission_phase_node_count;
	s_ActiveScenarioGraphs.mission_graph_active = 1;
	s_ActiveScenarioGraphs.mission_objective_runtime_active = 1;
	s_ActiveScenarioGraphs.mission_stage_flags = 0;
	s_ActiveScenarioGraphs.mission_stage_flags_valid = 1;

	sysLogPrintf(LOG_NOTE,
		"MISSION.GRAPH: activated mission graph '%s' scenario='%s' path=%s bytes=%u backend=%s",
		mission->id, scenario->id, mission->ext.mission.mission_graph_file,
		(unsigned)text_size,
		strstr(text, "\"parity_backend\"") ? "parity" : "graph");
	sysLogPrintf(LOG_NOTE,
		"MISSION.GRAPH: objective runtime source '%s' objectives=%d criteria=%d backend=graph.objective.source",
		mission_objectives_path, mission_objectives, mission_criteria);
	sysLogPrintf(LOG_NOTE,
		"MISSION.GRAPH: phase source '%s' phases=%d backend=graph.mission.phase+mission.graph.nodes",
		s_ActiveScenarioGraphs.mission_graph_path,
		s_ActiveScenarioGraphs.mission_phase_node_count);
	scenarioSourceMissionGraphRecordPhase("load", "mission.graph.activate");
	free(text);

	return active;
}

s32 scenarioSourceObjectiveGraphIsActive(void)
{
	return s_ActiveScenarioGraphs.mission_objective_runtime_active;
}

static s32 s_aiGraphRuntimeFailure(const char *action, const char *reason)
{
	if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
		sysFatalError("ASSET.SOURCE_ONLY: scenario graph '%s' cannot %s "
			"AI action from public source (%s); refusing legacy-only AI behavior.",
			s_ActiveScenarioGraphs.scenario_id[0]
				? s_ActiveScenarioGraphs.scenario_id : "?",
			action && action[0] ? action : "execute",
			reason && reason[0] ? reason : "graph mismatch");
	}

	sysLogPrintf(LOG_WARNING,
		"SCENARIO.GRAPH: cannot %s AI action for '%s' (%s)",
		action && action[0] ? action : "execute",
		s_ActiveScenarioGraphs.scenario_id[0]
			? s_ActiveScenarioGraphs.scenario_id : "?",
		reason && reason[0] ? reason : "graph mismatch");
	return 1;
}

static s32 s_aiGraphExecuteGoToPad(struct chrdata *chr, s32 pad,
	u32 goposflags, const char *action, const char *backend, s32 *logged)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure(action, "missing chr");
	}
	if (!s_ActiveScenarioGraphs.pads_path[0]) {
		return s_aiGraphRuntimeFailure(action, "missing pads.tsv source");
	}
	chrGoToPad(chr, pad, goposflags);
	if (logged && !*logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action %s pad=%d source=%s %s",
			action, pad, s_ActiveScenarioGraphs.pads_path, backend);
		*logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteJogToPad(struct chrdata *chr, s32 pad)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_jog_to_pad_node_count != 1) {
		return s_aiGraphRuntimeFailure("jog_to_pad",
			"missing scenario.ai.action.jog_to_pad node");
	}
	return s_aiGraphExecuteGoToPad(chr, pad, GOPOSFLAG_JOG,
		"jog_to_pad", "backend=graph.ai.action.jog_to_pad+pads.tsv",
		&s_ActiveScenarioGraphs.ai_action_jog_to_pad_logged);
}

s32 scenarioSourceAiGraphExecuteGoToPadPreset(struct chrdata *chr, s32 speed_code)
{
	u32 goposflags;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_goto_pad_preset_node_count != 1) {
		return s_aiGraphRuntimeFailure("go_to_pad_preset",
			"missing scenario.ai.action.go_to_pad_preset node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("go_to_pad_preset", "missing chr");
	}

	switch (speed_code) {
	case 0:
		goposflags = GOPOSFLAG_WALK;
		break;
	case 1:
		goposflags = GOPOSFLAG_JOG;
		break;
	default:
		goposflags = GOPOSFLAG_RUN;
		break;
	}

	return s_aiGraphExecuteGoToPad(chr, chr->padpreset1, goposflags,
		"go_to_pad_preset",
		"backend=graph.ai.action.go_to_pad_preset+pads.tsv",
		&s_ActiveScenarioGraphs.ai_action_goto_pad_preset_logged);
}

s32 scenarioSourceAiGraphExecuteWalkToPad(struct chrdata *chr, s32 pad)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_walk_to_pad_node_count != 1) {
		return s_aiGraphRuntimeFailure("walk_to_pad",
			"missing scenario.ai.action.walk_to_pad node");
	}
	return s_aiGraphExecuteGoToPad(chr, pad, GOPOSFLAG_WALK,
		"walk_to_pad", "backend=graph.ai.action.walk_to_pad+pads.tsv",
		&s_ActiveScenarioGraphs.ai_action_walk_to_pad_logged);
}

s32 scenarioSourceAiGraphExecuteRunToPad(struct chrdata *chr, s32 pad)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_run_to_pad_node_count != 1) {
		return s_aiGraphRuntimeFailure("run_to_pad",
			"missing scenario.ai.action.run_to_pad node");
	}
	return s_aiGraphExecuteGoToPad(chr, pad, GOPOSFLAG_RUN,
		"run_to_pad", "backend=graph.ai.action.run_to_pad+pads.tsv",
		&s_ActiveScenarioGraphs.ai_action_run_to_pad_logged);
}

s32 scenarioSourceAiGraphExecuteSetPath(struct chrdata *chr, s32 path_id)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_path_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_path",
			"missing scenario.ai.action.set_path node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_path", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.paths_path[0]) {
		return s_aiGraphRuntimeFailure("set_path",
			"missing navigation/paths.tsv source");
	}
	chrSetPath(chr, (u32)path_id);
	if (!s_ActiveScenarioGraphs.ai_action_set_path_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_path path=%d source=%s backend=graph.ai.action.set_path+navigation/paths.tsv",
			path_id, s_ActiveScenarioGraphs.paths_path);
		s_ActiveScenarioGraphs.ai_action_set_path_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteStartPatrol(struct chrdata *chr)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_start_patrol_node_count != 1) {
		return s_aiGraphRuntimeFailure("start_patrol",
			"missing scenario.ai.action.start_patrol node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("start_patrol", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.paths_path[0]) {
		return s_aiGraphRuntimeFailure("start_patrol",
			"missing navigation/paths.tsv source");
	}
	chrTryStartPatrol(chr);
	if (!s_ActiveScenarioGraphs.ai_action_start_patrol_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action start_patrol path=%u source=%s backend=graph.ai.action.start_patrol+navigation/paths.tsv",
			(unsigned)chr->path, s_ActiveScenarioGraphs.paths_path);
		s_ActiveScenarioGraphs.ai_action_start_patrol_logged = 1;
	}
	return 1;
}

static s32 s_missionObjectiveGraphRuntimeFailure(const char *action,
	s32 index, const char *reason)
{
	if (assetSourceDebugIsEnabledFor(ASSET_MISSION)) {
		sysFatalError("ASSET.SOURCE_ONLY: mission graph '%s' cannot %s "
			"runtime objective index %d from public source (%s); "
			"refusing legacy-only objective behavior.",
			s_ActiveScenarioGraphs.mission_id[0]
				? s_ActiveScenarioGraphs.mission_id : "?",
			action && action[0] ? action : "validate",
			index,
			reason && reason[0] ? reason : "graph mismatch");
	}

	sysLogPrintf(LOG_WARNING,
		"MISSION.GRAPH: cannot %s runtime objective index %d for '%s' (%s)",
		action && action[0] ? action : "validate",
		index,
		s_ActiveScenarioGraphs.mission_id[0]
			? s_ActiveScenarioGraphs.mission_id : "?",
		reason && reason[0] ? reason : "graph mismatch");
	return 0;
}

s32 scenarioSourceObjectiveGraphReportRuntimeMismatch(s32 index,
	const char *action, const char *reason)
{
	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active) {
		return 0;
	}
	return s_missionObjectiveGraphRuntimeFailure(action, index, reason);
}

s32 scenarioSourceMissionGraphRecordPhase(const char *phase,
	const char *reason)
{
	if (!s_ActiveScenarioGraphs.mission_graph_active) {
		return 1;
	}
	if (!phase || !phase[0]) {
		return s_missionObjectiveGraphRuntimeFailure("phase", -1,
			"missing mission phase");
	}
	if (s_ActiveScenarioGraphs.mission_phase_node_count < 5) {
		return s_missionObjectiveGraphRuntimeFailure("phase", -1,
			"missing mission phase source nodes");
	}
	if (strcmp(s_ActiveScenarioGraphs.mission_phase_state, phase) == 0) {
		return 1;
	}

	s_copyString(s_ActiveScenarioGraphs.mission_phase_state,
		sizeof(s_ActiveScenarioGraphs.mission_phase_state), phase);
	sysLogPrintf(LOG_NOTE,
		"MISSION.GRAPH: phase transition from graph source '%s' phase=%s reason=%s backend=graph.mission.phase",
		s_ActiveScenarioGraphs.mission_graph_path,
		phase,
		reason && reason[0] ? reason : "runtime");
	return 1;
}

s32 scenarioSourceObjectiveGraphGetCriterionCount(s32 index)
{
	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active) {
		return -1;
	}
	if (index < 0 || index >= s_ActiveScenarioGraphs.mission_objective_nodes
			|| !s_ActiveScenarioGraphs.mission_objectives) {
		return -1;
	}
	return s_ActiveScenarioGraphs.mission_objectives[index].criteria_count;
}

s32 scenarioSourceObjectiveGraphGetCriterionType(s32 index,
	s32 criterion_index, u8 *out_type)
{
	const scenario_source_objective_node_t *node;
	const scenario_source_objective_criteria_t *criteria;
	s32 criteria_offset;

	if (out_type) {
		*out_type = 0;
	}
	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active ||
			!out_type) {
		return 0;
	}
	if (index < 0 || index >= s_ActiveScenarioGraphs.mission_objective_nodes
			|| !s_ActiveScenarioGraphs.mission_objectives
			|| !s_ActiveScenarioGraphs.mission_objective_criteria) {
		return 0;
	}

	node = &s_ActiveScenarioGraphs.mission_objectives[index];
	if (criterion_index < 0 || criterion_index >= node->criteria_count) {
		return 0;
	}

	criteria_offset = node->criteria_start + criterion_index;
	if (criteria_offset < 0 ||
			criteria_offset >=
			s_ActiveScenarioGraphs.mission_objective_criteria_nodes) {
		return 0;
	}

	criteria =
		&s_ActiveScenarioGraphs.mission_objective_criteria[
			criteria_offset];
	*out_type = criteria->type;
	return 1;
}

s32 scenarioSourceObjectiveGraphGetCriterionOperand(s32 index,
	s32 criterion_index, scenario_source_objective_operand_t *out_operand)
{
	const scenario_source_objective_node_t *node;
	const scenario_source_objective_criteria_t *criteria;
	s32 criteria_offset;

	if (out_operand) {
		memset(out_operand, 0, sizeof(*out_operand));
		out_operand->tag_id = -1;
		out_operand->pad = -1;
		out_operand->initial_status = -1;
	}
	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active ||
			!out_operand) {
		return 0;
	}
	if (index < 0 || index >= s_ActiveScenarioGraphs.mission_objective_nodes
			|| !s_ActiveScenarioGraphs.mission_objectives
			|| !s_ActiveScenarioGraphs.mission_objective_criteria) {
		return 0;
	}

	node = &s_ActiveScenarioGraphs.mission_objectives[index];
	if (criterion_index < 0 || criterion_index >= node->criteria_count) {
		return 0;
	}

	criteria_offset = node->criteria_start + criterion_index;
	if (criteria_offset < 0 ||
			criteria_offset >=
			s_ActiveScenarioGraphs.mission_objective_criteria_nodes) {
		return 0;
	}

	criteria =
		&s_ActiveScenarioGraphs.mission_objective_criteria[
			criteria_offset];
	out_operand->type = criteria->type;
	out_operand->stage_flag_mask = criteria->stage_flag_mask;
	out_operand->tag_id = criteria->tag_id;
	out_operand->pad = criteria->pad;
	out_operand->match_value = criteria->match_value;
	out_operand->initial_status = criteria->initial_status;
	out_operand->runtime_status = criteria->runtime_status;
	out_operand->runtime_status_valid = criteria->runtime_status_valid;
	out_operand->runtime_object_state_valid =
		criteria->runtime_object_state_valid;
	out_operand->runtime_object_present =
		criteria->runtime_object_present;
	out_operand->runtime_object_healthy =
		criteria->runtime_object_healthy;
	out_operand->runtime_object_held_by_player =
		criteria->runtime_object_held_by_player;
	return 1;
}

s32 scenarioSourceObjectiveGraphRecordCriterionStatus(const void *criteria,
	s32 status)
{
	s32 i;

	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active) {
		return 1;
	}
	if (!criteria ||
			!s_ActiveScenarioGraphs.mission_objective_criteria) {
		return s_missionObjectiveGraphRuntimeFailure("state", -1,
			"missing objective criteria pointer");
	}

	for (i = 0; i < s_ActiveScenarioGraphs
			.mission_objective_criteria_nodes; i++) {
		scenario_source_objective_criteria_t *entry =
			&s_ActiveScenarioGraphs.mission_objective_criteria[i];

		if (entry->runtime_criteria == criteria) {
			entry->runtime_status = status;
			entry->runtime_status_valid = 1;

			if (!s_ActiveScenarioGraphs
					.mission_objective_state_logged) {
				sysLogPrintf(LOG_NOTE,
					"MISSION.GRAPH: objective state updated from graph source '%s' objective=%d node=%s status=%d backend=graph.objective.state",
					s_ActiveScenarioGraphs.mission_objectives_path,
					entry->objective_index,
					entry->graph_node,
					status);
				s_ActiveScenarioGraphs
					.mission_objective_state_logged = 1;
			}

			return 1;
		}
	}

	return s_missionObjectiveGraphRuntimeFailure("state", -1,
		"objective criteria status was not bound to graph source");
}

s32 scenarioSourceObjectiveGraphRecordObjectState(s32 tag_id,
	s32 present, s32 healthy, s32 held_by_player)
{
	s32 i;
	s32 updated = 0;

	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active) {
		return 1;
	}
	if (tag_id < 0 ||
			!s_ActiveScenarioGraphs.mission_objective_criteria) {
		return s_missionObjectiveGraphRuntimeFailure("object-state", -1,
			"missing objective object state source");
	}

	for (i = 0; i < s_ActiveScenarioGraphs
			.mission_objective_criteria_nodes; i++) {
		scenario_source_objective_criteria_t *entry =
			&s_ActiveScenarioGraphs.mission_objective_criteria[i];

		if (entry->tag_id == tag_id &&
				s_objectiveCriterionUsesObjectState(entry->type)) {
			entry->runtime_object_state_valid = 1;
			entry->runtime_object_present = present != 0;
			entry->runtime_object_healthy = healthy != 0;
			entry->runtime_object_held_by_player =
				held_by_player != 0;
			updated = 1;

			if (!s_ActiveScenarioGraphs
					.mission_objective_object_state_logged) {
				sysLogPrintf(LOG_NOTE,
					"MISSION.GRAPH: objective object state updated from graph source '%s' objective=%d node=%s tag=%d present=%d healthy=%d held=%d backend=graph.objective.object_state",
					s_ActiveScenarioGraphs.mission_objectives_path,
					entry->objective_index,
					entry->graph_node,
					tag_id,
					present != 0,
					healthy != 0,
					held_by_player != 0);
				s_ActiveScenarioGraphs
					.mission_objective_object_state_logged = 1;
			}
		}
	}

	(void)updated;
	return 1;
}

s32 scenarioSourceObjectiveGraphRecordStageFlags(u32 flags)
{
	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active) {
		return 1;
	}

	s_ActiveScenarioGraphs.mission_stage_flags = flags;
	s_ActiveScenarioGraphs.mission_stage_flags_valid = 1;

	if (!s_ActiveScenarioGraphs.mission_stage_flags_logged) {
		sysLogPrintf(LOG_NOTE,
			"MISSION.GRAPH: mission flags updated in graph runtime '%s' flags=0x%08x backend=graph.mission.flags",
			s_ActiveScenarioGraphs.mission_objectives_path,
			flags);
		s_ActiveScenarioGraphs.mission_stage_flags_logged = 1;
	}

	return 1;
}

s32 scenarioSourceObjectiveGraphHasStageFlag(u32 flag, s32 *out_has_flag)
{
	if (out_has_flag) {
		*out_has_flag = 0;
	}
	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active) {
		return 0;
	}
	if (!s_ActiveScenarioGraphs.mission_stage_flags_valid || flag == 0) {
		return 0;
	}
	if (out_has_flag) {
		*out_has_flag =
			(s_ActiveScenarioGraphs.mission_stage_flags & flag) != 0;
	}
	return 1;
}

s32 scenarioSourceObjectiveGraphRecordEvaluate(s32 index, s32 criteria_count)
{
	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active) {
		return 1;
	}
	if (index < 0 || index >= s_ActiveScenarioGraphs.mission_objective_nodes
			|| !s_ActiveScenarioGraphs.mission_objectives) {
		return s_missionObjectiveGraphRuntimeFailure("evaluate", index,
			"missing objective source node");
	}
	if (!s_ActiveScenarioGraphs.mission_objectives[index].inserted) {
		return s_missionObjectiveGraphRuntimeFailure("evaluate", index,
			"objective was not inserted from graph source");
	}
	if (criteria_count !=
			s_ActiveScenarioGraphs.mission_objectives[index].criteria_count) {
		return s_missionObjectiveGraphRuntimeFailure("evaluate", index,
			"criteria count differs from graph source");
	}

	if (!s_ActiveScenarioGraphs.mission_objective_evaluate_logged) {
		sysLogPrintf(LOG_NOTE,
			"MISSION.GRAPH: objective criteria evaluated from graph source '%s' index=%d criteria=%d backend=graph.objective.operands+graph.objective.state+graph.objective.object_state+graph.mission.flags",
			s_ActiveScenarioGraphs.mission_objectives_path,
			index, criteria_count);
		s_ActiveScenarioGraphs.mission_objective_evaluate_logged = 1;
	}

	return 1;
}

s32 scenarioSourceObjectiveGraphRecordInsert(const struct objective *objective)
{
	const u32 *cmd;
	const scenario_source_objective_node_t *node;
	s32 index;
	s32 seen_criteria = 0;
	s32 guard = 0;

	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active) {
		return 1;
	}
	if (!objective) {
		return s_missionObjectiveGraphRuntimeFailure("insert", -1,
			"missing objective pointer");
	}

	index = objective->index;
	if (index < 0 || index >= s_ActiveScenarioGraphs.mission_objective_nodes
			|| !s_ActiveScenarioGraphs.mission_objectives) {
		return s_missionObjectiveGraphRuntimeFailure("insert", index,
			"missing objective source node");
	}

	node = &s_ActiveScenarioGraphs.mission_objectives[index];
	if (((u32)(u8)objective->difficulties) != node->difficulty_bits) {
		return s_missionObjectiveGraphRuntimeFailure("insert", index,
			"difficulty mask differs from graph source");
	}

	cmd = (const u32 *)objective;
	while (guard++ < 128) {
		u8 type = (u8)PD_BE32(cmd[0]);
		u32 words;

		if (type == OBJTYPE_ENDOBJECTIVE) {
			break;
		}

		words = s_setupCommandLengthBytesForType(type) / sizeof(u32);
		if (words == 0) {
			return s_missionObjectiveGraphRuntimeFailure("insert",
				index, "objective command has no source length");
		}

		if (type != OBJTYPE_BEGINOBJECTIVE) {
			scenario_source_objective_criteria_t *criteria;

			if (seen_criteria >= node->criteria_count) {
				return s_missionObjectiveGraphRuntimeFailure(
					"insert", index,
					"runtime has more criteria than graph source");
			}

			criteria = &s_ActiveScenarioGraphs
				.mission_objective_criteria[
					node->criteria_start + seen_criteria];
			if (criteria->type != type) {
				return s_missionObjectiveGraphRuntimeFailure(
					"insert", index,
					"criteria type differs from graph source");
			}
			criteria->matched = 1;
			criteria->runtime_criteria = cmd;
			if (s_objectiveCriterionUsesGraphStatus(type)) {
				if (criteria->initial_status < 0) {
					return s_missionObjectiveGraphRuntimeFailure(
						"insert", index,
						"criteria graph state has no initial status");
				}
				criteria->runtime_status =
					criteria->initial_status;
				criteria->runtime_status_valid = 1;
			}
			seen_criteria++;
		}

		cmd += words;
	}

	if (guard >= 128) {
		return s_missionObjectiveGraphRuntimeFailure("insert", index,
			"objective command stream did not terminate");
	}
	if (seen_criteria != node->criteria_count) {
		return s_missionObjectiveGraphRuntimeFailure("insert", index,
			"runtime has fewer criteria than graph source");
	}

	s_ActiveScenarioGraphs.mission_objectives[index].inserted = 1;
	if (!s_ActiveScenarioGraphs.mission_objective_insert_logged) {
		sysLogPrintf(LOG_NOTE,
			"MISSION.GRAPH: objective insert matched graph source '%s' index=%d node=%s criteria=%d",
			s_ActiveScenarioGraphs.mission_objectives_path,
			index, node->graph_node, node->criteria_count);
		s_ActiveScenarioGraphs.mission_objective_insert_logged = 1;
	}

	return 1;
}

s32 scenarioSourceObjectiveGraphRecordCheck(s32 index, s32 status)
{
	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active) {
		return status;
	}

	if (index < 0 ||
			index >= s_ActiveScenarioGraphs.mission_objective_nodes) {
		if (assetSourceDebugIsEnabledFor(ASSET_MISSION)) {
			sysFatalError("ASSET.SOURCE_ONLY: mission graph '%s' has no "
				"objective node for runtime objective index %d "
				"(nodes=%d); refusing legacy-only objective result.",
				s_ActiveScenarioGraphs.mission_id[0]
					? s_ActiveScenarioGraphs.mission_id : "?",
				index,
				s_ActiveScenarioGraphs.mission_objective_nodes);
		}
		if (!s_ActiveScenarioGraphs.mission_objective_range_warning_logged) {
			sysLogPrintf(LOG_WARNING,
				"MISSION.GRAPH: runtime objective index %d is outside graph objective nodes=%d for '%s'",
				index,
				s_ActiveScenarioGraphs.mission_objective_nodes,
				s_ActiveScenarioGraphs.mission_id[0]
					? s_ActiveScenarioGraphs.mission_id : "?");
			s_ActiveScenarioGraphs.mission_objective_range_warning_logged = 1;
		}
		return status;
	}
	if (s_ActiveScenarioGraphs.mission_objectives
			&& !s_ActiveScenarioGraphs.mission_objectives[index].inserted) {
		s_missionObjectiveGraphRuntimeFailure("check", index,
			"objective was not inserted from graph source");
		return status;
	}

	if (!s_ActiveScenarioGraphs.mission_objective_check_logged) {
		sysLogPrintf(LOG_NOTE,
			"MISSION.GRAPH: objective check routed through graph source '%s' nodes=%d criteria=%d backend=graph.objective.operands+graph.objective.state+graph.objective.object_state+graph.mission.flags",
			s_ActiveScenarioGraphs.mission_objectives_path,
			s_ActiveScenarioGraphs.mission_objective_nodes,
			s_ActiveScenarioGraphs.mission_objective_criteria_nodes);
		s_ActiveScenarioGraphs.mission_objective_check_logged = 1;
	}

	return status;
}

static const scenario_source_volume_row_t *s_findLevelVolumeForPad(s32 pad)
{
	s32 i;

	if (!s_ActiveScenarioGraphs.level_volumes || pad < 0) {
		return NULL;
	}

	for (i = 0; i < s_ActiveScenarioGraphs.level_volume_count; i++) {
		const scenario_source_volume_row_t *row =
			&s_ActiveScenarioGraphs.level_volumes[i];

		if (row->padnum == pad) {
			return row;
		}
	}

	return NULL;
}

static s32 s_levelGraphVolumeRuntimeFailure(s32 pad, s32 room,
	const char *reason)
{
	if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
		sysFatalError("ASSET.SOURCE_ONLY: level graph '%s' cannot bind "
			"trigger volume pad=%d room=%d from public source '%s' "
			"(%s); refusing legacy-only trigger evaluation.",
			s_ActiveScenarioGraphs.scenario_id[0]
				? s_ActiveScenarioGraphs.scenario_id : "?",
			pad,
			room,
			s_ActiveScenarioGraphs.volumes_path[0]
				? s_ActiveScenarioGraphs.volumes_path : "(missing)",
			reason && reason[0] ? reason : "graph mismatch");
	}

	if (!s_ActiveScenarioGraphs.level_volume_missing_logged) {
		sysLogPrintf(LOG_WARNING,
			"SCENARIO.GRAPH: cannot bind trigger volume pad=%d room=%d for '%s' (%s)",
			pad,
			room,
			s_ActiveScenarioGraphs.scenario_id[0]
				? s_ActiveScenarioGraphs.scenario_id : "?",
			reason && reason[0] ? reason : "graph mismatch");
		s_ActiveScenarioGraphs.level_volume_missing_logged = 1;
	}
	return 0;
}

s32 scenarioSourceLevelGraphCheckPadRoom(s32 pad, s32 room,
	const char *reason, s32 *out_matches)
{
	const scenario_source_volume_row_t *row;
	s32 matched;

	if (out_matches) {
		*out_matches = 0;
	}
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (pad < 0 || room < 0) {
		return 0;
	}

	row = s_findLevelVolumeForPad(pad);
	if (!row) {
		return s_levelGraphVolumeRuntimeFailure(pad, room,
			"runtime pad was not present in volumes.tsv");
	}
	if (row->room < 0 || strcmp(row->shape, "aabb") != 0) {
		return s_levelGraphVolumeRuntimeFailure(pad, room,
			"volume row is not executable trigger source");
	}

	matched = row->room == room;
	if (out_matches) {
		*out_matches = matched;
	}

	if (!s_ActiveScenarioGraphs.level_volume_eval_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: trigger volume evaluated from graph source '%s' pad=%d room=%d source_room=%d matched=%d reason=%s backend=graph.trigger.volumes+objective.status",
			s_ActiveScenarioGraphs.volumes_path,
			pad,
			room,
			row->room,
			matched,
			reason && reason[0] ? reason : "runtime");
		s_ActiveScenarioGraphs.level_volume_eval_logged = 1;
	}

	return 1;
}

static s32 s_setupBehaviorLinkTargetsMatch(
	const scenario_source_setup_link_t *link, const s32 *targets,
	const s32 *aux)
{
	if (!link || !targets || !aux) {
		return 0;
	}

	return link->target[0] == targets[0] &&
		link->target[1] == targets[1] &&
		link->target[2] == targets[2] &&
		link->aux[0] == aux[0] &&
		link->aux[1] == aux[1];
}

static s32 s_setupGraphRuntimeFailure(u8 type, s32 record_index,
	const char *reason)
{
	const char *kind = s_setupBehaviorLinkKindForType(type);

	if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
		sysFatalError("ASSET.SOURCE_ONLY: level graph '%s' cannot bind "
			"setup behavior link kind=%s record=%d from public source "
			"(%s); refusing legacy-only setup behavior.",
			s_ActiveScenarioGraphs.scenario_id[0]
				? s_ActiveScenarioGraphs.scenario_id : "?",
			kind ? kind : "?",
			record_index,
			reason && reason[0] ? reason : "graph mismatch");
	}

	sysLogPrintf(LOG_WARNING,
		"SCENARIO.GRAPH: cannot bind setup behavior link kind=%s record=%d for '%s' (%s)",
		kind ? kind : "?",
		record_index,
		s_ActiveScenarioGraphs.scenario_id[0]
			? s_ActiveScenarioGraphs.scenario_id : "?",
		reason && reason[0] ? reason : "graph mismatch");
	return 0;
}

s32 scenarioSourceSetupGraphRecordBehaviorLink(u8 type, s32 record_index,
	s32 target0, s32 target1, s32 target2, s32 aux0, s32 aux1)
{
	const char *kind = s_setupBehaviorLinkKindForType(type);
	s32 targets[3];
	s32 aux[2];

	if (!kind) {
		return 1;
	}
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 1;
	}
	if (!s_ActiveScenarioGraphs.setup_links ||
			s_ActiveScenarioGraphs.setup_link_count <= 0) {
		return s_setupGraphRuntimeFailure(type, record_index,
			"missing setup behavior link source table");
	}

	targets[0] = target0;
	targets[1] = target1;
	targets[2] = target2;
	aux[0] = aux0;
	aux[1] = aux1;

	for (s32 i = 0; i < s_ActiveScenarioGraphs.setup_link_count; i++) {
		scenario_source_setup_link_t *link =
			&s_ActiveScenarioGraphs.setup_links[i];

		if (link->type != type || link->order != record_index) {
			continue;
		}
		if (!s_setupBehaviorLinkTargetsMatch(link, targets, aux)) {
			return s_setupGraphRuntimeFailure(type, record_index,
				"runtime targets differ from setup.fields.tsv");
		}

		link->matched = 1;
		if (!s_ActiveScenarioGraphs.setup_link_logged) {
			sysLogPrintf(LOG_NOTE,
				"SCENARIO.GRAPH: setup behavior link registered from graph source '%s' record=%d kind=%s backend=graph.setup.links+setup.fields.tsv",
				s_ActiveScenarioGraphs.setup_fields_path,
				record_index,
				kind);
			s_ActiveScenarioGraphs.setup_link_logged = 1;
		}
		return 1;
	}

	return s_setupGraphRuntimeFailure(type, record_index,
		"runtime link record was not present in setup.fields.tsv");
}

static s32 s_loadNavigationTables(const asset_entry_t *scenario,
                                  scenario_source_navigation_t *nav)
{
	char path[FS_MAXPATH + 1];
	char *text;
	u32 text_size;

	if (!scenario || !nav) {
		return 0;
	}

	memset(nav, 0, sizeof(*nav));

	if (s_activeGraphPathForScenario(scenario,
			s_ActiveScenarioGraphs.waypoints_path, path, sizeof(path))
			|| s_scenarioMemberPath(scenario, "navigation/waypoints.tsv",
			path, sizeof(path))) {
		text = s_loadOptionalText(path, &text_size);
		if (text) {
			if (!s_parseWaypointsTsv(text, nav)) {
				sysLogPrintf(LOG_ERROR,
					"SCENARIO.SOURCE: invalid public navigation table '%s'",
					path);
				free(text);
				s_freeNavigation(nav);
				return 0;
			}
			free(text);
		}
	}

	if (s_activeGraphPathForScenario(scenario,
			s_ActiveScenarioGraphs.waygroups_path, path, sizeof(path))
			|| s_scenarioMemberPath(scenario, "navigation/waygroups.tsv",
			path, sizeof(path))) {
		text = s_loadOptionalText(path, &text_size);
		if (text) {
			if (!s_parseWaygroupsTsv(text, nav)) {
				sysLogPrintf(LOG_ERROR,
					"SCENARIO.SOURCE: invalid public navigation table '%s'",
					path);
				free(text);
				s_freeNavigation(nav);
				return 0;
			}
			free(text);
		}
	}

	if (s_activeGraphPathForScenario(scenario,
			s_ActiveScenarioGraphs.covers_path, path, sizeof(path))
			|| s_scenarioMemberPath(scenario, "navigation/covers.tsv",
			path, sizeof(path))) {
		text = s_loadOptionalText(path, &text_size);
		if (text) {
			if (!s_parseCoversTsv(text, nav)) {
				sysLogPrintf(LOG_ERROR,
					"SCENARIO.SOURCE: invalid public navigation table '%s'",
					path);
				free(text);
				s_freeNavigation(nav);
				return 0;
			}
			free(text);
		}
	}

	return 1;
}

static s32 s_scenarioSetupFieldsPath(const asset_entry_t *scenario,
	char *out, size_t out_n)
{
	if (!scenario || !out || out_n == 0) {
		return 0;
	}

	out[0] = '\0';
	if (s_activeGraphPathForScenario(scenario,
			s_ActiveScenarioGraphs.setup_fields_path, out, out_n)) {
		return 1;
	}

	if (scenario->ext.scenario.setup_fields_file[0]) {
		strncpy(out, scenario->ext.scenario.setup_fields_file, out_n - 1);
		out[out_n - 1] = '\0';
		return out[0] != '\0';
	}

	return s_scenarioMemberPath(scenario, "setup.fields.tsv", out, out_n);
}

static s32 s_scenarioObjectsPath(const asset_entry_t *scenario,
	char *out, size_t out_n)
{
	if (!scenario || !out || out_n == 0) {
		return 0;
	}

	out[0] = '\0';
	if (s_activeGraphPathForScenario(scenario,
			s_ActiveScenarioGraphs.objects_path, out, out_n)) {
		return 1;
	}

	if (scenario->ext.scenario.objects_file[0]) {
		strncpy(out, scenario->ext.scenario.objects_file, out_n - 1);
		out[out_n - 1] = '\0';
		return out[0] != '\0';
	}

	return s_scenarioMemberPath(scenario, "objects.tsv", out, out_n);
}

static s32 s_scenarioSpawnsPath(const asset_entry_t *scenario,
	char *out, size_t out_n)
{
	if (!scenario || !out || out_n == 0) {
		return 0;
	}

	out[0] = '\0';
	if (s_activeGraphPathForScenario(scenario,
			s_ActiveScenarioGraphs.spawns_path, out, out_n)) {
		return 1;
	}

	if (scenario->ext.scenario.spawns_file[0]) {
		strncpy(out, scenario->ext.scenario.spawns_file, out_n - 1);
		out[out_n - 1] = '\0';
		return out[0] != '\0';
	}

	return s_scenarioMemberPath(scenario, "spawns.tsv", out, out_n);
}

static s32 s_scenarioAiListsPath(const asset_entry_t *scenario,
	char *out, size_t out_n)
{
	if (!scenario || !out || out_n == 0) {
		return 0;
	}

	out[0] = '\0';
	if (s_activeGraphPathForScenario(scenario,
			s_ActiveScenarioGraphs.ai_lists_path, out, out_n)) {
		return 1;
	}

	return s_scenarioMemberPath(scenario, "ai/ailists.tsv", out, out_n);
}

static s32 s_scenarioPathsPath(const asset_entry_t *scenario,
	char *out, size_t out_n)
{
	if (!scenario || !out || out_n == 0) {
		return 0;
	}

	out[0] = '\0';
	if (s_activeGraphPathForScenario(scenario,
			s_ActiveScenarioGraphs.paths_path, out, out_n)) {
		return 1;
	}

	return s_scenarioMemberPath(scenario, "navigation/paths.tsv",
		out, out_n);
}

u8 *scenarioSourceLoadSetupForStage(const catalog_stage_result_t *stage,
	s32 prefer_mp, s32 *out_size)
{
	const asset_entry_t *scenario;
	char setup_path[FS_MAXPATH + 1];
	char objects_path[FS_MAXPATH + 1];
	char spawns_path[FS_MAXPATH + 1];
	char ai_lists_path[FS_MAXPATH + 1];
	char paths_path[FS_MAXPATH + 1];
	u32 text_size;
	char *setup_text;
	char *objects_text;
	char *spawns_text;
	char *ai_text;
	char *paths_text;
	scenario_source_setup_table_t table;
	scenario_source_spawn_table_t spawn_table;
	scenario_source_ai_table_t ai_table;
	scenario_source_path_table_t path_table;
	u8 *setup_data;
	const char *stageid;

	if (out_size) {
		*out_size = 0;
	}

	scenario = s_findScenarioForStage(stage, prefer_mp);
	if (!scenario || !s_scenarioSetupFieldsPath(scenario, setup_path,
			sizeof(setup_path))) {
		return NULL;
	}

	text_size = 0;
	setup_text = s_loadOptionalText(setup_path, &text_size);
	if (!setup_text) {
		if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
			sysLogPrintf(LOG_ERROR,
				"SCENARIO.SOURCE: missing public setup.fields.tsv for '%s' at %s",
				scenario->id, setup_path);
		}
		return NULL;
	}

	memset(&table, 0, sizeof(table));
	memset(&spawn_table, 0, sizeof(spawn_table));
	memset(&ai_table, 0, sizeof(ai_table));
	memset(&path_table, 0, sizeof(path_table));
	if (!s_setupParseFieldsTsv(setup_text, &table)) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: invalid public setup.fields.tsv for '%s': %s",
			scenario->id, table.error[0] ? table.error : "parse failed");
		free(setup_text);
		s_setupFreeTable(&table);
		return NULL;
	}
	free(setup_text);

	objects_text = NULL;
	if (s_scenarioObjectsPath(scenario, objects_path, sizeof(objects_path))) {
		objects_text = s_loadOptionalText(objects_path, &text_size);
	}
	if (objects_text) {
		if (!s_setupApplyObjectsSummary(objects_text, &table)) {
			sysLogPrintf(LOG_ERROR,
				"SCENARIO.SOURCE: invalid public objects.tsv for '%s': %s",
				scenario->id, table.error[0] ? table.error : "parse failed");
			free(objects_text);
			s_setupFreeTable(&table);
			s_freePathTable(&path_table);
			s_aiFreeTable(&ai_table);
			return NULL;
		}
		free(objects_text);
	}

	spawns_text = NULL;
	if (s_scenarioSpawnsPath(scenario, spawns_path, sizeof(spawns_path))) {
		spawns_text = s_loadOptionalText(spawns_path, &text_size);
	}
	if (!spawns_text) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: missing public spawns.tsv for '%s'",
			scenario->id);
		s_setupFreeTable(&table);
		s_spawnFreeTable(&spawn_table);
		s_freePathTable(&path_table);
		s_aiFreeTable(&ai_table);
		return NULL;
	}
	if (!s_loadSpawnSourceRows(spawns_text, &spawn_table)) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: invalid public spawns.tsv for '%s': %s",
			scenario->id, spawn_table.error[0] ? spawn_table.error : "parse failed");
		free(spawns_text);
		s_setupFreeTable(&table);
		s_spawnFreeTable(&spawn_table);
		s_freePathTable(&path_table);
		s_aiFreeTable(&ai_table);
		return NULL;
	}
	free(spawns_text);

	paths_text = NULL;
	if (s_scenarioPathsPath(scenario, paths_path, sizeof(paths_path))) {
		paths_text = s_loadOptionalText(paths_path, &text_size);
	}
	if (!paths_text) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: missing public navigation/paths.tsv for '%s'",
			scenario->id);
		s_setupFreeTable(&table);
		s_spawnFreeTable(&spawn_table);
		s_freePathTable(&path_table);
		s_aiFreeTable(&ai_table);
		return NULL;
	}
	if (!s_loadPathSourceRows(paths_text, &path_table)) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: invalid public navigation/paths.tsv for '%s': %s",
			scenario->id, path_table.error[0]
				? path_table.error : "parse failed");
		free(paths_text);
		s_setupFreeTable(&table);
		s_spawnFreeTable(&spawn_table);
		s_freePathTable(&path_table);
		s_aiFreeTable(&ai_table);
		return NULL;
	}
	free(paths_text);

	ai_text = NULL;
	if (s_scenarioAiListsPath(scenario, ai_lists_path, sizeof(ai_lists_path))) {
		ai_text = s_loadOptionalText(ai_lists_path, &text_size);
	}
	if (!ai_text) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: missing public ai/ailists.tsv for '%s'",
			scenario->id);
		s_setupFreeTable(&table);
		s_spawnFreeTable(&spawn_table);
		s_freePathTable(&path_table);
		s_aiFreeTable(&ai_table);
		return NULL;
	}
	if (!s_loadAiListSourceRows(ai_text, &ai_table)) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: invalid public ai/ailists.tsv for '%s': %s",
			scenario->id, ai_table.error[0] ? ai_table.error : "parse failed");
		free(ai_text);
		s_setupFreeTable(&table);
		s_spawnFreeTable(&spawn_table);
		s_freePathTable(&path_table);
		s_aiFreeTable(&ai_table);
		return NULL;
	}
	free(ai_text);

	if (table.count > 1) {
		qsort(table.records, (size_t)table.count, sizeof(table.records[0]),
			s_setupCompareRecords);
	}
	if (!s_setupCollectBehaviorLinkSource(scenario, setup_path, &table)) {
		s_setupFreeTable(&table);
		s_spawnFreeTable(&spawn_table);
		s_freePathTable(&path_table);
		s_aiFreeTable(&ai_table);
		return NULL;
	}

	setup_data = s_setupBuildStageBlock(&table, &spawn_table, &ai_table,
		&path_table, scenario->id, out_size);
	stageid = (stage && stage->entry && stage->entry->id[0])
		? stage->entry->id : "?";
	if (setup_data) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.SOURCE: compiled setup.fields.tsv '%s', spawns.tsv '%s', navigation/paths.tsv '%s', and ai/ailists.tsv '%s' for stage '%s' as %d setup records, %d spawns, %d paths, %d AI lists (%d bytes)",
			setup_path, spawns_path, paths_path, ai_lists_path, stageid,
			table.count, spawn_table.count, path_table.count, ai_table.count,
			out_size ? *out_size : 0);
	}

	s_setupFreeTable(&table);
	s_spawnFreeTable(&spawn_table);
	s_freePathTable(&path_table);
	s_aiFreeTable(&ai_table);
	return setup_data;
}

static u32 s_sourceTileStride(void)
{
	return (u32)offsetof(struct geotilef, vertices)
		+ 3u * (u32)sizeof(struct coord);
}

static void s_sourceTileSetBounds(struct geotilef *tile)
{
	if (!tile) {
		return;
	}

	for (s32 axis = 0; axis < 3; axis++) {
		s32 min_index = 0;
		s32 max_index = 0;
		for (s32 i = 1; i < 3; i++) {
			if (tile->vertices[i].f[axis] <
					tile->vertices[min_index].f[axis]) {
				min_index = i;
			}
			if (tile->vertices[i].f[axis] >
					tile->vertices[max_index].f[axis]) {
				max_index = i;
			}
		}
		tile->min[axis] = (u8)min_index;
		tile->max[axis] = (u8)max_index;
	}
}

u8 *scenarioSourceLoadTilesForStage(const catalog_stage_result_t *stage,
	s32 prefer_mp, s32 *out_size, s32 *out_rooms, s32 *out_tiles)
{
	const asset_entry_t *scenario;
	struct colmesh *mesh;
	s32 max_room = 0;
	s32 source_tiles = 0;
	u32 header_size;
	u32 tile_stride = s_sourceTileStride();
	u32 total_size;
	u8 *data;
	u32 *u32data;
	u32 *rooms;
	u32 cursor;
	const char *stageid;
	const char *source_path;

	if (out_size) {
		*out_size = 0;
	}
	if (out_rooms) {
		*out_rooms = 0;
	}
	if (out_tiles) {
		*out_tiles = 0;
	}

	scenario = s_findScenarioForStage(stage, prefer_mp);
	if (!scenario || !scenario->id[0]) {
		return NULL;
	}

	if (!catalogLoadTypedAsset(ASSET_SCENARIO, scenario->id)) {
		if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
			sysLogPrintf(LOG_ERROR,
				"SCENARIO.SOURCE: failed to activate scene source for tiles '%s'",
				scenario->id);
		}
		return NULL;
	}

	mesh = catalogGetLoadedColmesh(scenario->id);
	if (!mesh || mesh->numtris <= 0) {
		if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
			sysLogPrintf(LOG_ERROR,
				"SCENARIO.SOURCE: no scene colmesh available for tile cache '%s'",
				scenario->id);
		}
		return NULL;
	}

	for (s32 i = 0; i < mesh->numtris; i++) {
		s32 room = mesh->tris[i].roomnum;
		if (room > 0) {
			if (room > max_room) {
				max_room = room;
			}
			source_tiles++;
		}
	}

	if (max_room <= 0 || source_tiles <= 0) {
		if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
			sysLogPrintf(LOG_ERROR,
				"SCENARIO.SOURCE: scene source '%s' has no room-tagged triangles for tile cache",
				scenario->id);
		}
		return NULL;
	}

	header_size = (u32)(max_room + 3) * (u32)sizeof(u32);
	if (source_tiles > 0
			&& (u32)source_tiles > (0xffffffffu - header_size) / tile_stride) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: tile cache too large for '%s' tiles=%d",
			scenario->id, source_tiles);
		return NULL;
	}

	total_size = header_size + (u32)source_tiles * tile_stride;
	data = mempAlloc(total_size, MEMPOOL_STAGE);
	if (!data) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: tile cache allocation failed for '%s' bytes=%u",
			scenario->id, (unsigned)total_size);
		return NULL;
	}
	memset(data, 0, total_size);

	u32data = (u32 *)data;
	*u32data = (u32)(max_room + 1);
	rooms = u32data + 1;
	cursor = header_size;

	for (s32 room = 0; room <= max_room; room++) {
		rooms[room] = cursor;
		for (s32 i = 0; i < mesh->numtris; i++) {
			const struct meshtri *src = &mesh->tris[i];
			struct geotilef *tile;
			if (src->roomnum != room) {
				continue;
			}
			tile = (struct geotilef *)(data + cursor);
			tile->header.type = GEOTYPE_TILE_F;
			tile->header.numvertices = 3;
			tile->header.flags = src->flags;
			tile->floortype = 0;
			tile->floorcol = 0;
			tile->vertices[0] = src->v0;
			tile->vertices[1] = src->v1;
			tile->vertices[2] = src->v2;
			s_sourceTileSetBounds(tile);
			cursor += tile_stride;
		}
	}
	rooms[max_room + 1] = cursor;

	stageid = (stage && stage->entry && stage->entry->id[0])
		? stage->entry->id : "?";
	source_path = scenario->ext.scenario.collision_file[0]
		? scenario->ext.scenario.collision_file
		: scenario->ext.scenario.scene_file;
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.SOURCE: compiled scene tiles '%s' for stage '%s' as %d rooms, %d tiles (%u bytes) source=%s",
		scenario->id, stageid, max_room + 1, source_tiles,
		(unsigned)cursor,
		source_path && source_path[0] ? source_path : "(none)");

	if (out_size) {
		*out_size = (s32)cursor;
	}
	if (out_rooms) {
		*out_rooms = max_room + 1;
	}
	if (out_tiles) {
		*out_tiles = source_tiles;
	}
	return data;
}

u8 *scenarioSourceLoadPadsForStage(const catalog_stage_result_t *stage,
	s32 prefer_mp, s32 *out_size)
{
	const asset_entry_t *scenario;
	char pads_path[FS_MAXPATH + 1];
	u32 text_size;
	char *text;
	scenario_source_pad_row_t *rows;
	s32 row_count;
	scenario_source_navigation_t nav;
	u8 *padfile;
	s32 padfile_size;
	s32 waypoint_count;
	s32 waygroup_count;
	s32 cover_count;
	const char *stageid;

	if (out_size) {
		*out_size = 0;
	}

	scenario = s_findScenarioForStage(stage, prefer_mp);
	if (!scenario || !s_scenarioPadsPath(scenario, pads_path, sizeof(pads_path))) {
		return NULL;
	}

	text_size = 0;
	text = (char *)fsFileLoad(pads_path, &text_size);
	if (!text || text_size == 0) {
		if (text) {
			free(text);
		}
		if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
			sysLogPrintf(LOG_ERROR,
				"SCENARIO.SOURCE: missing public pads.tsv for '%s' at %s",
				scenario->id, pads_path);
		}
		return NULL;
	}

	rows = s_parsePadsTsv(text, &row_count);
	free(text);

	if (!rows || row_count <= 0) {
		if (rows) {
			free(rows);
		}
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: no usable pad rows in public source '%s'",
			pads_path);
		return NULL;
	}

	if (!s_loadNavigationTables(scenario, &nav)) {
		if (rows) {
			free(rows);
		}
		return NULL;
	}

	padfile_size = 0;
	padfile = s_buildPadfile(rows, row_count, &nav, &padfile_size);
	waypoint_count = nav.waypoint_count;
	waygroup_count = nav.waygroup_count;
	cover_count = nav.cover_count;
	free(rows);
	s_freeNavigation(&nav);

	if (!padfile) {
		return NULL;
	}

	stageid = (stage && stage->entry && stage->entry->id[0])
		? stage->entry->id : "?";
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.SOURCE: compiled pads.tsv '%s' for stage '%s' as %d pads, %d waypoints, %d waygroups, %d covers (%d bytes)",
		pads_path, stageid, row_count,
		waypoint_count, waygroup_count, cover_count, padfile_size);

	if (out_size) {
		*out_size = padfile_size;
	}
	return padfile;
}
