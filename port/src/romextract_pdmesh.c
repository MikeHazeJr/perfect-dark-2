/**
 * romextract_pdmesh.c -- Catalog universality pivot Step 1 (2026-05-02).
 *
 * Walks the unique set of mesh references from the loader_pool weapon
 * pool (hi_model + lo_model fields) and emits one .pdmesh ZIP compound
 * per unique mesh at data/<romid>/meshes/<id>.pdmesh.
 *
 * Compound layout per universality-pivot-schemas.md Section 2.5:
 *   manifest.json       envelope + provenance
 *   model.obj           Wavefront OBJ converted from model display lists
 *   model.mtl           material stub for OBJ tooling
 *   *.sha256            content sidecars
 *
 * Reuses port/src/modarchive.c writer subset for ZIP atomic writes.
 *
 * Step 1 cross-reference convention: emits source_filenum_symbol with
 * the FILE_* enum string (provenance hint) and the catalog ID is
 * synthesized from the symbol (e.g. FILE_GFALCON2 -> base:falcon2_hi).
 * Step 4 universal loader can refine the ID minting.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <SDL.h>
#include <PR/gbi.h>
#include <PR/ultratypes.h>

#include "boot_pool.h"
#include "boot_progress.h"
#include "data.h"
#include "types.h"
#include "constants.h"
#include "fs.h"
#include "loader_enum_reverse.h"
#include "modarchive.h"
#include "romdata.h"
#include "romextract.h"
#include "romextract_pd.h"
#include "sha256.h"
#include "system.h"
#include "weapondata_authored.h"
#include "headdata_authored.h"
#include "bodydata_authored.h"
#include "lib/model.h"

/* Track filenums already emitted to avoid duplicate work when multiple
 * weapons / heads / bodies share a mesh. Cap covers ~86 weapons * 2
 * (hi + lo) + 84 head meshes + 68 body meshes + 68 hand meshes plus
 * dedup headroom. */
#define ROMEXTRACT_PDMESH_SEEN_CAP 512
static u16 s_SeenFilenums[ROMEXTRACT_PDMESH_SEEN_CAP];
static s32 s_SeenCount;

static s32 s_alreadySeen(u16 filenum)
{
	for (s32 i = 0; i < s_SeenCount; i++) {
		if (s_SeenFilenums[i] == filenum) return 1;
	}
	return 0;
}

static void s_markSeen(u16 filenum)
{
	if (s_SeenCount < ROMEXTRACT_PDMESH_SEEN_CAP) {
		s_SeenFilenums[s_SeenCount++] = filenum;
	}
}

static s32 s_existingArchiveHasEntry(const char *relpath, const char *entry)
{
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) return 0;
	mod_archive_t *arc = modArchiveOpen(full);
	if (!arc) return 0;
	s32 has_entry = modArchiveFindEntry(arc, entry) >= 0;
	modArchiveClose(arc);
	return has_entry;
}

typedef struct {
	char *data;
	u32   len;
	u32   cap;
} pdmesh_textbuf_t;

static void s_textbufFree(pdmesh_textbuf_t *b)
{
	if (b->data) free(b->data);
	memset(b, 0, sizeof(*b));
}

