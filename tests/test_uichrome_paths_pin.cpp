/*
 * tests/test_uichrome_paths_pin.cpp -- Phase 3 Pass B Slice 13 path pin.
 *
 * Locks the migration of UI chrome textures to data/ui/textures/ (the
 * BYOR tier alongside per-romid segs).  Pre-Slice-13 they lived under
 * mods/base-ui/textures/ (the user-overlay tier); the initial Slice 13
 * commit moved them to base/ui/textures/ but that was a misclassi-
 * fication -- the textures are extracted from the user-supplied ROM,
 * never ship with the project, and therefore belong in the BYOR data/
 * tier.  This pin enforces the corrected destination.
 *
 * Static text grep against port/fast3d/pdgui_theme.cpp + port/include/
 * pdgui_theme.h.  The audit is at
 * context/audits/catalog-phase3-passb-slice13-uichrome-2026-05-02.md.
 *
 * @SYNC: any future restructuring of pdgui_theme.cpp must keep the
 *        data/ui/textures/ path canonical.  Mods may overlay via the
 *        modvfs path but the runtime extraction destination + catalog
 *        source-of-truth lives under data/.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readSourceFile(const char *path) {
	std::ifstream in(path, std::ios::in | std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

unsigned countOccurrences(const std::string &haystack, const std::string &needle) {
	if (needle.empty()) return 0u;
	unsigned count = 0;
	std::size_t pos = 0;
	while ((pos = haystack.find(needle, pos)) != std::string::npos) {
		++count;
		pos += needle.size();
	}
	return count;
}

} /* anonymous namespace */

TEST_CASE("uichrome-paths: catalog entries point at data/ui/textures",
          "[catalog][uichrome][slice13][pass-b]") {
	const std::string src = readSourceFile("port/fast3d/pdgui_theme.cpp");
	REQUIRE(!src.empty());

	/* k_UiTextures[] entries -- 13 distinct catalog id / path pairs */
	const char *expected_paths[] = {
		"data/ui/textures/ui_bg_haze.tga",
		"data/ui/textures/ui_particles.tga",
		"data/ui/textures/ui_noise_sm.tga",
		"data/ui/textures/ui_noise_lg.tga",
		"data/ui/textures/ui_grad_bar.tga",
		"data/ui/textures/ui_mirror_tile.tga",
		"data/ui/textures/ui_dot_tile.tga",
		"data/ui/textures/ui_nuke.tga",
		"data/ui/textures/ui_bg_alt.tga",
		"data/ui/textures/ui_deco.tga",
		"data/ui/textures/ui_icon_a.tga",
		"data/ui/textures/ui_icon_b.tga",
		"data/ui/textures/ui_icon_c.tga",
	};
	for (const char *p : expected_paths) {
		INFO("expected catalog path: " << p);
		REQUIRE(src.find(p) != std::string::npos);
	}
}

TEST_CASE("uichrome-paths: extraction destination writes to data/ui/textures",
          "[catalog][uichrome][slice13][pass-b]") {
	const std::string src = readSourceFile("port/fast3d/pdgui_theme.cpp");
	REQUIRE(!src.empty());

	/* TGA + PNG + 9slice JSON destination format strings.
	 * Each appears at least once in the extraction code path. */
	REQUIRE(src.find("data/ui/textures/%s.tga") != std::string::npos);
	REQUIRE(src.find("data/ui/textures/%s.png") != std::string::npos);
	REQUIRE(src.find("data/ui/textures/%s.9slice.json") != std::string::npos);
}

TEST_CASE("uichrome-paths: directory creation targets data/ui/textures",
          "[catalog][uichrome][slice13][pass-b]") {
	const std::string src = readSourceFile("port/fast3d/pdgui_theme.cpp");
	REQUIRE(!src.empty());

	REQUIRE(src.find("fsCreateDir(\"data\")") != std::string::npos);
	REQUIRE(src.find("fsCreateDir(\"data/ui\")") != std::string::npos);
	REQUIRE(src.find("fsCreateDir(\"data/ui/textures\")") != std::string::npos);
}

