#ifndef _IN_PORT_MEMARENA_H
#define _IN_PORT_MEMARENA_H

#include <stddef.h>
#include "PR/ultratypes.h"  /* lightweight u8/s32 typedefs (NOT types.h -- that
                             * drags in os_libc.h whose C-linkage sprintf clashes
                             * with <cstdio> when this header is used from C++) */

/*
 * memarena -- reserve-and-commit growable arena (pointer-stable dynamic array).
 *
 * WHY: several game pools (chrs, models, anims, projectiles, effects, objects)
 * are fixed-size arrays sized at stage load, but must be able to GROW at runtime
 * for large mods (Mike's directive: "limits should be dynamic, not the N64's").
 * They cannot simply realloc: the code holds raw pointers INTO these pools
 * (prop->chr) and does pointer arithmetic on them (`chr - g_ChrSlots`,
 * `chr >= g_ChrSlots && chr < g_ChrSlots + g_NumChrSlots`) at ~100 sites, so the
 * backing store must be BOTH contiguous AND never move.
 *
 * HOW: reserve a large contiguous virtual address range once (cheap -- reserving
 * address space commits no RAM), then commit pages on demand as the pool grows.
 * The base address is fixed at reserve time and never changes, so existing
 * pointers and indices stay valid across growth. Growth is bounded by the
 * reservation (a virtual cap chosen far above any real content need); within it
 * the pool grows to whatever is asked. Windows-only (VirtualAlloc); the project
 * is Windows-only.
 *
 * This is deliberately NOT a linked block-list: a block-list would break the
 * `chr - g_ChrSlots` pointer arithmetic that is woven through networking and chr
 * tracking. Contiguity is a hard requirement here, so reserve-and-commit is the
 * correct shape.
 */
struct memarena {
	u8 *base;              /* reserved base address (STABLE); NULL until reserved */
	size_t stride;         /* bytes per element */
	size_t reservedbytes;  /* total reserved virtual bytes (the hard cap)         */
	size_t committedbytes; /* currently committed bytes                           */
	size_t capacity;       /* committedbytes / stride == usable element count     */
	const char *name;      /* short label for logging                             */
};

/*
 * Reserve address space for up to `maxelements` records of `stride` bytes. No
 * memory is committed yet (capacity 0). `name` is retained for log messages and
 * must outlive the arena (use a string literal). Returns 1 on success, 0 on
 * failure (bad args or the OS refused the reservation).
 */
s32 arenaReserve(struct memarena *a, const char *name, size_t stride, size_t maxelements);

/*
 * Ensure at least `elements` records are committed and usable. Grows by
 * committing whole pages; previously-committed data is preserved and freshly
 * committed pages are zero-filled by the OS. Returns the (stable) base pointer,
 * or NULL if the request exceeds the reservation or the OS refused the commit
 * (the arena is left at its previous capacity in that case). Shrinking is not
 * performed -- passing a smaller `elements` is a no-op that returns base.
 */
void *arenaEnsure(struct memarena *a, size_t elements);

/* Usable element capacity currently committed. 0 if not reserved. */
size_t arenaCapacity(const struct memarena *a);

/*
 * Decommit all pages back to capacity 0 but KEEP the reservation, so the next
 * stage reuses the same address range (and the same stable base). Cheap; call on
 * stage teardown.
 */
void arenaReset(struct memarena *a);

/* Release the reservation entirely; base becomes NULL. */
void arenaRelease(struct memarena *a);

#endif
