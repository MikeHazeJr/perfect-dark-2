#include <ultra64.h>
#include <string.h>
#include "constants.h"
#include "system.h" /* c3849 Wave 2: sysLogPrintf */
#include "game/prop.h"
#include "game/game_1531a0.h"
#include "game/bg.h"
#include "bss.h"
#include "lib/dma.h"
#include "lib/memp.h"
#include "lib/mtx.h"
#include "lib/anim.h"
#include "lib/lib_2f490.h"
#include "lib/libc/ll.h"
#include "data.h"
#include "types.h"
#include "mod.h"
#include "assetcatalog_anim_slots.h" /* c3849 Wave 2: ANIM_CUSTOM_COUNT */
#include "assetcatalog_load.h"       /* c3849 Wave 2: catalogSeedCustomAnimRows */

u8 *g_AnimFrameByteSlots;
u8 **g_AnimFrameBytes;
s16 *g_AnimFrameAnimNums;
s16 *g_AnimFrameFrameNums;
u8 *g_AnimFrameBirths;
u8 *g_AnimHeaderByteSlots;
u8 **g_AnimHeaderBytes;
s16 *g_AnimHeaderAnimNums;
s32 *g_AnimHeaderBirths;
s16 g_NumRomAnimations;
struct animtableentry *g_RomAnims;

u32 g_NextAnimFrameIndex = 0;
s32 g_NextAnimHeaderIndex = 0;
s16 g_NumAnimations = 0;
struct animtableentry *g_Anims = NULL;
u8 *g_AnimToHeaderSlot = NULL;
s16 *var8005f014 = NULL;
s32 g_AnimMaxBytesPerFrame = 176;
s32 g_AnimMaxHeaderLength = 608;
bool g_AnimHostEnabled = false;
u8 *g_AnimHostSegment = NULL;
static s16 s_AnimLastRejectedAnimnum = -1;
static s16 s_AnimLastRejectedFrame = -1;

u8 **g_AnimReplacements;

extern u8 EXT_SEG _animationsTableRomStart;
extern u8 EXT_SEG _animationsTableRomEnd;

