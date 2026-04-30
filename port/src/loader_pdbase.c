/*
 * port/src/loader_pdbase.c -- S484 F12: .pdbase loader implementation.
 *
 * F10 (S484) shipped the scaffold; F11 (S591) shipped the generated
 * base/weapons.pdbase archive + the Python extractor. This file is the
 * F12 runtime: a JSON parser + opcode codec + manager-owned pools that
 * are populated at startup from the JSON archive.
 *
 * Design ref: context/designs/catalog/catalog-full-pipeline-weapons.md
 *             Sections C, D, E (loader integration).
 *
 * Memory model (per Mike's 2026-04-30 unlock-state clarification):
 *   - The catalog row layer (assetcatalog) stores per-row identity +
 *     unlock metadata. Registration is unconditional: every parseable
 *     .pdbase entry registers, regardless of unlock state. Selectors
 *     filter `catalog union unlocked` separately.
 *   - The manager-owned pools below hold the typed runtime payload:
 *     struct weapon, struct weaponfunc_*, struct inventory_ammo, etc.
 *     One pool per record type; arena-style allocation; populated
 *     once at startup, freed at shutdown. The pools are reachable by
 *     loaderPdbaseGetWeapon(idx) and the catalog manager routes
 *     accessors through them after loaderPdbaseBuildWeaponManager.
 *
 * Single-thread invariant: the loader runs on the main thread at
 * startup, before any other system can read weapon data. No locking.
 * Future multi-threaded loading would gate the pools behind an
 * acquire/release fence on s_LoaderActive.
 *
 * Logging channels (per directive):
 *   LOADER.PDBASE.WEAPON.OK:           summary at end of load
 *   LOADER.PDBASE.WEAPON.SCAN_FAIL:    archive open / JSON parse error
 *   LOADER.PDBASE.WEAPON.RESOLVE_FAIL: required reference unresolvable
 *                                       (animation name, enum string)
 *   LOADER.PDBASE.WEAPON.FIELD_UNKNOWN: tolerated but logged
 *   LOADER.PDBASE.WEAPON.PARITY_FAIL:  field-equivalence mismatch vs
 *                                       g_Weapons[] (F12 self-test)
 *   LOADER.PDBASE.WEAPON.POOL_FULL:    pool capacity exhausted (treat
 *                                       as a sizing bug; raise the cap)
 */

#include <ultra64.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "data.h"
#include "types.h"
#include "constants.h"
#include "loader_pdbase.h"
#include "loader_pdbase_enums.h"
#include "catalog_mgr_weapons.h"
#include "system.h"
#include "fs.h"

/* S484 F13: g_Weapons[], invaimsettings_default, invnoisesettings_silent
 * retired 2026-04-30. Default fallbacks now sourced from .pdbase metadata
 * or hardcoded sentinels in this file. The parity self-test from F12
 * also retires here -- there is nothing to compare against. */

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
	s32   cmd_offset;
	s32   cmd_count;
} pdbase_anim_entry_t;

static struct weapon                  s_Weapons[CATALOG_MGR_WEAPON_COUNT];
static struct aibotweaponpreference   s_BotPrefs[CATALOG_MGR_WEAPON_COUNT];
static struct guncmd                  s_Guncmds[POOL_GUNCMDS];
static struct gunviscmd               s_Gunviscmds[POOL_GUNVISCMDS];
static struct modelpartvisibility     s_Partvis[POOL_PARTVIS];
static struct inventory_ammo          s_Ammos[POOL_AMMOS];
static struct invaimsettings          s_AimSettings[POOL_AIMSETTINGS];
static struct noisesettings           s_NoiseSettings[POOL_NOISESETTINGS];
static struct recoilsettings          s_RecoilSettings[POOL_RECOILSETTINGS];
static weaponfunc_any_t               s_WeaponFuncs[POOL_WEAPONFUNCS];
static f32                            s_Vibrations[POOL_VIBRATIONS];
static pdbase_anim_entry_t            s_Animations[POOL_ANIMATIONS];
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

static s32 s_LoaderActive;
static s32 s_WeaponsRegistered;

/* ------------------------------------------------------------------ */
/* Public accessors (consumed by catalog_mgr_weapons.c)               */
/* ------------------------------------------------------------------ */

s32 loaderPdbaseIsActive(void) { return s_LoaderActive; }

const struct weapon *loaderPdbaseGetWeapon(s32 idx)
{
	if (idx < 0 || idx >= CATALOG_MGR_WEAPON_COUNT) return NULL;
	if (!s_LoaderActive) return NULL;
	return &s_Weapons[idx];
}

