#ifndef PD_GRID_MATCH_WIRE_H
#define PD_GRID_MATCH_WIRE_H

#include "grid_match_cache.h"
#include "net/netmanifest.h"

struct netbuf;

#ifdef __cplusplus
extern "C" {
#endif

#define GRID_MATCH_MAX_SHARED_PACKAGES MANIFEST_MAX_ENTRIES
#define GRID_MATCH_SHARED_PACKAGES_WIRE_MAX (2u + 2u * GRID_MATCH_MAX_SHARED_PACKAGES)

typedef struct grid_match_shared_packages {
	u16 count;
	u16 indexes[GRID_MATCH_MAX_SHARED_PACKAGES];
} grid_match_shared_packages_t;

/* One little-endian u16 count followed by that many u16 indices. Indices are
 * strictly ascending and below MANIFEST_MAX_ENTRIES. The enclosing message
 * checks them against the exact offer's package count and sharing policy.
 * Refusal preserves the cursor, output and packet bytes. */
s32 gridMatchSharedPackagesWrite(struct netbuf *dst, const grid_match_shared_packages_t *packages);
s32 gridMatchSharedPackagesRead(struct netbuf *src, grid_match_shared_packages_t *packages);

/* Encode or decode one exact typed offer as a length-bounded lobby payload.
 * Caller supplies the authenticated room context and the outer message.
 * On refusal the cursor, previous decoded offer and owning plan are preserved. */
s32 gridMatchOfferWrite(struct netbuf *dst, const grid_match_offer_t *offer,
	const grid_match_sources_t *plan, char *err, size_t err_cap);
s32 gridMatchOfferRead(struct netbuf *src, grid_match_offer_t *offer,
	grid_match_sources_t *plan, char *err, size_t err_cap);

/* Grid readiness and decline are scoped to the exact offer, never a legacy
 * manifest hash. The caller supplies the authenticated participant context. */
s32 gridMatchStatusWrite(struct netbuf *dst, const grid_match_offer_t *offer, u8 ready);
s32 gridMatchStatusRead(struct netbuf *src, grid_match_offer_t *offer, u8 *ready);

/* The zero-ready server stage control is standalone and bound to the exact
 * accepted offer and stage epoch. A refusal preserves the buffer. */
s32 gridMatchStageCancelWrite(struct netbuf *dst, const grid_match_offer_t *offer, u32 epoch);
s32 gridMatchStageCancelRead(struct netbuf *src, const grid_match_offer_t *expected, u32 epoch);

#ifdef __cplusplus
}
#endif

#endif
