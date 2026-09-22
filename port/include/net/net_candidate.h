#ifndef _IN_NET_CANDIDATE_H
#define _IN_NET_CANDIDATE_H

#include "PR/ultratypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Candidate signaling is deliberately a different wire family from
 * presence-v5.  In particular, no field in this frame is an ENet
 * match-server route. */
#define NET_CANDIDATE_MAGIC "PDCND"
#define NET_CANDIDATE_MAGIC_LEN 5u
#define NET_CANDIDATE_VERSION 1u
#define NET_CANDIDATE_MAX 8u
#define NET_CANDIDATE_CREDENTIAL_LEN 16u
#define NET_CANDIDATE_AGENT_LEN 16u
#define NET_CANDIDATE_RECORD_LEN 12u
#define NET_CANDIDATE_BODY_LEN 192u
#define NET_CANDIDATE_PUBKEY_OFFSET 160u
#define NET_CANDIDATE_SIG_OFFSET 192u
#define NET_CANDIDATE_SIG_LEN 64u
#define NET_CANDIDATE_FRAME_LEN 256u
#define NET_CANDIDATE_SIG_DOMAIN "pd-candidate-v1"
#define NET_CANDIDATE_SIG_DOMAIN_LEN 15u

/* The ICE probe is a transport proof, not a candidate or match-route frame.
 * It carries the signed candidate owner's credential and is authenticated
 * with HMAC-SHA256 keyed by that credential. */
#define NET_CANDIDATE_PROBE_MAGIC "PDICE"
#define NET_CANDIDATE_PROBE_MAGIC_LEN 5u
#define NET_CANDIDATE_PROBE_VERSION 1u
#define NET_CANDIDATE_PROBE_KIND_PROBE 1u
#define NET_CANDIDATE_PROBE_KIND_ACK 2u
#define NET_CANDIDATE_PROBE_TAG_LEN 32u
#define NET_CANDIDATE_PROBE_TAG_OFFSET 52u
#define NET_CANDIDATE_PROBE_FRAME_LEN 84u
#define NET_CANDIDATE_PROBE_RETRY_INTERVAL_MS 300u
#define NET_CANDIDATE_PROBE_MAX_SENDS 8u
#define NET_CANDIDATE_PROBE_REPLAY_SLOTS 32u

/* Candidate signaling is retried only while the 2.5-second ICE tier is live.
 * Five sends at 500 ms cover the full attempt without approaching the
 * 30-second periodic presence cadence. The expiry floor includes the rounded
 * ICE window plus the accepted five-second wall-clock skew. */
#define NET_CANDIDATE_SIGNAL_RETRY_INTERVAL_MS 500u
#define NET_CANDIDATE_SIGNAL_MAX_SENDS 5u
#define NET_CANDIDATE_SIGNAL_EXPIRY_HEADROOM_SECONDS 8u

#define NET_CANDIDATE_FRESH_SECONDS 90u
#define NET_CANDIDATE_CLOCK_SKEW_SECONDS 5u
#define NET_CANDIDATE_MAX_LEASE_SECONDS 300u
#define NET_CANDIDATE_PRIORITY_HOST 100u
#define NET_CANDIDATE_PRIORITY_UPNP 150u
#define NET_CANDIDATE_PRIORITY_STUN 200u

typedef enum net_candidate_frame_kind_e {
	NET_CANDIDATE_FRAME_PUBLISH = 1,
	NET_CANDIDATE_FRAME_RETIRE = 2,
} net_candidate_frame_kind_t;

typedef enum net_candidate_type_e {
	NET_CANDIDATE_TYPE_HOST = 1,
	NET_CANDIDATE_TYPE_STUN = 2,
	NET_CANDIDATE_TYPE_UPNP = 3,
} net_candidate_type_t;

typedef struct net_candidate_s {
	u32 ipv4;       /* host byte order; never zero on the wire */
	u16 port;       /* host byte order; never zero on the wire */
	u8  type;       /* net_candidate_type_t */
	u8  provenance; /* repeats type; prevents source relabelling */
	u32 priority;   /* signed by the sender, nonzero */
} net_candidate_t;

