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
#include "files.h"
#include "fs.h"
#include "loader_enum_reverse.h"
#include "modarchive.h"
#include "romdata.h"
#include "romextract.h"
#include "romextract_pd.h"
#include "sha256.h"
#include "system.h"
#include "preprocess.h"
#include "weapondata_authored.h"
#include "headdata_authored.h"
#include "bodydata_authored.h"
#include "game/modeldef.h"
#include "lib/model.h"
#include "lib/rzip.h"

/* Track filenums already emitted to avoid duplicate work when multiple
 * weapons / heads / bodies share a mesh. Cap covers ~86 weapons * 2
 * (hi + lo) + 84 head meshes + 68 body meshes + 68 hand meshes plus
 * dedup headroom. */
#define ROMEXTRACT_PDMESH_SEEN_CAP 512
#define ROMEXTRACT_PDMESH_MODEL_VMA 0x05000000u
#define ROMEXTRACT_PDMESH_MTX_STACK_CAP 11
#define ROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION_LABEL "model_obj_mtx_v8"
#define ROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION ROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION_LABEL "\n"
#define ROMEXTRACT_PDMESH_FAST_CACHE_KIND "pdmesh_model_obj_mtx_v8"
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
	s32 found = 0;
	size_t needle_len = strlen(needle);
	if (needle_len == 0) {
		found = 1;
	} else if (size >= needle_len) {
		for (u32 i = 0; i + needle_len <= size; i++) {
			if (memcmp(bytes + i, needle, needle_len) == 0) {
				found = 1;
				break;
			}
		}
	}
	free(bytes);
	return found;
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
	struct modeldef *modeldef;
	u16             source_filenum;
	s32             static_gun_model;
	pdmesh_textbuf_t *obj;
	u32             next_index;
	u32             triangle_count;
	u32             gdl_count;
	u32             vtx_cmd_count;
	u32             vtx_slot_count;
	u32             tri_cmd_count;
	u32             tri_attempt_count;
	u32             tri_missing_slot_count;
	u32             vtx_bad_addr_count;
	u32             mtx_cmd_count;
	u32             popmtx_cmd_count;
	u32             mtx_model_ref_count;
	u32             mtx_bad_addr_count;
	u32             transformed_vertex_count;
	f32           (*model_matrices)[4][4];
	s32             model_matrix_count;
	f32             mtx_stack[ROMEXTRACT_PDMESH_MTX_STACK_CAP][4][4];
	s32             mtx_stack_size;
} pdmesh_obj_export_t;

typedef struct {
	u32 triangle_count;
	u32 gdl_count;
	u32 vtx_cmd_count;
	u32 vtx_slot_count;
	u32 tri_cmd_count;
	u32 tri_attempt_count;
	u32 tri_missing_slot_count;
	u32 vtx_bad_addr_count;
	u32 mtx_cmd_count;
	u32 popmtx_cmd_count;
	u32 mtx_model_ref_count;
	u32 mtx_bad_addr_count;
	u32 transformed_vertex_count;
} pdmesh_obj_stats_t;

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
	uintptr_t rawaddr = (uintptr_t)raw;
	/* The PC model preprocessor marks rewritten display-list pointers by
	 * setting bit 0. Mask that flag before reading commands; otherwise the
	 * OBJ extractor walks from an odd address and silently misses triangles. */
	rawaddr &= ~(uintptr_t)1;
	uintptr_t unseg = UNSEGADDR(rawaddr);
	if (s_ptrInModel(ctx, (const void *)unseg, sizeof(Gfx))) {
		return (Gfx *)unseg;
	}
	u32 off = (u32)(unseg & 0x00ffffffu);
	if (off + sizeof(Gfx) <= ctx->size) return (Gfx *)(ctx->base + off);
	return NULL;
}

static void s_objMtxIdentity(f32 m[4][4])
{
	memset(m, 0, sizeof(f32) * 16);
	m[0][0] = 1.0f;
	m[1][1] = 1.0f;
	m[2][2] = 1.0f;
	m[3][3] = 1.0f;
}

static void s_objMtxCopy(f32 dst[4][4], const f32 src[4][4])
{
	memcpy(dst, src, sizeof(f32) * 16);
}

static void s_objMtxMul(f32 dst[4][4], const f32 a[4][4],
                        const f32 b[4][4])
{
	f32 tmp[4][4];
	for (s32 i = 0; i < 4; i++) {
		for (s32 j = 0; j < 4; j++) {
			tmp[i][j] =
				a[i][0] * b[0][j] +
				a[i][1] * b[1][j] +
				a[i][2] * b[2][j] +
				a[i][3] * b[3][j];
		}
	}
	s_objMtxCopy(dst, tmp);
}

static void s_objMtxTranslation(const struct coord *pos, f32 m[4][4])
{
	s_objMtxIdentity(m);
	if (!pos) return;
	m[3][0] = pos->x;
	m[3][1] = pos->y;
	m[3][2] = pos->z;
}

static void s_objMtxTransformPoint(const f32 m[4][4],
                                   const Vtx *v,
                                   f32 out[3])
{
	f32 x = (f32)v->x;
	f32 y = (f32)v->y;
	f32 z = (f32)v->z;
	out[0] = x * m[0][0] + y * m[1][0] + z * m[2][0] + m[3][0];
	out[1] = x * m[0][1] + y * m[1][1] + z * m[2][1] + m[3][1];
	out[2] = x * m[0][2] + y * m[1][2] + z * m[2][2] + m[3][2];
}

