/**
 * romextract_pdweapon.c -- Catalog Universality BYOR Completion (2026-05-03),
 * weapon graph asset cutover (2026-05-21).
 *
 * Walks g_WeaponData[] from port/src/weapondata_authored.c and emits
 * one .pdweapon ZIP-openable archive per weapon at
 * data/<romid>/weapons/<id>.pdweapon.
 *
 * The archive root carries weapon.ini for editor-facing metadata,
 * manifest.json for the current universal walker/loader_pool bridge,
 * graph-shaped behavior.graph.json, and nested_payloads.json for generated
 * projectile/entity payload archives.
 *
 * Source: the historical weapon static records were retired from
 * src/game/invitems.c at S484 F13. They live in port/src/weapondata_authored.c
 * as the authored source-of-truth for the runtime emitter. The engine
 * never reads g_WeaponData[] -- it goes through the catalog after this
 * emitter writes the .pdweapon files and the walker registers them.
 *
 * Server build: emitter early-returns 0 (weapondata not linked server-side).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <SDL.h>
#include <PR/ultratypes.h>

#include "boot_pool.h"
#include "boot_progress.h"
#include "catalog_readable_ids.h"
#include "data.h"
#include "types.h"
#include "constants.h"
#include "fs.h"
#include "loader_enum_reverse.h"
#include "modarchive.h"
#include "romextract_pd.h"
#include "system.h"
#include "weapon_graph_archive.h"
#include "weapondata_authored.h"
#include "animdata_authored.h"

#define PDWEAPON_DEPENDENCY_CLOSURE_MARKER "embedded.v9"
#define PDWEAPON_FAST_CACHE_KIND "pdweapon_embedded_v9"
#define PDWEAPON_MAX_ANIM_DEPS 128
#define PDWEAPON_MAX_AUDIO_DEPS 128

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

static s32 s_existingArchiveHasEntry(const char *relpath, const char *entry)
{
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) return 0;
	mod_archive_t *arc = modArchiveOpen(full);
	if (!arc) return 0;
	s32 found = (modArchiveFindEntry(arc, entry) >= 0);
	modArchiveClose(arc);
	return found;
}

static s32 s_existingArchiveEntryContains(const char *relpath,
                                          const char *entry,
                                          const char *needle)
{
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0] || !needle) return 0;
	mod_archive_t *arc = modArchiveOpen(full);
	if (!arc) return 0;
	s32 idx = modArchiveFindEntry(arc, entry);
	if (idx < 0) {
		modArchiveClose(arc);
		return 0;
	}
	u32 size = 0;
	char *bytes = (char *)modArchiveExtractAlloc(arc, idx, &size);
	modArchiveClose(arc);
	if (!bytes) return 0;
	char *text = (char *)malloc((size_t)size + 1);
	if (!text) {
		free(bytes);
		return 0;
	}
	memcpy(text, bytes, size);
	text[size] = '\0';
	free(bytes);
	s32 found = strstr(text, needle) != NULL;
	free(text);
	return found;
}

static s32 s_existingWeaponArchiveGraphCurrent(const char *relpath)
{
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) return 0;

	char *graph = NULL;
	if (weaponGraphArchiveReadTextFile(full,
			WEAPON_GRAPH_ARCHIVE_GRAPH_ENTRY, &graph, NULL) != 0) {
		return 0;
	}
	s32 current =
		strstr(graph, "\"graph_id\": \"base_weapon_graph_v2\"") != NULL &&
		strstr(graph, "temporary.legacy_weapon_manifest") == NULL;
	free(graph);
	return current;
}

static s32 s_existingWeaponArchiveComplete(const char *relpath)
{
	return s_existingArchiveHasEntry(relpath, "weapon.ini") &&
	       s_existingArchiveHasEntry(relpath, "manifest.json") &&
	       s_existingArchiveHasEntry(relpath, "behavior.graph.json") &&
	       s_existingArchiveHasEntry(relpath, WEAPON_GRAPH_ARCHIVE_NESTED_PAYLOADS_ENTRY) &&
	       s_existingArchiveEntryContains(relpath, "weapon.ini",
		"dependency_closure = " PDWEAPON_DEPENDENCY_CLOSURE_MARKER) &&
	       s_existingWeaponArchiveGraphCurrent(relpath);
}

static void s_removeRelpathIfExists(const char *relpath)
{
	if (!relpath || fsFileSize(relpath) <= 0) return;
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (full && full[0]) {
		remove(full);
	}
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

static void jw_field_sfx_or_int(jw_t *w, const char *key, s32 val, s32 last)
{
	const char *name = loaderEnumNameForSfxEnum(val);
	if (name) jw_field_str(w, key, name, last);
	else      jw_field_int(w, key, val, last);
}

/* Resolve a guncmd* anim pointer to its "invanim_*" name via the
 * g_AnimData[] iteration table. Returns NULL when not found. */
static const char *s_animNameForCmds(const struct guncmd *cmds)
{
	if (!cmds) return NULL;
	for (s32 i = 0; i < g_AnimDataCount; i++) {
		if (g_AnimData[i].cmds == cmds) return g_AnimData[i].name;
	}
	return NULL;
}

/* Resolve a guncmd* anim pointer to its "invanim_*" name, emit string or null. */
static void jw_field_anim_ref(jw_t *w, const char *key,
                               const struct guncmd *cmds, s32 last)
{
	const char *name = s_animNameForCmds(cmds);
	jw_field_str(w, key, name, last);
}

typedef struct {
	const struct guncmd *cmds[PDWEAPON_MAX_ANIM_DEPS];
	const char *names[PDWEAPON_MAX_ANIM_DEPS];
	s32 count;
} pdweapon_anim_deps_t;

typedef struct {
	s32 sfx[PDWEAPON_MAX_AUDIO_DEPS];
	s32 count;
} pdweapon_audio_deps_t;

static s32 s_countGuncmdsBounded(const struct guncmd *cmds)
{
	s32 n = 0;
	if (!cmds) return 0;
	while (n < 4096) {
		n++;
		if (cmds[n - 1].type == GUNCMD_END) break;
	}
	return n;
}

static void s_addAudioDep(pdweapon_audio_deps_t *deps, s32 sfx)
{
	if (!deps || sfx <= 0) return;
	for (s32 i = 0; i < deps->count; i++) {
		if (deps->sfx[i] == sfx) return;
	}
	if (deps->count < PDWEAPON_MAX_AUDIO_DEPS) {
		deps->sfx[deps->count++] = sfx;
	}
}

static void s_collectAnimDepsFromCmds(pdweapon_anim_deps_t *anim_deps,
                                      pdweapon_audio_deps_t *audio_deps,
                                      const struct guncmd *cmds)
{
	if (!anim_deps || !cmds) return;
	for (s32 i = 0; i < anim_deps->count; i++) {
		if (anim_deps->cmds[i] == cmds) return;
	}

	const char *name = s_animNameForCmds(cmds);
	if (name && anim_deps->count < PDWEAPON_MAX_ANIM_DEPS) {
		anim_deps->cmds[anim_deps->count] = cmds;
		anim_deps->names[anim_deps->count] = name;
		anim_deps->count++;
	}

	s32 cmd_count = s_countGuncmdsBounded(cmds);
	for (s32 i = 0; i < cmd_count; i++) {
		const struct guncmd *cmd = &cmds[i];
		if (cmd->type == GUNCMD_PLAYSOUND) {
			s_addAudioDep(audio_deps, (s32)cmd->unk04);
		} else if (cmd->type == GUNCMD_INCLUDE ||
				cmd->type == GUNCMD_RANDOM) {
			s_collectAnimDepsFromCmds(anim_deps, audio_deps,
				(const struct guncmd *)(intptr_t)cmd->unk04);
		}
		if (cmd->type == GUNCMD_END) break;
	}
}

static void s_collectWeaponFuncDeps(const struct weaponfunc *f,
                                    pdweapon_anim_deps_t *anim_deps,
                                    pdweapon_audio_deps_t *audio_deps)
{
	if (!f) return;
	s_collectAnimDepsFromCmds(anim_deps, audio_deps, f->fire_animation);
	switch (f->type) {
	case INVENTORYFUNCTYPE_SHOOT_SINGLE:
	case INVENTORYFUNCTYPE_SHOOT_AUTOMATIC:
	case INVENTORYFUNCTYPE_SHOOT_PROJECTILE: {
		const struct weaponfunc_shoot *sh = (const struct weaponfunc_shoot *)f;
		s_addAudioDep(audio_deps, sh->shootsound);
		if (f->type == INVENTORYFUNCTYPE_SHOOT_PROJECTILE) {
			const struct weaponfunc_shootprojectile *sp =
				(const struct weaponfunc_shootprojectile *)f;
			s_addAudioDep(audio_deps, sp->soundnum);
		}
		break;
	}
	case INVENTORYFUNCTYPE_SPECIAL: {
		const struct weaponfunc_special *sx =
			(const struct weaponfunc_special *)f;
		s_addAudioDep(audio_deps, sx->soundnum);
		break;
	}
	default:
		break;
	}
}

static void s_collectWeaponDependencies(const struct weapon *wpn,
                                        pdweapon_anim_deps_t *anim_deps,
                                        pdweapon_audio_deps_t *audio_deps)
{
	memset(anim_deps, 0, sizeof(*anim_deps));
	memset(audio_deps, 0, sizeof(*audio_deps));
	if (!wpn) return;
	s_collectAnimDepsFromCmds(anim_deps, audio_deps, wpn->equip_animation);
	s_collectAnimDepsFromCmds(anim_deps, audio_deps, wpn->unequip_animation);
	s_collectAnimDepsFromCmds(anim_deps, audio_deps, wpn->pritosec_animation);
	s_collectAnimDepsFromCmds(anim_deps, audio_deps, wpn->sectopri_animation);
	for (s32 i = 0; i < 2; i++) {
		s_collectWeaponFuncDeps((const struct weaponfunc *)wpn->functions[i],
			anim_deps, audio_deps);
		if (wpn->ammos[i]) {
			s_collectAnimDepsFromCmds(anim_deps, audio_deps,
				wpn->ammos[i]->reload_animation);
		}
	}
}

