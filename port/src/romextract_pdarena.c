/**
 * romextract_pdarena.c -- Catalog universality pivot Step 2 (2026-05-03).
 *
 * Walks the loader_pool arena pool and emits TWO files per registered
 * arena per Mike's Q-1 unified-scenario directive:
 *
 *   1. data/<romid>/arenas/<id>.pdarena
 *      ZIP-openable typed asset archive. The archive root carries
 *      arena.ini for the modder-facing descriptor plus _meta/manifest.json for
 *      the legacy universal walker until the base walker consumes the INI
 *      path directly. Playable arenas also embed their authored scenario
 *      dependency closure under scenario/ so opening one .pdarena exposes
 *      the DCC-openable scene source, compatibility OBJ exports, decoded
 *      wall/floor textures, pads/spawns/source tables, graph hooks, and
 *      provenance files without chasing a separate archive.
 *
 *   2. data/<romid>/scenarios/<scenario_id>.pdscenario
 *      ZIP compound bundling scene.glb, compatibility visual/scene.obj,
 *      visual/scene.mtl, visual/textures/*.tga, tiles.tsv, pads.tsv,
 *      spawns.tsv, navigation.ini, level.graph.json, and
 *      _meta/generated-*.json envelopes. UNIFIED per Q-1 (one file per stage;
 *      modder-friendly atomic distribution).
 *      Schema: universality-pivot-schemas.md Section 2.10.
 *
 * Boot order: must run AFTER stageTableInit (g_Stages populated) AND
 * loaderPoolFinalize (arena pool populated) AND
 * romExtractAllFiles (per-stage .bin files extracted to disk).
 *
 * Cross-reference convention for Step 2: the .pdarena `scenario` field
 * carries the catalog ID `base:scenario_<arena_slug>` matching the
 * .pdscenario this emitter produces. Arenas without a resolvable
 * stagetable entry (Random meta arenas with token stagenums
 * STAGE_MP_RANDOM_MULTI=0x02 / STAGE_MP_RANDOM_SOLO=0x03) get a
 * `scenario: null` and skip the .pdscenario emit.
 *
 * ARENA_LOADMODE_CANVAS invariant: preserved per-arena via the
 * load_mode field in the .pdarena JSON. The 14 Solo Missions arenas
 * still get .pdscenario emits because their stage files exist on disk
 * (they are rendered as static canvases at runtime by the menu UI but
 * the asset bytes are real).
 *
 * Server build: emitter early-returns 0 (arena loader not active
 * server-side; g_RomFile is freed; no stage files extracted).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <math.h>
#include <SDL.h>
#include <PR/ultratypes.h>

#include "boot_pool.h"
#include "boot_progress.h"
#include "data.h"
#include "types.h"
#include "constants.h"
#include "fs.h"
#include "loader_enum_reverse.h"
#include "assetcatalog.h"
#include "asset_archive_writer.h"
#include "modarchive.h"
#include "preprocess.h"
#include "romdata.h"
#include "romextract.h"
#include "romextract_pd.h"
#include "system.h"
#include "memsizes.h"
#include "game/stagetable.h"
#include "game/texdecompress.h"
#include "game/tex.h"
#include "arenadata_authored.h"
#include "lib/rzip.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../external/stb_image_write.h"

#define PDSCENARIO_BG_VISUAL_EXPORT_VERSION "bg_visual_scene_glb_v1"
#define PDSCENARIO_BG_VISUAL_EXPORT_VERSION_FILE \
	PDSCENARIO_BG_VISUAL_EXPORT_VERSION "\n"

/* Convert "base:arena_mp_skedar" -> "base_arena_mp_skedar". */
static void s_idToFilename(const char *id, char *out, size_t n)
{
	if (!id || !out || n == 0) { if (out && n) out[0] = '\0'; return; }
	size_t i;
	for (i = 0; i + 1 < n && id[i]; i++) {
		out[i] = (id[i] == ':') ? '_' : id[i];
	}
	out[i] = '\0';
}

/* Build the scenario catalog ID for an arena slug. The arena's
 * `scenario` field carries this string; the .pdscenario file lives at
 * data/<romid>/scenarios/<filename_slug>.pdscenario. */
static void s_scenarioCatalogIdFromSlug(const char *slug, char *out, size_t n)
{
	if (!slug || !out || n == 0) { if (out && n) out[0] = '\0'; return; }
	snprintf(out, n, "base:scenario_%s", slug);
}

/* Map an arena's category to the scenario `kind` string per Section
 * 2.10 of the schema. "Solo Missions" -> "solo"; everything else -> "mp"
 * (Dark / Classic / Bonus / Random). The schema also defines
 * "firingrange" and "coop" but those map to stages outside the arena
 * pool (firing range is the boot training stage; coop is a per-game
 * mode flag, not a per-arena attribute). */
static const char *s_kindFromCategory(const char *category)
{
	if (!category || !category[0]) return "mp";
	if (strcmp(category, "Solo Missions") == 0) return "solo";
	return "mp";
}

typedef struct {
	char *data;
	u32   len;
	u32   cap;
} pdscenario_textbuf_t;

typedef struct {
	u8  *data;
	u32  len;
	u32  cap;
} pdscenario_binbuf_t;

typedef struct {
	f32 x, y, z;
	f32 u, v;
} pdscenario_visual_vertex_t;

typedef struct {
	pdscenario_visual_vertex_t *vertices;
	u32 vertex_count;
	u32 vertex_cap;
	u32 *indices;
	u32 index_count;
	u32 index_cap;
	f32 min_x, min_y, min_z;
	f32 max_x, max_y, max_z;
	s32 has_bounds;
} pdscenario_visualmesh_t;

typedef struct {
	u16 texnum;
	s32 used_by_geometry;
	s32 decoded;
	u32 width;
	u32 height;
	u8 *tga;
	u32 tga_size;
	u8 *png;
	u32 png_size;
	char path[96];
} pdscenario_bgtexture_t;

typedef struct {
	char name[48];
	s32 texnum;
	s32 texnum2;
	s32 subcmd;
	u32 w0;
	u32 w1;
	s32 smode;
	s32 tmode;
	s32 offset;
	s32 shifts;
	s32 shiftt;
	s32 min;
	s32 flag;
	u32 tri_count;
	pdscenario_visual_vertex_t *vertices;
	u32 vertex_count;
	u32 vertex_cap;
} pdscenario_bgmaterial_t;

typedef struct {
	pdscenario_textbuf_t obj;
	pdscenario_textbuf_t mtl;
	pdscenario_textbuf_t materials_tsv;
	u8 *scene_glb;
	u32 scene_glb_size;
	pdscenario_bgtexture_t *textures;
	u32 texture_count;
	u32 texture_cap;
	pdscenario_bgmaterial_t *materials;
	u32 material_count;
	u32 material_cap;
	u32 next_obj_index;
	s32 current_material;
	u32 room_count;
	u32 gdl_count;
	u32 tri_count;
	u32 decoded_texture_count;
	u32 failed_texture_count;
	f32 min_x, min_y, min_z;
	f32 max_x, max_y, max_z;
	s32 has_bounds;
} pdscenario_bgscene_t;

static void s_textbufFree(pdscenario_textbuf_t *b)
{
	if (b->data) free(b->data);
	memset(b, 0, sizeof(*b));
}

static void s_binbufFree(pdscenario_binbuf_t *b)
{
	if (b->data) free(b->data);
	memset(b, 0, sizeof(*b));
}

static s32 s_textbufReserve(pdscenario_textbuf_t *b, u32 extra)
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

static s32 s_textbufAppend(pdscenario_textbuf_t *b, const char *s)
{
	u32 n = (u32)strlen(s);
	if (s_textbufReserve(b, n) != 0) return -1;
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = '\0';
	return 0;
}

static s32 s_textbufAppendf(pdscenario_textbuf_t *b, const char *fmt, ...)
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

static s32 s_binbufReserve(pdscenario_binbuf_t *b, u32 extra)
{
	if (!b || extra > 0xffffffffu - b->len) return -1;
	u32 need = b->len + extra;
	if (need <= b->cap) return 0;
	u32 cap = b->cap ? b->cap : 8192;
	while (cap < need) {
		if (cap > 0x80000000u) return -1;
		cap *= 2;
	}
	u8 *p = (u8 *)realloc(b->data, cap);
	if (!p) return -1;
	b->data = p;
	b->cap = cap;
	return 0;
}

static s32 s_binbufAppend(pdscenario_binbuf_t *b, const void *data, u32 len)
{
	if (!b || (!data && len)) return -1;
	if (s_binbufReserve(b, len) != 0) return -1;
	if (len) memcpy(b->data + b->len, data, len);
	b->len += len;
	return 0;
}

static s32 s_binbufAppendZeroes(pdscenario_binbuf_t *b, u32 len)
{
	if (!b) return -1;
	if (s_binbufReserve(b, len) != 0) return -1;
	memset(b->data + b->len, 0, len);
	b->len += len;
	return 0;
}

static s32 s_binbufPad4(pdscenario_binbuf_t *b)
{
	u32 pad = (4u - (b->len & 3u)) & 3u;
	return pad ? s_binbufAppendZeroes(b, pad) : 0;
}

static s32 s_binbufAppendLe32(pdscenario_binbuf_t *b, u32 value)
{
	u8 tmp[4];
	tmp[0] = (u8)(value & 0xffu);
	tmp[1] = (u8)((value >> 8) & 0xffu);
	tmp[2] = (u8)((value >> 16) & 0xffu);
	tmp[3] = (u8)((value >> 24) & 0xffu);
	return s_binbufAppend(b, tmp, sizeof(tmp));
}

static void s_visualMeshFree(pdscenario_visualmesh_t *m)
{
	if (!m) return;
	if (m->vertices) free(m->vertices);
	if (m->indices) free(m->indices);
	memset(m, 0, sizeof(*m));
}

static void s_bgSceneFree(pdscenario_bgscene_t *s)
{
	if (!s) return;
	s_textbufFree(&s->obj);
	s_textbufFree(&s->mtl);
	s_textbufFree(&s->materials_tsv);
	if (s->scene_glb) free(s->scene_glb);
	if (s->textures) {
		for (u32 i = 0; i < s->texture_count; i++) {
			if (s->textures[i].tga) free(s->textures[i].tga);
			if (s->textures[i].png) free(s->textures[i].png);
		}
		free(s->textures);
	}
	if (s->materials) {
		for (u32 i = 0; i < s->material_count; i++) {
			if (s->materials[i].vertices) free(s->materials[i].vertices);
		}
		free(s->materials);
	}
	memset(s, 0, sizeof(*s));
}

static s32 s_visualMeshReserve(pdscenario_visualmesh_t *m,
                               u32 add_vertices, u32 add_indices)
{
	if (!m) return -1;
	if (add_vertices > 0xffffffffu - m->vertex_count) return -1;
	if (add_indices > 0xffffffffu - m->index_count) return -1;

	u32 need_vertices = m->vertex_count + add_vertices;
	if (need_vertices > m->vertex_cap) {
		u32 cap = m->vertex_cap ? m->vertex_cap : 4096;
		while (cap < need_vertices) {
			if (cap > 0x80000000u) return -1;
			cap *= 2;
		}
		pdscenario_visual_vertex_t *p =
			(pdscenario_visual_vertex_t *)realloc(m->vertices,
				(size_t)cap * sizeof(*m->vertices));
		if (!p) return -1;
		m->vertices = p;
		m->vertex_cap = cap;
	}

	u32 need_indices = m->index_count + add_indices;
	if (need_indices > m->index_cap) {
		u32 cap = m->index_cap ? m->index_cap : 4096;
		while (cap < need_indices) {
			if (cap > 0x80000000u) return -1;
			cap *= 2;
		}
		u32 *p = (u32 *)realloc(m->indices,
			(size_t)cap * sizeof(*m->indices));
		if (!p) return -1;
		m->indices = p;
		m->index_cap = cap;
	}

	return 0;
}

static void s_visualMeshBounds(pdscenario_visualmesh_t *m, f32 x, f32 y, f32 z)
{
	if (!m->has_bounds) {
		m->min_x = m->max_x = x;
		m->min_y = m->max_y = y;
		m->min_z = m->max_z = z;
		m->has_bounds = 1;
		return;
	}
	if (x < m->min_x) m->min_x = x;
	if (y < m->min_y) m->min_y = y;
	if (z < m->min_z) m->min_z = z;
	if (x > m->max_x) m->max_x = x;
	if (y > m->max_y) m->max_y = y;
	if (z > m->max_z) m->max_z = z;
}

static void s_triUvs(f32 x0, f32 y0, f32 z0,
                     f32 x1, f32 y1, f32 z1,
                     f32 x2, f32 y2, f32 z2,
                     f32 *u0, f32 *v0,
                     f32 *u1, f32 *v1,
                     f32 *u2, f32 *v2)
{
	f32 ax = x1 - x0, ay = y1 - y0, az = z1 - z0;
	f32 bx = x2 - x0, by = y2 - y0, bz = z2 - z0;
	f32 nx = ay * bz - az * by;
	f32 ny = az * bx - ax * bz;
	f32 nz = ax * by - ay * bx;
	if (nx < 0.0f) nx = -nx;
	if (ny < 0.0f) ny = -ny;
	if (nz < 0.0f) nz = -nz;

	const f32 scale = 1.0f / 256.0f;
	if (nx >= ny && nx >= nz) {
		*u0 = z0 * scale; *v0 = y0 * scale;
		*u1 = z1 * scale; *v1 = y1 * scale;
		*u2 = z2 * scale; *v2 = y2 * scale;
	} else if (ny >= nx && ny >= nz) {
		*u0 = x0 * scale; *v0 = z0 * scale;
		*u1 = x1 * scale; *v1 = z1 * scale;
		*u2 = x2 * scale; *v2 = z2 * scale;
	} else {
		*u0 = x0 * scale; *v0 = y0 * scale;
		*u1 = x1 * scale; *v1 = y1 * scale;
		*u2 = x2 * scale; *v2 = y2 * scale;
	}
}

static s32 s_visualMeshAddTri(pdscenario_visualmesh_t *m,
                              f32 x0, f32 y0, f32 z0, f32 u0, f32 v0,
                              f32 x1, f32 y1, f32 z1, f32 u1, f32 v1,
                              f32 x2, f32 y2, f32 z2, f32 u2, f32 v2)
{
	if (!m) return 0;
	if (s_visualMeshReserve(m, 3, 3) != 0) return -1;
	u32 base = m->vertex_count;
	m->vertices[m->vertex_count++] =
		(pdscenario_visual_vertex_t){ x0, y0, z0, u0, v0 };
	m->vertices[m->vertex_count++] =
		(pdscenario_visual_vertex_t){ x1, y1, z1, u1, v1 };
	m->vertices[m->vertex_count++] =
		(pdscenario_visual_vertex_t){ x2, y2, z2, u2, v2 };
	m->indices[m->index_count++] = base;
	m->indices[m->index_count++] = base + 1u;
	m->indices[m->index_count++] = base + 2u;
	s_visualMeshBounds(m, x0, y0, z0);
	s_visualMeshBounds(m, x1, y1, z1);
	s_visualMeshBounds(m, x2, y2, z2);
	return 0;
}

static void s_pdscenarioScratchFree(pdscenario_textbuf_t *rooms_obj,
                                    pdscenario_textbuf_t *tiles_tsv,
                                    pdscenario_textbuf_t *pads_tsv,
                                    pdscenario_textbuf_t *spawns_tsv,
                                    pdscenario_textbuf_t *volumes_tsv,
                                    pdscenario_textbuf_t *objects_tsv,
                                    pdscenario_textbuf_t *objectives_tsv,
                                    pdscenario_textbuf_t *navigation_ini,
                                    pdscenario_textbuf_t *level_graph_json,
                                    pdscenario_textbuf_t *collision_meta_json,
                                    pdscenario_textbuf_t *navmesh_meta_json,
                                    pdscenario_visualmesh_t *visual_mesh,
                                    pdscenario_bgscene_t *bg_scene)
{
	s_textbufFree(rooms_obj);
	s_textbufFree(tiles_tsv);
	s_textbufFree(pads_tsv);
	s_textbufFree(spawns_tsv);
	s_textbufFree(volumes_tsv);
	s_textbufFree(objects_tsv);
	s_textbufFree(objectives_tsv);
	s_textbufFree(navigation_ini);
	s_textbufFree(level_graph_json);
	s_textbufFree(collision_meta_json);
	s_textbufFree(navmesh_meta_json);
	s_visualMeshFree(visual_mesh);
	s_bgSceneFree(bg_scene);
}

static s32 s_loadStageFileRaw(u16 filenum, u8 **out_data, u32 *out_size)
{
	if (out_data) *out_data = NULL;
	if (out_size) *out_size = 0;
	if (filenum == 0) return 0;

	char src_rel[FS_MAXPATH];
	if (romExtractRelPathForFilenum((s32)filenum, src_rel, sizeof(src_rel)) <= 0) {
		return 0;
	}
	if (fsFileSize(src_rel) <= 0) {
		sysLogPrintf(LOG_NOTE,
			"romextract pdarena: stage source missing for filenum=0x%04x (rel=\"%s\")",
			(unsigned)filenum, src_rel);
		return 0;
	}

	u32 raw_size = 0;
	u8 *raw = (u8 *)fsFileLoad(src_rel, &raw_size);
	if (!raw || raw_size == 0) {
		if (raw) sysMemFree(raw);
		return 0;
	}

	*out_data = raw;
	*out_size = raw_size;
	return 1;
}

static s32 s_loadStageFilePreprocessed(u16 filenum, u32 loadtype,
                                       u8 **out_data, u32 *out_size)
{
	if (out_data) *out_data = NULL;
	if (out_size) *out_size = 0;

	u8 *raw = NULL;
	u32 raw_size = 0;
	s32 loaded = s_loadStageFileRaw(filenum, &raw, &raw_size);
	if (loaded <= 0) return loaded;

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
		"romextract pdarena: preprocessing filenum=0x%04x loadtype=%u raw_size=%u inflated_size=%u",
		(unsigned)filenum, (unsigned)loadtype, (unsigned)raw_size,
		(unsigned)inflated_size);
	if (loadtype == LOADTYPE_TILES) {
		(void)preprocessTilesFile(work, inflated_size, &new_size);
	} else if (loadtype == LOADTYPE_PADS) {
		(void)preprocessPadsFile(work, inflated_size, &new_size);
	} else if (loadtype == LOADTYPE_SETUP) {
		(void)preprocessSetupFile(work, inflated_size, &new_size);
	}

	if (new_size == 0 || new_size > cap) {
		sysMemFree(work);
		return -1;
	}

	*out_data = work;
	*out_size = new_size;
	return 1;
}

