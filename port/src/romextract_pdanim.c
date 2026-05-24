/**
 * romextract_pdanim.c -- Catalog universality pivot Step 1 (2026-05-02).
 *
 * Walks the loader_pool animation pool and emits one .pdanim ZIP
 * compound per registered weapon animation at
 * data/<romid>/animations/<id>.pdanim.
 *
 * Each .pdanim carries category="weapon_animation" per universality-
 * pivot-schemas.md Section 2.6. The archive carries animation.ini,
 * _meta/manifest.json, and opcodes.json so weapon/inventory animations use
 * the same editable compound-asset contract as the other typed assets.
 *
 * Per Mike's Q-3 ruling (2026-05-02): character anims are required
 * within catalog scope but ship as a follow-up slice. The .pdanim
 * parser introduced here will accept the `category` field cleanly so
 * Step 3a's character-anim emitter can ride the same schema.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <SDL.h>
#include <PR/ultratypes.h>

#include "boot_pool.h"
#include "boot_progress.h"
#include "data.h"
#include "types.h"
#include "constants.h"
#include "fs.h"
#include "loader_enum_reverse.h"
#include "modarchive.h"
#include "romextract_pd.h"
#include "system.h"
#include "animdata_authored.h"

/* Map struct guncmd::type to mnemonic + arg-format hint. Mirrors the
 * decoder in loader_pool.c::decodeOpcode so that round-trip parity
 * holds (Mike's Q-5 ruling: parity active during Step 1 to validate
 * the .pdweapon / .pdanim emit). */
typedef enum {
	OPFMT_NONE,         /* no args (end) */
	OPFMT_U16,          /* one u16 arg from unk02 */
	OPFMT_U16_INT,      /* unk02 + intptr_t unk04 */
	OPFMT_U16_ANIMNAME, /* unk02 + anim ptr resolved to name */
	OPFMT_U16_SFX,      /* unk02 + sfx enum from unk04 */
	OPFMT_PLAYANIM,     /* anim enum unk02, direction (high16 of unk04), speed (low16) */
	OPFMT_REPEATFULL,   /* unk02, dontloop (high16), gototrigger (low16) */
	OPFMT_INCLUDE,      /* unk01, anim ptr resolved to name */
	OPFMT_SETSPEED,     /* unk02, intptr unk04 */
} opfmt_e;

typedef struct { s32 type; const char *mnem; opfmt_e fmt; } opcode_meta_t;

static const opcode_meta_t k_OpcodeMeta[] = {
	{ GUNCMD_END,               "end",               OPFMT_NONE },
	{ GUNCMD_SHOWPART,          "showpart",          OPFMT_U16_INT },
	{ GUNCMD_HIDEPART,          "hidepart",          OPFMT_U16_INT },
	{ GUNCMD_WAITFORZRELEASED,  "waitforzreleased",  OPFMT_U16 },
	{ GUNCMD_WAITTIME,          "waittime",          OPFMT_U16_INT },
	{ GUNCMD_PLAYSOUND,         "playsound",         OPFMT_U16_SFX },
	{ GUNCMD_INCLUDE,           "include",           OPFMT_INCLUDE },
	{ GUNCMD_RANDOM,            "random",            OPFMT_U16_ANIMNAME },
	{ GUNCMD_REPEATUNTILFULL,   "repeatuntilfull",   OPFMT_REPEATFULL },
	{ GUNCMD_POPOUTSACKOFPILLS, "popoutsackofpills", OPFMT_U16 },
	{ GUNCMD_PLAYANIMATION,     "playanimation",     OPFMT_PLAYANIM },
	{ GUNCMD_SETSOUNDSPEED,     "setsoundspeed",     OPFMT_SETSPEED },
};

