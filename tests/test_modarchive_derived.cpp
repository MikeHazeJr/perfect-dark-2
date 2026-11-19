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

#include <atomic>
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

namespace {

std::atomic<int> s_runnerCalls{0};

// Stands in for bootPoolForRangeBlocking: four threads pull indices from one
// cursor and the call returns once every index has run.
void fourThreadRunner(int begin, int end, void (*fn)(int index, void *ctx), void *ctx)
{
	std::atomic<int> next{begin};
	std::vector<std::thread> workers;
	for (int t = 0; t < 4; ++t) {
		workers.emplace_back([&] {
			for (int i = next++; i < end; i = next++) {
				++s_runnerCalls;
				fn(i, ctx);
			}
		});
	}
	for (auto &worker : workers) worker.join();
}

} // namespace

TEST_CASE("B-1110 the barrier can spread its flushes over a range runner",
		"[modarchive][b1110]")
{
	TempDir root("runner");
	const auto lang = makeDir(root.path, "lang");
	for (int i = 0; i < 40; ++i) {
		publish(lang / ("bank" + std::to_string(i) + ".pdlang"), "bank " + std::to_string(i), true);
	}
	s_runnerCalls = 0;
	REQUIRE(pending(lang) == 40);
	REQUIRE(modArchiveDerivedBarrierWith(lang.string().c_str(), fourThreadRunner) == 0);
	REQUIRE(s_runnerCalls == 40);
	REQUIRE(pending(lang) == 0);
	REQUIRE(member(lang / "bank39.pdlang") == "bank 39");
}

TEST_CASE("B-1110 a flush that fails makes the barrier report it", "[modarchive][b1110]")
{
	TempDir root("refused");
	const auto fonts = makeDir(root.path, "fonts");
	const auto file = fonts / "locked.pdfont";
	publish(file, "font", true);
	struct Writable {
		std::filesystem::path path;
		~Writable()
		{
			std::error_code ec;
			std::filesystem::permissions(path, std::filesystem::perms::owner_write,
				std::filesystem::perm_options::add, ec);
		}
	} restore{file};
	// A read-only archive cannot be reopened for the flush.
	std::filesystem::permissions(file, std::filesystem::perms::owner_write,
		std::filesystem::perm_options::remove);
	REQUIRE(modArchiveDerivedBarrier(fonts.string().c_str()) == -1);
	// The stamp writer then writes no stamp; the path is not retried.
	REQUIRE(pending(fonts) == 0);
	REQUIRE(member(file) == "font");
}

