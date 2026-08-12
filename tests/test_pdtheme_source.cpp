#include "catch.hpp"

#include <cstring>
#include <fstream>
#include <sstream>
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
  "textures":{"dialog_background":"example:ui_ocean_panel"},
  "scanline":{"enabled":true,"alpha":0.5,"interval":2},
  "textGlow":{"enabled":true,"intensity":0.6,"color":"66ccffff"},
  "sounds":{"select":"example:sfx_theme_select","cancel":"example:sfx_theme_cancel"},
  "menuMusic":"example:music_theme_menu",
  "menuStyle":"example:ui_ocean_chrome",
  "font":"example:font_ocean",
  "caustics":[{"elementId":"example:ui_ocean_chrome","textureId":"example:ui_ocean_caustic","frameCount":4,"speed":2,"opacity":0.5,"scale":1,"blendMode":"screen"}],
  "borderEffects":[{"elementId":"example:ui_ocean_chrome","maskTextureId":"example:ui_ocean_border","opacity":0.5,"blendMode":"additive","tintColor":"66ccffff","scrollX":1,"scrollY":0}]
})JSON";
	pdtheme_source_info_t info = {};
	REQUIRE(parseTheme(source, "example:theme_ocean", &info));
	REQUIRE(std::string(info.catalog_id) == "example:theme_ocean");
	REQUIRE(std::string(info.name) == "Ocean");
	REQUIRE(info.palette_fields == 2);
	REQUIRE(info.texture_roles == 1);
	REQUIRE(info.sound_roles == 2);
	REQUIRE(info.caustics == 1);
	REQUIRE(info.border_effects == 1);
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

	REQUIRE_FALSE(parseTheme(
		R"({"schema":"pd2.theme.v1","catalog_id":"mod:theme","name":"X","version":"1","textures":{"panel":"mod:ui_panel"}})",
		"mod:theme", nullptr, &why));
	REQUIRE(why.find("unsupported role") != std::string::npos);

	REQUIRE_FALSE(parseTheme(
		R"({"schema":"pd2.theme.v1","catalog_id":"mod:theme","name":"X","version":"1","soundPack":"mod:legacy_pack"})",
		"mod:theme", nullptr, &why));
	REQUIRE(why.find("unknown") != std::string::npos);

	REQUIRE_FALSE(parseTheme(
		R"({"schema":"pd2.theme.v1","catalog_id":"mod:theme","name":"X","version":"1","scanline":{"interval":9}})",
		"mod:theme", nullptr, &why));
	REQUIRE(why.find("range") != std::string::npos);

	REQUIRE_FALSE(parseTheme(
		R"({"schema":"pd2.theme.v1","catalog_id":"mod:theme","name":"X","version":"1","menuStyle":"mod:chrome","caustics":[{"elementId":"chrome","textureId":"mod:mask","frameCount":1,"speed":1,"opacity":1,"scale":1,"blendMode":"screen"}]})",
		"mod:theme", nullptr, &why));
	REQUIRE(why.find("catalog ID") != std::string::npos);
}

