#include "catch.hpp"

#include <filesystem>
#include <fstream>
#include <string>

extern "C" {
#include "save_atomic.h"
}

namespace fs = std::filesystem;

static std::string readText(const fs::path &path)
{
	std::ifstream input(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>());
}

static size_t candidateCount(const fs::path &dir)
{
	size_t count = 0;
	for (const auto &entry : fs::directory_iterator(dir)) {
		if (entry.path().filename().string().find(".pd2tmp-") !=
				std::string::npos) {
			count++;
		}
	}
	return count;
}

TEST_CASE("atomic save failure preserves destination and removes candidate",
	"[v006][b1046][save][atomic]")
{
	fs::path dir = fs::temp_directory_path() / "pd2-save-atomic-failure";
	fs::remove_all(dir);
	fs::create_directories(dir);
	fs::path destination = dir / "mpsetup_atomic.json";
	std::ofstream(destination, std::ios::binary) << "prior-save";

	save_atomic_file_t transaction{};
	REQUIRE(saveAtomicBegin(&transaction, destination.string().c_str()) == 0);
	REQUIRE(std::fwrite("replacement", 1, 11,
		saveAtomicStream(&transaction)) == 11);
	saveAtomicDebugFailNextCommit();
	REQUIRE(saveAtomicCommit(&transaction) == -1);
	REQUIRE(readText(destination) == "prior-save");
	REQUIRE(candidateCount(dir) == 0);

	fs::remove_all(dir);
}

TEST_CASE("atomic save commit replaces complete destination",
	"[v006][b1046][save][atomic]")
{
	fs::path dir = fs::temp_directory_path() / "pd2-save-atomic-success";
	fs::remove_all(dir);
	fs::create_directories(dir);
	fs::path destination = dir / "mpsetups.bin";
	std::ofstream(destination, std::ios::binary) << "old";

	save_atomic_file_t transaction{};
	REQUIRE(saveAtomicBegin(&transaction, destination.string().c_str()) == 0);
	const unsigned char bytes[] = {3, 0, 0};
	REQUIRE(std::fwrite(bytes, 1, sizeof(bytes),
		saveAtomicStream(&transaction)) == sizeof(bytes));
	REQUIRE(saveAtomicCommit(&transaction) == 0);
	REQUIRE(readText(destination) == std::string("\x03\0\0", 3));
	REQUIRE(candidateCount(dir) == 0);

	fs::remove_all(dir);
}