static const opcode_meta_t *s_lookupOpMeta(s32 type)
{
	for (size_t i = 0; i < sizeof(k_OpcodeMeta) / sizeof(k_OpcodeMeta[0]); i++) {
		if (k_OpcodeMeta[i].type == type) return &k_OpcodeMeta[i];
	}
	return NULL;
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

typedef struct {
	char *data;
	u32   len;
	u32   cap;
} pdanim_textbuf_t;

static void s_textbufFree(pdanim_textbuf_t *b)
{
	if (b->data) free(b->data);
	memset(b, 0, sizeof(*b));
}

static s32 s_textbufReserve(pdanim_textbuf_t *b, u32 extra)
{
	if (extra > 0xffffffffu - b->len) return -1;
	u32 need = b->len + extra + 1;
	if (need <= b->cap) return 0;
	u32 cap = b->cap ? b->cap : 1024;
	while (cap < need) {
		if (cap > 0x80000000u) return -1;
		cap *= 2;
	}
	char *p = (char *)realloc(b->data, cap);
	if (!p) return -1;
	b->data = p;
	b->cap = cap;
	return 0;
}

static s32 s_textbufAppend(pdanim_textbuf_t *b, const char *s)
{
	u32 n = (u32)strlen(s);
	if (s_textbufReserve(b, n) != 0) return -1;
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = '\0';
	return 0;
}

static s32 s_textbufAppendf(pdanim_textbuf_t *b, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	va_list ap2;
	va_copy(ap2, ap);
	int need = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (need < 0) {
		va_end(ap2);
		return -1;
	}
	if (s_textbufReserve(b, (u32)need) != 0) {
		va_end(ap2);
		return -1;
	}
	int wrote = vsnprintf(b->data + b->len, (size_t)need + 1, fmt, ap2);
	va_end(ap2);
	if (wrote != need) return -1;
	b->len += (u32)need;
	return 0;
}

static s32 s_textbufAppendJsonString(pdanim_textbuf_t *b, const char *s)
{
	if (s_textbufAppend(b, "\"") != 0) return -1;
	if (s) {
		for (; *s; s++) {
			unsigned char c = (unsigned char)*s;
			if (c == '"') {
				if (s_textbufAppend(b, "\\\"") != 0) return -1;
			} else if (c == '\\') {
				if (s_textbufAppend(b, "\\\\") != 0) return -1;
			} else {
				if (s_textbufReserve(b, 1) != 0) return -1;
				b->data[b->len++] = (char)c;
				b->data[b->len] = '\0';
			}
		}
	}
	return s_textbufAppend(b, "\"");
}

static s32 s_emitOpcode(pdanim_textbuf_t *out, const struct guncmd *cmd, s32 last)
{
	const opcode_meta_t *m = s_lookupOpMeta(cmd->type);
	const char *mnem = m ? m->mnem : "unknown";
	opfmt_e fmt = m ? m->fmt : OPFMT_NONE;

	if (s_textbufAppend(out, "    [") != 0) return -1;
	if (s_textbufAppendJsonString(out, mnem) != 0) return -1;

	switch (fmt) {
	case OPFMT_NONE:
		break;
	case OPFMT_U16:
		if (s_textbufAppendf(out, ", %u", (unsigned)cmd->unk02) != 0) return -1;
		break;
	case OPFMT_U16_INT:
		if (s_textbufAppendf(out, ", %u, %lld",
			(unsigned)cmd->unk02, (long long)cmd->unk04) != 0) return -1;
		break;
	case OPFMT_U16_ANIMNAME: {
		const char *aname = s_animNameForCmds(
			(const struct guncmd *)(intptr_t)cmd->unk04);
		if (s_textbufAppendf(out, ", %u, ", (unsigned)cmd->unk02) != 0) return -1;
		if (s_textbufAppendJsonString(out, aname ? aname : "") != 0) return -1;
		break;
	}
	case OPFMT_U16_SFX: {
		const char *sname = loaderEnumNameForSfxEnum((s32)cmd->unk04);
		if (s_textbufAppendf(out, ", %u, ", (unsigned)cmd->unk02) != 0) return -1;
		if (sname) {
			if (s_textbufAppendJsonString(out, sname) != 0) return -1;
		} else if (s_textbufAppendf(out, "%lld", (long long)cmd->unk04) != 0) {
			return -1;
		}
		break;
	}
	case OPFMT_PLAYANIM: {
		const char *aname = loaderEnumNameForAnimEnum((s32)cmd->unk02);
		s32 direction = (s32)((cmd->unk04 >> 16) & 0xFFFF);
		s32 speed     = (s32)(cmd->unk04 & 0xFFFF);
		if (s_textbufAppend(out, ", ") != 0) return -1;
		if (aname) {
			if (s_textbufAppendJsonString(out, aname) != 0) return -1;
		} else if (s_textbufAppendf(out, "%u", (unsigned)cmd->unk02) != 0) {
			return -1;
		}
		if (s_textbufAppendf(out, ", %d, %d", direction, speed) != 0) return -1;
		break;
	}
	case OPFMT_REPEATFULL: {
		s32 dontloop = (s32)((cmd->unk04 >> 16) & 0xFFFF);
		s32 gototrigger = (s32)(cmd->unk04 & 0xFFFF);
		if (s_textbufAppendf(out, ", %u, %d, %d",
			(unsigned)cmd->unk02, dontloop, gototrigger) != 0) return -1;
		break;
	}
	case OPFMT_INCLUDE: {
		const char *aname = s_animNameForCmds(
			(const struct guncmd *)(intptr_t)cmd->unk04);
		if (s_textbufAppendf(out, ", %u, ", (unsigned)cmd->unk01) != 0) return -1;
		if (s_textbufAppendJsonString(out, aname ? aname : "") != 0) return -1;
		break;
	}
	case OPFMT_SETSPEED:
		if (s_textbufAppendf(out, ", %u, %lld",
			(unsigned)cmd->unk02, (long long)cmd->unk04) != 0) return -1;
		break;
	}

	return s_textbufAppend(out, last ? "]\n" : "],\n");
}

/* Walk a guncmd[] array until GUNCMD_END to count opcodes. */
static s32 s_countOpcodes(const struct guncmd *cmds)
{
	s32 n = 0;
	if (!cmds) return 0;
	while (n < 4096) {
		n++;
		if (cmds[n - 1].type == GUNCMD_END) break;
	}
	return n;
}

static s32 s_existingArchiveHasAnimPayloads(const char *relpath)
{
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) return 0;
	mod_archive_t *arc = modArchiveOpen(full);
	if (!arc) return 0;
	s32 ok = modArchiveFindEntry(arc, "animation.ini") >= 0
	      && modArchiveFindEntry(arc, "_meta/manifest.json") >= 0
	      && modArchiveFindEntry(arc, "opcodes.json") >= 0;
	modArchiveClose(arc);
	return ok;
}