static s32 s_objNodeMtxIndex(const pdmesh_obj_export_t *ctx,
                             const struct modelnode *node,
                             s32 which)
{
	if (!node || !s_ptrInModel(ctx, node, sizeof(*node)) || !node->rodata) {
		return -1;
	}
	switch (node->type & 0xff) {
	case MODELNODETYPE_CHRINFO:
		if (!s_ptrInModel(ctx, node->rodata,
		                  sizeof(struct modelrodata_chrinfo))) return -1;
		return node->rodata->chrinfo.mtxindex;
	case MODELNODETYPE_POSITION:
		if (!s_ptrInModel(ctx, node->rodata,
		                  sizeof(struct modelrodata_position))) return -1;
		if (which == 2) return node->rodata->position.mtxindex2;
		if (which == 1) return node->rodata->position.mtxindex1;
		return node->rodata->position.mtxindex0;
	case MODELNODETYPE_POSITIONHELD:
		if (!s_ptrInModel(ctx, node->rodata,
		                  sizeof(struct modelrodata_positionheld))) return -1;
		return node->rodata->positionheld.mtxindex;
	}
	return -1;
}

static s32 s_objParentMtxIndex(const pdmesh_obj_export_t *ctx,
                               const struct modelnode *node)
{
	const struct modelnode *parent = node ? node->parent : NULL;
	while (parent && s_ptrInModel(ctx, parent, sizeof(*parent))) {
		s32 idx = s_objNodeMtxIndex(ctx, parent, 0);
		if (idx >= 0) return idx;
		parent = parent->parent;
	}
	return -1;
}

static s32 s_objMtxIndexValid(const pdmesh_obj_export_t *ctx, s32 idx)
{
	return idx >= 0 && idx < ctx->model_matrix_count && ctx->model_matrices;
}

static void s_objStoreNodeTranslationMtx(pdmesh_obj_export_t *ctx,
                                         struct modelnode *node,
                                         const struct coord *pos,
                                         s32 idx)
{
	if (!s_objMtxIndexValid(ctx, idx)) return;
	f32 local[4][4];
	f32 out[4][4];
	s_objMtxTranslation(pos, local);
	s32 parent_idx = s_objParentMtxIndex(ctx, node);
	if (s_objMtxIndexValid(ctx, parent_idx)) {
		s_objMtxMul(out, local, ctx->model_matrices[parent_idx]);
		s_objMtxCopy(ctx->model_matrices[idx], out);
	} else {
		s_objMtxCopy(ctx->model_matrices[idx], local);
	}
}

static void s_objBuildDefaultModelMatrices(pdmesh_obj_export_t *ctx,
                                           struct modelnode *node)
{
	if (!node || !s_ptrInModel(ctx, node, sizeof(*node))) return;

	u32 type = node->type & 0xff;
	if (type == MODELNODETYPE_POSITION && node->rodata &&
	    s_ptrInModel(ctx, node->rodata, sizeof(struct modelrodata_position))) {
		struct modelrodata_position *pos = &node->rodata->position;
		s_objStoreNodeTranslationMtx(ctx, node, &pos->pos, pos->mtxindex0);
		s_objStoreNodeTranslationMtx(ctx, node, &pos->pos, pos->mtxindex1);
		s_objStoreNodeTranslationMtx(ctx, node, &pos->pos, pos->mtxindex2);
	} else if (type == MODELNODETYPE_POSITIONHELD && node->rodata &&
	           s_ptrInModel(ctx, node->rodata,
	                        sizeof(struct modelrodata_positionheld))) {
		struct modelrodata_positionheld *held = &node->rodata->positionheld;
		s_objStoreNodeTranslationMtx(ctx, node, &held->pos, held->mtxindex);
	} else if (type == MODELNODETYPE_CHRINFO && node->rodata &&
	           s_ptrInModel(ctx, node->rodata,
	                        sizeof(struct modelrodata_chrinfo))) {
		s32 idx = node->rodata->chrinfo.mtxindex;
		s32 parent_idx = s_objParentMtxIndex(ctx, node);
		if (s_objMtxIndexValid(ctx, idx) &&
		    s_objMtxIndexValid(ctx, parent_idx)) {
			s_objMtxCopy(ctx->model_matrices[idx],
			             ctx->model_matrices[parent_idx]);
		}
	}

	if (node->child) s_objBuildDefaultModelMatrices(ctx, node->child);
	if (node->next) s_objBuildDefaultModelMatrices(ctx, node->next);
}

static s32 s_objPartNumForNode(const pdmesh_obj_export_t *ctx,
                               const struct modelnode *node)
{
	if (!ctx || !ctx->modeldef || !node || ctx->modeldef->numparts <= 0) {
		return -1;
	}

	size_t parts_bytes = (size_t)ctx->modeldef->numparts *
		sizeof(*ctx->modeldef->parts);
	if (!s_ptrInModel(ctx, ctx->modeldef->parts, parts_bytes)) {
		return -1;
	}

	s16 *partnums = (s16 *)&ctx->modeldef->parts[ctx->modeldef->numparts];
	size_t partnums_bytes = (size_t)ctx->modeldef->numparts *
		sizeof(*partnums);
	if (!s_ptrInModel(ctx, partnums, partnums_bytes)) {
		return -1;
	}

	for (s32 i = 0; i < ctx->modeldef->numparts; i++) {
		if (ctx->modeldef->parts[i] == node) {
			return partnums[i];
		}
	}

	return -1;
}

static s32 s_objHiddenStaticGunTogglePart(s32 partnum)
{
	return partnum == MODELPART_0042 ||
	       partnum == MODELPART_FALCON2_002E ||
	       partnum == MODELPART_FALCON2_002F ||
	       partnum == MODELPART_GUN_MUZZLEFLASH1 ||
	       partnum == MODELPART_GUN_MUZZLEFLASH2 ||
	       partnum == MODELPART_GUN_MUZZLEFLASH3;
}

static s32 s_objMayCullDetachedGunEffects(const pdmesh_obj_export_t *ctx)
{
	return ctx && ctx->static_gun_model &&
	       (ctx->source_filenum == FILE_GFALCON2 ||
	        ctx->source_filenum == FILE_GFALCON2LOD);
}

