/**
 * romextract_pdwpn.c -- Catalog universality pivot Step 1 (2026-05-02).
 *
 * Walks the loader_pool weapon pool and emits one .pdwpn JSON file
 * per registered weapon at data/<romid>/weapons/<id>.pdwpn.
 *
 * Schema lock-down: context/designs/catalog/universality-pivot-schemas.md
 * Section 2.1 (.pdwpn).
 *
 * Cross-reference convention for Step 1: hi_model / lo_model / animation
 * fields preserve the original FILE_* and ANIM_* enum strings (resolved
 * via reverse lookup against loader_enum_reverse.c). Step 4 (universal
 * loader) will swap these to catalog IDs once the directory walker is
 * minting the universal mapping. The Step 1 emit format is therefore
 * intermediate and identical to the per-record envelope content; this is
 * intentional so the parity check at Step 1 is clean.
 *
 * Server build: emitter early-returns 0 (loader not active server-side).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "data.h"
#include "types.h"
#include "constants.h"
#include "fs.h"
#include "catalog_mgr_weapons.h"
#include "loader_pool.h"
#include "loader_enum_reverse.h"
#include "romextract_pd.h"
#include "system.h"

/* Convert a catalog ID like "base:falcon2" to a filename slug
 * "base_falcon2". Caller buffer must hold at least 64 bytes. */
static void s_idToFilename(const char *id, char *out, size_t n)
{
	if (!id || !out || n == 0) { if (out && n) out[0] = '\0'; return; }
	size_t i;
	for (i = 0; i + 1 < n && id[i]; i++) {
		out[i] = (id[i] == ':') ? '_' : id[i];
	}
	out[i] = '\0';
}

/* ------------------------------------------------------------------ */
/* JSON writer helpers (small, self-contained)                         */
/* ------------------------------------------------------------------ */

typedef struct {
	FILE *fp;
	s32   indent;
	s32   error;
} jw_t;

static void jw_indent(jw_t *w)
{
	for (s32 i = 0; i < w->indent; i++) fputs("  ", w->fp);
}

static void jw_write_str_escaped(jw_t *w, const char *s)
{
	fputc('"', w->fp);
	if (s) {
		for (; *s; s++) {
			unsigned char c = (unsigned char)*s;
			if      (c == '"')  fputs("\\\"", w->fp);
			else if (c == '\\') fputs("\\\\", w->fp);
			else if (c == '\n') fputs("\\n", w->fp);
			else if (c == '\r') fputs("\\r", w->fp);
			else if (c == '\t') fputs("\\t", w->fp);
			else if (c < 0x20)  fprintf(w->fp, "\\u%04x", c);
			else                fputc(c, w->fp);
		}
	}
	fputc('"', w->fp);
}

static void jw_field_str(jw_t *w, const char *key, const char *val,
                          s32 last)
{
	jw_indent(w);
	fprintf(w->fp, "\"%s\": ", key);
	if (val) jw_write_str_escaped(w, val);
	else     fputs("null", w->fp);
	fputs(last ? "\n" : ",\n", w->fp);
}

static void jw_field_int(jw_t *w, const char *key, long long val, s32 last)
{
	jw_indent(w);
	fprintf(w->fp, "\"%s\": %lld%s", key, val, last ? "\n" : ",\n");
}

static void jw_field_uint(jw_t *w, const char *key, unsigned long long val,
                           s32 last)
{
	jw_indent(w);
	fprintf(w->fp, "\"%s\": %llu%s", key, val, last ? "\n" : ",\n");
}

static void jw_field_f32(jw_t *w, const char *key, f32 val, s32 last)
{
	jw_indent(w);
	fprintf(w->fp, "\"%s\": %.7g%s", key, (double)val, last ? "\n" : ",\n");
}

/* Emit `"key": "<NAME>"` if the integer resolves to a FILE_* name,
 * else `"key": <int>`. Used for hi_model / lo_model. */
static void jw_field_file_or_int(jw_t *w, const char *key, s32 val, s32 last)
{
	const char *name = loaderEnumNameForFileEnum(val);
	if (name) jw_field_str(w, key, name, last);
	else      jw_field_int(w, key, val, last);
}

