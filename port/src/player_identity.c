/**
 * player_identity.c -- B-1064 exact typed player identity planning.
 *
 * The old player setup path could turn an unresolved typed body/head ID into
 * a cached numeric identity or slot zero.  This module is the bounded
 * prepare boundary for the later player-init transaction: it validates the
 * two exact catalog entries and publishes no partial result on failure.
 */

#include "player_identity.h"

#include "system.h"

#include <string.h>

/* The catalog stores IDs in fixed buffers, including their terminator. */
static s32 playerIdentityIdLength(const char *id)
{
	s32 i;

	if (id == NULL) {
		return -2;
	}

	for (i = 0; i < CATALOG_ID_LEN; i++) {
		if (id[i] == '\0') {
			return i;
		}
	}

	return -1;
}

static s32 playerIdentityEntryIdMatches(const char *id,
		const asset_entry_t *entry)
{
	s32 id_len;
	s32 entry_len;

	if (id == NULL || entry == NULL) {
		return 0;
	}

	id_len = playerIdentityIdLength(id);
	entry_len = playerIdentityIdLength(entry->id);

	if (id_len < 0 || entry_len < 0 || id_len != entry_len) {
		return 0;
	}

	return memcmp(id, entry->id, (size_t)id_len) == 0;
}

static s32 playerIdentityMpIndex(const asset_entry_t *entry)
{
	if (entry == NULL || entry->mp_index < 0 || entry->mp_index > 255) {
		return -1;
	}

	return (s32)entry->mp_index;
}

void playerIdentityPlanReset(player_identity_plan_t *plan)
{
	if (plan == NULL) {
		return;
	}

	memset(plan, 0, sizeof(*plan));
	plan->runtime_bodynum = -1;
	plan->runtime_headnum = -1;
	plan->mp_body_index = -1;
	plan->mp_head_index = -1;
	plan->status = PLAYER_IDENTITY_INVALID_ARGUMENT;
}

const char *playerIdentityStatusString(player_identity_status_e status)
{
	switch (status) {
	case PLAYER_IDENTITY_OK:
		return "ok";
	case PLAYER_IDENTITY_INVALID_ARGUMENT:
		return "invalid_argument";
	case PLAYER_IDENTITY_EMPTY_BODY_ID:
		return "empty_body_id";
	case PLAYER_IDENTITY_EMPTY_HEAD_ID:
		return "empty_head_id";
	case PLAYER_IDENTITY_UNTERMINATED_BODY_ID:
		return "unterminated_body_id";
	case PLAYER_IDENTITY_UNTERMINATED_HEAD_ID:
		return "unterminated_head_id";
	case PLAYER_IDENTITY_MISSING_BODY_ENTRY:
		return "missing_body_entry";
	case PLAYER_IDENTITY_MISSING_HEAD_ENTRY:
		return "missing_head_entry";
	case PLAYER_IDENTITY_DISABLED_BODY_ENTRY:
		return "disabled_body_entry";
	case PLAYER_IDENTITY_DISABLED_HEAD_ENTRY:
		return "disabled_head_entry";
	case PLAYER_IDENTITY_WRONG_BODY_TYPE:
		return "wrong_body_type";
	case PLAYER_IDENTITY_WRONG_HEAD_TYPE:
		return "wrong_head_type";
	case PLAYER_IDENTITY_UNBOUND_BODY_RUNTIME_INDEX:
		return "unbound_body_runtime_index";
	case PLAYER_IDENTITY_UNBOUND_HEAD_RUNTIME_INDEX:
		return "unbound_head_runtime_index";
	case PLAYER_IDENTITY_INCONSISTENT_BODY_BINDING:
		return "inconsistent_body_binding";
	case PLAYER_IDENTITY_INCONSISTENT_HEAD_BINDING:
		return "inconsistent_head_binding";
	case PLAYER_IDENTITY_BODY_ID_ENTRY_MISMATCH:
		return "body_id_entry_mismatch";
	case PLAYER_IDENTITY_HEAD_ID_ENTRY_MISMATCH:
		return "head_id_entry_mismatch";
	default:
		return "unknown";
	}
}

