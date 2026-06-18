/*
 * tests/test_uichrome_paths_pin.cpp -- Phase 3 Pass B Slice 13 path pin.
 *
 * Locks the migration of UI chrome textures to data/<romid>/ui/*.pdui (the
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
 *        data/<romid>/ui/*.pdui path canonical.  Mods may overlay via the
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

TEST_CASE("uichrome-paths: catalog entries point at pdui archives",
          "[catalog][uichrome][slice13][pass-b]") {
	const std::string src = readSourceFile("port/fast3d/pdgui_theme.cpp");
	REQUIRE(!src.empty());

	/* k_PduiEntries[] entries -- catalog id / .pdui slug pairs. */
	const char *expected_ids[] = {
		"\"base:ui_bg_haze\"",
		"\"base:ui_particles\"",
		"\"base:ui_noise_sm\"",
		"\"base:ui_noise_lg\"",
		"\"base:ui_grad_bar\"",
		"\"base:ui_mirror_tile\"",
		"\"base:ui_dot_tile\"",
		"\"base:ui_nuke\"",
		"\"base:ui_bg_alt\"",
		"\"base:ui_deco\"",
		"\"base:ui_icon_a\"",
		"\"base:ui_icon_b\"",
		"\"base:ui_icon_c\"",
		"\"base:ui_stars\"",
		"\"base:ui_chrome_frame\"",
	};
	for (const char *id : expected_ids) {
		INFO("expected catalog id: " << id);
		REQUIRE(src.find(id) != std::string::npos);
	}
}

TEST_CASE("uichrome-paths: extraction destination writes pdui archives",
          "[catalog][uichrome][slice13][pass-b]") {
	const std::string src = readSourceFile("port/fast3d/pdgui_theme.cpp");
	REQUIRE(!src.empty());

	REQUIRE(src.find("#define PDUI_OUT_DIR \"ui\"") != std::string::npos);
	REQUIRE(src.find("%s/%s/%s.pdui") != std::string::npos);
	REQUIRE(src.find("assetArchiveWriterInit(&asset_writer, aw, \"ui\"") != std::string::npos);
	REQUIRE(src.find("assetArchiveWriterAddPublicMem") != std::string::npos);
}

TEST_CASE("uichrome-paths: directory creation targets data rom ui dir",
          "[catalog][uichrome][slice13][pass-b]") {
	const std::string src = readSourceFile("port/fast3d/pdgui_theme.cpp");
	REQUIRE(!src.empty());

	REQUIRE(src.find("fsDataDirEnsure()") != std::string::npos);
	REQUIRE(src.find("fsDataDir(dataDirBuf, sizeof(dataDirBuf))") != std::string::npos);
	REQUIRE(src.find("fsCreateDir(ui_dir)") != std::string::npos);
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

TEST_CASE("uichrome-paths: 15 pdui entries pinned",
          "[catalog][uichrome][slice13][pass-b][counts]") {
	const std::string src = readSourceFile("port/fast3d/pdgui_theme.cpp");
	REQUIRE(!src.empty());

	/* k_PduiEntries[] has 15 archive rows, including ui_stars and chrome_frame. */
	REQUIRE(countOccurrences(src, "{ \"base:ui_") == 15u);

	/* Each slug becomes data/<romid>/ui/<slug>.pdui. */
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
		"\"ui_chrome_frame\"",
	};
	for (const char *fn : extract_filenames) {
		INFO("expected k_Extracts filename: " << fn);
		REQUIRE(src.find(fn) != std::string::npos);
	}
}