static s32 s_objEmitTri(pdscenario_textbuf_t *obj,
                        pdscenario_visualmesh_t *visual,
                        u32 *next_index,
                        f32 x0, f32 y0, f32 z0,
                        f32 x1, f32 y1, f32 z1,
                        f32 x2, f32 y2, f32 z2)
{
	f32 u0, v0, u1, v1, u2, v2;
	s_triUvs(x0, y0, z0, x1, y1, z1, x2, y2, z2,
		&u0, &v0, &u1, &v1, &u2, &v2);
	if (s_visualMeshAddTri(visual,
			x0, y0, z0, u0, v0,
			x1, y1, z1, u1, v1,
			x2, y2, z2, u2, v2) != 0) {
		return -1;
	}
	u32 i0 = (*next_index)++;
	u32 i1 = (*next_index)++;
	u32 i2 = (*next_index)++;
	return s_textbufAppendf(obj,
		"v %.6f %.6f %.6f\n"
		"v %.6f %.6f %.6f\n"
		"v %.6f %.6f %.6f\n"
		"vt %.6f %.6f\n"
		"vt %.6f %.6f\n"
		"vt %.6f %.6f\n"
		"f %u/%u %u/%u %u/%u\n",
		(double)x0, (double)y0, (double)z0,
		(double)x1, (double)y1, (double)z1,
		(double)x2, (double)y2, (double)z2,
		(double)u0, (double)v0,
		(double)u1, (double)v1,
		(double)u2, (double)v2,
		i0, i0, i1, i1, i2, i2);
}

static s32 s_tilesGeoStride(const struct geo *geo)
{
	if (!geo) return -1;
	s32 n = geo->numvertices;
	if (geo->type == GEOTYPE_TILE_I) {
		if (n < 3 || n > 64) return -1;
		return 0x0e + n * 6;
	}
	if (geo->type == GEOTYPE_TILE_F) {
		if (n < 3 || n > 64) return -1;
		return 0x10 + n * 12;
	}
	if (geo->type == GEOTYPE_BLOCK) {
		if (n < 3 || n > 8) return -1;
		return (s32)sizeof(struct geoblock);
	}
	if (geo->type == GEOTYPE_CYL) {
		return (s32)sizeof(struct geocyl);
	}
	return -1;
}

static s32 s_buildTilesExports(const u8 *data, u32 size,
                               pdscenario_textbuf_t *obj,
                               pdscenario_visualmesh_t *visual,
                               pdscenario_textbuf_t *tsv,
                               u32 *out_rooms, u32 *out_geos,
                               u32 *out_tris)
{
	if (out_rooms) *out_rooms = 0;
	if (out_geos) *out_geos = 0;
	if (out_tris) *out_tris = 0;
	if (!data || size < 12 || !obj || !tsv) return -1;

	u32 num_rooms = *(const u32 *)data;
	if (num_rooms == 0 || num_rooms > 4096) return -1;
	if (4u + (num_rooms + 1u) * 4u > size) return -1;
	const u32 *offsets = (const u32 *)(data + 4);
	if (offsets[0] < 4u + (num_rooms + 1u) * 4u || offsets[num_rooms] > size) {
		return -1;
	}
	for (u32 i = 0; i < num_rooms; i++) {
		if (offsets[i] > offsets[i + 1] || offsets[i + 1] > size) return -1;
	}

	if (s_textbufAppend(obj,
			"# Perfect Dark 2 base scenario collision export\n"
			"mtllib scenario.mtl\n"
			"usemtl collision\n") != 0 ||
	    s_textbufAppend(tsv,
			"room_index\tgeo_index\ttype\tflags\tfloortype\tfloorcol\tvertex_index\tx\ty\tz\tymin\tymax\tradius\n") != 0) {
		return -1;
	}

	u32 next_index = 1;
	u32 geos = 0;
	u32 tris = 0;

	static const f32 cylx[8] = { 1.0f, 0.707107f, 0.0f, -0.707107f, -1.0f, -0.707107f, 0.0f, 0.707107f };
	static const f32 cylz[8] = { 0.0f, 0.707107f, 1.0f, 0.707107f, 0.0f, -0.707107f, -1.0f, -0.707107f };

	for (u32 room = 0; room < num_rooms; room++) {
		const u8 *ptr = data + offsets[room];
		const u8 *end = data + offsets[room + 1];
		if (s_textbufAppendf(obj, "g room_%u\n", room) != 0) return -1;

		u32 geo_index = 0;
		while (ptr + sizeof(struct geo) <= end) {
			const struct geo *geo = (const struct geo *)ptr;
			s32 stride = s_tilesGeoStride(geo);
			if (stride <= 0 || ptr + stride > end) break;

			geos++;
			if (geo->type == GEOTYPE_TILE_I) {
				const struct geotilei *tile = (const struct geotilei *)ptr;
				for (s32 v = 0; v < geo->numvertices; v++) {
					if (s_textbufAppendf(tsv, "%u\t%u\tTILE_I\t%u\t%u\t%u\t%d\t%d\t%d\t%d\t\t\t\n",
							room, geo_index, (unsigned)geo->flags,
							(unsigned)tile->floortype, (unsigned)tile->floorcol,
							v, (int)tile->vertices[v][0],
							(int)tile->vertices[v][1],
							(int)tile->vertices[v][2]) != 0) return -1;
				}
				for (s32 i = 1; i < geo->numvertices - 1; i++) {
					if (s_objEmitTri(obj, visual, &next_index,
							(f32)tile->vertices[0][0], (f32)tile->vertices[0][1], (f32)tile->vertices[0][2],
							(f32)tile->vertices[i][0], (f32)tile->vertices[i][1], (f32)tile->vertices[i][2],
							(f32)tile->vertices[i + 1][0], (f32)tile->vertices[i + 1][1], (f32)tile->vertices[i + 1][2]) != 0) return -1;
					tris++;
				}
			} else if (geo->type == GEOTYPE_TILE_F) {
				const struct geotilef *tile = (const struct geotilef *)ptr;
				for (s32 v = 0; v < geo->numvertices; v++) {
					if (s_textbufAppendf(tsv, "%u\t%u\tTILE_F\t%u\t%u\t%u\t%d\t%.6f\t%.6f\t%.6f\t\t\t\n",
							room, geo_index, (unsigned)geo->flags,
							(unsigned)tile->floortype, (unsigned)tile->floorcol,
							v, (double)tile->vertices[v].x,
							(double)tile->vertices[v].y,
							(double)tile->vertices[v].z) != 0) return -1;
				}
				for (s32 i = 1; i < geo->numvertices - 1; i++) {
					const struct coord *v0 = &tile->vertices[0];
					const struct coord *v1 = &tile->vertices[i];
					const struct coord *v2 = &tile->vertices[i + 1];
					if (s_objEmitTri(obj, visual, &next_index,
							v0->x, v0->y, v0->z,
							v1->x, v1->y, v1->z,
							v2->x, v2->y, v2->z) != 0) return -1;
					tris++;
				}
			} else if (geo->type == GEOTYPE_BLOCK) {
				const struct geoblock *block = (const struct geoblock *)ptr;
				for (s32 v = 0; v < geo->numvertices; v++) {
					if (s_textbufAppendf(tsv, "%u\t%u\tBLOCK\t%u\t\t\t%d\t%.6f\t\t%.6f\t%.6f\t%.6f\t\n",
							room, geo_index, (unsigned)geo->flags, v,
							(double)block->vertices[v][0],
							(double)block->vertices[v][1],
							(double)block->ymin, (double)block->ymax) != 0) return -1;
				}
				for (s32 i = 0; i < geo->numvertices; i++) {
					s32 j = (i + 1) % geo->numvertices;
					f32 x0 = block->vertices[i][0], z0 = block->vertices[i][1];
					f32 x1 = block->vertices[j][0], z1 = block->vertices[j][1];
					if (s_objEmitTri(obj, visual, &next_index, x0, block->ymin, z0, x1, block->ymin, z1, x1, block->ymax, z1) != 0 ||
					    s_objEmitTri(obj, visual, &next_index, x0, block->ymin, z0, x1, block->ymax, z1, x0, block->ymax, z0) != 0) return -1;
					tris += 2;
				}
				for (s32 i = 1; i < geo->numvertices - 1; i++) {
					if (s_objEmitTri(obj, visual, &next_index,
							block->vertices[0][0], block->ymax, block->vertices[0][1],
							block->vertices[i][0], block->ymax, block->vertices[i][1],
							block->vertices[i + 1][0], block->ymax, block->vertices[i + 1][1]) != 0) return -1;
					tris++;
				}
			} else if (geo->type == GEOTYPE_CYL) {
				const struct geocyl *cyl = (const struct geocyl *)ptr;
				if (s_textbufAppendf(tsv, "%u\t%u\tCYL\t%u\t\t\t0\t%.6f\t\t%.6f\t%.6f\t%.6f\t%.6f\n",
						room, geo_index, (unsigned)geo->flags,
						(double)cyl->x, (double)cyl->z,
						(double)cyl->ymin, (double)cyl->ymax,
						(double)cyl->radius) != 0) return -1;
				for (s32 i = 0; i < 8; i++) {
					s32 j = (i + 1) & 7;
					f32 x0 = cyl->x + cylx[i] * cyl->radius;
					f32 z0 = cyl->z + cylz[i] * cyl->radius;
					f32 x1 = cyl->x + cylx[j] * cyl->radius;
					f32 z1 = cyl->z + cylz[j] * cyl->radius;
					if (s_objEmitTri(obj, visual, &next_index, x0, cyl->ymin, z0, x1, cyl->ymin, z1, x1, cyl->ymax, z1) != 0 ||
					    s_objEmitTri(obj, visual, &next_index, x0, cyl->ymin, z0, x1, cyl->ymax, z1, x0, cyl->ymax, z0) != 0) return -1;
					tris += 2;
				}
			}

			ptr += stride;
			geo_index++;
		}
	}

	if (out_rooms) *out_rooms = num_rooms;
	if (out_geos) *out_geos = geos;
	if (out_tris) *out_tris = tris;
	return 0;
}

static void s_padAlignedVec(u32 flags, u32 xflag, u32 yflag, u32 zflag,
                            u32 invertflag, f32 *x, f32 *y, f32 *z)
{
	*x = *y = *z = 0.0f;
	f32 sign = (flags & invertflag) ? -1.0f : 1.0f;
	if (flags & xflag) *x = sign;
	else if (flags & yflag) *y = sign;
	else if (flags & zflag) *z = sign;
}

static void s_padRef(s32 pad, char *out, size_t out_size)
{
	if (!out || out_size == 0) return;
	if (pad < 0) {
		out[0] = '\0';
		return;
	}
	snprintf(out, out_size, "pad_%04d", pad);
}

static void s_roomRef(s32 room, char *out, size_t out_size)
{
	if (!out || out_size == 0) return;
	if (room < 0) {
		out[0] = '\0';
		return;
	}
	snprintf(out, out_size, "room_%04d", room);
}

static s32 s_buildPadsTsv(const u8 *data, u32 size,
                          pdscenario_textbuf_t *tsv,
                          pdscenario_textbuf_t *spawns_tsv,
                          pdscenario_textbuf_t *volumes_tsv,
                          u32 *out_pads)
{
	if (out_pads) *out_pads = 0;
	if (!data || size < sizeof(struct padsfileheader) || !tsv) return -1;
	const struct padsfileheader *hdr = (const struct padsfileheader *)data;
	if (hdr->numpads < 0 || hdr->numpads > 8192) return -1;
	if ((uintptr_t)&hdr->padoffsets[hdr->numpads] > (uintptr_t)data + size) return -1;

	if (s_textbufAppend(tsv,
			"pad_id\troom_ref\tliftnum\tflags\tpos_x\tpos_y\tpos_z\tup_x\tup_y\tup_z\tlook_x\tlook_y\tlook_z\tbbox_xmin\tbbox_xmax\tbbox_ymin\tbbox_ymax\tbbox_zmin\tbbox_zmax\n") != 0) {
		return -1;
	}
	if (spawns_tsv && s_textbufAppend(spawns_tsv,
			"spawn_id\tpad_ref\troom_ref\tteam\tprofile\tpos_x\tpos_y\tpos_z\tlook_x\tlook_y\tlook_z\n") != 0) {
		return -1;
	}
	if (volumes_tsv && s_textbufAppend(volumes_tsv,
			"volume_id\tpad_ref\tkind\troom_ref\tshape\tmin_x\tmin_y\tmin_z\tmax_x\tmax_y\tmax_z\n") != 0) {
		return -1;
	}

	for (s32 i = 0; i < hdr->numpads; i++) {
		char pad_ref[32];
		char room_ref[32];
		u32 offset = hdr->padoffsets[i];
		if (offset + 4 > size) return -1;
		const u8 *ptr = data + offset;
		u32 header = *(const u32 *)ptr;
		u32 flags = header >> 14;
		s32 room = ((s32)(header << 18)) >> 22;
		s32 liftnum = (s32)(header & 0x0f);
		ptr += 4;

		f32 pos[3] = { 0.0f, 0.0f, 0.0f };
		f32 up[3] = { 0.0f, 1.0f, 0.0f };
		f32 look[3] = { 0.0f, 0.0f, 1.0f };
		f32 bbox[6] = { -100.0f, 100.0f, -100.0f, 100.0f, -100.0f, 100.0f };

		if (flags & PADFLAG_INTPOS) {
			if (ptr + 8 > data + size) return -1;
			const s16 *s = (const s16 *)ptr;
			pos[0] = (f32)s[0]; pos[1] = (f32)s[1]; pos[2] = (f32)s[2];
			ptr += 8;
		} else {
			if (ptr + 12 > data + size) return -1;
			const f32 *f = (const f32 *)ptr;
			pos[0] = f[0]; pos[1] = f[1]; pos[2] = f[2];
			ptr += 12;
		}

		if (flags & (PADFLAG_UPALIGNTOX | PADFLAG_UPALIGNTOY | PADFLAG_UPALIGNTOZ)) {
			s_padAlignedVec(flags, PADFLAG_UPALIGNTOX, PADFLAG_UPALIGNTOY,
				PADFLAG_UPALIGNTOZ, PADFLAG_UPALIGNINVERT, &up[0], &up[1], &up[2]);
		} else {
			if (ptr + 12 > data + size) return -1;
			const f32 *f = (const f32 *)ptr;
			up[0] = f[0]; up[1] = f[1]; up[2] = f[2];
			ptr += 12;
		}

		if (flags & (PADFLAG_LOOKALIGNTOX | PADFLAG_LOOKALIGNTOY | PADFLAG_LOOKALIGNTOZ)) {
			s_padAlignedVec(flags, PADFLAG_LOOKALIGNTOX, PADFLAG_LOOKALIGNTOY,
				PADFLAG_LOOKALIGNTOZ, PADFLAG_LOOKALIGNINVERT, &look[0], &look[1], &look[2]);
		} else {
			if (ptr + 12 > data + size) return -1;
			const f32 *f = (const f32 *)ptr;
			look[0] = f[0]; look[1] = f[1]; look[2] = f[2];
			ptr += 12;
		}

		if (flags & PADFLAG_HASBBOXDATA) {
			if (ptr + 24 > data + size) return -1;
			const f32 *f = (const f32 *)ptr;
			for (s32 j = 0; j < 6; j++) bbox[j] = f[j];
		}

		s_padRef(i, pad_ref, sizeof(pad_ref));
		s_roomRef(room, room_ref, sizeof(room_ref));

		if (s_textbufAppendf(tsv,
				"%s\t%s\t%d\t0x%05x\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\n",
				pad_ref, room_ref, liftnum, (unsigned)flags,
				(double)pos[0], (double)pos[1], (double)pos[2],
				(double)up[0], (double)up[1], (double)up[2],
				(double)look[0], (double)look[1], (double)look[2],
				(double)bbox[0], (double)bbox[1], (double)bbox[2],
				(double)bbox[3], (double)bbox[4], (double)bbox[5]) != 0) {
			return -1;
		}
		if (spawns_tsv && s_textbufAppendf(spawns_tsv,
				"spawn_%04d\t%s\t%s\tany\tdefault\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\n",
				i, pad_ref, room_ref,
				(double)pos[0], (double)pos[1], (double)pos[2],
				(double)look[0], (double)look[1], (double)look[2]) != 0) {
			return -1;
		}
		if (volumes_tsv && s_textbufAppendf(volumes_tsv,
				"volume_pad_%04d\t%s\tpad_bounds\t%s\taabb\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\n",
				i, pad_ref, room_ref,
				(double)(pos[0] + bbox[0]), (double)(pos[1] + bbox[2]),
				(double)(pos[2] + bbox[4]),
				(double)(pos[0] + bbox[1]), (double)(pos[1] + bbox[3]),
				(double)(pos[2] + bbox[5])) != 0) {
			return -1;
		}
	}

	if (out_pads) *out_pads = (u32)hdr->numpads;
	return 0;
}

static const char *s_objTypeName(u8 type)
{
	switch (type) {
	case OBJTYPE_DOOR: return "door";
	case OBJTYPE_DOORSCALE: return "door_scale";
	case OBJTYPE_BASIC: return "prop";
	case OBJTYPE_KEY: return "key";
	case OBJTYPE_ALARM: return "alarm";
	case OBJTYPE_CCTV: return "cctv";
	case OBJTYPE_AMMOCRATE: return "ammo_crate";
	case OBJTYPE_WEAPON: return "weapon_pickup";
	case OBJTYPE_CHR: return "character_spawn";
	case OBJTYPE_SINGLEMONITOR: return "single_monitor";
	case OBJTYPE_MULTIMONITOR: return "multi_monitor";
	case OBJTYPE_HANGINGMONITORS: return "hanging_monitors";
	case OBJTYPE_AUTOGUN: return "autogun";
	case OBJTYPE_LINKGUNS: return "linked_guns";
	case OBJTYPE_DEBRIS: return "debris";
	case OBJTYPE_HAT: return "hat";
	case OBJTYPE_GRENADEPROB: return "grenade_probability";
	case OBJTYPE_LINKLIFTDOOR: return "lift_door_link";
	case OBJTYPE_MULTIAMMOCRATE: return "multi_ammo_crate";
	case OBJTYPE_SHIELD: return "shield";
	case OBJTYPE_TAG: return "tag";
	case OBJTYPE_BEGINOBJECTIVE: return "objective_begin";
	case OBJTYPE_ENDOBJECTIVE: return "objective_end";
	case OBJECTIVETYPE_DESTROYOBJ: return "objective_destroy_object";
	case OBJECTIVETYPE_COMPFLAGS: return "objective_complete_flags";
	case OBJECTIVETYPE_FAILFLAGS: return "objective_fail_flags";
	case OBJECTIVETYPE_COLLECTOBJ: return "objective_collect_object";
	case OBJECTIVETYPE_THROWOBJ: return "objective_throw_object";
	case OBJECTIVETYPE_HOLOGRAPH: return "objective_holograph";
	case OBJECTIVETYPE_1F: return "objective_marker";
	case OBJECTIVETYPE_ENTERROOM: return "objective_enter_room";
	case OBJECTIVETYPE_THROWINROOM: return "objective_throw_in_room";
	case OBJTYPE_22: return "objective_marker_22";
	case OBJTYPE_BRIEFING: return "briefing";
	case OBJTYPE_GASBOTTLE: return "gas_bottle";
	case OBJTYPE_RENAMEOBJ: return "rename_object";
	case OBJTYPE_PADLOCKEDDOOR: return "padlocked_door";
	case OBJTYPE_TRUCK: return "truck";
	case OBJTYPE_HELI: return "heli";
	case OBJTYPE_TANK: return "tank";
	case OBJTYPE_CAMERAPOS: return "camera_position";
	case OBJTYPE_GLASS: return "glass";
	case OBJTYPE_SAFE: return "safe";
	case OBJTYPE_SAFEITEM: return "safe_item";
	case OBJTYPE_TINTEDGLASS: return "tinted_glass";
	case OBJTYPE_LIFT: return "lift";
	case OBJTYPE_CONDITIONALSCENERY: return "conditional_scenery";
	case OBJTYPE_BLOCKEDPATH: return "blocked_path";
	case OBJTYPE_HOVERBIKE: return "hoverbike";
	case OBJTYPE_HOVERPROP: return "hover_prop";
	case OBJTYPE_FAN: return "fan";
	case OBJTYPE_HOVERCAR: return "hover_car";
	case OBJTYPE_PADEFFECT: return "pad_effect";
	case OBJTYPE_CHOPPER: return "chopper";
	case OBJTYPE_MINE: return "mine";
	case OBJTYPE_ESCASTEP: return "escalator_step";
	default: return "unknown";
	}
}

