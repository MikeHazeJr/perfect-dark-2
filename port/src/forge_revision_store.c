#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "asset_archive_policy.h"
#include "asset_path_contract.h"
#include "assetcatalog_scanner.h"
#include "forge_revision_store.h"
#include "grid_archive_policy.h"
#include "grid_match_cache.h"
#include "grid_match_package_io.h"
#include "mod_asset_validate.h"
#include "mods_roots.h"
#include "net/net.h"
#include "net/netmanifest.h"
#include "save_atomic.h"

#define GRID_REVISION_MAX_BYTES (128u * 1024u * 1024u)
#define GRID_REVISION_POINTER_HEADER "PD2_GRID_REVISION_V1\n"
#define GRID_VARIANT_SOURCES_HEADER "PD2_GRID_VARIANT_SOURCES_V1\n"

static s32 revisionError(char *err, size_t cap, const char *message)
{
	if (err && cap) snprintf(err, cap, "%s", message);
	return 0;
}

static s32 revisionValidDigest(const char *digest)
{
	return digest && strlen(digest) == SHA256_HEX_SIZE - 1
		&& strspn(digest, "0123456789abcdef") == SHA256_HEX_SIZE - 1;
}

static s32 revisionDependencyPlanPath(const char *directory, const char *digest,
	char folder[FS_MAXPATH + 1], char path[FS_MAXPATH + 1])
{
	if (!directory || !assetPathIsAbsolute(directory) || assetPathHasParentTraversal(directory)
			|| strstr(directory, "::") || !revisionValidDigest(digest)) return 0;
	const int dir_length = snprintf(folder, FS_MAXPATH + 1, "%s/sources", directory);
	if (dir_length <= 0 || dir_length > FS_MAXPATH) return 0;
	const int path_length = snprintf(path, FS_MAXPATH + 1, "%s/%s.plan", folder, digest);
	return path_length > 0 && path_length <= FS_MAXPATH;
}

static s32 revisionPlanColumns(FILE *input, char *line, size_t capacity, char **fields, size_t count)
{
	if (!fgets(line, (int)capacity, input)) return 0;
	const size_t length = strlen(line);
	if (!length || line[length - 1] != '\n' || strchr(line, '\r')) return 0;
	line[length - 1] = '\0';
	for (size_t i = 0; i < count; i++) {
		fields[i] = line;
		char *separator = strchr(line, '\t');
		if (i + 1 == count) return !separator;
		if (!separator) return 0;
		*separator = '\0';
		line = separator + 1;
	}
	return 0;
}

static s32 revisionPlanNumber(const char *text, u32 max, u32 *out)
{
	if (!text[0] || strspn(text, "0123456789") != strlen(text) || (text[0] == '0' && text[1])) return 0;
	errno = 0;
	const unsigned long number = strtoul(text, NULL, 10);
	if (errno == ERANGE || number > max) return 0;
	*out = (u32)number;
	return 1;
}

static s32 revisionPlanPackage(char *const fields[6], grid_match_source_package_t *out)
{
	u32 type, bundled;
	if (!assetPathCopyChecked(out->owner.id, sizeof(out->owner.id), fields[0])
			|| !revisionPlanNumber(fields[1], ASSET_TYPE_COUNT - 1, &type)
			|| !revisionPlanNumber(fields[2], NET_DISTRIB_MAX_COMP, &out->size)
			|| !revisionPlanNumber(fields[3], 1, &bundled)
			|| !assetPathCopyChecked(out->suffix, sizeof(out->suffix), fields[4])
			|| !revisionValidDigest(fields[5])) return 0;
	out->owner.type = (asset_type_e)type;
	out->bundled = bundled;
	for (size_t i = 0; i < sizeof(out->digest); i++) {
		unsigned value;
		if (sscanf(fields[5] + 2 * i, "%2x", &value) != 1) return 0;
		out->digest[i] = (u8)value;
	}
	return type != ASSET_ARENA;
}