static s32 s_buildOpcodeJson(const char *catalog_id, const char *anim_name,
                             const struct guncmd *cmds, s32 cmd_count,
                             s32 include_loader_envelope,
                             pdanim_textbuf_t *out)
{
	if (s_textbufAppend(out, "{\n") != 0) return -1;
	if (include_loader_envelope) {
		if (s_textbufAppend(out, "  \"pd_kind\": \"animation\",\n") != 0) return -1;
		if (s_textbufAppend(out, "  \"pd_schema_version\": 1,\n") != 0) return -1;
		if (s_textbufAppend(out, "  \"id\": ") != 0) return -1;
		if (s_textbufAppendJsonString(out, catalog_id) != 0) return -1;
		if (s_textbufAppend(out, ",\n") != 0) return -1;
		if (s_textbufAppend(out, "  \"category\": \"weapon_animation\",\n") != 0) return -1;
	}
	if (s_textbufAppend(out, "  \"source_format\": \"gunscript_opcodes\",\n") != 0) return -1;
	if (s_textbufAppend(out, "  \"name\": ") != 0) return -1;
	if (s_textbufAppendJsonString(out, anim_name) != 0) return -1;
	if (s_textbufAppend(out, ",\n") != 0) return -1;
	if (s_textbufAppendf(out, "  \"opcode_count\": %d,\n", cmd_count) != 0) return -1;
	if (s_textbufAppend(out, "  \"opcodes\": [\n") != 0) return -1;
	for (s32 i = 0; i < cmd_count; i++) {
		if (s_emitOpcode(out, &cmds[i], i == cmd_count - 1) != 0) return -1;
	}
	if (s_textbufAppend(out, "  ]\n") != 0) return -1;
	return s_textbufAppend(out, "}\n");
}

