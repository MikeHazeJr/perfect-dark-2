#include "catch.hpp"

#include <algorithm>
#include <array>

extern "C" {
#include "constants.h"
}

#include "lib/anim_bits.h"

TEST_CASE("bounded animation bit reader preserves big-endian fields",
		"[anim][bitstream][B-1101]")
{
	const u8 bytes[] = {0xb2, 0x6c, 0xf0, 0x0d};
	u32 value = 0;

	REQUIRE(animReadBitsBounded(bytes, sizeof(bytes), 8, 3, &value));
	REQUIRE(value == 0x93u);

	REQUIRE(animReadBitsBounded(bytes, sizeof(bytes), 16, 8, &value));
	REQUIRE(value == 0x6cf0u);

	REQUIRE(animReadBitsBounded(bytes, sizeof(bytes), 32, 0, &value));
	REQUIRE(value == 0xb26cf00du);
}

TEST_CASE("bounded animation bit reader accepts exact boundaries",
		"[anim][bitstream][B-1101]")
{
	const u8 byte = 0x81;
	u32 value = 99;

	REQUIRE(animReadBitsBounded(&byte, 1, 1, 7, &value));
	REQUIRE(value == 1u);

	REQUIRE(animReadBitsBounded(&byte, 1, 0, 8, &value));
	REQUIRE(value == 0u);

	value = 99;
	REQUIRE(animReadBitsBounded(nullptr, 0, 0, 0, &value));
	REQUIRE(value == 0u);
}

TEST_CASE("bounded animation bit reader rejects invalid spans without output",
		"[anim][bitstream][B-1101]")
{
	const u8 byte = 0xff;
	u32 value = 0xfeedbeefu;

	REQUIRE_FALSE(animReadBitsBounded(nullptr, 1, 1, 0, &value));
	REQUIRE_FALSE(animReadBitsBounded(nullptr, 0, 1, 0, &value));
	REQUIRE_FALSE(animReadBitsBounded(&byte, 1, 1, 0, nullptr));
	REQUIRE_FALSE(animReadBitsBounded(&byte, 1, 33, 0, &value));
	REQUIRE_FALSE(animReadBitsBounded(&byte, 1, 2, 7, &value));
	REQUIRE_FALSE(animReadBitsBounded(&byte, 1, 0, 9, &value));
	REQUIRE_FALSE(animReadBitsBounded(&byte, 1, 1, 0xffffffffu, &value));
	REQUIRE(value == 0xfeedbeefu);
}

TEST_CASE("animation part locator preserves valid legacy and generic layouts",
		"[anim][layout][B-1101]")
{
	const u8 legacy_header[] = {
		ANIMFIELD_S16_TRANSLATE | ANIMFIELD_S16_ROTATE,
		0x00, 0x10, 3, 0x00, 0x20, 4, 0x00, 0x30, 5,
		0x01, 0x00, 6, 0x02, 0x00, 7, 0x03, 0x00, 8,
		ANIMFIELD_F32_ROTATE | ANIMFIELD_F32_SCALE,
	};
	anim_frame_part_layout layout{};

	REQUIRE(animFrameLocatePartBounded(legacy_header, sizeof(legacy_header),
		29, 0, &layout) == ANIM_FRAME_LAYOUT_OK);
	REQUIRE(layout.flags ==
		(ANIMFIELD_S16_TRANSLATE | ANIMFIELD_S16_ROTATE));
	REQUIRE(layout.frame_bit_offset == 0u);
	REQUIRE(layout.frame_bit_count == 33u);

	/* F32 rotation/scale is a valid legacy format that intentionally selects
	 * the generic decoder. Its fixed-width span must still be admitted by the
	 * same bounded layout contract. */
	REQUIRE(animFrameLocatePartBounded(legacy_header, sizeof(legacy_header),
		29, 1, &layout) == ANIM_FRAME_LAYOUT_OK);
	REQUIRE(layout.flags == (ANIMFIELD_F32_ROTATE | ANIMFIELD_F32_SCALE));
	REQUIRE(layout.frame_bit_offset == 33u);
	REQUIRE(layout.frame_bit_count == 192u);
}