static void s_synthMeshCatalogId(u16 filenum, const char *hint_suffix,
                                 char *out, size_t n)
{
	catalogReadableModelIdForFile(filenum, hint_suffix, "mesh", out, n);
}

static s32 s_meshRelForFilenum(u16 filenum, const char *hint,
                               char *out, size_t out_n)
{
	if (!out || out_n == 0) return 0;
	out[0] = '\0';
	if (filenum == 0) return 0;
	char catalog_id[128];
	char slug[128];
	s_synthMeshCatalogId(filenum, hint, catalog_id, sizeof(catalog_id));
	s_idToFilename(catalog_id, slug, sizeof(slug));
	char rel[FS_MAXPATH];
	snprintf(rel, sizeof(rel), "meshes/%s.pdmesh", slug);
	fsDataPathFor(rel, out, out_n);
	return out[0] ? 1 : 0;
}

static s32 s_meshRelForModelnum(s32 modelnum, char *out, size_t out_n)
{
	if (!out || out_n == 0) return 0;
	out[0] = '\0';
	if (modelnum < 0 || modelnum >= NUM_MODELS) return 0;
	u16 filenum = g_ModelStates[modelnum].fileid;
	return s_meshRelForFilenum(filenum, NULL, out, out_n);
}

static s32 s_addArchiveFileDiskRel(mod_archive_writer_t *aw,
                                   const char *entry,
                                   const char *src_rel,
                                   const char *context)
{
	if (!aw || !entry || !entry[0] || !src_rel || !src_rel[0]) return -1;
	if (fsFileSize(src_rel) <= 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdweapon: missing dependency %s for %s (rel=\"%s\")",
			entry, context ? context : "", src_rel);
		return -1;
	}
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(src_rel, full_buf, sizeof(full_buf));
	if (!full || !full[0]) return -1;
	return modArchiveAddFileDisk(aw, entry, full) == 0 ? 0 : -1;
}

static s32 s_addWeaponMeshDependency(mod_archive_writer_t *aw,
                                     const char *catalog_id,
                                     u16 filenum,
                                     const char *hint,
                                     const char *entry)
{
	if (filenum == 0) return 0;
	char src_rel[FS_MAXPATH];
	if (!s_meshRelForFilenum(filenum, hint, src_rel, sizeof(src_rel))) return -1;
	return s_addArchiveFileDiskRel(aw, entry, src_rel, catalog_id);
}

static s32 s_addProjectileModelDependency(mod_archive_writer_t *aw,
                                          const char *catalog_id,
                                          s32 modelnum,
                                          const char *entry)
{
	char src_rel[FS_MAXPATH];
	if (!s_meshRelForModelnum(modelnum, src_rel, sizeof(src_rel))) return 0;
	return s_addArchiveFileDiskRel(aw, entry, src_rel, catalog_id);
}

static void s_sfxCatalogIdForWeapon(s32 sfx_idx, char *out, size_t out_n)
{
	catalogReadableSfxId(sfx_idx, out, out_n);
}

static s32 s_audioRelForSfx(s32 sfx_idx, char *out, size_t out_n,
                            char *out_ext, size_t ext_n)
{
	if (!out || out_n == 0) return 0;
	out[0] = '\0';
	if (out_ext && ext_n) out_ext[0] = '\0';
	if (sfx_idx <= 0) return 0;

	char catalog_id[128];
	char slug[128];
	char rel[FS_MAXPATH];
	s_sfxCatalogIdForWeapon(sfx_idx, catalog_id, sizeof(catalog_id));
	s_idToFilename(catalog_id, slug, sizeof(slug));
	snprintf(rel, sizeof(rel), "audio/sfx/%s.pdsfx", slug);
	fsDataPathFor(rel, out, out_n);
	if (fsFileSize(out) > 0) {
		if (out_ext && ext_n) snprintf(out_ext, ext_n, ".pdsfx");
		return 1;
	}

	catalogReadableVoiceId(sfx_idx, catalog_id, sizeof(catalog_id));
	s_idToFilename(catalog_id, slug, sizeof(slug));
	snprintf(rel, sizeof(rel), "audio/voice/%s.pdvoice", slug);
	fsDataPathFor(rel, out, out_n);
	if (fsFileSize(out) > 0) {
		if (out_ext && ext_n) snprintf(out_ext, ext_n, ".pdvoice");
		return 1;
	}

	out[0] = '\0';
	return 0;
}

static s32 s_addWeaponAnimationDependency(mod_archive_writer_t *aw,
                                          const char *catalog_id,
                                          const char *anim_name)
{
	if (!anim_name || !anim_name[0]) return 0;
	char rel[FS_MAXPATH];
	char src_rel[FS_MAXPATH];
	snprintf(rel, sizeof(rel), "animations/base_%s.pdanim", anim_name);
	fsDataPathFor(rel, src_rel, sizeof(src_rel));
	char entry[FS_MAXPATH];
	snprintf(entry, sizeof(entry), "animations/%s.pdanim", anim_name);
	return s_addArchiveFileDiskRel(aw, entry, src_rel, catalog_id);
}

static s32 s_addWeaponAudioDependency(mod_archive_writer_t *aw,
                                      const char *catalog_id,
                                      s32 sfx_idx)
{
	char src_rel[FS_MAXPATH];
	char ext[16];
	if (!s_audioRelForSfx(sfx_idx, src_rel, sizeof(src_rel), ext, sizeof(ext))) {
		return 0;
	}
	char id[128];
	char slug[128];
	s_sfxCatalogIdForWeapon(sfx_idx, id, sizeof(id));
	if (strcmp(ext, ".pdvoice") == 0) {
		catalogReadableVoiceId(sfx_idx, id, sizeof(id));
	}
	s_idToFilename(id, slug, sizeof(slug));
	char entry[FS_MAXPATH];
	snprintf(entry, sizeof(entry), "audio/%s%s", slug, ext);
	return s_addArchiveFileDiskRel(aw, entry, src_rel, catalog_id);
}

