/**
 * asset_archive_writer.c -- shared typed asset archive emission helpers.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset_archive_policy.h"
#include "asset_archive_writer.h"

typedef struct text_builder {
	char *data;
	size_t len;
	size_t cap;
} text_builder_t;

static void copyStr(char *dst, size_t dst_cap, const char *src)
{
	if (!dst || dst_cap == 0) return;
	if (!src) src = "";
	size_t len = strlen(src);
	if (len >= dst_cap) len = dst_cap - 1;
	memcpy(dst, src, len);
	dst[len] = '\0';
}

static s32 builderReserve(text_builder_t *b, size_t extra)
{
	if (!b) return -1;
	size_t need = b->len + extra + 1;
	if (need <= b->cap) return 0;
	size_t next = b->cap ? b->cap : 512;
	while (next < need) {
		next *= 2;
	}
	char *mem = (char *)realloc(b->data, next);
	if (!mem) return -1;
	b->data = mem;
	b->cap = next;
	return 0;
}

static s32 builderAppendBytes(text_builder_t *b, const char *data, size_t len)
{
	if (!b || !data) return -1;
	if (builderReserve(b, len) != 0) return -1;
	memcpy(b->data + b->len, data, len);
	b->len += len;
	b->data[b->len] = '\0';
	return 0;
}

static s32 builderAppend(text_builder_t *b, const char *text)
{
	return builderAppendBytes(b, text ? text : "", text ? strlen(text) : 0);
}

static s32 builderAppendFmt(text_builder_t *b, const char *fmt, ...)
{
	if (!b || !fmt) return -1;
	va_list ap;
	va_start(ap, fmt);
	va_list ap2;
	va_copy(ap2, ap);
	int need = vsnprintf(NULL, 0, fmt, ap2);
	va_end(ap2);
	if (need < 0) {
		va_end(ap);
		return -1;
	}
	if (builderReserve(b, (size_t)need) != 0) {
		va_end(ap);
		return -1;
	}
	vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap);
	va_end(ap);
	b->len += (size_t)need;
	return 0;
}

static s32 builderAppendJsonString(text_builder_t *b, const char *value)
{
	if (!b) return -1;
	if (builderAppend(b, "\"") != 0) return -1;
	if (!value) value = "";
	for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
		if (*p == '"' || *p == '\\') {
			char out[3];
			out[0] = '\\';
			out[1] = (char)*p;
			out[2] = '\0';
			if (builderAppend(b, out) != 0) return -1;
		} else if (*p == '\n') {
			if (builderAppend(b, "\\n") != 0) return -1;
		} else if (*p == '\r') {
			if (builderAppend(b, "\\r") != 0) return -1;
		} else if (*p == '\t') {
			if (builderAppend(b, "\\t") != 0) return -1;
		} else if (*p < 0x20) {
			if (builderAppendFmt(b, "\\u%04x", (unsigned)*p) != 0) return -1;
		} else {
			char out[2];
			out[0] = (char)*p;
			out[1] = '\0';
			if (builderAppend(b, out) != 0) return -1;
		}
	}
	return builderAppend(b, "\"");
}

static void builderFree(text_builder_t *b)
{
	if (!b) return;
	free(b->data);
	b->data = NULL;
	b->len = 0;
	b->cap = 0;
}

static void hashMemHex(const void *data, u32 len, char out[SHA256_HEX_SIZE])
{
	u8 digest[SHA256_DIGEST_SIZE];
	sha256Hash(data, (size_t)len, digest);
	sha256ToHex(digest, out);
}

static s32 trackEntry(asset_archive_writer_t *writer,
                      const char *entry_name,
                      const char *role,
                      u32 len,
                      const char *sha_hex)
{
	if (!writer || !entry_name || !entry_name[0]) return MODARCHIVE_ERR_FORMAT;
	if (writer->entry_count >= ASSET_ARCHIVE_WRITER_MAX_ENTRIES) {
		return MODARCHIVE_ERR_LIMIT;
	}
	asset_archive_writer_entry_t *entry = &writer->entries[writer->entry_count++];
	copyStr(entry->path, sizeof(entry->path), entry_name);
	copyStr(entry->role, sizeof(entry->role), role && role[0] ? role : "source");
	entry->size = len;
	copyStr(entry->sha256, sizeof(entry->sha256), sha_hex ? sha_hex : "");
	return MODARCHIVE_OK;
}

static s32 sidecarPathFor(const char *entry_name, char *out, size_t out_cap)
{
	if (!entry_name || !entry_name[0] || !out || out_cap == 0) {
		return MODARCHIVE_ERR_FORMAT;
	}
	int n = snprintf(out, out_cap, ASSET_ARCHIVE_META_DIR "/%s.sha256",
		entry_name);
	if (n <= 0 || (size_t)n >= out_cap) {
		return MODARCHIVE_ERR_LIMIT;
	}
	return MODARCHIVE_OK;
}

static s32 addTrackedMem(asset_archive_writer_t *writer,
                         const char *entry_name,
                         const void *data,
                         u32 len,
                         const char *role,
                         s32 write_sidecar)
{
	if (!writer || !writer->archive || !entry_name || !entry_name[0]) {
		return MODARCHIVE_ERR_FORMAT;
	}
	if (!data && len != 0) {
		return MODARCHIVE_ERR_FORMAT;
	}

	char sha_hex[SHA256_HEX_SIZE];
	hashMemHex(data, len, sha_hex);

	char sidecar_name[ASSET_ARCHIVE_WRITER_PATH_MAX];
	if (write_sidecar) {
		s32 r = sidecarPathFor(entry_name, sidecar_name,
			sizeof(sidecar_name));
		if (r != MODARCHIVE_OK) return r;
	}

	s32 r = modArchiveAddFileMem(writer->archive, entry_name, data, len);
	if (r != MODARCHIVE_OK) return r;

	r = trackEntry(writer, entry_name, role, len, sha_hex);
	if (r != MODARCHIVE_OK) return r;

	if (write_sidecar) {
		char sidecar[SHA256_HEX_SIZE + 1];
		snprintf(sidecar, sizeof(sidecar), "%s\n", sha_hex);
		r = modArchiveAddFileMem(writer->archive, sidecar_name,
			sidecar, (u32)strlen(sidecar));
		if (r != MODARCHIVE_OK) return r;
	}

	return MODARCHIVE_OK;
}

static void markMeta(asset_archive_writer_t *writer, const char *leaf_name)
{
	if (!writer || !leaf_name) return;
	if (strcmp(leaf_name, ASSET_ARCHIVE_META_MANIFEST) == 0 ||
			strcmp(leaf_name, ASSET_ARCHIVE_META_MANIFEST_PATH) == 0) {
		writer->has_manifest = 1;
	} else if (strcmp(leaf_name, "inventory.json") == 0 ||
			strcmp(leaf_name, "_meta/inventory.json") == 0) {
		writer->has_inventory = 1;
	} else if (strcmp(leaf_name, "hashes.tsv") == 0 ||
			strcmp(leaf_name, "_meta/hashes.tsv") == 0) {
		writer->has_hashes = 1;
	} else if (strcmp(leaf_name, "provenance.json") == 0 ||
			strcmp(leaf_name, "_meta/provenance.json") == 0) {
		writer->has_provenance = 1;
	} else if (strcmp(leaf_name, "validation.json") == 0 ||
			strcmp(leaf_name, "_meta/validation.json") == 0) {
		writer->has_validation = 1;
	} else if (strcmp(leaf_name, "source-handles.json") == 0 ||
			strcmp(leaf_name, "_meta/source-handles.json") == 0) {
		writer->has_source_handles = 1;
	}
}

static s32 addMetaText(asset_archive_writer_t *writer,
                       const char *leaf_name,
                       const void *data,
                       u32 len)
{
	if (!writer || !writer->archive || !leaf_name || !leaf_name[0]) {
		return MODARCHIVE_ERR_FORMAT;
	}
	if (!data && len != 0) {
		return MODARCHIVE_ERR_FORMAT;
	}
	char path[ASSET_ARCHIVE_WRITER_PATH_MAX];
	assetArchiveMetaPath(leaf_name, path, sizeof(path));
	if (!path[0]) return MODARCHIVE_ERR_FORMAT;
	s32 r = modArchiveAddFileMem(writer->archive, path, data, len);
	if (r == MODARCHIVE_OK) {
		markMeta(writer, leaf_name);
	}
	return r;
}

static s32 entryIsDependencyArchive(const asset_archive_writer_entry_t *entry)
{
	if (!entry || !entry->path[0]) return 0;
	const char *root = ASSET_ARCHIVE_WRITER_DEPENDENCY_ROOT;
	size_t root_len = strlen(root);
	return strncmp(entry->path, root, root_len) == 0 &&
		(entry->path[root_len] == '/' || entry->path[root_len] == '\\');
}

static u32 dependencyArchiveCount(const asset_archive_writer_t *writer)
{
	if (!writer) return 0;
	u32 count = 0;
	for (u32 i = 0; i < writer->entry_count; i++) {
		if (entryIsDependencyArchive(&writer->entries[i])) {
			count++;
		}
	}
	return count;
}

static const char *dependencyTypeName(asset_type_e type)
{
	switch (type) {
	case ASSET_MAP:         return "map";
	case ASSET_CHARACTER:   return "character";
	case ASSET_SKIN:        return "skin";
	case ASSET_BOT_VARIANT: return "bot-variant";
	case ASSET_WEAPON:      return "weapon";
	case ASSET_TEXTURES:    return "texture-pack";
	case ASSET_SFX:         return "sfx";
	case ASSET_MUSIC:       return "music";
	case ASSET_PROP:        return "prop";
	case ASSET_VEHICLE:     return "vehicle";
	case ASSET_MISSION:     return "mission";
	case ASSET_UI:          return "ui";
	case ASSET_TOOL:        return "tool";
	case ASSET_ARENA:       return "arena";
	case ASSET_BODY:        return "body";
	case ASSET_HEAD:        return "head";
	case ASSET_ANIMATION:   return "animation";
	case ASSET_TEXTURE:     return "texture";
	case ASSET_GAMEMODE:    return "gamemode";
	case ASSET_AUDIO:       return "audio";
	case ASSET_HUD:         return "hud";
	case ASSET_EFFECT:      return "effect";
	case ASSET_MODEL:       return "model";
	case ASSET_LANG:        return "lang";
	case ASSET_BOT_PROFILE: return "botprofile";
	case ASSET_PROJECTILE:  return "projectile";
	case ASSET_ENTITY:      return "entity";
	case ASSET_MATERIAL:    return "material";
	case ASSET_FONT:        return "font";
	case ASSET_SCENARIO:    return "scenario";
	case ASSET_THEME:       return "theme";
	default:                return "asset";
	}
}

static void dependencyIdFromPath(const char *path, char *out, size_t out_cap)
{
	if (!out || out_cap == 0) return;
	out[0] = '\0';
	if (!path || !path[0]) return;

	const char *leaf = strrchr(path, '/');
	const char *leaf2 = strrchr(path, '\\');
	if (leaf2 && (!leaf || leaf2 > leaf)) leaf = leaf2;
	leaf = leaf ? leaf + 1 : path;

	char name[ASSET_ARCHIVE_WRITER_PATH_MAX];
	copyStr(name, sizeof(name), leaf);
	char *dot = strrchr(name, '.');
	if (dot) *dot = '\0';

	if (strncmp(name, "base_", 5) == 0) {
		snprintf(out, out_cap, "base:%s", name + 5);
	} else if (strncmp(name, "mod_", 4) == 0) {
		snprintf(out, out_cap, "mod:%s", name + 4);
	} else {
		copyStr(out, out_cap, name);
	}
}

static s32 writeDependencyArray(text_builder_t *b,
                                const asset_archive_writer_t *writer)
{
	u32 count = dependencyArchiveCount(writer);
	if (count == 0) {
		return builderAppend(b, "  \"dependencies\": [],\n");
	}

	if (builderAppend(b, "  \"dependencies\": [\n") != 0) {
		return -1;
	}

	u32 written = 0;
	for (u32 i = 0; i < writer->entry_count; i++) {
		const asset_archive_writer_entry_t *entry = &writer->entries[i];
		if (!entryIsDependencyArchive(entry)) continue;

		asset_type_e type = assetArchiveTypeForPath(entry->path);
		char id[ASSET_ARCHIVE_WRITER_PATH_MAX];
		dependencyIdFromPath(entry->path, id, sizeof(id));

		if (builderAppend(b, written ? ",\n    {\n" : "    {\n") != 0 ||
				builderAppend(b, "      \"role\": ") != 0 ||
				builderAppendJsonString(b, entry->role) != 0 ||
				builderAppend(b, ",\n      \"type\": ") != 0 ||
				builderAppendJsonString(b, dependencyTypeName(type)) != 0 ||
				builderAppend(b, ",\n      \"id\": ") != 0 ||
				builderAppendJsonString(b, id) != 0 ||
				builderAppend(b, ",\n      \"archive\": ") != 0 ||
				builderAppendJsonString(b, entry->path) != 0 ||
				builderAppend(b, ",\n      \"required\": true,\n") != 0 ||
				builderAppend(b, "      \"version\": \"\",\n") != 0 ||
				builderAppend(b, "      \"sha256\": ") != 0 ||
				builderAppendJsonString(b, entry->sha256) != 0 ||
				builderAppend(b, ",\n      \"fallback\": {\n") != 0 ||
				builderAppend(b, "        \"id\": \"\",\n") != 0 ||
				builderAppend(b, "        \"reason\": \"\"\n") != 0 ||
				builderAppend(b, "      }\n    }") != 0) {
			return -1;
		}
		written++;
	}

	return builderAppend(b, "\n  ],\n");
}

static s32 writeDefaultManifest(asset_archive_writer_t *writer)
{
	text_builder_t b;
	memset(&b, 0, sizeof(b));
	s32 ok =
		builderAppend(&b, "{\n") == 0 &&
		builderAppend(&b, "  \"pd_kind\": ") == 0 &&
		builderAppendJsonString(&b, writer->family) == 0 &&
		builderAppend(&b, ",\n  \"pd_schema_version\": 1,\n") == 0 &&
		builderAppend(&b, "  \"id\": ") == 0 &&
		builderAppendJsonString(&b, writer->catalog_id) == 0 &&
		builderAppend(&b, ",\n") == 0 &&
		writeDependencyArray(&b, writer) == 0 &&
		builderAppend(&b, "  \"dependency_schema\": {\n") == 0 &&
		builderAppend(&b, "    \"root\": \"") == 0 &&
		builderAppend(&b, ASSET_ARCHIVE_WRITER_DEPENDENCY_ROOT) == 0 &&
		builderAppend(&b, "\",\n") == 0 &&
		builderAppend(&b, "    \"required_fields\": [\"role\", \"type\", \"id\", \"archive\", \"required\", \"version\", \"sha256\", \"fallback.id\", \"fallback.reason\"]\n") == 0 &&
		builderAppend(&b, "  }\n") == 0 &&
		builderAppend(&b, "}\n") == 0;
	if (!ok) {
		builderFree(&b);
		return MODARCHIVE_ERR_MEM;
	}
	s32 r = addMetaText(writer, ASSET_ARCHIVE_META_MANIFEST, b.data,
		(u32)b.len);
	builderFree(&b);
	return r;
}

static s32 writeInventory(asset_archive_writer_t *writer)
{
	text_builder_t b;
	memset(&b, 0, sizeof(b));
	if (builderAppend(&b, "{\n  \"schema\": \"pd2.asset.inventory.v1\",\n") != 0 ||
			builderAppend(&b, "  \"id\": ") != 0 ||
			builderAppendJsonString(&b, writer->catalog_id) != 0 ||
			builderAppend(&b, ",\n  \"family\": ") != 0 ||
			builderAppendJsonString(&b, writer->family) != 0 ||
			builderAppend(&b, ",\n  \"dependency_archive_root\": \"") != 0 ||
			builderAppend(&b, ASSET_ARCHIVE_WRITER_DEPENDENCY_ROOT) != 0 ||
			builderAppend(&b, "\",\n  \"entries\": [\n") != 0) {
		builderFree(&b);
		return MODARCHIVE_ERR_MEM;
	}
	for (u32 i = 0; i < writer->entry_count; i++) {
		const asset_archive_writer_entry_t *entry = &writer->entries[i];
		if (builderAppend(&b, i ? ",\n    {\n" : "    {\n") != 0 ||
				builderAppend(&b, "      \"path\": ") != 0 ||
				builderAppendJsonString(&b, entry->path) != 0 ||
				builderAppend(&b, ",\n      \"role\": ") != 0 ||
				builderAppendJsonString(&b, entry->role) != 0 ||
				builderAppendFmt(&b, ",\n      \"size\": %u,\n",
					(unsigned)entry->size) != 0 ||
				builderAppend(&b, "      \"sha256\": ") != 0 ||
				builderAppendJsonString(&b, entry->sha256) != 0 ||
				builderAppend(&b, "\n    }") != 0) {
			builderFree(&b);
			return MODARCHIVE_ERR_MEM;
		}
	}
	if (builderAppend(&b, "\n  ]\n}\n") != 0) {
		builderFree(&b);
		return MODARCHIVE_ERR_MEM;
	}
	s32 r = addMetaText(writer, "inventory.json", b.data, (u32)b.len);
	builderFree(&b);
	return r;
}

static s32 writeHashes(asset_archive_writer_t *writer)
{
	text_builder_t b;
	memset(&b, 0, sizeof(b));
	if (builderAppend(&b, "path\tsha256\tsize\trole\n") != 0) {
		builderFree(&b);
		return MODARCHIVE_ERR_MEM;
	}
	for (u32 i = 0; i < writer->entry_count; i++) {
		const asset_archive_writer_entry_t *entry = &writer->entries[i];
		if (builderAppendFmt(&b, "%s\t%s\t%u\t%s\n",
				entry->path, entry->sha256,
				(unsigned)entry->size, entry->role) != 0) {
			builderFree(&b);
			return MODARCHIVE_ERR_MEM;
		}
	}
	s32 r = addMetaText(writer, "hashes.tsv", b.data, (u32)b.len);
	builderFree(&b);
	return r;
}

static s32 writeProvenance(asset_archive_writer_t *writer)
{
	text_builder_t b;
	memset(&b, 0, sizeof(b));
	if (builderAppend(&b, "{\n  \"schema\": \"pd2.asset.provenance.v1\",\n") != 0 ||
			builderAppend(&b, "  \"tool\": ") != 0 ||
			builderAppendJsonString(&b, writer->tool) != 0 ||
			builderAppend(&b, ",\n  \"family\": ") != 0 ||
			builderAppendJsonString(&b, writer->family) != 0 ||
			builderAppend(&b, ",\n  \"id\": ") != 0 ||
			builderAppendJsonString(&b, writer->catalog_id) != 0 ||
			builderAppend(&b, ",\n  \"source_path\": ") != 0 ||
			builderAppendJsonString(&b, writer->source_path) != 0 ||
			builderAppendFmt(&b, ",\n  \"source_index\": %d,\n",
				(int)writer->source_index) != 0 ||
			builderAppend(&b, "  \"source_symbol\": ") != 0 ||
			builderAppendJsonString(&b, writer->source_symbol) != 0 ||
			builderAppend(&b, "\n}\n") != 0) {
		builderFree(&b);
		return MODARCHIVE_ERR_MEM;
	}
	s32 r = addMetaText(writer, "provenance.json", b.data, (u32)b.len);
	builderFree(&b);
	return r;
}

static s32 writeValidation(asset_archive_writer_t *writer)
{
	text_builder_t b;
	memset(&b, 0, sizeof(b));
	if (builderAppend(&b, "{\n") != 0 ||
			builderAppend(&b, "  \"schema\": \"pd2.asset.validation.v1\",\n") != 0 ||
			builderAppend(&b, "  \"status\": \"writer_checked\",\n") != 0 ||
			builderAppend(&b, "  \"release_contract\": true,\n") != 0 ||
			builderAppend(&b, "  \"descriptor\": ") != 0 ||
			builderAppendJsonString(&b, writer->descriptor) != 0 ||
			builderAppend(&b, ",\n  \"manifest\": \"") != 0 ||
			builderAppend(&b, ASSET_ARCHIVE_META_MANIFEST_PATH) != 0 ||
			builderAppendFmt(&b, "\",\n  \"public_entry_count\": %u,\n",
				(unsigned)writer->entry_count) != 0 ||
			builderAppendFmt(&b, "  \"dependency_archive_count\": %u,\n",
				(unsigned)dependencyArchiveCount(writer)) != 0 ||
			builderAppend(&b, "  \"dependency_schema_fields\": [\"role\", \"type\", \"id\", \"archive\", \"required\", \"version\", \"sha256\", \"fallback.id\", \"fallback.reason\"]\n") != 0 ||
			builderAppend(&b, "}\n") != 0) {
		builderFree(&b);
		return MODARCHIVE_ERR_MEM;
	}
	s32 r = addMetaText(writer, "validation.json", b.data, (u32)b.len);
	builderFree(&b);
	return r;
}

static s32 writeSourceHandles(asset_archive_writer_t *writer)
{
	text_builder_t b;
	memset(&b, 0, sizeof(b));
	if (builderAppend(&b, "{\n  \"schema\": \"pd2.asset.source-handles.v1\",\n") != 0 ||
			builderAppend(&b, "  \"handles\": [") != 0) {
		builderFree(&b);
		return MODARCHIVE_ERR_MEM;
	}
	if (writer->source_path[0] || writer->source_symbol[0] ||
			writer->source_index >= 0) {
		if (builderAppend(&b, "\n    {\n      \"path\": ") != 0 ||
				builderAppendJsonString(&b, writer->source_path) != 0 ||
				builderAppendFmt(&b, ",\n      \"index\": %d,\n",
					(int)writer->source_index) != 0 ||
				builderAppend(&b, "      \"symbol\": ") != 0 ||
				builderAppendJsonString(&b, writer->source_symbol) != 0 ||
				builderAppend(&b, "\n    }\n  ") != 0) {
			builderFree(&b);
			return MODARCHIVE_ERR_MEM;
		}
	}
	if (builderAppend(&b, "]\n}\n") != 0) {
		builderFree(&b);
		return MODARCHIVE_ERR_MEM;
	}
	s32 r = addMetaText(writer, "source-handles.json", b.data, (u32)b.len);
	builderFree(&b);
	return r;
}

s32 assetArchiveWriterInit(asset_archive_writer_t *writer,
                           mod_archive_writer_t *archive,
                           const char *family,
                           const char *catalog_id)
{
	if (!writer || !archive) return MODARCHIVE_ERR_FORMAT;
	memset(writer, 0, sizeof(*writer));
	writer->archive = archive;
	writer->source_index = -1;
	copyStr(writer->family, sizeof(writer->family), family);
	copyStr(writer->catalog_id, sizeof(writer->catalog_id), catalog_id);
	return MODARCHIVE_OK;
}

void assetArchiveWriterSetProvenance(asset_archive_writer_t *writer,
                                     const char *tool,
                                     const char *source_path,
                                     s32 source_index,
                                     const char *source_symbol)
{
	if (!writer) return;
	copyStr(writer->tool, sizeof(writer->tool), tool);
	copyStr(writer->source_path, sizeof(writer->source_path), source_path);
	writer->source_index = source_index;
	copyStr(writer->source_symbol, sizeof(writer->source_symbol), source_symbol);
}

s32 assetArchiveWriterAddDescriptor(asset_archive_writer_t *writer,
                                    const char *descriptor_name,
                                    const void *data,
                                    u32 len)
{
	if (!writer || !descriptor_name || !descriptor_name[0]) {
		return MODARCHIVE_ERR_FORMAT;
	}
	copyStr(writer->descriptor, sizeof(writer->descriptor), descriptor_name);
	return addTrackedMem(writer, descriptor_name, data, len,
		"descriptor", 1);
}

s32 assetArchiveWriterAddManifestJson(asset_archive_writer_t *writer,
                                      const void *data,
                                      u32 len)
{
	return addMetaText(writer, ASSET_ARCHIVE_META_MANIFEST, data, len);
}

s32 assetArchiveWriterAddPublicMem(asset_archive_writer_t *writer,
                                   const char *entry_name,
                                   const void *data,
                                   u32 len,
                                   const char *role)
{
	return addTrackedMem(writer, entry_name, data, len, role, 1);
}

s32 assetArchiveWriterAddPublicDisk(asset_archive_writer_t *writer,
                                    const char *entry_name,
                                    const char *src_path,
                                    const char *role)
{
	if (!writer || !writer->archive || !entry_name || !entry_name[0] ||
			!src_path || !src_path[0]) {
		return MODARCHIVE_ERR_FORMAT;
	}

	char sidecar_name[ASSET_ARCHIVE_WRITER_PATH_MAX];
	s32 r = sidecarPathFor(entry_name, sidecar_name, sizeof(sidecar_name));
	if (r != MODARCHIVE_OK) return r;

	FILE *f = fopen(src_path, "rb");
	if (!f) return MODARCHIVE_ERR_OPEN;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return MODARCHIVE_ERR_IO;
	}
	long pos = ftell(f);
	fclose(f);
	if (pos < 0) return MODARCHIVE_ERR_IO;

	u8 digest[SHA256_DIGEST_SIZE];
	if (sha256HashFile(src_path, digest) != 0) return MODARCHIVE_ERR_IO;
	char sha_hex[SHA256_HEX_SIZE];
	sha256ToHex(digest, sha_hex);

	r = modArchiveAddFileDisk(writer->archive, entry_name, src_path);
	if (r != MODARCHIVE_OK) return r;
	r = trackEntry(writer, entry_name, role, (u32)pos, sha_hex);
	if (r != MODARCHIVE_OK) return r;

	char sidecar[SHA256_HEX_SIZE + 1];
	snprintf(sidecar, sizeof(sidecar), "%s\n", sha_hex);
	return modArchiveAddFileMem(writer->archive, sidecar_name,
		sidecar, (u32)strlen(sidecar));
}

s32 assetArchiveWriterAddBundledMem(asset_archive_writer_t *writer,
                                    const char *entry_name,
                                    const void *data,
                                    u32 len,
                                    const char *role)
{
	return addTrackedMem(writer, entry_name, data, len, role, 0);
}

s32 assetArchiveWriterAddMetaJson(asset_archive_writer_t *writer,
                                  const char *leaf_name,
                                  const void *data,
                                  u32 len)
{
	return addMetaText(writer, leaf_name, data, len);
}

s32 assetArchiveWriterFinishMetadata(asset_archive_writer_t *writer)
{
	if (!writer || !writer->archive) return MODARCHIVE_ERR_FORMAT;
	s32 r;
	if (!writer->has_manifest) {
		r = writeDefaultManifest(writer);
		if (r != MODARCHIVE_OK) return r;
	}
	if (!writer->has_inventory) {
		r = writeInventory(writer);
		if (r != MODARCHIVE_OK) return r;
	}
	if (!writer->has_hashes) {
		r = writeHashes(writer);
		if (r != MODARCHIVE_OK) return r;
	}
	if (!writer->has_provenance) {
		r = writeProvenance(writer);
		if (r != MODARCHIVE_OK) return r;
	}
	if (!writer->has_validation) {
		r = writeValidation(writer);
		if (r != MODARCHIVE_OK) return r;
	}
	if (!writer->has_source_handles) {
		r = writeSourceHandles(writer);
		if (r != MODARCHIVE_OK) return r;
	}
	return MODARCHIVE_OK;
}
