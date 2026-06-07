/**
 * romextract_pdanim_chr.c -- Catalog universality pivot Step 3a (2026-05-03).
 *
 * Walks the chr-animation table embedded in the "animations" ROM
 * segment (data/<romid>/segs/animations.bin on disk; in-memory pointer
 * via _animationsTableRomStart / _animationsTableRomEnd) and emits
 * one .pdanim ZIP compound per registered chr animation at
 * data/<romid>/animations/<id>.pdanim.
 *
 * Companion to romextract_pdanim.c (Step 1, weapon-animation gunscript
 * opcodes -- ZIP compound, category="weapon_animation"). This file
 * handles category="character_animation" -- ZIP compound per the
 * universality-pivot-schemas.md Section 2.6 lock-down.
 *
 * Per Mike's Q-3 ruling (2026-05-02): "DO NOT DEFER beyond the scope
 * of the catalog work. Catalog is not complete unless it is COMPLETE."
 * Step 3a closes that ruling so chr animations become per-asset files
 * alongside the Step 1 weapon-anim files.
 *
 * Per-anim byte layout in the lump (validated against
 * src/lib/anim.c::animLoadFrame, line 312 area):
 *   header bytes : entry.data .. entry.data + entry.headerlen
 *   frame data   : entry.data + entry.headerlen ..
 *                  entry.data + entry.headerlen + numframes * bytesperframe
 * Total per-anim span = headerlen + numframes * bytesperframe. The extractor
 * decodes those packed native bytes into semantic GLTF animation channels so
 * the base character animation archive stays editable/openable.
 * The shared writer owns _meta inventory, hashes, provenance, validation,
 * source handles, and public-file sidecars.
 *
 * Companion docs:
 *   context/designs/catalog/universality-pivot-schemas.md   schemas
 *   context/audits/catalog-universality-pivot-plan-2026-05-02.md  plan
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
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
#include "asset_archive_writer.h"
#include "modarchive.h"
#include "romdata.h"
#include "romextract.h"
#include "romextract_pd.h"
#include "system.h"

extern double sin(double);
extern double cos(double);

/* The chr animation table sits at the tail of the "animations" segment.
 * Byte length is fixed at 0x38a0 across ROM versions; first u32 is the
 * count (already byte-swapped by preprocessAnimations), followed by
 * `count` struct animtableentry records (also byte-swapped). */
#define PDANIM_CHR_TABLE_TAIL_BYTES 0x38a0
#define PDANIM_CHR_SCHEMA_VERSION 3
#define PDANIM_MAX_TAIL_VALUES 512

static s32 s_memContains(const char *data, u32 size, const char *needle)
{
	size_t needle_len;

	if (!data || !needle) return 0;
	needle_len = strlen(needle);
	if (needle_len == 0 || size < needle_len) return 0;

	for (u32 i = 0; i + needle_len <= size; i++) {
		if (memcmp(data + i, needle, needle_len) == 0) {
			return 1;
		}
	}
	return 0;
}

/* Locate the "animations" segment buffer + size by name. Returns 1 on
 * success with *outData / *outSize populated, 0 on miss (segment not
 * populated; e.g. server build with no ROM loaded). */
static s32 s_findAnimSegment(const u8 **outData, u32 *outSize)
{
	*outData = NULL;
	*outSize = 0;

	s32 nseg = romdataSegmentCount();
	for (s32 i = 0; i < nseg; i++) {
		const char *name = romdataSegmentGetName(i);
		if (name && strcmp(name, "animations") == 0) {
			const u8 *data = romdataSegmentGetData(i);
			u32 size = romdataSegmentGetSize(i);
			if (!data || size == 0) return 0;
			*outData = data;
			*outSize = size;
			return 1;
		}
	}
	return 0;
}

/* Build the catalog ID for a chr animation index. If the loader's
 * enum reverse-lookup has a symbolic name (e.g. "ANIM_HEROHIT"), the
 * shared readable-ID helper uses it. Otherwise it emits a readable
 * generated fallback and leaves the raw index as metadata only.
 *
 * Returns 1 if a symbolic name was found, 0 if generated. */