static s32 s_addDependencyManifests(mod_archive_writer_t *aw,
                                    const pdweapon_anim_deps_t *anim_deps,
                                    const pdweapon_audio_deps_t *audio_deps)
{
	char anim_manifest[8192];
	size_t len = 0;
	len += snprintf(anim_manifest + len, sizeof(anim_manifest) - len,
		"name\tarchive_entry\tcatalog_id\n");
	for (s32 i = 0; i < anim_deps->count && len < sizeof(anim_manifest); i++) {
		const char *name = anim_deps->names[i] ? anim_deps->names[i] : "";
		len += snprintf(anim_manifest + len, sizeof(anim_manifest) - len,
			"%s\tanimations/%s.pdanim\tbase:%s\n", name, name, name);
	}
	if (len >= sizeof(anim_manifest) ||
			modArchiveAddFileMem(aw, "animations_manifest.tsv",
				anim_manifest, (u32)strlen(anim_manifest)) != 0) {
		return -1;
	}

	char audio_manifest[8192];
	len = 0;
	len += snprintf(audio_manifest + len, sizeof(audio_manifest) - len,
		"sfx_index\tarchive_entry\n");
	for (s32 i = 0; i < audio_deps->count && len < sizeof(audio_manifest); i++) {
		char src_rel[FS_MAXPATH];
		char ext[16];
		if (!s_audioRelForSfx(audio_deps->sfx[i], src_rel, sizeof(src_rel),
				ext, sizeof(ext))) {
			continue;
		}
		char id[128];
		char slug[128];
		s_sfxCatalogIdForWeapon(audio_deps->sfx[i], id, sizeof(id));
		if (strcmp(ext, ".pdvoice") == 0) {
			catalogReadableVoiceId(audio_deps->sfx[i], id, sizeof(id));
		}
		s_idToFilename(id, slug, sizeof(slug));
		len += snprintf(audio_manifest + len, sizeof(audio_manifest) - len,
			"%d\taudio/%s%s\n", audio_deps->sfx[i], slug, ext);
	}
	if (len >= sizeof(audio_manifest) ||
			modArchiveAddFileMem(aw, "audio_manifest.tsv",
				audio_manifest, (u32)strlen(audio_manifest)) != 0) {
		return -1;
	}
	return 0;
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
/* Graph/nested payload emitters                                       */
/* ------------------------------------------------------------------ */

#define PDWEAPON_MAX_NESTED_PAYLOADS 16

typedef struct {
	asset_type_e type;
	char local_slug[WEAPON_GRAPH_ARCHIVE_LOCAL_SLUG_LEN];
	char catalog_id[CATALOG_ID_LEN];
	char archive_entry[FS_MAXPATH];
	char temp_relpath[FS_MAXPATH];
	char temp_fullpath[FS_MAXPATH + 1];
	char entity_ref[CATALOG_ID_LEN];
	const struct weaponfunc *func;
	s32 mode_index;
	s32 projectile_modelnum;
	const char *archetype;
} pdweapon_nested_payload_t;

typedef struct {
	pdweapon_nested_payload_t payloads[PDWEAPON_MAX_NESTED_PAYLOADS];
	weapon_graph_archive_inventory_t inventory;
	s32 count;
} pdweapon_payload_plan_t;

static const char *s_modeName(s32 mode)
{
	return mode == 0 ? "primary" : "secondary";
}

static const char *s_modelSlug(s32 modelnum)
{
	switch (modelnum) {
	case MODEL_CHRDYROCKETMIS:   return "dyrocket";
	case MODEL_CHRSKROCKETMIS:   return "skrocket";
	case MODEL_CHRCROSSBOLT:     return "crossbow_bolt";
	case MODEL_CHRDEVGRENADE:    return "devastator_grenade";
	case MODEL_CHRDRAGGRENADE:   return "dragon_grenade";
	case MODEL_CHRKNIFE:         return "knife";
	case MODEL_CHRBUG:           return "bug";
	case MODEL_TARGETAMP:        return "target_amplifier";
	case MODEL_CHRAUTOGUN:       return "autogun";
	case MODEL_CHRDRAGON:        return "dragon";
	case MODEL_CHRGRENADE:       return "grenade";
	case MODEL_CHRNBOMB:         return "nbomb";
	case MODEL_CHRTIMEDMINE:     return "timed_mine";
	case MODEL_CHRPROXIMITYMINE: return "proximity_mine";
	case MODEL_CHRREMOTEMINE:    return "remote_mine";
	case MODEL_CHRECMMINE:       return "ecm_mine";
	default:                     return NULL;
	}
}

static void s_modelRef(s32 modelnum, char *out, size_t cap)
{
	const char *slug = s_modelSlug(modelnum);
	if (slug) snprintf(out, cap, "MODEL_%s", slug);
	else      snprintf(out, cap, "MODEL_%04x", (unsigned)(modelnum & 0xffff));
	out[cap - 1] = '\0';
}

static const char *s_funcTypeName(s32 type)
{
	switch (type) {
	case INVENTORYFUNCTYPE_NONE:             return "none";
	case INVENTORYFUNCTYPE_SHOOT_SINGLE:     return "shoot_single";
	case INVENTORYFUNCTYPE_SHOOT_AUTOMATIC:  return "shoot_automatic";
	case INVENTORYFUNCTYPE_SHOOT_PROJECTILE: return "shoot_projectile";
	case INVENTORYFUNCTYPE_THROW:            return "throw";
	case INVENTORYFUNCTYPE_MELEE:            return "melee";
	case INVENTORYFUNCTYPE_SPECIAL:          return "special";
	case INVENTORYFUNCTYPE_DEVICE:           return "device";
	default:                                 return "unknown";
	}
}

static const char *s_deviceName(s32 device)
{
	switch (device) {
	case DEVICE_NIGHTVISION:  return "nightvision";
	case DEVICE_XRAYSCANNER:  return "xray_scanner";
	case DEVICE_EYESPY:       return "eyespy";
	case DEVICE_IRSCANNER:    return "ir_scanner";
	case DEVICE_RTRACKER:     return "rtracker";
	case DEVICE_SUICIDEPILL:  return "suicide_pill";
	case DEVICE_CLOAKDEVICE:  return "cloak_device";
	case DEVICE_CLOAKRCP120:  return "rcp120_cloak";
	default:                  return "unknown";
	}
}

static const char *s_specialName(s32 special)
{
	switch (special) {
	case HANDATTACKTYPE_DETONATE:    return "detonate";
	case HANDATTACKTYPE_BOOST:       return "boost";
	case HANDATTACKTYPE_REVERTBOOST: return "revert_boost";
	case HANDATTACKTYPE_CROUCH:      return "crouch";
	case HANDATTACKTYPE_RCP120CLOAK: return "rcp120_cloak";
	case HANDATTACKTYPE_UPLINK:      return "uplink";
	default:                         return "unknown";
	}
}

static s32 s_burstCountForFlags(u32 flags)
{
	if (flags & FUNCFLAG_BURST50) return 50;
	if (flags & FUNCFLAG_BURST5)  return 5;
	if (flags & FUNCFLAG_BURST3)  return 3;
	if (flags & FUNCFLAG_BURST2)  return 2;
	return 0;
}

static const char *s_weaponModuleKind(const char *catalog_id, s32 mode,
                                      const struct weaponfunc *f)
{
	if (!f) return "function.empty";
	switch (f->type) {
	case INVENTORYFUNCTYPE_NONE:
		return "function.empty";
	case INVENTORYFUNCTYPE_SHOOT_SINGLE:
		if (catalog_id && strcmp(catalog_id, "base:mauler") == 0 && mode == 1) {
			return "fire.charge_release";
		}
		if (s_burstCountForFlags(f->flags) > 0) return "fire.burst";
		return "fire.hitscan";
	case INVENTORYFUNCTYPE_SHOOT_AUTOMATIC:
		if ((catalog_id && strcmp(catalog_id, "base:laser") == 0 && mode == 1) ||
				(catalog_id && strcmp(catalog_id, "base:watchlaser") == 0)) {
			return "fire.beam_tick";
		}
		if (s_burstCountForFlags(f->flags) > 0) return "fire.burst";
		return "fire.auto_cadence";
	case INVENTORYFUNCTYPE_SHOOT_PROJECTILE:
		return "spawn.fired_projectile";
	case INVENTORYFUNCTYPE_THROW:
		return "spawn.thrown_physical";
	case INVENTORYFUNCTYPE_MELEE:
		return "melee.strike";
	case INVENTORYFUNCTYPE_SPECIAL: {
		const struct weaponfunc_special *sx = (const struct weaponfunc_special *)f;
		if (sx->specialfunc == HANDATTACKTYPE_DETONATE) return "special.remote_detonator";
		if (sx->specialfunc == HANDATTACKTYPE_BOOST ||
				sx->specialfunc == HANDATTACKTYPE_REVERTBOOST) {
			return "special.combat_boost";
		}
		return "special.weapon_state";
	}
	case INVENTORYFUNCTYPE_DEVICE:
		return "device.activate";
	default:
		return "function.unknown";
	}
}

static void s_projectileSlugForFunction(const char *catalog_id, s32 mode,
                                        const struct weaponfunc *f,
                                        char *out, size_t out_cap)
{
	const char *mode_name = s_modeName(mode);
	const u32 flags = f ? f->flags : 0;
	s32 modelnum = -1;
	if (f && f->type == INVENTORYFUNCTYPE_SHOOT_PROJECTILE) {
		const struct weaponfunc_shootprojectile *sp =
			(const struct weaponfunc_shootprojectile *)f;
		modelnum = sp->projectilemodelnum;
		if (flags & FUNCFLAG_FLYBYWIRE) {
			snprintf(out, out_cap, "flybywire_rocket");
		} else if (flags & FUNCFLAG_HOMINGROCKET) {
			snprintf(out, out_cap, "homing_rocket");
		} else if (flags & FUNCFLAG_STICKTOWALL) {
			snprintf(out, out_cap, "wallhugger_grenade_round");
		} else if (modelnum == MODEL_CHRCROSSBOLT && (flags & FUNCFLAG_MAKEDIZZY)) {
			snprintf(out, out_cap, "sedative_bolt");
		} else if (modelnum == MODEL_CHRCROSSBOLT) {
			snprintf(out, out_cap, "lethal_bolt");
		} else if (modelnum == MODEL_CHRSKROCKETMIS) {
			snprintf(out, out_cap, "powered_rocket");
		} else if (modelnum == MODEL_CHRDYROCKETMIS) {
			snprintf(out, out_cap, "rocket");
		} else if (modelnum == MODEL_CHRDEVGRENADE || modelnum == MODEL_CHRDRAGGRENADE) {
			snprintf(out, out_cap, "grenade_round");
		} else {
			const char *slug = s_modelSlug(modelnum);
			snprintf(out, out_cap, "%s_projectile", slug ? slug : mode_name);
		}
	} else if (f && f->type == INVENTORYFUNCTYPE_THROW) {
		const struct weaponfunc_throw *tw = (const struct weaponfunc_throw *)f;
		modelnum = tw->projectilemodelnum;
		if (catalog_id && strcmp(catalog_id, "base:laptopgun") == 0) {
			snprintf(out, out_cap, "thrown_laptop");
		} else if (catalog_id && strcmp(catalog_id, "base:dragon") == 0) {
			snprintf(out, out_cap, "thrown_dragon_proxy");
		} else if (catalog_id && strcmp(catalog_id, "base:grenade") == 0 && mode == 1) {
			snprintf(out, out_cap, "proxy_pinball_grenade");
		} else if (catalog_id && strcmp(catalog_id, "base:grenade") == 0) {
			snprintf(out, out_cap, "timed_grenade");
		} else if (catalog_id && strcmp(catalog_id, "base:nbomb") == 0 && mode == 1) {
			snprintf(out, out_cap, "proxy_nbomb");
		} else if (catalog_id && strcmp(catalog_id, "base:nbomb") == 0) {
			snprintf(out, out_cap, "timed_nbomb");
		} else if (catalog_id && strcmp(catalog_id, "base:timedmine") == 0) {
			snprintf(out, out_cap, "thrown_timed_mine");
		} else if (catalog_id && strcmp(catalog_id, "base:proximitymine") == 0) {
			snprintf(out, out_cap, "thrown_proximity_mine");
		} else if (catalog_id && strcmp(catalog_id, "base:remotemine") == 0) {
			snprintf(out, out_cap, "thrown_remote_mine");
		} else if (catalog_id && strcmp(catalog_id, "base:ecmmine") == 0) {
			snprintf(out, out_cap, "sticky_ecm_mine");
		} else if (catalog_id && strcmp(catalog_id, "base:combatknife") == 0) {
			snprintf(out, out_cap, "thrown_knife");
		} else if (catalog_id && strcmp(catalog_id, "base:commsrider") == 0) {
			snprintf(out, out_cap, "sticky_comms_device");
		} else if (catalog_id && strcmp(catalog_id, "base:tracerbug") == 0) {
			snprintf(out, out_cap, "sticky_tracer_device");
		} else if (catalog_id && strcmp(catalog_id, "base:targetamplifier") == 0) {
			snprintf(out, out_cap, "sticky_target_amplifier");
		} else {
			const char *slug = s_modelSlug(modelnum);
			snprintf(out, out_cap, "thrown_%s", slug ? slug : mode_name);
		}
	} else {
		snprintf(out, out_cap, "%s_payload", mode_name);
	}
	out[out_cap - 1] = '\0';
}

static const char *s_entitySlugForThrow(const char *catalog_id, s32 mode,
                                        const struct weaponfunc_throw *tw,
                                        const char **archetype_out)
{
	(void)tw;
	if (archetype_out) *archetype_out = NULL;
	if (!catalog_id) return NULL;
	if (strcmp(catalog_id, "base:laptopgun") == 0) {
		if (archetype_out) *archetype_out = "autogun";
		return "deployed_autogun";
	}
	if (strcmp(catalog_id, "base:dragon") == 0) {
		if (archetype_out) *archetype_out = "armed_proxy_explosive";
		return "armed_dragon_proxy";
	}
	if (strcmp(catalog_id, "base:grenade") == 0 && mode == 1) {
		if (archetype_out) *archetype_out = "proxy_grenade_trigger";
		return "armed_proxy_grenade";
	}
	if (strcmp(catalog_id, "base:nbomb") == 0) {
		if (archetype_out) *archetype_out = "nbomb_storm";
		return "nbomb_storm_trigger";
	}
	if (strcmp(catalog_id, "base:timedmine") == 0) {
		if (archetype_out) *archetype_out = "armed_timed_mine";
		return "armed_timed_mine";
	}
	if (strcmp(catalog_id, "base:proximitymine") == 0) {
		if (archetype_out) *archetype_out = "armed_proxy_mine";
		return "armed_proximity_mine";
	}
	if (strcmp(catalog_id, "base:remotemine") == 0) {
		if (archetype_out) *archetype_out = "armed_remote_mine";
		return "armed_remote_mine";
	}
	if (strcmp(catalog_id, "base:ecmmine") == 0) {
		if (archetype_out) *archetype_out = "sticky_device";
		return "armed_ecm_device";
	}
	if (strcmp(catalog_id, "base:commsrider") == 0) {
		if (archetype_out) *archetype_out = "sticky_device";
		return "attached_comms_device";
	}
	if (strcmp(catalog_id, "base:tracerbug") == 0) {
		if (archetype_out) *archetype_out = "sticky_device";
		return "attached_tracer_device";
	}
	if (strcmp(catalog_id, "base:targetamplifier") == 0) {
		if (archetype_out) *archetype_out = "sticky_device";
		return "attached_target_amplifier";
	}
	return NULL;
}

static pdweapon_nested_payload_t *s_payloadPlanFind(pdweapon_payload_plan_t *plan,
                                                    const char *catalog_id)
{
	for (s32 i = 0; i < plan->count; i++) {
		if (strcmp(plan->payloads[i].catalog_id, catalog_id) == 0) {
			return &plan->payloads[i];
		}
	}
	return NULL;
}

static pdweapon_nested_payload_t *s_payloadPlanAdd(pdweapon_payload_plan_t *plan,
                                                   const char *parent_id,
                                                   const char *out_dir,
                                                   const char *parent_filename,
                                                   asset_type_e type,
                                                   const char *local_slug,
                                                   const char *entity_ref,
                                                   const char *archetype,
                                                   const struct weaponfunc *func,
                                                   s32 mode_index,
                                                   s32 projectile_modelnum)
{
	char catalog_id[CATALOG_ID_LEN];
	if (weaponGraphArchiveDerivedNestedId(parent_id, type, local_slug,
			catalog_id, sizeof(catalog_id)) != 0) {
		return NULL;
	}

	pdweapon_nested_payload_t *existing = s_payloadPlanFind(plan, catalog_id);
	if (existing) return existing;
	if (plan->count >= PDWEAPON_MAX_NESTED_PAYLOADS) return NULL;

	pdweapon_nested_payload_t *p = &plan->payloads[plan->count++];
	memset(p, 0, sizeof(*p));
	p->type = type;
	p->func = func;
	p->mode_index = mode_index;
	p->projectile_modelnum = projectile_modelnum;
	p->archetype = archetype;
	strncpy(p->local_slug, local_slug, sizeof(p->local_slug) - 1);
	strncpy(p->catalog_id, catalog_id, sizeof(p->catalog_id) - 1);
	if (entity_ref) strncpy(p->entity_ref, entity_ref, sizeof(p->entity_ref) - 1);

	const char *folder = type == ASSET_PROJECTILE ? "projectiles" : "entities";
	const char *ext = weaponGraphArchiveExtensionForType(type);
	snprintf(p->archive_entry, sizeof(p->archive_entry), "%s/%s%s",
		folder, p->local_slug, ext ? ext : ".pdasset");
	snprintf(p->temp_relpath, sizeof(p->temp_relpath), "%s/%s.%s.tmp%s",
		out_dir, parent_filename, p->local_slug, ext ? ext : ".pdasset");
	const char *full = fsFullPath(p->temp_relpath, p->temp_fullpath,
		sizeof(p->temp_fullpath));
	if (!full || !full[0]) {
		memset(p, 0, sizeof(*p));
		plan->count--;
		return NULL;
	}
	return p;
}

static s32 s_planPayloadsForFunction(pdweapon_payload_plan_t *plan,
                                      const char *parent_id,
                                      const char *out_dir,
                                      const char *parent_filename,
                                      s32 mode,
                                      const struct weaponfunc *f,
                                      char projectile_ref[CATALOG_ID_LEN],
                                      char entity_ref[CATALOG_ID_LEN])
{
	projectile_ref[0] = '\0';
	entity_ref[0] = '\0';
	if (!f) return 0;

	if (f->type == INVENTORYFUNCTYPE_SHOOT_PROJECTILE) {
		const struct weaponfunc_shootprojectile *sp =
			(const struct weaponfunc_shootprojectile *)f;
		char local_slug[WEAPON_GRAPH_ARCHIVE_LOCAL_SLUG_LEN];
		s_projectileSlugForFunction(parent_id, mode, f, local_slug, sizeof(local_slug));
		pdweapon_nested_payload_t *p = s_payloadPlanAdd(plan, parent_id, out_dir,
			parent_filename, ASSET_PROJECTILE, local_slug, NULL, NULL, f, mode,
			sp->projectilemodelnum);
		if (!p) return -1;
		strncpy(projectile_ref, p->catalog_id, CATALOG_ID_LEN - 1);
		projectile_ref[CATALOG_ID_LEN - 1] = '\0';
		return 0;
	}

	if (f->type == INVENTORYFUNCTYPE_THROW) {
		const struct weaponfunc_throw *tw = (const struct weaponfunc_throw *)f;
		const char *archetype = NULL;
		const char *entity_slug = s_entitySlugForThrow(parent_id, mode, tw, &archetype);
		if (entity_slug) {
			pdweapon_nested_payload_t *e = s_payloadPlanAdd(plan, parent_id, out_dir,
				parent_filename, ASSET_ENTITY, entity_slug, NULL, archetype, f,
				mode, tw->projectilemodelnum);
			if (!e) return -1;
			strncpy(entity_ref, e->catalog_id, CATALOG_ID_LEN - 1);
			entity_ref[CATALOG_ID_LEN - 1] = '\0';
		}

		char local_slug[WEAPON_GRAPH_ARCHIVE_LOCAL_SLUG_LEN];
		s_projectileSlugForFunction(parent_id, mode, f, local_slug, sizeof(local_slug));
		pdweapon_nested_payload_t *p = s_payloadPlanAdd(plan, parent_id, out_dir,
			parent_filename, ASSET_PROJECTILE, local_slug,
			entity_ref[0] ? entity_ref : NULL, NULL, f, mode, tw->projectilemodelnum);
		if (!p) return -1;
		strncpy(projectile_ref, p->catalog_id, CATALOG_ID_LEN - 1);
		projectile_ref[CATALOG_ID_LEN - 1] = '\0';
		return 0;
	}

	return 0;
}

static void s_payloadPlanCleanup(pdweapon_payload_plan_t *plan)
{
	if (!plan) return;
	for (s32 i = 0; i < plan->count; i++) {
		if (plan->payloads[i].temp_relpath[0]) {
			s_removeRelpathIfExists(plan->payloads[i].temp_relpath);
		}
	}
}

static s32 s_writeProjectileArchive(const pdweapon_nested_payload_t *p)
{
	if (!p || p->type != ASSET_PROJECTILE) return -1;

	char model_ref[64];
	s_modelRef(p->projectile_modelnum, model_ref, sizeof(model_ref));

	const struct weaponfunc *f = p->func;
	u32 flags = f ? f->flags : 0;
	s32 timer60 = -1;
	s32 speed = 0;
	s32 traveldist = 0;
	f32 scale = 1.0f;
	f32 reflectangle = 0.0f;
	f32 damage = 0.0f;
	s32 activatetime60 = 0;
	s32 recoverytime60 = 0;

	if (f && f->type == INVENTORYFUNCTYPE_SHOOT_PROJECTILE) {
		const struct weaponfunc_shootprojectile *sp =
			(const struct weaponfunc_shootprojectile *)f;
		const struct weaponfunc_shoot *sh = &sp->base;
		timer60 = sp->timer60;
		speed = sp->speed;
		traveldist = sp->traveldist;
		scale = sp->scale;
		reflectangle = sp->reflectangle;
		damage = sh->damage;
	} else if (f && f->type == INVENTORYFUNCTYPE_THROW) {
		const struct weaponfunc_throw *tw = (const struct weaponfunc_throw *)f;
		activatetime60 = tw->activatetime60;
		recoverytime60 = tw->recoverytime60;
		damage = tw->damage;
	}

	char projectile_ini[1024];
	int ini_len = snprintf(projectile_ini, sizeof(projectile_ini),
		"[projectile]\n"
		"schema = pd.projectile.v1\n"
		"catalog_id = %s\n"
		"name = %s\n"
		"model_ref = %s\n"
		"model_archive = models/visual.pdmesh\n"
		"behavior_graph = behavior.graph.json\n"
		"%s%s%s",
		p->catalog_id,
		p->local_slug,
		model_ref,
		p->entity_ref[0] ? "entity_ref = " : "",
		p->entity_ref[0] ? p->entity_ref : "",
		p->entity_ref[0] ? "\n" : "");
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(projectile_ini)) return -1;

	char graph[4096];
	int graph_len = snprintf(graph, sizeof(graph),
		"{\n"
		"  \"schema\": \"pd.projectile_graph.v1\",\n"
		"  \"asset_id\": \"%s\",\n"
		"  \"graph_id\": \"base_projectile_payload_v1\",\n"
		"  \"nodes\": [\n"
		"    {\n"
		"      \"id\": \"spawn_state\",\n"
		"      \"kind\": \"projectile.spawn_state\",\n"
		"      \"params\": {\n"
		"        \"model_ref\": \"%s\",\n"
		"        \"model_archive\": \"models/visual.pdmesh\",\n"
		"        \"source_mode\": \"%s\",\n"
		"        \"source_function_type\": \"%s\",\n"
		"        \"scale\": %.7g,\n"
		"        \"damage\": %.7g,\n"
		"        \"flags\": %u\n"
		"      }\n"
		"    },\n"
		"    {\n"
		"      \"id\": \"motion\",\n"
		"      \"kind\": \"projectile.motion\",\n"
		"      \"params\": {\n"
		"        \"motion_kind\": \"%s\",\n"
		"        \"speed\": %d,\n"
		"        \"travel_distance\": %d,\n"
		"        \"timer60\": %d,\n"
		"        \"activation_time60\": %d,\n"
		"        \"recovery_time60\": %d,\n"
		"        \"reflect_angle\": %.7g,\n"
		"        \"powered\": %s,\n"
		"        \"calculate_trajectory\": %s\n"
		"      }\n"
		"    }%s%s%s\n"
		"  ],\n"
		"  \"edges\": [\n"
		"    { \"from\": \"spawn_state\", \"to\": \"motion\" }%s%s%s\n"
		"  ],\n"
		"  \"exports\": [\n"
		"    { \"name\": \"main\", \"node\": \"%s\" }\n"
		"  ]\n"
		"}\n",
		p->catalog_id,
		model_ref,
		s_modeName(p->mode_index),
		f ? s_funcTypeName(f->type) : "unknown",
		(double)scale,
		(double)damage,
		(unsigned)flags,
		(f && f->type == INVENTORYFUNCTYPE_THROW) ? "ballistic_throw" :
			((flags & FUNCFLAG_PROJECTILE_POWERED) ? "powered" : "ballistic"),
		speed,
		traveldist,
		timer60,
		activatetime60,
		recoverytime60,
		(double)reflectangle,
		(flags & FUNCFLAG_PROJECTILE_POWERED) ? "true" : "false",
		(flags & FUNCFLAG_CALCULATETRAJECTORY) ? "true" : "false",
		(flags & FUNCFLAG_HOMINGROCKET) ? ",\n    {\n      \"id\": \"homing\",\n      \"kind\": \"projectile.homing\",\n      \"params\": { \"target_source\": \"current_lock\", \"runtime_constants\": \"extract_in_runtime_adapter\" }\n    }" : "",
		(flags & FUNCFLAG_FLYBYWIRE) ? ",\n    {\n      \"id\": \"fly_by_wire\",\n      \"kind\": \"projectile.fly_by_wire\",\n      \"params\": { \"control_source\": \"owner\", \"bot_route_policy\": \"runtime_existing\" }\n    }" : "",
		(flags & FUNCFLAG_STICKTOWALL) ? ",\n    {\n      \"id\": \"wall_hugger\",\n      \"kind\": \"projectile.wall_hugger\",\n      \"params\": { \"stick_surface_filter\": \"background\", \"post_fall_timer60\": 360 }\n    }" : "",
		(flags & FUNCFLAG_HOMINGROCKET) ? ",\n    " : "",
		(flags & FUNCFLAG_HOMINGROCKET) ? "{ \"from\": \"motion\", \"to\": \"homing\" }" : "",
		(flags & FUNCFLAG_FLYBYWIRE) ? ",\n    { \"from\": \"motion\", \"to\": \"fly_by_wire\" }" :
			((flags & FUNCFLAG_STICKTOWALL) ? ",\n    { \"from\": \"motion\", \"to\": \"wall_hugger\" }" : ""),
		(flags & FUNCFLAG_FLYBYWIRE) ? "fly_by_wire" :
			((flags & FUNCFLAG_HOMINGROCKET) ? "homing" :
			((flags & FUNCFLAG_STICKTOWALL) ? "wall_hugger" : "motion")));
	if (graph_len <= 0 || (size_t)graph_len >= sizeof(graph)) return -1;

	mod_archive_writer_t *aw = modArchiveBegin(p->temp_fullpath);
	if (!aw) return -1;
	if (modArchiveAddFileMem(aw, "projectile.ini", projectile_ini, (u32)ini_len) != 0 ||
			modArchiveAddFileMem(aw, "behavior.graph.json", graph, (u32)graph_len) != 0) {
		modArchiveAbort(aw);
		return -1;
	}
	if (s_addProjectileModelDependency(aw, p->catalog_id,
			p->projectile_modelnum, "models/visual.pdmesh") != 0) {
		modArchiveAbort(aw);
		return -1;
	}
	return modArchiveFinish(aw) == 0 ? 0 : -1;
}