TEST_CASE("B-1110 the cache stamp is written only after its archives are flushed",
		"[modarchive][b1110][static]")
{
	const std::string cache = readSource("port/src/romextract_pd_cache.c");
	const std::string body = functionBody(cache, "void romExtractPdFastCacheWrite(");
	const size_t barrier =
		body.find("if (modArchiveDerivedBarrierWith(abs_dir, bootPoolForRangeBlocking) != 0) {");
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

TEST_CASE("arena clean markers validate the complete version record they write",
		"[modarchive][pdextract][static]")
{
	const std::string source = readSource("port/src/romextract_pdarena.c");
	const std::string reader = functionBody(source, "static s32 s_pdarenaCleanMarkerIsValid(");
	const std::string writer = functionBody(source, "static void s_pdarenaCleanMarkerWrite(");
	REQUIRE(source.find("\"scenario_kind=\" ROMEXTRACT_PDSCENARIO_FAST_CACHE_KIND") != std::string::npos);
	REQUIRE(reader.find("sizeof(k_PdarenaCleanMarker) - 1") != std::string::npos);
	REQUIRE(reader.find("fread(record, 1, sizeof(record), f) == sizeof(record)") != std::string::npos);
	REQUIRE(reader.find("memcmp(record, k_PdarenaCleanMarker, sizeof(record))") != std::string::npos);
	REQUIRE(reader.find("fgetc(f) == EOF && !ferror(f)") != std::string::npos);
	REQUIRE(writer.find("fputs(k_PdarenaCleanMarker, f)") != std::string::npos);
}

TEST_CASE("stage archive verification reuses only the exact verified public bytes",
		"[modarchive][pdextract][static]")
{
	const std::string source = readSource("port/src/romextract_pdarena.c");
	const std::string worker = functionBody(source, "static void s_hashStageTextureSource(");
	const std::string hash_archive = functionBody(source, "static s32 s_hashStageTextureArchive(");
	const std::string hash_version = functionBody(source, "static void s_beginStageTextureHash(");
	const std::string mapping = functionBody(source, "static void s_hashStageTextureMapping(");
	const std::string key = functionBody(source, "static void s_stageLedgerKey(");
	const std::string match = functionBody(source, "static s32 s_stageRecordsMatch(");
	const std::string reader = functionBody(source, "static s32 s_stageTextureMarkerRead(");
	const std::string writer = functionBody(source, "static void s_stageTextureMarkerWrite(");
	const std::string emit = functionBody(source, "s32 romExtractAllPdarena(");
	REQUIRE(worker.find("s_stageTextureArenaPath(") != std::string::npos);
	REQUIRE(worker.find("s_scenarioArchiveRelPath(") != std::string::npos);
	REQUIRE(worker.find("catalogStageIdByStagenum(") != std::string::npos);
	REQUIRE(hash_archive.find("sha256HashFile(full, digest)") != std::string::npos);
	REQUIRE(hash_archive.find("s_hashStageTextureString(hash, rel)") != std::string::npos);
	REQUIRE(hash_version.find("PDARENA_STAGE_TEXTURE_REVISION") != std::string::npos);
	REQUIRE(hash_version.find("ROMEXTRACT_PDARENA_FAST_CACHE_KIND") != std::string::npos);
	REQUIRE(hash_version.find("ROMEXTRACT_PDSCENARIO_FAST_CACHE_KIND") != std::string::npos);
	REQUIRE(mapping.find("entry->bundled && entry->type == ASSET_TEXTURE") != std::string::npos);
	REQUIRE(mapping.find("entry->ext.texture.texture_id") != std::string::npos);
	REQUIRE(mapping.find("s_hashStageTextureString(hash, entry->id)") != std::string::npos);
	REQUIRE(key.find("s_beginStageTextureHash(&hash)") != std::string::npos);
	REQUIRE(key.find("PDARENA_STAGE_LEDGER_SCHEMA") != std::string::npos);
	REQUIRE(key.find("s_hashStageTextureMapping(&hash)") != std::string::npos);
	REQUIRE(match.find("known->valid == 1 && live->valid == 1") != std::string::npos);
	REQUIRE(match.find("memcmp(known->digest, live->digest, sizeof(known->digest)) == 0") != std::string::npos);
	REQUIRE(reader.find("memcmp(recorded_key, key, sizeof(recorded_key)) == 0") != std::string::npos);
	REQUIRE(reader.find("fread(records, sizeof(*records), count, file) == count") != std::string::npos);
	REQUIRE(reader.find("fgetc(file) == EOF && !ferror(file)") != std::string::npos);
	REQUIRE(reader.find("if (!*has_ledger) memset(records, 0") != std::string::npos);

	const auto hash_match = emit.find("memcmp(stage_texture_previous, stage_texture_live");
	const auto clean_marker = emit.find("s_pdarenaCleanMarkerIsValid(");
	const auto skip = emit.find("EXTRACT.STAGE.TEXTURE: upgraded=0 failed=0 (fast-cache)");
	const auto repair = emit.find("s_pdarenaWork(i, &actx, verified)");
	const auto standalone_repair = emit.find("s_pdscenarioStageWork(spec, &actx, verified || deferred)", repair);
	REQUIRE(repair != std::string::npos);
	REQUIRE(standalone_repair != std::string::npos);
	REQUIRE(hash_match < clean_marker);
	REQUIRE(clean_marker < skip);
	REQUIRE(skip < repair);
	REQUIRE(emit.find("!effective_force && s_stageRecordsMatch(&prior[i], &live[i])", skip) < repair);
	REQUIRE(emit.find("!effective_force && s_stageRecordsMatch(&prior[job], &live[job])", repair) <
		standalone_repair);
	const std::string arena = functionBody(source, "static void s_pdarenaWork(");
	const std::string scenario = functionBody(source, "static void s_pdscenarioStageWork(");
	const std::string upgrade = functionBody(source, "static s32 s_upgradeStageTextureProperties(");
	REQUIRE(arena.find("verified ? 0 : s_emitOnePdscenario(") != std::string::npos);
	REQUIRE(arena.find("verified ? 0 : s_emitOnePdarena(") != std::string::npos);
	REQUIRE(scenario.find("verified ? 0 : s_emitOnePdscenarioSpec(") != std::string::npos);
	REQUIRE(upgrade.find("if (!force_rewrite && s_stageRecordsMatch(&prior[i], &live[i])) continue;") != std::string::npos);

	const auto success = emit.find("if (arenas_failed == 0 && scenarios_failed == 0) {", repair);
	const auto rehash = emit.find("&stage_specs, prior, after);", success);
	const auto stable = emit.find("!s_stageRecordsMatch(&prior[i], &live[i])", rehash);
	const auto invalidate = emit.find("prior[i].valid = 0;", stable);
	const auto no_full_skip = emit.find("if (!all_verified) memset(after, 0, sizeof(after));", invalidate);
	const auto publish = emit.find("s_stageTextureMarkerWrite(arenas_dir, after, key, prior, total_work)", no_full_skip);
	REQUIRE(publish != std::string::npos);
	REQUIRE(success < rehash);
	REQUIRE(rehash < stable);
	REQUIRE(stable < invalidate);
	REQUIRE(invalidate < no_full_skip);
	REQUIRE(no_full_skip < publish);

	const auto begin = writer.find("saveAtomicBegin(&transaction, path)");
	const auto records = writer.find("fwrite(records, sizeof(*records), count, file) == count");
	const auto commit = writer.find("saveAtomicCommit(&transaction) == 0");
	const auto abort = writer.find("saveAtomicAbort(&transaction)");
	REQUIRE(abort != std::string::npos);
	REQUIRE(begin < records);
	REQUIRE(records < commit);
	REQUIRE(commit < abort);
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