void animsInit(void)
{
	s32 i;
	s32 total;
	u32 *ptr;
	u32 tablelen = ALIGN64(REF_SEG _animationsTableRomEnd - REF_SEG _animationsTableRomStart);

	/* c3849 Wave 2: grow the table by the custom-anim slot range. Base rows
	 * copy unchanged; the custom tail is zero-init and later seeded from
	 * catalog metadata (catalogSeedCustomAnimRows). */
	ptr = mempAlloc(ALIGN64(tablelen + ANIM_CUSTOM_COUNT * sizeof(struct animtableentry)),
		MEMPOOL_PERMANENT);
	dmaExec(ptr, (romptr_t) REF_SEG _animationsTableRomStart, tablelen);

	g_NumAnimations = g_NumRomAnimations = ptr[0];
	g_Anims = g_RomAnims = (struct animtableentry *)&ptr[1];

	if (g_NumRomAnimations != ANIM_CUSTOM_START) {
		sysLogPrintf(LOG_WARNING,
			"ANIM.CUSTOM: ROM anim count %d != ANIM_END %d; custom slots may collide",
			g_NumRomAnimations, (s32)ANIM_CUSTOM_START);
	}
	total = g_NumAnimations + ANIM_CUSTOM_COUNT;
	memset(&g_Anims[g_NumAnimations], 0,
		ANIM_CUSTOM_COUNT * sizeof(struct animtableentry));

	g_AnimMaxHeaderLength = 1;
	g_AnimMaxBytesPerFrame = 1;

	for (i = 0; i < g_NumAnimations; i++) {
		if (g_Anims[i].headerlen > g_AnimMaxHeaderLength) {
			g_AnimMaxHeaderLength = g_Anims[i].headerlen;
		}

		if (g_Anims[i].bytesperframe > g_AnimMaxBytesPerFrame) {
			g_AnimMaxBytesPerFrame = g_Anims[i].bytesperframe;
		}
	}

	g_AnimMaxHeaderLength = ALIGN16(g_AnimMaxHeaderLength + 34);
	g_AnimMaxBytesPerFrame = ALIGN16(g_AnimMaxBytesPerFrame + 34);

	g_AnimToHeaderSlot    = mempAlloc(ALIGN64(total), MEMPOOL_PERMANENT);
	var8005f014           = mempAlloc(ALIGN64(total * sizeof(*var8005f014)), MEMPOOL_PERMANENT);
	g_AnimFrameByteSlots  = mempAlloc(ALIGN64(ANIM_FRAME_CACHE_SIZE * g_AnimMaxBytesPerFrame), MEMPOOL_PERMANENT);
	g_AnimFrameBytes      = mempAlloc(ALIGN64(ANIM_FRAME_CACHE_SIZE * sizeof(*g_AnimFrameBytes)), MEMPOOL_PERMANENT);
	g_AnimFrameAnimNums   = mempAlloc(ALIGN64(ANIM_FRAME_CACHE_SIZE * sizeof(*g_AnimFrameAnimNums)), MEMPOOL_PERMANENT);
	g_AnimFrameFrameNums  = mempAlloc(ALIGN64(ANIM_FRAME_CACHE_SIZE * sizeof(*g_AnimFrameFrameNums)), MEMPOOL_PERMANENT);
	g_AnimFrameBirths     = mempAlloc(ALIGN64(ANIM_FRAME_CACHE_SIZE * sizeof(*g_AnimFrameBirths)), MEMPOOL_PERMANENT);
	g_AnimHeaderByteSlots = mempAlloc(ALIGN64(ANIM_HEADER_CACHE_SIZE * g_AnimMaxHeaderLength), MEMPOOL_PERMANENT);
	g_AnimHeaderBytes     = mempAlloc(ALIGN64(ANIM_HEADER_CACHE_SIZE * sizeof(*g_AnimHeaderBytes)), MEMPOOL_PERMANENT);
	g_AnimHeaderAnimNums  = mempAlloc(ALIGN64(ANIM_HEADER_CACHE_SIZE * sizeof(*g_AnimHeaderAnimNums)), MEMPOOL_PERMANENT);
	g_AnimHeaderBirths    = mempAlloc(ALIGN64(ANIM_HEADER_CACHE_SIZE * sizeof(*g_AnimHeaderBirths)), MEMPOOL_PERMANENT);
	g_AnimReplacements    = mempAlloc(ALIGN64(total * sizeof(u8 *)), MEMPOOL_PERMANENT);
	bzero(g_AnimReplacements, total * sizeof(u8 *));

	animsInitTables();

	g_AnimHostSegment = NULL;
	g_AnimHostEnabled = false;

	/* c3849 Wave 2: seed catalog-registered custom anim rows (boot path;
	 * catalogLoadInit ran before g_Anims existed). */
	catalogSeedCustomAnimRows();
}

void animsInitTables(void)
{
	s32 i;

	/* c3849 Wave 2: cover the custom slot range too. */
	for (i = 0; i < g_NumAnimations + ANIM_CUSTOM_COUNT; i++) {
		g_AnimToHeaderSlot[i] = 0xff;
		var8005f014[i] = 0;
	}

	for (i = 0; i < ANIM_FRAME_CACHE_SIZE; i++) {
		g_AnimFrameAnimNums[i] = 0;
		g_AnimFrameFrameNums[i] = 0;
		g_AnimFrameBirths[i] = 0;
	}

	for (i = 0; i < ANIM_HEADER_CACHE_SIZE; i++) {
		g_AnimHeaderAnimNums[i] = 0;
		g_AnimHeaderBirths[i] = -2;
	}
}

void animsReset(void)
{
	g_NumAnimations = g_NumRomAnimations;
	g_Anims = g_RomAnims;
	g_AnimHostEnabled = false;
}

s32 animGetNumFrames(s16 animnum)
{
	return g_Anims[animnum].numframes;
}

bool animHasFrames(s16 animnum)
{
	return animnum < animGetTotalCount() && g_Anims[animnum].numframes > 0;
}

s32 animGetNumAnimations(void)
{
	return g_NumAnimations;
}

/* c3849 Wave 2: base count + the catalog-owned custom slot range. Bounds for
 * code that must accept custom animnums (clip install, playback gates); the
 * debug anim cycler and LRU byte-slot sizing stay on the base count. */
s32 animGetTotalCount(void)
{
	return g_NumAnimations + ANIM_CUSTOM_COUNT;
}

extern u8 EXT_SEG _animationsSegmentRomStart;