static s32 s_writeEntityArchive(const pdweapon_nested_payload_t *p)
{
	if (!p || p->type != ASSET_ENTITY) return -1;

	char model_ref[64];
	s_modelRef(p->projectile_modelnum, model_ref, sizeof(model_ref));
	const char *archetype = p->archetype ? p->archetype : "deployed_entity";
	u32 flags = p->func ? p->func->flags : 0;
	s32 activatetime60 = 0;
	s32 recoverytime60 = 0;
	if (p->func && p->func->type == INVENTORYFUNCTYPE_THROW) {
		const struct weaponfunc_throw *tw = (const struct weaponfunc_throw *)p->func;
		activatetime60 = tw->activatetime60;
		recoverytime60 = tw->recoverytime60;
	}

	char entity_ini[1024];
	int ini_len = snprintf(entity_ini, sizeof(entity_ini),
		"[entity]\n"
		"schema = pd.entity.v1\n"
		"catalog_id = %s\n"
		"name = %s\n"
		"archetype = %s\n"
		"model_ref = %s\n"
		"model_archive = models/visual.pdmesh\n"
		"behavior_graph = behavior.graph.json\n",
		p->catalog_id,
		p->local_slug,
		archetype,
		model_ref);
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(entity_ini)) return -1;

	const char *entity_kind = "entity.sticky_device";
	if (strcmp(archetype, "autogun") == 0) entity_kind = "entity.autogun";
	else if (strstr(archetype, "proxy")) entity_kind = "entity.proxy_trigger";
	else if (strstr(archetype, "remote")) entity_kind = "entity.remote_detonatable";
	else if (strstr(archetype, "timed")) entity_kind = "entity.timed_detonatable";
	else if (strstr(archetype, "storm")) entity_kind = "entity.nbomb_storm";

	char graph[4096];
	int graph_len = snprintf(graph, sizeof(graph),
		"{\n"
		"  \"schema\": \"pd.entity_graph.v1\",\n"
		"  \"asset_id\": \"%s\",\n"
		"  \"graph_id\": \"base_entity_payload_v1\",\n"
		"  \"nodes\": [\n"
		"    {\n"
		"      \"id\": \"entity_behavior\",\n"
		"      \"kind\": \"%s\",\n"
		"      \"params\": {\n"
		"        \"archetype\": \"%s\",\n"
		"        \"model_ref\": \"%s\",\n"
		"        \"model_archive\": \"models/visual.pdmesh\",\n"
		"        \"source_mode\": \"%s\",\n"
		"        \"activation_time60\": %d,\n"
		"        \"recovery_time60\": %d,\n"
		"        \"flags\": %u,\n"
		"        \"runtime_detail\": \"module adapter extracts current C constants\"\n"
		"      }\n"
		"    }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [\n"
		"    { \"name\": \"main\", \"node\": \"entity_behavior\" }\n"
		"  ]\n"
		"}\n",
		p->catalog_id,
		entity_kind,
		archetype,
		model_ref,
		s_modeName(p->mode_index),
		activatetime60,
		recoverytime60,
		(unsigned)flags);
	if (graph_len <= 0 || (size_t)graph_len >= sizeof(graph)) return -1;

	mod_archive_writer_t *aw = modArchiveBegin(p->temp_fullpath);
	if (!aw) return -1;
	if (modArchiveAddFileMem(aw, "entity.ini", entity_ini, (u32)ini_len) != 0 ||
			modArchiveAddFileMem(aw, "behavior.graph.json", graph, (u32)graph_len) != 0) {
		modArchiveAbort(aw);
		return -1;
	}
	if (s_addProjectileModelDependency(aw, p->catalog_id,
			p->projectile_modelnum, "models/visual.pdmesh") != 0) {
		modArchiveAbort(aw);
		return -1;
	}
	return modArchiveFinish(aw) == 0 ? 0 : -1;
}

