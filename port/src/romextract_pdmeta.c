/**
 * romextract_pdmeta.c -- c3843 table-backed metadata family emitters.
 *
 * These families already have deterministic base-game source records in
 * catalog/static tables. The extractor writes those records as clean typed
 * archives and binds the matching base catalog rows to the public source files
 * so runtime activation uses the same FileProvider path as modded content.
 */

#include <PR/ultratypes.h>
#include <SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arenadata_authored.h"
#include "asset_archive_writer.h"
#include "assetcatalog.h"
#include "boot_pool.h"
#include "boot_progress.h"
#include "constants.h"
#include "fs.h"
#include "game/stagetable.h"
#include "modarchive.h"
#include "romextract_pd.h"
#include "system.h"

#define PDMETA_FAST_CACHE_KIND "pdmeta_table_backed_v10_pdscenario_v92_objectives_spawns_volumes_pads_paths_ai_lists_json_navtables_json"
#define PDMETA_SCENARIO_DEP_CACHE_KIND \
	"pdscenario_scene_glb_clean_public_v95_standalone_backfill_collision_obj_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound_color0_alphamask_quip_shuffle_graph_portals_json_objects_json_setup_fields_json_ai_lists_json_navhashes_objectives_spawns_volumes_pads_paths_json_navtables_json"

#define PDMETA_MAX_MISSION_OBJECTIVE_ROWS 512

typedef struct pdmeta_textbuf {
	char *data;
	size_t len;
	size_t cap;
} pdmeta_textbuf_t;

typedef struct pdmeta_objective_row {
	char objective_id[64];
	char kind[64];
	char text_token[64];
	char difficulty_mask[32];
	char scenario_node[96];
	char operand_kind[48];
	char target_ref[32];
	char target_record_ref[32];
	char pad_ref[32];
	char state_ref[64];
	char match_value[32];
	char initial_status[32];
} pdmeta_objective_row_t;

static const u8 k_Transparent1x1Png[] = {
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
	0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
	0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
	0x89, 0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41,
	0x54, 0x78, 0xda, 0x63, 0x60, 0x00, 0x02, 0x00,
	0x00, 0x05, 0x00, 0x01, 0xe9, 0xfa, 0xdc, 0xd8,
	0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
	0xae, 0x42, 0x60, 0x82,
};

static void s_idToFilenameSlug(const char *id, char *out, size_t out_n)
{
	if (!out || out_n == 0) return;
	out[0] = '\0';
	if (!id) return;
	size_t j = 0;
	for (size_t i = 0; id[i] && j + 1 < out_n; i++) {
		char c = id[i];
		out[j++] = (c == ':' || c == '/' || c == '\\') ? '_' : c;
	}
	out[j] = '\0';
}

static void s_archiveRelPath(const char *dir, const char *id,
	const char *ext, char *out, size_t out_n)
{
	if (!out || out_n == 0) return;
	out[0] = '\0';
	char slug[CATALOG_ID_LEN];
	s_idToFilenameSlug(id, slug, sizeof(slug));
	snprintf(out, out_n, "%s/%s%s", dir, slug, ext);
}

static s32 s_existingArchiveHasEntry(const char *relpath, const char *entry)
{
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) return 0;
	mod_archive_t *arc = modArchiveOpen(full);
	if (!arc) return 0;
	s32 has_entry = modArchiveFindEntry(arc, entry) >= 0;
	modArchiveClose(arc);
	return has_entry;
}

static s32 s_existingArchiveEntryContains(const char *relpath,
	const char *entry, const char *needle)
{
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0] || !entry || !needle) return 0;
	mod_archive_t *arc = modArchiveOpen(full);
	if (!arc) return 0;
	s32 idx = modArchiveFindEntry(arc, entry);
	s32 found = 0;
	if (idx >= 0) {
		u32 size = 0;
		char *bytes = (char *)modArchiveExtractAlloc(arc, idx, &size);
		if (bytes && size > 0) {
			char *text = malloc((size_t)size + 1u);
			if (text) {
				memcpy(text, bytes, size);
				text[size] = '\0';
				found = strstr(text, needle) != NULL;
				free(text);
			}
			free(bytes);
		}
	}
	modArchiveClose(arc);
	return found;
}

static s32 s_openWriter(const char *relpath, const char *family,
	const char *catalog_id, const char *tool, const char *source,
	s32 source_index, mod_archive_writer_t **out_aw,
	asset_archive_writer_t *out_writer)
{
	if (out_aw) *out_aw = NULL;
	if (!relpath || !family || !catalog_id || !out_aw || !out_writer) {
		return -1;
	}

	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) {
		sysLoudFailf("EXTRACT.PDMETA",
			"fsFullPath failed for \"%s\"", relpath);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(full);
	if (!aw) {
		sysLoudFailf("EXTRACT.PDMETA",
			"modArchiveBegin failed for \"%s\"", full);
		return -1;
	}

	if (assetArchiveWriterInit(out_writer, aw, family, catalog_id) !=
			MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDMETA",
			"assetArchiveWriterInit failed for \"%s\"", full);
		modArchiveAbort(aw);
		return -1;
	}

	assetArchiveWriterSetProvenance(out_writer, tool, source,
		source_index, catalog_id);
	*out_aw = aw;
	return 0;
}

static s32 s_finishWriter(mod_archive_writer_t *aw,
	asset_archive_writer_t *writer, const char *relpath)
{
	if (assetArchiveWriterFinishMetadata(writer) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDMETA",
			"assetArchiveWriterFinishMetadata failed for \"%s\"", relpath);
		modArchiveAbort(aw);
		return -1;
	}
	if (modArchiveFinish(aw) != 0) {
		sysLoudFailf("EXTRACT.PDMETA",
			"modArchiveFinish failed for \"%s\"", relpath);
		return -1;
	}
	return 1;
}

static void s_copyString(char *out, size_t out_n, const char *value)
{
	if (!out || out_n == 0) return;
	out[0] = '\0';
	if (!value || !value[0]) return;
	strncpy(out, value, out_n - 1);
	out[out_n - 1] = '\0';
}

static s32 s_textbufReserve(pdmeta_textbuf_t *buf, size_t extra)
{
	size_t need;
	size_t cap;
	char *grown;

	if (!buf) return -1;
	need = buf->len + extra + 1u;
	if (need <= buf->cap) return 0;

	cap = buf->cap ? buf->cap : 4096u;
	while (cap < need) {
		cap *= 2u;
	}

	grown = (char *)realloc(buf->data, cap);
	if (!grown) return -1;
	buf->data = grown;
	buf->cap = cap;
	return 0;
}

static s32 s_textbufAppend(pdmeta_textbuf_t *buf, const char *text)
{
	size_t len;

	if (!buf || !text) return -1;
	len = strlen(text);
	if (s_textbufReserve(buf, len) != 0) return -1;
	memcpy(buf->data + buf->len, text, len);
	buf->len += len;
	buf->data[buf->len] = '\0';
	return 0;
}

static s32 s_textbufAppendf(pdmeta_textbuf_t *buf, const char *fmt, ...)
{
	va_list ap;
	va_list copy;
	int len;

	if (!buf || !fmt) return -1;

	va_start(ap, fmt);
	va_copy(copy, ap);
	len = vsnprintf(NULL, 0, fmt, copy);
	va_end(copy);
	if (len < 0) {
		va_end(ap);
		return -1;
	}

	if (s_textbufReserve(buf, (size_t)len) != 0) {
		va_end(ap);
		return -1;
	}

	vsnprintf(buf->data + buf->len, buf->cap - buf->len, fmt, ap);
	va_end(ap);
	buf->len += (size_t)len;
	return 0;
}

static s32 s_textbufAppendJsonString(pdmeta_textbuf_t *buf, const char *value)
{
	const unsigned char *p;

	if (s_textbufAppend(buf, "\"") != 0) {
		return -1;
	}

	if (!value) {
		value = "";
	}

	for (p = (const unsigned char *)value; *p; p++) {
		char tmp[8];
		switch (*p) {
		case '\\':
			if (s_textbufAppend(buf, "\\\\") != 0) return -1;
			break;
		case '"':
			if (s_textbufAppend(buf, "\\\"") != 0) return -1;
			break;
		case '\b':
			if (s_textbufAppend(buf, "\\b") != 0) return -1;
			break;
		case '\f':
			if (s_textbufAppend(buf, "\\f") != 0) return -1;
			break;
		case '\n':
			if (s_textbufAppend(buf, "\\n") != 0) return -1;
			break;
		case '\r':
			if (s_textbufAppend(buf, "\\r") != 0) return -1;
			break;
		case '\t':
			if (s_textbufAppend(buf, "\\t") != 0) return -1;
			break;
		default:
			if (*p < 0x20) {
				snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned)*p);
				if (s_textbufAppend(buf, tmp) != 0) return -1;
			} else {
				tmp[0] = (char)*p;
				tmp[1] = '\0';
				if (s_textbufAppend(buf, tmp) != 0) return -1;
			}
			break;
		}
	}

	return s_textbufAppend(buf, "\"");
}

static void s_textbufFree(pdmeta_textbuf_t *buf)
{
	if (!buf) return;
	free(buf->data);
	buf->data = NULL;
	buf->len = 0;
	buf->cap = 0;
}

