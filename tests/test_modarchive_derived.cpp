// B-1110: first-launch extraction forced every archive to disk one at a time.
//
// On the 7200 rpm D: drive the per-file flush plus the write-through rename
// cost about 99 ms per archive against about 1 ms without them, and an
// extraction writes about 5,600 archives. Extractors now publish through
// modArchiveBeginDerived, which keeps the temp file and the atomic replace
// but leaves the flush to modArchiveDerivedBarrier. The directory cache stamp
// lets a later launch skip a directory without opening its files, so the
// stamp writer runs the barrier first and writes no stamp when it fails.
#include "catch.hpp"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <PR/ultratypes.h>
#include "modarchive.h"
}

namespace {

struct TempDir {
	std::filesystem::path path;

	explicit TempDir(const char *stem)
	{
		const auto stamp =
			std::chrono::high_resolution_clock::now().time_since_epoch().count();
		path = std::filesystem::temp_directory_path()
			/ (std::string("pd2-b1110-") + stem + "-" + std::to_string(stamp));
		std::filesystem::create_directories(path);
	}

	~TempDir()
	{
		// Leave nothing pending that points into a folder about to vanish.
		modArchiveDerivedBarrier(path.string().c_str());
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
	}
};

std::filesystem::path makeDir(const std::filesystem::path &parent, const char *name)
{
	const auto dir = parent / name;
	std::filesystem::create_directories(dir);
	return dir;
}

void publish(const std::filesystem::path &file, const std::string &body, bool derived)
{
	const std::string path = file.string();
	mod_archive_writer_t *writer = derived
		? modArchiveBeginDerived(path.c_str())
		: modArchiveBegin(path.c_str());
	REQUIRE(writer != nullptr);
	REQUIRE(modArchiveAddFileMem(writer, "mesh.ini", body.data(),
		static_cast<u32>(body.size())) == MODARCHIVE_OK);
	REQUIRE(modArchiveFinish(writer) == MODARCHIVE_OK);
}

std::string member(const std::filesystem::path &file)
{
	mod_archive_t *archive = modArchiveOpen(file.string().c_str());
	REQUIRE(archive != nullptr);
	const s32 index = modArchiveFindEntry(archive, "mesh.ini");
	REQUIRE(index >= 0);
	u32 size = 0;
	char *bytes = static_cast<char *>(modArchiveExtractAlloc(archive, index, &size));
	REQUIRE(bytes != nullptr);
	std::string out(bytes, size);
	free(bytes);
	modArchiveClose(archive);
	return out;
}

s32 pending(const std::filesystem::path &dir)
{
	return modArchiveDerivedPendingCount(dir.string().c_str());
}

std::string readSource(const std::string &path)
{
	std::ifstream input(path, std::ios::in | std::ios::binary);
	REQUIRE(input.good());
	std::ostringstream out;
	out << input.rdbuf();
	return out.str();
}

// A definition runs from its signature to the first closing brace in column
// 0. Brace counting would not do: modArchiveFinish opens one brace in each
// branch of an #if and closes it once.
std::string functionBody(const std::string &source, const std::string &signature)
{
	const size_t start = source.find(signature);
	REQUIRE(start != std::string::npos);
	const size_t end = source.find("\n}", start);
	REQUIRE(end != std::string::npos);
	return source.substr(start, end + 2 - start);
}

} // namespace

TEST_CASE("B-1110 a derived archive is published whole and waits for the barrier",
		"[modarchive][b1110]")
{
	TempDir root("publish");
	const auto meshes = makeDir(root.path, "meshes");
	const auto other = makeDir(root.path, "textures");
	const auto file = meshes / "a.pdmesh";

	publish(file, "derived body", true);
	REQUIRE(std::filesystem::exists(file));
	REQUIRE_FALSE(std::filesystem::exists(file.string() + ".tmp"));
	REQUIRE(member(file) == "derived body");
	REQUIRE(pending(meshes) == 1);
	REQUIRE(pending(other) == 0);

	// Republishing replaces the archive atomically, as the durable writer does.
	publish(file, "second body", true);
	REQUIRE(member(file) == "second body");
	REQUIRE(pending(meshes) == 2);

	// The barrier for one directory flushes only that directory.
	publish(other / "b.pdtexture", "texture", true);
	REQUIRE(modArchiveDerivedBarrier(meshes.string().c_str()) == 0);
	REQUIRE(pending(meshes) == 0);
	REQUIRE(pending(other) == 1);
	REQUIRE(member(file) == "second body");
	REQUIRE(modArchiveDerivedBarrier(other.string().c_str()) == 0);
	REQUIRE(pending(other) == 0);
}

TEST_CASE("B-1110 the durable writer is unchanged and never waits for a barrier",
		"[modarchive][b1110]")
{
	TempDir root("durable");
	const auto mods = makeDir(root.path, "mods");
	publish(mods / "author.pdmod", "player made", false);
	REQUIRE(member(mods / "author.pdmod") == "player made");
	REQUIRE(pending(mods) == 0);
}