static u8 s_setupCommandType(const u8 *ptr)
{
	u32 first = 0;
	if (!ptr) return 0xffu;
	memcpy(&first, ptr, sizeof(first));
	return (u8)PD_BE32(first);
}

static s32 s_setupCommandLengthBytes(const u8 *ptr)
{
	if (!ptr) return 0;
	switch (s_setupCommandType(ptr)) {
	case OBJTYPE_CHR:                return (s32)sizeof(struct packedchr);
	case OBJTYPE_DOOR:               return (s32)sizeof(struct doorobj);
	case OBJTYPE_DOORSCALE:          return (s32)sizeof(struct doorscaleobj);
	case OBJTYPE_BASIC:              return (s32)sizeof(struct defaultobj);
	case OBJTYPE_DEBRIS:             return (s32)sizeof(struct debrisobj);
	case OBJTYPE_GLASS:              return (s32)sizeof(struct glassobj);
	case OBJTYPE_TINTEDGLASS:        return (s32)sizeof(struct tintedglassobj);
	case OBJTYPE_SAFE:               return (s32)sizeof(struct safeobj);
	case OBJTYPE_GASBOTTLE:          return (s32)sizeof(struct gasbottleobj);
	case OBJTYPE_KEY:                return (s32)sizeof(struct keyobj);
	case OBJTYPE_ALARM:              return (s32)sizeof(struct alarmobj);
	case OBJTYPE_CCTV:               return (s32)sizeof(struct cctvobj);
	case OBJTYPE_AMMOCRATE:          return (s32)sizeof(struct ammocrateobj);
	case OBJTYPE_WEAPON:             return (s32)sizeof(struct weaponobj);
	case OBJTYPE_SINGLEMONITOR:      return (s32)sizeof(struct singlemonitorobj);
	case OBJTYPE_MULTIMONITOR:       return (s32)sizeof(struct multimonitorobj);
	case OBJTYPE_HANGINGMONITORS:    return (s32)sizeof(struct hangingmonitorsobj);
	case OBJTYPE_AUTOGUN:            return (s32)sizeof(struct autogunobj);
	case OBJTYPE_LINKGUNS:           return (s32)sizeof(struct linkgunsobj);
	case OBJTYPE_HAT:                return (s32)sizeof(struct hatobj);
	case OBJTYPE_GRENADEPROB:        return (s32)sizeof(struct grenadeprobobj);
	case OBJTYPE_LINKLIFTDOOR:       return (s32)sizeof(struct linkliftdoorobj);
	case OBJTYPE_SAFEITEM:           return (s32)sizeof(struct safeitemobj);
	case OBJTYPE_MULTIAMMOCRATE:     return (s32)sizeof(struct multiammocrateobj);
	case OBJTYPE_SHIELD:             return (s32)sizeof(struct shieldobj);
	case OBJTYPE_TAG:                return (s32)sizeof(struct tag);
	case OBJTYPE_RENAMEOBJ:          return (s32)sizeof(struct textoverride);
	case OBJTYPE_BEGINOBJECTIVE:     return (s32)sizeof(struct objective);
	case OBJTYPE_ENDOBJECTIVE:       return (s32)sizeof(u32);
	case OBJECTIVETYPE_DESTROYOBJ:
	case OBJECTIVETYPE_COMPFLAGS:
	case OBJECTIVETYPE_FAILFLAGS:
	case OBJECTIVETYPE_COLLECTOBJ:
	case OBJECTIVETYPE_THROWOBJ:     return (s32)(sizeof(u32) * 2u);
	case OBJECTIVETYPE_HOLOGRAPH:    return (s32)sizeof(struct criteria_holograph);
	case OBJECTIVETYPE_1F:           return (s32)sizeof(u32);
	case OBJECTIVETYPE_ENTERROOM:    return (s32)sizeof(struct criteria_roomentered);
	case OBJECTIVETYPE_THROWINROOM:  return (s32)sizeof(struct criteria_throwinroom);
	case OBJTYPE_22:                 return (s32)sizeof(u32);
	case OBJTYPE_BRIEFING:           return (s32)sizeof(struct briefingobj);
	case OBJTYPE_PADLOCKEDDOOR:      return (s32)sizeof(struct padlockeddoorobj);
	case OBJTYPE_TRUCK:              return (s32)sizeof(struct truckobj);
	case OBJTYPE_HELI:               return (s32)sizeof(struct heliobj);
	case OBJTYPE_TANK:               return (s32)(32u * sizeof(u32));
	case OBJTYPE_CAMERAPOS:          return (s32)sizeof(struct cameraposobj);
	case OBJTYPE_LIFT:               return (s32)sizeof(struct liftobj);
	case OBJTYPE_CONDITIONALSCENERY: return (s32)sizeof(struct linksceneryobj);
	case OBJTYPE_BLOCKEDPATH:        return (s32)sizeof(struct blockedpathobj);
	case OBJTYPE_HOVERBIKE:          return (s32)sizeof(struct hoverbikeobj);
	case OBJTYPE_HOVERPROP:          return (s32)sizeof(struct hoverpropobj);
	case OBJTYPE_FAN:                return (s32)sizeof(struct fanobj);
	case OBJTYPE_HOVERCAR:           return (s32)sizeof(struct hovercarobj);
	case OBJTYPE_CHOPPER:            return (s32)sizeof(struct chopperobj);
	case OBJTYPE_PADEFFECT:          return (s32)sizeof(struct padeffectobj);
	case OBJTYPE_MINE:               return (s32)sizeof(struct weaponobj);
	case OBJTYPE_ESCASTEP:           return (s32)sizeof(struct escalatorobj);
	case OBJTYPE_END:                return (s32)sizeof(u32);
	default:                         return (s32)sizeof(u32);
	}
}

static s32 s_objTypeHasDefaultBase(u8 type)
{
	switch (type) {
	case OBJTYPE_DOOR:
	case OBJTYPE_BASIC:
	case OBJTYPE_KEY:
	case OBJTYPE_ALARM:
	case OBJTYPE_CCTV:
	case OBJTYPE_AMMOCRATE:
	case OBJTYPE_WEAPON:
	case OBJTYPE_SINGLEMONITOR:
	case OBJTYPE_MULTIMONITOR:
	case OBJTYPE_HANGINGMONITORS:
	case OBJTYPE_AUTOGUN:
	case OBJTYPE_DEBRIS:
	case OBJTYPE_HAT:
	case OBJTYPE_MULTIAMMOCRATE:
	case OBJTYPE_SHIELD:
	case OBJTYPE_GASBOTTLE:
	case OBJTYPE_TRUCK:
	case OBJTYPE_HELI:
	case OBJTYPE_GLASS:
	case OBJTYPE_SAFE:
	case OBJTYPE_TINTEDGLASS:
	case OBJTYPE_LIFT:
	case OBJTYPE_HOVERBIKE:
	case OBJTYPE_HOVERPROP:
	case OBJTYPE_FAN:
	case OBJTYPE_HOVERCAR:
	case OBJTYPE_CHOPPER:
	case OBJTYPE_MINE:
	case OBJTYPE_ESCASTEP:
		return 1;
	default:
		return 0;
	}
}

static const char *s_nonnullCatalogId(const char *id)
{
	return id ? id : "";
}

static s32 s_buildSetupTables(const u8 *data, u32 size,
                              pdscenario_textbuf_t *objects_tsv,
                              pdscenario_textbuf_t *objectives_tsv,
                              u32 *out_objects, u32 *out_objectives)
{
	if (out_objects) *out_objects = 0;
	if (out_objectives) *out_objectives = 0;
	if (!data || size < sizeof(struct stagesetup) || !objects_tsv ||
			!objectives_tsv) {
		return -1;
	}

	const struct stagesetup *setup = (const struct stagesetup *)data;
	uintptr_t props_ofs = (uintptr_t)setup->props;
	if (props_ofs >= size) return -1;

	if (s_textbufAppend(objects_tsv,
			"record_id\tkind\tpad_ref\tmodel_catalog_id\tweapon_catalog_id\tsecondary_weapon_catalog_id\tbody_catalog_id\thead_catalog_id\tailist_ref\tflags\tflags2\tflags3\n") != 0 ||
	    s_textbufAppend(objectives_tsv,
			"objective_id\tkind\ttext_token\tdifficulty_mask\tgraph_node\n") != 0) {
		return -1;
	}

	const u8 *ptr = data + props_ofs;
	const u8 *end = data + size;
	u32 object_count = 0;
	u32 objective_count = 0;
	while (ptr + sizeof(u32) <= end && object_count < 16384u) {
		u8 type = s_setupCommandType(ptr);
		s32 len = s_setupCommandLengthBytes(ptr);
		if (len <= 0 || ptr + len > end) return -1;
		if (type == OBJTYPE_END) break;

		char record_id[32];
		snprintf(record_id, sizeof(record_id), "setup_%04u", (unsigned)object_count);

		if (type == OBJTYPE_CHR) {
			const struct packedchr *chr = (const struct packedchr *)ptr;
			char pad_ref[32];
			char ailist_ref[32];
			s_padRef((s32)chr->padnum, pad_ref, sizeof(pad_ref));
			snprintf(ailist_ref, sizeof(ailist_ref), "ailist_%04u",
				(unsigned)chr->ailistnum);
			if (s_textbufAppendf(objects_tsv,
					"%s\t%s\t%s\t\t\t\t%s\t%s\t%s\t0x%08x\t0x%08x\t\n",
					record_id, s_objTypeName(type), pad_ref,
					s_nonnullCatalogId(catalogBodyIdByBodynum(chr->bodynum)),
					s_nonnullCatalogId(catalogHeadIdByHeadnum(chr->headnum)),
					ailist_ref, (unsigned)chr->flags,
					(unsigned)chr->flags2) != 0) {
				return -1;
			}
		} else if (s_objTypeHasDefaultBase(type)) {
			const struct defaultobj *obj = (const struct defaultobj *)ptr;
			char pad_ref[32];
			const char *weapon_ref = "";
			const char *dual_weapon_ref = "";
			s_padRef((s32)obj->pad, pad_ref, sizeof(pad_ref));
			if ((type == OBJTYPE_WEAPON || type == OBJTYPE_MINE) &&
					len >= (s32)sizeof(struct weaponobj)) {
				const struct weaponobj *w = (const struct weaponobj *)ptr;
				weapon_ref = s_nonnullCatalogId(
					catalogWeaponIdByRuntimeWeaponNum(w->weaponnum));
				dual_weapon_ref = s_nonnullCatalogId(
					catalogWeaponIdByRuntimeWeaponNum(w->dualweaponnum));
			}
			if (s_textbufAppendf(objects_tsv,
					"%s\t%s\t%s\t%s\t%s\t%s\t\t\t\t0x%08x\t0x%08x\t0x%08x\n",
					record_id, s_objTypeName(type), pad_ref,
					s_nonnullCatalogId(catalogModelIdByModelnum(obj->modelnum)),
					weapon_ref, dual_weapon_ref,
					(unsigned)obj->flags, (unsigned)obj->flags2,
					(unsigned)obj->flags3) != 0) {
				return -1;
			}
		} else {
			if (s_textbufAppendf(objects_tsv,
					"%s\t%s\t\t\t\t\t\t\t\t\t\t\n",
					record_id, s_objTypeName(type)) != 0) {
				return -1;
			}
		}

		if (type == OBJTYPE_BEGINOBJECTIVE) {
			const struct objective *obj = (const struct objective *)ptr;
			if (s_textbufAppendf(objectives_tsv,
					"objective_%04u\tobjective\tobjective_text_%04d\t0x%02x\tlevel.objective.%04u\n",
					(unsigned)objective_count, obj->index,
					(unsigned)obj->difficulties,
					(unsigned)objective_count) != 0) {
				return -1;
			}
			objective_count++;
		} else if (type >= OBJECTIVETYPE_DESTROYOBJ &&
				type <= OBJECTIVETYPE_THROWINROOM) {
			if (s_textbufAppendf(objectives_tsv,
					"objective_step_%04u\t%s\t\t\tlevel.objective_step.%04u\n",
					(unsigned)objective_count, s_objTypeName(type),
					(unsigned)objective_count) != 0) {
				return -1;
			}
			objective_count++;
		}

		object_count++;
		ptr += len;
	}

	if (out_objects) *out_objects = object_count;
	if (out_objectives) *out_objectives = objective_count;
	return 0;
}

static s32 s_buildScenarioSourceFiles(const char *scenario_id,
                                      const char *kind,
                                      u32 room_count, u32 tri_count,
                                      u32 pad_count, u32 object_count,
                                      u32 objective_count,
                                      pdscenario_textbuf_t *navigation_ini,
                                      pdscenario_textbuf_t *level_graph_json,
                                      pdscenario_textbuf_t *collision_meta_json,
                                      pdscenario_textbuf_t *navmesh_meta_json)
{
	if (!scenario_id || !navigation_ini || !level_graph_json ||
			!collision_meta_json || !navmesh_meta_json) {
		return -1;
	}

	if (s_textbufAppendf(navigation_ini,
			"[navigation]\n"
			"source = scene.glb\n"
			"collision_source = scene.glb\n"
			"generator = deterministic.surface_graph.v1\n"
			"supports_walk = true\n"
			"supports_jump = true\n"
			"supports_wall = true\n"
			"supports_ceiling = true\n"
			"pads_file = pads.tsv\n"
			"spawns_file = spawns.tsv\n"
			"volumes_file = volumes.tsv\n"
			"generated_cache = _meta/generated-navmesh.json\n") != 0) {
		return -1;
	}

	if (s_textbufAppendf(level_graph_json,
			"{\n"
			"  \"schema\": \"pd2.level.graph.v1\",\n"
			"  \"scenario\": \"%s\",\n"
			"  \"source\": \"scene.glb\",\n"
			"  \"kind\": \"%s\",\n"
			"  \"tables\": {\n"
			"    \"pads\": \"pads.tsv\",\n"
			"    \"spawns\": \"spawns.tsv\",\n"
			"    \"volumes\": \"volumes.tsv\",\n"
			"    \"objects\": \"objects.tsv\",\n"
			"    \"objectives\": \"objectives.tsv\"\n"
			"  },\n"
			"  \"nodes\": [\n"
			"    { \"id\": \"scenario.load\", \"kind\": \"event.scenario.load\" },\n"
			"    { \"id\": \"source.scene\", \"kind\": \"scenario.scene.source\", \"file\": \"scene.glb\" },\n"
			"    { \"id\": \"collision.generate\", \"kind\": \"scenario.collision.generate\", \"source\": \"scene.glb\", \"cache\": \"_meta/generated-collision.json\" },\n"
			"    { \"id\": \"navigation.generate\", \"kind\": \"scenario.navigation.generate\", \"source\": \"navigation.ini\", \"cache\": \"_meta/generated-navmesh.json\" },\n"
			"    { \"id\": \"setup.tables\", \"kind\": \"scenario.setup.tables\", \"objects\": \"objects.tsv\", \"objectives\": \"objectives.tsv\" }\n"
			"  ],\n"
			"  \"links\": [\n"
			"    { \"from\": \"scenario.load\", \"to\": \"source.scene\" },\n"
			"    { \"from\": \"source.scene\", \"to\": \"collision.generate\" },\n"
			"    { \"from\": \"collision.generate\", \"to\": \"navigation.generate\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.load\" }\n"
			"  ],\n"
			"  \"counts\": { \"rooms\": %u, \"triangles\": %u, \"pads\": %u, \"objects\": %u, \"objectives\": %u }\n"
			"}\n",
			scenario_id, kind ? kind : "scenario",
			(unsigned)room_count, (unsigned)tri_count,
			(unsigned)pad_count, (unsigned)object_count,
			(unsigned)objective_count) != 0) {
		return -1;
	}

	if (s_textbufAppendf(collision_meta_json,
			"{\n"
			"  \"schema\": \"pd2.generated.collision.v1\",\n"
			"  \"scenario\": \"%s\",\n"
			"  \"derived_from\": \"scene.glb\",\n"
			"  \"override\": null,\n"
			"  \"generator\": \"deterministic.scene_collision.v1\",\n"
			"  \"policy\": {\n"
			"    \"default_collidable\": true,\n"
			"    \"material_tags\": true,\n"
			"    \"node_tags\": true,\n"
			"    \"fallback_when_missing_override\": true\n"
			"  }\n"
			"}\n",
			scenario_id) != 0) {
		return -1;
	}

	if (s_textbufAppendf(navmesh_meta_json,
			"{\n"
			"  \"schema\": \"pd2.generated.navmesh.v1\",\n"
			"  \"scenario\": \"%s\",\n"
			"  \"derived_from\": \"scene.glb\",\n"
			"  \"inputs\": [\"navigation.ini\", \"pads.tsv\", \"spawns.tsv\", \"volumes.tsv\"],\n"
			"  \"generator\": \"deterministic.surface_graph.v1\",\n"
			"  \"capabilities\": [\"walk\", \"jump\", \"drop\", \"wall\", \"ceiling\"],\n"
			"  \"cache_only\": true\n"
			"}\n",
			scenario_id) != 0) {
		return -1;
	}

	return 0;
}

static u16 s_readBe16(const u8 *p)
{
	return (u16)(((u16)p[0] << 8) | p[1]);
}

static u32 s_readBe32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
		((u32)p[2] << 8) | (u32)p[3];
}

static u32 s_alignTo(u32 value, u32 align)
{
	return (value + align - 1u) & ~(align - 1u);
}

static s32 s_rangeInBuffer(const u8 *base, u32 size, const void *ptr, u32 len)
{
	const u8 *p = (const u8 *)ptr;
	if (!base || !p) return 0;
	if (p < base) return 0;
	if ((uintptr_t)(p - base) > size) return 0;
	if (len > size - (u32)(p - base)) return 0;
	return 1;
}

static void *s_promoteRoomPtr(u8 *base, u32 size, uintptr_t raw, u32 room_ofs)
{
	if (!raw) return NULL;
	if (raw < room_ofs) return NULL;
	u32 off = (u32)(raw - room_ofs);
	if (off >= size) return NULL;
	return base + off;
}

static void s_bgSceneBounds(pdscenario_bgscene_t *scene, f32 x, f32 y, f32 z)
{
	if (!scene->has_bounds) {
		scene->min_x = scene->max_x = x;
		scene->min_y = scene->max_y = y;
		scene->min_z = scene->max_z = z;
		scene->has_bounds = 1;
		return;
	}
	if (x < scene->min_x) scene->min_x = x;
	if (y < scene->min_y) scene->min_y = y;
	if (z < scene->min_z) scene->min_z = z;
	if (x > scene->max_x) scene->max_x = x;
	if (y > scene->max_y) scene->max_y = y;
	if (z > scene->max_z) scene->max_z = z;
}