u8 *animDma(u8 *dst, u32 segoffset, u32 len)
{
	if (g_AnimHostEnabled) {
		bcopy(&g_AnimHostSegment[segoffset], dst, len);
		return dst;
	}

	return dmaExecWithAutoAlign(dst, (romptr_t) REF_SEG _animationsSegmentRomStart + segoffset, len);
}

/**
 * Return -1 if the given apparent frame is a repeat frame, or if not a repeat
 * frame then remap the apparent frame to a real one and return it.
 *
 * The end of the header can contain a sequence of shorts such as:
 * -1, 55, 30
 *
 * The values are iterated backwards in pairs of 2 and are terminated by -1.
 *
 * In each pair, the right value is the repeatfromframe and the left value is
 * the repeattoframe. In the above example, apparent frames 30 to 55 are
 * repeated, so the remapping looks like:
 * 29 -> 29
 * 30 -> -1
 * ...
 * 55 -> -1
 * 56 -> 30
 * 57 -> 31
 */
s32 animGetRemappedFrame(s16 animnum, s32 apparentframe)
{
	u8 *ptr = (u8 *)(g_AnimHeaderBytes[g_AnimToHeaderSlot[animnum]] + g_Anims[animnum].headerlen - 2);
	s32 realframe = apparentframe;

	while (true) {
		s16 repeatfromframe = ptr[0] << 8 | ptr[1];
		s16 repeattoframe;

		if (repeatfromframe < 0) {
			break;
		}

		repeattoframe = ptr[-2] << 8 | ptr[-1];
		ptr -= 4;

		if (repeatfromframe <= apparentframe) {
			if (repeattoframe < apparentframe) {
				realframe = realframe - repeattoframe + repeatfromframe - 1;
			} else {
				realframe = -1;
				break;
			}
		}
	}

	return realframe;
}

/**
 * Similar to the above, but with the following differences:
 * - Write the remapped frame to the frameptr pointer instead of returning it.
 * - If the apparent frame is a repeat, write the original frame rather than -1.
 * - Return true if the frame is original or false if it's a repeat.
 */
bool animRemapFrameForLoad(s16 animnum, s32 apparentframe, s32 *frameptr)
{
	u8 *ptr = (u8 *)(g_AnimHeaderBytes[g_AnimToHeaderSlot[animnum]] + g_Anims[animnum].headerlen - 2);
	s32 result = apparentframe;
	bool ret = true;

	while (true) {
		s16 repeatfromframe = ptr[0] << 8 | ptr[1];
		s16 repeattoframe;

		if (repeatfromframe < 0) {
			break;
		}

		repeattoframe = ptr[-2] << 8 | ptr[-1];
		ptr -= 4;

		if (repeatfromframe <= apparentframe) {
			if (repeattoframe < apparentframe) {
				result = result - repeattoframe + repeatfromframe - 1;
			} else {
				result = result - apparentframe + repeatfromframe;
				ret = false;
				break;
			}
		}
	}

	*frameptr = result;

	return ret;
}

/**
 * Return true if the given animation and frame should be skipped.
 *
 * Used by cutscenes.
 *
 * The skip frame numbers are stored at the tail end of the header, prior to the
 * frame repeat data. The frame numbers are stored as a list of shorts.
 * The list is terminated on the left side with a negative value.
 */
bool animIsFrameCutSkipped(s16 animnum, s32 frame)
{
	u8 *ptr = (u8 *)(g_AnimHeaderBytes[g_AnimToHeaderSlot[animnum]] + g_Anims[animnum].headerlen - 2);

	// Iterate past the repeat list
	if (g_Anims[animnum].flags & ANIMFLAG_HASREPEATFRAMES) {
		while (true) {
			s16 repeatfromframe = ptr[0] << 8 | ptr[1];

			if (repeatfromframe < 0) {
				break;
			}

			ptr -= 4;
		}

		ptr -= 2;
	}

	while (true) {
		s16 skipframe = ptr[0] << 8 | ptr[1];

		if (skipframe < 0) {
			break;
		}

		if (skipframe == frame) {
			return true;
		}

		ptr -= 2;
	}

	return false;
}