static s32 s_textbufReserve(pdmesh_textbuf_t *b, u32 extra)
{
	if (extra > 0xffffffffu - b->len) return -1;
	u32 need = b->len + extra + 1;
	if (need <= b->cap) return 0;
	u32 cap = b->cap ? b->cap : 8192;
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

static s32 s_textbufAppend(pdmesh_textbuf_t *b, const char *s)
{
	u32 n = (u32)strlen(s);
	if (s_textbufReserve(b, n) != 0) return -1;
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = '\0';
	return 0;
}

static s32 s_textbufAppendf(pdmesh_textbuf_t *b, const char *fmt, ...)
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

typedef struct {
	const u8       *base;
	u32             size;
	pdmesh_textbuf_t *obj;
	u32             next_index;
	u32             triangle_count;
	u32             gdl_count;
} pdmesh_obj_export_t;

static s32 s_ptrInModel(const pdmesh_obj_export_t *ctx, const void *ptr,
                        size_t bytes)
{
	uintptr_t p = (uintptr_t)ptr;
	uintptr_t b = (uintptr_t)ctx->base;
	return p >= b && p + bytes >= p && p + bytes <= b + ctx->size;
}

static Gfx *s_resolveGdlPtr(const pdmesh_obj_export_t *ctx, Gfx *raw)
{
	if (!raw) return NULL;
	if (s_ptrInModel(ctx, raw, sizeof(Gfx))) return raw;
	u32 off = (u32)(UNSEGADDR(raw) & 0x00ffffffu);
	if (off + sizeof(Gfx) <= ctx->size) return (Gfx *)(ctx->base + off);
	return NULL;
}

static s32 s_objEmitTri(pdmesh_obj_export_t *ctx,
                        const Vtx *a, const Vtx *b, const Vtx *c)
{
	if (!a || !b || !c) return 0;
	u32 i0 = ctx->next_index++;
	u32 i1 = ctx->next_index++;
	u32 i2 = ctx->next_index++;
	if (s_textbufAppendf(ctx->obj,
			"v %d %d %d\n"
			"v %d %d %d\n"
			"v %d %d %d\n"
			"vt %.6f %.6f\n"
			"vt %.6f %.6f\n"
			"vt %.6f %.6f\n"
			"f %u/%u %u/%u %u/%u\n",
			(int)a->x, (int)a->y, (int)a->z,
			(int)b->x, (int)b->y, (int)b->z,
			(int)c->x, (int)c->y, (int)c->z,
			(double)a->s / 32.0, 1.0 - ((double)a->t / 32.0),
			(double)b->s / 32.0, 1.0 - ((double)b->t / 32.0),
			(double)c->s / 32.0, 1.0 - ((double)c->t / 32.0),
			(unsigned)i0, (unsigned)i0,
			(unsigned)i1, (unsigned)i1,
			(unsigned)i2, (unsigned)i2) != 0) {
		return -1;
	}
	ctx->triangle_count++;
	return 0;
}

static s32 s_exportGdlToObj(pdmesh_obj_export_t *ctx, Gfx *raw_gdl,
                            Vtx *vbuf, s32 numverts, s32 depth)
{
	if (!raw_gdl || !vbuf || numverts <= 0 || depth > 16) return 0;
	Gfx *gdl = s_resolveGdlPtr(ctx, raw_gdl);
	if (!gdl) return 0;
	if (!s_ptrInModel(ctx, vbuf, (size_t)numverts * sizeof(Vtx))) return 0;

	ctx->gdl_count++;
	const Vtx *slots[64];
	memset(slots, 0, sizeof(slots));
	s32 numslots = 0;

	for (s32 cmdidx = 0; cmdidx < 8192; cmdidx++) {
		if (!s_ptrInModel(ctx, &gdl[cmdidx], sizeof(Gfx))) break;
		u32 w0 = gdl[cmdidx].words.w0;
		u32 w1 = gdl[cmdidx].words.w1;
		u8 cmd = (u8)((w0 >> 24) & 0xff);

		if (cmd == (u8)G_ENDDL) break;

		if (cmd == (u8)G_DL) {
			Gfx *child = (Gfx *)(uintptr_t)w1;
			if (s_exportGdlToObj(ctx, child, vbuf, numverts, depth + 1) != 0) {
				return -1;
			}
			continue;
		}

		if (cmd == (u8)G_VTX) {
			s32 n = ((w0 >> 4) & 0xf) + 1;
			s32 v0 = (w0 & 0xf);
			uintptr_t src = (uintptr_t)w1;
			uintptr_t vstart = (uintptr_t)vbuf;
			uintptr_t vend = vstart + (size_t)numverts * sizeof(Vtx);
			s32 srcidx = -1;

			if (src >= vstart && src < vend) {
				srcidx = (s32)((src - vstart) / sizeof(Vtx));
			} else {
				u32 off = (u32)(UNSEGADDR(w1) & 0x00ffffffu);
				if ((off % sizeof(Vtx)) == 0) srcidx = (s32)(off / sizeof(Vtx));
			}

			if (srcidx >= 0) {
				for (s32 i = 0; i < n && (v0 + i) < 64; i++) {
					s32 vi = srcidx + i;
					if (vi >= 0 && vi < numverts) slots[v0 + i] = &vbuf[vi];
				}
				if (v0 + n > numslots) numslots = v0 + n;
			}
			continue;
		}

		if (cmd == (u8)G_TRI1) {
			s32 i0 = ((w1 >> 16) & 0xff) / 10;
			s32 i1 = ((w1 >> 8)  & 0xff) / 10;
			s32 i2 = ((w1 >> 0)  & 0xff) / 10;
			if (i0 < numslots && i1 < numslots && i2 < numslots) {
				if (s_objEmitTri(ctx, slots[i0], slots[i1], slots[i2]) != 0) return -1;
			}
			continue;
		}

		if (cmd == (u8)G_TRI4) {
			s32 idx[4][3] = {
				{ gdl[cmdidx].tri4.x1, gdl[cmdidx].tri4.y1, gdl[cmdidx].tri4.z1 },
				{ gdl[cmdidx].tri4.x2, gdl[cmdidx].tri4.y2, gdl[cmdidx].tri4.z2 },
				{ gdl[cmdidx].tri4.x3, gdl[cmdidx].tri4.y3, gdl[cmdidx].tri4.z3 },
				{ gdl[cmdidx].tri4.x4, gdl[cmdidx].tri4.y4, gdl[cmdidx].tri4.z4 },
			};
			for (s32 ti = 0; ti < 4; ti++) {
				s32 i0 = idx[ti][0];
				s32 i1 = idx[ti][1];
				s32 i2 = idx[ti][2];
				if (i0 == i1 && i1 == i2) continue;
				if (i0 < numslots && i1 < numslots && i2 < numslots) {
					if (s_objEmitTri(ctx, slots[i0], slots[i1], slots[i2]) != 0) return -1;
				}
			}
		}
	}
	return 0;
}

static s32 s_exportNodeObj(pdmesh_obj_export_t *ctx, struct modelnode *node)
{
	if (!node || !s_ptrInModel(ctx, node, sizeof(*node))) return 0;

	u32 type = node->type & 0xff;
	if (type == MODELNODETYPE_DL && node->rodata &&
	    s_ptrInModel(ctx, node->rodata, sizeof(struct modelrodata_dl))) {
		struct modelrodata_dl *dl = &node->rodata->dl;
		(void)s_textbufAppendf(ctx->obj, "g node_%p_dl\n", (void *)node);
		if (s_exportGdlToObj(ctx, dl->opagdl, dl->vertices, dl->numvertices, 0) != 0) return -1;
		if (s_exportGdlToObj(ctx, dl->xlugdl, dl->vertices, dl->numvertices, 0) != 0) return -1;
	} else if (type == MODELNODETYPE_GUNDL && node->rodata &&
	           s_ptrInModel(ctx, node->rodata, sizeof(struct modelrodata_gundl))) {
		struct modelrodata_gundl *gundl = &node->rodata->gundl;
		(void)s_textbufAppendf(ctx->obj, "g node_%p_gundl\n", (void *)node);
		if (s_exportGdlToObj(ctx, gundl->opagdl, gundl->vertices, gundl->numvertices, 0) != 0) return -1;
		if (s_exportGdlToObj(ctx, gundl->xlugdl, gundl->vertices, gundl->numvertices, 0) != 0) return -1;
	} else if (type == MODELNODETYPE_STARGUNFIRE && node->rodata &&
	           s_ptrInModel(ctx, node->rodata, sizeof(struct modelrodata_stargunfire))) {
		struct modelrodata_stargunfire *star = &node->rodata->stargunfire;
		(void)s_textbufAppendf(ctx->obj, "g node_%p_stargunfire\n", (void *)node);
		if (s_exportGdlToObj(ctx, star->gdl, star->vertices, 4, 0) != 0) return -1;
	}

	if (node->child && s_exportNodeObj(ctx, node->child) != 0) return -1;
	if (node->next && s_exportNodeObj(ctx, node->next) != 0) return -1;
	return 0;
}

static s32 s_buildModelObj(const u8 *src, u32 src_size,
                           pdmesh_textbuf_t *obj, u32 *out_tris,
                           u32 *out_gdls)
{
	if (!src || src_size < sizeof(struct modeldef)) return -1;

	u8 *copy = (u8 *)malloc(src_size);
	if (!copy) return -1;
	memcpy(copy, src, src_size);

	struct modeldef *modeldef = (struct modeldef *)copy;
	modelPromoteOffsetsToPointers(modeldef, 0x5000000, (uintptr_t)modeldef);

	if (s_textbufAppend(obj,
			"# Perfect Dark 2 base model export\n"
			"mtllib model.mtl\n"
			"usemtl pd_default\n") != 0) {
		free(copy);
		return -1;
	}

	pdmesh_obj_export_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.base = copy;
	ctx.size = src_size;
	ctx.obj = obj;
	ctx.next_index = 1;
	if (modeldef->rootnode && s_exportNodeObj(&ctx, modeldef->rootnode) != 0) {
		free(copy);
		return -1;
	}

	*out_tris = ctx.triangle_count;
	*out_gdls = ctx.gdl_count;
	free(copy);
	return 0;
}

static s32 s_addShaSidecar(mod_archive_writer_t *aw, const char *name,
                           const void *data, u32 len)
{
	u8 digest[SHA256_DIGEST_SIZE];
	sha256Hash(data, (size_t)len, digest);
	char hex[SHA256_HEX_SIZE + 1];
	sha256ToHex(digest, hex);
	hex[SHA256_HEX_SIZE] = '\0';
	char sidecar[SHA256_HEX_SIZE + 2];
	snprintf(sidecar, sizeof(sidecar), "%s\n", hex);
	return modArchiveAddFileMem(aw, name, sidecar, (u32)strlen(sidecar));
}

/* Convert "FILE_GFALCON2" -> "base:falcon2_hi" given a hint suffix.
 * Falls back to "base:rom_g_<HEX>" when the symbol is not known. */
static void s_synthCatalogId(u16 filenum, const char *hint_suffix,
                              char *out, size_t n)
{
	const char *sym = loaderEnumNameForFileEnum(filenum);
	if (!sym) {
		snprintf(out, n, "base:rom_g_%04x", (unsigned)filenum);
		return;
	}
	/* Strip "FILE_G" prefix (weapon model files all start with this) and
	 * lowercase the rest, append a discriminator hint when supplied. */
	const char *body = sym;
	if (strncmp(sym, "FILE_G", 6) == 0) body = sym + 6;
	else if (strncmp(sym, "FILE_", 5) == 0) body = sym + 5;

	char lowered[96];
	size_t i;
	for (i = 0; i + 1 < sizeof(lowered) && body[i]; i++) {
		lowered[i] = (char)tolower((unsigned char)body[i]);
	}
	lowered[i] = '\0';

	if (hint_suffix && hint_suffix[0]) {
		snprintf(out, n, "base:%s_%s", lowered, hint_suffix);
	} else {
		snprintf(out, n, "base:%s", lowered);
	}
}

/* Build the on-disk path of the existing extracted .bin for a given
 * filenum. Mirrors romExtractRelPathForFilenum (port/src/romextract.c)
 * but lives in the public API; we rely on it. */
static s32 s_resolveSourceBinPath(u16 filenum, char *out_rel, s32 out_n)
{
	return romExtractRelPathForFilenum((s32)filenum, out_rel, out_n);
}

/* Emit one .pdmesh compound. Returns 1 written, 0 skipped, -1 failed.
 *
 * Engine Phase 4: dedup is now done UPSTREAM by the collect-phase in
 * romExtractAllPdmesh; this function no longer touches s_SeenFilenums.
 * Callers from parallel workers can invoke this safely on disjoint
 * (filenum, hint) tuples. */
static s32 s_emitOneMesh(u16 filenum, const char *hint_suffix,
                          const char *out_dir, s32 force_rewrite)
{
	if (filenum == 0) return 0;

	char src_rel[FS_MAXPATH];
	if (s_resolveSourceBinPath(filenum, src_rel, sizeof(src_rel)) <= 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdmesh: filenum=0x%04x has no extracted .bin path",
			(unsigned)filenum);
		return -1;
	}
	if (fsFileSize(src_rel) <= 0) {
		/* Source bytes not available on disk. Likely the filenum slot
		 * is not present in this ROM region or extraction skipped it.
		 * Not a hard error; the .pdweapon keeps a symbolic FILE_* ref so
		 * Step 4 can resolve through whatever source is available. */
		sysLogPrintf(LOG_NOTE,
			"romextract pdmesh: source missing for filenum=0x%04x (rel=\"%s\")",
			(unsigned)filenum, src_rel);
		return 0;
	}

	char catalog_id[128];
	s_synthCatalogId(filenum, hint_suffix, catalog_id, sizeof(catalog_id));

	char filename_slug[128];
	for (size_t i = 0, j = 0; j + 1 < sizeof(filename_slug); i++) {
		char c = catalog_id[i];
		if (c == '\0') { filename_slug[j] = '\0'; break; }
		filename_slug[j++] = (c == ':') ? '_' : c;
	}
	filename_slug[sizeof(filename_slug) - 1] = '\0';

	char dst_rel[FS_MAXPATH];
	snprintf(dst_rel, sizeof(dst_rel), "%s/%s.pdmesh", out_dir, filename_slug);

	if (!force_rewrite && fsFileSize(dst_rel) > 0 &&
	    s_existingArchiveHasEntry(dst_rel, "model.obj")) return 0;

	u32 src_size = 0;
	void *src_bytes = fsFileLoad(src_rel, &src_size);
	if (!src_bytes || src_size == 0) {
		if (src_bytes) sysMemFree(src_bytes);
		sysLogPrintf(LOG_NOTE,
			"romextract pdmesh: source bytes unavailable for filenum=0x%04x (rel=\"%s\")",
			(unsigned)filenum, src_rel);
		return 0;
	}

	pdmesh_textbuf_t obj_buf;
	memset(&obj_buf, 0, sizeof(obj_buf));
	u32 triangle_count = 0;
	u32 gdl_count = 0;
	if (s_buildModelObj((const u8 *)src_bytes, src_size, &obj_buf,
	                    &triangle_count, &gdl_count) != 0 ||
	    triangle_count == 0) {
		sysMemFree(src_bytes);
		s_textbufFree(&obj_buf);
		sysLogPrintf(LOG_WARNING,
			"romextract pdmesh: OBJ export produced no triangles for filenum=0x%04x (rel=\"%s\")",
			(unsigned)filenum, src_rel);
		return 0;
	}
	sysMemFree(src_bytes);

	const char mtl_buf[] =
		"newmtl pd_default\n"
		"Kd 0.8 0.8 0.8\n"
		"Ka 0.2 0.2 0.2\n"
		"Ks 0.0 0.0 0.0\n"
		"d 1.0\n";
	const u32 mtl_len = (u32)(sizeof(mtl_buf) - 1);

	const char *sym_for_provenance = loaderEnumNameForFileEnum(filenum);

	/* Build manifest.json text in memory. */
	char manifest_buf[768];
	int manifest_len = snprintf(manifest_buf, sizeof(manifest_buf),
		"{\n"
		"  \"pd_kind\": \"mesh\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"source_filenum_symbol\": \"%s\",\n"
		"  \"source_format\": \"PD_MODELDEF\",\n"
		"  \"format\": \"OBJ\",\n"
		"  \"geometry\": \"model.obj\",\n"
		"  \"material\": \"model.mtl\",\n"
		"  \"triangle_count\": %u,\n"
		"  \"display_list_count\": %u\n"
		"}\n",
		catalog_id,
		sym_for_provenance ? sym_for_provenance : "",
		(unsigned)triangle_count,
		(unsigned)gdl_count);
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		s_textbufFree(&obj_buf);
		sysLoudFailf("EXTRACT.PDMESH",
			"manifest.json snprintf truncated for filenum=0x%04x",
			(unsigned)filenum);
		return -1;
	}

	char ini_buf[512];
	int ini_len = snprintf(ini_buf, sizeof(ini_buf),
		"[model]\n"
		"catalog_id = %s\n"
		"kind = mesh\n"
		"source_format = PD_MODELDEF\n"
		"format = OBJ\n"
		"geometry_file = model.obj\n"
		"material_file = model.mtl\n"
		"triangle_count = %u\n"
		"display_list_count = %u\n"
		"source_filenum_symbol = %s\n",
		catalog_id,
		(unsigned)triangle_count,
		(unsigned)gdl_count,
		sym_for_provenance ? sym_for_provenance : "");
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini_buf)) {
		s_textbufFree(&obj_buf);
		sysLoudFailf("EXTRACT.PDMESH",
			"model.ini snprintf truncated for filenum=0x%04x",
			(unsigned)filenum);
		return -1;
	}

	/* Open ZIP writer (atomic temp + rename via modArchive). */
	char dst_full_buf[FS_MAXPATH + 1];
	const char *dst_full = fsFullPath(dst_rel, dst_full_buf, sizeof(dst_full_buf));
	if (!dst_full || !dst_full[0]) {
		s_textbufFree(&obj_buf);
		sysLoudFailf("EXTRACT.PDMESH",
			"fsFullPath failed for \"%s\"", dst_rel);
		return -1;
	}
	mod_archive_writer_t *aw = modArchiveBegin(dst_full);
	if (!aw) {
		s_textbufFree(&obj_buf);
		sysLoudFailf("EXTRACT.PDMESH",
			"modArchiveBegin failed for \"%s\"", dst_full);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "model.ini", ini_buf, (u32)ini_len) != 0) {
		sysLoudFailf("EXTRACT.PDMESH",
			"AddFileMem model.ini failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_textbufFree(&obj_buf);
		return -1;
	}
	if (modArchiveAddFileMem(aw, "manifest.json",
	                          manifest_buf, (u32)manifest_len) != 0) {
		sysLoudFailf("EXTRACT.PDMESH",
			"AddFileMem manifest.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_textbufFree(&obj_buf);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "model.obj", obj_buf.data, obj_buf.len) != 0) {
		sysLoudFailf("EXTRACT.PDMESH",
			"AddFileMem model.obj failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_textbufFree(&obj_buf);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "model.mtl", mtl_buf, mtl_len) != 0) {
		sysLoudFailf("EXTRACT.PDMESH",
			"AddFileMem model.mtl failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_textbufFree(&obj_buf);
		return -1;
	}

	if (s_addShaSidecar(aw, "model.obj.sha256", obj_buf.data, obj_buf.len) != 0 ||
	    s_addShaSidecar(aw, "model.mtl.sha256", mtl_buf, mtl_len) != 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdmesh: sidecar write failed for \"%s\"", dst_full);
	}

	if (modArchiveFinish(aw) != 0) {
		s_textbufFree(&obj_buf);
		sysLoudFailf("EXTRACT.PDMESH",
			"modArchiveFinish failed for \"%s\"", dst_full);
		return -1;
	}
	s_textbufFree(&obj_buf);
	return 1;
}

/* Engine Phase 4: collect-then-emit pattern.  Phase 1 walks the
 * authoring tables single-threaded, dedups by filenum into a work
 * queue.  Phase 2 fans the unique (filenum, hint) tuples across the
 * boot pool.
 *
 * Dedup happens upstream so each worker writes a unique slug; ZIP
 * writes are independent and thread-safe. */

typedef struct {
	u16  filenum;
	char hint[8];   /* "hi", "lo", "hand", or "" */
} pdmesh_work_t;

typedef struct {
	const pdmesh_work_t *jobs;
	s32                  count;
	const char          *out_dir;
	s32                  force_rewrite;
	SDL_atomic_t         written;
	SDL_atomic_t         skipped;
	SDL_atomic_t         failed;
	SDL_atomic_t         processed;
} pdmesh_fanout_ctx_t;

static void s_pdmeshWork(int idx, void *user)
{
	pdmesh_fanout_ctx_t *c = (pdmesh_fanout_ctx_t *)user;
	if (idx < 0 || idx >= c->count) return;

	const pdmesh_work_t *j = &c->jobs[idx];
	const char *hint = j->hint[0] ? j->hint : NULL;
	s32 r = s_emitOneMesh(j->filenum, hint, c->out_dir, c->force_rewrite);
	if (r > 0)       SDL_AtomicAdd(&c->written, 1);
	else if (r == 0) SDL_AtomicAdd(&c->skipped, 1);
	else             SDL_AtomicAdd(&c->failed,  1);

	int done = SDL_AtomicAdd(&c->processed, 1) + 1;
	if ((done & 0x0f) == 0 || done == c->count) {
		bootProgressUpdate(done, c->count);
	}
}

static s32 s_pdmeshAddWork(pdmesh_work_t *jobs, s32 *job_count, s32 cap,
                            u16 filenum, const char *hint)
{
	if (filenum == 0) return 0;
	/* Dedup: linear scan; the cap is ~512 so this stays cheap. */
	for (s32 i = 0; i < *job_count; i++) {
		if (jobs[i].filenum == filenum) return 0;
	}
	if (*job_count >= cap) return 0;
	jobs[*job_count].filenum = filenum;
	jobs[*job_count].hint[0] = '\0';
	if (hint) {
		size_t hlen = strlen(hint);
		if (hlen >= sizeof(jobs[*job_count].hint)) {
			hlen = sizeof(jobs[*job_count].hint) - 1;
		}
		memcpy(jobs[*job_count].hint, hint, hlen);
		jobs[*job_count].hint[hlen] = '\0';
	}
	(*job_count)++;
	return 1;
}

s32 romExtractAllPdmesh(s32 force_rewrite)
{
	/* BYOR completion (2026-05-03): walks weapon hi/lo + head mesh +
	 * body mesh + body hand mesh from the authoring source-of-truth.
	 * Per schema 2.5, every .pdweapon / .pdhead / .pdbody / .pdbody hand
	 * field references a .pdmesh entry; this single emitter produces
	 * the full superset so cross-references resolve. */

	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDMESH", "fsDataDirEnsure failed");
		return -1;
	}

	char dataDirBuf[FS_MAXPATH + 1];
	char meshes_dir[FS_MAXPATH];
	snprintf(meshes_dir, sizeof(meshes_dir), "%s/meshes",
		fsDataDir(dataDirBuf, sizeof(dataDirBuf)));
	if (!fsCreateDir(meshes_dir)) {
		sysLoudFailf("EXTRACT.PDMESH",
			"fsCreateDir(\"%s\") failed", meshes_dir);
		return -1;
	}

	/* Phase 1: collect unique (filenum, hint) tuples single-threaded.
	 * Cap matches the pre-Phase-4 dedup cap. */
	pdmesh_work_t jobs[ROMEXTRACT_PDMESH_SEEN_CAP];
	s32 job_count = 0;

	for (s32 i = 0; i < g_WeaponDataCount; i++) {
		const struct weapon *wpn = g_WeaponData[i];
		if (!wpn) continue;
		s_pdmeshAddWork(jobs, &job_count, ROMEXTRACT_PDMESH_SEEN_CAP,
		                wpn->hi_model, "hi");
		s_pdmeshAddWork(jobs, &job_count, ROMEXTRACT_PDMESH_SEEN_CAP,
		                wpn->lo_model, "lo");
	}
	for (s32 i = 0; i < g_HeadDataCount; i++) {
		s_pdmeshAddWork(jobs, &job_count, ROMEXTRACT_PDMESH_SEEN_CAP,
		                g_HeadData[i].filenum, NULL);
	}
	for (s32 i = 0; i < g_BodyDataCount; i++) {
		s_pdmeshAddWork(jobs, &job_count, ROMEXTRACT_PDMESH_SEEN_CAP,
		                g_BodyData[i].filenum, NULL);
		if (g_BodyData[i].handfilenum != 0) {
			s_pdmeshAddWork(jobs, &job_count, ROMEXTRACT_PDMESH_SEEN_CAP,
			                g_BodyData[i].handfilenum, "hand");
		}
	}

	/* Phase 2: fan out the per-mesh emit work. */
	pdmesh_fanout_ctx_t mctx;
	memset(&mctx, 0, sizeof(mctx));
	mctx.jobs          = jobs;
	mctx.count         = job_count;
	mctx.out_dir       = meshes_dir;
	mctx.force_rewrite = force_rewrite;
	SDL_AtomicSet(&mctx.written,   0);
	SDL_AtomicSet(&mctx.skipped,   0);
	SDL_AtomicSet(&mctx.failed,    0);
	SDL_AtomicSet(&mctx.processed, 0);

	bootProgressUpdate(0, job_count);
	bootPoolForRangeBlocking(0, job_count, s_pdmeshWork, &mctx);
	bootProgressUpdate(job_count, job_count);

	s32 written = SDL_AtomicGet(&mctx.written);
	s32 skipped = SDL_AtomicGet(&mctx.skipped);
	s32 failed  = SDL_AtomicGet(&mctx.failed);

	/* Maintain the legacy s_SeenCount log field for diff parity. */
	s_SeenCount = job_count;

	sysLogPrintf(LOG_NOTE,
		"romextract pdmesh: written=%d skipped=%d failed=%d unique_filenums=%d",
		written, skipped, failed, s_SeenCount);

	return written;
}