static s32 s_emitNestedPayloadArchives(pdweapon_payload_plan_t *plan)
{
	memset(&plan->inventory, 0, sizeof(plan->inventory));
	for (s32 i = 0; i < plan->count; i++) {
		pdweapon_nested_payload_t *p = &plan->payloads[i];
		s32 r = p->type == ASSET_PROJECTILE
			? s_writeProjectileArchive(p)
			: s_writeEntityArchive(p);
		if (r != 0) return -1;

		char digest[SHA256_HEX_SIZE];
		if (weaponGraphArchiveCanonicalSha256File(p->temp_fullpath, digest) != 0) {
			return -1;
		}

		if (plan->inventory.count >= WEAPON_GRAPH_ARCHIVE_MAX_NESTED_PAYLOADS) {
			return -1;
		}
		weapon_graph_archive_payload_t *inv =
			&plan->inventory.payloads[plan->inventory.count++];
		memset(inv, 0, sizeof(*inv));
		inv->type = p->type;
		strncpy(inv->archive_entry, p->archive_entry, sizeof(inv->archive_entry) - 1);
		strncpy(inv->local_slug, p->local_slug, sizeof(inv->local_slug) - 1);
		strncpy(inv->catalog_id, p->catalog_id, sizeof(inv->catalog_id) - 1);
		strncpy(inv->canonical_sha256, digest, sizeof(inv->canonical_sha256) - 1);
	}
	return 0;
}

