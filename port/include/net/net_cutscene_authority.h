#ifndef _IN_NET_CUTSCENE_AUTHORITY_H
#define _IN_NET_CUTSCENE_AUTHORITY_H

#include "PR/ultratypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NET_CUTSCENE_AUTHORITY_MAX_CLIENTS 32u
#define NET_CUTSCENE_AUTHORITY_MAX_PLAYERS 8u

#define NET_CUTSCENE_SKIP_REQUEST_WIRE_SIZE 5u
#define NET_CUTSCENE_SKIP_ACCEPT_WIRE_SIZE  5u
#define NET_CUTSCENE_STATE_WIRE_SIZE        9u

typedef enum net_cutscene_authority_status_e {
	NET_CUTSCENE_AUTHORITY_OK = 0,
	NET_CUTSCENE_AUTHORITY_INVALID_ARGUMENT,
	NET_CUTSCENE_AUTHORITY_INVALID_WIRE,
	NET_CUTSCENE_AUTHORITY_INVALID_ROSTER,
	NET_CUTSCENE_AUTHORITY_SOURCE_NOT_IN_GAME,
	NET_CUTSCENE_AUTHORITY_NO_ACTIVE_CUTSCENE,
	NET_CUTSCENE_AUTHORITY_GENERATION_MISMATCH,
	NET_CUTSCENE_AUTHORITY_REQUESTER_NOT_FOUND,
	NET_CUTSCENE_AUTHORITY_STATE_CONFLICT,
} net_cutscene_authority_status_t;

typedef enum net_cutscene_authority_phase_e {
	NET_CUTSCENE_AUTHORITY_PHASE_IDLE = 0,
	NET_CUTSCENE_AUTHORITY_PHASE_ACTIVE,
	NET_CUTSCENE_AUTHORITY_PHASE_ENDED,
} net_cutscene_authority_phase_t;

typedef enum net_cutscene_authority_action_e {
	NET_CUTSCENE_AUTHORITY_APPLY = 0,
	NET_CUTSCENE_AUTHORITY_IGNORE_DUPLICATE,
	NET_CUTSCENE_AUTHORITY_IGNORE_STALE,
} net_cutscene_authority_action_t;

typedef struct net_cutscene_skip_request_s {
	u8 requested_playernum;
	u32 generation;
} net_cutscene_skip_request_t;

typedef struct net_cutscene_skip_accept_s {
	u8 requester_client_id;
	u32 generation;
} net_cutscene_skip_accept_t;

typedef struct net_cutscene_state_wire_s {
	u8 active;
	u32 client_mask;
	u32 generation;
} net_cutscene_state_wire_t;

typedef struct net_cutscene_participant_s {
	u8 client_id;
	u8 runtime_playernum;
} net_cutscene_participant_t;

typedef struct net_cutscene_authority_tracker_s {
	net_cutscene_authority_phase_t phase;
	u32 client_mask;
	u32 generation;
} net_cutscene_authority_tracker_t;

const char *netCutsceneAuthorityStatusString(
		net_cutscene_authority_status_t status);

s32 netCutsceneSkipRequestEncode(
		const net_cutscene_skip_request_t *request,
		u8 out[NET_CUTSCENE_SKIP_REQUEST_WIRE_SIZE]);
net_cutscene_authority_status_t netCutsceneSkipRequestDecode(
		const u8 *wire, size_t wire_size,
		net_cutscene_skip_request_t *out);

s32 netCutsceneSkipAcceptEncode(
		const net_cutscene_skip_accept_t *accept,
		u8 out[NET_CUTSCENE_SKIP_ACCEPT_WIRE_SIZE]);
net_cutscene_authority_status_t netCutsceneSkipAcceptDecode(
		const u8 *wire, size_t wire_size,
		net_cutscene_skip_accept_t *out);

s32 netCutsceneStateEncode(const net_cutscene_state_wire_t *state,
		u8 out[NET_CUTSCENE_STATE_WIRE_SIZE]);
net_cutscene_authority_status_t netCutsceneStateDecode(
		const u8 *wire, size_t wire_size,
		net_cutscene_state_wire_t *out);

/** Mint the next nonzero authority generation; exhaustion fails closed. */
s32 netCutsceneAuthorityNextGeneration(u32 current, u32 *out_generation);

/** Accept an idempotent/new authority token, but never move it backwards. */
net_cutscene_authority_status_t netCutsceneAuthoritySyncGeneration(
	u32 current, u32 authority_generation, u32 *out_generation);

/** Validate one frozen match roster independently of mutable netclient state. */
net_cutscene_authority_status_t netCutsceneValidateRoster(
	const net_cutscene_participant_t *participants,
	size_t participant_count);

/**
 * Plan a reliable authority-state transition without mutating the caller.
 * Exact duplicates and stale messages are consumed idempotently; contradictory
 * same-generation state or impossible reliable ordering fails closed.
 */
net_cutscene_authority_status_t netCutscenePlanStateTransition(
	const net_cutscene_authority_tracker_t *current,
	const net_cutscene_state_wire_t *incoming,
	net_cutscene_authority_tracker_t *out_next,
	net_cutscene_authority_action_t *out_action);

/** Convert server runtime-player bits to stable client-ID bits. */
net_cutscene_authority_status_t netCutsceneClientMaskFromPlayerMask(
		u8 player_mask, const net_cutscene_participant_t *participants,
		size_t participant_count, u32 *out_client_mask);

/** Convert stable client-ID bits to this receiver's runtime-player bits. */
net_cutscene_authority_status_t netCutscenePlayerMaskFromClientMask(
		u32 client_mask, const net_cutscene_participant_t *participants,
		size_t participant_count, u8 *out_player_mask);

/**
 * Validate an untrusted CLC request against authoritative source identity and
 * the current stage-scoped cutscene generation. The requested player number is
 * syntax only; the returned player always comes from the authenticated source.
 */
net_cutscene_authority_status_t netCutscenePlanServerSkip(
		const net_cutscene_skip_request_t *request,
		u8 source_client_id, u8 source_playernum, s32 source_in_game,
		s32 cutscene_active, u32 authoritative_generation,
		net_cutscene_skip_accept_t *out_accept,
		u8 *out_authoritative_playernum);

/** Resolve an authoritative SVC acceptance into this client's runtime slot. */
net_cutscene_authority_status_t netCutscenePlanClientSkip(
		const net_cutscene_skip_accept_t *accept,
		const net_cutscene_participant_t *participants,
		size_t participant_count, s32 cutscene_active,
		u32 current_generation, u8 *out_runtime_playernum);

#ifdef __cplusplus
}
#endif

#endif
