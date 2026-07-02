/**
 * texture_decode_pure.h -- PD native texel payload -> RGBA32 decode (B-945).
 *
 * Single source of truth for converting the game's inflated texture pool
 * payloads (texdecompress.c TEXFORMAT_* layouts) into RGBA32, shared by the
 * .pdtexture / .pdscenario extractors (romextract_pdarena.c) and pd-tests.
 *
 * The contract is N64 RDP sampling parity, matching fast3d's ROM-path
 * importers (port/fast3d/gfx_pc.cpp import_texture_*):
 *   - I4/I8 replicate intensity into ALPHA as well as RGB (the RDP samples
 *     I-format texels as R=G=B=A=I). Writing A=255 here is what made the
 *     credits motes render as solid opaque squares (B-346 recurrence).
 *   - IA16 is stored big-endian byte order [I][A]; reading it through a
 *     host-endian u16 swaps the channels and turns transparent glow sprites
 *     into opaque black rectangles.
 *   - RGBA32 is stored byte order [R][G][B][A]; a host-endian u32 unpack
 *     scrambles it to A,B,G,R.
 *   - Sub-byte channels widen by bit replication (SCALE_3/4/5_8), exactly
 *     like gfx_pc.cpp, so extracted PNGs match what fast3d uploads from ROM.
 *
 * Pure: no globals, no game headers, no I/O. Linkable in pd-tests.
 */
#ifndef TEXTURE_DECODE_PURE_H
#define TEXTURE_DECODE_PURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	PD_TEXDEC_RGBA32 = 0,
	PD_TEXDEC_RGB24,
	PD_TEXDEC_RGBA16,
	PD_TEXDEC_RGB15,
	PD_TEXDEC_IA16,
	PD_TEXDEC_IA8,
	PD_TEXDEC_IA4,
	PD_TEXDEC_I8,
	PD_TEXDEC_I4,
	PD_TEXDEC_RGBA16_CI8,
	PD_TEXDEC_IA16_CI8,
	PD_TEXDEC_RGBA16_CI4,
	PD_TEXDEC_IA16_CI4,
	PD_TEXDEC_FORMAT_COUNT,
};

/**
 * Decode a row-strided native texel image into tightly packed RGBA32
 * (width*height*4 bytes, caller-allocated).
 *
 * palette: 16-bit big-endian entries for CI formats, NULL otherwise.
 * Out-of-range palette indices and unknown formats write opaque magenta
 * (255,0,255,255) so misdecodes stay loudly visible instead of black.
 *
 * Returns 0 on success, -1 on invalid arguments.
 */
int pdTexDecodeToRgba32(int format, const uint8_t *data, uint32_t width,
	uint32_t height, uint32_t stride_bytes, const uint8_t *palette,
	uint32_t palette_count, uint8_t *out_rgba);

/**
 * 1 when the format STORES a real alpha channel that can be non-opaque
 * (IA4/IA8/IA16, IA16-palette CI, RGBA16 1-bit, RGBA32). 0 for I4/I8 --
 * their decoded alpha is RDP-replicated intensity, not stored alpha, so
 * material classifiers (glTF alphaMode MASK upgrades) must not treat them
 * as cutout textures -- and 0 for RGB15/RGB24 (forced opaque).
 */
int pdTexFormatHasStoredAlpha(int format);

#ifdef __cplusplus
}
#endif

#endif /* TEXTURE_DECODE_PURE_H */
