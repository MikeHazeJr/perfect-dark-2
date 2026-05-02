/*
 * port/src/catalog_mgr_arenas_pure.c -- Catalog Gate 3 Arenas F1: pure
 * validators for the catalog manager arenas module.
 *
 * No globals, no I/O. Linked into pd-tests so the contract can be
 * pinned without dragging globals. The live router
 * (port/src/catalog_mgr_arenas.c) delegates to these helpers.
 */

#include <stddef.h>
#include <string.h>
#include "catalog_mgr_arenas_pure.h"

s32 catalogMgrArenaIsInRangePure(s32 arena_index)
{
	if (arena_index < 0) {
		return 0;
	}
	if (arena_index >= CATALOG_MGR_ARENA_COUNT_PURE) {
		return 0;
	}
	return 1;
}

u32 catalogMgrArenaCategoryToMaskPure(const char *category)
{
	if (category == NULL || category[0] == '\0') {
		return 0;
	}
	if (strcmp(category, "Dark") == 0) {
		return CATALOG_MGR_ARENA_RNDMASK_DARK;
	}
	if (strcmp(category, "Classic") == 0) {
		return CATALOG_MGR_ARENA_RNDMASK_CLASSIC;
	}
	if (strcmp(category, "Bonus") == 0) {
		return CATALOG_MGR_ARENA_RNDMASK_BONUS;
	}
	if (strcmp(category, "Solo Missions") == 0) {
		return CATALOG_MGR_ARENA_RNDMASK_SOLOMISSIONS;
	}
	/* "Random" and unknown categories return 0: a Random meta arena
	 * cannot be the resolution target of another Random pick (would
	 * recurse). Live consumer in src/game/mplayer/setup.c::categoryToMask
	 * has the same behaviour. */
	return 0;
}

const char *catalogMgrArenaSlugFromIdPure(const char *catalog_id)
{
	const char *colon;
	const char *prefix;

	if (catalog_id == NULL || catalog_id[0] == '\0') {
		return NULL;
	}

	colon = strchr(catalog_id, ':');
	if (colon == NULL) {
		return NULL;
	}

	/* After the colon, the canonical form is "arena_<slug>". Reject any
	 * shape that does not start with the literal "arena_" so a future
	 * mod ID like "modid:something_else" does not silently extract a
	 * bogus slug. */
	prefix = colon + 1;
	if (strncmp(prefix, "arena_", 6) != 0) {
		return NULL;
	}
	if (prefix[6] == '\0') {
		return NULL;
	}
	return prefix + 6;
}
