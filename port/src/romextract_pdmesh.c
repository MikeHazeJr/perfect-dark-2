/**
 * romextract_pdmesh.c -- Catalog universality pivot Step 1 (2026-05-02).
 *
 * Walks the unique set of mesh references from g_ModelStates plus the
 * loader_pool weapon/head/body tables and emits one .pdmesh ZIP compound
 * per unique catalog mesh identity at data/<romid>/meshes/<id>.pdmesh.
 *
 * Compound layout per universality-pivot-schemas.md Section 2.5:
 *   mesh.ini             editable mesh descriptor
 *   _meta/manifest.json envelope + provenance
 *   _meta/*.json        shared inventory/provenance/validation/source handles
 *   model.obj           Wavefront OBJ converted from model display lists
 *   model.mtl           material stub for OBJ tooling
 *   model.nodes.json    original model node/matrix hierarchy
 *   model.parts.json    original model part table
 *   model.faces.json    original face-to-model-matrix bindings
 *   model.render.json   original render command stream by render node/group
 *   _meta/*.sha256     public-file SHA-256 sidecars
 *
 * Reuses port/src/modarchive.c writer subset for ZIP atomic writes.
 *
 * Step 1 cross-reference convention: emits source_filenum_symbol with
 * the FILE_* enum string (provenance hint) and the catalog ID is
 * synthesized from the symbol (e.g. FILE_GFALCON2 ->
 * base:model_falcon2_hi). Numeric filenums remain provenance metadata.
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
#include "assetcatalog.h"
#include "catalog_readable_ids.h"
#include "data.h"
#include "types.h"
#include "constants.h"
#include "files.h"
#include "fs.h"
#include "loader_enum_reverse.h"
#include "asset_archive_writer.h"
#include "modarchive.h"
#include "romdata.h"
#include "romextract.h"
#include "romextract_pd.h"
#include "modasset_compiler.h"
#include "system.h"
#include "preprocess.h"
#include "weapondata_authored.h"
#include "headdata_authored.h"
#include "bodydata_authored.h"
#include "game/modeldef.h"
#include "lib/model.h"
#include "lib/rzip.h"

/* Track filenums already emitted to avoid duplicate work when multiple
 * weapons / heads / bodies share a mesh. Cap covers every g_ModelStates
 * model plus weapon hi/lo, projectile/entity payloads, heads, bodies, and
 * body hand meshes with dedup headroom. */
#define ROMEXTRACT_PDMESH_SEEN_CAP 1024
#define ROMEXTRACT_PDMESH_MODEL_VMA 0x05000000u
#define ROMEXTRACT_PDMESH_MTX_STACK_CAP 11
#define ROMEXTRACT_PDMESH_NODE_DEPTH_CAP 2048
#define ROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION_LABEL "model_obj_mtx_v20_materials_hierarchy_parts_faces_json_relations_raw_mtx_render_commands_json"
#define ROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION ROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION_LABEL "\n"
#define ROMEXTRACT_PDMESH_FAST_CACHE_KIND "pdmesh_model_obj_mtx_v23_materials_hierarchy_parts_faces_json_relations_raw_mtx_render_commands_json_allmodels_menuhud_zero_tri_models"
extern u16 g_CartFileNums[];
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
	char name[64];
	char texture_catalog_id[128];
	char secondary_texture_catalog_id[128];
	s32 texnum;
	s32 texnum2;
	s32 subcmd;
	s32 smode;
	s32 tmode;
	s32 offset;
	s32 shifts;
	s32 shiftt;
	s32 min;
	s32 flag;
	u32 w0;
	u32 w1;
	u32 triangle_count;
} pdmesh_obj_material_t;

typedef struct {
	const u8       *base;
	u32             size;
	struct modeldef *modeldef;
	u16             source_filenum;
	s32             static_gun_model;
	pdmesh_textbuf_t *obj;
	pdmesh_textbuf_t *mtl;
	pdmesh_textbuf_t *nodes;
	pdmesh_textbuf_t *parts;
	pdmesh_textbuf_t *faces;
	pdmesh_textbuf_t *render;
	u32             next_index;
	u32             triangle_count;
	u32             render_cmd_count;
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
	const struct modelnode *node_ptrs[ROMEXTRACT_PDMESH_NODE_DEPTH_CAP];
	s32             node_count;
	s32             current_node_mtx_index;
	f32             mtx_stack[ROMEXTRACT_PDMESH_MTX_STACK_CAP][4][4];
	s32             mtx_stack_indices[ROMEXTRACT_PDMESH_MTX_STACK_CAP];
	s32             mtx_stack_size;
	pdmesh_obj_material_t *materials;
	u32             material_count;
	u32             material_cap;
	s32             current_material;
	s32             emitted_material;
	char            current_group[64];
	u32             material_switch_count;
} pdmesh_obj_export_t;

