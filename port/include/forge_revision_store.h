#ifndef PD_FORGE_REVISION_STORE_H
#define PD_FORGE_REVISION_STORE_H

#include "assetcatalog.h"
#include "grid_match_sources.h"
#include "sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct forge_revision {
	char arena_id[CATALOG_ID_LEN];
	char digest[SHA256_HEX_SIZE];
	char path[FS_MAXPATH + 1];
} forge_revision_t;

#define FORGE_REVISION_PARENT_MAP_KEY "grid_parent_map_sha256"
#define FORGE_REVISION_PARENT_DIGEST_KEY "grid_parent_revision_sha256"

typedef enum forge_revision_parent {
	FORGE_REVISION_PARENT_NONE,
	FORGE_REVISION_PARENT_AVAILABLE,
	FORGE_REVISION_PARENT_UNAVAILABLE
} forge_revision_parent_e;

/* The caller provides an existing private profile directory. Authored
 * bytes are validated and kept by content digest. Each arena has an
 * atomically replaced current pin. Storing bytes here grants no sharing rights. */
s32 forgeRevisionPublishLocal(const char *directory, const char *arena_id,
	const char *saved_path, const char *expected_digest, forge_revision_t *out,
	char *err, size_t err_cap);

s32 forgeRevisionReadCurrent(const char *directory, const char *arena_id,
	forge_revision_t *out, char *err, size_t err_cap);

/* Remember immutable bytes for local recovery through a separate per-arena
 * previous pointer. The explicit current pin stays independent. */
s32 forgeRevisionRememberPrevious(const char *directory, const char *arena_id,
	const char *saved_path, const char *expected_digest, forge_revision_t *out,
	char *err, size_t err_cap);
s32 forgeRevisionReadPrevious(const char *directory, const char *arena_id,
	forge_revision_t *out, char *err, size_t err_cap);

/* A previous or played revision remains reachable after the pointer moves. */
s32 forgeRevisionRead(const char *directory, const char *arena_id, const char *digest,
	forge_revision_t *out, char *err, size_t err_cap);

/* Stash exact, marked-original native source beneath the existing private
 * revision directory. Authorization and the variant's dependency set are
 * caller-owned. An existing digest file is accepted only with identical
 * bytes. Loading revalidates the archive and returns free()-owned bytes,
 * with optional size zero on failure. */
s32 forgeRevisionStoreDependency(const char *directory, const grid_match_source_package_t *package,
	const void *bytes, u32 size, char *err, size_t err_cap);
void *forgeRevisionLoadDependency(const char *directory, const grid_match_source_package_t *package,
	u32 *size, char *err, size_t err_cap);

#define FORGE_REVISION_DEPENDENCY_PLAN_KEY "grid_variant_dependencies_sha256"

/* An inspectable immutable plan contains typed dependencies and their exact
 * archive identities. Its semantic digest excludes local paths. Arenas and
 * dynamically selected match content are outside this variant plan. */
s32 forgeRevisionStoreDependencyPlan(const char *directory, const grid_match_sources_t *plan,
	char *err, size_t err_cap);
s32 forgeRevisionLoadDependencyPlan(const char *directory, const char *digest, grid_match_sources_t *out,
	char *err, size_t err_cap);

/* Resolve the Variant's recorded parent without consulting either current pin.
 * The parent archive must match both its recorded digest and the hash of its
 * own catalog ID. A standalone arena has no parent. */
forge_revision_parent_e forgeRevisionReadParent(const char *directory,
	const forge_revision_t *variant, forge_revision_t *out, char *err, size_t err_cap);

#ifdef __cplusplus
}
#endif

#endif