/* Emit `"key": "<NAME>"` if the integer resolves to a L_GUN_* name,
 * else `"key": <int>`. Used for shortname / name / manufacturer / description. */
static void jw_field_lang_or_int(jw_t *w, const char *key, s32 val, s32 last)
{
	const char *name = loaderEnumNameForLangEnum(val);
	if (name) jw_field_str(w, key, name, last);
	else      jw_field_int(w, key, val, last);
}

/* Resolve a guncmd* anim pointer to its "invanim_*" name, emit string or null. */
static void jw_field_anim_ref(jw_t *w, const char *key,
                               const struct guncmd *cmds, s32 last)
{
	const char *name = loaderPoolAnimationNameForCmds(cmds);
	jw_field_str(w, key, name, last);
}

static void jw_open_object(jw_t *w, const char *key)
{
	jw_indent(w);
	if (key) fprintf(w->fp, "\"%s\": {\n", key);
	else     fputs("{\n", w->fp);
	w->indent++;
}

static void jw_close_object(jw_t *w, s32 last)
{
	w->indent--;
	jw_indent(w);
	fputs(last ? "}\n" : "},\n", w->fp);
}

static void jw_open_array(jw_t *w, const char *key)
{
	jw_indent(w);
	fprintf(w->fp, "\"%s\": [\n", key);
	w->indent++;
}

static void jw_close_array(jw_t *w, s32 last)
{
	w->indent--;
	jw_indent(w);
	fputs(last ? "]\n" : "],\n", w->fp);
}

/* ------------------------------------------------------------------ */
/* Sub-record emitters                                                 */
/* ------------------------------------------------------------------ */

static void s_emitNoiseSettings(jw_t *w, const struct noisesettings *n,
                                 const char *key, s32 last)
{
	if (!n) {
		jw_field_str(w, key, NULL, last);
		return;
	}
	jw_open_object(w, key);
	jw_field_f32(w, "minradius",    n->minradius,    0);
	jw_field_f32(w, "maxradius",    n->maxradius,    0);
	jw_field_f32(w, "incradius",    n->incradius,    0);
	jw_field_f32(w, "decbasespeed", n->decbasespeed, 0);
	jw_field_f32(w, "decremspeed",  n->decremspeed,  1);
	jw_close_object(w, last);
}

static void s_emitRecoilSettings(jw_t *w, const struct recoilsettings *r,
                                  const char *key, s32 last)
{
	if (!r) {
		jw_field_str(w, key, NULL, last);
		return;
	}
	jw_open_object(w, key);
	jw_field_f32(w, "xrange", r->xrange, 0);
	jw_field_f32(w, "yrange", r->yrange, 0);
	jw_field_f32(w, "zrange", r->zrange, 0);
	jw_field_f32(w, "unk0c",  r->unk0c,  0);
	jw_field_uint(w, "unk10", (unsigned)r->unk10, 1);
	jw_close_object(w, last);
}

static void s_emitAimSettings(jw_t *w, const struct invaimsettings *a,
                               const char *key, s32 last)
{
	if (!a) {
		jw_field_str(w, key, NULL, last);
		return;
	}
	jw_open_object(w, key);
	jw_field_f32(w, "zoomfov",      a->zoomfov,      0);
	jw_field_f32(w, "guntransup",   a->guntransup,   0);
	jw_field_f32(w, "guntransdown", a->guntransdown, 0);
	jw_field_f32(w, "guntransside", a->guntransside, 0);
	jw_field_f32(w, "aimdamppal",   a->aimdamppal,   0);
	jw_field_f32(w, "aimdamp",      a->aimdamp,      0);
	jw_field_uint(w, "tracktype", a->tracktype, 0);
	jw_field_uint(w, "unk18_04",  a->unk18_04,  0);
	jw_field_uint(w, "flags",     a->flags,     1);
	jw_close_object(w, last);
}

/* Common weaponfunc base fields (always present). Last argument controls
 * whether trailing comma is suppressed; this function always emits with
 * a trailing comma since the caller appends per-type fields after. */