TEST_CASE("uichrome-paths: legacy mods/base-ui paths fully retired in code",
          "[catalog][uichrome][slice13][pass-b]") {
	const std::string cpp = readSourceFile("port/fast3d/pdgui_theme.cpp");
	const std::string hdr = readSourceFile("port/include/pdgui_theme.h");
	REQUIRE(!cpp.empty());
	REQUIRE(!hdr.empty());

	/* Path string must not appear anywhere in either file. */
	REQUIRE(cpp.find("mods/base-ui/textures/") == std::string::npos);
	REQUIRE(cpp.find("mods/base-ui/") == std::string::npos);
	REQUIRE(hdr.find("mods/base-ui/textures/") == std::string::npos);
	REQUIRE(hdr.find("mods/base-ui/") == std::string::npos);
}

TEST_CASE("uichrome-paths: base/ui/textures misclassification fully retired",
          "[catalog][uichrome][slice13][pass-b]") {
	const std::string cpp = readSourceFile("port/fast3d/pdgui_theme.cpp");
	const std::string hdr = readSourceFile("port/include/pdgui_theme.h");
	REQUIRE(!cpp.empty());
	REQUIRE(!hdr.empty());

	/* The interim base/ui/textures destination from the initial Slice
	 * 13 commit must not reappear: ROM-extracted content lives in the
	 * BYOR data/ tier, never in the project-authored base/ tier. */
	REQUIRE(cpp.find("base/ui/textures/") == std::string::npos);
	REQUIRE(cpp.find("base/ui/textures") == std::string::npos);
	REQUIRE(hdr.find("base/ui/textures/") == std::string::npos);
	REQUIRE(hdr.find("base/ui/textures") == std::string::npos);

	/* Defensive: no fsCreateDir("base/...") for ui chrome. */
	REQUIRE(cpp.find("fsCreateDir(\"base\")") == std::string::npos);
	REQUIRE(cpp.find("fsCreateDir(\"base/ui\")") == std::string::npos);
	REQUIRE(cpp.find("fsCreateDir(\"base/ui/textures\")") == std::string::npos);
}

TEST_CASE("uichrome-paths: mod.json autogen retired",
          "[catalog][uichrome][slice13][pass-b]") {
	const std::string src = readSourceFile("port/fast3d/pdgui_theme.cpp");
	REQUIRE(!src.empty());

	/* The literal path the autogen wrote must be gone. */
	REQUIRE(src.find("mods/base-ui/mod.json") == std::string::npos);
	/* The marker comment opening the autogen block must be gone too. */
	REQUIRE(src.find("Write mod.json manifest so the mod manager") == std::string::npos);
}

TEST_CASE("uichrome-paths: 13 catalog entries pinned, 14 extraction filenames pinned",
          "[catalog][uichrome][slice13][pass-b][counts]") {
	const std::string src = readSourceFile("port/fast3d/pdgui_theme.cpp");
	REQUIRE(!src.empty());

	/* k_UiTextures[] has exactly 13 entries (catalog rows). */
	REQUIRE(countOccurrences(src, "{ \"base:ui_") == 13u);

	/* k_Extracts[] has 14 distinct filenames (the catalog row count
	 * plus ui_stars which is extracted but not registered as ASSET_UI;
	 * see audit Section A.6 for the asymmetry rationale). */
	const char *extract_filenames[] = {
		"\"ui_noise_sm\"",
		"\"ui_particles\"",
		"\"ui_noise_lg\"",
		"\"ui_grad_bar\"",
		"\"ui_mirror_tile\"",
		"\"ui_bg_haze\"",
		"\"ui_dot_tile\"",
		"\"ui_nuke\"",
		"\"ui_bg_alt\"",
		"\"ui_icon_a\"",
		"\"ui_icon_b\"",
		"\"ui_icon_c\"",
		"\"ui_deco\"",
		"\"ui_stars\"",
	};
	for (const char *fn : extract_filenames) {
		INFO("expected k_Extracts filename: " << fn);
		REQUIRE(src.find(fn) != std::string::npos);
	}
}