u8 animLoadFrame(s16 animnum, s32 framenum)
{
	s32 slot = -1;
	s32 i;
	s32 offset;
	s32 stack;
	s32 loadframenum = framenum;

	for (i = 0; i < ANIM_FRAME_CACHE_SIZE; i++) {
		if (g_AnimFrameAnimNums[i] == animnum && g_AnimFrameFrameNums[i] == loadframenum) {
			slot = i;
			break;
		}
	}

	if (slot >= 0) {
		g_AnimFrameBirths[slot] = 1;
	} else {
		slot = g_NextAnimFrameIndex;

		while (g_AnimFrameBirths[slot]) {
			slot = (slot + 1) % ANIM_FRAME_CACHE_SIZE;
		}

		if (g_Anims[animnum].flags & ANIMFLAG_HASREPEATFRAMES) {
			animRemapFrameForLoad(animnum, framenum, &loadframenum);
		}

		if (g_Anims[animnum].bytesperframe) {
			offset = g_Anims[animnum].bytesperframe * loadframenum + (g_Anims[animnum].data + g_Anims[animnum].headerlen);
			if (g_Anims[animnum].data == 0xffffffff) {
				// load external replacement (this will fatal error if there's no data)
				if (!g_AnimReplacements[animnum]) {
					g_AnimReplacements[animnum] = modAnimationLoadData(animnum);
				}
				offset = g_Anims[animnum].bytesperframe * loadframenum + g_Anims[animnum].headerlen;
				g_AnimFrameBytes[slot] = g_AnimReplacements[animnum] + offset;
			} else {
				/* C-6: catalog override for ROM-based animations */
				if (!g_AnimReplacements[animnum]) {
					g_AnimReplacements[animnum] = modAnimationTryCatalogOverride(animnum);
				}
				if (g_AnimReplacements[animnum]) {
					offset = g_Anims[animnum].bytesperframe * loadframenum + g_Anims[animnum].headerlen;
					g_AnimFrameBytes[slot] = g_AnimReplacements[animnum] + offset;
				} else
				g_AnimFrameBytes[slot] = animDma(&g_AnimFrameByteSlots[slot * g_AnimMaxBytesPerFrame], offset, g_Anims[animnum].bytesperframe);
			}
		} else {
			g_AnimFrameBytes[slot] = &g_AnimFrameByteSlots[slot * g_AnimMaxBytesPerFrame];
		}

		g_AnimFrameAnimNums[slot] = animnum;
		g_AnimFrameFrameNums[slot] = framenum;
		g_AnimFrameBirths[slot] = 1;
		g_NextAnimFrameIndex = (slot + 1) % ANIM_FRAME_CACHE_SIZE;
	}

	return slot;
}

void animForgetFrameBirths(void)
{
	s32 i;

	for (i = 0; i < ANIM_FRAME_CACHE_SIZE; i++) {
		g_AnimFrameBirths[i] = 0;
	}
}

void animLoadHeader(s16 animnum)
{
	s32 i;

	if (g_AnimToHeaderSlot[animnum] != 0xff) {
		g_AnimHeaderBirths[g_AnimToHeaderSlot[animnum]] = g_Vars.thisframestart240;
		g_NextAnimHeaderIndex = (g_AnimToHeaderSlot[animnum] + 1) % ANIM_HEADER_CACHE_SIZE;
	} else {
		s32 tmp;
		s32 slot = g_NextAnimHeaderIndex;
		s32 stack;

		for (i = 0; i < ANIM_HEADER_CACHE_SIZE; i++) {
			if (g_AnimHeaderBirths[i] < g_AnimHeaderBirths[slot]) {
				slot = i;
			}
		}

		if (g_AnimHeaderBirths[slot]);
		if (&g_Vars && &g_Vars);

		if (g_AnimHeaderAnimNums[slot]) {
			g_AnimToHeaderSlot[g_AnimHeaderAnimNums[slot]] = 0xff;
		}

		tmp = g_Anims[animnum].headerlen;

		if (g_Anims[animnum].data == 0xffffffff) {
			// load external replacement (this will fatal error if there's no data)
			if (!g_AnimReplacements[animnum]) {
				g_AnimReplacements[animnum] = modAnimationLoadData(animnum);
			}
			g_AnimHeaderBytes[slot] = g_AnimReplacements[animnum];
		} else {
			/* C-6: catalog override for ROM-based animations */
			if (!g_AnimReplacements[animnum]) {
				g_AnimReplacements[animnum] = modAnimationTryCatalogOverride(animnum);
			}
			if (g_AnimReplacements[animnum]) {
				g_AnimHeaderBytes[slot] = g_AnimReplacements[animnum];
			} else
			g_AnimHeaderBytes[slot] = animDma(&g_AnimHeaderByteSlots[slot * g_AnimMaxHeaderLength], g_Anims[animnum].data, tmp);
		}
		g_AnimToHeaderSlot[animnum] = slot;
		g_AnimHeaderAnimNums[slot] = animnum;
		g_AnimHeaderBirths[slot] = g_Vars.thisframestart240;
		g_NextAnimHeaderIndex = (slot + 1) % ANIM_HEADER_CACHE_SIZE;
	}
}

