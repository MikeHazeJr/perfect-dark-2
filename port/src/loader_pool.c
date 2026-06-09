/*
 * port/src/loader_pool.c -- Catalog universality pivot Step 5
 * (2026-05-03). Heavyweight typed pool for weapon / head / body / arena
 * runtime payloads, populated from per-asset .pdweapon / .pdhead / .pdbody /
 * .pdarena content delivered by the universal directory walker.
 *
 * Pre-Step-5 history (retired): this TU previously parsed an aggregate
 * JSON archive at base/{weapons,heads,bodies,arenas}.pd-base. Step 5
 * retires that tier: the per-asset envelope shape inside .pd* files is
 * identical to the per-record shape the legacy archive used, so the
 * parsers below carry over unchanged; only the file-iteration layer
 * moved into loader_walker_{weapon,head,body,arena}.c which call the
 * loaderPoolParse*Json entrypoints below per file.
 *
 * Memory model:
 *   - The catalog row layer (assetcatalog) stores per-row identity +
 *     unlock metadata. Registration is unconditional: every parseable
 *     .pd* entry registers, regardless of unlock state. Selectors
 *     filter `catalog union unlocked` separately.
 *   - The pools below hold the typed runtime payload (struct weapon,
 *     struct weaponfunc_*, struct inventory_ammo, head_data_t, etc.).
 *     One pool per record type; arena-style allocation; populated once
 *     at startup. Catalog managers route through loaderPoolGet* once
 *     loaderPoolFinalize flips s_LoaderActive.
 *
 * Single-thread invariant: parses run on the main thread at startup
 * before any other system can read pool data. No locking. Future
 * multi-threaded loading would gate the pools behind an acquire/
 * release fence on s_LoaderActive.
 *
 * Logging channels:
 *   LOADER.POOL.WEAPON.OK / RESOLVE_FAIL / FIELD_UNKNOWN / POOL_FULL /
 *     STORED
 *   LOADER.POOL.HEAD.OK / RESOLVE_FAIL / FIELD_UNKNOWN
 *   LOADER.POOL.BODY.OK / RESOLVE_FAIL / FIELD_UNKNOWN
 *   LOADER.POOL.ARENA.OK / RESOLVE_FAIL / FIELD_UNKNOWN
 */

#include <ultra64.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <SDL.h>            /* Engine Phase 4: parser mutex for parallel walker */
#include "data.h"
#include "types.h"
#include "constants.h"
#include "loader_pool.h"
#include "loader_enum_reverse.h"
#include "assetcatalog.h"
#include "assetcatalog_weapon_slots.h"
#include "catalog_mgr_weapons.h"
#include "catalog_mgr_heads.h"
#include "catalog_mgr_bodies.h"
#include "catalog_mgr_arenas.h"
#include "system.h"
#include "fs.h"

/* g_Weapons[], invaimsettings_default, invnoisesettings_silent retired
 * 2026-04-30 (S484 F13). Default fallbacks (loaderPoolFinalize) now seed
 * inline sentinels matching the historical struct literals. */

/* ------------------------------------------------------------------ */
/* Pool storage                                                       */
/* ------------------------------------------------------------------ */

#define POOL_GUNCMDS         3000
#define POOL_GUNVISCMDS       500
#define POOL_PARTVIS          500
#define POOL_AMMOS            120
#define POOL_AIMSETTINGS      120
#define POOL_NOISESETTINGS    120
#define POOL_RECOILSETTINGS   120
#define POOL_WEAPONFUNCS      256
#define POOL_VIBRATIONS       256
#define POOL_ANIMATIONS       256
#define POOL_ANIM_FIXUPS      512  /* GUNCMD_INCLUDE / GUNCMD_RANDOM forward refs */

typedef union {
	struct weaponfunc                  base;
	struct weaponfunc_shootsingle      ss;
	struct weaponfunc_shootauto        sa;
	struct weaponfunc_shootprojectile  sp;
	struct weaponfunc_throw            tw;
	struct weaponfunc_melee            me;
	struct weaponfunc_special          sx;
	struct weaponfunc_device           dv;
} weaponfunc_any_t;

typedef struct {
	char  name[64];
	char  source_path[FS_MAXPATH + 1];
	s32   cmd_offset;
	s32   cmd_count;
} pool_anim_entry_t;

/* Forward-reference fixup record: a GUNCMD_INCLUDE / GUNCMD_RANDOM
 * encountered during decodeOpcode whose target animation has not yet
 * been parsed into s_Animations. unk04 is parked at 0 (NULL) until
 * loaderPoolFinalize walks this table and re-resolves every entry
 * after all animations are loaded. Pre-finalize, bgunStartAnimation's
 * defensive NULL guard prevents the crash class. */
typedef struct {
	struct guncmd *slot;
	char           anim_name[64];
} pool_anim_fixup_t;

static struct weapon                  s_Weapons[CATALOG_MGR_WEAPON_COUNT];
static struct aibotweaponpreference   s_BotPrefs[CATALOG_MGR_WEAPON_COUNT];
/* Per-weapon catalog ID captured from the per-asset envelope `id` field.
 * Used by the .pdweapon emitter when round-tripping the pool back to disk. */
static char                           s_WeaponCatalogIds[CATALOG_MGR_WEAPON_COUNT][64];
static struct guncmd                  s_Guncmds[POOL_GUNCMDS];
static struct gunviscmd               s_Gunviscmds[POOL_GUNVISCMDS];
static struct modelpartvisibility     s_Partvis[POOL_PARTVIS];
static struct inventory_ammo          s_Ammos[POOL_AMMOS];
static struct invaimsettings          s_AimSettings[POOL_AIMSETTINGS];
static struct noisesettings           s_NoiseSettings[POOL_NOISESETTINGS];
static struct recoilsettings          s_RecoilSettings[POOL_RECOILSETTINGS];
static weaponfunc_any_t               s_WeaponFuncs[POOL_WEAPONFUNCS];
static f32                            s_Vibrations[POOL_VIBRATIONS];
static pool_anim_entry_t              s_Animations[POOL_ANIMATIONS];
static pool_anim_fixup_t              s_AnimFixups[POOL_ANIM_FIXUPS];
static s32                            s_AnimFixupsUsed;
static struct invaimsettings          s_DefaultAim;
static struct noisesettings       s_DefaultNoise;

static s32 s_GuncmdsUsed;
static s32 s_GunviscmdsUsed;
static s32 s_PartvisUsed;
static s32 s_AmmosUsed;
static s32 s_AimSettingsUsed;
static s32 s_NoiseSettingsUsed;
static s32 s_RecoilSettingsUsed;
static s32 s_WeaponFuncsUsed;
static s32 s_VibrationsUsed;
static s32 s_AnimationsUsed;
static const char *s_ParseAnimationSourcePath;

static s32 s_LoaderActive;
static s32 s_WeaponsRegistered;
static s32 s_ParseWeaponOverrideId = -1;

/* Engine Phase 4 (2026-05-03): single mutex around all parser entry
 * points + any reader that walks pool counters during parsing.  The
 * walker fans out per-file work across the boot pool's worker threads;
 * each worker calls loaderPoolParse*Json into the shared pools and the
 * shared per-arena counters (s_GuncmdsUsed, s_AmmosUsed, etc.).  Without
 * the lock, two concurrent parses race on those counters and the arena
 * memcpy targets overlap.
 *
 * Lazy-create at first parse call; finalize/reset/get accessors all
 * acquire under the same lock to keep snapshot consistency. */
static SDL_mutex *s_PoolMutex = NULL;

static void s_poolEnsureMutex(void)
{
	if (s_PoolMutex == NULL) {
		s_PoolMutex = SDL_CreateMutex();
	}
}

#define POOL_LOCK()    do { if (s_PoolMutex) SDL_LockMutex(s_PoolMutex); } while (0)
#define POOL_UNLOCK()  do { if (s_PoolMutex) SDL_UnlockMutex(s_PoolMutex); } while (0)

/* Heads / bodies / arenas pools. Populated from per-asset .pdhead /
 * .pdbody / .pdarena envelopes via loaderPoolParse*Json; the catalog
 * managers (catalog_mgr_heads.c / _bodies.c / _arenas.c) gate their
 * pool reads on the matching loaderPool*Active() flag. */
static head_data_t  s_HeadsPool[CATALOG_MGR_HEAD_COUNT];
static s32          s_HeadsLoaderActive;
static s32          s_HeadsRegistered;
static body_data_t  s_BodiesPool[CATALOG_MGR_BODY_COUNT];
static s32          s_BodiesLoaderActive;
static s32          s_BodiesRegistered;
static arena_data_t s_ArenasPool[CATALOG_MGR_ARENA_COUNT];
static s32          s_ArenasLoaderActive;
static s32          s_ArenasRegistered;

/* ------------------------------------------------------------------ */
/* Public accessors (consumed by catalog_mgr_weapons.c)               */
/* ------------------------------------------------------------------ */

s32 loaderPoolIsActive(void) { return s_LoaderActive; }

const struct weapon *loaderPoolGetWeapon(s32 idx)
{
	if (idx < 0 || idx >= CATALOG_MGR_WEAPON_COUNT) return NULL;
	if (!s_LoaderActive) return NULL;
	return &s_Weapons[idx];
}

const struct invaimsettings *loaderPoolGetDefaultAim(void)
{
	return s_LoaderActive ? &s_DefaultAim : NULL;
}

const struct noisesettings *loaderPoolGetDefaultNoise(void)
{
	return s_LoaderActive ? &s_DefaultNoise : NULL;
}

const struct aibotweaponpreference *loaderPoolGetBotPref(s32 idx)
{
	if (idx < 0 || idx >= CATALOG_MGR_WEAPON_COUNT) return NULL;
	if (!s_LoaderActive) return NULL;
	return &s_BotPrefs[idx];
}

s32 loaderPoolGetWeaponsRegistered(void) { return s_WeaponsRegistered; }

/* Per-weapon catalog ID getter. Returns the string captured from the
 * per-asset envelope `id` field, or NULL if the loader is inactive,
 * the index is out of range, or no id was set. */
const char *loaderPoolGetWeaponCatalogId(s32 idx)
{
	if (!s_LoaderActive) return NULL;
	if (idx < 0 || idx >= CATALOG_MGR_WEAPON_COUNT) return NULL;
	if (s_WeaponCatalogIds[idx][0] == '\0') return NULL;
	return s_WeaponCatalogIds[idx];
}

/* Catalog universality pivot Step 1: animation pool accessors.
 * The .pdanim emitter (port/src/romextract_pdanim.c) walks the
 * animation table to write one .pdanim file per recorded animation.
 * Index 0..count-1; out_cmds points into the pool, out_count is
 * the opcode array length. Returns 1 on success, 0 if loader is
 * inactive or idx is out of range. */
s32 loaderPoolGetAnimationCount(void)
{
	return s_LoaderActive ? s_AnimationsUsed : 0;
}

const char *loaderPoolGetAnimationName(s32 idx)
{
	if (!s_LoaderActive) return NULL;
	if (idx < 0 || idx >= s_AnimationsUsed) return NULL;
	return s_Animations[idx].name;
}

s32 loaderPoolGetAnimationOpcodes(s32 idx, const struct guncmd **out_cmds,
                                     s32 *out_count)
{
	if (!s_LoaderActive) return 0;
	if (idx < 0 || idx >= s_AnimationsUsed) return 0;
	if (out_cmds) *out_cmds = &s_Guncmds[s_Animations[idx].cmd_offset];
	if (out_count) *out_count = s_Animations[idx].cmd_count;
	return 1;
}

/* Resolve a guncmd* pointer back to the animation name it belongs to.
 * Used by the .pdweapon emitter to convert struct weapon's anim pointers
 * (equip_animation, unequip_animation, etc.) to symbolic catalog IDs.
 * Returns the name (not a copy) on success, or NULL if the pointer
 * does not match any registered animation start. */
const char *loaderPoolAnimationNameForCmds(const struct guncmd *cmds)
{
	s32 i;
	if (!s_LoaderActive || cmds == NULL) return NULL;
	for (i = 0; i < s_AnimationsUsed; i++) {
		if (&s_Guncmds[s_Animations[i].cmd_offset] == cmds) {
			return s_Animations[i].name;
		}
	}
	return NULL;
}

const char *loaderPoolAnimationSourceForCmds(const struct guncmd *cmds)
{
	s32 i;
	if (!s_LoaderActive || cmds == NULL) return NULL;
	for (i = 0; i < s_AnimationsUsed; i++) {
		if (&s_Guncmds[s_Animations[i].cmd_offset] == cmds) {
			return s_Animations[i].source_path[0]
				? s_Animations[i].source_path
				: NULL;
		}
	}
	return NULL;
}

static struct guncmd *resolveAnimByName(const char *name)
{
	if (name == NULL) return NULL;
	/* B-329 (2026-05-16): tolerate a leading "<ns>:" namespace prefix on
	 * the caller's name in case a future .pdweapon emitter ships catalog-ID
	 * form. Pool storage is bare (parseAnimation strips), so compare the
	 * bare part of the inbound name. */
	const char *bare = name;
	const char *colon = strchr(name, ':');
	if (colon != NULL) bare = colon + 1;
	for (s32 i = 0; i < s_AnimationsUsed; i++) {
		if (strcmp(s_Animations[i].name, bare) == 0) {
			return &s_Guncmds[s_Animations[i].cmd_offset];
		}
	}
	return NULL;
}

/* Register a forward-reference fixup so loaderPoolFinalize can resolve
 * an animation name once all .pdanim files have been parsed. Used by
 * decodeOpcode for GUNCMD_INCLUDE / GUNCMD_RANDOM whose target animation
 * hasn't been parsed yet (load-order forward references). */
