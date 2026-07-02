/**
 * test_texture_decode_pure.cpp -- B-945 behavioral pins for the shared
 * native-texel -> RGBA32 decoder (port/src/texture_decode_pure.c).
 *
 * The contract under test is N64 RDP sampling parity with fast3d's ROM-path
 * importers (port/fast3d/gfx_pc.cpp import_texture_*). These are the exact
 * defects this decoder replaced (all shipped in extracted .pdtexture /
 * scene.glb PNGs before B-945):
 *   - I4/I8 decoded with dst[3]=255 instead of replicated intensity ->
 *     credits dust motes drew as solid opaque squares (B-346 recurrence).
 *   - IA16 read through a host-endian u16 -> I/A swapped -> transparent
 *     glow sprites decoded as opaque black rectangles.
 *   - RGBA32 unpacked host-endian -> channels scrambled to A,B,G,R.
 */
#include "catch.hpp"

extern "C" {
#include "texture_decode_pure.h"
}

TEST_CASE("I8 replicates intensity into alpha (RDP parity)",
		"[rendering][texture][decode][b945]") {
	/* 4x1 gradient: transparent-black -> opaque-white on the RDP. */
	const uint8_t texels[4] = { 0x00, 0x40, 0xc0, 0xff };
	uint8_t rgba[4 * 4] = {};
	REQUIRE(pdTexDecodeToRgba32(PD_TEXDEC_I8, texels, 4, 1, 4, nullptr, 0,
		rgba) == 0);
	for (int x = 0; x < 4; x++) {
		CHECK(rgba[x * 4 + 0] == texels[x]);
		CHECK(rgba[x * 4 + 1] == texels[x]);
		CHECK(rgba[x * 4 + 2] == texels[x]);
		/* THE B-346 pin: alpha == intensity, never forced opaque. */
		CHECK(rgba[x * 4 + 3] == texels[x]);
	}
}

TEST_CASE("I4 replicates 4->8 scaled intensity into alpha",
		"[rendering][texture][decode][b945]") {
	/* Two bytes = four 4-bit texels: 0x0, 0xf, 0x8, 0x3. */
	const uint8_t texels[2] = { 0x0f, 0x83 };
	const uint8_t expect[4] = { 0x00, 0xff, 0x88, 0x33 }; /* SCALE_4_8 = *17 */
	uint8_t rgba[4 * 4] = {};
	REQUIRE(pdTexDecodeToRgba32(PD_TEXDEC_I4, texels, 4, 1, 2, nullptr, 0,
		rgba) == 0);
	for (int x = 0; x < 4; x++) {
		CHECK(rgba[x * 4 + 0] == expect[x]);
		CHECK(rgba[x * 4 + 3] == expect[x]);
	}
}

TEST_CASE("IA16 keeps big-endian I,A byte order",
		"[rendering][texture][decode][b945]") {
	/* One texel stored [I][A] = bright core, faint alpha. A host-endian u16
	 * read returns A<<8|I and swaps the channels -- the black-rectangle bug. */
	const uint8_t texels[2] = { 0xe0, 0x22 };
	uint8_t rgba[4] = {};
	REQUIRE(pdTexDecodeToRgba32(PD_TEXDEC_IA16, texels, 1, 1, 2, nullptr, 0,
		rgba) == 0);
	CHECK(rgba[0] == 0xe0); /* intensity from byte 0 */
	CHECK(rgba[1] == 0xe0);
	CHECK(rgba[2] == 0xe0);
	CHECK(rgba[3] == 0x22); /* alpha from byte 1 */
}

TEST_CASE("IA8 widens nibbles by bit replication",
		"[rendering][texture][decode][b945]") {
	const uint8_t texels[2] = { 0xf0, 0x3c };
	uint8_t rgba[2 * 4] = {};
	REQUIRE(pdTexDecodeToRgba32(PD_TEXDEC_IA8, texels, 2, 1, 2, nullptr, 0,
		rgba) == 0);
	CHECK(rgba[0] == 0xff); /* I = 0xf * 17 */
	CHECK(rgba[3] == 0x00); /* A = 0x0 * 17 */
	CHECK(rgba[4] == 0x33); /* I = 0x3 * 17 */
	CHECK(rgba[7] == 0xcc); /* A = 0xc * 17 */
}