/**
 * Read the rotation, position and scale values for the given part for the frame
 * at the given frameslot.
 *
 * Both the anim header and frame data must be loaded already.
 */
static u32 animReadBe32(const u8 *ptr)
{
	return (u32)ptr[0] << 24 | (u32)ptr[1] << 16
		| (u32)ptr[2] << 8 | (u32)ptr[3];
}

static s16 animSignExtend16(u32 value, u8 width)
{
	if (width > 0 && width < 16 && (value & (1u << (width - 1)))) {
		value |= 0xffffu << width;
	}

	return (s16)value;
}

static bool animReadFrameField(const u8 *framebytes, u32 framebytelen,
		u8 width, u32 *bitoffset, u32 *value)
{
	if (bitoffset == NULL || !animReadBitsBounded(framebytes, framebytelen,
			width, *bitoffset, value)) {
		return false;
	}

	*bitoffset += width;
	return true;
}

static void animSetDefaultTransform(struct coord *rot,
		struct coord *translate, struct coord *scale)
{
	rot->x = rot->y = rot->z = 0.0f;
	translate->x = translate->y = translate->z = 0.0f;
	scale->x = scale->y = scale->z = 1.0f;
}

static void animLogFrameLayoutReject(s16 animnum, u8 frameslot, s32 part,
		enum anim_frame_layout_result result)
{
	s16 framenum = g_AnimFrameFrameNums != NULL
			&& frameslot < ANIM_FRAME_CACHE_SIZE
		? g_AnimFrameFrameNums[frameslot] : -1;

	if (s_AnimLastRejectedAnimnum == animnum
			&& s_AnimLastRejectedFrame == framenum) {
		return;
	}

	sysLogPrintf(LOG_ERROR,
		"ANIM.FRAME.LAYOUT.REJECT: anim=%d frame=%d part=%d reason=%s",
		animnum, framenum, part, animFrameLayoutResultString(result));
	s_AnimLastRejectedAnimnum = animnum;
	s_AnimLastRejectedFrame = framenum;
}

void animGetRotTranslateScale(s32 part, bool flip, struct skeleton *skel,
		s16 animnum, u8 frameslot, struct coord *rot,
		struct coord *translate, struct coord *scale)
{
	struct anim_frame_part_layout layout;
	enum anim_frame_layout_result layout_result;
	u16 introt[3];
	const u8 *framebytes;
	const u8 *ptr;
	u32 framebytelen;
	u32 bitoffset;
	u32 value;
	u8 flags;
	u8 framelen;
	u8 readbitlen;
	s32 axis;

	if (rot == NULL || translate == NULL || scale == NULL) {
		return;
	}

	animSetDefaultTransform(rot, translate, scale);

	if (animnum < 0 || animnum >= animGetTotalCount()
			|| frameslot >= ANIM_FRAME_CACHE_SIZE
			|| g_Anims == NULL || g_AnimToHeaderSlot == NULL
			|| g_AnimHeaderBytes == NULL || g_AnimFrameBytes == NULL
			|| g_AnimToHeaderSlot[animnum] == 0xff) {
		animLogFrameLayoutReject(animnum, frameslot, part,
			ANIM_FRAME_LAYOUT_INVALID_ARGUMENT);
		return;
	}

	if (flip) {
		if (skel == NULL || skel->things == NULL
				|| part < 0 || part >= skel->numthings) {
			animLogFrameLayoutReject(animnum, frameslot, part,
				ANIM_FRAME_LAYOUT_INVALID_ARGUMENT);
			return;
		}

		part = skel->things[part][1];
	}