static void registerAnimFixup(struct guncmd *slot, const char *anim_name)
{
	if (slot == NULL || anim_name == NULL || anim_name[0] == '\0') {
		return;
	}
	if (s_AnimFixupsUsed >= POOL_ANIM_FIXUPS) {
		sysLogPrintf(LOG_WARNING,
			"LOADER.POOL.WEAPON.FIXUP_FULL: cannot defer resolve of '%s' "
			"(pool full at %d)",
			anim_name, POOL_ANIM_FIXUPS);
		return;
	}
	pool_anim_fixup_t *fx = &s_AnimFixups[s_AnimFixupsUsed++];
	fx->slot = slot;
	/* Strip optional namespace prefix to match resolveAnimByName policy. */
	const char *bare = anim_name;
	const char *colon = strchr(anim_name, ':');
	if (colon != NULL) bare = colon + 1;
	size_t n = strlen(bare);
	if (n >= sizeof(fx->anim_name)) n = sizeof(fx->anim_name) - 1;
	memcpy(fx->anim_name, bare, n);
	fx->anim_name[n] = '\0';
}

/* ------------------------------------------------------------------ */
/* JSON tokenizer                                                     */
/* ------------------------------------------------------------------ */

typedef enum {
	JT_LBRACE, JT_RBRACE, JT_LBRACK, JT_RBRACK,
	JT_COLON, JT_COMMA,
	JT_STRING, JT_NUMBER, JT_TRUE, JT_FALSE, JT_NULL,
	JT_EOF, JT_ERROR
} jtok_kind_t;

typedef struct {
	jtok_kind_t kind;
	const char *start;
	s32 len;
	s64 ival;
	f64 fval;
	s32 is_float;
} jtok_t;

typedef struct {
	const char *src;
	const char *pos;
	const char *end;
	jtok_t cur;
	s32 error;
	s32 line;
} jstream_t;

static void jstream_skip_ws(jstream_t *s)
{
	while (s->pos < s->end) {
		char c = *s->pos;
		if (c == ' ' || c == '\t' || c == '\r') {
			s->pos++;
		} else if (c == '\n') {
			s->line++;
			s->pos++;
		} else {
			break;
		}
	}
}

static void jstream_lex_number(jstream_t *s, jtok_t *t)
{
	t->kind = JT_NUMBER;
	t->start = s->pos;
	const char *start = s->pos;
	s32 sign = 1;
	if (*s->pos == '-') { sign = -1; s->pos++; }
	t->is_float = 0;
	while (s->pos < s->end && (isdigit((unsigned char)*s->pos) || *s->pos == '.'
	                            || *s->pos == 'e' || *s->pos == 'E'
	                            || *s->pos == '+' || *s->pos == '-')) {
		if (*s->pos == '.' || *s->pos == 'e' || *s->pos == 'E') {
			t->is_float = 1;
		}
		s->pos++;
	}
	t->len = (s32)(s->pos - start);
	/* parse */
	char buf[64];
	if (t->len < (s32)sizeof(buf)) {
		memcpy(buf, start, t->len);
		buf[t->len] = '\0';
		if (t->is_float) {
			t->fval = strtod(buf, NULL);
			t->ival = (s64)t->fval;
		} else {
			t->ival = strtoll(buf, NULL, 10);
			t->fval = (f64)t->ival;
		}
	}
	(void)sign;
}

static void jstream_lex_string(jstream_t *s, jtok_t *t)
{
	t->kind = JT_STRING;
	s->pos++;  /* skip opening quote */
	t->start = s->pos;
	while (s->pos < s->end && *s->pos != '"') {
		if (*s->pos == '\\' && s->pos + 1 < s->end) {
			s->pos += 2;
		} else {
			s->pos++;
		}
	}
	t->len = (s32)(s->pos - t->start);
	if (s->pos < s->end && *s->pos == '"') s->pos++;
}

static void jstream_advance(jstream_t *s)
{
	jstream_skip_ws(s);
	jtok_t t;
	t.kind = JT_EOF;
	t.start = s->pos;
	t.len = 0;
	t.ival = 0;
	t.fval = 0;
	t.is_float = 0;
	if (s->pos >= s->end) { s->cur = t; return; }
	char c = *s->pos;
	switch (c) {
	case '{': t.kind = JT_LBRACE; s->pos++; t.len = 1; break;
	case '}': t.kind = JT_RBRACE; s->pos++; t.len = 1; break;
	case '[': t.kind = JT_LBRACK; s->pos++; t.len = 1; break;
	case ']': t.kind = JT_RBRACK; s->pos++; t.len = 1; break;
	case ':': t.kind = JT_COLON;  s->pos++; t.len = 1; break;
	case ',': t.kind = JT_COMMA;  s->pos++; t.len = 1; break;
	case '"': jstream_lex_string(s, &t); break;
	case 't':
		if (s->pos + 4 <= s->end && memcmp(s->pos, "true", 4) == 0) {
			t.kind = JT_TRUE; s->pos += 4; t.len = 4;
		} else {
			t.kind = JT_ERROR; s->error = 1;
		}
		break;
	case 'f':
		if (s->pos + 5 <= s->end && memcmp(s->pos, "false", 5) == 0) {
			t.kind = JT_FALSE; s->pos += 5; t.len = 5;
		} else {
			t.kind = JT_ERROR; s->error = 1;
		}
		break;
	case 'n':
		if (s->pos + 4 <= s->end && memcmp(s->pos, "null", 4) == 0) {
			t.kind = JT_NULL; s->pos += 4; t.len = 4;
		} else {
			t.kind = JT_ERROR; s->error = 1;
		}
		break;
	default:
		if (c == '-' || isdigit((unsigned char)c)) {
			jstream_lex_number(s, &t);
		} else {
			t.kind = JT_ERROR; s->error = 1;
			s->pos++;
		}
		break;
	}
	s->cur = t;
}

static s32 jstream_str_eq(const jtok_t *t, const char *lit)
{
	if (t->kind != JT_STRING) return 0;
	s32 ll = (s32)strlen(lit);
	return ll == t->len && memcmp(t->start, lit, ll) == 0;
}

static void jstream_str_copy(const jtok_t *t, char *dst, size_t dstn)
{
	if (t->kind != JT_STRING || dstn == 0) {
		if (dstn > 0) dst[0] = '\0';
		return;
	}
	size_t n = (size_t)t->len < dstn - 1 ? (size_t)t->len : dstn - 1;
	memcpy(dst, t->start, n);
	dst[n] = '\0';
}

static void jstream_skip_value(jstream_t *s)
{
	jtok_kind_t k = s->cur.kind;
	if (k == JT_LBRACE) {
		s32 depth = 1;
		jstream_advance(s);
		while (depth > 0 && s->cur.kind != JT_EOF) {
			if (s->cur.kind == JT_LBRACE) depth++;
			else if (s->cur.kind == JT_RBRACE) depth--;
			if (depth > 0) jstream_advance(s);
		}
		jstream_advance(s);  /* consume final RBRACE */
	} else if (k == JT_LBRACK) {
		s32 depth = 1;
		jstream_advance(s);
		while (depth > 0 && s->cur.kind != JT_EOF) {
			if (s->cur.kind == JT_LBRACK) depth++;
			else if (s->cur.kind == JT_RBRACK) depth--;
			if (depth > 0) jstream_advance(s);
		}
		jstream_advance(s);
	} else {
		/* primitive */
		jstream_advance(s);
	}
}

/* ------------------------------------------------------------------ */
/* Pool allocators                                                    */
/* ------------------------------------------------------------------ */

#define POOL_ALLOC_DEF(NAME, ARRAY, USED, CAP, TYPE)                  \
	static TYPE *NAME(s32 count, const char *who)                    \
	{                                                                  \
		if (count <= 0) return NULL;                                  \
		if (USED + count > CAP) {                                     \
			sysLogPrintf(LOG_WARNING,                                  \
				"LOADER.POOL.WEAPON.POOL_FULL: %s wants %d, "        \
				"used=%d cap=%d", who, count, USED, CAP);              \
			return NULL;                                               \
		}                                                              \
		TYPE *p = &ARRAY[USED];                                       \
		USED += count;                                                 \
		memset(p, 0, sizeof(TYPE) * count);                           \
		return p;                                                      \
	}

POOL_ALLOC_DEF(allocGuncmds,        s_Guncmds,        s_GuncmdsUsed,        POOL_GUNCMDS,        struct guncmd)
POOL_ALLOC_DEF(allocGunviscmds,     s_Gunviscmds,     s_GunviscmdsUsed,     POOL_GUNVISCMDS,     struct gunviscmd)
POOL_ALLOC_DEF(allocPartvis,        s_Partvis,        s_PartvisUsed,        POOL_PARTVIS,        struct modelpartvisibility)
POOL_ALLOC_DEF(allocAmmos,          s_Ammos,          s_AmmosUsed,          POOL_AMMOS,          struct inventory_ammo)
POOL_ALLOC_DEF(allocAimSettings,    s_AimSettings,    s_AimSettingsUsed,    POOL_AIMSETTINGS,    struct invaimsettings)
POOL_ALLOC_DEF(allocNoiseSettings,  s_NoiseSettings,  s_NoiseSettingsUsed,  POOL_NOISESETTINGS,  struct noisesettings)
POOL_ALLOC_DEF(allocRecoilSettings, s_RecoilSettings, s_RecoilSettingsUsed, POOL_RECOILSETTINGS, struct recoilsettings)
POOL_ALLOC_DEF(allocWeaponFuncs,    s_WeaponFuncs,    s_WeaponFuncsUsed,    POOL_WEAPONFUNCS,    weaponfunc_any_t)
POOL_ALLOC_DEF(allocVibrations,     s_Vibrations,     s_VibrationsUsed,     POOL_VIBRATIONS,     f32)

/* ------------------------------------------------------------------ */
/* Number / string helpers (defensive against malformed JSON)         */
/* ------------------------------------------------------------------ */

static s32 jread_int(jstream_t *s, s32 fallback)
{
	if (s->cur.kind == JT_NUMBER) {
		s32 v = (s32)s->cur.ival;
		jstream_advance(s);
		return v;
	}
	if (s->cur.kind == JT_NULL) {
		jstream_advance(s);
		return fallback;
	}
	jstream_skip_value(s);
	return fallback;
}

static f32 jread_float(jstream_t *s, f32 fallback)
{
	if (s->cur.kind == JT_NUMBER) {
		f32 v = (f32)(s->cur.is_float ? s->cur.fval : (f64)s->cur.ival);
		jstream_advance(s);
		return v;
	}
	if (s->cur.kind == JT_NULL) {
		jstream_advance(s);
		return fallback;
	}
	jstream_skip_value(s);
	return fallback;
}

static void jread_string_buf(jstream_t *s, char *dst, size_t dstn)
{
	if (s->cur.kind == JT_STRING) {
		jstream_str_copy(&s->cur, dst, dstn);
		jstream_advance(s);
	} else {
		if (dstn > 0) dst[0] = '\0';
		jstream_skip_value(s);
	}
}

/* Read a value that may be an enum string ("ANIM_..." / "SFX_..." /
 * "FILE_..." / "L_GUN_...") or a raw integer. Resolves via the
 * loader's enum tables. Falls back to `fallback` if the string is
 * unknown; logs RESOLVE_FAIL on miss. */
typedef enum {
	JREF_ANY,
	JREF_ANIM,
	JREF_SFX,
	JREF_FILE,
	JREF_LANG,
} jref_kind_t;

static s32 jread_enum_or_int(jstream_t *s, jref_kind_t kind, s32 fallback,
                              const char *site)
{
	if (s->cur.kind == JT_NUMBER) {
		return jread_int(s, fallback);
	}
	if (s->cur.kind == JT_STRING) {
		char buf[64];
		jstream_str_copy(&s->cur, buf, sizeof(buf));
		jstream_advance(s);
		s32 v = fallback;
		switch (kind) {
		case JREF_ANIM: v = loaderEnumResolveAnimEnum(buf, fallback); break;
		case JREF_SFX:  v = loaderEnumResolveSfxEnum(buf, fallback); break;
		case JREF_FILE: v = loaderEnumResolveFileEnum(buf, fallback); break;
		case JREF_LANG: v = loaderEnumResolveLangEnum(buf, fallback); break;
		default:
			/* Try each table in turn, accept the first hit. */
			v = loaderEnumResolveLangEnum(buf, -1);
			if (v < 0) v = loaderEnumResolveAnimEnum(buf, -1);
			if (v < 0) v = loaderEnumResolveSfxEnum(buf, -1);
			if (v < 0) v = loaderEnumResolveFileEnum(buf, fallback);
			break;
		}
		if (v == fallback && buf[0] != '\0') {
			sysLogPrintf(LOG_WARNING,
				"LOADER.POOL.WEAPON.RESOLVE_FAIL: %s name=\"%s\"",
				site ? site : "(?)", buf);
		}
		return v;
	}
	if (s->cur.kind == JT_NULL) {
		jstream_advance(s);
		return fallback;
	}
	jstream_skip_value(s);
	return fallback;
}

static s32 s_resolveAudioCatalogOrEnumName(const char *name, s32 fallback,
                                            const char *site)
{
	if (name != NULL && strchr(name, ':') != NULL) {
		const asset_entry_t *e = assetCatalogResolve(name);
		if (e != NULL && e->type == ASSET_AUDIO) {
			return e->ext.audio.sound_id;
		}
		sysLogPrintf(LOG_WARNING,
			"LOADER.POOL.WEAPON.RESOLVE_FAIL: %s audio_id=\"%s\"",
			site ? site : "(?)", name ? name : "");
		return fallback;
	}
	return loaderEnumResolveSfxEnum(name ? name : "", fallback);
}