static s32 revisionPlanEntry(char *const fields[4], grid_match_source_entry_t *out)
{
	u32 type, package;
	if (!assetPathCopyChecked(out->asset.id, sizeof(out->asset.id), fields[0])
			|| !revisionPlanNumber(fields[1], ASSET_TYPE_COUNT - 1, &type)
			|| !revisionPlanNumber(fields[2], MANIFEST_MAX_ENTRIES - 1, &package)) return 0;
	if (fields[3][0] && !assetPathCopyChecked(out->member_chain, sizeof(out->member_chain), fields[3])) return 0;
	out->asset.type = (asset_type_e)type;
	out->package_index = package;
	return type != ASSET_ARENA;
}

s32 forgeRevisionLoadDependencyPlan(const char *directory, const char *digest, grid_match_sources_t *out,
	char *err, size_t err_cap)
{
	char folder[FS_MAXPATH + 1], path[FS_MAXPATH + 1];
	if (err && err_cap) err[0] = '\0';
	if (!out || !revisionDependencyPlanPath(directory, digest, folder, path))
		return revisionError(err, err_cap, "invalid variant source plan identity");
	FILE *input = fopen(path, "rb");
	if (!input) return revisionError(err, err_cap, "the variant source plan is unavailable");

	grid_match_sources_t candidate = {0};
	char line[FS_MAXPATH + CATALOG_ID_LEN + 96], *fields[6];
	u32 packages = 0, entries = 0;
	s32 valid = fgets(line, sizeof(line), input) && strcmp(line, GRID_VARIANT_SOURCES_HEADER) == 0
		&& revisionPlanColumns(input, line, sizeof(line), fields, 2)
		&& revisionPlanNumber(fields[0], MANIFEST_MAX_ENTRIES, &packages)
		&& revisionPlanNumber(fields[1], MANIFEST_MAX_ENTRIES, &entries)
		&& packages && entries && packages <= entries;
	if (valid) {
		candidate.package_count = packages;
		candidate.entry_count = entries;
		candidate.packages = calloc(packages, sizeof(*candidate.packages));
		candidate.entries = calloc(entries, sizeof(*candidate.entries));
		valid = candidate.packages && candidate.entries;
	}
	for (u32 i = 0; valid && i < packages; i++)
		valid = revisionPlanColumns(input, line, sizeof(line), fields, 6)
			&& revisionPlanPackage(fields, &candidate.packages[i]);
	for (u32 i = 0; valid && i < entries; i++)
		valid = revisionPlanColumns(input, line, sizeof(line), fields, 4)
			&& revisionPlanEntry(fields, &candidate.entries[i]);
	valid = valid && fgetc(input) == EOF && !ferror(input)
		&& gridMatchSourcesSeal(&candidate, err, err_cap) && strcmp(candidate.digest, digest) == 0;
	fclose(input);
	if (!valid) {
		gridMatchSourcesFree(&candidate);
		return revisionError(err, err_cap, "the stored variant sources differ from the exact plan");
	}
	gridMatchSourcesFree(out);
	*out = candidate;
	return 1;
}

