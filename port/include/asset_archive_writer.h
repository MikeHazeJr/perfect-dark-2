/**
 * asset_archive_writer.h -- shared typed asset archive emission helpers.
 *
 * c3838-s1: new extractors should write the public root descriptor and
 * editable payload files through this wrapper, then let the wrapper emit the
 * standard _meta inventory, hashes, provenance, validation, and source handle
 * files. The underlying archive is still the shared modarchive ZIP writer.
 */
#ifndef _IN_ASSET_ARCHIVE_WRITER_H
#define _IN_ASSET_ARCHIVE_WRITER_H

#include <PR/ultratypes.h>

#include "modarchive.h"
#include "sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ASSET_ARCHIVE_WRITER_PATH_MAX 256
#define ASSET_ARCHIVE_WRITER_ROLE_MAX 32
#define ASSET_ARCHIVE_WRITER_FAMILY_MAX 32
#define ASSET_ARCHIVE_WRITER_ID_MAX 128
#define ASSET_ARCHIVE_WRITER_TOOL_MAX 64
#define ASSET_ARCHIVE_WRITER_SOURCE_MAX 256
#define ASSET_ARCHIVE_WRITER_MAX_ENTRIES 384

#define ASSET_ARCHIVE_WRITER_DEPENDENCY_ROOT "dependencies/assets"

typedef struct asset_archive_writer_entry {
	char path[ASSET_ARCHIVE_WRITER_PATH_MAX];
	char role[ASSET_ARCHIVE_WRITER_ROLE_MAX];
	u32 size;
	char sha256[SHA256_HEX_SIZE];
} asset_archive_writer_entry_t;

typedef struct asset_archive_writer {
	mod_archive_writer_t *archive;
	char family[ASSET_ARCHIVE_WRITER_FAMILY_MAX];
	char catalog_id[ASSET_ARCHIVE_WRITER_ID_MAX];
	char descriptor[ASSET_ARCHIVE_WRITER_PATH_MAX];
	char tool[ASSET_ARCHIVE_WRITER_TOOL_MAX];
	char source_path[ASSET_ARCHIVE_WRITER_SOURCE_MAX];
	char source_symbol[ASSET_ARCHIVE_WRITER_SOURCE_MAX];
	s32 source_index;
	u32 entry_count;
	asset_archive_writer_entry_t entries[ASSET_ARCHIVE_WRITER_MAX_ENTRIES];
	s32 has_manifest;
	s32 has_inventory;
	s32 has_hashes;
	s32 has_provenance;
	s32 has_validation;
	s32 has_source_handles;
} asset_archive_writer_t;

s32 assetArchiveWriterInit(asset_archive_writer_t *writer,
                           mod_archive_writer_t *archive,
                           const char *family,
                           const char *catalog_id);
void assetArchiveWriterSetProvenance(asset_archive_writer_t *writer,
                                     const char *tool,
                                     const char *source_path,
                                     s32 source_index,
                                     const char *source_symbol);

s32 assetArchiveWriterAddDescriptor(asset_archive_writer_t *writer,
                                    const char *descriptor_name,
                                    const void *data,
                                    u32 len);
s32 assetArchiveWriterAddManifestJson(asset_archive_writer_t *writer,
                                      const void *data,
                                      u32 len);
s32 assetArchiveWriterAddPublicMem(asset_archive_writer_t *writer,
                                   const char *entry_name,
                                   const void *data,
                                   u32 len,
                                   const char *role);
s32 assetArchiveWriterAddPublicDisk(asset_archive_writer_t *writer,
                                    const char *entry_name,
                                    const char *src_path,
                                    const char *role);
s32 assetArchiveWriterAddMetaJson(asset_archive_writer_t *writer,
                                  const char *leaf_name,
                                  const void *data,
                                  u32 len);
s32 assetArchiveWriterFinishMetadata(asset_archive_writer_t *writer);

#ifdef __cplusplus
}
#endif

#endif /* _IN_ASSET_ARCHIVE_WRITER_H */