static s32 s_resolveAnimationCatalogOrEnumName(const char *name, s32 fallback,
                                                const char *site)
{
	if (name != NULL && strchr(name, ':') != NULL) {
		const asset_entry_t *e = assetCatalogResolve(name);
		if (e != NULL && e->type == ASSET_ANIMATION
				&& e->ext.anim.anim_id >= 0) {
			return e->ext.anim.anim_id;
		}
		sysLogPrintf(LOG_WARNING,
			"LOADER.POOL.WEAPON.RESOLVE_FAIL: %s animation_id=\"%s\"",
			site ? site : "(?)", name ? name : "");
		return fallback;
	}
	return loaderEnumResolveAnimEnum(name ? name : "", fallback);
}

static s32 jread_audio_catalog_or_enum(jstream_t *s, s32 fallback,
                                        const char *site)
{
	if (s->cur.kind == JT_NUMBER) {
		return jread_int(s, fallback);
	}
	if (s->cur.kind == JT_STRING) {
		char buf[64];
		jstream_str_copy(&s->cur, buf, sizeof(buf));
		jstream_advance(s);
		return s_resolveAudioCatalogOrEnumName(buf, fallback, site);
	}
	if (s->cur.kind == JT_NULL) {
		jstream_advance(s);
		return fallback;
	}
	jstream_skip_value(s);
	return fallback;
}

/* ------------------------------------------------------------------ */
/* Opcode codec                                                       */
/* ------------------------------------------------------------------ */

/* Decode a JSON opcode array element into one struct guncmd entry.
 * Returns 1 on success, 0 if the array element is malformed. */
static s32 decodeOpcode(jstream_t *s, struct guncmd *out)
{
	if (s->cur.kind != JT_LBRACK) {
		sysLogPrintf(LOG_WARNING,
			"LOADER.POOL.WEAPON.SCAN_FAIL: opcode not an array");
		jstream_skip_value(s);
		return 0;
	}
	jstream_advance(s);  /* consume LBRACK */
	if (s->cur.kind != JT_STRING) {
		jstream_skip_value(s);
		return 0;
	}
	char mnem[32];
	jstream_str_copy(&s->cur, mnem, sizeof(mnem));
	jstream_advance(s);

	memset(out, 0, sizeof(*out));

	if (strcmp(mnem, "end") == 0) {
		out->type = GUNCMD_END;
	} else if (strcmp(mnem, "showpart") == 0) {
		out->type = GUNCMD_SHOWPART;
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		out->unk02 = (u16)jread_int(s, 0);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		out->unk04 = (intptr_t)jread_int(s, 0);
	} else if (strcmp(mnem, "hidepart") == 0) {
		out->type = GUNCMD_HIDEPART;
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		out->unk02 = (u16)jread_int(s, 0);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		out->unk04 = (intptr_t)jread_int(s, 0);
	} else if (strcmp(mnem, "waitforzreleased") == 0) {
		out->type = GUNCMD_WAITFORZRELEASED;
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		out->unk02 = (u16)jread_int(s, 0);
	} else if (strcmp(mnem, "waittime") == 0) {
		out->type = GUNCMD_WAITTIME;
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		out->unk02 = (u16)jread_int(s, 0);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		out->unk04 = (intptr_t)jread_int(s, 0);
	} else if (strcmp(mnem, "playsound") == 0) {
		out->type = GUNCMD_PLAYSOUND;
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		out->unk02 = (u16)jread_int(s, 0);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		out->unk04 = (intptr_t)jread_audio_catalog_or_enum(s, 0, "playsound.sound");
	} else if (strcmp(mnem, "include") == 0) {
		out->type = GUNCMD_INCLUDE;
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		out->unk01 = (u8)jread_int(s, 0);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		/* Forward references resolve in loaderPoolFinalize via registerAnimFixup. */
		char anim_name[64];
		jread_string_buf(s, anim_name, sizeof(anim_name));
		struct guncmd *resolved = resolveAnimByName(anim_name);
		out->unk04 = (intptr_t)resolved;
		if (resolved == NULL) {
			registerAnimFixup(out, anim_name);
		}
	} else if (strcmp(mnem, "random") == 0) {
		out->type = GUNCMD_RANDOM;
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		out->unk02 = (u16)jread_int(s, 0);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		char anim_name[64];
		jread_string_buf(s, anim_name, sizeof(anim_name));
		struct guncmd *resolved = resolveAnimByName(anim_name);
		out->unk04 = (intptr_t)resolved;
		if (resolved == NULL) {
			registerAnimFixup(out, anim_name);
		}
	} else if (strcmp(mnem, "repeatuntilfull") == 0) {
		out->type = GUNCMD_REPEATUNTILFULL;
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		out->unk02 = (u16)jread_int(s, 0);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		s32 dontloop = jread_int(s, 0);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		s32 gototrigger = jread_int(s, 0);
		out->unk04 = ((intptr_t)dontloop << 16) | (gototrigger & 0xFFFF);
	} else if (strcmp(mnem, "popoutsackofpills") == 0) {
		out->type = GUNCMD_POPOUTSACKOFPILLS;
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		out->unk02 = (u16)jread_int(s, 0);
	} else if (strcmp(mnem, "playanimation") == 0) {
		out->type = GUNCMD_PLAYANIMATION;
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		out->unk02 = (u16)jread_enum_or_int(s, JREF_ANIM, 0, "playanimation.anim");
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		s32 direction = jread_int(s, 0);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		s32 speed = jread_int(s, 0);
		out->unk04 = ((intptr_t)direction << 16) | (speed & 0xFFFF);
	} else if (strcmp(mnem, "setsoundspeed") == 0) {
		out->type = GUNCMD_SETSOUNDSPEED;
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		out->unk02 = (u16)jread_int(s, 0);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		out->unk04 = (intptr_t)jread_int(s, 0);
	} else {
		sysLogPrintf(LOG_WARNING,
			"LOADER.POOL.WEAPON.RESOLVE_FAIL: unknown opcode mnem=\"%s\"",
			mnem);
		/* skip remaining arg values until RBRACK */
	}
	/* Drain remaining args until RBRACK. */
	while (s->cur.kind != JT_RBRACK && s->cur.kind != JT_EOF) {
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		else jstream_skip_value(s);
	}
	if (s->cur.kind == JT_RBRACK) jstream_advance(s);
	return 1;
}

static s32 decodeCommandObject(jstream_t *s, struct guncmd *out)
{
	if (s->cur.kind != JT_LBRACE) {
		sysLogPrintf(LOG_WARNING,
			"LOADER.POOL.WEAPON.SCAN_FAIL: command not an object");
		jstream_skip_value(s);
		return 0;
	}
	jstream_advance(s);
	memset(out, 0, sizeof(*out));

	char command[64] = {0};
	char animation[64] = {0};
	s32 slot = 0;
	s32 part = 0;
	s32 value = 0;
	s32 ticks = 0;
	s32 weight = 0;
	s32 sound = 0;
	s32 direction = 0;
	s32 speed = 0;
	s32 dont_loop = 0;
	s32 goto_trigger = 0;

	while (s->cur.kind != JT_RBRACE && s->cur.kind != JT_EOF) {
		if (s->cur.kind != JT_STRING) { jstream_advance(s); continue; }
		jtok_t key = s->cur;
		jstream_advance(s);
		if (s->cur.kind != JT_COLON) continue;
		jstream_advance(s);

		if (jstream_str_eq(&key, "command")) {
			jread_string_buf(s, command, sizeof(command));
		} else if (jstream_str_eq(&key, "animation")) {
			jread_string_buf(s, animation, sizeof(animation));
		} else if (jstream_str_eq(&key, "sound")
				|| jstream_str_eq(&key, "sound_id")) {
			sound = jread_audio_catalog_or_enum(s, 0, "command.sound");
		} else if (jstream_str_eq(&key, "slot")) {
			slot = jread_int(s, 0);
		} else if (jstream_str_eq(&key, "part")) {
			part = jread_int(s, 0);
		} else if (jstream_str_eq(&key, "value")) {
			value = jread_int(s, 0);
		} else if (jstream_str_eq(&key, "ticks")) {
			ticks = jread_int(s, 0);
		} else if (jstream_str_eq(&key, "weight")) {
			weight = jread_int(s, 0);
		} else if (jstream_str_eq(&key, "direction")) {
			direction = jread_int(s, 0);
		} else if (jstream_str_eq(&key, "speed")) {
			speed = jread_int(s, 0);
		} else if (jstream_str_eq(&key, "dont_loop")) {
			dont_loop = jread_int(s, 0);
		} else if (jstream_str_eq(&key, "goto_trigger")) {
			goto_trigger = jread_int(s, 0);
		} else {
			jstream_skip_value(s);
		}
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
	}
	if (s->cur.kind == JT_RBRACE) jstream_advance(s);

	if (strcmp(command, "end") == 0) {
		out->type = GUNCMD_END;
	} else if (strcmp(command, "show_part") == 0) {
		out->type = GUNCMD_SHOWPART;
		out->unk02 = (u16)part;
		out->unk04 = (intptr_t)value;
	} else if (strcmp(command, "hide_part") == 0) {
		out->type = GUNCMD_HIDEPART;
		out->unk02 = (u16)part;
		out->unk04 = (intptr_t)value;
	} else if (strcmp(command, "wait_for_trigger_release") == 0) {
		out->type = GUNCMD_WAITFORZRELEASED;
		out->unk02 = (u16)slot;
	} else if (strcmp(command, "wait_ticks") == 0) {
		out->type = GUNCMD_WAITTIME;
		out->unk02 = (u16)slot;
		out->unk04 = (intptr_t)ticks;
	} else if (strcmp(command, "play_sound") == 0) {
		out->type = GUNCMD_PLAYSOUND;
		out->unk02 = (u16)slot;
		out->unk04 = (intptr_t)sound;
	} else if (strcmp(command, "include_animation") == 0) {
		out->type = GUNCMD_INCLUDE;
		out->unk01 = (u8)slot;
		struct guncmd *resolved = resolveAnimByName(animation);
		out->unk04 = (intptr_t)resolved;
		if (resolved == NULL) {
			registerAnimFixup(out, animation);
		}
	} else if (strcmp(command, "random_animation") == 0) {
		out->type = GUNCMD_RANDOM;
		out->unk02 = (u16)weight;
		struct guncmd *resolved = resolveAnimByName(animation);
		out->unk04 = (intptr_t)resolved;
		if (resolved == NULL) {
			registerAnimFixup(out, animation);
		}
	} else if (strcmp(command, "repeat_until_full") == 0) {
		out->type = GUNCMD_REPEATUNTILFULL;
		out->unk02 = (u16)slot;
		out->unk04 = ((intptr_t)dont_loop << 16) | (goto_trigger & 0xFFFF);
	} else if (strcmp(command, "popout_sack_of_pills") == 0) {
		out->type = GUNCMD_POPOUTSACKOFPILLS;
		out->unk02 = (u16)slot;
	} else if (strcmp(command, "play_character_animation") == 0) {
		out->type = GUNCMD_PLAYANIMATION;
		out->unk02 = (u16)s_resolveAnimationCatalogOrEnumName(animation, 0,
			"command.animation");
		out->unk04 = ((intptr_t)direction << 16) | (speed & 0xFFFF);
	} else if (strcmp(command, "set_sound_speed") == 0) {
		out->type = GUNCMD_SETSOUNDSPEED;
		out->unk02 = (u16)slot;
		out->unk04 = (intptr_t)speed;
	} else {
		sysLogPrintf(LOG_WARNING,
			"LOADER.POOL.WEAPON.RESOLVE_FAIL: unknown command=\"%s\"",
			command);
		return 0;
	}
	return 1;
}

/* Reverse of decodeOpcode: encode a struct guncmd back to a JSON-ish
 * representation used only by the F12 round-trip self-test. Returns
 * 1 on success and writes to `out_buf` (NUL-terminated). */
s32 loaderPoolEncodeOpcode(const struct guncmd *cmd, char *out_buf,
                              size_t out_n)
{
	if (cmd == NULL || out_buf == NULL || out_n == 0) return 0;
	const char *mnem = "unknown";
	switch (cmd->type) {
	case GUNCMD_END:               mnem = "end"; break;
	case GUNCMD_SHOWPART:          mnem = "showpart"; break;
	case GUNCMD_HIDEPART:          mnem = "hidepart"; break;
	case GUNCMD_WAITFORZRELEASED:  mnem = "waitforzreleased"; break;
	case GUNCMD_WAITTIME:          mnem = "waittime"; break;
	case GUNCMD_PLAYSOUND:         mnem = "playsound"; break;
	case GUNCMD_INCLUDE:           mnem = "include"; break;
	case GUNCMD_RANDOM:            mnem = "random"; break;
	case GUNCMD_REPEATUNTILFULL:   mnem = "repeatuntilfull"; break;
	case GUNCMD_POPOUTSACKOFPILLS: mnem = "popoutsackofpills"; break;
	case GUNCMD_PLAYANIMATION:     mnem = "playanimation"; break;
	case GUNCMD_SETSOUNDSPEED:     mnem = "setsoundspeed"; break;
	}
	snprintf(out_buf, out_n, "[%s, %u, %u, %lld]", mnem,
	         (unsigned)cmd->unk01, (unsigned)cmd->unk02,
	         (long long)cmd->unk04);
	return 1;
}

/* ------------------------------------------------------------------ */
/* Animation parser (struct guncmd[] array)                           */
/* ------------------------------------------------------------------ */

