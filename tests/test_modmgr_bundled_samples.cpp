/**
 * test_modmgr_bundled_samples.cpp -- the bundled sample inventory and the
 * fresh-profile choice of which installed package is each bundled sample
 * (port/src/modmgr_bundled_samples.c, T-RELEASE-007).
 */
#include "catch.hpp"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "modmgr_bundled_samples.h"
}

namespace {

const std::string kDigestA(64, 'a');
const std::string kDigestB = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

std::string line(const std::string &digest, const std::string &id)
{
	return digest + " *" + id + ".pdmod\n";
}

struct Parsed {
	bool ok = false;
	modmgr_bundled_inventory_t inv;
	std::string err;
};

Parsed parse(const std::string &text)
{
	Parsed out;
	char err[160] = "";
	std::memset(&out.inv, 0x5a, sizeof(out.inv));
	out.ok = modmgrBundledParse(text.data(), text.size(), &out.inv, err, sizeof(err)) == 1;
	out.err = err;
	return out;
}

struct VerifyLog {
	std::vector<std::string> paths;
	bool answer = true;
};

s32 verifyStub(const char *path, const char *digest, void *user)
{
	VerifyLog *log = static_cast<VerifyLog *>(user);
	log->paths.push_back(std::string(path) + "|" + digest);
	return log->answer ? 1 : 0;
}

modmgr_bundled_candidate_t row(const char *id, const char *archive, s32 valid = 1,
	s32 is_archive = 1, s32 session_only = 0)
{
	modmgr_bundled_candidate_t r;
	r.id = id;
	r.archive_path = archive;
	r.valid = valid;
	r.is_archive = is_archive;
	r.session_only = session_only;
	return r;
}

std::string readSource(const char *path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

} // namespace

TEST_CASE("the bundled sample inventory reads every package in order",
	"[modmgr][bundled][T-RELEASE-007]")
{
	const Parsed p = parse(line(kDigestA, "crystal_pistol") + line(kDigestB, "mod.vehicle_maian_hover")
		+ line(kDigestA, "blank_room") + line(kDigestB, "model_room") + line(kDigestA, "grid_two_rooms"));
	REQUIRE(p.ok);
	REQUIRE(p.inv.count == 5);
	CHECK(std::string(p.inv.entries[0].id) == "crystal_pistol");
	CHECK(std::string(p.inv.entries[1].id) == "mod.vehicle_maian_hover");
	CHECK(std::string(p.inv.entries[1].digest) == kDigestB);
	CHECK(std::string(p.inv.entries[4].id) == "grid_two_rooms");
	CHECK(modmgrBundledIndexOf(&p.inv, "blank_room") == 2);
	CHECK(modmgrBundledIndexOf(&p.inv, "Blank_Room") == -1);
	CHECK(modmgrBundledIndexOf(&p.inv, "needler") == -1);
	CHECK(modmgrBundledIndexOf(&p.inv, nullptr) == -1);

	const Parsed empty = parse("");
	REQUIRE(empty.ok);
	CHECK(empty.inv.count == 0);
}

TEST_CASE("a malformed bundled sample inventory enables nothing",
	"[modmgr][bundled][T-RELEASE-007]")
{
	std::string many;
	for (int i = 0; i < MODMGR_BUNDLED_MAX + 1; i++) many += line(kDigestA, "m" + std::to_string(i));
	const std::string upper = "0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef";
	std::string nul_in_id = kDigestA + " *a";
	nul_in_id.push_back('\0');
	nul_in_id += "b.pdmod\n";
	const std::vector<std::string> bad = {
		kDigestA + " *crystal_pistol.pdmod",                  /* no final newline */
		line(upper, "crystal_pistol"),                          /* uppercase digest */
		line(kDigestA.substr(1), "crystal_pistol"),             /* 63 digits */
		kDigestA + "  crystal_pistol.pdmod\n",                 /* text mode marker */
		kDigestA + " *crystal_pistol.zip\n",                   /* not a .pdmod */
		kDigestA + " *crystal_pistol.pdmod\r\n",               /* CRLF */
		line(kDigestA, ".."),
		line(kDigestA, "../crystal_pistol"),
		line(kDigestA, "sub/crystal_pistol"),
		line(kDigestA, "sub\\crystal_pistol"),
		line(kDigestA, "CON"),
		line(kDigestA, ".temp"),
		line(kDigestA, "x:y"),
		line(kDigestA, std::string(MODMGR_BUNDLED_ID_LEN, 'x')),
		line(kDigestA, "blank_room") + line(kDigestB, "Blank_Room"), /* twice, ignoring case */
		nul_in_id,
		many,
		std::string(MODMGR_BUNDLED_MAX_BYTES + 1, '\n'),
	};
	for (const std::string &text : bad) {
		INFO(text.substr(0, 90));
		const Parsed p = parse(text);
		CHECK_FALSE(p.ok);
		CHECK(p.inv.count == 0);
		CHECK_FALSE(p.err.empty());
	}
}

TEST_CASE("a bundled sample is enabled only as its own unchanged package",
	"[modmgr][bundled][T-RELEASE-007]")
{
	const Parsed p = parse(line(kDigestA, "crystal_pistol") + line(kDigestB, "blank_room"));
	REQUIRE(p.ok);
	const char *expected = "C:/Games/PD2/mods/bundled/crystal_pistol.pdmod";

	SECTION("the installed package with matching bytes")
	{
		VerifyLog log;
		const modmgr_bundled_candidate_t rows[] = {
			row("needler", ""),
			row("crystal_pistol", "C:\\Games\\PD2\\MODS\\Bundled\\Crystal_Pistol.pdmod"),
		};
		CHECK(modmgrBundledFindVerified(&p.inv, 0, rows, 2, expected, verifyStub, &log) == 1);
		REQUIRE(log.paths.size() == 1);
		CHECK(log.paths[0] == std::string(rows[1].archive_path) + "|" + kDigestA);
	}
	SECTION("changed bytes")
	{
		VerifyLog log;
		log.answer = false;
		const modmgr_bundled_candidate_t rows[] = { row("crystal_pistol", expected) };
		CHECK(modmgrBundledFindVerified(&p.inv, 0, rows, 1, expected, verifyStub, &log) == -1);
	}
	SECTION("a user's own copy with the same id wins and is not auto-enabled")
	{
		VerifyLog log;
		const modmgr_bundled_candidate_t user_only[] = {
			row("crystal_pistol", "C:/Users/p/AppData/Local/perfectdark/mods/crystal_pistol.pdmod"),
		};
		CHECK(modmgrBundledFindVerified(&p.inv, 0, user_only, 1, expected, verifyStub, &log) == -1);
		const modmgr_bundled_candidate_t both[] = {
			row("crystal_pistol", "C:/Users/p/AppData/Local/perfectdark/mods/crystal_pistol.pdmod"),
			row("crystal_pistol", expected),
		};
		CHECK(modmgrBundledFindVerified(&p.inv, 0, both, 2, expected, verifyStub, &log) == -1);
		CHECK(log.paths.empty());
	}
	SECTION("a loose folder, a session-only or an invalid mod is never the sample")
	{
		VerifyLog log;
		const modmgr_bundled_candidate_t loose[] = { row("crystal_pistol", "", 1, 0, 0) };
		const modmgr_bundled_candidate_t session[] = { row("crystal_pistol", expected, 1, 1, 1) };
		const modmgr_bundled_candidate_t invalid[] = { row("crystal_pistol", expected, 0, 1, 0) };
		CHECK(modmgrBundledFindVerified(&p.inv, 0, loose, 1, expected, verifyStub, &log) == -1);
		CHECK(modmgrBundledFindVerified(&p.inv, 0, session, 1, expected, verifyStub, &log) == -1);
		CHECK(modmgrBundledFindVerified(&p.inv, 0, invalid, 1, expected, verifyStub, &log) == -1);
		CHECK(log.paths.empty());
	}
	SECTION("absent, out of range or missing inputs")
	{
		VerifyLog log;
		const modmgr_bundled_candidate_t rows[] = { row("needler", "") };
		CHECK(modmgrBundledFindVerified(&p.inv, 0, rows, 1, expected, verifyStub, &log) == -1);
		CHECK(modmgrBundledFindVerified(&p.inv, 2, rows, 1, expected, verifyStub, &log) == -1);
		CHECK(modmgrBundledFindVerified(&p.inv, -1, rows, 1, expected, verifyStub, &log) == -1);
		CHECK(modmgrBundledFindVerified(&p.inv, 0, rows, 1, "", verifyStub, &log) == -1);
		CHECK(modmgrBundledFindVerified(&p.inv, 0, rows, 1, expected, nullptr, &log) == -1);
		CHECK(modmgrBundledFindVerified(nullptr, 0, rows, 1, expected, verifyStub, &log) == -1);
	}
}

TEST_CASE("the mod manager and the staging tool agree on the bundled sample names",
	"[modmgr][bundled][static][T-RELEASE-007]")
{
	const std::string modmgr = readSource("port/src/modmgr.c");
	CHECK(modmgr.find("\"$E/\" MODMGR_BUNDLED_INVENTORY_NAME") != std::string::npos);
	CHECK(modmgr.find("modmgrBundledFindVerified(&g_BundledInventory, index, rows,") != std::string::npos);
	CHECK(modmgr.find("crystal_pistol") == std::string::npos); /* no sample is special-cased */

	const std::string stager = readSource("tools/stage_bundled_samples.py");
	CHECK(stager.find(std::string("INVENTORY_NAME = \"") + MODMGR_BUNDLED_INVENTORY_NAME + "\"")
		!= std::string::npos);
	CHECK(stager.find(std::string("BUNDLED_DIR = Path(\"") + MODMGR_BUNDLED_DIR_NAME + "\")")
		!= std::string::npos);
	CHECK(stager.find(std::string("PACKAGE_EXT = \"") + MODMGR_BUNDLED_EXT + "\"") != std::string::npos);
	CHECK(stager.find(std::string("MAX_PACKAGES = ") + std::to_string(MODMGR_BUNDLED_MAX))
		!= std::string::npos);
	CHECK(stager.find(std::string("MAX_ID_LEN = ") + std::to_string(MODMGR_BUNDLED_ID_LEN - 1))
		!= std::string::npos);

	const std::string release = readSource("devtools/release.ps1");
	CHECK(release.find("tools/stage_bundled_samples.py") != std::string::npos);
}