static s32 s_buildCatalogId(s32 anim_idx, char *out, size_t out_n)
{
	const char *sym = loaderEnumNameForAnimEnum(anim_idx);
	catalogReadableAnimationId(anim_idx, "character", out, out_n);
	return sym && sym[0] ? 1 : 0;
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
	      && modArchiveFindEntry(arc, "animation.gltf") >= 0;
	if (ok) {
		u32 manifest_size = 0;
		s32 manifest_idx = modArchiveFindEntry(arc, "_meta/manifest.json");
		char *manifest = (char *)modArchiveExtractAlloc(arc, manifest_idx,
			&manifest_size);
		ok = manifest
			&& s_memContains(manifest, manifest_size,
				"\"pd_schema_version\": 3")
			&& s_memContains(manifest, manifest_size,
				"\"animation\": \"animation.gltf\"");
		free(manifest);
	}
	modArchiveClose(arc);
	return ok;
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

typedef struct {
	u8 *data;
	u32 len;
	u32 cap;
} pdanim_binbuf_t;

typedef struct {
	u8 flags;
	const u8 *header;
	u32 bit_offset;
} pdanim_part_desc_t;

typedef struct {
	s16 repeattoframe;
	s16 repeatfromframe;
} pdanim_repeat_range_t;

typedef struct {
	u32 part_count;
	u32 channel_count;
	u32 descriptor_header_len;
	u32 bits_per_frame;
	u32 anim_flags;
	u32 repeat_range_count;
	u32 cut_skip_count;
	u32 fields_pos_angle;
	u32 fields_s16_translate;
	u32 fields_s32_translate;
	u32 fields_s16_rotate;
	u32 fields_f32_rotate;
	u32 fields_camera;
	u32 fields_scale;
	u32 bin_len;
	u32 b64_len;
} pdanim_build_stats_t;

static void s_buildReason(char *reason, size_t reason_n,
                          const char *fmt, ...)
{
	if (!reason || reason_n == 0) {
		return;
	}

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(reason, reason_n, fmt, ap);
	va_end(ap);
	reason[reason_n - 1] = '\0';
}

static s16 s_readBeS16(const u8 *ptr)
{
	return (s16)(((u16)ptr[0] << 8) | (u16)ptr[1]);
}

static u32 s_animDescriptorHeaderLen(const u8 *header, u32 header_len,
                                     u32 anim_flags,
                                     pdanim_repeat_range_t *repeat_ranges,
                                     u32 *repeat_count,
                                     s16 *cut_skip_frames,
                                     u32 *cut_skip_count,
                                     char *reason, size_t reason_n)
{
	const u8 *start = header;
	const u8 *ptr;
	u32 repeats = 0;
	u32 skips = 0;

	if (repeat_count) *repeat_count = 0;
	if (cut_skip_count) *cut_skip_count = 0;
	if (!header || header_len < 2) {
		return header_len;
	}
	if ((anim_flags & (ANIMFLAG_HASREPEATFRAMES
			| ANIMFLAG_HASCUTSKIPFRAMES)) == 0) {
		return header_len;
	}

	ptr = header + header_len - 2;

	if (anim_flags & ANIMFLAG_HASREPEATFRAMES) {
		while (ptr >= start) {
			s16 repeatfromframe = s_readBeS16(ptr);
			s16 repeattoframe;
			if (repeatfromframe < 0) {
				break;
			}
			if (ptr < start + 2) {
				s_buildReason(reason, reason_n,
					"repeat-frame tail underflow");
				return header_len;
			}
			repeattoframe = s_readBeS16(ptr - 2);
			if (repeat_ranges && repeats < PDANIM_MAX_TAIL_VALUES) {
				repeat_ranges[repeats].repeatfromframe = repeatfromframe;
				repeat_ranges[repeats].repeattoframe = repeattoframe;
			}
			repeats++;
			ptr -= 4;
		}
		if (ptr < start) {
			s_buildReason(reason, reason_n,
				"repeat-frame tail missing negative terminator");
			return header_len;
		}
	}

	if (anim_flags & ANIMFLAG_HASCUTSKIPFRAMES) {
		if (anim_flags & ANIMFLAG_HASREPEATFRAMES) {
			if (ptr < start + 2) {
				s_buildReason(reason, reason_n,
					"cut-skip tail underflow before repeat terminator");
				return header_len;
			}
			ptr -= 2;
		}
		while (ptr >= start) {
			s16 skipframe = s_readBeS16(ptr);
			if (skipframe < 0) {
				break;
			}
			if (cut_skip_frames && skips < PDANIM_MAX_TAIL_VALUES) {
				cut_skip_frames[skips] = skipframe;
			}
			skips++;
			ptr -= 2;
		}
		if (ptr < start) {
			s_buildReason(reason, reason_n,
				"cut-skip tail missing negative terminator");
			return header_len;
		}
	}

	if (repeats > PDANIM_MAX_TAIL_VALUES || skips > PDANIM_MAX_TAIL_VALUES) {
		s_buildReason(reason, reason_n,
			"animation tail exceeds semantic export cap repeats=%u skips=%u",
			(unsigned)repeats, (unsigned)skips);
		return header_len;
	}

	if (repeat_count) *repeat_count = repeats;
	if (cut_skip_count) *cut_skip_count = skips;
	return (u32)(ptr - start);
}

static void s_binbufFree(pdanim_binbuf_t *b)
{
	if (b->data) free(b->data);
	memset(b, 0, sizeof(*b));
}

static s32 s_binbufReserve(pdanim_binbuf_t *b, u32 extra)
{
	if (extra > 0xffffffffu - b->len) return -1;
	u32 need = b->len + extra;
	if (need <= b->cap) return 0;
	u32 cap = b->cap ? b->cap : 1024;
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

static s32 s_binbufAppend(pdanim_binbuf_t *b, const void *data, u32 len)
{
	if (s_binbufReserve(b, len) != 0) return -1;
	memcpy(b->data + b->len, data, len);
	b->len += len;
	return 0;
}

static void s_writeLeFloat(u8 *dst, f32 value)
{
	union {
		f32 f;
		u32 u;
	} v;
	v.f = value;
	dst[0] = (u8)(v.u & 0xff);
	dst[1] = (u8)((v.u >> 8) & 0xff);
	dst[2] = (u8)((v.u >> 16) & 0xff);
	dst[3] = (u8)((v.u >> 24) & 0xff);
}

static s32 s_binbufAppendFloat(pdanim_binbuf_t *b, f32 value)
{
	u8 bytes[4];
	s_writeLeFloat(bytes, value);
	return s_binbufAppend(b, bytes, sizeof(bytes));
}

static u32 s_animReadBits(const u8 *ptr, u8 remainingbits, s32 bitoffset)
{
	u32 result = 0;
	u32 mask;
	u8 numbitsthisbyte;

	if (remainingbits == 0) {
		return 0;
	}

	ptr += bitoffset / 8;
	bitoffset %= 8;
	numbitsthisbyte = (u8)(8 - bitoffset);

	while (remainingbits >= numbitsthisbyte) {
		remainingbits -= numbitsthisbyte;
		mask = (1u << numbitsthisbyte) - 1u;
		result |= (*ptr & mask) << remainingbits;
		ptr++;
		numbitsthisbyte = 8;
	}

	if (remainingbits > 0) {
		mask = (1u << remainingbits) - 1u;
		result |= (*ptr >> (numbitsthisbyte - remainingbits)) & mask;
	}

	return result;
}

static s32 s_animReadSignedShort(const u8 *ptr, u8 readbitlen, s32 bitoffset)
{
	u16 result = (u16)s_animReadBits(ptr, readbitlen, bitoffset);

	if (readbitlen < 16 && readbitlen > 0
			&& (result & (1 << (readbitlen - 1)))) {
		result |= ((1 << (16 - readbitlen)) - 1) << readbitlen;
	}

	return (s16)result;
}

static f32 s_animReadF32Bits(const u8 *framebytes, s32 bitoffset)
{
	union {
		u32 u;
		f32 f;
	} v;
	v.u = s_animReadBits(framebytes, 32, bitoffset);
	return v.f;
}

static const u8 *s_animSkipPartHeader(const u8 *ptr, u8 flags,
                                      u32 *bits_per_frame)
{
	if (flags & ANIMFIELD_08) {
		*bits_per_frame += ptr[2] + ptr[5] + ptr[8] + ptr[11];
		ptr += 12;
	} else if (flags & ANIMFIELD_S16_TRANSLATE) {
		*bits_per_frame += ptr[2] + ptr[5] + ptr[8];
		ptr += 9;
	} else if (flags & ANIMFIELD_S32_TRANSLATE) {
		*bits_per_frame += ptr[0] + ptr[5] + ptr[10];
		ptr += 15;
	}

	if (flags & ANIMFIELD_S16_ROTATE) {
		*bits_per_frame += ptr[2] + ptr[5] + ptr[8];
		ptr += 9;
	} else if (flags & ANIMFIELD_F32_ROTATE) {
		*bits_per_frame += 96;
	}

	if (flags & ANIMFIELD_CAMERA) {
		*bits_per_frame += ptr[0];
		ptr += 5;
	}

	if (flags & ANIMFIELD_F32_SCALE) {
		*bits_per_frame += 96;
	}

	return ptr;
}

static s32 s_parsePartDescs(const u8 *header, u32 header_len,
                            pdanim_part_desc_t **out_parts,
                            u32 *out_count,
                            pdanim_build_stats_t *stats,
                            char *reason, size_t reason_n)
{
	const u8 *ptr = header;
	const u8 *end = header + header_len;
	pdanim_part_desc_t *parts = NULL;
	u32 count = 0;
	u32 cap = 0;
	u32 bit_offset = 0;

	while (ptr < end) {
		u8 flags = *ptr++;
		if (count == cap) {
			u32 next_cap = cap ? cap * 2 : 32;
			pdanim_part_desc_t *next =
				(pdanim_part_desc_t *)realloc(parts,
					next_cap * sizeof(*parts));
			if (!next) {
				free(parts);
				s_buildReason(reason, reason_n,
					"part descriptor allocation failed at part=%u",
					(unsigned)count);
				return -1;
			}
			parts = next;
			cap = next_cap;
		}

		parts[count].flags = flags;
		parts[count].header = ptr;
		parts[count].bit_offset = bit_offset;
		count++;

		ptr = s_animSkipPartHeader(ptr, flags, &bit_offset);
		if (ptr > end) {
			free(parts);
			s_buildReason(reason, reason_n,
				"part header overflow at part=%u flags=0x%02x offset=%u header_len=%u",
				(unsigned)(count - 1), (unsigned)flags,
				(unsigned)(ptr - header), (unsigned)header_len);
			return -1;
		}

		if (stats) {
			if (flags & ANIMFIELD_08) stats->fields_pos_angle++;
			if (flags & ANIMFIELD_S16_TRANSLATE) stats->fields_s16_translate++;
			if (flags & ANIMFIELD_S32_TRANSLATE) stats->fields_s32_translate++;
			if (flags & ANIMFIELD_S16_ROTATE) stats->fields_s16_rotate++;
			if (flags & ANIMFIELD_F32_ROTATE) stats->fields_f32_rotate++;
			if (flags & ANIMFIELD_CAMERA) stats->fields_camera++;
			if (flags & ANIMFIELD_F32_SCALE) stats->fields_scale++;
		}
	}

	if (count == 0) {
		s_buildReason(reason, reason_n, "no animation part descriptors");
		free(parts);
		return -1;
	}

	if (stats) {
		stats->part_count = count;
		stats->bits_per_frame = bit_offset;
	}

	*out_parts = parts;
	*out_count = count;
	return 0;
}

static s32 s_partHasTranslation(u8 flags)
{
	return !(flags & ANIMFIELD_08)
		&& (flags & (ANIMFIELD_S16_TRANSLATE | ANIMFIELD_S32_TRANSLATE)) != 0;
}

static s32 s_partHasRotation(u8 flags)
{
	return (flags & (ANIMFIELD_S16_ROTATE | ANIMFIELD_F32_ROTATE)) != 0;
}

static s32 s_partHasScale(u8 flags)
{
	return (flags & ANIMFIELD_F32_SCALE) != 0;
}

static void s_decodePartFrame(const pdanim_part_desc_t *part,
                              const u8 *framebytes,
                              u8 framelen,
                              struct coord *rot,
                              struct coord *translate,
                              struct coord *scale)
{
	const u8 *ptr = part->header;
	u8 flags = part->flags;
	s32 bitoffset = (s32)part->bit_offset;
	u8 readbitlen;

	translate->x = translate->y = translate->z = 0.0f;
	rot->x = rot->y = rot->z = 0.0f;
	scale->x = scale->y = scale->z = 1.0f;

	if (flags & ANIMFIELD_S16_TRANSLATE) {
		readbitlen = ptr[2];
		translate->x = (s16)(s_animReadSignedShort(framebytes, readbitlen,
			bitoffset) + (ptr[0] << 8) + ptr[1]);
		bitoffset += readbitlen;

		readbitlen = ptr[5];
		translate->y = (s16)(s_animReadSignedShort(framebytes, readbitlen,
			bitoffset) + (ptr[3] << 8) + ptr[4]);
		bitoffset += readbitlen;

		readbitlen = ptr[8];
		translate->z = (s16)(s_animReadSignedShort(framebytes, readbitlen,
			bitoffset) + (ptr[6] << 8) + ptr[7]);
		bitoffset += readbitlen;
		ptr += 9;
	} else if (flags & ANIMFIELD_S32_TRANSLATE) {
		readbitlen = ptr[0];
		translate->x = (s_animReadBits(framebytes, readbitlen, bitoffset)
			+ ((ptr[1] << 24) + (ptr[2] << 16) + (ptr[3] << 8) + ptr[4]))
			* 0.001f;
		bitoffset += readbitlen;

		readbitlen = ptr[5];
		translate->y = (s_animReadBits(framebytes, readbitlen, bitoffset)
			+ ((ptr[6] << 24) + (ptr[7] << 16) + (ptr[8] << 8) + ptr[9]))
			* 0.001f;
		bitoffset += readbitlen;

		readbitlen = ptr[10];
		translate->z = (s_animReadBits(framebytes, readbitlen, bitoffset)
			+ ((ptr[11] << 24) + (ptr[12] << 16) + (ptr[13] << 8) + ptr[14]))
			* 0.001f;
		bitoffset += readbitlen;
		ptr += 15;
	} else if (flags & ANIMFIELD_08) {
		bitoffset += ptr[2] + ptr[5] + ptr[8] + ptr[11];
		ptr += 12;
	}

	if (flags & ANIMFIELD_S16_ROTATE) {
		u16 introt[3];

		readbitlen = ptr[2];
		introt[0] = (u16)(s_animReadBits(framebytes, readbitlen, bitoffset)
			+ (ptr[0] << 8) + ptr[1]);
		introt[0] <<= 16 - framelen;
		bitoffset += readbitlen;

		readbitlen = ptr[5];
		introt[1] = (u16)(s_animReadBits(framebytes, readbitlen, bitoffset)
			+ (ptr[3] << 8) + ptr[4]);
		introt[1] <<= 16 - framelen;
		bitoffset += readbitlen;

		readbitlen = ptr[8];
		introt[2] = (u16)(s_animReadBits(framebytes, readbitlen, bitoffset)
			+ (ptr[6] << 8) + ptr[7]);
		introt[2] <<= 16 - framelen;
		bitoffset += readbitlen;

		rot->x = introt[0] * M_BADTAU / 65536.0f;
		rot->y = introt[1] * M_BADTAU / 65536.0f;
		rot->z = introt[2] * M_BADTAU / 65536.0f;
		ptr += 9;
	} else if (flags & ANIMFIELD_F32_ROTATE) {
		rot->x = s_animReadF32Bits(framebytes, bitoffset);
		bitoffset += 32;
		rot->y = s_animReadF32Bits(framebytes, bitoffset);
		bitoffset += 32;
		rot->z = s_animReadF32Bits(framebytes, bitoffset);
		bitoffset += 32;
	}

	if (flags & ANIMFIELD_CAMERA) {
		bitoffset += ptr[0];
		ptr += 5;
	}

	if (flags & ANIMFIELD_F32_SCALE) {
		scale->x = s_animReadF32Bits(framebytes, bitoffset);
		bitoffset += 32;
		scale->y = s_animReadF32Bits(framebytes, bitoffset);
		bitoffset += 32;
		scale->z = s_animReadF32Bits(framebytes, bitoffset);
	}
}

static void s_eulerToQuat(f32 x, f32 y, f32 z, f32 *qx, f32 *qy, f32 *qz, f32 *qw)
{
	f32 cx = (f32)cos((double)x * 0.5);
	f32 sx = (f32)sin((double)x * 0.5);
	f32 cy = (f32)cos((double)y * 0.5);
	f32 sy = (f32)sin((double)y * 0.5);
	f32 cz = (f32)cos((double)z * 0.5);
	f32 sz = (f32)sin((double)z * 0.5);

	*qw = cx * cy * cz + sx * sy * sz;
	*qx = sx * cy * cz - cx * sy * sz;
	*qy = cx * sy * cz + sx * cy * sz;
	*qz = cx * cy * sz - sx * sy * cz;
}

static s32 s_base64Encode(const u8 *data, u32 len, pdanim_textbuf_t *out)
{
	static const char alphabet[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	for (u32 i = 0; i < len; i += 3) {
		u32 remain = len - i;
		u32 v = ((u32)data[i] << 16)
			| ((remain > 1 ? data[i + 1] : 0) << 8)
			| (remain > 2 ? data[i + 2] : 0);
		char quad[4];
		quad[0] = alphabet[(v >> 18) & 0x3f];
		quad[1] = alphabet[(v >> 12) & 0x3f];
		quad[2] = remain > 1 ? alphabet[(v >> 6) & 0x3f] : '=';
		quad[3] = remain > 2 ? alphabet[v & 0x3f] : '=';
		if (s_textbufReserve(out, 4) != 0) return -1;
		memcpy(out->data + out->len, quad, 4);
		out->len += 4;
		out->data[out->len] = '\0';
	}
	return 0;
}

static s32 s_buildAnimationGltf(const u8 *anim_data,
                                u32 header_len,
                                u32 frame_count,
                                u32 bytes_per_frame,
                                u32 framelen,
                                u32 anim_flags,
                                const char *catalog_id,
                                pdanim_textbuf_t *out,
                                pdanim_build_stats_t *stats,
                                char *reason, size_t reason_n)
{
	pdanim_part_desc_t *parts = NULL;
	u32 part_count = 0;
	u32 descriptor_header_len = header_len;
	u32 channel_count = 0;
	pdanim_repeat_range_t repeat_ranges[PDANIM_MAX_TAIL_VALUES];
	s16 cut_skip_frames[PDANIM_MAX_TAIL_VALUES];
	u32 repeat_range_count = 0;
	u32 cut_skip_count = 0;
	pdanim_binbuf_t bin;
	u32 time_offset;
	u32 *view_offsets = NULL;
	u32 *view_lengths = NULL;
	u32 *accessor_counts = NULL;
	const char **accessor_types = NULL;
	s32 *channel_parts = NULL;
	const char **channel_paths = NULL;
	u32 view_count = 0;
	u32 channel_index = 0;
	pdanim_textbuf_t b64;

	memset(&bin, 0, sizeof(bin));
	memset(&b64, 0, sizeof(b64));
	memset(repeat_ranges, 0, sizeof(repeat_ranges));
	memset(cut_skip_frames, 0, sizeof(cut_skip_frames));
	if (stats) memset(stats, 0, sizeof(*stats));
	s_buildReason(reason, reason_n, "unknown GLTF build failure");

	descriptor_header_len = s_animDescriptorHeaderLen(anim_data, header_len,
		anim_flags, repeat_ranges, &repeat_range_count,
		cut_skip_frames, &cut_skip_count, reason, reason_n);
	if (stats) {
		stats->descriptor_header_len = descriptor_header_len;
		stats->anim_flags = anim_flags;
		stats->repeat_range_count = repeat_range_count;
		stats->cut_skip_count = cut_skip_count;
	}

	if (s_parsePartDescs(anim_data, descriptor_header_len, &parts, &part_count,
			stats, reason, reason_n) != 0) {
		return -1;
	}

	if (stats && stats->bits_per_frame > bytes_per_frame * 8u) {
		s_buildReason(reason, reason_n,
			"decoded frame bits exceed table bytes: bits=%u table_bits=%u",
			(unsigned)stats->bits_per_frame,
			(unsigned)(bytes_per_frame * 8u));
		goto fail;
	}

	for (u32 p = 0; p < part_count; p++) {
		channel_count += s_partHasTranslation(parts[p].flags);
		channel_count += s_partHasRotation(parts[p].flags);
		channel_count += s_partHasScale(parts[p].flags);
	}
	if (stats) stats->channel_count = channel_count;

	view_count = 1 + channel_count;
	view_offsets = (u32 *)calloc(view_count, sizeof(u32));
	view_lengths = (u32 *)calloc(view_count, sizeof(u32));
	accessor_counts = (u32 *)calloc(view_count, sizeof(u32));
	accessor_types = (const char **)calloc(view_count, sizeof(char *));
	channel_parts = (s32 *)calloc(channel_count ? channel_count : 1,
		sizeof(s32));
	channel_paths = (const char **)calloc(channel_count ? channel_count : 1,
		sizeof(char *));
	if (!view_offsets || !view_lengths || !accessor_counts || !accessor_types
			|| !channel_parts || !channel_paths) {
		s_buildReason(reason, reason_n, "GLTF channel allocation failed");
		goto fail;
	}

	time_offset = bin.len;
	for (u32 f = 0; f < frame_count; f++) {
		if (s_binbufAppendFloat(&bin, (f32)f) != 0) goto fail;
	}
	view_offsets[0] = time_offset;
	view_lengths[0] = bin.len - time_offset;
	accessor_counts[0] = frame_count;
	accessor_types[0] = "SCALAR";

	for (u32 part = 0; part < part_count; part++) {
		u8 flags = parts[part].flags;
		const char *paths[3] = { "translation", "rotation", "scale" };
		s32 enabled[3] = {
			s_partHasTranslation(flags),
			s_partHasRotation(flags),
			s_partHasScale(flags),
		};

		for (u32 kind = 0; kind < 3; kind++) {
			u32 offset;
			if (!enabled[kind]) {
				continue;
			}
			offset = bin.len;
			for (u32 frame = 0; frame < frame_count; frame++) {
				const u8 *framebytes = anim_data + header_len
					+ frame * bytes_per_frame;
				struct coord rot;
				struct coord translate;
				struct coord scale;
				s_decodePartFrame(&parts[part], framebytes, (u8)framelen,
					&rot, &translate, &scale);
				if (kind == 0) {
					if (s_binbufAppendFloat(&bin, translate.x) != 0 ||
							s_binbufAppendFloat(&bin, translate.y) != 0 ||
							s_binbufAppendFloat(&bin, translate.z) != 0) {
						goto fail;
					}
				} else if (kind == 1) {
					f32 qx, qy, qz, qw;
					s_eulerToQuat(rot.x, rot.y, rot.z, &qx, &qy, &qz, &qw);
					if (s_binbufAppendFloat(&bin, qx) != 0 ||
							s_binbufAppendFloat(&bin, qy) != 0 ||
							s_binbufAppendFloat(&bin, qz) != 0 ||
							s_binbufAppendFloat(&bin, qw) != 0) {
						goto fail;
					}
				} else {
					if (s_binbufAppendFloat(&bin, scale.x) != 0 ||
							s_binbufAppendFloat(&bin, scale.y) != 0 ||
							s_binbufAppendFloat(&bin, scale.z) != 0) {
						goto fail;
					}
				}
			}

			channel_index++;
			view_offsets[channel_index] = offset;
			view_lengths[channel_index] = bin.len - offset;
			accessor_counts[channel_index] = frame_count;
			accessor_types[channel_index] = kind == 1 ? "VEC4" : "VEC3";
			channel_parts[channel_index - 1] = (s32)part;
			channel_paths[channel_index - 1] = paths[kind];
		}
	}

	if (stats) stats->bin_len = bin.len;
	if (s_base64Encode(bin.data, bin.len, &b64) != 0) {
		s_buildReason(reason, reason_n,
			"base64 encode failed for bin_len=%u", (unsigned)bin.len);
		goto fail;
	}
	if (stats) stats->b64_len = b64.len;

	if (s_textbufAppend(out, "{\n") != 0) {
		s_buildReason(reason, reason_n, "GLTF text append failed at asset open");
		goto fail;
	}
	if (s_textbufAppend(out, "  \"asset\": { \"version\": \"2.0\", \"generator\": \"Perfect Dark 2 pdanim_chr semantic extractor v3\" },\n") != 0) {
		s_buildReason(reason, reason_n, "GLTF text append failed at asset metadata");
		goto fail;
	}
	if (s_textbufAppend(out, "  \"nodes\": [\n") != 0) {
		s_buildReason(reason, reason_n, "GLTF text append failed at nodes open");
		goto fail;
	}
	for (u32 p = 0; p < part_count; p++) {
		if (s_textbufAppendf(out,
				"    { \"name\": \"part_%03u\" }%s\n",
				(unsigned)p, p + 1 < part_count ? "," : "") != 0) {
			goto fail;
		}
	}
	if (s_textbufAppend(out, "  ],\n") != 0) goto fail;
	if (s_textbufAppendf(out,
			"  \"buffers\": [ { \"uri\": \"data:application/octet-stream;base64,%s\", \"byteLength\": %u } ],\n",
			b64.data ? b64.data : "", (unsigned)bin.len) != 0) {
		s_buildReason(reason, reason_n,
			"GLTF text append failed at embedded buffer bin_len=%u b64_len=%u",
			(unsigned)bin.len, (unsigned)b64.len);
		goto fail;
	}
	if (s_textbufAppend(out, "  \"bufferViews\": [\n") != 0) goto fail;
	for (u32 i = 0; i < view_count; i++) {
		if (s_textbufAppendf(out,
				"    { \"buffer\": 0, \"byteOffset\": %u, \"byteLength\": %u }%s\n",
				(unsigned)view_offsets[i], (unsigned)view_lengths[i],
				i + 1 < view_count ? "," : "") != 0) {
			goto fail;
		}
	}
	if (s_textbufAppend(out, "  ],\n") != 0) goto fail;
	if (s_textbufAppend(out, "  \"accessors\": [\n") != 0) goto fail;
	for (u32 i = 0; i < view_count; i++) {
		if (s_textbufAppendf(out,
				"    { \"bufferView\": %u, \"componentType\": 5126, \"count\": %u, \"type\": \"%s\" }%s\n",
				(unsigned)i, (unsigned)accessor_counts[i], accessor_types[i],
				i + 1 < view_count ? "," : "") != 0) {
			goto fail;
		}
	}
	if (s_textbufAppend(out, "  ],\n") != 0) goto fail;
	if (s_textbufAppendf(out,
			"  \"animations\": [ { \"name\": \"%s\", \"samplers\": [\n",
			catalog_id) != 0) goto fail;
	for (u32 i = 0; i < channel_count; i++) {
		if (s_textbufAppendf(out,
				"      { \"input\": 0, \"output\": %u, \"interpolation\": \"LINEAR\" }%s\n",
				(unsigned)(i + 1), i + 1 < channel_count ? "," : "") != 0) {
			goto fail;
		}
	}
	if (s_textbufAppend(out, "    ], \"channels\": [\n") != 0) goto fail;
	for (u32 i = 0; i < channel_count; i++) {
		if (s_textbufAppendf(out,
				"      { \"sampler\": %u, \"target\": { \"node\": %d, \"path\": \"%s\" } }%s\n",
				(unsigned)i, channel_parts[i], channel_paths[i],
				i + 1 < channel_count ? "," : "") != 0) {
			goto fail;
		}
	}
	if (s_textbufAppend(out, "    ] } ],\n") != 0) goto fail;
	if (s_textbufAppendf(out,
			"  \"extras\": { \"pd_kind\": \"animation\", \"pd_category\": \"character_animation\", \"pd_part_count\": %u, \"pd_channel_count\": %u, \"pd_anim_flags\": %u, \"pd_repeat_ranges\": [",
			(unsigned)part_count, (unsigned)channel_count,
			(unsigned)anim_flags) != 0) goto fail;
	for (u32 i = 0; i < repeat_range_count; i++) {
		u32 src = repeat_range_count - 1 - i;
		if (s_textbufAppendf(out,
				"%s{ \"repeat_to_frame\": %d, \"repeat_from_frame\": %d }",
				i ? ", " : "",
				(int)repeat_ranges[src].repeattoframe,
				(int)repeat_ranges[src].repeatfromframe) != 0) {
			goto fail;
		}
	}
	if (s_textbufAppend(out, "], \"pd_cut_skip_frames\": [") != 0) goto fail;
	for (u32 i = 0; i < cut_skip_count; i++) {
		u32 src = cut_skip_count - 1 - i;
		if (s_textbufAppendf(out, "%s%d", i ? ", " : "",
				(int)cut_skip_frames[src]) != 0) {
			goto fail;
		}
	}
	if (s_textbufAppend(out, "] }\n") != 0) goto fail;
	if (s_textbufAppend(out, "}\n") != 0) goto fail;

	free(parts);
	free(view_offsets);
	free(view_lengths);
	free(accessor_counts);
	free(accessor_types);
	free(channel_parts);
	free(channel_paths);
	s_binbufFree(&bin);
	s_textbufFree(&b64);
	return 0;

fail:
	free(parts);
	free(view_offsets);
	free(view_lengths);
	free(accessor_counts);
	free(accessor_types);
	free(channel_parts);
	free(channel_paths);
	s_binbufFree(&bin);
	s_textbufFree(&b64);
	return -1;
}

/* Emit one .pdanim ZIP compound. Returns 1 written, 0 skipped, -1 failed. */
static s32 s_emitOneChrAnim(s32 anim_idx,
                             const struct animtableentry *entry,
                             const u8 *seg_data, u32 seg_size,
                             const char *out_dir, s32 force_rewrite)
{
	/* mod-override marker: preprocessAnimations sets entry->data to
	 * 0xffffffff if a mod has hooked the slot via
	 * modAnimationLoadDescriptor. Mod scan runs LATER than this
	 * emitter in the boot path, so we should never see this state
	 * at extract time -- defensive bail with a warning. */
	if (entry->data == 0xffffffff) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdanim_chr: anim_idx=%d marked mod-override "
			"(data=0xffffffff); skipping -- boot order regression?",
			anim_idx);
		return 0;
	}

	/* Empty slot: numframes==0 AND headerlen==0 means the table entry
	 * is unused. Some legacy ROMs reserve table space for animations
	 * that never got authored. Skip silently. */
	if (entry->numframes == 0 && entry->headerlen == 0) {
		return 0;
	}

	/* Compute the per-anim byte span and validate it fits inside the
	 * frame-data region of the segment (everything before the table
	 * tail). If not, the table is corrupt or our layout assumption
	 * is wrong -- LOUDFAIL with diagnostic detail. */
	const u32 frame_region_size = (seg_size > PDANIM_CHR_TABLE_TAIL_BYTES)
		? (seg_size - PDANIM_CHR_TABLE_TAIL_BYTES) : 0;
	const u32 anim_bytes = (u32)entry->headerlen
	                     + (u32)entry->numframes * (u32)entry->bytesperframe;

	if (entry->data + anim_bytes > frame_region_size) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"anim_idx=%d byte range overflow: data=0x%x + "
			"len=0x%x > frame_region=0x%x (numframes=%u "
			"bytesperframe=%u headerlen=%u)",
			anim_idx, (unsigned)entry->data, (unsigned)anim_bytes,
			(unsigned)frame_region_size, (unsigned)entry->numframes,
			(unsigned)entry->bytesperframe, (unsigned)entry->headerlen);
		return -1;
	}

	char catalog_id[128];
	(void)s_buildCatalogId(anim_idx, catalog_id, sizeof(catalog_id));

	/* Filename: catalog_id with ':' -> '_'. */
	char filename_slug[128];
	{
		size_t i, j = 0;
		for (i = 0; catalog_id[i] && j + 1 < sizeof(filename_slug); i++) {
			filename_slug[j++] = (catalog_id[i] == ':') ? '_' : catalog_id[i];
		}
		filename_slug[j] = '\0';
	}

	char dst_rel[FS_MAXPATH];
	snprintf(dst_rel, sizeof(dst_rel), "%s/%s.pdanim", out_dir, filename_slug);

	if (!force_rewrite && fsFileSize(dst_rel) > 0 &&
	    s_existingArchiveHasAnimPayloads(dst_rel)) return 0;

	pdanim_textbuf_t animation_gltf;
	pdanim_build_stats_t build_stats;
	char build_reason[192];
	memset(&animation_gltf, 0, sizeof(animation_gltf));
	memset(&build_stats, 0, sizeof(build_stats));
	build_reason[0] = '\0';
	if (s_buildAnimationGltf(seg_data + entry->data,
	                     (u32)entry->headerlen,
	                     (u32)entry->numframes,
	                     (u32)entry->bytesperframe,
	                     (u32)entry->framelen,
	                     (u32)entry->flags,
	                     catalog_id,
	                     &animation_gltf,
	                     &build_stats,
	                     build_reason, sizeof(build_reason)) != 0) {
		s_textbufFree(&animation_gltf);
		const char *sym = loaderEnumNameForAnimEnum(anim_idx);
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"failed to build GLTF animation payload for anim_idx=%d "
			"sym=%s reason=\"%s\" frames=%u bytesperframe=%u "
			"headerlen=%u descriptor_header_len=%u framelen=%u "
			"anim_flags=0x%02x repeat_ranges=%u cut_skips=%u "
			"part_count=%u channel_count=%u bits_per_frame=%u "
			"table_bits=%u fields={pos_angle:%u,"
			"s16_t:%u,s32_t:%u,s16_r:%u,f32_r:%u,camera:%u,scale:%u} "
			"bin_len=%u b64_len=%u",
			anim_idx, sym ? sym : "(unnamed)",
			build_reason[0] ? build_reason : "unknown",
			(unsigned)entry->numframes, (unsigned)entry->bytesperframe,
			(unsigned)entry->headerlen,
			(unsigned)build_stats.descriptor_header_len,
			(unsigned)entry->framelen, (unsigned)entry->flags,
			(unsigned)build_stats.repeat_range_count,
			(unsigned)build_stats.cut_skip_count,
			(unsigned)build_stats.part_count,
			(unsigned)build_stats.channel_count,
			(unsigned)build_stats.bits_per_frame,
			(unsigned)((u32)entry->bytesperframe * 8u),
			(unsigned)build_stats.fields_pos_angle,
			(unsigned)build_stats.fields_s16_translate,
			(unsigned)build_stats.fields_s32_translate,
			(unsigned)build_stats.fields_s16_rotate,
			(unsigned)build_stats.fields_f32_rotate,
			(unsigned)build_stats.fields_camera,
			(unsigned)build_stats.fields_scale,
			(unsigned)build_stats.bin_len,
			(unsigned)build_stats.b64_len);
		return -1;
	}

	/* Build _meta/manifest.json text in memory. Schema fields per Section 2.6
	 * + provenance hints (source_offset, source_index) for the parity
	 * check and Step 4 round-trip. */
	const char *sym = loaderEnumNameForAnimEnum(anim_idx);
	char manifest_buf[1152];
	int manifest_len = snprintf(manifest_buf, sizeof(manifest_buf),
		"{\n"
		"  \"pd_kind\": \"animation\",\n"
		"  \"pd_schema_version\": %d,\n"
		"  \"id\": \"%s\",\n"
		"  \"category\": \"character_animation\",\n"
		"  \"animation\": \"animation.gltf\",\n"
		"  \"runtime_source\": \"animation.gltf\",\n"
		"  \"frame_count\": %u,\n"
		"  \"bytes_per_frame\": %u,\n"
		"  \"header_len\": %u,\n"
		"  \"animation_size\": %u,\n"
		"  \"framelen\": %u,\n"
		"  \"flags\": %u,\n"
		"  \"source_index\": %d,\n"
		"  \"source_offset\": %u,\n"
		"  \"source_symbol\": \"%s\"\n"
		"}\n",
		PDANIM_CHR_SCHEMA_VERSION,
		catalog_id,
		(unsigned)entry->numframes,
		(unsigned)entry->bytesperframe,
		(unsigned)entry->headerlen,
		(unsigned)animation_gltf.len,
		(unsigned)entry->framelen,
		(unsigned)entry->flags,
		anim_idx,
		(unsigned)entry->data,
		sym ? sym : "");
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		s_textbufFree(&animation_gltf);
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"manifest.json snprintf truncated for anim_idx=%d", anim_idx);
		return -1;
	}

	char ini_buf[1024];
	int ini_len = snprintf(ini_buf, sizeof(ini_buf),
		"[animation]\n"
		"catalog_id = %s\n"
		"category = character_animation\n"
		"animation_file = animation.gltf\n"
		"frame_count = %u\n"
		"bytes_per_frame = %u\n"
		"header_len = %u\n"
		"animation_size = %u\n"
		"framelen = %u\n"
		"flags = %u\n"
		"source_index = %d\n"
		"source_offset = %u\n"
		"source_symbol = %s\n",
		catalog_id,
		(unsigned)entry->numframes,
		(unsigned)entry->bytesperframe,
		(unsigned)entry->headerlen,
		(unsigned)animation_gltf.len,
		(unsigned)entry->framelen,
		(unsigned)entry->flags,
		anim_idx,
		(unsigned)entry->data,
		sym ? sym : "");
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini_buf)) {
		s_textbufFree(&animation_gltf);
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"animation.ini snprintf truncated for anim_idx=%d", anim_idx);
		return -1;
	}

	char dst_full_buf[FS_MAXPATH + 1];
	const char *dst_full = fsFullPath(dst_rel, dst_full_buf, sizeof(dst_full_buf));
	if (!dst_full || !dst_full[0]) {
		s_textbufFree(&animation_gltf);
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"fsFullPath failed for \"%s\"", dst_rel);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(dst_full);
	if (!aw) {
		s_textbufFree(&animation_gltf);
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"modArchiveBegin failed for \"%s\"", dst_full);
		return -1;
	}

	asset_archive_writer_t asset_writer;
	if (assetArchiveWriterInit(&asset_writer, aw, "animation",
			catalog_id) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"assetArchiveWriterInit failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_textbufFree(&animation_gltf);
		return -1;
	}
	assetArchiveWriterSetProvenance(&asset_writer, "romextract_pdanim_chr",
		"animations", anim_idx, sym ? sym : "");

	if (assetArchiveWriterAddDescriptor(&asset_writer, "animation.ini",
			ini_buf, (u32)ini_len) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"AddFileMem animation.ini failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_textbufFree(&animation_gltf);
		return -1;
	}
	if (assetArchiveWriterAddManifestJson(&asset_writer,
			manifest_buf, (u32)manifest_len) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"AddFileMem _meta/manifest.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_textbufFree(&animation_gltf);
		return -1;
	}

	if (assetArchiveWriterAddPublicMem(&asset_writer, "animation.gltf",
			animation_gltf.data, animation_gltf.len,
			"animation") != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"AddFileMem animation.gltf failed for \"%s\" "
			"(anim_idx=%d len=%u)",
			dst_full, anim_idx, (unsigned)animation_gltf.len);
		modArchiveAbort(aw);
		s_textbufFree(&animation_gltf);
		return -1;
	}

	if (assetArchiveWriterFinishMetadata(&asset_writer) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"assetArchiveWriterFinishMetadata failed for \"%s\" "
			"(anim_idx=%d)", dst_full, anim_idx);
		modArchiveAbort(aw);
		s_textbufFree(&animation_gltf);
		return -1;
	}

	if (modArchiveFinish(aw) != 0) {
		s_textbufFree(&animation_gltf);
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"modArchiveFinish failed for \"%s\"", dst_full);
		return -1;
	}

	s_textbufFree(&animation_gltf);
	return 1;
}

