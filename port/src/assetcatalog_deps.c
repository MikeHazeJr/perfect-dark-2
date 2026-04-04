/**
 * assetcatalog_deps.c -- Phase 2: Catalog dependency graph implementation
 *
 * Dynamically allocated table of (owner, dep) pairs.  Populated by the
 * scanner; queried by the manifest build functions.  Grows by doubling from
 * CATALOG_MAX_DEP_PAIRS initial capacity so mods with many dependencies
 * never hit a silent drop.
 *
 * See assetcatalog_deps.h for design rationale.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <PR/ultratypes.h>
#include "types.h"
#include "system.h"
#include "assetcatalog_deps.h"

/* =========================================================================
 * Internal types
 * ========================================================================= */

typedef struct {
    u32  owner_hash;                    /* FNV-1a of owner_id (fast compare) */
    char owner_id[64];                  /* catalog string ID of the owning entry */
    char dep_id[64];                    /* catalog string ID of the dependency */
    s32  is_bundled;                    /* 1 = base-game owner, 0 = mod owner */
} s_DepPair;

/* =========================================================================
 * Module state
 * ========================================================================= */

static s_DepPair *s_DepTable    = NULL;   /* heap-allocated, grows on demand */
static s32        s_DepCap      = 0;      /* allocated capacity */
static s32        s_NumDepPairs = 0;

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/* FNV-1a hash of a NUL-terminated string */
static u32 s_fnv1a(const char *str)
{
    u32 h = 0x811c9dc5u;
    while (*str) {
        h ^= (u8)*str++;
        h *= 0x01000193u;
    }
    return h;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void catalogDepRegister(const char *owner_id, const char *dep_id,
                        s32 is_bundled)
{
    s32 i;
    u32 ohash;

    if (!owner_id || owner_id[0] == '\0' ||
        !dep_id   || dep_id[0]   == '\0') {
        return;
    }

    ohash = s_fnv1a(owner_id);

    /* Dedup: skip if this exact pair already exists */
    for (i = 0; i < s_NumDepPairs; i++) {
        if (s_DepTable[i].owner_hash == ohash &&
            strcmp(s_DepTable[i].owner_id, owner_id) == 0 &&
            strcmp(s_DepTable[i].dep_id,   dep_id)   == 0) {
            return;
        }
    }

    if (s_NumDepPairs >= s_DepCap) {
        /* Grow by doubling; initial alloc uses CATALOG_MAX_DEP_PAIRS as base */
        s32 newCap = (s_DepCap > 0) ? s_DepCap * 2 : CATALOG_MAX_DEP_PAIRS;
        s_DepPair *newTable = (s_DepPair *)realloc(s_DepTable,
                                                    (size_t)newCap * sizeof(s_DepPair));
        if (!newTable) {
            sysLogPrintf(LOG_WARNING,
                         "CATALOG-DEPS: realloc failed (cap=%d), dropping dep '%s' -> '%s'",
                         s_DepCap, owner_id, dep_id);
            return;
        }
        s_DepTable = newTable;
        s_DepCap   = newCap;
    }

    s_DepTable[s_NumDepPairs].owner_hash = ohash;
    strncpy(s_DepTable[s_NumDepPairs].owner_id, owner_id,
            sizeof(s_DepTable[0].owner_id) - 1);
    s_DepTable[s_NumDepPairs].owner_id[sizeof(s_DepTable[0].owner_id) - 1] = '\0';
    strncpy(s_DepTable[s_NumDepPairs].dep_id, dep_id,
            sizeof(s_DepTable[0].dep_id) - 1);
    s_DepTable[s_NumDepPairs].dep_id[sizeof(s_DepTable[0].dep_id) - 1] = '\0';
    s_DepTable[s_NumDepPairs].is_bundled = is_bundled;

    s_NumDepPairs++;
}

void catalogDepForEach(const char *owner_id,
                       CatalogDepIterFn fn, void *userdata)
{
    s32 i;
    u32 ohash;

    if (!owner_id || owner_id[0] == '\0' || !fn) {
        return;
    }

    ohash = s_fnv1a(owner_id);

    for (i = 0; i < s_NumDepPairs; i++) {
        /* Skip bundled pairs -- base-game assets are always ROM-resident */
        if (s_DepTable[i].is_bundled) {
            continue;
        }
        if (s_DepTable[i].owner_hash != ohash) {
            continue;
        }
        if (strcmp(s_DepTable[i].owner_id, owner_id) != 0) {
            continue;
        }
        fn(s_DepTable[i].dep_id, userdata);
    }
}

void catalogDepClearMods(void)
{
    s32 i;
    s32 write;

    /* Compact: keep only bundled pairs */
    write = 0;
    for (i = 0; i < s_NumDepPairs; i++) {
        if (s_DepTable[i].is_bundled) {
            if (write != i) {
                s_DepTable[write] = s_DepTable[i];
            }
            write++;
        }
    }
    s_NumDepPairs = write;
}

void catalogDepClear(void)
{
    free(s_DepTable);
    s_DepTable    = NULL;
    s_DepCap      = 0;
    s_NumDepPairs = 0;
}

s32 catalogDepCount(void)
{
    return s_NumDepPairs;
}