TEST_CASE("IA4 splits 3-bit intensity and 1-bit alpha",
		"[rendering][texture][decode][b945]") {
	/* One byte = two texels: 0xF (I=7,A=1) and 0x6 (I=3,A=0). */
	const uint8_t texels[1] = { 0xf6 };
	uint8_t rgba[2 * 4] = {};
	REQUIRE(pdTexDecodeToRgba32(PD_TEXDEC_IA4, texels, 2, 1, 1, nullptr, 0,
		rgba) == 0);
	CHECK(rgba[0] == 0xff); /* SCALE_3_8(7) */
	CHECK(rgba[3] == 0xff);
	CHECK(rgba[4] == 0x6d); /* SCALE_3_8(3) = 0b01101101 */
	CHECK(rgba[7] == 0x00);
}

TEST_CASE("RGBA32 preserves R,G,B,A byte order",
		"[rendering][texture][decode][b945]") {
	/* Distinct channel values so any reorder is caught: R=1,G=2,B=3,A=4. */
	const uint8_t texels[4] = { 0x01, 0x02, 0x03, 0x04 };
	uint8_t rgba[4] = {};
	REQUIRE(pdTexDecodeToRgba32(PD_TEXDEC_RGBA32, texels, 1, 1, 4, nullptr, 0,
		rgba) == 0);
	CHECK(rgba[0] == 0x01);
	CHECK(rgba[1] == 0x02);
	CHECK(rgba[2] == 0x03);
	CHECK(rgba[3] == 0x04);
}

TEST_CASE("RGB24 copies RGB and forces opaque alpha",
		"[rendering][texture][decode][b945]") {
	const uint8_t texels[4] = { 0x10, 0x20, 0x30, 0x99 };
	uint8_t rgba[4] = {};
	REQUIRE(pdTexDecodeToRgba32(PD_TEXDEC_RGB24, texels, 1, 1, 4, nullptr, 0,
		rgba) == 0);
	CHECK(rgba[0] == 0x10);
	CHECK(rgba[1] == 0x20);
	CHECK(rgba[2] == 0x30);
	CHECK(rgba[3] == 0xff);
}

TEST_CASE("RGBA16 decodes 5551 big-endian with bit-replicated channels",
		"[rendering][texture][decode][b945]") {
	/* 0xFFFF -> white opaque; 0x0000 -> black transparent;
	 * 0x0843 -> R=1,G=1,B=1 (5-bit each), alpha 1. */
	const uint8_t texels[6] = { 0xff, 0xff, 0x00, 0x00, 0x08, 0x43 };
	uint8_t rgba[3 * 4] = {};
	REQUIRE(pdTexDecodeToRgba32(PD_TEXDEC_RGBA16, texels, 3, 1, 6, nullptr, 0,
		rgba) == 0);
	CHECK(rgba[0] == 0xff);
	CHECK(rgba[3] == 0xff);
	CHECK(rgba[4] == 0x00);
	CHECK(rgba[7] == 0x00);
	/* 0x0843 = 00001_00001_00001_1: each 5-bit channel = 1 ->
	 * SCALE_5_8(1) = (1<<3)|(1>>2) = 0x08; A bit set -> 255. */
	CHECK(rgba[8] == 0x08);
	CHECK(rgba[9] == 0x08);
	CHECK(rgba[10] == 0x08);
	CHECK(rgba[11] == 0xff);
}