static s32 s_bgSceneReserveTextures(pdscenario_bgscene_t *scene, u32 add)
{
	if (add > 0xffffffffu - scene->texture_count) return -1;
	u32 need = scene->texture_count + add;
	if (need <= scene->texture_cap) return 0;
	u32 cap = scene->texture_cap ? scene->texture_cap : 64;
	while (cap < need) {
		if (cap > 0x80000000u) return -1;
		cap *= 2;
	}
	pdscenario_bgtexture_t *p = (pdscenario_bgtexture_t *)realloc(
		scene->textures, (size_t)cap * sizeof(*scene->textures));
	if (!p) return -1;
	memset(p + scene->texture_cap, 0,
		(size_t)(cap - scene->texture_cap) * sizeof(*p));
	scene->textures = p;
	scene->texture_cap = cap;
	return 0;
}

static s32 s_bgSceneFindTexture(const pdscenario_bgscene_t *scene, u16 texnum)
{
	for (u32 i = 0; i < scene->texture_count; i++) {
		if (scene->textures[i].texnum == texnum) return (s32)i;
	}
	return -1;
}

static s32 s_bgSceneEnsureTexture(pdscenario_bgscene_t *scene, u16 texnum,
                                  s32 used_by_geometry)
{
	s32 existing = s_bgSceneFindTexture(scene, texnum);
	if (existing >= 0) {
		if (used_by_geometry) scene->textures[existing].used_by_geometry = 1;
		return existing;
	}
	if (s_bgSceneReserveTextures(scene, 1) != 0) return -1;
	pdscenario_bgtexture_t *tex = &scene->textures[scene->texture_count];
	memset(tex, 0, sizeof(*tex));
	tex->texnum = texnum;
	tex->used_by_geometry = used_by_geometry ? 1 : 0;
	snprintf(tex->path, sizeof(tex->path),
		"visual/textures/tex_%04x.tga", (unsigned)texnum);
	scene->texture_count++;
	return (s32)(scene->texture_count - 1u);
}

static s32 s_bgSceneReserveMaterials(pdscenario_bgscene_t *scene, u32 add)
{
	if (add > 0xffffffffu - scene->material_count) return -1;
	u32 need = scene->material_count + add;
	if (need <= scene->material_cap) return 0;
	u32 cap = scene->material_cap ? scene->material_cap : 32;
	while (cap < need) {
		if (cap > 0x80000000u) return -1;
		cap *= 2;
	}
	pdscenario_bgmaterial_t *p = (pdscenario_bgmaterial_t *)realloc(
		scene->materials, (size_t)cap * sizeof(*scene->materials));
	if (!p) return -1;
	memset(p + scene->material_cap, 0,
		(size_t)(cap - scene->material_cap) * sizeof(*p));
	scene->materials = p;
	scene->material_cap = cap;
	return 0;
}

static s32 s_bgMaterialReserveVertices(pdscenario_bgmaterial_t *m, u32 add)
{
	if (!m || add > 0xffffffffu - m->vertex_count) return -1;
	u32 need = m->vertex_count + add;
	if (need <= m->vertex_cap) return 0;
	u32 cap = m->vertex_cap ? m->vertex_cap : 1024;
	while (cap < need) {
		if (cap > 0x80000000u) return -1;
		cap *= 2;
	}
	pdscenario_visual_vertex_t *p =
		(pdscenario_visual_vertex_t *)realloc(m->vertices,
			(size_t)cap * sizeof(*m->vertices));
	if (!p) return -1;
	m->vertices = p;
	m->vertex_cap = cap;
	return 0;
}

static s32 s_bgMaterialAddTri(pdscenario_bgmaterial_t *m,
                              f32 x0, f32 y0, f32 z0, f32 u0, f32 v0,
                              f32 x1, f32 y1, f32 z1, f32 u1, f32 v1,
                              f32 x2, f32 y2, f32 z2, f32 u2, f32 v2)
{
	if (!m || s_bgMaterialReserveVertices(m, 3) != 0) return -1;
	m->vertices[m->vertex_count++] =
		(pdscenario_visual_vertex_t){ x0, y0, z0, u0, v0 };
	m->vertices[m->vertex_count++] =
		(pdscenario_visual_vertex_t){ x1, y1, z1, u1, v1 };
	m->vertices[m->vertex_count++] =
		(pdscenario_visual_vertex_t){ x2, y2, z2, u2, v2 };
	return 0;
}

static s32 s_bgSceneEnsureMaterial(pdscenario_bgscene_t *scene, s32 texnum,
                                   s32 texnum2, s32 subcmd, u32 w0, u32 w1)
{
	s32 smode = (s32)((w0 >> 22) & 3u);
	s32 tmode = (s32)((w0 >> 20) & 3u);
	s32 offset = (s32)((w0 >> 18) & 3u);
	s32 shifts = (s32)((w0 >> 14) & 0x0fu);
	s32 shiftt = (s32)((w0 >> 10) & 0x0fu);
	s32 min = (s32)((w1 >> 24) & 0xffu);
	s32 flag = (w0 & 0x200u) ? 1 : 0;

	for (u32 i = 0; i < scene->material_count; i++) {
		pdscenario_bgmaterial_t *m = &scene->materials[i];
		if (m->texnum == texnum && m->texnum2 == texnum2 &&
		    m->subcmd == subcmd && m->w0 == w0 && m->w1 == w1) {
			return (s32)i;
		}
	}

	if (s_bgSceneReserveMaterials(scene, 1) != 0) return -1;
	pdscenario_bgmaterial_t *m = &scene->materials[scene->material_count];
	memset(m, 0, sizeof(*m));
	m->texnum = texnum;
	m->texnum2 = texnum2;
	m->subcmd = subcmd;
	m->w0 = w0;
	m->w1 = w1;
	m->smode = smode;
	m->tmode = tmode;
	m->offset = offset;
	m->shifts = shifts;
	m->shiftt = shiftt;
	m->min = min;
	m->flag = flag;
	if (texnum >= 0 && texnum2 >= 0) {
		snprintf(m->name, sizeof(m->name), "mat_%03u_tex_%04x_%04x_c%d",
			(unsigned)scene->material_count, (unsigned)texnum,
			(unsigned)texnum2, subcmd);
	} else if (texnum >= 0) {
		snprintf(m->name, sizeof(m->name), "mat_%03u_tex_%04x_c%d",
			(unsigned)scene->material_count, (unsigned)texnum, subcmd);
	} else {
		snprintf(m->name, sizeof(m->name), "mat_%03u_untextured",
			(unsigned)scene->material_count);
	}
	if (texnum >= 0) {
		if (s_bgSceneEnsureTexture(scene, (u16)texnum, 1) < 0) return -1;
	}
	if (texnum2 >= 0) {
		if (s_bgSceneEnsureTexture(scene, (u16)texnum2, 1) < 0) return -1;
	}
	scene->material_count++;
	return (s32)(scene->material_count - 1u);
}

static s32 s_bgSceneInit(pdscenario_bgscene_t *scene)
{
	memset(scene, 0, sizeof(*scene));
	scene->next_obj_index = 1;
	scene->current_material = -1;
	if (s_textbufAppend(&scene->obj,
			"# Perfect Dark 2 BG visual display-list export\n"
			"# Import visual/scene.obj in Blender; linked textures live in visual/textures/.\n"
			"mtllib scene.mtl\n") != 0) {
		return -1;
	}
	return s_bgSceneEnsureMaterial(scene, -1, -1, -1, 0, 0);
}

static s32 s_bgSceneAddTri(pdscenario_bgscene_t *scene, s32 material_index,
                           const Vtx *a, const Vtx *b, const Vtx *c,
                           const struct coord *room_pos)
{
	if (!scene || !a || !b || !c || !room_pos) return -1;
	if (material_index < 0 || (u32)material_index >= scene->material_count) {
		material_index = 0;
	}
	if (scene->current_material != material_index) {
		if (s_textbufAppendf(&scene->obj, "usemtl %s\n",
				scene->materials[material_index].name) != 0) {
			return -1;
		}
		scene->current_material = material_index;
	}

	f32 x0 = room_pos->x + (f32)a->x;
	f32 y0 = room_pos->y + (f32)a->y;
	f32 z0 = room_pos->z + (f32)a->z;
	f32 x1 = room_pos->x + (f32)b->x;
	f32 y1 = room_pos->y + (f32)b->y;
	f32 z1 = room_pos->z + (f32)b->z;
	f32 x2 = room_pos->x + (f32)c->x;
	f32 y2 = room_pos->y + (f32)c->y;
	f32 z2 = room_pos->z + (f32)c->z;
	f32 u0 = (f32)a->s / 32.0f;
	f32 v0 = 1.0f - ((f32)a->t / 32.0f);
	f32 u1 = (f32)b->s / 32.0f;
	f32 v1 = 1.0f - ((f32)b->t / 32.0f);
	f32 u2 = (f32)c->s / 32.0f;
	f32 v2 = 1.0f - ((f32)c->t / 32.0f);
	u32 i0 = scene->next_obj_index++;
	u32 i1 = scene->next_obj_index++;
	u32 i2 = scene->next_obj_index++;

	if (s_textbufAppendf(&scene->obj,
			"v %.6f %.6f %.6f\n"
			"v %.6f %.6f %.6f\n"
			"v %.6f %.6f %.6f\n"
			"vt %.6f %.6f\n"
			"vt %.6f %.6f\n"
			"vt %.6f %.6f\n"
			"f %u/%u %u/%u %u/%u\n",
			(double)x0, (double)y0, (double)z0,
			(double)x1, (double)y1, (double)z1,
			(double)x2, (double)y2, (double)z2,
			(double)u0, (double)v0,
			(double)u1, (double)v1,
			(double)u2, (double)v2,
			i0, i0, i1, i1, i2, i2) != 0) {
		return -1;
	}
	if (s_bgMaterialAddTri(&scene->materials[material_index],
			x0, y0, z0, u0, v0,
			x1, y1, z1, u1, v1,
			x2, y2, z2, u2, v2) != 0) {
		return -1;
	}

	s_bgSceneBounds(scene, x0, y0, z0);
	s_bgSceneBounds(scene, x1, y1, z1);
	s_bgSceneBounds(scene, x2, y2, z2);
	scene->materials[material_index].tri_count++;
	scene->tri_count++;
	return 0;
}

static s32 s_tgaFromRgba(const u8 *rgba, u32 width, u32 height,
                         u8 **out_data, u32 *out_size)
{
	if (out_data) *out_data = NULL;
	if (out_size) *out_size = 0;
	if (!rgba || !width || !height || width > 8192 || height > 8192) return -1;
	if (width > (0xffffffffu - 18u) / 4u / height) return -1;
	u32 pixels = width * height;
	u32 size = 18u + pixels * 4u;
	u8 *tga = (u8 *)malloc(size);
	if (!tga) return -1;
	memset(tga, 0, 18);
	tga[2] = 2; /* uncompressed true-colour */
	tga[12] = (u8)(width & 0xff);
	tga[13] = (u8)(width >> 8);
	tga[14] = (u8)(height & 0xff);
	tga[15] = (u8)(height >> 8);
	tga[16] = 32;
	tga[17] = 0x28; /* top-left origin, 8 alpha bits */
	for (u32 i = 0; i < pixels; i++) {
		tga[18u + i * 4u + 0u] = rgba[i * 4u + 2u];
		tga[18u + i * 4u + 1u] = rgba[i * 4u + 1u];
		tga[18u + i * 4u + 2u] = rgba[i * 4u + 0u];
		tga[18u + i * 4u + 3u] = rgba[i * 4u + 3u];
	}
	*out_data = tga;
	*out_size = size;
	return 0;
}

static void s_rgba16ToRgba(u16 p, u8 *out)
{
	u8 r = (u8)((((p >> 11) & 0x1f) * 255u) / 31u);
	u8 g = (u8)((((p >> 6) & 0x1f) * 255u) / 31u);
	u8 b = (u8)((((p >> 1) & 0x1f) * 255u) / 31u);
	out[0] = r;
	out[1] = g;
	out[2] = b;
	out[3] = (p & 1u) ? 255u : 0u;
}

static s32 s_texExportFormat(const struct tex *tex)
{
	if (!tex) return -1;
	if (tex->lutmodeindex != 0) {
		if (tex->depth == G_IM_SIZ_4b) {
			return tex->lutmodeindex == (G_TT_RGBA16 >> G_MDSFT_TEXTLUT)
				? TEXFORMAT_RGBA16_CI4 : TEXFORMAT_IA16_CI4;
		}
		return tex->lutmodeindex == (G_TT_RGBA16 >> G_MDSFT_TEXTLUT)
			? TEXFORMAT_RGBA16_CI8 : TEXFORMAT_IA16_CI8;
	}
	if (tex->gbiformat == G_IM_FMT_RGBA) {
		return tex->depth == G_IM_SIZ_32b ? TEXFORMAT_RGBA32 : TEXFORMAT_RGBA16;
	}
	if (tex->gbiformat == G_IM_FMT_IA) {
		if (tex->depth == G_IM_SIZ_16b) return TEXFORMAT_IA16;
		if (tex->depth == G_IM_SIZ_4b) return TEXFORMAT_IA4;
		return TEXFORMAT_IA8;
	}
	if (tex->gbiformat == G_IM_FMT_I) {
		return tex->depth == G_IM_SIZ_4b ? TEXFORMAT_I4 : TEXFORMAT_I8;
	}
	return -1;
}

static u32 s_texFormatStrideBytes(s32 format, u32 width)
{
	switch (format) {
	case TEXFORMAT_RGBA32:
	case TEXFORMAT_RGB24:
		return s_alignTo(width, 4) * 4u;
	case TEXFORMAT_RGBA16:
	case TEXFORMAT_RGB15:
	case TEXFORMAT_IA16:
		return s_alignTo(width, 4) * 2u;
	case TEXFORMAT_IA8:
	case TEXFORMAT_I8:
	case TEXFORMAT_RGBA16_CI8:
	case TEXFORMAT_IA16_CI8:
		return s_alignTo(width, 8);
	case TEXFORMAT_IA4:
	case TEXFORMAT_I4:
		return s_alignTo(width, 16) >> 1;
	case TEXFORMAT_RGBA16_CI4:
	case TEXFORMAT_IA16_CI4:
		return s_alignTo((width + 1u) >> 1, 8);
	}
	return 0;
}

static u32 s_texFormatImageBytes(s32 format, u32 width, u32 height)
{
	return s_texFormatStrideBytes(format, width) * height;
}

static s32 s_decodeTexToRgba(const struct tex *tex, u8 **out_rgba)
{
	if (out_rgba) *out_rgba = NULL;
	if (!tex || !tex->data || !tex->width || !tex->height) return -1;
	s32 format = s_texExportFormat(tex);
	if (format < 0) return -1;
	u32 width = tex->width;
	u32 height = tex->height;
	if (width > 4096 || height > 4096) return -1;
	u8 *rgba = (u8 *)malloc((size_t)width * height * 4u);
	if (!rgba) return -1;

	u32 stride = s_texFormatStrideBytes(format, width);
	if (stride == 0) {
		free(rgba);
		return -1;
	}

	const u8 *palette = NULL;
	u32 palette_count = 0;
	if (tex->lutmodeindex != 0) {
		u32 lods = tex->numlods ? tex->numlods : 1u;
		u32 pw = width;
		u32 ph = height;
		u32 palette_off = 0;
		for (u32 lod = 0; lod < lods; lod++) {
			palette_off += s_texFormatImageBytes(format, pw, ph);
			if (pw > 1) pw = (pw + 1u) >> 1;
			if (ph > 1) ph = (ph + 1u) >> 1;
		}
		palette = tex->data + palette_off;
		palette_count = (u32)tex->unk0a + 1u;
		if (palette_count > 256u) palette_count = 256u;
	}

	for (u32 y = 0; y < height; y++) {
		const u8 *row = tex->data + y * stride;
		for (u32 x = 0; x < width; x++) {
			u8 *dst = rgba + ((size_t)y * width + x) * 4u;
			switch (format) {
			case TEXFORMAT_RGBA32:
			case TEXFORMAT_RGB24: {
				u32 p = 0;
				memcpy(&p, row + x * 4u, 4);
				dst[0] = (u8)((p >> 24) & 0xffu);
				dst[1] = (u8)((p >> 16) & 0xffu);
				dst[2] = (u8)((p >> 8) & 0xffu);
				dst[3] = format == TEXFORMAT_RGB24 ? 255u : (u8)(p & 0xffu);
				break;
			}
			case TEXFORMAT_RGBA16:
			case TEXFORMAT_RGB15:
				s_rgba16ToRgba(s_readBe16(row + x * 2u), dst);
				if (format == TEXFORMAT_RGB15) dst[3] = 255u;
				break;
			case TEXFORMAT_IA16: {
				u16 p = 0;
				memcpy(&p, row + x * 2u, 2);
				dst[0] = dst[1] = dst[2] = (u8)((p >> 8) & 0xffu);
				dst[3] = (u8)(p & 0xffu);
				break;
			}
			case TEXFORMAT_IA8: {
				u8 p = row[x];
				u8 i = (u8)(((p >> 4) & 0x0fu) * 17u);
				u8 a = (u8)((p & 0x0fu) * 17u);
				dst[0] = dst[1] = dst[2] = i;
				dst[3] = a;
				break;
			}
			case TEXFORMAT_I8:
				dst[0] = dst[1] = dst[2] = row[x];
				dst[3] = 255u;
				break;
			case TEXFORMAT_IA4: {
				u8 p = row[x >> 1];
				u8 n = (x & 1u) ? (p & 0x0fu) : (p >> 4);
				u8 i = (u8)((((n >> 1) & 0x07u) * 255u) / 7u);
				dst[0] = dst[1] = dst[2] = i;
				dst[3] = (n & 1u) ? 255u : 0u;
				break;
			}
			case TEXFORMAT_I4: {
				u8 p = row[x >> 1];
				u8 n = (x & 1u) ? (p & 0x0fu) : (p >> 4);
				dst[0] = dst[1] = dst[2] = (u8)(n * 17u);
				dst[3] = 255u;
				break;
			}
			case TEXFORMAT_RGBA16_CI8:
			case TEXFORMAT_IA16_CI8:
			case TEXFORMAT_RGBA16_CI4:
			case TEXFORMAT_IA16_CI4: {
				u32 idx;
				if (format == TEXFORMAT_RGBA16_CI8 ||
				    format == TEXFORMAT_IA16_CI8) {
					idx = row[x];
				} else {
					u8 p = row[x >> 1];
					idx = (x & 1u) ? (p & 0x0fu) : (p >> 4);
				}
				if (!palette || idx >= palette_count) {
					dst[0] = 255u; dst[1] = 0u; dst[2] = 255u; dst[3] = 255u;
				} else if (format == TEXFORMAT_RGBA16_CI8 ||
				           format == TEXFORMAT_RGBA16_CI4) {
					s_rgba16ToRgba(s_readBe16(palette + idx * 2u), dst);
				} else {
					u16 p = s_readBe16(palette + idx * 2u);
					dst[0] = dst[1] = dst[2] = (u8)((p >> 8) & 0xffu);
					dst[3] = (u8)(p & 0xffu);
				}
				break;
			}
			default:
				dst[0] = 255u; dst[1] = 0u; dst[2] = 255u; dst[3] = 255u;
				break;
			}
		}
	}

	*out_rgba = rgba;
	return 0;
}

