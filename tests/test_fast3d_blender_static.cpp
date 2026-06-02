/*
 * B-346: fast3d blender decode must recognize fog/additive alpha masks
 * without turning fog alpha into material/output alpha.
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

TEST_CASE("fast3d blender decode treats fog alpha as fog color state only",
          "[rendering][fast3d][fog][static][b346]")
{
	const std::string src = readTextFile("port/fast3d/gfx_pc.cpp");

	REQUIRE(src.find("gfx_blender_uses_fog(") != std::string::npos);
	REQUIRE(src.find("gfx_blender_uses_fog_alpha(") == std::string::npos);
	REQUIRE(src.find("gfx_blender_uses_translucent_mem(") != std::string::npos);
	REQUIRE(src.find("gfx_blender_uses_additive_fog(") != std::string::npos);

	REQUIRE(src.find("gfx_blender_field(mode, 30) == G_BL_CLR_FOG") != std::string::npos);
	REQUIRE(src.find("gfx_blender_field(mode, 28) == G_BL_CLR_FOG") != std::string::npos);
	REQUIRE(src.find("gfx_blender_field(mode, 22) == G_BL_CLR_FOG") != std::string::npos);
	REQUIRE(src.find("gfx_blender_field(mode, 20) == G_BL_CLR_FOG") != std::string::npos);
	REQUIRE(src.find("gfx_blender_field(mode, 26) == G_BL_A_FOG") != std::string::npos);
	REQUIRE(src.find("gfx_blender_field(mode, 24) == G_BL_A_FOG") != std::string::npos);

	REQUIRE(src.find("gfx_blender_field(mode, 18) == G_BL_A_FOG") == std::string::npos);
	REQUIRE(src.find("gfx_blender_field(mode, 16) == G_BL_A_FOG") == std::string::npos);

	REQUIRE(src.find("bool use_alpha = gfx_blender_uses_translucent_mem(rdp.other_mode_l) || use_additive_fog;") != std::string::npos);
	REQUIRE(src.find("|| use_fog_alpha") == std::string::npos);
	REQUIRE(src.find("SHADER_OPT_BLEND_ALPHA_FOG") == std::string::npos);
}

TEST_CASE("fast3d additive fog uses a narrow blend tuple",
          "[rendering][fast3d][fog][static][b346]")
{
	const std::string src = readTextFile("port/fast3d/gfx_pc.cpp");
	const std::string gl = readTextFile("port/fast3d/gfx_opengl.cpp");

	REQUIRE(src.find("gfx_blender_field(mode, 30) == G_BL_CLR_IN") != std::string::npos);
	REQUIRE(src.find("gfx_blender_field(mode, 26) == G_BL_A_FOG") != std::string::npos);
	REQUIRE(src.find("gfx_blender_field(mode, 22) == G_BL_CLR_MEM") != std::string::npos);
	REQUIRE(src.find("gfx_blender_field(mode, 18) == G_BL_1") != std::string::npos);

	REQUIRE(src.find("gfx_blender_field(mode, 28) == G_BL_CLR_IN") != std::string::npos);
	REQUIRE(src.find("gfx_blender_field(mode, 24) == G_BL_A_FOG") != std::string::npos);
	REQUIRE(src.find("gfx_blender_field(mode, 20) == G_BL_CLR_MEM") != std::string::npos);
	REQUIRE(src.find("gfx_blender_field(mode, 16) == G_BL_1") != std::string::npos);

	REQUIRE(src.find("SHADER_OPT_ALPHA_FROM_FOG") != std::string::npos);
	REQUIRE(src.find("gfx_rapi->set_use_alpha(use_alpha, use_modulate, use_additive_fog);") != std::string::npos);
	REQUIRE(gl.find("glBlendFunc(GL_SRC_ALPHA, GL_ONE);") != std::string::npos);
}

TEST_CASE("fast3d fog shader preserves material alpha",
          "[rendering][fast3d][fog][static][b346]")
{
	const std::string header = readTextFile("port/fast3d/gfx_cc.h");
	const std::string cc = readTextFile("port/fast3d/gfx_cc.cpp");
	const std::string gl = readTextFile("port/fast3d/gfx_opengl.cpp");

	REQUIRE(header.find("SHADER_OPT_BLEND_ALPHA_FOG") == std::string::npos);
	REQUIRE(header.find("SHADER_OPT_ALPHA_FROM_FOG") != std::string::npos);
	REQUIRE(header.find("bool opt_blend_alpha_fog;") == std::string::npos);
	REQUIRE(header.find("bool opt_alpha_from_fog;") != std::string::npos);
	REQUIRE(cc.find("opt_blend_alpha_fog") == std::string::npos);
	REQUIRE(cc.find("opt_alpha_from_fog") != std::string::npos);
	REQUIRE(gl.find("if (cc_features.opt_blend_alpha_fog)") == std::string::npos);
	REQUIRE(gl.find("if (cc_features.opt_alpha_from_fog)") != std::string::npos);
	REQUIRE(gl.find("texel = vec4(mix(texel.rgb, vFog.rgb, vFog.a), texel.a);") != std::string::npos);
}

TEST_CASE("fast3d normalizes legacy palette texture tuples before import",
          "[rendering][fast3d][texture][static][b412]")
{
	const std::string src = readTextFile("port/fast3d/gfx_pc.cpp");

	REQUIRE(src.find("gfx_normalize_legacy_texture_tuple") != std::string::npos);
	REQUIRE(src.find("*fmt == G_IM_FMT_RGBA && *siz < G_IM_SIZ_16b") !=
	        std::string::npos);
	REQUIRE(src.find("*fmt = G_IM_FMT_CI;") != std::string::npos);
	REQUIRE(src.find("*fmt == G_IM_FMT_IA && *siz == G_IM_SIZ_32b") !=
	        std::string::npos);
	REQUIRE(src.find("*fmt = G_IM_FMT_I;") != std::string::npos);
	REQUIRE(src.find("*siz = G_IM_SIZ_8b;") != std::string::npos);
	REQUIRE(src.find("gfx_normalize_legacy_texture_tuple(&fmt, &siz);") !=
	        std::string::npos);
	REQUIRE(src.find("import_texture_has_valid_dimensions") != std::string::npos);
	REQUIRE(src.find("FAST3D.TEXTURE: zero-sized texture import skipped") !=
	        std::string::npos);
	REQUIRE(src.find("gfx_rapi->upload_texture(tex_upload_buffer, 1, 1);") !=
	        std::string::npos);
	REQUIRE(src.find("sysFatalError(\"Bad size for RGBA texture") !=
	        std::string::npos);
}
