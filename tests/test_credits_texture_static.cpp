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
