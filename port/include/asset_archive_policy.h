/**
 * asset_archive_policy.h -- shared typed asset archive layout rules.
 *
 * c3824-s1: all typed *.pdxxx files are zip-openable authoring units with a
 * public descriptor at archive root and machine-owned metadata under _meta/.
 * Readers use migration mode to accept the older root metadata while emitters
 * and package gates use release mode for the frozen two-zone contract.
 */
#ifndef _IN_ASSET_ARCHIVE_POLICY_H
#define _IN_ASSET_ARCHIVE_POLICY_H

#include <stddef.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "modarchive.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ASSET_ARCHIVE_META_DIR "_meta"
#define ASSET_ARCHIVE_META_MANIFEST "manifest.json"
#define ASSET_ARCHIVE_META_MANIFEST_PATH "_meta/manifest.json"

typedef enum asset_archive_validation_mode {
	ASSET_ARCHIVE_VALIDATE_MIGRATION = 0,
	ASSET_ARCHIVE_VALIDATE_RELEASE = 1,
} asset_archive_validation_mode_e;

s32 assetArchivePathIsDeprecated(const char *path);
s32 assetArchivePathIsTyped(const char *path);
asset_type_e assetArchiveTypeForPath(const char *path);
const char *assetArchiveDescriptorForPath(const char *path);
const char *assetArchiveLegacyDescriptorForPath(const char *path);

void assetArchiveMetaPath(const char *leaf, char *out, size_t out_cap);
s32 assetArchiveFindMetadataEntry(mod_archive_t *archive, const char *leaf);
s32 assetArchiveEntryIsForbiddenBinPayload(const char *entry_name);
s32 assetArchiveEntryIsRootMachineMetadata(const char *entry_name);

s32 assetArchiveFindDescriptorEntry(mod_archive_t *archive,
                                    const char *archive_path,
                                    asset_archive_validation_mode_e mode,
                                    const char **out_entry_name);

char *assetArchiveExtractDescriptorMemAlloc(const void *archive_bytes,
                                            u32 archive_size,
                                            const char *archive_name,
                                            asset_archive_validation_mode_e mode,
                                            u32 *out_size,
                                            const char **out_entry_name);

s32 assetArchiveValidateOpened(mod_archive_t *archive,
                               const char *archive_path,
                               asset_archive_validation_mode_e mode,
                               char *err, size_t err_cap);
s32 assetArchiveValidateFile(const char *archive_path,
                             asset_archive_validation_mode_e mode,
                             char *err, size_t err_cap);
s32 assetArchiveValidateBytes(const void *archive_bytes, u32 archive_size,
                              const char *archive_name,
                              asset_archive_validation_mode_e mode,
                              char *err, size_t err_cap);

#ifdef __cplusplus
}
#endif

#endif /* _IN_ASSET_ARCHIVE_POLICY_H */