static char *s_archiveEntryTextAlloc(const char *relpath,
	const char *entry, u32 *out_size)
{
	char full_buf[FS_MAXPATH + 1];
	const char *full;
	mod_archive_t *arc;
	s32 idx;
	u32 size = 0;
	char *bytes;
	char *text;

	if (out_size) *out_size = 0;
	if (!relpath || !entry) return NULL;

	full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) return NULL;

	arc = modArchiveOpen(full);
	if (!arc) return NULL;

	idx = modArchiveFindEntry(arc, entry);
	if (idx < 0) {
		modArchiveClose(arc);
		return NULL;
	}

	bytes = (char *)modArchiveExtractAlloc(arc, idx, &size);
	modArchiveClose(arc);
	if (!bytes || size == 0) {
		free(bytes);
		return NULL;
	}

	text = (char *)malloc((size_t)size + 1u);
	if (!text) {
		free(bytes);
		return NULL;
	}
	memcpy(text, bytes, size);
	text[size] = '\0';
	free(bytes);
	if (out_size) *out_size = size;
	return text;
}

static char *s_nextTsvLine(char **cursor)
{
	char *start;
	char *p;

	if (!cursor || !*cursor) return NULL;
	start = *cursor;
	p = start;
	while (*p && *p != '\n') p++;
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

static char *s_nextTsvField(char **cursor)
{
	char *start;
	char *p;

	if (!cursor || !*cursor) return NULL;
	start = *cursor;
	p = start;
	while (*p && *p != '\t') p++;
	if (*p == '\t') {
		*p++ = '\0';
		*cursor = p;
	} else {
		*cursor = NULL;
	}
	return start;
}

static const char *s_jsonSkipWs(const char *p, const char *end)
{
	while (p && p < end && (*p == ' ' || *p == '\t' ||
			*p == '\r' || *p == '\n')) {
		p++;
	}
	return p;
}

static const char *s_jsonFindObjectEnd(const char *start)
{
	const char *p;
	s32 depth = 0;
	s32 in_string = 0;
	s32 escape = 0;

	if (!start || *start != '{') return NULL;
	for (p = start; *p; p++) {
		char c = *p;
		if (in_string) {
			if (escape) {
				escape = 0;
			} else if (c == '\\') {
				escape = 1;
			} else if (c == '"') {
				in_string = 0;
			}
			continue;
		}
		if (c == '"') {
			in_string = 1;
		} else if (c == '{') {
			depth++;
		} else if (c == '}') {
			depth--;
			if (depth == 0) return p + 1;
			if (depth < 0) return NULL;
		}
	}
	return NULL;
}

static const char *s_jsonFindKey(const char *start, const char *end,
	const char *key)
{
	char quoted[96];
	const char *p;
	size_t key_len;

	if (!start || !end || !key) return NULL;
	key_len = strlen(key);
	if (key_len + 3 > sizeof(quoted)) return NULL;
	snprintf(quoted, sizeof(quoted), "\"%s\"", key);
	for (p = start; p < end; p++) {
		size_t remaining = (size_t)(end - p);
		size_t quoted_len = strlen(quoted);
		if (remaining < quoted_len) return NULL;
		if (memcmp(p, quoted, quoted_len) == 0) return p + quoted_len;
	}
	return NULL;
}

static s32 s_jsonObjectStringValue(const char *object_start,
	const char *object_end, const char *key, char *out, size_t out_n)
{
	const char *p;
	char *dst;
	size_t remaining;
	s32 escape = 0;

	if (!out || out_n == 0) return -1;
	out[0] = '\0';
	if (!object_start || !object_end || object_start >= object_end || !key) {
		return -1;
	}

	p = s_jsonFindKey(object_start, object_end, key);
	if (!p) return 0;
	p = s_jsonSkipWs(p, object_end);
	if (!p || p >= object_end || *p != ':') return -1;
	p++;
	p = s_jsonSkipWs(p, object_end);
	if (!p || p >= object_end || *p != '"') return -1;
	p++;

	dst = out;
	remaining = out_n - 1;
	while (p < object_end && *p) {
		char c = *p++;
		if (escape) {
			switch (c) {
			case 'n': c = '\n'; break;
			case 'r': c = '\r'; break;
			case 't': c = '\t'; break;
			case 'b': c = '\b'; break;
			case 'f': c = '\f'; break;
			default: break;
			}
			escape = 0;
		} else if (c == '\\') {
			escape = 1;
			continue;
		} else if (c == '"') {
			*dst = '\0';
			return 1;
		}
		if (remaining > 0) {
			*dst++ = c;
			remaining--;
		}
	}
	return -1;
}

static s32 s_loadScenarioObjectiveRows(const char *scenario_rel,
	pdmeta_objective_row_t *rows, s32 max_rows)
{
	char *text;
	const char *rows_key;
	const char *array_start;
	const char *array_end;
	const char *cursor;
	s32 count = 0;

	if (!scenario_rel || !rows || max_rows <= 0) return -1;

	text = s_archiveEntryTextAlloc(scenario_rel, "objectives.json", NULL);
	if (!text) return -1;
	if (!strstr(text, "\"schema\": \"pd2.scenario.objectives.v1\"")) {
		free(text);
		return -1;
	}
	rows_key = strstr(text, "\"rows\"");
	if (!rows_key) {
		free(text);
		return -1;
	}
	array_start = strchr(rows_key, '[');
	if (!array_start) {
		free(text);
		return -1;
	}
	array_end = strchr(array_start, ']');
	if (!array_end) {
		free(text);
		return -1;
	}

	cursor = array_start + 1;
	while (cursor && cursor < array_end) {
		const char *object_start = strchr(cursor, '{');
		const char *object_end;

		if (!object_start || object_start >= array_end) break;
		object_end = s_jsonFindObjectEnd(object_start);
		if (!object_end || object_end > array_end + 1) {
			free(text);
			return -1;
		}
		if (count >= max_rows) {
			free(text);
			return -1;
		}
		memset(&rows[count], 0, sizeof(rows[count]));
		if (s_jsonObjectStringValue(object_start, object_end,
				"objective_id", rows[count].objective_id,
				sizeof(rows[count].objective_id)) <= 0 ||
				s_jsonObjectStringValue(object_start, object_end,
					"kind", rows[count].kind,
					sizeof(rows[count].kind)) <= 0) {
			cursor = object_end;
			continue;
		}
		(void)s_jsonObjectStringValue(object_start, object_end,
			"text_token", rows[count].text_token,
			sizeof(rows[count].text_token));
		(void)s_jsonObjectStringValue(object_start, object_end,
			"difficulty_mask", rows[count].difficulty_mask,
			sizeof(rows[count].difficulty_mask));
		(void)s_jsonObjectStringValue(object_start, object_end,
			"graph_node", rows[count].scenario_node,
			sizeof(rows[count].scenario_node));
		(void)s_jsonObjectStringValue(object_start, object_end,
			"operand_kind", rows[count].operand_kind,
			sizeof(rows[count].operand_kind));
		(void)s_jsonObjectStringValue(object_start, object_end,
			"target_ref", rows[count].target_ref,
			sizeof(rows[count].target_ref));
		(void)s_jsonObjectStringValue(object_start, object_end,
			"target_record_ref", rows[count].target_record_ref,
			sizeof(rows[count].target_record_ref));
		(void)s_jsonObjectStringValue(object_start, object_end,
			"pad_ref", rows[count].pad_ref,
			sizeof(rows[count].pad_ref));
		(void)s_jsonObjectStringValue(object_start, object_end,
			"state_ref", rows[count].state_ref,
			sizeof(rows[count].state_ref));
		(void)s_jsonObjectStringValue(object_start, object_end,
			"match_value", rows[count].match_value,
			sizeof(rows[count].match_value));
		(void)s_jsonObjectStringValue(object_start, object_end,
			"initial_status", rows[count].initial_status,
			sizeof(rows[count].initial_status));
		count++;
		cursor = object_end;
	}

	free(text);
	return count;
}

static void s_missionObjectiveNodeId(const pdmeta_objective_row_t *row,
	s32 row_index, char *out, size_t out_n)
{
	const char *suffix;

	if (!out || out_n == 0) return;
	out[0] = '\0';
	if (!row) return;

	suffix = strrchr(row->scenario_node, '.');
	if (suffix && suffix[1]) {
		snprintf(out, out_n, "%s.%s",
			strcmp(row->kind, "objective") == 0
				? "mission.objective"
				: "mission.objective_step",
			suffix + 1);
	} else {
		snprintf(out, out_n, "%s.%04d",
			strcmp(row->kind, "objective") == 0
				? "mission.objective"
				: "mission.objective_step",
			row_index);
	}
}

static s32 s_buildMissionObjectivesJson(const char *scenario_file,
	const pdmeta_objective_row_t *rows, s32 row_count,
	pdmeta_textbuf_t *out)
{
	s32 i;

	if (!scenario_file || !rows || row_count <= 0 || !out) return -1;

	if (s_textbufAppend(out,
			"{\n"
			"  \"schema\": \"pd2.mission.objectives.v1\",\n"
			"  \"rows\": [\n") != 0) {
		return -1;
	}

	for (i = 0; i < row_count; i++) {
		char node_id[96];
		char scenario_source[192];
		s_missionObjectiveNodeId(&rows[i], i, node_id, sizeof(node_id));
		snprintf(scenario_source, sizeof(scenario_source),
			"dependencies/assets/scenarios/%s.pdscenario::objectives.json#%s",
			scenario_file, rows[i].objective_id);

		if (s_textbufAppend(out, i ? ",\n    {\n" : "    {\n") != 0 ||
				s_textbufAppend(out, "      \"objective_id\": ") != 0 ||
				s_textbufAppendJsonString(out, rows[i].objective_id) != 0 ||
				s_textbufAppend(out, ",\n      \"kind\": ") != 0 ||
				s_textbufAppendJsonString(out, rows[i].kind) != 0 ||
				s_textbufAppend(out, ",\n      \"text_token\": ") != 0 ||
				s_textbufAppendJsonString(out, rows[i].text_token) != 0 ||
				s_textbufAppend(out, ",\n      \"difficulty_mask\": ") != 0 ||
				s_textbufAppendJsonString(out, rows[i].difficulty_mask) != 0 ||
				s_textbufAppend(out, ",\n      \"graph_node\": ") != 0 ||
				s_textbufAppendJsonString(out, node_id) != 0 ||
				s_textbufAppend(out, ",\n      \"scenario_source\": ") != 0 ||
				s_textbufAppendJsonString(out, scenario_source) != 0 ||
				s_textbufAppend(out, ",\n      \"operand_kind\": ") != 0 ||
				s_textbufAppendJsonString(out, rows[i].operand_kind) != 0 ||
				s_textbufAppend(out, ",\n      \"target_ref\": ") != 0 ||
				s_textbufAppendJsonString(out, rows[i].target_ref) != 0 ||
				s_textbufAppend(out, ",\n      \"target_record_ref\": ") != 0 ||
				s_textbufAppendJsonString(out, rows[i].target_record_ref) != 0 ||
				s_textbufAppend(out, ",\n      \"pad_ref\": ") != 0 ||
				s_textbufAppendJsonString(out, rows[i].pad_ref) != 0 ||
				s_textbufAppend(out, ",\n      \"state_ref\": ") != 0 ||
				s_textbufAppendJsonString(out, rows[i].state_ref) != 0 ||
				s_textbufAppend(out, ",\n      \"match_value\": ") != 0 ||
				s_textbufAppendJsonString(out, rows[i].match_value) != 0 ||
				s_textbufAppend(out, ",\n      \"initial_status\": ") != 0 ||
				s_textbufAppendJsonString(out, rows[i].initial_status) != 0 ||
				s_textbufAppend(out, "\n    }") != 0) {
			return -1;
		}
	}

	return s_textbufAppend(out, "\n  ]\n}\n");
}

static s32 s_buildMissionGraph(const char *mission_id,
	const char *scenario_id, const char *scenario_file, const char *slug,
	const pdmeta_objective_row_t *rows, s32 row_count,
	pdmeta_textbuf_t *out)
{
	s32 i;
	char current_objective[96] = "";

	if (!mission_id || !scenario_id || !scenario_file || !slug ||
			!rows || row_count <= 0 || !out) {
		return -1;
	}

	if (s_textbufAppendf(out,
			"{\n"
			"  \"schema\": \"pd2.mission.graph.v1\",\n"
			"  \"catalog_id\": \"%s\",\n"
			"  \"scenario_ref\": \"%s\",\n"
			"  \"runtime\": { \"parity_backend\": \"og.mission.%s\" },\n"
			"  \"nodes\": [\n"
			"    { \"id\": \"mission.load\", \"kind\": \"event.mission.load\", \"scenario\": \"%s\" },\n"
			"    { \"id\": \"mission.phase.load\", \"kind\": \"mission.phase.source\", \"phase\": \"load\" },\n"
			"    { \"id\": \"mission.phase.active\", \"kind\": \"mission.phase.source\", \"phase\": \"active\" },\n"
			"    { \"id\": \"mission.phase.complete\", \"kind\": \"mission.phase.source\", \"phase\": \"complete\" },\n"
			"    { \"id\": \"mission.phase.failed\", \"kind\": \"mission.phase.source\", \"phase\": \"failed\" },\n"
			"    { \"id\": \"mission.phase.end\", \"kind\": \"mission.phase.source\", \"phase\": \"end\" },\n"
			"    { \"id\": \"mission.objectives\", \"kind\": \"mission.objectives.source\", \"file\": \"objectives.json\", \"scenario_table\": \"dependencies/assets/scenarios/%s.pdscenario::objectives.json\" },\n",
			mission_id, scenario_id, slug, scenario_id, scenario_file) != 0) {
		return -1;
	}

	for (i = 0; i < row_count; i++) {
		char node_id[96];
		const char *node_kind;

		s_missionObjectiveNodeId(&rows[i], i, node_id, sizeof(node_id));
		node_kind = strcmp(rows[i].kind, "objective") == 0
			? "mission.objective.source"
			: "mission.objective.criteria.source";
		if (s_textbufAppendf(out,
				"    { \"id\": \"%s\", \"kind\": \"%s\", \"source_row\": \"%s\", \"scenario_node\": \"%s\", \"text_token\": \"%s\", \"difficulty_mask\": \"%s\", \"criteria\": \"%s\", \"operand_kind\": \"%s\", \"target_ref\": \"%s\", \"target_record_ref\": \"%s\", \"pad_ref\": \"%s\", \"state_ref\": \"%s\", \"match_value\": \"%s\", \"initial_status\": \"%s\" },\n",
				node_id, node_kind, rows[i].objective_id,
				rows[i].scenario_node, rows[i].text_token,
				rows[i].difficulty_mask, rows[i].kind,
				rows[i].operand_kind, rows[i].target_ref,
				rows[i].target_record_ref, rows[i].pad_ref,
				rows[i].state_ref, rows[i].match_value,
				rows[i].initial_status) != 0) {
			return -1;
		}
	}

	if (s_textbufAppendf(out,
			"    { \"id\": \"mission.parity_backend\", \"kind\": \"mission.behavior.parity_backend\", \"module\": \"og.mission.%s\" }\n"
			"  ],\n"
			"  \"edges\": [\n"
			"    { \"from\": \"mission.load\", \"to\": \"mission.phase.load\" },\n"
			"    { \"from\": \"mission.phase.load\", \"to\": \"mission.phase.active\" },\n"
			"    { \"from\": \"mission.phase.active\", \"to\": \"mission.objectives\" }",
			slug) != 0) {
		return -1;
	}

	for (i = 0; i < row_count; i++) {
		char node_id[96];
		s_missionObjectiveNodeId(&rows[i], i, node_id, sizeof(node_id));
		if (strcmp(rows[i].kind, "objective") == 0) {
			s_copyString(current_objective, sizeof(current_objective),
				node_id);
			if (s_textbufAppendf(out,
					",\n    { \"from\": \"mission.objectives\", \"to\": \"%s\" }",
					node_id) != 0) {
				return -1;
			}
		} else if (current_objective[0]) {
			if (s_textbufAppendf(out,
					",\n    { \"from\": \"%s\", \"to\": \"%s\" }",
					current_objective, node_id) != 0) {
				return -1;
			}
		} else if (s_textbufAppendf(out,
				",\n    { \"from\": \"mission.objectives\", \"to\": \"%s\" }",
				node_id) != 0) {
			return -1;
		}
	}

	if (s_textbufAppend(out,
			",\n    { \"from\": \"mission.objectives\", \"to\": \"mission.phase.complete\" },\n"
			"    { \"from\": \"mission.objectives\", \"to\": \"mission.phase.failed\" },\n"
			"    { \"from\": \"mission.phase.complete\", \"to\": \"mission.phase.end\" },\n"
			"    { \"from\": \"mission.phase.failed\", \"to\": \"mission.phase.end\" },\n"
			"    { \"from\": \"mission.objectives\", \"to\": \"mission.parity_backend\" }\n"
			"  ]\n"
			"}\n") != 0) {
		return -1;
	}

	return 0;
}

static const char *s_modeKey(s32 mode_id)
{
	switch (mode_id) {
	case 0: return "combat";
	case 1: return "hold_the_briefcase";
	case 2: return "hacker_central";
	case 3: return "pop_a_cap";
	case 4: return "king_of_the_hill";
	case 5: return "capture_the_case";
	default: return "custom";
	}
}

static const char *s_botTypeKey(s32 type)
{
	switch (type) {
	case BOTTYPE_GENERAL: return "general";
	case BOTTYPE_PEACE:   return "peace";
	case BOTTYPE_SHIELD:  return "shield";
	case BOTTYPE_ROCKET:  return "rocket";
	case BOTTYPE_KAZE:    return "kaze";
	case BOTTYPE_FIST:    return "fist";
	case BOTTYPE_PREY:    return "prey";
	case BOTTYPE_COWARD:  return "coward";
	case BOTTYPE_JUDGE:   return "judge";
	case BOTTYPE_FEUD:    return "feud";
	case BOTTYPE_SPEED:   return "speed";
	case BOTTYPE_TURTLE:  return "turtle";
	case BOTTYPE_VENGE:   return "venge";
	default:              return "general";
	}
}

static const char *s_botDifficultyKey(s32 difficulty)
{
	switch (difficulty) {
	case BOTDIFF_MEAT:    return "meat";
	case BOTDIFF_EASY:    return "easy";
	case BOTDIFF_NORMAL:  return "normal";
	case BOTDIFF_HARD:    return "hard";
	case BOTDIFF_PERFECT: return "perfect";
	case BOTDIFF_DARK:    return "dark";
	default:              return "normal";
	}
}

static const char *s_hudElementKey(s32 element_type)
{
	switch (element_type) {
	case HUD_ELEM_CROSSHAIR: return "crosshair";
	case HUD_ELEM_AMMO:      return "ammo";
	case HUD_ELEM_RADAR:     return "radar";
	case HUD_ELEM_HEALTH:    return "health";
	case HUD_ELEM_TIMER:     return "timer";
	case HUD_ELEM_SCORE:     return "score";
	default:                 return "hud";
	}
}

static const char *s_propKey(s32 prop_type)
{
	switch (prop_type) {
	case 1: return "object";
	case 2: return "door";
	case 3: return "character";
	case 4: return "weapon_pickup";
	case 5: return "eyespy";
	case 6: return "player";
	case 7: return "explosion";
	case 8: return "smoke";
	default: return "prop";
	}
}

static const char *s_effectTypeKey(s32 effect_type)
{
	switch (effect_type) {
	case EFFECT_TYPE_TINT:     return "tint";
	case EFFECT_TYPE_GLOW:     return "glow";
	case EFFECT_TYPE_SHIMMER:  return "shimmer";
	case EFFECT_TYPE_DARKEN:   return "darken";
	case EFFECT_TYPE_SCREEN:   return "screen";
	case EFFECT_TYPE_PARTICLE: return "particle";
	default:                   return "effect";
	}
}

static const char *s_effectTargetKey(s32 target)
{
	switch (target) {
	case EFFECT_TARGET_SCENE:  return "scene";
	case EFFECT_TARGET_PLAYER: return "player";
	case EFFECT_TARGET_CHR:    return "character";
	case EFFECT_TARGET_PROP:   return "prop";
	case EFFECT_TARGET_WEAPON: return "weapon";
	case EFFECT_TARGET_LEVEL:  return "level";
	default:                   return "target";
	}
}

static const char *s_materialPresetKey(const char *id)
{
	if (id && strstr(id, "translucent")) return "classic_alpha";
	if (id && strstr(id, "emissive")) return "classic_emissive";
	return "classic_lit";
}

static const char *s_materialName(const char *id)
{
	if (id && strstr(id, "translucent")) return "Translucent Material";
	if (id && strstr(id, "emissive")) return "Emissive Material";
	return "Default Material";
}

static const char *s_pathBaseName(const char *path)
{
	const char *slash;
	if (!path || !path[0]) return "";
	slash = strrchr(path, '/');
	if (!slash) slash = strrchr(path, '\\');
	return slash ? slash + 1 : path;
}

static s32 s_emitGamemode(const asset_entry_t *e, const char *out_dir,
	s32 force_rewrite)
{
	char relpath[FS_MAXPATH];
	s_archiveRelPath(out_dir, e->id, ".pdgamemode", relpath, sizeof(relpath));
	if (!force_rewrite && fsFileSize(relpath) > 0 &&
			s_existingArchiveHasEntry(relpath, "gamemode.ini") &&
			s_existingArchiveHasEntry(relpath, "rules.json")) {
		return 0;
	}

	char ini[1024];
	int ini_len = snprintf(ini, sizeof(ini),
		"[gamemode]\n"
		"catalog_id = %s\n"
		"mode_key = %s\n"
		"name = %s\n"
		"description = %s\n"
		"min_players = %d\n"
		"max_players = %d\n"
		"team_based = %d\n"
		"requirefeature = %u\n"
		"rules_file = rules.json\n",
		e->id, s_modeKey(e->ext.gamemode.mode_id),
		e->ext.gamemode.name,
		e->ext.gamemode.description, e->ext.gamemode.min_players,
		e->ext.gamemode.max_players, e->ext.gamemode.team_based,
		(unsigned)e->ext.gamemode.requirefeature);
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini)) return -1;

	char rules[1536];
	int rules_len = snprintf(rules, sizeof(rules),
		"{\n"
		"  \"schema\": \"pd2.gamemode.rules.v1\",\n"
		"  \"catalog_id\": \"%s\",\n"
		"  \"mode_key\": \"%s\",\n"
		"  \"players\": { \"min\": %d, \"max\": %d },\n"
		"  \"teams\": { \"required\": %s },\n"
		"  \"score\": { \"source\": \"original_perfect_dark_rules\" },\n"
		"  \"runtime\": { \"parity_backend\": \"og.mpscenario.%s\" }\n"
		"}\n",
		e->id, s_modeKey(e->ext.gamemode.mode_id),
		e->ext.gamemode.min_players, e->ext.gamemode.max_players,
		e->ext.gamemode.team_based ? "true" : "false",
		s_modeKey(e->ext.gamemode.mode_id));
	if (rules_len <= 0 || (size_t)rules_len >= sizeof(rules)) return -1;

	char manifest[1024];
	int manifest_len = snprintf(manifest, sizeof(manifest),
		"{\n"
		"  \"pd_kind\": \"gamemode\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"mode_key\": \"%s\",\n"
		"  \"rules_file\": \"rules.json\"\n"
		"}\n",
		e->id, s_modeKey(e->ext.gamemode.mode_id));
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest)) {
		return -1;
	}

	mod_archive_writer_t *aw;
	asset_archive_writer_t writer;
	if (s_openWriter(relpath, "gamemode", e->id, "romextract_pdmeta",
			"assetcatalog_base_extended", e->runtime_index,
			&aw, &writer) != 0) {
		return -1;
	}
	if (assetArchiveWriterAddDescriptor(&writer, "gamemode.ini",
			ini, (u32)ini_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddManifestJson(&writer,
			manifest, (u32)manifest_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddPublicMem(&writer, "rules.json",
			rules, (u32)rules_len, "rules") != MODARCHIVE_OK) {
		modArchiveAbort(aw);
		return -1;
	}
	return s_finishWriter(aw, &writer, relpath);
}

static s32 s_emitBotProfile(const asset_entry_t *e, const char *out_dir,
	s32 force_rewrite)
{
	char relpath[FS_MAXPATH];
	s_archiveRelPath(out_dir, e->id, ".pdbotprofile", relpath, sizeof(relpath));
	if (!force_rewrite && fsFileSize(relpath) > 0 &&
			s_existingArchiveHasEntry(relpath, "botprofile.ini") &&
			s_existingArchiveHasEntry(relpath, "profile.json")) {
		return 0;
	}

	const char *type_key = s_botTypeKey(e->ext.bot_profile.type);
	const char *diff_key = s_botDifficultyKey(e->ext.bot_profile.difficulty);
	const char *body_id = e->ext.bot_profile.target_body[0]
		? e->ext.bot_profile.target_body : "base:dark_combat";

	char ini[1024];
	int ini_len = snprintf(ini, sizeof(ini),
		"[botprofile]\n"
		"catalog_id = %s\n"
		"type_key = %s\n"
		"difficulty_key = %s\n"
		"target_body = %s\n"
		"requirefeature = %u\n"
		"profile_file = profile.json\n",
		e->id, type_key, diff_key, body_id,
		(unsigned)e->ext.bot_profile.requirefeature);
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini)) return -1;

	char profile[1536];
	int profile_len = snprintf(profile, sizeof(profile),
		"{\n"
		"  \"schema\": \"pd2.botprofile.v1\",\n"
		"  \"catalog_id\": \"%s\",\n"
		"  \"type_key\": \"%s\",\n"
		"  \"difficulty_key\": \"%s\",\n"
		"  \"target_body\": \"%s\",\n"
		"  \"runtime\": { \"parity_backend\": \"og.botprofile.%s.%s\" }\n"
		"}\n",
		e->id, type_key, diff_key, body_id, type_key, diff_key);
	if (profile_len <= 0 || (size_t)profile_len >= sizeof(profile)) return -1;

	char manifest[1024];
	int manifest_len = snprintf(manifest, sizeof(manifest),
		"{\n"
		"  \"pd_kind\": \"botprofile\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"target_body\": \"%s\",\n"
		"  \"profile_file\": \"profile.json\"\n"
		"}\n",
		e->id, body_id);
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest)) {
		return -1;
	}

	mod_archive_writer_t *aw;
	asset_archive_writer_t writer;
	if (s_openWriter(relpath, "botprofile", e->id, "romextract_pdmeta",
			"assetcatalog_base_extended", e->runtime_index,
			&aw, &writer) != 0) {
		return -1;
	}
	if (assetArchiveWriterAddDescriptor(&writer, "botprofile.ini",
			ini, (u32)ini_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddManifestJson(&writer,
			manifest, (u32)manifest_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddPublicMem(&writer, "profile.json",
			profile, (u32)profile_len, "profile") != MODARCHIVE_OK) {
		modArchiveAbort(aw);
		return -1;
	}
	return s_finishWriter(aw, &writer, relpath);
}

static s32 s_emitHud(const asset_entry_t *e, const char *out_dir,
	s32 force_rewrite)
{
	char relpath[FS_MAXPATH];
	s_archiveRelPath(out_dir, e->id, ".pdhud", relpath, sizeof(relpath));
	if (!force_rewrite && fsFileSize(relpath) > 0 &&
			s_existingArchiveHasEntry(relpath, "hud.ini") &&
			s_existingArchiveHasEntry(relpath, "layout.json")) {
		return 0;
	}

	const char *element_key = s_hudElementKey(e->ext.hud.element_type);
	char ini[1024];
	int ini_len = snprintf(ini, sizeof(ini),
		"[hud]\n"
		"catalog_id = %s\n"
		"hud_key = %s\n"
		"name = %s\n"
		"layout_file = layout.json\n",
		e->id, element_key, e->ext.hud.name);
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini)) return -1;

	char layout[1536];
	int layout_len = snprintf(layout, sizeof(layout),
		"{\n"
		"  \"schema\": \"pd2.hud.layout.v1\",\n"
		"  \"catalog_id\": \"%s\",\n"
		"  \"element\": \"%s\",\n"
		"  \"renderer\": \"original_perfect_dark_hud\",\n"
		"  \"slots\": []\n"
		"}\n",
		e->id, element_key);
	if (layout_len <= 0 || (size_t)layout_len >= sizeof(layout)) return -1;

	char manifest[1024];
	int manifest_len = snprintf(manifest, sizeof(manifest),
		"{\n"
		"  \"pd_kind\": \"hud\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"layout_file\": \"layout.json\"\n"
		"}\n",
		e->id);
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest)) {
		return -1;
	}

	mod_archive_writer_t *aw;
	asset_archive_writer_t writer;
	if (s_openWriter(relpath, "hud", e->id, "romextract_pdmeta",
			"assetcatalog_base_extended", e->runtime_index,
			&aw, &writer) != 0) {
		return -1;
	}
	if (assetArchiveWriterAddDescriptor(&writer, "hud.ini",
			ini, (u32)ini_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddManifestJson(&writer,
			manifest, (u32)manifest_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddPublicMem(&writer, "layout.json",
			layout, (u32)layout_len, "layout") != MODARCHIVE_OK) {
		modArchiveAbort(aw);
		return -1;
	}
	return s_finishWriter(aw, &writer, relpath);
}

static s32 s_emitTexture(const asset_entry_t *e, const char *out_dir,
	s32 force_rewrite)
{
	char relpath[FS_MAXPATH];
	s_archiveRelPath(out_dir, e->id, ".pdtexture", relpath, sizeof(relpath));
	if (!force_rewrite && fsFileSize(relpath) > 0 &&
			s_existingArchiveHasEntry(relpath, "texture.ini") &&
			s_existingArchiveHasEntry(relpath, "texture.png")) {
		return 0;
	}

	u8 *tga = NULL;
	u8 *png = NULL;
	u32 tga_size = 0;
	u32 png_size = 0;
	u32 width = 0;
	u32 height = 0;
	s32 empty_rom_slot = 0;
	if (romExtractDecodeTextureImages((u16)e->ext.texture.texture_id,
			&tga, &tga_size, &png, &png_size, &width, &height, NULL) != 0 ||
			!png || png_size == 0) {
		if (tga) { free(tga); tga = NULL; }
		if (png) { free(png); png = NULL; }
		empty_rom_slot =
			romExtractTextureSlotIsEmpty((u16)e->ext.texture.texture_id);
		if (!empty_rom_slot) {
			sysLogPrintf(LOG_WARNING,
				"romextract pdtexture: failed to decode %s (texture_id=%d)",
				e->id, e->ext.texture.texture_id);
			return -1;
		}
		png = (u8 *)malloc(sizeof(k_Transparent1x1Png));
		if (!png) {
			return -1;
		}
		memcpy(png, k_Transparent1x1Png, sizeof(k_Transparent1x1Png));
		png_size = (u32)sizeof(k_Transparent1x1Png);
		width = 1;
		height = 1;
		sysLogPrintf(LOG_NOTE,
			"romextract pdtexture: emitted transparent source for empty ROM slot %s",
			e->id);
	}

	char ini[1024];
	int ini_len = snprintf(ini, sizeof(ini),
		"[texture]\n"
		"catalog_id = %s\n"
		"name = %s\n"
		"width = %u\n"
		"height = %u\n"
		"empty_rom_slot = %s\n"
		"texture_file = texture.png\n",
		e->id, e->id, (unsigned)width, (unsigned)height,
		empty_rom_slot ? "true" : "false");
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini)) {
		free(tga);
		free(png);
		return -1;
	}

	char manifest[1024];
	int manifest_len = snprintf(manifest, sizeof(manifest),
		"{\n"
		"  \"pd_kind\": \"texture\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"texture_file\": \"texture.png\",\n"
		"  \"source_state\": \"%s\",\n"
		"  \"size\": { \"width\": %u, \"height\": %u }\n"
		"}\n",
		e->id,
		empty_rom_slot ? "empty_rom_slot" : "decoded_rom_texture",
		(unsigned)width, (unsigned)height);
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest)) {
		free(tga);
		free(png);
		return -1;
	}

	mod_archive_writer_t *aw;
	asset_archive_writer_t writer;
	if (s_openWriter(relpath, "texture", e->id, "romextract_pdmeta",
			"textureslist/texturesdata", e->ext.texture.texture_id,
			&aw, &writer) != 0) {
		free(tga);
		free(png);
		return -1;
	}
	if (assetArchiveWriterAddDescriptor(&writer, "texture.ini",
			ini, (u32)ini_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddManifestJson(&writer,
			manifest, (u32)manifest_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddPublicMem(&writer, "texture.png",
			png, png_size, "texture") != MODARCHIVE_OK) {
		modArchiveAbort(aw);
		free(tga);
		free(png);
		return -1;
	}
	free(tga);
	free(png);
	return s_finishWriter(aw, &writer, relpath);
}

static s32 s_emitMaterial(const asset_entry_t *e, const char *out_dir,
	s32 force_rewrite)
{
	char relpath[FS_MAXPATH];
	s_archiveRelPath(out_dir, e->id, ".pdmaterial", relpath, sizeof(relpath));
	if (!force_rewrite && fsFileSize(relpath) > 0 &&
			s_existingArchiveHasEntry(relpath, "material.ini") &&
			s_existingArchiveHasEntry(relpath, "material.json")) {
		return 0;
	}

	const char *preset = s_materialPresetKey(e->id);
	const char *name = s_materialName(e->id);
	const char *alpha = strstr(preset, "alpha") ? "0.5" : "1.0";
	const char *emissive = strstr(preset, "emissive") ? "true" : "false";
	char ini[1024];
	int ini_len = snprintf(ini, sizeof(ini),
		"[material]\n"
		"catalog_id = %s\n"
		"name = %s\n"
		"preset = %s\n"
		"material_file = material.json\n",
		e->id, name, preset);
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini)) return -1;

	char material[1536];
	int material_len = snprintf(material, sizeof(material),
		"{\n"
		"  \"schema\": \"pd2.material.v1\",\n"
		"  \"catalog_id\": \"%s\",\n"
		"  \"name\": \"%s\",\n"
		"  \"shading_model\": \"%s\",\n"
		"  \"base_color\": [1.0, 1.0, 1.0, %s],\n"
		"  \"emissive\": %s,\n"
		"  \"roughness\": 0.55,\n"
		"  \"metallic\": 0.0\n"
		"}\n",
		e->id, name, preset, alpha, emissive);
	if (material_len <= 0 || (size_t)material_len >= sizeof(material)) {
		return -1;
	}

	char manifest[1024];
	int manifest_len = snprintf(manifest, sizeof(manifest),
		"{\n"
		"  \"pd_kind\": \"material\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"material_file\": \"material.json\"\n"
		"}\n",
		e->id);
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest)) {
		return -1;
	}

	mod_archive_writer_t *aw;
	asset_archive_writer_t writer;
	if (s_openWriter(relpath, "material", e->id, "romextract_pdmeta",
			"renderer_material_defaults", e->runtime_index,
			&aw, &writer) != 0) {
		return -1;
	}
	if (assetArchiveWriterAddDescriptor(&writer, "material.ini",
			ini, (u32)ini_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddManifestJson(&writer,
			manifest, (u32)manifest_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddPublicMem(&writer, "material.json",
			material, (u32)material_len, "material") != MODARCHIVE_OK) {
		modArchiveAbort(aw);
		return -1;
	}
	return s_finishWriter(aw, &writer, relpath);
}

static s32 s_emitSkin(const asset_entry_t *e, const char *out_dir,
	s32 force_rewrite)
{
	char relpath[FS_MAXPATH];
	s_archiveRelPath(out_dir, e->id, ".pdskin", relpath, sizeof(relpath));
	if (!force_rewrite && fsFileSize(relpath) > 0 &&
			s_existingArchiveHasEntry(relpath, "skin.ini") &&
			s_existingArchiveHasEntry(relpath, "skin.json")) {
		return 0;
	}

	const char *target = e->ext.skin.target_id[0]
		? e->ext.skin.target_id : "base:body_default";
	char ini[1024];
	int ini_len = snprintf(ini, sizeof(ini),
		"[skin]\n"
		"catalog_id = %s\n"
		"target = %s\n"
		"skin_file = skin.json\n"
		"swatches_file = swatches.json\n",
		e->id, target);
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini)) return -1;

	char skin[1536];
	int skin_len = snprintf(skin, sizeof(skin),
		"{\n"
		"  \"schema\": \"pd2.skin.v1\",\n"
		"  \"catalog_id\": \"%s\",\n"
		"  \"target\": \"%s\",\n"
		"  \"material_slots\": [\n"
		"    { \"slot\": \"default\", \"material\": \"base:material_default\" }\n"
		"  ]\n"
		"}\n",
		e->id, target);
	if (skin_len <= 0 || (size_t)skin_len >= sizeof(skin)) return -1;

	const char *swatches =
		"{\n"
		"  \"schema\": \"pd2.skin.swatches.v1\",\n"
		"  \"swatches\": [\n"
		"    { \"name\": \"default\", \"rgba\": [1.0, 1.0, 1.0, 1.0] }\n"
		"  ]\n"
		"}\n";

	char manifest[1024];
	int manifest_len = snprintf(manifest, sizeof(manifest),
		"{\n"
		"  \"pd_kind\": \"skin\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"target\": \"%s\",\n"
		"  \"skin_file\": \"skin.json\"\n"
		"}\n",
		e->id, target);
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest)) {
		return -1;
	}

	mod_archive_writer_t *aw;
	asset_archive_writer_t writer;
	if (s_openWriter(relpath, "skin", e->id, "romextract_pdmeta",
			"body catalog default material bindings", e->runtime_index,
			&aw, &writer) != 0) {
		return -1;
	}
	if (assetArchiveWriterAddDescriptor(&writer, "skin.ini",
			ini, (u32)ini_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddManifestJson(&writer,
			manifest, (u32)manifest_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddPublicMem(&writer, "skin.json",
			skin, (u32)skin_len, "skin") != MODARCHIVE_OK ||
			assetArchiveWriterAddPublicMem(&writer, "swatches.json",
			swatches, (u32)strlen(swatches), "swatches") != MODARCHIVE_OK) {
		modArchiveAbort(aw);
		return -1;
	}
	return s_finishWriter(aw, &writer, relpath);
}

static s32 s_emitEffect(const asset_entry_t *e, const char *out_dir,
	s32 force_rewrite)
{
	char relpath[FS_MAXPATH];
	s_archiveRelPath(out_dir, e->id, ".pdeffect", relpath, sizeof(relpath));
	if (!force_rewrite && fsFileSize(relpath) > 0 &&
			s_existingArchiveHasEntry(relpath, "effect.ini") &&
			s_existingArchiveHasEntry(relpath, "effect.graph.json")) {
		return 0;
	}

	const char *type_key = s_effectTypeKey(e->ext.effect.effect_type);
	const char *target_key = s_effectTargetKey(e->ext.effect.target);
	char ini[1024];
	int ini_len = snprintf(ini, sizeof(ini),
		"[effect]\n"
		"catalog_id = %s\n"
		"name = %s\n"
		"effect_key = %s\n"
		"target_key = %s\n"
		"shader_id = %s\n"
		"intensity = %.3f\n"
		"effect_file = effect.graph.json\n",
		e->id, e->ext.effect.name[0] ? e->ext.effect.name : type_key,
		type_key, target_key, e->ext.effect.shader_id,
		e->ext.effect.intensity);
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini)) return -1;

	char graph[1536];
	int graph_len = snprintf(graph, sizeof(graph),
		"{\n"
		"  \"schema\": \"pd2.effect.graph.v1\",\n"
		"  \"catalog_id\": \"%s\",\n"
		"  \"effect\": \"%s\",\n"
		"  \"target\": \"%s\",\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"apply\", \"kind\": \"effect.%s\", \"params\": { \"shader\": \"%s\", \"intensity\": %.3f } }\n"
		"  ],\n"
		"  \"edges\": []\n"
		"}\n",
		e->id, type_key, target_key, type_key, e->ext.effect.shader_id,
		e->ext.effect.intensity);
	if (graph_len <= 0 || (size_t)graph_len >= sizeof(graph)) return -1;

	char manifest[1024];
	int manifest_len = snprintf(manifest, sizeof(manifest),
		"{\n"
		"  \"pd_kind\": \"effect\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"effect_file\": \"effect.graph.json\"\n"
		"}\n",
		e->id);
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest)) {
		return -1;
	}

	mod_archive_writer_t *aw;
	asset_archive_writer_t writer;
	if (s_openWriter(relpath, "effect", e->id, "romextract_pdmeta",
			"renderer_effect_defaults", e->runtime_index,
			&aw, &writer) != 0) {
		return -1;
	}
	if (assetArchiveWriterAddDescriptor(&writer, "effect.ini",
			ini, (u32)ini_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddManifestJson(&writer,
			manifest, (u32)manifest_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddPublicMem(&writer, "effect.graph.json",
			graph, (u32)graph_len, "effect-graph") != MODARCHIVE_OK) {
		modArchiveAbort(aw);
		return -1;
	}
	return s_finishWriter(aw, &writer, relpath);
}

static s32 s_emitProp(const asset_entry_t *e, const char *out_dir,
	s32 force_rewrite)
{
	char relpath[FS_MAXPATH];
	s_archiveRelPath(out_dir, e->id, ".pdprop", relpath, sizeof(relpath));
	if (!force_rewrite && fsFileSize(relpath) > 0 &&
			s_existingArchiveHasEntry(relpath, "prop.ini") &&
			s_existingArchiveHasEntry(relpath, "prop.json")) {
		return 0;
	}

	const char *prop_key = s_propKey(e->ext.prop.prop_type);
	char ini[1024];
	int ini_len = snprintf(ini, sizeof(ini),
		"[prop]\n"
		"catalog_id = %s\n"
		"name = %s\n"
		"prop_key = %s\n"
		"health = %.3f\n"
		"prop_file = prop.json\n",
		e->id, e->ext.prop.name, prop_key, e->ext.prop.health);
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini)) return -1;

	char prop[1536];
	int prop_len = snprintf(prop, sizeof(prop),
		"{\n"
		"  \"schema\": \"pd2.prop.v1\",\n"
		"  \"catalog_id\": \"%s\",\n"
		"  \"prop_key\": \"%s\",\n"
		"  \"display_name\": \"%s\",\n"
		"  \"health\": %.3f,\n"
		"  \"runtime\": { \"parity_backend\": \"og.prop.%s\" }\n"
		"}\n",
		e->id, prop_key, e->ext.prop.name, e->ext.prop.health, prop_key);
	if (prop_len <= 0 || (size_t)prop_len >= sizeof(prop)) return -1;

	char manifest[1024];
	int manifest_len = snprintf(manifest, sizeof(manifest),
		"{\n"
		"  \"pd_kind\": \"prop\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"prop_file\": \"prop.json\"\n"
		"}\n",
		e->id);
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest)) {
		return -1;
	}

	mod_archive_writer_t *aw;
	asset_archive_writer_t writer;
	if (s_openWriter(relpath, "prop", e->id, "romextract_pdmeta",
			"prop runtime category table", e->runtime_index,
			&aw, &writer) != 0) {
		return -1;
	}
	if (assetArchiveWriterAddDescriptor(&writer, "prop.ini",
			ini, (u32)ini_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddManifestJson(&writer,
			manifest, (u32)manifest_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddPublicMem(&writer, "prop.json",
			prop, (u32)prop_len, "prop") != MODARCHIVE_OK) {
		modArchiveAbort(aw);
		return -1;
	}
	return s_finishWriter(aw, &writer, relpath);
}

static s32 s_emitVehicle(const asset_entry_t *e, const char *out_dir,
	const char *meshes_dir, s32 force_rewrite)
{
	char relpath[FS_MAXPATH];
	s_archiveRelPath(out_dir, e->id, ".pdvehicle", relpath, sizeof(relpath));
	if (!force_rewrite && fsFileSize(relpath) > 0 &&
			s_existingArchiveHasEntry(relpath, "vehicle.ini") &&
			s_existingArchiveHasEntry(relpath, "physics.json") &&
			s_existingArchiveHasEntry(relpath, "behavior.graph.json")) {
		return 0;
	}

	const char *model_member = e->ext.vehicle.model_file;
	const char *model_base = s_pathBaseName(model_member);
	char model_source[FS_MAXPATH];
	s32 has_model = 0;
	model_source[0] = '\0';
	if (model_base[0]) {
		snprintf(model_source, sizeof(model_source), "%s/%s",
			meshes_dir, model_base);
		has_model = fsFileSize(model_source) > 0;
	}

	char ini[1536];
	int ini_len = snprintf(ini, sizeof(ini),
		"[vehicle]\n"
		"catalog_id = %s\n"
		"name = Hoverbike\n"
		"%s%s%s"
		"physics_file = physics.json\n"
		"behavior_graph = behavior.graph.json\n",
		e->id,
		has_model ? "model_file = " : "",
		has_model ? model_member : "",
		has_model ? "\n" : "");
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini)) return -1;

	const char *physics =
		"{\n"
		"  \"schema\": \"pd2.vehicle.physics.v1\",\n"
		"  \"archetype\": \"hoverbike\",\n"
		"  \"movement\": { \"mode\": \"hover\", \"source\": \"original_perfect_dark\" },\n"
		"  \"collision\": { \"shape\": \"geocyl\", \"source\": \"original_perfect_dark\" }\n"
		"}\n";
	const char *behavior =
		"{\n"
		"  \"schema\": \"pd2.vehicle.behavior_graph.v1\",\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"mount\", \"kind\": \"vehicle.mount\" },\n"
		"    { \"id\": \"drive\", \"kind\": \"vehicle.hoverbike.drive\" },\n"
		"    { \"id\": \"dismount\", \"kind\": \"vehicle.dismount\" }\n"
		"  ],\n"
		"  \"edges\": []\n"
		"}\n";

	char manifest[1536];
	int manifest_len = snprintf(manifest, sizeof(manifest),
		"{\n"
		"  \"pd_kind\": \"vehicle\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"physics_file\": \"physics.json\",\n"
		"  \"behavior_graph\": \"behavior.graph.json\"%s%s%s\n"
		"}\n",
		e->id,
		has_model ? ",\n  \"model_file\": \"" : "",
		has_model ? model_member : "",
		has_model ? "\"" : "");
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest)) {
		return -1;
	}

	mod_archive_writer_t *aw;
	asset_archive_writer_t writer;
	if (s_openWriter(relpath, "vehicle", e->id, "romextract_pdmeta",
			"hoverbike setup/runtime data", e->runtime_index,
			&aw, &writer) != 0) {
		return -1;
	}
	if (assetArchiveWriterAddDescriptor(&writer, "vehicle.ini",
			ini, (u32)ini_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddManifestJson(&writer,
			manifest, (u32)manifest_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddPublicMem(&writer, "physics.json",
			physics, (u32)strlen(physics), "vehicle-physics") != MODARCHIVE_OK ||
			assetArchiveWriterAddPublicMem(&writer, "behavior.graph.json",
			behavior, (u32)strlen(behavior), "vehicle-graph") != MODARCHIVE_OK) {
		modArchiveAbort(aw);
		return -1;
	}
	if (has_model && assetArchiveWriterAddPublicDisk(&writer, model_member,
			model_source, "vehicle-model") != MODARCHIVE_OK) {
		modArchiveAbort(aw);
		return -1;
	}
	return s_finishWriter(aw, &writer, relpath);
}

static void s_scenarioIdFromSlug(const char *slug, char *out, size_t out_n)
{
	if (!out || out_n == 0) return;
	out[0] = '\0';
	if (!slug || !slug[0]) return;
	snprintf(out, out_n, "base:scenario_%s", slug);
}

static s32 s_emitMission(const arena_authored_record_t *a, const char *out_dir,
	const char *scenarios_dir, s32 arena_index, s32 force_rewrite)
{
	if (!a || !a->category || strcmp(a->category, "Solo Missions") != 0) {
		return 0;
	}
	if (stageGetIndex(a->stagenum) < 0) {
		return 0;
	}

	char mission_id[CATALOG_ID_LEN];
	snprintf(mission_id, sizeof(mission_id), "base:mission_%s", a->slug);
	char mission_file[CATALOG_ID_LEN];
	s_idToFilenameSlug(mission_id, mission_file, sizeof(mission_file));
	char relpath[FS_MAXPATH];
	s_archiveRelPath(out_dir, mission_id, ".pdmission", relpath, sizeof(relpath));
	s32 archive_current = (!force_rewrite && fsFileSize(relpath) > 0 &&
		s_existingArchiveHasEntry(relpath, "mission.ini") &&
		s_existingArchiveHasEntry(relpath, "mission.graph.json") &&
		s_existingArchiveHasEntry(relpath, "objectives.json") &&
		s_existingArchiveHasEntry(relpath, "briefing.json") &&
		s_existingArchiveEntryContains(relpath, "mission.graph.json",
			"mission.phase.source") &&
		s_existingArchiveEntryContains(relpath, "mission.graph.json",
			"mission.objectives.source") &&
		s_existingArchiveEntryContains(relpath, "mission.graph.json",
			"mission.objective.source") &&
		s_existingArchiveEntryContains(relpath, "mission.ini",
			"scenario_graph_cache = " PDMETA_SCENARIO_DEP_CACHE_KIND) &&
		s_existingArchiveEntryContains(relpath, "mission.graph.json",
			"\"operand_kind\"") &&
		s_existingArchiveEntryContains(relpath, "objectives.json",
			"operand_kind") &&
		!s_existingArchiveEntryContains(relpath, "mission.graph.json",
			"\"nodes\": []") &&
		!s_existingArchiveEntryContains(relpath, "objectives.json",
			"original_perfect_dark_setup"));

	char scenario_id[CATALOG_ID_LEN];
	s_scenarioIdFromSlug(a->slug, scenario_id, sizeof(scenario_id));
	char scenario_file[CATALOG_ID_LEN];
	s_idToFilenameSlug(scenario_id, scenario_file, sizeof(scenario_file));
	char scenario_rel[FS_MAXPATH];
	snprintf(scenario_rel, sizeof(scenario_rel), "%s/%s.pdscenario",
		scenarios_dir, scenario_file);
	if (fsFileSize(scenario_rel) <= 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdmission: missing scenario dependency %s for %s",
			scenario_rel, mission_id);
		return -1;
	}

	pdmeta_objective_row_t objective_rows[PDMETA_MAX_MISSION_OBJECTIVE_ROWS];
	s32 objective_row_count = s_loadScenarioObjectiveRows(scenario_rel,
		objective_rows, ARRAYCOUNT(objective_rows));
	if (objective_row_count <= 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdmission: missing decoded scenario objectives for %s",
			mission_id);
		return -1;
	}

	asset_entry_t *entry = assetCatalogRegister(mission_id, ASSET_MISSION);
	if (entry) {
		strncpy(entry->category, "base", CATALOG_CATEGORY_LEN - 1);
		entry->bundled = 1;
		entry->enabled = 1;
		entry->runtime_index = arena_index;
	snprintf(entry->ext.mission.scenario_archive,
		sizeof(entry->ext.mission.scenario_archive),
		"%s/%s.pdmission::dependencies/assets/scenarios/%s.pdscenario",
		out_dir, mission_file, scenario_file);
	snprintf(entry->ext.mission.objectives_file,
		sizeof(entry->ext.mission.objectives_file),
		"%s/%s.pdmission::objectives.json", out_dir, mission_file);
	snprintf(entry->ext.mission.briefing_file,
		sizeof(entry->ext.mission.briefing_file),
		"%s/%s.pdmission::briefing.json", out_dir, mission_file);
	snprintf(entry->ext.mission.mission_graph_file,
		sizeof(entry->ext.mission.mission_graph_file),
		"%s/%s.pdmission::mission.graph.json", out_dir, mission_file);
		catalogSetPrimaryFile(entry, entry->ext.mission.mission_graph_file);
		entry->load_state = ASSET_STATE_LOADED;
		entry->ref_count = ASSET_REF_BUNDLED;
	}

	if (archive_current) {
		return 0;
	}

	char ini[1536];
	int ini_len = snprintf(ini, sizeof(ini),
		"[mission]\n"
		"catalog_id = %s\n"
		"scenario = %s\n"
		"scenario_archive = dependencies/assets/scenarios/%s.pdscenario\n"
		"scenario_graph_cache = %s\n"
		"mission_graph_file = mission.graph.json\n"
		"objectives_file = objectives.json\n"
		"briefing_file = briefing.json\n"
		"category = %s\n",
		mission_id, scenario_id, scenario_file,
		PDMETA_SCENARIO_DEP_CACHE_KIND, a->category);
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini)) return -1;

	pdmeta_textbuf_t graph = { 0 };
	pdmeta_textbuf_t objectives = { 0 };
	if (s_buildMissionGraph(mission_id, scenario_id, scenario_file,
			a->slug, objective_rows, objective_row_count, &graph) != 0 ||
			s_buildMissionObjectivesJson(scenario_file, objective_rows,
			objective_row_count, &objectives) != 0) {
		s_textbufFree(&graph);
		s_textbufFree(&objectives);
		return -1;
	}
	const char *briefing =
		"{\n"
		"  \"schema\": \"pd2.mission.briefing.v1\",\n"
		"  \"sections\": [\n"
		"    { \"id\": \"summary\", \"text\": \"Original mission briefing is loaded from base language banks.\" }\n"
		"  ]\n"
		"}\n";

	char manifest[1536];
	int manifest_len = snprintf(manifest, sizeof(manifest),
		"{\n"
		"  \"pd_kind\": \"mission\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"scenario\": \"%s\",\n"
		"  \"scenario_archive\": \"dependencies/assets/scenarios/%s.pdscenario\",\n"
		"  \"mission_graph_file\": \"mission.graph.json\"\n"
		"}\n",
		mission_id, scenario_id, scenario_file);
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest)) {
		return -1;
	}

	mod_archive_writer_t *aw;
	asset_archive_writer_t writer;
	if (s_openWriter(relpath, "mission", mission_id, "romextract_pdmeta",
			"arenadata_authored", arena_index, &aw, &writer) != 0) {
		return -1;
	}
	char dep_name[FS_MAXPATH];
	snprintf(dep_name, sizeof(dep_name),
		"dependencies/assets/scenarios/%s.pdscenario", scenario_file);
	if (assetArchiveWriterAddDescriptor(&writer, "mission.ini",
			ini, (u32)ini_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddManifestJson(&writer,
			manifest, (u32)manifest_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddPublicMem(&writer, "mission.graph.json",
			graph.data, (u32)graph.len, "mission-graph") != MODARCHIVE_OK ||
			assetArchiveWriterAddPublicMem(&writer, "objectives.json",
			objectives.data, (u32)objectives.len, "objectives") != MODARCHIVE_OK ||
			assetArchiveWriterAddPublicMem(&writer, "briefing.json",
			briefing, (u32)strlen(briefing), "briefing") != MODARCHIVE_OK ||
			assetArchiveWriterAddPublicDisk(&writer, dep_name,
			scenario_rel, "scenario-dependency") != MODARCHIVE_OK) {
		s_textbufFree(&graph);
		s_textbufFree(&objectives);
		modArchiveAbort(aw);
		return -1;
	}
	s_textbufFree(&graph);
	s_textbufFree(&objectives);
	return s_finishWriter(aw, &writer, relpath);
}

typedef struct pdmeta_ctx {
	const char *gamemodes_dir;
	const char *botprofiles_dir;
	const char *hud_dir;
	const char *missions_dir;
	const char *scenarios_dir;
	const char *textures_dir;
	const char *materials_dir;
	const char *skins_dir;
	const char *effects_dir;
	const char *props_dir;
	const char *vehicles_dir;
	const char *meshes_dir;
	s32 force_rewrite;
	SDL_atomic_t written;
	SDL_atomic_t skipped;
	SDL_atomic_t failed;
} pdmeta_ctx_t;

static void s_emitEntryArchive(const asset_entry_t *e, pdmeta_ctx_t *ctx)
{
	s32 r = 0;
	switch (e->type) {
	case ASSET_GAMEMODE:
		r = s_emitGamemode(e, ctx->gamemodes_dir, ctx->force_rewrite);
		break;
	case ASSET_BOT_PROFILE:
		r = s_emitBotProfile(e, ctx->botprofiles_dir, ctx->force_rewrite);
		break;
	case ASSET_HUD:
		r = s_emitHud(e, ctx->hud_dir, ctx->force_rewrite);
		break;
	case ASSET_TEXTURE:
		r = s_emitTexture(e, ctx->textures_dir, ctx->force_rewrite);
		break;
	case ASSET_MATERIAL:
		r = s_emitMaterial(e, ctx->materials_dir, ctx->force_rewrite);
		break;
	case ASSET_SKIN:
		r = s_emitSkin(e, ctx->skins_dir, ctx->force_rewrite);
		break;
	case ASSET_EFFECT:
		r = s_emitEffect(e, ctx->effects_dir, ctx->force_rewrite);
		break;
	case ASSET_PROP:
		r = s_emitProp(e, ctx->props_dir, ctx->force_rewrite);
		break;
	case ASSET_VEHICLE:
		r = s_emitVehicle(e, ctx->vehicles_dir, ctx->meshes_dir,
			ctx->force_rewrite);
		break;
	default:
		return;
	}

	if (r > 0) SDL_AtomicAdd(&ctx->written, 1);
	else if (r == 0) SDL_AtomicAdd(&ctx->skipped, 1);
	else SDL_AtomicAdd(&ctx->failed, 1);
}

s32 romExtractAllPdmeta(s32 force_rewrite)
{
#if defined(PD_SERVER)
	(void)force_rewrite;
	return 0;
#else
	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDMETA", "fsDataDirEnsure failed");
		return -1;
	}

	char data_dir_buf[FS_MAXPATH + 1];
	const char *data_dir = fsDataDir(data_dir_buf, sizeof(data_dir_buf));
	char gamemodes_dir[FS_MAXPATH];
	char botprofiles_dir[FS_MAXPATH];
	char hud_dir[FS_MAXPATH];
	char missions_dir[FS_MAXPATH];
	char scenarios_dir[FS_MAXPATH];
	char textures_dir[FS_MAXPATH];
	char materials_dir[FS_MAXPATH];
	char skins_dir[FS_MAXPATH];
	char effects_dir[FS_MAXPATH];
	char props_dir[FS_MAXPATH];
	char vehicles_dir[FS_MAXPATH];
	char meshes_dir[FS_MAXPATH];
	snprintf(gamemodes_dir, sizeof(gamemodes_dir), "%s/gamemodes", data_dir);
	snprintf(botprofiles_dir, sizeof(botprofiles_dir), "%s/botprofiles", data_dir);
	snprintf(hud_dir, sizeof(hud_dir), "%s/hud", data_dir);
	snprintf(missions_dir, sizeof(missions_dir), "%s/missions", data_dir);
	snprintf(scenarios_dir, sizeof(scenarios_dir), "%s/scenarios", data_dir);
	snprintf(textures_dir, sizeof(textures_dir), "%s/textures", data_dir);
	snprintf(materials_dir, sizeof(materials_dir), "%s/materials", data_dir);
	snprintf(skins_dir, sizeof(skins_dir), "%s/skins", data_dir);
	snprintf(effects_dir, sizeof(effects_dir), "%s/effects", data_dir);
	snprintf(props_dir, sizeof(props_dir), "%s/props", data_dir);
	snprintf(vehicles_dir, sizeof(vehicles_dir), "%s/vehicles", data_dir);
	snprintf(meshes_dir, sizeof(meshes_dir), "%s/meshes", data_dir);

	if (!fsCreateDir(gamemodes_dir) || !fsCreateDir(botprofiles_dir) ||
			!fsCreateDir(hud_dir) || !fsCreateDir(missions_dir) ||
			!fsCreateDir(textures_dir) || !fsCreateDir(materials_dir) ||
			!fsCreateDir(skins_dir) || !fsCreateDir(effects_dir) ||
			!fsCreateDir(props_dir) || !fsCreateDir(vehicles_dir)) {
		sysLoudFailf("EXTRACT.PDMETA", "failed to create metadata output dirs");
		return -1;
	}

	pdmeta_ctx_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.gamemodes_dir = gamemodes_dir;
	ctx.botprofiles_dir = botprofiles_dir;
	ctx.hud_dir = hud_dir;
	ctx.missions_dir = missions_dir;
	ctx.scenarios_dir = scenarios_dir;
	ctx.textures_dir = textures_dir;
	ctx.materials_dir = materials_dir;
	ctx.skins_dir = skins_dir;
	ctx.effects_dir = effects_dir;
	ctx.props_dir = props_dir;
	ctx.vehicles_dir = vehicles_dir;
	ctx.meshes_dir = meshes_dir;
	ctx.force_rewrite = force_rewrite;
	SDL_AtomicSet(&ctx.written, 0);
	SDL_AtomicSet(&ctx.skipped, 0);
	SDL_AtomicSet(&ctx.failed, 0);

	s32 total = assetCatalogGetCount();
	sysLogPrintf(LOG_NOTE,
		"romextract pdmeta: begin entries=%d missions=%d",
		total, g_ArenaDataCount);
	bootProgressUpdate(0, total + g_ArenaDataCount);
	for (s32 i = 0; i < total; i++) {
		const asset_entry_t *e = assetCatalogGetByIndex(i);
		if (e && e->occupied && e->bundled) {
			s_emitEntryArchive(e, &ctx);
		}
		if ((i & 0x1f) == 0) {
			bootProgressUpdate(i, total + g_ArenaDataCount);
		}
	}

	for (s32 i = 0; i < g_ArenaDataCount; i++) {
		s32 r = s_emitMission(&g_ArenaData[i], missions_dir, scenarios_dir,
			i, force_rewrite);
		if (r > 0) SDL_AtomicAdd(&ctx.written, 1);
		else if (r == 0) SDL_AtomicAdd(&ctx.skipped, 1);
		else SDL_AtomicAdd(&ctx.failed, 1);
		bootProgressUpdate(total + i + 1, total + g_ArenaDataCount);
	}

	s32 written = SDL_AtomicGet(&ctx.written);
	s32 skipped = SDL_AtomicGet(&ctx.skipped);
	s32 failed = SDL_AtomicGet(&ctx.failed);
	sysLogPrintf(LOG_NOTE,
		"romextract pdmeta: written=%d skipped=%d failed=%d",
		written, skipped, failed);

	if (failed == 0) {
		romExtractPdFastCacheWrite(PDMETA_FAST_CACHE_KIND, gamemodes_dir,
			".pdgamemode");
		romExtractPdFastCacheWrite(PDMETA_FAST_CACHE_KIND, botprofiles_dir,
			".pdbotprofile");
		romExtractPdFastCacheWrite(PDMETA_FAST_CACHE_KIND, hud_dir,
			".pdhud");
		romExtractPdFastCacheWrite(PDMETA_FAST_CACHE_KIND, missions_dir,
			".pdmission");
		romExtractPdFastCacheWrite(PDMETA_FAST_CACHE_KIND, textures_dir,
			".pdtexture");
		romExtractPdFastCacheWrite(PDMETA_FAST_CACHE_KIND, materials_dir,
			".pdmaterial");
		romExtractPdFastCacheWrite(PDMETA_FAST_CACHE_KIND, skins_dir,
			".pdskin");
		romExtractPdFastCacheWrite(PDMETA_FAST_CACHE_KIND, effects_dir,
			".pdeffect");
		romExtractPdFastCacheWrite(PDMETA_FAST_CACHE_KIND, props_dir,
			".pdprop");
		romExtractPdFastCacheWrite(PDMETA_FAST_CACHE_KIND, vehicles_dir,
			".pdvehicle");
	}
	return failed ? -1 : written;
#endif
}