static void s_emitWeaponFuncBase(jw_t *w, const struct weaponfunc *f)
{
	jw_field_uint(w, "type",      f->type, 0);
	jw_field_lang_or_int(w, "name", f->name, 0);
	jw_field_uint(w, "unk06",     f->unk06, 0);
	jw_field_int(w, "ammoindex",  f->ammoindex, 0);
	s_emitNoiseSettings(w, f->noisesettings, "noisesettings", 0);
	jw_field_anim_ref(w, "fire_animation", f->fire_animation, 0);
	jw_field_uint(w, "flags", f->flags, 0);
}

/* Emit one weaponfunc element. f may be NULL (emits null). The struct
 * type is taken from f->type (matches struct weaponfunc::type). */
static void s_emitWeaponFunc(jw_t *w, const void *func_ptr, s32 last)
{
	if (func_ptr == NULL) {
		jw_indent(w);
		fputs(last ? "null\n" : "null,\n", w->fp);
		return;
	}
	const struct weaponfunc *f = (const struct weaponfunc *)func_ptr;
	jw_indent(w);
	fputs("{\n", w->fp);
	w->indent++;
	s_emitWeaponFuncBase(w, f);

	switch (f->type) {
	case INVENTORYFUNCTYPE_SHOOT_SINGLE: {
		const struct weaponfunc_shootsingle *ss =
			(const struct weaponfunc_shootsingle *)f;
		const struct weaponfunc_shoot *sh = &ss->base;
		s_emitRecoilSettings(w, sh->recoilsettings, "recoilsettings", 0);
		jw_field_int(w, "recoverytime60", sh->recoverytime60, 0);
		jw_field_f32(w, "damage", sh->damage, 0);
		jw_field_f32(w, "spread", sh->spread, 0);
		jw_field_int(w, "unk24", sh->unk24, 0);
		jw_field_int(w, "unk25", sh->unk25, 0);
		jw_field_int(w, "unk26", sh->unk26, 0);
		jw_field_int(w, "unk27", sh->unk27, 0);
		jw_field_f32(w, "recoildist",  sh->recoildist, 0);
		jw_field_f32(w, "recoilangle", sh->recoilangle, 0);
		jw_field_f32(w, "slidemax",    sh->slidemax, 0);
		jw_field_f32(w, "impactforce", sh->impactforce, 0);
		jw_field_uint(w, "duration60", sh->duration60, 0);
		/* Q-2 type-tolerance: shootsound accepts any audio kind.
		 * Step 1 emits SFX_* enum string (matches the source data). */
		const char *sfx = loaderEnumNameForSfxEnum(sh->shootsound);
		if (sfx) jw_field_str(w, "shootsound", sfx, 0);
		else     jw_field_int(w, "shootsound", sh->shootsound, 0);
		jw_field_uint(w, "penetration", sh->penetration, 1);
		break;
	}
	case INVENTORYFUNCTYPE_SHOOT_AUTOMATIC: {
		const struct weaponfunc_shootauto *sa =
			(const struct weaponfunc_shootauto *)f;
		const struct weaponfunc_shoot *sh = &sa->base;
		s_emitRecoilSettings(w, sh->recoilsettings, "recoilsettings", 0);
		jw_field_int(w, "recoverytime60", sh->recoverytime60, 0);
		jw_field_f32(w, "damage", sh->damage, 0);
		jw_field_f32(w, "spread", sh->spread, 0);
		jw_field_int(w, "unk24", sh->unk24, 0);
		jw_field_int(w, "unk25", sh->unk25, 0);
		jw_field_int(w, "unk26", sh->unk26, 0);
		jw_field_int(w, "unk27", sh->unk27, 0);
		jw_field_f32(w, "recoildist",  sh->recoildist, 0);
		jw_field_f32(w, "recoilangle", sh->recoilangle, 0);
		jw_field_f32(w, "slidemax",    sh->slidemax, 0);
		jw_field_f32(w, "impactforce", sh->impactforce, 0);
		jw_field_uint(w, "duration60", sh->duration60, 0);
		const char *sfx = loaderEnumNameForSfxEnum(sh->shootsound);
		if (sfx) jw_field_str(w, "shootsound", sfx, 0);
		else     jw_field_int(w, "shootsound", sh->shootsound, 0);
		jw_field_uint(w, "penetration", sh->penetration, 0);
		jw_field_f32(w, "initialrpm", sa->initialrpm, 0);
		jw_field_f32(w, "maxrpm",     sa->maxrpm, 0);
		jw_field_int(w, "turretaccel", sa->turretaccel, 0);
		jw_field_int(w, "turretdecel", sa->turretdecel, 1);
		break;
	}
	case INVENTORYFUNCTYPE_SHOOT_PROJECTILE: {
		const struct weaponfunc_shootprojectile *sp =
			(const struct weaponfunc_shootprojectile *)f;
		const struct weaponfunc_shoot *sh = &sp->base;
		s_emitRecoilSettings(w, sh->recoilsettings, "recoilsettings", 0);
		jw_field_int(w, "recoverytime60", sh->recoverytime60, 0);
		jw_field_f32(w, "damage", sh->damage, 0);
		jw_field_f32(w, "spread", sh->spread, 0);
		jw_field_f32(w, "recoildist",  sh->recoildist, 0);
		jw_field_f32(w, "recoilangle", sh->recoilangle, 0);
		jw_field_f32(w, "slidemax",    sh->slidemax, 0);
		jw_field_f32(w, "impactforce", sh->impactforce, 0);
		jw_field_uint(w, "duration60", sh->duration60, 0);
		const char *sfx = loaderEnumNameForSfxEnum(sh->shootsound);
		if (sfx) jw_field_str(w, "shootsound", sfx, 0);
		else     jw_field_int(w, "shootsound", sh->shootsound, 0);
		jw_field_uint(w, "penetration", sh->penetration, 0);
		jw_field_int(w, "projectilemodelnum", sp->projectilemodelnum, 0);
		jw_field_f32(w, "scale", sp->scale, 0);
		jw_field_int(w, "speed", sp->speed, 0);
		jw_field_int(w, "traveldist", sp->traveldist, 0);
		jw_field_int(w, "timer60",    sp->timer60, 0);
		jw_field_f32(w, "reflectangle", sp->reflectangle, 0);
		jw_field_int(w, "soundnum", sp->soundnum, 1);
		break;
	}
	case INVENTORYFUNCTYPE_THROW: {
		const struct weaponfunc_throw *tw =
			(const struct weaponfunc_throw *)f;
		jw_field_int(w, "projectilemodelnum", tw->projectilemodelnum, 0);
		jw_field_int(w, "activatetime60",    tw->activatetime60, 0);
		jw_field_int(w, "recoverytime60",    tw->recoverytime60, 0);
		jw_field_f32(w, "damage", tw->damage, 1);
		break;
	}
	case INVENTORYFUNCTYPE_MELEE: {
		const struct weaponfunc_melee *me =
			(const struct weaponfunc_melee *)f;
		jw_field_f32(w, "damage", me->damage, 0);
		jw_field_f32(w, "range",  me->range, 1);
		break;
	}
	case INVENTORYFUNCTYPE_SPECIAL: {
		const struct weaponfunc_special *sx =
			(const struct weaponfunc_special *)f;
		jw_field_int(w, "specialfunc", sx->specialfunc, 0);
		jw_field_int(w, "recoverytime60", sx->recoverytime60, 0);
		jw_field_int(w, "soundnum", sx->soundnum, 1);
		break;
	}
	case INVENTORYFUNCTYPE_DEVICE: {
		const struct weaponfunc_device *dv =
			(const struct weaponfunc_device *)f;
		jw_field_uint(w, "device", dv->device, 1);
		break;
	}
	default: {
		/* Unknown subtype. Base fields already emitted above; emit a
		 * sentinel marker so schema parsers can skip cleanly. */
		jw_field_uint(w, "_no_subtype_fields", 0, 1);
		break;
	}
	}

	w->indent--;
	jw_indent(w);
	fputs(last ? "}\n" : "},\n", w->fp);
}

