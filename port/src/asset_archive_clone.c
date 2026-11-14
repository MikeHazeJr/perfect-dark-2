/**
 * asset_archive_clone.c -- copy a typed asset archive to a new catalog id.
 *
 * See asset_archive_clone.h for the contract. Two passes over the source:
 * the first collects the catalog id of the root and of every embedded typed
 * archive and assigns each a new id; the second rebuilds every archive
 * through the shared archive writer, innermost first, rewriting ids and
 * embedded-archive hashes in text members on the way.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <PR/ultratypes.h>

#include "asset_archive_clone.h"
#include "asset_archive_policy.h"
#include "asset_archive_writer.h"
#include "modarchive.h"
#include "sha256.h"
#include "weapon_graph_archive.h"

#define CLONE_MAX_SOURCE_BYTES (512u * 1024u * 1024u)
#define CLONE_PATH_MAX         1024
#define CLONE_SHA_TEXT_LEN     ((u32)(SHA256_HEX_SIZE - 1))

typedef struct clone_hash_pair {
	char old_hex[SHA256_HEX_SIZE];
	char new_hex[SHA256_HEX_SIZE];
} clone_hash_pair_t;

typedef struct clone_names {
	char **items;
	u32 count;
	u32 cap;
	s32 failed;
} clone_names_t;

typedef struct clone_buf {
	char *data;
	size_t len;
	size_t cap;
	s32 failed;
} clone_buf_t;

typedef struct clone_ctx {
	asset_archive_clone_report_t *report;
	const char *tool;
	const char *out_path;
	u32 scratch_seq;
	s32 code;
	s32 keep_ids;   /* same id as the source: an edit, nothing is renamed */
	const asset_archive_clone_override_t *overrides;
	u32 override_count;
	u8 override_used[ASSET_ARCHIVE_CLONE_MAX_OVERRIDES];
	size_t old_len[ASSET_ARCHIVE_CLONE_MAX_IDS];
	char new_ns[CATALOG_ID_LEN];
	char new_root_slug[CATALOG_ID_LEN];
	char old_root_name[CATALOG_ID_LEN];
	char *err;
	size_t err_cap;
} clone_ctx_t;

/* Records the first failure only; later ones are consequences of it. */
static s32 cloneFail(clone_ctx_t *ctx, s32 code, const char *fmt, ...)
{
	if (ctx->code == ASSET_ARCHIVE_CLONE_OK) {
		ctx->code = code;
		if (ctx->err && ctx->err_cap) {
			va_list ap;
			va_start(ap, fmt);
			vsnprintf(ctx->err, ctx->err_cap, fmt, ap);
			va_end(ap);
			ctx->err[ctx->err_cap - 1] = '\0';
		}
	}
	return 0;
}

static s32 cloneIsIdChar(unsigned char c)
{
	return isalnum(c) || c == '_' || c == '.' || c == ':' || c == '-';
}

static const char *cloneLeaf(const char *path)
{
	const char *leaf = path;
	for (const char *p = path; *p; p++) {
		if (*p == '/' || *p == '\\') leaf = p + 1;
	}
	return leaf;
}

static const char *cloneExt(const char *path)
{
	const char *dot = strrchr(cloneLeaf(path), '.');
	return dot ? dot : "";
}

static s32 cloneEqNoCase(const char *a, const char *b)
{
	while (*a && *b) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
		a++;
		b++;
	}
	return *a == '\0' && *b == '\0';
}

static s32 cloneSamePath(const char *a, const char *b)
{
	while (*a && *b) {
		char ca = (*a == '\\') ? '/' : (char)tolower((unsigned char)*a);
		char cb = (*b == '\\') ? '/' : (char)tolower((unsigned char)*b);
		if (ca != cb) return 0;
		a++;
		b++;
	}
	return *a == '\0' && *b == '\0';
}

static s32 cloneEndsWith(const char *text, const char *suffix)
{
	size_t n = strlen(text);
	size_t m = strlen(suffix);
	return n >= m && cloneEqNoCase(text + n - m, suffix);
}

/* namespace:name, as the scanner and the offline validator accept it. */
static s32 cloneValidId(const char *id)
{
	size_t n = id ? strlen(id) : 0;
	const char *colon;

	if (n < 3 || n >= CATALOG_ID_LEN) return 0;
	if (!isalpha((unsigned char)id[0])) return 0;
	colon = strchr(id, ':');
	if (!colon || colon == id || colon[1] == '\0') return 0;
	for (const char *p = id; p < colon; p++) {
		unsigned char c = (unsigned char)*p;
		if (!(isalnum(c) || c == '_' || c == '.' || c == '-')) return 0;
	}
	if (!isalnum((unsigned char)colon[1])) return 0;
	for (const char *p = colon + 1; *p; p++) {
		if (!cloneIsIdChar((unsigned char)*p)) return 0;
	}
	return 1;
}

static void *cloneReadFile(const char *path, u32 *out_size)
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
	if (size <= 0 || (unsigned long)size > CLONE_MAX_SOURCE_BYTES ||
			fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return NULL;
	}
	data = malloc((size_t)size);
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
	*out_size = (u32)size;
	return data;
}

static s32 cloneFileExists(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	fclose(f);
	return 1;
}

static void cloneNamesFree(clone_names_t *names)
{
	for (u32 i = 0; i < names->count; i++) free(names->items[i]);
	free(names->items);
	memset(names, 0, sizeof(*names));
}

