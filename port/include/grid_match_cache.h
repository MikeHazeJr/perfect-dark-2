#ifndef PD_GRID_MATCH_CACHE_H
#define PD_GRID_MATCH_CACHE_H

#include "grid_match_sources.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grid_match_offer {
	u8 room_id;
	u64 generation;
	char manifest_digest[SHA256_HEX_SIZE];
} grid_match_offer_t;

typedef struct grid_match_cache grid_match_cache_t;

/* A caller-owned, single-threaded cache. Start with a NULL handle and reset
 * before beginning another offer. The caller authenticates the room and
 * supplies a unique nonzero generation. The offer must name the digest of
 * the canonical source plan. Begin copies the expected packages and logical
 * assets. Package paths can be empty when the plan came from an offer. */
s32 gridMatchCacheBegin(grid_match_cache_t **cache, const grid_match_offer_t *offer,
	const grid_match_sources_t *plan, char *err, size_t err_cap);

/* Check the normal package and recursive expansion budgets before probing
 * a borrowed archive. Exact type, identity and aggregate cache validation
 * are still required at admission. */
s32 gridMatchCacheCheckArchiveBudget(const void *bytes, u32 size, char *err, size_t err_cap);

/* Copies and validates the exact package bytes for this offer. An identical
 * repeat succeeds. A failed delivery leaves previously accepted bytes intact. */
s32 gridMatchCacheAccept(grid_match_cache_t *cache, const grid_match_offer_t *offer,
	size_t package_index, const void *bytes, u32 size, char *err, size_t err_cap);
s32 gridMatchCacheReady(const grid_match_cache_t *cache, const grid_match_offer_t *offer);

/* Copies the sealed logical rows and package descriptors from a ready cache.
 * Replaces the caller's owning plan on success. Archive bytes stay in cache. */
s32 gridMatchCacheCopySources(const grid_match_cache_t *cache, const grid_match_offer_t *offer,
	grid_match_sources_t *out, char *err, size_t err_cap);

/* Prove that every combined package has a ready donor and the sum of their
 * verified expanded sizes fits one session. No bytes or ownership move. */
s32 gridMatchCacheValidateCombinedBudget(const grid_match_sources_t *plan,
	const grid_match_cache_t *first, const grid_match_offer_t *first_offer,
	const grid_match_cache_t *second, const grid_match_offer_t *second_offer,
	char *err, size_t err_cap);

/* Create a ready cache for a sealed plan from two ready donors in the same
 * room. Each typed asset must match a donor row and its exact package.
 * Package bytes are shared and retained independently of the donor caches.
 * The caller supplies an empty output handle. */
s32 gridMatchCacheCompose(grid_match_cache_t **out, const grid_match_offer_t *offer,
	const grid_match_sources_t *plan, const grid_match_cache_t *first, const grid_match_offer_t *first_offer,
	const grid_match_cache_t *second, const grid_match_offer_t *second_offer, char *err, size_t err_cap);

/* Retain a second owning handle to the exact ready cache. The caller
 * supplies an empty output handle and resets each owner independently. */
s32 gridMatchCacheRetainReady(grid_match_cache_t **out, grid_match_cache_t *ready,
	const grid_match_offer_t *offer, char *err, size_t err_cap);

/* These sealed package rows stay cache-owned while the handle is retained. */
const grid_match_source_package_t *gridMatchCachePackage(const grid_match_cache_t *cache,
	const grid_match_offer_t *offer, size_t package_index);

/* Returns cache-owned bytes only when the whole offer is ready. The borrowed
 * pointer stays valid while the handle is retained. out_size is zero when no
 * bytes are returned. */
const void *gridMatchCacheRead(const grid_match_cache_t *cache,
	const grid_match_offer_t *offer, size_t package_index, u32 *out_size);
void gridMatchCacheReset(grid_match_cache_t **cache);

#ifdef __cplusplus
}
#endif

#endif
