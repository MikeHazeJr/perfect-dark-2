/*
 * S301 — Crash breadcrumb ring.
 *
 * See port/include/crashbreadcrumb.h for motivation.
 *
 * All storage is static. vsnprintf is signal-safe in practice on the
 * platforms we target (Windows CRT + glibc); we keep the total write
 * bounded and never call malloc. Ring index is updated via
 * __atomic_fetch_add so parallel push from render + game threads
 * produces consistent slot assignment, even if the strings race.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "crashbreadcrumb.h"
#include "system.h"

#define CRASH_BC_SLOTS     256u
#define CRASH_BC_TEXT_MAX  112u  /* fits in a single cache line with headers */

typedef struct {
	u64 timestamp_us;  /* sysGetMicroseconds() at push */
	u32 sequence;      /* monotonic; 0 = unused slot */
	char text[CRASH_BC_TEXT_MAX];
} CrashBc;

static CrashBc s_Ring[CRASH_BC_SLOTS];
static volatile u32 s_Head;  /* next write index (mod CRASH_BC_SLOTS) */
static volatile u32 s_Seq;   /* monotonic sequence */
static s32 s_Inited = 0;

void crashBreadcrumbInit(void)
{
	if (s_Inited) return;
	memset(s_Ring, 0, sizeof(s_Ring));
	s_Head = 0;
	s_Seq = 0;
	s_Inited = 1;
}

u32 crashBreadcrumbCount(void)
{
	return s_Seq;
}

void crashBreadcrumbPush(const char *fmt, ...)
{
	if (!fmt) return;
	if (!s_Inited) crashBreadcrumbInit();

	/* Claim a slot atomically. */
	const u32 idx = __atomic_fetch_add(&s_Head, 1u, __ATOMIC_RELAXED) % CRASH_BC_SLOTS;
	const u32 seq = __atomic_add_fetch(&s_Seq, 1u, __ATOMIC_RELAXED);

	CrashBc *slot = &s_Ring[idx];
	slot->timestamp_us = sysGetMicroseconds();

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(slot->text, sizeof(slot->text), fmt, ap);
	va_end(ap);

	/* Publish sequence last so a reader that sees a non-zero seq also
	 * sees the populated text / timestamp. */
	__atomic_store_n(&slot->sequence, seq, __ATOMIC_RELEASE);
}

void crashBreadcrumbDump(FILE *f, u32 entriesWanted)
{
	if (!f) return;
	if (!s_Inited) return;

	const u32 total = s_Seq;
	if (total == 0) {
		fputs("[breadcrumb ring is empty]\n", f);
		return;
	}

	const u32 capacity = CRASH_BC_SLOTS;
	u32 want = entriesWanted ? entriesWanted : capacity;
	if (want > capacity) want = capacity;
	if (want > total)    want = total;

	fprintf(f, "[breadcrumb ring: total=%u dumping=%u]\n",
		(unsigned)total, (unsigned)want);

	/* The oldest entry we want is at sequence (total - want + 1). Find
	 * its slot and walk forward. Note: entries with lower sequence
	 * than our starting point may still be in the ring if the writer
	 * hasn't wrapped yet; guard by sequence, not slot. */
	const u32 startSeq = total - want + 1;

	for (u32 s = startSeq; s <= total; s++) {
		/* Slots are written at (seq - 1) % capacity. */
		const u32 slotIdx = (s - 1) % capacity;
		const CrashBc *slot = &s_Ring[slotIdx];
		const u32 slotSeq = __atomic_load_n(&slot->sequence, __ATOMIC_ACQUIRE);
		if (slotSeq != s) {
			/* This slot was overwritten or never populated; skip. */
			continue;
		}

		const u64 ts = slot->timestamp_us;
		const u32 secs = (u32)(ts / 1000000ULL);
		const u32 mics = (u32)(ts % 1000000ULL);
		fprintf(f, "  #%u [%u.%06us] %s\n",
			(unsigned)s, (unsigned)secs, (unsigned)mics, slot->text);
	}
}

void crashBreadcrumbLogRecent(u32 count)
{
	if (!s_Inited) return;

	const u32 total = s_Seq;
	if (total == 0) {
		sysLogPrintf(LOG_NOTE, "CRASH.DIAG: breadcrumb ring empty");
		return;
	}

	u32 want = count ? count : 32;
	if (want > CRASH_BC_SLOTS) want = CRASH_BC_SLOTS;
	if (want > total)          want = total;

	sysLogPrintf(LOG_NOTE, "CRASH.DIAG: dumping %u of %u breadcrumbs",
		(unsigned)want, (unsigned)total);

	const u32 startSeq = total - want + 1;
	for (u32 s = startSeq; s <= total; s++) {
		const u32 slotIdx = (s - 1) % CRASH_BC_SLOTS;
		const CrashBc *slot = &s_Ring[slotIdx];
		const u32 slotSeq = __atomic_load_n(&slot->sequence, __ATOMIC_ACQUIRE);
		if (slotSeq != s) continue;
		sysLogPrintf(LOG_NOTE, "CRASH.DIAG: #%u %s",
			(unsigned)s, slot->text);
	}
}
