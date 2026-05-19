/*
 * B-346: fast3d blender decode must recognize fog/additive alpha masks
 * without confusing the second blender alpha slot with G_BL_A_FOG.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

static std::string readTextFile(const char *path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());

	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

TEST_CASE("fast3d blender decode covers c1 and c2 fog/additive alpha",
          "[rendering][fast3d][fog][static][b346]")
{
	const std::string src = readTextFile("port/fast3d/gfx_pc.cpp");

	REQUIRE(src.find("gfx_blender_uses_fog(") != std::string::npos);
	REQUIRE(src.find("gfx_blender_uses_fog_alpha(") != std::string::npos);
	REQUIRE(src.find("gfx_blender_uses_translucent_mem(") != std::string::npos);

	REQUIRE(src.find("gfx_blender_field(mode, 30) == G_BL_CLR_FOG") != std::string::npos);
	REQUIRE(src.find("gfx_blender_field(mode, 28) == G_BL_CLR_FOG") != std::string::npos);
	REQUIRE(src.find("gfx_blender_field(mode, 22) == G_BL_CLR_FOG") != std::string::npos);
	REQUIRE(src.find("gfx_blender_field(mode, 20) == G_BL_CLR_FOG") != std::string::npos);
	REQUIRE(src.find("gfx_blender_field(mode, 26) == G_BL_A_FOG") != std::string::npos);
	REQUIRE(src.find("gfx_blender_field(mode, 24) == G_BL_A_FOG") != std::string::npos);

	REQUIRE(src.find("gfx_blender_field(mode, 18) == G_BL_A_FOG") == std::string::npos);
	REQUIRE(src.find("gfx_blender_field(mode, 16) == G_BL_A_FOG") == std::string::npos);

	REQUIRE(src.find("use_fog_alpha = gfx_blender_uses_fog_alpha(rdp.other_mode_l)") != std::string::npos);
	REQUIRE(src.find("SHADER_OPT_BLEND_ALPHA_FOG") != std::string::npos);
}

TEST_CASE("fast3d fog-alpha blender option reaches shader alpha",
          "[rendering][fast3d][fog][static][b346]")
{
	const std::string header = readTextFile("port/fast3d/gfx_cc.h");
	const std::string cc = readTextFile("port/fast3d/gfx_cc.cpp");
	const std::string gl = readTextFile("port/fast3d/gfx_opengl.cpp");

	REQUIRE(header.find("SHADER_OPT_BLEND_ALPHA_FOG") != std::string::npos);
	REQUIRE(header.find("bool opt_blend_alpha_fog;") != std::string::npos);
	REQUIRE(cc.find("opt_blend_alpha_fog = (shader_id1 & SHADER_OPT_BLEND_ALPHA_FOG) != 0") != std::string::npos);
	REQUIRE(gl.find("if (cc_features.opt_blend_alpha_fog)") != std::string::npos);
	REQUIRE(gl.find("texel.a = vFog.a;") != std::string::npos);
}
