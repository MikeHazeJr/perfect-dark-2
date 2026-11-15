// B-1160: the client wrote outside the folders it was told to use.
//
// - Thirteen menu saves passed configSave the bare name "pd.ini". A bare name
//   resolves like a read (catalog, mods, then the base folder), so the config
//   landed in the base folder while configInit reads $S/pd.ini; and fsInit
//   copied that stray file into the next fresh --savedir started from there.
// - A --moddir that did not exist yet was dropped, so mod discovery mounted
//   the folders beside the executable (on the week layout a sibling git
//   checkout) and ran the one-shot folder migration there.
// - Fonts, themes and the migration bench kept their own copies of the
//   discovery list and ignored --moddir altogether.
#include "catch.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string readSource(const std::string &path)
{
	std::ifstream input(path, std::ios::in | std::ios::binary);
	REQUIRE(input.good());
	std::ostringstream out;
	out << input.rdbuf();
	return out.str();
}

// Removes block and line comments so a path named in a comment is not
// counted as code. String and character literals are kept as they are, and
// skipped whole so a quote inside one cannot start a string.
std::string stripComments(const std::string &text)
{
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size(); ) {
		if (text[i] == '"' || text[i] == '\'') {
			const char quote = text[i];
			const size_t start = i++;
			while (i < text.size() && text[i] != quote && text[i] != '\n') {
				i += text[i] == '\\' ? 2 : 1;
			}
			i = i < text.size() ? i + 1 : text.size();
			out.append(text, start, std::min(i, text.size()) - start);
		} else if (text.compare(i, 2, "/*") == 0) {
			const size_t end = text.find("*/", i + 2);
			i = end == std::string::npos ? text.size() : end + 2;
		} else if (text.compare(i, 2, "//") == 0) {
			const size_t end = text.find('\n', i);
			i = end == std::string::npos ? text.size() : end;
		} else {
			out.push_back(text[i++]);
		}
	}
	return out;
}

std::vector<std::string> projectSources()
{
	std::vector<std::string> files;
	for (const char *root : {"port/src", "port/fast3d", "port/include", "src"}) {
		for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
			if (!entry.is_regular_file()) {
				continue;
			}
			const std::string ext = entry.path().extension().string();
			if (ext == ".c" || ext == ".cpp" || ext == ".h") {
				files.push_back(entry.path().generic_string());
			}
		}
	}
	REQUIRE(files.size() > 100);
	return files;
}

} // namespace

TEST_CASE("B-1160: every config save names its folder", "[b1160][config-save][static]")
{
	std::vector<std::string> offenders;
	const std::string call = "configSave(\"";
	for (const auto &file : projectSources()) {
		const std::string code = stripComments(readSource(file));
		for (size_t at = code.find(call); at != std::string::npos; at = code.find(call, at + 1)) {
			if (code.compare(at + call.size(), 1, "$") != 0) {
				offenders.push_back(file + ": " + code.substr(at, 40));
			}
		}
	}
	INFO("configSave with a bare file name (use CONFIG_PATH): " << offenders.size());
	for (const auto &offender : offenders) {
		INFO(offender);
	}
	REQUIRE(offenders.empty());

	const std::string config = readSource("port/src/config.c");
	REQUIRE(config.find("if (fname[0] != '$' && !fsPathIsAbsolute(fname) && !fsPathIsCwdRelative(fname)) {")
	        < config.find("fsFullPath(fname, destination, sizeof(destination));"));
	REQUIRE(config.find("configSave: refusing bare file name") != std::string::npos);
	REQUIRE(readSource("port/include/config.h").find("#define CONFIG_PATH \"$S/\" CONFIG_FNAME")
	        != std::string::npos);
}