	framebytes = g_AnimFrameBytes[frameslot];
	framebytelen = g_Anims[animnum].bytesperframe;
	framelen = g_Anims[animnum].framelen;

	if ((framebytes == NULL && framebytelen != 0) || framelen > 16) {
		animLogFrameLayoutReject(animnum, frameslot, part,
			ANIM_FRAME_LAYOUT_INVALID_ARGUMENT);
		return;
	}

	layout_result = animFrameLocatePartBounded(
		g_AnimHeaderBytes[g_AnimToHeaderSlot[animnum]],
		g_Anims[animnum].headerlen, framebytelen, part, &layout);

	if (layout_result != ANIM_FRAME_LAYOUT_OK) {
		animLogFrameLayoutReject(animnum, frameslot, part, layout_result);
		return;
	}

	ptr = layout.field_header;
	bitoffset = layout.frame_bit_offset;
	flags = layout.flags;

	if (flags & ANIMFIELD_S16_TRANSLATE) {
		for (axis = 0; axis < 3; axis++) {
			readbitlen = ptr[axis * 3 + 2];

			if (!animReadFrameField(framebytes, framebytelen, readbitlen,
					&bitoffset, &value)) {
				animLogFrameLayoutReject(animnum, frameslot, part,
					ANIM_FRAME_LAYOUT_PAYLOAD_TRUNCATED);
				animSetDefaultTransform(rot, translate, scale);
				return;
			}

			translate->f[axis] = (s16)(animSignExtend16(value, readbitlen)
				+ ((u16)ptr[axis * 3] << 8) + ptr[axis * 3 + 1]);
		}

		ptr += 9;
	} else if (flags & ANIMFIELD_S32_TRANSLATE) {
		for (axis = 0; axis < 3; axis++) {
			readbitlen = ptr[axis * 5];

			if (!animReadFrameField(framebytes, framebytelen, readbitlen,
					&bitoffset, &value)) {
				animLogFrameLayoutReject(animnum, frameslot, part,
					ANIM_FRAME_LAYOUT_PAYLOAD_TRUNCATED);
				animSetDefaultTransform(rot, translate, scale);
				return;
			}

			translate->f[axis] = ((s64)(s32)animReadBe32(
				&ptr[axis * 5 + 1]) + value) * 0.001f;
		}

		ptr += 15;
	} else if (flags & ANIMFIELD_08) {
		bitoffset += ptr[2] + ptr[5] + ptr[8] + ptr[11];
		ptr += 12;
	}

	if (flags & ANIMFIELD_S16_ROTATE) {
		for (axis = 0; axis < 3; axis++) {
			readbitlen = ptr[axis * 3 + 2];

			if (!animReadFrameField(framebytes, framebytelen, readbitlen,
					&bitoffset, &value)) {
				animLogFrameLayoutReject(animnum, frameslot, part,
					ANIM_FRAME_LAYOUT_PAYLOAD_TRUNCATED);
				animSetDefaultTransform(rot, translate, scale);
				return;
			}

			introt[axis] = (u16)((u32)(value
				+ ((u16)ptr[axis * 3] << 8) + ptr[axis * 3 + 1])
				<< (16 - framelen));
		}

		ptr += 9;

		rot->x = introt[0] * M_BADTAU / 65536.0f;

		if (flip) {
			rot->y = introt[1] != 0
				? (0x10000 - introt[1]) * M_BADTAU / 65536.0f : 0.0f;
			rot->z = introt[2] != 0
				? (0x10000 - introt[2]) * M_BADTAU / 65536.0f : 0.0f;
		} else {
			rot->y = introt[1] * M_BADTAU / 65536.0f;
			rot->z = introt[2] * M_BADTAU / 65536.0f;
		}
	} else if (flags & ANIMFIELD_F32_ROTATE) {
		for (axis = 0; axis < 3; axis++) {
			if (!animReadFrameField(framebytes, framebytelen, 32,
					&bitoffset, &value)) {
				animLogFrameLayoutReject(animnum, frameslot, part,
					ANIM_FRAME_LAYOUT_PAYLOAD_TRUNCATED);
				animSetDefaultTransform(rot, translate, scale);
				return;
			}

			memcpy(&rot->f[axis], &value, sizeof(value));
		}

		if (flip) {
			if (rot->y != 0.0f) {
				rot->y = M_BADTAU - rot->y;
			}

			if (rot->z != 0.0f) {
				rot->z = M_BADTAU - rot->z;
			}
		}
	}