static player_identity_status_e playerIdentityValidateId(
	const char *id,
	player_identity_status_e empty_status,
	player_identity_status_e unterminated_status)
{
	s32 length = playerIdentityIdLength(id);

	if (length == -2) {
		return PLAYER_IDENTITY_INVALID_ARGUMENT;
	}
	if (length == 0) {
		return empty_status;
	}
	if (length < 0) {
		return unterminated_status;
	}

	return PLAYER_IDENTITY_OK;
}

static player_identity_status_e playerIdentityFailure(
		player_identity_plan_t *out_plan,
		player_identity_status_e status)
{
	if (out_plan != NULL) {
		out_plan->status = status;
	}

	return status;
}

player_identity_status_e playerIdentityPrepareResolved(
	const char *body_id,
	const char *head_id,
	const asset_entry_t *body_entry,
	const asset_entry_t *head_entry,
	player_identity_plan_t *out_plan)
{
	player_identity_status_e status;
	player_identity_plan_t candidate;
	s32 body_length;
	s32 head_length;

	if (out_plan != NULL) {
		playerIdentityPlanReset(out_plan);
	}

	status = playerIdentityValidateId(body_id,
		PLAYER_IDENTITY_EMPTY_BODY_ID,
		PLAYER_IDENTITY_UNTERMINATED_BODY_ID);
	if (status != PLAYER_IDENTITY_OK) {
		return playerIdentityFailure(out_plan, status);
	}

	status = playerIdentityValidateId(head_id,
		PLAYER_IDENTITY_EMPTY_HEAD_ID,
		PLAYER_IDENTITY_UNTERMINATED_HEAD_ID);
	if (status != PLAYER_IDENTITY_OK) {
		return playerIdentityFailure(out_plan, status);
	}

	if (out_plan == NULL) {
		return playerIdentityFailure(NULL, PLAYER_IDENTITY_INVALID_ARGUMENT);
	}

	body_length = playerIdentityIdLength(body_id);
	head_length = playerIdentityIdLength(head_id);

	if (body_entry == NULL || !body_entry->occupied) {
		return playerIdentityFailure(out_plan,
			PLAYER_IDENTITY_MISSING_BODY_ENTRY);
	}
	if (head_entry == NULL || !head_entry->occupied) {
		return playerIdentityFailure(out_plan,
			PLAYER_IDENTITY_MISSING_HEAD_ENTRY);
	}
	if (!body_entry->enabled) {
		return playerIdentityFailure(out_plan,
			PLAYER_IDENTITY_DISABLED_BODY_ENTRY);
	}
	if (!head_entry->enabled) {
		return playerIdentityFailure(out_plan,
			PLAYER_IDENTITY_DISABLED_HEAD_ENTRY);
	}

	if (!playerIdentityEntryIdMatches(body_id, body_entry)) {
		return playerIdentityFailure(out_plan,
			PLAYER_IDENTITY_BODY_ID_ENTRY_MISMATCH);
	}
	if (!playerIdentityEntryIdMatches(head_id, head_entry)) {
		return playerIdentityFailure(out_plan,
			PLAYER_IDENTITY_HEAD_ID_ENTRY_MISMATCH);
	}

	if (body_entry->type != ASSET_BODY) {
		return playerIdentityFailure(out_plan,
			PLAYER_IDENTITY_WRONG_BODY_TYPE);
	}
	if (head_entry->type != ASSET_HEAD) {
		return playerIdentityFailure(out_plan,
			PLAYER_IDENTITY_WRONG_HEAD_TYPE);
	}

	if (body_entry->runtime_index < 0 || body_entry->ext.body.bodynum < 0) {
		return playerIdentityFailure(out_plan,
			PLAYER_IDENTITY_UNBOUND_BODY_RUNTIME_INDEX);
	}
	if (head_entry->runtime_index < 0 || head_entry->ext.head.headnum < 0) {
		return playerIdentityFailure(out_plan,
			PLAYER_IDENTITY_UNBOUND_HEAD_RUNTIME_INDEX);
	}

	if (body_entry->runtime_index != (s32)body_entry->ext.body.bodynum) {
		return playerIdentityFailure(out_plan,
			PLAYER_IDENTITY_INCONSISTENT_BODY_BINDING);
	}
	if (head_entry->runtime_index != (s32)head_entry->ext.head.headnum) {
		return playerIdentityFailure(out_plan,
			PLAYER_IDENTITY_INCONSISTENT_HEAD_BINDING);
	}

	playerIdentityPlanReset(&candidate);
	memcpy(candidate.body_id, body_id, (size_t)body_length);
	candidate.body_id[body_length] = '\0';
	memcpy(candidate.head_id, head_id, (size_t)head_length);
	candidate.head_id[head_length] = '\0';
	candidate.runtime_bodynum = body_entry->runtime_index;
	candidate.runtime_headnum = head_entry->runtime_index;
	candidate.mp_body_index = playerIdentityMpIndex(body_entry);
	candidate.mp_head_index = playerIdentityMpIndex(head_entry);
	candidate.status = PLAYER_IDENTITY_OK;

	*out_plan = candidate;
	return PLAYER_IDENTITY_OK;
}

