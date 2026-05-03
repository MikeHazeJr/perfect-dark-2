/*
 * tests/test_pdbase_retired_audit.cpp -- Catalog universality pivot
 * Step 5 grep-guard.
 *
 * Pins that the pre-Step-5 .pdbase aggregate tier stays retired:
 *   - No source file under port/ or src/game/ contains "pdbase" /
 *     "PDBASE" / "loaderPdbase" / "loader_pdbase" substrings (catches
 *     any reintroduction of the old loader, parser, struct fields,
 *     or function names).
 *   - No file under devtools/ matches the same pattern (catches the
 *     extract_*_pdbase.py extractor scripts being checked back in).
 *   - No file matching base/*.pdbase exists at the repo root (catches
 *     the aggregate JSON archives reappearing).
 *
 * Allowed sites: context/ / docs / kanban / memory entries and audit
 * documents are out of scope (history is preserved there).
 *
 * @SYNC: any reintroduction of "pdbase" in the scanned trees is a
 *        regression and will fail this test. The 2026-05-03 Step 5
 *        retirement migrated row + pool population onto the universal
 *        directory walker (loaderWalkerLoadAll) + loader_pool TU.
 */

#include "catch.hpp"

#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

std::string readFile(const char *path) {
	std::ifstream in(path, std::ios::in | std::ios::binary);
	if (!in.good()) return std::string();
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

bool isCSourceExtension(const std::string &name) {
	const char *exts[] = { ".c", ".h", ".cpp", ".hpp", ".cc", ".cxx" };
	for (const char *e : exts) {
		size_t elen = strlen(e);
		if (name.size() >= elen
			&& name.compare(name.size() - elen, elen, e) == 0) {
			return true;
		}
	}
	return false;
}

bool isExtractorScript(const std::string &name) {
	return name.size() > 3
		&& name.compare(name.size() - 3, 3, ".py") == 0;
}

void walkDir(const std::string &dir, std::vector<std::string> &out_files,
             bool (*want)(const std::string &)) {
	DIR *d = opendir(dir.c_str());
	if (!d) return;
	struct dirent *de;
	while ((de = readdir(d)) != NULL) {
		std::string name = de->d_name;
		if (name == "." || name == "..") continue;
		std::string full = dir + "/" + name;
		struct stat st;
		if (stat(full.c_str(), &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) {
			walkDir(full, out_files, want);
		} else if (S_ISREG(st.st_mode) && want(name)) {
			out_files.push_back(full);
		}
	}
	closedir(d);
}

bool fileContainsPdbase(const std::string &path) {
	std::string src = readFile(path.c_str());
	if (src.empty()) return false;
	if (src.find("pdbase") != std::string::npos) return true;
	if (src.find("PDBASE") != std::string::npos) return true;
	if (src.find("loaderPdbase") != std::string::npos) return true;
	if (src.find("loader_pdbase") != std::string::npos) return true;
	return false;
}

}  /* anonymous namespace */

TEST_CASE("step5: no pdbase substrings remain in port/ source",
          "[universality-pivot][step5][grep-guard]") {
	std::vector<std::string> files;
	walkDir("port", files, isCSourceExtension);

	std::vector<std::string> offenders;
	for (const std::string &f : files) {
		if (fileContainsPdbase(f)) offenders.push_back(f);
	}

	if (!offenders.empty()) {
		printf("STEP5 GUARD: pdbase substring found in:\n");
		for (const std::string &f : offenders) printf("  %s\n", f.c_str());
	}
	REQUIRE(offenders.empty());
}

TEST_CASE("step5: no pdbase substrings remain in src/game/",
          "[universality-pivot][step5][grep-guard]") {
	std::vector<std::string> files;
	walkDir("src/game", files, isCSourceExtension);

	std::vector<std::string> offenders;
	for (const std::string &f : files) {
		if (fileContainsPdbase(f)) offenders.push_back(f);
	}

	if (!offenders.empty()) {
		printf("STEP5 GUARD: pdbase substring found in:\n");
		for (const std::string &f : offenders) printf("  %s\n", f.c_str());
	}
	REQUIRE(offenders.empty());
}

TEST_CASE("step5: no pdbase extractor scripts under devtools/",
          "[universality-pivot][step5][grep-guard]") {
	std::vector<std::string> files;
	walkDir("devtools", files, isExtractorScript);

	std::vector<std::string> offenders;
	for (const std::string &f : files) {
		/* Catch both the file name pattern (extract_*_pdbase.py) and any
		 * residual content that mentions the legacy archive format. */
		if (f.find("pdbase") != std::string::npos) offenders.push_back(f);
		else if (fileContainsPdbase(f)) offenders.push_back(f);
	}

	if (!offenders.empty()) {
		printf("STEP5 GUARD: pdbase extractor or reference under devtools/:\n");
		for (const std::string &f : offenders) printf("  %s\n", f.c_str());
	}
	REQUIRE(offenders.empty());
}

TEST_CASE("step5: no base/*.pdbase aggregate archives remain",
          "[universality-pivot][step5][grep-guard]") {
	const char *paths[] = {
		"base/weapons.pdbase",
		"base/heads.pdbase",
		"base/bodies.pdbase",
		"base/arenas.pdbase",
	};
	for (const char *p : paths) {
		struct stat st;
		REQUIRE(stat(p, &st) != 0);
	}
}
