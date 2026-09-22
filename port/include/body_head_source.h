#ifndef PD_BODY_HEAD_SOURCE_H
#define PD_BODY_HEAD_SOURCE_H

#include <stddef.h>
#include "assetcatalog.h"
#include "assetcatalog_scanner.h"
#include "catalog_mgr_bodies.h"
#include "catalog_mgr_heads.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Draft: public descriptor scalars and typed references only. Runtime indices,
 * native file bridges and modeldef ownership never come from this document. */
typedef struct body_head_source {
    s32 is_head;
    char id[CATALOG_ID_LEN];
    u8 ismale, complete, canvaryheight, type;
    u16 height;
    f32 scale, animscale;
    f32 model_scale;
    u8 enabled, bundled, requirefeature;
    s32 has_name_langid, has_display_name;
    s16 name_langid;
    char display_name[64], rig_class[32];
    char mesh_id[CATALOG_ID_LEN], hand_id[CATALOG_ID_LEN];
    char mesh_archive[FS_MAXPATH], hand_archive[FS_MAXPATH];
} body_head_source_t;

/* Shared strict section reader for body/head and their nested public mesh.ini.
 * Publishes the selected section only, rejects duplicate keys and sections. */
s32 bodyHeadSourceReadIniBytes(const char *data, u32 size, const char *section,
    ini_section_t *out, char *error, size_t error_capacity);
s32 bodyHeadSourceCatalogIdValid(const char *id);

/* The caller supplies the existing shared INI parse result. Defaults for
 * omitted scalars match the old native record defaults: flags/type/height=0,
 * scale/animscale=1. Present invalid values fail, with no private fallback.
 * Unknown unrelated descriptor keys are retained by the caller, not consumed.
 * A missing mesh ID may be resolved from that nested public mesh.ini; a
 * present ID must match it. No numeric or generated identity is synthesized. */
s32 bodyHeadSourceParseIni(const ini_section_t *ini, const char *expected_id,
    s32 is_head, body_head_source_t *out, char *error, size_t error_capacity);
s32 bodyHeadSourceParseArchiveBytes(const void *archive, u32 archive_size,
    const char *expected_id, s32 is_head, body_head_source_t *out,
    char *error, size_t error_capacity);

/* File bridges are resolved from the selected typed mesh/hand sources by the
 * caller. Zero mesh bridge is valid for direct catalog body/head providers.
 * A declared hand requires a usable positive native bridge; never drop it. */
s32 bodyHeadSourceBuildBody(const body_head_source_t *source, s32 owned_slot,
    s32 mesh_filenum, s32 hand_filenum, body_data_t *out);
s32 bodyHeadSourceBuildHead(const body_head_source_t *source, s32 owned_slot,
    s32 mesh_filenum, head_data_t *out);

#ifdef __cplusplus
}
#endif
#endif