typedef struct {
	u32 triangle_count;
	u32 gdl_count;
	u32 vtx_cmd_count;
	u32 vtx_slot_count;
	u32 tri_cmd_count;
	u32 tri_attempt_count;
	u32 tri_missing_slot_count;
	u32 render_cmd_count;
	u32 vtx_bad_addr_count;
	u32 mtx_cmd_count;
	u32 popmtx_cmd_count;
	u32 mtx_model_ref_count;
	u32 mtx_bad_addr_count;
	u32 transformed_vertex_count;
	char skeleton_symbol[64];
	u32 material_count;
	u32 textured_material_count;
	u32 material_switch_count;
	u32 node_count;
	u32 part_count;
	f32 model_scale;
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

static s32 s_objMtxIndexValid(const pdmesh_obj_export_t *ctx, s32 idx);

static s32 s_objMtxInverseTransformPoint(const f32 m[4][4],
                                         const f32 in[3],
                                         f32 out[3])
{
	f32 a00 = m[0][0], a01 = m[0][1], a02 = m[0][2];
	f32 a10 = m[1][0], a11 = m[1][1], a12 = m[1][2];
	f32 a20 = m[2][0], a21 = m[2][1], a22 = m[2][2];
	f32 det =
		a00 * (a11 * a22 - a12 * a21) -
		a01 * (a10 * a22 - a12 * a20) +
		a02 * (a10 * a21 - a11 * a20);

	if (det > -0.000001f && det < 0.000001f) {
		return 0;
	}

	f32 invdet = 1.0f / det;
	f32 inv[3][3];
	inv[0][0] =  (a11 * a22 - a12 * a21) * invdet;
	inv[0][1] = -(a01 * a22 - a02 * a21) * invdet;
	inv[0][2] =  (a01 * a12 - a02 * a11) * invdet;
	inv[1][0] = -(a10 * a22 - a12 * a20) * invdet;
	inv[1][1] =  (a00 * a22 - a02 * a20) * invdet;
	inv[1][2] = -(a00 * a12 - a02 * a10) * invdet;
	inv[2][0] =  (a10 * a21 - a11 * a20) * invdet;
	inv[2][1] = -(a00 * a21 - a01 * a20) * invdet;
	inv[2][2] =  (a00 * a11 - a01 * a10) * invdet;

	f32 x = in[0] - m[3][0];
	f32 y = in[1] - m[3][1];
	f32 z = in[2] - m[3][2];
	out[0] = x * inv[0][0] + y * inv[1][0] + z * inv[2][0];
	out[1] = x * inv[0][1] + y * inv[1][1] + z * inv[2][1];
	out[2] = x * inv[0][2] + y * inv[1][2] + z * inv[2][2];
	return 1;
}

static void s_objMakeNodeLocalPoint(pdmesh_obj_export_t *ctx, f32 point[3])
{
	f32 local[3];

	if (!ctx || !s_objMtxIndexValid(ctx, ctx->current_node_mtx_index)) {
		return;
	}

	if (s_objMtxInverseTransformPoint(
			ctx->model_matrices[ctx->current_node_mtx_index],
			point, local)) {
		point[0] = local[0];
		point[1] = local[1];
		point[2] = local[2];
	}
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

static void s_objRegisterNodes(pdmesh_obj_export_t *ctx,
                               const struct modelnode *node,
                               s32 depth)
{
	if (!ctx || !node || depth > ROMEXTRACT_PDMESH_NODE_DEPTH_CAP ||
			!s_ptrInModel(ctx, node, sizeof(*node)) ||
			ctx->node_count >= ROMEXTRACT_PDMESH_NODE_DEPTH_CAP) {
		return;
	}

	ctx->node_ptrs[ctx->node_count++] = node;

	if (node->child) {
		s_objRegisterNodes(ctx, node->child, depth + 1);
	}
	if (node->next) {
		s_objRegisterNodes(ctx, node->next, depth + 1);
	}
}

static s32 s_objNodeId(const pdmesh_obj_export_t *ctx,
                       const struct modelnode *node)
{
	if (!ctx || !node) {
		return -1;
	}
	for (s32 i = 0; i < ctx->node_count; i++) {
		if (ctx->node_ptrs[i] == node) {
			return i;
		}
	}
	return -1;
}

static s32 s_objParentMtxIndex(const pdmesh_obj_export_t *ctx,
                               const struct modelnode *node)
{
	const struct modelnode *parent = node ? node->parent : NULL;
	s32 guard = 0;
	while (parent && s_ptrInModel(ctx, parent, sizeof(*parent)) &&
			guard++ < ROMEXTRACT_PDMESH_NODE_DEPTH_CAP) {
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
                                           struct modelnode *node,
                                           s32 depth)
{
	if (!node || !s_ptrInModel(ctx, node, sizeof(*node))) return;
	if (depth > ROMEXTRACT_PDMESH_NODE_DEPTH_CAP) return;

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

	if (node->child) {
		s_objBuildDefaultModelMatrices(ctx, node->child, depth + 1);
	}
	if (node->next) {
		s_objBuildDefaultModelMatrices(ctx, node->next, depth + 1);
	}
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
	s32 guard = 0;

	if (!ctx || !ctx->static_gun_model) {
		return 0;
	}

	while (cur && s_ptrInModel(ctx, cur, sizeof(*cur)) &&
			guard++ < ROMEXTRACT_PDMESH_NODE_DEPTH_CAP) {
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
	s32 guard = 0;

	if (!ctx || !node || !target) {
		return 0;
	}

	while (cur && s_ptrInModel(ctx, cur, sizeof(*cur)) &&
			guard++ < ROMEXTRACT_PDMESH_NODE_DEPTH_CAP) {
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
                           f32 out[4][4], u32 *from_model_table,
                           s32 *model_matrix_index)
{
	if (from_model_table) *from_model_table = 0;
	if (model_matrix_index) *model_matrix_index = -1;
	if (!raw) return 0;

	if (raw & 1u) {
		u32 seg = (u32)((raw & 0x0f000000u) >> 24);
		u32 off = (u32)(raw & 0x00fffffeu);
		if (seg == SPSEGMENT_MODEL_MTX && ctx->model_matrices) {
			s32 idx = (s32)(off / sizeof(Mtxf));
			if ((off % sizeof(Mtxf)) == 0 && s_objMtxIndexValid(ctx, idx)) {
				s_objMtxCopy(out, ctx->model_matrices[idx]);
				if (from_model_table) *from_model_table = 1;
				if (model_matrix_index) *model_matrix_index = idx;
				return 1;
			}
			idx = (s32)(off / sizeof(Mtx));
			if ((off % sizeof(Mtx)) == 0 && s_objMtxIndexValid(ctx, idx)) {
				s_objMtxCopy(out, ctx->model_matrices[idx]);
				if (from_model_table) *from_model_table = 1;
				if (model_matrix_index) *model_matrix_index = idx;
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

static const char *s_objCurrentGroup(const pdmesh_obj_export_t *ctx)
{
	return ctx && ctx->current_group[0] ? ctx->current_group : "default";
}

static s32 s_textbufAppendJsonString(pdmesh_textbuf_t *b, const char *text)
{
	if (s_textbufAppend(b, "\"") != 0) {
		return -1;
	}
	if (text) {
		for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
			switch (*p) {
			case '\\':
				if (s_textbufAppend(b, "\\\\") != 0) return -1;
				break;
			case '"':
				if (s_textbufAppend(b, "\\\"") != 0) return -1;
				break;
			case '\n':
				if (s_textbufAppend(b, "\\n") != 0) return -1;
				break;
			case '\r':
				if (s_textbufAppend(b, "\\r") != 0) return -1;
				break;
			case '\t':
				if (s_textbufAppend(b, "\\t") != 0) return -1;
				break;
			default:
				if (*p < 0x20) {
					if (s_textbufAppendf(b, "\\u%04x", (unsigned)*p) != 0) {
						return -1;
					}
				} else if (s_textbufAppendf(b, "%c", *p) != 0) {
					return -1;
				}
				break;
			}
		}
	}
	return s_textbufAppend(b, "\"");
}

static s32 s_objAppendRenderRow(pdmesh_obj_export_t *ctx,
                                const char *op,
                                s32 face,
                                s32 matrix,
                                u8 parameters,
                                s32 material)
{
	if (!ctx || !ctx->render || !op) {
		return 0;
	}
	if (ctx->render_cmd_count > 0 &&
			s_textbufAppend(ctx->render, ",\n") != 0) {
		return -1;
	}
	if (s_textbufAppend(ctx->render, "    { \"group\": ") != 0 ||
			s_textbufAppendJsonString(ctx->render, s_objCurrentGroup(ctx)) != 0 ||
			s_textbufAppend(ctx->render, ", \"command\": ") != 0 ||
			s_textbufAppendJsonString(ctx->render, op) != 0 ||
			s_textbufAppendf(ctx->render,
				", \"face_index\": %d, \"matrix_index\": %d, "
				"\"matrix_flags\": %u, \"matrix_mode\": { "
				"\"projection\": %s, \"load\": %s, \"push\": %s }, "
				"\"material_index\": %d }",
				face, matrix, (unsigned)parameters,
				(parameters & G_MTX_PROJECTION) ? "true" : "false",
				(parameters & G_MTX_LOAD) ? "true" : "false",
				(parameters & G_MTX_PUSH) ? "true" : "false",
				material) != 0) {
		return -1;
	}
	ctx->render_cmd_count++;
	return 0;
}

static s32 s_objApplyMtxCommand(pdmesh_obj_export_t *ctx,
                                u8 parameters,
                                uintptr_t raw,
                                s32 *out_model_matrix_index)
{
	f32 matrix[4][4];
	u32 from_model_table = 0;
	s32 model_matrix_index = -1;
	if (out_model_matrix_index) *out_model_matrix_index = -1;
	ctx->mtx_cmd_count++;
	if (!s_objResolveMtx(ctx, raw, matrix, &from_model_table,
			&model_matrix_index)) {
		ctx->mtx_bad_addr_count++;
		return 0;
	}
	if (from_model_table) ctx->mtx_model_ref_count++;
	if (out_model_matrix_index) {
		*out_model_matrix_index = model_matrix_index;
	}
	if (parameters & G_MTX_PROJECTION) return 1;

	if (ctx->mtx_stack_size <= 0) {
		ctx->mtx_stack_size = 1;
		s_objMtxIdentity(ctx->mtx_stack[0]);
		ctx->mtx_stack_indices[0] = ctx->current_node_mtx_index;
	}
	if ((parameters & G_MTX_PUSH) &&
	    ctx->mtx_stack_size < ROMEXTRACT_PDMESH_MTX_STACK_CAP) {
		s_objMtxCopy(ctx->mtx_stack[ctx->mtx_stack_size],
		             ctx->mtx_stack[ctx->mtx_stack_size - 1]);
		ctx->mtx_stack_indices[ctx->mtx_stack_size] =
			ctx->mtx_stack_indices[ctx->mtx_stack_size - 1];
		ctx->mtx_stack_size++;
	}
	if (parameters & G_MTX_LOAD) {
		s_objMtxCopy(ctx->mtx_stack[ctx->mtx_stack_size - 1], matrix);
		ctx->mtx_stack_indices[ctx->mtx_stack_size - 1] =
			model_matrix_index;
	} else {
		s_objMtxMul(ctx->mtx_stack[ctx->mtx_stack_size - 1],
		            matrix,
		            ctx->mtx_stack[ctx->mtx_stack_size - 1]);
		ctx->mtx_stack_indices[ctx->mtx_stack_size - 1] = -1;
	}
	return 1;
}

static void s_objMaterialNameFromCatalog(const char *catalog_id, u32 ordinal,
                                         char *out, size_t out_n)
{
	char alpha[24];
	char slug[48];
	size_t j = 0;

	if (!out || out_n == 0) {
		return;
	}

	out[0] = '\0';
	catalogReadableAlphaOrdinal((s32)ordinal, alpha, sizeof(alpha));

	if (catalog_id) {
		for (size_t i = 0; catalog_id[i] && j + 1 < sizeof(slug); i++) {
			u8 c = (u8)catalog_id[i];
			if (isalnum(c)) {
				slug[j++] = (char)tolower(c);
			} else if (j > 0 && slug[j - 1] != '_') {
				slug[j++] = '_';
			}
		}
	}
	while (j > 0 && slug[j - 1] == '_') {
		j--;
	}
	slug[j] = '\0';

	snprintf(out, out_n, "mat_%s_%s", alpha, slug[0] ? slug : "texture");
}

static s32 s_objReserveMaterials(pdmesh_obj_export_t *ctx, u32 add)
{
	pdmesh_obj_material_t *p;
	u32 need;
	u32 cap;

	if (!ctx || add > 0xffffffffu - ctx->material_count) {
		return -1;
	}

	need = ctx->material_count + add;
	if (need <= ctx->material_cap) {
		return 0;
	}

	cap = ctx->material_cap ? ctx->material_cap : 8;
	while (cap < need) {
		if (cap > 0x80000000u) {
			return -1;
		}
		cap *= 2;
	}

	p = (pdmesh_obj_material_t *)realloc(ctx->materials,
		(size_t)cap * sizeof(*ctx->materials));
	if (!p) {
		return -1;
	}

	memset(p + ctx->material_cap, 0,
		(size_t)(cap - ctx->material_cap) * sizeof(*p));
	ctx->materials = p;
	ctx->material_cap = cap;
	return 0;
}

static s32 s_objEnsureDefaultMaterial(pdmesh_obj_export_t *ctx)
{
	if (!ctx) {
		return -1;
	}

	for (u32 i = 0; i < ctx->material_count; i++) {
		if (strcmp(ctx->materials[i].name, "pd_default") == 0) {
			return (s32)i;
		}
	}

	if (s_objReserveMaterials(ctx, 1) != 0) {
		return -1;
	}

	pdmesh_obj_material_t *m = &ctx->materials[ctx->material_count];
	memset(m, 0, sizeof(*m));
	strncpy(m->name, "pd_default", sizeof(m->name) - 1);
	m->name[sizeof(m->name) - 1] = '\0';
	m->texnum = -1;
	m->texnum2 = -1;
	m->subcmd = -1;
	ctx->material_count++;
	return (s32)(ctx->material_count - 1u);
}

static s32 s_objEnsureTextureMaterial(pdmesh_obj_export_t *ctx, s32 texnum,
                                      s32 texnum2, s32 subcmd,
                                      u32 w0, u32 w1)
{
	s32 smode = (s32)((w0 >> 22) & 3u);
	s32 tmode = (s32)((w0 >> 20) & 3u);
	s32 offset = (s32)((w0 >> 18) & 3u);
	s32 shifts = (s32)((w0 >> 14) & 0x0fu);
	s32 shiftt = (s32)((w0 >> 10) & 0x0fu);
	s32 min = (s32)((w1 >> 24) & 0xffu);
	s32 flag = (w0 & 0x200u) ? 1 : 0;

	if (!ctx || texnum < 0 || texnum >= NUM_TEXTURES) {
		return -1;
	}

	for (u32 i = 0; i < ctx->material_count; i++) {
		pdmesh_obj_material_t *m = &ctx->materials[i];
		if (m->texnum == texnum && m->texnum2 == texnum2 &&
		    m->subcmd == subcmd && m->w0 == w0 && m->w1 == w1) {
			return (s32)i;
		}
	}

	if (s_objReserveMaterials(ctx, 1) != 0) {
		return -1;
	}

	pdmesh_obj_material_t *m = &ctx->materials[ctx->material_count];
	memset(m, 0, sizeof(*m));
	m->texnum = texnum;
	m->texnum2 = texnum2;
	m->subcmd = subcmd;
	m->smode = smode;
	m->tmode = tmode;
	m->offset = offset;
	m->shifts = shifts;
	m->shiftt = shiftt;
	m->min = min;
	m->flag = flag;
	m->w0 = w0;
	m->w1 = w1;

	catalogReadableTextureId(texnum, m->texture_catalog_id,
		sizeof(m->texture_catalog_id));
	if (texnum2 >= 0 && texnum2 < NUM_TEXTURES) {
		catalogReadableTextureId(texnum2, m->secondary_texture_catalog_id,
			sizeof(m->secondary_texture_catalog_id));
	}
	s_objMaterialNameFromCatalog(m->texture_catalog_id,
		ctx->material_count, m->name, sizeof(m->name));

	ctx->material_count++;
	return (s32)(ctx->material_count - 1u);
}

static s32 s_objFinalizeMaterials(pdmesh_obj_export_t *ctx)
{
	if (!ctx || !ctx->mtl) {
		return -1;
	}
	if (ctx->material_count == 0 &&
			s_objEnsureDefaultMaterial(ctx) < 0) {
		return -1;
	}

	if (s_textbufAppend(ctx->mtl,
			"# Perfect Dark 2 base model material export\n") != 0) {
		return -1;
	}

	for (u32 i = 0; i < ctx->material_count; i++) {
		const pdmesh_obj_material_t *m = &ctx->materials[i];
		if (s_textbufAppendf(ctx->mtl,
				"\nnewmtl %s\n"
				"Kd 1.000000 1.000000 1.000000\n"
				"Ka 0.150000 0.150000 0.150000\n"
				"Ks 0.000000 0.000000 0.000000\n"
				"d 1.000000\n",
				m->name) != 0) {
			return -1;
		}

		if (m->texture_catalog_id[0]) {
			if (s_textbufAppendf(ctx->mtl,
					"pd_texture_catalog = %s\n"
					"pd_texture_subcmd = %d\n"
					"pd_texture_smode = %d\n"
					"pd_texture_tmode = %d\n"
					"pd_texture_offset = %d\n"
					"pd_texture_shifts = %d\n"
					"pd_texture_shiftt = %d\n"
					"pd_texture_min = %d\n"
					"pd_texture_flag = %d\n",
					m->texture_catalog_id,
					m->subcmd,
					m->smode,
					m->tmode,
					m->offset,
					m->shifts,
					m->shiftt,
					m->min,
					m->flag) != 0) {
				return -1;
			}
			if (m->secondary_texture_catalog_id[0] &&
					s_textbufAppendf(ctx->mtl,
						"pd_secondary_texture_catalog = %s\n",
						m->secondary_texture_catalog_id) != 0) {
				return -1;
			}
		}
	}

	return 0;
}

static s32 s_objEmitTri(pdmesh_obj_export_t *ctx,
                        const Vtx *a, const Vtx *b, const Vtx *c)
{
	if (!a || !b || !c) return 0;
	if (ctx->mtx_stack_size <= 0) {
		ctx->mtx_stack_size = 1;
		s_objMtxIdentity(ctx->mtx_stack[0]);
		ctx->mtx_stack_indices[0] = ctx->current_node_mtx_index;
	}
	f32 va[3], vb[3], vc[3];
	s32 face_mtx = ctx->mtx_stack_indices[ctx->mtx_stack_size - 1];
	if (s_objMtxIndexValid(ctx, face_mtx)) {
		va[0] = a->x; va[1] = a->y; va[2] = a->z;
		vb[0] = b->x; vb[1] = b->y; vb[2] = b->z;
		vc[0] = c->x; vc[1] = c->y; vc[2] = c->z;
	} else {
		face_mtx = ctx->current_node_mtx_index;
		s_objMtxTransformPoint(ctx->mtx_stack[ctx->mtx_stack_size - 1], a, va);
		s_objMtxTransformPoint(ctx->mtx_stack[ctx->mtx_stack_size - 1], b, vb);
		s_objMtxTransformPoint(ctx->mtx_stack[ctx->mtx_stack_size - 1], c, vc);
		s_objMakeNodeLocalPoint(ctx, va);
		s_objMakeNodeLocalPoint(ctx, vb);
		s_objMakeNodeLocalPoint(ctx, vc);
		if (!s_objMtxIndexValid(ctx, face_mtx)) {
			face_mtx = 0;
		}
	}
	u32 i0 = ctx->next_index++;
	u32 i1 = ctx->next_index++;
	u32 i2 = ctx->next_index++;
	s32 material_index = ctx->current_material;
	if (material_index < 0 ||
			(u32)material_index >= ctx->material_count) {
		material_index = s_objEnsureDefaultMaterial(ctx);
		if (material_index < 0) {
			return -1;
		}
		ctx->current_material = material_index;
	}
	if (ctx->emitted_material != material_index) {
		const pdmesh_obj_material_t *m = &ctx->materials[material_index];
		if (s_textbufAppendf(ctx->obj, "usemtl %s\n", m->name) != 0) {
			return -1;
		}
		ctx->emitted_material = material_index;
		ctx->material_switch_count++;
	}
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
	if (ctx->faces) {
		u32 face_index = ctx->triangle_count - 1u;
		if ((face_index > 0 && s_textbufAppend(ctx->faces, ",\n") != 0) ||
				s_textbufAppendf(ctx->faces,
					"    { \"face_index\": %u, \"matrix_index\": %d }",
					(unsigned)face_index, face_mtx) != 0) {
			return -1;
		}
	}
	if (s_objAppendRenderRow(ctx, "tri", (s32)(ctx->triangle_count - 1u),
			face_mtx, 0, material_index) != 0) {
		return -1;
	}
	ctx->materials[material_index].triangle_count++;
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

		if (cmd == (u8)G_NOOP) {
			s32 texnum = (s32)(w1 & 0xfffu);
			s32 texnum2 = -1;
			s32 subcmd = (s32)gdl[cmdidx].unkc0.subcmd;
			if (subcmd == 1) {
				texnum2 = (s32)((w1 >> 12) & 0xfffu);
			}
			if (texnum >= 0 && texnum < NUM_TEXTURES) {
				s32 mat = s_objEnsureTextureMaterial(ctx, texnum,
					(texnum2 >= 0 && texnum2 < NUM_TEXTURES) ? texnum2 : -1,
					subcmd, w0, w1);
				if (mat >= 0) {
					ctx->current_material = mat;
					if (s_objAppendRenderRow(ctx, "material", -1, -1,
							0, mat) != 0) {
						return -1;
					}
				}
			}
			continue;
		}

		if (cmd == (u8)G_DL) {
			Gfx *child = (Gfx *)(((uintptr_t)w1) & ~(uintptr_t)1);
			if (s_exportGdlToObj(ctx, child, vbuf, numverts, depth + 1) != 0) {
				return -1;
			}
			continue;
		}

		if (cmd == (u8)G_MTX) {
			u8 parameters = (u8)((w0 >> 16) & 0xffu);
			s32 model_matrix_index = -1;
			if (s_objApplyMtxCommand(ctx, parameters, (uintptr_t)w1,
					&model_matrix_index) &&
					!(parameters & G_MTX_PROJECTION)) {
				if (s_objAppendRenderRow(ctx, "mtx", -1,
						model_matrix_index, parameters, -1) != 0) {
					return -1;
				}
			}
			continue;
		}

		if (cmd == (u8)G_POPMTX) {
			ctx->popmtx_cmd_count++;
			if (ctx->mtx_stack_size > 1) ctx->mtx_stack_size--;
			if (s_objAppendRenderRow(ctx, "pop", -1, -1,
					0, -1) != 0) {
				return -1;
			}
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

	s32 node_id = s_objNodeId(ctx, node);
	char saved_group[sizeof(ctx->current_group)];
	strncpy(saved_group, ctx->current_group, sizeof(saved_group));
	saved_group[sizeof(saved_group) - 1] = '\0';
	if (node_id < 0) {
		node_id = ctx->node_count;
	}
	snprintf(ctx->current_group, sizeof(ctx->current_group),
		"node_%d_gundl", node_id);

	pdmesh_textbuf_t trial_buf = {0};
	pdmesh_textbuf_t trial_faces = {0};
	pdmesh_textbuf_t trial_render = {0};
	pdmesh_obj_export_t trial = *ctx;
	trial.obj = &trial_buf;
	trial.faces = &trial_faces;
	trial.render = &trial_render;
	if (ctx->material_count > 0) {
		trial.materials = malloc((size_t)ctx->material_count *
			sizeof(*trial.materials));
		if (!trial.materials) {
			s_textbufFree(&trial_buf);
			s_textbufFree(&trial_faces);
			s_textbufFree(&trial_render);
			strncpy(ctx->current_group, saved_group,
				sizeof(ctx->current_group));
			ctx->current_group[sizeof(ctx->current_group) - 1] = '\0';
			return -1;
		}
		memcpy(trial.materials, ctx->materials,
			(size_t)ctx->material_count * sizeof(*trial.materials));
		trial.material_cap = ctx->material_count;
	}

	if (s_exportGdlToObj(&trial, gundl->opagdl, gundl->vertices,
			gundl->numvertices, 0) != 0 ||
	    s_exportGdlToObj(&trial, gundl->xlugdl, gundl->vertices,
			gundl->numvertices, 0) != 0) {
		s_textbufFree(&trial_buf);
		s_textbufFree(&trial_faces);
		s_textbufFree(&trial_render);
		free(trial.materials);
		strncpy(ctx->current_group, saved_group,
			sizeof(ctx->current_group));
		ctx->current_group[sizeof(ctx->current_group) - 1] = '\0';
		return -1;
	}

	if (s_objMayCullDetachedGunEffects(ctx) &&
	    s_objTextLooksDetachedGunEffect(&trial_buf)) {
		s_textbufFree(&trial_buf);
		s_textbufFree(&trial_faces);
		s_textbufFree(&trial_render);
		free(trial.materials);
		strncpy(ctx->current_group, saved_group,
			sizeof(ctx->current_group));
		ctx->current_group[sizeof(ctx->current_group) - 1] = '\0';
		return 0;
	}

	if (trial_buf.len > 0) {
		pdmesh_textbuf_t *real_obj = ctx->obj;
		pdmesh_textbuf_t *real_faces = ctx->faces;
		pdmesh_textbuf_t *real_render = ctx->render;
		if (s_textbufAppendf(real_obj, "g %s\n",
				ctx->current_group) != 0 ||
		    s_textbufAppend(real_obj, trial_buf.data) != 0 ||
		    s_textbufAppend(real_faces, trial_faces.data ?
				trial_faces.data : "") != 0 ||
		    s_textbufAppend(real_render, trial_render.data ?
				trial_render.data : "") != 0) {
			s_textbufFree(&trial_buf);
			s_textbufFree(&trial_faces);
			s_textbufFree(&trial_render);
			free(trial.materials);
			strncpy(ctx->current_group, saved_group,
				sizeof(ctx->current_group));
			ctx->current_group[sizeof(ctx->current_group) - 1] = '\0';
			return -1;
		}
		free(ctx->materials);
		*ctx = trial;
		ctx->obj = real_obj;
		ctx->faces = real_faces;
		ctx->render = real_render;
		strncpy(ctx->current_group, saved_group,
			sizeof(ctx->current_group));
		ctx->current_group[sizeof(ctx->current_group) - 1] = '\0';
		trial.materials = NULL;
	}

	s_textbufFree(&trial_buf);
	s_textbufFree(&trial_faces);
	s_textbufFree(&trial_render);
	free(trial.materials);
	strncpy(ctx->current_group, saved_group, sizeof(ctx->current_group));
	ctx->current_group[sizeof(ctx->current_group) - 1] = '\0';
	return 0;
}

static s32 s_exportNodeObj(pdmesh_obj_export_t *ctx, struct modelnode *node,
                           s32 depth)
{
	if (!node || !s_ptrInModel(ctx, node, sizeof(*node))) return 0;
	if (depth > ROMEXTRACT_PDMESH_NODE_DEPTH_CAP) return 0;
	if (s_objNodeUnderHiddenGunToggle(ctx, node) ||
	    s_objHiddenGunToggleTargetsNode(ctx, ctx->modeldef ?
			ctx->modeldef->rootnode : NULL, node, 0)) {
		if (node->next && s_exportNodeObj(ctx, node->next, depth + 1) != 0) return -1;
		return 0;
	}

	u32 type = node->type & 0xff;
	if (type == MODELNODETYPE_DL && node->rodata &&
	    s_ptrInModel(ctx, node->rodata, sizeof(struct modelrodata_dl))) {
		struct modelrodata_dl *dl = &node->rodata->dl;
		s32 saved_mtx = ctx->current_node_mtx_index;
		s32 node_id = s_objNodeId(ctx, node);
		char saved_group[sizeof(ctx->current_group)];
		strncpy(saved_group, ctx->current_group, sizeof(saved_group));
		saved_group[sizeof(saved_group) - 1] = '\0';
		ctx->current_node_mtx_index = s_objParentMtxIndex(ctx, node);
		if (ctx->mtx_stack_size > 0) {
			ctx->mtx_stack_indices[ctx->mtx_stack_size - 1] =
				ctx->current_node_mtx_index;
		}
		snprintf(ctx->current_group, sizeof(ctx->current_group),
			"node_%d_dl", node_id);
		(void)s_textbufAppendf(ctx->obj, "g %s\n", ctx->current_group);
		if (s_exportGdlToObj(ctx, dl->opagdl, dl->vertices, dl->numvertices, 0) != 0) return -1;
		if (s_exportGdlToObj(ctx, dl->xlugdl, dl->vertices, dl->numvertices, 0) != 0) return -1;
		ctx->current_node_mtx_index = saved_mtx;
		strncpy(ctx->current_group, saved_group,
			sizeof(ctx->current_group));
		ctx->current_group[sizeof(ctx->current_group) - 1] = '\0';
	} else if (type == MODELNODETYPE_GUNDL && node->rodata &&
	           s_ptrInModel(ctx, node->rodata, sizeof(struct modelrodata_gundl))) {
		struct modelrodata_gundl *gundl = &node->rodata->gundl;
		s32 saved_mtx = ctx->current_node_mtx_index;
		ctx->current_node_mtx_index = s_objParentMtxIndex(ctx, node);
		if (ctx->mtx_stack_size > 0) {
			ctx->mtx_stack_indices[ctx->mtx_stack_size - 1] =
				ctx->current_node_mtx_index;
		}
		if (s_exportGunDlToObj(ctx, node, gundl) != 0) return -1;
		ctx->current_node_mtx_index = saved_mtx;
	} else if (type == MODELNODETYPE_STARGUNFIRE && node->rodata &&
	           s_ptrInModel(ctx, node->rodata, sizeof(struct modelrodata_stargunfire))) {
		struct modelrodata_stargunfire *star = &node->rodata->stargunfire;
		s32 saved_mtx = ctx->current_node_mtx_index;
		s32 node_id = s_objNodeId(ctx, node);
		char saved_group[sizeof(ctx->current_group)];
		strncpy(saved_group, ctx->current_group, sizeof(saved_group));
		saved_group[sizeof(saved_group) - 1] = '\0';
		ctx->current_node_mtx_index = s_objParentMtxIndex(ctx, node);
		if (ctx->mtx_stack_size > 0) {
			ctx->mtx_stack_indices[ctx->mtx_stack_size - 1] =
				ctx->current_node_mtx_index;
		}
		snprintf(ctx->current_group, sizeof(ctx->current_group),
			"node_%d_stargunfire", node_id);
		(void)s_textbufAppendf(ctx->obj, "g %s\n", ctx->current_group);
		if (s_exportGdlToObj(ctx, star->gdl, star->vertices, 4, 0) != 0) return -1;
		ctx->current_node_mtx_index = saved_mtx;
		strncpy(ctx->current_group, saved_group,
			sizeof(ctx->current_group));
		ctx->current_group[sizeof(ctx->current_group) - 1] = '\0';
	}

	if (node->child && s_exportNodeObj(ctx, node->child, depth + 1) != 0) return -1;
	if (node->next && s_exportNodeObj(ctx, node->next, depth + 1) != 0) return -1;
	return 0;
}

static const char *s_objRenderGroupSuffix(u32 type)
{
	switch (type & 0xff) {
	case MODELNODETYPE_DL:
		return "dl";
	case MODELNODETYPE_GUNDL:
		return "gundl";
	case MODELNODETYPE_STARGUNFIRE:
		return "stargunfire";
	default:
		return "";
	}
}

static s32 s_writeNodeHierarchyJson(pdmesh_obj_export_t *ctx)
{
	if (!ctx || !ctx->nodes) {
		return -1;
	}

	if (s_textbufAppend(ctx->nodes,
			"{\n"
			"  \"pd_kind\": \"mesh_nodes\",\n"
			"  \"pd_schema_version\": 1,\n"
			"  \"nodes\": [\n") != 0) {
		return -1;
	}

	for (s32 i = 0; i < ctx->node_count; i++) {
		const struct modelnode *node = ctx->node_ptrs[i];
		u32 type = node ? (node->type & 0xff) : 0;
		s32 parent = node ? s_objNodeId(ctx, node->parent) : -1;
		s32 partnum = s_objPartNumForNode(ctx, node);
		s32 part = -1;
		s32 mtx0 = -1;
		s32 mtx1 = -1;
		s32 mtx2 = -1;
		f32 pos_x = 0.0f;
		f32 pos_y = 0.0f;
		f32 pos_z = 0.0f;
		f32 drawdist = 0.0f;
		s32 target = -1;
		char group[64] = "-";
		s32 render_mtx = -1;
		s32 mcount = 1;
		s32 hitpart = 0;
		f32 xmin = 0.0f, xmax = 0.0f;
		f32 ymin = 0.0f, ymax = 0.0f;
		f32 zmin = 0.0f, zmax = 0.0f;
		f32 distance_near = 0.0f, distance_far = 0.0f;
		f32 reorder_x = 0.0f, reorder_y = 0.0f, reorder_z = 0.0f;
		f32 reorder_axis_x = 0.0f, reorder_axis_y = 0.0f, reorder_axis_z = 0.0f;
		s32 reorder_target_a = -1, reorder_target_b = -1, reorder_side = 0;

		if (!node || !s_ptrInModel(ctx, node, sizeof(*node))) {
			continue;
		}

		switch (type) {
		case MODELNODETYPE_CHRINFO:
			if (node->rodata &&
					s_ptrInModel(ctx, node->rodata,
						sizeof(struct modelrodata_chrinfo))) {
				part = node->rodata->chrinfo.animpart;
				mtx0 = node->rodata->chrinfo.mtxindex;
			}
			break;
		case MODELNODETYPE_POSITION:
			if (node->rodata &&
					s_ptrInModel(ctx, node->rodata,
						sizeof(struct modelrodata_position))) {
				struct modelrodata_position *pos = &node->rodata->position;
				part = pos->part;
				mtx0 = pos->mtxindex0;
				mtx1 = pos->mtxindex1;
				mtx2 = pos->mtxindex2;
				pos_x = pos->pos.x;
				pos_y = pos->pos.y;
				pos_z = pos->pos.z;
				drawdist = pos->drawdist;
			}
			break;
		case MODELNODETYPE_POSITIONHELD:
			if (node->rodata &&
					s_ptrInModel(ctx, node->rodata,
						sizeof(struct modelrodata_positionheld))) {
				struct modelrodata_positionheld *pos =
					&node->rodata->positionheld;
				mtx0 = pos->mtxindex;
				pos_x = pos->pos.x;
				pos_y = pos->pos.y;
				pos_z = pos->pos.z;
			}
			break;
		case MODELNODETYPE_TOGGLE:
			if (node->rodata &&
					s_ptrInModel(ctx, node->rodata,
						sizeof(struct modelrodata_toggle))) {
				target = s_objNodeId(ctx, node->rodata->toggle.target);
			}
			break;
		case MODELNODETYPE_DISTANCE:
			if (node->rodata &&
					s_ptrInModel(ctx, node->rodata,
						sizeof(struct modelrodata_distance))) {
				struct modelrodata_distance *distance = &node->rodata->distance;
				distance_near = distance->near;
				distance_far = distance->far;
				target = s_objNodeId(ctx, distance->target);
			}
			break;
		case MODELNODETYPE_REORDER:
			if (node->rodata &&
					s_ptrInModel(ctx, node->rodata,
						sizeof(struct modelrodata_reorder))) {
				struct modelrodata_reorder *reorder = &node->rodata->reorder;
				reorder_x = reorder->unk00;
				reorder_y = reorder->unk04;
				reorder_z = reorder->unk08;
				reorder_axis_x = reorder->unk0c[0];
				reorder_axis_y = reorder->unk0c[1];
				reorder_axis_z = reorder->unk0c[2];
				reorder_target_a = s_objNodeId(ctx, reorder->unk18);
				reorder_target_b = s_objNodeId(ctx, reorder->unk1c);
				reorder_side = reorder->side;
			}
			break;
		case MODELNODETYPE_BBOX:
			if (node->rodata &&
					s_ptrInModel(ctx, node->rodata,
						sizeof(struct modelrodata_bbox))) {
				struct modelrodata_bbox *bbox = &node->rodata->bbox;
				hitpart = bbox->hitpart;
				xmin = bbox->xmin;
				xmax = bbox->xmax;
				ymin = bbox->ymin;
				ymax = bbox->ymax;
				zmin = bbox->zmin;
				zmax = bbox->zmax;
			}
			break;
		case MODELNODETYPE_DL:
			if (node->rodata &&
					s_ptrInModel(ctx, node->rodata,
						sizeof(struct modelrodata_dl))) {
				mcount = node->rodata->dl.mcount;
			}
			/* fall through */
		case MODELNODETYPE_GUNDL:
			if (type == MODELNODETYPE_GUNDL && node->rodata &&
					s_ptrInModel(ctx, node->rodata,
						sizeof(struct modelrodata_gundl))) {
				mcount = node->rodata->gundl.unk12;
			}
			/* fall through */
		case MODELNODETYPE_STARGUNFIRE:
			if (s_objRenderGroupSuffix(type)[0]) {
				snprintf(group, sizeof(group), "node_%d_%s", i,
					s_objRenderGroupSuffix(type));
				render_mtx = s_objParentMtxIndex(ctx, node);
			}
			break;
		default:
			break;
		}

		if (i > 0 && s_textbufAppend(ctx->nodes, ",\n") != 0) {
			return -1;
		}
		if (s_textbufAppendf(ctx->nodes,
				"    { \"id\": %d, \"parent\": %d, \"type\": %u, "
				"\"partnum\": %d, \"part\": %d, "
				"\"mtx0\": %d, \"mtx1\": %d, \"mtx2\": %d, "
				"\"position\": { \"x\": %.6f, \"y\": %.6f, \"z\": %.6f }, "
				"\"drawdist\": %.6f, \"target\": %d, \"group\": ",
				i, parent, (unsigned)type, partnum, part, mtx0, mtx1, mtx2,
				(double)pos_x, (double)pos_y, (double)pos_z,
				(double)drawdist, target) != 0 ||
				s_textbufAppendJsonString(ctx->nodes, group) != 0 ||
				s_textbufAppendf(ctx->nodes,
					", \"render_mtx\": %d, \"mcount\": %d, "
					"\"hitpart\": %d, "
					"\"bounds\": { \"xmin\": %.6f, \"xmax\": %.6f, "
					"\"ymin\": %.6f, \"ymax\": %.6f, "
					"\"zmin\": %.6f, \"zmax\": %.6f }, "
					"\"distance\": { \"near\": %.6f, \"far\": %.6f }, "
					"\"reorder\": { "
					"\"pivot\": { \"x\": %.6f, \"y\": %.6f, \"z\": %.6f }, "
					"\"axis\": { \"x\": %.6f, \"y\": %.6f, \"z\": %.6f }, "
					"\"target_a\": %d, \"target_b\": %d, \"side\": %d } }",
					render_mtx, mcount, hitpart,
					(double)xmin, (double)xmax, (double)ymin,
					(double)ymax, (double)zmin, (double)zmax,
					(double)distance_near, (double)distance_far,
					(double)reorder_x, (double)reorder_y, (double)reorder_z,
					(double)reorder_axis_x, (double)reorder_axis_y,
					(double)reorder_axis_z, reorder_target_a,
					reorder_target_b, reorder_side) != 0) {
			return -1;
		}
	}

	return s_textbufAppend(ctx->nodes, "\n  ]\n}\n");
}

static s32 s_writePartTableJson(pdmesh_obj_export_t *ctx)
{
	if (!ctx || !ctx->parts || !ctx->modeldef ||
			ctx->modeldef->numparts <= 0) {
		if (ctx && ctx->parts) {
			return s_textbufAppend(ctx->parts,
				"{\n"
				"  \"pd_kind\": \"mesh_parts\",\n"
				"  \"pd_schema_version\": 1,\n"
				"  \"parts\": []\n"
				"}\n");
		}
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

	if (s_textbufAppend(ctx->parts,
			"{\n"
			"  \"pd_kind\": \"mesh_parts\",\n"
			"  \"pd_schema_version\": 1,\n"
			"  \"parts\": [\n") != 0) {
		return -1;
	}

	for (s32 i = 0; i < ctx->modeldef->numparts; i++) {
		s32 node_id = s_objNodeId(ctx, ctx->modeldef->parts[i]);
		if (i > 0 && s_textbufAppend(ctx->parts, ",\n") != 0) {
			return -1;
		}
		if (s_textbufAppendf(ctx->parts,
				"    { \"partnum\": %d, \"node\": %d, \"node_unresolved\": %s }",
				(s32)partnums[i], node_id,
				node_id < 0 ? "true" : "false") != 0) {
			return -1;
		}
	}

	return s_textbufAppend(ctx->parts, "\n  ]\n}\n");
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
	if (!modeldef) return 0;
	if (modeldef->numparts < 0 || modeldef->numparts > 500) return 0;
	if (modeldef->rootnode &&
			!s_vmaOffsetInModel((uintptr_t)modeldef->rootnode, size,
			                    sizeof(struct modelnode))) {
		return 0;
	}
	if (!modeldef->rootnode && modeldef->numparts > 0) return 0;
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
                           pdmesh_textbuf_t *mtl,
                           pdmesh_textbuf_t *nodes,
                           pdmesh_textbuf_t *parts,
                           pdmesh_textbuf_t *faces,
                           pdmesh_textbuf_t *render,
                           pdmesh_obj_stats_t *out_stats)
{
	if (!src || src_size < sizeof(struct modeldef) || !obj || !mtl ||
			!nodes || !parts || !faces || !render) return -1;

	u8 *copy = (u8 *)malloc(src_size);
	if (!copy) return -1;
	memcpy(copy, src, src_size);

	struct modeldef *modeldef = (struct modeldef *)copy;
	if (!s_modeldefOffsetsLookPromotable(modeldef, src_size)) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdmesh: modeldef header rejected for filenum=0x%04x size=%u root=0x%08llx skel=0x%08llx parts=0x%08llx numparts=%d nummatrices=%d scale=%.9g rwdatalen=%d numtexconfigs=%d texconfigs=0x%08llx",
			(unsigned)source_filenum, (unsigned)src_size,
			(unsigned long long)(uintptr_t)modeldef->rootnode,
			(unsigned long long)(uintptr_t)modeldef->skel,
			(unsigned long long)(uintptr_t)modeldef->parts,
			(int)modeldef->numparts, (int)modeldef->nummatrices,
			(double)modeldef->scale, (int)modeldef->rwdatalen,
			(int)modeldef->numtexconfigs,
			(unsigned long long)(uintptr_t)modeldef->texconfigs);
		free(copy);
		return -1;
	}
	modelPromoteTypeToPointer(modeldef);
	modelPromoteOffsetsToPointers(modeldef, ROMEXTRACT_PDMESH_MODEL_VMA,
	                              (uintptr_t)modeldef);
	if (modeldef->nummatrices < 0 || modeldef->nummatrices > 4096) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdmesh: modeldef matrix count rejected for filenum=0x%04x size=%u nummatrices=%d",
			(unsigned)source_filenum, (unsigned)src_size,
			(int)modeldef->nummatrices);
		free(copy);
		return -1;
	}

	if (s_textbufAppend(obj,
			"# Perfect Dark 2 base model export\n"
			"mtllib model.mtl\n") != 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdmesh: model OBJ header write failed for filenum=0x%04x",
			(unsigned)source_filenum);
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
	ctx.mtl = mtl;
	ctx.nodes = nodes;
	ctx.parts = parts;
	ctx.faces = faces;
	ctx.render = render;
	ctx.next_index = 1;
	ctx.current_node_mtx_index = -1;
	ctx.current_material = s_objEnsureDefaultMaterial(&ctx);
	ctx.emitted_material = -1;
	if (ctx.current_material < 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdmesh: default material allocation failed for filenum=0x%04x",
			(unsigned)source_filenum);
		free(copy);
		return -1;
	}
	ctx.mtx_stack_size = 1;
	s_objMtxIdentity(ctx.mtx_stack[0]);
	ctx.mtx_stack_indices[0] = -1;
	if (s_textbufAppend(ctx.faces,
			"{\n"
			"  \"pd_kind\": \"mesh_faces\",\n"
			"  \"pd_schema_version\": 1,\n"
			"  \"faces\": [\n") != 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdmesh: faces JSON header write failed for filenum=0x%04x",
			(unsigned)source_filenum);
		free(ctx.materials);
		free(copy);
		return -1;
	}
	if (s_textbufAppend(ctx.render,
			"{\n"
			"  \"pd_kind\": \"mesh_render_commands\",\n"
			"  \"pd_schema_version\": 1,\n"
			"  \"commands\": [\n") != 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdmesh: render JSON header write failed for filenum=0x%04x",
			(unsigned)source_filenum);
		free(ctx.materials);
		free(copy);
		return -1;
	}
	if (modeldef->nummatrices > 0) {
		size_t matrix_bytes = (size_t)modeldef->nummatrices *
			sizeof(*ctx.model_matrices);
		ctx.model_matrices = (f32 (*)[4][4])malloc(matrix_bytes);
		if (!ctx.model_matrices) {
			sysLogPrintf(LOG_WARNING,
				"romextract pdmesh: matrix allocation failed for filenum=0x%04x matrices=%d",
				(unsigned)source_filenum, (int)modeldef->nummatrices);
			free(ctx.materials);
			free(copy);
			return -1;
		}
		ctx.model_matrix_count = modeldef->nummatrices;
		for (s32 i = 0; i < ctx.model_matrix_count; i++) {
			s_objMtxIdentity(ctx.model_matrices[i]);
		}
		if (modeldef->rootnode) {
			s_objBuildDefaultModelMatrices(&ctx, modeldef->rootnode, 0);
		}
	}
	if (modeldef->rootnode) {
		s_objRegisterNodes(&ctx, modeldef->rootnode, 0);
	}
	if (s_writeNodeHierarchyJson(&ctx) != 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdmesh: node hierarchy JSON write failed for filenum=0x%04x root=%p nodes=%d",
			(unsigned)source_filenum, (void *)modeldef->rootnode,
			(int)ctx.node_count);
		if (ctx.model_matrices) free(ctx.model_matrices);
		free(ctx.materials);
		free(copy);
		return -1;
	}
	if (s_writePartTableJson(&ctx) != 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdmesh: part table JSON write failed for filenum=0x%04x root=%p parts=%d",
			(unsigned)source_filenum, (void *)modeldef->rootnode,
			(int)modeldef->numparts);
		if (ctx.model_matrices) free(ctx.model_matrices);
		free(ctx.materials);
		free(copy);
		return -1;
	}
	if (modeldef->rootnode &&
			s_exportNodeObj(&ctx, modeldef->rootnode, 0) != 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdmesh: node OBJ export failed for filenum=0x%04x root=%p nodes=%d",
			(unsigned)source_filenum, (void *)modeldef->rootnode,
			(int)ctx.node_count);
		if (ctx.model_matrices) free(ctx.model_matrices);
		free(ctx.materials);
		free(copy);
		return -1;
	}
	if (s_objFinalizeMaterials(&ctx) != 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdmesh: material finalization failed for filenum=0x%04x materials=%u",
			(unsigned)source_filenum, (unsigned)ctx.material_count);
		if (ctx.model_matrices) free(ctx.model_matrices);
		free(ctx.materials);
		free(copy);
		return -1;
	}
	if (s_textbufAppend(ctx.render, "\n  ]\n}\n") != 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdmesh: render JSON close failed for filenum=0x%04x",
			(unsigned)source_filenum);
		if (ctx.model_matrices) free(ctx.model_matrices);
		free(ctx.materials);
		free(copy);
		return -1;
	}
	if (s_textbufAppend(ctx.faces, "\n  ]\n}\n") != 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdmesh: faces JSON close failed for filenum=0x%04x",
			(unsigned)source_filenum);
		if (ctx.model_matrices) free(ctx.model_matrices);
		free(ctx.materials);
		free(copy);
		return -1;
	}

	if (out_stats) {
		memset(out_stats, 0, sizeof(*out_stats));
		out_stats->triangle_count = ctx.triangle_count;
		out_stats->gdl_count = ctx.gdl_count;
		out_stats->vtx_cmd_count = ctx.vtx_cmd_count;
		out_stats->vtx_slot_count = ctx.vtx_slot_count;
		out_stats->tri_cmd_count = ctx.tri_cmd_count;
		out_stats->tri_attempt_count = ctx.tri_attempt_count;
		out_stats->tri_missing_slot_count = ctx.tri_missing_slot_count;
		out_stats->render_cmd_count = ctx.render_cmd_count;
		out_stats->vtx_bad_addr_count = ctx.vtx_bad_addr_count;
		out_stats->mtx_cmd_count = ctx.mtx_cmd_count;
		out_stats->popmtx_cmd_count = ctx.popmtx_cmd_count;
		out_stats->mtx_model_ref_count = ctx.mtx_model_ref_count;
		out_stats->mtx_bad_addr_count = ctx.mtx_bad_addr_count;
		out_stats->transformed_vertex_count = ctx.transformed_vertex_count;
		out_stats->material_count = ctx.material_count;
		out_stats->material_switch_count = ctx.material_switch_count;
		out_stats->node_count = (u32)ctx.node_count;
		out_stats->part_count = (u32)modeldef->numparts;
		out_stats->model_scale = modeldef->scale;
		for (u32 i = 0; i < ctx.material_count; i++) {
			if (ctx.materials[i].texture_catalog_id[0]) {
				out_stats->textured_material_count++;
			}
		}
		const char *skeleton_symbol =
			modAssetCompilerSkeletonSymbolForPointer(modeldef->skel);
		if (skeleton_symbol) {
			strncpy(out_stats->skeleton_symbol, skeleton_symbol,
				sizeof(out_stats->skeleton_symbol) - 1);
			out_stats->skeleton_symbol[
				sizeof(out_stats->skeleton_symbol) - 1] = '\0';
		}
	}
	if (ctx.model_matrices) free(ctx.model_matrices);
	free(ctx.materials);
	free(copy);
	return 0;
}

