/**
 * sparks_custom.c -- runtime spark-row registry (c3849 Unit 8, B6.9).
 *
 * The OG spark table (g_SparkTypes, sparks.c) is a fixed 27-row static array
 * whose colour words (unk1c/unk20) are render-consumed, which makes spark
 * colour the one effect channel that IS parameterizable without renderer
 * work (explosion fireballs render hard-white; smoke rows have 28 direct
 * index sites -- both deferred). This registry appends bounded custom rows
 * ABOVE the base table so .pdeffect graphs can deliver tinted sparks (the
 * needler pink proving asset) while base indices < SPARKTYPE_BASE_COUNT stay
 * byte-identical OG behavior.
 *
 * This lives in its own translation unit (not sparks.c) deliberately: the
 * registry is pure (table + constants + log), so pd-tests links it directly
 * and the [effect_graph] tests exercise the REAL row allocation instead of a
 * stub. sparks.c/sparkstick.c route their three g_SparkTypes reads through
 * sparkTypeFor below.
 *
 * Lifetime: growable and append-only between resets. sparksResetCustomTypes is wired into
 * effectGraphRuntimeClearAll (the effect-record reset path), NOT into the
 * per-stage sparksReset -- registered rows are mod-lifetime data, not stage
 * state. Re-registration of an identical row is deduplicated by value, so a
 * mod reload cycle does not consume new slots; a re-tinted row does take a
 * fresh slot until the next full clear. Allocation failure is loud.
 */

#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include "constants.h"
#include "data.h"
#include "types.h"
#include "game/sparks.h"
#include "system.h"

static struct sparktype *s_CustomSparkTypes;
static s32 s_NumCustomSparkTypes = 0;
static s32 s_CustomSparkTypeCapacity = 0;
static struct sparktype s_BaseProfileOverride[SPARKTYPE_BASE_COUNT];
static s32 s_HasBaseProfileOverride;

struct sparktype *sparkTypeFor(s32 typenum)
{
	if (typenum >= 0 && typenum < SPARKTYPE_BASE_COUNT) {
		return s_HasBaseProfileOverride
			? &s_BaseProfileOverride[typenum] : &g_SparkTypes[typenum];
	}
	if (typenum >= SPARKTYPE_BASE_COUNT &&
			typenum < SPARKTYPE_BASE_COUNT + s_NumCustomSparkTypes) {
		return &s_CustomSparkTypes[typenum - SPARKTYPE_BASE_COUNT];
	}
	/* Stale custom index (registry cleared under a live spark group) or a
	 * corrupt typenum: fail safe into the OG default row rather than read
	 * out of bounds. */
	return s_HasBaseProfileOverride
		? &s_BaseProfileOverride[SPARKTYPE_DEFAULT]
		: &g_SparkTypes[SPARKTYPE_DEFAULT];
}

void sparksSetBaseProfileOverride(const struct sparktype *rows, s32 count)
{
	if (!rows || count != SPARKTYPE_BASE_COUNT) {
		sysLogPrintf(LOG_ERROR,
			"EFFECT.PROFILE.SPARK: rejected row count %d (expected %d)",
			count, SPARKTYPE_BASE_COUNT);
		return;
	}
	memcpy(s_BaseProfileOverride, rows, sizeof(s_BaseProfileOverride));
	s_HasBaseProfileOverride = 1;
}

void sparksClearBaseProfileOverride(void)
{
	s_HasBaseProfileOverride = 0;
}

/* Field compare instead of memcmp: struct sparktype carries alignment
 * padding (between numsparks and unk18) whose bytes are unspecified after
 * struct assignment. */
static s32 sparkRowsEqual(const struct sparktype *a, const struct sparktype *b)
{
	return a->unk00 == b->unk00 && a->unk02 == b->unk02 &&
		a->unk04 == b->unk04 && a->unk06 == b->unk06 &&
		a->unk08 == b->unk08 && a->unk0a == b->unk0a &&
		a->weight == b->weight && a->maxage == b->maxage &&
		a->unk12 == b->unk12 && a->numsparks == b->numsparks &&
		a->unk18 == b->unk18 && a->unk1c == b->unk1c &&
		a->unk20 == b->unk20 && a->decel == b->decel;
}