static s32 s_objNodeUnderHiddenGunToggle(const pdmesh_obj_export_t *ctx,
                                         const struct modelnode *node)
{
	const struct modelnode *cur = node;

	if (!ctx || !ctx->static_gun_model) {
		return 0;
	}

	while (cur && s_ptrInModel(ctx, cur, sizeof(*cur))) {
		u32 type = cur->type & 0xff;
		s32 partnum = s_objPartNumForNode(ctx, cur);

		if (partnum == MODELPART_GUN_LASERLIQUID) {
			return 1;
		}

		if (type == MODELNODETYPE_TOGGLE) {
			if (s_objHiddenStaticGunTogglePart(partnum)) {
				return 1;
			}
		}

		cur = cur->parent;
	}

	return 0;
}

static s32 s_objNodeIsOrDescendsFrom(const pdmesh_obj_export_t *ctx,
                                     const struct modelnode *node,
                                     const struct modelnode *target)
{
	const struct modelnode *cur = node;

	if (!ctx || !node || !target) {
		return 0;
	}

	while (cur && s_ptrInModel(ctx, cur, sizeof(*cur))) {
		if (cur == target) {
			return 1;
		}
		cur = cur->parent;
	}

	return 0;
}

static s32 s_objHiddenGunToggleTargetsNode(
	const pdmesh_obj_export_t *ctx,
	const struct modelnode *scan,
	const struct modelnode *node,
	s32 depth)
{
	if (!ctx || !ctx->static_gun_model || !scan || !node || depth > 2048) {
		return 0;
	}
	if (!s_ptrInModel(ctx, scan, sizeof(*scan))) {
		return 0;
	}

	u32 type = scan->type & 0xff;
	if (type == MODELNODETYPE_TOGGLE && scan->rodata &&
	    s_ptrInModel(ctx, scan->rodata, sizeof(struct modelrodata_toggle)) &&
	    s_objHiddenStaticGunTogglePart(s_objPartNumForNode(ctx, scan))) {
		struct modelnode *target = scan->rodata->toggle.target;
		if (s_ptrInModel(ctx, target, sizeof(*target)) &&
		    s_objNodeIsOrDescendsFrom(ctx, node, target)) {
			return 1;
		}
	}

	if (scan->child &&
	    s_objHiddenGunToggleTargetsNode(ctx, scan->child, node, depth + 1)) {
		return 1;
	}
	if (scan->next &&
	    s_objHiddenGunToggleTargetsNode(ctx, scan->next, node, depth + 1)) {
		return 1;
	}

	return 0;
}

static s32 s_objStaticGunVertsLookDetachedEffect(
	const pdmesh_obj_export_t *ctx,
	const Vtx *vertices,
	s32 numvertices)
{
	if (!s_objMayCullDetachedGunEffects(ctx) || !vertices ||
	    numvertices <= 0) {
		return 0;
	}
	if (!s_ptrInModel(ctx, vertices, (size_t)numvertices * sizeof(*vertices))) {
		return 0;
	}

	s32 minx = vertices[0].x;
	s32 maxx = vertices[0].x;
	s32 miny = vertices[0].y;
	s32 maxy = vertices[0].y;

	for (s32 i = 1; i < numvertices; i++) {
		if (vertices[i].x < minx) minx = vertices[i].x;
		if (vertices[i].x > maxx) maxx = vertices[i].x;
		if (vertices[i].y < miny) miny = vertices[i].y;
		if (vertices[i].y > maxy) maxy = vertices[i].y;
	}

	return minx > 200 || maxx < -200 || miny > 250 || maxy < -250;
}

static s32 s_objTextLooksDetachedGunEffect(const pdmesh_textbuf_t *buf)
{
	if (!buf || !buf->data || buf->len == 0) {
		return 0;
	}

	s32 saw_vert = 0;
	f32 minx = 0.0f, maxx = 0.0f, miny = 0.0f, maxy = 0.0f;
	const char *line = buf->data;

	while (line && *line) {
		if (line[0] == 'v' && line[1] == ' ') {
			f32 x, y, z;
			if (sscanf(line, "v %f %f %f", &x, &y, &z) == 3) {
				if (!saw_vert) {
					minx = maxx = x;
					miny = maxy = y;
					saw_vert = 1;
				} else {
					if (x < minx) minx = x;
					if (x > maxx) maxx = x;
					if (y < miny) miny = y;
					if (y > maxy) maxy = y;
				}
			}
		}

		line = strchr(line, '\n');
		if (line) line++;
	}

	return saw_vert &&
	       (minx > 200.0f || maxx < -200.0f ||
	        miny > 250.0f || maxy < -250.0f);
}

static s32 s_objDecodeFixedMtx(const s32 *addr, f32 out[4][4])
{
	if (!addr) return 0;
#ifdef GBI_FLOATS
	memcpy(out, addr, sizeof(f32) * 16);
#else
	for (s32 i = 0; i < 4; i++) {
		for (s32 j = 0; j < 4; j += 2) {
			s32 int_part = addr[i * 2 + j / 2];
			u32 frac_part = (u32)addr[8 + i * 2 + j / 2];
			out[i][j] = (f32)((s32)((int_part & 0xffff0000) |
				(frac_part >> 16))) / 65536.0f;
			out[i][j + 1] = (f32)((s32)((int_part << 16) |
				(frac_part & 0xffff))) / 65536.0f;
		}
	}
#endif
	return 1;
}

