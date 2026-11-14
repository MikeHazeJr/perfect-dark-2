/**
 * mod_author.c -- turn a template archive plus edits into a complete mod.
 *
 * See mod_author.h for the contract.
 */

#include <direct.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include <PR/ultratypes.h>

#include "asset_archive_clone.h"
#include "asset_archive_policy.h"
#include "mod_asset_validate.h"
#include "mod_author.h"
#include "mod_manifest_validate.h"

#define AUTHOR_SOURCE_MAX   (512u * 1024u * 1024u)
#define AUTHOR_MANIFEST_MAX (64u * 1024u)

/* What the mod manager keeps of each mod.json field (modmgr.h). Longer text
 * would be cut on load, possibly in the middle of a character. */
#define AUTHOR_VERSION_CAP      32
#define AUTHOR_AUTHOR_CAP       64
#define AUTHOR_DESCRIPTION_CAP  256

/* Families a creator can save today, and how each is named on disk. */
typedef struct author_family {
	const char *ext;         /* typed archive extension */
	const char *kind;        /* slug prefix, tag and `contents` entry */
	const char *category;    /* folder under the mods root */
	const char *name_key;    /* descriptor key for the display name, or NULL */
	const char *name_alias;  /* older spelling of that key, or NULL */
	const char *base_keys;   /* space-separated descriptor keys that only a
	                          * base-game asset should carry */
} author_family_t;

/*
 * Keys a template extracted from the base game carries and a mod must not:
 * `bundled` says the asset ships with the game, a weapon's `category` names
 * its owner (the catalog falls back to the mod's own id), and
 * `requirefeature` locks the asset behind base-game progress.
 */
static const author_family_t s_Families[] = {
	{ ".pdweapon",    "weapon",    "Weapons",    "name",         NULL,   "bundled category" },
	{ ".pdcharacter", "character", "Characters", "display_name", NULL,   "bundled requirefeature" },
	{ ".pdbody",      "body",      "Characters", "display_name", "name", "bundled requirefeature" },
	{ ".pdhead",      "head",      "Characters", "display_name", "name", "bundled requirefeature" },
	{ ".pdvehicle",   "vehicle",   "Vehicles",   "name",         NULL,   "bundled" },
	{ ".pdarena",     "arena",     "MP Maps",    NULL,           NULL,   "bundled requirefeature" },
};

typedef struct author_buf {
	char *data;
	size_t len;
	size_t cap;
	s32 failed;
} author_buf_t;

static s32 authorFail(char *err, size_t err_cap, s32 code, const char *fmt, ...)
{
	if (err && err_cap) {
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(err, err_cap, fmt, ap);
		va_end(ap);
		err[err_cap - 1] = '\0';
	}
	return code;
}

static const char *authorLeaf(const char *path)
{
	const char *leaf = path;
	for (const char *p = path; *p; p++) {
		if (*p == '/' || *p == '\\') leaf = p + 1;
	}
	return leaf;
}

static char authorLower(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static s32 authorEqNoCase(const char *a, const char *b)
{
	while (*a && *b) {
		if (authorLower(*a) != authorLower(*b)) return 0;
		a++;
		b++;
	}
	return *a == '\0' && *b == '\0';
}

static const author_family_t *authorFamilyForPath(const char *path)
{
	const char *dot = strrchr(authorLeaf(path), '.');
	if (!dot) return NULL;
	for (size_t i = 0; i < sizeof(s_Families) / sizeof(s_Families[0]); i++) {
		if (authorEqNoCase(dot, s_Families[i].ext)) return &s_Families[i];
	}
	return NULL;
}

static void authorBufAppend(author_buf_t *b, const char *data, size_t n)
{
	if (b->failed || n == 0) return;
	if (b->len + n + 1 > b->cap) {
		size_t cap = b->cap ? b->cap : 256;
		char *grown;
		while (cap < b->len + n + 1) cap *= 2;
		grown = (char *)realloc(b->data, cap);
		if (!grown) {
			b->failed = 1;
			return;
		}
		b->data = grown;
		b->cap = cap;
	}
	memcpy(b->data + b->len, data, n);
	b->len += n;
	b->data[b->len] = '\0';
}

static void authorBufText(author_buf_t *b, const char *text)
{
	authorBufAppend(b, text, strlen(text));
}

/* A JSON string literal, quotes included. Bytes above 0x7f pass through; the
 * callers only hand in validated UTF-8 with no control characters. */
static void authorBufJsonString(author_buf_t *b, const char *text)
{
	authorBufText(b, "\"");
	for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
		if (*p == '"') authorBufText(b, "\\\"");
		else if (*p == '\\') authorBufText(b, "\\\\");
		else if (*p < 0x20) {
			char esc[8];
			snprintf(esc, sizeof(esc), "\\u%04x", (unsigned)*p);
			authorBufText(b, esc);
		} else {
			authorBufAppend(b, (const char *)p, 1);
		}
	}
	authorBufText(b, "\"");
}

