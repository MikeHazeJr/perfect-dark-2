#include "catch.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "asset_path_contract.h"
#include "fs.h"
#include "pdca_extract_transaction.h"
}

namespace fs = std::filesystem;

static void appendLe16(std::vector<u8> &out, uint16_t value)
{
	out.push_back((u8)(value & 0xff));
	out.push_back((u8)(value >> 8));
}

static void appendLe32(std::vector<u8> &out, uint32_t value)
{
	for (int shift = 0; shift < 32; shift += 8)
		out.push_back((u8)((value >> shift) & 0xff));
}

static std::vector<u8> makePdca(
	const std::vector<std::pair<std::string, std::string>> &members)
{
	std::vector<u8> out;
	appendLe32(out, PDCA_ARCHIVE_MAGIC);
	appendLe16(out, (uint16_t)members.size());
	for (const auto &member : members) {
		appendLe16(out, (uint16_t)(member.first.size() + 1));
		out.insert(out.end(), member.first.begin(), member.first.end());
		out.push_back(0);
		appendLe32(out, (uint32_t)member.second.size());
		out.insert(out.end(), member.second.begin(), member.second.end());
	}
	return out;
}

class PdcaTempRoot {
public:
	PdcaTempRoot()
	{
		root = fs::temp_directory_path() / "pd2_pdca_txn_tcatalog003";
		std::error_code ec;
		fs::remove_all(root, ec);
		fs::create_directories(root);
	}
	~PdcaTempRoot()
	{
		std::error_code ec;
		fs::remove_all(root, ec);
	}
	fs::path root;
};

static std::string readFile(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
}

static size_t transactionResidueCount(const fs::path &root)
{
	size_t count = 0;
	for (const auto &entry : fs::recursive_directory_iterator(root)) {
		std::string name = entry.path().filename().string();
		if (name.rfind(".pd2-recv-", 0) == 0
				|| name.rfind(".pd2-backup-", 0) == 0) count++;
	}
	return count;
}

static uint32_t transactionPathHash(const std::string &path)
{
	uint32_t hash = 2166136261u;
	for (unsigned char ch : path) {
		hash ^= ch;
		hash *= 16777619u;
	}
	return hash;
}

static fs::path transactionSibling(const fs::path &dest, const char *kind,
	unsigned attempt = 0)
{
	char leaf[64];
	std::snprintf(leaf, sizeof(leaf), ".pd2-%s-%08x-%02x", kind,
		(unsigned)transactionPathHash(dest.string()), attempt);
	return dest.parent_path() / leaf;
}

TEST_CASE("PDCA receive publishes a complete nested tree transactionally",
	"[catalog][network][pdca][transaction][T-CATALOG-003]")
{
	PdcaTempRoot temp;
	fs::path dest = temp.root / "installed-component";
	fs::create_directories(dest);
	std::ofstream(dest / "old-user-file.txt") << "preserved until commit";
	auto archive = makePdca({
		{"component.ini", "[effect]\nid = mod:effect\n"},
		{"nested/source/effect.json", "{\"schema\":\"pd.effect.v2\"}"},
	});

	REQUIRE(pdcaExtractArchiveTransactional(archive.data(), (u32)archive.size(),
		dest.string().c_str(), nullptr)
		== PDCA_EXTRACT_OK);
	REQUIRE(readFile(dest / "component.ini").find("mod:effect") != std::string::npos);
	REQUIRE(readFile(dest / "nested/source/effect.json").find("pd.effect.v2") != std::string::npos);
	REQUIRE_FALSE(fs::exists(dest / "old-user-file.txt"));
	REQUIRE(transactionResidueCount(temp.root) == 0);
}

TEST_CASE("PDCA publication preserves authored member case",
	"[catalog][network][pdca][identity][T-CATALOG-003]")
{
	PdcaTempRoot temp;
	fs::path dest = temp.root / "new-category/new-component";
	REQUIRE_FALSE(fs::exists(dest.parent_path()));
	auto archive = makePdca({{"Mixed/Case.TXT", "exact"}});
	REQUIRE(pdcaExtractArchiveTransactional(archive.data(), (u32)archive.size(),
		dest.string().c_str(), nullptr) == PDCA_EXTRACT_OK);
	std::vector<std::string> dirs;
	for (const auto &entry : fs::directory_iterator(dest))
		dirs.push_back(entry.path().filename().string());
	REQUIRE(dirs.size() == 1);
	REQUIRE(dirs[0] == "Mixed");
	std::vector<std::string> files;
	for (const auto &entry : fs::directory_iterator(dest / "Mixed"))
		files.push_back(entry.path().filename().string());
	REQUIRE(files.size() == 1);
	REQUIRE(files[0] == "Case.TXT");
	REQUIRE(readFile(dest / "Mixed/Case.TXT") == "exact");
}

