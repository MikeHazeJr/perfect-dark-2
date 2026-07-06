#include <ultra64.h>
#include "constants.h"
#include "bss.h"
#include "lib/boot.h"
#include "lib/crash.h"
#include "lib/memp.h"
#include "lib/mempc.h"
#include "data.h"
#include "types.h"
#include "system.h"

/**
 * memp - memory pool allocation system.
 *
 * Memp is the main memory allocation system in the game. Memp's heap size is
 * around 1MB without the expansion pak, and around 5MB with the expansion pak.
 *
 * There are other memory systems in the game, particularly mema, graphics
 * memory and the audio heap, which are all allocated out of memp.
 *
 * The memp system has two banks - onboard and expansion - which refer to the
 * onboard memory and expansion pak memory if present. If the expansion pak is
 * present, it's used entirely for memp.
 *
 * Each bank consists of 8 pools, which start off overlapping. Care must be
 * taken to not allocate from the wrong pool at the wrong time. In practice it
 * appears only two pools are used which makes this easy:
 *
 * MEMPOOL_PERMANENT (index 6) is for permanent data and is never cleared.
 * MEMPOOL_STAGE (index 4) is for general data and is cleared on stage load.
 *
 * After the permanent pool has finished its allocations, it is closed off and
 * the stage pool is then placed immediately after it. All allocations from
 * there on are made from the stage pool.
 *
 * Each pool has a start and end address. Allocations are typically served from
 * the left side of the pool but can also be allocated from the right side.
 * In practice right side allocations are only used once (by texture related
 * code).
 *
 * Resizing an allocation is also supported, but only from the left side and
 * only if it's the most recent allocation.
 *
 * Freeing individual allocations is not supported by memp. The only way to free
 * memp memory is to load a new stage which wipes the stage pool.
 *
 * M5 (S338): Pools now occupy separate, non-overlapping address regions.
 *   PERMANENT and STAGE no longer share the same physical memory, eliminating
 *   the class of corruption bugs where one pool would silently overwrite another.
 *
 * M6 (S338): Thread-safe via optional lock/unlock hooks registered by the port
 *   layer (SDL_mutex). All mutation points acquire the lock.
 */

/* M5: dedicated region sizes carved from the flat heap at init time. */
#define MEMP_PERMANENT_SIZE  (16u * 1024u * 1024u)  /* 16 MB — permanent session data */
#define MEMP_STAGE_SIZE      (40u * 1024u * 1024u)  /* 40 MB — stage-lifetime data    */
#define MEMP_POOL8_SIZE      ( 4u * 1024u * 1024u)  /*  4 MB — utils scratch (POOL_8) */

/* M6: optional port-supplied lock/unlock pair (registered by pdmain.c). */
static void (*s_mempLock)(void)   = NULL;
static void (*s_mempUnlock)(void) = NULL;

#define MEMP_LOCK()   do { if (s_mempLock)   s_mempLock();   } while (0)
#define MEMP_UNLOCK() do { if (s_mempUnlock) s_mempUnlock(); } while (0)

struct memorypool {
	/*0x00*/ u8 *start;
	/*0x04*/ u8 *leftpos;
	/*0x08*/ u8 *rightpos;
	/*0x0c*/ u8 *end;
	/*0x10*/ u8 *prevallocation;
};

struct memorypool g_MempOnboardPools[9];
struct memorypool g_MempExpansionPools[9];

void mempInit(void)
{
	// empty
}

/**
 * Register port-side lock/unlock functions for thread safety (M6).
 * Call once before any mempAlloc, ideally immediately after mempSetHeap.
 */
void mempSetLockFns(void (*lockFn)(void), void (*unlockFn)(void))
{
	s_mempLock   = lockFn;
	s_mempUnlock = unlockFn;
}

/* Initialise one pool descriptor for a dedicated address region. */
static void mempInitRegion(struct memorypool *pool, u8 *start, u32 size)
{
	pool->start          = start;
	pool->leftpos        = 0;       /* enabled by mempResetPool(), not here */
	pool->rightpos       = start + size;
	pool->end            = start + size;
	pool->prevallocation = 0;
}

/**
 * Initialise memp by carving the flat heap into dedicated, non-overlapping
 * regions for each active pool (M5).
 *
 * On N64, all pools shared the same address space (overlap was intentional to
 * maximise the tiny RAM). On PC, we have gigabytes; giving each pool its own
 * region eliminates the class of cross-pool corruption bugs.
 *
 * The heapstart/heaplen arguments are kept for API compatibility — the caller
 * (pdmain.c / server_main.c) still performs the initial sysMemZeroAlloc.
 */
