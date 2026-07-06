#ifndef _IN_PORT_MEMWATCH_H
#define _IN_PORT_MEMWATCH_H

#include "types.h"

/*
 * Hardware write-watchpoint harness (Windows DR0-DR3 debug registers).
 *
 * Catches the EXACT instruction that writes to a watched address -- built to find
 * what corrupts a chr modeldef's rootnode (the B-952 hunt) and kept as general
 * heap-corruption infra. Enabled by --memp-watch; the chr whose model chain gets
 * armed is chosen with --memp-watch-chrnum=N (see memWatchChrnum below). Up to 4
 * addresses (4 debug registers). When a watched address is written, MEMWATCH.HIT
 * logs the writing RIP + stack pointer -- symbolize RIP to find the culprit
 * function. No-op unless --memp-watch is set.
 */
int memWatchEnabled(void);

/* Target chr for the arm hook, from --memp-watch-chrnum=N. Returns -1 (no chr)
 * when not specified, so the harness stays inert until a target is named. */
s32 memWatchChrnum(void);

/* Arm a 4-byte hardware WRITE watchpoint on `addr` (deduped; up to 4 total).
 * chrnum/bodynum are for the log so the HIT can be tied back to a chr. */
void memWatchAddr(void *addr, s32 chrnum, s32 bodynum);

/* Prove the DR mechanism works here: arm a probe var, write it, expect a HIT. */
void memWatchSelfTest(void);

/* Re-arm all watchpoints on every current thread (covers loader threads spawned
 * after the initial arm). Call periodically. */
void memWatchRefresh(void);

#endif