const struct invaimsettings *loaderPdbaseGetDefaultAim(void)
{
	return s_LoaderActive ? &s_DefaultAim : NULL;
}

const struct noisesettings *loaderPdbaseGetDefaultNoise(void)
{
	return s_LoaderActive ? &s_DefaultNoise : NULL;
}

const struct aibotweaponpreference *loaderPdbaseGetBotPref(s32 idx)
{
	if (idx < 0 || idx >= CATALOG_MGR_WEAPON_COUNT) return NULL;
	if (!s_LoaderActive) return NULL;
	return &s_BotPrefs[idx];
}

s32 loaderPdbaseGetWeaponsRegistered(void) { return s_WeaponsRegistered; }

static struct guncmd *resolveAnimByName(const char *name)
{
	if (name == NULL) return NULL;
	for (s32 i = 0; i < s_AnimationsUsed; i++) {
		if (strcmp(s_Animations[i].name, name) == 0) {
			return &s_Guncmds[s_Animations[i].cmd_offset];
		}
	}
	return NULL;
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
				"LOADER.PDBASE.WEAPON.POOL_FULL: %s wants %d, "        \
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
		case JREF_ANIM: v = loaderPdbaseResolveAnimEnum(buf, fallback); break;
		case JREF_SFX:  v = loaderPdbaseResolveSfxEnum(buf, fallback); break;
		case JREF_FILE: v = loaderPdbaseResolveFileEnum(buf, fallback); break;
		case JREF_LANG: v = loaderPdbaseResolveLangEnum(buf, fallback); break;
		default:
			/* Try each table in turn, accept the first hit. */
			v = loaderPdbaseResolveLangEnum(buf, -1);
			if (v < 0) v = loaderPdbaseResolveAnimEnum(buf, -1);
			if (v < 0) v = loaderPdbaseResolveSfxEnum(buf, -1);
			if (v < 0) v = loaderPdbaseResolveFileEnum(buf, fallback);
			break;
		}
		if (v == fallback && buf[0] != '\0') {
			sysLogPrintf(LOG_WARNING,
				"LOADER.PDBASE.WEAPON.RESOLVE_FAIL: %s name=\"%s\"",
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

/* ------------------------------------------------------------------ */
/* Opcode codec                                                       */
/* ------------------------------------------------------------------ */

/* Decode a JSON opcode array element into one struct guncmd entry.
 * Returns 1 on success, 0 if the array element is malformed. */
static s32 decodeOpcode(jstream_t *s, struct guncmd *out)
{
	if (s->cur.kind != JT_LBRACK) {
		sysLogPrintf(LOG_WARNING,
			"LOADER.PDBASE.WEAPON.SCAN_FAIL: opcode not an array");
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
		out->unk04 = (intptr_t)jread_enum_or_int(s, JREF_SFX, 0, "playsound.sound");
	} else if (strcmp(mnem, "include") == 0) {
		out->type = GUNCMD_INCLUDE;
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		out->unk01 = (u8)jread_int(s, 0);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		/* address is an animation name; resolved in second pass */
		char anim_name[64];
		jread_string_buf(s, anim_name, sizeof(anim_name));
		struct guncmd *resolved = resolveAnimByName(anim_name);
		out->unk04 = (intptr_t)resolved;
	} else if (strcmp(mnem, "random") == 0) {
		out->type = GUNCMD_RANDOM;
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		out->unk02 = (u16)jread_int(s, 0);
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
		char anim_name[64];
		jread_string_buf(s, anim_name, sizeof(anim_name));
		struct guncmd *resolved = resolveAnimByName(anim_name);
		out->unk04 = (intptr_t)resolved;
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
			"LOADER.PDBASE.WEAPON.RESOLVE_FAIL: unknown opcode mnem=\"%s\"",
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

/* Reverse of decodeOpcode: encode a struct guncmd back to a JSON-ish
 * representation used only by the F12 round-trip self-test. Returns
 * 1 on success and writes to `out_buf` (NUL-terminated). */
s32 loaderPdbaseEncodeOpcode(const struct guncmd *cmd, char *out_buf,
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
		s32 is_id      = jstream_str_eq(&s->cur, "id");
		s32 is_opcodes = jstream_str_eq(&s->cur, "opcodes");
		jstream_advance(s);  /* consume key */
		if (s->cur.kind != JT_COLON) continue;
		jstream_advance(s);  /* consume : */
		if (is_id) {
			jread_string_buf(s, anim_name, sizeof(anim_name));
		} else if (is_opcodes) {
			if (s->cur.kind != JT_LBRACK) { jstream_skip_value(s); }
			else {
				jstream_advance(s);  /* consume [ */
				cmds_start = &s_Guncmds[s_GuncmdsUsed];
				s32 reserved_start = s_GuncmdsUsed;
				while (s->cur.kind != JT_RBRACK && s->cur.kind != JT_EOF) {
					if (s_GuncmdsUsed >= POOL_GUNCMDS) {
						sysLogPrintf(LOG_WARNING,
							"LOADER.PDBASE.WEAPON.POOL_FULL: guncmds while "
							"parsing %s", anim_name);
						jstream_skip_value(s);
						if (s->cur.kind == JT_COMMA) jstream_advance(s);
						continue;
					}
					struct guncmd *slot = &s_Guncmds[s_GuncmdsUsed];
					s_GuncmdsUsed++;
					decodeOpcode(s, slot);
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
		pdbase_anim_entry_t *e = &s_Animations[s_AnimationsUsed++];
		size_t n = strlen(anim_name);
		if (n >= sizeof(e->name)) n = sizeof(e->name) - 1;
		memcpy(e->name, anim_name, n);
		e->name[n] = '\0';
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
			/* throw uses s32, others use s8 -- keep both views in sync. */
			s32 v = jread_int(s, 0);
			out->ss.base.recoverytime60 = (s8)v;
			out->tw.recoverytime60 = v;
			out->sx.recoverytime60 = v;
		}
		else if (jstream_str_eq(&key, "damage")) {
			f32 v = jread_float(s, 0);
			out->ss.base.damage = v;
			out->tw.damage = v;
			out->me.damage = v;
		}
		else if (jstream_str_eq(&key, "spread"))      out->ss.base.spread       = jread_float(s, 0);
		else if (jstream_str_eq(&key, "unk24")) {
			s32 v = jread_int(s, 0);
			out->ss.base.unk24 = (s8)v;
			out->me.unk24 = (u32)v;
		}
		else if (jstream_str_eq(&key, "unk25")) out->ss.base.unk25 = (s8)jread_int(s, 0);
		else if (jstream_str_eq(&key, "unk26")) out->ss.base.unk26 = (s8)jread_int(s, 0);
		else if (jstream_str_eq(&key, "unk27")) out->ss.base.unk27 = (s8)jread_int(s, 0);
		else if (jstream_str_eq(&key, "recoildist"))  out->ss.base.recoildist  = jread_float(s, 0);
		else if (jstream_str_eq(&key, "recoilangle")) out->ss.base.recoilangle = jread_float(s, 0);
		else if (jstream_str_eq(&key, "slidemax"))    out->ss.base.slidemax    = jread_float(s, 0);
		else if (jstream_str_eq(&key, "impactforce")) out->ss.base.impactforce = jread_float(s, 0);
		else if (jstream_str_eq(&key, "duration60"))  out->ss.base.duration60  = (u8)jread_int(s, 0);
		else if (jstream_str_eq(&key, "shootsound"))  out->ss.base.shootsound  = (u16)jread_enum_or_int(s, JREF_SFX, 0, "weaponfunc.shootsound");
		else if (jstream_str_eq(&key, "penetration")) out->ss.base.penetration = (u8)jread_int(s, 0);
		/* shootauto extension */
		else if (jstream_str_eq(&key, "initialrpm"))  out->sa.initialrpm = jread_float(s, 0);
		else if (jstream_str_eq(&key, "maxrpm"))      out->sa.maxrpm     = jread_float(s, 0);
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
				if (s_VibrationsUsed > reserved) {
					if (is_max) out->sa.vibrationmax = arr;
					else        out->sa.vibrationstart = arr;
				}
			} else {
				jstream_skip_value(s);
			}
		}
		else if (jstream_str_eq(&key, "turretaccel")) out->sa.turretaccel = (s8)jread_int(s, 0);
		else if (jstream_str_eq(&key, "turretdecel")) out->sa.turretdecel = (s8)jread_int(s, 0);
		/* shootprojectile extension */
		else if (jstream_str_eq(&key, "projectilemodelnum")) {
			s32 v = jread_enum_or_int(s, JREF_FILE, 0, "weaponfunc.projectilemodelnum");
			out->sp.projectilemodelnum = v;
			out->tw.projectilemodelnum = v;
		}
		else if (jstream_str_eq(&key, "unk44") && struct_name[0]) {
			if (strcmp(struct_name, "weaponfunc_shootprojectile") == 0) out->sp.unk44 = (u32)jread_int(s, 0);
			else if (strcmp(struct_name, "weaponfunc_melee") == 0) out->me.unk44 = jread_float(s, 0);
			else jstream_skip_value(s);
		}
		else if (jstream_str_eq(&key, "scale"))         out->sp.scale       = jread_float(s, 0);
		else if (jstream_str_eq(&key, "speed") && struct_name[0]) {
			if (strcmp(struct_name, "weaponfunc_shootprojectile") == 0) out->sp.speed = jread_int(s, 0);
			else jstream_skip_value(s);
		}
		else if (jstream_str_eq(&key, "unk50"))         out->sp.unk50       = jread_float(s, 0);
		else if (jstream_str_eq(&key, "traveldist"))    out->sp.traveldist  = jread_int(s, 0);
		else if (jstream_str_eq(&key, "timer60"))       out->sp.timer60     = jread_int(s, 0);
		else if (jstream_str_eq(&key, "reflectangle"))  out->sp.reflectangle = jread_float(s, 0);
		else if (jstream_str_eq(&key, "soundnum"))      out->sp.soundnum    = (s16)jread_enum_or_int(s, JREF_SFX, 0, "weaponfunc.soundnum");
		/* throw extension */
		else if (jstream_str_eq(&key, "activatetime60")) out->tw.activatetime60 = (s16)jread_int(s, 0);
		/* melee extension */
		else if (jstream_str_eq(&key, "range"))         out->me.range  = jread_float(s, 0);
		else if (jstream_str_eq(&key, "unk1c"))         out->me.unk1c  = (u32)jread_int(s, 0);
		else if (jstream_str_eq(&key, "unk20"))         out->me.unk20  = (u32)jread_int(s, 0);
		else if (jstream_str_eq(&key, "unk28"))         out->me.unk28  = jread_float(s, 0);
		else if (jstream_str_eq(&key, "unk2c"))         out->me.unk2c  = jread_float(s, 0);
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
			sysLogPrintf(LOG_WARNING, "LOADER.PDBASE.WEAPON.POOL_FULL: gunviscmds");
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
			sysLogPrintf(LOG_WARNING, "LOADER.PDBASE.WEAPON.POOL_FULL: partvis");
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
	memset(&w, 0, sizeof(w));
	memset(&bp, 0, sizeof(bp));
	w.aimsettings = &s_DefaultAim;  /* default fallback */

	while (s->cur.kind != JT_RBRACE && s->cur.kind != JT_EOF) {
		if (s->cur.kind != JT_STRING) { jstream_advance(s); continue; }
		jtok_t key = s->cur;
		jstream_advance(s);
		if (s->cur.kind != JT_COLON) continue;
		jstream_advance(s);

		if      (jstream_str_eq(&key, "id")) jstream_skip_value(s);
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

	if (weapon_id >= 0 && weapon_id < CATALOG_MGR_WEAPON_COUNT) {
		s_Weapons[weapon_id] = w;
		if (bp_present) s_BotPrefs[weapon_id] = bp;
		s_WeaponsRegistered++;
	} else {
		sysLogPrintf(LOG_WARNING,
			"LOADER.PDBASE.WEAPON.RESOLVE_FAIL: weapon_id=%d out of range",
			weapon_id);
	}
}

/* ------------------------------------------------------------------ */
/* Top-level parser                                                   */
/* ------------------------------------------------------------------ */

static void parseTopLevel(jstream_t *s)
{
	if (s->cur.kind != JT_LBRACE) {
		s->error = 1;
		return;
	}
	jstream_advance(s);
	while (s->cur.kind != JT_RBRACE && s->cur.kind != JT_EOF) {
		if (s->cur.kind != JT_STRING) { jstream_advance(s); continue; }
		jtok_t key = s->cur;
		jstream_advance(s);
		if (s->cur.kind != JT_COLON) continue;
		jstream_advance(s);
		if (jstream_str_eq(&key, "animations")) {
			if (s->cur.kind != JT_LBRACK) { jstream_skip_value(s); }
			else {
				jstream_advance(s);
				while (s->cur.kind != JT_RBRACK && s->cur.kind != JT_EOF) {
					parseAnimation(s);
					if (s->cur.kind == JT_COMMA) jstream_advance(s);
				}
				if (s->cur.kind == JT_RBRACK) jstream_advance(s);
			}
		}
		else if (jstream_str_eq(&key, "weapons")) {
			if (s->cur.kind != JT_LBRACK) { jstream_skip_value(s); }
			else {
				jstream_advance(s);
				while (s->cur.kind != JT_RBRACK && s->cur.kind != JT_EOF) {
					parseWeapon(s);
					if (s->cur.kind == JT_COMMA) jstream_advance(s);
				}
				if (s->cur.kind == JT_RBRACK) jstream_advance(s);
			}
		}
		else {
			jstream_skip_value(s);
		}
		if (s->cur.kind == JT_COMMA) jstream_advance(s);
	}
}

/* ------------------------------------------------------------------ */
/* Public scan + build                                                */
/* ------------------------------------------------------------------ */

void loaderPdbaseScan(const char *dir, loader_pdbase_result_t *out)
{
	loader_pdbase_result_t local = {0};
	if (out != NULL) memset(out, 0, sizeof(*out));

	if (dir == NULL || dir[0] == '\0') {
		sysLogPrintf(LOG_NOTE,
			"LOADER.PDBASE.WEAPON.OK: scan skipped (no dir specified)");
		if (out) *out = local;
		return;
	}

	/* For the proving domain we know the single archive name. F13+ will
	 * generalise to walking the dir for all *.pdbase files. */
	char path[512];
	snprintf(path, sizeof(path), "%s/weapons.pdbase", dir);

	char *src = NULL;
	s32 size = 0;
	src = (char *)fsFileLoad(path, (u32 *)&size);
	if (src == NULL) {
		sysLogPrintf(LOG_NOTE,
			"LOADER.PDBASE.WEAPON.OK: dir=%s no archives found", dir);
		if (out) *out = local;
		return;
	}

	jstream_t st;
	memset(&st, 0, sizeof(st));
	st.src = src;
	st.pos = src;
	st.end = src + size;
	st.line = 1;
	jstream_advance(&st);
	parseTopLevel(&st);

	if (st.error) {
		sysLogPrintf(LOG_WARNING,
			"LOADER.PDBASE.WEAPON.SCAN_FAIL: parse error in %s near line %d",
			path, st.line);
		local.scan_failures++;
	}

	local.archives_scanned = 1;
	local.weapons_registered = s_WeaponsRegistered;

	sysLogPrintf(LOG_NOTE,
		"LOADER.PDBASE.WEAPON.OK: dir=%s archives=%d weapons=%d animations=%d "
		"funcs=%d ammos=%d aim=%d noise=%d recoil=%d guncmds=%d gunvis=%d "
		"partvis=%d vibrations=%d",
		dir, local.archives_scanned, local.weapons_registered,
		s_AnimationsUsed, s_WeaponFuncsUsed, s_AmmosUsed, s_AimSettingsUsed,
		s_NoiseSettingsUsed, s_RecoilSettingsUsed, s_GuncmdsUsed,
		s_GunviscmdsUsed, s_PartvisUsed, s_VibrationsUsed);

	/* fsFileLoad returns a buffer we can free. */
	sysMemFree(src);

	if (out) *out = local;
}

s32 loaderPdbaseBuildWeaponManager(void)
{
	if (s_WeaponsRegistered <= 0) {
		sysLogPrintf(LOG_NOTE,
			"LOADER.PDBASE.WEAPON.OK: build skipped (no records loaded)");
		return 0;
	}

	/* S484 F13: defaults are now hardcoded sentinels matching the
	 * pre-retirement values. invaimsettings_default sourced from
	 * src/game/invitems.c:90 (the historical struct literal); the
	 * silent noise settings are zeroed (no audible noise). Keeping
	 * them here keeps fallback semantics identical to the legacy
	 * externs the manager defaulted to before F13. */
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

	s_LoaderActive = 1;
	sysLogPrintf(LOG_NOTE,
		"LOADER.PDBASE.WEAPON.OK: manager active, weapons=%d (expected=%d)",
		s_WeaponsRegistered, CATALOG_MGR_WEAPON_COUNT);
	return s_WeaponsRegistered;
}

/* ------------------------------------------------------------------ */
/* Field-equivalence verifier (RETIRED at F13)                        */
/*                                                                    */
/* The parity check was a one-shot diagnostic that compared the       */
/* loader's pool-backed weapons against the legacy g_Weapons[] table  */
/* during the F12 parity period. It served its purpose (verified the  */
/* loader is correct on Mike's 2026-04-30 playtest -- "parity check   */
/* PASS (86 weapons)") and is removed in F13 because g_Weapons[] no   */
/* longer exists to compare against. Future regression coverage comes */
/* from behavioral tests + the existing structure-pin tests in        */
/* tests/test_loader_pdbase_scan.cpp.                                  */
/* ------------------------------------------------------------------ */