void mempSetHeap(u8 *heapstart, u32 heaplen)
{
	s32 i;
	u8 *ptr       = heapstart;
	u32 remaining = heaplen;

	if (heaplen < MEMP_PERMANENT_SIZE + MEMP_STAGE_SIZE + MEMP_POOL8_SIZE) {
		sysFatalError(
			"MemorySize too low (%u MB). Minimum required: 64 MB.\n"
			"Delete pd.ini to reset to defaults.",
			heaplen / (1024u * 1024u));
	}

	/* Zero all pool descriptors. */
	for (i = 0; i < ARRAYCOUNT(g_MempOnboardPools); i++) {
		g_MempOnboardPools[i].start = 0;
		g_MempOnboardPools[i].leftpos = 0;
		g_MempOnboardPools[i].rightpos = 0;
		g_MempOnboardPools[i].end = 0;
		g_MempOnboardPools[i].prevallocation = 0;

		g_MempExpansionPools[i].start = 0;
		g_MempExpansionPools[i].leftpos = 0;
		g_MempExpansionPools[i].rightpos = 0;
		g_MempExpansionPools[i].end = 0;
		g_MempExpansionPools[i].prevallocation = 0;
	}

#define CARVE(pool_idx, size)                                                  \
	do {                                                                       \
		u32 _sz = (remaining >= (size)) ? (size) : remaining;                 \
		mempInitRegion(&g_MempOnboardPools[pool_idx], ptr, _sz);              \
		ptr += _sz; remaining -= _sz;                                         \
	} while (0)

	CARVE(MEMPOOL_PERMANENT, MEMP_PERMANENT_SIZE);   /* [base + 0,    +16 MB) */
	CARVE(MEMPOOL_STAGE,     MEMP_STAGE_SIZE);       /* [base + 16MB, +40 MB) */
	CARVE(MEMPOOL_8,         MEMP_POOL8_SIZE);       /* [base + 56MB, + 4 MB) */
	/* Remainder (~4 MB) is unassigned — reserved for future pools. */

#undef CARVE

	sysLogPrintf(LOG_NOTE, "MEMP: PERMANENT [%p, +16 MB)",
		g_MempOnboardPools[MEMPOOL_PERMANENT].start);
	sysLogPrintf(LOG_NOTE, "MEMP: STAGE     [%p, +40 MB)",
		g_MempOnboardPools[MEMPOOL_STAGE].start);
	sysLogPrintf(LOG_NOTE, "MEMP: POOL_8    [%p, + 4 MB)",
		g_MempOnboardPools[MEMPOOL_8].start);
}

/**
 * Return the amount of free space in the stage pool.
 *
 * M5: STAGE now lives in the onboard pool with its own dedicated region.
 */
u32 mempGetStageFree(void)
{
	return g_MempOnboardPools[MEMPOOL_STAGE].rightpos
	     - g_MempOnboardPools[MEMPOOL_STAGE].leftpos;
}

/**
 * M5: STAGE now lives in the onboard pool.
 */
void *mempGetNextStageAllocation(void)
{
	return g_MempOnboardPools[MEMPOOL_STAGE].leftpos;
}

void *mempAllocFromBank(struct memorypool *pool, u32 size, u8 poolnum)
{
	u8 *allocation;

	pool += poolnum;

	allocation = pool->leftpos;

	if (pool->leftpos == 0) {
		return allocation;
	}

	if (pool->leftpos > pool->rightpos) {
		sysLogPrintf(LOG_NOTE, "#warning: memory pool %x is full. Req: %d\n", pool, size);
		return 0;
	}

	if (pool->leftpos + size > pool->rightpos) {
		sysLogPrintf(LOG_NOTE, "#warning: memory pool %x is full. Req: %d\n", pool, size);
		return 0;
	}

	pool->leftpos += size;
	pool->prevallocation = allocation;

	if (1);

	return (void *)allocation;
}

/* -------------------------------------------------------------------------
 * B-952 heap-corruption hunt: red-zone canaries on STAGE-pool allocations.
 *
 * A stray write overruns some STAGE allocation and clobbers an adjacent one --
 * it nulls a chr modeldef's rootnode between spawn and first tick (skedarruins
 * chrnum 5052) and corrupts other model memory. The corruption is a sub-region
 * of the single 40 MB MEMPOOL_STAGE bump block, so stock ASAN can't see it.
 * Enabled by `--memp-canary`. Each STAGE alloc gets MEMP_CANARY_BYTES of magic
 * appended; mempCheckCanaries() (called ~once/frame from chrTick) scans them and
 * logs the FIRST corrupted allocation + the caller that allocated it -- the
 * overflowing edge, whose caller is the likely culprit. Debug-only; off unless
 * the flag is passed, so zero cost in normal builds/runs. */
