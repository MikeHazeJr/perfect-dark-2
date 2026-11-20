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
#include "loader_walker_mesh_source.h"
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

		/* Holds for every root id within ASSET_ARCHIVE_CLONE_ROOT_ID_MAX. */
		if (prefix_len + 9 >= CATALOG_ID_LEN) {
			return cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_IDS,
				"new catalog id is too long to derive ids for embedded archives "
				"(keep it within %d characters)", (int)ASSET_ARCHIVE_CLONE_ROOT_ID_MAX);
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
/*
 * A descriptor value the archive's manifest repeats (a vehicle's model_file,
 * physics_file and behavior_graph, for example) follows the descriptor. When
 * an override changed such a value, the carried manifest takes the new value
 * too, so the two never disagree. Only top-level string members change, only
 * where the source descriptor and the source manifest held the same value,
 * and only to values a JSON string holds without escapes (paths and ids).
 */
static u32 cloneJsonSkipWs(const char *s, u32 n, u32 i)
{
	while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
	return i;
}

/* `i` at an opening quote: just past the closing quote, or `n` if unterminated. */
static u32 cloneJsonSkipString(const char *s, u32 n, u32 i)
{
	for (i++; i < n; i++) {
		if (s[i] == '\\') i++;
		else if (s[i] == '"') return i + 1;
	}
	return n;
}

/* `i` at a value: just past it, or `n` if it does not end. */
static u32 cloneJsonSkipValue(const char *s, u32 n, u32 i)
{
	u32 depth = 0;

	if (i >= n) return n;
	if (s[i] == '"') return cloneJsonSkipString(s, n, i);
	if (s[i] != '{' && s[i] != '[') {
		while (i < n && s[i] != ',' && s[i] != '}' && s[i] != ']'
				&& s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && s[i] != '\n') i++;
		return i;
	}
	while (i < n) {
		if (s[i] == '"') {
			i = cloneJsonSkipString(s, n, i);
			continue;
		}
		if (s[i] == '{' || s[i] == '[') depth++;
		else if ((s[i] == '}' || s[i] == ']') && --depth == 0) return i + 1;
		i++;
	}
	return n;
}

/* The value span (quotes included) of the top-level string member `key`. */
static s32 cloneJsonTopString(const char *s, u32 n, const char *key, u32 *vstart, u32 *vend)
{
	const size_t klen = strlen(key);
	u32 i = cloneJsonSkipWs(s, n, 0);

	if (i >= n || s[i] != '{') return 0;
	i = cloneJsonSkipWs(s, n, i + 1);
	while (i < n && s[i] == '"') {
		const u32 kstart = i + 1;
		const u32 kend = cloneJsonSkipString(s, n, i);
		u32 v;
		u32 after;

		if (kend >= n) return 0;
		i = cloneJsonSkipWs(s, n, kend);
		if (i >= n || s[i] != ':') return 0;
		v = cloneJsonSkipWs(s, n, i + 1);
		after = cloneJsonSkipValue(s, n, v);
		if (after >= n) return 0;
		if (kend - 1 - kstart == klen && memcmp(s + kstart, key, klen) == 0) {
			if (s[v] != '"') return 0;
			*vstart = v;
			*vend = after;
			return 1;
		}
		i = cloneJsonSkipWs(s, n, after);
		if (i < n && s[i] == ',') i = cloneJsonSkipWs(s, n, i + 1);
	}
	return 0;
}

/* The value span of the top-level member `key`, whatever its type. */
static s32 cloneJsonTopValue(const char *s, u32 n, const char *key, u32 *vstart, u32 *vend)
{
	const size_t klen = strlen(key);
	u32 i = cloneJsonSkipWs(s, n, 0);

	if (i >= n || s[i] != '{') return 0;
	i = cloneJsonSkipWs(s, n, i + 1);
	while (i < n && s[i] == '"') {
		const u32 kstart = i + 1;
		const u32 kend = cloneJsonSkipString(s, n, i);
		u32 v;
		u32 after;

		if (kend >= n) return 0;
		i = cloneJsonSkipWs(s, n, kend);
		if (i >= n || s[i] != ':') return 0;
		v = cloneJsonSkipWs(s, n, i + 1);
		after = cloneJsonSkipValue(s, n, v);
		if (after >= n) return 0;
		if (kend - 1 - kstart == klen && memcmp(s + kstart, key, klen) == 0) {
			*vstart = v;
			*vend = after;
			return 1;
		}
		i = cloneJsonSkipWs(s, n, after);
		if (i < n && s[i] == ',') i = cloneJsonSkipWs(s, n, i + 1);
	}
	return 0;
}