static s32 cloneNamesCb(const char *entryName, u32 uncompressedSize, void *user)
{
	clone_names_t *names = (clone_names_t *)user;
	size_t n = entryName ? strlen(entryName) : 0;
	char *copy;

	(void)uncompressedSize;
	if (n == 0 || entryName[n - 1] == '/') return 0;
	if (names->count == names->cap) {
		u32 cap = names->cap ? names->cap * 2 : 32;
		char **items = (char **)realloc(names->items, (size_t)cap * sizeof(char *));
		if (!items) {
			names->failed = 1;
			return 1;
		}
		names->items = items;
		names->cap = cap;
	}
	copy = (char *)malloc(n + 1);
	if (!copy) {
		names->failed = 1;
		return 1;
	}
	memcpy(copy, entryName, n + 1);
	names->items[names->count++] = copy;
	return 0;
}

static s32 cloneNamesHas(const clone_names_t *names, const char *name)
{
	for (u32 i = 0; i < names->count; i++) {
		if (strcmp(names->items[i], name) == 0) return 1;
	}
	return 0;
}

static s32 cloneListEntries(const void *bytes, u32 size, clone_names_t *names)
{
	memset(names, 0, sizeof(*names));
	if (modArchiveMemForEachEntry(bytes, size, cloneNamesCb, names) != MODARCHIVE_OK ||
			names->failed) {
		cloneNamesFree(names);
		return 0;
	}
	return 1;
}

static s32 cloneIsMeta(const char *entry)
{
	return strncmp(entry, ASSET_ARCHIVE_META_DIR "/", strlen(ASSET_ARCHIVE_META_DIR) + 1) == 0;
}

/* Members in which catalog ids appear as text and can be rewritten. */
static s32 cloneIsText(const char *entry)
{
	return cloneEndsWith(entry, ".ini") || cloneEndsWith(entry, ".json") ||
		cloneEndsWith(entry, ".gltf");
}

/* The `_meta` records the shared writer owns and regenerates. */
static s32 cloneIsWriterMeta(const char *leaf)
{
	return strcmp(leaf, "inventory.json") == 0 || strcmp(leaf, "hashes.json") == 0 ||
		strcmp(leaf, "provenance.json") == 0 || strcmp(leaf, "validation.json") == 0 ||
		strcmp(leaf, "source-handles.json") == 0 || cloneEndsWith(leaf, ".sha256");
}

/* `catalog_id = value` from a descriptor. */
static s32 cloneDescriptorCatalogId(const char *text, u32 len, char *out, size_t cap)
{
	static const char key[] = "catalog_id";
	const u32 key_len = (u32)(sizeof(key) - 1);
	u32 i = 0;

	out[0] = '\0';
	while (i < len) {
		u32 start = i;
		u32 end;

		while (i < len && text[i] != '\n') i++;
		end = i;
		if (i < len) i++;
		while (start < end && (text[start] == ' ' || text[start] == '\t')) start++;
		while (end > start && (text[end - 1] == '\r' || text[end - 1] == ' ' ||
				text[end - 1] == '\t')) end--;
		if (end - start <= key_len || memcmp(text + start, key, key_len) != 0) continue;
		start += key_len;
		while (start < end && (text[start] == ' ' || text[start] == '\t')) start++;
		if (start >= end || text[start] != '=') continue;
		start++;
		while (start < end && (text[start] == ' ' || text[start] == '\t')) start++;
		if (end == start || (size_t)(end - start) >= cap) return 0;
		memcpy(out, text + start, end - start);
		out[end - start] = '\0';
		return 1;
	}
	return 0;
}

/* Lower-case letters and digits, every other run collapsed to one '_'. */
static void cloneSlug(const char *in, char *out, size_t cap)
{
	size_t j = 0;
	s32 last_us = 1;

	for (; *in && j + 1 < cap; in++) {
		unsigned char c = (unsigned char)*in;
		if (isalnum(c)) {
			out[j++] = (char)tolower(c);
			last_us = 0;
		} else if (!last_us) {
			out[j++] = '_';
			last_us = 1;
		}
	}
	while (j > 0 && out[j - 1] == '_') j--;
	out[j] = '\0';
}

static const char *cloneNewIdFor(const clone_ctx_t *ctx, const char *old_id)
{
	for (u32 i = 0; i < ctx->report->id_count; i++) {
		if (strcmp(ctx->report->ids[i].old_id, old_id) == 0) {
			return ctx->report->ids[i].new_id;
		}
	}
	return NULL;
}

static s32 cloneNewIdTaken(const clone_ctx_t *ctx, const char *new_id)
{
	for (u32 i = 0; i < ctx->report->id_count; i++) {
		if (strcmp(ctx->report->ids[i].new_id, new_id) == 0) return 1;
	}
	return 0;
}

static void cloneShaHex(const void *data, size_t len, char hex[SHA256_HEX_SIZE])
{
	u8 digest[SHA256_DIGEST_SIZE];
	sha256Hash(data, len, digest);
	sha256ToHex(digest, hex);
}

