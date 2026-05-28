/*
 * tests/test_updater_rom_cleanup_static.cpp
 *
 * Static pin for B-338: updater stale-file cleanup must never delete the
 * user's BYOR ROM from the install root. Release zips intentionally exclude
 * ROMs, so root-level ROM files must be protected outside the configurable
 * folder list in both updater implementations.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readSourceFile(const char *path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

} /* anonymous namespace */

TEST_CASE("updater cleanup protects root ROM files in both apply paths",
          "[updater][rom][static][b338]")
{
	const std::string ingame = readSourceFile("port/src/updater.c");
	const std::string standalone = readSourceFile("port/src/updater_standalone/updater_gui.c");
	REQUIRE(!ingame.empty());
	REQUIRE(!standalone.empty());

	const std::string sources[] = { ingame, standalone };
	for (const std::string &src : sources) {
		REQUIRE(src.find("isProtectedRootRomRelPath") != std::string::npos);
		REQUIRE(src.find("strchr(norm, '/') != NULL") != std::string::npos);
		REQUIRE(src.find("strrchr(norm, '.')") != std::string::npos);
		REQUIRE(src.find("strcmp(dot, \".z64\") == 0") != std::string::npos);
		REQUIRE(src.find("strcmp(dot, \".v64\") == 0") != std::string::npos);
		REQUIRE(src.find("strcmp(dot, \".n64\") == 0") != std::string::npos);
		REQUIRE(src.find("if (isProtectedRootRomRelPath(norm))") != std::string::npos);
	}
}

TEST_CASE("updater cleanup protects generated logs in both apply paths",
          "[updater][logs][static][b349]")
{
	const std::string ingame = readSourceFile("port/src/updater.c");
	const std::string standalone = readSourceFile("port/src/updater_standalone/updater_gui.c");
	REQUIRE(!ingame.empty());
	REQUIRE(!standalone.empty());

	REQUIRE(ingame.find("mods,data,extracted,saves,logs") != std::string::npos);
	REQUIRE(standalone.find("mods,data,extracted,saves,logs") != std::string::npos);
	REQUIRE(standalone.find("%s\\\\logs") != std::string::npos);
	REQUIRE(standalone.find("%s\\\\updater") != std::string::npos);
	REQUIRE(standalone.find("pd-updater.log") != std::string::npos);
}

TEST_CASE("standalone updater warns that root ROM files are preserved",
          "[updater][rom][static][b338]")
{
	const std::string standalone = readSourceFile("port/src/updater_standalone/updater_gui.c");
	REQUIRE(!standalone.empty());

	REQUIRE(standalone.find("root ROM files") != std::string::npos);
	REQUIRE(standalone.find("*.z64/*.v64/*.n64") != std::string::npos);
}

TEST_CASE("release package keeps one ROM instruction file and excludes root README",
          "[release][layout][static][b349]")
{
	const std::string release = readSourceFile("devtools/release.ps1");
	REQUIRE(!release.empty());

	REQUIRE(release.find("put_your_rom_here.txt") != std::string::npos);
	REQUIRE(release.find("README\\.txt$") != std::string::npos);
	REQUIRE(release.find("Set-Content -LiteralPath $readmePath") != std::string::npos);
}

TEST_CASE("release package includes typed pdxxx modding examples",
          "[release][layout][static][c3811]")
{
	const std::string release = readSourceFile("devtools/release.ps1");
	const std::string examples = readSourceFile("examples/modding/README.md");
	const std::string sample = readSourceFile(
		"examples/modding/typed-pdxxx-basic/README.md");
	REQUIRE(!release.empty());
	REQUIRE(!examples.empty());
	REQUIRE(!sample.empty());

	REQUIRE(release.find("examples/modding/typed-pdxxx-basic") != std::string::npos);
	REQUIRE(release.find("examples\\modding") != std::string::npos);
	REQUIRE(release.find("typed .pdxxx samples") != std::string::npos);
	REQUIRE(release.find("Copy-Item -Path $examplesSource -Destination $examplesDest -Recurse -Force") !=
	        std::string::npos);
	REQUIRE(release.find("Test-ReleaseTypedArchiveTree -Root $DistDir") !=
	        std::string::npos);

	REQUIRE(examples.find("typed `*.pdxxx` asset archives as the authoring surface") !=
	        std::string::npos);
	REQUIRE(examples.find("`.pdmod` is only the transport wrapper") !=
	        std::string::npos);
	REQUIRE(sample.find("The content units are the typed `*.pdxxx` asset archives") !=
	        std::string::npos);
	REQUIRE(sample.find("Machine-owned manifest and provenance data lives under `_meta/`") !=
	        std::string::npos);
	REQUIRE(sample.find("`.pdmod` is not the authoring format") !=
	        std::string::npos);
}

TEST_CASE("release package validates clean typed asset archive outputs",
          "[release][layout][static][c3824]")
{
	const std::string release = readSourceFile("devtools/release.ps1");
	REQUIRE(!release.empty());

	REQUIRE(release.find("function Test-ReleaseTypedArchiveTree") !=
	        std::string::npos);
	REQUIRE(release.find("$script:TypedArchiveDescriptors") !=
	        std::string::npos);
	REQUIRE(release.find("\".pdprojectile\"") != std::string::npos);
	REQUIRE(release.find("\".pdentity\"") != std::string::npos);
	REQUIRE(release.find("\".pdmaterial\"") != std::string::npos);
	REQUIRE(release.find("\".pdtexture\"") != std::string::npos);
	REQUIRE(release.find("\".pdcharacter\"") != std::string::npos);
	REQUIRE(release.find("uses deprecated .pdwpn") != std::string::npos);
	REQUIRE(release.find("is not a zip-openable typed archive") !=
	        std::string::npos);
	REQUIRE(release.find("is missing root descriptor") != std::string::npos);
	REQUIRE(release.find("uses legacy model.ini instead of mesh.ini") !=
	        std::string::npos);
	REQUIRE(release.find("keeps machine metadata at archive root") !=
	        std::string::npos);
	REQUIRE(release.find("contains forbidden authored .bin payload") !=
	        std::string::npos);
	REQUIRE(release.find("Test-IsTypedArchiveFamilyPath") != std::string::npos);
	REQUIRE(release.find("stale typed-asset .zip inspection copy") !=
	        std::string::npos);
	REQUIRE(release.find("loose extracted typed archive folder") !=
	        std::string::npos);
}

TEST_CASE("game client logs are rooted under logs game client",
          "[logging][layout][static][b349]")
{
	const std::string system = readSourceFile("port/src/system.c");
	const std::string crash = readSourceFile("port/src/crash.c");
	REQUIRE(!system.empty());
	REQUIRE(!crash.empty());

	REQUIRE(system.find("LOG_ROOT_DIR \"logs\"") != std::string::npos);
	REQUIRE(system.find("LOG_CLIENT_DIR \"logs/game client\"") != std::string::npos);
	REQUIRE(system.find("pd-client.log") != std::string::npos);
	REQUIRE(crash.find("CRASH_LOG_DIR \"logs/game client\"") != std::string::npos);
}