	if (flags & ANIMFIELD_CAMERA) {
		bitoffset += ptr[0];
		ptr += 5;
	}

	if (flags & ANIMFIELD_F32_SCALE) {
		for (axis = 0; axis < 3; axis++) {
			if (!animReadFrameField(framebytes, framebytelen, 32,
					&bitoffset, &value)) {
				animLogFrameLayoutReject(animnum, frameslot, part,
					ANIM_FRAME_LAYOUT_PAYLOAD_TRUNCATED);
				animSetDefaultTransform(rot, translate, scale);
				return;
			}

			memcpy(&scale->f[axis], &value, sizeof(value));
		}
	}
}

/**
 * Read the position and Y rotation (?) values for the given part at the given
 * frame number.
 *
 * No data needs to be loaded by the caller - the function will ensure the
 * header and frame are loaded.
 */
u16 animGetPosAngleAsInt(s32 part, bool flip, struct skeleton *skel,
		s16 animnum, s32 framenum, s16 inttranslate[3], bool arg6)
{
	struct anim_frame_part_layout layout;
	enum anim_frame_layout_result layout_result;
	const u8 *framebytes;
	const u8 *ptr;
	u32 framebytelen;
	u32 bitoffset;
	u32 value;
	u16 result = 0;
	u8 readbitlen;
	u8 slot;
	s32 axis;

	if (inttranslate == NULL) {
		return 0;
	}

	inttranslate[0] = 0;
	inttranslate[1] = 0;
	inttranslate[2] = 0;

	if (animnum < 0 || animnum >= animGetTotalCount()
			|| g_Anims == NULL) {
		return 0;
	}

	if (arg6) {
		if (var8005f014 != NULL) {
			inttranslate[2] = var8005f014[animnum];
		}

		return 0;
	}

	if (g_AnimToHeaderSlot == NULL || g_AnimHeaderBytes == NULL
			|| g_AnimFrameBytes == NULL || framenum < 0
			|| framenum >= g_Anims[animnum].numframes) {
		return 0;
	}

	if (flip) {
		if (skel == NULL || skel->things == NULL
				|| part < 0 || part >= skel->numthings) {
			return 0;
		}

		part = skel->things[part][1];
	}

	animLoadHeader(animnum);
	slot = animLoadFrame(animnum, framenum);
	animForgetFrameBirths();

	if (slot >= ANIM_FRAME_CACHE_SIZE
			|| g_AnimToHeaderSlot[animnum] == 0xff) {
		return 0;
	}

	framebytes = g_AnimFrameBytes[slot];
	framebytelen = g_Anims[animnum].bytesperframe;

	if (framebytes == NULL && framebytelen != 0) {
		return 0;
	}

	layout_result = animFrameLocatePartBounded(
		g_AnimHeaderBytes[g_AnimToHeaderSlot[animnum]],
		g_Anims[animnum].headerlen, framebytelen, part, &layout);

	if (layout_result != ANIM_FRAME_LAYOUT_OK) {
		animLogFrameLayoutReject(animnum, slot, part, layout_result);
		return 0;
	}

	/* ANIMFIELD_08 is the native root-motion descriptor: three signed
	 * translations followed by one signed yaw field. A regular transform part
	 * has no root-motion result and therefore resolves to the zero defaults. */
	if ((layout.flags & ANIMFIELD_08) == 0) {
		return 0;
	}

	ptr = layout.field_header;
	bitoffset = layout.frame_bit_offset;

	for (axis = 0; axis < 3; axis++) {
		readbitlen = ptr[axis * 3 + 2];

		if (!animReadFrameField(framebytes, framebytelen, readbitlen,
				&bitoffset, &value)) {
			animLogFrameLayoutReject(animnum, slot, part,
				ANIM_FRAME_LAYOUT_PAYLOAD_TRUNCATED);
			inttranslate[0] = inttranslate[1] = inttranslate[2] = 0;
			return 0;
		}

		inttranslate[axis] = (s16)(animSignExtend16(value, readbitlen)
			+ ((u16)ptr[axis * 3] << 8) + ptr[axis * 3 + 1]);
	}

	readbitlen = ptr[11];

	if (!animReadFrameField(framebytes, framebytelen, readbitlen,
			&bitoffset, &value)) {
		animLogFrameLayoutReject(animnum, slot, part,
			ANIM_FRAME_LAYOUT_PAYLOAD_TRUNCATED);
		inttranslate[0] = inttranslate[1] = inttranslate[2] = 0;
		return 0;
	}

	result = (u16)(animSignExtend16(value, readbitlen)
		+ ((u16)ptr[9] << 8) + ptr[10]);

	if (flip) {
		inttranslate[0] = -inttranslate[0];

		if (result != 0) {
			result = 0x10000 - result;
		}
	}

	return result;
}