static s32 authorUtf8Valid(const unsigned char *p, size_t size)
{
	size_t i = 0;

	while (i < size) {
		unsigned code = p[i++];
		unsigned minimum;
		unsigned remaining;

		if (code < 0x80) continue;
		if (code >= 0xc2 && code <= 0xdf) { code &= 0x1f; remaining = 1; minimum = 0x80; }
		else if (code >= 0xe0 && code <= 0xef) { code &= 0x0f; remaining = 2; minimum = 0x800; }
		else if (code >= 0xf0 && code <= 0xf4) { code &= 0x07; remaining = 3; minimum = 0x10000; }
		else return 0;
		if (remaining > size - i) return 0;
		while (remaining--) {
			if ((p[i] & 0xc0) != 0x80) return 0;
			code = (code << 6) | (p[i++] & 0x3f);
		}
		if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return 0;
	}
	return 1;
}

/*
 * One line of player-entered text: surrounding spaces and tabs removed, then
 * 1 to cap-1 bytes of valid UTF-8 with no control characters.
 */
static s32 authorCleanLine(const char *in, char *out, size_t cap)
{
	size_t n;

	out[0] = '\0';
	if (!in) return 0;
	while (*in == ' ' || *in == '\t') in++;
	n = strlen(in);
	while (n > 0 && (in[n - 1] == ' ' || in[n - 1] == '\t')) n--;
	if (n == 0 || n >= cap) return 0;
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)in[i];
		if (c < 0x20 || c == 0x7f) return 0;
	}
	if (!authorUtf8Valid((const unsigned char *)in, n)) return 0;
	memcpy(out, in, n);
	out[n] = '\0';
	return 1;
}

/* Lower-case ASCII letters and digits, every other run collapsed to one '_'. */
static void authorSlug(const char *in, char *out, size_t cap)
{
	size_t j = 0;
	s32 last_us = 1;

	for (; *in && j + 1 < cap; in++) {
		char c = authorLower(*in);
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
			out[j++] = c;
			last_us = 0;
		} else if (!last_us) {
			out[j++] = '_';
			last_us = 1;
		}
	}
	while (j > 0 && out[j - 1] == '_') j--;
	out[j] = '\0';
}

static u32 authorHash(const char *text)
{
	u32 h = 2166136261u;   /* FNV-1a */
	for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
		h = (h ^ *p) * 16777619u;
	}
	return h;
}

