/**
 * loader_walker_mesh_source_plan.c -- pure typed .pdmesh source planner.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <PR/ultratypes.h>

#include "asset_path_contract.h"
#include "loader_enum_reverse.h"
#include "loader_walker_mesh_source.h"

enum manifest_string_result {
	MANIFEST_STRING_INVALID = -1,
	MANIFEST_STRING_ABSENT = 0,
	MANIFEST_STRING_PRESENT = 1,
};

#define MESH_MANIFEST_JSON_MAX_DEPTH 64

static const char *const k_mesh_public_geometry_keys[] = {
	"model_file", "geometry_file", "model", "geometry", "file_path"
};

typedef struct mesh_manifest_fields {
	char kind[32];
	char id[CATALOG_ID_LEN];
	char geometry[FS_MAXPATH];
	char source_symbol[64];
	s32 kind_result;
	s32 id_result;
	s32 geometry_result;
	s32 symbol_result;
} mesh_manifest_fields_t;

typedef struct mesh_json_cursor {
	const char *cursor;
	const char *end;
} mesh_json_cursor_t;

size_t loaderWalkerMeshPublicGeometryKeyCount(void)
{
	return sizeof(k_mesh_public_geometry_keys)
		/ sizeof(k_mesh_public_geometry_keys[0]);
}

const char *loaderWalkerMeshPublicGeometryKey(size_t index)
{
	return index < loaderWalkerMeshPublicGeometryKeyCount()
		? k_mesh_public_geometry_keys[index] : NULL;
}

static void meshSourceSetErr(char *err, size_t err_cap, const char *fmt,
		const char *a, const char *b)
{
	if (!err || err_cap == 0) return;
	snprintf(err, err_cap, fmt, a ? a : "", b ? b : "");
	err[err_cap - 1] = '\0';
}

static void meshJsonSkipWs(mesh_json_cursor_t *json)
{
	while (json->cursor < json->end
			&& isspace((unsigned char)*json->cursor)) json->cursor++;
}

static s32 meshJsonHexDigit(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
		|| (c >= 'A' && c <= 'F');
}

/* Validate one JSON string. Source-owning top-level keys and values reject
 * escapes so there is exactly one textual representation of their identity;
 * unrelated nested metadata may use ordinary JSON escapes. */
static s32 meshJsonString(mesh_json_cursor_t *json, char *out, size_t out_cap,
		s32 allow_escapes, s32 *out_overflow)
{
	size_t used = 0;
	s32 overflow = 0;

	if (!json || json->cursor >= json->end || *json->cursor != '"') return 0;
	json->cursor++;
	while (json->cursor < json->end) {
		unsigned char c = (unsigned char)*json->cursor++;
		if (c == '"') {
			if (out && out_cap) out[used < out_cap ? used : out_cap - 1] = '\0';
			if (out_overflow) *out_overflow = overflow;
			return 1;
		}
		if (c < 0x20) return 0;
		if (c == '\\') {
			if (!allow_escapes || json->cursor >= json->end) return 0;
			c = (unsigned char)*json->cursor++;
			if (c == 'u') {
				for (s32 i = 0; i < 4; i++) {
					if (json->cursor >= json->end
							|| !meshJsonHexDigit(*json->cursor++)) return 0;
				}
			} else if (c != '"' && c != '\\' && c != '/'
					&& c != 'b' && c != 'f' && c != 'n'
					&& c != 'r' && c != 't') {
				return 0;
			}
			/* Escaped text is validated but never copied into identity output. */
			continue;
		}
		if (out) {
			if (used + 1 < out_cap) out[used++] = (char)c;
			else overflow = 1;
		}
	}
	return 0;
}

static s32 meshJsonValue(mesh_json_cursor_t *json, s32 depth);

static s32 meshJsonNumber(mesh_json_cursor_t *json)
{
	const char *p = json->cursor;
	if (p < json->end && *p == '-') p++;
	if (p >= json->end) return 0;
	if (*p == '0') {
		p++;
		if (p < json->end && isdigit((unsigned char)*p)) return 0;
	} else {
		if (*p < '1' || *p > '9') return 0;
		do { p++; } while (p < json->end && isdigit((unsigned char)*p));
	}
	if (p < json->end && *p == '.') {
		p++;
		if (p >= json->end || !isdigit((unsigned char)*p)) return 0;
		do { p++; } while (p < json->end && isdigit((unsigned char)*p));
	}
	if (p < json->end && (*p == 'e' || *p == 'E')) {
		p++;
		if (p < json->end && (*p == '+' || *p == '-')) p++;
		if (p >= json->end || !isdigit((unsigned char)*p)) return 0;
		do { p++; } while (p < json->end && isdigit((unsigned char)*p));
	}
	json->cursor = p;
	return 1;
}

