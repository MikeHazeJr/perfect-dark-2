#ifndef PD_BODY_HEAD_SOURCE_BIND_H
#define PD_BODY_HEAD_SOURCE_BIND_H
#include "body_head_source.h"
#include "loader_walker_mesh_source.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct body_head_mesh_binding {
    char id[CATALOG_ID_LEN];
    loader_walker_mesh_source_plan_t plan;
    s32 present;
    s32 source_by_catalog_id;
} body_head_mesh_binding_t;

/* Read-only complete source preflight. Empty expected_id derives identity
 * from the selected public mesh.ini. archive_path is already qualified.
 * source_by_catalog_id permits established native provenance aliases for
 * body/head and hand models, which load their selected catalog source directly.
 * Filenum-only routes and private bridges require unique reverse ownership. */
s32 bodyHeadSourcePrepareMesh(const char *archive_path, const char *expected_id,
    s32 source_by_catalog_id, body_head_mesh_binding_t *out, char *error, size_t error_capacity);
/* Caller holds the ordinary scanner transaction including model slot map.
 * Catalog pool pointers become invalid on any nested registration. */
s32 bodyHeadSourceBindMesh(body_head_mesh_binding_t *binding, s32 bundled,
    s32 temporary, s32 *out_filenum, char *error, size_t error_capacity);

/* Shared scanner/walker ordinary registration entry. Generated base_slot_hint
 * is optional private slot provenance, never a public native scalar source.
 * Existing typed owner slot always wins; occupied conflicting slots fail.
 * Returns 1 success / 0 failure with the whole transaction restored. */
s32 assetCatalogRegisterBodyHeadArchive(const char *archive_path,
    const char *expected_id, s32 is_head, s32 base_slot_hint, s32 bundled,
    char *error, size_t error_capacity);

/* Received loose descriptors keep the existing admission transaction alive
 * until their filesystem publication commits. No catalog pointers escape.
 * Finish exactly once: commit=0 restores all snapshots; commit=1 releases them.
 * Preparation enforces the transport's expected catalog ID before mutation. */
void *assetCatalogPrepareBodyHeadDescriptor(const char *descriptor_path,
    const char *expected_id, const char *component_dir, const char *category,
    s32 is_head, s32 temporary, char *error, size_t error_capacity);
s32 assetCatalogFinishBodyHeadAdmission(void *admission, s32 commit);

#ifdef __cplusplus
}
#endif
#endif