static s32 s_emitOneAnim(s32 anim_idx, const char *out_dir, s32 force_rewrite)
{
	if (anim_idx < 0 || anim_idx >= g_AnimDataCount) return 0;
	const char *anim_name = g_AnimData[anim_idx].name;
	if (!anim_name || !anim_name[0]) return 0;

	const struct guncmd *cmds = g_AnimData[anim_idx].cmds;
	s32 cmd_count = s_countOpcodes(cmds);
	if (!cmds || cmd_count <= 0) return 0;

	/* Catalog ID convention: animation names already begin with
	 * "invanim_*" so prefixing with "base:" yields a clean catalog ID. */
	char catalog_id[128];
	snprintf(catalog_id, sizeof(catalog_id), "base:%s", anim_name);

	char filename[128];
	snprintf(filename, sizeof(filename), "base_%s", anim_name);

	char relpath[FS_MAXPATH];
	snprintf(relpath, sizeof(relpath), "%s/%s.pdanim", out_dir, filename);

	if (!force_rewrite && s_existingArchiveHasAnimPayloads(relpath)) return 0;

	pdanim_textbuf_t manifest = {0};
	pdanim_textbuf_t opcodes = {0};

	if (s_buildOpcodeJson(catalog_id, anim_name, cmds, cmd_count, true, &manifest) != 0
			|| s_buildOpcodeJson(catalog_id, anim_name, cmds, cmd_count, false, &opcodes) != 0) {
		s_textbufFree(&manifest);
		s_textbufFree(&opcodes);
		sysLoudFailf("EXTRACT.PDANIM",
			"JSON build failed for \"%s\"", relpath);
		return -1;
	}

	char ini_buf[512];
	int ini_len = snprintf(ini_buf, sizeof(ini_buf),
		"[animation]\n"
		"catalog_id = %s\n"
		"category = weapon_animation\n"
		"source_format = gunscript_opcodes\n"
		"opcodes_file = opcodes.json\n"
		"manifest_file = _meta/manifest.json\n"
		"opcode_count = %d\n"
		"source_index = %d\n",
		catalog_id, cmd_count, anim_idx);
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini_buf)) {
		s_textbufFree(&manifest);
		s_textbufFree(&opcodes);
		sysLoudFailf("EXTRACT.PDANIM",
			"animation.ini snprintf truncated for \"%s\"", relpath);
		return -1;
	}

	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) {
		s_textbufFree(&manifest);
		s_textbufFree(&opcodes);
		sysLoudFailf("EXTRACT.PDANIM",
			"fsFullPath failed for \"%s\"", relpath);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(full);
	if (!aw) {
		s_textbufFree(&manifest);
		s_textbufFree(&opcodes);
		sysLoudFailf("EXTRACT.PDANIM",
			"modArchiveBegin failed for \"%s\"", full);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "animation.ini", ini_buf, (u32)ini_len) != 0) {
		sysLoudFailf("EXTRACT.PDANIM",
			"AddFileMem animation.ini failed for \"%s\"", full);
		modArchiveAbort(aw);
		s_textbufFree(&manifest);
		s_textbufFree(&opcodes);
		return -1;
	}
	if (modArchiveAddFileMem(aw, "_meta/manifest.json", manifest.data, manifest.len) != 0) {
		sysLoudFailf("EXTRACT.PDANIM",
			"AddFileMem _meta/manifest.json failed for \"%s\"", full);
		modArchiveAbort(aw);
		s_textbufFree(&manifest);
		s_textbufFree(&opcodes);
		return -1;
	}
	if (modArchiveAddFileMem(aw, "opcodes.json", opcodes.data, opcodes.len) != 0) {
		sysLoudFailf("EXTRACT.PDANIM",
			"AddFileMem opcodes.json failed for \"%s\"", full);
		modArchiveAbort(aw);
		s_textbufFree(&manifest);
		s_textbufFree(&opcodes);
		return -1;
	}

	if (modArchiveFinish(aw) != 0) {
		s_textbufFree(&manifest);
		s_textbufFree(&opcodes);
		sysLoudFailf("EXTRACT.PDANIM",
			"modArchiveFinish failed for \"%s\"", full);
		return -1;
	}

	s_textbufFree(&manifest);
	s_textbufFree(&opcodes);
	return 1;
}