TEST_CASE("B-1110 the barrier matches folders, not text", "[modarchive][b1110]")
{
	TempDir root("match");
	const auto exact = makeDir(root.path, "ab");
	const auto longer = makeDir(root.path, "abc");
	publish(longer / "x.pdmesh", "x", true);

	// "ab" is a text prefix of "abc" but not its folder.
	REQUIRE(modArchiveDerivedBarrier(exact.string().c_str()) == 0);
	REQUIRE(pending(longer) == 1);

	// Separators and letter case do not matter; a trailing separator is fine.
	std::string spelled = longer.string();
	for (char &c : spelled) {
		if (c == '\\') c = '/';
		else c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	}
	spelled += "/";
	REQUIRE(pending(std::filesystem::path(spelled)) == 1);
	REQUIRE(modArchiveDerivedBarrier(spelled.c_str()) == 0);
	REQUIRE(pending(longer) == 0);

	// Archives in subfolders belong to the folder's barrier too.
	const auto nested = makeDir(longer, "nested");
	publish(nested / "y.pdmesh", "y", true);
	REQUIRE(pending(longer) == 1);
	REQUIRE(modArchiveDerivedBarrier(longer.string().c_str()) == 0);
	REQUIRE(pending(nested) == 0);
}

TEST_CASE("B-1110 a removed intermediate needs no flush", "[modarchive][b1110]")
{
	TempDir root("removed");
	const auto weapons = makeDir(root.path, "weapons");
	const auto temp = weapons / "parent.slug.tmp.pdasset";
	publish(temp, "nested payload", true);
	std::filesystem::remove(temp);
	REQUIRE(pending(weapons) == 1);
	REQUIRE(modArchiveDerivedBarrier(weapons.string().c_str()) == 0);
	REQUIRE(pending(weapons) == 0);
}

TEST_CASE("B-1110 boot-pool workers can publish derived archives at once",
		"[modarchive][b1110]")
{
	TempDir root("threads");
	const auto anims = makeDir(root.path, "animations");
	constexpr int kThreads = 4;
	constexpr int kEach = 25;
	std::vector<std::thread> workers;
	std::vector<int> failures(kThreads, 0);
	for (int t = 0; t < kThreads; ++t) {
		workers.emplace_back([&, t] {
			for (int i = 0; i < kEach; ++i) {
				const auto file = anims / ("t" + std::to_string(t) + "_" + std::to_string(i) + ".pdanim");
				const std::string body = "anim " + std::to_string(t * 100 + i);
				mod_archive_writer_t *writer = modArchiveBeginDerived(file.string().c_str());
				if (!writer || modArchiveAddFileMem(writer, "mesh.ini", body.data(),
						static_cast<u32>(body.size())) != MODARCHIVE_OK
						|| modArchiveFinish(writer) != MODARCHIVE_OK) {
					++failures[t];
				}
			}
		});
	}
	for (auto &worker : workers) worker.join();
	for (int t = 0; t < kThreads; ++t) REQUIRE(failures[t] == 0);
	REQUIRE(pending(anims) == kThreads * kEach);
	REQUIRE(modArchiveDerivedBarrier(anims.string().c_str()) == 0);
	REQUIRE(pending(anims) == 0);
	REQUIRE(member(anims / "t3_24.pdanim") == "anim 324");
}

TEST_CASE("B-1110 the cache stamp is written only after its archives are flushed",
		"[modarchive][b1110][static]")
{
	const std::string cache = readSource("port/src/romextract_pd_cache.c");
	const std::string body = functionBody(cache, "void romExtractPdFastCacheWrite(");
	const size_t barrier = body.find("if (modArchiveDerivedBarrier(abs_dir) != 0) {");
	const size_t refusal = body.find("no fast-cache stamp written");
	const size_t scan = body.find("s_scanDir(abs_dir, ext, &fp)");
	const size_t write = body.find("fopen(stamp_path, \"wb\")");
	REQUIRE(barrier != std::string::npos);
	REQUIRE(refusal != std::string::npos);
	REQUIRE(barrier < refusal);
	REQUIRE(refusal < scan);
	REQUIRE(scan < write);
	REQUIRE(body.find("return;", refusal) < scan);
}

TEST_CASE("B-1110 every extractor publishes derived archives", "[modarchive][b1110][static]")
{
	const std::regex durable("\\bmodArchiveBegin\\(");
	int extractors = 0;
	int derived = 0;
	for (const auto &entry : std::filesystem::directory_iterator("port/src")) {
		const std::string name = entry.path().filename().string();
		if (name.rfind("romextract_pd", 0) != 0 || entry.path().extension() != ".c") continue;
		const std::string source = readSource(entry.path().generic_string());
		INFO(name);
		REQUIRE_FALSE(std::regex_search(source, durable));
		size_t at = 0;
		int here = 0;
		while ((at = source.find("modArchiveBeginDerived(", at)) != std::string::npos) {
			++here;
			++at;
		}
		if (here > 0) ++extractors;
		derived += here;
	}
	REQUIRE(extractors >= 14);
	REQUIRE(derived >= 18);
}

TEST_CASE("B-1110 the durable writer still flushes and writes through", "[modarchive][b1110][static]")
{
	const std::string source = readSource("port/src/modarchive.c");
	const std::string finish = functionBody(source, "s32 modArchiveFinish(mod_archive_writer_t *w)");
	const size_t guard = finish.find("if (!w->derived) {");
	const size_t commit = finish.find("_commit(_fileno(w->fp))");
	const size_t move = finish.find("MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH");
	const size_t record = finish.find("derivedRecord(w->finalPath) != 0");
	REQUIRE(guard != std::string::npos);
	REQUIRE(commit != std::string::npos);
	REQUIRE(guard < commit);
	REQUIRE(move != std::string::npos);
	REQUIRE(record != std::string::npos);
	REQUIRE(move < record);
	REQUIRE(finish.find("derivedFlushPath(w->finalPath) != 0", record) != std::string::npos);
}
