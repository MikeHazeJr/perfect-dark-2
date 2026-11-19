#ifndef PD_GRID_MATCH_SOURCES_H
#define PD_GRID_MATCH_SOURCES_H

#include "assetcatalog.h"
#include "catalog_dep_activation_plan.h"
#include "sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GRID_MATCH_SOURCE_FIESTA_WIRE_BIT 0x80

typedef struct grid_match_source_root {
	char id[CATALOG_ID_LEN];
	asset_type_e type;
} grid_match_source_root_t;

/* Identity of a logical asset inside an already measured typed archive.
 * A top-level asset has an empty member chain. All other paths are archive
 * members, with :: between successive typed archives. */
typedef struct grid_match_source_archive {
	char path[FS_MAXPATH + 1];
	char member_chain[FS_MAXPATH + 1];
	grid_match_source_root_t owner;
	char suffix[24];
	u8 digest[SHA256_DIGEST_SIZE];
	u32 size;
	s32 bundled;
	/* Set after verifying an eligible nested child's owning archive from its
	 * exact bytes. The plan includes the same archive's top-level catalog
	 * row and its dependencies, but this grants no transfer or selection. */
	s32 complete_owner;
} grid_match_source_archive_t;

typedef s32 (*grid_match_source_resolve_fn)(const char *id, asset_type_e *type,
	void *userdata);
typedef s32 (*grid_match_source_archive_fn)(const grid_match_source_root_t *asset,
	grid_match_source_archive_t *archive, void *userdata, char *err, size_t err_cap);
typedef s32 (*grid_match_source_texture_visit_fn)(const char *id, void *userdata);
typedef s32 (*grid_match_source_model_fn)(const grid_match_source_root_t *asset,
	grid_match_source_archive_t *archive, grid_match_source_texture_visit_fn visit, void *visit_data,
	void *userdata, char *err, size_t err_cap);

typedef struct grid_match_source_entry {
	grid_match_source_root_t asset;
	char member_chain[FS_MAXPATH + 1];
	size_t package_index;
	u8 fiesta_pool; /* Explicit host-selected Fiesta membership. */
} grid_match_source_entry_t;

typedef struct grid_match_source_package {
	char path[FS_MAXPATH + 1];
	grid_match_source_root_t owner;
	char suffix[24];
	u8 digest[SHA256_DIGEST_SIZE];
	u32 size;
	s32 bundled;
} grid_match_source_package_t;

typedef struct grid_match_sources {
	grid_match_source_entry_t *entries;
	grid_match_source_package_t *packages;
	size_t entry_count;
	size_t package_count;
	u64 total_bytes;
	char digest[SHA256_HEX_SIZE];
} grid_match_sources_t;

/* Build the typed closure transactionally, in stable ID order. Local paths
 * are excluded from the digest. A failed build leaves an older plan intact.
 * This preflight conveys identity only. Admission and permission to share
 * must come from the match transaction. */
s32 gridMatchSourcesBuild(grid_match_sources_t *out,
	const grid_match_source_root_t *roots, size_t root_count,
	grid_match_source_resolve_fn resolve, grid_match_source_archive_fn inspect,
	void *userdata, char *err, size_t err_cap);
/* Inspect each encountered model once, binding its archive fingerprint and
 * explicit texture edges from the same bytes in the ordinary typed closure. */
s32 gridMatchSourcesBuildWithModelTextures(grid_match_sources_t *out,
	const grid_match_source_root_t *roots, size_t root_count,
	grid_match_source_resolve_fn resolve, grid_match_source_archive_fn inspect,
	grid_match_source_model_fn inspect_model, void *userdata, char *err, size_t err_cap);
/* Supplement installed catalog edges with caller-owned exact typed edges.
 * These and model textures participate in the same cycle and type checks. */
s32 gridMatchSourcesBuildWithModelTexturesAndEdges(grid_match_sources_t *out,
	const grid_match_source_root_t *roots, size_t root_count,
	grid_match_source_resolve_fn resolve, grid_match_source_archive_fn inspect,
	grid_match_source_model_fn inspect_model, void *userdata,
	catalog_dep_activation_supplement_fn extra_edges, void *extra_data,
	char *err, size_t err_cap);
/* Validate canonical rows received from the match wire and derive their
 * aggregate byte count and digest. Local paths are outside this identity. */
s32 gridMatchSourcesSeal(grid_match_sources_t *plan, char *err, size_t err_cap);
size_t gridMatchSourcesFiestaCount(const grid_match_sources_t *plan);
/* Replace the explicit Fiesta membership on a sealed plan and update its
 * digest. Each typed weapon must already be in the exact closure. Zero roots
 * clears the pool. Refusal leaves the rows and digest unchanged. */
s32 gridMatchSourcesSetFiestaPool(grid_match_sources_t *plan, const grid_match_source_root_t *roots,
	size_t count, char *err, size_t err_cap);
/* Compare sealed, nonempty plans by logical ID, typed member chain and
 * owning package identity. Explicit membership in the subset requires the
 * same membership in the whole. Local locators and package indices may differ. */
s32 gridMatchSourcesExactSubset(const grid_match_sources_t *subset, const grid_match_sources_t *whole,
	char *err, size_t err_cap);
/* Own a canonical union of two sealed plans. Exact overlaps merge Fiesta
 * membership. An empty local locator is valid. Refusal leaves the caller's
 * plan intact. */
s32 gridMatchSourcesCombine(grid_match_sources_t *out, const grid_match_sources_t *gameplay,
	const grid_match_sources_t *arena, char *err, size_t err_cap);
void gridMatchSourcesFree(grid_match_sources_t *plan);

#ifdef __cplusplus
}
#endif

#endif