static void parseAnimation(jstream_t *s)
{
	if (s->cur.kind != JT_LBRACE) { jstream_skip_value(s); return; }
	jstream_advance(s);

	char anim_name[64] = {0};
	struct guncmd *cmds_start = NULL;
	s32 cmds_count = 0;

	while (s->cur.kind != JT_RBRACE && s->cur.kind != JT_EOF) {
		if (s->cur.kind != JT_STRING) { jstream_advance(s); continue; }
		s32 is_id       = jstream_str_eq(&s->cur, "id");
		s32 is_name     = jstream_str_eq(&s->cur, "name");
		s32 is_commands = jstream_str_eq(&s->cur, "commands");
		s32 is_opcodes  = jstream_str_eq(&s->cur, "opcodes");
		jstream_advance(s);  /* consume key */
		if (s->cur.kind != JT_COLON) continue;
		jstream_advance(s);  /* consume : */
		if (is_id || (is_name && anim_name[0] == '\0')) {
			jread_string_buf(s, anim_name, sizeof(anim_name));
		} else if (is_commands || is_opcodes) {
			if (s->cur.kind != JT_LBRACK) { jstream_skip_value(s); }
			else {
				jstream_advance(s);  /* consume [ */
				cmds_start = &s_Guncmds[s_GuncmdsUsed];
				s32 reserved_start = s_GuncmdsUsed;
				while (s->cur.kind != JT_RBRACK && s->cur.kind != JT_EOF) {
					if (s_GuncmdsUsed >= POOL_GUNCMDS) {
						sysLogPrintf(LOG_WARNING,
							"LOADER.POOL.WEAPON.POOL_FULL: guncmds while "
							"parsing %s", anim_name);
						jstream_skip_value(s);
						if (s->cur.kind == JT_COMMA) jstream_advance(s);
						continue;
					}
					struct guncmd *slot = &s_Guncmds[s_GuncmdsUsed];
					s_GuncmdsUsed++;
					if (is_commands) {
						decodeCommandObject(s, slot);
					} else {
						decodeOpcode(s, slot);
					}
					if (s->cur.kind == JT_COMMA) jstream_advance(s);
				}
				cmds_count = s_GuncmdsUsed - reserved_start;
				if (s->cur.kind == JT_RBRACK) jstream_advance(s);
			}
		} else {
			jstream_skip_value(s);
		}
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
	}
	if (s->cur.kind == JT_RBRACE) jstream_advance(s);

	if (anim_name[0] != '\0' && s_AnimationsUsed < POOL_ANIMATIONS) {
		pool_anim_entry_t *e = &s_Animations[s_AnimationsUsed++];
		/* B-329 (2026-05-16): the .pdanim emitter writes the catalog ID
		 * with a namespace prefix (e.g. "base:invanim_falcon2_equip"),
		 * but the .pdweapon emitter writes anim refs as the bare symbol
		 * (e.g. "invanim_falcon2_equip") via g_AnimData[].name. The local
		 * pool name table is the lookup that resolveAnimByName uses for
		 * weapon equip/unequip/pritosec/sectopri/fire/reload references;
		 * it must store the bare symbol so the .pdweapon refs resolve.
		 * Strip any leading "<ns>:" prefix before storing. Asset-catalog
		 * identity is unaffected -- that lives in the asset_entry_t row
		 * registered by loaderWalkerScanAnimations, not in this pool. */
		const char *bare = anim_name;
		const char *colon = strchr(anim_name, ':');
		if (colon != NULL) bare = colon + 1;
		size_t n = strlen(bare);
		if (n >= sizeof(e->name)) n = sizeof(e->name) - 1;
		memcpy(e->name, bare, n);
		e->name[n] = '\0';
		e->source_path[0] = '\0';
		if (s_ParseAnimationSourcePath && s_ParseAnimationSourcePath[0]) {
			strncpy(e->source_path, s_ParseAnimationSourcePath,
				sizeof(e->source_path) - 1);
			e->source_path[sizeof(e->source_path) - 1] = '\0';
		}
		e->cmd_offset = (s32)(cmds_start - s_Guncmds);
		e->cmd_count = cmds_count;
	}
}

/* ------------------------------------------------------------------ */
/* Sub-record parsers                                                 */
/* ------------------------------------------------------------------ */

static void parseNoiseSettingsInto(jstream_t *s, struct noisesettings *out)
{
	if (s->cur.kind != JT_LBRACE) { jstream_skip_value(s); return; }
	jstream_advance(s);
	while (s->cur.kind != JT_RBRACE && s->cur.kind != JT_EOF) {
		if (s->cur.kind != JT_STRING) { jstream_advance(s); continue; }
		jtok_t key = s->cur;
		jstream_advance(s);
		if (s->cur.kind != JT_COLON) continue;
		jstream_advance(s);
		if      (jstream_str_eq(&key, "minradius"))    out->minradius    = jread_float(s, 0);
		else if (jstream_str_eq(&key, "maxradius"))    out->maxradius    = jread_float(s, 0);
		else if (jstream_str_eq(&key, "incradius"))    out->incradius    = jread_float(s, 0);
		else if (jstream_str_eq(&key, "decbasespeed")) out->decbasespeed = jread_float(s, 0);
		else if (jstream_str_eq(&key, "decremspeed"))  out->decremspeed  = jread_float(s, 0);
		else jstream_skip_value(s);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
	}
	if (s->cur.kind == JT_RBRACE) jstream_advance(s);
}

static void parseRecoilSettingsInto(jstream_t *s, struct recoilsettings *out)
{
	if (s->cur.kind != JT_LBRACE) { jstream_skip_value(s); return; }
	jstream_advance(s);
	while (s->cur.kind != JT_RBRACE && s->cur.kind != JT_EOF) {
		if (s->cur.kind != JT_STRING) { jstream_advance(s); continue; }
		jtok_t key = s->cur;
		jstream_advance(s);
		if (s->cur.kind != JT_COLON) continue;
		jstream_advance(s);
		if      (jstream_str_eq(&key, "xrange")) out->xrange = jread_float(s, 0);
		else if (jstream_str_eq(&key, "yrange")) out->yrange = jread_float(s, 0);
		else if (jstream_str_eq(&key, "zrange")) out->zrange = jread_float(s, 0);
		else if (jstream_str_eq(&key, "unk0c"))  out->unk0c  = jread_float(s, 0);
		else if (jstream_str_eq(&key, "unk10"))  out->unk10  = (u8)jread_int(s, 0);
		else jstream_skip_value(s);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
	}
	if (s->cur.kind == JT_RBRACE) jstream_advance(s);
}

static void parseAimSettingsInto(jstream_t *s, struct invaimsettings *out)
{
	if (s->cur.kind != JT_LBRACE) { jstream_skip_value(s); return; }
	jstream_advance(s);
	while (s->cur.kind != JT_RBRACE && s->cur.kind != JT_EOF) {
		if (s->cur.kind != JT_STRING) { jstream_advance(s); continue; }
		jtok_t key = s->cur;
		jstream_advance(s);
		if (s->cur.kind != JT_COLON) continue;
		jstream_advance(s);
		if      (jstream_str_eq(&key, "zoomfov"))      out->zoomfov      = jread_float(s, 0);
		else if (jstream_str_eq(&key, "guntransup"))   out->guntransup   = jread_float(s, 0);
		else if (jstream_str_eq(&key, "guntransdown")) out->guntransdown = jread_float(s, 0);
		else if (jstream_str_eq(&key, "guntransside")) out->guntransside = jread_float(s, 0);
		else if (jstream_str_eq(&key, "aimdamppal"))   out->aimdamppal   = jread_float(s, 0);
		else if (jstream_str_eq(&key, "aimdamp"))      out->aimdamp      = jread_float(s, 0);
		else if (jstream_str_eq(&key, "tracktype"))    out->tracktype    = (u32)jread_int(s, 0);
		else if (jstream_str_eq(&key, "unk18_04"))     out->unk18_04     = (u32)jread_int(s, 0);
		else if (jstream_str_eq(&key, "flags"))        out->flags        = (u32)jread_int(s, 0);
		else jstream_skip_value(s);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
	}
	if (s->cur.kind == JT_RBRACE) jstream_advance(s);
}

static struct inventory_ammo *parseAmmoIfPresent(jstream_t *s)
{
	if (s->cur.kind == JT_NULL) { jstream_advance(s); return NULL; }
	if (s->cur.kind != JT_LBRACE) { jstream_skip_value(s); return NULL; }
	struct inventory_ammo *out = allocAmmos(1, "ammo");
	if (out == NULL) { jstream_skip_value(s); return NULL; }
	jstream_advance(s);
	while (s->cur.kind != JT_RBRACE && s->cur.kind != JT_EOF) {
		if (s->cur.kind != JT_STRING) { jstream_advance(s); continue; }
		jtok_t key = s->cur;
		jstream_advance(s);
		if (s->cur.kind != JT_COLON) continue;
		jstream_advance(s);
		if      (jstream_str_eq(&key, "type"))            out->type        = (u32)jread_int(s, 0);
		else if (jstream_str_eq(&key, "casingeject"))     out->casingeject = (u32)jread_int(s, 0);
		else if (jstream_str_eq(&key, "clipsize"))        out->clipsize    = (s16)jread_int(s, 0);
		else if (jstream_str_eq(&key, "reload_animation")) {
			char anim[64]; jread_string_buf(s, anim, sizeof(anim));
			out->reload_animation = resolveAnimByName(anim);
		}
		else if (jstream_str_eq(&key, "flags")) out->flags = (u8)jread_int(s, 0);
		else jstream_skip_value(s);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
	}
	if (s->cur.kind == JT_RBRACE) jstream_advance(s);
	return out;
}

/* True iff struct_name is in the shoot-subclass family (or empty,
 * meaning "_struct" was omitted in the JSON and the writer trusts
 * the field set to imply variant). The shoot subclasses share the
 * weaponfunc_shoot byte layout from offset 0x14 onward; writes to
 * out->ss.base.* are byte-correct for any of them. Non-shoot
 * variants (throw / melee / special / device) have unrelated layouts
 * past the shared weaponfunc base struct, so a write through the
 * shoot view would corrupt their fields.
 *
 * S484-followup-4 fixed the recoverytime60 / damage / unk24 /
 * projectilemodelnum / unk44 / speed cross-variant writes; this
 * helper extends the same discipline to every remaining shoot-only
 * field write inside parseWeaponFunc. */
static int weaponFuncStructIsShootFamily(const char *struct_name)
{
	if (struct_name[0] == '\0') return 1;
	if (strcmp(struct_name, "weaponfunc_shootsingle") == 0) return 1;
	if (strcmp(struct_name, "weaponfunc_shootauto") == 0) return 1;
	if (strcmp(struct_name, "weaponfunc_shootprojectile") == 0) return 1;
	return 0;
}

/* Parse a function record. The "_struct" key tells us which subclass
 * to populate. Returns a pointer (cast to void*) suitable for
 * struct weapon's functions[i] slot. */
