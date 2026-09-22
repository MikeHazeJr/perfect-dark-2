/**
 * p2p_turn.c -- fail-closed compatibility surface for the reserved relay tier.
 *
 * The legacy PDTRN allocation ACK authenticated only a predictable nonce. It
 * did not bind the relay identity, intended target, a signed credential, or
 * the exact UDP source. Relay candidates may still be tracked for diagnostics,
 * but no socket is opened and no relay result can publish a production path.
 */

#include "net/p2p.h"

#include <string.h>

#define TURN_MAX_RELAY_CANDS 16

typedef struct {
	u32 peer_handle;
	u32 ipv4;
	u16 port;
	u32 reported_kbps;
	u8 in_use;
} relay_cand_t;

static relay_cand_t s_Cands[TURN_MAX_RELAY_CANDS];

void p2pTurnRegisterRelayCandidate(u32 peer_handle, u32 ipv4, u16 port,
	u32 kbps)
{
	relay_cand_t *candidate = NULL;
	for (s32 i = 0; i < TURN_MAX_RELAY_CANDS; i++) {
		if (s_Cands[i].in_use && s_Cands[i].peer_handle == peer_handle) {
			candidate = &s_Cands[i];
			break;
		}
	}
	if (kbps == 0 || peer_handle == 0 || ipv4 == 0 || port == 0) {
		if (candidate) memset(candidate, 0, sizeof(*candidate));
		return;
	}
	if (!candidate) {
		for (s32 i = 0; i < TURN_MAX_RELAY_CANDS; i++) {
			if (!s_Cands[i].in_use) {
				candidate = &s_Cands[i];
				break;
			}
		}
	}
	if (!candidate) return;
	candidate->in_use = 1;
	candidate->peer_handle = peer_handle;
	candidate->ipv4 = ipv4;
	candidate->port = port;
	candidate->reported_kbps = kbps;
}

s32 p2pTurnStart(u32 pair_id)
{
	p2pInternalReportFailure(pair_id, P2P_TIER_TURN,
		"relay allocation proof is not authenticated");
	return -1;
}

void p2pTurnPoll(void)
{
}

void p2pTurnCancel(u32 pair_id)
{
	(void)pair_id;
}

void p2pTurnShutdown(void)
{
	memset(s_Cands, 0, sizeof(s_Cands));
}