static s32 s_decodeTextureImages(u16 texnum,
                                 u8 **out_tga, u32 *out_tga_size,
                                 u8 **out_png, u32 *out_png_size,
                                 u32 *out_width, u32 *out_height)
{
	if (out_tga) *out_tga = NULL;
	if (out_tga_size) *out_tga_size = 0;
	if (out_png) *out_png = NULL;
	if (out_png_size) *out_png_size = 0;
	if (out_width) *out_width = 0;
	if (out_height) *out_height = 0;

	u8 *list_data = romdataSegGetData("textureslist");
	u8 *data = romdataSegGetData("texturesdata");
	u32 list_size = romdataSegGetSize("textureslist");
	u32 data_size = romdataSegGetSize("texturesdata");
	if (!list_data || !data || list_size < sizeof(struct texture) * 2u) return -1;
	u32 list_count = list_size / (u32)sizeof(struct texture);
	if ((u32)texnum + 1u >= list_count || (u32)texnum >= NUM_TEXTURES) return -1;
	const struct texture *list = (const struct texture *)list_data;
	u32 thisoffset = list[texnum].dataoffset;
	u32 nextoffset = list[(u32)texnum + 1u].dataoffset;
	if (thisoffset == nextoffset || nextoffset < thisoffset) return -1;
	u32 base = thisoffset & 0xfffffff8u;
	u32 rel = thisoffset & 7u;
	u32 need = s_alignTo((nextoffset - thisoffset) + rel, 16);
	if (base > data_size || need > data_size - base) return -1;
	u8 *compptr = data + base + rel;
	s32 hasloddata = (*compptr & 0x80) >> 7;
	s32 iszlib = (*compptr & 0x40) >> 6;
	s32 numlods = *compptr & 0x3f;
	if (numlods > 5) numlods = 5;
	compptr++;

	const u32 pool_len = 256u * 1024u;
	u8 *pool_mem = (u8 *)calloc(1, pool_len);
	if (!pool_mem) return -1;
	struct texpool pool;
	texInitPool(&pool, pool_mem, (s32)pool_len);
	*(s16 *)pool.leftpos = (s16)texnum;
	pool.leftpos += 8;
	pool.rightpos--;
	struct tex *tex = pool.rightpos;
	memset(tex, 0, sizeof(*tex));
	tex->texturenum = texnum;
	tex->data = pool.leftpos;
	tex->unk0c_03 = false;

	s32 bytesout = iszlib
		? texInflateZlib(compptr, pool.leftpos, hasloddata, numlods, &pool, 0)
		: texInflateNonZlib(compptr, pool.leftpos, hasloddata, numlods, &pool, 0);
	if (bytesout <= 0 || tex->width == 0 || tex->height == 0) {
		free(pool_mem);
		return -1;
	}

	u8 *rgba = NULL;
	if (s_decodeTexToRgba(tex, &rgba) != 0) {
		free(pool_mem);
		return -1;
	}
	u8 *tga = NULL;
	u32 tga_size = 0;
	s32 ok = s_tgaFromRgba(rgba, tex->width, tex->height, &tga, &tga_size);
	if (ok != 0) {
		free(rgba);
		free(pool_mem);
		return -1;
	}
	int png_len = 0;
	u8 *png = stbi_write_png_to_mem(rgba, (int)tex->width * 4,
		(int)tex->width, (int)tex->height, 4, &png_len);
	free(rgba);
	if (!png || png_len <= 0) {
		if (png) free(png);
		free(tga);
		free(pool_mem);
		return -1;
	}
	if (out_width) *out_width = tex->width;
	if (out_height) *out_height = tex->height;
	*out_tga = tga;
	*out_tga_size = tga_size;
	*out_png = png;
	*out_png_size = (u32)png_len;
	free(pool_mem);
	return 0;
}

static void s_bgSceneDecodeTextures(pdscenario_bgscene_t *scene)
{
	for (u32 i = 0; i < scene->texture_count; i++) {
		pdscenario_bgtexture_t *tex = &scene->textures[i];
		if (tex->decoded || tex->tga) continue;
		if (s_decodeTextureImages(tex->texnum,
				&tex->tga, &tex->tga_size,
				&tex->png, &tex->png_size,
				&tex->width, &tex->height) == 0) {
			tex->decoded = 1;
			scene->decoded_texture_count++;
		} else {
			scene->failed_texture_count++;
		}
	}
}

static const pdscenario_bgtexture_t *s_bgSceneTextureByNum(
	const pdscenario_bgscene_t *scene, s32 texnum)
{
	if (texnum < 0) return NULL;
	s32 idx = s_bgSceneFindTexture(scene, (u16)texnum);
	return idx >= 0 ? &scene->textures[idx] : NULL;
}

static s32 s_bgSceneFinalizeMaterials(pdscenario_bgscene_t *scene)
{
	if (s_textbufAppend(&scene->mtl,
			"# Perfect Dark 2 BG visual material export\n") != 0 ||
	    s_textbufAppend(&scene->materials_tsv,
			"material\ttexture_num\tsecondary_texture_num\tsubcmd\tsmode\ttmode\toffset\tshifts\tshiftt\tmin\tflag\ttriangles\ttexture_file\twidth\theight\tdecoded\n") != 0) {
		return -1;
	}

	for (u32 i = 0; i < scene->material_count; i++) {
		const pdscenario_bgmaterial_t *m = &scene->materials[i];
		const pdscenario_bgtexture_t *tex = s_bgSceneTextureByNum(scene, m->texnum);
		const char *texture_path = (tex && tex->decoded) ? tex->path : "";
		const char *mtl_texture_path = texture_path;
		if (strncmp(mtl_texture_path, "visual/", 7) == 0) {
			mtl_texture_path += 7;
		}
		u32 width = tex ? tex->width : 0u;
		u32 height = tex ? tex->height : 0u;
		s32 decoded = tex ? tex->decoded : 0;
		if (s_textbufAppendf(&scene->mtl,
				"newmtl %s\n"
				"Kd 1.000000 1.000000 1.000000\n"
				"Ka 0.150000 0.150000 0.150000\n"
				"Ks 0.000000 0.000000 0.000000\n",
				m->name) != 0) {
			return -1;
		}
		if (texture_path[0]) {
			if (s_textbufAppendf(&scene->mtl, "map_Kd %s\n",
					mtl_texture_path) != 0) {
				return -1;
			}
		}
		if (s_textbufAppend(&scene->mtl, "\n") != 0) return -1;
		if (s_textbufAppendf(&scene->materials_tsv,
				"%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%u\t%s\t%u\t%u\t%d\n",
				m->name, m->texnum, m->texnum2, m->subcmd,
				m->smode, m->tmode, m->offset, m->shifts,
				m->shiftt, m->min, m->flag, (unsigned)m->tri_count,
				texture_path, (unsigned)width, (unsigned)height, decoded) != 0) {
			return -1;
		}
	}

	for (u32 i = 0; i < scene->texture_count; i++) {
		const pdscenario_bgtexture_t *tex = &scene->textures[i];
		if (tex->used_by_geometry) continue;
		const char *texture_path = tex->decoded ? tex->path : "";
		if (s_textbufAppendf(&scene->materials_tsv,
				"texture_inventory_%04x\t%d\t-1\t-1\t0\t0\t0\t0\t0\t0\t0\t0\t%s\t%u\t%u\t%d\n",
				(unsigned)tex->texnum, (int)tex->texnum, texture_path,
				(unsigned)tex->width, (unsigned)tex->height,
				tex->decoded) != 0) {
			return -1;
		}
	}

	return 0;
}

typedef struct {
	u32 pos_view;
	u32 uv_view;
	u32 pos_accessor;
	u32 uv_accessor;
	f32 min_x, min_y, min_z;
	f32 max_x, max_y, max_z;
} pdscenario_gltf_material_t;

static s32 s_binbufAppendF32(pdscenario_binbuf_t *b, f32 value)
{
	u32 raw;
	memcpy(&raw, &value, sizeof(raw));
	return s_binbufAppendLe32(b, raw);
}

static s32 s_bgMaterialComputeBounds(const pdscenario_bgmaterial_t *m,
                                     pdscenario_gltf_material_t *out)
{
	if (!m || !out || m->vertex_count == 0) return -1;
	out->min_x = out->max_x = m->vertices[0].x;
	out->min_y = out->max_y = m->vertices[0].y;
	out->min_z = out->max_z = m->vertices[0].z;
	for (u32 i = 1; i < m->vertex_count; i++) {
		const pdscenario_visual_vertex_t *v = &m->vertices[i];
		if (v->x < out->min_x) out->min_x = v->x;
		if (v->y < out->min_y) out->min_y = v->y;
		if (v->z < out->min_z) out->min_z = v->z;
		if (v->x > out->max_x) out->max_x = v->x;
		if (v->y > out->max_y) out->max_y = v->y;
		if (v->z > out->max_z) out->max_z = v->z;
	}
	return 0;
}

static s32 s_bgSceneDecodedTextureIndex(const pdscenario_bgscene_t *scene,
                                        s32 texnum)
{
	if (!scene || texnum < 0) return -1;
	s32 idx = 0;
	for (u32 i = 0; i < scene->texture_count; i++) {
		const pdscenario_bgtexture_t *tex = &scene->textures[i];
		if (!tex->decoded || !tex->png || tex->png_size == 0) continue;
		if ((s32)tex->texnum == texnum) return idx;
		idx++;
	}
	return -1;
}

static u32 s_bgSceneDecodedTextureCount(const pdscenario_bgscene_t *scene)
{
	u32 count = 0;
	if (!scene) return 0;
	for (u32 i = 0; i < scene->texture_count; i++) {
		const pdscenario_bgtexture_t *tex = &scene->textures[i];
		if (tex->decoded && tex->png && tex->png_size > 0) count++;
	}
	return count;
}

static s32 s_bgSceneBuildSceneGlb(pdscenario_bgscene_t *scene)
{
	if (!scene || scene->tri_count == 0 || scene->material_count == 0) return -1;

	pdscenario_gltf_material_t *mr =
		(pdscenario_gltf_material_t *)calloc(scene->material_count, sizeof(*mr));
	u32 *texture_views =
		(u32 *)calloc(scene->texture_count ? scene->texture_count : 1u,
			sizeof(*texture_views));
	if (!mr || !texture_views) {
		free(mr);
		free(texture_views);
		return -1;
	}
	for (u32 i = 0; i < scene->material_count; i++) {
		mr[i].pos_view = mr[i].uv_view = 0xffffffffu;
		mr[i].pos_accessor = mr[i].uv_accessor = 0xffffffffu;
	}
	for (u32 i = 0; i < scene->texture_count; i++) {
		texture_views[i] = 0xffffffffu;
	}

	pdscenario_binbuf_t bin = { 0 };
	u32 view_count = 0;
	u32 accessor_count = 0;

	for (u32 i = 0; i < scene->material_count; i++) {
		const pdscenario_bgmaterial_t *m = &scene->materials[i];
		if (m->vertex_count == 0) continue;
		if (s_bgMaterialComputeBounds(m, &mr[i]) != 0) goto fail;
		if (s_binbufPad4(&bin) != 0) goto fail;
		mr[i].pos_view = view_count++;
		mr[i].pos_accessor = accessor_count++;
		for (u32 v = 0; v < m->vertex_count; v++) {
			if (s_binbufAppendF32(&bin, m->vertices[v].x) != 0 ||
			    s_binbufAppendF32(&bin, m->vertices[v].y) != 0 ||
			    s_binbufAppendF32(&bin, m->vertices[v].z) != 0) goto fail;
		}
		if (s_binbufPad4(&bin) != 0) goto fail;
		mr[i].uv_view = view_count++;
		mr[i].uv_accessor = accessor_count++;
		for (u32 v = 0; v < m->vertex_count; v++) {
			if (s_binbufAppendF32(&bin, m->vertices[v].u) != 0 ||
			    s_binbufAppendF32(&bin, m->vertices[v].v) != 0) goto fail;
		}
	}

	for (u32 i = 0; i < scene->texture_count; i++) {
		const pdscenario_bgtexture_t *tex = &scene->textures[i];
		if (!tex->decoded || !tex->png || tex->png_size == 0) continue;
		if (s_binbufPad4(&bin) != 0) goto fail;
		texture_views[i] = view_count++;
		if (s_binbufAppend(&bin, tex->png, tex->png_size) != 0) goto fail;
	}
	if (s_binbufPad4(&bin) != 0) goto fail;

	pdscenario_textbuf_t json = { 0 };
	if (s_textbufAppend(&json,
			"{\"asset\":{\"version\":\"2.0\","
			"\"generator\":\"Perfect Dark 2 PDSCENARIO scene.glb exporter "
			PDSCENARIO_BG_VISUAL_EXPORT_VERSION "\"},"
			"\"scene\":0,"
			"\"scenes\":[{\"nodes\":[0]}],"
			"\"nodes\":[{\"name\":\"scenario_visual_scene\",\"mesh\":0}],"
			"\"meshes\":[{\"name\":\"scene\",\"primitives\":[") != 0) goto fail_json;

	s32 first = 1;
	for (u32 i = 0; i < scene->material_count; i++) {
		if (scene->materials[i].vertex_count == 0) continue;
		if (!first && s_textbufAppend(&json, ",") != 0) goto fail_json;
		first = 0;
		if (s_textbufAppendf(&json,
				"{\"attributes\":{\"POSITION\":%u,\"TEXCOORD_0\":%u},"
				"\"material\":%u,\"mode\":4}",
				(unsigned)mr[i].pos_accessor,
				(unsigned)mr[i].uv_accessor,
				(unsigned)i) != 0) goto fail_json;
	}
	if (s_textbufAppend(&json, "]}],\"materials\":[") != 0) goto fail_json;

	for (u32 i = 0; i < scene->material_count; i++) {
		const pdscenario_bgmaterial_t *m = &scene->materials[i];
		s32 texture_index = s_bgSceneDecodedTextureIndex(scene, m->texnum);
		if (i && s_textbufAppend(&json, ",") != 0) goto fail_json;
		if (texture_index >= 0) {
			if (s_textbufAppendf(&json,
					"{\"name\":\"%s\","
					"\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":%d},"
					"\"metallicFactor\":0.0,\"roughnessFactor\":1.0}}",
					m->name, texture_index) != 0) goto fail_json;
		} else {
			if (s_textbufAppendf(&json,
					"{\"name\":\"%s\","
					"\"pbrMetallicRoughness\":{\"baseColorFactor\":[1.0,1.0,1.0,1.0],"
					"\"metallicFactor\":0.0,\"roughnessFactor\":1.0}}",
					m->name) != 0) goto fail_json;
		}
	}

	u32 decoded_texture_count = s_bgSceneDecodedTextureCount(scene);
	if (s_textbufAppend(&json, "],\"textures\":[") != 0) goto fail_json;
	for (u32 i = 0, idx = 0; i < scene->texture_count; i++) {
		const pdscenario_bgtexture_t *tex = &scene->textures[i];
		if (!tex->decoded || !tex->png || tex->png_size == 0) continue;
		if (idx && s_textbufAppend(&json, ",") != 0) goto fail_json;
		if (s_textbufAppendf(&json, "{\"source\":%u}", (unsigned)idx) != 0) {
			goto fail_json;
		}
		idx++;
	}
	if (s_textbufAppend(&json, "],\"images\":[") != 0) goto fail_json;
	for (u32 i = 0, idx = 0; i < scene->texture_count; i++) {
		const pdscenario_bgtexture_t *tex = &scene->textures[i];
		if (!tex->decoded || !tex->png || tex->png_size == 0) continue;
		if (idx && s_textbufAppend(&json, ",") != 0) goto fail_json;
		if (s_textbufAppendf(&json,
				"{\"name\":\"tex_%04x\",\"mimeType\":\"image/png\","
				"\"bufferView\":%u}",
				(unsigned)tex->texnum, (unsigned)texture_views[i]) != 0) {
			goto fail_json;
		}
		idx++;
	}

	if (s_textbufAppendf(&json,
			"],\"buffers\":[{\"byteLength\":%u}],\"bufferViews\":[",
			(unsigned)bin.len) != 0) goto fail_json;

	u32 emitted_view = 0;
	u32 offset = 0;
	for (u32 i = 0; i < scene->material_count; i++) {
		const pdscenario_bgmaterial_t *m = &scene->materials[i];
		if (m->vertex_count == 0) continue;
		u32 pos_len = m->vertex_count * 12u;
		u32 uv_len = m->vertex_count * 8u;
		offset = (offset + 3u) & ~3u;
		if (emitted_view++ && s_textbufAppend(&json, ",") != 0) goto fail_json;
		if (s_textbufAppendf(&json,
				"{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u,\"target\":34962}",
				(unsigned)offset, (unsigned)pos_len) != 0) goto fail_json;
		offset += pos_len;
		offset = (offset + 3u) & ~3u;
		if (s_textbufAppend(&json, ",") != 0) goto fail_json;
		emitted_view++;
		if (s_textbufAppendf(&json,
				"{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u,\"target\":34962}",
				(unsigned)offset, (unsigned)uv_len) != 0) goto fail_json;
		offset += uv_len;
	}
	for (u32 i = 0; i < scene->texture_count; i++) {
		const pdscenario_bgtexture_t *tex = &scene->textures[i];
		if (!tex->decoded || !tex->png || tex->png_size == 0) continue;
		offset = (offset + 3u) & ~3u;
		if (emitted_view++ && s_textbufAppend(&json, ",") != 0) goto fail_json;
		if (s_textbufAppendf(&json,
				"{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}",
				(unsigned)offset, (unsigned)tex->png_size) != 0) goto fail_json;
		offset += tex->png_size;
	}

	if (s_textbufAppend(&json, "],\"accessors\":[") != 0) goto fail_json;
	u32 emitted_accessor = 0;
	for (u32 i = 0; i < scene->material_count; i++) {
		const pdscenario_bgmaterial_t *m = &scene->materials[i];
		if (m->vertex_count == 0) continue;
		if (emitted_accessor++ && s_textbufAppend(&json, ",") != 0) goto fail_json;
		if (s_textbufAppendf(&json,
				"{\"bufferView\":%u,\"componentType\":5126,\"count\":%u,"
				"\"type\":\"VEC3\",\"min\":[%.6f,%.6f,%.6f],"
				"\"max\":[%.6f,%.6f,%.6f]}",
				(unsigned)mr[i].pos_view, (unsigned)m->vertex_count,
				(double)mr[i].min_x, (double)mr[i].min_y, (double)mr[i].min_z,
				(double)mr[i].max_x, (double)mr[i].max_y, (double)mr[i].max_z) != 0) {
			goto fail_json;
		}
		if (s_textbufAppend(&json, ",") != 0) goto fail_json;
		emitted_accessor++;
		if (s_textbufAppendf(&json,
				"{\"bufferView\":%u,\"componentType\":5126,\"count\":%u,"
				"\"type\":\"VEC2\"}",
				(unsigned)mr[i].uv_view, (unsigned)m->vertex_count) != 0) {
			goto fail_json;
		}
	}
	if (s_textbufAppend(&json, "]}") != 0) goto fail_json;

	u32 json_padded = (json.len + 3u) & ~3u;
	u32 bin_padded = (bin.len + 3u) & ~3u;
	u32 total_len = 12u + 8u + json_padded + 8u + bin_padded;
	pdscenario_binbuf_t glb = { 0 };
	if (s_binbufAppend(&glb, "glTF", 4) != 0 ||
	    s_binbufAppendLe32(&glb, 2u) != 0 ||
	    s_binbufAppendLe32(&glb, total_len) != 0 ||
	    s_binbufAppendLe32(&glb, json_padded) != 0 ||
	    s_binbufAppendLe32(&glb, 0x4e4f534au) != 0 ||
	    s_binbufAppend(&glb, json.data, json.len) != 0) goto fail_glb;
	while (glb.len < 12u + 8u + json_padded) {
		u8 space = 0x20;
		if (s_binbufAppend(&glb, &space, 1) != 0) goto fail_glb;
	}
	if (s_binbufAppendLe32(&glb, bin_padded) != 0 ||
	    s_binbufAppendLe32(&glb, 0x004e4942u) != 0 ||
	    s_binbufAppend(&glb, bin.data, bin.len) != 0) goto fail_glb;
	while (glb.len < total_len) {
		u8 zero = 0;
		if (s_binbufAppend(&glb, &zero, 1) != 0) goto fail_glb;
	}

	scene->scene_glb = glb.data;
	scene->scene_glb_size = glb.len;
	glb.data = NULL;
	(void)decoded_texture_count;
	s_textbufFree(&json);
	s_binbufFree(&bin);
	free(mr);
	free(texture_views);
	return 0;

