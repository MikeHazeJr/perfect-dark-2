/**
 * p2p.h -- 6-tier peer-to-peer connection layer for Phase 1 connectivity.
 *
 * Replaces the dedicated-server friend-play role per
 * context/designs/connectivity-and-modern-main-menu.md (Section 0). This
 * layer is responsible for opening a peer-to-peer datagram channel between
 * two clients, escalating through six tiers in sequence:
 *
 *   T0 LAN broadcast            -- same subnet, UDP broadcast / mDNS
 *   T1 Direct UDP                -- known endpoint
 *   T2 STUN-assisted hole punch  -- reflexive addresses + simultaneous send
 *   T3 UPnP / NAT-PMP            -- temporary port mapping
 *   T4 ICE candidate gathering   -- host / srflx / prflx pair testing
 *   T5 TURN relay                -- last resort, ~2-3x relayer bandwidth
 *
 * Per-pair sequential escalation with ~2-3s per tier timeout. Each tier
 * exposes UX feedback ("Trying direct connection...", "Trying NAT
 * traversal...", "Trying relay...").
 *
 * This module DOES NOT itself open ENet hosts. Once a tier succeeds it
 * publishes the resulting UDP endpoint (or relay descriptor) via the
 * `on_open` callback; the caller is responsible for using that endpoint
 * for whatever follows -- ENet connect, presence ping exchange, group
 * session handshake. The layer is reusable by:
 *   - presence pings (Section 3)
 *   - in-match P2P invites (Section 9 invite flow)
 *   - chat / file transfer (Phase 2)
 *
 * The orchestrator is single-threaded: tick from the main loop. STUN /
 * UPnP / TURN have their own background threads owned by their helper
 * modules; p2p.c only consumes their results.
 */

#ifndef _IN_P2P_H
#define _IN_P2P_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Tier identity
 * ------------------------------------------------------------------------- */

typedef enum {
	P2P_TIER_NONE   = -1,
	P2P_TIER_LAN    = 0,
	P2P_TIER_DIRECT = 1,
	P2P_TIER_STUN   = 2,
	P2P_TIER_UPNP   = 3,
	P2P_TIER_ICE    = 4,
	P2P_TIER_TURN   = 5,
	P2P_TIER_COUNT  = 6,
} p2p_tier_t;

const char *p2pTierName(p2p_tier_t t);
const char *p2pTierUxLabel(p2p_tier_t t); /* short user-facing string */

/* Per-tier soft timeout (ms) for the orchestrator's escalation loop. */
#define P2P_TIER_TIMEOUT_MS 2500

/* -------------------------------------------------------------------------
 * Pair state
 * ------------------------------------------------------------------------- */

typedef enum {
	P2P_PAIR_IDLE     = 0,
	P2P_PAIR_WORKING  = 1,
	P2P_PAIR_OPEN     = 2,
	P2P_PAIR_FAILED   = 3,
} p2p_pair_state_t;

#define P2P_PAIR_ID_INVALID 0u

/* Endpoint description published on success. */
typedef struct p2p_endpoint_s {
	u32  ipv4;       /* host byte order, 0 if relay-only */
	u16  port;       /* host byte order */
	u16  flags;      /* see P2P_EP_* below */
	u32  relay_ipv4; /* TURN relay address when flags & P2P_EP_RELAYED */
	u16  relay_port;
	u16  _pad;
} p2p_endpoint_t;

#define P2P_EP_DIRECT      0x0000  /* peer reachable directly */
#define P2P_EP_HOLE_PUNCH  0x0001  /* path opened via simultaneous send */
#define P2P_EP_PORT_MAPPED 0x0002  /* peer's router has a UPnP/NAT-PMP map */
#define P2P_EP_ICE_PAIR    0x0004  /* selected via ICE pair test */
#define P2P_EP_RELAYED     0x0008  /* traffic flows through TURN relayer */

/* -------------------------------------------------------------------------
 * Diagnostics snapshot
 * ------------------------------------------------------------------------- */

typedef struct p2p_pair_diag_s {
	u32              pair_id;
	u32              peer_handle;          /* social handle of remote peer */
	p2p_pair_state_t state;
	p2p_tier_t       current_tier;
	p2p_tier_t       last_attempted_tier;  /* highest tier touched so far */
	u32              attempt_count;
	u32              ms_since_pair_start;
	u32              ms_in_current_tier;
	p2p_endpoint_t   endpoint;             /* valid when state == OPEN */
	char             last_error[96];
} p2p_pair_diag_t;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

/** Initialise the layer. Idempotent. Must follow socialInit(). */
void p2pInit(void);

/** Tear down all pairs, free background work. */
void p2pShutdown(void);

/** Drive the per-pair state machines. Call once per frame. */
void p2pTick(void);

/** Total milliseconds since process start; same epoch all p2p_* modules use. */
u32  p2pNowMs(void);

/* -------------------------------------------------------------------------
 * Pair management
 *
 * The peer is identified by social handle (32-bit). The optional hint_addr
 * (host byte order ipv4 + port) is used as the tier 1 / tier 2 candidate
 * when the social layer has cached a prior endpoint. Pass 0/0 if unknown.
 * ------------------------------------------------------------------------- */

/**
 * Begin opening a path to peer_handle. Returns a non-zero pair id on
 * success, 0 if the layer is full or the handle is invalid.
 *
 * If a pair for the same peer already exists, the old pair id is returned
 * (no duplicate work).
 */