s32 sparksRegisterCustomType(const struct sparktype *row)
{
	s32 i;

	if (row == NULL) {
		return -1;
	}

	for (i = 0; i < s_NumCustomSparkTypes; i++) {
		if (sparkRowsEqual(&s_CustomSparkTypes[i], row)) {
			return SPARKTYPE_BASE_COUNT + i;
		}
	}

	if (s_NumCustomSparkTypes == s_CustomSparkTypeCapacity) {
		s32 next = s_CustomSparkTypeCapacity ? s_CustomSparkTypeCapacity * 2 : 16;
		if (next <= s_CustomSparkTypeCapacity ||
				(size_t)next > (size_t)-1 / sizeof(*s_CustomSparkTypes)) {
			sysLogPrintf(LOG_WARNING,
				"SPARK.CUSTOM_ROW_FAIL: custom spark registry size overflow");
			return -1;
		}
		struct sparktype *grown = (struct sparktype *)realloc(
			s_CustomSparkTypes, (size_t)next * sizeof(*s_CustomSparkTypes));
		if (!grown) {
			sysLogPrintf(LOG_WARNING,
				"SPARK.CUSTOM_ROW_FAIL: out of memory growing custom spark registry");
			return -1;
		}
		s_CustomSparkTypes = grown;
		s_CustomSparkTypeCapacity = next;
	}

	s_CustomSparkTypes[s_NumCustomSparkTypes] = *row;
	return SPARKTYPE_BASE_COUNT + s_NumCustomSparkTypes++;
}

/* Effect-runtime seam: clone the OG SPARKTYPE_PROJECTILE row (the neutral
 * weapon-impact spark: NTSC {50, 28, 100, 1, 0, 0, 1, 60, 30, 10, 1,
 * 0xffff80ff, 0xffffffff, 0.02}, sparks.c row 0x10) and swap in the authored
 * RGBA colour words. Cloning the LIVE row keeps a single source of truth
 * with whatever sparks.c compiled for this build (PAL vs NTSC literals). */
s32 sparksRegisterCustomTintedType(u32 color1, u32 color2)
{
	struct sparktype row;

	memset(&row, 0, sizeof(row));
	row = *sparkTypeFor(SPARKTYPE_PROJECTILE);
	row.unk1c = color1;
	row.unk20 = color2;

	return sparksRegisterCustomType(&row);
}

s32 sparksCustomTypeCheckpoint(void)
{
	return s_NumCustomSparkTypes;
}

void sparksRollbackCustomTypes(s32 checkpoint)
{
	if (checkpoint >= 0 && checkpoint <= s_NumCustomSparkTypes) {
		s_NumCustomSparkTypes = checkpoint;
	}
}

void sparksResetCustomTypes(void)
{
	free(s_CustomSparkTypes);
	s_CustomSparkTypes = NULL;
	s_NumCustomSparkTypes = 0;
	s_CustomSparkTypeCapacity = 0;
}

s32 sparksCustomTypeCount(void)
{
	return s_NumCustomSparkTypes;
}

static s32 sparkTypenumLive(s32 typenum)
{
	return typenum >= 0 &&
		typenum < SPARKTYPE_BASE_COUNT + s_NumCustomSparkTypes;
}

s32 sparksTypeColors(s32 typenum, u32 *color1, u32 *color2)
{
	const struct sparktype *row;

	if (!sparkTypenumLive(typenum)) {
		return 0;
	}

	row = sparkTypeFor(typenum);
	if (color1 != NULL) *color1 = row->unk1c;
	if (color2 != NULL) *color2 = row->unk20;
	return 1;
}

s32 sparksTypeClonesRowExceptColors(s32 typenum, s32 base_typenum)
{
	const struct sparktype *a;
	const struct sparktype *b;

	if (!sparkTypenumLive(typenum) || !sparkTypenumLive(base_typenum)) {
		return 0;
	}

	a = sparkTypeFor(typenum);
	b = sparkTypeFor(base_typenum);
	return a->unk00 == b->unk00 && a->unk02 == b->unk02 &&
		a->unk04 == b->unk04 && a->unk06 == b->unk06 &&
		a->unk08 == b->unk08 && a->unk0a == b->unk0a &&
		a->weight == b->weight && a->maxage == b->maxage &&
		a->unk12 == b->unk12 && a->numsparks == b->numsparks &&
		a->unk18 == b->unk18 && a->decel == b->decel;
}