/* Give an embedded archive's id a new id under the new root. */
static s32 cloneRegisterNested(clone_ctx_t *ctx, const char *old_id, const char *archive_name)
{
	char tail[CATALOG_ID_LEN];
	char candidate[CATALOG_ID_LEN * 3];
	const char *colon = strchr(old_id, ':');
	const char *name = colon ? colon + 1 : old_id;
	size_t root_len = strlen(ctx->old_root_name);
	asset_archive_clone_id_t *slot;
	int n;

	if (cloneNewIdFor(ctx, old_id)) return 1;
	if (ctx->report->id_count >= ASSET_ARCHIVE_CLONE_MAX_IDS) {
		return cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_IDS,
			"more than %d embedded catalog ids", ASSET_ARCHIVE_CLONE_MAX_IDS);
	}
	if (ctx->keep_ids) {
		/* Editing an archive under its own id: embedded ids stay as they are. */
		slot = &ctx->report->ids[ctx->report->id_count++];
		snprintf(slot->old_id, sizeof(slot->old_id), "%s", old_id);
		snprintf(slot->new_id, sizeof(slot->new_id), "%s", old_id);
		return 1;
	}

	if (root_len > 0 && strncmp(name, ctx->old_root_name, root_len) == 0 &&
			name[root_len] == '_' && name[root_len + 1] == '_' &&
			name[root_len + 2] != '\0') {
		/* Already `root__kind_name`: keep the part after the old root. */
		cloneSlug(name + root_len + 2, tail, sizeof(tail));
	} else {
		char kind[32];
		char slug[CATALOG_ID_LEN];
		const char *ext = cloneExt(archive_name);

		cloneSlug(strlen(ext) > 3 ? ext + 3 : "asset", kind, sizeof(kind));
		cloneSlug(name, slug, sizeof(slug));
		n = snprintf(tail, sizeof(tail), "%s_%s", kind[0] ? kind : "asset",
			slug[0] ? slug : "x");
		if (n < 0) tail[0] = '\0';
	}
	if (!tail[0]) snprintf(tail, sizeof(tail), "asset");

	n = snprintf(candidate, sizeof(candidate), "%s:%s__%s",
		ctx->new_ns, ctx->new_root_slug, tail);
	if (n < 0 || (size_t)n >= CATALOG_ID_LEN || cloneNewIdTaken(ctx, candidate)) {
		/* Too long or already used: shorten and add a stable suffix. */
		char hex[SHA256_HEX_SIZE];
		size_t prefix_len = strlen(ctx->new_ns) + 1 + strlen(ctx->new_root_slug) + 2;
		size_t budget;

		if (prefix_len + 9 >= CATALOG_ID_LEN) {
			return cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_IDS,
				"new catalog id is too long to derive ids for embedded archives");
		}
		budget = CATALOG_ID_LEN - 1 - prefix_len - 9;
		if (strlen(tail) > budget) tail[budget] = '\0';
		while (tail[0] && tail[strlen(tail) - 1] == '_') tail[strlen(tail) - 1] = '\0';
		cloneShaHex(old_id, strlen(old_id), hex);
		hex[8] = '\0';
		n = snprintf(candidate, sizeof(candidate), "%s:%s__%s%s%s",
			ctx->new_ns, ctx->new_root_slug, tail, tail[0] ? "_" : "", hex);
		if (n < 0 || (size_t)n >= CATALOG_ID_LEN || cloneNewIdTaken(ctx, candidate)) {
			return cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_IDS,
				"could not derive a unique id for embedded %s", old_id);
		}
	}
	if (!cloneValidId(candidate)) {
		return cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_IDS,
			"derived id for embedded %s is not a valid catalog id", old_id);
	}

	slot = &ctx->report->ids[ctx->report->id_count++];
	snprintf(slot->old_id, sizeof(slot->old_id), "%s", old_id);
	snprintf(slot->new_id, sizeof(slot->new_id), "%s", candidate);
	return 1;
}

static s32 cloneReadArchiveId(clone_ctx_t *ctx, const void *bytes, u32 size,
		const char *name, char *out, size_t cap)
{
	const char *descriptor_name = NULL;
	u32 descriptor_size = 0;
	char *descriptor = assetArchiveExtractDescriptorMemAlloc(bytes, size, name,
		ASSET_ARCHIVE_VALIDATE_RELEASE, &descriptor_size, &descriptor_name);

	if (!descriptor) {
		return cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_SOURCE,
			"%s has no root descriptor", cloneLeaf(name));
	}
	if (!cloneDescriptorCatalogId(descriptor, descriptor_size, out, cap) ||
			!cloneValidId(out)) {
		free(descriptor);
		return cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_IDS,
			"%s descriptor has no usable catalog_id", cloneLeaf(name));
	}
	free(descriptor);
	return 1;
}

/* One public member of an archive being rebuilt, after overrides. */
typedef struct clone_member {
	char *entry;          /* owned member name inside this archive */
	const void *data;     /* override bytes when from_override >= 0 */
	u32 len;
	s32 from_override;    /* index into ctx->overrides, or -1 for source bytes */
	s32 in_source;        /* the source archive has a member of this name */
} clone_member_t;

typedef struct clone_members {
	clone_member_t *items;
	u32 count;
	u32 cap;
} clone_members_t;

static void cloneMembersFree(clone_members_t *members)
{
	for (u32 i = 0; i < members->count; i++) free(members->items[i].entry);
	free(members->items);
	memset(members, 0, sizeof(*members));
}

static s32 cloneMembersPush(clone_members_t *members, const char *entry,
		const void *data, u32 len, s32 from_override, s32 in_source)
{
	size_t n = strlen(entry);
	clone_member_t *slot;

	if (members->count == members->cap) {
		u32 cap = members->cap ? members->cap * 2 : 32;
		clone_member_t *items = (clone_member_t *)realloc(members->items,
			(size_t)cap * sizeof(clone_member_t));
		if (!items) return 0;
		members->items = items;
		members->cap = cap;
	}
	slot = &members->items[members->count];
	slot->entry = (char *)malloc(n + 1);
	if (!slot->entry) return 0;
	memcpy(slot->entry, entry, n + 1);
	slot->data = data;
	slot->len = len;
	slot->from_override = from_override;
	slot->in_source = in_source;
	members->count++;
	return 1;
}

/* "prefix::entry", or just entry at the root. */
static s32 cloneJoin(const char *prefix, const char *entry, char *out, size_t cap)
{
	int n = prefix[0]
		? snprintf(out, cap, "%s::%s", prefix, entry)
		: snprintf(out, cap, "%s", entry);
	return n >= 0 && (size_t)n < cap;
}

static s32 cloneFindOverride(const clone_ctx_t *ctx, const char *path)
{
	for (u32 i = 0; i < ctx->override_count; i++) {
		if (strcmp(ctx->overrides[i].entry, path) == 0) return (s32)i;
	}
	return -1;
}