/* Engine Phase 4: file-scope fan-out context for chr animations. */
typedef struct {
	const struct animtableentry *entries;
	const u8                    *seg_data;
	u32                          seg_size;
	const char                  *anims_dir;
	s32                          force_rewrite;
	s32                          count;
	SDL_atomic_t                 written;
	SDL_atomic_t                 skipped;
	SDL_atomic_t                 failed;
	SDL_atomic_t                 named;
	SDL_atomic_t                 processed;
} pdanim_chr_fanout_ctx_t;

static void s_pdanimChrWork(int i, void *user)
{
	pdanim_chr_fanout_ctx_t *c = (pdanim_chr_fanout_ctx_t *)user;
	if (i < 0 || i >= c->count) return;

	s32 r = s_emitOneChrAnim(i, &c->entries[i], c->seg_data, c->seg_size,
	                         c->anims_dir, c->force_rewrite);
	if (r > 0)       SDL_AtomicAdd(&c->written, 1);
	else if (r == 0) SDL_AtomicAdd(&c->skipped, 1);
	else             SDL_AtomicAdd(&c->failed,  1);

	if (loaderEnumNameForAnimEnum(i)) SDL_AtomicAdd(&c->named, 1);

	int done = SDL_AtomicAdd(&c->processed, 1) + 1;
	if ((done & 0x3f) == 0 || done == c->count) {
		bootProgressUpdate(done, c->count);
	}
}

