#ifndef PD_GRID_MATCH_TRANSFER_H
#define PD_GRID_MATCH_TRANSFER_H

#include "grid_match_cache.h"

#define GRID_MATCH_CHUNK_WIRE_HEADER_SIZE (18u + SHA256_HEX_SIZE - 1u)

struct netbuf;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grid_match_chunk {
	grid_match_offer_t offer;
	u16 package_index;
	u32 offset;
	u16 size;
	const u8 *data;
} grid_match_chunk_t;

/* Encode or decode one raw package chunk. Failure preserves the buffer and
 * previous output. A decoded payload borrows the reader's packet storage. */
s32 gridMatchChunkWrite(struct netbuf *dst, const grid_match_chunk_t *chunk);
s32 gridMatchChunkRead(struct netbuf *src, grid_match_chunk_t *chunk);

typedef struct grid_match_transfer grid_match_transfer_t;

/* Pass the authenticated offer and package metadata from its validated plan
 * after consent. Start with a NULL handle and reset before another package. */
s32 gridMatchTransferBegin(grid_match_transfer_t **transfer, const grid_match_offer_t *offer,
	u16 package_index, u32 package_size);

/* Copy the next contiguous bytes or accept an identical repeat wholly inside
 * the received prefix. A refusal preserves that prefix. */
s32 gridMatchTransferAppend(grid_match_transfer_t *transfer, const grid_match_chunk_t *chunk);

/* The completed bytes are private and borrowed until reset. The caller uses
 * gridMatchCacheAccept to verify and admit them. If no bytes are returned,
 * out_size is zero. */
const u8 *gridMatchTransferComplete(const grid_match_transfer_t *transfer,
	const grid_match_offer_t *offer, u16 package_index, u32 *out_size);
void gridMatchTransferReset(grid_match_transfer_t **transfer);

#ifdef __cplusplus
}
#endif

#endif