static s32 s_objResolveMtx(const pdmesh_obj_export_t *ctx, uintptr_t raw,
                           f32 out[4][4], u32 *from_model_table)
{
	if (from_model_table) *from_model_table = 0;
	if (!raw) return 0;

	if (raw & 1u) {
		u32 seg = (u32)((raw & 0x0f000000u) >> 24);
		u32 off = (u32)(raw & 0x00fffffeu);
		if (seg == SPSEGMENT_MODEL_MTX && ctx->model_matrices) {
			s32 idx = (s32)(off / sizeof(Mtxf));
			if ((off % sizeof(Mtxf)) == 0 && s_objMtxIndexValid(ctx, idx)) {
				s_objMtxCopy(out, ctx->model_matrices[idx]);
				if (from_model_table) *from_model_table = 1;
				return 1;
			}
			idx = (s32)(off / sizeof(Mtx));
			if ((off % sizeof(Mtx)) == 0 && s_objMtxIndexValid(ctx, idx)) {
				s_objMtxCopy(out, ctx->model_matrices[idx]);
				if (from_model_table) *from_model_table = 1;
				return 1;
			}
		}
	}

	uintptr_t ptr = UNSEGADDR(raw);
	if (s_ptrInModel(ctx, (const void *)ptr, sizeof(Mtx))) {
		return s_objDecodeFixedMtx((const s32 *)ptr, out);
	}
	u32 off = (u32)(ptr & 0x00ffffffu);
	if (off + sizeof(Mtx) <= ctx->size) {
		return s_objDecodeFixedMtx((const s32 *)(ctx->base + off), out);
	}
	return 0;
}

static void s_objApplyMtxCommand(pdmesh_obj_export_t *ctx,
                                 u8 parameters,
                                 uintptr_t raw)
{
	f32 matrix[4][4];
	u32 from_model_table = 0;
	ctx->mtx_cmd_count++;
	if (!s_objResolveMtx(ctx, raw, matrix, &from_model_table)) {
		ctx->mtx_bad_addr_count++;
		return;
	}
	if (from_model_table) ctx->mtx_model_ref_count++;
	if (parameters & G_MTX_PROJECTION) return;

	if (ctx->mtx_stack_size <= 0) {
		ctx->mtx_stack_size = 1;
		s_objMtxIdentity(ctx->mtx_stack[0]);
	}
	if ((parameters & G_MTX_PUSH) &&
	    ctx->mtx_stack_size < ROMEXTRACT_PDMESH_MTX_STACK_CAP) {
		s_objMtxCopy(ctx->mtx_stack[ctx->mtx_stack_size],
		             ctx->mtx_stack[ctx->mtx_stack_size - 1]);
		ctx->mtx_stack_size++;
	}
	if (parameters & G_MTX_LOAD) {
		s_objMtxCopy(ctx->mtx_stack[ctx->mtx_stack_size - 1], matrix);
	} else {
		s_objMtxMul(ctx->mtx_stack[ctx->mtx_stack_size - 1],
		            matrix,
		            ctx->mtx_stack[ctx->mtx_stack_size - 1]);
	}
}