static void *parseWeaponFunc(jstream_t *s)
{
	if (s->cur.kind == JT_NULL) { jstream_advance(s); return NULL; }
	if (s->cur.kind != JT_LBRACE) { jstream_skip_value(s); return NULL; }

	weaponfunc_any_t *out = allocWeaponFuncs(1, "weaponfunc");
	if (out == NULL) { jstream_skip_value(s); return NULL; }

	/* First scan _struct to determine variant; we'll keep walking the
	 * rest of the object reading shared and variant-specific keys. */
	jstream_advance(s);  /* consume LBRACE */

	char struct_name[48] = {0};
	struct weaponfunc *base = &out->base;

	while (s->cur.kind != JT_RBRACE && s->cur.kind != JT_EOF) {
		if (s->cur.kind != JT_STRING) { jstream_advance(s); continue; }
		jtok_t key = s->cur;
		jstream_advance(s);
		if (s->cur.kind != JT_COLON) continue;
		jstream_advance(s);

		if      (jstream_str_eq(&key, "_struct")) {
			jread_string_buf(s, struct_name, sizeof(struct_name));
		}
		else if (jstream_str_eq(&key, "_symbol")) jstream_skip_value(s);
		else if (jstream_str_eq(&key, "type"))    base->type        = jread_int(s, 0);
		else if (jstream_str_eq(&key, "name"))    base->name        = (u16)jread_enum_or_int(s, JREF_LANG, 0, "weaponfunc.name");
		else if (jstream_str_eq(&key, "unk06"))   base->unk06       = (u8)jread_int(s, 0);
		else if (jstream_str_eq(&key, "ammoindex")) base->ammoindex = (s8)jread_int(s, 0);
		else if (jstream_str_eq(&key, "noisesettings")) {
			if (s->cur.kind == JT_NULL) { jstream_advance(s); base->noisesettings = NULL; }
			else {
				struct noisesettings *ns = allocNoiseSettings(1, "weaponfunc.noisesettings");
				if (ns) { parseNoiseSettingsInto(s, ns); base->noisesettings = ns; }
				else jstream_skip_value(s);
			}
		}
		else if (jstream_str_eq(&key, "fire_animation")) {
			char anim[64]; jread_string_buf(s, anim, sizeof(anim));
			base->fire_animation = resolveAnimByName(anim);
		}
		else if (jstream_str_eq(&key, "flags")) base->flags = (u32)jread_int(s, 0);
		else if (jstream_str_eq(&key, "recoilsettings")) {
			if (s->cur.kind == JT_NULL) { jstream_advance(s); /* shoot subclass only */ }
			else {
				struct recoilsettings *rs = allocRecoilSettings(1, "weaponfunc.recoilsettings");
				if (rs) {
					parseRecoilSettingsInto(s, rs);
					/* Variant-specific: only shoot subclasses have this field. */
					out->ss.base.recoilsettings = rs;
				}
				else jstream_skip_value(s);
			}
		}
		/* shoot-base extension fields */
		else if (jstream_str_eq(&key, "recoverytime60")) {
			/* S484-followup-4 (2026-05-01): variant-specific offsets.
			 *
			 * On 64-bit ABI the recoverytime60 field lands at different
			 * byte offsets depending on which weaponfunc variant is
			 * active, because earlier fields (pointers) grew from 4 to
			 * 8 bytes:
			 *   weaponfunc_shoot.recoverytime60   @ 0x28 (s8)
			 *   weaponfunc_throw.recoverytime60   @ 0x28 (s32)
			 *   weaponfunc_special.recoverytime60 @ 0x24 (s32)
			 *
			 * The pre-fix loader wrote to all three views unconditionally
			 * "to keep all views in sync." For a SHOOT variant that
			 * overwrote bytes 0x24-0x27 of the slot via the sx write --
			 * which is the HIGH 4 BYTES OF THE recoilsettings POINTER
			 * (recoilsettings is at 0x20-0x27 in weaponfunc_shoot). The
			 * pointer became invalid; bgun's recoil-block dereference
			 * crashed (Mike's reproduced 5x fire-time crashes).
			 *
			 * Diagnosed via the canary + range instrumentation in
			 * S484-followup-3: shootfunc was in-pool, recoilsettings
			 * was OUT-OF-POOL with value 0x000000103f800000. Low 4
			 * bytes = float 1.0 (= damage=1.0 from the parallel
			 * "damage" cross-variant bug), high 4 bytes = int 16
			 * (= recoverytime60=16 from this special-variant write).
			 *
			 * Fix: gate each write on struct_name so only the ACTIVE
			 * variant's field is written. Any non-matching variant's
			 * write would land at an offset that belongs to a
			 * different field of the active variant -- corruption. */
			s32 v = jread_int(s, 0);
			if (struct_name[0] == '\0'
					|| strcmp(struct_name, "weaponfunc_shootsingle") == 0
					|| strcmp(struct_name, "weaponfunc_shootauto") == 0
					|| strcmp(struct_name, "weaponfunc_shootprojectile") == 0) {
				out->ss.base.recoverytime60 = (s8)v;
			} else if (strcmp(struct_name, "weaponfunc_throw") == 0) {
				out->tw.recoverytime60 = v;
			} else if (strcmp(struct_name, "weaponfunc_special") == 0) {
				out->sx.recoverytime60 = v;
			}
			/* Other variants (melee, device) have no recoverytime60. */
		}
		else if (jstream_str_eq(&key, "damage")) {
			/* S484-followup-4: variant-specific offsets.
			 *   weaponfunc_shoot.damage  @ 0x2c (f32)
			 *   weaponfunc_throw.damage  @ 0x2c (f32) [same offset]
			 *   weaponfunc_melee.damage  @ 0x20 (f32)
			 *
			 * Pre-fix wrote all three; the me write corrupted byte
			 * 0x20-0x23 of shoot variants' recoilsettings (low 4
			 * bytes). See recoverytime60 above for full rationale. */
			f32 v = jread_float(s, 0);
			if (struct_name[0] == '\0'
					|| strcmp(struct_name, "weaponfunc_shootsingle") == 0
					|| strcmp(struct_name, "weaponfunc_shootauto") == 0
					|| strcmp(struct_name, "weaponfunc_shootprojectile") == 0) {
				out->ss.base.damage = v;
			} else if (strcmp(struct_name, "weaponfunc_throw") == 0) {
				out->tw.damage = v;
			} else if (strcmp(struct_name, "weaponfunc_melee") == 0) {
				out->me.damage = v;
			}
		}
		else if (jstream_str_eq(&key, "spread")) {
			f32 v = jread_float(s, 0);
			if (weaponFuncStructIsShootFamily(struct_name)) {
				out->ss.base.spread = v;
			}
		}
		else if (jstream_str_eq(&key, "unk24")) {
			/* S484-followup-4: variant-specific offsets.
			 *   weaponfunc_shoot.unk24  @ 0x34 (s8)
			 *   weaponfunc_melee.unk24  @ 0x30 (u32)
			 * Different fields, different offsets, different sizes. */
			s32 v = jread_int(s, 0);
			if (weaponFuncStructIsShootFamily(struct_name)) {
				out->ss.base.unk24 = (s8)v;
			} else if (strcmp(struct_name, "weaponfunc_melee") == 0) {
				out->me.unk24 = (u32)v;
			}
		}
		/* Catalog coverage audit (2026-05-01) Section 4.4 closure:
		 * gate the remaining shoot-only field writes on struct_name so a
		 * non-shoot variant's JSON cannot corrupt fields at the same
		 * byte offset in another union member. Same discipline as
		 * unk24 / recoverytime60 / damage from S484-followup-4. */
		else if (jstream_str_eq(&key, "unk25")) {
			s32 v = jread_int(s, 0);
			if (weaponFuncStructIsShootFamily(struct_name)) {
				out->ss.base.unk25 = (s8)v;
			}
		}
		else if (jstream_str_eq(&key, "unk26")) {
			s32 v = jread_int(s, 0);
			if (weaponFuncStructIsShootFamily(struct_name)) {
				out->ss.base.unk26 = (s8)v;
			}
		}
		else if (jstream_str_eq(&key, "unk27")) {
			s32 v = jread_int(s, 0);
			if (weaponFuncStructIsShootFamily(struct_name)) {
				out->ss.base.unk27 = (s8)v;
			}
		}
		else if (jstream_str_eq(&key, "recoildist")) {
			f32 v = jread_float(s, 0);
			if (weaponFuncStructIsShootFamily(struct_name)) {
				out->ss.base.recoildist = v;
			}
		}
		else if (jstream_str_eq(&key, "recoilangle")) {
			f32 v = jread_float(s, 0);
			if (weaponFuncStructIsShootFamily(struct_name)) {
				out->ss.base.recoilangle = v;
			}
		}
		else if (jstream_str_eq(&key, "slidemax")) {
			f32 v = jread_float(s, 0);
			if (weaponFuncStructIsShootFamily(struct_name)) {
				out->ss.base.slidemax = v;
			}
		}
		else if (jstream_str_eq(&key, "impactforce")) {
			f32 v = jread_float(s, 0);
			if (weaponFuncStructIsShootFamily(struct_name)) {
				out->ss.base.impactforce = v;
			}
		}
		else if (jstream_str_eq(&key, "duration60")) {
			s32 v = jread_int(s, 0);
			if (weaponFuncStructIsShootFamily(struct_name)) {
				out->ss.base.duration60 = (u8)v;
			}
		}
		else if (jstream_str_eq(&key, "shootsound")) {
			s32 v = jread_enum_or_int(s, JREF_SFX, 0, "weaponfunc.shootsound");
			if (weaponFuncStructIsShootFamily(struct_name)) {
				out->ss.base.shootsound = (u16)v;
			}
		}
		else if (jstream_str_eq(&key, "penetration")) {
			s32 v = jread_int(s, 0);
			if (weaponFuncStructIsShootFamily(struct_name)) {
				out->ss.base.penetration = (u8)v;
			}
		}
		/* shootauto extension -- gated on weaponfunc_shootauto so a
		 * non-shootauto JSON record cannot stomp on offsets 0x40-0x51
		 * of another union variant (Section 4.4 closure). */
		else if (jstream_str_eq(&key, "initialrpm")) {
			f32 v = jread_float(s, 0);
			if (struct_name[0] == '\0' || strcmp(struct_name, "weaponfunc_shootauto") == 0) {
				out->sa.initialrpm = v;
			}
		}
		else if (jstream_str_eq(&key, "maxrpm")) {
			f32 v = jread_float(s, 0);
			if (struct_name[0] == '\0' || strcmp(struct_name, "weaponfunc_shootauto") == 0) {
				out->sa.maxrpm = v;
			}
		}
		else if (jstream_str_eq(&key, "vibrationstart") || jstream_str_eq(&key, "vibrationmax")) {
			s32 is_max = jstream_str_eq(&key, "vibrationmax");
			if (s->cur.kind == JT_NULL) { jstream_advance(s); }
			else if (s->cur.kind == JT_LBRACK) {
				jstream_advance(s);
				f32 *arr = &s_Vibrations[s_VibrationsUsed];
				s32 reserved = s_VibrationsUsed;
				while (s->cur.kind != JT_RBRACK && s->cur.kind != JT_EOF) {
					if (s_VibrationsUsed < POOL_VIBRATIONS) {
						s_Vibrations[s_VibrationsUsed++] = jread_float(s, 0);
					} else {
						jstream_skip_value(s);
					}
					if (s->cur.kind == JT_COMMA) jstream_advance(s);
				}
				if (s->cur.kind == JT_RBRACK) jstream_advance(s);
				if (s_VibrationsUsed > reserved
						&& (struct_name[0] == '\0'
							|| strcmp(struct_name, "weaponfunc_shootauto") == 0)) {
					if (is_max) out->sa.vibrationmax = arr;
					else        out->sa.vibrationstart = arr;
				}
			} else {
				jstream_skip_value(s);
			}
		}
		else if (jstream_str_eq(&key, "turretaccel")) {
			s32 v = jread_int(s, 0);
			if (struct_name[0] == '\0' || strcmp(struct_name, "weaponfunc_shootauto") == 0) {
				out->sa.turretaccel = (s8)v;
			}
		}
		else if (jstream_str_eq(&key, "turretdecel")) {
			s32 v = jread_int(s, 0);
			if (struct_name[0] == '\0' || strcmp(struct_name, "weaponfunc_shootauto") == 0) {
				out->sa.turretdecel = (s8)v;
			}
		}
		/* shootprojectile extension */
		else if (jstream_str_eq(&key, "projectilemodelnum")) {
			/* S484-followup-4: variant-specific offsets.
			 *   weaponfunc_shootprojectile.projectilemodelnum @ 0x50
			 *   weaponfunc_throw.projectilemodelnum           @ 0x20
			 * The throw write at 0x20 corrupted recoilsettings on
			 * shootprojectile variants. */
			s32 v = jread_enum_or_int(s, JREF_FILE, 0, "weaponfunc.projectilemodelnum");
			if (struct_name[0] != '\0'
					&& strcmp(struct_name, "weaponfunc_shootprojectile") == 0) {
				out->sp.projectilemodelnum = v;
			} else if (struct_name[0] != '\0'
					&& strcmp(struct_name, "weaponfunc_throw") == 0) {
				out->tw.projectilemodelnum = v;
			}
		}
		else if (jstream_str_eq(&key, "unk44") && struct_name[0]) {
			if (strcmp(struct_name, "weaponfunc_shootprojectile") == 0) out->sp.unk44 = (u32)jread_int(s, 0);
			else if (strcmp(struct_name, "weaponfunc_melee") == 0) out->me.unk44 = jread_float(s, 0);
			else jstream_skip_value(s);
		}
		/* shootprojectile-only fields (Section 4.4): gate so a non-sp
		 * variant's JSON cannot stomp other variants' fields at the
		 * same offsets in the union. */
		else if (jstream_str_eq(&key, "scale")) {
			f32 v = jread_float(s, 0);
			if (struct_name[0] == '\0' || strcmp(struct_name, "weaponfunc_shootprojectile") == 0) {
				out->sp.scale = v;
			}
		}
		else if (jstream_str_eq(&key, "speed") && struct_name[0]) {
			if (strcmp(struct_name, "weaponfunc_shootprojectile") == 0) out->sp.speed = jread_int(s, 0);
			else jstream_skip_value(s);
		}
		else if (jstream_str_eq(&key, "unk50")) {
			f32 v = jread_float(s, 0);
			if (struct_name[0] == '\0' || strcmp(struct_name, "weaponfunc_shootprojectile") == 0) {
				out->sp.unk50 = v;
			}
		}
		else if (jstream_str_eq(&key, "traveldist")) {
			s32 v = jread_int(s, 0);
			if (struct_name[0] == '\0' || strcmp(struct_name, "weaponfunc_shootprojectile") == 0) {
				out->sp.traveldist = v;
			}
		}
		else if (jstream_str_eq(&key, "timer60")) {
			s32 v = jread_int(s, 0);
			if (struct_name[0] == '\0' || strcmp(struct_name, "weaponfunc_shootprojectile") == 0) {
				out->sp.timer60 = v;
			}
		}
		else if (jstream_str_eq(&key, "reflectangle")) {
			f32 v = jread_float(s, 0);
			if (struct_name[0] == '\0' || strcmp(struct_name, "weaponfunc_shootprojectile") == 0) {
				out->sp.reflectangle = v;
			}
		}
		else if (jstream_str_eq(&key, "soundnum")) {
			/* shootprojectile.soundnum @ 0x60 (s16) vs special.soundnum
			 * @ 0x1c (u16). Special variant is matched explicitly below
			 * with a stricter else-if; this branch handles shootprojectile
			 * (and any unspecified variant for backwards compat). */
			s32 v = jread_enum_or_int(s, JREF_SFX, 0, "weaponfunc.soundnum");
			if (struct_name[0] == '\0' || strcmp(struct_name, "weaponfunc_shootprojectile") == 0) {
				out->sp.soundnum = (s16)v;
			}
		}
		/* throw extension */
		else if (jstream_str_eq(&key, "activatetime60")) {
			s32 v = jread_int(s, 0);
			if (struct_name[0] == '\0' || strcmp(struct_name, "weaponfunc_throw") == 0) {
				out->tw.activatetime60 = (s16)v;
			}
		}
		/* melee extension */
		else if (jstream_str_eq(&key, "range")) {
			f32 v = jread_float(s, 0);
			if (struct_name[0] == '\0' || strcmp(struct_name, "weaponfunc_melee") == 0) {
				out->me.range = v;
			}
		}
		else if (jstream_str_eq(&key, "unk1c")) {
			s32 v = jread_int(s, 0);
			if (struct_name[0] == '\0' || strcmp(struct_name, "weaponfunc_melee") == 0) {
				out->me.unk1c = (u32)v;
			}
		}
		else if (jstream_str_eq(&key, "unk20")) {
			s32 v = jread_int(s, 0);
			if (struct_name[0] == '\0' || strcmp(struct_name, "weaponfunc_melee") == 0) {
				out->me.unk20 = (u32)v;
			}
		}
		else if (jstream_str_eq(&key, "unk28")) {
			f32 v = jread_float(s, 0);
			if (struct_name[0] == '\0' || strcmp(struct_name, "weaponfunc_melee") == 0) {
				out->me.unk28 = v;
			}
		}
		else if (jstream_str_eq(&key, "unk2c")) {
			f32 v = jread_float(s, 0);
			if (struct_name[0] == '\0' || strcmp(struct_name, "weaponfunc_melee") == 0) {
				out->me.unk2c = v;
			}
		}
		else if (jstream_str_eq(&key, "unk30"))         out->me.unk30  = jread_float(s, 0);
		else if (jstream_str_eq(&key, "unk34"))         out->me.unk34  = jread_float(s, 0);
		else if (jstream_str_eq(&key, "unk38"))         out->me.unk38  = jread_float(s, 0);
		else if (jstream_str_eq(&key, "unk3c"))         out->me.unk3c  = jread_float(s, 0);
		else if (jstream_str_eq(&key, "unk40"))         out->me.unk40  = jread_float(s, 0);
		else if (jstream_str_eq(&key, "unk48"))         out->me.unk48  = (u32)jread_int(s, 0);
		/* special extension */
		else if (jstream_str_eq(&key, "specialfunc"))   out->sx.specialfunc = jread_int(s, 0);
		else if (jstream_str_eq(&key, "soundnum") && struct_name[0]
		         && strcmp(struct_name, "weaponfunc_special") == 0) {
			out->sx.soundnum = (u16)jread_int(s, 0);
		}
		/* device extension */
		else if (jstream_str_eq(&key, "device")) out->dv.device = (u32)jread_int(s, 0);
		else jstream_skip_value(s);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
	}
	if (s->cur.kind == JT_RBRACE) jstream_advance(s);
	return (void *)out;
}

