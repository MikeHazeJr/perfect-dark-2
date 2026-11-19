#include <string.h>

#include "forge_level_doc.h"
#include "net/grid_edit_snapshot_wire.h"
#include "net/netbuf.h"
#include "platform.h"

_Static_assert(GRID_EDIT_SNAPSHOT_CHUNK_MAX_BYTES <= 0xffffu,
	"Grid snapshot chunk size must fit its wire length");

static s32 validCursor(const struct netbuf *buf)
{
	return buf && buf->data && !buf->error && buf->rp <= buf->wp && buf->wp <= buf->size;
}

static s32 validIdentity(const grid_edit_wire_identity_t *identity)
{
	return identity->room_id != 0xff && identity->generation != 0;
}

static s32 validOfferKind(grid_edit_snapshot_kind_e kind)
{
	return kind == GRID_EDIT_SNAPSHOT_OFFER || kind == GRID_EDIT_SNAPSHOT_ACK;
}

static s32 validOffer(const grid_edit_snapshot_offer_t *offer)
{
	static const u8 empty_sha[SHA256_DIGEST_SIZE] = {0};
	return offer && validIdentity(&offer->identity) && offer->revision
		&& offer->size && offer->size <= FORGE_LEVEL_CHECKPOINT_MAX_BYTES
		&& memcmp(offer->sha256, empty_sha, sizeof(empty_sha)) != 0;
}

static s32 validChunk(const grid_edit_snapshot_chunk_t *chunk)
{
	return chunk && validIdentity(&chunk->identity) && chunk->bytes
		&& chunk->size && chunk->size <= GRID_EDIT_SNAPSHOT_CHUNK_MAX_BYTES
		&& chunk->offset < FORGE_LEVEL_CHECKPOINT_MAX_BYTES
		&& chunk->size <= FORGE_LEVEL_CHECKPOINT_MAX_BYTES - chunk->offset;
}

static void writePrefix(struct netbuf *wire, grid_edit_snapshot_kind_e kind,
	const grid_edit_wire_identity_t *identity)
{
	const u64 generation = PD_LE64(identity->generation);
	netbufWriteU8(wire, (u8)kind);
	netbufWriteU8(wire, identity->room_id);
	netbufWriteData(wire, &generation, sizeof(generation));
}

static void readIdentity(struct netbuf *wire, grid_edit_wire_identity_t *identity)
{
	identity->room_id = netbufReadU8(wire);
	identity->generation = netbufReadU64(wire);
}

s32 gridEditSnapshotOfferWrite(struct netbuf *dst, grid_edit_snapshot_kind_e kind,
	const grid_edit_snapshot_offer_t *offer)
{
	if (!validCursor(dst) || !validOfferKind(kind) || !validOffer(offer)
			|| GRID_EDIT_SNAPSHOT_OFFER_WIRE_SIZE > dst->size - dst->wp) return 0;

	u8 bytes[GRID_EDIT_SNAPSHOT_OFFER_WIRE_SIZE];
	struct netbuf wire = { .data = bytes, .size = sizeof(bytes) };
	const u64 revision = PD_LE64(offer->revision);
	const u64 host_sequence = PD_LE64(offer->host_sequence);
	const u32 size = PD_LE32(offer->size);
	writePrefix(&wire, kind, &offer->identity);
	netbufWriteData(&wire, &revision, sizeof(revision));
	netbufWriteData(&wire, &host_sequence, sizeof(host_sequence));
	netbufWriteData(&wire, &size, sizeof(size));
	netbufWriteData(&wire, offer->sha256, sizeof(offer->sha256));
	return !wire.error && wire.wp == sizeof(bytes)
		&& netbufWriteData(dst, bytes, sizeof(bytes)) == sizeof(bytes);
}

s32 gridEditSnapshotOfferRead(struct netbuf *src, grid_edit_snapshot_kind_e kind,
	grid_edit_snapshot_offer_t *offer)
{
	if (!validCursor(src) || !validOfferKind(kind) || !offer
			|| src->wp - src->rp != GRID_EDIT_SNAPSHOT_OFFER_WIRE_SIZE) return 0;

	struct netbuf wire = *src;
	if (netbufReadU8(&wire) != kind) return 0;
	grid_edit_snapshot_offer_t candidate = {0};
	readIdentity(&wire, &candidate.identity);
	candidate.revision = netbufReadU64(&wire);
	candidate.host_sequence = netbufReadU64(&wire);
	candidate.size = netbufReadU32(&wire);
	netbufReadData(&wire, candidate.sha256, sizeof(candidate.sha256));
	if (wire.error || wire.rp != wire.wp || !validOffer(&candidate)) return 0;

	*offer = candidate;
	src->rp = wire.rp;
	return 1;
}

s32 gridEditSnapshotChunkWrite(struct netbuf *dst, const grid_edit_snapshot_chunk_t *chunk)
{
	if (!validCursor(dst) || !validChunk(chunk)) return 0;
	const u32 frame_size = GRID_EDIT_SNAPSHOT_CHUNK_WIRE_HEADER_SIZE + chunk->size;
	if (frame_size > dst->size - dst->wp) return 0;

	u8 header[GRID_EDIT_SNAPSHOT_CHUNK_WIRE_HEADER_SIZE];
	struct netbuf wire = { .data = header, .size = sizeof(header) };
	const u32 offset = PD_LE32(chunk->offset);
	const u16 size = PD_LE16(chunk->size);
	writePrefix(&wire, GRID_EDIT_SNAPSHOT_CHUNK, &chunk->identity);
	netbufWriteData(&wire, &offset, sizeof(offset));
	netbufWriteData(&wire, &size, sizeof(size));
	if (wire.error || wire.wp != sizeof(header)) return 0;

	/* The borrowed bytes may come from an overlapping frame. */
	u8 *frame = dst->data + dst->wp;
	memmove(frame + sizeof(header), chunk->bytes, chunk->size);
	memcpy(frame, header, sizeof(header));
	dst->wp += frame_size;
	return 1;
}

s32 gridEditSnapshotChunkRead(struct netbuf *src, grid_edit_snapshot_chunk_t *chunk)
{
	if (!validCursor(src) || !chunk
			|| src->wp - src->rp <= GRID_EDIT_SNAPSHOT_CHUNK_WIRE_HEADER_SIZE
			|| src->wp - src->rp > GRID_EDIT_SNAPSHOT_CHUNK_WIRE_MAX) return 0;

	struct netbuf wire = *src;
	if (netbufReadU8(&wire) != GRID_EDIT_SNAPSHOT_CHUNK) return 0;
	grid_edit_snapshot_chunk_t candidate = {0};
	readIdentity(&wire, &candidate.identity);
	candidate.offset = netbufReadU32(&wire);
	candidate.size = netbufReadU16(&wire);
	candidate.bytes = wire.data + wire.rp;
	if (wire.error || !validChunk(&candidate) || candidate.size != wire.wp - wire.rp) return 0;

	*chunk = candidate;
	src->rp = wire.wp;
	return 1;
}

s32 gridEditSnapshotOfferMatches(const grid_edit_snapshot_offer_t *a,
	const grid_edit_snapshot_offer_t *b)
{
	return a && b && a->identity.room_id == b->identity.room_id
		&& a->identity.generation == b->identity.generation
		&& a->revision == b->revision && a->host_sequence == b->host_sequence
		&& a->size == b->size && memcmp(a->sha256, b->sha256, sizeof(a->sha256)) == 0;
}