/* Convert "FILE_GFALCON2" -> "base:model_falcon2_hi" given a hint
 * suffix. Unknown file symbols still get readable fallback ordinals; raw
 * filenums stay private metadata. */
static void s_synthCatalogId(u16 filenum, const char *hint_suffix,
                              char *out, size_t n)
{
	catalogReadableModelIdForFile(filenum, hint_suffix, "mesh", out, n);
}

static void s_catalogIdToFilename(const char *catalog_id, char *out, size_t n)
{
	if (!out || n == 0) return;
	out[0] = '\0';
	if (!catalog_id) return;
	size_t j = 0;
	for (size_t i = 0; catalog_id[i] && j + 1 < n; i++) {
		out[j++] = (catalog_id[i] == ':') ? '_' : catalog_id[i];
	}
	out[j] = '\0';
}

static void s_removeRelpathIfExists(const char *relpath)
{
	if (!relpath || fsFileSize(relpath) <= 0) return;
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (full && full[0] && remove(full) == 0) {
		sysLogPrintf(LOG_NOTE,
			"romextract pdmesh: removed stale typed-archive zip \"%s\"",
			relpath);
	}
}

static void s_removeLegacyZipForMesh(const char *out_dir, u16 filenum,
                                     const char *hint_suffix)
{
	char catalog_id[128];
	char filename_slug[128];
	char relpath[FS_MAXPATH];
	s_synthCatalogId(filenum, hint_suffix, catalog_id, sizeof(catalog_id));
	s_catalogIdToFilename(catalog_id, filename_slug, sizeof(filename_slug));
	snprintf(relpath, sizeof(relpath), "%s/%s.zip", out_dir, filename_slug);
	s_removeRelpathIfExists(relpath);
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
	sysLogPrintf(LOG_NOTE,
		"romextract pdmesh: preprocessed filenum=0x%04x loadtype=%u new_size=%u",
		(unsigned)filenum, (unsigned)loadtype, (unsigned)new_size);

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
                          const char *catalog_id_override,
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
	if (catalog_id_override && catalog_id_override[0]) {
		strncpy(catalog_id, catalog_id_override, sizeof(catalog_id) - 1);
		catalog_id[sizeof(catalog_id) - 1] = '\0';
	} else {
		s_synthCatalogId(filenum, hint_suffix, catalog_id, sizeof(catalog_id));
	}

	char filename_slug[128];
	s_catalogIdToFilename(catalog_id, filename_slug, sizeof(filename_slug));

	char dst_rel[FS_MAXPATH];
	snprintf(dst_rel, sizeof(dst_rel), "%s/%s.pdmesh", out_dir, filename_slug);

	if (!force_rewrite && fsFileSize(dst_rel) > 0 &&
	    s_existingArchiveHasEntry(dst_rel, "mesh.ini") &&
	    s_existingArchiveHasEntry(dst_rel, "_meta/manifest.json") &&
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
	pdmesh_textbuf_t mtl_buf;
	pdmesh_textbuf_t nodes_buf;
	pdmesh_textbuf_t parts_buf;
	pdmesh_textbuf_t faces_buf;
	pdmesh_textbuf_t render_buf;
	memset(&obj_buf, 0, sizeof(obj_buf));
	memset(&mtl_buf, 0, sizeof(mtl_buf));
	memset(&nodes_buf, 0, sizeof(nodes_buf));
	memset(&parts_buf, 0, sizeof(parts_buf));
	memset(&faces_buf, 0, sizeof(faces_buf));
	memset(&render_buf, 0, sizeof(render_buf));
	pdmesh_obj_stats_t stats;
	memset(&stats, 0, sizeof(stats));
	if (s_buildModelObj((const u8 *)model_bytes, model_size, loadtype,
	                    filenum, &obj_buf, &mtl_buf, &nodes_buf, &parts_buf,
	                    &faces_buf, &render_buf, &stats) != 0) {
		sysMemFree(model_bytes);
		s_textbufFree(&obj_buf);
		s_textbufFree(&mtl_buf);
		s_textbufFree(&nodes_buf);
		s_textbufFree(&parts_buf);
		s_textbufFree(&faces_buf);
		s_textbufFree(&render_buf);
		sysLogPrintf(LOG_WARNING,
			"romextract pdmesh: OBJ export failed for filenum=0x%04x (rel=\"%s\", gdls=%u vtxcmds=%u vtxslots=%u mtxcmds=%u mtxrefs=%u tricmds=%u triattempts=%u trimiss=%u badvtxaddr=%u badmtxaddr=%u)",
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
	if (stats.triangle_count == 0) {
		sysLogPrintf(LOG_NOTE,
			"romextract pdmesh: exported zero-triangle modeldef for filenum=0x%04x (rel=\"%s\", gdls=%u vtxcmds=%u vtxslots=%u mtxcmds=%u mtxrefs=%u tricmds=%u triattempts=%u trimiss=%u badvtxaddr=%u badmtxaddr=%u)",
			(unsigned)filenum, src_rel, (unsigned)stats.gdl_count,
			(unsigned)stats.vtx_cmd_count, (unsigned)stats.vtx_slot_count,
			(unsigned)stats.mtx_cmd_count,
			(unsigned)stats.mtx_model_ref_count,
			(unsigned)stats.tri_cmd_count, (unsigned)stats.tri_attempt_count,
			(unsigned)stats.tri_missing_slot_count,
			(unsigned)stats.vtx_bad_addr_count,
			(unsigned)stats.mtx_bad_addr_count);
	}
	sysLogPrintf(LOG_NOTE,
		"romextract pdmesh: exported filenum=0x%04x loadtype=%u tris=%u gdls=%u materials=%u textured_materials=%u material_switches=%u mtxcmds=%u mtxrefs=%u",
		(unsigned)filenum, (unsigned)loadtype,
		(unsigned)stats.triangle_count, (unsigned)stats.gdl_count,
		(unsigned)stats.material_count,
		(unsigned)stats.textured_material_count,
		(unsigned)stats.material_switch_count,
		(unsigned)stats.mtx_cmd_count,
		(unsigned)stats.mtx_model_ref_count);
	sysMemFree(model_bytes);

	const char *sym_for_provenance = loaderEnumNameForFileEnum(filenum);

	/* Build _meta/manifest.json text in memory. */
	char manifest_buf[1792];
	int manifest_len = snprintf(manifest_buf, sizeof(manifest_buf),
		"{\n"
		"  \"pd_kind\": \"mesh\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"source_filenum_symbol\": \"%s\",\n"
		"  \"skeleton_symbol\": \"%s\",\n"
		"  \"source_format\": \"PD_MODELDEF\",\n"
		"  \"format\": \"OBJ\",\n"
		"  \"obj_export_version\": \"%s\",\n"
		"  \"geometry\": \"model.obj\",\n"
		"  \"material\": \"model.mtl\",\n"
		"  \"hierarchy\": \"model.nodes.json\",\n"
		"  \"parts\": \"model.parts.json\",\n"
		"  \"faces\": \"model.faces.json\",\n"
		"  \"render_stream\": \"model.render.json\",\n"
		"  \"model_scale\": %.9g,\n"
		"  \"triangle_count\": %u,\n"
		"  \"node_count\": %u,\n"
		"  \"part_count\": %u,\n"
		"  \"material_count\": %u,\n"
		"  \"textured_material_count\": %u,\n"
		"  \"material_switch_count\": %u,\n"
		"  \"display_list_count\": %u,\n"
		"  \"render_command_count\": %u,\n"
		"  \"matrix_command_count\": %u,\n"
		"  \"model_matrix_reference_count\": %u\n"
		"}\n",
		catalog_id,
		sym_for_provenance ? sym_for_provenance : "",
		stats.skeleton_symbol,
		ROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION_LABEL,
		(double)stats.model_scale,
		(unsigned)stats.triangle_count,
		(unsigned)stats.node_count,
		(unsigned)stats.part_count,
		(unsigned)stats.material_count,
		(unsigned)stats.textured_material_count,
		(unsigned)stats.material_switch_count,
		(unsigned)stats.gdl_count,
		(unsigned)stats.render_cmd_count,
		(unsigned)stats.mtx_cmd_count,
		(unsigned)stats.mtx_model_ref_count);
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		s_textbufFree(&obj_buf);
		s_textbufFree(&mtl_buf);
		s_textbufFree(&nodes_buf);
		s_textbufFree(&parts_buf);
		s_textbufFree(&faces_buf);
		s_textbufFree(&render_buf);
		sysLoudFailf("EXTRACT.PDMESH",
			"manifest.json snprintf truncated for filenum=0x%04x",
			(unsigned)filenum);
		return -1;
	}

	char ini_buf[1200];
	int ini_len = snprintf(ini_buf, sizeof(ini_buf),
		"[model]\n"
		"catalog_id = %s\n"
		"kind = mesh\n"
		"source_format = PD_MODELDEF\n"
		"format = OBJ\n"
		"obj_export_version = %s\n"
		"geometry_file = model.obj\n"
		"material_file = model.mtl\n"
		"hierarchy_file = model.nodes.json\n"
		"parts_file = model.parts.json\n"
		"faces_file = model.faces.json\n"
		"render_stream_file = model.render.json\n"
		"model_scale = %.9g\n"
		"skeleton_symbol = %s\n"
		"triangle_count = %u\n"
		"node_count = %u\n"
		"part_count = %u\n"
		"material_count = %u\n"
		"textured_material_count = %u\n"
		"material_switch_count = %u\n"
		"display_list_count = %u\n"
		"render_command_count = %u\n"
		"matrix_command_count = %u\n"
		"model_matrix_reference_count = %u\n"
		"source_filenum_symbol = %s\n",
		catalog_id,
		ROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION_LABEL,
		(double)stats.model_scale,
		stats.skeleton_symbol,
		(unsigned)stats.triangle_count,
		(unsigned)stats.node_count,
		(unsigned)stats.part_count,
		(unsigned)stats.material_count,
		(unsigned)stats.textured_material_count,
		(unsigned)stats.material_switch_count,
		(unsigned)stats.gdl_count,
		(unsigned)stats.render_cmd_count,
		(unsigned)stats.mtx_cmd_count,
		(unsigned)stats.mtx_model_ref_count,
		sym_for_provenance ? sym_for_provenance : "");
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini_buf)) {
		s_textbufFree(&obj_buf);
		s_textbufFree(&mtl_buf);
		s_textbufFree(&nodes_buf);
		s_textbufFree(&parts_buf);
		s_textbufFree(&faces_buf);
		s_textbufFree(&render_buf);
		sysLoudFailf("EXTRACT.PDMESH",
			"mesh.ini snprintf truncated for filenum=0x%04x",
			(unsigned)filenum);
		return -1;
	}

	/* Open ZIP writer (atomic temp + rename via modArchive). */
	char dst_full_buf[FS_MAXPATH + 1];
	const char *dst_full = fsFullPath(dst_rel, dst_full_buf, sizeof(dst_full_buf));
	if (!dst_full || !dst_full[0]) {
		s_textbufFree(&obj_buf);
		s_textbufFree(&mtl_buf);
		s_textbufFree(&nodes_buf);
		s_textbufFree(&parts_buf);
		s_textbufFree(&faces_buf);
		s_textbufFree(&render_buf);
		sysLoudFailf("EXTRACT.PDMESH",
			"fsFullPath failed for \"%s\"", dst_rel);
		return -1;
	}
	mod_archive_writer_t *aw = modArchiveBegin(dst_full);
	if (!aw) {
		s_textbufFree(&obj_buf);
		s_textbufFree(&mtl_buf);
		s_textbufFree(&nodes_buf);
		s_textbufFree(&parts_buf);
		s_textbufFree(&faces_buf);
		s_textbufFree(&render_buf);
		sysLoudFailf("EXTRACT.PDMESH",
			"modArchiveBegin failed for \"%s\"", dst_full);
		return -1;
	}

	asset_archive_writer_t asset_writer;
	if (assetArchiveWriterInit(&asset_writer, aw, "mesh", catalog_id) !=
			MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDMESH",
			"assetArchiveWriterInit failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_textbufFree(&obj_buf);
		s_textbufFree(&mtl_buf);
		s_textbufFree(&nodes_buf);
		s_textbufFree(&parts_buf);
		s_textbufFree(&faces_buf);
		s_textbufFree(&render_buf);
		return -1;
	}
	assetArchiveWriterSetProvenance(&asset_writer, "romextract_pdmesh",
		src_rel, (s32)filenum, sym_for_provenance ? sym_for_provenance : "");

	if (assetArchiveWriterAddDescriptor(&asset_writer, "mesh.ini",
			ini_buf, (u32)ini_len) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDMESH",
			"AddFileMem mesh.ini failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_textbufFree(&obj_buf);
		s_textbufFree(&mtl_buf);
		s_textbufFree(&nodes_buf);
		s_textbufFree(&parts_buf);
		s_textbufFree(&faces_buf);
		s_textbufFree(&render_buf);
		return -1;
	}
	if (assetArchiveWriterAddManifestJson(&asset_writer,
			manifest_buf, (u32)manifest_len) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDMESH",
			"AddFileMem _meta/manifest.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_textbufFree(&obj_buf);
		s_textbufFree(&mtl_buf);
		s_textbufFree(&nodes_buf);
		s_textbufFree(&parts_buf);
		s_textbufFree(&faces_buf);
		s_textbufFree(&render_buf);
		return -1;
	}

	if (assetArchiveWriterAddPublicMem(&asset_writer, "export_version.txt",
			ROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION,
			(u32)strlen(ROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION),
			"version") != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDMESH",
			"AddFileMem export_version.txt failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_textbufFree(&obj_buf);
		s_textbufFree(&mtl_buf);
		s_textbufFree(&nodes_buf);
		s_textbufFree(&parts_buf);
		s_textbufFree(&faces_buf);
		s_textbufFree(&render_buf);
		return -1;
	}

	if (assetArchiveWriterAddPublicMem(&asset_writer, "model.obj",
			obj_buf.data, obj_buf.len, "geometry") != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDMESH",
			"AddFileMem model.obj failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_textbufFree(&obj_buf);
		s_textbufFree(&mtl_buf);
		s_textbufFree(&nodes_buf);
		s_textbufFree(&parts_buf);
		s_textbufFree(&faces_buf);
		s_textbufFree(&render_buf);
		return -1;
	}

	if (assetArchiveWriterAddPublicMem(&asset_writer, "model.mtl",
			mtl_buf.data, mtl_buf.len, "material") != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDMESH",
			"AddFileMem model.mtl failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_textbufFree(&obj_buf);
		s_textbufFree(&mtl_buf);
		s_textbufFree(&nodes_buf);
		s_textbufFree(&parts_buf);
		s_textbufFree(&faces_buf);
		s_textbufFree(&render_buf);
		return -1;
	}

	if (assetArchiveWriterAddPublicMem(&asset_writer, "model.nodes.json",
			nodes_buf.data, nodes_buf.len, "hierarchy") != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDMESH",
			"AddFileMem model.nodes.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_textbufFree(&obj_buf);
		s_textbufFree(&mtl_buf);
		s_textbufFree(&nodes_buf);
		s_textbufFree(&parts_buf);
		s_textbufFree(&faces_buf);
		s_textbufFree(&render_buf);
		return -1;
	}

	if (assetArchiveWriterAddPublicMem(&asset_writer, "model.parts.json",
			parts_buf.data, parts_buf.len, "parts") != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDMESH",
			"AddFileMem model.parts.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_textbufFree(&obj_buf);
		s_textbufFree(&mtl_buf);
		s_textbufFree(&nodes_buf);
		s_textbufFree(&parts_buf);
		s_textbufFree(&faces_buf);
		s_textbufFree(&render_buf);
		return -1;
	}

	if (assetArchiveWriterAddPublicMem(&asset_writer, "model.faces.json",
			faces_buf.data, faces_buf.len, "faces") != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDMESH",
			"AddFileMem model.faces.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_textbufFree(&obj_buf);
		s_textbufFree(&mtl_buf);
		s_textbufFree(&nodes_buf);
		s_textbufFree(&parts_buf);
		s_textbufFree(&faces_buf);
		s_textbufFree(&render_buf);
		return -1;
	}

	if (assetArchiveWriterAddPublicMem(&asset_writer, "model.render.json",
			render_buf.data, render_buf.len, "render_stream") !=
			MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDMESH",
			"AddFileMem model.render.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_textbufFree(&obj_buf);
		s_textbufFree(&mtl_buf);
		s_textbufFree(&nodes_buf);
		s_textbufFree(&parts_buf);
		s_textbufFree(&faces_buf);
		s_textbufFree(&render_buf);
		return -1;
	}

	if (assetArchiveWriterFinishMetadata(&asset_writer) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDMESH",
			"assetArchiveWriterFinishMetadata failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_textbufFree(&obj_buf);
		s_textbufFree(&mtl_buf);
		s_textbufFree(&nodes_buf);
		s_textbufFree(&parts_buf);
		s_textbufFree(&faces_buf);
		s_textbufFree(&render_buf);
		return -1;
	}

	if (modArchiveFinish(aw) != 0) {
		s_textbufFree(&obj_buf);
		s_textbufFree(&mtl_buf);
		s_textbufFree(&nodes_buf);
		s_textbufFree(&parts_buf);
		s_textbufFree(&faces_buf);
		s_textbufFree(&render_buf);
		sysLoudFailf("EXTRACT.PDMESH",
			"modArchiveFinish failed for \"%s\"", dst_full);
		return -1;
	}
	s_textbufFree(&obj_buf);
	s_textbufFree(&mtl_buf);
	s_textbufFree(&nodes_buf);
	s_textbufFree(&parts_buf);
	s_textbufFree(&faces_buf);
	s_textbufFree(&render_buf);
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
	char catalog_id[CATALOG_ID_LEN];
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
	const char *catalog_id = j->catalog_id[0] ? j->catalog_id : NULL;
	s32 r = s_emitOneMesh(j->filenum, hint, catalog_id,
		c->out_dir, c->force_rewrite);
	if (r > 0)       SDL_AtomicAdd(&c->written, 1);
	else if (r == 0) SDL_AtomicAdd(&c->skipped, 1);
	else             SDL_AtomicAdd(&c->failed,  1);

	int done = SDL_AtomicAdd(&c->processed, 1) + 1;
	if ((done & 0x0f) == 0 || done == c->count) {
		bootProgressUpdate(done, c->count);
	}
}

