#ifndef _IN_GAME_SPARKS_H
#define _IN_GAME_SPARKS_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

/* c3849 Unit 8 (B6.9): number of rows in the static g_SparkTypes table
 * (identical for the PAL and NTSC initializer blocks; pinned by a
 * _Static_assert in sparks.c). Indices below this are OG base rows; indices
 * at or above it address the bounded custom-row registry in
 * sparks_custom.c. */
#define SPARKTYPE_BASE_COUNT 27

void sparksReset(void);

void sparksTick(void);

void sparkCreate(struct coord *pos, struct sparktype *type);
void sparkgroupEnsureFreeSparkSlot(struct sparkgroup *group);
void sparksCreate(s32 room, struct prop *prop, struct coord *pos, struct coord *arg3, struct coord *dir, s32 type);
Gfx *sparksRender(Gfx *gdl);

/* c3849 Unit 8 (B6.9): runtime spark-row registry (sparks_custom.c).
 *
 * sparkTypeFor replaces the direct g_SparkTypes[typenum] reads: base indices
 * (< SPARKTYPE_BASE_COUNT) resolve into the OG table exactly as before;
 * registry indices resolve into the custom rows; anything stale or out of
 * range falls back to the SPARKTYPE_DEFAULT row so a cleared registry can
 * never produce an out-of-bounds read from a live spark group.
 *
 * sparksRegisterCustomType appends a row (deduplicated by value) and returns
 * its typenum (>= SPARKTYPE_BASE_COUNT), or -1 with a loud
 * SPARK.CUSTOM_ROW_FAIL log on exhaustion. sparksRegisterCustomTintedType is
 * the effect-runtime seam: it clones the OG SPARKTYPE_PROJECTILE row with
 * the two RGBA colour words swapped in. sparksResetCustomTypes empties the
 * registry (wired into effectGraphRuntimeClearAll so mod reloads do not
 * leak rows). */
struct sparktype *sparkTypeFor(s32 typenum);
void sparksSetBaseProfileOverride(const struct sparktype *rows, s32 count);
void sparksClearBaseProfileOverride(void);
s32 sparksRegisterCustomType(const struct sparktype *row);
s32 sparksRegisterCustomTintedType(u32 color1, u32 color2);
void sparksResetCustomTypes(void);
s32 sparksCustomTypeCount(void);

/* Layout-free accessors for C++ tests/tools (types.h's bool macro keeps
 * struct sparktype out of C++ translation units): colour words of a live
 * row, and a clone check that compares every field EXCEPT the two colour
 * words against a base row. Both return 0 for out-of-range typenums. */
s32 sparksTypeColors(s32 typenum, u32 *color1, u32 *color2);
s32 sparksTypeClonesRowExceptColors(s32 typenum, s32 base_typenum);

#endif
