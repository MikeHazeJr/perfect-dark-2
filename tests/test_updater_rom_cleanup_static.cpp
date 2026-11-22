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

TEST_CASE("game client crash init suppresses modal fault dialogs for smoke runs",
          "[logging][crash][static][b927]")
{
	const std::string crash = readSourceFile("port/src/crash.c");
	const std::string smoke_runner = readSourceFile("tools/smoke-verify/run.ps1");
	const std::string install_harness = readSourceFile("tools/smoke-verify/lib/Install-Harness.ps1");
	REQUIRE(!crash.empty());
	REQUIRE(!smoke_runner.empty());
	REQUIRE(!install_harness.empty());

	REQUIRE(crash.find("SEM_NOGPFAULTERRORBOX") != std::string::npos);
	REQUIRE(crash.find("SEM_NOOPENFILEERRORBOX") != std::string::npos);
	REQUIRE(crash.find("SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX)") !=
	        std::string::npos);
	REQUIRE(smoke_runner.find("PdSmokeWinErrorMode") != std::string::npos);
	REQUIRE(smoke_runner.find("$loaderErrorMode = $SEM_FAILCRITICALERRORS -bor $SEM_NOGPFAULTERRORBOX -bor $SEM_NOOPENFILEERRORBOX") !=
	        std::string::npos);
	REQUIRE(smoke_runner.find("Stop-SmokeOwnedFaultProcesses") != std::string::npos);
	REQUIRE(smoke_runner.find("Stop-SmokeOwnedProcess -Ownership $script:SmokeOwnedProcesses[$pidKey]") !=
	        std::string::npos);
	REQUIRE(smoke_runner.find("Missing launch identity for smoke-owned pid=$pidKey; retain resource leases") !=
	        std::string::npos);
	REQUIRE(smoke_runner.find("Get-CimInstance Win32_Process -Filter \"name = 'WerFault.exe'\" -ErrorAction Stop") !=
	        std::string::npos);
	REQUIRE(smoke_runner.find("if ($KnownPids.Count -eq 0) { return }") != std::string::npos);
	REQUIRE(smoke_runner.find("$script:SmokeOwnedPids += [int]$proc.Id") != std::string::npos);
	REQUIRE(smoke_runner.find("$script:SmokeOwnedPids += [int]$p.Id") != std::string::npos);
	REQUIRE(smoke_runner.find("$cmd -match '--smoke'") == std::string::npos);
	REQUIRE(smoke_runner.find("$cmd -like \"*.claude\\smoke-verify*\"") == std::string::npos);
	REQUIRE(smoke_runner.find("$crashArgs = @()") != std::string::npos);
	REQUIRE(smoke_runner.find("$env:PD_SMOKE_DISABLE_CRASH_HANDLER -eq \"1\"") !=
	        std::string::npos);
	REQUIRE(smoke_runner.find("$crashArgs = @(\"--no-crash-handler\")") !=
	        std::string::npos);
	REQUIRE(smoke_runner.find("$crashArgs = @(\"--no-crash-handler\")\n\t\tif") ==
	        std::string::npos);
	REQUIRE(install_harness.find("New-NetFirewallRule -DisplayName $displayName `") !=
	        std::string::npos);
	REQUIRE(install_harness.find("-ErrorAction Stop | Out-Null") !=
	        std::string::npos);
}

TEST_CASE("video shutdown only frees owned display mode storage",
          "[video][shutdown][static][b928]")
{
	const std::string video = readSourceFile("port/src/video.c");
	REQUIRE(!video.empty());

	REQUIRE(video.find("static displaymode *vidModes = &vidModeDefault;") !=
	        std::string::npos);
	REQUIRE(video.find("static bool vidModesOwned = false;") != std::string::npos);
	REQUIRE(video.find("displaymode *resizedModeList = sysMemRealloc") !=
	        std::string::npos);
	REQUIRE(video.find("sysMemFree(modeList);") != std::string::npos);
	REQUIRE(video.find("if (vidModesOwned)") != std::string::npos);
	REQUIRE(video.find("sysMemFree(vidModes);") != std::string::npos);
	REQUIRE(video.find("vidModes = &vidModeDefault;") != std::string::npos);
	REQUIRE(video.find("vidModesOwned = false;") != std::string::npos);
	REQUIRE(video.find("free(vidModes);") == std::string::npos);
}

