#ifndef PD_GRID_EDIT_SNAPSHOT_WIRE_H
#define PD_GRID_EDIT_SNAPSHOT_WIRE_H

#include "net/grid_edit_wire.h"
#include "sha256.h"

struct netbuf;

#ifdef __cplusplus
extern "C" {
#endif

#define GRID_EDIT_SNAPSHOT_OFFER_WIRE_SIZE (1u + 1u + 3u * 8u + 4u + SHA256_DIGEST_SIZE)
#define GRID_EDIT_SNAPSHOT_CHUNK_MAX_BYTES 8192u
#define GRID_EDIT_SNAPSHOT_CHUNK_WIRE_HEADER_SIZE (1u + 1u + 8u + 4u + 2u)
#define GRID_EDIT_SNAPSHOT_CHUNK_WIRE_MAX \
	(GRID_EDIT_SNAPSHOT_CHUNK_WIRE_HEADER_SIZE + GRID_EDIT_SNAPSHOT_CHUNK_MAX_BYTES)

typedef enum grid_edit_snapshot_kind {
	GRID_EDIT_SNAPSHOT_OFFER = 1,
	GRID_EDIT_SNAPSHOT_CHUNK = 2,
	GRID_EDIT_SNAPSHOT_ACK = 3
} grid_edit_snapshot_kind_e;

typedef struct grid_edit_snapshot_offer {
	grid_edit_wire_identity_t identity;
	u64 revision;
	u64 host_sequence;
	u32 size;
	u8 sha256[SHA256_DIGEST_SIZE];
} grid_edit_snapshot_offer_t;

typedef struct grid_edit_snapshot_chunk {
	grid_edit_wire_identity_t identity;
	u32 offset;
	u16 size;
	const u8 *bytes; /* Borrowed from the input frame after decoding. */
} grid_edit_snapshot_chunk_t;

/* Payload includes the subtype. Each reader consumes one exact payload.
 * Failed calls preserve both cursor and output. The caller checks connection
 * authorization, expected snapshot size, ordering, and reassembled digest. */
s32 gridEditSnapshotOfferWrite(struct netbuf *dst, grid_edit_snapshot_kind_e kind,
	const grid_edit_snapshot_offer_t *offer);
s32 gridEditSnapshotOfferRead(struct netbuf *src, grid_edit_snapshot_kind_e kind,
	grid_edit_snapshot_offer_t *offer);
s32 gridEditSnapshotChunkWrite(struct netbuf *dst, const grid_edit_snapshot_chunk_t *chunk);
s32 gridEditSnapshotChunkRead(struct netbuf *src, grid_edit_snapshot_chunk_t *chunk);
s32 gridEditSnapshotOfferMatches(const grid_edit_snapshot_offer_t *a,
	const grid_edit_snapshot_offer_t *b);

#ifdef __cplusplus
}
#endif

#endif
