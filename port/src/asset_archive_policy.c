/**
 * asset_archive_policy.c -- shared typed asset archive layout rules.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset_archive_policy.h"

typedef struct asset_archive_family_rule {
	const char *extension;
	const char *descriptor;
	const char *legacy_descriptor;
	asset_type_e type;
} asset_archive_family_rule_t;

typedef struct asset_archive_mem_validate_ctx {
	const char *archive_name;
	const asset_archive_family_rule_t *rule;
	asset_archive_validation_mode_e mode;
	u32 depth;
	s32 canonical_descriptor_seen;
	s32 legacy_descriptor_seen;
	s32 failed;
	char *err;
	size_t err_cap;
} asset_archive_mem_validate_ctx_t;

typedef struct asset_archive_mem_ref_ctx {
	const void *archive_bytes;
	u32 archive_size;
	const char *archive_name;
	asset_archive_validation_mode_e mode;
	u32 depth;
	s32 failed;
	char *err;
	size_t err_cap;
} asset_archive_mem_ref_ctx_t;

#define ASSET_ARCHIVE_MAX_REF_DEPTH 8
#define ASSET_ARCHIVE_TEXT_SCAN_LIMIT (4u * 1024u * 1024u)

static const asset_archive_family_rule_t s_FamilyRules[] = {
	{ ".pdweapon",     "weapon.ini",     NULL,        ASSET_WEAPON },
	{ ".pdprojectile", "projectile.ini", NULL,        ASSET_PROJECTILE },
	{ ".pdentity",     "entity.ini",     NULL,        ASSET_ENTITY },
	{ ".pdmaterial",   "material.ini",   NULL,        ASSET_MATERIAL },
	{ ".pdtexture",    "texture.ini",    NULL,        ASSET_TEXTURE },
	{ ".pdcharacter",  "character.ini",  NULL,        ASSET_CHARACTER },
	{ ".pdhead",       "head.ini",       NULL,        ASSET_HEAD },
	{ ".pdbody",       "body.ini",       NULL,        ASSET_BODY },
	{ ".pdarena",      "arena.ini",      NULL,        ASSET_ARENA },
	{ ".pdscenario",   "scenario.ini",   NULL,        ASSET_SCENARIO },
	{ ".pdmesh",       "mesh.ini",       "model.ini", ASSET_MODEL },
	{ ".pdanim",       "animation.ini",  NULL,        ASSET_ANIMATION },
	{ ".pdsfx",        "sound.ini",      NULL,        ASSET_AUDIO },
	{ ".pdvoice",      "voice.ini",      NULL,        ASSET_AUDIO },
	{ ".pdsong",       "music.ini",      NULL,        ASSET_AUDIO },
	{ ".pdui",         "ui.ini",         NULL,        ASSET_UI },
	{ ".pdfont",       "font.ini",       NULL,        ASSET_FONT },
	{ ".pdlang",       "lang.ini",       NULL,        ASSET_LANG },
	{ ".pdskin",       "skin.ini",       NULL,        ASSET_SKIN },
	{ ".pdeffect",     "effect.ini",     NULL,        ASSET_EFFECT },
	{ ".pdprop",       "prop.ini",       NULL,        ASSET_PROP },
	{ ".pdvehicle",    "vehicle.ini",    NULL,        ASSET_VEHICLE },
	{ ".pdmission",    "mission.ini",    NULL,        ASSET_MISSION },
	{ ".pdgamemode",   "gamemode.ini",   NULL,        ASSET_GAMEMODE },
	{ ".pdbotprofile", "botprofile.ini", NULL,        ASSET_BOT_PROFILE },
	{ ".pdhud",        "hud.ini",        NULL,        ASSET_HUD },
	{ ".pdtheme",      "theme.ini",      NULL,        ASSET_THEME },
};

static void setErr(char *err, size_t err_cap, const char *fmt, ...)
{
	if (!err || err_cap == 0) return;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(err, err_cap, fmt, ap);
	va_end(ap);
	err[err_cap - 1] = '\0';
}

static s32 endsWithNoCase(const char *value, const char *suffix)
{
	if (!value || !suffix) return 0;
	size_t vlen = strlen(value);
	size_t slen = strlen(suffix);
	if (slen > vlen) return 0;
	value += vlen - slen;
	for (size_t i = 0; i < slen; i++) {
		if (tolower((unsigned char)value[i]) !=
				tolower((unsigned char)suffix[i])) {
			return 0;
		}
	}
	return 1;
}

static s32 pathHasSlash(const char *path)
{
	return path && (strchr(path, '/') || strchr(path, '\\'));
}

static void copyTrimmed(const char *begin, const char *end, char *out, size_t out_cap)
{
	if (!out || out_cap == 0) return;
	out[0] = '\0';
	if (!begin) return;
	if (!end) end = begin + strlen(begin);
	while (begin < end && isspace((unsigned char)*begin)) begin++;
	while (end > begin && isspace((unsigned char)end[-1])) end--;
	size_t len = (size_t)(end - begin);
	if (len >= out_cap) len = out_cap - 1;
	memcpy(out, begin, len);
	out[len] = '\0';
}

static s32 keyEqualsNoCase(const char *key, const char *wanted)
{
	if (!key || !wanted) return 0;
	while (*key && *wanted) {
		if (tolower((unsigned char)*key) !=
				tolower((unsigned char)*wanted)) {
			return 0;
		}
		key++;
		wanted++;
	}
	return *key == '\0' && *wanted == '\0';
}

static s32 keyEndsWithNoCase(const char *key, const char *suffix)
{
	return endsWithNoCase(key, suffix);
}

static s32 valueStartsWithNoCase(const char *value, const char *prefix)
{
	if (!value || !prefix) return 0;
	while (*prefix) {
		if (!*value ||
				tolower((unsigned char)*value) !=
				tolower((unsigned char)*prefix)) {
			return 0;
		}
		value++;
		prefix++;
	}
	return 1;
}

static s32 valueLooksLikeFileReference(const char *value)
{
	if (!value || !value[0]) return 0;
	if (valueStartsWithNoCase(value, "data:")) return 0;
	if (valueStartsWithNoCase(value, "http:") ||
			valueStartsWithNoCase(value, "https:")) {
		return 0;
	}
	if (strchr(value, '.') || strchr(value, '/') || strchr(value, '\\')) {
		return 1;
	}
	return 0;
}

static s32 iniKeyLooksLikeFileReference(const char *key)
{
	if (!key || !key[0]) return 0;
	if (keyEqualsNoCase(key, "catalog_id") ||
			keyEqualsNoCase(key, "asset_id") ||
			keyEndsWithNoCase(key, "_id") ||
			keyEndsWithNoCase(key, "_format") ||
			keyEndsWithNoCase(key, "_closure") ||
			keyEndsWithNoCase(key, "_class") ||
			keyEndsWithNoCase(key, "_role") ||
			keyEndsWithNoCase(key, "_tag") ||
			keyEndsWithNoCase(key, "_tags")) {
		return 0;
	}
	if (keyEndsWithNoCase(key, "_file") ||
			keyEndsWithNoCase(key, "_path") ||
			keyEndsWithNoCase(key, "_graph") ||
			keyEndsWithNoCase(key, "_payloads") ||
			keyEndsWithNoCase(key, "_archive") ||
			keyEqualsNoCase(key, "manifest") ||
			keyEqualsNoCase(key, "model") ||
			keyEqualsNoCase(key, "geometry") ||
			keyEqualsNoCase(key, "texture") ||
			keyEqualsNoCase(key, "font") ||
			keyEqualsNoCase(key, "strings") ||
			keyEqualsNoCase(key, "rooms") ||
			keyEqualsNoCase(key, "pads") ||
			keyEqualsNoCase(key, "setup") ||
			keyEqualsNoCase(key, "mpsetup") ||
			keyEqualsNoCase(key, "props") ||
			keyEqualsNoCase(key, "objectives") ||
			keyEqualsNoCase(key, "tiles") ||
			keyEqualsNoCase(key, "collision") ||
			keyEqualsNoCase(key, "material") ||
			keyEqualsNoCase(key, "animation") ||
			keyEqualsNoCase(key, "music") ||
			keyEqualsNoCase(key, "midi") ||
			keyEqualsNoCase(key, "events") ||
			keyEqualsNoCase(key, "bodyfile") ||
			keyEqualsNoCase(key, "bindings") ||
			keyEqualsNoCase(key, "composition")) {
		return 1;
	}
	return 0;
}

static s32 jsonKeyLooksLikeFileReference(const char *key, const char *value)
{
	if (!key || !key[0] || !valueLooksLikeFileReference(value)) return 0;
	if (keyEqualsNoCase(key, "catalog_id") ||
			keyEqualsNoCase(key, "asset_id") ||
			keyEqualsNoCase(key, "canonical_sha256") ||
			keyEqualsNoCase(key, "schema") ||
			keyEqualsNoCase(key, "type") ||
			keyEndsWithNoCase(key, "_id") ||
			keyEndsWithNoCase(key, "_sha256")) {
		return 0;
	}
	if (keyEqualsNoCase(key, "uri") ||
			keyEqualsNoCase(key, "archive") ||
			keyEqualsNoCase(key, "entry") ||
			keyEqualsNoCase(key, "file") ||
			keyEqualsNoCase(key, "path") ||
			keyEqualsNoCase(key, "data") ||
			keyEndsWithNoCase(key, "_file") ||
			keyEndsWithNoCase(key, "_path") ||
			keyEndsWithNoCase(key, "_graph") ||
			keyEndsWithNoCase(key, "_payloads") ||
			keyEndsWithNoCase(key, "_archive") ||
			keyEndsWithNoCase(key, "_uri")) {
		return 1;
	}
	return 0;
}

static s32 pathContainsParentSegment(const char *path)
{
	if (!path) return 0;
	const char *p = path;
	while (*p) {
		while (*p == '/' || *p == '\\') p++;
		const char *seg = p;
		while (*p && *p != '/' && *p != '\\') p++;
		if ((size_t)(p - seg) == 2 && seg[0] == '.' && seg[1] == '.') {
			return 1;
		}
	}
	return 0;
}

static void normalizedRefPath(const char *value, char *out, size_t out_cap)
{
	if (!out || out_cap == 0) return;
	out[0] = '\0';
	if (!value) return;
	copyTrimmed(value, NULL, out, out_cap);
	for (char *p = out; *p; p++) {
		if (*p == '\\') *p = '/';
	}
}

static s32 refPathIsUnsafe(const char *ref)
{
	if (!ref || !ref[0]) return 1;
	if (valueStartsWithNoCase(ref, "data:")) return 0;
	if (ref[0] == '/' || ref[0] == '\\') return 1;
	if (strstr(ref, "::")) return 1;
	if (strchr(ref, ':')) return 1;
	if (pathContainsParentSegment(ref)) return 1;
	return 0;
}

static void entryDirFor(const char *entry_name, char *out, size_t out_cap)
{
	if (!out || out_cap == 0) return;
	out[0] = '\0';
	if (!entry_name) return;
	const char *slash = strrchr(entry_name, '/');
	if (!slash) return;
	size_t len = (size_t)(slash - entry_name + 1);
	if (len >= out_cap) len = out_cap - 1;
	memcpy(out, entry_name, len);
	out[len] = '\0';
}

static s32 entryIsMetaPath(const char *entry_name)
{
	return entry_name && strncmp(entry_name, ASSET_ARCHIVE_META_DIR "/",
		strlen(ASSET_ARCHIVE_META_DIR) + 1) == 0;
}

static const asset_archive_family_rule_t *ruleForPath(const char *path)
{
	if (!path) return NULL;
	for (u32 i = 0; i < (u32)(sizeof(s_FamilyRules) / sizeof(s_FamilyRules[0])); i++) {
		if (endsWithNoCase(path, s_FamilyRules[i].extension)) {
			return &s_FamilyRules[i];
		}
	}
	return NULL;
}

s32 assetArchivePathIsDeprecated(const char *path)
{
	return endsWithNoCase(path, ".pdwpn");
}

s32 assetArchivePathIsTyped(const char *path)
{
	return ruleForPath(path) != NULL;
}

asset_type_e assetArchiveTypeForPath(const char *path)
{
	const asset_archive_family_rule_t *rule = ruleForPath(path);
	return rule ? rule->type : ASSET_NONE;
}

const char *assetArchiveDescriptorForPath(const char *path)
{
	const asset_archive_family_rule_t *rule = ruleForPath(path);
	return rule ? rule->descriptor : NULL;
}

const char *assetArchiveLegacyDescriptorForPath(const char *path)
{
	const asset_archive_family_rule_t *rule = ruleForPath(path);
	return rule ? rule->legacy_descriptor : NULL;
}

void assetArchiveMetaPath(const char *leaf, char *out, size_t out_cap)
{
	if (!out || out_cap == 0) return;
	if (!leaf || !leaf[0]) {
		out[0] = '\0';
		return;
	}
	if (strncmp(leaf, ASSET_ARCHIVE_META_DIR "/", strlen(ASSET_ARCHIVE_META_DIR) + 1) == 0) {
		snprintf(out, out_cap, "%s", leaf);
	} else {
		snprintf(out, out_cap, ASSET_ARCHIVE_META_DIR "/%s", leaf);
	}
	out[out_cap - 1] = '\0';
}

s32 assetArchiveFindMetadataEntry(mod_archive_t *archive, const char *leaf)
{
	if (!archive || !leaf || !leaf[0]) return -1;
	char meta_path[128];
	assetArchiveMetaPath(leaf, meta_path, sizeof(meta_path));
	s32 idx = modArchiveFindEntry(archive, meta_path);
	if (idx >= 0) {
		return idx;
	}
	return modArchiveFindEntry(archive, leaf);
}

s32 assetArchiveEntryIsForbiddenBinPayload(const char *entry_name)
{
	if (!entry_name) return 0;
	for (const char *p = entry_name; *p; p++) {
		if (p[0] != '.') continue;
		if ((p[1] == 'b' || p[1] == 'B') &&
		    (p[2] == 'i' || p[2] == 'I') &&
		    (p[3] == 'n' || p[3] == 'N') &&
		    (p[4] == '\0' || p[4] == '.' || p[4] == '/' || p[4] == '\\')) {
			return 1;
		}
	}
	return 0;
}

s32 assetArchiveEntryIsForbiddenTsvPayload(const char *entry_name)
{
	return entry_name && endsWithNoCase(entry_name, ".tsv");
}

s32 assetArchiveEntryIsRootMachineMetadata(const char *entry_name)
{
	if (!entry_name || !entry_name[0] || pathHasSlash(entry_name)) {
		return 0;
	}
	if (strcmp(entry_name, "manifest.json") == 0 ||
	    strcmp(entry_name, "inventory.json") == 0 ||
	    strcmp(entry_name, "provenance.json") == 0 ||
	    strcmp(entry_name, "validation.json") == 0 ||
	    strcmp(entry_name, "hashes.json") == 0 ||
	    strcmp(entry_name, "source-format.json") == 0) {
		return 1;
	}
	return endsWithNoCase(entry_name, ".sha256");
}

s32 assetArchiveFindDescriptorEntry(mod_archive_t *archive,
                                    const char *archive_path,
                                    asset_archive_validation_mode_e mode,
                                    const char **out_entry_name)
{
	if (out_entry_name) *out_entry_name = NULL;
	if (!archive) return -1;

	const asset_archive_family_rule_t *rule = ruleForPath(archive_path);
	if (!rule) return -1;

	s32 idx = modArchiveFindEntry(archive, rule->descriptor);
	if (idx >= 0) {
		if (out_entry_name) *out_entry_name = rule->descriptor;
		return idx;
	}

	if (mode == ASSET_ARCHIVE_VALIDATE_MIGRATION && rule->legacy_descriptor) {
		idx = modArchiveFindEntry(archive, rule->legacy_descriptor);
		if (idx >= 0) {
			if (out_entry_name) *out_entry_name = rule->legacy_descriptor;
			return idx;
		}
	}

	return -1;
}

static s32 assetArchiveValidateBytesDepth(const void *archive_bytes, u32 archive_size,
                                          const char *archive_name,
                                          asset_archive_validation_mode_e mode,
                                          u32 depth,
                                          char *err, size_t err_cap);

char *assetArchiveExtractDescriptorMemAlloc(const void *archive_bytes,
                                            u32 archive_size,
                                            const char *archive_name,
                                            asset_archive_validation_mode_e mode,
                                            u32 *out_size,
                                            const char **out_entry_name)
{
	if (out_size) *out_size = 0;
	if (out_entry_name) *out_entry_name = NULL;

	const asset_archive_family_rule_t *rule = ruleForPath(archive_name);
	if (!rule || !archive_bytes || archive_size == 0) {
		return NULL;
	}

	char *bytes = (char *)modArchiveExtractMemAlloc(archive_bytes, archive_size,
		rule->descriptor, out_size);
	if (bytes) {
		if (out_entry_name) *out_entry_name = rule->descriptor;
		return bytes;
	}

	if (mode == ASSET_ARCHIVE_VALIDATE_MIGRATION && rule->legacy_descriptor) {
		bytes = (char *)modArchiveExtractMemAlloc(archive_bytes, archive_size,
			rule->legacy_descriptor, out_size);
		if (bytes && out_entry_name) {
			*out_entry_name = rule->legacy_descriptor;
		}
	}

	return bytes;
}

static char *extractOpenedEntryTextAlloc(mod_archive_t *archive, s32 idx,
                                         u32 *out_size)
{
	if (out_size) *out_size = 0;
	if (!archive || idx < 0) return NULL;
	u32 size = 0;
	void *raw = modArchiveExtractAlloc(archive, idx, &size);
	if (!raw) return NULL;
	if (size > ASSET_ARCHIVE_TEXT_SCAN_LIMIT) {
		if (out_size) *out_size = size;
		free(raw);
		return NULL;
	}
	char *text = (char *)malloc((size_t)size + 1);
	if (!text) {
		free(raw);
		return NULL;
	}
	memcpy(text, raw, size);
	text[size] = '\0';
	free(raw);
	if (out_size) *out_size = size;
	return text;
}

static char *extractMemEntryTextAlloc(const void *archive_bytes, u32 archive_size,
                                      const char *entry_name, u32 *out_size)
{
	if (out_size) *out_size = 0;
	if (!archive_bytes || archive_size == 0 || !entry_name) return NULL;
	u32 size = 0;
	void *raw = modArchiveExtractMemAlloc(archive_bytes, archive_size,
		entry_name, &size);
	if (!raw) return NULL;
	if (size > ASSET_ARCHIVE_TEXT_SCAN_LIMIT) {
		if (out_size) *out_size = size;
		free(raw);
		return NULL;
	}
	char *text = (char *)malloc((size_t)size + 1);
	if (!text) {
		free(raw);
		return NULL;
	}
	memcpy(text, raw, size);
	text[size] = '\0';
	free(raw);
	if (out_size) *out_size = size;
	return text;
}

static s32 openedArchiveHasEntry(mod_archive_t *archive, const char *entry)
{
	return archive && entry && entry[0] && modArchiveFindEntry(archive, entry) >= 0;
}

static s32 memArchiveHasEntry(const void *archive_bytes, u32 archive_size,
                              const char *entry)
{
	if (!archive_bytes || archive_size == 0 || !entry || !entry[0]) return 0;
	u32 size = 0;
	void *bytes = modArchiveExtractMemAlloc(archive_bytes, archive_size,
		entry, &size);
	if (!bytes) return 0;
	free(bytes);
	(void)size;
	return 1;
}

static void refRelativeToEntry(const char *entry_name, const char *ref,
                               char *out, size_t out_cap)
{
	if (!out || out_cap == 0) return;
	out[0] = '\0';
	if (!ref) return;
	char dir[256];
	entryDirFor(entry_name, dir, sizeof(dir));
	if (dir[0]) {
		snprintf(out, out_cap, "%s%s", dir, ref);
		out[out_cap - 1] = '\0';
	} else {
		snprintf(out, out_cap, "%s", ref);
		out[out_cap - 1] = '\0';
	}
}

static s32 validateOpenedRefExists(mod_archive_t *archive,
                                   const char *archive_path,
                                   const char *entry_name,
                                   const char *ref_context,
                                   const char *ref_value,
                                   char *err, size_t err_cap)
{
	char ref[FS_MAXPATH + 1];
	normalizedRefPath(ref_value, ref, sizeof(ref));
	if (!ref[0] || valueStartsWithNoCase(ref, "data:")) return 0;
	if (refPathIsUnsafe(ref)) {
		setErr(err, err_cap, "%s has unsafe internal reference %s in %s: %s",
			archive_path, ref_context ? ref_context : "reference",
			entry_name ? entry_name : "(entry)", ref);
		return -1;
	}
	if (assetArchiveEntryIsForbiddenBinPayload(ref)) {
		setErr(err, err_cap, "%s references forbidden authored .bin payload in %s: %s",
			archive_path, entry_name ? entry_name : "(entry)", ref);
		return -1;
	}

	char rel[FS_MAXPATH + 1];
	refRelativeToEntry(entry_name, ref, rel, sizeof(rel));
	if (openedArchiveHasEntry(archive, ref) ||
			(rel[0] && openedArchiveHasEntry(archive, rel))) {
		return 0;
	}

	setErr(err, err_cap, "%s references missing internal file in %s: %s",
		archive_path, entry_name ? entry_name : "(entry)", ref);
	return -1;
}

static s32 validateMemRefExists(const void *archive_bytes, u32 archive_size,
                                const char *archive_name,
                                const char *entry_name,
                                const char *ref_context,
                                const char *ref_value,
                                char *err, size_t err_cap)
{
	char ref[FS_MAXPATH + 1];
	normalizedRefPath(ref_value, ref, sizeof(ref));
	if (!ref[0] || valueStartsWithNoCase(ref, "data:")) return 0;
	if (refPathIsUnsafe(ref)) {
		setErr(err, err_cap, "%s has unsafe internal reference %s in %s: %s",
			archive_name, ref_context ? ref_context : "reference",
			entry_name ? entry_name : "(entry)", ref);
		return -1;
	}
	if (assetArchiveEntryIsForbiddenBinPayload(ref)) {
		setErr(err, err_cap, "%s references forbidden authored .bin payload in %s: %s",
			archive_name, entry_name ? entry_name : "(entry)", ref);
		return -1;
	}

	char rel[FS_MAXPATH + 1];
	refRelativeToEntry(entry_name, ref, rel, sizeof(rel));
	if (memArchiveHasEntry(archive_bytes, archive_size, ref) ||
			(rel[0] && memArchiveHasEntry(archive_bytes, archive_size, rel))) {
		return 0;
	}

	setErr(err, err_cap, "%s references missing internal file in %s: %s",
		archive_name, entry_name ? entry_name : "(entry)", ref);
	return -1;
}

static s32 validateIniTextRefsOpened(mod_archive_t *archive,
                                     const char *archive_path,
                                     const char *entry_name,
                                     const char *text,
                                     char *err, size_t err_cap)
{
	const char *line = text;
	while (line && *line) {
		const char *next = strchr(line, '\n');
		const char *end = next ? next : line + strlen(line);
		char key[128];
		char value[FS_MAXPATH + 1];
		const char *eq = memchr(line, '=', (size_t)(end - line));
		if (eq) {
			copyTrimmed(line, eq, key, sizeof(key));
			copyTrimmed(eq + 1, end, value, sizeof(value));
			if (iniKeyLooksLikeFileReference(key) &&
					valueLooksLikeFileReference(value)) {
				if (validateOpenedRefExists(archive, archive_path,
						entry_name, key, value, err, err_cap) != 0) {
					return -1;
				}
			}
		}
		line = next ? next + 1 : NULL;
	}
	return 0;
}

static s32 validateIniTextRefsMem(const void *archive_bytes, u32 archive_size,
                                  const char *archive_name,
                                  const char *entry_name,
                                  const char *text,
                                  char *err, size_t err_cap)
{
	const char *line = text;
	while (line && *line) {
		const char *next = strchr(line, '\n');
		const char *end = next ? next : line + strlen(line);
		char key[128];
		char value[FS_MAXPATH + 1];
		const char *eq = memchr(line, '=', (size_t)(end - line));
		if (eq) {
			copyTrimmed(line, eq, key, sizeof(key));
			copyTrimmed(eq + 1, end, value, sizeof(value));
			if (iniKeyLooksLikeFileReference(key) &&
					valueLooksLikeFileReference(value)) {
				if (validateMemRefExists(archive_bytes, archive_size,
						archive_name, entry_name, key, value,
						err, err_cap) != 0) {
					return -1;
				}
			}
		}
		line = next ? next + 1 : NULL;
	}
	return 0;
}

static const char *jsonReadString(const char *p, char *out, size_t out_cap)
{
	if (!p || *p != '"' || !out || out_cap == 0) return NULL;
	p++;
	size_t used = 0;
	while (*p && *p != '"') {
		char c = *p++;
		if (c == '\\' && *p) {
			c = *p++;
		}
		if (used + 1 < out_cap) {
			out[used++] = c;
		}
	}
	if (*p != '"') return NULL;
	out[used] = '\0';
	return p + 1;
}

static const char *strstrRange(const char *begin, const char *end,
                               const char *needle)
{
	if (!begin || !needle || !needle[0]) return NULL;
	size_t nlen = strlen(needle);
	for (const char *p = begin; *p; p++) {
		if (end && p + nlen > end) return NULL;
		if (strncmp(p, needle, nlen) == 0) return p;
	}
	return NULL;
}

static const char *jsonStringValueInRange(const char *begin, const char *end,
                                          const char *key,
                                          char *out, size_t out_cap)
{
	if (!begin || !key || !out || out_cap == 0) return NULL;
	char needle[160];
	snprintf(needle, sizeof(needle), "\"%s\"", key);
	const char *p = strstrRange(begin, end, needle);
	if (!p) return NULL;
	p += strlen(needle);
	while (*p && (!end || p < end) && isspace((unsigned char)*p)) p++;
	if (!*p || (end && p >= end) || *p != ':') return NULL;
	p++;
	while (*p && (!end || p < end) && isspace((unsigned char)*p)) p++;
	if (!*p || (end && p >= end) || *p != '"') return NULL;
	return jsonReadString(p, out, out_cap);
}

static s32 dependencyRangeHasFallback(const char *begin, const char *end)
{
	const char *fallback = strstrRange(begin, end, "\"fallback\"");
	if (!fallback) return 0;
	char id[CATALOG_ID_LEN];
	char reason[256];
	if (!jsonStringValueInRange(fallback, end, "id", id, sizeof(id))) {
		return 0;
	}
	if (!jsonStringValueInRange(fallback, end, "reason", reason, sizeof(reason))) {
		return 0;
	}
	return id[0] && reason[0];
}

static s32 dependencySchemaHasRequiredFields(const char *schema)
{
	static const char *const fields[] = {
		"role", "type", "id", "archive", "required", "version",
		"sha256", "fallback.id", "fallback.reason",
	};
	if (!schema) return 0;
	for (u32 i = 0; i < (u32)(sizeof(fields) / sizeof(fields[0])); i++) {
		char needle[64];
		snprintf(needle, sizeof(needle), "\"%s\"", fields[i]);
		if (!strstr(schema, needle)) {
			return 0;
		}
	}
	return 1;
}

typedef s32 (*dependency_archive_exists_fn)(void *userdata, const char *entry);

static s32 validateManifestDependencyText(const char *archive_name,
                                          const char *text,
                                          dependency_archive_exists_fn exists,
                                          void *userdata,
                                          char *err, size_t err_cap)
{
	const char *deps = strstr(text ? text : "", "\"dependencies\"");
	if (!deps) return 0;

	const char *schema = strstr(deps, "\"dependency_schema\"");
	const char *first_archive = strstrRange(deps, schema, "\"archive\"");
	if (!first_archive) return 0;

	if (!dependencySchemaHasRequiredFields(schema)) {
		setErr(err, err_cap,
			"%s dependency manifest is missing required dependency_schema fields",
			archive_name ? archive_name : "(archive)");
		return -1;
	}

	const char *p = deps;
	while ((p = strstrRange(p, schema, "\"archive\"")) != NULL) {
		char ref[FS_MAXPATH + 1];
		const char *after = jsonStringValueInRange(p, schema, "archive",
			ref, sizeof(ref));
		if (!after) return 0;
		const char *next = strstrRange(after, schema, "\"archive\"");
		const char *range_end = next ? next : schema;

		char normalized[FS_MAXPATH + 1];
		normalizedRefPath(ref, normalized, sizeof(normalized));
		if (normalized[0] && refPathIsUnsafe(normalized)) {
			setErr(err, err_cap,
				"%s has unsafe dependency archive reference: %s",
				archive_name ? archive_name : "(archive)",
				normalized);
			return -1;
		}
		if (normalized[0] && exists && exists(userdata, normalized)) {
			p = after;
			continue;
		}
		if (dependencyRangeHasFallback(after, range_end)) {
			p = after;
			continue;
		}
		setErr(err, err_cap,
			"%s dependency archive missing and no explicit fallback: %s",
			archive_name ? archive_name : "(archive)",
			normalized[0] ? normalized : "(empty)");
		return -1;
	}

	return 0;
}

static s32 openedDependencyArchiveExists(void *userdata, const char *entry)
{
	return openedArchiveHasEntry((mod_archive_t *)userdata, entry);
}

typedef struct mem_dependency_exists_ctx {
	const void *archive_bytes;
	u32 archive_size;
} mem_dependency_exists_ctx_t;

static s32 memDependencyArchiveExists(void *userdata, const char *entry)
{
	mem_dependency_exists_ctx_t *ctx = (mem_dependency_exists_ctx_t *)userdata;
	if (!ctx) return 0;
	return memArchiveHasEntry(ctx->archive_bytes, ctx->archive_size, entry);
}

static s32 validateOpenedManifestDependencies(mod_archive_t *archive,
                                              const char *archive_path,
                                              char *err, size_t err_cap)
{
	s32 idx = assetArchiveFindMetadataEntry(archive,
		ASSET_ARCHIVE_META_MANIFEST);
	if (idx < 0) return 0;
	u32 size = 0;
	char *text = extractOpenedEntryTextAlloc(archive, idx, &size);
	if (!text && size > ASSET_ARCHIVE_TEXT_SCAN_LIMIT) return 0;
	if (!text) return 0;
	s32 r = validateManifestDependencyText(archive_path, text,
		openedDependencyArchiveExists, archive, err, err_cap);
	free(text);
	return r;
}

static s32 validateMemManifestDependencies(const void *archive_bytes,
                                           u32 archive_size,
                                           const char *archive_name,
                                           char *err, size_t err_cap)
{
	u32 size = 0;
	char *text = extractMemEntryTextAlloc(archive_bytes, archive_size,
		ASSET_ARCHIVE_META_MANIFEST_PATH, &size);
	if (!text) {
		text = extractMemEntryTextAlloc(archive_bytes, archive_size,
			ASSET_ARCHIVE_META_MANIFEST, &size);
	}
	if (!text && size > ASSET_ARCHIVE_TEXT_SCAN_LIMIT) return 0;
	if (!text) return 0;
	mem_dependency_exists_ctx_t ctx;
	ctx.archive_bytes = archive_bytes;
	ctx.archive_size = archive_size;
	s32 r = validateManifestDependencyText(archive_name, text,
		memDependencyArchiveExists, &ctx, err, err_cap);
	free(text);
	return r;
}

static s32 validateJsonTextRefsOpened(mod_archive_t *archive,
                                      const char *archive_path,
                                      const char *entry_name,
                                      const char *text,
                                      char *err, size_t err_cap)
{
	const char *p = text;
	while (p && *p) {
		if (*p++ != '"') continue;
		char key[128];
		const char *after_key = jsonReadString(p - 1, key, sizeof(key));
		if (!after_key) return 0;
		p = after_key;
		while (*p && isspace((unsigned char)*p)) p++;
		if (*p++ != ':') continue;
		while (*p && isspace((unsigned char)*p)) p++;
		if (*p != '"') continue;
		char value[FS_MAXPATH + 1];
		const char *after_value = jsonReadString(p, value, sizeof(value));
		if (!after_value) return 0;
		if (jsonKeyLooksLikeFileReference(key, value)) {
			if (validateOpenedRefExists(archive, archive_path, entry_name,
					key, value, err, err_cap) != 0) {
				return -1;
			}
		}
		p = after_value;
	}
	return 0;
}

static s32 validateJsonTextRefsMem(const void *archive_bytes, u32 archive_size,
                                   const char *archive_name,
                                   const char *entry_name,
                                   const char *text,
                                   char *err, size_t err_cap)
{
	const char *p = text;
	while (p && *p) {
		if (*p++ != '"') continue;
		char key[128];
		const char *after_key = jsonReadString(p - 1, key, sizeof(key));
		if (!after_key) return 0;
		p = after_key;
		while (*p && isspace((unsigned char)*p)) p++;
		if (*p++ != ':') continue;
		while (*p && isspace((unsigned char)*p)) p++;
		if (*p != '"') continue;
		char value[FS_MAXPATH + 1];
		const char *after_value = jsonReadString(p, value, sizeof(value));
		if (!after_value) return 0;
		if (jsonKeyLooksLikeFileReference(key, value)) {
			if (validateMemRefExists(archive_bytes, archive_size,
					archive_name, entry_name, key, value,
					err, err_cap) != 0) {
				return -1;
			}
		}
		p = after_value;
	}
	return 0;
}

static void objMtlTextureToken(const char *value, char *out, size_t out_cap)
{
	if (!out || out_cap == 0) return;
	out[0] = '\0';
	if (!value) return;
	char tmp[FS_MAXPATH + 1];
	copyTrimmed(value, NULL, tmp, sizeof(tmp));
	const char *last = tmp;
	for (const char *p = tmp; *p; p++) {
		if (isspace((unsigned char)*p)) {
			while (*p && isspace((unsigned char)*p)) p++;
			if (*p) last = p;
		}
	}
	snprintf(out, out_cap, "%s", last);
	out[out_cap - 1] = '\0';
}

static s32 validateObjTextRefsOpened(mod_archive_t *archive,
                                     const char *archive_path,
                                     const char *entry_name,
                                     const char *text,
                                     char *err, size_t err_cap)
{
	const char *line = text;
	while (line && *line) {
		const char *next = strchr(line, '\n');
		const char *end = next ? next : line + strlen(line);
		char trimmed[FS_MAXPATH + 1];
		copyTrimmed(line, end, trimmed, sizeof(trimmed));
		if (strncmp(trimmed, "mtllib ", 7) == 0) {
			char ref[FS_MAXPATH + 1];
			copyTrimmed(trimmed + 7, NULL, ref, sizeof(ref));
			if (validateOpenedRefExists(archive, archive_path,
					entry_name, "mtllib", ref, err, err_cap) != 0) {
				return -1;
			}
		}
		line = next ? next + 1 : NULL;
	}
	return 0;
}

static s32 validateObjTextRefsMem(const void *archive_bytes, u32 archive_size,
                                  const char *archive_name,
                                  const char *entry_name,
                                  const char *text,
                                  char *err, size_t err_cap)
{
	const char *line = text;
	while (line && *line) {
		const char *next = strchr(line, '\n');
		const char *end = next ? next : line + strlen(line);
		char trimmed[FS_MAXPATH + 1];
		copyTrimmed(line, end, trimmed, sizeof(trimmed));
		if (strncmp(trimmed, "mtllib ", 7) == 0) {
			char ref[FS_MAXPATH + 1];
			copyTrimmed(trimmed + 7, NULL, ref, sizeof(ref));
			if (validateMemRefExists(archive_bytes, archive_size,
					archive_name, entry_name, "mtllib", ref,
					err, err_cap) != 0) {
				return -1;
			}
		}
		line = next ? next + 1 : NULL;
	}
	return 0;
}

static s32 validateMtlTextRefsOpened(mod_archive_t *archive,
                                     const char *archive_path,
                                     const char *entry_name,
                                     const char *text,
                                     char *err, size_t err_cap)
{
	const char *line = text;
	while (line && *line) {
		const char *next = strchr(line, '\n');
		const char *end = next ? next : line + strlen(line);
		char trimmed[FS_MAXPATH + 1];
		copyTrimmed(line, end, trimmed, sizeof(trimmed));
		if (strncmp(trimmed, "map_", 4) == 0) {
			const char *space = trimmed;
			while (*space && !isspace((unsigned char)*space)) space++;
			if (*space) {
				char ref[FS_MAXPATH + 1];
				objMtlTextureToken(space, ref, sizeof(ref));
				if (validateOpenedRefExists(archive, archive_path,
						entry_name, "map", ref, err, err_cap) != 0) {
					return -1;
				}
			}
		}
		line = next ? next + 1 : NULL;
	}
	return 0;
}

static s32 validateMtlTextRefsMem(const void *archive_bytes, u32 archive_size,
                                  const char *archive_name,
                                  const char *entry_name,
                                  const char *text,
                                  char *err, size_t err_cap)
{
	const char *line = text;
	while (line && *line) {
		const char *next = strchr(line, '\n');
		const char *end = next ? next : line + strlen(line);
		char trimmed[FS_MAXPATH + 1];
		copyTrimmed(line, end, trimmed, sizeof(trimmed));
		if (strncmp(trimmed, "map_", 4) == 0) {
			const char *space = trimmed;
			while (*space && !isspace((unsigned char)*space)) space++;
			if (*space) {
				char ref[FS_MAXPATH + 1];
				objMtlTextureToken(space, ref, sizeof(ref));
				if (validateMemRefExists(archive_bytes, archive_size,
						archive_name, entry_name, "map", ref,
						err, err_cap) != 0) {
					return -1;
				}
			}
		}
		line = next ? next + 1 : NULL;
	}
	return 0;
}

static s32 validateOpenedTextEntryRefs(mod_archive_t *archive,
                                       const char *archive_path,
                                       const char *entry_name,
                                       char *err, size_t err_cap)
{
	s32 idx = modArchiveFindEntry(archive, entry_name);
	if (idx < 0) return 0;
	u32 size = 0;
	char *text = extractOpenedEntryTextAlloc(archive, idx, &size);
	if (!text && size > ASSET_ARCHIVE_TEXT_SCAN_LIMIT) return 0;
	if (!text) return 0;
	s32 r = 0;
	if (endsWithNoCase(entry_name, ".ini")) {
		r = validateIniTextRefsOpened(archive, archive_path, entry_name,
			text, err, err_cap);
	} else if (endsWithNoCase(entry_name, ".gltf")) {
		r = validateJsonTextRefsOpened(archive, archive_path, entry_name,
			text, err, err_cap);
	} else if (endsWithNoCase(entry_name, ".obj")) {
		r = validateObjTextRefsOpened(archive, archive_path, entry_name,
			text, err, err_cap);
	} else if (endsWithNoCase(entry_name, ".mtl")) {
		r = validateMtlTextRefsOpened(archive, archive_path, entry_name,
			text, err, err_cap);
	} else if (endsWithNoCase(entry_name, ".json")) {
		r = validateJsonTextRefsOpened(archive, archive_path, entry_name,
			text, err, err_cap);
	}
	free(text);
	return r;
}

static s32 validateMemTextEntryRefs(const void *archive_bytes, u32 archive_size,
                                    const char *archive_name,
                                    const char *entry_name,
                                    char *err, size_t err_cap)
{
	u32 size = 0;
	char *text = extractMemEntryTextAlloc(archive_bytes, archive_size,
		entry_name, &size);
	if (!text && size > ASSET_ARCHIVE_TEXT_SCAN_LIMIT) return 0;
	if (!text) return 0;
	s32 r = 0;
	if (endsWithNoCase(entry_name, ".ini")) {
		r = validateIniTextRefsMem(archive_bytes, archive_size,
			archive_name, entry_name, text, err, err_cap);
	} else if (endsWithNoCase(entry_name, ".gltf")) {
		r = validateJsonTextRefsMem(archive_bytes, archive_size,
			archive_name, entry_name, text, err, err_cap);
	} else if (endsWithNoCase(entry_name, ".obj")) {
		r = validateObjTextRefsMem(archive_bytes, archive_size,
			archive_name, entry_name, text, err, err_cap);
	} else if (endsWithNoCase(entry_name, ".mtl")) {
		r = validateMtlTextRefsMem(archive_bytes, archive_size,
			archive_name, entry_name, text, err, err_cap);
	} else if (endsWithNoCase(entry_name, ".json")) {
		r = validateJsonTextRefsMem(archive_bytes, archive_size,
			archive_name, entry_name, text, err, err_cap);
	}
	free(text);
	return r;
}

static s32 validateOpenedReferences(mod_archive_t *archive,
                                    const char *archive_path,
                                    asset_archive_validation_mode_e mode,
                                    u32 depth,
                                    char *err, size_t err_cap)
{
	if (!archive) return 0;
	if (depth > ASSET_ARCHIVE_MAX_REF_DEPTH) {
		setErr(err, err_cap, "%s exceeds nested typed archive validation depth",
			archive_path ? archive_path : "(archive)");
		return -1;
	}

	s32 count = modArchiveGetEntryCount(archive);
	for (s32 i = 0; i < count; i++) {
		const char *entry = modArchiveGetEntryName(archive, i);
		if (!entry) continue;
		if (!entryIsMetaPath(entry) &&
				validateOpenedTextEntryRefs(archive, archive_path, entry,
					err, err_cap) != 0) {
			return -1;
		}
		if (assetArchivePathIsDeprecated(entry)) {
			setErr(err, err_cap, "%s embeds deprecated .pdwpn: %s",
				archive_path, entry);
			return -1;
		}
		if (assetArchivePathIsTyped(entry)) {
			u32 nested_size = 0;
			void *nested = modArchiveExtractAlloc(archive, i, &nested_size);
			if (!nested) {
				setErr(err, err_cap, "%s could not read nested typed archive: %s",
					archive_path, entry);
				return -1;
			}
			s32 r = assetArchiveValidateBytesDepth(nested, nested_size,
				entry, mode, depth + 1, err, err_cap);
			free(nested);
			if (r != 0) {
				return -1;
			}
		}
	}

	return 0;
}

static s32 memReferenceEntryCb(const char *entry_name, u32 uncompressed_size,
                               void *user)
{
	(void)uncompressed_size;
	asset_archive_mem_ref_ctx_t *ctx = (asset_archive_mem_ref_ctx_t *)user;
	if (!ctx || ctx->failed || !entry_name) return 0;
	if (!entryIsMetaPath(entry_name) &&
			validateMemTextEntryRefs(ctx->archive_bytes, ctx->archive_size,
				ctx->archive_name, entry_name, ctx->err, ctx->err_cap) != 0) {
		ctx->failed = 1;
		return 1;
	}
	if (assetArchivePathIsDeprecated(entry_name)) {
		setErr(ctx->err, ctx->err_cap, "%s embeds deprecated .pdwpn: %s",
			ctx->archive_name, entry_name);
		ctx->failed = 1;
		return 1;
	}
	if (assetArchivePathIsTyped(entry_name)) {
		u32 nested_size = 0;
		void *nested = modArchiveExtractMemAlloc(ctx->archive_bytes,
			ctx->archive_size, entry_name, &nested_size);
		if (!nested) {
			setErr(ctx->err, ctx->err_cap,
				"%s could not read nested typed archive: %s",
				ctx->archive_name, entry_name);
			ctx->failed = 1;
			return 1;
		}
		s32 r = assetArchiveValidateBytesDepth(nested, nested_size,
			entry_name, ctx->mode, ctx->depth + 1,
			ctx->err, ctx->err_cap);
		free(nested);
		if (r != 0) {
			ctx->failed = 1;
			return 1;
		}
	}
	return 0;
}

static s32 validateMemReferences(const void *archive_bytes, u32 archive_size,
                                 const char *archive_name,
                                 asset_archive_validation_mode_e mode,
                                 u32 depth,
                                 char *err, size_t err_cap)
{
	if (depth > ASSET_ARCHIVE_MAX_REF_DEPTH) {
		setErr(err, err_cap, "%s exceeds nested typed archive validation depth",
			archive_name ? archive_name : "(archive)");
		return -1;
	}
	asset_archive_mem_ref_ctx_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.archive_bytes = archive_bytes;
	ctx.archive_size = archive_size;
	ctx.archive_name = archive_name;
	ctx.mode = mode;
	ctx.depth = depth;
	ctx.err = err;
	ctx.err_cap = err_cap;
	s32 r = modArchiveMemForEachEntry(archive_bytes, archive_size,
		memReferenceEntryCb, &ctx);
	if (r != MODARCHIVE_OK && !ctx.failed) {
		setErr(err, err_cap, "%s is not a zip-openable typed archive",
			archive_name ? archive_name : "(archive)");
		return -1;
	}
	return ctx.failed ? -1 : 0;
}

s32 assetArchiveValidateOpened(mod_archive_t *archive,
                               const char *archive_path,
                               asset_archive_validation_mode_e mode,
                               char *err, size_t err_cap)
{
	if (err && err_cap > 0) err[0] = '\0';
	if (!archive_path || !archive_path[0]) {
		setErr(err, err_cap, "typed archive path is empty");
		return -1;
	}
	if (assetArchivePathIsDeprecated(archive_path)) {
		setErr(err, err_cap, "%s uses deprecated .pdwpn", archive_path);
		return -1;
	}

	const asset_archive_family_rule_t *rule = ruleForPath(archive_path);
	if (!rule) {
		setErr(err, err_cap, "%s is not a known typed asset archive", archive_path);
		return -1;
	}
	if (!archive) {
		setErr(err, err_cap, "%s is not a zip-openable typed archive", archive_path);
		return -1;
	}

	s32 descriptor_idx = modArchiveFindEntry(archive, rule->descriptor);
	if (descriptor_idx < 0 && mode == ASSET_ARCHIVE_VALIDATE_MIGRATION &&
			rule->legacy_descriptor) {
		descriptor_idx = modArchiveFindEntry(archive, rule->legacy_descriptor);
	}
	if (descriptor_idx < 0) {
		setErr(err, err_cap, "%s is missing root descriptor %s",
			archive_path, rule->descriptor);
		return -1;
	}

	s32 count = modArchiveGetEntryCount(archive);
	for (s32 i = 0; i < count; i++) {
		const char *entry = modArchiveGetEntryName(archive, i);
		if (!entry) continue;
		if (assetArchiveEntryIsForbiddenBinPayload(entry)) {
			setErr(err, err_cap, "%s contains forbidden authored .bin payload: %s",
				archive_path, entry);
			return -1;
		}
		if (mode == ASSET_ARCHIVE_VALIDATE_RELEASE &&
				assetArchiveEntryIsForbiddenTsvPayload(entry)) {
			setErr(err, err_cap, "%s contains forbidden public TSV payload: %s",
				archive_path, entry);
			return -1;
		}
		if (mode == ASSET_ARCHIVE_VALIDATE_RELEASE &&
				assetArchiveEntryIsRootMachineMetadata(entry)) {
			setErr(err, err_cap,
				"%s keeps machine metadata at archive root: %s",
				archive_path, entry);
			return -1;
		}
		if (assetArchivePathIsDeprecated(entry)) {
			setErr(err, err_cap, "%s embeds deprecated .pdwpn: %s",
				archive_path, entry);
			return -1;
		}
	}

	if (mode == ASSET_ARCHIVE_VALIDATE_RELEASE &&
			validateOpenedManifestDependencies(archive, archive_path,
				err, err_cap) != 0) {
		return -1;
	}

	return validateOpenedReferences(archive, archive_path, mode, 0,
		err, err_cap);
}

s32 assetArchiveValidateFile(const char *archive_path,
                             asset_archive_validation_mode_e mode,
                             char *err, size_t err_cap)
{
	if (err && err_cap > 0) err[0] = '\0';
	if (!archive_path || !archive_path[0]) {
		setErr(err, err_cap, "typed archive path is empty");
		return -1;
	}
	if (assetArchivePathIsDeprecated(archive_path)) {
		setErr(err, err_cap, "%s uses deprecated .pdwpn", archive_path);
		return -1;
	}
	if (!assetArchivePathIsTyped(archive_path)) {
		setErr(err, err_cap, "%s is not a known typed asset archive", archive_path);
		return -1;
	}

	mod_archive_t *archive = modArchiveOpen(archive_path);
	if (!archive) {
		setErr(err, err_cap, "%s is not a zip-openable typed archive", archive_path);
		return -1;
	}
	s32 r = assetArchiveValidateOpened(archive, archive_path, mode, err, err_cap);
	modArchiveClose(archive);
	return r;
}

static s32 validateMemEntryCb(const char *entry_name, u32 uncompressed_size, void *user)
{
	(void)uncompressed_size;
	asset_archive_mem_validate_ctx_t *ctx = (asset_archive_mem_validate_ctx_t *)user;
	if (!entry_name || !ctx || ctx->failed) {
		return 0;
	}
	if (strcmp(entry_name, ctx->rule->descriptor) == 0) {
		ctx->canonical_descriptor_seen = 1;
	}
	if (ctx->rule->legacy_descriptor &&
			strcmp(entry_name, ctx->rule->legacy_descriptor) == 0) {
		ctx->legacy_descriptor_seen = 1;
	}
	if (assetArchiveEntryIsForbiddenBinPayload(entry_name)) {
		setErr(ctx->err, ctx->err_cap, "%s contains forbidden authored .bin payload: %s",
			ctx->archive_name, entry_name);
		ctx->failed = 1;
		return 1;
	}
	if (ctx->mode == ASSET_ARCHIVE_VALIDATE_RELEASE &&
			assetArchiveEntryIsForbiddenTsvPayload(entry_name)) {
		setErr(ctx->err, ctx->err_cap,
			"%s contains forbidden public TSV payload: %s",
			ctx->archive_name, entry_name);
		ctx->failed = 1;
		return 1;
	}
	if (ctx->mode == ASSET_ARCHIVE_VALIDATE_RELEASE &&
			assetArchiveEntryIsRootMachineMetadata(entry_name)) {
		setErr(ctx->err, ctx->err_cap,
			"%s keeps machine metadata at archive root: %s",
			ctx->archive_name, entry_name);
		ctx->failed = 1;
		return 1;
	}
	if (assetArchivePathIsDeprecated(entry_name)) {
		setErr(ctx->err, ctx->err_cap, "%s embeds deprecated .pdwpn: %s",
			ctx->archive_name, entry_name);
		ctx->failed = 1;
		return 1;
	}
	return 0;
}

static s32 assetArchiveValidateBytesDepth(const void *archive_bytes, u32 archive_size,
                                          const char *archive_name,
                                          asset_archive_validation_mode_e mode,
                                          u32 depth,
                                          char *err, size_t err_cap)
{
	if (!archive_name || !archive_name[0]) {
		setErr(err, err_cap, "typed archive name is empty");
		return -1;
	}
	if (assetArchivePathIsDeprecated(archive_name)) {
		setErr(err, err_cap, "%s uses deprecated .pdwpn", archive_name);
		return -1;
	}

	const asset_archive_family_rule_t *rule = ruleForPath(archive_name);
	if (!rule) {
		setErr(err, err_cap, "%s is not a known typed asset archive", archive_name);
		return -1;
	}
	if (!archive_bytes || archive_size == 0) {
		setErr(err, err_cap, "%s is empty", archive_name);
		return -1;
	}

	asset_archive_mem_validate_ctx_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.archive_name = archive_name;
	ctx.rule = rule;
	ctx.mode = mode;
	ctx.depth = depth;
	ctx.err = err;
	ctx.err_cap = err_cap;

	s32 r = modArchiveMemForEachEntry(archive_bytes, archive_size,
		validateMemEntryCb, &ctx);
	if (r != MODARCHIVE_OK && !ctx.failed) {
		setErr(err, err_cap, "%s is not a zip-openable typed archive", archive_name);
		return -1;
	}
	if (ctx.failed) {
		return -1;
	}
	if (!ctx.canonical_descriptor_seen &&
			!(mode == ASSET_ARCHIVE_VALIDATE_MIGRATION && ctx.legacy_descriptor_seen)) {
		setErr(err, err_cap, "%s is missing root descriptor %s",
			archive_name, rule->descriptor);
		return -1;
	}
	if (mode == ASSET_ARCHIVE_VALIDATE_RELEASE &&
			validateMemManifestDependencies(archive_bytes, archive_size,
				archive_name, err, err_cap) != 0) {
		return -1;
	}

	return validateMemReferences(archive_bytes, archive_size, archive_name,
		mode, depth, err, err_cap);
}

s32 assetArchiveValidateBytes(const void *archive_bytes, u32 archive_size,
                              const char *archive_name,
                              asset_archive_validation_mode_e mode,
                              char *err, size_t err_cap)
{
	if (err && err_cap > 0) err[0] = '\0';
	return assetArchiveValidateBytesDepth(archive_bytes, archive_size,
		archive_name, mode, 0, err, err_cap);
}
