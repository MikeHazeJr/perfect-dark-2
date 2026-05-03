#ifndef PD_LOADER_ENUM_REVERSE_H
#define PD_LOADER_ENUM_REVERSE_H

#include <PR/ultratypes.h>

/*
 * port/include/loader_enum_reverse.h -- Catalog universality pivot Step 5
 * (2026-05-03). Symbolic <-> integer lookup tables for the ANIM_*, SFX_*,
 * FILE_*, L_GUN_* (lang) enum families.
 *
 * Forward resolvers (`loaderEnumResolve*`) are consumed internally by
 * loader_pool.c when parsing per-asset .pd* JSON. Reverse lookups
 * (`loaderEnumNameFor*`) are consumed by the romextract per-asset
 * emitters when writing symbolic field values back out to the on-disk
 * .pd<ext> compounds. Tables are generated from the relevant headers
 * (one-time scan; not regenerated at runtime).
 *
 * Lookups are linear scan because the loader runs once at startup; the
 * tables are small enough that bsearch would not move the needle.
 */

#ifdef __cplusplus
extern "C" {
#endif

s32 loaderEnumResolveAnimEnum(const char *name, s32 fallback);
s32 loaderEnumResolveSfxEnum(const char *name, s32 fallback);
s32 loaderEnumResolveLangEnum(const char *name, s32 fallback);
s32 loaderEnumResolveFileEnum(const char *name, s32 fallback);

const char *loaderEnumNameForAnimEnum(s32 value);
const char *loaderEnumNameForSfxEnum(s32 value);
const char *loaderEnumNameForLangEnum(s32 value);
const char *loaderEnumNameForFileEnum(s32 value);

/* Small-cardinality reverse lookups for the .pdhead / .pdbody / .pdarena
 * emitters (HEADBODYTYPE_* and ARENA_LOADMODE_*). Implemented inline in
 * port/src/loader_enum_reverse.c. */
const char *loaderEnumNameForHeadbodyType(s32 value);
const char *loaderEnumNameForArenaLoadMode(s32 value);

#ifdef __cplusplus
}
#endif

#endif /* PD_LOADER_ENUM_REVERSE_H */
