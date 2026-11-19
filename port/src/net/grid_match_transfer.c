#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "net/grid_match_transfer.h"
#include "net/net.h"
#include "net/netmanifest.h"
#include "net/transfer_buffer.h"

#define GRID_MATCH_CHUNK_WIRE_VERSION 1

_Static_assert(NET_DISTRIB_CHUNK_SIZE <= UINT16_MAX, "Grid chunk size must fit its wire length");

struct grid_match_transfer {
	grid_match_offer_t offer;
	u16 package_index;
	u32 package_size;
	u8 *bytes;
	u32 capacity;
	u32 received;
};

static s32 offerValid(const grid_match_offer_t *offer)
{
	return offer && offer->room_id != 0xff && offer->generation
		&& offer->manifest_digest[SHA256_HEX_SIZE - 1] == '\0'
		&& strspn(offer->manifest_digest, "0123456789abcdef") == SHA256_HEX_SIZE - 1
		&& strspn(offer->manifest_digest, "0") != SHA256_HEX_SIZE - 1;
}

static s32 chunkValid(const grid_match_chunk_t *chunk)
{
	return chunk && offerValid(&chunk->offer) && chunk->package_index < MANIFEST_MAX_ENTRIES
		&& chunk->data && chunk->size && chunk->size <= NET_DISTRIB_CHUNK_SIZE
		&& chunk->offset < NET_DISTRIB_MAX_COMP
		&& chunk->size <= NET_DISTRIB_MAX_COMP - chunk->offset;
}

s32 gridMatchChunkWrite(struct netbuf *dst, const grid_match_chunk_t *chunk)
{
	if (!dst || !dst->data || dst->error || dst->wp > dst->size
			|| !chunkValid(chunk)
			|| dst->size - dst->wp < GRID_MATCH_CHUNK_WIRE_HEADER_SIZE + chunk->size) return 0;

	/* Keep the numeric writes aligned even if the payload follows an opcode. */
	_Alignas(u64) u8 header[GRID_MATCH_CHUNK_WIRE_HEADER_SIZE];
	struct netbuf wire = { .data = header, .size = sizeof(header) };
	netbufWriteU64(&wire, chunk->offer.generation);
	netbufWriteU32(&wire, chunk->offset);
	netbufWriteU16(&wire, chunk->package_index);
	netbufWriteU16(&wire, chunk->size);
	netbufWriteU8(&wire, GRID_MATCH_CHUNK_WIRE_VERSION);
	netbufWriteU8(&wire, chunk->offer.room_id);
	netbufWriteData(&wire, chunk->offer.manifest_digest, SHA256_HEX_SIZE - 1);
	if (wire.error || wire.wp != sizeof(header)) return 0;

	/* A borrowed packet payload may overlap the destination frame. */
	u8 *frame = dst->data + dst->wp;
	memmove(frame + sizeof(header), chunk->data, chunk->size);
	memcpy(frame, header, sizeof(header));
	dst->wp += sizeof(header) + chunk->size;
	return 1;
}

s32 gridMatchChunkRead(struct netbuf *src, grid_match_chunk_t *chunk)
{
	if (!src || !src->data || src->error || src->rp > src->wp || src->wp > src->size
			|| !chunk || src->wp - src->rp < GRID_MATCH_CHUNK_WIRE_HEADER_SIZE) return 0;

	struct netbuf wire = *src;
	grid_match_chunk_t candidate = {0};
	candidate.offer.generation = netbufReadU64(&wire);
	candidate.offset = netbufReadU32(&wire);
	candidate.package_index = netbufReadU16(&wire);
	candidate.size = netbufReadU16(&wire);
	const u8 version = netbufReadU8(&wire);
	candidate.offer.room_id = netbufReadU8(&wire);
	netbufReadData(&wire, candidate.offer.manifest_digest, SHA256_HEX_SIZE - 1);
	candidate.data = wire.data + wire.rp;
	if (wire.error || version != GRID_MATCH_CHUNK_WIRE_VERSION || !chunkValid(&candidate)
			|| wire.wp - wire.rp < candidate.size) return 0;

	wire.rp += candidate.size;
	*chunk = candidate;
	*src = wire;
	return 1;
}

s32 gridMatchTransferBegin(grid_match_transfer_t **transfer, const grid_match_offer_t *offer,
	u16 package_index, u32 package_size)
{
	if (!transfer || *transfer || !offerValid(offer) || package_index >= MANIFEST_MAX_ENTRIES
			|| !package_size || package_size > NET_DISTRIB_MAX_COMP) return 0;

	grid_match_transfer_t *candidate = calloc(1, sizeof(*candidate));
	if (!candidate) return 0;
	candidate->offer = *offer;
	candidate->package_index = package_index;
	candidate->package_size = package_size;
	*transfer = candidate;
	return 1;
}

static s32 transferMatches(const grid_match_transfer_t *transfer, const grid_match_offer_t *offer,
	u16 package_index)
{
	return transfer && offer && transfer->package_index == package_index
		&& transfer->offer.room_id == offer->room_id
		&& transfer->offer.generation == offer->generation
		&& memcmp(transfer->offer.manifest_digest, offer->manifest_digest, SHA256_HEX_SIZE) == 0;
}

s32 gridMatchTransferAppend(grid_match_transfer_t *transfer, const grid_match_chunk_t *chunk)
{
	if (!chunkValid(chunk) || !transferMatches(transfer, &chunk->offer, chunk->package_index)
			|| chunk->offset > transfer->received) return 0;

	if (chunk->offset < transfer->received) {
		return chunk->size <= transfer->received - chunk->offset
			&& memcmp(transfer->bytes + chunk->offset, chunk->data, chunk->size) == 0;
	}
	return netTransferBufferAppend(&transfer->bytes, &transfer->capacity, &transfer->received,
		chunk->data, chunk->size, transfer->package_size, NULL);
}

const u8 *gridMatchTransferComplete(const grid_match_transfer_t *transfer,
	const grid_match_offer_t *offer, u16 package_index, u32 *out_size)
{
	if (out_size) *out_size = 0;
	if (!transferMatches(transfer, offer, package_index)
			|| transfer->received != transfer->package_size) return NULL;
	if (out_size) *out_size = transfer->received;
	return transfer->bytes;
}

void gridMatchTransferReset(grid_match_transfer_t **transfer)
{
	if (!transfer || !*transfer) return;
	free((*transfer)->bytes);
	free(*transfer);
	*transfer = NULL;
}