TEST_CASE("CI palette formats decode via big-endian palette entries",
		"[rendering][texture][decode][b945]") {
	/* IA16 palette: entry0 = [I=0xAA][A=0x11], entry1 = [I=0x55][A=0xFF]. */
	const uint8_t palette[4] = { 0xaa, 0x11, 0x55, 0xff };
	const uint8_t texels_ci8[2] = { 0x00, 0x01 };
	uint8_t rgba[2 * 4] = {};
	REQUIRE(pdTexDecodeToRgba32(PD_TEXDEC_IA16_CI8, texels_ci8, 2, 1, 2,
		palette, 2, rgba) == 0);
	CHECK(rgba[0] == 0xaa);
	CHECK(rgba[3] == 0x11);
	CHECK(rgba[4] == 0x55);
	CHECK(rgba[7] == 0xff);

	/* Out-of-range index -> loud magenta, not silent black. */
	const uint8_t texels_oob[1] = { 0x05 };
	uint8_t rgba_oob[4] = {};
	REQUIRE(pdTexDecodeToRgba32(PD_TEXDEC_IA16_CI8, texels_oob, 1, 1, 1,
		palette, 2, rgba_oob) == 0);
	CHECK(rgba_oob[0] == 0xff);
	CHECK(rgba_oob[1] == 0x00);
	CHECK(rgba_oob[2] == 0xff);
	CHECK(rgba_oob[3] == 0xff);
}

TEST_CASE("row stride honored for padded images",
		"[rendering][texture][decode][b945]") {
	/* 2x2 I8 image with 8-byte padded rows (pool alignment). */
	const uint8_t texels[16] = {
		0x11, 0x22, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad,
		0x33, 0x44, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad,
	};
	uint8_t rgba[2 * 2 * 4] = {};
	REQUIRE(pdTexDecodeToRgba32(PD_TEXDEC_I8, texels, 2, 2, 8, nullptr, 0,
		rgba) == 0);
	CHECK(rgba[0] == 0x11);
	CHECK(rgba[4] == 0x22);
	CHECK(rgba[8] == 0x33);
	CHECK(rgba[12] == 0x44);
}

TEST_CASE("stored-alpha classification excludes I formats",
		"[rendering][texture][decode][b945][alphamode]") {
	/* Drives the scene.glb alphaMode MASK upgrade: I4/I8 must NOT count as
	 * alpha textures (their decoded alpha is replicated intensity), or
	 * opaque-list scene materials would gain alpha-test holes on re-extract. */
	CHECK(pdTexFormatHasStoredAlpha(PD_TEXDEC_I4) == 0);
	CHECK(pdTexFormatHasStoredAlpha(PD_TEXDEC_I8) == 0);
	CHECK(pdTexFormatHasStoredAlpha(PD_TEXDEC_RGB15) == 0);
	CHECK(pdTexFormatHasStoredAlpha(PD_TEXDEC_RGB24) == 0);

	CHECK(pdTexFormatHasStoredAlpha(PD_TEXDEC_IA4) == 1);
	CHECK(pdTexFormatHasStoredAlpha(PD_TEXDEC_IA8) == 1);
	CHECK(pdTexFormatHasStoredAlpha(PD_TEXDEC_IA16) == 1);
	CHECK(pdTexFormatHasStoredAlpha(PD_TEXDEC_RGBA16) == 1);
	CHECK(pdTexFormatHasStoredAlpha(PD_TEXDEC_RGBA32) == 1);
	CHECK(pdTexFormatHasStoredAlpha(PD_TEXDEC_RGBA16_CI4) == 1);
	CHECK(pdTexFormatHasStoredAlpha(PD_TEXDEC_RGBA16_CI8) == 1);
	CHECK(pdTexFormatHasStoredAlpha(PD_TEXDEC_IA16_CI4) == 1);
	CHECK(pdTexFormatHasStoredAlpha(PD_TEXDEC_IA16_CI8) == 1);
}

TEST_CASE("invalid arguments rejected",
		"[rendering][texture][decode][b945]") {
	uint8_t rgba[4] = {};
	const uint8_t texels[4] = {};
	CHECK(pdTexDecodeToRgba32(PD_TEXDEC_I8, nullptr, 1, 1, 1, nullptr, 0,
		rgba) == -1);
	CHECK(pdTexDecodeToRgba32(PD_TEXDEC_I8, texels, 0, 1, 1, nullptr, 0,
		rgba) == -1);
	CHECK(pdTexDecodeToRgba32(PD_TEXDEC_I8, texels, 1, 1, 0, nullptr, 0,
		rgba) == -1);
	CHECK(pdTexDecodeToRgba32(PD_TEXDEC_I8, texels, 1, 1, 1, nullptr, 0,
		nullptr) == -1);
}