s32 forgeRevisionStoreDependencyPlan(const char *directory, const grid_match_sources_t *plan,
	char *err, size_t err_cap)
{
	char folder[FS_MAXPATH + 1], path[FS_MAXPATH + 1];
	if (err && err_cap) err[0] = '\0';
	if (!plan || !revisionDependencyPlanPath(directory, plan->digest, folder, path))
		return revisionError(err, err_cap, "invalid variant source plan identity");
	grid_match_sources_t sealed = *plan;
	if (!gridMatchSourcesSeal(&sealed, err, err_cap) || strcmp(sealed.digest, plan->digest) != 0
			|| gridMatchSourcesFiestaCount(&sealed))
		return revisionError(err, err_cap, "the variant requires an exact source plan");
	for (size_t i = 0; i < sealed.package_count; i++) {
		if (sealed.packages[i].owner.type == ASSET_ARENA || sealed.packages[i].size > NET_DISTRIB_MAX_COMP)
			return revisionError(err, err_cap, "the dependency plan contains an unsupported archive");
	}
	for (size_t i = 0; i < sealed.entry_count; i++) {
		if (sealed.entries[i].asset.type == ASSET_ARENA || strpbrk(sealed.entries[i].member_chain, "\r\n\t"))
			return revisionError(err, err_cap, "the dependency plan contains an invalid native source row");
	}
	if (!modsRootsEnsureDir(folder)) return revisionError(err, err_cap, "the variant source folder is unavailable");
	FILE *existing = fopen(path, "rb");
	if (!existing && errno != ENOENT) return revisionError(err, err_cap, "the variant source plan could not be opened");
	if (existing) {
		fclose(existing);
	} else {
		save_atomic_file_t file;
		if (saveAtomicBegin(&file, path) != 0)
			return revisionError(err, err_cap, "the variant source plan could not be staged");
		FILE *output = saveAtomicStream(&file);
		s32 written = fprintf(output, GRID_VARIANT_SOURCES_HEADER "%zu\t%zu\n",
			sealed.package_count, sealed.entry_count) > 0;
		for (size_t i = 0; written && i < sealed.package_count; i++) {
			const grid_match_source_package_t *package = &sealed.packages[i];
			char hex[SHA256_HEX_SIZE];
			sha256ToHex(package->digest, hex);
			written = fprintf(output, "%s\t%u\t%u\t%d\t%s\t%s\n", package->owner.id,
				(unsigned)package->owner.type, package->size, package->bundled, package->suffix, hex) > 0;
		}
		for (size_t i = 0; written && i < sealed.entry_count; i++) {
			const grid_match_source_entry_t *entry = &sealed.entries[i];
			written = fprintf(output, "%s\t%u\t%zu\t%s\n", entry->asset.id, (unsigned)entry->asset.type,
				entry->package_index, entry->member_chain) > 0;
		}
		if (written && saveAtomicCommitNew(&file) == 0) return 1;
		saveAtomicAbort(&file);
	}
	grid_match_sources_t stored = {0};
	const s32 valid = forgeRevisionLoadDependencyPlan(directory, sealed.digest, &stored, err, err_cap);
	gridMatchSourcesFree(&stored);
	return valid;
}

static s32 revisionPointerPath(const char *directory, const char *arena_id, s32 previous,
	char *path, size_t cap)
{
	if (!directory || !directory[0] || !arena_id || !arena_id[0]
			|| strlen(arena_id) >= CATALOG_ID_LEN || strpbrk(arena_id, "\r\n")) {
		return 0;
	}
	u8 hash[SHA256_DIGEST_SIZE];
	char hex[SHA256_HEX_SIZE];
	sha256Hash(arena_id, strlen(arena_id), hash);
	sha256ToHex(hash, hex);
	int len = snprintf(path, cap, "%s/%s.%s", directory, hex, previous ? "previous" : "current");
	return len > 0 && (size_t)len < cap;
}

static s32 revisionDescribe(const char *directory, const char *arena_id, const char *digest,
	forge_revision_t *out)
{
	if (!out || !directory || !directory[0]
			|| (arena_id && (!arena_id[0] || strlen(arena_id) >= sizeof(out->arena_id)))
			|| !revisionValidDigest(digest)) {
		return 0;
	}
	int len = snprintf(out->path, sizeof(out->path), "%s/%s.pdarena", directory, digest);
	if (len <= 0 || (size_t)len >= sizeof(out->path)) return 0;
	memcpy(out->digest, digest, sizeof(out->digest));
	snprintf(out->arena_id, sizeof(out->arena_id), "%s", arena_id ? arena_id : "");
	return 1;
}