typedef struct net_candidate_set_s {
	u32 sender_handle;
	u32 target_handle; /* direct peer only; zero is not a broadcast */
	u16 proto_version;
	u8  kind;           /* net_candidate_frame_kind_t */
	u8  flags;          /* reserved, must be zero */
	u32 generation;    /* nonzero and monotonic per sender/target */
	u32 issued_unix_seconds;
	u32 expires_unix_seconds;
	u8  credential[NET_CANDIDATE_CREDENTIAL_LEN];
	u8  candidate_count;
	u8  _pad[3];
	char agent_name[NET_CANDIDATE_AGENT_LEN];
	net_candidate_t candidates[NET_CANDIDATE_MAX];
} net_candidate_set_t;

typedef struct net_candidate_peer_state_s {
	u32 generation;
	u32 issued_unix_seconds;
	u32 expires_unix_seconds;
	u8  credential[NET_CANDIDATE_CREDENTIAL_LEN];
	u8  candidate_count;
	u8  active;
	u8  _pad[2];
	net_candidate_t candidates[NET_CANDIDATE_MAX];
} net_candidate_peer_state_t;

typedef enum net_candidate_update_e {
	NET_CANDIDATE_UPDATE_REJECT = 0,
	NET_CANDIDATE_UPDATE_ACCEPT = 1,
	NET_CANDIDATE_UPDATE_DUPLICATE = 2,
	NET_CANDIDATE_UPDATE_RETIRE = 3,
} net_candidate_update_t;

typedef struct net_candidate_probe_s {
	u8  kind;                 /* NET_CANDIDATE_PROBE_KIND_* */
	u32 sender_handle;        /* packet sender, never wildcard */
	u32 target_handle;        /* packet recipient, never wildcard */
	u32 candidate_owner_handle; /* signed set owner authenticated by credential */
	u32 generation;           /* candidate owner's active set generation */
	u32 nonce;                /* one-use challenge */
	u8  credential[NET_CANDIDATE_CREDENTIAL_LEN];
	u32 candidate_ipv4;       /* exact signed candidate being nominated */
	u16 candidate_port;
	u8  candidate_index;
} net_candidate_probe_t;

typedef struct net_candidate_probe_retry_s {
	u32 next_send_ms;
	u8 send_count;
	u8 _pad[3];
} net_candidate_probe_retry_t;

typedef enum net_candidate_probe_retry_action_e {
	NET_CANDIDATE_PROBE_RETRY_WAIT = 0,
	NET_CANDIDATE_PROBE_RETRY_SEND = 1,
	NET_CANDIDATE_PROBE_RETRY_EXHAUSTED = 2,
} net_candidate_probe_retry_action_t;

typedef struct net_candidate_signal_retry_s {
	u32 next_send_ms;
	u8 send_count;
	u8 _pad[3];
} net_candidate_signal_retry_t;

typedef enum net_candidate_signal_retry_action_e {
	NET_CANDIDATE_SIGNAL_RETRY_WAIT = 0,
	NET_CANDIDATE_SIGNAL_RETRY_SEND = 1,
	NET_CANDIDATE_SIGNAL_RETRY_EXHAUSTED = 2,
} net_candidate_signal_retry_action_t;

typedef struct net_candidate_probe_replay_s {
	u32 nonce;
	u32 source_ipv4;
	u16 source_port;
	u8 in_use;
	u8 _pad;
} net_candidate_probe_replay_t;

typedef enum net_candidate_probe_replay_result_e {
	NET_CANDIDATE_PROBE_REPLAY_REJECT = 0,
	NET_CANDIDATE_PROBE_REPLAY_NEW = 1,
	NET_CANDIDATE_PROBE_REPLAY_DUPLICATE = 2,
} net_candidate_probe_replay_result_t;

/** Validate a host-order IPv4 endpoint without accepting broadcast/multicast. */
s32 netCandidateIpv4IsUnicast(u32 ipv4);

/** Type-specific route safety for local and reflexive candidates. */
s32 netCandidateIpv4IsHostRoute(u32 ipv4);
s32 netCandidateIpv4IsPublicRoute(u32 ipv4);

/** Fill bytes from the platform cryptographic random source. */
s32 netCandidateRandomBytes(u8 *out, size_t len);