player_identity_status_e playerIdentityPrepare(
	const char *body_id,
	const char *head_id,
	player_identity_plan_t *out_plan)
{
	const asset_entry_t *body_entry;
	const asset_entry_t *head_entry;
	player_identity_status_e status;

	if (out_plan != NULL) {
		playerIdentityPlanReset(out_plan);
	}

	status = playerIdentityValidateId(body_id,
		PLAYER_IDENTITY_EMPTY_BODY_ID,
		PLAYER_IDENTITY_UNTERMINATED_BODY_ID);
	if (status == PLAYER_IDENTITY_OK) {
		status = playerIdentityValidateId(head_id,
			PLAYER_IDENTITY_EMPTY_HEAD_ID,
			PLAYER_IDENTITY_UNTERMINATED_HEAD_ID);
	}
	if (status == PLAYER_IDENTITY_OK && out_plan == NULL) {
		status = PLAYER_IDENTITY_INVALID_ARGUMENT;
	}

	if (status == PLAYER_IDENTITY_OK) {
		body_entry = assetCatalogResolve(body_id);
		head_entry = assetCatalogResolve(head_id);
		status = playerIdentityPrepareResolved(
			body_id, head_id, body_entry, head_entry, out_plan);
	} else {
		playerIdentityFailure(out_plan, status);
	}

	if (status != PLAYER_IDENTITY_OK) {
		sysLogPrintf(LOG_ERROR,
			"PLAYER.INIT.PREFLIGHT phase=identity status=%s",
			playerIdentityStatusString(status));
	}

	return status;
}