static void s_pdmeshWorkCatalogId(const pdmesh_work_t *job,
	char *out, size_t out_n)
{
	if (!out || out_n == 0) {
		return;
	}

	out[0] = '\0';
	if (!job) {
		return;
	}

	if (job->catalog_id[0]) {
		strncpy(out, job->catalog_id, out_n - 1);
		out[out_n - 1] = '\0';
		return;
	}

	s_synthCatalogId(job->filenum, job->hint[0] ? job->hint : NULL, out,
		out_n);
}

static s32 s_pdmeshAddWorkWithId(pdmesh_work_t *jobs, s32 *job_count, s32 cap,
                            u16 filenum, const char *hint,
                            const char *catalog_id)
{
	if (filenum == 0) return 0;
	const char *wanted_hint = hint ? hint : "";
	char wanted_id[CATALOG_ID_LEN];

	if (catalog_id && catalog_id[0]) {
		strncpy(wanted_id, catalog_id, sizeof(wanted_id) - 1);
		wanted_id[sizeof(wanted_id) - 1] = '\0';
	} else {
		s_synthCatalogId(filenum, wanted_hint[0] ? wanted_hint : NULL,
			wanted_id, sizeof(wanted_id));
	}

	/* Dedup by output identity, not only filenum. Some files are both
	 * weapon meshes and body/hand meshes, which intentionally emit separate
	 * archive names from the same source bytes. */
	for (s32 i = 0; i < *job_count; i++) {
		char existing_id[CATALOG_ID_LEN];
		s_pdmeshWorkCatalogId(&jobs[i], existing_id, sizeof(existing_id));
		if (strcmp(existing_id, wanted_id) == 0) {
			return 0;
		}
	}
	if (*job_count >= cap) return 0;
	jobs[*job_count].filenum = filenum;
	jobs[*job_count].hint[0] = '\0';
	jobs[*job_count].catalog_id[0] = '\0';
	if (hint && hint[0]) {
		size_t hlen = strlen(hint);
		if (hlen >= sizeof(jobs[*job_count].hint)) {
			hlen = sizeof(jobs[*job_count].hint) - 1;
		}
		memcpy(jobs[*job_count].hint, hint, hlen);
		jobs[*job_count].hint[hlen] = '\0';
	}
	if (catalog_id && catalog_id[0]) {
		strncpy(jobs[*job_count].catalog_id, catalog_id,
			sizeof(jobs[*job_count].catalog_id) - 1);
		jobs[*job_count].catalog_id[
			sizeof(jobs[*job_count].catalog_id) - 1] = '\0';
	}
	(*job_count)++;
	return 1;
}

