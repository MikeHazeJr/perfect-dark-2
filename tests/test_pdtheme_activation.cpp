#include "catch.hpp"

#include <cstring>
#include <map>
#include <fstream>
#include <sstream>
#include <string>

extern "C" {
#include "assetcatalog.h"
#include "mod_sequence_path.h"
#include "pdtheme_activation.h"
}

static std::string readText(const char *path)
{
	std::ifstream in(path, std::ios::binary);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

namespace {
struct Refs {
	std::map<std::string, int> refs;
	std::string rejected;
};

s32 loadTheme(const char *id, void *userdata)
{
	auto *refs = static_cast<Refs *>(userdata);
	if (refs->rejected == id) return 0;
	refs->refs[id]++;
	return 1;
}

void releaseTheme(const char *id, void *userdata)
{
	auto *refs = static_cast<Refs *>(userdata);
	refs->refs[id]--;
}
}

TEST_CASE("pdtheme activation keeps last good ownership across failure and swap",
	"[modding][pdxxx][pdtheme][lifecycle][T-ASSETS-030]")
{
	pdtheme_activation_state_t state = {};
	Refs refs;

	REQUIRE(pdthemeActivationBegin(&state, "base:theme_blue", loadTheme, &refs));
	pdthemeActivationCommit(&state, releaseTheme, &refs);
	REQUIRE(refs.refs["base:theme_blue"] == 1);
	REQUIRE(std::string(state.active_id) == "base:theme_blue");

	REQUIRE(pdthemeActivationBegin(&state, "mod:theme_bad", loadTheme, &refs));
	pdthemeActivationAbort(&state, releaseTheme, &refs);
	REQUIRE(refs.refs["mod:theme_bad"] == 0);
	REQUIRE(refs.refs["base:theme_blue"] == 1);
	REQUIRE(std::string(state.active_id) == "base:theme_blue");

	REQUIRE(pdthemeActivationBegin(&state, "mod:theme_green", loadTheme, &refs));
	pdthemeActivationCommit(&state, releaseTheme, &refs);
	REQUIRE(refs.refs["base:theme_blue"] == 0);
	REQUIRE(refs.refs["mod:theme_green"] == 1);
	REQUIRE(std::string(state.active_id) == "mod:theme_green");

	pdthemeActivationShutdown(&state, releaseTheme, &refs);
	REQUIRE(refs.refs["mod:theme_green"] == 0);
	REQUIRE(state.active_id[0] == '\0');
}

TEST_CASE("pdtheme repeated activation has zero reference growth",
	"[modding][pdxxx][pdtheme][lifecycle][rollback][T-ASSETS-030]")
{
	pdtheme_activation_state_t state = {};
	Refs refs;

	for (int i = 0; i < 64; i++) {
		REQUIRE(pdthemeActivationBegin(&state, "mod:theme_repeat", loadTheme, &refs));
		pdthemeActivationCommit(&state, releaseTheme, &refs);
		REQUIRE(refs.refs["mod:theme_repeat"] == 1);
	}

	refs.rejected = "mod:theme_rejected";
	REQUIRE_FALSE(pdthemeActivationBegin(&state, "mod:theme_rejected", loadTheme, &refs));
	REQUIRE(refs.refs["mod:theme_rejected"] == 0);
	REQUIRE(refs.refs["mod:theme_repeat"] == 1);

	pdthemeActivationShutdown(&state, releaseTheme, &refs);
	REQUIRE(refs.refs["mod:theme_repeat"] == 0);
}

TEST_CASE("nested pdtheme vector font paths retain the complete public source chain",
	"[modding][pdxxx][pdtheme][font][path][T-ASSETS-030]")
{
	asset_entry_t font = {};
	const std::string nested =
		"./mods/installed/example_typed_pdxxx_basic.pdmod::themes/"
		"tri_theme.pdtheme::dependencies/assets/font/"
		"tri_theme_font.pdfont::font.otf";

	REQUIRE(nested.size() > 128);
	REQUIRE(nested.size() < sizeof(font.ext.font.font_file));
	std::memcpy(font.ext.font.font_file, nested.c_str(), nested.size() + 1);
	REQUIRE(std::string(font.ext.font.font_file) == nested);
	REQUIRE(std::string(font.ext.font.font_file).rfind(".otf") ==
		nested.size() - 4);

	const std::string catalog = readText("port/include/assetcatalog.h");
	REQUIRE(catalog.find("char font_file[FS_MAXPATH]") != std::string::npos);
	REQUIRE(catalog.find("char metrics_file[FS_MAXPATH]") != std::string::npos);
}

TEST_CASE("nested pdtheme song sibling lookup preserves every archive boundary",
	"[modding][pdxxx][pdtheme][music][path][T-ASSETS-030]")
{
	char resolved[FS_MAXPATH + 1] = {};
	const char *nested =
		"./mods/installed/example.pdmod::themes/demo.pdtheme::dependencies/"
		"assets/music/song.pdsong::sequence.mid";

	REQUIRE(modSequenceSiblingPath(nested, "sequence.json", resolved,
		sizeof(resolved)) == 0);
	REQUIRE(std::string(resolved) ==
		"./mods/installed/example.pdmod::themes/demo.pdtheme::dependencies/"
		"assets/music/song.pdsong::sequence.json");

	REQUIRE(modSequenceSiblingPath("data/music/base.pdsong::sequence.mid",
		"music.ini", resolved, sizeof(resolved)) == 0);
	REQUIRE(std::string(resolved) == "data/music/base.pdsong::music.ini");

	REQUIRE(modSequenceSiblingPath("mods/music/loose.mid", "sequence.json",
		resolved, sizeof(resolved)) == 0);
	REQUIRE(std::string(resolved) == "mods/music/sequence.json");

	char too_small[8] = {};
	REQUIRE(modSequenceSiblingPath(nested, "music.ini", too_small,
		sizeof(too_small)) == -1);
}

TEST_CASE("production theme loader owns public dependency lifecycle exactly once",
	"[modding][pdxxx][pdtheme][lifecycle][production][T-ASSETS-030]")
{
	const std::string loader = readText("port/fast3d/pdgui_theme_loader.cpp");
	const std::string audio = readText("port/fast3d/pdgui_audio.cpp");
	const std::string distrib = readText("port/src/net/netdistrib.c");
	const std::string scanner = readText("port/src/assetcatalog_scanner.c");
	const std::string walker = readText("port/src/loader_walker_meta.c");
	const std::string main = readText("port/src/main.c");

	REQUIRE(loader.find("catalogLoadTypedAsset(ASSET_THEME, catalog_id)") !=
		std::string::npos);
	REQUIRE(loader.find("pdthemeActivationBegin(&s_ActivationState") !=
		std::string::npos);
	REQUIRE(loader.find("pdthemeActivationAbort(&s_ActivationState") !=
		std::string::npos);
	REQUIRE(loader.find("pdthemeActivationCommit(&s_ActivationState") !=
		std::string::npos);
	REQUIRE(loader.find("pdthemeActivationShutdown(&s_ActivationState") !=
		std::string::npos);
	REQUIRE(loader.find(
		"PDTHEME_FAST_CACHE_KIND \"pdtheme_builtin_v3_manifest_identity\"") !=
		std::string::npos);
	REQUIRE(loader.find("\"  \\\"id\\\": \\\"%s\\\",\\n\"") !=
		std::string::npos);

	/* Consumer validation must not take independent child references. The
	 * parent lifecycle already owns all four declared children. */
	REQUIRE(audio.find("return catalogLoadTypedAsset(ASSET_AUDIO, catalogId)") ==
		std::string::npos);
	const auto validate = loader.find("validate_theme_def_consumers");
	const auto apply = loader.find("static s32 apply_theme_def", validate);
	REQUIRE(validate != std::string::npos);
	REQUIRE(apply != std::string::npos);
	REQUIRE(loader.substr(validate, apply - validate).find(
		"catalogLoadTypedAsset(ASSET_AUDIO") == std::string::npos);

	/* Base walker, local typed archive, mounted pdmod and network staging all
	 * converge on the same strict registrar before activation. */
	REQUIRE(walker.find("assetCatalogRegisterThemeNestedDependencies(id, file_path") !=
		std::string::npos);
	REQUIRE(scanner.find("typed_archive_entry && ini_type == ASSET_THEME") !=
		std::string::npos);
	REQUIRE(distrib.find("assetCatalogScanExternalLayoutFolderDeferred(") !=
		std::string::npos);

	/* Startup must preserve the configured ID until the real catalog/mod walk
	 * has completed, then perform exactly one main-thread apply before UI use. */
	REQUIRE(loader.find("void pdguiThemeLoaderOnCatalogReady(void)") !=
		std::string::npos);
	REQUIRE(loader.find("if (!s_LoaderInitDone || s_CatalogReadyApplied) return;") !=
		std::string::npos);
	REQUIRE(loader.find(
		"strcmp(s_ActivationState.active_id, s_ActiveThemeId) != 0") !=
		std::string::npos);
	REQUIRE(loader.find("catalogLoadTypedAsset(ASSET_THEME, entry->id)") !=
		std::string::npos);
	REQUIRE(loader.find("catalogReleaseTypedAsset(ASSET_THEME, entry->id)") !=
		std::string::npos);
	const auto init = loader.find("void pdguiThemeLoaderInit(void)");
	const auto ready = loader.find("void pdguiThemeLoaderOnCatalogReady(void)");
	REQUIRE(init != std::string::npos);
	REQUIRE(ready != std::string::npos);
	REQUIRE(loader.substr(init, ready - init).find(
		"saved theme '%s' not resolvable on startup") == std::string::npos);
	const auto workerJoin = main.find("bootPoolWaitIdle();");
	const auto readyHook = main.find("pdguiThemeLoaderOnCatalogReady();");
	const auto scheduler = main.find("bootCreateSched();");
	REQUIRE(workerJoin < readyHook);
	REQUIRE(readyHook < scheduler);
}