static void *revisionReadArchive(FILE *stream, u32 expected_size, u32 *out_size)
{
	if (fseek(stream, 0, SEEK_END) != 0) return NULL;
	long size = ftell(stream);
	if (size <= 0 || (unsigned long)size > GRID_REVISION_MAX_BYTES
			|| (expected_size && (unsigned long)size != expected_size)
			|| fseek(stream, 0, SEEK_SET) != 0) return NULL;
	void *bytes = malloc((size_t)size);
	if (!bytes) return NULL;
	if (fread(bytes, 1, (size_t)size, stream) != (size_t)size
			|| fgetc(stream) != EOF || ferror(stream)) {
		free(bytes);
		return NULL;
	}
	*out_size = (u32)size;
	return bytes;
}

static void *revisionLoadArchive(const char *path, u32 *size, char *err, size_t err_cap)
{
	FILE *stream = fopen(path, "rb");
	if (!stream) {
		revisionError(err, err_cap, "the revision file could not be opened");
		return NULL;
	}
	void *bytes = revisionReadArchive(stream, 0, size);
	fclose(stream);
	if (!bytes) revisionError(err, err_cap, "the revision archive could not be read");
	return bytes;
}

static s32 revisionVerifyDigest(const void *bytes, u32 size, const char *digest,
	char *err, size_t err_cap)
{
	u8 hash[SHA256_DIGEST_SIZE];
	char hex[SHA256_HEX_SIZE];
	sha256Hash(bytes, size, hash);
	sha256ToHex(hash, hex);
	if (strcmp(hex, digest) != 0) {
		return revisionError(err, err_cap, "the archive bytes do not match this revision");
	}
	return 1;
}

static ini_section_t *revisionDescriptor(const void *bytes, u32 size, const char *path,
	char *err, size_t err_cap)
{
	u32 descriptor_size = 0;
	const char *entry = NULL;
	char *descriptor = assetArchiveExtractDescriptorMemAlloc(bytes, size, path,
		ASSET_ARCHIVE_VALIDATE_RELEASE, &descriptor_size, &entry);
	ini_section_t *ini = malloc(sizeof(*ini));
	const s32 ok = descriptor && ini && strcmp(entry, "arena.ini") == 0
		&& !memchr(descriptor, '\0', descriptor_size)
		&& iniParseText(descriptor, descriptor_size, ini, err, err_cap);
	free(descriptor);
	if (!ok) {
		free(ini);
		if (!err || !err_cap || !err[0]) {
			revisionError(err, err_cap, "the arena descriptor could not be read");
		}
		return NULL;
	}
	return ini;
}

static const char *revisionField(const ini_section_t *ini, const char *key, s32 *duplicate)
{
	const char *value = NULL;
	for (s32 i = 0; i < ini->count; i++) {
		if (strcmp(ini->pairs[i].key, key) != 0) continue;
		if (value) *duplicate = 1;
		value = ini->pairs[i].value;
	}
	return value;
}

static s32 revisionMatches(const void *bytes, u32 size, forge_revision_t *revision,
	const char *map_hash, char *err, size_t err_cap)
{
	if (!revisionVerifyDigest(bytes, size, revision->digest, err, err_cap)
			|| modAssetValidateArchiveBytes(bytes, size, revision->path, err, err_cap)
				!= MOD_ASSET_VALIDATE_OK) {
		return 0;
	}
	ini_section_t *ini = revisionDescriptor(bytes, size, revision->path, err, err_cap);
	if (!ini) return 0;
	s32 duplicate = 0;
	const char *id = revisionField(ini, "catalog_id", &duplicate);
	s32 matches = id && id[0] && strlen(id) < sizeof(revision->arena_id) && !duplicate;
	if (matches && map_hash) {
		matches = revisionVerifyDigest(id, (u32)strlen(id), map_hash, NULL, 0);
		if (matches) snprintf(revision->arena_id, sizeof(revision->arena_id), "%s", id);
	} else if (matches) {
		matches = strcmp(id, revision->arena_id) == 0;
	}
	free(ini);
	if (!matches) return revisionError(err, err_cap, "the arena ID does not match this revision");
	return 1;
}