TEST_CASE("PDCA open write and publish failures restore the prior install",
	"[catalog][network][pdca][rollback][T-CATALOG-003]")
{
	for (const pdca_extract_faults_t fault : {
			pdca_extract_faults_t{1, -1, 0},
			pdca_extract_faults_t{-1, 1, 0},
			pdca_extract_faults_t{-1, -1, 1}}) {
		PdcaTempRoot temp;
		fs::path dest = temp.root / "installed-component";
		fs::create_directories(dest);
		std::ofstream(dest / "owned.txt") << "original bytes";
		auto archive = makePdca({{"first.txt", "first"}, {"second.txt", "second"}});

		pdca_extract_result_t result = pdcaExtractArchiveTransactional(
			archive.data(), (u32)archive.size(), dest.string().c_str(), &fault);
		INFO("result=" << (int)result);
		REQUIRE(result != PDCA_EXTRACT_OK);
		REQUIRE(readFile(dest / "owned.txt") == "original bytes");
		REQUIRE_FALSE(fs::exists(dest / "first.txt"));
		REQUIRE_FALSE(fs::exists(dest / "second.txt"));
		REQUIRE(transactionResidueCount(temp.root) == 0);
	}
}

TEST_CASE("PDCA published transaction waits for catalog admission",
	"[catalog][network][pdca][rollback][T-CATALOG-003][B-1012]")
{
	PdcaTempRoot temp;
	fs::path dest = temp.root / "installed-component";
	fs::create_directories(dest);
	std::ofstream(dest / "owned.txt") << "prior install";
	auto archive = makePdca({{"candidate.txt", "candidate"}});
	pdca_extract_transaction_t transaction{};

	REQUIRE(pdcaExtractArchiveBegin(archive.data(), (u32)archive.size(),
		dest.string().c_str(), nullptr, &transaction) == PDCA_EXTRACT_OK);
	REQUIRE(transaction.active == 1);
	REQUIRE(readFile(dest / "candidate.txt") == "candidate");
	REQUIRE_FALSE(fs::exists(dest / "owned.txt"));
	REQUIRE(pdcaExtractTransactionRollback(&transaction) == 1);
	REQUIRE(readFile(dest / "owned.txt") == "prior install");
	REQUIRE_FALSE(fs::exists(dest / "candidate.txt"));
	REQUIRE(transactionResidueCount(temp.root) == 0);

	REQUIRE(pdcaExtractArchiveBegin(archive.data(), (u32)archive.size(),
		dest.string().c_str(), nullptr, &transaction) == PDCA_EXTRACT_OK);
	REQUIRE(pdcaExtractTransactionCommit(&transaction) == PDCA_EXTRACT_OK);
	REQUIRE(readFile(dest / "candidate.txt") == "candidate");
	REQUIRE_FALSE(fs::exists(dest / "owned.txt"));
	REQUIRE(transactionResidueCount(temp.root) == 0);
}

TEST_CASE("PDCA validation rejection leaves no installed mutation",
	"[catalog][network][pdca][path][rollback][T-CATALOG-003]")
{
	PdcaTempRoot temp;
	fs::path dest = temp.root / "installed-component";
	fs::create_directories(dest);
	std::ofstream(dest / "owned.txt") << "original bytes";

	for (auto archive : {
			makePdca({{"safe.txt", "safe"}, {"../escape.txt", "bad"}}),
			makePdca({{"same.txt", "one"}, {"same.txt", "two"}}),
			makePdca({{"A/B.txt", "one"}, {"a\\b.TXT", "two"}}),
			makePdca({{"ok.txt", "one"}, {"ads.txt:stream", "two"}}),
			makePdca({{"ok.txt", "one"}, {"nested::member.txt", "two"}}),
			makePdca({{"ok.txt", "one"}, {"a//empty.txt", "two"}}),
			makePdca({{"ok.txt", "one"}, {"a/./dot.txt", "two"}}),
			makePdca({{"ok.txt", "one"}, {"CON.txt", "two"}}),
			makePdca({{"ok.txt", "one"}, {"devices/LpT1.log", "two"}}),
			makePdca({{"ok.txt", "one"}, {"traildot./bad", "two"}}),
			makePdca({{"ok.txt", "one"}, {"wild*/bad", "two"}}),
			makePdca({{std::string(FS_MAXPATH, 'x'), "bad"}})}) {
		REQUIRE(pdcaExtractArchiveTransactional(archive.data(), (u32)archive.size(),
			dest.string().c_str(), nullptr)
			!= PDCA_EXTRACT_OK);
		REQUIRE(readFile(dest / "owned.txt") == "original bytes");
		REQUIRE(transactionResidueCount(temp.root) == 0);
	}
}