TEST_CASE("animation part locator preserves root-motion and camera ordering",
		"[anim][layout][B-1101]")
{
	const u8 header[] = {
		ANIMFIELD_08 | ANIMFIELD_CAMERA,
		0x00, 0x10, 3,
		0x00, 0x20, 4,
		0x00, 0x30, 5,
		0x00, 0x40, 6,
		7, 0x00, 0x00, 0x13, 0x88,
		ANIMFIELD_S16_ROTATE,
		0x01, 0x00, 8, 0x02, 0x00, 8, 0x03, 0x00, 8,
	};
	anim_frame_part_layout layout{};

	REQUIRE(animFrameLocatePartBounded(header, sizeof(header), 7, 0,
		&layout) == ANIM_FRAME_LAYOUT_OK);
	REQUIRE(layout.flags == (ANIMFIELD_08 | ANIMFIELD_CAMERA));
	REQUIRE(layout.field_header == &header[1]);
	REQUIRE(layout.frame_bit_offset == 0u);
	REQUIRE(layout.frame_bit_count == 25u);

	REQUIRE(animFrameLocatePartBounded(header, sizeof(header), 7, 1,
		&layout) == ANIM_FRAME_LAYOUT_OK);
	REQUIRE(layout.flags == ANIMFIELD_S16_ROTATE);
	REQUIRE(layout.field_header == &header[19]);
	REQUIRE(layout.frame_bit_offset == 25u);
	REQUIRE(layout.frame_bit_count == 24u);
}

TEST_CASE("animation part locator rejects malformed descriptors before fallback",
		"[anim][layout][B-1101]")
{
	anim_frame_part_layout layout{};
	const u8 truncated[] = {ANIMFIELD_S16_ROTATE, 0, 0, 3};
	const u8 invalid_flags[] = {0x04};
	const u8 conflicting_translation[] = {
		ANIMFIELD_08 | ANIMFIELD_S16_TRANSLATE,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	};
	const u8 conflicting_rotation[] = {
		ANIMFIELD_S16_ROTATE | ANIMFIELD_F32_ROTATE,
		0, 0, 0, 0, 0, 0, 0, 0, 0,
	};
	const u8 invalid_width[] = {
		ANIMFIELD_S16_ROTATE,
		0, 0, 17, 0, 0, 0, 0, 0, 0,
	};
	const u8 exact[] = {
		ANIMFIELD_S16_ROTATE,
		0, 0, 8, 0, 0, 8, 0, 0, 8,
	};

	REQUIRE(animFrameLocatePartBounded(truncated, sizeof(truncated), 1, 0,
		&layout) == ANIM_FRAME_LAYOUT_HEADER_TRUNCATED);
	REQUIRE(animFrameLocatePartBounded(invalid_flags, sizeof(invalid_flags), 0,
		0, &layout) == ANIM_FRAME_LAYOUT_INVALID_FLAGS);
	REQUIRE(animFrameLocatePartBounded(conflicting_translation,
		sizeof(conflicting_translation), 0, 0, &layout) ==
		ANIM_FRAME_LAYOUT_INVALID_FLAGS);
	REQUIRE(animFrameLocatePartBounded(conflicting_rotation,
		sizeof(conflicting_rotation), 0, 0, &layout) ==
		ANIM_FRAME_LAYOUT_INVALID_FLAGS);
	REQUIRE(animFrameLocatePartBounded(invalid_width, sizeof(invalid_width), 3,
		0, &layout) == ANIM_FRAME_LAYOUT_FIELD_WIDTH_INVALID);
	REQUIRE(animFrameLocatePartBounded(exact, sizeof(exact), 2, 0,
		&layout) == ANIM_FRAME_LAYOUT_PAYLOAD_TRUNCATED);
	REQUIRE(animFrameLocatePartBounded(exact, sizeof(exact), 3, 0,
		&layout) == ANIM_FRAME_LAYOUT_OK);
	REQUIRE(layout.frame_bit_count == 24u);

	REQUIRE(std::string(animFrameLayoutResultString(
		ANIM_FRAME_LAYOUT_INVALID_FLAGS)) == "invalid_flags");
}

