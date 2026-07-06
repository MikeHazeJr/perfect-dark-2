/*
 * memarena -- reserve-and-commit growable arena. See port/include/memarena.h for
 * the rationale (pointer-stable growth for pools that hold raw interior pointers).
 *
 * Windows-only: VirtualAlloc reserves a contiguous virtual range once and commits
 * pages into it on demand. Reserved-but-uncommitted address space costs no RAM,
 * so the reservation can be far larger than any realistic content need; committed
 * pages are the only physical cost and grow with the pool.
 */
#include <windows.h>
#include <stddef.h>

#include "types.h"
#include "system.h"
#include "memarena.h"

static size_t arenaPageSize(void)
{
	static size_t s_page = 0;
	if (s_page == 0) {
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		s_page = (size_t)si.dwPageSize;
		if (s_page == 0) {
			s_page = 4096; /* sane fallback */
		}
	}
	return s_page;
}

static size_t arenaRoundUp(size_t bytes, size_t to)
{
	if (to == 0) {
		return bytes;
	}
	return ((bytes + to - 1) / to) * to;
}

s32 arenaReserve(struct memarena *a, const char *name, size_t stride, size_t maxelements)
{
	size_t reserve;
	void *base;

	if (a == NULL || stride == 0 || maxelements == 0) {
		return 0;
	}

	/* Guard the multiply against overflow before rounding to a page boundary. */
	if (maxelements > (size_t)-1 / stride) {
		sysLogPrintf(LOG_ERROR, "arenaReserve(%s): stride %zu * maxelements %zu overflows",
			name ? name : "?", stride, maxelements);
		return 0;
	}

	reserve = arenaRoundUp(stride * maxelements, arenaPageSize());

	base = VirtualAlloc(NULL, reserve, MEM_RESERVE, PAGE_NOACCESS);
	if (base == NULL) {
		sysLogPrintf(LOG_ERROR, "arenaReserve(%s): VirtualAlloc MEM_RESERVE %zu bytes failed (err %lu)",
			name ? name : "?", reserve, (unsigned long)GetLastError());
		return 0;
	}

	a->base = (u8 *)base;
	a->stride = stride;
	a->reservedbytes = reserve;
	a->committedbytes = 0;
	a->capacity = 0;
	a->name = name;
	return 1;
}

void *arenaEnsure(struct memarena *a, size_t elements)
{
	size_t needbytes;

	if (a == NULL || a->base == NULL || a->stride == 0) {
		return NULL;
	}

	needbytes = arenaRoundUp(elements * a->stride, arenaPageSize());

	if (needbytes <= a->committedbytes) {
		return a->base; /* already large enough (never shrinks) */
	}

	if (needbytes > a->reservedbytes) {
		sysLogPrintf(LOG_ERROR,
			"arenaEnsure(%s): need %zu elements (%zu bytes) exceeds reservation of %zu bytes -- "
			"raise the reserve cap for this pool",
			a->name ? a->name : "?", elements, needbytes, a->reservedbytes);
		return NULL;
	}

	/* Commit [base, base+needbytes). Committing already-committed pages is a
	 * documented no-op that preserves their contents; only the newly-committed
	 * tail pages are zero-filled by the OS. So existing records survive growth. */
	if (VirtualAlloc(a->base, needbytes, MEM_COMMIT, PAGE_READWRITE) == NULL) {
		sysLogPrintf(LOG_ERROR, "arenaEnsure(%s): VirtualAlloc MEM_COMMIT %zu bytes failed (err %lu)",
			a->name ? a->name : "?", needbytes, (unsigned long)GetLastError());
		return NULL; /* left at previous capacity */
	}

	a->committedbytes = needbytes;
	a->capacity = needbytes / a->stride;
	return a->base;
}

size_t arenaCapacity(const struct memarena *a)
{
	if (a == NULL) {
		return 0;
	}
	return a->capacity;
}

void arenaReset(struct memarena *a)
{
	if (a == NULL || a->base == NULL || a->committedbytes == 0) {
		return;
	}
	/* Decommit but keep the reservation so the next stage reuses the same base. */
	VirtualFree(a->base, a->committedbytes, MEM_DECOMMIT);
	a->committedbytes = 0;
	a->capacity = 0;
}

void arenaRelease(struct memarena *a)
{
	if (a == NULL || a->base == NULL) {
		return;
	}
	VirtualFree(a->base, 0, MEM_RELEASE); /* size must be 0 for MEM_RELEASE */
	a->base = NULL;
	a->stride = 0;
	a->reservedbytes = 0;
	a->committedbytes = 0;
	a->capacity = 0;
}
