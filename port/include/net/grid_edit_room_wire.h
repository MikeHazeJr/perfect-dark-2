#ifndef PD_GRID_EDIT_ROOM_WIRE_H
#define PD_GRID_EDIT_ROOM_WIRE_H

#include "net/grid_edit_room.h"
#include "net/grid_match_transfer.h"

struct netbuf;

#ifdef __cplusplus
extern "C" {
#endif

/* Each negotiated extension has its own wire bound. */
#define GRID_EDIT_ROOM_CONTROL_LEGACY_WIRE_MAX (1u + 1u + 8u + 1u + (CATALOG_ID_LEN - 1u) \
	+ (SHA256_HEX_SIZE - 1u) + 3u * 8u)
#define GRID_EDIT_ROOM_CONTROL_DEPENDENCY_WIRE_MAX (GRID_EDIT_ROOM_CONTROL_LEGACY_WIRE_MAX + SHA256_DIGEST_SIZE)
#define GRID_EDIT_ROOM_CONTROL_TRANSFER_WIRE_MAX (GRID_EDIT_ROOM_CONTROL_DEPENDENCY_WIRE_MAX - 1u + 4u)
#define GRID_EDIT_ROOM_CONTROL_APPLY_WIRE_MAX (GRID_EDIT_ROOM_CONTROL_DEPENDENCY_WIRE_MAX + 2u * 8u + SHA256_DIGEST_SIZE)
#define GRID_EDIT_ROOM_CONTROL_WIRE_MAX GRID_EDIT_ROOM_CONTROL_APPLY_WIRE_MAX
#define GRID_EDIT_REVIEW_CHUNK_WIRE_MAX (8u + GRID_MATCH_CHUNK_WIRE_HEADER_SIZE + 16u * 1024u)
#define GRID_EDIT_REVIEW_CAP_DEPS_V1 0x01u
#define GRID_EDIT_REVIEW_CAP_PREPARED 0x02u
#define GRID_EDIT_REVIEW_CAP_TRANSFER 0x04u
#define GRID_EDIT_REVIEW_CAP_APPLY 0x08u
#define GRID_EDIT_REVIEW_CAP_APPLY_BASELINE 0x10u
#define GRID_EDIT_REVIEW_CAP_INTERACTIVE_VIEW 0x20u

typedef enum grid_edit_room_control_kind {
	GRID_EDIT_ROOM_CONTROL_INVITE = 1,
	GRID_EDIT_ROOM_CONTROL_CONFIRM,
	GRID_EDIT_ROOM_CONTROL_GRANT,
	GRID_EDIT_ROOM_CONTROL_CLOSE,
	GRID_EDIT_ROOM_CONTROL_REFUSAL,
	GRID_EDIT_ROOM_CONTROL_REVIEW_SUPPORTED,
	GRID_EDIT_ROOM_CONTROL_REVIEW_OFFER,
	GRID_EDIT_ROOM_CONTROL_REVIEW_REPLY,
	GRID_EDIT_ROOM_CONTROL_REVIEW_CANCEL,
	GRID_EDIT_ROOM_CONTROL_REVIEW_CAPABILITIES,
	GRID_EDIT_ROOM_CONTROL_REVIEW_DEPS_OFFER,
	GRID_EDIT_ROOM_CONTROL_REVIEW_DEPS_REPLY,
	/* Both peers retain the verified source cache for the review window. */
	GRID_EDIT_ROOM_CONTROL_REVIEW_PREPARED_OFFER,
	GRID_EDIT_ROOM_CONTROL_REVIEW_TRANSFER_OFFER,
	GRID_EDIT_ROOM_CONTROL_REVIEW_TRANSFER_REQUEST,
	GRID_EDIT_ROOM_CONTROL_APPLY_PREPARE,
	GRID_EDIT_ROOM_CONTROL_APPLY_REPLY,
	GRID_EDIT_ROOM_CONTROL_APPLY_OUTCOME,
	/* Host identity stamp for a freshly granted creator. */
	GRID_EDIT_ROOM_CONTROL_APPLY_BASELINE
} grid_edit_room_control_kind_e;