/* The member name if `path` is a direct member of the archive at `prefix`. */
static const char *cloneChildLeaf(const char *prefix, const char *path)
{
	size_t n = strlen(prefix);
	const char *leaf = path;

	if (n) {
		if (strncmp(path, prefix, n) != 0 || path[n] != ':' || path[n + 1] != ':') {
			return NULL;
		}
		leaf = path + n + 2;
	}
	return (leaf[0] && !strstr(leaf, "::")) ? leaf : NULL;
}

/*
 * The public members of the archive at `prefix` once overrides are applied:
 * source members in source order, minus removals, with replacement bytes
 * where given, followed by added members in override order.
 */
static s32 cloneBuildMembers(clone_ctx_t *ctx, const clone_names_t *names,
		const char *prefix, clone_members_t *members)
{
	char path[CLONE_PATH_MAX];

	memset(members, 0, sizeof(*members));
	for (u32 i = 0; i < names->count; i++) {
		const char *entry = names->items[i];
		s32 at;
		s32 pushed;

		if (cloneIsMeta(entry)) continue;
		if (!cloneJoin(prefix, entry, path, sizeof(path))) {
			cloneMembersFree(members);
			return cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_SOURCE,
				"member path is too long: %s", entry);
		}
		at = cloneFindOverride(ctx, path);
		if (at >= 0) {
			ctx->override_used[at] = 1;
			if (ctx->overrides[at].remove) continue;
			pushed = cloneMembersPush(members, entry, ctx->overrides[at].data,
				ctx->overrides[at].len, at, 1);
		} else {
			pushed = cloneMembersPush(members, entry, NULL, 0, -1, 1);
		}
		if (!pushed) {
			cloneMembersFree(members);
			return cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_WRITE, "out of memory");
		}
	}
	for (u32 i = 0; i < ctx->override_count; i++) {
		const asset_archive_clone_override_t *ov = &ctx->overrides[i];
		const char *leaf = cloneChildLeaf(prefix, ov->entry);

		if (!leaf || cloneNamesHas(names, leaf)) continue;
		ctx->override_used[i] = 1;
		if (ov->remove) {
			cloneMembersFree(members);
			return cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_ARGS,
				"cannot remove %s: the archive has no such member", ov->entry);
		}
		if (!cloneMembersPush(members, leaf, ov->data, ov->len, (s32)i, 0)) {
			cloneMembersFree(members);
			return cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_WRITE, "out of memory");
		}
	}
	return 1;
}

/*
 * Bytes of a member: override bytes (borrowed, `*owned` stays NULL) or the
 * member extracted from the source archive (`*owned` must be freed).
 */
static const void *cloneMemberData(clone_ctx_t *ctx, const clone_member_t *member,
		const void *bytes, u32 size, const char *archive_name,
		u32 *out_len, void **owned)
{
	*owned = NULL;
	*out_len = 0;
	if (member->from_override >= 0) {
		*out_len = member->len;
		return member->data ? member->data : "";
	}
	*owned = modArchiveExtractMemAlloc(bytes, size, member->entry, out_len);
	if (!*owned) {
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_SOURCE,
			"%s::%s could not be read", cloneLeaf(archive_name), member->entry);
	}
	return *owned;
}

/* Pass 1: assign a new id to the root and to every embedded typed archive. */
static s32 cloneCollectIds(clone_ctx_t *ctx, const void *bytes, u32 size,
		const char *name, s32 depth, const char *prefix)
{
	char old_id[CATALOG_ID_LEN];
	char path[CLONE_PATH_MAX];
	clone_names_t names;
	clone_members_t members;
	s32 ok = 1;

	if (depth > ASSET_ARCHIVE_CLONE_MAX_DEPTH) {
		return cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_SOURCE,
			"typed archives nest deeper than %d levels at %s",
			ASSET_ARCHIVE_CLONE_MAX_DEPTH, cloneLeaf(name));
	}
	if (!cloneReadArchiveId(ctx, bytes, size, name, old_id, sizeof(old_id))) return 0;

	if (depth == 0) {
		const char *colon = strchr(old_id, ':');
		if (strcmp(old_id, ctx->report->ids[0].new_id) == 0) {
			/* Same id: an edit of the archive, which needs something to edit. */
			if (ctx->override_count == 0) {
				return cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_ARGS,
					"new catalog id equals the source id %s", old_id);
			}
			ctx->keep_ids = 1;
		}
		snprintf(ctx->report->ids[0].old_id, sizeof(ctx->report->ids[0].old_id),
			"%s", old_id);
		snprintf(ctx->old_root_name, sizeof(ctx->old_root_name), "%s",
			colon ? colon + 1 : old_id);
		ctx->report->id_count = 1;
	} else if (!cloneRegisterNested(ctx, old_id, name)) {
		return 0;
	}

	if (!cloneListEntries(bytes, size, &names)) {
		return cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_SOURCE,
			"%s could not be listed", cloneLeaf(name));
	}
	if (!cloneBuildMembers(ctx, &names, prefix, &members)) {
		cloneNamesFree(&names);
		return 0;
	}
	for (u32 i = 0; i < members.count && ok; i++) {
		const clone_member_t *member = &members.items[i];
		u32 nested_size = 0;
		void *owned = NULL;
		const void *nested;

		if (!assetArchivePathIsTyped(member->entry)) continue;
		if (!cloneJoin(prefix, member->entry, path, sizeof(path))) {
			ok = cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_SOURCE,
				"member path is too long: %s", member->entry);
			break;
		}
		nested = cloneMemberData(ctx, member, bytes, size, name, &nested_size, &owned);
		if (!nested) {
			ok = 0;
			break;
		}
		ok = cloneCollectIds(ctx, nested, nested_size, member->entry, depth + 1, path);
		free(owned);
	}
	cloneMembersFree(&members);
	cloneNamesFree(&names);
	return ok;
}