static void s_emitShootGraphParams(jw_t *w,
                                    const struct weaponfunc_shoot *sh,
                                    s32 has_more)
{
	s_emitRecoilSettings(w, sh->recoilsettings, "recoilsettings", 0);
	jw_field_int(w, "recoverytime_ticks60", sh->recoverytime60, 0);
	jw_field_f32(w, "damage", sh->damage, 0);
	jw_field_f32(w, "spread", sh->spread, 0);
	jw_field_int(w, "recoil_anim_unk24", sh->unk24, 0);
	jw_field_int(w, "recoil_anim_unk25", sh->unk25, 0);
	jw_field_int(w, "recoil_anim_unk26", sh->unk26, 0);
	jw_field_int(w, "recoil_anim_unk27", sh->unk27, 0);
	jw_field_f32(w, "recoildist", sh->recoildist, 0);
	jw_field_f32(w, "recoilangle", sh->recoilangle, 0);
	jw_field_f32(w, "slidemax", sh->slidemax, 0);
	jw_field_f32(w, "impactforce", sh->impactforce, 0);
	jw_field_uint(w, "duration_ticks60", sh->duration60, 0);
	jw_field_sfx_or_int(w, "shootsound", sh->shootsound, 0);
	jw_field_uint(w, "penetration", sh->penetration, has_more ? 0 : 1);
}

static void s_emitWeaponGraphParams(jw_t *w, const char *catalog_id,
                                     s32 mode, const struct weaponfunc *f,
                                     const char *projectile_ref,
                                     const char *entity_ref)
{
	jw_field_str(w, "mode", s_modeName(mode), 0);
	if (!f || f->type == INVENTORYFUNCTYPE_NONE) {
		jw_field_str(w, "function_type", "none", 0);
		jw_field_str(w, "source", "empty_slot", 1);
		return;
	}

	jw_field_str(w, "function_type", s_funcTypeName(f->type), 0);
	jw_field_lang_or_int(w, "name", f->name, 0);
	jw_field_int(w, "ammo_slot", f->ammoindex, 0);
	jw_field_uint(w, "flags", f->flags, 0);
	jw_field_anim_ref(w, "fire_animation", f->fire_animation, 0);

	switch (f->type) {
	case INVENTORYFUNCTYPE_SHOOT_SINGLE: {
		const struct weaponfunc_shootsingle *ss =
			(const struct weaponfunc_shootsingle *)f;
		jw_field_str(w, "trigger_policy",
			(strcmp(s_weaponModuleKind(catalog_id, mode, f), "fire.charge_release") == 0)
				? "charge_release"
				: "press_release",
			0);
		if (s_burstCountForFlags(f->flags) > 0) {
			jw_field_int(w, "burst_count", s_burstCountForFlags(f->flags), 0);
		}
		s_emitShootGraphParams(w, &ss->base, 0);
		break;
	}
	case INVENTORYFUNCTYPE_SHOOT_AUTOMATIC: {
		const struct weaponfunc_shootauto *sa =
			(const struct weaponfunc_shootauto *)f;
		jw_field_str(w, "trigger_policy", "held_repeat", 0);
		if (s_burstCountForFlags(f->flags) > 0) {
			jw_field_int(w, "burst_count", s_burstCountForFlags(f->flags), 0);
		}
		s_emitShootGraphParams(w, &sa->base, 1);
		jw_field_f32(w, "initial_rpm", sa->initialrpm, 0);
		jw_field_f32(w, "max_rpm", sa->maxrpm, 0);
		jw_field_int(w, "turret_accel", sa->turretaccel, 0);
		jw_field_int(w, "turret_decel", sa->turretdecel, 1);
		break;
	}
	case INVENTORYFUNCTYPE_SHOOT_PROJECTILE: {
		const struct weaponfunc_shootprojectile *sp =
			(const struct weaponfunc_shootprojectile *)f;
		char model_ref[64];
		s_modelRef(sp->projectilemodelnum, model_ref, sizeof(model_ref));
		jw_field_str(w, "projectile_ref",
			(projectile_ref && projectile_ref[0]) ? projectile_ref : NULL, 0);
		jw_field_str(w, "projectile_model_ref", model_ref, 0);
		s_emitShootGraphParams(w, &sp->base, 1);
		jw_field_f32(w, "scale", sp->scale, 0);
		jw_field_int(w, "speed", sp->speed, 0);
		jw_field_int(w, "travel_distance", sp->traveldist, 0);
		jw_field_int(w, "timer_ticks60", sp->timer60, 0);
		jw_field_f32(w, "reflect_angle", sp->reflectangle, 0);
		jw_field_int(w, "soundnum", sp->soundnum, 1);
		break;
	}
	case INVENTORYFUNCTYPE_THROW: {
		const struct weaponfunc_throw *tw =
			(const struct weaponfunc_throw *)f;
		char model_ref[64];
		s_modelRef(tw->projectilemodelnum, model_ref, sizeof(model_ref));
		jw_field_str(w, "payload_ref",
			(projectile_ref && projectile_ref[0]) ? projectile_ref :
			((entity_ref && entity_ref[0]) ? entity_ref : NULL), 0);
		jw_field_str(w, "projectile_ref",
			(projectile_ref && projectile_ref[0]) ? projectile_ref : NULL, 0);
		jw_field_str(w, "entity_ref",
			(entity_ref && entity_ref[0]) ? entity_ref : NULL, 0);
		jw_field_str(w, "projectile_model_ref", model_ref, 0);
		jw_field_int(w, "activation_time_ticks60", tw->activatetime60, 0);
		jw_field_int(w, "recovery_time_ticks60", tw->recoverytime60, 0);
		jw_field_f32(w, "damage", tw->damage, 1);
		break;
	}
	case INVENTORYFUNCTYPE_MELEE: {
		const struct weaponfunc_melee *me =
			(const struct weaponfunc_melee *)f;
		jw_field_f32(w, "damage", me->damage, 0);
		jw_field_f32(w, "range", me->range, 1);
		break;
	}
	case INVENTORYFUNCTYPE_SPECIAL: {
		const struct weaponfunc_special *sx =
			(const struct weaponfunc_special *)f;
		jw_field_int(w, "specialfunc", sx->specialfunc, 0);
		jw_field_str(w, "special_name", s_specialName(sx->specialfunc), 0);
		jw_field_int(w, "recovery_time_ticks60", sx->recoverytime60, 0);
		jw_field_int(w, "soundnum", sx->soundnum, 1);
		break;
	}
	case INVENTORYFUNCTYPE_DEVICE: {
		const struct weaponfunc_device *dv =
			(const struct weaponfunc_device *)f;
		jw_field_uint(w, "device", dv->device, 0);
		jw_field_str(w, "device_name", s_deviceName(dv->device), 1);
		break;
	}
	default:
		jw_field_str(w, "runtime_detail", "unknown_function_type", 1);
		break;
	}
}

static void s_emitWeaponGraphNode(jw_t *w, const char *node_id,
                                  const char *catalog_id, s32 mode,
                                  const struct weaponfunc *f,
                                  const char *projectile_ref,
                                  const char *entity_ref,
                                  s32 last)
{
	jw_indent(w);
	fputs("{\n", w->fp);
	w->indent++;
	jw_field_str(w, "id", node_id, 0);
	jw_field_str(w, "kind", s_weaponModuleKind(catalog_id, mode, f), 0);
	jw_field_str(w, "subgraph", s_modeName(mode), 0);
	jw_open_object(w, "params");
	s_emitWeaponGraphParams(w, catalog_id, mode, f, projectile_ref, entity_ref);
	jw_close_object(w, 1);
	w->indent--;
	jw_indent(w);
	fputs(last ? "}\n" : "},\n", w->fp);
}

static const char *s_weaponTriggerEventKind(const char *catalog_id,
                                            s32 mode,
                                            const struct weaponfunc *f)
{
	if (!f) return "event.trigger_pressed";
	if (strcmp(s_weaponModuleKind(catalog_id, mode, f), "fire.charge_release") == 0) {
		return "event.trigger_released";
	}
	if (f->type == INVENTORYFUNCTYPE_SHOOT_AUTOMATIC) {
		return "event.trigger_held";
	}
	if (f->type == INVENTORYFUNCTYPE_THROW) {
		return "event.trigger_released";
	}
	return "event.trigger_pressed";
}

static void s_emitWeaponGraphEventNode(jw_t *w, const char *node_id,
                                       const char *catalog_id, s32 mode,
                                       const struct weaponfunc *f,
                                       s32 last)
{
	const char *kind = s_weaponTriggerEventKind(catalog_id, mode, f);
	const char *trigger = strstr(kind, "held") ? "held" :
		(strstr(kind, "released") ? "released" : "pressed");
	jw_indent(w);
	fputs("{\n", w->fp);
	w->indent++;
	jw_field_str(w, "id", node_id, 0);
	jw_field_str(w, "kind", kind, 0);
	jw_field_str(w, "subgraph", s_modeName(mode), 0);
	jw_open_object(w, "params");
	jw_field_str(w, "mode", s_modeName(mode), 0);
	jw_field_str(w, "trigger", trigger, 1);
	jw_close_object(w, 1);
	w->indent--;
	jw_indent(w);
	fputs(last ? "}\n" : "},\n", w->fp);
}

static void s_emitWeaponGraphContext(jw_t *w, const char *name,
                                     const char *scope, const char *source,
                                     const char *type, const char *lifetime,
                                     s32 last)
{
	jw_open_object(w, NULL);
	jw_field_str(w, "name", name, 0);
	jw_field_str(w, "scope", scope, 0);
	jw_field_str(w, "source", source, 0);
	jw_field_str(w, "type", type, 0);
	jw_field_str(w, "lifetime", lifetime, 1);
	jw_close_object(w, last);
}

static void s_emitWeaponGraphSubgraph(jw_t *w, const char *id,
                                      const char *entry, s32 last)
{
	jw_open_object(w, NULL);
	jw_field_str(w, "id", id, 0);
	jw_field_str(w, "entry", entry, 1);
	jw_close_object(w, last);
}

static void s_emitWeaponGraphExport(jw_t *w, const char *name,
                                    const char *node, s32 last)
{
	jw_indent(w);
	fputs("{\n", w->fp);
	w->indent++;
	jw_field_str(w, "name", name, 0);
	jw_field_str(w, "node", node, 1);
	w->indent--;
	jw_indent(w);
	fputs(last ? "}\n" : "},\n", w->fp);
}

