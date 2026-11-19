#include <string.h>

#include "net/grid_edit_room_wire.h"
#include "net/net.h"
#include "net/netbuf.h"
#include "platform.h"

enum {
	CONTROL_PREFIX_SIZE = 1 + 1 + 8,
	CONTROL_REVIEW_CAPABILITIES_SIZE = CONTROL_PREFIX_SIZE + 1,
	CONTROL_INVITE_FIXED_SIZE = CONTROL_PREFIX_SIZE + 1 + SHA256_HEX_SIZE - 1 + 2 * 8,
	CONTROL_GRANT_ACTOR_SIZE = 8,
	CONTROL_REFUSAL_SIZE = CONTROL_PREFIX_SIZE + 2 * 8 + 1,
	CONTROL_REVIEW_FIXED_SIZE = CONTROL_PREFIX_SIZE + 2 * 8 + 4 + 1 + 2 + 1
		+ 2 * SHA256_DIGEST_SIZE,
	CONTROL_APPLY_SUFFIX_SIZE = 2 * 8 + SHA256_DIGEST_SIZE + 1,
};

_Static_assert(CATALOG_ID_LEN - 1 <= 0xff, "Grid arena ID must fit its wire length");
_Static_assert(GRID_EDIT_ROOM_CONTROL_LEGACY_WIRE_MAX == CONTROL_INVITE_FIXED_SIZE
	+ (CATALOG_ID_LEN - 1) + CONTROL_GRANT_ACTOR_SIZE, "Grid room control wire bound must fit a grant");
_Static_assert(GRID_EDIT_ROOM_CONTROL_LEGACY_WIRE_MAX == CONTROL_REVIEW_FIXED_SIZE
	+ (CATALOG_ID_LEN - 1) + 1, "Grid legacy review reply must keep the v65 bound");
_Static_assert(GRID_EDIT_ROOM_CONTROL_DEPENDENCY_WIRE_MAX == CONTROL_REVIEW_FIXED_SIZE + (CATALOG_ID_LEN - 1)
	+ 1 + SHA256_DIGEST_SIZE, "Grid dependency review proof must fit the negotiated bound");
_Static_assert(GRID_EDIT_ROOM_CONTROL_TRANSFER_WIRE_MAX == CONTROL_REVIEW_FIXED_SIZE + (CATALOG_ID_LEN - 1)
	+ SHA256_DIGEST_SIZE + sizeof(u32), "Grid transfer offer must fit the negotiated bound");
_Static_assert(GRID_EDIT_ROOM_CONTROL_APPLY_WIRE_MAX == CONTROL_REVIEW_FIXED_SIZE + (CATALOG_ID_LEN - 1)
	+ SHA256_DIGEST_SIZE + CONTROL_APPLY_SUFFIX_SIZE, "Grid Apply must fit the negotiated bound");
_Static_assert(GRID_EDIT_REVIEW_CHUNK_WIRE_MAX == sizeof(u64) + GRID_MATCH_CHUNK_WIRE_HEADER_SIZE
	+ NET_DISTRIB_CHUNK_SIZE, "Grid review chunks must use the raw package transfer size");

static s32 carriesInvite(grid_edit_room_control_kind_e kind)
{
	return kind == GRID_EDIT_ROOM_CONTROL_INVITE || kind == GRID_EDIT_ROOM_CONTROL_CONFIRM
		|| kind == GRID_EDIT_ROOM_CONTROL_GRANT;
}

static s32 carriesReviewTransfer(grid_edit_room_control_kind_e kind)
{
	return kind == GRID_EDIT_ROOM_CONTROL_REVIEW_TRANSFER_OFFER
		|| kind == GRID_EDIT_ROOM_CONTROL_REVIEW_TRANSFER_REQUEST;
}

static s32 carriesApply(grid_edit_room_control_kind_e kind)
{
	return kind == GRID_EDIT_ROOM_CONTROL_APPLY_PREPARE || kind == GRID_EDIT_ROOM_CONTROL_APPLY_REPLY
		|| kind == GRID_EDIT_ROOM_CONTROL_APPLY_OUTCOME || kind == GRID_EDIT_ROOM_CONTROL_APPLY_BASELINE;
}

