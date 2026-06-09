/**
 * weapon_graph_archive.c -- shared weapon graph archive policy.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modarchive.h"
#include "weapon_graph_archive.h"

typedef struct name_list {
	char **names;
	s32 count;
	s32 cap;
} name_list_t;

static void setErr(char *err, size_t err_cap, const char *fmt, ...)
{
	if (!err || err_cap == 0) return;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(err, err_cap, fmt, ap);
	va_end(ap);
	err[err_cap - 1] = '\0';
}

static void copyStr(char *dst, size_t cap, const char *src)
{
	if (!dst || cap == 0) return;
	if (!src) src = "";
	strncpy(dst, src, cap - 1);
	dst[cap - 1] = '\0';
}

static char *trimLocal(char *s)
{
	if (!s) return s;
	while (*s && isspace((unsigned char)*s)) s++;
	if (!*s) return s;
	char *end = s + strlen(s) - 1;
	while (end >= s && isspace((unsigned char)*end)) {
		*end = '\0';
		end--;
	}
	return s;
}

static char *dupStr(const char *s)
{
	size_t len = s ? strlen(s) : 0;
	char *out = (char *)malloc(len + 1);
	if (!out) return NULL;
	if (len) memcpy(out, s, len);
	out[len] = '\0';
	return out;
}

const char *weaponGraphArchiveDescriptorForType(asset_type_e type)
{
	switch (type) {
	case ASSET_WEAPON:     return "weapon.ini";
	case ASSET_PROJECTILE: return "projectile.ini";
	case ASSET_ENTITY:     return "entity.ini";
	default:               return NULL;
	}
}

const char *weaponGraphArchiveExtensionForType(asset_type_e type)
{
	switch (type) {
	case ASSET_WEAPON:     return ".pdweapon";
	case ASSET_PROJECTILE: return ".pdprojectile";
	case ASSET_ENTITY:     return ".pdentity";
	default:               return NULL;
	}
}

const char *weaponGraphArchiveTypeName(asset_type_e type)
{
	switch (type) {
	case ASSET_WEAPON:     return "weapon";
	case ASSET_PROJECTILE: return "projectile";
	case ASSET_ENTITY:     return "entity";
	default:               return "unknown";
	}
}

static const char *sectionForType(asset_type_e type)
{
	switch (type) {
	case ASSET_WEAPON:     return "weapon";
	case ASSET_PROJECTILE: return "projectile";
	case ASSET_ENTITY:     return "entity";
	default:               return NULL;
	}
}

s32 weaponGraphArchiveGraphRequired(asset_type_e type)
{
	return (type == ASSET_WEAPON || type == ASSET_PROJECTILE ||
			type == ASSET_ENTITY) ? 1 : 0;
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

static asset_type_e typeForNestedArchiveName(const char *name)
{
	if (endsWithNoCase(name, ".pdprojectile")) return ASSET_PROJECTILE;
	if (endsWithNoCase(name, ".pdentity")) return ASSET_ENTITY;
	return ASSET_NONE;
}

static s32 nameListAdd(name_list_t *list, const char *name)
{
	if (!list || !name || !name[0]) return -1;
	if (list->count >= list->cap) {
		s32 new_cap = list->cap ? list->cap * 2 : 16;
		char **next = (char **)realloc(list->names, sizeof(char *) * (size_t)new_cap);
		if (!next) return -1;
		list->names = next;
		list->cap = new_cap;
	}
	list->names[list->count] = dupStr(name);
	if (!list->names[list->count]) return -1;
	list->count++;
	return 0;
}

static void nameListFree(name_list_t *list)
{
	if (!list) return;
	for (s32 i = 0; i < list->count; i++) {
		free(list->names[i]);
	}
	free(list->names);
	memset(list, 0, sizeof(*list));
}

static int cmpNamePtr(const void *a, const void *b)
{
	const char * const *sa = (const char * const *)a;
	const char * const *sb = (const char * const *)b;
	return strcmp(*sa, *sb);
}

static void hashU64LE(sha256_ctx *ctx, u64 value)
{
	u8 bytes[8];
	for (s32 i = 0; i < 8; i++) {
		bytes[i] = (u8)((value >> (i * 8)) & 0xffu);
	}
	sha256Update(ctx, bytes, sizeof(bytes));
}

static void hashCanonicalEntry(sha256_ctx *ctx, const char *name,
                               const void *bytes, u32 size)
{
	static const u8 zero = 0;
	sha256Update(ctx, name, strlen(name));
	sha256Update(ctx, &zero, 1);
	hashU64LE(ctx, (u64)size);
	if (size > 0 && bytes) {
		sha256Update(ctx, bytes, size);
	}
}

static s32 memEntryCollectCb(const char *entryName, u32 uncompressedSize, void *user)
{
	(void)uncompressedSize;
	return nameListAdd((name_list_t *)user, entryName);
}

s32 weaponGraphArchiveReadTextFile(const char *archive_path, const char *entry_name,
                                   char **out_text, u32 *out_size)
{
	if (out_text) *out_text = NULL;
	if (out_size) *out_size = 0;
	if (!archive_path || !entry_name || !out_text) return -1;

	mod_archive_t *arc = modArchiveOpen(archive_path);
	if (!arc) return -1;
	s32 idx = modArchiveFindEntry(arc, entry_name);
	if (idx < 0) {
		modArchiveClose(arc);
		return -1;
	}

	u32 size = 0;
	void *raw = modArchiveExtractAlloc(arc, idx, &size);
	modArchiveClose(arc);
	if (!raw) return -1;

	char *text = (char *)realloc(raw, (size_t)size + 1);
	if (!text) {
		free(raw);
		return -1;
	}
	text[size] = '\0';
	*out_text = text;
	if (out_size) *out_size = size;
	return 0;
}

static s32 readDescriptorFromText(const char *label, const char *text, u32 size,
                                  asset_type_e expected_type,
                                  weapon_graph_archive_descriptor_t *out,
                                  char *err, size_t err_cap)
{
	if (!text || !out) {
		setErr(err, err_cap, "descriptor read called with null input");
		return -1;
	}

	const char *expected_section = sectionForType(expected_type);
	if (!expected_section) {
		setErr(err, err_cap, "unsupported weapon graph archive type %d", (s32)expected_type);
		return -1;
	}

	memset(out, 0, sizeof(*out));
	out->type = expected_type;
	copyStr(out->descriptor_entry, sizeof(out->descriptor_entry),
		weaponGraphArchiveDescriptorForType(expected_type));

	char *buf = (char *)malloc((size_t)size + 1);
	if (!buf) {
		setErr(err, err_cap, "out of memory parsing %s", label ? label : "descriptor");
		return -1;
	}
	memcpy(buf, text, size);
	buf[size] = '\0';

	s32 saw_section = 0;
	for (char *line = buf; line; ) {
		char *next = strchr(line, '\n');
		if (next) {
			*next = '\0';
			next++;
		}
		char *p = trimLocal(line);
		if (*p == '\0' || *p == ';' || *p == '#') {
			line = next;
			continue;
		}
		if (*p == '[') {
			char *end = strchr(p, ']');
			if (!end) {
				free(buf);
				setErr(err, err_cap, "malformed section in %s", label ? label : "descriptor");
				return -1;
			}
			*end = '\0';
			if (!saw_section) {
				copyStr(out->section, sizeof(out->section), trimLocal(p + 1));
				saw_section = 1;
			}
			line = next;
			continue;
		}

		char *eq = strchr(p, '=');
		if (!eq) {
			line = next;
			continue;
		}
		*eq = '\0';
		char *key = trimLocal(p);
		char *value = trimLocal(eq + 1);

		if (strcmp(key, "catalog_id") == 0) {
			copyStr(out->catalog_id, sizeof(out->catalog_id), value);
		} else if (strcmp(key, "id") == 0 && out->catalog_id[0] == '\0') {
			copyStr(out->catalog_id, sizeof(out->catalog_id), value);
		} else if (strcmp(key, "behavior_graph") == 0) {
			copyStr(out->behavior_graph, sizeof(out->behavior_graph), value);
		} else if (strcmp(key, "graph") == 0 && out->behavior_graph[0] == '\0') {
			copyStr(out->behavior_graph, sizeof(out->behavior_graph), value);
		} else if (strcmp(key, "primary_graph") == 0) {
			copyStr(out->primary_graph, sizeof(out->primary_graph), value);
		} else if (strcmp(key, "secondary_graph") == 0) {
			copyStr(out->secondary_graph, sizeof(out->secondary_graph), value);
		} else if (strcmp(key, "shared_context") == 0) {
			copyStr(out->shared_context, sizeof(out->shared_context), value);
		} else if (strcmp(key, "settings") == 0) {
			copyStr(out->settings, sizeof(out->settings), value);
		} else if (strcmp(key, "variables") == 0) {
			copyStr(out->variables, sizeof(out->variables), value);
		} else if (strcmp(key, "manifest") == 0) {
			copyStr(out->manifest, sizeof(out->manifest), value);
		} else if (strcmp(key, "nested_payloads") == 0) {
			copyStr(out->nested_payloads, sizeof(out->nested_payloads), value);
		} else if (strcmp(key, "model_file") == 0) {
			copyStr(out->model_file, sizeof(out->model_file), value);
		} else if (strcmp(key, "model") == 0 && out->model_file[0] == '\0') {
			copyStr(out->model_file, sizeof(out->model_file), value);
		} else if (strcmp(key, "entity_ref") == 0) {
			copyStr(out->entity_ref, sizeof(out->entity_ref), value);
		} else if (strcmp(key, "transition_entity") == 0 && out->entity_ref[0] == '\0') {
			copyStr(out->entity_ref, sizeof(out->entity_ref), value);
		} else if (strcmp(key, "archetype") == 0) {
			copyStr(out->archetype, sizeof(out->archetype), value);
		}

		line = next;
	}

	free(buf);
	if (!saw_section || strcmp(out->section, expected_section) != 0) {
		setErr(err, err_cap, "descriptor section [%s] does not match %s",
			out->section[0] ? out->section : "(missing)", expected_section);
		return -1;
	}
	return 0;
}

s32 weaponGraphArchiveReadDescriptorFile(const char *archive_path,
                                         asset_type_e expected_type,
                                         weapon_graph_archive_descriptor_t *out,
                                         char *err, size_t err_cap)
{
	if (!archive_path || !out) {
		setErr(err, err_cap, "descriptor read called with null input");
		return -1;
	}
	const char *descriptor = weaponGraphArchiveDescriptorForType(expected_type);
	if (!descriptor) {
		setErr(err, err_cap, "unsupported weapon graph archive type %d", (s32)expected_type);
		return -1;
	}

	char *text = NULL;
	u32 size = 0;
	if (weaponGraphArchiveReadTextFile(archive_path, descriptor, &text, &size) != 0) {
		setErr(err, err_cap, "%s missing %s", archive_path, descriptor);
		return -1;
	}
	s32 ok = readDescriptorFromText(descriptor, text, size, expected_type,
		out, err, err_cap);
	free(text);
	return ok;
}

s32 weaponGraphArchiveReadDescriptorBytes(const void *archive_bytes,
                                          u32 archive_size,
                                          asset_type_e expected_type,
                                          weapon_graph_archive_descriptor_t *out,
                                          char *err, size_t err_cap)
{
	if (!archive_bytes || archive_size == 0 || !out) {
		setErr(err, err_cap, "descriptor bytes read called with null input");
		return -1;
	}
	const char *descriptor = weaponGraphArchiveDescriptorForType(expected_type);
	if (!descriptor) {
		setErr(err, err_cap, "unsupported weapon graph archive type %d", (s32)expected_type);
		return -1;
	}

	u32 size = 0;
	char *text = (char *)modArchiveExtractMemAlloc(archive_bytes, archive_size,
		descriptor, &size);
	if (!text) {
		setErr(err, err_cap, "nested archive missing %s", descriptor);
		return -1;
	}
	s32 ok = readDescriptorFromText(descriptor, text, size, expected_type,
		out, err, err_cap);
	free(text);
	return ok;
}

s32 weaponGraphArchiveValidateRootFile(const char *archive_path,
                                       asset_type_e expected_type,
                                       char *err, size_t err_cap)
{
	weapon_graph_archive_descriptor_t desc;
	if (weaponGraphArchiveReadDescriptorFile(archive_path, expected_type,
			&desc, err, err_cap) != 0) {
		return -1;
	}

	mod_archive_t *arc = modArchiveOpen(archive_path);
	if (!arc) {
		setErr(err, err_cap, "could not open archive %s", archive_path ? archive_path : "(null)");
		return -1;
	}

	if (weaponGraphArchiveGraphRequired(expected_type) && desc.behavior_graph[0] == '\0') {
		if (expected_type != ASSET_WEAPON ||
				desc.primary_graph[0] == '\0' ||
				desc.secondary_graph[0] == '\0') {
			setErr(err, err_cap, "%s requires behavior_graph or primary_graph/secondary_graph",
				archive_path);
			modArchiveClose(arc);
			return -1;
		}
	}
	if (desc.behavior_graph[0] &&
			modArchiveFindEntry(arc, desc.behavior_graph) < 0) {
		setErr(err, err_cap, "%s missing graph entry %s",
			archive_path, desc.behavior_graph);
		modArchiveClose(arc);
		return -1;
	}
	if (expected_type == ASSET_WEAPON && desc.primary_graph[0] &&
			modArchiveFindEntry(arc, desc.primary_graph) < 0) {
		setErr(err, err_cap, "%s missing primary graph entry %s",
			archive_path, desc.primary_graph);
		modArchiveClose(arc);
		return -1;
	}
	if (expected_type == ASSET_WEAPON && desc.secondary_graph[0] &&
			modArchiveFindEntry(arc, desc.secondary_graph) < 0) {
		setErr(err, err_cap, "%s missing secondary graph entry %s",
			archive_path, desc.secondary_graph);
		modArchiveClose(arc);
		return -1;
	}
	if (expected_type == ASSET_WEAPON && desc.nested_payloads[0] &&
			modArchiveFindEntry(arc, desc.nested_payloads) < 0) {
		setErr(err, err_cap, "%s missing nested payload inventory %s",
			archive_path, desc.nested_payloads);
		modArchiveClose(arc);
		return -1;
	}

	modArchiveClose(arc);
	return 0;
}

static s32 sanitizeSlug(const char *in, char *out, size_t cap)
{
	if (!in || !out || cap == 0) return -1;
	size_t j = 0;
	s32 last_us = 0;
	for (size_t i = 0; in[i] && j + 1 < cap; i++) {
		unsigned char c = (unsigned char)in[i];
		if (isalnum(c)) {
			out[j++] = (char)tolower(c);
			last_us = 0;
		} else if (c == '_' || c == '-' || c == '.' || c == ' ') {
			if (j > 0 && !last_us) {
				out[j++] = '_';
				last_us = 1;
			}
		}
	}
	while (j > 0 && out[j - 1] == '_') j--;
	out[j] = '\0';
	return (j > 0) ? 0 : -1;
}

s32 weaponGraphArchiveDerivedNestedId(const char *parent_id, asset_type_e type,
                                      const char *local_slug,
                                      char *out, size_t out_cap)
{
	if (!parent_id || !local_slug || !out || out_cap == 0) return -1;
	out[0] = '\0';
	if (type != ASSET_PROJECTILE && type != ASSET_ENTITY) return -1;

	const char *colon = strchr(parent_id, ':');
	if (!colon || colon == parent_id || colon[1] == '\0') return -1;

	char namespace_part[CATALOG_ID_LEN];
	size_t ns_len = (size_t)(colon - parent_id);
	if (ns_len == 0 || ns_len >= sizeof(namespace_part)) return -1;
	memcpy(namespace_part, parent_id, ns_len);
	namespace_part[ns_len] = '\0';

	char parent_slug[WEAPON_GRAPH_ARCHIVE_LOCAL_SLUG_LEN];
	char nested_slug[WEAPON_GRAPH_ARCHIVE_LOCAL_SLUG_LEN];
	if (sanitizeSlug(colon + 1, parent_slug, sizeof(parent_slug)) != 0 ||
			sanitizeSlug(local_slug, nested_slug, sizeof(nested_slug)) != 0) {
		return -1;
	}

	const char *kind = (type == ASSET_PROJECTILE) ? "projectile" : "entity";
	int n = snprintf(out, out_cap, "%s:%s__%s_%s",
		namespace_part, parent_slug, kind, nested_slug);
	if (n <= 0 || (size_t)n >= out_cap || n >= CATALOG_ID_LEN) {
		out[0] = '\0';
		return -1;
	}
	return 0;
}

static s32 collectFileArchiveNames(mod_archive_t *arc, name_list_t *names)
{
	s32 count = modArchiveGetEntryCount(arc);
	for (s32 i = 0; i < count; i++) {
		const char *name = modArchiveGetEntryName(arc, i);
		if (!name || !name[0]) continue;
		if (nameListAdd(names, name) != 0) return -1;
	}
	return (names->count > 0) ? 0 : -1;
}

s32 weaponGraphArchiveCanonicalSha256File(const char *archive_path,
                                          char out_hex[SHA256_HEX_SIZE])
{
	if (out_hex) out_hex[0] = '\0';
	if (!archive_path || !out_hex) return -1;

	mod_archive_t *arc = modArchiveOpen(archive_path);
	if (!arc) return -1;

	name_list_t names;
	memset(&names, 0, sizeof(names));
	if (collectFileArchiveNames(arc, &names) != 0) {
		nameListFree(&names);
		modArchiveClose(arc);
		return -1;
	}
	qsort(names.names, (size_t)names.count, sizeof(char *), cmpNamePtr);

	sha256_ctx ctx;
	sha256Init(&ctx);
	static const char domain[] = "pd.weapon_graph.archive_content.v1\n";
	sha256Update(&ctx, domain, sizeof(domain) - 1);

	for (s32 i = 0; i < names.count; i++) {
		s32 idx = modArchiveFindEntry(arc, names.names[i]);
		if (idx < 0) {
			nameListFree(&names);
			modArchiveClose(arc);
			return -1;
		}
		u32 size = 0;
		void *bytes = modArchiveExtractAlloc(arc, idx, &size);
		if (!bytes) {
			nameListFree(&names);
			modArchiveClose(arc);
			return -1;
		}
		hashCanonicalEntry(&ctx, names.names[i], bytes, size);
		free(bytes);
	}

	u8 digest[SHA256_DIGEST_SIZE];
	sha256Final(&ctx, digest);
	sha256ToHex(digest, out_hex);
	nameListFree(&names);
	modArchiveClose(arc);
	return 0;
}

s32 weaponGraphArchiveCanonicalSha256Bytes(const void *archive_bytes,
                                           u32 archive_size,
                                           char out_hex[SHA256_HEX_SIZE])
{
	if (out_hex) out_hex[0] = '\0';
	if (!archive_bytes || archive_size == 0 || !out_hex) return -1;

	name_list_t names;
	memset(&names, 0, sizeof(names));
	if (modArchiveMemForEachEntry(archive_bytes, archive_size,
			memEntryCollectCb, &names) != MODARCHIVE_OK) {
		nameListFree(&names);
		return -1;
	}
	qsort(names.names, (size_t)names.count, sizeof(char *), cmpNamePtr);

	sha256_ctx ctx;
	sha256Init(&ctx);
	static const char domain[] = "pd.weapon_graph.archive_content.v1\n";
	sha256Update(&ctx, domain, sizeof(domain) - 1);

	for (s32 i = 0; i < names.count; i++) {
		u32 size = 0;
		void *bytes = modArchiveExtractMemAlloc(archive_bytes, archive_size,
			names.names[i], &size);
		if (!bytes) {
			nameListFree(&names);
			return -1;
		}
		hashCanonicalEntry(&ctx, names.names[i], bytes, size);
		free(bytes);
	}

	u8 digest[SHA256_DIGEST_SIZE];
	sha256Final(&ctx, digest);
	sha256ToHex(digest, out_hex);
	nameListFree(&names);
	return 0;
}

static void localSlugFromArchiveEntry(const char *entry, asset_type_e type,
                                      char *out, size_t out_cap)
{
	if (!out || out_cap == 0) return;
	out[0] = '\0';
	if (!entry) return;

	const char *leaf = strrchr(entry, '/');
	leaf = leaf ? leaf + 1 : entry;
	const char *ext = weaponGraphArchiveExtensionForType(type);
	size_t len = strlen(leaf);
	size_t ext_len = ext ? strlen(ext) : 0;
	if (ext_len > 0 && len > ext_len &&
			endsWithNoCase(leaf + len - ext_len, ext)) {
		len -= ext_len;
	}
	if (len >= out_cap) len = out_cap - 1;
	char tmp[WEAPON_GRAPH_ARCHIVE_LOCAL_SLUG_LEN];
	memcpy(tmp, leaf, len);
	tmp[len] = '\0';
	if (sanitizeSlug(tmp, out, out_cap) != 0) {
		copyStr(out, out_cap, "payload");
	}
}

static s32 sameNamespace(const char *a, const char *b)
{
	const char *ac = a ? strchr(a, ':') : NULL;
	const char *bc = b ? strchr(b, ':') : NULL;
	if (!ac || !bc || ac == a || bc == b) return 0;
	size_t alen = (size_t)(ac - a);
	size_t blen = (size_t)(bc - b);
	return alen == blen && strncmp(a, b, alen) == 0;
}

static s32 inventoryFindById(const weapon_graph_archive_inventory_t *inv,
                             const char *catalog_id)
{
	if (!inv || !catalog_id) return -1;
	for (s32 i = 0; i < inv->count; i++) {
		if (strcmp(inv->payloads[i].catalog_id, catalog_id) == 0) {
			return i;
		}
	}
	return -1;
}

s32 weaponGraphArchiveScanNestedPayloadsFile(const char *archive_path,
                                             const char *parent_id,
                                             weapon_graph_archive_inventory_t *out,
                                             char *err, size_t err_cap)
{
	if (!archive_path || !parent_id || !out) {
		setErr(err, err_cap, "nested payload scan called with null input");
		return -1;
	}
	memset(out, 0, sizeof(*out));

	mod_archive_t *arc = modArchiveOpen(archive_path);
	if (!arc) {
		setErr(err, err_cap, "could not open archive %s", archive_path);
		return -1;
	}

	s32 count = modArchiveGetEntryCount(arc);
	for (s32 i = 0; i < count; i++) {
		const char *entry = modArchiveGetEntryName(arc, i);
		asset_type_e type = typeForNestedArchiveName(entry);
		if (type == ASSET_NONE) continue;

		u32 nested_size = 0;
		void *nested_bytes = modArchiveExtractAlloc(arc, i, &nested_size);
		if (!nested_bytes || nested_size == 0) {
			free(nested_bytes);
			setErr(err, err_cap, "could not read nested payload %s", entry);
			modArchiveClose(arc);
			return -1;
		}

		char digest[SHA256_HEX_SIZE];
		if (weaponGraphArchiveCanonicalSha256Bytes(nested_bytes, nested_size, digest) != 0) {
			free(nested_bytes);
			setErr(err, err_cap, "nested payload %s is not a valid archive", entry);
			modArchiveClose(arc);
			return -1;
		}

		weapon_graph_archive_descriptor_t desc;
		if (weaponGraphArchiveReadDescriptorBytes(nested_bytes, nested_size,
				type, &desc, err, err_cap) != 0) {
			free(nested_bytes);
			modArchiveClose(arc);
			return -1;
		}
		free(nested_bytes);

		char local_slug[WEAPON_GRAPH_ARCHIVE_LOCAL_SLUG_LEN];
		localSlugFromArchiveEntry(entry, type, local_slug, sizeof(local_slug));

		char catalog_id[CATALOG_ID_LEN];
		if (desc.catalog_id[0]) {
			if (!sameNamespace(parent_id, desc.catalog_id)) {
				setErr(err, err_cap,
					"nested payload %s uses external catalog ID %s without explicit dependency support",
					entry, desc.catalog_id);
				modArchiveClose(arc);
				return -1;
			}
			copyStr(catalog_id, sizeof(catalog_id), desc.catalog_id);
		} else if (weaponGraphArchiveDerivedNestedId(parent_id, type, local_slug,
				catalog_id, sizeof(catalog_id)) != 0) {
			setErr(err, err_cap, "could not derive catalog ID for %s", entry);
			modArchiveClose(arc);
			return -1;
		}

		s32 existing = inventoryFindById(out, catalog_id);
		if (existing >= 0) {
			if (strcmp(out->payloads[existing].canonical_sha256, digest) != 0) {
				setErr(err, err_cap,
					"nested payload ID collision for %s with different content",
					catalog_id);
				modArchiveClose(arc);
				return -1;
			}
			continue;
		}
		if (out->count >= WEAPON_GRAPH_ARCHIVE_MAX_NESTED_PAYLOADS) {
			setErr(err, err_cap, "too many nested payloads in %s", archive_path);
			modArchiveClose(arc);
			return -1;
		}

		weapon_graph_archive_payload_t *p = &out->payloads[out->count++];
		memset(p, 0, sizeof(*p));
		p->type = type;
		copyStr(p->archive_entry, sizeof(p->archive_entry), entry);
		copyStr(p->local_slug, sizeof(p->local_slug), local_slug);
		copyStr(p->catalog_id, sizeof(p->catalog_id), catalog_id);
		copyStr(p->canonical_sha256, sizeof(p->canonical_sha256), digest);
	}

	modArchiveClose(arc);
	return out->count;
}

static s32 appendFmt(char **cursor, size_t *left, const char *fmt, ...)
{
	if (!cursor || !*cursor || !left || *left == 0) return -1;
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(*cursor, *left, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= *left) return -1;
	*cursor += n;
	*left -= (size_t)n;
	return 0;
}

static s32 appendJsonString(char **cursor, size_t *left, const char *value)
{
	if (appendFmt(cursor, left, "\"") != 0) return -1;
	if (!value) value = "";
	for (const char *p = value; *p; p++) {
		unsigned char c = (unsigned char)*p;
		if (c == '"' || c == '\\') {
			if (appendFmt(cursor, left, "\\%c", c) != 0) return -1;
		} else if (c == '\n') {
			if (appendFmt(cursor, left, "\\n") != 0) return -1;
		} else if (c == '\r') {
			if (appendFmt(cursor, left, "\\r") != 0) return -1;
		} else if (c == '\t') {
			if (appendFmt(cursor, left, "\\t") != 0) return -1;
		} else if (c < 0x20) {
			if (appendFmt(cursor, left, "\\u%04x", c) != 0) return -1;
		} else {
			if (appendFmt(cursor, left, "%c", c) != 0) return -1;
		}
	}
	return appendFmt(cursor, left, "\"");
}

s32 weaponGraphArchiveFormatNestedPayloadsJson(const char *asset_id,
                                               const weapon_graph_archive_inventory_t *inventory,
                                               char *out, size_t out_cap)
{
	if (!asset_id || !out || out_cap == 0) return -1;
	char *cursor = out;
	size_t left = out_cap;
	if (appendFmt(&cursor, &left, "{\n  \"schema\": \"pd.weapon_nested_payloads.v1\",\n") != 0) return -1;
	if (appendFmt(&cursor, &left, "  \"asset_id\": ") != 0) return -1;
	if (appendJsonString(&cursor, &left, asset_id) != 0) return -1;
	if (appendFmt(&cursor, &left, ",\n  \"payloads\": [") != 0) return -1;

	const s32 count = inventory ? inventory->count : 0;
	for (s32 i = 0; i < count; i++) {
		const weapon_graph_archive_payload_t *p = &inventory->payloads[i];
		if (appendFmt(&cursor, &left, "%s\n    {\n      \"type\": ",
				i == 0 ? "" : ",") != 0) return -1;
		if (appendJsonString(&cursor, &left, weaponGraphArchiveTypeName(p->type)) != 0) return -1;
		if (appendFmt(&cursor, &left, ",\n      \"local_slug\": ") != 0) return -1;
		if (appendJsonString(&cursor, &left, p->local_slug) != 0) return -1;
		if (appendFmt(&cursor, &left, ",\n      \"catalog_id\": ") != 0) return -1;
		if (appendJsonString(&cursor, &left, p->catalog_id) != 0) return -1;
		if (appendFmt(&cursor, &left, ",\n      \"archive\": ") != 0) return -1;
		if (appendJsonString(&cursor, &left, p->archive_entry) != 0) return -1;
		if (appendFmt(&cursor, &left, ",\n      \"canonical_sha256\": ") != 0) return -1;
		if (appendJsonString(&cursor, &left, p->canonical_sha256) != 0) return -1;
		if (appendFmt(&cursor, &left, "\n    }") != 0) return -1;
	}

	if (count > 0) {
		if (appendFmt(&cursor, &left, "\n  ]\n}\n") != 0) return -1;
	} else {
		if (appendFmt(&cursor, &left, "]\n}\n") != 0) return -1;
	}
	return 0;
}