TEST_CASE("PDCA preflight reaches 127 128 and 1023 final-path boundaries",
	"[catalog][network][pdca][path][capacity][T-CATALOG-003]")
{
	for (size_t total : {size_t(127), size_t(128), size_t(FS_MAXPATH - 1)}) {
		PdcaTempRoot temp;
		fs::path parent = temp.root / "received-components/effects";
		fs::create_directories(parent);
		std::string dest = (parent / "mod_effect_with_long_identity").string();
		const size_t memberLength = total - dest.size() - 1;
		REQUIRE(memberLength > 0);
		auto archive = makePdca({{std::string(memberLength, 'm'), "data"}});
		pdca_extract_faults_t fault{0, -1, 0};
		/* OPEN_FAILED proves the complete envelope and destination preflight
		 * passed without depending on host long-path policy for the final open. */
		REQUIRE(pdcaExtractArchiveTransactional(archive.data(), (u32)archive.size(),
			dest.c_str(), &fault)
			== PDCA_EXTRACT_OPEN_FAILED);
		REQUIRE_FALSE(fs::exists(dest));
		REQUIRE(transactionResidueCount(temp.root) == 0);
	}
}

TEST_CASE("PDCA crash backup is restored or blocks ambiguous overwrite",
	"[catalog][network][pdca][recovery][T-CATALOG-003]")
{
	PdcaTempRoot temp;
	fs::path dest = temp.root / "installed-component";
	auto archive = makePdca({{"new.txt", "new"}});

	/* The production name is path-hash-derived and intentionally opaque. Use a
	 * failed publish to exercise restore in-process; the earlier rollback test
	 * proves this branch leaves no residue. A manually introduced unmatched
	 * backup is not guessed at or deleted by a different destination hash. */
	pdca_extract_faults_t publishFault{-1, -1, 1};
	fs::create_directories(dest);
	std::ofstream(dest / "owned.txt") << "old";
	REQUIRE(pdcaExtractArchiveTransactional(archive.data(), (u32)archive.size(),
		dest.string().c_str(), &publishFault)
		== PDCA_EXTRACT_PUBLISH_FAILED);
	REQUIRE(readFile(dest / "owned.txt") == "old");
	REQUIRE(transactionResidueCount(temp.root) == 0);

	/* A crash after moving the destination but before publishing the stage is
	 * recovered before new writes. Injected open failure leaves restored bytes. */
	fs::remove_all(dest);
	fs::path backup = transactionSibling(dest, "backup");
	fs::create_directories(backup);
	std::ofstream(backup / "recovery.txt") << "recover me";
	pdca_extract_faults_t openFault{0, -1, 0};
	REQUIRE(pdcaExtractArchiveTransactional(archive.data(), (u32)archive.size(),
		dest.string().c_str(), &openFault) == PDCA_EXTRACT_OPEN_FAILED);
	REQUIRE(readFile(dest / "recovery.txt") == "recover me");
	REQUIRE_FALSE(fs::exists(backup));

	/* Both trees present is ambiguous. Never delete either one automatically. */
	fs::create_directories(backup);
	std::ofstream(backup / "older.txt") << "older";
	REQUIRE(pdcaExtractArchiveTransactional(archive.data(), (u32)archive.size(),
		dest.string().c_str(), nullptr) == PDCA_EXTRACT_RECOVERY_REQUIRED);
	REQUIRE(readFile(dest / "recovery.txt") == "recover me");
	REQUIRE(readFile(backup / "older.txt") == "older");
}

TEST_CASE("PDCA removes only matching abandoned stage residue",
	"[catalog][network][pdca][recovery][T-CATALOG-003]")
{
	PdcaTempRoot temp;
	fs::path dest = temp.root / "installed-component";
	fs::path stale = transactionSibling(dest, "recv");
	fs::create_directories(stale);
	std::ofstream(stale / "partial.txt") << "partial";
	fs::path unrelated = temp.root / ".pd2-recv-deadbeef-00";
	fs::create_directories(unrelated);
	std::ofstream(unrelated / "owned.txt") << "other transaction";
	auto archive = makePdca({{"complete.txt", "complete"}});
	REQUIRE(pdcaExtractArchiveTransactional(archive.data(), (u32)archive.size(),
		dest.string().c_str(), nullptr) == PDCA_EXTRACT_OK);
	REQUIRE_FALSE(fs::exists(stale));
	REQUIRE(readFile(dest / "complete.txt") == "complete");
	REQUIRE(readFile(unrelated / "owned.txt") == "other transaction");
}
