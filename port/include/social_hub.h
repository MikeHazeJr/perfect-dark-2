#ifndef _IN_SOCIAL_HUB_H
#define _IN_SOCIAL_HUB_H

#include <PR/ultratypes.h>

/* social_hub.h -- cold-network lifecycle (Mike directive 2026-05-17)
 *
 * Mike's contract: "The online functionality shouldn't even exist until
 * signing an agent in, since it's tied to the specific agent." Connect
 * codes are per-agent; any subsystem that publishes the local identity
 * (presence pings, p2p LAN broadcast, voice peer handshakes, group
 * session handle, chat presence) must stay COLD -- no socket bound, no
 * tick body running -- until prefsAgentLoad fires.
 *
 * This module owns the lifecycle:
 *
 *   socialHubBringOnline()  -- called from prefsAgentLoad (and the CLI
 *                              fast-path bootLaunchLoadAgentTick).
 *                              Runs all the online subsystem inits in
 *                              the same order mainInit used to. Idempotent.
 *
 *   socialHubGoOffline()    -- shutdown counterpart. Currently not called
 *                              in the normal flow (no agent-sign-out UI yet)
 *                              but exposed for future use + symmetry.
 *
 *   socialHubIsOnline()     -- predicate. True iff BringOnline has fired
 *                              this session and GoOffline hasn't followed.
 *
 * --no-net: BringOnline short-circuits when g_BootNoNet is set so the
 * smoke harness's "no firewall focus-steal" path stays clean.
 */

#ifdef __cplusplus
extern "C" {
#endif

void socialHubBringOnline(void);
void socialHubGoOffline(void);
s32  socialHubIsOnline(void);

#ifdef __cplusplus
}
#endif

#endif
