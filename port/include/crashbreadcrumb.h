#ifndef _IN_CRASHBREADCRUMB_H
#define _IN_CRASHBREADCRUMB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <PR/ultratypes.h>

/**
 * S301 — Crash breadcrumb ring.
 *
 * Static 256-entry ring of short diagnostic tags that the VEH / SIGABRT
 * handlers dump on crash. All storage is static (no heap, no stack growth),
 * so the ring survives stack overflow. Writes are racy-but-benign: worst
 * case on a crash we lose one entry. The dump walks the ring in temporal
 * order and appends to an already-open FILE*.
 *
 * Push sites (current callers):
 *   - src/lib/main.c: mainTick entry per frame (heartbeat every 60 frames)
 *   - src/game/lv.c: lvTick entry
 *   - src/game/chraction.c: chraTickBg entry, chraTick entry/exit
 *   - src/game/bondmove.c / physics tick
 *   - src/game/bot.c: botSpawn begin/end
 *   - port/src/net/matchsetup.c: matchStart phases
 *   - port/src/net/netmsg.c: SVC_STAGE_START send/receive
 *   - port/src/audio.c: mixer callback on hitch
 *
 * Rationale: B-126 silent crashes on Chicago (~9s in) currently leave no
 * trail — VEH + SIGABRT get called with a corrupted stack and just dump
 * exception context. The last breadcrumb before death pinpoints the
 * subsystem that was executing, narrowing the search from "everywhere"
 * to a single tick subroutine.
 */

/* Push a breadcrumb. Safe to call from any thread; vsnprintf into a
 * thread-local static buffer, then atomically bump the ring head.
 * Max text length per entry is CRASH_BC_TEXT_MAX-1. */
void crashBreadcrumbPush(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Dump the ring to the given open file. Safe to call from a signal /
 * exception handler — no heap, no locks, bounded stack.
 * Writes oldest-first. entriesWanted may be less than the ring size
 * to keep VEH output bounded; pass 0 for "all entries". */
void crashBreadcrumbDump(FILE *f, u32 entriesWanted);

/* Initialise: zero the ring + reset head. Called once at startup
 * before any push site fires. Idempotent. */
void crashBreadcrumbInit(void);

/* Accessor for tests / diagnostics. Returns 0 until first push. */
u32 crashBreadcrumbCount(void);

/* Console dump — invoked by the on-screen log viewer or a menu debug
 * key. Routes through sysLogPrintf so it shows up in the ring. */
void crashBreadcrumbLogRecent(u32 count);

#ifdef __cplusplus
}
#endif

#endif /* _IN_CRASHBREADCRUMB_H */
