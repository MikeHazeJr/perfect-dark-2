/**
 * group_session.h -- Phase 1 mesh + authority election + invite-to-match handoff.
 *
 * Owns the small state machine that bridges three things:
 *
 *   1. The presence invite from a friend ("come play with me").
 *   2. The p2p layer's tier-escalation result (an open endpoint, or
 *      a documented failure mode).
 *   3. The existing ENet match-start flow (netStartClient on the
 *      resolved "ip:port" string).
 *
 * Phase 1 group capacity is 4 humans (mesh -- 6 edges trivially handled).
 * Within-match authority is per-match, not persistent: it is elected
 * each time a match starts. Election rule (Section 2.1):
 *
 *   1. Highest reported_kbps wins.
 *   2. Match initiator as fallback when no kbps data is available
 *      (first session of a fresh install) or on an exact tie.
 *   3. Smallest public handle as the deterministic tie-break if the
 *      initiator is no longer an eligible candidate.
 *
 * If the authority drops mid-match, quiet failover re-elects the next
 * highest-bandwidth peer in the mesh. The match continues.
 *
 * This module deliberately does NOT implement the wire-protocol additions
 * for the in-match channel itself -- that's the existing
 * `port/src/net/netmsg.c` ENet pipeline. The group session sits one layer
 * above: it produces the "ip:port" string that netStartClient consumes,
 * and tracks the high-level mesh membership while a session is alive.
 */

#ifndef _IN_GROUP_SESSION_H
#define _IN_GROUP_SESSION_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GROUP_SESSION_MAX_PEERS 4

typedef enum {
	GROUP_PEER_UNKNOWN     = 0,
	GROUP_PEER_INVITED     = 1,  /* invite sent, awaiting response */
	GROUP_PEER_RESOLVING   = 2,  /* p2p tier escalation in progress */
	GROUP_PEER_CONNECTED   = 3,  /* ENet path open (we triggered netStartClient) */
	GROUP_PEER_FAILED      = 4,  /* terminal failure (per-tier exhausted) */
} group_peer_state_t;

typedef enum {
	GROUP_FAIL_NONE                 = 0,
	GROUP_FAIL_VERSION_MISMATCH     = 1, /* Q14 mismatch UX */
	GROUP_FAIL_NETWORK_BLOCKED      = 2, /* tier 5 exhausted -- Section 2.4 residue */
	GROUP_FAIL_PEER_OFFLINE         = 3, /* no presence response */
	GROUP_FAIL_REJECTED             = 4, /* peer declined the invite */
	GROUP_FAIL_INTERNAL             = 5, /* socket error / OOM / unhandled */
} group_fail_reason_t;

typedef struct group_peer_s {
	u32                 handle;
	u32                 pair_id;          /* p2p pair, 0 if not yet started */
	group_peer_state_t  state;
	group_fail_reason_t fail;
	u32                 last_kbps;        /* used for authority election */
	u32                 last_kbps_ms;     /* signed presence report freshness */
	u32                 entered_state_ms;
	u32                 ipv4;             /* host order, valid in CONNECTED */
	u16                 port;
	u16                 _pad;
	u16                 their_proto;      /* their NET_PROTOCOL_VER for mismatch UX */
	char                their_agent[16];
	char                their_version[16];/* free-text "0.0.165" etc., for UX */
} group_peer_t;

typedef struct group_session_s {
	u8           in_session;       /* 1 once at least one peer is non-UNKNOWN */
	u8           authority_idx;    /* index into peers[] of the elected authority */
	u8           is_local_authority;
	u8           _pad;
	u32          authority_handle; /* mirror for read accessors */
	u32          initiator_handle; /* tie/no-data fallback for this group */
	u32          local_kbps;       /* fresh passive local ENet measurement */
	group_peer_t peers[GROUP_SESSION_MAX_PEERS];
} group_session_t;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

void groupSessionInit(void);
void groupSessionShutdown(void);
void groupSessionTick(void);  /* drives pending p2p pairs forward */

/** Read-only snapshot. The pointer is stable for the lifetime of the process. */
const group_session_t *groupSessionGet(void);

/** Currently elected authority. Returns 0 if no authority is set. */
u32 groupSessionAuthorityHandle(void);

/** True if WE are the authority (host) of this session. */
s32 groupSessionIsLocalAuthority(void);

/* -------------------------------------------------------------------------
 * Hooks fired by presence.c on invite events
 * ------------------------------------------------------------------------- */

/** Local user accepted an inbound invite. Begins the p2p pair + tracks
 *  it through to ENet handoff. Returns 0 on accept, -1 if the table is
 *  full or the handle is blocked. */
s32 groupSessionAcceptInvite(u32 inviter_handle);

/** Local user just sent an invite. Tracks it for authority election +
 *  mesh membership once the peer accepts. */
s32 groupSessionRecordSentInvite(u32 invitee_handle);

/** Inbound invite-response (accept / decline). */
void groupSessionOnInviteResponse(u32 from_handle, s32 accepted);

/** Drop a peer from the mesh (presence BYE, kick, manual leave). */
void groupSessionDropPeer(u32 handle);

/* -------------------------------------------------------------------------
 * Authority election
 * ------------------------------------------------------------------------- */

/** Recompute the elected authority. Called automatically on peer
 *  add/drop and on periodic kbps refresh. Idempotent. */
void groupSessionRecomputeAuthority(void);

/** Refresh a peer's kbps measurement. group_session.c uses this for the
 *  highest-upload-speed election rule. */
void groupSessionUpdateKbps(u32 handle, u32 kbps);

/* -------------------------------------------------------------------------
 * UX helpers
 * ------------------------------------------------------------------------- */

const char *groupPeerStateName(group_peer_state_t s);
const char *groupFailReasonText(group_fail_reason_t r);

/** Format the exact Q14 version-mismatch string into out:
 *  "Invite failed. Version info (you: 0.1.0, smarch: 0.0.165 (old))" */
void groupSessionFormatVersionMismatch(const group_peer_t *peer,
                                        char *out, u32 outsize);

#ifdef __cplusplus
}
#endif

#endif /* _IN_GROUP_SESSION_H */