/* Parse a 2-element [func0, func1] or [ammo0, ammo1] array. The
 * `parse_one` callback handles each element (returns void* to a
 * pool-allocated record, or NULL). */
static void parsePairArray(jstream_t *s, void **slot0, void **slot1,
                            void *(*parse_one)(jstream_t *))
{
	*slot0 = NULL;
	*slot1 = NULL;
	if (s->cur.kind != JT_LBRACK) { jstream_skip_value(s); return; }
	jstream_advance(s);
	s32 idx = 0;
	while (s->cur.kind != JT_RBRACK && s->cur.kind != JT_EOF) {
		void *v = parse_one(s);
		if (idx == 0) *slot0 = v;
		else if (idx == 1) *slot1 = v;
		idx++;
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
	}
	if (s->cur.kind == JT_RBRACK) jstream_advance(s);
}

static void *parseAmmoElement(jstream_t *s) { return (void *)parseAmmoIfPresent(s); }
static void *parseFuncElement(jstream_t *s) { return parseWeaponFunc(s); }

/* Parse a [[mnemonic, args...], ...] array of gunviscmd opcodes. Returns
 * pointer to first allocated entry (count terminated by GUNVISCMD_END). */
static struct gunviscmd *parseGunviscmdsArray(jstream_t *s)
{
	if (s->cur.kind == JT_NULL) { jstream_advance(s); return NULL; }
	if (s->cur.kind != JT_LBRACK) { jstream_skip_value(s); return NULL; }
	jstream_advance(s);
	struct gunviscmd *first = &s_Gunviscmds[s_GunviscmdsUsed];
	s32 reserved = s_GunviscmdsUsed;
	while (s->cur.kind != JT_RBRACK && s->cur.kind != JT_EOF) {
		if (s_GunviscmdsUsed >= POOL_GUNVISCMDS) {
			sysLogPrintf(LOG_WARNING, "LOADER.POOL.WEAPON.POOL_FULL: gunviscmds");
			jstream_skip_value(s);
			if (s->cur.kind == JT_COMMA) jstream_advance(s);
			continue;
		}
		struct gunviscmd *slot = &s_Gunviscmds[s_GunviscmdsUsed++];
		memset(slot, 0, sizeof(*slot));
		if (s->cur.kind != JT_LBRACK) { jstream_skip_value(s); if (s->cur.kind == JT_COMMA) jstream_advance(s); continue; }
		jstream_advance(s);
		char mnem[32];
		if (s->cur.kind == JT_STRING) {
			jstream_str_copy(&s->cur, mnem, sizeof(mnem));
			jstream_advance(s);
		} else {
			mnem[0] = '\0';
		}
		if (strcmp(mnem, "end") == 0) {
			slot->type = GUNVISCMD_END;
		} else if (strcmp(mnem, "sethidden") == 0) {
			slot->type = GUNVISCMD_ALWAYSTRUE;
			slot->op = GUNVISOP_IFTRUE_SETHIDDEN;
			if (s->cur.kind == JT_COMMA) jstream_advance(s);
			slot->partnum = (u16)jread_int(s, 0);
		} else if (strcmp(mnem, "checkupgrade") == 0) {
			slot->type = GUNVISCMD_CHECKUPGRADE;
			if (s->cur.kind == JT_COMMA) jstream_advance(s);
			slot->param = (u16)jread_int(s, 0);
			if (s->cur.kind == JT_COMMA) jstream_advance(s);
			slot->op = (u8)jread_int(s, 0);
			if (s->cur.kind == JT_COMMA) jstream_advance(s);
			slot->partnum = (u16)jread_int(s, 0);
		} else if (strcmp(mnem, "checkinlefthand") == 0) {
			slot->type = GUNVISCMD_CHECKINLEFTHAND;
			if (s->cur.kind == JT_COMMA) jstream_advance(s);
			slot->op = (u8)jread_int(s, 0);
			if (s->cur.kind == JT_COMMA) jstream_advance(s);
			slot->partnum = (u16)jread_int(s, 0);
		} else if (strcmp(mnem, "checkinrighthand") == 0) {
			slot->type = GUNVISCMD_CHECKINRIGHTHAND;
			if (s->cur.kind == JT_COMMA) jstream_advance(s);
			slot->op = (u8)jread_int(s, 0);
			if (s->cur.kind == JT_COMMA) jstream_advance(s);
			slot->partnum = (u16)jread_int(s, 0);
		}
		while (s->cur.kind != JT_RBRACK && s->cur.kind != JT_EOF) {
			if (s->cur.kind == JT_COMMA) jstream_advance(s);
			else jstream_skip_value(s);
		}
		if (s->cur.kind == JT_RBRACK) jstream_advance(s);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
	}
	if (s->cur.kind == JT_RBRACK) jstream_advance(s);
	return s_GunviscmdsUsed > reserved ? first : NULL;
}

/* Parse [[part, visible], ..., [255]] modelpartvisibility array. */
static struct modelpartvisibility *parsePartvisArray(jstream_t *s)
{
	if (s->cur.kind == JT_NULL) { jstream_advance(s); return NULL; }
	if (s->cur.kind != JT_LBRACK) { jstream_skip_value(s); return NULL; }
	jstream_advance(s);
	struct modelpartvisibility *first = &s_Partvis[s_PartvisUsed];
	s32 reserved = s_PartvisUsed;
	while (s->cur.kind != JT_RBRACK && s->cur.kind != JT_EOF) {
		if (s_PartvisUsed >= POOL_PARTVIS) {
			sysLogPrintf(LOG_WARNING, "LOADER.POOL.WEAPON.POOL_FULL: partvis");
			jstream_skip_value(s);
			if (s->cur.kind == JT_COMMA) jstream_advance(s);
			continue;
		}
		struct modelpartvisibility *slot = &s_Partvis[s_PartvisUsed++];
		memset(slot, 0, sizeof(*slot));
		if (s->cur.kind == JT_LBRACK) {
			jstream_advance(s);
			slot->part = (u8)jread_int(s, 0);
			if (s->cur.kind == JT_COMMA) {
				jstream_advance(s);
				slot->visible = (u8)jread_int(s, 0);
			}
			while (s->cur.kind != JT_RBRACK && s->cur.kind != JT_EOF) {
				if (s->cur.kind == JT_COMMA) jstream_advance(s);
				else jstream_skip_value(s);
			}
			if (s->cur.kind == JT_RBRACK) jstream_advance(s);
		} else {
			jstream_skip_value(s);
		}
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
	}
	if (s->cur.kind == JT_RBRACK) jstream_advance(s);
	return s_PartvisUsed > reserved ? first : NULL;
}

/* ------------------------------------------------------------------ */
/* Weapon record parser                                               */
/* ------------------------------------------------------------------ */

static void parseWeapon(jstream_t *s)
{
	if (s->cur.kind != JT_LBRACE) { jstream_skip_value(s); return; }
	jstream_advance(s);

	s32 weapon_id = -1;
	struct weapon w;
	struct aibotweaponpreference bp;
	s32 bp_present = 0;
	char catalog_id[64] = {0};
	memset(&w, 0, sizeof(w));
	memset(&bp, 0, sizeof(bp));
	w.aimsettings = &s_DefaultAim;  /* default fallback */

	while (s->cur.kind != JT_RBRACE && s->cur.kind != JT_EOF) {
		if (s->cur.kind != JT_STRING) { jstream_advance(s); continue; }
		jtok_t key = s->cur;
		jstream_advance(s);
		if (s->cur.kind != JT_COLON) continue;
		jstream_advance(s);

		if      (jstream_str_eq(&key, "id")) jread_string_buf(s, catalog_id, sizeof(catalog_id));
		else if (jstream_str_eq(&key, "symbol")) jstream_skip_value(s);
		else if (jstream_str_eq(&key, "weapon_id")) weapon_id = jread_int(s, -1);
		else if (jstream_str_eq(&key, "hi_model")) w.hi_model = (u16)jread_enum_or_int(s, JREF_FILE, 0, "weapon.hi_model");
		else if (jstream_str_eq(&key, "lo_model")) w.lo_model = (u16)jread_enum_or_int(s, JREF_FILE, 0, "weapon.lo_model");
		else if (jstream_str_eq(&key, "equip_animation")) {
			char anim[64]; jread_string_buf(s, anim, sizeof(anim));
			w.equip_animation = resolveAnimByName(anim);
		}
		else if (jstream_str_eq(&key, "unequip_animation")) {
			char anim[64]; jread_string_buf(s, anim, sizeof(anim));
			w.unequip_animation = resolveAnimByName(anim);
		}
		else if (jstream_str_eq(&key, "pritosec_animation")) {
			char anim[64]; jread_string_buf(s, anim, sizeof(anim));
			w.pritosec_animation = resolveAnimByName(anim);
		}
		else if (jstream_str_eq(&key, "sectopri_animation")) {
			char anim[64]; jread_string_buf(s, anim, sizeof(anim));
			w.sectopri_animation = resolveAnimByName(anim);
		}
		else if (jstream_str_eq(&key, "functions")) {
			void *f0, *f1;
			parsePairArray(s, &f0, &f1, parseFuncElement);
			w.functions[0] = f0;
			w.functions[1] = f1;
		}
		else if (jstream_str_eq(&key, "ammos")) {
			void *a0, *a1;
			parsePairArray(s, &a0, &a1, parseAmmoElement);
			w.ammos[0] = (struct inventory_ammo *)a0;
			w.ammos[1] = (struct inventory_ammo *)a1;
		}
		else if (jstream_str_eq(&key, "aimsettings")) {
			if (s->cur.kind == JT_NULL) { jstream_advance(s); }
			else {
				struct invaimsettings *aim = allocAimSettings(1, "weapon.aimsettings");
				if (aim) { parseAimSettingsInto(s, aim); w.aimsettings = aim; }
				else { jstream_skip_value(s); }
			}
		}
		else if (jstream_str_eq(&key, "muzzlez")) w.muzzlez = jread_float(s, 0);
		else if (jstream_str_eq(&key, "posx"))    w.posx    = jread_float(s, 0);
		else if (jstream_str_eq(&key, "posy"))    w.posy    = jread_float(s, 0);
		else if (jstream_str_eq(&key, "posz"))    w.posz    = jread_float(s, 0);
		else if (jstream_str_eq(&key, "sway"))    w.sway    = jread_float(s, 0);
		else if (jstream_str_eq(&key, "gunviscmds")) w.gunviscmds = parseGunviscmdsArray(s);
		else if (jstream_str_eq(&key, "partvisibility")) w.partvisibility = parsePartvisArray(s);
		else if (jstream_str_eq(&key, "shortname"))    w.shortname    = (u16)jread_enum_or_int(s, JREF_LANG, 0, "weapon.shortname");
		else if (jstream_str_eq(&key, "name"))         w.name         = (u16)jread_enum_or_int(s, JREF_LANG, 0, "weapon.name");
		else if (jstream_str_eq(&key, "manufacturer")) w.manufacturer = (u16)jread_enum_or_int(s, JREF_LANG, 0, "weapon.manufacturer");
		else if (jstream_str_eq(&key, "description"))  w.description  = (u16)jread_enum_or_int(s, JREF_LANG, 0, "weapon.description");
		else if (jstream_str_eq(&key, "flags"))        w.flags        = (u32)jread_int(s, 0);
		else if (jstream_str_eq(&key, "bot_pref")) {
			/* Parse the aibotweaponpreference sub-struct into a temp,
			 * then commit at the same slot as the weapon below. */
			if (s->cur.kind != JT_LBRACE) { jstream_skip_value(s); }
			else {
				jstream_advance(s);
				while (s->cur.kind != JT_RBRACE && s->cur.kind != JT_EOF) {
					if (s->cur.kind != JT_STRING) { jstream_advance(s); continue; }
					jtok_t bk = s->cur;
					jstream_advance(s);
					if (s->cur.kind != JT_COLON) continue;
					jstream_advance(s);
					if      (jstream_str_eq(&bk, "unk00"))                  bp.unk00                  = (u8)jread_int(s, 0);
					else if (jstream_str_eq(&bk, "unk01"))                  bp.unk01                  = (u8)jread_int(s, 0);
					else if (jstream_str_eq(&bk, "unk02"))                  bp.unk02                  = (u8)jread_int(s, 0);
					else if (jstream_str_eq(&bk, "unk03"))                  bp.unk03                  = (u8)jread_int(s, 0);
					else if (jstream_str_eq(&bk, "haspriammogoal"))         bp.haspriammogoal         = (u16)jread_int(s, 0);
					else if (jstream_str_eq(&bk, "hassecammogoal"))         bp.hassecammogoal         = (u16)jread_int(s, 0);
					else if (jstream_str_eq(&bk, "pridistconfig"))          bp.pridistconfig          = (u16)jread_int(s, 0);
					else if (jstream_str_eq(&bk, "secdistconfig"))          bp.secdistconfig          = (u16)jread_int(s, 0);
					else if (jstream_str_eq(&bk, "targetammopri"))          bp.targetammopri          = (u16)jread_int(s, 0);
					else if (jstream_str_eq(&bk, "targetammosec"))          bp.targetammosec          = (u16)jread_int(s, 0);
					else if (jstream_str_eq(&bk, "criticalammopri"))        bp.criticalammopri        = (u16)jread_int(s, 0);
					else if (jstream_str_eq(&bk, "criticalammosec"))        bp.criticalammosec        = (u16)jread_int(s, 0);
					else if (jstream_str_eq(&bk, "reloaddelay"))            bp.reloaddelay            = (u16)jread_int(s, 0);
					else if (jstream_str_eq(&bk, "allowpartialreloaddelay")) bp.allowpartialreloaddelay = (u16)jread_int(s, 0);
					else jstream_skip_value(s);
					if (s->cur.kind == JT_COMMA) jstream_advance(s);
				}
				if (s->cur.kind == JT_RBRACE) jstream_advance(s);
				bp_present = 1;
			}
		}
		else jstream_skip_value(s);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
	}
	if (s->cur.kind == JT_RBRACE) jstream_advance(s);

	if (s_ParseWeaponOverrideId >= 0) {
		weapon_id = s_ParseWeaponOverrideId;
	}

	if (weapon_id >= 0 && weapon_id < CATALOG_MGR_WEAPON_COUNT) {
		s_Weapons[weapon_id] = w;
		if (bp_present) s_BotPrefs[weapon_id] = bp;
		strncpy(s_WeaponCatalogIds[weapon_id], catalog_id,
			sizeof(s_WeaponCatalogIds[weapon_id]) - 1);
		s_WeaponCatalogIds[weapon_id][sizeof(s_WeaponCatalogIds[weapon_id]) - 1] = '\0';
		s_WeaponsRegistered++;
		assetCatalogRefreshWeaponPrivateSlotDefaults(weapon_id,
			&s_Weapons[weapon_id], bp_present ? &s_BotPrefs[weapon_id] : NULL);

		/* S484-followup-4 diag (2026-05-01): per-weapon name dump.
		 * Mike's playtest log surfaced a UI mismatch where Falcon 2
		 * Silencer's secondary fire label said "Rapid Fire"
		 * (L_GUN_086 = 19534) when the JSON specifies "Pistol Whip"
		 * (L_GUN_094 = 19542). The dispatch path was correct -- the
		 * actual fire mechanic used the right secondary -- but the
		 * label-side lookup of weapon->functions[1]->name returned a
		 * different langid than the JSON had. Surfacing the values
		 * stored in the manager pool right after parse so the next
		 * playtest log shows whether the parser stored the right
		 * langid (parser-side bug) or the UI is reading from a stale
		 * source (consumer-side bug). */
		const struct weaponfunc *fn0 =
			(const struct weaponfunc *)w.functions[0];
		const struct weaponfunc *fn1 =
			(const struct weaponfunc *)w.functions[1];
		const u32 shortname = (u32)w.shortname;
		const u32 name = (u32)w.name;
		const u32 fn0_name = fn0 ? (u32)fn0->name : 0u;
		const u32 fn1_name = fn1 ? (u32)fn1->name : 0u;
		const u32 fn0_type = fn0 ? (u32)fn0->type : 0u;
		const u32 fn1_type = fn1 ? (u32)fn1->type : 0u;
		sysLogPrintf(LOG_NOTE,
			"LOADER.POOL.WEAPON.STORED: weapon_id=%d "
			"shortname=%u name=%u "
			"functions[0]=%p name=%u type=%u "
			"functions[1]=%p name=%u type=%u",
			weapon_id, shortname, name,
			(const void *)fn0, fn0_name, fn0_type,
			(const void *)fn1, fn1_name, fn1_type);
	} else {
		sysLogPrintf(LOG_WARNING,
			"LOADER.POOL.WEAPON.RESOLVE_FAIL: weapon_id=%d out of range",
			weapon_id);
	}
}