TEST_CASE("B-1160: one list of mod roots, and an explicit --moddir is kept", "[b1160][mods][static]")
{
	// Only fs.c spells the discovery list, and since B-1169 no file names the
	// folder beside the install or a mods/ under the working folder.
	std::vector<std::string> copies;
	for (const auto &file : projectSources()) {
		const std::string code = stripComments(readSource(file));
		if (code.find("\"$E/../mods\"") != std::string::npos
		    || code.find("\"$E/../\" MODMGR_MODS_DIR") != std::string::npos
		    || code.find("\"./\" MODMGR_MODS_DIR") != std::string::npos
		    || code.find("\"./mods\"") != std::string::npos) {
			copies.push_back(file);
		}
	}
	for (const auto &copy : copies) {
		INFO("own copy of the mod discovery list: " << copy);
	}
	REQUIRE(copies.empty());

	for (const char *scanner : {"port/src/modmgr.c", "port/fast3d/pdgui_font_mod.cpp",
	                            "port/fast3d/pdgui_theme.cpp", "port/fast3d/pdgui_theme_loader.cpp",
	                            "port/src/modarchive_bench.c"}) {
		INFO(scanner);
		REQUIRE(readSource(scanner).find("fsModRoots(&roots);") != std::string::npos);
	}

	const std::string fsSource = readSource("port/src/fs.c");
	const size_t roots = fsSource.find("void fsModRoots(fs_mod_roots_t *out)");
	REQUIRE(roots != std::string::npos);
	const size_t rootsEnd = fsSource.find("\n}\n", roots);
	REQUIRE(rootsEnd != std::string::npos);
	const std::string body = fsSource.substr(roots, rootsEnd - roots);
	// Native reuse first, then the explicit folder, then discovery.
	const size_t native = body.find("if (nativeWritePolicyActive()) {");
	const size_t explicitDir = body.find("if (modDir[0]) {");
	const size_t discovery = body.find("out->count = modsInstallRoots(exeDir, baseDir, paths");
	REQUIRE(native != std::string::npos);
	REQUIRE(explicitDir != std::string::npos);
	REQUIRE(discovery != std::string::npos);
	REQUIRE(native < explicitDir);
	REQUIRE(explicitDir < discovery);
	REQUIRE(body.find("out->explicit_dir = 1;", explicitDir) < discovery);
	// B-1169: discovery reads only the install's own folders, never the
	// folder beside the install or the working folder's mods/.
	const std::string discoveryCode = stripComments(body.substr(explicitDir));
	REQUIRE(discoveryCode.find("$E/../") == std::string::npos);
	REQUIRE(discoveryCode.find("\"./\" MODMGR_MODS_DIR") == std::string::npos);

	// fsInit keeps an explicit --moddir before it exists.
	REQUIRE(fsSource.find("could not find specified moddir") == std::string::npos);
	REQUIRE(fsSource.find("// path is explicit; use it as-is\n\t\t\tfsFullPath(path, modDir, sizeof(modDir));")
	        != std::string::npos);
	REQUIRE(fsSource.find("fsFullPath(strFmt(\"./%s\", path), modDir, sizeof(modDir));") != std::string::npos);
	REQUIRE(fsSource.find("does not exist yet; it is created as the mod folder") != std::string::npos);

	// modmgr creates that folder; without --moddir it creates the install's
	// own folder (B-1169), never ./mods.
	const std::string modmgr = readSource("port/src/modmgr.c");
	REQUIRE(modmgr.find("fsCreateDir(roots.path[roots.count - 1]);")
	        < modmgr.find("dir = opendir(candidates[i]);"));
	REQUIRE(stripComments(modmgr).find("fsCreateDir(\"./\" MODMGR_MODS_DIR)") == std::string::npos);
	REQUIRE(stripComments(modmgr).find("\"./\" MODMGR_MODS_DIR") == std::string::npos);

	// The standalone migration probes no root by creating a file in it.
	const std::string bench = stripComments(readSource("port/src/modarchive_bench.c"));
	const size_t migration = bench.find("static void runStandaloneMigration(void)");
	const size_t migrationEnd = bench.find("void modArchiveRunBenchmark(void)");
	REQUIRE(migration != std::string::npos);
	REQUIRE(migrationEnd != std::string::npos);
	REQUIRE(migration < migrationEnd);
	const std::string migrationBody = bench.substr(migration, migrationEnd - migration);
	REQUIRE(migrationBody.find("fopen(") == std::string::npos);
	REQUIRE(migrationBody.find("MIGRATE: refused under the native write policy")
	        < migrationBody.find("fsModRoots(&roots);"));
}

TEST_CASE("B-1160: an explicit --savedir gets no migrated files", "[b1160][static]")
{
	const std::string fsSource = readSource("port/src/fs.c");
	REQUIRE(fsSource.find("if (!portable && !sysArgGetString(\"--savedir\") && !nativeWritePolicyActive()) {")
	        < fsSource.find("static const char *migrateFiles[] = { \"eeprom.bin\", CONFIG_FNAME, NULL };"));
}
