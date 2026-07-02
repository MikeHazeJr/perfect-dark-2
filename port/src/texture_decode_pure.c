/**
 * texture_decode_pure.c -- PD native texel payload -> RGBA32 decode (B-945).
 * See texture_decode_pure.h for the N64 RDP parity contract.
 */
#include "texture_decode_pure.h"

#include <string.h>

/* Bit-replication channel widening, identical to fast3d gfx_pc.cpp so the
 * extracted PNGs byte-match what the ROM path uploads to GL. */
#define PDTD_SCALE_5_8(v) (uint8_t)((((v) & 0x1fu) << 3) | (((v) & 0x1fu) >> 2))
#define PDTD_SCALE_4_8(v) (uint8_t)(((v) & 0x0fu) * 0x11u)
#define PDTD_SCALE_3_8(v) (uint8_t)((((v) & 0x07u) << 5) | (((v) & 0x07u) << 2) | (((v) & 0x07u) >> 1))

static uint16_t pdtdReadBe16(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/* N64 RGBA16 is RRRRRGGGGGBBBBBA (5551). */
static void pdtdRgba16ToRgba32(uint16_t p, uint8_t *out)
{
	out[0] = PDTD_SCALE_5_8(p >> 11);
	out[1] = PDTD_SCALE_5_8(p >> 6);
	out[2] = PDTD_SCALE_5_8(p >> 1);
	out[3] = (p & 1u) ? 255u : 0u;
}

static void pdtdWriteMagenta(uint8_t *dst)
{
	dst[0] = 255u;
	dst[1] = 0u;
	dst[2] = 255u;
	dst[3] = 255u;
}

int pdTexFormatHasStoredAlpha(int format)
{
	switch (format) {
	case PD_TEXDEC_RGBA32:
	case PD_TEXDEC_RGBA16:
	case PD_TEXDEC_IA16:
	case PD_TEXDEC_IA8:
	case PD_TEXDEC_IA4:
	case PD_TEXDEC_RGBA16_CI8:
	case PD_TEXDEC_IA16_CI8:
	case PD_TEXDEC_RGBA16_CI4:
	case PD_TEXDEC_IA16_CI4:
		return 1;
	/* I4/I8: decoded alpha is replicated intensity (RDP sampling rule),
	 * not stored alpha. RGB15/RGB24: no alpha bits at all. */
	case PD_TEXDEC_I8:
	case PD_TEXDEC_I4:
	case PD_TEXDEC_RGB15:
	case PD_TEXDEC_RGB24:
	default:
		return 0;
	}
}

int pdTexDecodeToRgba32(int format, const uint8_t *data, uint32_t width,
	uint32_t height, uint32_t stride_bytes, const uint8_t *palette,
	uint32_t palette_count, uint8_t *out_rgba)
{
	uint32_t x;
	uint32_t y;

	if (!data || !out_rgba || width == 0 || height == 0 || stride_bytes == 0) {
		return -1;
	}

	for (y = 0; y < height; y++) {
		const uint8_t *row = data + (size_t)y * stride_bytes;
		for (x = 0; x < width; x++) {
			uint8_t *dst = out_rgba + ((size_t)y * width + x) * 4u;
			switch (format) {
			case PD_TEXDEC_RGBA32:
			case PD_TEXDEC_RGB24: {
				/* Stored byte order is R,G,B,A -- copy bytes directly.
				 * (A host-endian u32 unpack scrambles this to A,B,G,R.) */
				const uint8_t *px = row + (size_t)x * 4u;
				dst[0] = px[0];
				dst[1] = px[1];
				dst[2] = px[2];
				dst[3] = format == PD_TEXDEC_RGB24 ? 255u : px[3];
				break;
			}
			case PD_TEXDEC_RGBA16:
			case PD_TEXDEC_RGB15:
				pdtdRgba16ToRgba32(pdtdReadBe16(row + (size_t)x * 2u), dst);
				if (format == PD_TEXDEC_RGB15) {
					dst[3] = 255u;
				}
				break;
			case PD_TEXDEC_IA16: {
				/* Stored byte order is [I][A] (big-endian pair). */
				const uint8_t *px = row + (size_t)x * 2u;
				dst[0] = dst[1] = dst[2] = px[0];
				dst[3] = px[1];
				break;
			}
			case PD_TEXDEC_IA8: {
				uint8_t p = row[x];
				dst[0] = dst[1] = dst[2] = PDTD_SCALE_4_8(p >> 4);
				dst[3] = PDTD_SCALE_4_8(p);
				break;
			}
			case PD_TEXDEC_I8:
				/* RDP samples I texels as R=G=B=A=I (fast3d import_texture_i8). */
				dst[0] = dst[1] = dst[2] = row[x];
				dst[3] = row[x];
				break;
			case PD_TEXDEC_IA4: {
				uint8_t p = row[x >> 1];
				uint8_t n = (x & 1u) ? (uint8_t)(p & 0x0fu) : (uint8_t)(p >> 4);
				dst[0] = dst[1] = dst[2] = PDTD_SCALE_3_8(n >> 1);
				dst[3] = (n & 1u) ? 255u : 0u;
				break;
			}
			case PD_TEXDEC_I4: {
				uint8_t p = row[x >> 1];
				uint8_t n = (x & 1u) ? (uint8_t)(p & 0x0fu) : (uint8_t)(p >> 4);
				uint8_t i = PDTD_SCALE_4_8(n);
				/* RDP samples I texels as R=G=B=A=I (fast3d import_texture_i4). */
				dst[0] = dst[1] = dst[2] = i;
				dst[3] = i;
				break;
			}
			case PD_TEXDEC_RGBA16_CI8:
			case PD_TEXDEC_IA16_CI8:
			case PD_TEXDEC_RGBA16_CI4:
			case PD_TEXDEC_IA16_CI4: {
				uint32_t idx;
				if (format == PD_TEXDEC_RGBA16_CI8 ||
						format == PD_TEXDEC_IA16_CI8) {
					idx = row[x];
				} else {
					uint8_t p = row[x >> 1];
					idx = (x & 1u) ? (p & 0x0fu) : (uint32_t)(p >> 4);
				}
				if (!palette || idx >= palette_count) {
					pdtdWriteMagenta(dst);
				} else if (format == PD_TEXDEC_RGBA16_CI8 ||
						format == PD_TEXDEC_RGBA16_CI4) {
					pdtdRgba16ToRgba32(pdtdReadBe16(palette + idx * 2u), dst);
				} else {
					uint16_t p = pdtdReadBe16(palette + idx * 2u);
					dst[0] = dst[1] = dst[2] = (uint8_t)(p >> 8);
					dst[3] = (uint8_t)(p & 0xffu);
				}
				break;
			}
			default:
				pdtdWriteMagenta(dst);
				break;
			}
		}
	}

	return 0;
}