fail_glb:
	s_binbufFree(&glb);
fail_json:
	s_textbufFree(&json);
fail:
	s_binbufFree(&bin);
	free(mr);
	free(texture_views);
	return -1;
}

static s32 s_promoteBgRoomPointers(u8 *room, u32 size, u32 room_ofs)
{
	if (!room || size < sizeof(struct roomgfxdata)) return -1;
	struct roomgfxdata *gfx = (struct roomgfxdata *)room;
	gfx->vertices = (Vtx *)s_promoteRoomPtr(room, size, (uintptr_t)gfx->vertices, room_ofs);
	gfx->colours = (Col *)s_promoteRoomPtr(room, size, (uintptr_t)gfx->colours, room_ofs);
	gfx->opablocks = (struct roomblock *)s_promoteRoomPtr(room, size, (uintptr_t)gfx->opablocks, room_ofs);
	gfx->xlublocks = (struct roomblock *)s_promoteRoomPtr(room, size, (uintptr_t)gfx->xlublocks, room_ofs);
	if (!gfx->vertices || !gfx->colours) return -1;

	uintptr_t end = (uintptr_t)gfx->vertices;
	struct roomblock *block = gfx->blocks;
	while (s_rangeInBuffer(room, size, block, (u32)sizeof(*block)) &&
	       (uintptr_t)(block + 1) <= end) {
		if (block->next) {
			block->next = (struct roomblock *)s_promoteRoomPtr(
				room, size, (uintptr_t)block->next, room_ofs);
		}
		if (block->type == ROOMBLOCKTYPE_LEAF) {
			if (block->gdl) {
				block->gdl = (Gfx *)s_promoteRoomPtr(
					room, size, (uintptr_t)block->gdl, room_ofs);
			}
			if (block->vertices) {
				block->vertices = (Vtx *)s_promoteRoomPtr(
					room, size, (uintptr_t)block->vertices, room_ofs);
			}
			if (block->colours) {
				block->colours = (Col *)s_promoteRoomPtr(
					room, size, (uintptr_t)block->colours, room_ofs);
			}
		} else if (block->type == ROOMBLOCKTYPE_PARENT) {
			if (block->child) {
				block->child = (struct roomblock *)s_promoteRoomPtr(
					room, size, (uintptr_t)block->child, room_ofs);
			}
			if (block->unk0c) {
				block->unk0c = (struct coord *)s_promoteRoomPtr(
					room, size, (uintptr_t)block->unk0c, room_ofs);
				if (block->unk0c && (uintptr_t)block->unk0c < end) {
					end = (uintptr_t)block->unk0c;
				}
			}
		} else {
			break;
		}
		block++;
	}
	if ((uintptr_t)gfx->colours > (uintptr_t)gfx->vertices) {
		gfx->numvertices = (s16)(((uintptr_t)gfx->colours -
			(uintptr_t)gfx->vertices) / sizeof(Vtx));
	}
	return 0;
}

static s32 s_bgGdlCommand(const Gfx *cmd)
{
	return (s32)((cmd->words.w0 >> 24) & 0xffu);
}

static s32 s_exportBgTriByIndices(pdscenario_bgscene_t *scene,
                                  s32 material_index,
                                  const Vtx loaded[16],
                                  const u8 valid[16],
                                  u32 a, u32 b, u32 c,
                                  const struct coord *room_pos)
{
	if (a >= 16u || b >= 16u || c >= 16u) return 0;
	if (!valid[a] || !valid[b] || !valid[c]) return 0;
	if (a == b && b == c) return 0;
	return s_bgSceneAddTri(scene, material_index,
		&loaded[a], &loaded[b], &loaded[c], room_pos);
}

static s32 s_exportBgGdl(pdscenario_bgscene_t *scene, Gfx *gdl,
                         const u8 *room_base, u32 room_size,
                         const Vtx *vertices, u32 vertex_span,
                         const struct coord *room_pos)
{
	if (!gdl || !vertices || !room_pos) return 0;
	if (!s_rangeInBuffer(room_base, room_size, gdl, (u32)sizeof(Gfx))) return 0;
	Vtx loaded[16];
	u8 valid[16];
	memset(loaded, 0, sizeof(loaded));
	memset(valid, 0, sizeof(valid));
	s32 material_index = 0;
	u32 max_cmds = room_size / (u32)sizeof(Gfx);
	if (max_cmds > 20000u) max_cmds = 20000u;

	for (u32 i = 0; i < max_cmds; i++) {
		Gfx *cmd = &gdl[i];
		if (!s_rangeInBuffer(room_base, room_size, cmd, (u32)sizeof(*cmd))) {
			break;
		}
		s32 op = s_bgGdlCommand(cmd);
		if (op == (u8)G_ENDDL) break;
		if (op == G_NOOP) {
			u32 w0 = (u32)cmd->words.w0;
			u32 w1 = (u32)cmd->words.w1;
			s32 texnum = (s32)(w1 & 0xfffu);
			s32 texnum2 = -1;
			s32 subcmd = (s32)cmd->unkc0.subcmd;
			if (subcmd == 1) texnum2 = (s32)((w1 >> 12) & 0xfffu);
			if (texnum >= 0 && texnum < NUM_TEXTURES) {
				s32 next_mat = s_bgSceneEnsureMaterial(scene, texnum,
					(texnum2 >= 0 && texnum2 < NUM_TEXTURES) ? texnum2 : -1,
					subcmd, w0, w1);
				if (next_mat >= 0) material_index = next_mat;
			}
		} else if (op == G_VTX) {
			u32 w0 = (u32)cmd->words.w0;
			u32 count = (w0 & 0xffffu) / (u32)sizeof(Vtx);
			u32 dest = (w0 >> 16) & 0x0fu;
			u32 off = (u32)(UNSEGADDR(cmd->words.w1) & 0x00ffffffu);
			if (count == 0 || count > 16u || dest + count > 16u) continue;
			if (off > vertex_span ||
			    count > (vertex_span - off) / (u32)sizeof(Vtx)) {
				continue;
			}
			const Vtx *src = (const Vtx *)((const u8 *)vertices + off);
			for (u32 v = 0; v < count; v++) {
				loaded[dest + v] = src[v];
				valid[dest + v] = 1;
			}
		} else if (op == (u8)G_TRI1) {
			u32 w1 = (u32)cmd->words.w1;
			u32 a = ((w1 >> 16) & 0xffu) / 10u;
			u32 b = ((w1 >> 8) & 0xffu) / 10u;
			u32 c = (w1 & 0xffu) / 10u;
			if (s_exportBgTriByIndices(scene, material_index, loaded,
					valid, a, b, c, room_pos) != 0) {
				return -1;
			}
		} else if (op == (u8)G_TRI4) {
			u32 w0 = (u32)cmd->words.w0;
			u32 w1 = (u32)cmd->words.w1;
			u32 x1 = (w1 >> 0) & 0x0fu, y1 = (w1 >> 4) & 0x0fu, z1 = (w0 >> 0) & 0x0fu;
			u32 x2 = (w1 >> 8) & 0x0fu, y2 = (w1 >> 12) & 0x0fu, z2 = (w0 >> 4) & 0x0fu;
			u32 x3 = (w1 >> 16) & 0x0fu, y3 = (w1 >> 20) & 0x0fu, z3 = (w0 >> 8) & 0x0fu;
			u32 x4 = (w1 >> 24) & 0x0fu, y4 = (w1 >> 28) & 0x0fu, z4 = (w0 >> 12) & 0x0fu;
			if (s_exportBgTriByIndices(scene, material_index, loaded,
					valid, x1, y1, z1, room_pos) != 0 ||
			    s_exportBgTriByIndices(scene, material_index, loaded,
					valid, x2, y2, z2, room_pos) != 0 ||
			    s_exportBgTriByIndices(scene, material_index, loaded,
					valid, x3, y3, z3, room_pos) != 0 ||
			    s_exportBgTriByIndices(scene, material_index, loaded,
					valid, x4, y4, z4, room_pos) != 0) {
				return -1;
			}
		}
	}
	scene->gdl_count++;
	return 0;
}

static s32 s_exportBgRoomBlocks(pdscenario_bgscene_t *scene,
                                struct roomgfxdata *gfx,
                                const u8 *room_base, u32 room_size,
                                const struct coord *room_pos)
{
	if (!scene || !gfx || !gfx->vertices || !gfx->colours) return 0;
	uintptr_t end = (uintptr_t)gfx->vertices;
	struct roomblock *block = gfx->blocks;
	while (s_rangeInBuffer(room_base, room_size, block, (u32)sizeof(*block)) &&
	       (uintptr_t)(block + 1) <= end) {
		if (block->type == ROOMBLOCKTYPE_LEAF && block->gdl && block->vertices) {
			if (s_rangeInBuffer(room_base, room_size, block->vertices, (u32)sizeof(Vtx))) {
				u32 vertex_span = 0;
				if (gfx->colours && (uintptr_t)gfx->colours > (uintptr_t)block->vertices) {
					vertex_span = (u32)((uintptr_t)gfx->colours -
						(uintptr_t)block->vertices);
				}
				if (vertex_span > 0) {
					if (s_exportBgGdl(scene, block->gdl, room_base,
							room_size, block->vertices, vertex_span,
							room_pos) != 0) {
						return -1;
					}
				}
			}
		} else if (block->type == ROOMBLOCKTYPE_PARENT && block->unk0c &&
		           (uintptr_t)block->unk0c < end) {
			end = (uintptr_t)block->unk0c;
		} else if (block->type != ROOMBLOCKTYPE_LEAF &&
		           block->type != ROOMBLOCKTYPE_PARENT) {
			break;
		}
		block++;
	}
	return 0;
}

static s32 s_loadBgPrimary(const u8 *bg, u32 bg_size, u8 **out_primary,
                           u32 *out_primary_size, struct bgroom **out_rooms,
                           u32 *out_room_count)
{
	if (out_primary) *out_primary = NULL;
	if (out_primary_size) *out_primary_size = 0;
	if (out_rooms) *out_rooms = NULL;
	if (out_room_count) *out_room_count = 0;
	if (!bg || bg_size < 12) return -1;
	u32 primary_infsize = s_readBe32(bg + 0);
	u32 section1_cmpsize = s_readBe32(bg + 4);
	u32 primary_cmpsize = s_readBe32(bg + 8);
	if (!primary_infsize || !primary_cmpsize ||
	    12u + primary_cmpsize > bg_size ||
	    section1_cmpsize < primary_cmpsize ||
	    12u + section1_cmpsize > bg_size) {
		return -1;
	}
	u32 cap = ALIGN16(primary_infsize + BG_INFLATE_SCRATCH_LARGE +
		BG_INFLATE_SCRATCH_HEADER);
	if (cap < primary_infsize) return -1;
	u8 *primary = (u8 *)sysMemZeroAlloc(cap);
	if (!primary) return -1;
	if (rzipIs1173((u8 *)(bg + 12))) {
		u8 scratch[5 * 1024];
		s32 inflated = rzipInflate((u8 *)(bg + 12), primary, scratch);
		if (inflated <= 0) {
			sysMemFree(primary);
			return -1;
		}
	} else {
		memcpy(primary, bg + 12, primary_cmpsize);
	}
	preprocessBgSection1(primary, cap, 0x0f000000);

	uintptr_t *hdr = (uintptr_t *)primary;
	struct bgroom *rooms = (struct bgroom *)s_promoteRoomPtr(primary, cap,
		hdr[1], 0x0f000000);
	if (!rooms) {
		sysMemFree(primary);
		return -1;
	}
	u32 room_count = 0;
	for (u32 i = 1; i < 4096u; i++) {
		if (!s_rangeInBuffer(primary, cap, &rooms[i], (u32)sizeof(rooms[i]))) break;
		if (rooms[i].unk00 == 0) break;
		room_count++;
	}
	if (room_count == 0) {
		sysMemFree(primary);
		return -1;
	}
	*out_primary = primary;
	*out_primary_size = cap;
	*out_rooms = rooms;
	*out_room_count = room_count;
	return 0;
}

static s32 s_exportBgSection2Textures(const u8 *bg, u32 bg_size,
                                      pdscenario_bgscene_t *scene)
{
	if (!bg || bg_size < 16 || !scene) return 0;
	u32 section1_cmpsize = s_readBe32(bg + 4);
	u32 section2start = section1_cmpsize + 12u;
	if (section2start + 4u > bg_size) return 0;
	u16 inf_masked = s_readBe16(bg + section2start);
	u16 cmpsize = s_readBe16(bg + section2start + 2u);
	u32 count = (inf_masked & 0x7fffu) >> 1;
	u32 infsize = s_alignTo((inf_masked & 0x7fffu), 16);
	if (count == 0 || cmpsize == 0 || section2start + 4u + cmpsize > bg_size) return 0;
	u8 *section2 = (u8 *)sysMemZeroAlloc(infsize + 16u);
	if (!section2) return -1;
	if (rzipIs1173((u8 *)(bg + section2start + 4u))) {
		u8 scratch[5 * 1024];
		s32 inflated = rzipInflate((u8 *)(bg + section2start + 4u),
			section2, scratch);
		if (inflated <= 0) {
			sysMemFree(section2);
			return -1;
		}
	} else {
		memcpy(section2, bg + section2start + 4u, cmpsize);
	}
	preprocessBgSection2(section2, count);
	u16 *ids = (u16 *)section2;
	for (u32 i = 0; i < count; i++) {
		if (ids[i] < NUM_TEXTURES) {
			if (s_bgSceneEnsureTexture(scene, ids[i], 0) < 0) {
				sysMemFree(section2);
				return -1;
			}
		}
	}
	sysMemFree(section2);
	return 0;
}

static s32 s_buildBgVisualExports(const u8 *bg, u32 bg_size,
                                  pdscenario_bgscene_t *scene)
{
	if (!bg || !bg_size || !scene) return -1;
	if (s_bgSceneInit(scene) != 0) return -1;

	u8 *primary = NULL;
	u32 primary_size = 0;
	struct bgroom *rooms = NULL;
	u32 room_count = 0;
	if (s_loadBgPrimary(bg, bg_size, &primary, &primary_size,
			&rooms, &room_count) != 0) {
		return -1;
	}

	u32 primary_infsize = s_readBe32(bg + 0);
	u32 primary_cmpsize = s_readBe32(bg + 8);
	s32 file_bias = (s32)primary_infsize - (s32)primary_cmpsize - 12;

	for (u32 roomnum = 1; roomnum < room_count; roomnum++) {
		if (!rooms[roomnum].unk00 || !rooms[roomnum + 1].unk00 ||
		    rooms[roomnum + 1].unk00 <= rooms[roomnum].unk00) {
			continue;
		}
		u32 room_ofs = (u32)rooms[roomnum].unk00;
		u32 next_ofs = (u32)rooms[roomnum + 1].unk00;
		u32 room_span = next_ofs - room_ofs;
		if (room_span > 0x1ffff000u) continue;
		u32 readlen = ALIGN16(room_span);
		s32 fileoffset = (s32)(room_ofs - 0x0f000000u) - file_bias;
		if (fileoffset < 0 || (u32)fileoffset >= bg_size ||
		    readlen > bg_size - (u32)fileoffset) {
			continue;
		}
		u32 room_cap = s_alignTo(room_span * 8u + 4096u, 16);
		if (room_cap < 65536u) room_cap = 65536u;
		u8 *room = (u8 *)sysMemZeroAlloc(room_cap);
		if (!room) {
			sysMemFree(primary);
			return -1;
		}
		const u8 *src = bg + (u32)fileoffset;
		s32 inflatedlen = 0;
		if (rzipIs1173((u8 *)src)) {
			u8 scratch[5 * 1024];
			inflatedlen = rzipInflate((u8 *)src, room, scratch);
		} else {
			u32 copylen = room_span;
			if (copylen > room_cap) copylen = room_cap;
			memcpy(room, src, copylen);
			inflatedlen = (s32)copylen;
		}
		if (inflatedlen <= 0 || (u32)inflatedlen > room_cap / 8u) {
			sysMemFree(room);
			continue;
		}
		u32 new_len = preprocessBgRoom(room, (u32)inflatedlen, room_ofs);
		if (new_len == 0 || new_len > room_cap ||
		    s_promoteBgRoomPointers(room, new_len, room_ofs) != 0) {
			sysMemFree(room);
			continue;
		}
		if (s_textbufAppendf(&scene->obj, "g room_%u\n",
				(unsigned)roomnum) != 0) {
			sysMemFree(room);
			sysMemFree(primary);
			return -1;
		}
		struct roomgfxdata *gfx = (struct roomgfxdata *)room;
		if (s_exportBgRoomBlocks(scene, gfx, room, new_len,
				&rooms[roomnum].pos) != 0) {
			sysMemFree(room);
			sysMemFree(primary);
			return -1;
		}
		scene->room_count++;
		sysMemFree(room);
	}

	if (s_exportBgSection2Textures(bg, bg_size, scene) != 0) {
		sysMemFree(primary);
		return -1;
	}
	s_bgSceneDecodeTextures(scene);
	if (s_bgSceneFinalizeMaterials(scene) != 0) {
		sysMemFree(primary);
		return -1;
	}
	if (s_bgSceneBuildSceneGlb(scene) != 0) {
		sysMemFree(primary);
		return -1;
	}
	sysMemFree(primary);
	return scene->tri_count > 0 ? 0 : -1;
}