static void s_emitAmmo(jw_t *w, const struct inventory_ammo *a, s32 last)
{
	if (a == NULL) {
		jw_indent(w);
		fputs(last ? "null\n" : "null,\n", w->fp);
		return;
	}
	jw_indent(w);
	fputs("{\n", w->fp);
	w->indent++;
	jw_field_uint(w, "type",        a->type, 0);
	jw_field_uint(w, "casingeject", a->casingeject, 0);
	jw_field_int(w, "clipsize",    a->clipsize, 0);
	jw_field_anim_ref(w, "reload_animation", a->reload_animation, 0);
	jw_field_uint(w, "flags", a->flags, 1);
	w->indent--;
	jw_indent(w);
	fputs(last ? "}\n" : "},\n", w->fp);
}

static void s_emitGunviscmds(jw_t *w, const struct gunviscmd *cmds, s32 last)
{
	jw_open_array(w, "gunviscmds");
	if (cmds) {
		for (s32 i = 0; ; i++) {
			const struct gunviscmd *c = &cmds[i];
			s32 is_terminator = (c->type == 0);
			jw_indent(w);
			fprintf(w->fp, "[%u, %u, %u, %u, %u]%s\n",
				(unsigned)c->type, (unsigned)c->param,
				(unsigned)c->op, (unsigned)c->partnum,
				(unsigned)c->unk08,
				is_terminator ? "" : ",");
			if (is_terminator) break;
			if (i > 256) break;  /* safety bound */
		}
	}
	jw_close_array(w, last);
}

