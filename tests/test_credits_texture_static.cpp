/*
 * B-346: credits text and IA8 particle masks depend on explicit texture
 * state. These checks pin the setup that prevents solid colored rectangles
 * when stale fast3d state leaks into credits drawing.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

static std::string readTextFileForCreditsTexture(const char *path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());

	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

TEST_CASE("credits text setup explicitly resets texture enable and scale",
          "[rendering][credits][texture][static][b346]")
{
	const std::string src = readTextFileForCreditsTexture("src/game/game_1531a0.c");
	const std::size_t fn = src.find("Gfx *text0f153628(Gfx *gdl)");
	REQUIRE(fn != std::string::npos);

	const std::size_t end = src.find("Gfx *text0f153780(Gfx *gdl)", fn);
	REQUIRE(end != std::string::npos);

	const std::string body = src.substr(fn, end - fn);
	REQUIRE(body.find("gDPSetTextureLUT(gdl++, G_TT_NONE);") != std::string::npos);
	REQUIRE(body.find("gSPTexture(gdl++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_ON);") != std::string::npos);
}

TEST_CASE("credits particle masks still use textured alpha",
          "[rendering][credits][texture][static][b346]")
{
	const std::string credits = readTextFileForCreditsTexture("src/game/credits.c");
	const std::string gfx = readTextFileForCreditsTexture("port/fast3d/gfx_pc.cpp");

	REQUIRE(credits.find("{ 0x2a, 0, 0, 32, 32 }") != std::string::npos);
	REQUIRE(credits.find("{ 0x2b, 0, 0, 32, 32 }") != std::string::npos);
	REQUIRE(credits.find("texSelect(&gdl, &g_TexGeneralConfigs[g_CreditParticleConfigs[confignum].texturenum], 2, 1, 2, 1, 0);") != std::string::npos);
	REQUIRE(credits.find("gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);") != std::string::npos);
	REQUIRE(credits.find("TEXEL0, 0, SHADE, 0") != std::string::npos);

	REQUIRE(gfx.find("static void import_texture_ia8") != std::string::npos);
	REQUIRE(gfx.find("const uint8_t intensity = SCALE_4_8(addr[i] >> 4);") != std::string::npos);
	REQUIRE(gfx.find("const uint8_t alpha = SCALE_4_8(addr[i] & 0xf);") != std::string::npos);
	REQUIRE(gfx.find("dest[3] = alpha;") != std::string::npos);
}

TEST_CASE("credits tlut fallback does not move subsequent glyph loads",
          "[rendering][credits][texture][static][b346]")
{
	const std::string gfx = readTextFileForCreditsTexture("port/fast3d/gfx_pc.cpp");
	const std::size_t fn = gfx.find("static void gfx_dp_load_tlut");
	REQUIRE(fn != std::string::npos);

	const std::size_t end = gfx.find("static void gfx_dp_load_block", fn);
	REQUIRE(end != std::string::npos);

	const std::string body = gfx.substr(fn, end - fn);
	REQUIRE(body.find("if (tmem < 256)") != std::string::npos);
	REQUIRE(body.find("tmem = 256;") != std::string::npos);
	REQUIRE(body.find("rdp.texture_tile[tile].tmem = tmem;") == std::string::npos);
}

TEST_CASE("ci texture cache key includes palette format",
          "[rendering][credits][texture][static][b346]")
{
	const std::string header = readTextFileForCreditsTexture("port/fast3d/gfx_pc.h");
	const std::string gfx = readTextFileForCreditsTexture("port/fast3d/gfx_pc.cpp");

	REQUIRE(header.find("uint32_t palette_fmt;") != std::string::npos);
	REQUIRE(gfx.find("{ orig_addr, { rdp.palette_addrs[0], rdp.palette_addrs[1] }, rdp.palette_fmt, fmt, siz, palette_index }") != std::string::npos);
	REQUIRE(gfx.find("{ orig_addr, {}, 0, fmt, siz, palette_index }") != std::string::npos);
}

TEST_CASE("frame start invalidates cached fast3d alpha blend state",
          "[rendering][credits][texture][static][b346]")
{
	const std::string gfx = readTextFileForCreditsTexture("port/fast3d/gfx_pc.cpp");
	const std::size_t start = gfx.find("gfx_rapi->start_frame();");
	REQUIRE(start != std::string::npos);

	const std::size_t draw = gfx.find("gfx_rapi->start_draw_to_framebuffer", start);
	REQUIRE(draw != std::string::npos);

	const std::string body = gfx.substr(start, draw - start);
	REQUIRE(body.find("rendering_state.alpha_blend = false;") != std::string::npos);
	REQUIRE(body.find("rendering_state.modulate = false;") != std::string::npos);
	REQUIRE(body.find("rendering_state.additive_blend = false;") != std::string::npos);
}
