/**
 * romextract_pdarena.c -- Catalog universality pivot Step 2 (2026-05-03).
 *
 * Walks the loader_pool arena pool and emits TWO files per registered
 * arena per Mike's Q-1 unified-scenario directive:
 *
 *   1. data/<romid>/arenas/<id>.pdarena
 *      ZIP-openable typed asset archive. The archive root carries
 *      arena.ini for the modder-facing descriptor plus manifest.json for
 *      the legacy universal walker until the base walker consumes the INI
 *      path directly.
 *
 *   2. data/<romid>/scenarios/<scenario_id>.pdscenario
 *      ZIP compound bundling rooms.obj / tiles.tsv / pads.tsv / setup.tsv /
 *      mpsetup.tsv plus a manifest.json envelope. UNIFIED per Q-1 (one
 *      file per stage; modder-friendly atomic distribution).
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
#include "preprocess.h"
#include "romdata.h"
#include "romextract.h"
#include "romextract_pd.h"
#include "sha256.h"
#include "system.h"
#include "game/stagetable.h"
#include "arenadata_authored.h"

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

static void s_textbufFree(pdscenario_textbuf_t *b)
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

	u32 cap = romdataFileGetEstimatedSize(raw_size, loadtype);
	if (cap < raw_size) cap = raw_size;
	u8 *work = sysMemZeroAlloc(cap);
	if (!work) {
		sysMemFree(raw);
		return -1;
	}
	memcpy(work, raw, raw_size);
	sysMemFree(raw);

	u32 new_size = raw_size;
	if (loadtype == LOADTYPE_TILES) {
		(void)preprocessTilesFile(work, raw_size, &new_size);
	} else if (loadtype == LOADTYPE_PADS) {
		(void)preprocessPadsFile(work, raw_size, &new_size);
	} else if (loadtype == LOADTYPE_SETUP) {
		(void)preprocessSetupFile(work, raw_size, &new_size);
	}

	*out_data = work;
	*out_size = new_size;
	return 1;
}

static s32 s_objEmitTri(pdscenario_textbuf_t *obj, u32 *next_index,
                        f32 x0, f32 y0, f32 z0,
                        f32 x1, f32 y1, f32 z1,
                        f32 x2, f32 y2, f32 z2)
{
	u32 i0 = (*next_index)++;
	u32 i1 = (*next_index)++;
	u32 i2 = (*next_index)++;
	return s_textbufAppendf(obj,
		"v %.6f %.6f %.6f\n"
		"v %.6f %.6f %.6f\n"
		"v %.6f %.6f %.6f\n"
		"f %u %u %u\n",
		(double)x0, (double)y0, (double)z0,
		(double)x1, (double)y1, (double)z1,
		(double)x2, (double)y2, (double)z2,
		i0, i1, i2);
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
					if (s_objEmitTri(obj, &next_index,
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
					if (s_objEmitTri(obj, &next_index,
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
					if (s_objEmitTri(obj, &next_index, x0, block->ymin, z0, x1, block->ymin, z1, x1, block->ymax, z1) != 0 ||
					    s_objEmitTri(obj, &next_index, x0, block->ymin, z0, x1, block->ymax, z1, x0, block->ymax, z0) != 0) return -1;
					tris += 2;
				}
				for (s32 i = 1; i < geo->numvertices - 1; i++) {
					if (s_objEmitTri(obj, &next_index,
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
					if (s_objEmitTri(obj, &next_index, x0, cyl->ymin, z0, x1, cyl->ymin, z1, x1, cyl->ymax, z1) != 0 ||
					    s_objEmitTri(obj, &next_index, x0, cyl->ymin, z0, x1, cyl->ymax, z1, x0, cyl->ymax, z0) != 0) return -1;
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

static s32 s_buildPadsTsv(const u8 *data, u32 size, pdscenario_textbuf_t *tsv,
                          u32 *out_pads)
{
	if (out_pads) *out_pads = 0;
	if (!data || size < sizeof(struct padsfileheader) || !tsv) return -1;
	const struct padsfileheader *hdr = (const struct padsfileheader *)data;
	if (hdr->numpads < 0 || hdr->numpads > 8192) return -1;
	if ((uintptr_t)&hdr->padoffsets[hdr->numpads] > (uintptr_t)data + size) return -1;

	if (s_textbufAppend(tsv,
			"pad_index\troom\tliftnum\tflags\tpos_x\tpos_y\tpos_z\tup_x\tup_y\tup_z\tlook_x\tlook_y\tlook_z\tbbox_xmin\tbbox_xmax\tbbox_ymin\tbbox_ymax\tbbox_zmin\tbbox_zmax\n") != 0) {
		return -1;
	}

	for (s32 i = 0; i < hdr->numpads; i++) {
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

		if (s_textbufAppendf(tsv,
				"%d\t%d\t%d\t0x%05x\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\n",
				i, room, liftnum, (unsigned)flags,
				(double)pos[0], (double)pos[1], (double)pos[2],
				(double)up[0], (double)up[1], (double)up[2],
				(double)look[0], (double)look[1], (double)look[2],
				(double)bbox[0], (double)bbox[1], (double)bbox[2],
				(double)bbox[3], (double)bbox[4], (double)bbox[5]) != 0) {
			return -1;
		}
	}

	if (out_pads) *out_pads = (u32)hdr->numpads;
	return 0;
}

static s32 s_buildWordsTsv(const u8 *data, u32 size, const char *source_format,
                           pdscenario_textbuf_t *tsv)
{
	if (!data || !size || !tsv) return -1;
	if (s_textbufAppendf(tsv,
			"# source_format = %s\n"
			"offset\tu32_le\ts32_le\tf32_le\tascii4\n",
			source_format ? source_format : "unknown") != 0) {
		return -1;
	}
	for (u32 off = 0; off < size; off += 4) {
		u32 word = 0;
		u32 avail = size - off;
		if (avail > 4) avail = 4;
		memcpy(&word, data + off, avail);
		f32 f = 0.0f;
		memcpy(&f, &word, sizeof(f));
		char ascii[5];
		for (u32 i = 0; i < 4; i++) {
			u8 c = (i < avail) ? data[off + i] : 0;
			ascii[i] = (c >= 32 && c <= 126) ? (char)c : '.';
		}
		ascii[4] = '\0';
		if (s_textbufAppendf(tsv, "0x%06x\t0x%08x\t%d\t%.9g\t%s\n",
				(unsigned)off, (unsigned)word, (s32)word, (double)f, ascii) != 0) {
			return -1;
		}
	}
	return 0;
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

/* Emit one .pdarena ZIP-openable archive. Returns 1 written, 0 skipped,
 * -1 failed. */
