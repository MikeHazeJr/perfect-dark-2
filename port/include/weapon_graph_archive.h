/**
 * weapon_graph_archive.h -- shared weapon/projectile/entity archive helpers.
 *
 * c3814: weapon behavior graph assets use zip-openable .pdweapon,
 * .pdprojectile, and .pdentity archives. This module centralizes descriptor
 * reads, derived nested IDs, nested payload inventory, and canonical payload
 * SHA-256 so the editor, emitter, scanner, and runtime compiler do not drift.
 */
#ifndef _IN_WEAPON_GRAPH_ARCHIVE_H
#define _IN_WEAPON_GRAPH_ARCHIVE_H

#include <stddef.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WEAPON_GRAPH_ARCHIVE_GRAPH_ENTRY "behavior.graph.json"
#define WEAPON_GRAPH_ARCHIVE_NESTED_PAYLOADS_ENTRY "nested_payloads.json"
#define WEAPON_GRAPH_ARCHIVE_LOCAL_SLUG_LEN 64
#define WEAPON_GRAPH_ARCHIVE_MAX_NESTED_PAYLOADS 64

typedef struct weapon_graph_archive_descriptor {
	asset_type_e type;
	char descriptor_entry[32];
	char section[32];
	char catalog_id[CATALOG_ID_LEN];
	char behavior_graph[128];
	char primary_graph[128];
	char secondary_graph[128];
	char shared_context[128];
	char settings[128];
	char variables[128];
	char manifest[128];
	char nested_payloads[128];
	char model_file[128];
	char entity_ref[CATALOG_ID_LEN];
	char archetype[64];
} weapon_graph_archive_descriptor_t;

typedef struct weapon_graph_archive_payload {
	asset_type_e type;
	char archive_entry[FS_MAXPATH];
	char local_slug[WEAPON_GRAPH_ARCHIVE_LOCAL_SLUG_LEN];
	char catalog_id[CATALOG_ID_LEN];
	char canonical_sha256[SHA256_HEX_SIZE];
} weapon_graph_archive_payload_t;

typedef struct weapon_graph_archive_inventory {
	weapon_graph_archive_payload_t payloads[WEAPON_GRAPH_ARCHIVE_MAX_NESTED_PAYLOADS];
	s32 count;
} weapon_graph_archive_inventory_t;

const char *weaponGraphArchiveDescriptorForType(asset_type_e type);
const char *weaponGraphArchiveExtensionForType(asset_type_e type);
const char *weaponGraphArchiveTypeName(asset_type_e type);
s32 weaponGraphArchiveGraphRequired(asset_type_e type);

s32 weaponGraphArchiveReadTextFile(const char *archive_path, const char *entry_name,
                                   char **out_text, u32 *out_size);
s32 weaponGraphArchiveReadDescriptorFile(const char *archive_path,
                                         asset_type_e expected_type,
                                         weapon_graph_archive_descriptor_t *out,
                                         char *err, size_t err_cap);
s32 weaponGraphArchiveReadDescriptorBytes(const void *archive_bytes,
                                          u32 archive_size,
                                          asset_type_e expected_type,
                                          weapon_graph_archive_descriptor_t *out,
                                          char *err, size_t err_cap);
s32 weaponGraphArchiveValidateRootFile(const char *archive_path,
                                       asset_type_e expected_type,
                                       char *err, size_t err_cap);

s32 weaponGraphArchiveDerivedNestedId(const char *parent_id, asset_type_e type,
                                      const char *local_slug,
                                      char *out, size_t out_cap);

s32 weaponGraphArchiveCanonicalSha256File(const char *archive_path,
                                          char out_hex[SHA256_HEX_SIZE]);
s32 weaponGraphArchiveCanonicalSha256Bytes(const void *archive_bytes,
                                           u32 archive_size,
                                           char out_hex[SHA256_HEX_SIZE]);

s32 weaponGraphArchiveScanNestedPayloadsFile(const char *archive_path,
                                             const char *parent_id,
                                             weapon_graph_archive_inventory_t *out,
                                             char *err, size_t err_cap);
s32 weaponGraphArchiveFormatNestedPayloadsJson(const char *asset_id,
                                               const weapon_graph_archive_inventory_t *inventory,
                                               char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* _IN_WEAPON_GRAPH_ARCHIVE_H */