static void s_emitPartvisibility(jw_t *w,
                                  const struct modelpartvisibility *parts,
                                  s32 last)
{
	jw_open_array(w, "partvisibility");
	if (parts) {
		for (s32 i = 0; ; i++) {
			const struct modelpartvisibility *p = &parts[i];
			s32 is_terminator = (p->part == 0xff);
			jw_indent(w);
			if (is_terminator) {
				fputs("[255]\n", w->fp);
				break;
			}
			fprintf(w->fp, "[%u, %u],\n",
				(unsigned)p->part, (unsigned)p->visible);
			if (i > 256) break;  /* safety bound */
		}
	}
	jw_close_array(w, last);
}

static void s_emitBotPref(jw_t *w, const struct aibotweaponpreference *bp,
                           s32 last)
{
	if (!bp) {
		jw_field_str(w, "bot_pref", NULL, last);
		return;
	}
	jw_open_object(w, "bot_pref");
	jw_field_uint(w, "unk00", bp->unk00, 0);
	jw_field_uint(w, "unk01", bp->unk01, 0);
	jw_field_uint(w, "unk02", bp->unk02, 0);
	jw_field_uint(w, "unk03", bp->unk03, 0);
	jw_field_uint(w, "haspriammogoal", bp->haspriammogoal, 0);
	jw_field_uint(w, "hassecammogoal", bp->hassecammogoal, 0);
	jw_field_uint(w, "pridistconfig",  bp->pridistconfig, 0);
	jw_field_uint(w, "secdistconfig",  bp->secdistconfig, 1);
	jw_close_object(w, last);
}

/* ------------------------------------------------------------------ */
/* Per-weapon emitter                                                  */
/* ------------------------------------------------------------------ */

