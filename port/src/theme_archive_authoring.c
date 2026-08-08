/**
 * theme_archive_authoring.c -- atomic self-contained .pdtheme emission.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "asset_archive_policy.h"
#include "asset_archive_writer.h"
#include "fs.h"
#include "modarchive.h"
#include "pdtheme_source.h"
#include "system.h"
#include "theme_archive_authoring.h"

#define THEME_AUTHOR_PATH_CAP 2048
#define THEME_AUTHOR_DESCRIPTOR_CAP 4096

typedef struct theme_role_contract {
	const char *descriptor_key;
	const char *directory;
	const char *extension;
	asset_type_e type;
} theme_role_contract_t;

static const theme_role_contract_t k_Roles[THEME_ARCHIVE_DEP_COUNT] = {
	{ "ui_archive", "ui", ".pdui", ASSET_UI },
	{ "font_archive", "font", ".pdfont", ASSET_FONT },
	{ "audio_archive", "audio", ".pdsfx", ASSET_AUDIO },
	{ "music_archive", "music", ".pdsong", ASSET_AUDIO },
};

static void s_error(char *error, size_t error_cap, const char *fmt, ...)
{
	va_list args;
	if (!error || error_cap == 0) return;
	va_start(args, fmt);
	vsnprintf(error, error_cap, fmt, args);
	va_end(args);
	error[error_cap - 1] = '\0';
}

static s32 s_textSafe(const char *text)
{
	if (!text || !text[0]) return 0;
	for (; *text; text++) {
		if ((unsigned char)*text < 0x20 || *text == '=' || *text == ';'
				|| *text == '#') return 0;
	}
	return 1;
}

static s32 s_catalogIdSafe(const char *id)
{
	s32 colons = 0;
	const char *colon = NULL;
	if (!id || !id[0]) return 0;
	const char *begin = id;
	for (; *id; id++) {
		if (*id == ':') { colons++; colon = id; continue; }
		if (!(isalnum((unsigned char)*id) || *id == '_' || *id == '-'
				|| *id == '.')) return 0;
	}
	return colons == 1 && colon != begin && colon[1] != '\0'
		&& (size_t)(id - begin) < 128;
}

static s32 s_extensionIs(const char *path, const char *expected)
{
	const char *dot = path ? strrchr(path, '.') : NULL;
	if (!dot) return 0;
	while (*dot && *expected) {
		if (tolower((unsigned char)*dot++) !=
				tolower((unsigned char)*expected++)) return 0;
	}
	return *dot == '\0' && *expected == '\0';
}

static s32 s_descriptorCatalogId(const char *text, size_t size,
	char *out, size_t out_cap)
{
	const char *p = text;
	const char *end = text + size;
	if (!text || !out || out_cap == 0) return 0;
	out[0] = '\0';
	while (p < end) {
		const char *line = p;
		const char *line_end = memchr(p, '\n', (size_t)(end - p));
		if (!line_end) line_end = end;
		p = line_end < end ? line_end + 1 : end;
		while (line < line_end && isspace((unsigned char)*line)) line++;
		const char key[] = "catalog_id";
		if ((size_t)(line_end - line) < sizeof(key) - 1
				|| memcmp(line, key, sizeof(key) - 1) != 0) continue;
		line += sizeof(key) - 1;
		while (line < line_end && isspace((unsigned char)*line)) line++;
		if (line >= line_end || *line++ != '=') continue;
		while (line < line_end && isspace((unsigned char)*line)) line++;
		const char *value_end = line_end;
		while (value_end > line && isspace((unsigned char)value_end[-1])) value_end--;
		size_t len = (size_t)(value_end - line);
		if (len == 0 || len >= out_cap) return 0;
		memcpy(out, line, len);
		out[len] = '\0';
		return s_catalogIdSafe(out);
	}
	return 0;
}

static void s_memberSlug(const char *catalog_id, char *out, size_t out_cap)
{
	size_t used = 0;
	for (const char *p = catalog_id; p && *p && used + 1 < out_cap; p++) {
		char c = (char)tolower((unsigned char)*p);
		out[used++] = isalnum((unsigned char)c) || c == '_' || c == '-'
			? c : '_';
	}
	out[used] = '\0';
}

static s32 s_replaceFile(const char *source, const char *destination)
{
#ifdef _WIN32
	return MoveFileExA(source, destination,
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
	return rename(source, destination) == 0;
#endif
}

s32 themeArchiveAuthor(const theme_archive_author_request_t *request,
	char *error, size_t error_cap)
{
	pdtheme_source_info_t parsed;
	char parse_error[256];
	char descriptor[THEME_AUTHOR_DESCRIPTOR_CAP];
	char candidate[THEME_AUTHOR_PATH_CAP] = { 0 };
	char member_paths[THEME_ARCHIVE_DEP_COUNT][256];
	void *dependency_bytes[THEME_ARCHIVE_DEP_COUNT] = { 0 };
	u32 dependency_sizes[THEME_ARCHIVE_DEP_COUNT] = { 0 };
	const theme_archive_dependency_t *by_role[THEME_ARCHIVE_DEP_COUNT] = { 0 };
	mod_archive_writer_t *archive = NULL;
	asset_archive_writer_t writer;
	s32 descriptor_len;
	s32 result = 0;

	if (error && error_cap) error[0] = '\0';
	if (!request || !request->archive_path || !request->archive_path[0]
			|| !s_catalogIdSafe(request->catalog_id)
			|| !s_textSafe(request->display_name) || !request->theme_json
			|| request->theme_json_size == 0
			|| request->dependency_count > THEME_ARCHIVE_DEP_COUNT
			|| (request->dependency_count && !request->dependencies)) {
		s_error(error, error_cap, "theme archive path, catalog ID, name, and JSON are required");
		return 0;
	}
	if (!pdthemeSourceParse(request->theme_json, request->theme_json_size,
			request->catalog_id, &parsed, parse_error, sizeof(parse_error))) {
		s_error(error, error_cap, "theme JSON is invalid: %s",
			parse_error[0] ? parse_error : "strict source rejection");
		return 0;
	}

	for (size_t i = 0; i < request->dependency_count; i++) {
		const theme_archive_dependency_t *dep = &request->dependencies[i];
		if (dep->role < 0 || dep->role >= THEME_ARCHIVE_DEP_COUNT
				|| by_role[dep->role] || !s_catalogIdSafe(dep->catalog_id)
				|| !dep->archive_path || !dep->archive_path[0]) {
			s_error(error, error_cap, "theme dependency role is duplicate or invalid");
			goto cleanup;
		}
		const theme_role_contract_t *contract = &k_Roles[dep->role];
		if (!s_extensionIs(dep->archive_path, contract->extension)
				|| assetArchiveTypeForPath(dep->archive_path) != contract->type) {
			s_error(error, error_cap, "theme dependency %s has the wrong archive type",
				dep->catalog_id);
			goto cleanup;
		}
		dependency_bytes[dep->role] = fsFileLoad(dep->archive_path,
			&dependency_sizes[dep->role]);
		if (!dependency_bytes[dep->role] || !dependency_sizes[dep->role]) {
			s_error(error, error_cap, "theme dependency %s is unreadable", dep->catalog_id);
			goto cleanup;
		}
		if (assetArchiveValidateBytes(dependency_bytes[dep->role],
				dependency_sizes[dep->role], dep->archive_path,
				ASSET_ARCHIVE_VALIDATE_RELEASE, parse_error,
				sizeof(parse_error)) != 0) {
			s_error(error, error_cap, "theme dependency %s is invalid: %s",
				dep->catalog_id, parse_error);
			goto cleanup;
		}
		u32 descriptor_size = 0;
		const char *descriptor_name = NULL;
		char *source_descriptor = assetArchiveExtractDescriptorMemAlloc(
			dependency_bytes[dep->role], dependency_sizes[dep->role],
			dep->archive_path, ASSET_ARCHIVE_VALIDATE_RELEASE,
			&descriptor_size, &descriptor_name);
		char declared_id[128];
		if (!source_descriptor || !s_descriptorCatalogId(source_descriptor,
				descriptor_size, declared_id, sizeof(declared_id))
				|| strcmp(declared_id, dep->catalog_id) != 0) {
			free(source_descriptor);
			s_error(error, error_cap,
				"theme dependency %s descriptor identity does not match", dep->catalog_id);
			goto cleanup;
		}
		free(source_descriptor);
		char slug[128];
		s_memberSlug(dep->catalog_id, slug, sizeof(slug));
		s32 member_len = snprintf(member_paths[dep->role], sizeof(member_paths[dep->role]),
			"dependencies/assets/%s/%s%s", contract->directory, slug,
			contract->extension);
		if (member_len <= 0
				|| (size_t)member_len >= sizeof(member_paths[dep->role])) {
			s_error(error, error_cap, "theme dependency member path is too long");
			goto cleanup;
		}
		by_role[dep->role] = dep;
	}

	descriptor_len = snprintf(descriptor, sizeof(descriptor),
		"; Creator-authored self-contained theme asset\n"
		"[theme]\n"
		"catalog_id = %s\n"
		"name = %s\n"
		"theme_file = theme.json\n",
		request->catalog_id, request->display_name);
	if (descriptor_len <= 0 || (size_t)descriptor_len >= sizeof(descriptor)) {
		s_error(error, error_cap, "theme descriptor is too large");
		goto cleanup;
	}
	for (s32 role = 0; role < THEME_ARCHIVE_DEP_COUNT; role++) {
		if (!by_role[role]) continue;
		s32 n = snprintf(descriptor + descriptor_len,
			sizeof(descriptor) - (size_t)descriptor_len, "%s = %s\n",
			k_Roles[role].descriptor_key, member_paths[role]);
		if (n <= 0 || (size_t)n >= sizeof(descriptor) - (size_t)descriptor_len) {
			s_error(error, error_cap, "theme descriptor dependencies are too large");
			goto cleanup;
		}
		descriptor_len += n;
	}

	s32 candidate_len = snprintf(candidate, sizeof(candidate), "%s.candidate.pdtheme",
		request->archive_path);
	if (candidate_len <= 0 || (size_t)candidate_len >= sizeof(candidate)) {
		s_error(error, error_cap, "theme archive path is too long");
		goto cleanup;
	}
	remove(candidate);
	archive = modArchiveBegin(candidate);
	if (!archive || assetArchiveWriterInit(&writer, archive, "theme",
			request->catalog_id) != MODARCHIVE_OK) {
		s_error(error, error_cap, "could not create staged .pdtheme archive");
		goto cleanup;
	}
	assetArchiveWriterSetProvenance(&writer, "Theme Editor", "creator", -1,
		"creator-authored");
	if (assetArchiveWriterAddDescriptor(&writer, "theme.ini", descriptor,
			(u32)descriptor_len) != MODARCHIVE_OK
			|| assetArchiveWriterAddPublicMem(&writer, "theme.json",
				request->theme_json, (u32)request->theme_json_size,
				"theme-source") != MODARCHIVE_OK) {
		s_error(error, error_cap, "could not write theme public source");
		goto cleanup;
	}
	for (s32 role = 0; role < THEME_ARCHIVE_DEP_COUNT; role++) {
		if (!by_role[role]) continue;
		if (assetArchiveWriterAddPublicMem(&writer, member_paths[role],
				dependency_bytes[role], dependency_sizes[role],
				k_Roles[role].descriptor_key) != MODARCHIVE_OK) {
			s_error(error, error_cap, "could not embed theme dependency %s",
				by_role[role]->catalog_id);
			goto cleanup;
		}
	}
	if (assetArchiveWriterFinishMetadata(&writer) != MODARCHIVE_OK
			|| modArchiveFinish(archive) != MODARCHIVE_OK) {
		archive = NULL;
		s_error(error, error_cap, "could not finish staged .pdtheme archive");
		goto cleanup;
	}
	archive = NULL;
	if (assetArchiveValidateFile(candidate, ASSET_ARCHIVE_VALIDATE_RELEASE,
			parse_error, sizeof(parse_error)) != 0) {
		s_error(error, error_cap, "staged .pdtheme validation failed: %s",
			parse_error[0] ? parse_error : "unknown archive error");
		goto cleanup;
	}
	if (!s_replaceFile(candidate, request->archive_path)) {
		s_error(error, error_cap, "could not atomically replace the .pdtheme archive");
		goto cleanup;
	}
	result = 1;

cleanup:
	if (archive) modArchiveAbort(archive);
	for (s32 role = 0; role < THEME_ARCHIVE_DEP_COUNT; role++) {
		if (dependency_bytes[role]) sysMemFree(dependency_bytes[role]);
	}
	if (!result && candidate[0]) remove(candidate);
	return result;
}