static s32 carriesReview(grid_edit_room_control_kind_e kind)
{
	return kind == GRID_EDIT_ROOM_CONTROL_REVIEW_OFFER || kind == GRID_EDIT_ROOM_CONTROL_REVIEW_REPLY
		|| kind == GRID_EDIT_ROOM_CONTROL_REVIEW_CANCEL || kind == GRID_EDIT_ROOM_CONTROL_REVIEW_DEPS_OFFER
		|| kind == GRID_EDIT_ROOM_CONTROL_REVIEW_DEPS_REPLY
		|| kind == GRID_EDIT_ROOM_CONTROL_REVIEW_PREPARED_OFFER || carriesReviewTransfer(kind) || carriesApply(kind);
}

static s32 carriesReviewReply(grid_edit_room_control_kind_e kind)
{
	return kind == GRID_EDIT_ROOM_CONTROL_REVIEW_REPLY || kind == GRID_EDIT_ROOM_CONTROL_REVIEW_DEPS_REPLY;
}

static s32 carriesReviewDigest(const grid_edit_room_control_t *control)
{
	return control->kind == GRID_EDIT_ROOM_CONTROL_REVIEW_DEPS_OFFER
		|| control->kind == GRID_EDIT_ROOM_CONTROL_REVIEW_PREPARED_OFFER
		|| carriesReviewTransfer(control->kind) || carriesApply(control->kind)
		|| (control->kind == GRID_EDIT_ROOM_CONTROL_REVIEW_DEPS_REPLY
			&& control->review_reply == GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED);
}

s32 gridEditVariantReviewMatches(const grid_edit_variant_review_t *a, const grid_edit_variant_review_t *b)
{
	return a && b && a->offer_serial == b->offer_serial && a->draft_revision == b->draft_revision
		&& a->stage_epoch == b->stage_epoch && a->edited_room == b->edited_room && a->extension == b->extension
		&& strncmp(a->arena_id, b->arena_id, CATALOG_ID_LEN) == 0
		&& memcmp(a->arena_sha256, b->arena_sha256, SHA256_DIGEST_SIZE) == 0
		&& memcmp(a->parent_sha256, b->parent_sha256, SHA256_DIGEST_SIZE) == 0;
}

s32 gridEditLayoutApplyMatches(const grid_edit_room_control_t *a, const grid_edit_room_control_t *b)
{
	return a && b && carriesApply(a->kind) && carriesApply(b->kind)
		&& a->invite.identity.room_id == b->invite.identity.room_id
		&& a->invite.identity.generation == b->invite.identity.generation
		&& gridEditVariantReviewMatches(&a->review, &b->review)
		&& memcmp(a->review_deps_sha256, b->review_deps_sha256, SHA256_DIGEST_SIZE) == 0
		&& a->apply_previous_layout_generation == b->apply_previous_layout_generation
		&& a->apply_layout_generation == b->apply_layout_generation
		&& memcmp(a->apply_manifest_sha256, b->apply_manifest_sha256, SHA256_DIGEST_SIZE) == 0;
}

static s32 applyResultMatchesKind(const grid_edit_room_control_t *control)
{
	switch (control->kind) {
	case GRID_EDIT_ROOM_CONTROL_APPLY_PREPARE:
	case GRID_EDIT_ROOM_CONTROL_APPLY_BASELINE:
		return control->apply_result == 0;
	case GRID_EDIT_ROOM_CONTROL_APPLY_REPLY:
		return control->apply_result == GRID_EDIT_LAYOUT_APPLY_READY
			|| control->apply_result == GRID_EDIT_LAYOUT_APPLY_REFUSED;
	case GRID_EDIT_ROOM_CONTROL_APPLY_OUTCOME:
		return control->apply_result == GRID_EDIT_LAYOUT_APPLY_COMMITTED
			|| control->apply_result == GRID_EDIT_LAYOUT_APPLY_ABORTED
			|| control->apply_result == GRID_EDIT_LAYOUT_APPLY_ROLLED_BACK;
	default:
		return 0;
	}
}