static void cloneBufAppend(clone_buf_t *b, const char *data, size_t n)
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

/*
 * Replace whole-token old ids with their new ids and whole-token old
 * embedded-archive hashes with the new ones. A token is bounded by characters
 * that cannot be part of a catalog id, so a longer id that merely starts with
 * an old id is left alone. Returns a malloc'd, NUL-terminated copy.
 */
static char *cloneRewriteText(const clone_ctx_t *ctx, const char *in, u32 len,
		const clone_hash_pair_t *hashes, u32 hash_count, u32 *out_len)
{
	clone_buf_t b;
	u32 i = 0;
	u32 run = 0;

	memset(&b, 0, sizeof(b));
	*out_len = 0;
	while (i < len) {
		s32 replaced = 0;

		if (i == 0 || !cloneIsIdChar((unsigned char)in[i - 1])) {
			for (u32 k = 0; k < ctx->report->id_count && !replaced; k++) {
				const char *old = ctx->report->ids[k].old_id;
				size_t old_len = ctx->old_len[k];

				if (old_len == 0 || old_len > len - i || in[i] != old[0] ||
						memcmp(in + i, old, old_len) != 0) continue;
				if (i + old_len < len && cloneIsIdChar((unsigned char)in[i + old_len])) continue;
				cloneBufAppend(&b, in + run, i - run);
				cloneBufAppend(&b, ctx->report->ids[k].new_id,
					strlen(ctx->report->ids[k].new_id));
				i += (u32)old_len;
				run = i;
				replaced = 1;
			}
			for (u32 k = 0; k < hash_count && !replaced; k++) {
				if (CLONE_SHA_TEXT_LEN > len - i || in[i] != hashes[k].old_hex[0] ||
						memcmp(in + i, hashes[k].old_hex, CLONE_SHA_TEXT_LEN) != 0) continue;
				if (i + CLONE_SHA_TEXT_LEN < len &&
						isxdigit((unsigned char)in[i + CLONE_SHA_TEXT_LEN])) continue;
				cloneBufAppend(&b, in + run, i - run);
				cloneBufAppend(&b, hashes[k].new_hex, CLONE_SHA_TEXT_LEN);
				i += CLONE_SHA_TEXT_LEN;
				run = i;
				replaced = 1;
			}
		}
		if (!replaced) i++;
	}
	cloneBufAppend(&b, in + run, len - run);
	if (b.failed) {
		free(b.data);
		return NULL;
	}
	if (!b.data) {
		b.data = (char *)malloc(1);
		if (!b.data) return NULL;
		b.data[0] = '\0';
	}
	*out_len = (u32)b.len;
	return b.data;
}

/*
 * After rewriting, an old id must not survive anywhere it would still be read
 * as that id: bounded on the left, and on the right by anything other than a
 * name character. That also catches forms the token rewrite does not handle
 * (an id inside a binary member, or one followed by "::member").
 */
static const char *cloneFindStaleId(const clone_ctx_t *ctx, const char *data, u32 len)
{
	for (u32 k = 0; k < ctx->report->id_count; k++) {
		const char *old = ctx->report->ids[k].old_id;
		size_t old_len = ctx->old_len[k];

		if (old_len == 0 || old_len > len) continue;
		if (strcmp(old, ctx->report->ids[k].new_id) == 0) continue;
		for (u32 i = 0; i + old_len <= len; i++) {
			if (data[i] != old[0] || memcmp(data + i, old, old_len) != 0) continue;
			if (i > 0 && cloneIsIdChar((unsigned char)data[i - 1])) continue;
			if (i + old_len < len) {
				unsigned char next = (unsigned char)data[i + old_len];
				if (isalnum(next) || next == '_' || next == '.' || next == '-') continue;
			}
			return old;
		}
	}
	return NULL;
}

static s32 cloneWriteArchive(clone_ctx_t *ctx, const void *bytes, u32 size,
		const char *name, const char *out_file, s32 depth, const char *prefix);

/* Rebuild one embedded archive through a scratch file and return its bytes. */
static void *cloneNestedBytes(clone_ctx_t *ctx, const void *nested, u32 nested_size,
		const char *entry, s32 depth, const char *prefix, u32 *out_size)
{
	char scratch[CLONE_PATH_MAX];
	void *rebuilt;
	int n = snprintf(scratch, sizeof(scratch), "%s.clone%u.tmp",
		ctx->out_path, (unsigned)ctx->scratch_seq++);

	*out_size = 0;
	if (n < 0 || (size_t)n >= sizeof(scratch)) {
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_WRITE, "output path is too long");
		return NULL;
	}
	if (!cloneWriteArchive(ctx, nested, nested_size, entry, scratch, depth, prefix)) {
		remove(scratch);
		return NULL;
	}
	rebuilt = cloneReadFile(scratch, out_size);
	remove(scratch);
	if (!rebuilt) {
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_WRITE,
			"rebuilt %s could not be read back", entry);
	}
	return rebuilt;
}

/* Raw and canonical hash of one embedded archive before and after rebuild. */
static void cloneAddHashPairs(clone_hash_pair_t *hashes, u32 *hash_count,
		const void *before, u32 before_size, const void *after, u32 after_size)
{
	cloneShaHex(before, before_size, hashes[*hash_count].old_hex);
	cloneShaHex(after, after_size, hashes[*hash_count].new_hex);
	(*hash_count)++;
	if (weaponGraphArchiveCanonicalSha256Bytes(before, before_size,
			hashes[*hash_count].old_hex) == 0 &&
			weaponGraphArchiveCanonicalSha256Bytes(after, after_size,
			hashes[*hash_count].new_hex) == 0) {
		(*hash_count)++;
	}
}