static s32 s_emitOnePdarena(const arena_authored_record_t *a, s32 arena_index,
                             const char *out_dir, s32 force_rewrite)
{
	const char *catalog_id = a->catalog_id;
	if (!catalog_id || !catalog_id[0]) return 0;

	char filename[128];
	s_idToFilename(catalog_id, filename, sizeof(filename));

	char relpath[FS_MAXPATH];
	snprintf(relpath, sizeof(relpath), "%s/%s.pdarena", out_dir, filename);

	if (!force_rewrite && fsFileSize(relpath) > 0 && s_existingZipArchive(relpath)) {
		return 0;
	}

	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) {
		sysLoudFailf("EXTRACT.PDARENA",
			"fsFullPath failed for \"%s\"", relpath);
		return -1;
	}

	char scenario_id[96];
	s32 stage_idx = stageGetIndex(a->stagenum);
	if (stage_idx >= 0) {
		s_scenarioCatalogIdFromSlug(a->slug, scenario_id, sizeof(scenario_id));
	} else {
		scenario_id[0] = '\0';
	}

	const char *load_mode_str = loaderEnumNameForArenaLoadMode(a->load_mode);

	char manifest_buf[1024];
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
			? "  \"scenario\": \"%s\"\n}\n"
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
			"scenario = %s\n", scenario_id);
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
	if (modArchiveAddFileMem(aw, "arena.ini", ini_buf, (u32)ini_len) != 0) {
		sysLoudFailf("EXTRACT.PDARENA",
			"AddFileMem arena.ini failed for \"%s\"", full);
		modArchiveAbort(aw);
		return -1;
	}
	if (modArchiveAddFileMem(aw, "manifest.json",
	                          manifest_buf, (u32)manifest_len) != 0) {
		sysLoudFailf("EXTRACT.PDARENA",
			"AddFileMem manifest.json failed for \"%s\"", full);
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

	char filename[128];
	s_idToFilename(scenario_id, filename, sizeof(filename));

	char dst_rel[FS_MAXPATH];
	snprintf(dst_rel, sizeof(dst_rel), "%s/%s.pdscenario", out_dir, filename);

	if (!force_rewrite && fsFileSize(dst_rel) > 0 &&
	    s_existingArchiveHasEntry(dst_rel, "scenario.ini") &&
	    s_existingArchiveHasEntry(dst_rel, "rooms.obj")) return 0;

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

	pdscenario_textbuf_t rooms_obj = { 0 };
	pdscenario_textbuf_t tiles_tsv = { 0 };
	pdscenario_textbuf_t pads_tsv = { 0 };
	pdscenario_textbuf_t setup_tsv = { 0 };
	pdscenario_textbuf_t mpsetup_tsv = { 0 };
	pdscenario_textbuf_t bg_tsv = { 0 };

	u32 room_count = 0;
	u32 geo_count = 0;
	u32 tri_count = 0;
	u32 pad_count = 0;

	u8 *tiles_data = NULL;
	u32 tiles_size = 0;
	s32 has_tiles = s_loadStageFilePreprocessed(st->tilefileid, LOADTYPE_TILES,
		&tiles_data, &tiles_size);
	if (has_tiles > 0) {
		if (s_buildTilesExports(tiles_data, tiles_size, &rooms_obj, &tiles_tsv,
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
		if (s_buildPadsTsv(pads_data, pads_size, &pads_tsv, &pad_count) != 0) {
			sysLogPrintf(LOG_WARNING,
				"romextract pdscenario: pads conversion failed for \"%s\"",
				scenario_id);
			has_pads = -1;
		}
		sysMemFree(pads_data);
	}

	u8 *setup_data = NULL;
	u32 setup_size = 0;
	s32 has_setup = s_loadStageFilePreprocessed(st->setupfileid, LOADTYPE_SETUP,
		&setup_data, &setup_size);
	if (has_setup > 0) {
		if (s_buildWordsTsv(setup_data, setup_size, "PD_SETUP_PREPROCESSED", &setup_tsv) != 0) {
			has_setup = -1;
		}
		sysMemFree(setup_data);
	}

	u8 *mpsetup_data = NULL;
	u32 mpsetup_size = 0;
	s32 has_mpsetup = s_loadStageFilePreprocessed(st->mpsetupfileid, LOADTYPE_SETUP,
		&mpsetup_data, &mpsetup_size);
	if (has_mpsetup > 0) {
		if (s_buildWordsTsv(mpsetup_data, mpsetup_size, "PD_MPSETUP_PREPROCESSED", &mpsetup_tsv) != 0) {
			has_mpsetup = -1;
		}
		sysMemFree(mpsetup_data);
	}

	u8 *bg_data = NULL;
	u32 bg_size = 0;
	s32 has_bg = s_loadStageFileRaw(st->bgfileid, &bg_data, &bg_size);
	if (has_bg > 0) {
		if (s_buildWordsTsv(bg_data, bg_size, "PD_BG_SEGMENT_SOURCE", &bg_tsv) != 0) {
			has_bg = -1;
		}
		sysMemFree(bg_data);
	}

	if (has_tiles < 0 || has_pads < 0 || has_setup < 0 ||
	    has_mpsetup < 0 || has_bg < 0) {
		s_textbufFree(&rooms_obj);
		s_textbufFree(&tiles_tsv);
		s_textbufFree(&pads_tsv);
		s_textbufFree(&setup_tsv);
		s_textbufFree(&mpsetup_tsv);
		s_textbufFree(&bg_tsv);
		modArchiveAbort(aw);
		return -1;
	}

	if (has_tiles > 0) {
		if (modArchiveAddFileMem(aw, "rooms.obj", rooms_obj.data, rooms_obj.len) != 0 ||
		    modArchiveAddFileMem(aw, "tiles.tsv", tiles_tsv.data, tiles_tsv.len) != 0 ||
		    s_addShaSidecar(aw, "rooms.obj.sha256", rooms_obj.data, rooms_obj.len) != 0 ||
		    s_addShaSidecar(aw, "tiles.tsv.sha256", tiles_tsv.data, tiles_tsv.len) != 0) {
			s_textbufFree(&rooms_obj);
			s_textbufFree(&tiles_tsv);
			s_textbufFree(&pads_tsv);
			s_textbufFree(&setup_tsv);
			s_textbufFree(&mpsetup_tsv);
			s_textbufFree(&bg_tsv);
			modArchiveAbort(aw);
			return -1;
		}
		static const char mtl_buf[] =
			"# Perfect Dark 2 base scenario material export\n"
			"newmtl collision\n"
			"Kd 0.650000 0.650000 0.650000\n"
			"Ka 0.150000 0.150000 0.150000\n"
			"Ks 0.000000 0.000000 0.000000\n";
		if (modArchiveAddFileMem(aw, "scenario.mtl", mtl_buf, (u32)strlen(mtl_buf)) != 0 ||
		    s_addShaSidecar(aw, "scenario.mtl.sha256", mtl_buf, (u32)strlen(mtl_buf)) != 0) {
			s_textbufFree(&rooms_obj);
			s_textbufFree(&tiles_tsv);
			s_textbufFree(&pads_tsv);
			s_textbufFree(&setup_tsv);
			s_textbufFree(&mpsetup_tsv);
			s_textbufFree(&bg_tsv);
			modArchiveAbort(aw);
			return -1;
		}
	}

	if (has_pads > 0) {
		if (modArchiveAddFileMem(aw, "pads.tsv", pads_tsv.data, pads_tsv.len) != 0 ||
		    s_addShaSidecar(aw, "pads.tsv.sha256", pads_tsv.data, pads_tsv.len) != 0) {
			s_textbufFree(&rooms_obj);
			s_textbufFree(&tiles_tsv);
			s_textbufFree(&pads_tsv);
			s_textbufFree(&setup_tsv);
			s_textbufFree(&mpsetup_tsv);
			s_textbufFree(&bg_tsv);
			modArchiveAbort(aw);
			return -1;
		}
	}
	if (has_setup > 0) {
		if (modArchiveAddFileMem(aw, "setup.tsv", setup_tsv.data, setup_tsv.len) != 0 ||
		    s_addShaSidecar(aw, "setup.tsv.sha256", setup_tsv.data, setup_tsv.len) != 0) {
			s_textbufFree(&rooms_obj);
			s_textbufFree(&tiles_tsv);
			s_textbufFree(&pads_tsv);
			s_textbufFree(&setup_tsv);
			s_textbufFree(&mpsetup_tsv);
			s_textbufFree(&bg_tsv);
			modArchiveAbort(aw);
			return -1;
		}
	}
	if (has_mpsetup > 0) {
		if (modArchiveAddFileMem(aw, "mpsetup.tsv", mpsetup_tsv.data, mpsetup_tsv.len) != 0 ||
		    s_addShaSidecar(aw, "mpsetup.tsv.sha256", mpsetup_tsv.data, mpsetup_tsv.len) != 0) {
			s_textbufFree(&rooms_obj);
			s_textbufFree(&tiles_tsv);
			s_textbufFree(&pads_tsv);
			s_textbufFree(&setup_tsv);
			s_textbufFree(&mpsetup_tsv);
			s_textbufFree(&bg_tsv);
			modArchiveAbort(aw);
			return -1;
		}
	}
	if (has_bg > 0) {
		if (modArchiveAddFileMem(aw, "visual_segments.tsv", bg_tsv.data, bg_tsv.len) != 0 ||
		    s_addShaSidecar(aw, "visual_segments.tsv.sha256", bg_tsv.data, bg_tsv.len) != 0) {
			s_textbufFree(&rooms_obj);
			s_textbufFree(&tiles_tsv);
			s_textbufFree(&pads_tsv);
			s_textbufFree(&setup_tsv);
			s_textbufFree(&mpsetup_tsv);
			s_textbufFree(&bg_tsv);
			modArchiveAbort(aw);
			return -1;
		}
	}

	/* Build manifest.json */
	const char *kind = s_kindFromCategory(a->category);

	char manifest_buf[1024];
	int n = 0;
	n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		"{\n"
		"  \"pd_kind\": \"scenario\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"kind\": \"%s\",\n"
		"  \"stagenum\": %d,\n"
		"  \"display_name\": \"%s\"",
		scenario_id, kind, (s32)a->stagenum, a->slug);

	if (has_tiles > 0) n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		",\n  \"geometry\": \"rooms.obj\""
		",\n  \"materials\": \"scenario.mtl\""
		",\n  \"tiles\": \"tiles.tsv\""
		",\n  \"room_count\": %u"
		",\n  \"geo_count\": %u"
		",\n  \"triangle_count\": %u",
		(unsigned)room_count, (unsigned)geo_count, (unsigned)tri_count);
	if (has_pads > 0) n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		",\n  \"pads\": \"pads.tsv\""
		",\n  \"pad_count\": %u",
		(unsigned)pad_count);
	if (has_setup > 0) n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		",\n  \"setup\": \"setup.tsv\"");
	if (has_mpsetup > 0) n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		",\n  \"mpsetup\": \"mpsetup.tsv\"");
	if (has_bg > 0) n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		",\n  \"visual_source\": \"visual_segments.tsv\"");

	n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n, "\n}\n");

	if (n <= 0 || (size_t)n >= sizeof(manifest_buf)) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"manifest snprintf truncated for \"%s\"", scenario_id);
		modArchiveAbort(aw);
		return -1;
	}

	char ini_buf[1024];
	int ini_len = snprintf(ini_buf, sizeof(ini_buf),
		"[scenario]\n"
		"catalog_id = %s\n"
		"kind = %s\n"
		"stagenum = %d\n"
		"display_name = %s\n",
		scenario_id, kind, (s32)a->stagenum, a->slug);
	if (has_tiles > 0) ini_len += snprintf(ini_buf + ini_len,
		sizeof(ini_buf) - ini_len,
		"geometry_file = rooms.obj\n"
		"material_file = scenario.mtl\n"
		"geometry_format = OBJ\n");
	if (has_tiles > 0) ini_len += snprintf(ini_buf + ini_len,
		sizeof(ini_buf) - ini_len, "tiles_file = tiles.tsv\n");
	if (has_pads > 0) ini_len += snprintf(ini_buf + ini_len,
		sizeof(ini_buf) - ini_len, "pads_file = pads.tsv\n");
	if (has_setup > 0) ini_len += snprintf(ini_buf + ini_len,
		sizeof(ini_buf) - ini_len, "setup_file = setup.tsv\n");
	if (has_mpsetup > 0) ini_len += snprintf(ini_buf + ini_len,
		sizeof(ini_buf) - ini_len, "mpsetup_file = mpsetup.tsv\n");
	if (has_bg > 0) ini_len += snprintf(ini_buf + ini_len,
		sizeof(ini_buf) - ini_len, "visual_source_file = visual_segments.tsv\n");
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini_buf)) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"scenario.ini snprintf truncated for \"%s\"", scenario_id);
		modArchiveAbort(aw);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "scenario.ini", ini_buf, (u32)ini_len) != 0) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"AddFileMem scenario.ini failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		return -1;
	}
	if (modArchiveAddFileMem(aw, "manifest.json",
	                          manifest_buf, (u32)n) != 0) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"AddFileMem manifest.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		return -1;
	}

	if (modArchiveFinish(aw) != 0) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"modArchiveFinish failed for \"%s\"", dst_full);
		return -1;
	}
	s_textbufFree(&rooms_obj);
	s_textbufFree(&tiles_tsv);
	s_textbufFree(&pads_tsv);
	s_textbufFree(&setup_tsv);
	s_textbufFree(&mpsetup_tsv);
	s_textbufFree(&bg_tsv);
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

	s32 r1 = s_emitOnePdarena(a, i, c->arenas_dir, c->force_rewrite);
	if (r1 > 0)       SDL_AtomicAdd(&c->arenas_written, 1);
	else if (r1 == 0) SDL_AtomicAdd(&c->arenas_skipped, 1);
	else              SDL_AtomicAdd(&c->arenas_failed,  1);

	s32 r2 = s_emitOnePdscenario(a, c->scenarios_dir, c->force_rewrite);
	if (r2 > 0)       SDL_AtomicAdd(&c->scenarios_written, 1);
	else if (r2 == 0) SDL_AtomicAdd(&c->scenarios_skipped, 1);
	else              SDL_AtomicAdd(&c->scenarios_failed,  1);

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

	bootProgressUpdate(0, g_ArenaDataCount);
	bootPoolForRangeBlocking(0, g_ArenaDataCount, s_pdarenaWork, &actx);
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

	return arenas_written + scenarios_written;
}