/* ------------------------------------------------------------------ */
/* Catalog Gate 3 F12: head record parser                              */
/* ------------------------------------------------------------------ */

/* HEADBODYTYPE_* is a small (6-entry) enum. The values are stable
 * per src/include/constants.h. Resolved inline so loader_enum_reverse.c
 * does not need to grow another table; the matching reverse lookup at
 * loaderEnumNameForHeadbodyType keeps emit/parse round-trip in sync. */
static s32 s_resolveHeadbodyType(const char *name)
{
	if (!name || !name[0]) return 0;
	if (strcmp(name, "HEADBODYTYPE_DEFAULT")     == 0) return 0;
	if (strcmp(name, "HEADBODYTYPE_FEMALE")      == 0) return 1;
	if (strcmp(name, "HEADBODYTYPE_FEMALEGUARD") == 0) return 2;
	if (strcmp(name, "HEADBODYTYPE_CASS")        == 0) return 3;
	if (strcmp(name, "HEADBODYTYPE_MAIAN")       == 0) return 4;
	if (strcmp(name, "HEADBODYTYPE_MRBLONDE")    == 0) return 5;
	return -1;
}

static s32 jread_headbodytype(jstream_t *s)
{
	if (s->cur.kind == JT_STRING) {
		char buf[64];
		jread_string_buf(s, buf, sizeof(buf));
		s32 v = s_resolveHeadbodyType(buf);
		if (v < 0) {
			sysLogPrintf(LOG_NOTE,
				"LOADER.POOL.HEAD.FIELD_UNKNOWN: type=\"%s\" (defaulting to 0)",
				buf);
			return 0;
		}
		return v;
	}
	return jread_int(s, 0);
}

static void parseHead(jstream_t *s)
{
	if (s->cur.kind != JT_LBRACE) { jstream_skip_value(s); return; }
	jstream_advance(s);

	head_data_t h;
	memset(&h, 0, sizeof(h));
	h.scale = 1.0f;
	h.animscale = 1.0f;
	s32 headnum = -1;

	while (s->cur.kind != JT_RBRACE && s->cur.kind != JT_EOF) {
		if (s->cur.kind != JT_STRING) { jstream_advance(s); continue; }
		jtok_t key = s->cur;
		jstream_advance(s);
		if (s->cur.kind != JT_COLON) continue;
		jstream_advance(s);

		if      (jstream_str_eq(&key, "id")) {
			jread_string_buf(s, h.catalog_id, sizeof(h.catalog_id));
		}
		else if (jstream_str_eq(&key, "headnum"))   headnum = jread_int(s, -1);
		else if (jstream_str_eq(&key, "ismale"))    h.ismale    = (u8)jread_int(s, 0);
		else if (jstream_str_eq(&key, "unk00_01"))  h.unk00_01  = (u8)jread_int(s, 0);
		else if (jstream_str_eq(&key, "type"))      h.type      = (u8)jread_headbodytype(s);
		else if (jstream_str_eq(&key, "height"))    h.height    = (u16)jread_int(s, 0);
		/* Canonical schema key is "mesh" (universality-pivot-schemas.md
		 * Section 2.2). "filenum" alias accepted for forward-compat with
		 * any stale .pdhead emitted from a divergent build, but the
		 * shipping emitter writes "mesh". The mismatch between these two
		 * names was the root cause of B-328 (weapon visible=0, body
		 * handfilenum=0; all weapon-load / FP-render gating goes silent
		 * because the master loader can never reach LOADED state). */
		else if (jstream_str_eq(&key, "mesh")
		      || jstream_str_eq(&key, "filenum"))   h.filenum   = (u16)jread_enum_or_int(s, JREF_FILE, 0, "head.mesh");
		else if (jstream_str_eq(&key, "scale"))     h.scale     = jread_float(s, 1.0f);
		else if (jstream_str_eq(&key, "animscale")) h.animscale = jread_float(s, 1.0f);
		else jstream_skip_value(s);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
	}
	if (s->cur.kind == JT_RBRACE) jstream_advance(s);

	if (headnum < 0 || headnum >= CATALOG_MGR_HEAD_COUNT) {
		sysLogPrintf(LOG_WARNING,
			"LOADER.POOL.HEAD.RESOLVE_FAIL: headnum=%d out of range [0,%d)",
			headnum, CATALOG_MGR_HEAD_COUNT);
		return;
	}
	h.headnum = (s16)headnum;
	s_HeadsPool[headnum] = h;
	s_HeadsRegistered++;
}

/* ------------------------------------------------------------------ */
/* Catalog Gate 3 Bodies F12: body record parser.  Parallels parseHead. */
/* ------------------------------------------------------------------ */

static void parseBody(jstream_t *s)
{
	if (s->cur.kind != JT_LBRACE) { jstream_skip_value(s); return; }
	jstream_advance(s);

	body_data_t b;
	memset(&b, 0, sizeof(b));
	b.scale = 1.0f;
	b.animscale = 1.0f;
	s32 bodynum = -1;

	while (s->cur.kind != JT_RBRACE && s->cur.kind != JT_EOF) {
		if (s->cur.kind != JT_STRING) { jstream_advance(s); continue; }
		jtok_t key = s->cur;
		jstream_advance(s);
		if (s->cur.kind != JT_COLON) continue;
		jstream_advance(s);

		if      (jstream_str_eq(&key, "id")) {
			jread_string_buf(s, b.catalog_id, sizeof(b.catalog_id));
		}
		else if (jstream_str_eq(&key, "bodynum"))       bodynum = jread_int(s, -1);
		else if (jstream_str_eq(&key, "ismale"))        b.ismale        = (u8)jread_int(s, 0);
		else if (jstream_str_eq(&key, "unk00_01"))      b.unk00_01      = (u8)jread_int(s, 0);
		else if (jstream_str_eq(&key, "canvaryheight")) b.canvaryheight = (u8)jread_int(s, 0);
		else if (jstream_str_eq(&key, "type"))          b.type          = (u8)jread_headbodytype(s);
		else if (jstream_str_eq(&key, "height"))        b.height        = (u16)jread_int(s, 0);
		/* Canonical schema keys are "mesh" / "hand" (universality-pivot-
		 * schemas.md Section 2.3). The "filenum" / "handfilenum" aliases
		 * are kept for forward-compat with any stale .pdbody emitted by a
		 * divergent build. See B-328: the rename mismatch caused
		 * b.filenum and b.handfilenum to stay 0, so the body mesh failed
		 * to bind and the master gun-loader stalled at HANDS state. */
		else if (jstream_str_eq(&key, "mesh")
		      || jstream_str_eq(&key, "filenum"))       b.filenum       = (u16)jread_enum_or_int(s, JREF_FILE, 0, "body.mesh");
		else if (jstream_str_eq(&key, "scale"))         b.scale         = jread_float(s, 1.0f);
		else if (jstream_str_eq(&key, "animscale"))     b.animscale     = jread_float(s, 1.0f);
		else if (jstream_str_eq(&key, "hand")
		      || jstream_str_eq(&key, "handfilenum"))   b.handfilenum   = (u16)jread_enum_or_int(s, JREF_FILE, 0, "body.hand");
		else jstream_skip_value(s);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
	}
	if (s->cur.kind == JT_RBRACE) jstream_advance(s);

	if (bodynum < 0 || bodynum >= CATALOG_MGR_BODY_COUNT) {
		sysLogPrintf(LOG_WARNING,
			"LOADER.POOL.BODY.RESOLVE_FAIL: bodynum=%d out of range [0,%d)",
			bodynum, CATALOG_MGR_BODY_COUNT);
		return;
	}
	b.bodynum = (s16)bodynum;
	s_BodiesPool[bodynum] = b;
	s_BodiesRegistered++;
}

/* ------------------------------------------------------------------ */
/* Arena record parser. Parallels parseHead / parseBody. The per-asset */
/* envelope emits stagenum / name_langid as integers (Path B for these */
/* large enum families); load_mode stays symbolic and is resolved via  */
/* the inline 3-entry helper.                                          */
/* ------------------------------------------------------------------ */

static s32 s_resolveArenaLoadMode(const char *name)
{
	if (!name || !name[0]) return 0;
	if (strcmp(name, "ARENA_LOADMODE_PLAYABLE") == 0) return 0;
	if (strcmp(name, "ARENA_LOADMODE_CANVAS")   == 0) return 1;
	return -1;
}

static s32 jread_arena_load_mode(jstream_t *s)
{
	if (s->cur.kind == JT_STRING) {
		char buf[64];
		jread_string_buf(s, buf, sizeof(buf));
		s32 v = s_resolveArenaLoadMode(buf);
		if (v < 0) {
			sysLogPrintf(LOG_NOTE,
				"LOADER.POOL.ARENA.FIELD_UNKNOWN: load_mode=\"%s\" (defaulting to 0)",
				buf);
			return 0;
		}
		return v;
	}
	return jread_int(s, 0);
}