/*
 * A scenario's navigation record, `_meta/generated-navmesh.json`, repeats two
 * facts about eleven of the scenario's members: a hash of each, and the row
 * count of six tables. The game compares both with the members before it
 * uses the level's navigation and refuses the level when they differ
 * (scenario_source_runtime.c: "source counts mismatch public navigation
 * tables", "source hashes mismatch public navigation inputs").
 *
 * A plain clone never changes those members: none of them holds a catalog
 * id. A rebuild with overrides can. Forge saves a level by overriding its
 * pads.json, spawns.json and volumes.json, and the record then has to say
 * what the saved level holds, or the level its author just saved would not
 * load. So the record is one of the things a rebuild derives from its result.
 */
#define CLONE_NAV_INPUT_COUNT 11
#define CLONE_NAV_TABLE_COUNT 6

/* The members the record names, in the record's order. */
static const char *const s_CloneNavInputs[CLONE_NAV_INPUT_COUNT] = {
	"scene.glb", "collision.obj", "navigation.ini", "portals.json", "pads.json",
	"spawns.json", "volumes.json", "navigation/waypoints.json",
	"navigation/waygroups.json", "navigation/covers.json", "navigation/paths.json",
};
/* The tables it counts, in the record's order, and which member each one is. */
static const char *const s_CloneNavTables[CLONE_NAV_TABLE_COUNT] = {
	"pads", "volumes", "waypoints", "waygroups", "covers", "paths",
};
static const s32 s_CloneNavTableInput[CLONE_NAV_TABLE_COUNT] = { 4, 6, 7, 8, 9, 10 };

typedef struct clone_nav_facts {
	char hex[CLONE_NAV_INPUT_COUNT][SHA256_HEX_SIZE];
	s32 have[CLONE_NAV_INPUT_COUNT];
	s32 rows[CLONE_NAV_INPUT_COUNT];   /* -1: not a table with a rows array */
} clone_nav_facts_t;

/* How many rows a table file holds: the elements of its top-level "rows" array, or -1. */
static s32 cloneJsonRowCount(const char *s, u32 n)
{
	u32 vs = 0;
	u32 ve = 0;
	u32 i;
	s32 count = 0;

	if (!cloneJsonTopValue(s, n, "rows", &vs, &ve) || s[vs] != '[') return -1;
	i = cloneJsonSkipWs(s, ve, vs + 1);
	if (i < ve && s[i] == ']') return 0;
	while (i < ve) {
		const u32 after = cloneJsonSkipValue(s, ve, i);

		if (after >= ve || after == i) return -1;
		count++;
		i = cloneJsonSkipWs(s, ve, after);
		if (i < ve && s[i] == ',') {
			i = cloneJsonSkipWs(s, ve, i + 1);
			continue;
		}
		return (i < ve && s[i] == ']') ? count : -1;
	}
	return -1;
}

/* Notes a member as it is written to the result, when the record names it. */
static void cloneNavNote(clone_nav_facts_t *nav, const char *entry, const char *data, u32 size)
{
	for (s32 i = 0; i < CLONE_NAV_INPUT_COUNT; i++) {
		if (strcmp(entry, s_CloneNavInputs[i]) != 0) continue;
		cloneShaHex(data, size, nav->hex[i]);
		nav->rows[i] = cloneEndsWith(entry, ".json") ? cloneJsonRowCount(data, size) : -1;
		nav->have[i] = 1;
		return;
	}
}

/*
 * The record with its two lists made true for the members the result holds.
 * NULL when the record already says exactly that, or when it cannot be
 * brought up to date here: a member it names is not in the archive, a table
 * has no rows array, or the record has no such lists. The caller then writes
 * the record as it is, and the game is the judge of it.
 *
 * The lists are written the way the extractor writes them
 * (romextract_pdarena.c), which is the exact text the game looks for.
 */
