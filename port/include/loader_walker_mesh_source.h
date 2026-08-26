/**
 * loader_walker_mesh_source.h -- canonical typed .pdmesh source binding.
 *
 * A model catalog ID may be seeded before its public .pdmesh is discovered
 * (for example, a weapon model row seeded from a legacy ROM filenum).  This
 * contract keeps the row identity stable while making the validated typed
 * archive member its byte source.  Top-level mesh walking and nested weapon
 * dependency publication must both use this plan/bind path.
 */
#ifndef PD_LOADER_WALKER_MESH_SOURCE_H
#define PD_LOADER_WALKER_MESH_SOURCE_H

#include <stddef.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "fs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct loader_walker_mesh_source_plan {
	char archive_path[FS_MAXPATH + 1];
	char geometry_member[FS_MAXPATH];
	char source_path[FS_MAXPATH + 1];
	s32 source_filenum;
	s32 source_symbol_present;
} loader_walker_mesh_source_plan_t;

/**
 * Canonical public mesh geometry aliases, in one shared precedence order.
 * Top-level and nested descriptor readers use this list so registration order
 * cannot select different source members from the same public mesh.ini.
 */
size_t loaderWalkerMeshPublicGeometryKeyCount(void);
const char *loaderWalkerMeshPublicGeometryKey(size_t index);

/**
 * Build one canonical FileProvider source plan for a typed .pdmesh.
 *
 * public_geometry is the game-facing mesh.ini geometry member when the
 * caller has it.  The private manifest geometry may only confirm that value;
 * disagreement fails closed. Production top-level and nested walkers pass the
 * public descriptor value; callers without one retain the legacy manifest
 * fallback. Both sources default to model.obj when absent.
 *
 * A declared source_filenum_symbol must resolve.  If an existing stable row
 * already has a positive filenum, a declared symbol must resolve to the same
 * value.  When the manifest omits the symbol, the existing filenum is kept.
 */
s32 loaderWalkerMeshSourcePlanManifest(
	const char *manifest, size_t manifest_len,
	const char *expected_catalog_id,
	const char *archive_path,
	const char *public_geometry,
	s32 existing_source_filenum,
	loader_walker_mesh_source_plan_t *out,
	char *err, size_t err_cap);

/**
 * Return whether a source-changing bind is safe for the row's current
 * lifecycle. REGISTERED/ENABLED rows are mutable. The only LOADED exception
 * is the historical bundled sentinel row which has no resident payload;
 * ACTIVE rows and every row with resident payload state are immutable.
 */
s32 loaderWalkerMeshSourceChangeAllowed(
	asset_load_state_t load_state,
	s32 bundled,
	s32 ref_count,
	s32 has_loaded_data,
	asset_payload_kind_t payload_kind);

/**
 * Compare a candidate typed archive with the typed FileProvider archive that
 * already owns an entry. candidate_archive_bytes may be supplied by a nested
 * scanner to avoid reopening the candidate; otherwise candidate_archive_path
 * is loaded through the VFS. Comparison is the archive's canonical SHA-256,
 * not path or registration order.
 */
s32 loaderWalkerMeshSourceMatchesEntry(
	const asset_entry_t *entry,
	const char *candidate_archive_path,
	const void *candidate_archive_bytes,
	u32 candidate_archive_size,
	char *err, size_t err_cap);

/**
 * Bind a prepared typed source without replacing catalog identity or runtime
 * indices.  If reject_live_source_change is non-zero, changing the source of
 * a row with resident/active payload state fails; an already-identical bind
 * remains idempotent.
 */
s32 loaderWalkerBindMeshSource(
	asset_entry_t *entry,
	const loader_walker_mesh_source_plan_t *plan,
	s32 reject_live_source_change,
	char *err, size_t err_cap);

#ifdef __cplusplus
}
#endif

#endif /* PD_LOADER_WALKER_MESH_SOURCE_H */