static s32 s_existingZipArchive(const char *relpath)
{
	u32 size = 0;
	void *bytes = fsFileLoad(relpath, &size);
	if (!bytes) return 0;
	s32 is_zip = size >= 2
		&& ((const u8 *)bytes)[0] == 'P'
		&& ((const u8 *)bytes)[1] == 'K';
	sysMemFree(bytes);
	return is_zip;
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

static void s_scenarioArchiveRelPath(const char *out_dir, const char *scenario_id,
                                     char *out, size_t out_size)
{
	if (!out || out_size == 0) return;
	out[0] = '\0';
	if (!out_dir || !scenario_id || !scenario_id[0]) return;
	char filename[128];
	s_idToFilename(scenario_id, filename, sizeof(filename));
	snprintf(out, out_size, "%s/%s.pdscenario", out_dir, filename);
}

static s32 s_copyArchiveEntriesWithPrefix(asset_archive_writer_t *writer,
                                          const char *src_rel,
                                          const char *prefix)
{
	if (!writer || !src_rel || !prefix || !prefix[0]) return -1;
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(src_rel, full_buf, sizeof(full_buf));
	if (!full || !full[0]) return -1;

	mod_archive_t *arc = modArchiveOpen(full);
	if (!arc) return -1;
	s32 count = modArchiveGetEntryCount(arc);
	for (s32 i = 0; i < count; i++) {
		const char *name = modArchiveGetEntryName(arc, i);
		if (!name || !name[0]) continue;
		char dst_name[FS_MAXPATH];
		int n = snprintf(dst_name, sizeof(dst_name), "%s/%s", prefix, name);
		if (n <= 0 || (size_t)n >= sizeof(dst_name)) {
			modArchiveClose(arc);
			return -1;
		}
		u32 size = 0;
		void *bytes = modArchiveExtractAlloc(arc, i, &size);
		if (!bytes) {
			modArchiveClose(arc);
			return -1;
		}
		s32 add = assetArchiveWriterAddPublicMem(writer, dst_name,
			bytes, size, "scenario");
		free(bytes);
		if (add != MODARCHIVE_OK) {
			modArchiveClose(arc);
			return -1;
		}
	}
	modArchiveClose(arc);
	return 0;
}

/* Emit one .pdarena ZIP-openable archive. Returns 1 written, 0 skipped,
 * -1 failed. */
static s32 s_emitOnePdarena(const arena_authored_record_t *a, s32 arena_index,
                             const char *out_dir, const char *scenarios_dir,
                             s32 force_rewrite)
{
	const char *catalog_id = a->catalog_id;
	if (!catalog_id || !catalog_id[0]) return 0;

	char filename[128];
	s_idToFilename(catalog_id, filename, sizeof(filename));

	char relpath[FS_MAXPATH];
	snprintf(relpath, sizeof(relpath), "%s/%s.pdarena", out_dir, filename);

	char scenario_id[96];
	s32 stage_idx = stageGetIndex(a->stagenum);
	if (stage_idx >= 0) {
		s_scenarioCatalogIdFromSlug(a->slug, scenario_id, sizeof(scenario_id));
	} else {
		scenario_id[0] = '\0';
	}

	char scenario_rel[FS_MAXPATH];
	s_scenarioArchiveRelPath(scenarios_dir, scenario_id,
		scenario_rel, sizeof(scenario_rel));

	if (!force_rewrite && fsFileSize(relpath) > 0 && s_existingZipArchive(relpath)) {
		if (s_existingArchiveHasEntry(relpath, "arena.ini") &&
		    s_existingArchiveHasEntry(relpath, "_meta/manifest.json") &&
		    (!scenario_id[0] ||
		    (s_existingArchiveHasEntry(relpath, "scenario/scenario.ini") &&
		     s_existingArchiveHasEntry(relpath, "scenario/scene.glb") &&
		     s_existingArchiveHasEntry(relpath, "scenario/rooms.obj") &&
		     s_existingArchiveHasEntry(relpath, "scenario/spawns.tsv") &&
		     s_existingArchiveHasEntry(relpath, "scenario/volumes.tsv") &&
		     s_existingArchiveHasEntry(relpath, "scenario/objects.tsv") &&
		     s_existingArchiveHasEntry(relpath, "scenario/objectives.tsv") &&
		     s_existingArchiveHasEntry(relpath, "scenario/navigation.ini") &&
		     s_existingArchiveHasEntry(relpath, "scenario/level.graph.json") &&
		     s_existingArchiveHasEntry(relpath, "scenario/_meta/generated-collision.json") &&
		     s_existingArchiveHasEntry(relpath, "scenario/_meta/generated-navmesh.json") &&
		     s_existingArchiveHasEntry(relpath, "scenario/visual/export_version.txt") &&
		     s_existingArchiveHasEntry(relpath, "scenario/visual/scene.obj") &&
		     s_existingArchiveHasEntry(relpath, "scenario/visual/scene.mtl") &&
		     s_existingArchiveHasEntry(relpath, "scenario/visual/materials.tsv")))) {
			return 0;
		}
	}

	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) {
		sysLoudFailf("EXTRACT.PDARENA",
			"fsFullPath failed for \"%s\"", relpath);
		return -1;
	}

	const char *load_mode_str = loaderEnumNameForArenaLoadMode(a->load_mode);

	char manifest_buf[1536];
	int manifest_len = snprintf(manifest_buf, sizeof(manifest_buf),
		"{\n"
		"  \"pd_kind\": \"arena\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"arena_index\": %d,\n"
		"  \"slug\": \"%s\",\n"
		"  \"category\": \"%s\",\n"
		"  \"stagenum\": %d,\n"
		"  \"requirefeature\": %u,\n"
		"  \"name_langid\": %d,\n",
		catalog_id, arena_index, a->slug, a->category, (s32)a->stagenum,
		(unsigned)a->requirefeature, a->name_langid);
	if (load_mode_str) {
		manifest_len += snprintf(manifest_buf + manifest_len,
			sizeof(manifest_buf) - manifest_len,
			"  \"load_mode\": \"%s\",\n", load_mode_str);
	} else {
		manifest_len += snprintf(manifest_buf + manifest_len,
			sizeof(manifest_buf) - manifest_len,
			"  \"load_mode\": %u,\n", (unsigned)a->load_mode);
	}
	manifest_len += snprintf(manifest_buf + manifest_len,
		sizeof(manifest_buf) - manifest_len,
		scenario_id[0]
			? "  \"scenario\": \"%s\",\n"
			  "  \"scenario_root\": \"scenario\"\n}\n"
			: "  \"scenario\": null\n}\n",
		scenario_id);

	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		sysLoudFailf("EXTRACT.PDARENA",
			"manifest snprintf truncated for \"%s\"", catalog_id);
		return -1;
	}

	char ini_buf[1024];
	int ini_len = snprintf(ini_buf, sizeof(ini_buf),
		"[arena]\n"
		"catalog_id = %s\n"
		"arena_index = %d\n"
		"slug = %s\n"
		"category = %s\n"
		"stagenum = %d\n"
		"requirefeature = %u\n"
		"name_langid = %d\n"
		"load_mode = %s\n",
		catalog_id, arena_index, a->slug, a->category, (s32)a->stagenum,
		(unsigned)a->requirefeature, a->name_langid,
		load_mode_str ? load_mode_str : "ARENA_LOADMODE_PLAYABLE");
	if (scenario_id[0]) {
		ini_len += snprintf(ini_buf + ini_len, sizeof(ini_buf) - ini_len,
			"scenario = %s\n"
			"scenario_root = scenario\n",
			scenario_id);
	}
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini_buf)) {
		sysLoudFailf("EXTRACT.PDARENA",
			"arena.ini snprintf truncated for \"%s\"", catalog_id);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(full);
	if (!aw) {
		sysLoudFailf("EXTRACT.PDARENA",
			"modArchiveBegin failed for \"%s\"", full);
		return -1;
	}
	asset_archive_writer_t asset_writer;
	if (assetArchiveWriterInit(&asset_writer, aw, "arena", catalog_id) !=
			MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDARENA",
			"assetArchiveWriterInit failed for \"%s\"", full);
		modArchiveAbort(aw);
		return -1;
	}
	assetArchiveWriterSetProvenance(&asset_writer, "romextract_pdarena",
		"arenadata_authored", arena_index, a->slug);

	if (assetArchiveWriterAddDescriptor(&asset_writer, "arena.ini",
			ini_buf, (u32)ini_len) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDARENA",
			"AddFileMem arena.ini failed for \"%s\"", full);
		modArchiveAbort(aw);
		return -1;
	}
	if (assetArchiveWriterAddManifestJson(&asset_writer,
			manifest_buf, (u32)manifest_len) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDARENA",
			"AddFileMem _meta/manifest.json failed for \"%s\"", full);
		modArchiveAbort(aw);
		return -1;
	}
	if (scenario_id[0]) {
		if (!scenario_rel[0] ||
		    s_copyArchiveEntriesWithPrefix(&asset_writer,
				scenario_rel, "scenario") != 0) {
			sysLoudFailf("EXTRACT.PDARENA",
				"scenario dependency copy failed for \"%s\" from \"%s\"",
				full, scenario_rel[0] ? scenario_rel : "(missing)");
			modArchiveAbort(aw);
			return -1;
		}
	}
	if (assetArchiveWriterFinishMetadata(&asset_writer) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDARENA",
			"assetArchiveWriterFinishMetadata failed for \"%s\"", full);
		modArchiveAbort(aw);
		return -1;
	}
	if (modArchiveFinish(aw) != 0) {
		sysLoudFailf("EXTRACT.PDARENA",
			"modArchiveFinish failed for \"%s\"", full);
		return -1;
	}
	return 1;
}

