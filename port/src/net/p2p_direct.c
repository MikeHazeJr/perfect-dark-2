/**
 * p2p_direct.c -- fail-closed compatibility surface for legacy direct UDP.
 *
 * The old PDDIR nonce exchange was neither signed nor bound to the intended
 * social handle and UDP source. Host/LAN reachability now runs exclusively
 * through the signed candidate plus authenticated ICE flow. These entry
 * points remain for ABI compatibility but own no socket or attempt state.
 */

#include "net/p2p.h"

s32 p2pDirectStart(u32 pair_id, u32 ipv4, u16 port)
{
	(void)ipv4;
	(void)port;
	p2pInternalReportFailure(pair_id, P2P_TIER_DIRECT,
		"legacy direct proof is not authenticated");
	return -1;
}

void p2pDirectPoll(void)
{
}

void p2pDirectCancel(u32 pair_id)
{
	(void)pair_id;
}

void p2pDirectShutdown(void)
{
}
