#ifndef _IN_PORT_MEMWATCH_H
#define _IN_PORT_MEMWATCH_H

#include "types.h"

/*
 * B-952 hardware write-watchpoint harness (Windows DR0-DR3 debug registers).
 *
 * Catches the EXACT instruction that writes to a watched address, to find what
 * corrupts a chr modeldef's rootnode (skedarruins chrnum 5052: rootnode valid at
 * spawn, NULL by first tick -- a wild-pointer write / UAF that the memp red-zone
 * canaries could not see because it lands mid-allocation). Enabled by
 * --memp-watch. Up to 4 addresses (4 debug registers). When a watched address is
 * written, MEMWATCH.HIT logs the writing RIP + stack pointer -- symbolize RIP to
 * find the culprit function. No-op unless the flag is set.
 */
int memWatchEnabled(void);

/* Arm a 4-byte hardware WRITE watchpoint on `addr` (deduped; up to 4 total).
 * chrnum/bodynum are for the log so the HIT can be tied back to a chr. */
void memWatchAddr(void *addr, s32 chrnum, s32 bodynum);

/* Prove the DR mechanism works here: arm a probe var, write it, expect a HIT. */
void memWatchSelfTest(void);

/* Re-arm all watchpoints on every current thread (covers loader threads spawned
 * after the initial arm). Call periodically. */
void memWatchRefresh(void);

#endif