static s32 knownRefusal(grid_edit_result_t result)
{
	switch (result) {
	case GRID_EDIT_DENIED:
	case GRID_EDIT_INVALID:
	case GRID_EDIT_STALE:
	case GRID_EDIT_OUT_OF_ORDER:
	case GRID_EDIT_SEQUENCE_REUSED:
	case GRID_EDIT_RESOURCE_EXHAUSTED:
	case GRID_EDIT_RESOURCE_LEASED:
		return 1;
	default:
		return 0;
	}
}

static u32 arenaIdLength(const char id[CATALOG_ID_LEN])
{
	const char *end = memchr(id, '\0', CATALOG_ID_LEN);
	if (!end || strspn(id, "abcdefghijklmnopqrstuvwxyz0123456789:_.-")
			!= (size_t)(end - id)) return 0;
	const char *separator = strchr(id, ':');
	if (!separator || separator == id || !separator[1] || strchr(separator + 1, ':')) return 0;
	return (u32)(end - id);
}

static u32 controlSize(const grid_edit_room_control_t *control)
{
	if (!control || control->invite.identity.room_id == 0xff
			|| !control->invite.identity.generation) return 0;
	const u8 zero_sha[SHA256_DIGEST_SIZE] = {0};
	const s32 has_digest = memcmp(control->review_deps_sha256, zero_sha, sizeof(zero_sha)) != 0;
	if (has_digest != carriesReviewDigest(control)) return 0;
	const s32 has_transfer = carriesReviewTransfer(control->kind);
	if (has_transfer != (control->review_arena_size != 0) || control->review_arena_size > NET_DISTRIB_MAX_COMP) return 0;
	const s32 has_apply = carriesApply(control->kind);
	const s32 has_manifest = memcmp(control->apply_manifest_sha256, zero_sha, sizeof(zero_sha)) != 0;
	if (has_apply) {
		if (!has_manifest || control->apply_layout_generation <= control->apply_previous_layout_generation
				|| !applyResultMatchesKind(control)) return 0;
	} else if (has_manifest || control->apply_previous_layout_generation
			|| control->apply_layout_generation || control->apply_result) {
		return 0;
	}
	if (control->kind == GRID_EDIT_ROOM_CONTROL_REVIEW_CAPABILITIES) {
		/* Advertise each capability separately so v65 V1 readers still receive 0x01. */
		const u8 cap = control->review_capabilities;
		return cap == GRID_EDIT_REVIEW_CAP_DEPS_V1 || cap == GRID_EDIT_REVIEW_CAP_PREPARED
			|| cap == GRID_EDIT_REVIEW_CAP_TRANSFER || cap == GRID_EDIT_REVIEW_CAP_APPLY
			|| cap == GRID_EDIT_REVIEW_CAP_APPLY_BASELINE
			|| cap == GRID_EDIT_REVIEW_CAP_INTERACTIVE_VIEW
			? CONTROL_REVIEW_CAPABILITIES_SIZE : 0;
	}
	if (control->review_capabilities) return 0;
	if (carriesInvite(control->kind)) {
		const grid_edit_room_invite_t *invite = &control->invite;
		const u32 length = arenaIdLength(invite->arena_id);
		const s32 is_grant = control->kind == GRID_EDIT_ROOM_CONTROL_GRANT;
		if (!length || invite->arena_sha256[SHA256_HEX_SIZE - 1] != '\0'
				|| strspn(invite->arena_sha256, "0123456789abcdef") != SHA256_HEX_SIZE - 1
				|| strspn(invite->arena_sha256, "0") == SHA256_HEX_SIZE - 1
				|| invite->baseline_revision != 1 || !invite->host_actor
				|| (is_grant && (!control->granted_actor || control->granted_actor == invite->host_actor))) {
			return 0;
		}
		return CONTROL_INVITE_FIXED_SIZE + length + (is_grant ? CONTROL_GRANT_ACTOR_SIZE : 0);
	}
	if (carriesReview(control->kind)) {
		const grid_edit_variant_review_t *review = &control->review;
		const u32 length = arenaIdLength(review->arena_id);
		const s32 is_reply = carriesReviewReply(control->kind);
		const grid_edit_variant_review_reply_e max_reply = control->kind == GRID_EDIT_ROOM_CONTROL_REVIEW_DEPS_REPLY
			? GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED : GRID_EDIT_VARIANT_REVIEW_SOURCE_VERIFIED;
		if (!length || !review->offer_serial || !review->draft_revision || !review->stage_epoch
				|| (review->edited_room != 1 && review->edited_room != 2) || !review->extension
				|| memcmp(review->arena_sha256, zero_sha, sizeof(zero_sha)) == 0
				|| memcmp(review->parent_sha256, zero_sha, sizeof(zero_sha)) == 0) return 0;
		if ((is_reply && (control->review_reply < GRID_EDIT_VARIANT_REVIEW_DECLINED
					|| control->review_reply > max_reply))
				|| (!is_reply && control->review_reply != 0)) return 0;
		return CONTROL_REVIEW_FIXED_SIZE + length + is_reply + (has_digest ? SHA256_DIGEST_SIZE : 0)
			+ (has_transfer ? sizeof(u32) : 0) + (has_apply ? CONTROL_APPLY_SUFFIX_SIZE : 0);
	}
	if (control->kind == GRID_EDIT_ROOM_CONTROL_CLOSE
			|| control->kind == GRID_EDIT_ROOM_CONTROL_REVIEW_SUPPORTED) return CONTROL_PREFIX_SIZE;
	if (control->kind == GRID_EDIT_ROOM_CONTROL_REFUSAL && control->requested_sequence
			&& control->expected_revision && knownRefusal(control->result)) return CONTROL_REFUSAL_SIZE;
	return 0;
}