/* Engine Phase 4: per-anim fan-out context. */
typedef struct {
	const char  *anims_dir;
	s32          force_rewrite;
	s32          count;
	SDL_atomic_t written;
	SDL_atomic_t skipped;
	SDL_atomic_t failed;
	SDL_atomic_t processed;
} pdanim_fanout_ctx_t;

static void s_pdanimWork(int i, void *user)
{
	pdanim_fanout_ctx_t *c = (pdanim_fanout_ctx_t *)user;
	if (i < 0 || i >= c->count) return;

	s32 r = s_emitOneAnim(i, c->anims_dir, c->force_rewrite);
	if (r > 0)       SDL_AtomicAdd(&c->written, 1);
	else if (r == 0) SDL_AtomicAdd(&c->skipped, 1);
	else             SDL_AtomicAdd(&c->failed,  1);

	int done = SDL_AtomicAdd(&c->processed, 1) + 1;
	if ((done & 0x07) == 0 || done == c->count) {
		bootProgressUpdate(done, c->count);
	}
}

s32 romExtractAllPdanim(s32 force_rewrite)
{
	/* BYOR completion (2026-05-03): walks g_AnimData[] from the
	 * authoring source-of-truth (port/src/animdata_authored.c). */

	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDANIM", "fsDataDirEnsure failed");
		return -1;
	}

	char dataDirBuf[FS_MAXPATH + 1];
	char anims_dir[FS_MAXPATH];
	snprintf(anims_dir, sizeof(anims_dir), "%s/animations",
		fsDataDir(dataDirBuf, sizeof(dataDirBuf)));
	if (!fsCreateDir(anims_dir)) {
		sysLoudFailf("EXTRACT.PDANIM",
			"fsCreateDir(\"%s\") failed", anims_dir);
		return -1;
	}

	pdanim_fanout_ctx_t actx;
	memset(&actx, 0, sizeof(actx));
	actx.anims_dir     = anims_dir;
	actx.force_rewrite = force_rewrite;
	actx.count         = g_AnimDataCount;
	SDL_AtomicSet(&actx.written,   0);
	SDL_AtomicSet(&actx.skipped,   0);
	SDL_AtomicSet(&actx.failed,    0);
	SDL_AtomicSet(&actx.processed, 0);

	bootProgressUpdate(0, g_AnimDataCount);
	bootPoolForRangeBlocking(0, g_AnimDataCount, s_pdanimWork, &actx);
	bootProgressUpdate(g_AnimDataCount, g_AnimDataCount);

	s32 written = SDL_AtomicGet(&actx.written);
	s32 skipped = SDL_AtomicGet(&actx.skipped);
	s32 failed  = SDL_AtomicGet(&actx.failed);

	sysLogPrintf(LOG_NOTE,
		"romextract pdanim: written=%d skipped=%d failed=%d total=%d",
		written, skipped, failed, g_AnimDataCount);

	return written;
}