static s32 authorFileExists(const char *path)
{
	DWORD attrs = GetFileAttributesA(path);
	return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static s32 authorDirExists(const char *path)
{
	DWORD attrs = GetFileAttributesA(path);
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

/* Returns 1 if the directory is there afterwards; *created says whether this
 * call made it. */
static s32 authorEnsureDir(const char *path, s32 *created)
{
	*created = 0;
	if (authorDirExists(path)) return 1;
	if (_mkdir(path) == 0) {
		*created = 1;
		return 1;
	}
	return authorDirExists(path);
}

static void *authorReadFile(const char *path, u32 *out_size)
{
	FILE *f = fopen(path, "rb");
	long size;
	void *data;

	*out_size = 0;
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return NULL;
	}
	size = ftell(f);
	if (size <= 0 || (unsigned long)size > AUTHOR_SOURCE_MAX ||
			fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return NULL;
	}
	data = malloc((size_t)size + 1);
	if (!data) {
		fclose(f);
		return NULL;
	}
	if (fread(data, 1, (size_t)size, f) != (size_t)size) {
		free(data);
		fclose(f);
		return NULL;
	}
	fclose(f);
	((char *)data)[size] = '\0';
	*out_size = (u32)size;
	return data;
}

static s32 authorWriteFile(const char *path, const void *data, size_t size)
{
	FILE *f = fopen(path, "wb");
	s32 ok;

	if (!f) return 0;
	ok = fwrite(data, 1, size, f) == size;
	if (fclose(f) != 0) ok = 0;
	return ok;
}

static s32 authorReplaceFile(const char *source, const char *destination)
{
	return MoveFileExA(source, destination,
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

/* One line of `text` from `*pos`: [*start, *end) without its terminator,
 * which may be "\n", "\r\n" or a lone "\r". */
static s32 authorNextLine(const char *text, u32 len, u32 *pos, u32 *start, u32 *end)
{
	u32 i = *pos;

	if (i >= len) return 0;
	*start = i;
	while (i < len && text[i] != '\n' && text[i] != '\r') i++;
	*end = i;
	if (i < len) {
		if (text[i] == '\r' && i + 1 < len && text[i + 1] == '\n') i++;
		i++;
	}
	*pos = i;
	return 1;
}

/* Does this line (leading blanks already skipped) assign `key`? */
static s32 authorLineHasKey(const char *line, u32 len, const char *key)
{
	const u32 key_len = (u32)strlen(key);
	u32 i = key_len;

	if (len <= key_len || memcmp(line, key, key_len) != 0) return 0;
	while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
	return i < len && line[i] == '=';
}

/* Is `key` (length `key_len`) one of the space-separated names in `list`? */
static s32 authorKeyInList(const char *list, const char *key, u32 key_len)
{
	while (list && *list) {
		const char *end = list;
		while (*end && *end != ' ') end++;
		if ((u32)(end - list) == key_len && memcmp(list, key, key_len) == 0) return 1;
		list = *end ? end + 1 : end;
	}
	return 0;
}

/*
 * Remove the lines of the first section that assign one of `keys`. Every
 * other byte is kept. Returns a malloc'd copy even when nothing was removed;
 * `*removed` counts the lines dropped.
 */
static char *authorDropDescriptorKeys(const char *text, u32 len, const char *keys,
		u32 *out_len, u32 *removed)
{
	author_buf_t b;
	u32 pos = 0;
	u32 start = 0;
	u32 end = 0;
	s32 sections = 0;

	memset(&b, 0, sizeof(b));
	*out_len = 0;
	*removed = 0;
	while (authorNextLine(text, len, &pos, &start, &end)) {
		u32 first = start;
		u32 key_end;

		while (first < end && (text[first] == ' ' || text[first] == '\t')) first++;
		if (first < end && text[first] == '[' && memchr(text + first, ']', end - first)) {
			sections++;
		} else if (sections == 1 && first < end && text[first] != ';' && text[first] != '#') {
			key_end = first;
			while (key_end < end && text[key_end] != '=' && text[key_end] != ' ' &&
					text[key_end] != '\t') {
				key_end++;
			}
			if (key_end > first && memchr(text + key_end, '=', end - key_end) &&
					authorKeyInList(keys, text + first, key_end - first)) {
				(*removed)++;
				continue;
			}
		}
		authorBufAppend(&b, text + start, pos - start);
	}
	if (b.failed) {
		free(b.data);
		return NULL;
	}
	if (!b.data) {
		/* Empty input or every line removed: still hand back a buffer. */
		b.data = (char *)calloc(1, 1);
		if (!b.data) return NULL;
	}
	*out_len = (u32)b.len;
	return b.data;
}

/*
 * Set the display name in a descriptor.
 *
 * The descriptor readers take keys from the first section, so that is the
 * only place this writes: an existing `key` line there is replaced, else an
 * existing `alias` line, else a new `key` line goes in after `catalog_id`.
 * Every other byte is kept. Returns NULL when the descriptor has no section,
 * or keeps the key in a later section where a second copy would make the
 * strict readers refuse the file.
 */
static char *authorSetDescriptorName(const char *text, u32 len, const char *key,
		const char *alias, const char *value, u32 *out_len)
{
	author_buf_t b;
	const char *eol = "\n";
	const char *write_key = key;
	u32 pos = 0;
	u32 start = 0;
	u32 end = 0;
	u32 insert_at = 0;
	u32 key_start = 0, key_end = 0;
	u32 alias_start = 0, alias_end = 0;
	s32 sections = 0;
	s32 have_eol = 0;
	s32 have_key = 0;
	s32 have_alias = 0;

	memset(&b, 0, sizeof(b));
	*out_len = 0;

	while (authorNextLine(text, len, &pos, &start, &end)) {
		u32 first = start;

		if (!have_eol && end < len) {
			eol = (text[end] == '\r' && end + 1 < len && text[end + 1] == '\n') ? "\r\n" : "\n";
			have_eol = 1;
		}
		while (first < end && (text[first] == ' ' || text[first] == '\t')) first++;
		if (first == end || text[first] == ';' || text[first] == '#') continue;
		if (text[first] == '[') {
			if (!memchr(text + first, ']', end - first)) continue;
			sections++;
			if (sections == 1) insert_at = pos;
			continue;
		}
		if (sections == 0) continue;
		if (authorLineHasKey(text + first, end - first, key)) {
			if (sections > 1) return NULL;
			if (!have_key) {
				key_start = start;
				key_end = end;
				have_key = 1;
			}
		} else if (alias && authorLineHasKey(text + first, end - first, alias)) {
			if (sections > 1) return NULL;
			if (!have_alias) {
				alias_start = start;
				alias_end = end;
				have_alias = 1;
			}
		} else if (sections == 1 && authorLineHasKey(text + first, end - first, "catalog_id")) {
			insert_at = pos;
		}
	}
	if (sections == 0) return NULL;

	if (have_key || have_alias) {
		const u32 line_start = have_key ? key_start : alias_start;
		const u32 line_end = have_key ? key_end : alias_end;

		if (!have_key) write_key = alias;
		authorBufAppend(&b, text, line_start);
		authorBufText(&b, write_key);
		authorBufText(&b, " = ");
		authorBufText(&b, value);
		authorBufAppend(&b, text + line_end, len - line_end);
	} else {
		authorBufAppend(&b, text, insert_at);
		if (insert_at > 0 && text[insert_at - 1] != '\n' && text[insert_at - 1] != '\r') {
			authorBufText(&b, eol);
		}
		authorBufText(&b, key);
		authorBufText(&b, " = ");
		authorBufText(&b, value);
		authorBufText(&b, eol);
		authorBufAppend(&b, text + insert_at, len - insert_at);
	}
	if (b.failed || !b.data) {
		free(b.data);
		return NULL;
	}
	*out_len = (u32)b.len;
	return b.data;
}

/* Names and paths for a request. Writes nothing. */
static s32 authorPlan(const mod_author_request_t *request, mod_author_result_t *result,
		const author_family_t **out_family, char *display, size_t display_cap,
		char *err, size_t err_cap)
{
	const author_family_t *family;
	char name_slug[MOD_AUTHOR_NAME_MAX];
	char full[MOD_AUTHOR_NAME_MAX + 32];
	/* "mod:<slug>" must leave room for the ids the rebuild derives from it
	 * for embedded archives; "mod.<slug>" is the same length. */
	const size_t room = ASSET_ARCHIVE_CLONE_ROOT_ID_MAX - 4;
	size_t kind_len;
	int n;

	memset(result, 0, sizeof(*result));
	*out_family = NULL;
	if (!request || !request->template_path || !request->template_path[0] ||
			!request->mods_root || !request->mods_root[0]) {
		return authorFail(err, err_cap, MOD_AUTHOR_ERR_ARGS,
			"missing template path or mods folder");
	}
	if (request->override_count > 0 && !request->overrides) {
		return authorFail(err, err_cap, MOD_AUTHOR_ERR_ARGS, "override list is missing");
	}
	family = authorFamilyForPath(request->template_path);
	if (!family) {
		return authorFail(err, err_cap, MOD_AUTHOR_ERR_TEMPLATE,
			"no creator for this kind of archive yet: %s",
			authorLeaf(request->template_path));
	}
	if (!authorCleanLine(request->display_name, display, display_cap)) {
		return authorFail(err, err_cap, MOD_AUTHOR_ERR_ARGS,
			"the name must be one line of 1 to %d bytes of plain text",
			MOD_AUTHOR_NAME_MAX - 1);
	}
	authorSlug(display, name_slug, sizeof(name_slug));
	if (!name_slug[0]) {
		return authorFail(err, err_cap, MOD_AUTHOR_ERR_ARGS,
			"the name needs at least one letter or digit (A-Z, 0-9)");
	}

	/* "<kind>_<name>", not doubled when the name already starts with it. */
	kind_len = strlen(family->kind);
	if (strncmp(name_slug, family->kind, kind_len) == 0 && name_slug[kind_len] == '_' &&
			name_slug[kind_len + 1] != '\0') {
		snprintf(full, sizeof(full), "%s", name_slug);
	} else {
		snprintf(full, sizeof(full), "%s_%s", family->kind, name_slug);
	}
	if (strlen(full) <= room) {
		snprintf(result->slug, sizeof(result->slug), "%s", full);
	} else {
		/* Too long for an id: keep the start and tell long names apart by a
		 * hash of the whole slug, so two of them never share a folder. */
		size_t keep = room - 9;
		while (keep > 0 && full[keep - 1] == '_') keep--;
		snprintf(result->slug, sizeof(result->slug), "%.*s_%08x", (int)keep, full,
			(unsigned)authorHash(full));
	}

	snprintf(result->kind, sizeof(result->kind), "%s", family->kind);
	snprintf(result->mod_id, sizeof(result->mod_id), "mod.%s", result->slug);
	snprintf(result->catalog_id, sizeof(result->catalog_id), "mod:%s", result->slug);
	n = snprintf(result->mod_dir, sizeof(result->mod_dir), "%s/%s/%s",
		request->mods_root, family->category, result->slug);
	/* Room for "/<slug><ext>.candidate" and the rebuild's scratch names. */
	if (n <= 0 || (size_t)n + strlen(result->slug) + 96 >= sizeof(result->mod_dir)) {
		memset(result, 0, sizeof(*result));
		return authorFail(err, err_cap, MOD_AUTHOR_ERR_ARGS, "the mods folder path is too long");
	}
	snprintf(result->archive_path, sizeof(result->archive_path), "%s/%s%s",
		result->mod_dir, result->slug, family->ext);
	snprintf(result->manifest_path, sizeof(result->manifest_path), "%s/mod.json",
		result->mod_dir);
	*out_family = family;
	return MOD_AUTHOR_OK;
}

/*
 * Who owns the planned folder: nobody yet (`plan->updated` 0), this mod
 * (`plan->updated` 1), or something else (MOD_AUTHOR_ERR_EXISTS).
 */
static s32 authorCheckOwner(mod_author_result_t *plan, char *err, size_t err_cap)
{
	plan->updated = 0;
	if (authorFileExists(plan->manifest_path)) {
		u32 size = 0;
		char id[MOD_AUTHOR_ID_MAX];
		char *json = (char *)authorReadFile(plan->manifest_path, &size);
		s32 same = json && modManifestReadString(json, size, "id", id, sizeof(id)) &&
			strcmp(id, plan->mod_id) == 0;

		free(json);
		if (!same) {
			return authorFail(err, err_cap, MOD_AUTHOR_ERR_EXISTS,
				"%s already holds a different mod", plan->mod_dir);
		}
		plan->updated = 1;
	} else if (authorFileExists(plan->archive_path)) {
		return authorFail(err, err_cap, MOD_AUTHOR_ERR_EXISTS,
			"%s already holds an archive that has no mod.json", plan->mod_dir);
	}
	return MOD_AUTHOR_OK;
}

s32 modAuthorPlan(const mod_author_request_t *request, mod_author_result_t *result,
		char *err, size_t err_cap)
{
	const author_family_t *family = NULL;
	char display[MOD_AUTHOR_NAME_MAX];
	s32 code;

	if (err && err_cap) err[0] = '\0';
	if (!result) {
		return authorFail(err, err_cap, MOD_AUTHOR_ERR_ARGS, "result is required");
	}
	code = authorPlan(request, result, &family, display, sizeof(display), err, err_cap);
	if (code == MOD_AUTHOR_OK) {
		code = authorCheckOwner(result, err, err_cap);
		if (code != MOD_AUTHOR_OK) memset(result, 0, sizeof(*result));
	}
	return code;
}

/* The mod.json for this mod: malloc'd, and already accepted by the packers'
 * manifest gate. NULL with `err` set otherwise. */
static char *authorBuildManifest(const mod_author_request_t *request,
		const mod_author_result_t *plan, const author_family_t *family,
		const char *display, size_t *out_len, char *err, size_t err_cap)
{
	author_buf_t b;
	char author[AUTHOR_AUTHOR_CAP];
	char version[AUTHOR_VERSION_CAP];
	char description[AUTHOR_DESCRIPTION_CAP];
	char verdict[192];

	memset(&b, 0, sizeof(b));
	*out_len = 0;
	if (!authorCleanLine((request->author && request->author[0]) ? request->author : "Player",
			author, sizeof(author))) {
		authorFail(err, err_cap, MOD_AUTHOR_ERR_ARGS,
			"the author must be one line of 1 to %d bytes of plain text",
			AUTHOR_AUTHOR_CAP - 1);
		return NULL;
	}
	if (!authorCleanLine((request->version && request->version[0]) ? request->version : "1.0.0",
			version, sizeof(version))) {
		authorFail(err, err_cap, MOD_AUTHOR_ERR_ARGS,
			"the version must be one line of 1 to %d bytes of plain text",
			AUTHOR_VERSION_CAP - 1);
		return NULL;
	}
	if (!authorCleanLine((request->description && request->description[0])
				? request->description : "Created with the in-game mod tools.",
			description, sizeof(description))) {
		authorFail(err, err_cap, MOD_AUTHOR_ERR_ARGS,
			"the description must be one line of 1 to %d bytes of plain text",
			AUTHOR_DESCRIPTION_CAP - 1);
		return NULL;
	}

	authorBufText(&b, "{\n  \"id\": ");
	authorBufJsonString(&b, plan->mod_id);
	authorBufText(&b, ",\n  \"name\": ");
	authorBufJsonString(&b, display);
	authorBufText(&b, ",\n  \"version\": ");
	authorBufJsonString(&b, version);
	authorBufText(&b, ",\n  \"author\": ");
	authorBufJsonString(&b, author);
	authorBufText(&b, ",\n  \"description\": ");
	authorBufJsonString(&b, description);
	authorBufText(&b, ",\n  \"tags\": [");
	authorBufJsonString(&b, family->kind);
	authorBufText(&b, ", ");
	authorBufJsonString(&b, family->ext + 1);
	authorBufText(&b, ", \"user\"],\n  \"contents\": [");
	authorBufJsonString(&b, family->kind);
	authorBufText(&b, "]\n}\n");

	if (b.failed || !b.data || b.len > AUTHOR_MANIFEST_MAX) {
		free(b.data);
		authorFail(err, err_cap, MOD_AUTHOR_ERR_WRITE, "out of memory");
		return NULL;
	}
	/* Never write a manifest the packers would refuse. */
	if (modManifestValidateBytes(b.data, b.len, verdict, sizeof(verdict)) != MOD_MANIFEST_OK) {
		free(b.data);
		authorFail(err, err_cap, MOD_AUTHOR_ERR_ARGS, "mod.json would be invalid: %s", verdict);
		return NULL;
	}
	*out_len = b.len;
	return b.data;
}

s32 modAuthorSave(const mod_author_request_t *request, mod_author_result_t *result,
		char *err, size_t err_cap)
{
	mod_author_result_t *plan = NULL;
	const author_family_t *family = NULL;
	char display[MOD_AUTHOR_NAME_MAX];
	char category_dir[MOD_AUTHOR_PATH_MAX];
	char candidate[MOD_AUTHOR_PATH_MAX + 32];
	char manifest_tmp[MOD_AUTHOR_PATH_MAX + 32];
	char clone_err[256];
	asset_archive_clone_override_t *overrides = NULL;
	u32 override_count = 0;
	char *manifest = NULL;
	size_t manifest_len = 0;
	char *descriptor = NULL;
	const char *descriptor_name;
	s32 caller_sets_descriptor = 0;
	s32 made_category = 0;
	s32 made_mod_dir = 0;
	s32 archive_was_new;
	s32 code;
	s32 r;

	if (err && err_cap) err[0] = '\0';
	if (result) memset(result, 0, sizeof(*result));

	plan = (mod_author_result_t *)calloc(1, sizeof(*plan));
	if (!plan) return authorFail(err, err_cap, MOD_AUTHOR_ERR_WRITE, "out of memory");

	category_dir[0] = '\0';
	code = authorPlan(request, plan, &family, display, sizeof(display), err, err_cap);
	if (code != MOD_AUTHOR_OK) goto done;

	if (!authorDirExists(request->mods_root)) {
		code = authorFail(err, err_cap, MOD_AUTHOR_ERR_WRITE,
			"the mods folder does not exist: %s", request->mods_root);
		goto done;
	}
	if (!authorFileExists(request->template_path)) {
		code = authorFail(err, err_cap, MOD_AUTHOR_ERR_TEMPLATE,
			"template archive not found: %s", authorLeaf(request->template_path));
		goto done;
	}
	code = authorCheckOwner(plan, err, err_cap);
	if (code != MOD_AUTHOR_OK) goto done;

	manifest = authorBuildManifest(request, plan, family, display, &manifest_len,
		err, err_cap);
	if (!manifest) {
		code = MOD_AUTHOR_ERR_ARGS;
		goto done;
	}

	/* The caller's edits, plus the display name in the descriptor unless the
	 * caller supplies the descriptor itself. */
	descriptor_name = assetArchiveDescriptorForPath(request->template_path);
	for (u32 i = 0; i < request->override_count; i++) {
		if (descriptor_name && request->overrides[i].entry &&
				strcmp(request->overrides[i].entry, descriptor_name) == 0) {
			caller_sets_descriptor = 1;
		}
	}
	overrides = (asset_archive_clone_override_t *)calloc(
		(size_t)request->override_count + 1, sizeof(*overrides));
	if (!overrides) {
		code = authorFail(err, err_cap, MOD_AUTHOR_ERR_WRITE, "out of memory");
		goto done;
	}
	for (u32 i = 0; i < request->override_count; i++) overrides[i] = request->overrides[i];
	override_count = request->override_count;

	if (descriptor_name && !caller_sets_descriptor) {
		u32 template_size = 0;
		u32 source_size = 0;
		u32 owned_size = 0;
		u32 patched_size = 0;
		u32 dropped = 0;
		char *owned;
		const char *entry_name = NULL;
		void *template_bytes = authorReadFile(request->template_path, &template_size);
		char *source = template_bytes
			? assetArchiveExtractDescriptorMemAlloc(template_bytes, template_size,
				request->template_path, ASSET_ARCHIVE_VALIDATE_RELEASE,
				&source_size, &entry_name)
			: NULL;

		free(template_bytes);
		if (!source) {
			code = authorFail(err, err_cap, MOD_AUTHOR_ERR_TEMPLATE,
				"the template has no readable %s", descriptor_name);
			goto done;
		}
		/* The mod owns this asset now: drop what marks it as base content. */
		owned = authorDropDescriptorKeys(source, source_size, family->base_keys,
			&owned_size, &dropped);
		free(source);
		if (!owned) {
			code = authorFail(err, err_cap, MOD_AUTHOR_ERR_WRITE, "out of memory");
			goto done;
		}
		if (family->name_key) {
			descriptor = authorSetDescriptorName(owned, owned_size, family->name_key,
				family->name_alias, display, &patched_size);
			free(owned);
			if (!descriptor) {
				code = authorFail(err, err_cap, MOD_AUTHOR_ERR_TEMPLATE,
					"the name could not be set in the template's %s", descriptor_name);
				goto done;
			}
		} else {
			descriptor = owned;
			patched_size = owned_size;
		}
		/* A family with no name key and nothing to drop keeps its descriptor
		 * as it is, so the rebuild is not handed an empty edit. */
		if (family->name_key || dropped > 0) {
			overrides[override_count].entry = descriptor_name;
			overrides[override_count].data = descriptor;
			overrides[override_count].len = patched_size;
			overrides[override_count].remove = 0;
			override_count++;
		}
	}

	snprintf(category_dir, sizeof(category_dir), "%s/%s", request->mods_root, family->category);
	snprintf(candidate, sizeof(candidate), "%s%s", plan->archive_path,
		ASSET_ARCHIVE_CLONE_STAGING_SUFFIX);
	snprintf(manifest_tmp, sizeof(manifest_tmp), "%s%s", plan->manifest_path,
		ASSET_ARCHIVE_CLONE_STAGING_SUFFIX);
	if (!authorEnsureDir(category_dir, &made_category) ||
			!authorEnsureDir(plan->mod_dir, &made_mod_dir)) {
		code = authorFail(err, err_cap, MOD_AUTHOR_ERR_WRITE,
			"could not create %s", plan->mod_dir);
		goto done;
	}

	/* Stage the archive under a name the catalog scanner does not pick up.
	 * A leftover from an interrupted save of this mod is ours to replace. */
	remove(candidate);
	remove(manifest_tmp);
	clone_err[0] = '\0';
	r = assetArchiveRebuildFile(request->template_path, candidate, plan->catalog_id,
		(request->tool && request->tool[0]) ? request->tool : "mod-author",
		overrides, override_count, &plan->ids, clone_err, sizeof(clone_err));
	if (r != ASSET_ARCHIVE_CLONE_OK) {
		code = authorFail(err, err_cap,
			r == ASSET_ARCHIVE_CLONE_ERR_SOURCE ? MOD_AUTHOR_ERR_TEMPLATE
				: (r == ASSET_ARCHIVE_CLONE_ERR_ARGS ? MOD_AUTHOR_ERR_ARGS
				: MOD_AUTHOR_ERR_ARCHIVE),
			"%s", clone_err[0] ? clone_err : "the archive could not be rebuilt");
		goto done;
	}

	/* Release validation passed inside the rebuild. Now read what the
	 * members say, the way the game will when it loads them. */
	{
		u32 staged_size = 0;
		void *staged = authorReadFile(candidate, &staged_size);
		char why[320];
		s32 verdict;

		why[0] = '\0';
		verdict = staged
			? modAssetValidateArchiveBytes(staged, staged_size, plan->archive_path, why, sizeof(why))
			: MOD_ASSET_VALIDATE_ERR_ARGS;
		free(staged);
		if (verdict != MOD_ASSET_VALIDATE_OK) {
			remove(candidate);
			code = authorFail(err, err_cap, MOD_AUTHOR_ERR_ARCHIVE, "%s",
				why[0] ? why : "the staged archive could not be read back");
			goto done;
		}
	}

	/* Stage the manifest, then swap both in: archive first, so a manifest
	 * never names an archive that is not there. */
	if (!authorWriteFile(manifest_tmp, manifest, manifest_len)) {
		remove(candidate);
		remove(manifest_tmp);
		code = authorFail(err, err_cap, MOD_AUTHOR_ERR_WRITE, "could not write mod.json");
		goto done;
	}
	archive_was_new = !authorFileExists(plan->archive_path);
	if (!authorReplaceFile(candidate, plan->archive_path)) {
		remove(candidate);
		remove(manifest_tmp);
		code = authorFail(err, err_cap, MOD_AUTHOR_ERR_WRITE,
			"could not replace %s", authorLeaf(plan->archive_path));
		goto done;
	}
	if (!authorReplaceFile(manifest_tmp, plan->manifest_path)) {
		remove(manifest_tmp);
		/* A new mod must not be left as an archive nobody owns. An updated
		 * one keeps its previous mod.json, which names the same mod. */
		if (archive_was_new) remove(plan->archive_path);
		code = authorFail(err, err_cap, MOD_AUTHOR_ERR_WRITE, "could not write mod.json");
		goto done;
	}
	code = MOD_AUTHOR_OK;
	if (result) *result = *plan;

done:
	if (code != MOD_AUTHOR_OK) {
		/* Only removes folders this call created, and only if still empty. */
		if (made_mod_dir) _rmdir(plan->mod_dir);
		if (made_category) _rmdir(category_dir);
	}
	free(descriptor);
	free(overrides);
	free(manifest);
	free(plan);
	return code;
}