static void writeU64(struct netbuf *dst, u64 value)
{
	const u64 encoded = PD_LE64(value);
	netbufWriteData(dst, &encoded, sizeof(encoded));
}

s32 gridEditRoomControlWrite(struct netbuf *dst, const grid_edit_room_control_t *control)
{
	if (!dst || !dst->data || dst->error || dst->rp > dst->wp || dst->wp > dst->size) return 0;
	const u32 size = controlSize(control);
	if (!size || size > dst->size - dst->wp) return 0;

	u8 bytes[GRID_EDIT_ROOM_CONTROL_WIRE_MAX];
	struct netbuf wire = { .data = bytes, .size = sizeof(bytes) };
	const grid_edit_room_invite_t *invite = &control->invite;
	netbufWriteU8(&wire, (u8)control->kind);
	netbufWriteU8(&wire, invite->identity.room_id);
	writeU64(&wire, invite->identity.generation);
	if (carriesInvite(control->kind)) {
		const u8 length = (u8)strlen(invite->arena_id);
		netbufWriteU8(&wire, length);
		netbufWriteData(&wire, invite->arena_id, length);
		netbufWriteData(&wire, invite->arena_sha256, SHA256_HEX_SIZE - 1);
		writeU64(&wire, invite->baseline_revision);
		writeU64(&wire, invite->host_actor);
		if (control->kind == GRID_EDIT_ROOM_CONTROL_GRANT) writeU64(&wire, control->granted_actor);
	} else if (control->kind == GRID_EDIT_ROOM_CONTROL_REVIEW_CAPABILITIES) {
		netbufWriteU8(&wire, control->review_capabilities);
	} else if (control->kind == GRID_EDIT_ROOM_CONTROL_REFUSAL) {
		writeU64(&wire, control->requested_sequence);
		writeU64(&wire, control->expected_revision);
		netbufWriteU8(&wire, (u8)-(s32)control->result);
	} else if (carriesReview(control->kind)) {
		const grid_edit_variant_review_t *review = &control->review;
		const u32 epoch = PD_LE32(review->stage_epoch);
		const u16 extension = PD_LE16(review->extension);
		const u8 length = (u8)strlen(review->arena_id);
		writeU64(&wire, review->offer_serial);
		writeU64(&wire, review->draft_revision);
		netbufWriteData(&wire, &epoch, sizeof(epoch));
		netbufWriteU8(&wire, review->edited_room);
		netbufWriteData(&wire, &extension, sizeof(extension));
		netbufWriteU8(&wire, length);
		netbufWriteData(&wire, review->arena_id, length);
		netbufWriteData(&wire, review->arena_sha256, sizeof(review->arena_sha256));
		netbufWriteData(&wire, review->parent_sha256, sizeof(review->parent_sha256));
		if (carriesReviewReply(control->kind)) netbufWriteU8(&wire, (u8)control->review_reply);
		if (carriesReviewDigest(control)) {
			netbufWriteData(&wire, control->review_deps_sha256, sizeof(control->review_deps_sha256));
		}
		if (carriesReviewTransfer(control->kind)) {
			const u32 arena_size = PD_LE32(control->review_arena_size);
			netbufWriteData(&wire, &arena_size, sizeof(arena_size));
		}
		if (carriesApply(control->kind)) {
			writeU64(&wire, control->apply_previous_layout_generation);
			writeU64(&wire, control->apply_layout_generation);
			netbufWriteData(&wire, control->apply_manifest_sha256, sizeof(control->apply_manifest_sha256));
			netbufWriteU8(&wire, (u8)control->apply_result);
		}
	}
	return !wire.error && wire.wp == size && netbufWriteData(dst, bytes, size) == size;
}