s32 romExtractAllPdanimChr(s32 force_rewrite)
{
	const u8 *seg_data = NULL;
	u32 seg_size = 0;

	if (!s_findAnimSegment(&seg_data, &seg_size)) {
		sysLogPrintf(LOG_NOTE,
			"romextract pdanim_chr: \"animations\" segment not loaded "
			"(server build or pre-romdata-init); skipping");
		return 0;
	}

	if (seg_size <= PDANIM_CHR_TABLE_TAIL_BYTES) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"animations segment too small (size=%u, expected > 0x%x)",
			(unsigned)seg_size, PDANIM_CHR_TABLE_TAIL_BYTES);
		return -1;
	}

	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDANIM_CHR", "fsDataDirEnsure failed");
		return -1;
	}

	char dataDirBuf[FS_MAXPATH + 1];
	char anims_dir[FS_MAXPATH];
	snprintf(anims_dir, sizeof(anims_dir), "%s/animations",
		fsDataDir(dataDirBuf, sizeof(dataDirBuf)));
	if (!fsCreateDir(anims_dir)) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"fsCreateDir(\"%s\") failed", anims_dir);
		return -1;
	}

	/* Parse the table tail. preprocessAnimations already byte-swapped
	 * the count + entry fields, so direct native-endian read is safe. */
	const u8 *table_base = seg_data + (seg_size - PDANIM_CHR_TABLE_TAIL_BYTES);
	const u32 *table_u32 = (const u32 *)table_base;
	const u32 anim_count = table_u32[0];
	const struct animtableentry *entries =
		(const struct animtableentry *)&table_u32[1];

	/* Sanity: 1208 entries is the upper bound the 0x38a0-byte tail can
	 * hold (including the u32 count prefix at offset 0). Table sizes
	 * less than this are normal across ROM versions. */
	const u32 max_entries =
		(PDANIM_CHR_TABLE_TAIL_BYTES - sizeof(u32)) / sizeof(struct animtableentry);
	if (anim_count > max_entries) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"anim_count=%u exceeds table capacity=%u "
			"(table_tail=0x%x bytes)",
			(unsigned)anim_count, (unsigned)max_entries,
			PDANIM_CHR_TABLE_TAIL_BYTES);
		return -1;
	}

	pdanim_chr_fanout_ctx_t cctx;
	memset(&cctx, 0, sizeof(cctx));
	cctx.entries       = entries;
	cctx.seg_data      = seg_data;
	cctx.seg_size      = seg_size;
	cctx.anims_dir     = anims_dir;
	cctx.force_rewrite = force_rewrite;
	cctx.count         = (s32)anim_count;
	SDL_AtomicSet(&cctx.written,   0);
	SDL_AtomicSet(&cctx.skipped,   0);
	SDL_AtomicSet(&cctx.failed,    0);
	SDL_AtomicSet(&cctx.named,     0);
	SDL_AtomicSet(&cctx.processed, 0);

	bootProgressUpdate(0, (s32)anim_count);
	bootPoolForRangeBlocking(0, (int)anim_count, s_pdanimChrWork, &cctx);
	bootProgressUpdate((s32)anim_count, (s32)anim_count);

	s32 written = SDL_AtomicGet(&cctx.written);
	s32 skipped = SDL_AtomicGet(&cctx.skipped);
	s32 failed  = SDL_AtomicGet(&cctx.failed);
	s32 named   = SDL_AtomicGet(&cctx.named);

	sysLogPrintf(LOG_NOTE,
		"romextract pdanim_chr: written=%d skipped=%d failed=%d "
		"total=%u named=%d (anim_chr lump emitted to %s)",
		written, skipped, failed, (unsigned)anim_count, named, anims_dir);

	return written;
}