TEST_CASE("release.ps1 checks the release body against GitHub's limit before it commits, builds or pushes",
          "[release][static][b1314]")
{
	const std::string release = readSourceFile("devtools/release.ps1");
	REQUIRE(!release.empty());

	/* GitHub refuses a body over 125,000 characters only at step 5, after step 4 has pushed the tag,
	 * so the check comes before the version is written and before step 0 commits and pushes. */
	const size_t limit = release.find("$GhReleaseBodyLimit = 125000");
	const size_t version = release.find("# Resolve version / nightly date code");
	const size_t step0 = release.find("[0/8]");
	REQUIRE(limit != std::string::npos);
	REQUIRE(version != std::string::npos);
	REQUIRE(step0 != std::string::npos);
	CHECK(limit < version);
	CHECK(limit < step0);
	CHECK(release.find("if ($notesChars -gt $GhReleaseBodyLimit)") != std::string::npos);
	CHECK(release.find("[string]$NotesFile = \"\"") != std::string::npos);
	CHECK(release.find("Test-ReleaseNotesLookStale -Path $ReleaseNotes)") != std::string::npos);
	CHECK(release.find("$ReleaseNotes = \"UNRELEASED.md\"") == std::string::npos);

	/* A publish that did not succeed keeps the zip and its sidecars. */
	CHECK(release.find("$keepUploads = (-not $DryRun -and -not $SkipPush -and ($script:ReleasePublishOk -ne $true))") !=
	      std::string::npos);
	CHECK(release.find("if (-not $DryRun -and -not $SkipPush -and -not $keepUploads) {") != std::string::npos);
}

TEST_CASE("release.ps1 never makes a signing key and never commits the updater's public key",
          "[release][static][b1315]")
{
	const std::string raw = readSourceFile("devtools/release.ps1");
	REQUIRE(!raw.empty());
	/* .gitattributes checks PowerShell scripts out with CRLF; the two-line pins below compare without CR. */
	std::string release;
	release.reserve(raw.size());
	for (char c : raw) {
		if (c != '\r') {
			release += c;
		}
	}

	/* A key made in step 2b rewrote port/include/updater_pubkey.h after the build, signed with a key no
	 * installed client trusts, and step 4 committed the header (B-1315). */
	CHECK(release.find("(Join-Path $PSScriptRoot \"keygen.ps1\")") == std::string::npos);
	CHECK(release.find("[string]$SigningKey = \"\"") != std::string::npos);
	const size_t keycheck = release.find("$SigningKeyPath = $(if ($SigningKey)");
	const size_t version = release.find("# Resolve version / nightly date code");
	REQUIRE(keycheck != std::string::npos);
	REQUIRE(version != std::string::npos);
	CHECK(keycheck < version);
	CHECK(release.find("$signKey = $SigningKeyPath") != std::string::npos);

	/* Every release commit is refused while the header differs from HEAD: the definition and three calls. */
	size_t uses = 0;
	for (size_t at = release.find("Assert-UpdaterKeyUnchanged"); at != std::string::npos;
	     at = release.find("Assert-UpdaterKeyUnchanged", at + 1)) {
		uses++;
	}
	CHECK(uses == 4);

	/* B-1316: step 4's pulls run with Continue, or Windows PowerShell 5.1 stops on git's stderr. */
	CHECK(release.find("    $ErrorActionPreference = \"Continue\"\n    $rebaseOut = git pull --rebase origin $currentBranch 2>&1") !=
	      std::string::npos);
	CHECK(release.find("            $ErrorActionPreference = \"Continue\"\n            $rebaseOut = git pull --rebase origin $currentBranch 2>&1") !=
	      std::string::npos);
}
