/**
 * test_modpack_archive.cpp -- the component archives inside a .pdpack
 * (port/src/modpack_archive.c) and the import route in port/src/modpack.c.
 *
 * CAT-C02 (T-MODDING-023): the same files give the same archive bytes,
 * whatever order the folder lists them in, with members in strcmp order.
 * CAT-C01 (T-MODDING-022): a pack's folder names must be one safe path
 * component, member paths go only through the checked PDCA extractor, and the
 * import is rooted at the writable mods folder.
 */
#include "catch.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "fs.h"
#include "modpack_archive.h"
#include "pdca_extract_transaction.h"
}

namespace {

namespace fs = std::filesystem;

class ModpackTempRoot {
public:
	explicit ModpackTempRoot(const char *leaf)
	{
		root = fs::temp_directory_path() / leaf;
		std::error_code ec;
		fs::remove_all(root, ec);
		fs::create_directories(root);
	}
	~ModpackTempRoot()
	{
		std::error_code ec;
		fs::remove_all(root, ec);
	}
	fs::path root;
};

void writeText(const fs::path &path, const std::string &text)
{
	fs::create_directories(path.parent_path());
	std::ofstream out(path, std::ios::binary);
	out << text;
}

std::string readBytes(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

std::string readSource(const char *path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

struct Built {
	std::vector<u8> bytes;
	std::string err;
	bool ok = false;
};

Built build(const fs::path &dir)
{
	Built out;
	char err[256] = "";
	u32 len = 0;
	u8 *raw = modpackArchiveBuild(dir.string().c_str(), &len, err, sizeof(err));
	out.err = err;
	if (raw) {
		out.bytes.assign(raw, raw + len);
		out.ok = true;
		free(raw);
	}
	return out;
}

uint32_t le32(const std::vector<u8> &b, size_t at)
{
	return (uint32_t)b[at] | ((uint32_t)b[at + 1] << 8) | ((uint32_t)b[at + 2] << 16)
		| ((uint32_t)b[at + 3] << 24);
}

uint16_t le16(const std::vector<u8> &b, size_t at)
{
	return (uint16_t)(b[at] | (b[at + 1] << 8));
}

/* The member paths of an envelope, in stored order. */
std::vector<std::string> memberPaths(const std::vector<u8> &b)
{
	std::vector<std::string> paths;
	REQUIRE(b.size() >= 6);
	REQUIRE(le32(b, 0) == PDCA_ARCHIVE_MAGIC);
	const uint16_t count = le16(b, 4);
	size_t at = 6;
	for (uint16_t i = 0; i < count; i++) {
		REQUIRE(at + 2 <= b.size());
		const uint16_t path_len = le16(b, at);
		at += 2;
		REQUIRE(path_len >= 2);
		REQUIRE(at + path_len <= b.size());
		paths.emplace_back((const char *)&b[at]);
		at += path_len;
		REQUIRE(at + 4 <= b.size());
		at += 4 + le32(b, at);
	}
	REQUIRE(at == b.size());
	return paths;
}

std::vector<u8> makeEnvelope(const std::vector<std::pair<std::string, std::string>> &members)
{
	std::vector<u8> out;
	auto put16 = [&out](uint16_t v) { out.push_back((u8)v); out.push_back((u8)(v >> 8)); };
	auto put32 = [&out](uint32_t v) {
		for (int s = 0; s < 32; s += 8) out.push_back((u8)(v >> s));
	};
	put32(PDCA_ARCHIVE_MAGIC);
	put16((uint16_t)members.size());
	for (const auto &m : members) {
		put16((uint16_t)(m.first.size() + 1));
		out.insert(out.end(), m.first.begin(), m.first.end());
		out.push_back(0);
		put32((uint32_t)m.second.size());
		out.insert(out.end(), m.second.begin(), m.second.end());
	}
	return out;
}

size_t countFiles(const fs::path &root)
{
	size_t n = 0;
	for (const auto &entry : fs::recursive_directory_iterator(root)) {
		if (entry.is_regular_file()) n++;
	}
	return n;
}

} // namespace

TEST_CASE("pdpack component archives are byte-identical whatever the folder order",
	"[modpack][pdpack][determinism][T-MODDING-023]")
{
	ModpackTempRoot temp("pd2_modpack_archive_order");
	const fs::path first = temp.root / "first";
	const fs::path second = temp.root / "second";

	/* Same files, written in opposite orders, so a directory listing that
	 * follows creation order differs between the two folders. */
	const std::vector<std::pair<std::string, std::string>> files = {
		{"mod.json", "{\"id\":\"sample\"}"},
		{"b.pdarena", "arena bytes"},
		{"textures/z.png", "zz"},
		{"textures/a.png", "aa"},
		{"a/b.txt", "nested"},
		{"a.txt", "flat"},
		{"Upper.ini", "upper"},
		{"empty.bin", ""},
	};
	for (const auto &f : files) writeText(first / f.first, f.second);
	for (auto it = files.rbegin(); it != files.rend(); ++it) writeText(second / it->first, it->second);
	/* Hidden files and folders are never packed. */
	writeText(first / ".hidden", "skip");
	writeText(second / ".git" / "HEAD", "skip");

	const Built a = build(first);
	const Built b = build(second);
	const Built again = build(first);
	REQUIRE(a.ok);
	REQUIRE(b.ok);
	REQUIRE(again.ok);
	REQUIRE(a.bytes == b.bytes);
	REQUIRE(a.bytes == again.bytes);

	const std::vector<std::string> expected = {
		"Upper.ini", "a.txt", "a/b.txt", "b.pdarena", "empty.bin", "mod.json",
		"textures/a.png", "textures/z.png",
	};
	REQUIRE(memberPaths(a.bytes) == expected);
}

TEST_CASE("pdpack component archives extract back to the same files",
	"[modpack][pdpack][pdca][T-MODDING-022][T-MODDING-023]")
{
	ModpackTempRoot temp("pd2_modpack_archive_roundtrip");
	const fs::path source = temp.root / "source";
	writeText(source / "mod.json", "{\"id\":\"roundtrip\"}");
	writeText(source / "dependencies" / "assets" / "x.pdmesh", std::string("\x00\x01\x02\xff", 4));
	writeText(source / "readme.txt", "hello");

	const Built built = build(source);
	REQUIRE(built.ok);

	const fs::path dest = temp.root / "mods" / "Weapons" / "roundtrip";
	REQUIRE(pdcaExtractArchiveTransactional(built.bytes.data(), (u32)built.bytes.size(),
		dest.string().c_str(), nullptr) == PDCA_EXTRACT_OK);
	REQUIRE(countFiles(dest) == 3);
	REQUIRE(readBytes(dest / "mod.json") == readBytes(source / "mod.json"));
	REQUIRE(readBytes(dest / "dependencies" / "assets" / "x.pdmesh")
		== readBytes(source / "dependencies" / "assets" / "x.pdmesh"));
	REQUIRE(readBytes(dest / "readme.txt") == "hello");
}

TEST_CASE("pdpack component builds refuse what they cannot pack whole",
	"[modpack][pdpack][T-MODDING-023]")
{
	ModpackTempRoot temp("pd2_modpack_archive_refuse");
	char err[256] = "";
	u32 len = 7;

	REQUIRE(modpackArchiveBuild(nullptr, &len, err, sizeof(err)) == nullptr);
	REQUIRE(len == 0);
	REQUIRE(std::string(err).size() > 0);

	const fs::path empty = temp.root / "empty";
	fs::create_directories(empty / "only_dirs");
	writeText(empty / ".hidden_only", "x");
	const Built none = build(empty);
	REQUIRE_FALSE(none.ok);
	REQUIRE(none.err.find("no files") != std::string::npos);

	const Built missing = build(temp.root / "absent");
	REQUIRE_FALSE(missing.ok);
	REQUIRE(missing.err.find("cannot open folder") != std::string::npos);
}

TEST_CASE("pdpack folder names are single safe path components",
	"[modpack][pdpack][path][T-MODDING-022]")
{
	for (const char *name : {"Weapons", "Maps", "crystal_pistol", "mod.weapon_x", "a-b_c.1",
			"Forge Maps"}) {
		INFO(name);
		REQUIRE(modpackArchiveNameIsSafe(name));
	}
	for (const char *name : {"", ".", "..", ".temp", ".hidden", "../x", "a/b", "a\\b",
			"..\\..\\Startup", "C:", "C:\\Windows", "/abs", "\\\\server\\share", "mod:weapon",
			"CON", "con.txt", "Nul", "LPT1", "com9.log", "trail.", "trail ", "wild*", "q?",
			"pipe|", "quote\"", "less<", "more>", "tab\tname"}) {
		INFO(name);
		REQUIRE_FALSE(modpackArchiveNameIsSafe(name));
	}
	REQUIRE_FALSE(modpackArchiveNameIsSafe(nullptr));
	REQUIRE_FALSE(modpackArchiveNameIsSafe(std::string(FS_MAXPATH, 'x').c_str()));
}

TEST_CASE("a hostile pdpack component cannot write outside its folder",
	"[modpack][pdpack][pdca][path][T-MODDING-022]")
{
	ModpackTempRoot temp("pd2_modpack_archive_hostile");
	const fs::path mods = temp.root / "mods";
	const fs::path dest = mods / "Weapons" / "victim";
	fs::create_directories(dest);
	writeText(dest / "owned.txt", "original bytes");

	/* The audit's example (CAT-C01) and its close relatives. */
	for (const auto &archive : {
			makeEnvelope({{"mod.json", "{}"},
				{"../../AppData/Roaming/Microsoft/Windows/Start Menu/Programs/Startup/pwn.cmd", "x"}}),
			makeEnvelope({{"mod.json", "{}"}, {"..\\..\\..\\pwn.cmd", "x"}}),
			makeEnvelope({{"mod.json", "{}"}, {"C:/Users/Public/pwn.cmd", "x"}}),
			makeEnvelope({{"mod.json", "{}"}, {"/abs/pwn.cmd", "x"}}),
			makeEnvelope({{"mod.json", "{}"}, {"sub/../../pwn.cmd", "x"}})}) {
		pdca_extract_transaction_t transaction;
		REQUIRE(pdcaExtractArchiveBegin(archive.data(), (u32)archive.size(),
			dest.string().c_str(), nullptr, &transaction) == PDCA_EXTRACT_PATH_REJECTED);
		REQUIRE(readBytes(dest / "owned.txt") == "original bytes");
		REQUIRE(countFiles(temp.root) == 1);
	}
}

TEST_CASE("modpack import goes only through the checked extractor under the mods folder",
	"[modpack][pdpack][static][T-MODDING-022][T-MODDING-023]")
{
	const std::string source = readSource("port/src/modpack.c");

	/* Extraction: the transactional, path-checked extractor only. */
	REQUIRE(source.find("pdcaExtractArchiveBegin(raw, raw_size,") != std::string::npos);
	REQUIRE(source.find("pdcaExtractTransactionRollback(") != std::string::npos);
	REQUIRE(source.find("pdcaExtractTransactionCommit(") != std::string::npos);
	REQUIRE(source.find("fopen(outpath") == std::string::npos);
	REQUIRE(source.find("snprintf(outpath") == std::string::npos);

	/* Folder names from the pack are checked; the root is the writable mods
	 * folder, not an enabled mod's own folder (fsGetModDir). */
	REQUIRE(source.find("modpackFindUnsafeComponent(mf, &unsafe_id)") != std::string::npos);
	REQUIRE(source.find("modpackFindUnsafeComponent(&mf, &unsafe_id)") != std::string::npos);
	REQUIRE(source.find("modmgrGetModsDir()") != std::string::npos);
	REQUIRE(source.find("fsGetModDir") == std::string::npos);

	/* Export: the deterministic builder, and no wall clock in the manifest. */
	REQUIRE(source.find("modpackArchiveBuild(entry->dirpath,") != std::string::npos);
	REQUIRE(source.find("time(NULL)") == std::string::npos);
	REQUIRE(source.find("readdir(") == std::string::npos);
	REQUIRE(source.find("\"created=") == std::string::npos);
}