static s32 meshJsonArray(mesh_json_cursor_t *json, s32 depth)
{
	if (depth >= MESH_MANIFEST_JSON_MAX_DEPTH || *json->cursor++ != '[') return 0;
	meshJsonSkipWs(json);
	if (json->cursor < json->end && *json->cursor == ']') {
		json->cursor++;
		return 1;
	}
	for (;;) {
		if (!meshJsonValue(json, depth + 1)) return 0;
		meshJsonSkipWs(json);
		if (json->cursor >= json->end) return 0;
		if (*json->cursor == ']') {
			json->cursor++;
			return 1;
		}
		if (*json->cursor++ != ',') return 0;
		meshJsonSkipWs(json);
	}
}

static s32 meshJsonObject(mesh_json_cursor_t *json, s32 depth)
{
	if (depth >= MESH_MANIFEST_JSON_MAX_DEPTH || *json->cursor++ != '{') return 0;
	meshJsonSkipWs(json);
	if (json->cursor < json->end && *json->cursor == '}') {
		json->cursor++;
		return 1;
	}
	for (;;) {
		if (!meshJsonString(json, NULL, 0, 1, NULL)) return 0;
		meshJsonSkipWs(json);
		if (json->cursor >= json->end || *json->cursor++ != ':') return 0;
		meshJsonSkipWs(json);
		if (!meshJsonValue(json, depth + 1)) return 0;
		meshJsonSkipWs(json);
		if (json->cursor >= json->end) return 0;
		if (*json->cursor == '}') {
			json->cursor++;
			return 1;
		}
		if (*json->cursor++ != ',') return 0;
		meshJsonSkipWs(json);
	}
}

static s32 meshJsonValue(mesh_json_cursor_t *json, s32 depth)
{
	static const char *literals[] = { "true", "false", "null" };
	static const size_t literal_lengths[] = { 4, 5, 4 };
	if (!json) return 0;
	meshJsonSkipWs(json);
	if (json->cursor >= json->end
			|| depth >= MESH_MANIFEST_JSON_MAX_DEPTH) return 0;
	if (*json->cursor == '"') return meshJsonString(json, NULL, 0, 1, NULL);
	if (*json->cursor == '{') return meshJsonObject(json, depth);
	if (*json->cursor == '[') return meshJsonArray(json, depth);
	if (*json->cursor == '-' || isdigit((unsigned char)*json->cursor)) {
		return meshJsonNumber(json);
	}
	for (size_t i = 0; i < sizeof(literals) / sizeof(literals[0]); i++) {
		if ((size_t)(json->end - json->cursor) >= literal_lengths[i]
				&& memcmp(json->cursor, literals[i], literal_lengths[i]) == 0) {
			json->cursor += literal_lengths[i];
			return 1;
		}
	}
	return 0;
}

static s32 meshManifestTarget(mesh_manifest_fields_t *fields, const char *key,
		char **out, size_t *out_cap, s32 **result)
{
	if (strcmp(key, "pd_kind") == 0) {
		*out = fields->kind; *out_cap = sizeof(fields->kind);
		*result = &fields->kind_result; return 1;
	}
	if (strcmp(key, "id") == 0) {
		*out = fields->id; *out_cap = sizeof(fields->id);
		*result = &fields->id_result; return 1;
	}
	if (strcmp(key, "geometry") == 0) {
		*out = fields->geometry; *out_cap = sizeof(fields->geometry);
		*result = &fields->geometry_result; return 1;
	}
	if (strcmp(key, "source_filenum_symbol") == 0) {
		*out = fields->source_symbol; *out_cap = sizeof(fields->source_symbol);
		*result = &fields->symbol_result; return 1;
	}
	return 0;
}

static s32 meshManifestParse(const char *manifest, size_t manifest_len,
		mesh_manifest_fields_t *fields)
{
	mesh_json_cursor_t json;
	if (!fields) return 0;
	memset(fields, 0, sizeof(*fields));
	if (!manifest || manifest_len == 0) return 1;
	json.cursor = manifest;
	json.end = manifest + manifest_len;
	meshJsonSkipWs(&json);
	if (json.cursor >= json.end || *json.cursor++ != '{') return 0;
	meshJsonSkipWs(&json);
	if (json.cursor < json.end && *json.cursor == '}') {
		json.cursor++;
	} else {
		for (;;) {
			char key[64];
			s32 key_overflow = 0;
			char *target = NULL;
			size_t target_cap = 0;
			s32 *target_result = NULL;
			s32 target_overflow = 0;
			if (!meshJsonString(&json, key, sizeof(key), 0, &key_overflow)) return 0;
			meshJsonSkipWs(&json);
			if (json.cursor >= json.end || *json.cursor++ != ':') return 0;
			meshJsonSkipWs(&json);
			if (!key_overflow && meshManifestTarget(fields, key, &target,
					&target_cap, &target_result)) {
				if (*target_result != MANIFEST_STRING_ABSENT
						|| !meshJsonString(&json, target, target_cap, 0,
							&target_overflow) || target_overflow) return 0;
				*target_result = MANIFEST_STRING_PRESENT;
			} else if (!meshJsonValue(&json, 1)) {
				return 0;
			}
			meshJsonSkipWs(&json);
			if (json.cursor >= json.end) return 0;
			if (*json.cursor == '}') {
				json.cursor++;
				break;
			}
			if (*json.cursor++ != ',') return 0;
			meshJsonSkipWs(&json);
		}
	}
	meshJsonSkipWs(&json);
	return json.cursor == json.end;
}