#define MEMP_CANARY_MAGIC 0xb952b952u
#define MEMP_CANARY_WORDS 4
#define MEMP_CANARY_BYTES (MEMP_CANARY_WORDS * 4)
#define MEMP_CANARY_MAX   131072
struct mempcanary { u8 *loc; u32 size; void *caller; };
static struct mempcanary s_MempCanaries[MEMP_CANARY_MAX];
static s32 s_MempCanaryCount = 0;
static s8 s_MempCanaryEnabled = -1; /* -1 = not yet resolved from args */

void mempCheckCanaries(void)
{
	s32 i, j;
	static s32 s_firstscan = -1;

	if (s_MempCanaryCount <= 0) {
		return;
	}

	if (s_firstscan < 0) {
		s_firstscan = s_MempCanaryCount;
		sysLogPrintf(LOG_NOTE, "MEMP.CANARY.SCAN: active, %d STAGE canaries at first scan", s_MempCanaryCount);
	}

	for (i = 0; i < s_MempCanaryCount; i++) {
		u32 *c = (u32 *)s_MempCanaries[i].loc;

		for (j = 0; j < MEMP_CANARY_WORDS; j++) {
			if (c[j] != MEMP_CANARY_MAGIC) {
				sysLogPrintf(LOG_ERROR,
					"MEMP.CANARY.CORRUPT: alloc#%d loc=%p size=%u caller=%p word%d=0x%08x -- this STAGE allocation was overrun (its caller/the next writer is the B-952 culprit)",
					i, (void *)s_MempCanaries[i].loc, s_MempCanaries[i].size,
					(void *)s_MempCanaries[i].caller, j, c[j]);
				c[j] = MEMP_CANARY_MAGIC; /* repair so each corruption logs once */
				break;
			}
		}
	}
}

void *mempAlloc(u32 len, u8 pool)
{
	void *allocation;
	u32 alloclen = len;

	/* s_MempCanaryEnabled is resolved once per stage reset (post-args) in
	 * mempResetPool, so this hot path stays a single cached compare when off. */
	if (s_MempCanaryEnabled > 0 && pool == MEMPOOL_STAGE && len > 0) {
		alloclen = len + MEMP_CANARY_BYTES;
	}

	MEMP_LOCK();
	allocation = mempAllocFromBank(g_MempOnboardPools, alloclen, pool);

	if (!allocation) {
		allocation = mempAllocFromBank(g_MempExpansionPools, alloclen, pool);
	}

	if (allocation && alloclen != len) {
		u8 *canary = (u8 *)allocation + len;
		s32 i;

		for (i = 0; i < MEMP_CANARY_WORDS; i++) {
			((u32 *)canary)[i] = MEMP_CANARY_MAGIC;
		}

		if (s_MempCanaryCount < MEMP_CANARY_MAX) {
			s_MempCanaries[s_MempCanaryCount].loc = canary;
			s_MempCanaries[s_MempCanaryCount].size = len;
			s_MempCanaries[s_MempCanaryCount].caller = __builtin_return_address(0);
			s_MempCanaryCount++;
		}
	}
	MEMP_UNLOCK();

	if (!allocation && len) {
#if VERSION < VERSION_NTSC_1_0
#ifdef DEBUG
		if (pool != MEMPOOL_8 && pool != MEMPOOL_7) {
			char buffer[80];
			u32 free;
			u32 sz;

			if (pool == MEMPOOL_STAGE) {
				free = mempGetPoolFree(MEMPOOL_STAGE, MEMBANK_ONBOARD);
				sz   = mempGetPoolSize(MEMPOOL_STAGE, MEMBANK_ONBOARD);
				snprintf(buffer, sizeof(buffer), "Out of mem - LEV: %d f %d s %d", len, free, sz);
			} else {
				free = mempGetPoolFree(MEMPOOL_PERMANENT, MEMBANK_ONBOARD);
				sz   = mempGetPoolSize(MEMPOOL_PERMANENT, MEMBANK_ONBOARD);
				snprintf(buffer, sizeof(buffer), "Out of mem - ETR: %d f %d s %d", len, free, sz);
			}

			crashSetMessage(buffer);
			CRASH();
		}
#endif
#endif
	}

	return allocation;
}

/**
 * Reallocate the given allocation in the given pool.
 * The pointer will remain unchanged.
 *
 * The allocation must be the most recent allocation.
 *
 * @dangerous: This function does not check the limits of the memory pool.
 * If it allocates past the rightpos of the pool it could lead to memory corruption.
 */
