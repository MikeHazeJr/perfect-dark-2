/**
 * player_identity.h -- B-1064 exact typed player identity planning.
 *
 * Player setup must validate the catalog identity before publishing any
 * runtime body/head state.  This API is deliberately a prepare-only seam:
 * it never substitutes a numeric cache slot or a default identity when a
 * typed ID is missing or inconsistent.
 */

#ifndef PD_PLAYER_IDENTITY_H
#define PD_PLAYER_IDENTITY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "assetcatalog.h"

typedef enum player_identity_status_e {
	PLAYER_IDENTITY_OK = 0,
	PLAYER_IDENTITY_INVALID_ARGUMENT,
	PLAYER_IDENTITY_EMPTY_BODY_ID,
	PLAYER_IDENTITY_EMPTY_HEAD_ID,
	PLAYER_IDENTITY_UNTERMINATED_BODY_ID,
	PLAYER_IDENTITY_UNTERMINATED_HEAD_ID,
	PLAYER_IDENTITY_MISSING_BODY_ENTRY,
	PLAYER_IDENTITY_MISSING_HEAD_ENTRY,
	PLAYER_IDENTITY_DISABLED_BODY_ENTRY,
	PLAYER_IDENTITY_DISABLED_HEAD_ENTRY,
	PLAYER_IDENTITY_WRONG_BODY_TYPE,
	PLAYER_IDENTITY_WRONG_HEAD_TYPE,
	PLAYER_IDENTITY_UNBOUND_BODY_RUNTIME_INDEX,
	PLAYER_IDENTITY_UNBOUND_HEAD_RUNTIME_INDEX,
	PLAYER_IDENTITY_INCONSISTENT_BODY_BINDING,
	PLAYER_IDENTITY_INCONSISTENT_HEAD_BINDING,
	PLAYER_IDENTITY_BODY_ID_ENTRY_MISMATCH,
	PLAYER_IDENTITY_HEAD_ID_ENTRY_MISMATCH,
} player_identity_status_e;

/*
 * A prepared identity is valid only when status == PLAYER_IDENTITY_OK.
 * The mp_* fields are compatibility metadata for legacy selectors.  They
 * are never used to recover an identity and are -1 when the catalog entry is
 * not representable as a u8 selector index.
 */
typedef struct player_identity_plan_t {
	char body_id[CATALOG_ID_LEN];
	char head_id[CATALOG_ID_LEN];
	s32 runtime_bodynum;
	s32 runtime_headnum;
	s32 mp_body_index;
	s32 mp_head_index;
	player_identity_status_e status;
} player_identity_plan_t;

/* Establish the deterministic invalid state used before every prepare. */
void playerIdentityPlanReset(player_identity_plan_t *plan);

/*
 * Validate already-resolved entries without catalog or runtime side effects.
 * The entries must correspond exactly to body_id and head_id.
 */
player_identity_status_e playerIdentityPrepareResolved(
	const char *body_id,
	const char *head_id,
	const asset_entry_t *body_entry,
	const asset_entry_t *head_entry,
	player_identity_plan_t *out_plan);

/* Resolve both typed IDs, then apply the same pure validation contract. */
player_identity_status_e playerIdentityPrepare(
	const char *body_id,
	const char *head_id,
	player_identity_plan_t *out_plan);

/* Stable machine-readable status text for logs and test receipts. */
const char *playerIdentityStatusString(player_identity_status_e status);

#ifdef __cplusplus
}
#endif

#endif /* PD_PLAYER_IDENTITY_H */