static s32 geometryMemberIsSafe(const char *member)
{
	if (!member || !member[0]) return 0;
	if (assetPathIsAbsolute(member) || assetPathHasParentTraversal(member)
			|| strstr(member, "::") || strchr(member, '\\')) return 0;
	if ((member[0] == '.' && member[1] == '/')
			|| member[strlen(member) - 1] == '/') return 0;
	return 1;
}

s32 loaderWalkerMeshSourcePlanManifest(
		const char *manifest, size_t manifest_len,
		const char *expected_catalog_id,
		const char *archive_path,
		const char *public_geometry,
		s32 existing_source_filenum,
		loader_walker_mesh_source_plan_t *out,
		char *err, size_t err_cap)
{
	mesh_manifest_fields_t fields;
	const char *geometry;

	if (err && err_cap) err[0] = '\0';
	if (!out || !archive_path || !archive_path[0]) {
		meshSourceSetErr(err, err_cap,
			"mesh source plan is missing archive path %s%s", "", "");
		return 0;
	}
	memset(out, 0, sizeof(*out));
	out->source_filenum = existing_source_filenum > 0
		? existing_source_filenum : -1;
	if (!assetPathCopyChecked(out->archive_path, sizeof(out->archive_path),
			archive_path) || !meshManifestParse(manifest, manifest_len, &fields)) {
		meshSourceSetErr(err, err_cap,
			"mesh manifest is malformed, ambiguous, or too long in %s%s",
			archive_path, "");
		return 0;
	}
	if (fields.kind_result > 0 && strcmp(fields.kind, "mesh") != 0) {
		meshSourceSetErr(err, err_cap,
			"mesh manifest kind mismatch in %s: %s", archive_path,
			fields.kind);
		return 0;
	}
	if (fields.id_result > 0 && expected_catalog_id && expected_catalog_id[0]
			&& strcmp(fields.id, expected_catalog_id) != 0) {
		meshSourceSetErr(err, err_cap,
			"mesh manifest ID mismatch in %s: %s", archive_path,
			fields.id);
		return 0;
	}

	geometry = public_geometry && public_geometry[0]
		? public_geometry
		: (fields.geometry_result > 0 ? fields.geometry : "model.obj");
	if (fields.geometry_result > 0 && public_geometry && public_geometry[0]
			&& strcmp(public_geometry, fields.geometry) != 0) {
		meshSourceSetErr(err, err_cap,
			"mesh public/manifest geometry mismatch in %s: %s",
			archive_path, fields.geometry);
		return 0;
	}
	if (!geometryMemberIsSafe(geometry)
			|| !assetPathCopyChecked(out->geometry_member,
				sizeof(out->geometry_member), geometry)
			|| !assetPathJoinChecked(out->source_path,
				sizeof(out->source_path), archive_path, "::", geometry)) {
		meshSourceSetErr(err, err_cap,
			"mesh geometry source is unsafe or too long in %s: %s",
			archive_path, geometry);
		return 0;
	}

	if (fields.symbol_result > 0) {
		s32 resolved;
		if (!fields.source_symbol[0]
				|| (resolved = loaderEnumResolveFileEnum(fields.source_symbol, -1)) <= 0) {
			meshSourceSetErr(err, err_cap,
				"mesh source_filenum_symbol is unresolved in %s: %s",
				archive_path, fields.source_symbol);
			return 0;
		}
		if (existing_source_filenum > 0
				&& resolved != existing_source_filenum) {
			meshSourceSetErr(err, err_cap,
				"mesh source filenum conflicts with stable row in %s: %s",
				archive_path, fields.source_symbol);
			return 0;
		}
		out->source_filenum = resolved;
		out->source_symbol_present = 1;
	}
	return 1;
}

s32 loaderWalkerMeshSourceChangeAllowed(
		asset_load_state_t load_state,
		s32 bundled,
		s32 ref_count,
		s32 has_loaded_data,
		asset_payload_kind_t payload_kind)
{
	if (has_loaded_data || payload_kind != ASSET_PAYLOAD_NONE) return 0;
	if (load_state == ASSET_STATE_REGISTERED
			|| load_state == ASSET_STATE_ENABLED) return 1;
	/* Base registration historically labels a provider-only row LOADED before
	 * any catalog-owned payload exists. This exact sentinel is metadata-only
	 * and remains safe to hydrate from its public typed source. */
	if (load_state == ASSET_STATE_LOADED) {
		return bundled && ref_count == ASSET_REF_BUNDLED;
	}
	return 0;
}