s32 gridEditRoomControlRead(struct netbuf *src, grid_edit_room_control_t *control)
{
	if (!src || !src->data || src->error || src->rp > src->wp || src->wp > src->size || !control) return 0;
	const u32 size = src->wp - src->rp;
	if (size < CONTROL_PREFIX_SIZE || size > GRID_EDIT_ROOM_CONTROL_WIRE_MAX) return 0;

	struct netbuf wire = *src;
	grid_edit_room_control_t candidate = {0};
	grid_edit_room_invite_t *invite = &candidate.invite;
	candidate.kind = (grid_edit_room_control_kind_e)netbufReadU8(&wire);
	invite->identity.room_id = netbufReadU8(&wire);
	invite->identity.generation = netbufReadU64(&wire);
	if (carriesInvite(candidate.kind)) {
		const s32 is_grant = candidate.kind == GRID_EDIT_ROOM_CONTROL_GRANT;
		const u32 fixed_size = CONTROL_INVITE_FIXED_SIZE + (is_grant ? CONTROL_GRANT_ACTOR_SIZE : 0);
		if (size <= fixed_size) return 0;
		const u8 length = netbufReadU8(&wire);
		if (!length || length >= sizeof(invite->arena_id) || size != fixed_size + length) return 0;
		netbufReadData(&wire, invite->arena_id, length);
		if (memchr(invite->arena_id, '\0', length)) return 0;
		netbufReadData(&wire, invite->arena_sha256, SHA256_HEX_SIZE - 1);
		invite->baseline_revision = netbufReadU64(&wire);
		invite->host_actor = netbufReadU64(&wire);
		if (is_grant) candidate.granted_actor = netbufReadU64(&wire);
	} else if (candidate.kind == GRID_EDIT_ROOM_CONTROL_REVIEW_CAPABILITIES) {
		if (size != CONTROL_REVIEW_CAPABILITIES_SIZE) return 0;
		candidate.review_capabilities = netbufReadU8(&wire);
	} else if (candidate.kind == GRID_EDIT_ROOM_CONTROL_REFUSAL) {
		if (size != CONTROL_REFUSAL_SIZE) return 0;
		candidate.requested_sequence = netbufReadU64(&wire);
		candidate.expected_revision = netbufReadU64(&wire);
		candidate.result = (grid_edit_result_t)-(s32)netbufReadU8(&wire);
	} else if (carriesReview(candidate.kind)) {
		grid_edit_variant_review_t *review = &candidate.review;
		const s32 is_reply = carriesReviewReply(candidate.kind);
		const u32 fixed_size = CONTROL_REVIEW_FIXED_SIZE + is_reply;
		if (size <= fixed_size) return 0;
		review->offer_serial = netbufReadU64(&wire);
		review->draft_revision = netbufReadU64(&wire);
		review->stage_epoch = netbufReadU32(&wire);
		review->edited_room = netbufReadU8(&wire);
		review->extension = netbufReadU16(&wire);
		const u8 length = netbufReadU8(&wire);
		if (!length || length >= sizeof(review->arena_id) || size < fixed_size + length) return 0;
		netbufReadData(&wire, review->arena_id, length);
		if (memchr(review->arena_id, '\0', length)) return 0;
		netbufReadData(&wire, review->arena_sha256, sizeof(review->arena_sha256));
		netbufReadData(&wire, review->parent_sha256, sizeof(review->parent_sha256));
		if (is_reply) candidate.review_reply = (grid_edit_variant_review_reply_e)netbufReadU8(&wire);
		const s32 has_digest = carriesReviewDigest(&candidate);
		const s32 has_transfer = carriesReviewTransfer(candidate.kind);
		const s32 has_apply = carriesApply(candidate.kind);
		if (size != fixed_size + length + (has_digest ? SHA256_DIGEST_SIZE : 0)
				+ (has_transfer ? sizeof(u32) : 0) + (has_apply ? CONTROL_APPLY_SUFFIX_SIZE : 0)) return 0;
		if (has_digest) netbufReadData(&wire, candidate.review_deps_sha256, sizeof(candidate.review_deps_sha256));
		if (has_transfer) candidate.review_arena_size = netbufReadU32(&wire);
		if (has_apply) {
			candidate.apply_previous_layout_generation = netbufReadU64(&wire);
			candidate.apply_layout_generation = netbufReadU64(&wire);
			netbufReadData(&wire, candidate.apply_manifest_sha256, sizeof(candidate.apply_manifest_sha256));
			candidate.apply_result = (grid_edit_layout_apply_result_e)netbufReadU8(&wire);
		}
	}
	if (wire.error || wire.rp != wire.wp || controlSize(&candidate) != size) return 0;
	*control = candidate;
	src->rp = wire.rp;
	return 1;
}