static char *cloneNavRefresh(const char *record, u32 record_size,
		const clone_nav_facts_t *nav, u32 *out_size)
{
	char counts[256];
	char hashes[CLONE_NAV_INPUT_COUNT * (SHA256_HEX_SIZE + 48) + 16];
	size_t used = 0;
	u32 cs = 0, ce = 0, hs = 0, he = 0;
	u32 first_s, first_e, second_s, second_e;
	const char *first_text;
	const char *second_text;
	size_t first_len, second_len, total;
	char *out;
	char *w;

	for (s32 i = 0; i < CLONE_NAV_INPUT_COUNT; i++) {
		if (!nav->have[i]) return NULL;
	}
	for (s32 k = 0; k < CLONE_NAV_TABLE_COUNT; k++) {
		if (nav->rows[s_CloneNavTableInput[k]] < 0) return NULL;
	}
	snprintf(counts, sizeof(counts),
		"{ \"%s\": %d, \"%s\": %d, \"%s\": %d, \"%s\": %d, \"%s\": %d, \"%s\": %d }",
		s_CloneNavTables[0], nav->rows[s_CloneNavTableInput[0]],
		s_CloneNavTables[1], nav->rows[s_CloneNavTableInput[1]],
		s_CloneNavTables[2], nav->rows[s_CloneNavTableInput[2]],
		s_CloneNavTables[3], nav->rows[s_CloneNavTableInput[3]],
		s_CloneNavTables[4], nav->rows[s_CloneNavTableInput[4]],
		s_CloneNavTables[5], nav->rows[s_CloneNavTableInput[5]]);
	used = (size_t)snprintf(hashes, sizeof(hashes), "{ ");
	for (s32 i = 0; i < CLONE_NAV_INPUT_COUNT; i++) {
		used += (size_t)snprintf(hashes + used, sizeof(hashes) - used, "%s\"%s\": \"%s\"",
			i ? ", " : "", s_CloneNavInputs[i], nav->hex[i]);
		if (used >= sizeof(hashes)) return NULL;
	}
	used += (size_t)snprintf(hashes + used, sizeof(hashes) - used, " }");
	if (used >= sizeof(hashes)) return NULL;

	if (!cloneJsonTopValue(record, record_size, "source_counts", &cs, &ce) ||
			!cloneJsonTopValue(record, record_size, "source_hashes", &hs, &he) ||
			record[cs] != '{' || record[hs] != '{') {
		return NULL;
	}
	if (ce - cs == strlen(counts) && memcmp(record + cs, counts, ce - cs) == 0 &&
			he - hs == strlen(hashes) && memcmp(record + hs, hashes, he - hs) == 0) {
		return NULL;
	}

	if (cs < hs) {
		first_s = cs; first_e = ce; first_text = counts;
		second_s = hs; second_e = he; second_text = hashes;
	} else {
		first_s = hs; first_e = he; first_text = hashes;
		second_s = cs; second_e = ce; second_text = counts;
	}
	first_len = strlen(first_text);
	second_len = strlen(second_text);
	total = (size_t)first_s + first_len + (second_s - first_e) + second_len
		+ (record_size - second_e);
	if (total > 0xffffffffu) return NULL;
	out = (char *)malloc(total + 1);
	if (!out) return NULL;
	w = out;
	memcpy(w, record, first_s);
	w += first_s;
	memcpy(w, first_text, first_len);
	w += first_len;
	memcpy(w, record + first_e, second_s - first_e);
	w += second_s - first_e;
	memcpy(w, second_text, second_len);
	w += second_len;
	memcpy(w, record + second_e, record_size - second_e);
	w += record_size - second_e;
	*w = '\0';
	*out_size = (u32)total;
	return out;
}