static s32 s_objEmitTri(pdmesh_obj_export_t *ctx,
                        const Vtx *a, const Vtx *b, const Vtx *c)
{
	if (!a || !b || !c) return 0;
	if (ctx->mtx_stack_size <= 0) {
		ctx->mtx_stack_size = 1;
		s_objMtxIdentity(ctx->mtx_stack[0]);
	}
	f32 va[3], vb[3], vc[3];
	s_objMtxTransformPoint(ctx->mtx_stack[ctx->mtx_stack_size - 1], a, va);
	s_objMtxTransformPoint(ctx->mtx_stack[ctx->mtx_stack_size - 1], b, vb);
	s_objMtxTransformPoint(ctx->mtx_stack[ctx->mtx_stack_size - 1], c, vc);
	u32 i0 = ctx->next_index++;
	u32 i1 = ctx->next_index++;
	u32 i2 = ctx->next_index++;
	if (s_textbufAppendf(ctx->obj,
			"v %.6f %.6f %.6f\n"
			"v %.6f %.6f %.6f\n"
			"v %.6f %.6f %.6f\n"
			"vt %.6f %.6f\n"
			"vt %.6f %.6f\n"
			"vt %.6f %.6f\n"
			"f %u/%u %u/%u %u/%u\n",
			(double)va[0], (double)va[1], (double)va[2],
			(double)vb[0], (double)vb[1], (double)vb[2],
			(double)vc[0], (double)vc[1], (double)vc[2],
			(double)a->s / 32.0, 1.0 - ((double)a->t / 32.0),
			(double)b->s / 32.0, 1.0 - ((double)b->t / 32.0),
			(double)c->s / 32.0, 1.0 - ((double)c->t / 32.0),
			(unsigned)i0, (unsigned)i0,
			(unsigned)i1, (unsigned)i1,
			(unsigned)i2, (unsigned)i2) != 0) {
		return -1;
	}
	ctx->triangle_count++;
	ctx->transformed_vertex_count += 3;
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
			Gfx *child = (Gfx *)(((uintptr_t)w1) & ~(uintptr_t)1);
			if (s_exportGdlToObj(ctx, child, vbuf, numverts, depth + 1) != 0) {
				return -1;
			}
			continue;
		}

		if (cmd == (u8)G_MTX) {
			s_objApplyMtxCommand(ctx, (u8)((w0 >> 16) & 0xffu),
			                     (uintptr_t)w1);
			continue;
		}

		if (cmd == (u8)G_POPMTX && w1 != 0) {
			ctx->popmtx_cmd_count++;
			if (ctx->mtx_stack_size > 1) ctx->mtx_stack_size--;
			continue;
		}

		if (cmd == (u8)G_VTX) {
			ctx->vtx_cmd_count++;
			s32 n = (s32)((w0 & 0xffffu) / sizeof(Vtx));
			s32 v0 = (s32)((w0 >> 16) & 0xf);
			uintptr_t src = ((uintptr_t)w1) & ~(uintptr_t)1;
			uintptr_t vstart = (uintptr_t)vbuf;
			uintptr_t vend = vstart + (size_t)numverts * sizeof(Vtx);
			s32 srcidx = -1;

			if (src >= vstart && src < vend) {
				srcidx = (s32)((src - vstart) / sizeof(Vtx));
			} else {
				u32 seg = (u32)((src >> 24) & 0x0fu);
				u32 off = (u32)(src & 0x00ffffffu);
				if (seg == SPSEGMENT_MODEL_VTX) {
					if ((off % sizeof(Vtx)) == 0) {
						srcidx = (s32)(off / sizeof(Vtx));
					}
				} else {
					if (off + sizeof(Vtx) <= ctx->size) {
						const u8 *baseptr = ctx->base + off;
						if ((uintptr_t)baseptr >= vstart &&
						    (uintptr_t)baseptr < vend) {
							srcidx = (s32)(((uintptr_t)baseptr - vstart) /
							               sizeof(Vtx));
						}
					}
				}
			}

			if (srcidx >= 0) {
				for (s32 i = 0; i < n && (v0 + i) < 64; i++) {
					s32 vi = srcidx + i;
					if (vi >= 0 && vi < numverts) {
						slots[v0 + i] = &vbuf[vi];
						ctx->vtx_slot_count++;
					}
				}
				if (v0 + n > numslots) numslots = v0 + n;
			} else {
				ctx->vtx_bad_addr_count++;
			}
			continue;
		}

		if (cmd == (u8)G_TRI1) {
			ctx->tri_cmd_count++;
			s32 i0 = ((w1 >> 16) & 0xff) / 10;
			s32 i1 = ((w1 >> 8)  & 0xff) / 10;
			s32 i2 = ((w1 >> 0)  & 0xff) / 10;
			if (i0 < numslots && i1 < numslots && i2 < numslots) {
				ctx->tri_attempt_count++;
				if (s_objEmitTri(ctx, slots[i0], slots[i1], slots[i2]) != 0) return -1;
			} else {
				ctx->tri_missing_slot_count++;
			}
			continue;
		}

		if (cmd == (u8)G_TRI4) {
			ctx->tri_cmd_count++;
			s32 idx[4][3] = {
				{ (s32)((w1 >> 0)  & 0xf), (s32)((w1 >> 4)  & 0xf), (s32)((w0 >> 0)  & 0xf) },
				{ (s32)((w1 >> 8)  & 0xf), (s32)((w1 >> 12) & 0xf), (s32)((w0 >> 4)  & 0xf) },
				{ (s32)((w1 >> 16) & 0xf), (s32)((w1 >> 20) & 0xf), (s32)((w0 >> 8)  & 0xf) },
				{ (s32)((w1 >> 24) & 0xf), (s32)((w1 >> 28) & 0xf), (s32)((w0 >> 12) & 0xf) },
			};
			for (s32 ti = 0; ti < 4; ti++) {
				s32 i0 = idx[ti][0];
				s32 i1 = idx[ti][1];
				s32 i2 = idx[ti][2];
				if (i0 == i1 && i1 == i2) continue;
				if (i0 < numslots && i1 < numslots && i2 < numslots) {
					ctx->tri_attempt_count++;
					if (s_objEmitTri(ctx, slots[i0], slots[i1], slots[i2]) != 0) return -1;
				} else {
					ctx->tri_missing_slot_count++;
				}
			}
		}
	}
	return 0;
}

static s32 s_exportGunDlToObj(pdmesh_obj_export_t *ctx,
                              struct modelnode *node,
                              struct modelrodata_gundl *gundl)
{
	if (!ctx || !node || !gundl) {
		return 0;
	}

	if (s_objStaticGunVertsLookDetachedEffect(ctx, gundl->vertices,
			gundl->numvertices)) {
		return 0;
	}

	pdmesh_textbuf_t trial_buf = {0};
	pdmesh_obj_export_t trial = *ctx;
	trial.obj = &trial_buf;

	if (s_exportGdlToObj(&trial, gundl->opagdl, gundl->vertices,
			gundl->numvertices, 0) != 0 ||
	    s_exportGdlToObj(&trial, gundl->xlugdl, gundl->vertices,
			gundl->numvertices, 0) != 0) {
		s_textbufFree(&trial_buf);
		return -1;
	}

	if (s_objMayCullDetachedGunEffects(ctx) &&
	    s_objTextLooksDetachedGunEffect(&trial_buf)) {
		s_textbufFree(&trial_buf);
		return 0;
	}

	if (trial_buf.len > 0) {
		pdmesh_textbuf_t *real_obj = ctx->obj;
		if (s_textbufAppendf(real_obj, "g node_%p_gundl\n",
				(void *)node) != 0 ||
		    s_textbufAppend(real_obj, trial_buf.data) != 0) {
			s_textbufFree(&trial_buf);
			return -1;
		}
		*ctx = trial;
		ctx->obj = real_obj;
	}

	s_textbufFree(&trial_buf);
	return 0;
}

