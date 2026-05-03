/**
 * romextract_pdanim.c -- Catalog universality pivot Step 1 (2026-05-02).
 *
 * Walks the loader_pool animation pool and emits one .pdanim JSON
 * file per registered weapon animation at
 * data/<romid>/animations/<id>.pdanim.
 *
 * Each .pdanim carries category="weapon_animation" per universality-
 * pivot-schemas.md Section 2.6. Character animations (the segs/
 * animations.bin lump) become Step 3a work; this emitter handles only
 * the gunscript opcode arrays loaded from the per-asset envelope.
 *
 * Per Mike's Q-3 ruling (2026-05-02): character anims are required
 * within catalog scope but ship as a follow-up slice. The .pdanim
 * parser introduced here will accept the `category` field cleanly so
 * Step 3a's character-anim emitter can ride the same schema.
 */

#include <stdio.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "data.h"
#include "types.h"
#include "constants.h"
#include "fs.h"
#include "loader_enum_reverse.h"
#include "romextract_pd.h"
#include "system.h"
#include "animdata_authored.h"

/* Map struct guncmd::type to mnemonic + arg-format hint. Mirrors the
 * decoder in loader_pool.c::decodeOpcode so that round-trip parity
 * holds (Mike's Q-5 ruling: parity active during Step 1 to validate
 * the .pdwpn / .pdanim emit). */
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

static void s_writeStrEscaped(FILE *fp, const char *s)
{
	fputc('"', fp);
	if (s) {
		for (; *s; s++) {
			unsigned char c = (unsigned char)*s;
			if      (c == '"')  fputs("\\\"", fp);
			else if (c == '\\') fputs("\\\\", fp);
			else                fputc(c, fp);
		}
	}
	fputc('"', fp);
}

static void s_emitOpcode(FILE *fp, const struct guncmd *cmd, s32 last)
{
	const opcode_meta_t *m = s_lookupOpMeta(cmd->type);
	const char *mnem = m ? m->mnem : "unknown";
	opfmt_e fmt = m ? m->fmt : OPFMT_NONE;

	fputs("    [", fp);
	s_writeStrEscaped(fp, mnem);

	switch (fmt) {
	case OPFMT_NONE:
		break;
	case OPFMT_U16:
		fprintf(fp, ", %u", (unsigned)cmd->unk02);
		break;
	case OPFMT_U16_INT:
		fprintf(fp, ", %u, %lld",
			(unsigned)cmd->unk02, (long long)cmd->unk04);
		break;
	case OPFMT_U16_ANIMNAME: {
		const char *aname = s_animNameForCmds(
			(const struct guncmd *)(intptr_t)cmd->unk04);
		fprintf(fp, ", %u, ", (unsigned)cmd->unk02);
		s_writeStrEscaped(fp, aname ? aname : "");
		break;
	}
	case OPFMT_U16_SFX: {
		const char *sname = loaderEnumNameForSfxEnum((s32)cmd->unk04);
		fprintf(fp, ", %u, ", (unsigned)cmd->unk02);
		if (sname) s_writeStrEscaped(fp, sname);
		else       fprintf(fp, "%lld", (long long)cmd->unk04);
		break;
	}
	case OPFMT_PLAYANIM: {
		const char *aname = loaderEnumNameForAnimEnum((s32)cmd->unk02);
		s32 direction = (s32)((cmd->unk04 >> 16) & 0xFFFF);
		s32 speed     = (s32)(cmd->unk04 & 0xFFFF);
		fputs(", ", fp);
		if (aname) s_writeStrEscaped(fp, aname);
		else       fprintf(fp, "%u", (unsigned)cmd->unk02);
		fprintf(fp, ", %d, %d", direction, speed);
		break;
	}
	case OPFMT_REPEATFULL: {
		s32 dontloop = (s32)((cmd->unk04 >> 16) & 0xFFFF);
		s32 gototrigger = (s32)(cmd->unk04 & 0xFFFF);
		fprintf(fp, ", %u, %d, %d",
			(unsigned)cmd->unk02, dontloop, gototrigger);
		break;
	}
	case OPFMT_INCLUDE: {
		const char *aname = s_animNameForCmds(
			(const struct guncmd *)(intptr_t)cmd->unk04);
		fprintf(fp, ", %u, ", (unsigned)cmd->unk01);
		s_writeStrEscaped(fp, aname ? aname : "");
		break;
	}
	case OPFMT_SETSPEED:
		fprintf(fp, ", %u, %lld",
			(unsigned)cmd->unk02, (long long)cmd->unk04);
		break;
	}

	fputs(last ? "]\n" : "],\n", fp);
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

	if (!force_rewrite && fsFileSize(relpath) > 0) return 0;

	FILE *fp = fsFileOpenWrite(relpath);
	if (!fp) {
		sysLoudFailf("EXTRACT.PDANIM",
			"fsFileOpenWrite failed for \"%s\"", relpath);
		return -1;
	}

	fputs("{\n", fp);
	fputs("  \"pd_kind\": \"animation\",\n", fp);
	fputs("  \"pd_schema_version\": 1,\n", fp);
	fprintf(fp, "  \"id\": \"%s\",\n", catalog_id);
	fputs("  \"category\": \"weapon_animation\",\n", fp);
	fputs("  \"opcodes\": [\n", fp);
	for (s32 i = 0; i < cmd_count; i++) {
		s_emitOpcode(fp, &cmds[i], i == cmd_count - 1);
	}
	fputs("  ]\n", fp);
	fputs("}\n", fp);
	fclose(fp);
	return 1;
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

	s32 written = 0;
	s32 skipped = 0;
	s32 failed = 0;

	for (s32 i = 0; i < g_AnimDataCount; i++) {
		s32 r = s_emitOneAnim(i, anims_dir, force_rewrite);
		if (r > 0)       written++;
		else if (r == 0) skipped++;
		else             failed++;
	}

	sysLogPrintf(LOG_NOTE,
		"romextract pdanim: written=%d skipped=%d failed=%d total=%d",
		written, skipped, failed, g_AnimDataCount);

	return written;
}