/* See asset_archive_clone.h. */
s32 assetArchiveCloneJsonDropTopMember(char *s, u32 *len, const char *key)
{
	const size_t klen = key ? strlen(key) : 0;
	u32 n;
	u32 i;
	u32 prev_comma;

	if (!s || !len || !klen) return 0;
	n = *len;
	prev_comma = n; /* the comma before the current member; n while there is none */
	i = cloneJsonSkipWs(s, n, 0);
	if (i >= n || s[i] != '{') return 0;
	i = cloneJsonSkipWs(s, n, i + 1);
	while (i < n && s[i] == '"') {
		const u32 mstart = i;
		const u32 kstart = i + 1;
		const u32 kend = cloneJsonSkipString(s, n, i);
		u32 v;
		u32 after;
		u32 next;

		if (kend >= n) return 0;
		i = cloneJsonSkipWs(s, n, kend);
		if (i >= n || s[i] != ':') return 0;
		v = cloneJsonSkipWs(s, n, i + 1);
		after = cloneJsonSkipValue(s, n, v);
		if (after >= n) return 0;
		next = cloneJsonSkipWs(s, n, after);
		if (kend - 1 - kstart == klen && memcmp(s + kstart, key, klen) == 0) {
			u32 cut_start = mstart;
			u32 cut_end = after;

			if (next < n && s[next] == ',') {
				/* Not the last member: it goes with the comma after it, up to
				 * where the next member starts. */
				cut_end = cloneJsonSkipWs(s, n, next + 1);
			} else if (prev_comma < n) {
				/* The last member: it goes with the comma before it. */
				cut_start = prev_comma;
			}
			memmove(s + cut_start, s + cut_end, n - cut_end);
			*len = n - (cut_end - cut_start);
			return 1;
		}
		if (next < n && s[next] == ',') {
			prev_comma = next;
			i = cloneJsonSkipWs(s, n, next + 1);
		} else {
			i = next;
		}
	}
	return 0;
}

static s32 cloneJsonPlain(const char *value, u32 len)
{
	for (u32 i = 0; i < len; i++) {
		if (value[i] == '"' || value[i] == '\\' || (unsigned char)value[i] < 0x20) return 0;
	}
	return 1;
}

/* One `key = value` descriptor line: trimmed spans; 0 for comments, sections
 * and lines without a key. */
static s32 cloneIniLine(const char *line, u32 len, u32 *ks, u32 *ke, u32 *vs, u32 *ve)
{
	u32 a = 0;
	u32 b = len;
	u32 eq;

	while (a < b && (line[a] == ' ' || line[a] == '\t')) a++;
	while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t' || line[b - 1] == '\r')) b--;
	if (a == b || line[a] == ';' || line[a] == '#' || line[a] == '[') return 0;
	for (eq = a; eq < b && line[eq] != '='; eq++) {
	}
	if (eq == b) return 0;
	*ks = a;
	*ke = eq;
	while (*ke > *ks && (line[*ke - 1] == ' ' || line[*ke - 1] == '\t')) (*ke)--;
	*vs = eq + 1;
	while (*vs < b && (line[*vs] == ' ' || line[*vs] == '\t')) (*vs)++;
	*ve = b;
	return *ke > *ks;
}

/* The first value of `key` in descriptor text (sections ignored). */
static s32 cloneIniValue(const char *text, u32 n, const char *key, u32 *vs, u32 *ve)
{
	const size_t klen = strlen(key);
	u32 pos = 0;

	while (pos < n) {
		u32 end = pos;
		u32 ks, ke, a, b;

		while (end < n && text[end] != '\n') end++;
		if (cloneIniLine(text + pos, end - pos, &ks, &ke, &a, &b)
				&& ke - ks == klen && memcmp(text + pos + ks, key, klen) == 0) {
			*vs = pos + a;
			*ve = pos + b;
			return 1;
		}
		pos = end + 1;
	}
	return 0;
}

/* Replace the JSON string at [ms, me) (quotes included) in *out (length *len)
 * with value. 0 when out of memory (*out is then still valid). */
static s32 cloneReplaceJsonString(char **out, u32 *len, u32 ms, u32 me,
		const char *value, u32 value_len)
{
	const u32 old_len = me - ms - 2;
	const u32 new_len = *len - old_len + value_len;

	if (new_len > *len) {
		char *grown = (char *)realloc(*out, (size_t)new_len + 1);
		if (!grown) return 0;
		*out = grown;
	}
	/* The closing quote and everything after it move as one. */
	memmove(*out + ms + 1 + value_len, *out + me - 1, *len - (me - 1));
	memcpy(*out + ms + 1, value, value_len);
	*len = new_len;
	(*out)[new_len] = '\0';
	return 1;
}