u32 p2pPairBegin(u32 peer_handle, u32 hint_ipv4, u16 hint_port);

/** Cancel a pair (frees the slot). Idempotent. */
void p2pPairCancel(u32 pair_id);

/** Returns the current state of pair_id, or P2P_PAIR_IDLE if unknown. */
p2p_pair_state_t p2pPairGetState(u32 pair_id);

/**
 * Read the open endpoint. Returns 1 if valid, 0 otherwise (not yet open
 * or closed). out may be NULL if the caller only wants the boolean.
 */
s32 p2pPairGetEndpoint(u32 pair_id, p2p_endpoint_t *out);

/** Snapshot the pair's diagnostics. Returns 1 on success, 0 if unknown. */
s32 p2pPairDiag(u32 pair_id, p2p_pair_diag_t *out);

/** Number of currently-tracked pairs. */
s32 p2pPairCount(void);

/** Iterate. Returns the pair id at index, or 0 if oob. */
u32 p2pPairIdAt(s32 idx);

/* -------------------------------------------------------------------------
 * Tier 0 -- LAN broadcast (port/src/net/p2p_lan.c)
 *
 * On the same subnet, peers exchange a small announcement datagram so the
 * orchestrator can find them without any internet round-trip. Listening
 * is opt-in -- the local presence layer enables it whenever the local
 * client is online and visible to friends.
 * ------------------------------------------------------------------------- */

/** Open the LAN listener. Idempotent. */
s32  p2pLanStart(void);

/** Close the LAN listener and broadcast a goodbye. */
void p2pLanStop(void);

/** Periodic announce; safe to call every frame, throttled internally. */
void p2pLanTick(void);

/** True if the listener bound successfully. */
s32  p2pLanIsRunning(void);

/**
 * Fetch a previously-discovered peer's ipv4:port if recent. Returns 1 on
 * success. Out parameters are host byte order. Stale entries (>15s) are
 * pruned automatically.
 */
s32  p2pLanLookup(u32 peer_handle, u32 *out_ipv4, u16 *out_port);

/* -------------------------------------------------------------------------
 * Tier 1 -- Direct UDP (port/src/net/p2p_direct.c)
 *
 * Trivial path: send a probe to (hint_ipv4 : hint_port) and wait for a
 * pong. Reuses the existing connectionless punch primitives where useful.
 * ------------------------------------------------------------------------- */

s32  p2pDirectStart(u32 pair_id, u32 ipv4, u16 port);
void p2pDirectPoll(void);

/* -------------------------------------------------------------------------
 * Tier 2 -- STUN / hole punch (wrapping port/src/net/netstun.c +
 * netholepunch.c). Triggered only when tier 1 times out.
 * ------------------------------------------------------------------------- */

s32  p2pStunStart(u32 pair_id);
void p2pStunPoll(void);

/* Tell the LAN module our STUN reflexive ip:port so neighbours behind the
 * same NAT may try our public endpoint as a tier-1 hint. */
s32  p2pPublishMyReflexive(u32 ipv4, u16 port);

/** Latest known STUN reflexive endpoint, host byte order. 0 if unknown. */
u32  p2pMyReflexiveIpv4(void);
u16  p2pMyReflexivePort(void);

/* -------------------------------------------------------------------------
 * Tier 3 -- UPnP / NAT-PMP (wrapping port/src/net/netupnp.c).
 * ------------------------------------------------------------------------- */

s32  p2pUpnpStart(u32 pair_id);
void p2pUpnpPoll(void);

/* -------------------------------------------------------------------------
 * Tier 4 -- ICE candidate gathering + pair testing.
 * Consumes STUN reflexive results plus an exchange of candidate lists.
 * ------------------------------------------------------------------------- */

s32  p2pIceStart(u32 pair_id);
void p2pIcePoll(void);

/**
 * Inject a candidate (ipv4 + port) for an ICE attempt. The signaling layer
 * calls this when a peer's candidate list arrives via the friend list.
 */
s32  p2pIceAddPeerCandidate(u32 pair_id, u32 ipv4, u16 port);

/* -------------------------------------------------------------------------
 * Tier 5 -- TURN relay. Last resort when every direct path failed.
 * The relayer is selected from group peers with bandwidth headroom.
 * ------------------------------------------------------------------------- */

s32  p2pTurnStart(u32 pair_id);
void p2pTurnPoll(void);

/**
 * Register a peer as a willing relay candidate (filled in by group_session
 * once we know upload-speed measurements). Highest reported_kbps wins.
 * Pass kbps=0 to remove.
 */
void p2pTurnRegisterRelayCandidate(u32 peer_handle, u32 ipv4, u16 port, u32 kbps);

/* -------------------------------------------------------------------------
 * Internal callbacks fired by tier modules into the orchestrator.
 * Tier modules must call exactly one of these per attempt.
 * ------------------------------------------------------------------------- */

/** Tier module reports success. The endpoint is published to readers. */
void p2pInternalReportSuccess(u32 pair_id, p2p_tier_t tier,
                              const p2p_endpoint_t *ep);

/** Tier module reports failure or timeout; orchestrator escalates. */
void p2pInternalReportFailure(u32 pair_id, p2p_tier_t tier,
                              const char *reason);

#ifdef __cplusplus
}
#endif

#endif /* _IN_P2P_H */