static s32 s_pdmeshAddWork(pdmesh_work_t *jobs, s32 *job_count, s32 cap,
                            u16 filenum, const char *hint)
{
	return s_pdmeshAddWorkWithId(jobs, job_count, cap, filenum, hint, NULL);
}

static void s_pdmeshAddModelnumWork(pdmesh_work_t *jobs, s32 *job_count,
                                    s32 cap, s32 modelnum)
{
	if (modelnum < 0 || modelnum >= NUM_MODELS) return;
	u16 filenum = g_ModelStates[modelnum].fileid;
	const char *catalog_id = catalogModelIdByModelnum(modelnum);
	char fallback_id[CATALOG_ID_LEN];

	if (!catalog_id || !catalog_id[0]) {
		catalogReadableModelIdForModelnum(modelnum, (s32)filenum,
			fallback_id, sizeof(fallback_id));
		catalog_id = fallback_id;
	}

	s_pdmeshAddWorkWithId(jobs, job_count, cap, filenum, NULL, catalog_id);
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

	/* Phase 1: collect unique output mesh identities single-threaded. */
	pdmesh_work_t jobs[ROMEXTRACT_PDMESH_SEEN_CAP];
	s32 job_count = 0;

	for (s32 i = 0; i < NUM_MODELS; i++) {
		s_pdmeshAddModelnumWork(jobs, &job_count,
			ROMEXTRACT_PDMESH_SEEN_CAP, i);
	}
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
	{
		static const char *cart_ids[] = {
			"base:model_cartridge_rifle",
			"base:model_cartridge_rifle_alt",
			"base:model_cartridge_blue",
			"base:model_cartridge_shell",
		};

		for (s32 i = 0; i < (s32)(sizeof(cart_ids) / sizeof(cart_ids[0])); i++) {
			s_pdmeshAddWorkWithId(jobs, &job_count,
				ROMEXTRACT_PDMESH_SEEN_CAP, g_CartFileNums[i],
				"cart", cart_ids[i]);
		}
	}
	{
		char menu_hudpiece_id[CATALOG_ID_LEN];
		catalogReadableModelIdForFile((s32)FILE_GHUDPIECE, "menu", "menu",
			menu_hudpiece_id, sizeof(menu_hudpiece_id));
		s_pdmeshAddWorkWithId(jobs, &job_count,
			ROMEXTRACT_PDMESH_SEEN_CAP, (u16)FILE_GHUDPIECE, "menu",
			menu_hudpiece_id);
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

	for (s32 i = 0; i < job_count; i++) {
		const char *hint = jobs[i].hint[0] ? jobs[i].hint : NULL;
		s_removeLegacyZipForMesh(meshes_dir, jobs[i].filenum, hint);
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