/* Pass 2: rebuild one archive, embedded archives first. */
static s32 cloneWriteArchive(clone_ctx_t *ctx, const void *bytes, u32 size,
		const char *name, const char *out_file, s32 depth, const char *prefix)
{
	char old_id[CATALOG_ID_LEN];
	char family[ASSET_ARCHIVE_WRITER_FAMILY_MAX];
	char sidecar[ASSET_ARCHIVE_WRITER_PATH_MAX + 16];
	char path[CLONE_PATH_MAX];
	const char *new_id;
	const char *descriptor_name = assetArchiveDescriptorForPath(name);
	const char *ext = cloneExt(name);
	clone_names_t names;
	clone_members_t members;
	void **nested_data = NULL;
	u32 *nested_size = NULL;
	clone_hash_pair_t *hashes = NULL;
	u32 hash_count = 0;
	mod_archive_writer_t *arc = NULL;
	asset_archive_writer_t *writer = NULL;
	s32 source_uses_sidecars = 0;
	s32 wrote_descriptor = 0;
	s32 ok = 0;

	memset(&names, 0, sizeof(names));
	memset(&members, 0, sizeof(members));
	if (!descriptor_name ||
			!cloneReadArchiveId(ctx, bytes, size, name, old_id, sizeof(old_id))) {
		if (!descriptor_name) {
			cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_SOURCE,
				"%s is not a typed archive", cloneLeaf(name));
		}
		return 0;
	}
	new_id = cloneNewIdFor(ctx, old_id);
	if (!new_id) {
		return cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_IDS,
			"%s has no new id", old_id);
	}
	cloneSlug(strlen(ext) > 3 ? ext + 3 : "asset", family, sizeof(family));

	if (!cloneListEntries(bytes, size, &names)) {
		return cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_SOURCE,
			"%s could not be listed", cloneLeaf(name));
	}
	if (!cloneBuildMembers(ctx, &names, prefix, &members)) {
		cloneNamesFree(&names);
		return 0;
	}
	for (u32 i = 0; i < names.count; i++) {
		if (cloneIsMeta(names.items[i]) && cloneEndsWith(names.items[i], ".sha256")) {
			source_uses_sidecars = 1;
			break;
		}
	}
	nested_data = (void **)calloc(members.count ? members.count : 1, sizeof(void *));
	nested_size = (u32 *)calloc(members.count ? members.count : 1, sizeof(u32));
	/* Two hash forms per embedded archive: raw bytes and canonical content. */
	hashes = (clone_hash_pair_t *)calloc(members.count ? (size_t)members.count * 2 : 1,
		sizeof(clone_hash_pair_t));
	if (!nested_data || !nested_size || !hashes) {
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_WRITE, "out of memory");
		goto cleanup;
	}

	/* Embedded archives first, so their new hashes are known to the parent. */
	for (u32 i = 0; i < members.count; i++) {
		const clone_member_t *member = &members.items[i];
		u32 source_size = 0;
		void *owned = NULL;
		const void *source;

		if (!assetArchivePathIsTyped(member->entry)) continue;
		if (!cloneJoin(prefix, member->entry, path, sizeof(path))) {
			cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_SOURCE,
				"member path is too long: %s", member->entry);
			goto cleanup;
		}
		source = cloneMemberData(ctx, member, bytes, size, name, &source_size, &owned);
		if (!source) goto cleanup;
		nested_data[i] = cloneNestedBytes(ctx, source, source_size, member->entry,
			depth + 1, path, &nested_size[i]);
		if (nested_data[i] && member->in_source) {
			/* The parent may record the hash of the archive it shipped with,
			 * which is the source member even when an override replaces it. */
			if (member->from_override >= 0) {
				u32 original_size = 0;
				void *original = modArchiveExtractMemAlloc(bytes, size,
					member->entry, &original_size);
				if (original) {
					cloneAddHashPairs(hashes, &hash_count, original, original_size,
						nested_data[i], nested_size[i]);
					free(original);
				}
			} else {
				cloneAddHashPairs(hashes, &hash_count, source, source_size,
					nested_data[i], nested_size[i]);
			}
		}
		free(owned);
		if (!nested_data[i]) goto cleanup;
	}

	arc = modArchiveBegin(out_file);
	writer = (asset_archive_writer_t *)calloc(1, sizeof(*writer));
	if (!arc || !writer ||
			assetArchiveWriterInit(writer, arc, family, new_id) != MODARCHIVE_OK) {
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_WRITE,
			"%s could not be opened for writing", cloneLeaf(out_file));
		goto cleanup;
	}
	assetArchiveWriterSetProvenance(writer,
		(ctx->tool && ctx->tool[0]) ? ctx->tool : "asset-archive-clone",
		cloneLeaf(name), -1, old_id);

	/* Public members: source order, then members added by overrides. */
	for (u32 i = 0; i < members.count; i++) {
		const clone_member_t *member = &members.items[i];
		const char *entry = member->entry;
		s32 with_sidecar;
		s32 r;

		snprintf(sidecar, sizeof(sidecar), ASSET_ARCHIVE_META_DIR "/%s.sha256", entry);
		with_sidecar = member->in_source ? cloneNamesHas(&names, sidecar)
			: source_uses_sidecars;

		if (nested_data[i]) {
			r = with_sidecar
				? assetArchiveWriterAddPublicMem(writer, entry, nested_data[i],
					nested_size[i], "dependency")
				: assetArchiveWriterAddBundledMem(writer, entry, nested_data[i],
					nested_size[i], "dependency");
		} else {
			u32 data_size = 0;
			void *owned = NULL;
			char *rewritten = NULL;
			const char *data = (const char *)cloneMemberData(ctx, member, bytes, size,
				name, &data_size, &owned);
			const char *stale;

			if (!data) goto cleanup;
			if (cloneIsText(entry)) {
				rewritten = cloneRewriteText(ctx, data, data_size, hashes, hash_count,
					&data_size);
				free(owned);
				owned = NULL;
				if (!rewritten) {
					cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_WRITE, "out of memory");
					goto cleanup;
				}
				data = rewritten;
			}
			stale = cloneFindStaleId(ctx, data, data_size);
			if (stale) {
				cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_IDS,
					"%s::%s still names %s in a form that cannot be re-identified",
					cloneLeaf(name), entry, stale);
				free(owned);
				free(rewritten);
				goto cleanup;
			}
			if (strcmp(entry, descriptor_name) == 0) {
				char declared[CATALOG_ID_LEN];
				if (!cloneDescriptorCatalogId(data, data_size, declared, sizeof(declared)) ||
						strcmp(declared, new_id) != 0) {
					cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_ARGS,
						"%s::%s must keep its catalog_id", cloneLeaf(name), entry);
					free(owned);
					free(rewritten);
					goto cleanup;
				}
				r = assetArchiveWriterAddDescriptor(writer, entry, data, data_size);
				wrote_descriptor = 1;
			} else if (with_sidecar) {
				r = assetArchiveWriterAddPublicMem(writer, entry, data, data_size, "public");
			} else {
				r = assetArchiveWriterAddBundledMem(writer, entry, data, data_size, "public");
			}
			free(owned);
			free(rewritten);
		}
		if (r != MODARCHIVE_OK) {
			cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_WRITE,
				"%s::%s could not be written", cloneLeaf(name), entry);
			goto cleanup;
		}
	}
	if (!wrote_descriptor) {
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_ARGS,
			"%s would lose its root descriptor %s", cloneLeaf(name), descriptor_name);
		goto cleanup;
	}

	/* `_meta`: carry the manifest and family-owned records with ids rewritten;
	 * the writer regenerates the records it owns. */
	for (u32 i = 0; i < names.count; i++) {
		const char *entry = names.items[i];
		const char *leaf;
		u32 source_size = 0;
		u32 data_size = 0;
		char *source;
		char *data;
		const char *stale;
		s32 r;

		if (!cloneIsMeta(entry)) continue;
		leaf = entry + strlen(ASSET_ARCHIVE_META_DIR) + 1;
		if (cloneIsWriterMeta(leaf)) continue;
		source = (char *)modArchiveExtractMemAlloc(bytes, size, entry, &source_size);
		if (!source) {
			cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_SOURCE,
				"%s::%s could not be read", cloneLeaf(name), entry);
			goto cleanup;
		}
		data = source;
		data_size = source_size;
		if (cloneIsText(entry)) {
			data = cloneRewriteText(ctx, source, source_size, hashes, hash_count, &data_size);
			free(source);
			if (!data) {
				cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_WRITE, "out of memory");
				goto cleanup;
			}
		}
		stale = cloneFindStaleId(ctx, data, data_size);
		if (stale) {
			cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_IDS,
				"%s::%s still names %s in a form that cannot be re-identified",
				cloneLeaf(name), entry, stale);
			free(data);
			goto cleanup;
		}
		r = (strcmp(leaf, ASSET_ARCHIVE_META_MANIFEST) == 0)
			? assetArchiveWriterAddManifestJson(writer, data, data_size)
			: assetArchiveWriterAddMetaJson(writer, leaf, data, data_size);
		free(data);
		if (r != MODARCHIVE_OK) {
			cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_WRITE,
				"%s::%s could not be written", cloneLeaf(name), entry);
			goto cleanup;
		}
	}

	if (assetArchiveWriterFinishMetadata(writer) != MODARCHIVE_OK) {
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_WRITE,
			"%s metadata could not be written", cloneLeaf(name));
		goto cleanup;
	}
	if (modArchiveFinish(arc) != MODARCHIVE_OK) {
		arc = NULL;
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_WRITE,
			"%s could not be finished", cloneLeaf(out_file));
		goto cleanup;
	}
	arc = NULL;
	ctx->report->archive_count++;
	ok = 1;