static s32 s_exportNodeObj(pdmesh_obj_export_t *ctx, struct modelnode *node)
{
	if (!node || !s_ptrInModel(ctx, node, sizeof(*node))) return 0;
	if (s_objNodeUnderHiddenGunToggle(ctx, node) ||
	    s_objHiddenGunToggleTargetsNode(ctx, ctx->modeldef ?
			ctx->modeldef->rootnode : NULL, node, 0)) {
		if (node->next && s_exportNodeObj(ctx, node->next) != 0) return -1;
		return 0;
	}

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
		if (s_exportGunDlToObj(ctx, node, gundl) != 0) return -1;
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

static s32 s_vmaOffsetInModel(uintptr_t ptr, u32 size, size_t bytes)
{
	if (!ptr) return 1;
	uintptr_t unseg = UNSEGADDR(ptr);
	if ((unseg & 0xff000000u) != ROMEXTRACT_PDMESH_MODEL_VMA) return 0;
	u32 off = (u32)(unseg & 0x00ffffffu);
	return off + bytes >= off && off + bytes <= size;
}

static s32 s_modeldefOffsetsLookPromotable(const struct modeldef *modeldef,
                                           u32 size)
{
	if (!modeldef || !modeldef->rootnode) return 0;
	if (modeldef->numparts < 0 || modeldef->numparts > 500) return 0;
	if (!s_vmaOffsetInModel((uintptr_t)modeldef->rootnode, size,
	                        sizeof(struct modelnode))) {
		return 0;
	}
	if (modeldef->numparts > 0) {
		size_t part_bytes = (size_t)modeldef->numparts *
			(sizeof(uintptr_t) + sizeof(s16));
		if (!s_vmaOffsetInModel((uintptr_t)modeldef->parts, size,
		                        part_bytes)) {
			return 0;
		}
	}
	if (modeldef->numtexconfigs > 0) {
		size_t tex_bytes = (size_t)modeldef->numtexconfigs *
			sizeof(struct textureconfig);
		if (!s_vmaOffsetInModel((uintptr_t)modeldef->texconfigs, size,
		                        tex_bytes)) {
			return 0;
		}
	}
	return 1;
}

static s32 s_buildModelObj(const u8 *src, u32 src_size,
                           u32 loadtype, u16 source_filenum,
                           pdmesh_textbuf_t *obj,
                           pdmesh_obj_stats_t *out_stats)
{
	if (!src || src_size < sizeof(struct modeldef)) return -1;

	u8 *copy = (u8 *)malloc(src_size);
	if (!copy) return -1;
	memcpy(copy, src, src_size);

	struct modeldef *modeldef = (struct modeldef *)copy;
	if (!s_modeldefOffsetsLookPromotable(modeldef, src_size)) {
		free(copy);
		return -1;
	}
	modelPromoteTypeToPointer(modeldef);
	modelPromoteOffsetsToPointers(modeldef, ROMEXTRACT_PDMESH_MODEL_VMA,
	                              (uintptr_t)modeldef);
	if (modeldef->nummatrices < 0 || modeldef->nummatrices > 4096) {
		free(copy);
		return -1;
	}

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
	ctx.modeldef = modeldef;
	ctx.source_filenum = source_filenum;
	ctx.static_gun_model = loadtype == LOADTYPE_GUN;
	ctx.obj = obj;
	ctx.next_index = 1;
	ctx.mtx_stack_size = 1;
	s_objMtxIdentity(ctx.mtx_stack[0]);
	if (modeldef->nummatrices > 0) {
		size_t matrix_bytes = (size_t)modeldef->nummatrices *
			sizeof(*ctx.model_matrices);
		ctx.model_matrices = (f32 (*)[4][4])malloc(matrix_bytes);
		if (!ctx.model_matrices) {
			free(copy);
			return -1;
		}
		ctx.model_matrix_count = modeldef->nummatrices;
		for (s32 i = 0; i < ctx.model_matrix_count; i++) {
			s_objMtxIdentity(ctx.model_matrices[i]);
		}
		if (modeldef->rootnode) {
			s_objBuildDefaultModelMatrices(&ctx, modeldef->rootnode);
		}
	}
	if (modeldef->rootnode && s_exportNodeObj(&ctx, modeldef->rootnode) != 0) {
		if (ctx.model_matrices) free(ctx.model_matrices);
		free(copy);
		return -1;
	}

	if (out_stats) {
		out_stats->triangle_count = ctx.triangle_count;
		out_stats->gdl_count = ctx.gdl_count;
		out_stats->vtx_cmd_count = ctx.vtx_cmd_count;
		out_stats->vtx_slot_count = ctx.vtx_slot_count;
		out_stats->tri_cmd_count = ctx.tri_cmd_count;
		out_stats->tri_attempt_count = ctx.tri_attempt_count;
		out_stats->tri_missing_slot_count = ctx.tri_missing_slot_count;
		out_stats->vtx_bad_addr_count = ctx.vtx_bad_addr_count;
		out_stats->mtx_cmd_count = ctx.mtx_cmd_count;
		out_stats->popmtx_cmd_count = ctx.popmtx_cmd_count;
		out_stats->mtx_model_ref_count = ctx.mtx_model_ref_count;
		out_stats->mtx_bad_addr_count = ctx.mtx_bad_addr_count;
		out_stats->transformed_vertex_count = ctx.transformed_vertex_count;
	}
	if (ctx.model_matrices) free(ctx.model_matrices);
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

static u32 s_loadTypeForMeshFilenum(u16 filenum, const char *hint_suffix)
{
	const char *sym = loaderEnumNameForFileEnum(filenum);
	if ((hint_suffix && (!strcmp(hint_suffix, "hi") ||
	                     !strcmp(hint_suffix, "lo"))) ||
	    (sym && strncmp(sym, "FILE_G", 6) == 0)) {
		return LOADTYPE_GUN;
	}
	return LOADTYPE_MODEL;
}

static s32 s_loadSourceModelPreprocessed(u16 filenum, const char *src_rel,
                                         const char *hint_suffix,
                                         u8 **out_data, u32 *out_size,
                                         u32 *out_loadtype)
{
	if (out_data) *out_data = NULL;
	if (out_size) *out_size = 0;
	if (out_loadtype) *out_loadtype = LOADTYPE_MODEL;

	u32 raw_size = 0;
	u8 *raw = (u8 *)fsFileLoad(src_rel, &raw_size);
	if (!raw || raw_size == 0) {
		if (raw) sysMemFree(raw);
		return 0;
	}

	u32 loadtype = s_loadTypeForMeshFilenum(filenum, hint_suffix);
	u32 inflated_size = raw_size;
	if (raw_size >= 5 && rzipIs1173(raw)) {
		inflated_size = ALIGN16((raw[2] << 16) | (raw[3] << 8) | raw[4]);
	}

	u32 cap = romdataFileGetEstimatedSize(inflated_size, loadtype);
	if (cap < inflated_size) cap = inflated_size;
	u8 *work = sysMemZeroAlloc(cap);
	if (!work) {
		sysMemFree(raw);
		return -1;
	}

	if (raw_size >= 5 && rzipIs1173(raw)) {
		u8 scratch[5 * 1024];
		s32 result = rzipInflate(raw, work, scratch);
		sysMemFree(raw);
		if (result <= 0) {
			sysMemFree(work);
			return -1;
		}
		inflated_size = ALIGN16((u32)result);
	} else {
		memcpy(work, raw, raw_size);
		sysMemFree(raw);
	}

	u32 new_size = inflated_size;
	sysLogPrintf(LOG_NOTE,
		"romextract pdmesh: preprocessing filenum=0x%04x loadtype=%u raw_size=%u inflated_size=%u",
		(unsigned)filenum, (unsigned)loadtype, (unsigned)raw_size,
		(unsigned)inflated_size);
	if (loadtype == LOADTYPE_GUN) {
		(void)preprocessGunFile(work, inflated_size, &new_size);
	} else {
		(void)preprocessModelFile(work, inflated_size, &new_size);
	}

	if (new_size == 0 || new_size > cap) {
		sysMemFree(work);
		return -1;
	}

	if (out_data) *out_data = work;
	if (out_size) *out_size = new_size;
	if (out_loadtype) *out_loadtype = loadtype;
	return 1;
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
	    s_existingArchiveHasEntry(dst_rel, "model.obj") &&
	    s_existingArchiveEntryContains(dst_rel, "export_version.txt",
		ROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION)) return 0;

	u8 *model_bytes = NULL;
	u32 model_size = 0;
	u32 loadtype = LOADTYPE_MODEL;
	s32 loaded = s_loadSourceModelPreprocessed(filenum, src_rel, hint_suffix,
	                                           &model_bytes, &model_size,
	                                           &loadtype);
	if (loaded <= 0) {
		sysLogPrintf(LOG_NOTE,
			"romextract pdmesh: source model unavailable for filenum=0x%04x (rel=\"%s\")",
			(unsigned)filenum, src_rel);
		return loaded < 0 ? -1 : 0;
	}

	pdmesh_textbuf_t obj_buf;
	memset(&obj_buf, 0, sizeof(obj_buf));
	pdmesh_obj_stats_t stats;
	memset(&stats, 0, sizeof(stats));
	if (s_buildModelObj((const u8 *)model_bytes, model_size, loadtype,
	                    filenum, &obj_buf, &stats) != 0 ||
	    stats.triangle_count == 0) {
		sysMemFree(model_bytes);
		s_textbufFree(&obj_buf);
		sysLogPrintf(LOG_WARNING,
			"romextract pdmesh: OBJ export produced no triangles for filenum=0x%04x (rel=\"%s\", gdls=%u vtxcmds=%u vtxslots=%u mtxcmds=%u mtxrefs=%u tricmds=%u triattempts=%u trimiss=%u badvtxaddr=%u badmtxaddr=%u)",
			(unsigned)filenum, src_rel, (unsigned)stats.gdl_count,
			(unsigned)stats.vtx_cmd_count, (unsigned)stats.vtx_slot_count,
			(unsigned)stats.mtx_cmd_count,
			(unsigned)stats.mtx_model_ref_count,
			(unsigned)stats.tri_cmd_count, (unsigned)stats.tri_attempt_count,
			(unsigned)stats.tri_missing_slot_count,
			(unsigned)stats.vtx_bad_addr_count,
			(unsigned)stats.mtx_bad_addr_count);
		return -1;
	}
	sysLogPrintf(LOG_NOTE,
		"romextract pdmesh: exported filenum=0x%04x loadtype=%u tris=%u gdls=%u mtxcmds=%u mtxrefs=%u",
		(unsigned)filenum, (unsigned)loadtype,
		(unsigned)stats.triangle_count, (unsigned)stats.gdl_count,
		(unsigned)stats.mtx_cmd_count,
		(unsigned)stats.mtx_model_ref_count);
	sysMemFree(model_bytes);

	const char mtl_buf[] =
		"newmtl pd_default\n"
		"Kd 0.8 0.8 0.8\n"
		"Ka 0.2 0.2 0.2\n"
		"Ks 0.0 0.0 0.0\n"
		"d 1.0\n";
	const u32 mtl_len = (u32)(sizeof(mtl_buf) - 1);

	const char *sym_for_provenance = loaderEnumNameForFileEnum(filenum);

	/* Build manifest.json text in memory. */
	char manifest_buf[1024];
	int manifest_len = snprintf(manifest_buf, sizeof(manifest_buf),
		"{\n"
		"  \"pd_kind\": \"mesh\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"source_filenum_symbol\": \"%s\",\n"
		"  \"source_format\": \"PD_MODELDEF\",\n"
		"  \"format\": \"OBJ\",\n"
		"  \"obj_export_version\": \"%s\",\n"
		"  \"geometry\": \"model.obj\",\n"
		"  \"material\": \"model.mtl\",\n"
		"  \"triangle_count\": %u,\n"
		"  \"display_list_count\": %u,\n"
		"  \"matrix_command_count\": %u,\n"
		"  \"model_matrix_reference_count\": %u\n"
		"}\n",
		catalog_id,
		sym_for_provenance ? sym_for_provenance : "",
		ROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION_LABEL,
		(unsigned)stats.triangle_count,
		(unsigned)stats.gdl_count,
		(unsigned)stats.mtx_cmd_count,
		(unsigned)stats.mtx_model_ref_count);
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		s_textbufFree(&obj_buf);
		sysLoudFailf("EXTRACT.PDMESH",
			"manifest.json snprintf truncated for filenum=0x%04x",
			(unsigned)filenum);
		return -1;
	}

	char ini_buf[768];
	int ini_len = snprintf(ini_buf, sizeof(ini_buf),
		"[model]\n"
		"catalog_id = %s\n"
		"kind = mesh\n"
		"source_format = PD_MODELDEF\n"
		"format = OBJ\n"
		"obj_export_version = %s\n"
		"geometry_file = model.obj\n"
		"material_file = model.mtl\n"
		"triangle_count = %u\n"
		"display_list_count = %u\n"
		"matrix_command_count = %u\n"
		"model_matrix_reference_count = %u\n"
		"source_filenum_symbol = %s\n",
		catalog_id,
		ROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION_LABEL,
		(unsigned)stats.triangle_count,
		(unsigned)stats.gdl_count,
		(unsigned)stats.mtx_cmd_count,
		(unsigned)stats.mtx_model_ref_count,
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

	if (modArchiveAddFileMem(aw, "export_version.txt",
	                          ROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION,
	                          (u32)strlen(ROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION)) != 0) {
		sysLoudFailf("EXTRACT.PDMESH",
			"AddFileMem export_version.txt failed for \"%s\"", dst_full);
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

	if (s_addShaSidecar(aw, "export_version.txt.sha256",
			ROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION,
			(u32)strlen(ROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION)) != 0 ||
	    s_addShaSidecar(aw, "model.obj.sha256", obj_buf.data, obj_buf.len) != 0 ||
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
 * queue.  Phase 2 emits the unique (filenum, hint) tuples after
 * preprocessing each ROM model into the PC modeldef layout.
 *
 * The model preprocessor uses process-global marker/GBI scratch state,
 * so keep the emission loop single-threaded rather than fanning this
 * pass out through the boot pool. */

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
	const char *wanted_hint = hint ? hint : "";
	/* Dedup by output identity, not only filenum. Some files are both
	 * weapon meshes and body/hand meshes, which intentionally emit separate
	 * archive names from the same source bytes. */
	for (s32 i = 0; i < *job_count; i++) {
		if (jobs[i].filenum == filenum &&
		    strcmp(jobs[i].hint, wanted_hint) == 0) {
			return 0;
		}
	}
	if (*job_count >= cap) return 0;
	jobs[*job_count].filenum = filenum;
	jobs[*job_count].hint[0] = '\0';
	if (hint && hint[0]) {
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

static void s_pdmeshAddModelnumWork(pdmesh_work_t *jobs, s32 *job_count,
                                    s32 cap, s32 modelnum)
{
	if (modelnum < 0 || modelnum >= NUM_MODELS) return;
	u16 filenum = g_ModelStates[modelnum].fileid;
	s_pdmeshAddWork(jobs, job_count, cap, filenum, NULL);
}

static void s_pdmeshAddWeaponFuncPayloadWork(pdmesh_work_t *jobs,
                                             s32 *job_count, s32 cap,
                                             const struct weaponfunc *f)
{
	if (!f) return;
	if (f->type == INVENTORYFUNCTYPE_SHOOT_PROJECTILE) {
		const struct weaponfunc_shootprojectile *sp =
			(const struct weaponfunc_shootprojectile *)f;
		s_pdmeshAddModelnumWork(jobs, job_count, cap, sp->projectilemodelnum);
	} else if (f->type == INVENTORYFUNCTYPE_THROW) {
		const struct weaponfunc_throw *tw =
			(const struct weaponfunc_throw *)f;
		s_pdmeshAddModelnumWork(jobs, job_count, cap, tw->projectilemodelnum);
	}
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
		s_pdmeshAddWeaponFuncPayloadWork(jobs, &job_count,
			ROMEXTRACT_PDMESH_SEEN_CAP,
			(const struct weaponfunc *)wpn->functions[0]);
		s_pdmeshAddWeaponFuncPayloadWork(jobs, &job_count,
			ROMEXTRACT_PDMESH_SEEN_CAP,
			(const struct weaponfunc *)wpn->functions[1]);
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

	if (romExtractPdFastCacheCanSkip(ROMEXTRACT_PDMESH_FAST_CACHE_KIND, meshes_dir,
			".pdmesh", force_rewrite)) {
		bootProgressUpdate(job_count, job_count);
		s_SeenCount = job_count;
		sysLogPrintf(LOG_NOTE,
			"romextract pdmesh: written=0 skipped=%d failed=0 unique_mesh_jobs=%d (fast-cache)",
			job_count, s_SeenCount);
		return 0;
	}

	/* Phase 2: emit the per-mesh work. */
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
	for (s32 i = 0; i < job_count; i++) {
		s_pdmeshWork(i, &mctx);
	}
	bootProgressUpdate(job_count, job_count);

	s32 written = SDL_AtomicGet(&mctx.written);
	s32 skipped = SDL_AtomicGet(&mctx.skipped);
	s32 failed  = SDL_AtomicGet(&mctx.failed);

	/* Maintain the legacy s_SeenCount log field for diff parity. */
	s_SeenCount = job_count;

	sysLogPrintf(LOG_NOTE,
		"romextract pdmesh: written=%d skipped=%d failed=%d unique_mesh_jobs=%d",
		written, skipped, failed, s_SeenCount);

	if (failed == 0) {
		romExtractPdFastCacheWrite(ROMEXTRACT_PDMESH_FAST_CACHE_KIND,
			meshes_dir, ".pdmesh");
	}

	return written;
}