f32 animGetTranslateAngle(s32 part, bool flip, struct skeleton *skel, s16 animnum, s32 framenum, struct coord *translate, bool arg6)
{
	s16 inttranslate[3];
	f32 angle;

	if (translate == NULL) {
		return 0.0f;
	}

	angle = animGetPosAngleAsInt(part, flip, skel, animnum, framenum,
		inttranslate, arg6);

	translate->x = inttranslate[0];
	translate->y = inttranslate[1];
	translate->z = inttranslate[2];

	return angle * M_BADTAU / 65536.0f;
}

/**
 * Return a camera value (FOV Y or blur frac) for the current frame.
 *
 * The function assumes the current frame's data has been loaded.
 * Its slot is provided by the frameslot argument.
 *
 * When part = 1, the returned value is the FOV Y.
 * When part = 2, the returned value is the blur frac.
 */
f32 animGetCameraValue(s32 part, s16 animnum, u8 frameslot)
{
	struct anim_frame_part_layout layout;
	enum anim_frame_layout_result layout_result;
	const u8 *framebytes;
	const u8 *ptr;
	u32 framebytelen;
	u32 bitoffset;
	u32 framevalue;
	u8 flags;

	if (animnum < 0 || animnum >= animGetTotalCount()
			|| frameslot >= ANIM_FRAME_CACHE_SIZE
			|| g_Anims == NULL || g_AnimToHeaderSlot == NULL
			|| g_AnimHeaderBytes == NULL || g_AnimFrameBytes == NULL
			|| g_AnimToHeaderSlot[animnum] == 0xff) {
		return 0.0f;
	}

	framebytes = g_AnimFrameBytes[frameslot];
	framebytelen = g_Anims[animnum].bytesperframe;

	if (framebytes == NULL && framebytelen != 0) {
		return 0.0f;
	}

	layout_result = animFrameLocatePartBounded(
		g_AnimHeaderBytes[g_AnimToHeaderSlot[animnum]],
		g_Anims[animnum].headerlen, framebytelen, part, &layout);

	if (layout_result != ANIM_FRAME_LAYOUT_OK) {
		animLogFrameLayoutReject(animnum, frameslot, part, layout_result);
		return 0.0f;
	}

	flags = layout.flags;

	if ((flags & ANIMFIELD_CAMERA) == 0) {
		return 0.0f;
	}

	ptr = layout.field_header;
	bitoffset = layout.frame_bit_offset;

	if (flags & ANIMFIELD_08) {
		bitoffset += ptr[2] + ptr[5] + ptr[8] + ptr[11];
		ptr += 12;
	} else if (flags & ANIMFIELD_S16_TRANSLATE) {
		bitoffset += ptr[2] + ptr[5] + ptr[8];
		ptr += 9;
	} else if (flags & ANIMFIELD_S32_TRANSLATE) {
		bitoffset += ptr[0] + ptr[5] + ptr[10];
		ptr += 15;
	}

	if (flags & ANIMFIELD_S16_ROTATE) {
		bitoffset += ptr[2] + ptr[5] + ptr[8];
		ptr += 9;
	} else if (flags & ANIMFIELD_F32_ROTATE) {
		bitoffset += 96;
	}

	/**
	 * In the header, ptr[0] is the frame adjustment width and ptr[1..4]
	 * is its signed big-endian base value.
	 */
	if (!animReadFrameField(framebytes, framebytelen, ptr[0],
			&bitoffset, &framevalue)) {
		animLogFrameLayoutReject(animnum, frameslot, part,
			ANIM_FRAME_LAYOUT_PAYLOAD_TRUNCATED);
		return 0.0f;
	}

	return ((s64)(s32)animReadBe32(&ptr[1]) + framevalue) * 0.001f;
}
