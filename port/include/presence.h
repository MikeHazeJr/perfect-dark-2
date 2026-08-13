/**
 * presence.h -- Always-on peer-to-peer presence layer.
 *
 * Phase 1 of the connectivity rollout. Pings every friend in the social
 * store at a low rate, accepts pings from friends and pending-invitees,
 * and records last_seen timestamps. The friend list is the address
 * book -- there is no central signaling server.
 *
 * Lifecycle states (per local player):
 *   OFFLINE                 -- no socket bound
 *   PRESENCE_BOOTSTRAP      -- socket bound, sending initial pings
 *   ONLINE_IDLE             -- presence broadcasting, no match
 *   IN_MATCH (mission/cs)   -- in a match, presence still flowing
 *   SPECTATING              -- watching a friend's match
 *
 * Wire format runs on a dedicated UDP socket (port 27105). Frames are
 * small and connectionless; reliability is per-ping retransmit, not
 * stream-oriented. ENet sits one layer up for invitations / chat once a
 * p2p path opens.
 *
 * Anti-DoS: incoming ping accepted only from connect codes already in
 * the local friend list or in the pending-invite set. Per-source rate
 * limit at 1 ping per 5 s.
 */

#ifndef _IN_PRESENCE_H
#define _IN_PRESENCE_H

#include <PR/ultratypes.h>
#include "net/net_match_route.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Presence state machine (local lifecycle + per-friend status)
 * ------------------------------------------------------------------------- */

typedef enum {
	PRESENCE_OFFLINE        = 0,
	PRESENCE_BOOTSTRAP      = 1,
	PRESENCE_ONLINE_IDLE    = 2,
	PRESENCE_IN_MATCH       = 3,
	PRESENCE_IN_MISSION     = 4,
	PRESENCE_SPECTATING     = 5,
	PRESENCE_APPEAR_OFFLINE = 6,
} presence_state_t;

const char *presenceStateName(presence_state_t s);

/* -------------------------------------------------------------------------
 * Per-friend presence record
 * ------------------------------------------------------------------------- */

typedef struct presence_peer_s {
	u32              handle;          /* friend's social handle */
	presence_state_t state;
	u32              last_pong_ms;    /* millisecond timestamp from p2pNowMs */
	u32              cached_ipv4;     /* host order; 0 if unknown */
	u16              cached_port;
	u16              proto_version;   /* peer's NET_PROTOCOL_VER */
	u8               input_class;     /* ACTIONMAP_INPUT_CLASS_*; privacy-safe category */
	u8               _pad[3];
	u32              upload_kbps;     /* signed passive prior/current-session report */
	net_match_route_t match_route;     /* normalized signed ENet authority route */
	u32              match_route_received_ms;
	u32              match_route_latest_issued_unix_seconds;
	u32              match_route_latest_nonce;
	char             status_blurb[64];/* "Mission: Pelagic" / "CS: Felicity" / etc. */
} presence_peer_t;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

/** Initialise. Must follow socialInit() and p2pInit(). */
void presenceInit(void);

/** Tear down. Sends a goodbye if running. */
void presenceShutdown(void);

/** Drive ping schedule + drain receive socket. Call once per frame. */
void presenceTick(void);

/** Report local lifecycle to outgoing pings. */
void presenceSetLocalState(presence_state_t s);
presence_state_t presenceGetLocalState(void);

/**
 * Mike directive 2026-05-17: the client must not begin presence pings
 * (or any social-hub activity) until an agent profile has been loaded.
 * Connect codes are agent-specific, so the keypair/handle that drives
 * outbound presence must be tied to a known agent. Call this from the
 * unified Agent Profile activation completion path. Idempotent.
 */
void presenceMarkAgentLoaded(void);

/** Returns 1 once presenceMarkAgentLoaded() has been called this session. */
s32 presenceIsAgentLoaded(void);

/** Set the local status blurb. Truncated to 63 chars. */
void presenceSetLocalBlurb(const char *blurb);
const char *presenceGetLocalBlurb(void);

/* -------------------------------------------------------------------------
 * Friend snapshot accessors
 * ------------------------------------------------------------------------- */

/** Fetch the last known presence record for a friend. NULL if unknown. */
const presence_peer_t *presencePeerByHandle(u32 handle);

/** Returns 1 if the friend has pong'd within the last PEER_FRESH_MS. */
s32 presencePeerIsOnline(u32 handle);

/** Return a fresh, normalized signed ENet match-server route for a peer. */
s32 presencePeerMatchRoute(u32 handle, net_match_route_t *out_route);

/**
 * Set or clear the local authority's signed match-server route. The route is
 * wire-typed: source-derived routes keep ipv4 zero, while STUN/UPnP routes
 * carry their explicit server address. Setting a route publishes it
 * immediately to cached peers; normal presence traffic provides retries.
 */
s32 presenceSetLocalMatchRoute(const net_match_route_t *route);
void presenceClearLocalMatchRoute(void);
void presencePublishMatchRoute(void);

/* -------------------------------------------------------------------------
 * Pending invites (for the friend acceptance / DoS allowlist).
 * Adding a peer here permits unsolicited pings from them for a short
 * window. The social layer maintains the underlying friend list; this is
 * just an in-memory shadow set.
 * ------------------------------------------------------------------------- */

s32  presencePendingInviteAdd(u32 handle);
void presencePendingInviteRemove(u32 handle);

/* -------------------------------------------------------------------------
 * Outgoing invite (Phase 1 minimum: send a friend an invite to play).
 * Returns 0 on success (invite in flight), -1 if no path is open.
 * ------------------------------------------------------------------------- */

s32  presenceSendInvite(u32 friend_handle, u8 kind);

/* Invite kinds (mirrored on the wire) */
#define PRESENCE_INVITE_KIND_MATCH      1  /* "join my match" */
#define PRESENCE_INVITE_KIND_GROUP      2  /* "join my group" */
#define PRESENCE_INVITE_KIND_LISTEN     3  /* listening room (Phase 4) */

/* -------------------------------------------------------------------------
 * Inbound invite queue. The UI (sidebar / Social menu) reads pending
 * invites and the user accepts or declines.
 * ------------------------------------------------------------------------- */

typedef struct presence_invite_s {
	u32 from_handle;
	u8  kind;
	u8  _pad[3];
	u32 received_ms;
	char from_agent[16];
} presence_invite_t;

s32 presenceInviteCount(void);
const presence_invite_t *presenceInviteAt(s32 idx);
s32 presenceInviteAccept(s32 idx);  /* opens a p2p pair to the inviter */
s32 presenceInviteDecline(s32 idx);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PRESENCE_H */
