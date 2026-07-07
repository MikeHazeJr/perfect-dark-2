/*
 * arenapool -- growable pointer-stable slot pool over memarena. See
 * port/include/arenapool.h. Thin: all memory mechanics live in memarena; this
 * layer only tracks the logical slot count and the grow granularity so the many
 * fixed-size stage pools convert to one uniform, tested shape.
 */
#include <stddef.h>

#include "types.h"
#include "system.h"
#include "memarena.h"
#include "arenapool.h"

void *arenaPoolSetup(struct arenapool *p, const char *name, size_t stride,
		size_t reservemax, s32 chunk, s32 count)
{
	void *base;

	if (p == NULL || count < 0) {
		return NULL;
	}

	/* Reserve the virtual range on first use; idempotent afterwards. */
	if (p->arena.base == NULL) {
		if (!arenaReserve(&p->arena, name, stride, reservemax)) {
			return NULL;
		}
	}

	p->chunk = (chunk > 0) ? chunk : 64;

	base = arenaEnsure(&p->arena, (size_t)count);
	if (base == NULL) {
		return NULL;
	}

	p->count = count;
	return base;
}

void *arenaPoolGrow(struct arenapool *p, s32 *firstnew)
{
	s32 newcount;
	void *base;

	if (firstnew != NULL) {
		*firstnew = -1;
	}

	if (p == NULL || p->arena.base == NULL) {
		return NULL;
	}

	newcount = p->count + (p->chunk > 0 ? p->chunk : 64);

	base = arenaEnsure(&p->arena, (size_t)newcount);
	if (base == NULL) {
		sysLogPrintf(LOG_ERROR,
			"arenaPoolGrow(%s): cannot grow past %d slots (reservation exhausted)",
			p->arena.name ? p->arena.name : "?", p->count);
		return NULL; /* p->count unchanged */
	}

	if (firstnew != NULL) {
		*firstnew = p->count;
	}
	p->count = newcount;
	return base;
}

void arenaPoolReset(struct arenapool *p)
{
	if (p == NULL) {
		return;
	}
	arenaReset(&p->arena);
	p->count = 0;
}