TEST_CASE("schema-v5 special-part stream retains every flags discriminant",
		"[anim][layout][B-1102]")
{
	/* This is the real schema-v5 part-0 descriptor from the generated
	 * base:animation_two_gun_hold source. Its 0x09 flags select the native
	 * ANIMFIELD_08 position/angle fields plus S16 rotation. The remaining
	 * fourteen descriptors use the real 0x01 S16-rotation shape. */
	const u8 part0_header[] = {
		0x00, 0x00, 0x00, 0x04, 0x3b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x0f, 0x96, 0x05, 0x0f, 0x7e, 0x02, 0x00, 0x07, 0x01,
	};
	const u8 part_header[] = {
		0x00, 0x2c, 0x05, 0x00, 0x1c, 0x05, 0x0f, 0xfa, 0x02,
	};
	std::array<u8, 162> framed{};
	std::array<u8, 147> omitted_flags{};
	size_t framed_offset = 0;
	size_t omitted_offset = 0;
	struct anim_frame_stream_layout layout{};

	framed[framed_offset++] = ANIMFIELD_08 | ANIMFIELD_S16_ROTATE;
	std::copy(part0_header, part0_header + sizeof(part0_header),
		framed.begin() + framed_offset);
	framed_offset += sizeof(part0_header);
	for (s32 part = 1; part < 15; part++) {
		framed[framed_offset++] = ANIMFIELD_S16_ROTATE;
		std::copy(part_header, part_header + sizeof(part_header),
			framed.begin() + framed_offset);
		framed_offset += sizeof(part_header);
	}

	REQUIRE((ANIMFIELD_08 | ANIMFIELD_S16_ROTATE) == 0x09);
	REQUIRE(framed[0] == 0x09);
	/* The omitted-flags fixture is exactly the same 15 header_hex descriptors,
	 * concatenated without their discriminants. It must not be reclassified as
	 * a valid descriptor stream merely because some header bytes happen to be
	 * legal flag values. */
	std::copy(part0_header, part0_header + sizeof(part0_header),
		omitted_flags.begin() + omitted_offset);
	omitted_offset += sizeof(part0_header);
	for (s32 part = 1; part < 15; part++) {
		std::copy(part_header, part_header + sizeof(part_header),
			omitted_flags.begin() + omitted_offset);
		omitted_offset += sizeof(part_header);
	}

	REQUIRE(framed_offset == framed.size());
	REQUIRE(omitted_offset == omitted_flags.size());
	REQUIRE(animFrameMeasureDescriptorStreamBounded(
		framed.data(), static_cast<u32>(framed.size()), 22, 15, &layout)
		== ANIM_FRAME_LAYOUT_OK);
	REQUIRE(layout.descriptor_bytes == 162u);
	REQUIRE(layout.frame_bit_count == 176u);
	REQUIRE(animFrameMeasureDescriptorStreamBounded(
		framed.data(), static_cast<u32>(framed.size()), 21, 15, &layout)
		== ANIM_FRAME_LAYOUT_PAYLOAD_TRUNCATED);
	REQUIRE(animFrameMeasureDescriptorStreamBounded(
		framed.data(), static_cast<u32>(framed.size()), 22, 16, &layout)
		== ANIM_FRAME_LAYOUT_HEADER_TRUNCATED);
	std::array<u8, 163> trailing{};
	std::copy(framed.begin(), framed.end(), trailing.begin());
	trailing.back() = 0;
	REQUIRE(animFrameMeasureDescriptorStreamBounded(
		trailing.data(), static_cast<u32>(trailing.size()), 22, 15, &layout)
		== ANIM_FRAME_LAYOUT_DESCRIPTOR_TRAILING);
	REQUIRE(std::string(animFrameLayoutResultString(
		ANIM_FRAME_LAYOUT_DESCRIPTOR_TRAILING)) == "descriptor_trailing");

	REQUIRE(animFrameMeasureDescriptorStreamBounded(
		omitted_flags.data(), static_cast<u32>(omitted_flags.size()),
		22, 15, &layout) == ANIM_FRAME_LAYOUT_INVALID_FLAGS);
}
