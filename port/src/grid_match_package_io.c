#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "asset_archive_policy.h"
#include "asset_path_contract.h"
#include "fs.h"
#include "grid_match_package_io.h"
#include "mod_asset_validate.h"
#include "net/net.h"

typedef struct grid_match_package_owner {
	char physical[FS_MAXPATH + 1];
	const char *member;
	const char *end;
	const char *descriptor;
} grid_match_package_owner_t;

static s32 isSeparator(char c)
{
	return c == '/' || c == '\\';
}

static s32 physicalPathSafe(const char *path)
{
	if (!path[0] || (isSeparator(path[0]) && isSeparator(path[1]))
			|| assetPathHasParentTraversal(path)) return 0;
	if (path[0] == '$' && (!path[1] || !strchr("EHMBS", path[1]) || !isSeparator(path[2]))) return 0;

	const s32 drive_letter = (path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z');
	for (size_t i = 0; path[i]; i++) {
		unsigned char c = (unsigned char)path[i];
		if (c < 0x20 || c == 0x7f) return 0;
		if (c == ':' && (i != 1 || !drive_letter || !isSeparator(path[2]))) return 0;
	}
	return 1;
}

static s32 memberPathSafe(const char *start, const char *end)
{
	const char *segment = start;
	for (const char *p = start; ; p++) {
		if (p == end || *p == '/') {
			size_t length = (size_t)(p - segment);
			if (!length || (length == 1 && segment[0] == '.')
					|| (length == 2 && segment[0] == '.' && segment[1] == '.')) return 0;
			if (p == end) return 1;
			segment = p + 1;
		} else {
			unsigned char c = (unsigned char)*p;
			if (c < 0x20 || c == 0x7f || c == ':' || c == '\\') return 0;
		}
	}
}

static const char *partEnd(const char *start)
{
	const char *separator = strstr(start, "::");
	return separator ? separator : start + strlen(start);
}

static s32 copyPart(char out[FS_MAXPATH + 1], const char *start, const char *end)
{
	size_t length = (size_t)(end - start);
	if (!length || length > FS_MAXPATH) return 0;
	memcpy(out, start, length);
	out[length] = '\0';
	return 1;
}

static s32 isModWrapper(const char *path)
{
	static const char extension[] = ".pdmod";
	size_t length = strlen(path);
	size_t suffix = sizeof(extension) - 1;
	return length > suffix && strcasecmp(path + length - suffix, extension) == 0;
}

static const char *memberDescriptor(const char *start, const char *end)
{
	char member[FS_MAXPATH + 1];
	if (!memberPathSafe(start, end) || !copyPart(member, start, end)) return NULL;
	return assetArchiveDescriptorForPath(member);
}

static s32 parseOwner(const char *locator, grid_match_package_owner_t *out)
{
	if (!locator || !locator[0]) return 0;

	out->end = partEnd(locator);
	if (!copyPart(out->physical, locator, out->end) || !physicalPathSafe(out->physical)) return 0;
	out->member = NULL;
	out->descriptor = assetArchiveDescriptorForPath(out->physical);
	if (out->descriptor) return 1;

	if (!*out->end || !isModWrapper(out->physical)) return 0;
	out->member = out->end + 2;
	out->end = partEnd(out->member);
	out->descriptor = memberDescriptor(out->member, out->end);
	return out->descriptor != NULL;
}

s32 gridMatchPackageLocation(const char *descriptor, char *root, size_t root_cap,
	char *member_chain, size_t chain_cap, const char **descriptor_leaf)
{
	grid_match_package_owner_t owner;
	if (!root || !member_chain || !parseOwner(descriptor, &owner) || !*owner.end) return 0;

	const char *chain = owner.end + 2;
	const char *chain_end = chain;
	const char *leaf = chain;
	const char *expected = owner.descriptor;
	for (const char *separator = strstr(leaf, "::"); separator; separator = strstr(leaf, "::")) {
		expected = memberDescriptor(leaf, separator);
		if (!expected) return 0;
		chain_end = separator;
		leaf = separator + 2;
	}
	if (strcmp(leaf, expected) != 0) return 0;

	size_t root_size = (size_t)(owner.end - descriptor);
	size_t chain_size = (size_t)(chain_end - chain);
	if (root_size >= root_cap || chain_size >= chain_cap) return 0;

	memmove(root, descriptor, root_size);
	root[root_size] = '\0';
	memmove(member_chain, chain, chain_size);
	member_chain[chain_size] = '\0';
	if (descriptor_leaf) *descriptor_leaf = leaf;
	return 1;
}

s32 gridMatchPackageMemberOwner(asset_type_e outer_type, const char *chain,
	asset_type_e *owner_type, const char **member)
{
	if (outer_type <= ASSET_NONE || outer_type >= ASSET_TYPE_COUNT || !chain
			|| !owner_type || !member || strlen(chain) > FS_MAXPATH) return 0;

	asset_type_e owner = outer_type;
	for (s32 depth = 0; depth < MOD_ASSET_VALIDATE_MAX_DEPTH; ++depth) {
		const char *end = partEnd(chain);
		char part[FS_MAXPATH + 1];
		if (!memberPathSafe(chain, end) || !copyPart(part, chain, end)) return 0;
		const asset_type_e type = assetArchiveTypeForPath(part);
		if (type == ASSET_NONE) return 0;
		if (!*end) {
			*owner_type = owner;
			*member = chain;
			return 1;
		}
		owner = type;
		chain = end + 2;
	}
	return 0;
}

static void *loadPhysical(const char *path, u32 *size)
{
	FILE *stream = fopen(path, "rb");
	if (!stream) return NULL;

	void *bytes = NULL;
	if (fseek(stream, 0, SEEK_END) == 0) {
		long length = ftell(stream);
		if (length > 0 && length <= NET_DISTRIB_MAX_COMP && fseek(stream, 0, SEEK_SET) == 0) {
			bytes = malloc((size_t)length);
			if (bytes && fread(bytes, 1, (size_t)length, stream) == (size_t)length
					&& fgetc(stream) == EOF && !ferror(stream)) {
				*size = (u32)length;
			} else {
				free(bytes);
				bytes = NULL;
			}
		}
	}
	fclose(stream);
	return bytes;
}

static void *loadWrapped(const char *path, const char *member, u32 *size)
{
	mod_archive_t *archive = modArchiveOpen(path);
	if (!archive) return NULL;

	void *bytes = NULL;
	s32 entry = modArchiveFindEntry(archive, member);
	if (entry >= 0) {
		u32 length = modArchiveGetEntrySize(archive, entry);
		if (length && length <= NET_DISTRIB_MAX_COMP) {
			bytes = modArchiveExtractAlloc(archive, entry, size);
			if (bytes && *size != length) {
				free(bytes);
				bytes = NULL;
			}
		}
	}
	modArchiveClose(archive);
	return bytes;
}

void *gridMatchPackageLoad(const char *locator, u32 *out_size)
{
	if (out_size) *out_size = 0;
	grid_match_package_owner_t owner;
	if (!parseOwner(locator, &owner) || *owner.end) return NULL;

	char physical[FS_MAXPATH + 1];
	fsFullPath(owner.physical, physical, sizeof(physical));
	if (strlen(physical) >= FS_MAXPATH || physical[0] == '$' || !physicalPathSafe(physical)) return NULL;
	if (owner.member ? !isModWrapper(physical)
			: assetArchiveTypeForPath(physical) != assetArchiveTypeForPath(owner.physical)) return NULL;

	u32 size = 0;
	void *bytes = owner.member ? loadWrapped(physical, owner.member, &size) : loadPhysical(physical, &size);
	if (bytes && out_size) *out_size = size;
	return bytes;
}

s32 gridMatchPackageTypeShareable(asset_type_e type, const char *suffix)
{
	static const char *const shareable_suffixes[ASSET_TYPE_COUNT] = {
		[ASSET_ARENA] = ".pdarena",
		[ASSET_WEAPON] = ".pdweapon",
		[ASSET_MODEL] = ".pdmesh",
		[ASSET_BODY] = ".pdbody",
		[ASSET_HEAD] = ".pdhead",
		[ASSET_TEXTURE] = ".pdtexture",
		[ASSET_EFFECT] = ".pdeffect",
		[ASSET_PROJECTILE] = ".pdprojectile",
		[ASSET_ENTITY] = ".pdentity",
		[ASSET_MATERIAL] = ".pdmaterial",
		[ASSET_UI] = ".pdui",
		[ASSET_VEHICLE] = ".pdvehicle",
	};
	if (type <= ASSET_NONE || type >= ASSET_TYPE_COUNT || !suffix) return 0;
	if (type == ASSET_AUDIO) return strcmp(suffix, ".pdsfx") == 0 || strcmp(suffix, ".pdsong") == 0;
	const char *expected = shareable_suffixes[type];
	return expected && strcmp(suffix, expected) == 0;
}

s32 gridMatchPackagePrivatePath(const u8 digest[SHA256_DIGEST_SIZE], const char *suffix,
	char folder[FS_MAXPATH + 1], char path[FS_MAXPATH + 1])
{
	if (!digest || !folder || !path || folder == path
			|| !gridMatchPackageTypeShareable(assetArchiveTypeForPath(suffix), suffix)) return 0;

	char resolved[FS_MAXPATH + 1], locator[FS_MAXPATH + 1], hex[SHA256_HEX_SIZE];
	fsFullPath("$S/grid-session", resolved, sizeof(resolved));
	if (resolved[0] == '\\' || !assetPathIsAbsolute(resolved)) return 0;
	sha256ToHex(digest, hex);
	const int length = snprintf(locator, sizeof(locator), "%s/%s%s", resolved, hex, suffix);
	if (length <= 0 || length > FS_MAXPATH) return 0;

	memcpy(folder, resolved, strlen(resolved) + 1);
	memcpy(path, locator, (size_t)length + 1);
	return 1;
}

typedef struct grid_match_package_member {
	const char *name;
	u32 size;
	u32 matches;
} grid_match_package_member_t;

static s32 measureExactMember(const char *name, u32 size, void *userdata)
{
	grid_match_package_member_t *member = userdata;
	if (strcmp(name, member->name) == 0) {
		member->size = size;
		member->matches++;
	}
	return 0;
}

void *gridMatchPackageReadMember(const void *archive, u32 size, const char *name,
	u32 max_size, u32 *out_size, s32 *absent)
{
	if (out_size) *out_size = 0;
	if (absent) *absent = 0;
	if (!archive || !size || !name || !memberPathSafe(name, name + strlen(name)) || !max_size) return NULL;
	grid_match_package_member_t member = { .name = name };
	if (modArchiveMemForEachEntry(archive, size, measureExactMember, &member) != MODARCHIVE_OK) return NULL;
	if (!member.matches && absent) *absent = 1;
	if (member.matches != 1 || member.size > max_size) return NULL;

	u32 extracted_size = 0;
	void *bytes = modArchiveExtractMemAlloc(archive, size, name, &extracted_size);
	if (!bytes || extracted_size != member.size) {
		free(bytes);
		return NULL;
	}
	if (out_size) *out_size = extracted_size;
	return bytes;
}