static s32 revisionVerifyArchive(forge_revision_t *revision, const char *map_hash,
	char *err, size_t err_cap)
{
	u32 size = 0;
	void *bytes = revisionLoadArchive(revision->path, &size, err, err_cap);
	if (!bytes) return 0;
	const s32 matches = revisionMatches(bytes, size, revision, map_hash, err, err_cap);
	free(bytes);
	return matches;
}

static s32 revisionWrite(const char *path, const void *bytes, size_t size, s32 create_only)
{
	save_atomic_file_t file;
	if (saveAtomicBegin(&file, path) != 0) return 0;
	if (fwrite(bytes, 1, size, saveAtomicStream(&file)) != size) {
		saveAtomicAbort(&file);
		return 0;
	}
	return (create_only ? saveAtomicCommitNew(&file) : saveAtomicCommit(&file)) == 0;
}

static s32 revisionDependencyPath(const char *directory, const grid_match_source_package_t *package,
	char folder[FS_MAXPATH + 1], char path[FS_MAXPATH + 1])
{
	static const u8 empty_digest[SHA256_DIGEST_SIZE] = {0};
	if (!directory || !directory[0] || !assetPathIsAbsolute(directory)
			|| (directory[1] == ':' && directory[2] != '/' && directory[2] != '\\')
			|| strstr(directory, "::") || assetPathHasParentTraversal(directory)
			|| !package || package->bundled || !package->size || package->size > NET_DISTRIB_MAX_COMP
			|| !memchr(package->suffix, '\0', sizeof(package->suffix))
			|| !gridMatchPackageTypeShareable(package->owner.type, package->suffix)
			|| memcmp(package->digest, empty_digest, sizeof(empty_digest)) == 0) return 0;

	const char *id = package->owner.id;
	if (!memchr(id, '\0', sizeof(package->owner.id)) || strncmp(id, "base:", 5) == 0
			|| strspn(id, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:_.-") != strlen(id)) return 0;
	const char *separator = strchr(id, ':');
	if (!separator || separator == id || !separator[1] || strchr(separator + 1, ':')) return 0;

	char hex[SHA256_HEX_SIZE];
	sha256ToHex(package->digest, hex);
	const int folder_len = snprintf(folder, FS_MAXPATH + 1, "%s/sources", directory);
	if (folder_len <= 0 || folder_len > FS_MAXPATH) return 0;
	const int path_len = snprintf(path, FS_MAXPATH + 1, "%s/%s%s", folder, hex, package->suffix);
	return path_len > 0 && path_len <= FS_MAXPATH;
}

static s32 revisionDependencySectionMatches(asset_type_e type, const char *leaf, const char *section)
{
	/* The native scanner accepts both mesh and audio aliases. For the other
	 * shareable families, the section is the canonical descriptor stem. */
	if (type == ASSET_MODEL) return strcmp(section, "mesh") == 0 || strcmp(section, "model") == 0;
	if (type == ASSET_AUDIO) return strcmp(section, "audio") == 0 || strcmp(section, "sfx") == 0
		|| strcmp(section, "voice") == 0 || strcmp(section, "music") == 0;
	const size_t stem_size = strlen(leaf) - strlen(".ini");
	return strlen(section) == stem_size && strncmp(section, leaf, stem_size) == 0;
}

static s32 revisionDependencyMatches(const grid_match_source_package_t *package,
	const void *bytes, u32 size, char *err, size_t err_cap)
{
	if (!bytes || size != package->size) {
		return revisionError(err, err_cap, "the variant dependency size does not match");
	}
	u8 hash[SHA256_DIGEST_SIZE];
	sha256Hash(bytes, size, hash);
	if (memcmp(hash, package->digest, sizeof(hash)) != 0) {
		return revisionError(err, err_cap, "the variant dependency checksum does not match");
	}
	if (!gridMatchCacheCheckArchiveBudget(bytes, size, err, err_cap)
			|| modAssetValidateArchiveBytes(bytes, size, package->suffix, err, err_cap) != MOD_ASSET_VALIDATE_OK) return 0;
	if (gridMatchPackageOrigin(package->suffix, bytes, size) != GRID_ARCHIVE_ORIGINAL) {
		return revisionError(err, err_cap, "the variant dependency is not marked as original native source");
	}

	u32 descriptor_size = 0;
	const char *leaf = assetArchiveDescriptorForPath(package->suffix);
	if (!leaf) return revisionError(err, err_cap, "the variant dependency type has no native descriptor");
	char *descriptor = gridMatchPackageReadMember(bytes, size, leaf, NET_DISTRIB_MAX_COMP, &descriptor_size, NULL);
	ini_section_t *ini = malloc(sizeof(*ini));
	s32 duplicate = 0;
	const char *id = descriptor && ini && !memchr(descriptor, '\0', descriptor_size)
		&& iniParseText(descriptor, descriptor_size, ini, NULL, 0)
		? revisionField(ini, "catalog_id", &duplicate) : NULL;
	const s32 matches = id && !duplicate && strcmp(id, package->owner.id) == 0
		&& revisionDependencySectionMatches(package->owner.type, leaf, ini->type);
	free(ini);
	free(descriptor);
	return matches || revisionError(err, err_cap, "the variant dependency owning ID or type does not match");
}

static void *revisionReadDependencyFile(const char *path, u32 expected_size, s32 *missing)
{
	FILE *stream = fopen(path, "rb");
	if (missing) *missing = !stream && errno == ENOENT;
	if (!stream) return NULL;
	u32 size = 0;
	void *bytes = revisionReadArchive(stream, expected_size, &size);
	fclose(stream);
	return bytes;
}

s32 forgeRevisionStoreDependency(const char *directory, const grid_match_source_package_t *package,
	const void *bytes, u32 size, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	char folder[FS_MAXPATH + 1], path[FS_MAXPATH + 1];
	if (!revisionDependencyPath(directory, package, folder, path)) {
		return revisionError(err, err_cap, "invalid variant dependency identity or revision directory");
	}
	if (!revisionDependencyMatches(package, bytes, size, err, err_cap)) return 0;
	struct stat parent;
	if (stat(directory, &parent) != 0 || !S_ISDIR(parent.st_mode) || !modsRootsEnsureDir(folder)) {
		return revisionError(err, err_cap, "could not open the private variant dependency directory");
	}
	s32 missing = 0;
	void *stored = revisionReadDependencyFile(path, size, &missing);
	if (missing) {
		if (revisionWrite(path, bytes, size, 1)) return 1;
		/* A concurrent save may have published the same digest. */
		stored = revisionReadDependencyFile(path, size, NULL);
	}
	const s32 exact = stored && memcmp(stored, bytes, size) == 0;
	free(stored);
	return exact || revisionError(err, err_cap, "the exact variant dependency could not be preserved");
}

void *forgeRevisionLoadDependency(const char *directory, const grid_match_source_package_t *package,
	u32 *size, char *err, size_t err_cap)
{
	if (size) *size = 0;
	if (err && err_cap) err[0] = '\0';
	char folder[FS_MAXPATH + 1], path[FS_MAXPATH + 1];
	if (!revisionDependencyPath(directory, package, folder, path)) {
		revisionError(err, err_cap, "invalid variant dependency identity or revision directory");
		return NULL;
	}
	void *bytes = revisionReadDependencyFile(path, package->size, NULL);
	if (!bytes) {
		revisionError(err, err_cap, "the exact saved variant dependency could not be read");
		return NULL;
	}
	if (!revisionDependencyMatches(package, bytes, package->size, err, err_cap)) {
		free(bytes);
		return NULL;
	}
	if (size) *size = package->size;
	return bytes;
}

s32 forgeRevisionRead(const char *directory, const char *arena_id, const char *digest,
	forge_revision_t *out, char *err, size_t err_cap)
{
	forge_revision_t revision;
	if (err && err_cap) err[0] = '\0';
	if (!out || !arena_id || !revisionDescribe(directory, arena_id, digest, &revision)) {
		return revisionError(err, err_cap, "invalid arena revision");
	}
	if (!revisionVerifyArchive(&revision, NULL, err, err_cap)) return 0;
	*out = revision;
	return 1;
}

forge_revision_parent_e forgeRevisionReadParent(const char *directory,
	const forge_revision_t *variant, forge_revision_t *out, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!variant || !out || !revisionValidDigest(variant->digest)) {
		revisionError(err, err_cap, "invalid Variant revision");
		return FORGE_REVISION_PARENT_UNAVAILABLE;
	}
	u32 size = 0;
	void *bytes = revisionLoadArchive(variant->path, &size, err, err_cap);
	if (!bytes) return FORGE_REVISION_PARENT_UNAVAILABLE;
	ini_section_t *ini = revisionVerifyDigest(bytes, size, variant->digest, err, err_cap)
		? revisionDescriptor(bytes, size, variant->path, err, err_cap) : NULL;
	free(bytes);
	if (!ini) return FORGE_REVISION_PARENT_UNAVAILABLE;

	s32 duplicate = 0;
	const char *id = revisionField(ini, "catalog_id", &duplicate);
	const char *map_hash = revisionField(ini, FORGE_REVISION_PARENT_MAP_KEY, &duplicate);
	const char *digest = revisionField(ini, FORGE_REVISION_PARENT_DIGEST_KEY, &duplicate);
	forge_revision_parent_e status = FORGE_REVISION_PARENT_UNAVAILABLE;
	forge_revision_t parent;
	if (!id || strcmp(id, variant->arena_id) != 0 || duplicate) {
		revisionError(err, err_cap, "the Variant's arena descriptor is ambiguous or mismatched");
	} else if (!map_hash && !digest) {
		status = FORGE_REVISION_PARENT_NONE;
	} else if (!revisionValidDigest(map_hash) || !revisionDescribe(directory, NULL, digest, &parent)
			|| strcmp(variant->digest, digest) == 0) {
		revisionError(err, err_cap, "invalid parent revision recorded in this Variant");
	} else if (revisionVerifyArchive(&parent, map_hash, err, err_cap)) {
		if (strcmp(parent.arena_id, variant->arena_id) == 0) {
			revisionError(err, err_cap, "the Variant must have its own arena ID");
		} else {
			*out = parent;
			status = FORGE_REVISION_PARENT_AVAILABLE;
		}
	}
	free(ini);
	return status;
}

static s32 revisionReadPointer(const char *directory, const char *arena_id, s32 previous,
	forge_revision_t *out, char *err, size_t err_cap)
{
	char pointer[FS_MAXPATH + 1];
	char record[sizeof(GRID_REVISION_POINTER_HEADER) + CATALOG_ID_LEN + SHA256_HEX_SIZE + 2];
	char header[sizeof(GRID_REVISION_POINTER_HEADER) + CATALOG_ID_LEN];
	if (err && err_cap) err[0] = '\0';
	if (!revisionPointerPath(directory, arena_id, previous, pointer, sizeof(pointer))) {
		return revisionError(err, err_cap, "invalid arena revision pointer");
	}
	FILE *stream = fopen(pointer, "rb");
	if (!stream) return revisionError(err, err_cap, previous
		? "this arena has no remembered previous revision" : "this arena has no local published revision");
	size_t size = fread(record, 1, sizeof(record) - 1, stream);
	s32 ended = fgetc(stream) == EOF && !ferror(stream);
	fclose(stream);
	int prefix_len = snprintf(header, sizeof(header), GRID_REVISION_POINTER_HEADER "%s\n", arena_id);
	if (!ended || prefix_len <= 0 || (size_t)prefix_len >= sizeof(header)
			|| size != (size_t)prefix_len + SHA256_HEX_SIZE
			|| memcmp(record, header, (size_t)prefix_len) != 0 || record[size - 1] != '\n') {
		return revisionError(err, err_cap, "the arena revision pointer is invalid");
	}
	record[size - 1] = '\0';
	return forgeRevisionRead(directory, arena_id, record + prefix_len, out, err, err_cap);
}

s32 forgeRevisionReadCurrent(const char *directory, const char *arena_id,
	forge_revision_t *out, char *err, size_t err_cap)
{
	return revisionReadPointer(directory, arena_id, 0, out, err, err_cap);
}

s32 forgeRevisionReadPrevious(const char *directory, const char *arena_id,
	forge_revision_t *out, char *err, size_t err_cap)
{
	return revisionReadPointer(directory, arena_id, 1, out, err, err_cap);
}

static s32 revisionPublishPointer(const char *directory, const char *arena_id, s32 previous,
	const char *saved_path, const char *expected_digest, forge_revision_t *out,
	char *err, size_t err_cap)
{
	forge_revision_t revision;
	char pointer[FS_MAXPATH + 1];
	if (err && err_cap) err[0] = '\0';
	if (!out || !saved_path || !arena_id
			|| (expected_digest && !revisionValidDigest(expected_digest))
			|| !revisionPointerPath(directory, arena_id, previous, pointer, sizeof(pointer))) {
		return revisionError(err, err_cap, "invalid arena or saved revision identity");
	}

	FILE *stream = fopen(saved_path, "rb");
	if (!stream) return revisionError(err, err_cap, "the saved archive could not be opened");
	u32 size = 0;
	void *bytes = revisionReadArchive(stream, 0, &size);
	fclose(stream);
	if (!bytes) return revisionError(err, err_cap, "the saved archive could not be read or is too large");
	char actual_digest[SHA256_HEX_SIZE];
	if (!expected_digest) {
		u8 hash[SHA256_DIGEST_SIZE];
		sha256Hash(bytes, size, hash);
		sha256ToHex(hash, actual_digest);
		expected_digest = actual_digest;
	}
	if (!revisionDescribe(directory, arena_id, expected_digest, &revision)) {
		free(bytes);
		return revisionError(err, err_cap, "invalid arena or saved revision identity");
	}
	if (!revisionMatches(bytes, size, &revision, NULL, err, err_cap)) {
		free(bytes);
		return 0;
	}

	stream = fopen(revision.path, "rb");
	s32 stored;
	if (stream) {
		/* The same content may already have been preserved by an older
		 * publish or an interrupted attempt. Never replace these bytes. */
		fclose(stream);
		u8 hash[SHA256_DIGEST_SIZE];
		char hex[SHA256_HEX_SIZE];
		stored = modArchiveSha256(revision.path, hash) == 0;
		if (stored) {
			sha256ToHex(hash, hex);
			stored = strcmp(hex, revision.digest) == 0;
		}
	} else {
		stored = errno == ENOENT && revisionWrite(revision.path, bytes, size, 0);
	}
	free(bytes);
	if (!stored) return revisionError(err, err_cap, "the immutable revision could not be preserved");

	char record[sizeof(GRID_REVISION_POINTER_HEADER) + CATALOG_ID_LEN + SHA256_HEX_SIZE + 2];
	int len = snprintf(record, sizeof(record), GRID_REVISION_POINTER_HEADER "%s\n%s\n",
		revision.arena_id, revision.digest);
	if (len <= 0 || (size_t)len >= sizeof(record) || !revisionWrite(pointer, record, (size_t)len, 0)) {
		return revisionError(err, err_cap, previous
			? "the previous revision pointer could not be saved" : "the current revision pointer could not be saved");
	}
	*out = revision;
	return 1;
}

s32 forgeRevisionPublishLocal(const char *directory, const char *arena_id,
	const char *saved_path, const char *expected_digest, forge_revision_t *out,
	char *err, size_t err_cap)
{
	return revisionPublishPointer(directory, arena_id, 0, saved_path, expected_digest, out, err, err_cap);
}

s32 forgeRevisionRememberPrevious(const char *directory, const char *arena_id,
	const char *saved_path, const char *expected_digest, forge_revision_t *out,
	char *err, size_t err_cap)
{
	return revisionPublishPointer(directory, arena_id, 1, saved_path, expected_digest, out, err, err_cap);
}