static s32 s_emitOneWeapon(s32 weapon_id, const struct weapon *wpn,
                            const struct aibotweaponpreference *bp,
                            const char *catalog_id, const char *out_dir,
                            s32 force_rewrite)
{
	char filename[128];
	s_idToFilename(catalog_id, filename, sizeof(filename));

	char relpath[FS_MAXPATH];
	snprintf(relpath, sizeof(relpath), "%s/%s.pdwpn", out_dir, filename);

	if (!force_rewrite) {
		s32 sz = fsFileSize(relpath);
		if (sz > 0) return 0;  /* already exists, skip */
	}

	FILE *fp = fsFileOpenWrite(relpath);
	if (!fp) {
		sysLoudFailf("EXTRACT.PDWPN",
			"fsFileOpenWrite failed for \"%s\"", relpath);
		return -1;
	}

	jw_t w;
	w.fp = fp;
	w.indent = 0;
	w.error = 0;

	fputs("{\n", fp);
	w.indent = 1;

	jw_field_str(&w, "pd_kind", "weapon", 0);
	jw_field_int(&w, "pd_schema_version", 1, 0);
	jw_field_str(&w, "id", catalog_id, 0);
	jw_field_int(&w, "weapon_id", weapon_id, 0);

	jw_field_file_or_int(&w, "hi_model", wpn->hi_model, 0);
	jw_field_file_or_int(&w, "lo_model", wpn->lo_model, 0);

	jw_field_anim_ref(&w, "equip_animation",   wpn->equip_animation, 0);
	jw_field_anim_ref(&w, "unequip_animation", wpn->unequip_animation, 0);
	jw_field_anim_ref(&w, "pritosec_animation", wpn->pritosec_animation, 0);
	jw_field_anim_ref(&w, "sectopri_animation", wpn->sectopri_animation, 0);

	jw_open_array(&w, "functions");
	s_emitWeaponFunc(&w, wpn->functions[0], 0);
	s_emitWeaponFunc(&w, wpn->functions[1], 1);
	jw_close_array(&w, 0);

	jw_open_array(&w, "ammos");
	s_emitAmmo(&w, wpn->ammos[0], 0);
	s_emitAmmo(&w, wpn->ammos[1], 1);
	jw_close_array(&w, 0);

	s_emitAimSettings(&w, wpn->aimsettings, "aimsettings", 0);

	jw_field_f32(&w, "muzzlez", wpn->muzzlez, 0);
	jw_field_f32(&w, "posx",    wpn->posx, 0);
	jw_field_f32(&w, "posy",    wpn->posy, 0);
	jw_field_f32(&w, "posz",    wpn->posz, 0);
	jw_field_f32(&w, "sway",    wpn->sway, 0);

	s_emitGunviscmds(&w, wpn->gunviscmds, 0);
	s_emitPartvisibility(&w, wpn->partvisibility, 0);

	jw_field_lang_or_int(&w, "shortname",    wpn->shortname, 0);
	jw_field_lang_or_int(&w, "name",         wpn->name, 0);
	jw_field_lang_or_int(&w, "manufacturer", wpn->manufacturer, 0);
	jw_field_lang_or_int(&w, "description",  wpn->description, 0);
	jw_field_uint(&w, "flags", wpn->flags, 0);

	s_emitBotPref(&w, bp, 1);

	w.indent = 0;
	fputs("}\n", fp);
	fclose(fp);
	return 1;
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                  */
/* ------------------------------------------------------------------ */

s32 romExtractAllPdwpn(s32 force_rewrite)
{
	if (!loaderPoolIsActive()) {
		sysLogPrintf(LOG_NOTE,
			"romextract pdwpn: loader not active, skipping");
		return 0;
	}

	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDWPN",
			"fsDataDirEnsure failed; cannot create output dir");
		return -1;
	}

	/* Ensure data/<romid>/weapons/ exists. */
	char weapons_dir[FS_MAXPATH];
	snprintf(weapons_dir, sizeof(weapons_dir), "%s/weapons", fsDataDir());
	if (!fsCreateDir(weapons_dir)) {
		sysLoudFailf("EXTRACT.PDWPN",
			"fsCreateDir(\"%s\") failed", weapons_dir);
		return -1;
	}

	s32 written = 0;
	s32 skipped = 0;
	s32 failed = 0;
	s32 total = loaderPoolGetWeaponsRegistered();

	for (s32 i = 0; i < CATALOG_MGR_WEAPON_COUNT; i++) {
		const struct weapon *wpn = loaderPoolGetWeapon(i);
		if (!wpn) continue;

		const char *catalog_id = loaderPoolGetWeaponCatalogId(i);
		if (!catalog_id) {
			/* Pool slot is populated but no catalog ID was captured.
			 * Fall back to a synthetic ID so emit still produces a
			 * file; future investigation can decide whether the slot
			 * is canonical content or pool padding. */
			static char synth[64];
			snprintf(synth, sizeof(synth), "base:weapon_%03d", i);
			catalog_id = synth;
		}

		const struct aibotweaponpreference *bp = loaderPoolGetBotPref(i);
		s32 r = s_emitOneWeapon(i, wpn, bp, catalog_id,
		                        weapons_dir, force_rewrite);
		if (r > 0)      written++;
		else if (r == 0) skipped++;
		else              failed++;
	}

	sysLogPrintf(LOG_NOTE,
		"romextract pdwpn: written=%d skipped=%d failed=%d total=%d",
		written, skipped, failed, total);

	return written;
}
