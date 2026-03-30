/**
 * mempc.c — PC persistent memory allocator.
 *
 * Provides malloc-backed allocations that survive stage transitions.
 * On the N64 every byte counted and the bump allocator made sense;
 * on PC we have gigabytes to spare and should not recycle memory that
 * holds read-only data like fonts or palettes.
 *
 * Allocations are tracked in a linked list so we can:
 *   - enumerate them for debug display
 *   - validate guard canaries to detect buffer overruns
 *   - free everything on shutdown (leak-free exit)
 *
 * In debug builds, each allocation is sandwiched between 16-byte
 * canary regions filled with a known pattern (0xCD).  mempPCValidate()
 * walks the list and checks every canary, logging corruption with the
 * original caller tag so you know exactly which allocation was overrun.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ultra64.h>
#include "constants.h"
#include "lib/memp.h"
#include "lib/mempc.h"
#include "lib/main.h"
#include "types.h"
#include "system.h"

/* ---- configuration ---- */

#define GUARD_SIZE     16           /* bytes of canary on each side */
#define GUARD_FILL     0xCD         /* byte pattern for canaries     */
#define MAX_TAG_LEN    48           /* caller tag string length      */

/* ---- internal bookkeeping ---- */

struct pcalloc {
	struct pcalloc *next;
	u32             size;           /* user-requested size (excl. guards) */
	char            tag[MAX_TAG_LEN];
#ifndef NDEBUG
	u8              guardBefore[GUARD_SIZE];
#endif
	/* user data follows immediately after guardBefore[] (or after tag in release) */
};

static struct pcalloc *s_Head = NULL;
static u32 s_TotalAllocated = 0;
static u32 s_NumAllocations = 0;

/* ---- helpers ---- */

static inline u8 *userPtr(struct pcalloc *a)
{
#ifndef NDEBUG
	return (u8 *)a + offsetof(struct pcalloc, guardBefore) + GUARD_SIZE;
#else
	return (u8 *)a + sizeof(struct pcalloc);
#endif
}

static inline u8 *guardAfter(struct pcalloc *a)
{
	return userPtr(a) + a->size;
}

static void fillGuards(struct pcalloc *a)
{
#ifndef NDEBUG
	memset(a->guardBefore, GUARD_FILL, GUARD_SIZE);
	memset(guardAfter(a), GUARD_FILL, GUARD_SIZE);
#endif
}

static bool checkGuards(struct pcalloc *a, const char *context)
{
#ifndef NDEBUG
	bool ok = true;
	for (int i = 0; i < GUARD_SIZE; i++) {
		if (a->guardBefore[i] != GUARD_FILL) {
			sysLogPrintf(LOG_WARNING,
				"MEMPC [%s]: UNDERFLOW detected in '%s' (offset -%d, "
				"expected 0x%02x got 0x%02x)",
				context, a->tag, GUARD_SIZE - i,
				GUARD_FILL, a->guardBefore[i]);
			ok = false;
			break;
		}
	}
	u8 *after = guardAfter(a);
	for (int i = 0; i < GUARD_SIZE; i++) {
		if (after[i] != GUARD_FILL) {
			sysLogPrintf(LOG_WARNING,
				"MEMPC [%s]: OVERFLOW detected in '%s' (offset +%d past %u bytes, "
				"expected 0x%02x got 0x%02x)",
				context, a->tag, i, a->size,
				GUARD_FILL, after[i]);
			ok = false;
			break;
		}
	}
	return ok;
#else
	return true;
#endif
}

/* ---- public API ---- */

/**
 * Allocate persistent PC memory that survives stage transitions.
 *
 * @param size  Number of bytes to allocate.
 * @param tag   Human-readable label for debugging (e.g. "FontHandelGothicSm").
 * @return      Pointer to zeroed memory, or NULL on failure.
 */
void *mempPCAlloc(u32 size, const char *tag)
{
	u32 headerSize = sizeof(struct pcalloc);
	u32 totalSize = headerSize + size;
#ifndef NDEBUG
	totalSize += GUARD_SIZE; /* guardAfter (guardBefore is inside struct) */
#endif

	struct pcalloc *a = (struct pcalloc *)malloc(totalSize);
	if (!a) {
		sysLogPrintf(LOG_WARNING, "MEMPC: malloc failed for '%s' (%u bytes)", tag, size);
		return NULL;
	}

	memset(a, 0, totalSize);
	a->size = size;
	if (tag) {
		strncpy(a->tag, tag, MAX_TAG_LEN - 1);
		a->tag[MAX_TAG_LEN - 1] = '\0';
	} else {
		strcpy(a->tag, "(unnamed)");
	}

	fillGuards(a);

	/* link into list */
	a->next = s_Head;
	s_Head = a;

	s_TotalAllocated += size;
	s_NumAllocations++;

	sysLogPrintf(LOG_NOTE, "MEMPC: alloc '%s' %u bytes at %p (total: %u in %u allocs)",
		a->tag, size, (void *)userPtr(a), s_TotalAllocated, s_NumAllocations);

	return userPtr(a);
}

/**
 * Validate all persistent allocations.
 * Checks guard canaries for buffer overruns/underruns.
 * Call periodically or at critical transitions (stage load, endscreen).
 *
 * @param context  Label for log messages (e.g. "stageReset", "endscreen").
 * @return         true if all allocations are intact.
 */
bool mempPCValidate(const char *context)
{
	bool allOk = true;
	u32 count = 0;

	for (struct pcalloc *a = s_Head; a; a = a->next) {
		if (!checkGuards(a, context)) {
			allOk = false;
		}
		count++;
	}

	if (allOk) {
		sysLogPrintf(LOG_NOTE, "MEMPC [%s]: all %u allocations intact (%u bytes total)",
			context, count, s_TotalAllocated);
	}

	return allOk;
}

/**
 * Free all persistent allocations (call at shutdown).
 */
void mempPCFreeAll(void)
{
	struct pcalloc *a = s_Head;
	while (a) {
		struct pcalloc *next = a->next;
		free(a);
		a = next;
	}
	s_Head = NULL;
	s_TotalAllocated = 0;
	s_NumAllocations = 0;
}

/**
 * Get total bytes allocated in the persistent pool.
 */
u32 mempPCGetTotalAllocated(void)
{
	return s_TotalAllocated;
}

/**
 * Get number of persistent allocations.
 */
u32 mempPCGetNumAllocations(void)
{
	return s_NumAllocations;
}
