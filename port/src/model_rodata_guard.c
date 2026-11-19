/*
 * model_rodata_guard.c -- S483b (2026-04-27)
 *
 * See port/include/model_rodata_guard.h for design rationale.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "model_rodata_guard.h"
#include "system.h"

int modelRodataIsNonNull(const void *rodata)
{
	return rodata != NULL ? 1 : 0;
}

#ifdef _WIN32
/*
 * Ask the operating system about [start, end), one VirtualQuery at a
 * time.  A single heap allocation can span multiple
 * MEMORY_BASIC_INFORMATION regions if the OS coalesced or reserved them
 * differently, so we keep advancing until the entire requested range is
 * committed and readable, or one segment fails the check.
 *
 * On success *run_lo and *run_hi receive the bounds of the regions that
 * were walked.  Every byte of [*run_lo, *run_hi) is committed and
 * readable; *run_lo is the base of the page that holds `start` (a query
 * reports from the queried page onward, not from the start of the
 * allocation) and *run_hi is at or past `end`.
 *
 * The caller has already refused a NULL start, an empty range and a range
 * that wraps past the top of the address space.
 */
static int rodataQueryRun(uintptr_t start, uintptr_t end,
                          uintptr_t *run_lo, uintptr_t *run_hi)
{
	uintptr_t cur = start;
	uintptr_t lo = start;

	while (cur < end) {
		MEMORY_BASIC_INFORMATION mbi;
		SIZE_T n = VirtualQuery((LPCVOID)cur, &mbi, sizeof(mbi));
		if (n == 0) {
			return 0;
		}
		if (mbi.State != MEM_COMMIT) {
			return 0;
		}
		if (mbi.Protect & PAGE_GUARD) {
			return 0;
		}
		const DWORD readable_mask = (PAGE_READONLY | PAGE_READWRITE
		                             | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE
		                             | PAGE_EXECUTE_WRITECOPY | PAGE_WRITECOPY);
		if ((mbi.Protect & readable_mask) == 0) {
			return 0;
		}

		const uintptr_t region_lo = (uintptr_t)mbi.BaseAddress;
		const uintptr_t region_hi = region_lo + mbi.RegionSize;
		if (region_hi <= cur || region_lo > cur) {
			/* Pathological: VirtualQuery returned a region that does
			 * not contain `cur`.  Treat as failure. */
			return 0;
		}
		if (cur == start) {
			lo = region_lo;
		}
		cur = region_hi;
	}

	*run_lo = lo;
	*run_hi = cur;
	return 1;
}
#endif

int modelRodataIsReadable(const void *rodata, size_t bytes)
{
	const uintptr_t start = (uintptr_t)rodata;

	if (rodata == NULL || bytes == 0) {
		return 0;
	}
	if (start + bytes < start) {
		/* The range wraps past the top of the address space. */
		return 0;
	}

#ifdef _WIN32
	uintptr_t lo;
	uintptr_t hi;

	return rodataQueryRun(start, start + bytes, &lo, &hi);
#else
	/* Non-Windows fallback: NULL is the only check we can do cheaply
	 * without installing a per-call SIGSEGV handler.  PD2 is
	 * Windows-only at HEAD, so this branch exists for portability /
	 * future-proofing only. */
	return 1;
#endif
}

int modelRodataIsReadableInSpan(model_rodata_span_t *span,
                                const void *rodata, size_t bytes)
{
	const uintptr_t start = (uintptr_t)rodata;

	if (span == NULL) {
		return modelRodataIsReadable(rodata, bytes);
	}
	if (rodata == NULL || bytes == 0) {
		return 0;
	}
	if (start + bytes < start) {
		/* The range wraps past the top of the address space. */
		return 0;
	}

	/* Inside what this walk already knows to be readable: no system
	 * call.  This is the whole point of the span (B-1234). */
	if (span->hi != 0 && start >= span->lo && start + bytes <= span->hi) {
		return 1;
	}

#ifdef _WIN32
	uintptr_t lo;
	uintptr_t hi;

	if (!rodataQueryRun(start, start + bytes, &lo, &hi)) {
		/* What the span already holds was verified earlier in this
		 * walk and is still true, so it stays. */
		return 0;
	}

	if (span->hi != 0 && lo <= span->hi && span->lo <= hi) {
		/* The new run touches or overlaps the old one: together they
		 * are one run of readable memory. */
		if (span->lo < lo) {
			lo = span->lo;
		}
		if (span->hi > hi) {
			hi = span->hi;
		}
	}
	span->lo = lo;
	span->hi = hi;
	return 1;
#else
	/* Non-Windows fallback, as in modelRodataIsReadable.  Nothing was
	 * verified, so the span learns nothing. */
	return 1;
#endif
}

/* Per-site rate limiter: cap miss warnings to MODEL_RODATA_MISS_BUDGET
 * per (site_tag) to keep the log readable when a corrupt model ticks
 * continuously.  The site_tag pointer is used as the identity (the
 * argument is expected to be a string literal at the call site). */
#define MODEL_RODATA_MISS_BUDGET 8
#define MODEL_RODATA_MISS_TABLE_SIZE 16

struct miss_slot {
	const char *site;
	unsigned int count;
};

static struct miss_slot s_MissTable[MODEL_RODATA_MISS_TABLE_SIZE];

static unsigned int *missCounterFor(const char *site_tag)
{
	unsigned int free_slot = MODEL_RODATA_MISS_TABLE_SIZE;
	for (unsigned int i = 0; i < MODEL_RODATA_MISS_TABLE_SIZE; i++) {
		if (s_MissTable[i].site == site_tag) {
			return &s_MissTable[i].count;
		}
		if (s_MissTable[i].site == NULL && free_slot == MODEL_RODATA_MISS_TABLE_SIZE) {
			free_slot = i;
		}
	}
	if (free_slot < MODEL_RODATA_MISS_TABLE_SIZE) {
		s_MissTable[free_slot].site = site_tag;
		s_MissTable[free_slot].count = 0;
		return &s_MissTable[free_slot].count;
	}
	/* Table full -- fall back to a dummy that always exceeds the
	 * budget so we silently drop further misses for new sites.  In
	 * practice the table is sized for many more distinct sites than
	 * the relations-update tree has. */
	static unsigned int s_OverflowSink;
	s_OverflowSink = MODEL_RODATA_MISS_BUDGET + 1;
	return &s_OverflowSink;
}

void modelRodataLogMiss(const char *site_tag,
                        const void *model,
                        const void *node,
                        const void *rodata,
                        size_t bytes,
                        const char *reason)
{
	unsigned int *counter = missCounterFor(site_tag ? site_tag : "?");
	if (*counter >= MODEL_RODATA_MISS_BUDGET) {
		return;
	}
	*counter += 1;

	sysLogPrintf(LOG_WARNING,
	             "MODEL.RODATA.MISS: site=%s model=%p node=%p rodata=%p bytes=%zu reason=%s (count=%u)",
	             site_tag ? site_tag : "?",
	             model,
	             node,
	             rodata,
	             bytes,
	             reason ? reason : "?",
	             *counter);
}