/* The manifest text with every mirrored value the descriptor changed brought
 * up to date. NULL when out of memory. */
static char *cloneSyncManifestMirrors(const char *manifest, u32 size,
		const char *source_desc, u32 source_size,
		const char *final_desc, u32 final_size, u32 *out_size)
{
	char *out = (char *)malloc((size_t)size + 1);
	u32 len = size;
	u32 pos = 0;

	if (!out) return NULL;
	memcpy(out, manifest, size);
	out[len] = '\0';
	while (pos < final_size) {
		u32 end = pos;
		u32 ks, ke, a, b, os, oe, ms, me;
		char key[128];

		while (end < final_size && final_desc[end] != '\n') end++;
		if (cloneIniLine(final_desc + pos, end - pos, &ks, &ke, &a, &b)
				&& ke - ks < sizeof(key)) {
			const char *value = final_desc + pos + a;
			const u32 value_len = b - a;

			memcpy(key, final_desc + pos + ks, ke - ks);
			key[ke - ks] = '\0';
			if (cloneIniValue(source_desc, source_size, key, &os, &oe)
					&& !(oe - os == value_len
						&& memcmp(source_desc + os, value, value_len) == 0)
					&& cloneJsonTopString(out, len, key, &ms, &me)
					&& me - ms - 2 == oe - os
					&& memcmp(out + ms + 1, source_desc + os, oe - os) == 0
					&& cloneJsonPlain(value, value_len)
					&& !cloneReplaceJsonString(&out, &len, ms, me, value, value_len)) {
				free(out);
				return NULL;
			}
		}
		pos = end + 1;
	}

	/* A mesh manifest's "geometry" echoes the descriptor's geometry key
	 * (model_file and its aliases, loaderWalkerMeshPublicGeometryKey). The
	 * loader takes the descriptor as the truth and the echo only as a
	 * fallback, so when an override moves the geometry (a recipe that swaps a
	 * mesh's glTF for an OBJ) the echo that named the old file follows it,
	 * instead of naming a member that may be gone. */
	for (size_t i = 0; i < loaderWalkerMeshPublicGeometryKeyCount(); i++) {
		const char *key = loaderWalkerMeshPublicGeometryKey(i);
		u32 os, oe, ns, ne, ms, me;

		if (strcmp(key, "geometry") == 0) continue; /* the same-name pass above */
		if (cloneIniValue(source_desc, source_size, key, &os, &oe)
				&& cloneIniValue(final_desc, final_size, key, &ns, &ne)
				&& !(ne - ns == oe - os
					&& memcmp(source_desc + os, final_desc + ns, oe - os) == 0)
				&& cloneJsonPlain(final_desc + ns, ne - ns)
				&& cloneJsonTopString(out, len, "geometry", &ms, &me)
				&& me - ms - 2 == oe - os
				&& memcmp(out + ms + 1, source_desc + os, oe - os) == 0) {
			if (!cloneReplaceJsonString(&out, &len, ms, me, final_desc + ns, ne - ns)) {
				free(out);
				return NULL;
			}
			break;
		}
	}
	*out_size = len;
	return out;
}

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
	/* An overridden descriptor, before and after, for the manifest mirrors. */
	char *source_desc = NULL;
	u32 source_desc_size = 0;
	char *final_desc = NULL;
	u32 final_desc_size = 0;
	/* What the result holds of the members a navigation record names. */
	clone_nav_facts_t nav;

	memset(&names, 0, sizeof(names));
	memset(&members, 0, sizeof(members));
	memset(&nav, 0, sizeof(nav));
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
			cloneNavNote(&nav, entry, data, data_size);
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
				if (r == MODARCHIVE_OK && member->from_override >= 0) {
					u32 src_size = 0;
					char *src = (char *)modArchiveExtractMemAlloc(bytes, size, entry, &src_size);

					final_desc = (char *)malloc((size_t)data_size + 1);
					if (final_desc) {
						memcpy(final_desc, data, data_size);
						final_desc[data_size] = '\0';
						final_desc_size = data_size;
					}
					if (src) {
						source_desc = cloneRewriteText(ctx, src, src_size, hashes, hash_count,
							&source_desc_size);
						free(src);
					}
					if (!final_desc || !source_desc) {
						cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_WRITE, "out of memory");
						free(owned);
						free(rewritten);
						goto cleanup;
					}
				}
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
			if (final_desc && strcmp(leaf, ASSET_ARCHIVE_META_MANIFEST) == 0) {
				u32 synced_size = 0;
				char *synced = cloneSyncManifestMirrors(data, data_size,
					source_desc, source_desc_size, final_desc, final_desc_size, &synced_size);

				free(data);
				if (!synced) {
					cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_WRITE, "out of memory");
					goto cleanup;
				}
				data = synced;
				data_size = synced_size;
			}
			if (strcmp(leaf, ASSET_ARCHIVE_META_MANIFEST) == 0
					&& cloneEqNoCase(ext, ".pdmesh")
					&& (strcmp(old_id, new_id) != 0 || strncmp(new_id, "base:", 5) != 0)) {
				/* The extractor's file belongs to its base mesh. Keep the
					* binding on in-place edits of that identity. Other meshes
					* get their own slot (B-1152). */
				(void)assetArchiveCloneJsonDropTopMember(data, &data_size,
					"source_filenum_symbol");
			}
			if (strcmp(leaf, "generated-navmesh.json") == 0) {
				/* The record says what its members hold; see cloneNavRefresh. */
				u32 fresh_size = 0;
				char *fresh = cloneNavRefresh(data, data_size, &nav, &fresh_size);

				if (fresh) {
					free(data);
					data = fresh;
					data_size = fresh_size;
				}
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
	free(source_desc);
	free(final_desc);
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

/*
 * The typed name an output path stands for: the path itself, or the path
 * without one trailing staging suffix. Returns 0 when it does not fit.
 */
static s32 cloneLogicalOut(const char *out_path, char *logical, size_t cap)
{
	const size_t m = sizeof(ASSET_ARCHIVE_CLONE_STAGING_SUFFIX) - 1;
	size_t n = strlen(out_path);

	if (n > m && cloneEqNoCase(out_path + n - m, ASSET_ARCHIVE_CLONE_STAGING_SUFFIX)) {
		n -= m;
	}
	if (n == 0 || n >= cap) return 0;
	memcpy(logical, out_path, n);
	logical[n] = '\0';
	return 1;
}

/*
 * The written file must pass release validation under the name it will
 * finally carry, through both validators: the file form the packers use and
 * the in-memory form the catalog scanner uses when it loads the archive.
 */
static s32 cloneValidateResult(const char *out_path, const char *logical,
		char *err, size_t err_cap)
{
	mod_archive_t *archive = modArchiveOpen(out_path);
	void *bytes;
	u32 size = 0;
	s32 ok;

	if (err && err_cap) err[0] = '\0';
	ok = assetArchiveValidateOpened(archive, logical, ASSET_ARCHIVE_VALIDATE_RELEASE,
		err, err_cap) == 0;
	if (archive) modArchiveClose(archive);
	if (!ok) return 0;

	bytes = cloneReadFile(out_path, &size);
	if (!bytes) {
		if (err && err_cap) snprintf(err, err_cap, "the result could not be read back");
		return 0;
	}
	ok = assetArchiveValidateBytes(bytes, size, logical, ASSET_ARCHIVE_VALIDATE_RELEASE,
		err, err_cap) == 0;
	free(bytes);
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
	char logical_out[CLONE_PATH_MAX];
	const char *colon;
	void *source = NULL;
	u32 source_size = 0;
	size_t ns_len;
	s32 code;

	if (err && err_cap) err[0] = '\0';
	if (report) memset(report, 0, sizeof(*report));
	logical_out[0] = '\0';

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
	} else if (!cloneLogicalOut(out_path, logical_out, sizeof(logical_out))) {
		cloneFail(ctx, ASSET_ARCHIVE_CLONE_ERR_ARGS, "output path is too long");
	} else if (!assetArchivePathIsTyped(src_path) || !assetArchivePathIsTyped(logical_out) ||
			!cloneEqNoCase(cloneExt(src_path), cloneExt(logical_out))) {
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
	if (!cloneValidateResult(out_path, logical_out, policy_err, sizeof(policy_err))) {
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