static s32 s_emitWeaponGraphFile(const char *graph_tmp_relpath,
                                  const char *catalog_id,
                                  const struct weapon *wpn,
                                  char projectile_refs[2][CATALOG_ID_LEN],
                                  char entity_refs[2][CATALOG_ID_LEN])
{
	FILE *fp = fsFileOpenWrite(graph_tmp_relpath);
	if (!fp) return -1;

	jw_t w;
	w.fp = fp;
	w.indent = 0;
	w.error = 0;

	fputs("{\n", fp);
	w.indent = 1;
	jw_field_str(&w, "schema", "pd.weapon_graph.v1", 0);
	jw_field_str(&w, "asset_id", catalog_id, 0);
	jw_field_str(&w, "graph_id", "base_weapon_graph_v2", 0);
	jw_open_object(&w, "compatibility");
	jw_field_str(&w, "manifest", "manifest.json", 0);
	jw_field_str(&w, "nested_payloads", WEAPON_GRAPH_ARCHIVE_NESTED_PAYLOADS_ENTRY, 0);
	jw_field_str(&w, "runtime_source", "graph_ir_pending_manifest_bridge", 1);
	jw_close_object(&w, 0);

	jw_open_array(&w, "shared_context");
	s_emitWeaponGraphContext(&w, "owner_player", "player", "equipped_player",
		"player_ref", "weapon_instance", 0);
	s_emitWeaponGraphContext(&w, "owner_team", "player", "equipped_player_team",
		"team_ref", "weapon_instance", 0);
	s_emitWeaponGraphContext(&w, "weapon_instance", "weapon", "equipped_weapon",
		"weapon_instance_ref", "weapon_instance", 0);
	s_emitWeaponGraphContext(&w, "damage_credit_player", "projectile", "owner_player",
		"player_ref", "projectile_life", 1);
	jw_close_array(&w, 0);

	jw_open_array(&w, "subgraphs");
	s_emitWeaponGraphSubgraph(&w, "primary", "primary_trigger", 0);
	s_emitWeaponGraphSubgraph(&w, "secondary", "secondary_trigger", 1);
	jw_close_array(&w, 0);

	jw_open_array(&w, "nodes");
	s_emitWeaponGraphEventNode(&w, "primary_trigger", catalog_id, 0,
		(const struct weaponfunc *)wpn->functions[0], 0);
	s_emitWeaponGraphNode(&w, "primary_action", catalog_id, 0,
		(const struct weaponfunc *)wpn->functions[0],
		projectile_refs[0], entity_refs[0], 0);
	s_emitWeaponGraphEventNode(&w, "secondary_trigger", catalog_id, 1,
		(const struct weaponfunc *)wpn->functions[1], 0);
	s_emitWeaponGraphNode(&w, "secondary_action", catalog_id, 1,
		(const struct weaponfunc *)wpn->functions[1],
		projectile_refs[1], entity_refs[1], 1);
	jw_close_array(&w, 0);

	jw_open_array(&w, "edges");
	jw_open_object(&w, NULL);
	jw_field_str(&w, "from", "primary_trigger", 0);
	jw_field_str(&w, "to", "primary_action", 1);
	jw_close_object(&w, 0);
	jw_open_object(&w, NULL);
	jw_field_str(&w, "from", "secondary_trigger", 0);
	jw_field_str(&w, "to", "secondary_action", 1);
	jw_close_object(&w, 1);
	jw_close_array(&w, 0);

	jw_open_array(&w, "exports");
	s_emitWeaponGraphExport(&w, "primary", "primary_action", 0);
	s_emitWeaponGraphExport(&w, "secondary", "secondary_action", 1);
	jw_close_array(&w, 1);
	w.indent = 0;
	fputs("}\n", fp);

	s32 ok = ferror(fp) == 0;
	if (fclose(fp) != 0) ok = 0;
	return ok ? 0 : -1;
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
	snprintf(relpath, sizeof(relpath), "%s/%s.pdweapon", out_dir, filename);

	char old_relpath[FS_MAXPATH];
	const char *removed_ext = ".pd" "wpn";
	snprintf(old_relpath, sizeof(old_relpath), "%s/%s%s",
		out_dir, filename, removed_ext);
	s_removeRelpathIfExists(old_relpath);

	if (!force_rewrite) {
		s32 sz = fsFileSize(relpath);
		if (sz > 0 && s_existingWeaponArchiveComplete(relpath)) return 0;
	}

	pdweapon_anim_deps_t anim_deps;
	pdweapon_audio_deps_t audio_deps;
	s_collectWeaponDependencies(wpn, &anim_deps, &audio_deps);

	char manifest_tmp_relpath[FS_MAXPATH];
	snprintf(manifest_tmp_relpath, sizeof(manifest_tmp_relpath),
		"%s/%s.pdweapon.manifest.tmp", out_dir, filename);

	FILE *fp = fsFileOpenWrite(manifest_tmp_relpath);
	if (!fp) {
		sysLoudFailf("EXTRACT.PDWEAPON",
			"fsFileOpenWrite failed for \"%s\"", manifest_tmp_relpath);
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
	jw_field_str(&w, "dependency_closure",
		PDWEAPON_DEPENDENCY_CLOSURE_MARKER, 0);
	jw_open_object(&w, "embedded_archives");
	jw_field_str(&w, "hi_model", wpn->hi_model ? "models/held_hi.pdmesh" : NULL, 0);
	jw_field_str(&w, "lo_model", wpn->lo_model ? "models/held_lo.pdmesh" : NULL, 0);
	jw_field_str(&w, "animations_manifest", "animations_manifest.tsv", 0);
	jw_field_str(&w, "audio_manifest", "audio_manifest.tsv", 1);
	jw_close_object(&w, 0);
	jw_open_object(&w, "dependency_counts");
	jw_field_int(&w, "animations", anim_deps.count, 0);
	jw_field_int(&w, "audio", audio_deps.count, 1);
	jw_close_object(&w, 0);

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

	u32 manifest_size = 0;
	void *manifest_bytes = fsFileLoad(manifest_tmp_relpath, &manifest_size);
	s_removeRelpathIfExists(manifest_tmp_relpath);
	if (!manifest_bytes || manifest_size == 0) {
		if (manifest_bytes) sysMemFree(manifest_bytes);
		sysLoudFailf("EXTRACT.PDWEAPON",
			"temporary manifest load failed for \"%s\"", manifest_tmp_relpath);
		return -1;
	}

	char weapon_ini[768];
	int weapon_ini_len = snprintf(weapon_ini, sizeof(weapon_ini),
		"[weapon]\n"
		"schema = pd.weapon.v1\n"
		"dependency_closure = " PDWEAPON_DEPENDENCY_CLOSURE_MARKER "\n"
		"catalog_id = %s\n"
		"weapon_id = %d\n"
		"manifest = manifest.json\n"
		"behavior_graph = behavior.graph.json\n"
		"nested_payloads = nested_payloads.json\n",
		catalog_id, weapon_id);
	if (weapon_ini_len <= 0 || (size_t)weapon_ini_len >= sizeof(weapon_ini)) {
		sysMemFree(manifest_bytes);
		sysLoudFailf("EXTRACT.PDWEAPON",
			"weapon.ini snprintf truncated for \"%s\"", catalog_id);
		return -1;
	}

	pdweapon_payload_plan_t payload_plan;
	memset(&payload_plan, 0, sizeof(payload_plan));
	char projectile_refs[2][CATALOG_ID_LEN];
	char entity_refs[2][CATALOG_ID_LEN];
	memset(projectile_refs, 0, sizeof(projectile_refs));
	memset(entity_refs, 0, sizeof(entity_refs));

	for (s32 mode = 0; mode < 2; mode++) {
		const struct weaponfunc *func =
			(const struct weaponfunc *)wpn->functions[mode];
		if (s_planPayloadsForFunction(&payload_plan, catalog_id, out_dir,
				filename, mode, func, projectile_refs[mode],
				entity_refs[mode]) != 0) {
			sysMemFree(manifest_bytes);
			s_payloadPlanCleanup(&payload_plan);
			sysLoudFailf("EXTRACT.PDWEAPON",
				"nested payload planning failed for \"%s\"", catalog_id);
			return -1;
		}
	}

	if (s_emitNestedPayloadArchives(&payload_plan) != 0) {
		sysMemFree(manifest_bytes);
		s_payloadPlanCleanup(&payload_plan);
		sysLoudFailf("EXTRACT.PDWEAPON",
			"nested payload archive emit failed for \"%s\"", catalog_id);
		return -1;
	}

	char nested_payloads_json[8192];
	if (weaponGraphArchiveFormatNestedPayloadsJson(catalog_id,
			&payload_plan.inventory,
			nested_payloads_json, sizeof(nested_payloads_json)) != 0) {
		sysMemFree(manifest_bytes);
		s_payloadPlanCleanup(&payload_plan);
		sysLoudFailf("EXTRACT.PDWEAPON",
			"nested_payloads.json snprintf truncated for \"%s\"", catalog_id);
		return -1;
	}

	char graph_tmp_relpath[FS_MAXPATH];
	snprintf(graph_tmp_relpath, sizeof(graph_tmp_relpath),
		"%s/%s.pdweapon.graph.tmp", out_dir, filename);
	if (s_emitWeaponGraphFile(graph_tmp_relpath, catalog_id, wpn,
			projectile_refs, entity_refs) != 0) {
		s_removeRelpathIfExists(graph_tmp_relpath);
		sysMemFree(manifest_bytes);
		s_payloadPlanCleanup(&payload_plan);
		sysLoudFailf("EXTRACT.PDWEAPON",
			"behavior graph emit failed for \"%s\"", catalog_id);
		return -1;
	}

	u32 graph_size = 0;
	void *graph_bytes = fsFileLoad(graph_tmp_relpath, &graph_size);
	s_removeRelpathIfExists(graph_tmp_relpath);
	if (!graph_bytes || graph_size == 0) {
		if (graph_bytes) sysMemFree(graph_bytes);
		sysMemFree(manifest_bytes);
		s_payloadPlanCleanup(&payload_plan);
		sysLoudFailf("EXTRACT.PDWEAPON",
			"temporary graph load failed for \"%s\"", graph_tmp_relpath);
		return -1;
	}

	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) {
		sysMemFree(graph_bytes);
		sysMemFree(manifest_bytes);
		s_payloadPlanCleanup(&payload_plan);
		sysLoudFailf("EXTRACT.PDWEAPON",
			"fsFullPath failed for \"%s\"", relpath);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(full);
	if (!aw) {
		sysMemFree(graph_bytes);
		sysMemFree(manifest_bytes);
		s_payloadPlanCleanup(&payload_plan);
		sysLoudFailf("EXTRACT.PDWEAPON",
			"modArchiveBegin failed for \"%s\"", full);
		return -1;
	}
	if (modArchiveAddFileMem(aw, "weapon.ini",
	                         weapon_ini, (u32)weapon_ini_len) != 0) {
		sysMemFree(graph_bytes);
		sysMemFree(manifest_bytes);
		s_payloadPlanCleanup(&payload_plan);
		sysLoudFailf("EXTRACT.PDWEAPON",
			"AddFileMem weapon.ini failed for \"%s\"", full);
		modArchiveAbort(aw);
		return -1;
	}
	if (modArchiveAddFileMem(aw, "manifest.json",
	                         manifest_bytes, manifest_size) != 0) {
		sysMemFree(graph_bytes);
		sysMemFree(manifest_bytes);
		s_payloadPlanCleanup(&payload_plan);
		sysLoudFailf("EXTRACT.PDWEAPON",
			"AddFileMem manifest.json failed for \"%s\"", full);
		modArchiveAbort(aw);
		return -1;
	}
	if (modArchiveAddFileMem(aw, "behavior.graph.json",
	                         graph_bytes, graph_size) != 0) {
		sysMemFree(graph_bytes);
		sysMemFree(manifest_bytes);
		s_payloadPlanCleanup(&payload_plan);
		sysLoudFailf("EXTRACT.PDWEAPON",
			"AddFileMem behavior.graph.json failed for \"%s\"", full);
		modArchiveAbort(aw);
		return -1;
	}
	if (modArchiveAddFileMem(aw, WEAPON_GRAPH_ARCHIVE_NESTED_PAYLOADS_ENTRY,
	                         nested_payloads_json,
	                         (u32)strlen(nested_payloads_json)) != 0) {
		sysMemFree(graph_bytes);
		sysMemFree(manifest_bytes);
		s_payloadPlanCleanup(&payload_plan);
		sysLoudFailf("EXTRACT.PDWEAPON",
			"AddFileMem nested_payloads.json failed for \"%s\"", full);
		modArchiveAbort(aw);
		return -1;
	}
	for (s32 i = 0; i < payload_plan.count; i++) {
		pdweapon_nested_payload_t *p = &payload_plan.payloads[i];
		if (modArchiveAddFileDisk(aw, p->archive_entry,
				p->temp_fullpath) != 0) {
			sysMemFree(graph_bytes);
			sysMemFree(manifest_bytes);
			s_payloadPlanCleanup(&payload_plan);
			sysLoudFailf("EXTRACT.PDWEAPON",
				"AddFileDisk %s failed for \"%s\"", p->archive_entry, full);
			modArchiveAbort(aw);
			return -1;
		}
	}
	if (s_addWeaponMeshDependency(aw, catalog_id, wpn->hi_model, "hi",
			"models/held_hi.pdmesh") != 0 ||
			s_addWeaponMeshDependency(aw, catalog_id, wpn->lo_model, "lo",
			"models/held_lo.pdmesh") != 0) {
		sysMemFree(graph_bytes);
		sysMemFree(manifest_bytes);
		s_payloadPlanCleanup(&payload_plan);
		sysLoudFailf("EXTRACT.PDWEAPON",
			"held model dependency embed failed for \"%s\"", catalog_id);
		modArchiveAbort(aw);
		return -1;
	}
	for (s32 i = 0; i < anim_deps.count; i++) {
		if (s_addWeaponAnimationDependency(aw, catalog_id,
				anim_deps.names[i]) != 0) {
			sysMemFree(graph_bytes);
			sysMemFree(manifest_bytes);
			s_payloadPlanCleanup(&payload_plan);
			sysLoudFailf("EXTRACT.PDWEAPON",
				"animation dependency embed failed for \"%s\" (%s)",
				catalog_id, anim_deps.names[i] ? anim_deps.names[i] : "");
			modArchiveAbort(aw);
			return -1;
		}
	}
	for (s32 i = 0; i < audio_deps.count; i++) {
		if (s_addWeaponAudioDependency(aw, catalog_id,
				audio_deps.sfx[i]) != 0) {
			sysMemFree(graph_bytes);
			sysMemFree(manifest_bytes);
			s_payloadPlanCleanup(&payload_plan);
			sysLoudFailf("EXTRACT.PDWEAPON",
				"audio dependency embed failed for \"%s\" (sfx=%d)",
				catalog_id, audio_deps.sfx[i]);
			modArchiveAbort(aw);
			return -1;
		}
	}
	if (s_addDependencyManifests(aw, &anim_deps, &audio_deps) != 0) {
		sysMemFree(graph_bytes);
		sysMemFree(manifest_bytes);
		s_payloadPlanCleanup(&payload_plan);
		sysLoudFailf("EXTRACT.PDWEAPON",
			"dependency manifest embed failed for \"%s\"", catalog_id);
		modArchiveAbort(aw);
		return -1;
	}
	sysMemFree(graph_bytes);
	sysMemFree(manifest_bytes);
	if (modArchiveFinish(aw) != 0) {
		s_payloadPlanCleanup(&payload_plan);
		sysLoudFailf("EXTRACT.PDWEAPON",
			"modArchiveFinish failed for \"%s\"", full);
		return -1;
	}
	s_payloadPlanCleanup(&payload_plan);
	return 1;
}

/* ------------------------------------------------------------------ */
/* Engine Phase 4 fan-out                                              */
/* ------------------------------------------------------------------ */

typedef struct {
	const char  *weapons_dir;
	s32          force_rewrite;
	s32          count;
	SDL_atomic_t written;
	SDL_atomic_t skipped;
	SDL_atomic_t failed;
	SDL_atomic_t processed;
} pdweapon_fanout_ctx_t;

static void s_pdweaponWork(int i, void *user)
{
	pdweapon_fanout_ctx_t *c = (pdweapon_fanout_ctx_t *)user;
	if (i < 0 || i >= c->count) return;

	const struct weapon *wpn = g_WeaponData[i];
	if (!wpn) {
		SDL_AtomicAdd(&c->skipped, 1);
		goto progress;
	}

	const char *catalog_id = g_WeaponDataCatalogIds[i];
	const struct aibotweaponpreference *bp = (i < g_BotPrefDataCount)
		? &g_BotPrefData[i] : NULL;

	s32 r = s_emitOneWeapon(i, wpn, bp, catalog_id,
	                        c->weapons_dir, c->force_rewrite);
	if (r > 0)       SDL_AtomicAdd(&c->written, 1);
	else if (r == 0) SDL_AtomicAdd(&c->skipped, 1);
	else             SDL_AtomicAdd(&c->failed,  1);

progress:
	{
		int done = SDL_AtomicAdd(&c->processed, 1) + 1;
		if ((done & 0x07) == 0 || done == c->count) {
			bootProgressUpdate(done, c->count);
		}
	}
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                  */
/* ------------------------------------------------------------------ */

s32 romExtractAllPdweapon(s32 force_rewrite)
{
	/* BYOR completion (2026-05-03): walks g_WeaponData[] from the
	 * authoring source-of-truth (port/src/weapondata_authored.c). The
	 * pool path is gone -- the walker downstream registers .pdweapon files
	 * we emit here into the catalog row layer. */

	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDWEAPON",
			"fsDataDirEnsure failed; cannot create output dir");
		return -1;
	}

	char dataDirBuf[FS_MAXPATH + 1];
	char weapons_dir[FS_MAXPATH];
	snprintf(weapons_dir, sizeof(weapons_dir), "%s/weapons",
		fsDataDir(dataDirBuf, sizeof(dataDirBuf)));
	if (!fsCreateDir(weapons_dir)) {
		sysLoudFailf("EXTRACT.PDWEAPON",
			"fsCreateDir(\"%s\") failed", weapons_dir);
		return -1;
	}

	if (romExtractPdFastCacheCanSkip(PDWEAPON_FAST_CACHE_KIND, weapons_dir,
			".pdweapon", force_rewrite)) {
		bootProgressUpdate(g_WeaponDataCount, g_WeaponDataCount);
		sysLogPrintf(LOG_NOTE,
			"romextract pdweapon: written=0 skipped=%d failed=0 total=%d (fast-cache)",
			g_WeaponDataCount, g_WeaponDataCount);
		return 0;
	}

	pdweapon_fanout_ctx_t wctx;
	memset(&wctx, 0, sizeof(wctx));
	wctx.weapons_dir   = weapons_dir;
	wctx.force_rewrite = force_rewrite;
	wctx.count         = g_WeaponDataCount;
	SDL_AtomicSet(&wctx.written,   0);
	SDL_AtomicSet(&wctx.skipped,   0);
	SDL_AtomicSet(&wctx.failed,    0);
	SDL_AtomicSet(&wctx.processed, 0);

	bootProgressUpdate(0, g_WeaponDataCount);
	bootPoolForRangeBlocking(0, g_WeaponDataCount, s_pdweaponWork, &wctx);
	bootProgressUpdate(g_WeaponDataCount, g_WeaponDataCount);

	s32 written = SDL_AtomicGet(&wctx.written);
	s32 skipped = SDL_AtomicGet(&wctx.skipped);
	s32 failed  = SDL_AtomicGet(&wctx.failed);

	sysLogPrintf(LOG_NOTE,
		"romextract pdweapon: written=%d skipped=%d failed=%d total=%d",
		written, skipped, failed, g_WeaponDataCount);

	if (failed == 0) {
		romExtractPdFastCacheWrite(PDWEAPON_FAST_CACHE_KIND,
			weapons_dir, ".pdweapon");
	}

	return written;
}