cleanup:
	if (arc) modArchiveAbort(arc);
	free(writer);
	if (nested_data) {
		for (u32 i = 0; i < members.count; i++) free(nested_data[i]);
	}
	free(nested_data);
	free(nested_size);
	free(hashes);
	cloneMembersFree(&members);
	cloneNamesFree(&names);
	return ok;
}

/* An override path: members joined by "::", none of them under `_meta`. */
static s32 cloneOverridePathOk(const char *path)
{
	const char *part = path;

	if (!path || !path[0] || strlen(path) >= CLONE_PATH_MAX) return 0;
	for (;;) {
		const char *next = strstr(part, "::");
		size_t n = next ? (size_t)(next - part) : strlen(part);

		if (n == 0 || part[0] == '/' || part[0] == '\\') return 0;
		if (n >= 5 && strncmp(part, ASSET_ARCHIVE_META_DIR, 5) == 0 &&
				(n == 5 || part[5] == '/')) return 0;
		for (size_t i = 0; i < n; i++) {
			if (part[i] == '\\') return 0;
		}
		if (!next) break;
		part = next + 2;
	}
	return 1;
}

s32 assetArchiveRebuildFile(const char *src_path, const char *out_path,
		const char *new_catalog_id, const char *tool,
		const asset_archive_clone_override_t *overrides, u32 override_count,
		asset_archive_clone_report_t *report,
		char *err, size_t err_cap)
{
	asset_archive_clone_report_t *work = report;
	clone_ctx_t *ctx;
	char policy_err[256];
	const char *colon;
	void *source = NULL;
	u32 source_size = 0;
	size_t ns_len;
	s32 code;

	if (err && err_cap) err[0] = '\0';
	if (report) memset(report, 0, sizeof(*report));

	ctx = (clone_ctx_t *)calloc(1, sizeof(*ctx));
	if (!ctx) {
		if (err && err_cap) snprintf(err, err_cap, "out of memory");
		return ASSET_ARCHIVE_CLONE_ERR_WRITE;
	}
	ctx->tool = tool;
	ctx->out_path = out_path;
	ctx->overrides = overrides;
	ctx->override_count = overrides ? override_count : 0;
	ctx->err = err;
	ctx->err_cap = err_cap;
	ctx->code = ASSET_ARCHIVE_CLONE_OK;

	if (!work) {
		work = (asset_archive_clone_report_t *)calloc(1, sizeof(*work));
		if (!work) {
			if (err && err_cap) snprintf(err, err_cap, "out of memory");
			free(ctx);
			return ASSET_ARCHIVE_CLONE_ERR_WRITE;
		}
	}
	ctx->report = work;

	if (!src_path || !src_path[0] || !out_path || !out_path[0] ||
			!new_catalog_id || !new_catalog_id[0]) {
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_ARGS,
			"missing source path, output path or new catalog id");
	} else if (!assetArchivePathIsTyped(src_path) || !assetArchivePathIsTyped(out_path) ||
			!cloneEqNoCase(cloneExt(src_path), cloneExt(out_path))) {
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_ARGS,
			"source and output must be typed archives of the same family");
	} else if (!cloneValidId(new_catalog_id)) {
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_ARGS,
			"new catalog id must look like namespace:name and be under %d characters",
			CATALOG_ID_LEN);
	} else if (cloneSamePath(src_path, out_path)) {
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_ARGS,
			"output path is the source archive");
	} else if (strlen(out_path) + 32 >= CLONE_PATH_MAX) {
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_ARGS, "output path is too long");
	} else if (cloneFileExists(out_path)) {
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_ARGS,
			"output already exists: %s", cloneLeaf(out_path));
	} else if (override_count > 0 && !overrides) {
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_ARGS, "override list is missing");
	} else if (override_count > ASSET_ARCHIVE_CLONE_MAX_OVERRIDES) {
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_ARGS,
			"more than %d overrides", ASSET_ARCHIVE_CLONE_MAX_OVERRIDES);
	}
	for (u32 i = 0; i < ctx->override_count && ctx->code == ASSET_ARCHIVE_CLONE_OK; i++) {
		const asset_archive_clone_override_t *ov = &overrides[i];

		if (!cloneOverridePathOk(ov->entry)) {
			cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_ARGS,
				"override %u does not name a public archive member", (unsigned)i);
		} else if (!ov->remove && !ov->data && ov->len != 0) {
			cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_ARGS,
				"override %s has a length but no data", ov->entry);
		}
		for (u32 k = 0; k < i && ctx->code == ASSET_ARCHIVE_CLONE_OK; k++) {
			if (strcmp(overrides[k].entry, ov->entry) == 0) {
				cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_ARGS,
					"override %s is listed twice", ov->entry);
			}
		}
	}
	if (ctx->code != ASSET_ARCHIVE_CLONE_OK) goto done;

	source = cloneReadFile(src_path, &source_size);
	if (!source) {
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_SOURCE,
			"source archive could not be read: %s", cloneLeaf(src_path));
		goto done;
	}
	policy_err[0] = '\0';
	if (assetArchiveValidateBytes(source, source_size, src_path,
			ASSET_ARCHIVE_VALIDATE_RELEASE, policy_err, sizeof(policy_err)) != 0) {
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_SOURCE,
			"source is not a valid typed archive: %s",
			policy_err[0] ? policy_err : "release validation failed");
		goto done;
	}

	colon = strchr(new_catalog_id, ':');
	ns_len = (size_t)(colon - new_catalog_id);
	memcpy(ctx->new_ns, new_catalog_id, ns_len);
	ctx->new_ns[ns_len] = '\0';
	cloneSlug(colon + 1, ctx->new_root_slug, sizeof(ctx->new_root_slug));
	if (!ctx->new_root_slug[0]) {
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_ARGS,
			"new catalog id has no usable name part");
		goto done;
	}
	snprintf(work->ids[0].new_id, sizeof(work->ids[0].new_id), "%s", new_catalog_id);

	if (!cloneCollectIds(ctx, source, source_size, src_path, 0, "")) goto done;
	for (u32 i = 0; i < work->id_count; i++) {
		ctx->old_len[i] = strlen(work->ids[i].old_id);
	}

	if (!cloneWriteArchive(ctx, source, source_size, src_path, out_path, 0, "")) {
		remove(out_path);
		goto done;
	}
	for (u32 i = 0; i < ctx->override_count; i++) {
		if (!ctx->override_used[i]) {
			remove(out_path);
			cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_ARGS,
				"override %s does not belong to this archive or to an archive embedded in it",
				overrides[i].entry);
			goto done;
		}
	}

	policy_err[0] = '\0';
	if (assetArchiveValidateFile(out_path, ASSET_ARCHIVE_VALIDATE_RELEASE,
			policy_err, sizeof(policy_err)) != 0) {
		remove(out_path);
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_OUTPUT,
			"result failed release validation: %s",
			policy_err[0] ? policy_err : "no detail");
	}

done:
	code = ctx->code;
	free(source);
	if (code != ASSET_ARCHIVE_CLONE_OK && report) {
		memset(report, 0, sizeof(*report));
	}
	if (work != report) free(work);
	free(ctx);
	return code;
}

s32 assetArchiveCloneFile(const char *src_path, const char *out_path,
		const char *new_catalog_id, const char *tool,
		asset_archive_clone_report_t *report,
		char *err, size_t err_cap)
{
	return assetArchiveRebuildFile(src_path, out_path, new_catalog_id, tool,
		NULL, 0, report, err, err_cap);
}
