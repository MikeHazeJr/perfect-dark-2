/**
 * romextract_pdhead.c -- Catalog universality pivot Step 2 (2026-05-03).
 *
 * Walks the loader_pool head pool and emits one .pdhead JSON file
 * per registered head at data/<romid>/heads/<id>.pdhead.
 *
 * Schema lock-down: context/designs/catalog/universality-pivot-schemas.md
 * Section 2.2 (.pdhead).
 *
 * Cross-reference convention for Step 2: the `mesh` field preserves the
 * original FILE_* enum string (resolved via reverse lookup against
 * loader_enum_reverse.c). Step 4 (universal loader) will swap this to
 * a catalog ID once the directory walker is minting the universal
 * mapping. The Step 2 emit format is therefore intermediate and
 * identical to the per-record envelope content; this is intentional so the
 * parity check at Step 2 is clean.
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
#include "headdata_authored.h"

/* Convert a catalog ID like "base:head_carrington" to a filename slug
 * "base_head_carrington". Caller buffer must hold at least 64 bytes. */
static void s_idToFilename(const char *id, char *out, size_t n)
{
	if (!id || !out || n == 0) { if (out && n) out[0] = '\0'; return; }
	size_t i;
	for (i = 0; i + 1 < n && id[i]; i++) {
		out[i] = (id[i] == ':') ? '_' : id[i];
	}
	out[i] = '\0';
}

/* Emit one .pdhead. Returns 1 written, 0 skipped, -1 failed. */
static s32 s_emitOneHead(const head_authored_record_t *h,
                          const char *out_dir, s32 force_rewrite)
{
	const char *catalog_id = h->catalog_id;
	if (!catalog_id || !catalog_id[0]) return 0;

	char filename[128];
	s_idToFilename(catalog_id, filename, sizeof(filename));

	char relpath[FS_MAXPATH];
	snprintf(relpath, sizeof(relpath), "%s/%s.pdhead", out_dir, filename);

	if (!force_rewrite && fsFileSize(relpath) > 0) return 0;

	FILE *fp = fsFileOpenWrite(relpath);
	if (!fp) {
		sysLoudFailf("EXTRACT.PDHEAD",
			"fsFileOpenWrite failed for \"%s\"", relpath);
		return -1;
	}

	const char *type_str = loaderEnumNameForHeadbodyType(h->type);
	const char *file_str = loaderEnumNameForFileEnum(h->filenum);

	fputs("{\n", fp);
	fputs("  \"pd_kind\": \"head\",\n", fp);
	fputs("  \"pd_schema_version\": 1,\n", fp);
	fprintf(fp, "  \"id\": \"%s\",\n", catalog_id);
	fprintf(fp, "  \"headnum\": %d,\n", (s32)h->headnum);
	fprintf(fp, "  \"ismale\": %u,\n", (unsigned)h->ismale);
	fprintf(fp, "  \"unk00_01\": %u,\n", (unsigned)h->unk00_01);
	if (type_str) fprintf(fp, "  \"type\": \"%s\",\n", type_str);
	else          fprintf(fp, "  \"type\": %u,\n", (unsigned)h->type);
	fprintf(fp, "  \"height\": %u,\n", (unsigned)h->height);
	if (file_str) fprintf(fp, "  \"mesh\": \"%s\",\n", file_str);
	else          fprintf(fp, "  \"mesh\": %u,\n", (unsigned)h->filenum);
	fprintf(fp, "  \"scale\": %.7g,\n",     (double)h->scale);
	fprintf(fp, "  \"animscale\": %.7g\n",  (double)h->animscale);
	fputs("}\n", fp);
	fclose(fp);
	return 1;
}

s32 romExtractAllPdhead(s32 force_rewrite)
{
	/* BYOR completion (2026-05-03): walks g_HeadData[] from the
	 * authoring source-of-truth (port/src/headdata_authored.c). */

	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDHEAD",
			"fsDataDirEnsure failed; cannot create output dir");
		return -1;
	}

	char heads_dir[FS_MAXPATH];
	snprintf(heads_dir, sizeof(heads_dir), "%s/heads", fsDataDir());
	if (!fsCreateDir(heads_dir)) {
		sysLoudFailf("EXTRACT.PDHEAD",
			"fsCreateDir(\"%s\") failed", heads_dir);
		return -1;
	}

	s32 written = 0;
	s32 skipped = 0;
	s32 failed = 0;

	for (s32 i = 0; i < g_HeadDataCount; i++) {
		s32 r = s_emitOneHead(&g_HeadData[i], heads_dir, force_rewrite);
		if (r > 0)       written++;
		else if (r == 0) skipped++;
		else             failed++;
	}

	sysLogPrintf(LOG_NOTE,
		"romextract pdhead: written=%d skipped=%d failed=%d total=%d",
		written, skipped, failed, g_HeadDataCount);

	return written;
}
