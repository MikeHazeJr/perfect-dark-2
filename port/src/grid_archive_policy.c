#include <stdlib.h>
#include <string.h>

#include "asset_archive_policy.h"
#include "assetcatalog_scanner.h"
#include "grid_archive_policy.h"
#include "grid_match_package_io.h"
#include "net/net.h"

static grid_archive_origin_e gridDescriptorOrigin(const char *descriptor, u32 size)
{
	if (!descriptor) return GRID_ARCHIVE_INVALID;
	ini_section_t *ini = malloc(sizeof(*ini));
	grid_archive_origin_e origin = GRID_ARCHIVE_INVALID;
	if (ini && iniParseText(descriptor, size, ini, NULL, 0)) {
		origin = GRID_ARCHIVE_LEGACY;
		for (s32 i = 0; i < ini->count; i++) {
			if (strcmp(ini->pairs[i].key, GRID_ARENA_PROVENANCE_KEY) != 0) continue;
			if (origin != GRID_ARCHIVE_LEGACY) {
				origin = GRID_ARCHIVE_INVALID;
				break;
			}
			const char *value = ini->pairs[i].value;
			if (strcmp(value, GRID_ARENA_PROVENANCE_BYOR) == 0) origin = GRID_ARCHIVE_BYOR;
			else if (strcmp(value, GRID_ARENA_PROVENANCE_ORIGINAL) == 0) origin = GRID_ARCHIVE_ORIGINAL;
			else if (strcmp(value, GRID_ARENA_PROVENANCE_UNVERIFIED) == 0) origin = GRID_ARCHIVE_UNVERIFIED;
			else origin = GRID_ARCHIVE_INVALID;
		}
	}
	free(ini);
	return origin;
}

grid_archive_origin_e gridArchiveOrigin(const char *name, const void *bytes, u32 size)
{
	const asset_type_e type = assetArchiveTypeForPath(name);
	if (type != ASSET_ARENA && type != ASSET_WEAPON) return GRID_ARCHIVE_LEGACY;

	u32 descriptor_size = 0;
	char *descriptor = assetArchiveExtractDescriptorMemAlloc(bytes, size, name,
		ASSET_ARCHIVE_VALIDATE_RELEASE, &descriptor_size, NULL);
	const grid_archive_origin_e origin = gridDescriptorOrigin(descriptor, descriptor_size);
	free(descriptor);
	return origin;
}

grid_archive_origin_e gridMatchPackageOrigin(const char *name, const void *bytes, u32 size)
{
	const char *leaf = assetArchiveDescriptorForPath(name);
	if (!leaf || size > NET_DISTRIB_MAX_COMP) return GRID_ARCHIVE_INVALID;

	u32 descriptor_size = 0;
	char *descriptor = gridMatchPackageReadMember(bytes, size, leaf,
		NET_DISTRIB_MAX_COMP, &descriptor_size, NULL);
	grid_archive_origin_e origin = GRID_ARCHIVE_INVALID;
	if (descriptor && !memchr(descriptor, '\0', descriptor_size)) {
		origin = gridDescriptorOrigin(descriptor, descriptor_size);
	}
	free(descriptor);
	return origin;
}