static player_identity_status_e playerIdentityPrepareRuntimeBodyOnly(
	s32 runtime_bodynum,
	const char *body_id,
	player_identity_plan_t *out_plan)
{
	const asset_entry_t *body_entry;
	player_identity_status_e status;
	player_identity_plan_t candidate;
	s32 body_length;

	if (out_plan != NULL) {
		playerIdentityPlanReset(out_plan);
	}

	status = playerIdentityValidateId(body_id,
		PLAYER_IDENTITY_EMPTY_BODY_ID,
		PLAYER_IDENTITY_UNTERMINATED_BODY_ID);
	if (status != PLAYER_IDENTITY_OK) {
		return playerIdentityFailure(out_plan, status);
	}
	if (out_plan == NULL) {
		return PLAYER_IDENTITY_INVALID_ARGUMENT;
	}

	body_entry = assetCatalogResolve(body_id);
	if (body_entry == NULL || !body_entry->occupied) {
		return playerIdentityFailure(out_plan,
			PLAYER_IDENTITY_MISSING_BODY_ENTRY);
	}
	if (!body_entry->enabled) {
		return playerIdentityFailure(out_plan,
			PLAYER_IDENTITY_DISABLED_BODY_ENTRY);
	}
	if (!playerIdentityEntryIdMatches(body_id, body_entry)) {
		return playerIdentityFailure(out_plan,
			PLAYER_IDENTITY_BODY_ID_ENTRY_MISMATCH);
	}
	if (body_entry->type != ASSET_BODY) {
		return playerIdentityFailure(out_plan,
			PLAYER_IDENTITY_WRONG_BODY_TYPE);
	}
	if (body_entry->runtime_index < 0 || body_entry->ext.body.bodynum < 0) {
		return playerIdentityFailure(out_plan,
			PLAYER_IDENTITY_UNBOUND_BODY_RUNTIME_INDEX);
	}
	if (body_entry->runtime_index != (s32)body_entry->ext.body.bodynum
			|| body_entry->runtime_index != runtime_bodynum) {
		return playerIdentityFailure(out_plan,
			PLAYER_IDENTITY_INCONSISTENT_BODY_BINDING);
	}
	if (!catalogGetBodyIsComplete(runtime_bodynum)) {
		return playerIdentityFailure(out_plan,
			PLAYER_IDENTITY_UNBOUND_HEAD_RUNTIME_INDEX);
	}

	body_length = playerIdentityIdLength(body_id);
	playerIdentityPlanReset(&candidate);
	memcpy(candidate.body_id, body_id, (size_t)body_length);
	candidate.body_id[body_length] = '\0';
	candidate.runtime_bodynum = runtime_bodynum;
	candidate.mp_body_index = playerIdentityMpIndex(body_entry);
	candidate.status = PLAYER_IDENTITY_OK;
	*out_plan = candidate;
	return PLAYER_IDENTITY_OK;
}

player_identity_status_e playerIdentityPrepareRuntime(
	s32 runtime_bodynum,
	s32 runtime_headnum,
	player_identity_plan_t *out_plan)
{
	const char *body_id = catalogBodyIdByBodynum(runtime_bodynum);
	const char *head_id;
	player_identity_status_e status;

	if (runtime_headnum < -1) {
		if (out_plan != NULL) {
			playerIdentityPlanReset(out_plan);
		}
		status = playerIdentityFailure(out_plan,
			PLAYER_IDENTITY_INVALID_ARGUMENT);
	} else if (runtime_headnum == -1) {
		status = playerIdentityPrepareRuntimeBodyOnly(runtime_bodynum,
			body_id, out_plan);
	} else {
		head_id = catalogHeadIdByHeadnum(runtime_headnum);
		status = playerIdentityPrepare(body_id, head_id, out_plan);
	}

	if (status == PLAYER_IDENTITY_OK
			&& (out_plan->runtime_bodynum != runtime_bodynum
				|| out_plan->runtime_headnum != runtime_headnum)) {
		status = out_plan->runtime_bodynum != runtime_bodynum
			? PLAYER_IDENTITY_INCONSISTENT_BODY_BINDING
			: PLAYER_IDENTITY_INCONSISTENT_HEAD_BINDING;
		playerIdentityPlanReset(out_plan);
		out_plan->status = status;
	}

	if (status != PLAYER_IDENTITY_OK) {
		sysLogPrintf(LOG_ERROR,
			"PLAYER.INIT.PREFLIGHT phase=identity runtime=reject status=%s body=%d head=%d",
			playerIdentityStatusString(status), runtime_bodynum,
			runtime_headnum);
	}

	return status;
}