typedef enum grid_edit_variant_review_reply {
	GRID_EDIT_VARIANT_REVIEW_DECLINED = 1,
	GRID_EDIT_VARIANT_REVIEW_ACKNOWLEDGED = 2,
	GRID_EDIT_VARIANT_REVIEW_SOURCE_VERIFIED = 3, /* Exact local native source checks passed. */
	GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED = 4 /* Independent exact-source dependency digest matched. */
} grid_edit_variant_review_reply_e;

typedef enum grid_edit_layout_apply_result {
	GRID_EDIT_LAYOUT_APPLY_READY = 1,
	GRID_EDIT_LAYOUT_APPLY_REFUSED = 2,
	GRID_EDIT_LAYOUT_APPLY_COMMITTED = 3,
	GRID_EDIT_LAYOUT_APPLY_ABORTED = 4,
	GRID_EDIT_LAYOUT_APPLY_ROLLED_BACK = 5
} grid_edit_layout_apply_result_e;

typedef struct grid_edit_variant_review {
	u64 offer_serial;
	u64 draft_revision;
	u32 stage_epoch;
	u8 edited_room;
	u16 extension;
	char arena_id[CATALOG_ID_LEN];
	u8 arena_sha256[SHA256_DIGEST_SIZE];
	u8 parent_sha256[SHA256_DIGEST_SIZE];
} grid_edit_variant_review_t;

typedef struct grid_edit_room_control {
	grid_edit_room_control_kind_e kind;
	/* All messages carry invite.identity. Only INVITE, CONFIRM and GRANT
	 * carry the remaining invite fields. */
	grid_edit_room_invite_t invite;
	u64 granted_actor;
	u64 requested_sequence;
	u64 expected_revision;
	grid_edit_result_t result;
	/* Offer, reply and cancel carry the exact variant tuple. Only a reply
	 * carries a disposition. Review support carries invite.identity alone. */
	grid_edit_variant_review_t review;
	grid_edit_variant_review_reply_e review_reply;
	u8 review_capabilities;
	u8 review_deps_sha256[SHA256_DIGEST_SIZE];
	u32 review_arena_size;
	/* Apply retains the match epoch in review and advances the layout identity. */
	u64 apply_previous_layout_generation;
	u64 apply_layout_generation;
	u8 apply_manifest_sha256[SHA256_DIGEST_SIZE];
	grid_edit_layout_apply_result_e apply_result;
} grid_edit_room_control_t;

/* Compare the exact review tuple. The caller also checks invite.identity. */
s32 gridEditVariantReviewMatches(const grid_edit_variant_review_t *a, const grid_edit_variant_review_t *b);

/* Compare the exact Apply intent across prepare, reply and outcome. The
 * caller also checks direction, disposition and the live layout generation. */
s32 gridEditLayoutApplyMatches(const grid_edit_room_control_t *a, const grid_edit_room_control_t *b);

/* One complete control payload per packet. The caller enforces message
 * direction, active room, authenticated connection and creator permissions.
 * A malformed payload leaves the cursor and output unchanged. An invalid
 * write leaves the destination bytes and cursor unchanged. */
s32 gridEditRoomControlWrite(struct netbuf *dst, const grid_edit_room_control_t *control);
s32 gridEditRoomControlRead(struct netbuf *src, grid_edit_room_control_t *control);

/* One complete chunk payload after its opcode. Only package index zero is
 * valid. Failure preserves the cursor, outputs and destination bytes. */
s32 gridEditReviewChunkWrite(struct netbuf *dst, u64 draft_generation, const grid_match_chunk_t *chunk);
s32 gridEditReviewChunkRead(struct netbuf *src, u64 *draft_generation, grid_match_chunk_t *chunk);

#ifdef __cplusplus
}
#endif

#endif
