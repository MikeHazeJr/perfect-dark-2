#include "catch.hpp"

#include <cstring>
#include <string>

extern "C" {
#include "pdtheme_source.h"
}

static bool parseTheme(const std::string &json, const char *expected,
	pdtheme_source_info_t *out = nullptr, std::string *why = nullptr)
{
	char error[256] = {0};
	pdtheme_source_info_t local = {};
	s32 ok = pdthemeSourceParse(json.data(), json.size(), expected,
		out ? out : &local, error, sizeof(error));
	if (why) *why = error;
	return ok != 0;
}

TEST_CASE("pdtheme strict public source accepts canonical authored schema",
	"[modding][pdxxx][pdtheme][T-ASSETS-026]")
{
	const std::string source = R"JSON({
  "schema":"pd2.theme.v1",
  "catalog_id":"example:theme_ocean",
  "name":"Ocean",
  "author":"Creator",
  "version":"1",
  "palette":{"dialog_border1":"66ccffff","title_glow":"99ddffff"},
  "textures":{"panel":"example:ui_ocean_panel"},
  "scanline":{"enabled":true,"alpha":0.5,"interval":2},
  "textGlow":{"enabled":true,"intensity":0.6,"color":"66ccffff"},
  "soundPack":"example:sfx_theme_pack",
  "menuStyle":"example:ui_ocean_chrome",
  "font":"example:font_ocean"
})JSON";
	pdtheme_source_info_t info = {};
	REQUIRE(parseTheme(source, "example:theme_ocean", &info));
	REQUIRE(std::string(info.catalog_id) == "example:theme_ocean");
	REQUIRE(std::string(info.name) == "Ocean");
	REQUIRE(info.palette_fields == 2);
	REQUIRE(info.texture_roles == 1);
}

TEST_CASE("pdtheme strict source fails closed on identity schema and duplicate drift",
	"[modding][pdxxx][pdtheme][T-ASSETS-026]")
{
	std::string why;
	REQUIRE_FALSE(parseTheme(
		R"({"schema":"pd2.theme.v1","catalog_id":"mod:other","name":"X","version":"1"})",
		"mod:expected", nullptr, &why));
	REQUIRE(why.find("does not match") != std::string::npos);

	REQUIRE_FALSE(parseTheme(
		R"({"schema":"pd2.theme.v0","catalog_id":"mod:expected","name":"X","version":"1"})",
		"mod:expected", nullptr, &why));
	REQUIRE(why.find("schema") != std::string::npos);

	REQUIRE_FALSE(parseTheme(
		R"({"schema":"pd2.theme.v1","catalog_id":"mod:expected","name":"X","name":"Y","version":"1"})",
		"mod:expected", nullptr, &why));
	REQUIRE(why.find("duplicate") != std::string::npos);
}

TEST_CASE("pdtheme strict source rejects raw paths unknown fields ranges and overflow",
	"[modding][pdxxx][pdtheme][T-ASSETS-026]")
{
	std::string why;
	REQUIRE_FALSE(parseTheme(
		R"({"schema":"pd2.theme.v1","catalog_id":"mod:theme","name":"X","version":"1","font":{"path":"font.ttf"}})",
		"mod:theme", nullptr, &why));
	REQUIRE(why.find("font") != std::string::npos);

	REQUIRE_FALSE(parseTheme(
		R"({"schema":"pd2.theme.v1","catalog_id":"mod:theme","name":"X","version":"1","chromeStyle":"raw/path.png"})",
		"mod:theme", nullptr, &why));
	REQUIRE(why.find("unknown") != std::string::npos);

	REQUIRE_FALSE(parseTheme(
		R"({"schema":"pd2.theme.v1","catalog_id":"mod:theme","name":"X","version":"1","scanline":{"alpha":2}})",
		"mod:theme", nullptr, &why));
	REQUIRE(why.find("range") != std::string::npos);

	std::string textures = R"({"schema":"pd2.theme.v1","catalog_id":"mod:theme","name":"X","version":"1","textures":{)";
	for (int i = 0; i < 17; i++) {
		if (i) textures += ',';
		textures += "\"r" + std::to_string(i) + "\":\"mod:ui_" + std::to_string(i) + "\"";
	}
	textures += "}}";
	REQUIRE_FALSE(parseTheme(textures, "mod:theme", nullptr, &why));
	REQUIRE(why.find("capacity") != std::string::npos);
}