s32 gridEditReviewChunkWrite(struct netbuf *dst, u64 draft_generation, const grid_match_chunk_t *chunk)
{
	if (!dst || !dst->data || dst->error || dst->rp > dst->wp || dst->wp > dst->size
			|| dst->size - dst->wp < sizeof(draft_generation)
			|| !draft_generation || !chunk || chunk->package_index != 0) return 0;
	struct netbuf wire = *dst;
	wire.wp += sizeof(draft_generation);
	if (!gridMatchChunkWrite(&wire, chunk)) return 0;
	/* Encode the prefix last because a borrowed chunk may overlap it. */
	const u64 encoded = PD_LE64(draft_generation);
	memcpy(dst->data + dst->wp, &encoded, sizeof(encoded));
	dst->wp = wire.wp;
	return 1;
}

s32 gridEditReviewChunkRead(struct netbuf *src, u64 *draft_generation, grid_match_chunk_t *chunk)
{
	if (!src || !src->data || src->error || src->rp > src->wp || src->wp > src->size
			|| src->wp - src->rp < sizeof(u64) || src->wp - src->rp > GRID_EDIT_REVIEW_CHUNK_WIRE_MAX
			|| !draft_generation || !chunk) return 0;
	struct netbuf wire = *src;
	const u64 generation = netbufReadU64(&wire);
	grid_match_chunk_t candidate;
	if (!generation || !gridMatchChunkRead(&wire, &candidate) || wire.rp != wire.wp
			|| candidate.package_index != 0) return 0;
	*draft_generation = generation;
	*chunk = candidate;
	src->rp = wire.rp;
	return 1;
}