static std::string readSource(const char *path)
{
	std::ifstream in(path, std::ios::binary);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

TEST_CASE("pdtheme supported fields reach fail-closed production consumers",
	"[modding][pdxxx][pdtheme][T-ASSETS-028]")
{
	const std::string loader = readSource("port/fast3d/pdgui_theme_loader.cpp");
	const std::string audio = readSource("port/fast3d/pdgui_audio.cpp");
	const std::string backend = readSource("port/fast3d/pdgui_backend.cpp");
	const std::string font = readSource("port/fast3d/pdgui_font_mod.cpp");
	const std::string style = readSource("port/fast3d/pdgui_style.cpp");
	const std::string music = readSource("src/game/music.c");
	const std::string glyphs = readSource("port/fast3d/pdgui_glyphs.cpp");
	const std::string theme = readSource("port/fast3d/pdgui_theme.cpp");
	REQUIRE(loader.find("validate_theme_def_consumers") != std::string::npos);
	REQUIRE(loader.find("pdguiAudioReplaceThemeRoles") != std::string::npos);
	REQUIRE(loader.find("pdguiFontModValidateCatalogId") != std::string::npos);
	REQUIRE(loader.find("pdguiRequestFontAtlasRebuild") != std::string::npos);
	REQUIRE(loader.find("pdguiNinesliceCanRegister") != std::string::npos);
	REQUIRE(loader.find("pdguiEffectsCanSet") != std::string::npos);
	REQUIRE(loader.find("s_ActiveEffectIds") != std::string::npos);
	REQUIRE(audio.find("refusing native fallback") != std::string::npos);
	REQUIRE(font.find("entry->ext.font.font_file") != std::string::npos);
	REQUIRE(theme.find("pdguiFontModValidateCatalogId(entry->id") != std::string::npos);
	REQUIRE(theme.find("skipped invalid catalog font") != std::string::npos);
	REQUIRE(backend.find("AddFontFromMemoryTTF") != std::string::npos);
	REQUIRE(style.find("declaredChromeFailed") != std::string::npos);
	REQUIRE(style.find("pdguiEffectsDrawAll(activeChromeId") != std::string::npos);
	REQUIRE(music.find("pdguiThemeGetMenuMusicTrack") != std::string::npos);
	REQUIRE(loader.find("modSequenceVirtualTrackForCatalogId") != std::string::npos);
	REQUIRE(glyphs.find("pdguiPalImU32(PDPAL_BODYBG") != std::string::npos);
	REQUIRE(glyphs.find("pdguiImU32TitleGlow") != std::string::npos);
}

TEST_CASE("pdtheme field parser is transport invariant and rejects every public boundary",
	"[modding][network][pdxxx][pdtheme][negative][T-ASSETS-030]")
{
	const std::string full = R"JSON({
  "schema":"pd2.theme.v1","catalog_id":"example:theme_full",
  "name":"Full","author":"Creator","version":"1",
  "palette":{"dialog_border1":"01020304","dialog_titlebg":"11121314","title_glow":"aabbccdd"},
  "textures":{"dialog_background":"example:ui_panel"},
  "scanline":{"enabled":true,"alpha":0.25,"interval":8},
  "textGlow":{"enabled":true,"intensity":4,"color":"01020304"},
  "sounds":{"swipe":"example:sfx","open":"example:sfx2","focus":"example:sfx3","select":"example:sfx4","error":"example:sfx5","toggle_on":"example:sfx6","toggle_off":"example:sfx7","subfocus":"example:sfx8","keyboard_focus":"example:sfx9","cancel":"example:sfx10","success":"example:sfx11"},
  "menuMusic":"example:music","menuStyle":"example:ui_panel","font":"example:font",
  "nineslices":[{"id":"example:ui_panel","left":1,"right":2,"top":3,"bottom":4,"edgeMode":"tile","centerMode":"stretch"}],
  "caustics":[{"elementId":"example:ui_panel","textureId":"example:ui_fx","frameCount":1024,"speed":1000,"opacity":1,"scale":100,"blendMode":"screen"}],
  "borderEffects":[{"elementId":"example:ui_panel","maskTextureId":"example:ui_mask","opacity":1,"blendMode":"additive","tintColor":"ffffffff","scrollX":-1000,"scrollY":1000}],
  "fontShadow":{"offsetX":-32,"offsetY":32,"color":"000000ff"},
  "fontGlow":{"radius":32,"intensity":4,"color":"ffffffff","passes":8}
})JSON";

	/* Base, loose-folder typed archive, mounted pdmod, and network-staged
	 * pdmod all feed these exact public bytes to the one strict parser. */
	for (const char *transport : {"base", "local", "pdmod", "network"}) {
		INFO(transport);
		pdtheme_source_info_t info = {};
		REQUIRE(parseTheme(full, "example:theme_full", &info));
		REQUIRE(info.palette_fields == 3);
		REQUIRE(info.texture_roles == 1);
		REQUIRE(info.sound_roles == PDTHEME_SOURCE_MAX_SOUND_ROLES);
		REQUIRE(info.nineslices == 1);
		REQUIRE(info.caustics == 1);
		REQUIRE(info.border_effects == 1);
	}

	std::string why;
	REQUIRE_FALSE(parseTheme("", "example:theme_full", nullptr, &why));
	REQUIRE_FALSE(parseTheme("{", "example:theme_full", nullptr, &why));
	REQUIRE_FALSE(parseTheme(R"({"schema":"pd2.theme.v1","catalog_id":"example:theme_full","name":"X"})",
		"example:theme_full", nullptr, &why));
	REQUIRE_FALSE(parseTheme(R"({"schema":"pd2.theme.v1","catalog_id":"example:wrong","name":"X","version":"1"})",
		"example:theme_full", nullptr, &why));
	REQUIRE_FALSE(parseTheme(R"({"schema":1,"catalog_id":"example:theme_full","name":"X","version":"1"})",
		"example:theme_full", nullptr, &why));
	REQUIRE_FALSE(parseTheme(R"({"schema":"pd2.theme.v1","catalog_id":"example:theme_full","name":"X","version":"1","scanline":[]})",
		"example:theme_full", nullptr, &why));

	std::string tooMany = R"({"schema":"pd2.theme.v1","catalog_id":"example:theme_full","name":"X","version":"1","caustics":[)";
	for (int i = 0; i < PDTHEME_SOURCE_MAX_EFFECTS + 1; i++) {
		if (i) tooMany += ",";
		tooMany += R"({"elementId":"example:ui","textureId":"example:fx","frameCount":1,"speed":1,"opacity":1,"scale":1,"blendMode":"screen"})";
	}
	tooMany += "]}";
	REQUIRE_FALSE(parseTheme(tooMany, "example:theme_full", nullptr, &why));
	REQUIRE(why.find("capacity") != std::string::npos);
}
