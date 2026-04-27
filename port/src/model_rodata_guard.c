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

int modelRodataIsReadable(const void *rodata, size_t bytes)
{
	if (rodata == NULL || bytes == 0) {
		return 0;
	}

#ifdef _WIN32
	/* Walk the address range one VirtualQuery at a time.  A single
	 * heap allocation can span multiple MEMORY_BASIC_INFORMATION
	 * regions if the OS coalesced or reserved them differently, so we
	 * keep advancing until the entire requested range is committed and
	 * readable, or one segment fails the check. */
	const unsigned char *cur = (const unsigned char *)rodata;
	const unsigned char *end = cur + bytes;

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

		const unsigned char *region_end = (const unsigned char *)mbi.BaseAddress + mbi.RegionSize;
		if (region_end <= cur) {
			/* Pathological: VirtualQuery returned a region that does
			 * not contain `cur`.  Treat as failure. */
			return 0;
		}
		cur = region_end;
	}
	return 1;
#else
	/* Non-Windows fallback: NULL is the only check we can do cheaply
	 * without installing a per-call SIGSEGV handler.  PD2 is
	 * Windows-only at HEAD, so this branch exists for portability /
	 * future-proofing only. */
	(void)bytes;
	return rodata != NULL ? 1 : 0;
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
