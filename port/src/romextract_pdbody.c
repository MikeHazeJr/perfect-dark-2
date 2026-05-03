/**
 * romextract_pdbody.c -- Catalog universality pivot Step 2 (2026-05-03).
 *
 * Walks the loader_pool body pool and emits one .pdbody JSON file
 * per registered body at data/<romid>/bodies/<id>.pdbody.
 *
 * Schema lock-down: context/designs/catalog/universality-pivot-schemas.md
 * Section 2.3 (.pdbody).
 *
 * Cross-reference convention for Step 2: `mesh` and `hand` fields
 * preserve the original FILE_* enum strings (resolved via reverse
 * lookup against loader_enum_reverse.c). Step 4 (universal loader) will
 * swap these to catalog IDs once the directory walker is minting the
 * universal mapping. The Step 2 emit format is therefore intermediate
 * and identical to the per-record envelope content; this is intentional so
 * the parity check at Step 2 is clean.
 *
 * Bodies-only fields preserved per audit: canvaryheight (Skedar's
 * per-chr height variance), handfilenum (first-person hand model;
 * B-275 dependency), unk00_01 == 1 sentinel for integrated-head bodies
 * (Skedar / Dr Caroll / EyeSpy; bodyAllocateModel warning gate at
 * body.c:417).
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
#include "loader_enum_reverse.h"
#include "romextract_pd.h"
#include "system.h"
#include "bodydata_authored.h"

/* Convert "base:dark_combat" -> "base_dark_combat". */
static void s_idToFilename(const char *id, char *out, size_t n)
{
	if (!id || !out || n == 0) { if (out && n) out[0] = '\0'; return; }
	size_t i;
	for (i = 0; i + 1 < n && id[i]; i++) {
		out[i] = (id[i] == ':') ? '_' : id[i];
	}
	out[i] = '\0';
}

/* Emit one .pdbody. Returns 1 written, 0 skipped, -1 failed. */
static s32 s_emitOneBody(const body_authored_record_t *b,
                          const char *out_dir, s32 force_rewrite)
{
	const char *catalog_id = b->catalog_id;
	if (!catalog_id || !catalog_id[0]) return 0;

	char filename[128];
	s_idToFilename(catalog_id, filename, sizeof(filename));

	char relpath[FS_MAXPATH];
	snprintf(relpath, sizeof(relpath), "%s/%s.pdbody", out_dir, filename);

	if (!force_rewrite && fsFileSize(relpath) > 0) return 0;

	FILE *fp = fsFileOpenWrite(relpath);
	if (!fp) {
		sysLoudFailf("EXTRACT.PDBODY",
			"fsFileOpenWrite failed for \"%s\"", relpath);
		return -1;
	}

	const char *type_str = loaderEnumNameForHeadbodyType(b->type);
	const char *mesh_str = loaderEnumNameForFileEnum(b->filenum);
	const char *hand_str = loaderEnumNameForFileEnum(b->handfilenum);

	fputs("{\n", fp);
	fputs("  \"pd_kind\": \"body\",\n", fp);
	fputs("  \"pd_schema_version\": 1,\n", fp);
	fprintf(fp, "  \"id\": \"%s\",\n", catalog_id);
	fprintf(fp, "  \"bodynum\": %d,\n", (s32)b->bodynum);
	fprintf(fp, "  \"ismale\": %u,\n", (unsigned)b->ismale);
	fprintf(fp, "  \"unk00_01\": %u,\n", (unsigned)b->unk00_01);
	fprintf(fp, "  \"canvaryheight\": %u,\n", (unsigned)b->canvaryheight);
	if (type_str) fprintf(fp, "  \"type\": \"%s\",\n", type_str);
	else          fprintf(fp, "  \"type\": %u,\n", (unsigned)b->type);
	fprintf(fp, "  \"height\": %u,\n", (unsigned)b->height);
	if (mesh_str) fprintf(fp, "  \"mesh\": \"%s\",\n", mesh_str);
	else          fprintf(fp, "  \"mesh\": %u,\n", (unsigned)b->filenum);
	fprintf(fp, "  \"scale\": %.7g,\n",    (double)b->scale);
	fprintf(fp, "  \"animscale\": %.7g,\n",(double)b->animscale);
	if (b->handfilenum != 0) {
		if (hand_str) fprintf(fp, "  \"hand\": \"%s\"\n", hand_str);
		else          fprintf(fp, "  \"hand\": %u\n", (unsigned)b->handfilenum);
	} else {
		fputs("  \"hand\": null\n", fp);
	}
	fputs("}\n", fp);
	fclose(fp);
	return 1;
}

s32 romExtractAllPdbody(s32 force_rewrite)
{
	/* BYOR completion (2026-05-03): walks g_BodyData[] from the
	 * authoring source-of-truth (port/src/bodydata_authored.c). */

	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDBODY",
			"fsDataDirEnsure failed; cannot create output dir");
		return -1;
	}

	char dataDirBuf[FS_MAXPATH + 1];
	char bodies_dir[FS_MAXPATH];
	snprintf(bodies_dir, sizeof(bodies_dir), "%s/bodies",
		fsDataDir(dataDirBuf, sizeof(dataDirBuf)));
	if (!fsCreateDir(bodies_dir)) {
		sysLoudFailf("EXTRACT.PDBODY",
			"fsCreateDir(\"%s\") failed", bodies_dir);
		return -1;
	}

	s32 written = 0;
	s32 skipped = 0;
	s32 failed = 0;

	for (s32 i = 0; i < g_BodyDataCount; i++) {
		s32 r = s_emitOneBody(&g_BodyData[i], bodies_dir, force_rewrite);
		if (r > 0)       written++;
		else if (r == 0) skipped++;
		else             failed++;
	}

	sysLogPrintf(LOG_NOTE,
		"romextract pdbody: written=%d skipped=%d failed=%d total=%d",
		written, skipped, failed, g_BodyDataCount);

	return written;
}