s32 mempRealloc(void *allocation, s32 newsize, u8 poolnum)
{
	struct memorypool *pool;
	s32 origsize;
	s32 growsize;

	MEMP_LOCK();

	pool = &g_MempOnboardPools[poolnum];

	if (pool->prevallocation != allocation) {
		pool = &g_MempExpansionPools[poolnum];

		if (pool->prevallocation != allocation) {
			MEMP_UNLOCK();
			return 2;
		}
	}

	origsize = pool->leftpos - pool->prevallocation;
	growsize = newsize - origsize;

	if (growsize <= 0) {
		pool->leftpos += growsize;
		pool->leftpos = (u8 *)ALIGN16((uintptr_t) pool->leftpos);
		MEMP_UNLOCK();
		return 1;
	}

	pool->leftpos += growsize;
	MEMP_UNLOCK();
	return 1;
}

void memp000124cc(void)
{
	// empty
}

/**
 * Return the amount of free space in the given pool and bank.
 */
u32 mempGetPoolFree(u8 poolnum, u32 bank)
{
	struct memorypool *pool;

	if (bank == MEMBANK_ONBOARD) {
		pool = &g_MempOnboardPools[poolnum];
	} else {
		pool = &g_MempExpansionPools[poolnum];
	}

	return pool->rightpos - pool->leftpos;
}

#ifdef DEBUG
u32 mempGetPoolSize(u8 poolnum, u32 bank)
{
	struct memorypool *pool;

	if (bank == MEMBANK_ONBOARD) {
		pool = &g_MempOnboardPools[poolnum];
	} else {
		pool = &g_MempExpansionPools[poolnum];
	}

	return pool->rightpos - pool->start;
}
#endif

#if VERSION < VERSION_NTSC_1_0
void *mempAllocFromPackedWord(u32 word)
{
	return mempAlloc(word >> 4, word & 0x0f);
}
#endif

/**
 * Reset the pool's left side to its start address, effectively freeing the left
 * side of the pool.
 *
 * M5: STAGE and PERMANENT now occupy separate address regions. On STAGE reset,
 * we simply reset STAGE to its own start — no repositioning against PERMANENT
 * is needed. PERMANENT's boundary is managed independently.
 */
void mempResetPool(u8 pool)
{
	if (pool == MEMPOOL_STAGE) {
		/* Validate persistent PC allocations before the stage pool is wiped.
		 * If any persistent data (fonts, etc.) has been overwritten by a stray
		 * stage-pool write, the canaries will catch it here and log the culprit.
		 * Called outside the lock — mempPCValidate only reads mempc list (stdlib). */
		mempPCValidate("mempResetPool(STAGE)");
	}

	MEMP_LOCK();
	g_MempOnboardPools[pool].leftpos = g_MempOnboardPools[pool].start;
	g_MempExpansionPools[pool].leftpos = g_MempExpansionPools[pool].start;
	g_MempOnboardPools[pool].prevallocation = 0;
	g_MempExpansionPools[pool].prevallocation = 0;

	if (pool == MEMPOOL_STAGE) {
		s32 want = sysArgCheck("--memp-canary") ? 1 : 0;
		s_MempCanaryCount = 0; /* B-952: old canaries point into the wiped pool */
		if (s_MempCanaryEnabled != want) {
			s_MempCanaryEnabled = want; /* resolved post-args, cached for the hot path */
			if (want) {
				sysLogPrintf(LOG_NOTE, "MEMP.CANARY: enabled -- %d-byte red zone per STAGE alloc", MEMP_CANARY_BYTES);
			}
		}
	}

	MEMP_UNLOCK();
}

/**
 * Setting leftpos to 0 means that this pool will refuse allocations from the
 * left.
 *
 * Setting rightpos to the end means it's resetting the right side and making
 * that available for allocations. It would have made more sense to do this in
 * mempResetPool instead.
 */
void mempDisablePool(u8 pool)
{
	MEMP_LOCK();
	g_MempOnboardPools[pool].leftpos = 0;
	g_MempExpansionPools[pool].leftpos = 0;
	g_MempOnboardPools[pool].rightpos = g_MempOnboardPools[pool].end;
	g_MempExpansionPools[pool].rightpos = g_MempExpansionPools[pool].end;
	MEMP_UNLOCK();
}

void *mempAllocFromBankRight(struct memorypool *pool, u32 size, u8 poolnum)
{
	u8 *allocation;

	pool += poolnum;

	allocation = pool->rightpos;

	if (allocation == 0) {
		return allocation;
	}

	if (pool->rightpos < pool->leftpos) {
		return 0;
	}

	if (pool->rightpos - size < pool->leftpos) {
		return 0;
	}

	pool->rightpos -= size;

	return (void *)pool->rightpos;
}

void *mempAllocFromRight(u32 len, u8 pool)
{
	void *allocation;

	MEMP_LOCK();
	allocation = mempAllocFromBankRight(g_MempOnboardPools, len, pool);

	if (!allocation) {
		allocation = mempAllocFromBankRight(g_MempExpansionPools, len, pool);
	}
	MEMP_UNLOCK();

	return allocation;
}