/* Emit one .pdscenario ZIP. Returns 1 written, 0 skipped, -1 failed. */
static s32 s_emitOnePdscenario(const arena_authored_record_t *a,
                                const char *out_dir, s32 force_rewrite)
{
	s32 stage_idx = stageGetIndex(a->stagenum);
	if (stage_idx < 0) {
		/* Arena's stagenum has no stagetable entry. Random meta arenas
		 * (STAGE_MP_RANDOM_MULTI / SOLO) hit this branch. The .pdarena's
		 * `scenario` field is null and no .pdscenario is emitted. */
		return 0;
	}

	const struct stagetableentry *st = stageGetEntry(stage_idx);
	if (!st) return 0;

	char scenario_id[96];
	s_scenarioCatalogIdFromSlug(a->slug, scenario_id, sizeof(scenario_id));

	char dst_rel[FS_MAXPATH];
	s_scenarioArchiveRelPath(out_dir, scenario_id, dst_rel, sizeof(dst_rel));

	if (!force_rewrite && fsFileSize(dst_rel) > 0 &&
	    s_existingArchiveHasEntry(dst_rel, "scenario.ini") &&
	    s_existingArchiveHasEntry(dst_rel, "_meta/manifest.json") &&
	    s_existingArchiveHasEntry(dst_rel, "scene.glb") &&
	    s_existingArchiveHasEntry(dst_rel, "rooms.obj") &&
	    s_existingArchiveHasEntry(dst_rel, "spawns.tsv") &&
	    s_existingArchiveHasEntry(dst_rel, "volumes.tsv") &&
	    s_existingArchiveHasEntry(dst_rel, "objects.tsv") &&
	    s_existingArchiveHasEntry(dst_rel, "objectives.tsv") &&
	    s_existingArchiveHasEntry(dst_rel, "navigation.ini") &&
	    s_existingArchiveHasEntry(dst_rel, "level.graph.json") &&
	    s_existingArchiveHasEntry(dst_rel, "_meta/generated-collision.json") &&
	    s_existingArchiveHasEntry(dst_rel, "_meta/generated-navmesh.json") &&
	    s_existingArchiveHasEntry(dst_rel, "visual/export_version.txt") &&
	    s_existingArchiveHasEntry(dst_rel, "visual/scene.obj") &&
	    s_existingArchiveHasEntry(dst_rel, "visual/scene.mtl") &&
	    s_existingArchiveHasEntry(dst_rel, "visual/materials.tsv")) return 0;

	char dst_full_buf[FS_MAXPATH + 1];
	const char *dst_full = fsFullPath(dst_rel, dst_full_buf, sizeof(dst_full_buf));
	if (!dst_full || !dst_full[0]) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"fsFullPath failed for \"%s\"", dst_rel);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(dst_full);
	if (!aw) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"modArchiveBegin failed for \"%s\"", dst_full);
		return -1;
	}
	asset_archive_writer_t asset_writer;
	if (assetArchiveWriterInit(&asset_writer, aw, "scenario",
			scenario_id) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"assetArchiveWriterInit failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		return -1;
	}
	assetArchiveWriterSetProvenance(&asset_writer, "romextract_pdarena",
		"stagetable", stage_idx, a->slug);

	pdscenario_textbuf_t rooms_obj = { 0 };
	pdscenario_textbuf_t tiles_tsv = { 0 };
	pdscenario_textbuf_t pads_tsv = { 0 };
	pdscenario_textbuf_t spawns_tsv = { 0 };
	pdscenario_textbuf_t volumes_tsv = { 0 };
	pdscenario_textbuf_t objects_tsv = { 0 };
	pdscenario_textbuf_t objectives_tsv = { 0 };
	pdscenario_textbuf_t navigation_ini = { 0 };
	pdscenario_textbuf_t level_graph_json = { 0 };
	pdscenario_textbuf_t collision_meta_json = { 0 };
	pdscenario_textbuf_t navmesh_meta_json = { 0 };
	pdscenario_visualmesh_t visual_mesh = { 0 };
	pdscenario_bgscene_t bg_scene = { 0 };

	const char *kind = s_kindFromCategory(a->category);
	u32 room_count = 0;
	u32 geo_count = 0;
	u32 tri_count = 0;
	u32 pad_count = 0;
	u32 object_count = 0;
	u32 objective_count = 0;
	s32 has_visual = 0;

	u8 *tiles_data = NULL;
	u32 tiles_size = 0;
	s32 has_tiles = s_loadStageFilePreprocessed(st->tilefileid, LOADTYPE_TILES,
		&tiles_data, &tiles_size);
	if (has_tiles > 0) {
		if (s_buildTilesExports(tiles_data, tiles_size, &rooms_obj,
				&visual_mesh, &tiles_tsv,
				&room_count, &geo_count, &tri_count) != 0) {
			sysLogPrintf(LOG_WARNING,
				"romextract pdscenario: tile conversion failed for \"%s\"",
				scenario_id);
			has_tiles = -1;
		}
		sysMemFree(tiles_data);
	}

	u8 *pads_data = NULL;
	u32 pads_size = 0;
	s32 has_pads = s_loadStageFilePreprocessed(st->padsfileid, LOADTYPE_PADS,
		&pads_data, &pads_size);
	if (has_pads > 0) {
		if (s_buildPadsTsv(pads_data, pads_size, &pads_tsv,
				&spawns_tsv, &volumes_tsv, &pad_count) != 0) {
			sysLogPrintf(LOG_WARNING,
				"romextract pdscenario: pads conversion failed for \"%s\"",
				scenario_id);
			has_pads = -1;
		}
		sysMemFree(pads_data);
	}

	u8 *setup_data = NULL;
	u32 setup_size = 0;
	s32 has_setup = s_loadStageFilePreprocessed(st->setupfileid,
		LOADTYPE_SETUP, &setup_data, &setup_size);
	if (has_setup > 0) {
		if (s_buildSetupTables(setup_data, setup_size, &objects_tsv,
				&objectives_tsv, &object_count,
				&objective_count) != 0) {
			sysLogPrintf(LOG_WARNING,
				"romextract pdscenario: setup table conversion failed for \"%s\"",
				scenario_id);
			has_setup = -1;
		}
		sysMemFree(setup_data);
	} else {
		if (s_textbufAppend(&objects_tsv,
				"record_id\tkind\tpad_ref\tmodel_catalog_id\tweapon_catalog_id\tsecondary_weapon_catalog_id\tbody_catalog_id\thead_catalog_id\tailist_ref\tflags\tflags2\tflags3\n") != 0 ||
		    s_textbufAppend(&objectives_tsv,
				"objective_id\tkind\ttext_token\tdifficulty_mask\tgraph_node\n") != 0) {
			has_setup = -1;
		}
	}

	u8 *bg_data = NULL;
	u32 bg_size = 0;
	s32 has_bg = s_loadStageFileRaw(st->bgfileid, &bg_data, &bg_size);
	if (has_bg > 0) {
		if (s_buildBgVisualExports(bg_data, bg_size, &bg_scene) == 0) {
			has_visual = 1;
		} else {
			sysLogPrintf(LOG_WARNING,
				"romextract pdscenario: BG visual display-list export failed for \"%s\"",
				scenario_id);
		}
		sysMemFree(bg_data);
	}

	if (has_visual <= 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdscenario: scene.glb source missing for \"%s\"",
			scenario_id);
		has_bg = -1;
	}

	if (s_buildScenarioSourceFiles(scenario_id, kind, room_count, tri_count,
			pad_count, object_count, objective_count, &navigation_ini,
			&level_graph_json, &collision_meta_json,
			&navmesh_meta_json) != 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdscenario: source metadata conversion failed for \"%s\"",
			scenario_id);
		has_bg = -1;
	}

	if (has_tiles < 0 || has_pads < 0 || has_setup < 0 || has_bg < 0) {
		s_pdscenarioScratchFree(&rooms_obj, &tiles_tsv, &pads_tsv,
			&spawns_tsv, &volumes_tsv, &objects_tsv, &objectives_tsv,
			&navigation_ini, &level_graph_json, &collision_meta_json,
			&navmesh_meta_json,
			&visual_mesh, &bg_scene);
		modArchiveAbort(aw);
		return -1;
	}

	if (has_tiles > 0) {
		if (assetArchiveWriterAddPublicMem(&asset_writer, "rooms.obj",
				rooms_obj.data, rooms_obj.len, "geometry") != MODARCHIVE_OK ||
		    assetArchiveWriterAddPublicMem(&asset_writer, "tiles.tsv",
				tiles_tsv.data, tiles_tsv.len, "tiles") != MODARCHIVE_OK) {
			s_pdscenarioScratchFree(&rooms_obj, &tiles_tsv, &pads_tsv,
				&spawns_tsv, &volumes_tsv, &objects_tsv, &objectives_tsv,
				&navigation_ini, &level_graph_json, &collision_meta_json,
				&navmesh_meta_json,
				&visual_mesh, &bg_scene);
			modArchiveAbort(aw);
			return -1;
		}
		static const char mtl_buf[] =
			"# Perfect Dark 2 base scenario collision material export\n"
			"newmtl collision\n"
			"Kd 0.650000 0.650000 0.650000\n"
			"Ka 0.150000 0.150000 0.150000\n"
			"Ks 0.000000 0.000000 0.000000\n";
		if (assetArchiveWriterAddPublicMem(&asset_writer, "scenario.mtl",
				mtl_buf, (u32)strlen(mtl_buf), "material") != MODARCHIVE_OK) {
			s_pdscenarioScratchFree(&rooms_obj, &tiles_tsv, &pads_tsv,
				&spawns_tsv, &volumes_tsv, &objects_tsv, &objectives_tsv,
				&navigation_ini, &level_graph_json, &collision_meta_json,
				&navmesh_meta_json,
				&visual_mesh, &bg_scene);
			modArchiveAbort(aw);
			return -1;
		}
	}

	if (has_visual > 0) {
		if (assetArchiveWriterAddPublicMem(&asset_writer, "scene.glb",
				bg_scene.scene_glb, bg_scene.scene_glb_size,
				"scene") != MODARCHIVE_OK ||
		    assetArchiveWriterAddPublicMem(&asset_writer, "visual/export_version.txt",
				PDSCENARIO_BG_VISUAL_EXPORT_VERSION_FILE,
				(u32)strlen(PDSCENARIO_BG_VISUAL_EXPORT_VERSION_FILE),
				"version") != MODARCHIVE_OK ||
		    assetArchiveWriterAddPublicMem(&asset_writer, "visual/scene.obj",
				bg_scene.obj.data, bg_scene.obj.len, "visual_geometry") != MODARCHIVE_OK ||
		    assetArchiveWriterAddPublicMem(&asset_writer, "visual/scene.mtl",
				bg_scene.mtl.data, bg_scene.mtl.len, "visual_material") != MODARCHIVE_OK ||
		    assetArchiveWriterAddPublicMem(&asset_writer, "visual/materials.tsv",
				bg_scene.materials_tsv.data, bg_scene.materials_tsv.len,
				"visual_materials") != MODARCHIVE_OK) {
			s_pdscenarioScratchFree(&rooms_obj, &tiles_tsv, &pads_tsv,
				&spawns_tsv, &volumes_tsv, &objects_tsv, &objectives_tsv,
				&navigation_ini, &level_graph_json, &collision_meta_json,
				&navmesh_meta_json,
				&visual_mesh, &bg_scene);
			modArchiveAbort(aw);
			return -1;
		}
		for (u32 i = 0; i < bg_scene.texture_count; i++) {
			const pdscenario_bgtexture_t *tex = &bg_scene.textures[i];
			if (!tex->decoded || !tex->tga || tex->tga_size == 0) continue;
			if (assetArchiveWriterAddPublicMem(&asset_writer, tex->path,
					tex->tga, tex->tga_size, "texture") != MODARCHIVE_OK) {
				s_pdscenarioScratchFree(&rooms_obj, &tiles_tsv, &pads_tsv,
					&spawns_tsv, &volumes_tsv, &objects_tsv, &objectives_tsv,
					&navigation_ini, &level_graph_json, &collision_meta_json,
					&navmesh_meta_json,
					&visual_mesh, &bg_scene);
				modArchiveAbort(aw);
				return -1;
			}
		}
	}

	if (has_pads > 0) {
		if (assetArchiveWriterAddPublicMem(&asset_writer, "pads.tsv",
				pads_tsv.data, pads_tsv.len, "pads") != MODARCHIVE_OK ||
		    assetArchiveWriterAddPublicMem(&asset_writer, "spawns.tsv",
				spawns_tsv.data, spawns_tsv.len, "spawns") != MODARCHIVE_OK ||
		    assetArchiveWriterAddPublicMem(&asset_writer, "volumes.tsv",
				volumes_tsv.data, volumes_tsv.len, "volumes") != MODARCHIVE_OK) {
			s_pdscenarioScratchFree(&rooms_obj, &tiles_tsv, &pads_tsv,
				&spawns_tsv, &volumes_tsv, &objects_tsv, &objectives_tsv,
				&navigation_ini, &level_graph_json, &collision_meta_json,
				&navmesh_meta_json,
				&visual_mesh, &bg_scene);
			modArchiveAbort(aw);
			return -1;
		}
	}

	if (assetArchiveWriterAddPublicMem(&asset_writer, "objects.tsv",
			objects_tsv.data, objects_tsv.len, "objects") != MODARCHIVE_OK ||
	    assetArchiveWriterAddPublicMem(&asset_writer, "objectives.tsv",
			objectives_tsv.data, objectives_tsv.len, "objectives") != MODARCHIVE_OK ||
	    assetArchiveWriterAddPublicMem(&asset_writer, "navigation.ini",
			navigation_ini.data, navigation_ini.len, "navigation") != MODARCHIVE_OK ||
	    assetArchiveWriterAddPublicMem(&asset_writer, "level.graph.json",
			level_graph_json.data, level_graph_json.len, "level_graph") != MODARCHIVE_OK ||
	    assetArchiveWriterAddPublicMem(&asset_writer, "_meta/generated-collision.json",
			collision_meta_json.data, collision_meta_json.len, "generated_collision") != MODARCHIVE_OK ||
	    assetArchiveWriterAddPublicMem(&asset_writer, "_meta/generated-navmesh.json",
			navmesh_meta_json.data, navmesh_meta_json.len, "generated_navmesh") != MODARCHIVE_OK) {
		s_pdscenarioScratchFree(&rooms_obj, &tiles_tsv, &pads_tsv,
			&spawns_tsv, &volumes_tsv, &objects_tsv, &objectives_tsv,
			&navigation_ini, &level_graph_json, &collision_meta_json,
			&navmesh_meta_json,
			&visual_mesh, &bg_scene);
		modArchiveAbort(aw);
		return -1;
	}

	/* Build _meta/manifest.json */

	char manifest_buf[4096];
	int n = 0;
	n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		"{\n"
		"  \"pd_kind\": \"scenario\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"kind\": \"%s\",\n"
		"  \"stagenum\": %d,\n"
		"  \"display_name\": \"%s\",\n"
		"  \"scene\": \"scene.glb\",\n"
		"  \"scene_format\": \"GLB\",\n"
		"  \"runtime_source\": \"scene.glb\",\n"
		"  \"collision_source\": \"scene.glb\",\n"
		"  \"collision_fallback\": \"from_scene\",\n"
		"  \"navigation\": \"navigation.ini\",\n"
		"  \"level_graph\": \"level.graph.json\",\n"
		"  \"objects\": \"objects.tsv\",\n"
		"  \"objectives\": \"objectives.tsv\",\n"
		"  \"generated_collision\": \"_meta/generated-collision.json\",\n"
		"  \"generated_navmesh\": \"_meta/generated-navmesh.json\"",
		scenario_id, kind, (s32)a->stagenum, a->slug);

	if (has_tiles > 0) n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		",\n  \"compatibility_collision_obj\": \"rooms.obj\""
		",\n  \"materials\": \"scenario.mtl\""
		",\n  \"tiles\": \"tiles.tsv\""
		",\n  \"room_count\": %u"
		",\n  \"geo_count\": %u"
		",\n  \"triangle_count\": %u",
		(unsigned)room_count, (unsigned)geo_count, (unsigned)tri_count);
	if (has_visual > 0) n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		",\n  \"blender_scene\": \"scene.glb\""
		",\n  \"visual_scene\": \"visual/scene.obj\""
		",\n  \"visual_materials\": \"visual/scene.mtl\""
		",\n  \"visual_materials_tsv\": \"visual/materials.tsv\""
		",\n  \"visual_compatibility_format\": \"OBJ+MTL+TGA\""
		",\n  \"visual_format\": \"OBJ+MTL+TGA\""
		",\n  \"visual_export_version\": \"%s\""
		",\n  \"visual_room_count\": %u"
		",\n  \"visual_gdl_count\": %u"
		",\n  \"visual_triangle_count\": %u"
		",\n  \"visual_texture_count\": %u"
		",\n  \"visual_textures_decoded\": %u"
		",\n  \"visual_textures_failed\": %u",
		PDSCENARIO_BG_VISUAL_EXPORT_VERSION,
		(unsigned)bg_scene.room_count, (unsigned)bg_scene.gdl_count,
		(unsigned)bg_scene.tri_count, (unsigned)bg_scene.texture_count,
		(unsigned)bg_scene.decoded_texture_count,
		(unsigned)bg_scene.failed_texture_count);
	if (has_pads > 0) n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		",\n  \"pads\": \"pads.tsv\""
		",\n  \"spawns\": \"spawns.tsv\""
		",\n  \"volumes\": \"volumes.tsv\""
		",\n  \"pad_count\": %u",
		(unsigned)pad_count);

	n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n, "\n}\n");

	if (n <= 0 || (size_t)n >= sizeof(manifest_buf)) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"manifest snprintf truncated for \"%s\"", scenario_id);
		s_pdscenarioScratchFree(&rooms_obj, &tiles_tsv, &pads_tsv,
			&spawns_tsv, &volumes_tsv, &objects_tsv, &objectives_tsv,
			&navigation_ini, &level_graph_json, &collision_meta_json,
			&navmesh_meta_json,
			&visual_mesh, &bg_scene);
		modArchiveAbort(aw);
		return -1;
	}

	char ini_buf[2048];
	int ini_len = snprintf(ini_buf, sizeof(ini_buf),
		"[scenario]\n"
		"catalog_id = %s\n"
		"kind = %s\n"
		"stagenum = %d\n"
		"display_name = %s\n"
		"scene_file = scene.glb\n"
		"scene_format = GLB\n"
		"runtime_source_file = scene.glb\n"
		"collision_source = scene.glb\n"
		"collision_fallback = scene\n"
		"navigation_file = navigation.ini\n"
		"level_graph_file = level.graph.json\n"
		"objects_file = objects.tsv\n"
		"objectives_file = objectives.tsv\n",
		scenario_id, kind, (s32)a->stagenum, a->slug);
	if (has_tiles > 0) ini_len += snprintf(ini_buf + ini_len,
		sizeof(ini_buf) - ini_len,
		"geometry_file = rooms.obj\n"
		"material_file = scenario.mtl\n"
		"geometry_format = OBJ\n"
		"geometry_role = compatibility_collision_export\n");
	if (has_visual > 0) ini_len += snprintf(ini_buf + ini_len,
		sizeof(ini_buf) - ini_len,
		"blender_scene_file = scene.glb\n"
		"visual_scene_file = visual/scene.obj\n"
		"visual_material_file = visual/scene.mtl\n"
		"visual_materials_file = visual/materials.tsv\n"
		"visual_format = OBJ+MTL+TGA\n"
		"visual_role = compatibility_export\n"
		"visual_export_version = " PDSCENARIO_BG_VISUAL_EXPORT_VERSION "\n"
		"texture_manifest_file = visual/materials.tsv\n");
	if (has_tiles > 0) ini_len += snprintf(ini_buf + ini_len,
		sizeof(ini_buf) - ini_len, "tiles_file = tiles.tsv\n");
	if (has_pads > 0) ini_len += snprintf(ini_buf + ini_len,
		sizeof(ini_buf) - ini_len,
		"pads_file = pads.tsv\n"
		"spawns_file = spawns.tsv\n"
		"volumes_file = volumes.tsv\n");
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini_buf)) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"scenario.ini snprintf truncated for \"%s\"", scenario_id);
		s_pdscenarioScratchFree(&rooms_obj, &tiles_tsv, &pads_tsv,
			&spawns_tsv, &volumes_tsv, &objects_tsv, &objectives_tsv,
			&navigation_ini, &level_graph_json, &collision_meta_json,
			&navmesh_meta_json,
			&visual_mesh, &bg_scene);
		modArchiveAbort(aw);
		return -1;
	}

	if (assetArchiveWriterAddDescriptor(&asset_writer, "scenario.ini",
			ini_buf, (u32)ini_len) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"AddFileMem scenario.ini failed for \"%s\"", dst_full);
		s_pdscenarioScratchFree(&rooms_obj, &tiles_tsv, &pads_tsv,
			&spawns_tsv, &volumes_tsv, &objects_tsv, &objectives_tsv,
			&navigation_ini, &level_graph_json, &collision_meta_json,
			&navmesh_meta_json,
			&visual_mesh, &bg_scene);
		modArchiveAbort(aw);
		return -1;
	}
	if (assetArchiveWriterAddManifestJson(&asset_writer,
			manifest_buf, (u32)n) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"AddFileMem _meta/manifest.json failed for \"%s\"", dst_full);
		s_pdscenarioScratchFree(&rooms_obj, &tiles_tsv, &pads_tsv,
			&spawns_tsv, &volumes_tsv, &objects_tsv, &objectives_tsv,
			&navigation_ini, &level_graph_json, &collision_meta_json,
			&navmesh_meta_json,
			&visual_mesh, &bg_scene);
		modArchiveAbort(aw);
		return -1;
	}
	if (assetArchiveWriterFinishMetadata(&asset_writer) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"assetArchiveWriterFinishMetadata failed for \"%s\"", dst_full);
		s_pdscenarioScratchFree(&rooms_obj, &tiles_tsv, &pads_tsv,
			&spawns_tsv, &volumes_tsv, &objects_tsv, &objectives_tsv,
			&navigation_ini, &level_graph_json, &collision_meta_json,
			&navmesh_meta_json,
			&visual_mesh, &bg_scene);
		modArchiveAbort(aw);
		return -1;
	}

	if (modArchiveFinish(aw) != 0) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"modArchiveFinish failed for \"%s\"", dst_full);
		s_pdscenarioScratchFree(&rooms_obj, &tiles_tsv, &pads_tsv,
			&spawns_tsv, &volumes_tsv, &objects_tsv, &objectives_tsv,
			&navigation_ini, &level_graph_json, &collision_meta_json,
			&navmesh_meta_json,
			&visual_mesh, &bg_scene);
		return -1;
	}
	s_pdscenarioScratchFree(&rooms_obj, &tiles_tsv, &pads_tsv,
		&spawns_tsv, &volumes_tsv, &objects_tsv, &objectives_tsv,
		&navigation_ini, &level_graph_json, &collision_meta_json,
		&navmesh_meta_json,
		&visual_mesh, &bg_scene);
	return 1;
}

/* Engine Phase 4: file-scope fan-out context for the dual-emit pdarena
 * + pdscenario walk. */
typedef struct {
	const char  *arenas_dir;
	const char  *scenarios_dir;
	s32          force_rewrite;
	s32          count;
	SDL_atomic_t arenas_written;
	SDL_atomic_t arenas_skipped;
	SDL_atomic_t arenas_failed;
	SDL_atomic_t scenarios_written;
	SDL_atomic_t scenarios_skipped;
	SDL_atomic_t scenarios_failed;
	SDL_atomic_t processed;
} pdarena_fanout_ctx_t;

static void s_pdarenaWork(int i, void *user)
{
	pdarena_fanout_ctx_t *c = (pdarena_fanout_ctx_t *)user;
	if (i < 0 || i >= c->count) return;

	const arena_authored_record_t *a = &g_ArenaData[i];

	s32 r2 = s_emitOnePdscenario(a, c->scenarios_dir, c->force_rewrite);
	if (r2 > 0)       SDL_AtomicAdd(&c->scenarios_written, 1);
	else if (r2 == 0) SDL_AtomicAdd(&c->scenarios_skipped, 1);
	else              SDL_AtomicAdd(&c->scenarios_failed,  1);

	s32 r1 = s_emitOnePdarena(a, i, c->arenas_dir, c->scenarios_dir,
		c->force_rewrite);
	if (r1 > 0)       SDL_AtomicAdd(&c->arenas_written, 1);
	else if (r1 == 0) SDL_AtomicAdd(&c->arenas_skipped, 1);
	else              SDL_AtomicAdd(&c->arenas_failed,  1);

	int done = SDL_AtomicAdd(&c->processed, 1) + 1;
	if ((done & 0x07) == 0 || done == c->count) {
		bootProgressUpdate(done, c->count);
	}
}

s32 romExtractAllPdarena(s32 force_rewrite)
{
	/* BYOR completion (2026-05-03): walks g_ArenaData[] from the
	 * authoring source-of-truth (port/src/arenadata_authored.c). */

	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDARENA",
			"fsDataDirEnsure failed; cannot create output dir");
		return -1;
	}

	char dataDirBuf[FS_MAXPATH + 1];
	const char *dataDir = fsDataDir(dataDirBuf, sizeof(dataDirBuf));

	char arenas_dir[FS_MAXPATH];
	snprintf(arenas_dir, sizeof(arenas_dir), "%s/arenas", dataDir);
	if (!fsCreateDir(arenas_dir)) {
		sysLoudFailf("EXTRACT.PDARENA",
			"fsCreateDir(\"%s\") failed", arenas_dir);
		return -1;
	}

	char scenarios_dir[FS_MAXPATH];
	snprintf(scenarios_dir, sizeof(scenarios_dir), "%s/scenarios", dataDir);
	if (!fsCreateDir(scenarios_dir)) {
		sysLoudFailf("EXTRACT.PDARENA",
			"fsCreateDir(\"%s\") failed", scenarios_dir);
		return -1;
	}

	if (romExtractPdFastCacheCanSkip("pdarena", arenas_dir,
			".pdarena", force_rewrite) &&
	    romExtractPdFastCacheCanSkip("pdscenario", scenarios_dir,
			".pdscenario", force_rewrite)) {
		bootProgressUpdate(g_ArenaDataCount, g_ArenaDataCount);
		sysLogPrintf(LOG_NOTE,
			"romextract pdarena: arenas written=0 skipped=%d failed=0 total=%d (fast-cache)",
			g_ArenaDataCount, g_ArenaDataCount);
		sysLogPrintf(LOG_NOTE,
			"romextract pdscenario: written=0 skipped=%d failed=0 (fast-cache)",
			g_ArenaDataCount);
		return 0;
	}

	pdarena_fanout_ctx_t actx;
	memset(&actx, 0, sizeof(actx));
	actx.arenas_dir    = arenas_dir;
	actx.scenarios_dir = scenarios_dir;
	actx.force_rewrite = force_rewrite;
	actx.count         = g_ArenaDataCount;
	SDL_AtomicSet(&actx.arenas_written,    0);
	SDL_AtomicSet(&actx.arenas_skipped,    0);
	SDL_AtomicSet(&actx.arenas_failed,     0);
	SDL_AtomicSet(&actx.scenarios_written, 0);
	SDL_AtomicSet(&actx.scenarios_skipped, 0);
	SDL_AtomicSet(&actx.scenarios_failed,  0);
	SDL_AtomicSet(&actx.processed,         0);

	/* Stage preprocessors share process-global scratch state; emit
	 * scenarios in order rather than running preprocess work in parallel. */
	bootProgressUpdate(0, g_ArenaDataCount);
	for (s32 i = 0; i < g_ArenaDataCount; i++) {
		s_pdarenaWork(i, &actx);
	}
	bootProgressUpdate(g_ArenaDataCount, g_ArenaDataCount);

	s32 arenas_written    = SDL_AtomicGet(&actx.arenas_written);
	s32 arenas_skipped    = SDL_AtomicGet(&actx.arenas_skipped);
	s32 arenas_failed     = SDL_AtomicGet(&actx.arenas_failed);
	s32 scenarios_written = SDL_AtomicGet(&actx.scenarios_written);
	s32 scenarios_skipped = SDL_AtomicGet(&actx.scenarios_skipped);
	s32 scenarios_failed  = SDL_AtomicGet(&actx.scenarios_failed);

	sysLogPrintf(LOG_NOTE,
		"romextract pdarena: arenas written=%d skipped=%d failed=%d total=%d",
		arenas_written, arenas_skipped, arenas_failed, g_ArenaDataCount);
	sysLogPrintf(LOG_NOTE,
		"romextract pdscenario: written=%d skipped=%d failed=%d",
		scenarios_written, scenarios_skipped, scenarios_failed);

	if (arenas_failed == 0 && scenarios_failed == 0) {
		romExtractPdFastCacheWrite("pdarena", arenas_dir, ".pdarena");
		romExtractPdFastCacheWrite("pdscenario", scenarios_dir, ".pdscenario");
	}

	return arenas_written + scenarios_written;
}