/** Validate issue/expiry bounds at the receiver's current Unix time. */
s32 netCandidateTimestampIsFresh(u32 issued_unix_seconds,
		u32 expires_unix_seconds, u32 now_unix_seconds);

/** Normalize and validate a decoded candidate set before live state changes. */
s32 netCandidateSetNormalize(const net_candidate_set_t *wire,
		u32 expected_sender_handle, u32 expected_target_handle,
		u32 now_unix_seconds, net_candidate_set_t *out);

/** Compare candidate content, provenance, and priority in canonical order. */
s32 netCandidateSetEqual(const net_candidate_set_t *a,
		const net_candidate_set_t *b);

/** Decide whether a validated set is new, a replay, a duplicate, or retirement. */
net_candidate_update_t netCandidatePlanUpdate(
		const net_candidate_peer_state_t *prior,
		const net_candidate_set_t *incoming);

/** Commit a validated set after netCandidatePlanUpdate accepts it. */
void netCandidatePeerStateCommit(net_candidate_peer_state_t *state,
		const net_candidate_set_t *set);

/** Encode/decode and authenticate the fixed-size ICE probe/ack packet. */
s32 netCandidateProbeEncode(const net_candidate_probe_t *probe,
		const u8 key[NET_CANDIDATE_CREDENTIAL_LEN],
		u8 out_frame[NET_CANDIDATE_PROBE_FRAME_LEN]);
s32 netCandidateProbeDecode(const u8 *frame, size_t frame_len,
		net_candidate_probe_t *out);
s32 netCandidateProbeAuthenticate(const u8 *frame, size_t frame_len,
		const u8 key[NET_CANDIDATE_CREDENTIAL_LEN]);
s32 netCandidateProbeSourceMatches(const net_candidate_probe_t *probe,
		u32 source_ipv4, u16 source_port);
s32 netCandidateProbeValidate(const net_candidate_probe_t *probe,
		const u8 *frame, size_t frame_len,
		const u8 key[NET_CANDIDATE_CREDENTIAL_LEN],
		u32 expected_sender_handle, u32 expected_target_handle,
		u32 expected_owner_handle, u32 expected_generation,
		const u8 expected_credential[NET_CANDIDATE_CREDENTIAL_LEN],
		u32 expires_unix_seconds, u32 now_unix_seconds,
		u32 prior_nonce);

/** Pure bounded retry state used by ICE; wrap-safe for SDL millisecond ticks. */
void netCandidateProbeRetryInit(net_candidate_probe_retry_t *state,
		u32 now_ms);
net_candidate_probe_retry_action_t netCandidateProbeRetryPoll(
		net_candidate_probe_retry_t *state, u32 now_ms, u32 deadline_ms);

/** Pure bounded signed-frame retry state; independent of probe retries. */
void netCandidateSignalRetryInit(net_candidate_signal_retry_t *state,
		u32 now_ms);
net_candidate_signal_retry_action_t netCandidateSignalRetryPoll(
		net_candidate_signal_retry_t *state, u32 now_ms, u32 deadline_ms);

/** True when expiry remains valid through the complete skewed ICE attempt. */
s32 netCandidateExpiryHasHeadroom(u32 expires_unix_seconds,
		u32 now_unix_seconds, u32 required_seconds);

/**
 * Record a probe challenge against its exact UDP source. Repeating the same
 * nonce from the same source is an idempotent retry; moving that nonce to a
 * different source is a replay and fails closed.
 */
net_candidate_probe_replay_result_t netCandidateProbeReplayRecord(
		net_candidate_probe_replay_t *entries, u32 entry_count,
		u32 nonce, u32 source_ipv4, u16 source_port);

/** Encode the signed body, including the public key at its fixed offset. */
s32 netCandidateFrameEncodeBody(const net_candidate_set_t *set,
		const u8 pubkey[32], u8 out_body[NET_CANDIDATE_BODY_LEN]);

/** Decode only the fixed-size body and expose pubkey/signature slices. */
s32 netCandidateFrameDecode(const u8 *frame, size_t frame_len,
		net_candidate_set_t *out, const u8 **out_pubkey,
		const u8 **out_signature);

#ifdef __cplusplus
}
#endif

#endif /* _IN_NET_CANDIDATE_H */