static void parseArena(jstream_t *s)
{
	if (s->cur.kind != JT_LBRACE) { jstream_skip_value(s); return; }
	jstream_advance(s);

	arena_data_t a;
	memset(&a, 0, sizeof(a));
	s32 arena_index = -1;

	while (s->cur.kind != JT_RBRACE && s->cur.kind != JT_EOF) {
		if (s->cur.kind != JT_STRING) { jstream_advance(s); continue; }
		jtok_t key = s->cur;
		jstream_advance(s);
		if (s->cur.kind != JT_COLON) continue;
		jstream_advance(s);

		if      (jstream_str_eq(&key, "id")) {
			jread_string_buf(s, a.catalog_id, sizeof(a.catalog_id));
		}
		else if (jstream_str_eq(&key, "arena_index"))     arena_index       = jread_int(s, -1);
		else if (jstream_str_eq(&key, "slug")) {
			jread_string_buf(s, a.slug, sizeof(a.slug));
		}
		else if (jstream_str_eq(&key, "category")) {
			jread_string_buf(s, a.category, sizeof(a.category));
		}
		else if (jstream_str_eq(&key, "stagenum"))        a.stagenum        = (s16)jread_int(s, 0);
		else if (jstream_str_eq(&key, "requirefeature"))  a.requirefeature  = (u8)jread_int(s, 0);
		else if (jstream_str_eq(&key, "name_langid"))     a.name_langid     = jread_int(s, 0);
		else if (jstream_str_eq(&key, "load_mode"))       a.load_mode       = (u8)jread_arena_load_mode(s);
		else jstream_skip_value(s);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
	}
	if (s->cur.kind == JT_RBRACE) jstream_advance(s);

	if (arena_index < 0 || arena_index >= CATALOG_MGR_ARENA_COUNT) {
		sysLogPrintf(LOG_WARNING,
			"LOADER.POOL.ARENA.RESOLVE_FAIL: arena_index=%d out of range [0,%d)",
			arena_index, CATALOG_MGR_ARENA_COUNT);
		return;
	}
	a.arena_index = (s16)arena_index;
	s_ArenasPool[arena_index] = a;
	s_ArenasRegistered++;
}

/* ------------------------------------------------------------------ */
/* Public per-asset entry points                                       */
/* ------------------------------------------------------------------ */

/* Drive a parse over `json` (NUL-terminated) using the per-record
 * parser supplied by the caller. The walker scaffold passes plain
 * .pdweapon _meta/manifest.json or the manifest envelope extracted from a .pdhead /
 * .pdbody / .pdarena ZIP -- both shapes carry the per-record fields
 * at the top level, so the same parser handles them. Returns 1 on
 * success (parser reached an end-of-record), 0 if the JSON could not
 * even be opened (caller logs at the walker layer). */
static s32 s_parseOneRecord(const char *json, size_t json_len,
                             void (*parse_one)(jstream_t *))
{
	if (json == NULL || json_len == 0 || parse_one == NULL) return 0;
	jstream_t st;
	memset(&st, 0, sizeof(st));
	st.src = json;
	st.pos = json;
	st.end = json + json_len;
	st.line = 1;
	jstream_advance(&st);
	parse_one(&st);
	return st.error ? 0 : 1;
}

void loaderPoolReset(void)
{
	s_poolEnsureMutex();
	POOL_LOCK();
	memset(s_Weapons, 0, sizeof(s_Weapons));
	memset(s_BotPrefs, 0, sizeof(s_BotPrefs));
	memset(s_WeaponCatalogIds, 0, sizeof(s_WeaponCatalogIds));
	memset(s_Guncmds, 0, sizeof(s_Guncmds));
	memset(s_Gunviscmds, 0, sizeof(s_Gunviscmds));
	memset(s_Partvis, 0, sizeof(s_Partvis));
	memset(s_Ammos, 0, sizeof(s_Ammos));
	memset(s_AimSettings, 0, sizeof(s_AimSettings));
	memset(s_NoiseSettings, 0, sizeof(s_NoiseSettings));
	memset(s_RecoilSettings, 0, sizeof(s_RecoilSettings));
	memset(s_WeaponFuncs, 0, sizeof(s_WeaponFuncs));
	memset(s_Vibrations, 0, sizeof(s_Vibrations));
	memset(s_Animations, 0, sizeof(s_Animations));
	memset(s_AnimFixups, 0, sizeof(s_AnimFixups));
	memset(s_HeadsPool, 0, sizeof(s_HeadsPool));
	memset(s_BodiesPool, 0, sizeof(s_BodiesPool));
	memset(s_ArenasPool, 0, sizeof(s_ArenasPool));
	s_GuncmdsUsed = 0;
	s_GunviscmdsUsed = 0;
	s_PartvisUsed = 0;
	s_AmmosUsed = 0;
	s_AimSettingsUsed = 0;
	s_NoiseSettingsUsed = 0;
	s_RecoilSettingsUsed = 0;
	s_WeaponFuncsUsed = 0;
	s_VibrationsUsed = 0;
	s_AnimationsUsed = 0;
	s_AnimFixupsUsed = 0;
	s_LoaderActive = 0;
	s_WeaponsRegistered = 0;
	s_HeadsLoaderActive = 0;
	s_HeadsRegistered = 0;
	s_BodiesLoaderActive = 0;
	s_BodiesRegistered = 0;
	s_ArenasLoaderActive = 0;
	s_ArenasRegistered = 0;
	POOL_UNLOCK();
}

s32 loaderPoolParseWeaponJson(const char *json, size_t json_len)
{
	s_poolEnsureMutex();
	POOL_LOCK();
	s_ParseWeaponOverrideId = -1;
	s32 r = s_parseOneRecord(json, json_len, parseWeapon);
	s_ParseWeaponOverrideId = -1;
	POOL_UNLOCK();
	return r;
}

s32 loaderPoolParseWeaponJsonWithRuntimeSlot(const char *json, size_t json_len,
                s32 runtime_weapon_id)
{
	s_poolEnsureMutex();
	POOL_LOCK();
	s_ParseWeaponOverrideId = runtime_weapon_id;
	s32 r = s_parseOneRecord(json, json_len, parseWeapon);
	s_ParseWeaponOverrideId = -1;
	POOL_UNLOCK();
	return r;
}

s32 loaderPoolParseHeadJson(const char *json, size_t json_len)
{
	s_poolEnsureMutex();
	POOL_LOCK();
	s32 r = s_parseOneRecord(json, json_len, parseHead);
	POOL_UNLOCK();
	return r;
}

s32 loaderPoolParseBodyJson(const char *json, size_t json_len)
{
	s_poolEnsureMutex();
	POOL_LOCK();
	s32 r = s_parseOneRecord(json, json_len, parseBody);
	POOL_UNLOCK();
	return r;
}

s32 loaderPoolParseArenaJson(const char *json, size_t json_len)
{
	s_poolEnsureMutex();
	POOL_LOCK();
	s32 r = s_parseOneRecord(json, json_len, parseArena);
	POOL_UNLOCK();
	return r;
}

s32 loaderPoolParseAnimationJson(const char *json, size_t json_len)
{
	return loaderPoolParseAnimationSourceJson(json, json_len, NULL);
}

s32 loaderPoolParseAnimationSourceJson(const char *json, size_t json_len,
                const char *source_path)
{
	s_poolEnsureMutex();
	POOL_LOCK();
	const char *prev_source_path = s_ParseAnimationSourcePath;
	s_ParseAnimationSourcePath = source_path;
	s32 r = s_parseOneRecord(json, json_len, parseAnimation);
	s_ParseAnimationSourcePath = prev_source_path;
	POOL_UNLOCK();
	return r;
}

void loaderPoolFinalize(void)
{
	s_poolEnsureMutex();
	POOL_LOCK();
	/* Seed default fallback aim / noise sentinels (historical
	 * invaimsettings_default + invnoisesettings_silent values) so
	 * weapons that did not supply their own settings still have
	 * a non-NULL pointer. Pre-Step-5 these were per-extern globals;
	 * post-Step-5 the loader owns them inline. */
	{
		struct invaimsettings def_aim = {
			0,                                   /* zoomfov */
			3,                                   /* guntransup */
			8,                                   /* guntransdown */
			15,                                  /* guntransside */
			0.9721f,                             /* aimdamppal */
			0.9767f,                             /* aimdamp */
			SIGHTTRACKTYPE_DEFAULT,              /* tracktype */
			0,                                   /* unk18_04 */
			INVAIMFLAG_AUTOAIM,                  /* flags */
		};
		s_DefaultAim = def_aim;
	}
	{
		struct noisesettings def_noise = { 0, 0, 0, 1, 6 };
		s_DefaultNoise = def_noise;
	}

	if (s_WeaponsRegistered > 0)  s_LoaderActive       = 1;
	if (s_HeadsRegistered > 0)    s_HeadsLoaderActive  = 1;
	if (s_BodiesRegistered > 0)   s_BodiesLoaderActive = 1;
	if (s_ArenasRegistered > 0)   s_ArenasLoaderActive = 1;

	/* Deferred fixup pass for GUNCMD_INCLUDE / GUNCMD_RANDOM forward
	 * references: every fixup recorded during decodeOpcode gets its
	 * unk04 pointer patched here, after every .pdanim has been parsed
	 * into s_Animations. Any still-unresolved name is loud (LOG_WARNING
	 * with the failing reference) so the underlying missing animation
	 * surfaces in the log instead of silently leaving a NULL pointer
	 * that crashes during gameplay. */
	s32 fixupOk = 0, fixupFail = 0;
	for (s32 fi = 0; fi < s_AnimFixupsUsed; fi++) {
		pool_anim_fixup_t *fx = &s_AnimFixups[fi];
		if (fx->slot == NULL) continue;
		struct guncmd *resolved = resolveAnimByName(fx->anim_name);
		if (resolved != NULL) {
			fx->slot->unk04 = (intptr_t)resolved;
			fixupOk++;
		} else {
			sysLogPrintf(LOG_WARNING,
				"LOADER.POOL.WEAPON.FIXUP_MISS: unresolved animation '%s' "
				"referenced by GUNCMD_INCLUDE/RANDOM -- unk04 left NULL, "
				"bgunStartAnimation will skip-guard at runtime",
				fx->anim_name);
			fixupFail++;
		}
	}
	if (s_AnimFixupsUsed > 0) {
		sysLogPrintf(LOG_NOTE,
			"LOADER.POOL.WEAPON.FIXUP: deferred=%d resolved=%d unresolved=%d",
			s_AnimFixupsUsed, fixupOk, fixupFail);
	}

	sysLogPrintf(LOG_NOTE,
		"LOADER.POOL.WEAPON.OK: active=%d weapons=%d animations=%d funcs=%d "
		"ammos=%d aim=%d noise=%d recoil=%d guncmds=%d gunvis=%d partvis=%d "
		"vibrations=%d",
		s_LoaderActive, s_WeaponsRegistered, s_AnimationsUsed,
		s_WeaponFuncsUsed, s_AmmosUsed, s_AimSettingsUsed,
		s_NoiseSettingsUsed, s_RecoilSettingsUsed, s_GuncmdsUsed,
		s_GunviscmdsUsed, s_PartvisUsed, s_VibrationsUsed);
	sysLogPrintf(LOG_NOTE,
		"LOADER.POOL.HEAD.OK: active=%d heads=%d (expected=%d)",
		s_HeadsLoaderActive, s_HeadsRegistered, CATALOG_MGR_HEAD_COUNT);
	sysLogPrintf(LOG_NOTE,
		"LOADER.POOL.BODY.OK: active=%d bodies=%d (expected=%d)",
		s_BodiesLoaderActive, s_BodiesRegistered, CATALOG_MGR_BODY_COUNT);
	sysLogPrintf(LOG_NOTE,
		"LOADER.POOL.ARENA.OK: active=%d arenas=%d (expected=%d)",
		s_ArenasLoaderActive, s_ArenasRegistered, CATALOG_MGR_ARENA_COUNT);
	POOL_UNLOCK();
}

/* The pre-Step-5 aggregate-archive scan + per-kind BuildManager helpers
 * + arena parity check are retired. The walker drives parsing per asset;
 * loaderPoolFinalize flips the active flags. The walker is the sole
 * source for both rows and pool slots, so there is no second source
 * left to compare against. */

/* ================================================================== */
/* Heads pool accessors                                                 */
/* ================================================================== */

s32 loaderPoolHeadsActive(void)
{
	return s_HeadsLoaderActive;
}

const head_data_t *loaderPoolGetHead(s32 idx)
{
	if (idx < 0 || idx >= CATALOG_MGR_HEAD_COUNT) return NULL;
	if (!s_HeadsLoaderActive) return NULL;
	return &s_HeadsPool[idx];
}

s32 loaderPoolGetHeadsRegistered(void)
{
	return s_HeadsRegistered;
}

/* ================================================================== */
/* Bodies pool accessors                                                */
/* ================================================================== */

s32 loaderPoolBodiesActive(void)
{
	return s_BodiesLoaderActive;
}

const body_data_t *loaderPoolGetBody(s32 idx)
{
	if (idx < 0 || idx >= CATALOG_MGR_BODY_COUNT) return NULL;
	if (!s_BodiesLoaderActive) return NULL;
	return &s_BodiesPool[idx];
}

s32 loaderPoolGetBodiesRegistered(void)
{
	return s_BodiesRegistered;
}

/* ================================================================== */
/* Arenas pool accessors                                                */
/* ================================================================== */

s32 loaderPoolArenasActive(void)
{
	return s_ArenasLoaderActive;
}

const arena_data_t *loaderPoolGetArena(s32 idx)
{
	if (idx < 0 || idx >= CATALOG_MGR_ARENA_COUNT) return NULL;
	if (!s_ArenasLoaderActive) return NULL;
	return &s_ArenasPool[idx];
}

s32 loaderPoolGetArenasRegistered(void)
{
	return s_ArenasRegistered;
}
