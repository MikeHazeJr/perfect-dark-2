/**
 * test_mod_asset_validate.cpp -- one in-game Validate for typed archives.
 *
 * Release validation does not read member content. These cases show that the
 * in-game Validate accepts every checked-in example archive and refuses the
 * same archives once a public text member is malformed or a family's own
 * reader would refuse them, naming the member at fault.
 */
#include "catch.hpp"
extern "C" {
#include "asset_archive_clone.h"
#include "asset_archive_policy.h"
#include "assetcatalog_deps.h"
#include "forge_revision_store.h"
#include "grid_archive_policy.h"
#include "grid_match_sources.h"
#include "mod_asset_validate.h"
#include "mod_author.h"
#include "modarchive.h"
#include "modpack_pdmod.h"
#include "save_atomic.h"
}
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct ValidateTempDir {
	fs::path path;

	ValidateTempDir() {
		static unsigned counter = 0;
		const auto stamp =
			std::chrono::high_resolution_clock::now().time_since_epoch().count();
		path = fs::temp_directory_path()
			/ ("pd2-mod-validate-" + std::to_string(stamp) + "-" + std::to_string(counter++));
		fs::create_directories(path);
	}

	~ValidateTempDir() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

std::string validateReadFile(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string validateExample(const char *relative)
{
	return (fs::path(PD_SOURCE_DIR) / relative).string();
}

std::string validateEntry(const std::string &archive, const std::string &entry)
{
	u32 size = 0;
	void *data = modArchiveExtractMemAlloc(archive.data(),
		static_cast<u32>(archive.size()), entry.c_str(), &size);
	REQUIRE(data != nullptr);
	std::string out(static_cast<const char *>(data), size);
	free(data);
	return out;
}

struct Verdict {
	s32 code = 0;
	std::string err;
};

Verdict validateFile(const fs::path &path)
{
	Verdict v;
	char err[320];
	std::memset(err, 'x', sizeof(err));
	err[sizeof(err) - 1] = '\0';
	v.code = modAssetValidateArchiveFile(path.string().c_str(), err, sizeof(err));
	v.err = err;
	return v;
}

/* The example at `relative`, rebuilt under its own id with one member
 * replaced. Release validation still passes: it does not read the content. */
fs::path validateWithMember(const ValidateTempDir &dir, const char *relative, const char *id,
	const char *entry, const std::string &bytes, const char *leaf)
{
	const std::string source = validateExample(relative);
	const fs::path out = dir.path / leaf;
	asset_archive_clone_override_t ov{};
	ov.entry = entry;
	ov.data = bytes.data();
	ov.len = static_cast<u32>(bytes.size());
	char err[256] = {};
	const s32 built = assetArchiveRebuildFile(source.c_str(), out.string().c_str(), id,
		"pd-tests", &ov, 1, nullptr, err, sizeof(err));
	INFO("template " << leaf << " error: " << err);
	REQUIRE(built == ASSET_ARCHIVE_CLONE_OK);
	char policy[256] = {};
	REQUIRE(assetArchiveValidateFile(out.string().c_str(), ASSET_ARCHIVE_VALIDATE_RELEASE,
		policy, sizeof(policy)) == 0);
	return out;
}

const char *const kWeapon = "examples/modding/typed-pdxxx-basic/weapons/tri_weapon.pdweapon";
const char *const kVehicle = "examples/modding/typed-pdxxx-basic/vehicles/tri_vehicle.pdvehicle";
const char *const kBody = "examples/modding/typed-pdxxx-basic/bodies/tri_body.pdbody";
const char *const kCharacter =
	"examples/modding/typed-pdxxx-basic/characters/tri_character.pdcharacter";

} // namespace

TEST_CASE("Grid revisions preserve exact previous arenas and atomically move the current pointer",
	"[forge-revision][modding][pdxxx]")
{
	const ValidateTempDir dir;
	const fs::path store = dir.path / "revisions";
	REQUIRE(fs::create_directory(store));
	const auto directory = store.string();
	const char *id = "mod_blank_room:arena_blank_room";
	const char *example = "dev-mods/blank_room/blank_room.pdarena";
	const auto first = validateExample(example);
	const auto descriptor = validateEntry(validateReadFile(first), "arena.ini");
	const auto second = validateWithMember(dir, example, id, "arena.ini",
		descriptor + "revision_note = changed\n", "next.pdarena").string();
	auto digest = [](const std::string &path) {
		u8 hash[SHA256_DIGEST_SIZE];
		char hex[SHA256_HEX_SIZE];
		REQUIRE(modArchiveSha256(path.c_str(), hash) == 0);
		sha256ToHex(hash, hex);
		return std::string(hex);
	};
	const auto first_digest = digest(first);
	const auto second_digest = digest(second);
	REQUIRE(first_digest != second_digest);
	forge_revision_t revision{};
	char err[256]{};

	REQUIRE_FALSE(forgeRevisionPublishLocal(directory.c_str(), id, first.c_str(),
		second_digest.c_str(), &revision, err, sizeof(err)));
	REQUIRE_FALSE(forgeRevisionReadCurrent(directory.c_str(), id, &revision, err, sizeof(err)));
	REQUIRE(forgeRevisionPublishLocal(directory.c_str(), id, first.c_str(),
		first_digest.c_str(), &revision, err, sizeof(err)));
	REQUIRE(validateReadFile(revision.path) == validateReadFile(first));
	REQUIRE(forgeRevisionPublishLocal(directory.c_str(), id, second.c_str(),
		nullptr, &revision, err, sizeof(err)));
	REQUIRE(std::string(revision.digest) == second_digest);
	REQUIRE(forgeRevisionRead(directory.c_str(), id, first_digest.c_str(), &revision, err, sizeof(err)));
	REQUIRE(validateReadFile(revision.path) == validateReadFile(first));
	REQUIRE_FALSE(forgeRevisionRead(directory.c_str(), "mod:wrong_arena",
		first_digest.c_str(), &revision, err, sizeof(err)));

	saveAtomicDebugFailNextCommit();
	REQUIRE_FALSE(forgeRevisionPublishLocal(directory.c_str(), id, first.c_str(),
		first_digest.c_str(), &revision, err, sizeof(err)));
	REQUIRE(forgeRevisionReadCurrent(directory.c_str(), id, &revision, err, sizeof(err)));
	REQUIRE(std::string(revision.digest) == second_digest);
	REQUIRE(validateReadFile(revision.path) == validateReadFile(second));

	u8 id_hash[SHA256_DIGEST_SIZE];
	char map_hash[SHA256_HEX_SIZE];
	sha256Hash(id, static_cast<u32>(std::strlen(id)), id_hash);
	sha256ToHex(id_hash, map_hash);
	auto saved = std::make_unique<mod_author_result_t>();
	const std::string mods_path = dir.path.string();
	mod_author_descriptor_value_t lineage[] = {
		{FORGE_REVISION_PARENT_MAP_KEY, map_hash},
		{FORGE_REVISION_PARENT_DIGEST_KEY, first_digest.c_str()},
	};
	mod_author_request_t request{};
	request.template_path = first.c_str();
	request.mods_root = mods_path.c_str();
	request.display_name = "Lineage Variant";
	request.descriptor_values = lineage;
	request.descriptor_value_count = 2;
	forge_revision_t parent{};
	forge_revision_t variant{};

	SECTION("a remembered predecessor has an independent atomic pointer") {
		const fs::path unpinned_store = dir.path / "unpinned-revisions";
		REQUIRE(fs::create_directory(unpinned_store));
		const auto unpinned = unpinned_store.string();
		forge_revision_t previous{};
		REQUIRE(forgeRevisionRememberPrevious(unpinned.c_str(), id, first.c_str(),
			first_digest.c_str(), &previous, err, sizeof(err)));
		CHECK(std::string(previous.arena_id) == id);
		CHECK(std::string(previous.digest) == first_digest);
		CHECK(validateReadFile(previous.path) == validateReadFile(first));
		const std::string pointer_stem = map_hash;
		const std::string previous_leaf = pointer_stem + ".previous";
		const std::string current_leaf = pointer_stem + ".current";
		CHECK(validateReadFile(unpinned_store / previous_leaf) ==
			std::string("PD2_GRID_REVISION_V1\n") + id + "\n" + first_digest + "\n");
		CHECK_FALSE(fs::exists(unpinned_store / current_leaf));
		REQUIRE(forgeRevisionReadPrevious(unpinned.c_str(), id, &previous, err, sizeof(err)));

		const auto original_pin = validateReadFile(store / current_leaf);
		REQUIRE_FALSE(original_pin.empty());
		REQUIRE(forgeRevisionRememberPrevious(directory.c_str(), id, first.c_str(),
			first_digest.c_str(), &previous, err, sizeof(err)));
		const auto previous_record = validateReadFile(store / previous_leaf);
		REQUIRE_FALSE(previous_record.empty());
		const forge_revision_t remembered = previous;

		REQUIRE_FALSE(forgeRevisionRememberPrevious(directory.c_str(), id, first.c_str(),
			second_digest.c_str(), &previous, err, sizeof(err)));
		CHECK(std::string(err).find("bytes do not match") != std::string::npos);
		REQUIRE_FALSE(forgeRevisionRememberPrevious(directory.c_str(), "mod:wrong_arena", first.c_str(),
			first_digest.c_str(), &previous, err, sizeof(err)));
		CHECK(std::string(err).find("arena ID does not match") != std::string::npos);
		CHECK(std::memcmp(&previous, &remembered, sizeof(previous)) == 0);

		// The second digest already exists, so the injected failure targets the pointer.
		saveAtomicDebugFailNextCommit();
		REQUIRE_FALSE(forgeRevisionRememberPrevious(directory.c_str(), id, second.c_str(),
			second_digest.c_str(), &previous, err, sizeof(err)));
		CHECK(std::string(err).find("previous revision pointer could not be saved") != std::string::npos);
		CHECK(std::memcmp(&previous, &remembered, sizeof(previous)) == 0);
		CHECK(validateReadFile(store / previous_leaf) == previous_record);
		CHECK(validateReadFile(store / current_leaf) == original_pin);
		REQUIRE(forgeRevisionReadPrevious(directory.c_str(), id, &previous, err, sizeof(err)));
		CHECK(std::string(previous.digest) == first_digest);
		REQUIRE(forgeRevisionReadCurrent(directory.c_str(), id, &revision, err, sizeof(err)));
		CHECK(std::string(revision.digest) == second_digest);

		REQUIRE(forgeRevisionRememberPrevious(directory.c_str(), id, second.c_str(),
			second_digest.c_str(), &previous, err, sizeof(err)));
		REQUIRE(forgeRevisionReadPrevious(directory.c_str(), id, &previous, err, sizeof(err)));
		CHECK(std::string(previous.digest) == second_digest);
		CHECK(validateReadFile(previous.path) == validateReadFile(second));
		CHECK(validateReadFile(store / current_leaf) == original_pin);
	}

	SECTION("a Variant resolves the recorded older digest even after the current pin moves") {
		REQUIRE(modAuthorSave(&request, saved.get(), err, sizeof(err)) == MOD_AUTHOR_OK);
		std::snprintf(variant.arena_id, sizeof(variant.arena_id), "%s", saved->catalog_id);
		std::snprintf(variant.path, sizeof(variant.path), "%s", saved->archive_path);
		std::snprintf(variant.digest, sizeof(variant.digest), "%s", digest(variant.path).c_str());
		REQUIRE(forgeRevisionReadParent(directory.c_str(), &variant, &parent, err, sizeof(err))
			== FORGE_REVISION_PARENT_AVAILABLE);
		REQUIRE(std::string(parent.arena_id) == id);
		REQUIRE(std::string(parent.digest) == first_digest);
		REQUIRE(validateReadFile(parent.path) == validateReadFile(first));
		REQUIRE(forgeRevisionReadCurrent(directory.c_str(), id, &revision, err, sizeof(err)));
		REQUIRE(std::string(revision.digest) == second_digest);

		REQUIRE(fs::copy_file(second, parent.path, fs::copy_options::overwrite_existing));
		CHECK(forgeRevisionReadParent(directory.c_str(), &variant, &parent, err, sizeof(err))
			== FORGE_REVISION_PARENT_UNAVAILABLE);
		CHECK(std::string(err).find("bytes do not match") != std::string::npos);
	}

	SECTION("a valid revision digest cannot stand in for the parent's arena identity") {
		lineage[0].value = second_digest.c_str();
		REQUIRE(modAuthorSave(&request, saved.get(), err, sizeof(err)) == MOD_AUTHOR_OK);
		std::snprintf(variant.arena_id, sizeof(variant.arena_id), "%s", saved->catalog_id);
		std::snprintf(variant.path, sizeof(variant.path), "%s", saved->archive_path);
		std::snprintf(variant.digest, sizeof(variant.digest), "%s", digest(variant.path).c_str());
		CHECK(forgeRevisionReadParent(directory.c_str(), &variant, &parent, err, sizeof(err))
			== FORGE_REVISION_PARENT_UNAVAILABLE);
		CHECK(std::string(err).find("arena ID does not match") != std::string::npos);
	}
}

TEST_CASE("Saved Grid variant plans survive reopen and refuse changed metadata",
	"[forge-revision][grid-variant-fallback]")
{
	ValidateTempDir directory;
	const auto root = directory.path.string();
	grid_match_source_package_t package{};
	grid_match_source_entry_t entry{};
	package.owner.type = ASSET_MODEL;
	std::strcpy(package.owner.id, "grid:placed_mesh");
	std::strcpy(package.suffix, ".pdmesh");
	std::strcpy(package.path, "C:/private/source/placed_mesh.pdmesh");
	const auto source = validateExample("tools/smoke-verify/fixtures/grid_required_mesh/tri_required.pdmesh");
	package.size = static_cast<u32>(fs::file_size(source));
	REQUIRE(modArchiveSha256(source.c_str(), package.digest) == 0);
	entry.asset = package.owner;

	grid_match_sources_t selected{};
	selected.entries = &entry;
	selected.packages = &package;
	selected.entry_count = selected.package_count = 1;
	char error[256]{};
	REQUIRE(gridMatchSourcesSeal(&selected, error, sizeof(error)));
	REQUIRE(forgeRevisionStoreDependencyPlan(root.c_str(), &selected, error, sizeof(error)));
	REQUIRE(forgeRevisionStoreDependencyPlan(root.c_str(), &selected, error, sizeof(error)));

	const auto file = directory.path / "sources" / (std::string(selected.digest) + ".plan");
	CHECK(validateReadFile(file).find(package.path) == std::string::npos);

	struct LoadedPlan {
		grid_match_sources_t plan{};
		~LoadedPlan() { gridMatchSourcesFree(&plan); }
	} loaded;
	REQUIRE(forgeRevisionLoadDependencyPlan(root.c_str(), selected.digest, &loaded.plan, error, sizeof(error)));
	REQUIRE(loaded.plan.package_count == 1);
	REQUIRE(loaded.plan.entry_count == 1);
	CHECK(std::string(loaded.plan.entries[0].asset.id) == entry.asset.id);
	CHECK(std::memcmp(loaded.plan.packages[0].digest, package.digest, SHA256_DIGEST_SIZE) == 0);
	CHECK(std::string(loaded.plan.digest) == selected.digest);
	CHECK(loaded.plan.packages[0].path[0] == '\0');

	{
		std::ofstream corrupt(file, std::ios::binary | std::ios::app);
		corrupt << "unexpected content\n";
		REQUIRE(corrupt.good());
	}
	REQUIRE_FALSE(forgeRevisionLoadDependencyPlan(root.c_str(), selected.digest, &loaded.plan, error, sizeof(error)));
	CHECK(std::string(loaded.plan.digest) == selected.digest);
	CHECK(std::string(loaded.plan.entries[0].asset.id) == entry.asset.id);
}

TEST_CASE("Grid match sources group exact catalog closure by immutable owning archive",
	"[grid-match-sources][manifest]")
{
	struct Asset {
		asset_type_e type;
		const char *file;
		const char *member;
		const char *content;
	};
	std::map<std::string, Asset> catalog{
		{"grid:arena", {ASSET_ARENA, "arena.pdarena", "", "arena!"}},
		{"grid:scenario", {ASSET_SCENARIO, "arena.pdarena", "map.pdscenario", "arena!"}},
		{"grid:weapon", {ASSET_WEAPON, "weapon.pdweapon", "", "weapon!"}},
		{"grid:effect", {ASSET_EFFECT, "weapon.pdweapon", "effect.pdeffect", "weapon!"}},
		{"grid:audio", {ASSET_AUDIO, "audio.pdsong", "", "audio!"}},
		{"grid:unrelated", {ASSET_WEAPON, "other.pdweapon", "", "other!"}},
	};
	using Catalog = decltype(catalog);
	const auto resolve = [](const char *id, asset_type_e *type, void *user) -> s32 {
		const auto &table = *static_cast<const Catalog *>(user);
		const auto found = table.find(id);
		if (found == table.end()) return 0;
		*type = found->second.type;
		return 1;
	};
	const auto inspect = [](const grid_match_source_root_t *asset,
			grid_match_source_archive_t *archive, void *user, char *, size_t) -> s32 {
		const auto &entry = static_cast<const Catalog *>(user)->at(asset->id);
		std::snprintf(archive->path, sizeof(archive->path), "%s", entry.file);
		std::snprintf(archive->member_chain, sizeof(archive->member_chain), "%s", entry.member);
		const char *owner = asset->type == ASSET_SCENARIO ? "grid:arena"
			: asset->type == ASSET_EFFECT ? "grid:weapon" : asset->id;
		archive->owner.type = static_cast<const Catalog *>(user)->at(owner).type;
		std::snprintf(archive->owner.id, sizeof(archive->owner.id), "%s", owner);
		const char *suffix = archive->owner.type == ASSET_AUDIO ? ".pdsong"
			: archive->owner.type == ASSET_WEAPON ? ".pdweapon" : ".pdarena";
		std::snprintf(archive->suffix, sizeof(archive->suffix), "%s", suffix);
		archive->size = static_cast<u32>(std::strlen(entry.content));
		sha256Hash(entry.content, archive->size, archive->digest);
		return 1;
	};
	struct SourceTest {
		catalog_dep_snapshot_t previous{};
		grid_match_sources_t manifest{};
		SourceTest() {
			REQUIRE(catalogDepSnapshotCreate(&previous));
			catalogDepClear();
		}
		~SourceTest() {
			gridMatchSourcesFree(&manifest);
			catalogDepSnapshotRestore(&previous);
			catalogDepSnapshotDestroy(&previous);
		}
	} scope;
	REQUIRE(catalogDepRegisterTyped("grid:arena", "grid:weapon", ASSET_WEAPON, 0));
	REQUIRE(catalogDepRegisterTyped("grid:scenario", "grid:weapon", ASSET_WEAPON, 0));
	REQUIRE(catalogDepRegisterTyped("grid:weapon", "grid:effect", ASSET_EFFECT, 0));
	REQUIRE(catalogDepRegisterTyped("grid:effect", "grid:audio", ASSET_AUDIO, 0));
	grid_match_source_root_t roots[] = {{"grid:scenario", ASSET_SCENARIO}, {"grid:arena", ASSET_ARENA}};
	char err[256]{};
	auto &plan = scope.manifest;
	REQUIRE(gridMatchSourcesBuild(&plan, roots, 2, resolve, inspect, &catalog, err, sizeof(err)));
	REQUIRE(plan.entry_count == 5);
	REQUIRE(plan.package_count == 3);
	REQUIRE(std::string(plan.entries[0].asset.id) == "grid:arena");
	REQUIRE(plan.entries[0].package_index == plan.entries[3].package_index);
	REQUIRE(plan.entries[2].package_index == plan.entries[4].package_index);
	const std::string previous = plan.digest;

	std::swap(roots[0], roots[1]);
	catalog["grid:arena"].file = "other_install/renamed.pdarena";
	REQUIRE(gridMatchSourcesBuild(&plan, roots, 2, resolve, inspect, &catalog, err, sizeof(err)));
	REQUIRE(std::string(plan.digest) == previous);
	catalog["grid:audio"].type = ASSET_ANIMATION;
	REQUIRE_FALSE(gridMatchSourcesBuild(&plan, roots, 2, resolve, inspect, &catalog, err, sizeof(err)));
	REQUIRE(std::string(plan.digest) == previous);

	catalog["grid:audio"].type = ASSET_AUDIO;
	catalog["grid:audio"].file = "weapon.pdweapon";
	REQUIRE_FALSE(gridMatchSourcesBuild(&plan, roots, 2, resolve, inspect, &catalog, err, sizeof(err)));
	REQUIRE(std::string(plan.digest) == previous);

	catalog["grid:audio"].file = "audio.pdsong";
	catalog["grid:audio"].content = "audio changed!";
	REQUIRE(gridMatchSourcesBuild(&plan, roots, 2, resolve, inspect, &catalog, err, sizeof(err)));
	REQUIRE(std::string(plan.digest) != previous);
	const std::string updated = plan.digest;
	REQUIRE(catalogDepRegisterTyped("grid:audio", "grid:effect", ASSET_EFFECT, 0));
	REQUIRE_FALSE(gridMatchSourcesBuild(&plan, roots, 2, resolve, inspect, &catalog, err, sizeof(err)));
	REQUIRE(std::string(plan.digest) == updated);
}

TEST_CASE("Grid arena provenance stays in the native descriptor across copies",
	"[grid-provenance][modding][pdxxx]")
{
	const ValidateTempDir dir;
	const char *id = "mod_blank_room:arena_blank_room";
	const char *relative = "dev-mods/blank_room/blank_room.pdarena";
	const auto original = validateReadFile(validateExample(relative));
	auto origin = [](const std::string &bytes) {
		return gridArchiveOrigin("arena.pdarena", bytes.data(), static_cast<u32>(bytes.size()));
	};
	REQUIRE(origin(original) == GRID_ARCHIVE_LEGACY);

	const auto descriptor = validateEntry(original, "arena.ini");
	const auto marked = validateWithMember(dir, relative, id, "arena.ini",
		descriptor + "grid_provenance = byor\n", "byor.pdarena");
	REQUIRE(origin(validateReadFile(marked)) == GRID_ARCHIVE_BYOR);
	const fs::path fork = dir.path / "fork.pdarena";
	char err[256]{};
	REQUIRE(assetArchiveCloneFile(marked.string().c_str(), fork.string().c_str(), "mod:arena_fork",
		"pd-tests", nullptr, err, sizeof(err)) == ASSET_ARCHIVE_CLONE_OK);
	REQUIRE(origin(validateReadFile(fork)) == GRID_ARCHIVE_BYOR);
	const auto ambiguous = validateWithMember(dir, relative, id, "arena.ini",
		descriptor + "grid_provenance = original\ngrid_provenance = byor\n", "ambiguous.pdarena");
	REQUIRE(origin(validateReadFile(ambiguous)) == GRID_ARCHIVE_INVALID);
	const auto author_marked = validateWithMember(dir, relative, id, "arena.ini",
		descriptor + "grid_provenance = original\n", "authored.pdarena");
	REQUIRE(origin(validateReadFile(author_marked)) == GRID_ARCHIVE_ORIGINAL);
}

TEST_CASE("Grid weapon redistribution requires unambiguous native original provenance",
	"[grid-provenance][modding][pdxxx]")
{
	const ValidateTempDir dir;
	const char *id = "example:tri_weapon";
	const char *relative = "examples/modding/typed-pdxxx-basic/weapons/tri_weapon.pdweapon";
	const auto original = validateReadFile(validateExample(relative));
	const auto descriptor = validateEntry(original, "weapon.ini");
	const auto origin = [](const std::string &bytes) {
		return gridArchiveOrigin("received.pdweapon", bytes.data(), static_cast<u32>(bytes.size()));
	};
	REQUIRE(origin(original) == GRID_ARCHIVE_LEGACY);
	const auto author = validateWithMember(dir, relative, id, "weapon.ini",
		descriptor + "grid_provenance = original\n", "original.pdweapon");
	REQUIRE(origin(validateReadFile(author)) == GRID_ARCHIVE_ORIGINAL);
	const auto byor = validateWithMember(dir, relative, id, "weapon.ini",
		descriptor + "grid_provenance = byor\n", "byor.pdweapon");
	REQUIRE(origin(validateReadFile(byor)) == GRID_ARCHIVE_BYOR);
	const auto ambiguous = validateWithMember(dir, relative, id, "weapon.ini",
		descriptor + "grid_provenance = original\ngrid_provenance = byor\n", "ambiguous.pdweapon");
	REQUIRE(origin(validateReadFile(ambiguous)) == GRID_ARCHIVE_INVALID);
	REQUIRE(origin("truncated") == GRID_ARCHIVE_INVALID);
}

TEST_CASE("in-game Validate accepts every checked-in example archive",
          "[modding][validate]") {
	std::vector<fs::path> archives;
	for (const char *root : { "examples/modding/typed-pdxxx-basic", "dev-mods/needler" }) {
		for (const auto &entry : fs::recursive_directory_iterator(fs::path(PD_SOURCE_DIR) / root)) {
			if (entry.is_regular_file() &&
					assetArchivePathIsTyped(entry.path().string().c_str())) {
				archives.push_back(entry.path());
			}
		}
	}
	std::sort(archives.begin(), archives.end());
	REQUIRE(archives.size() >= 28);

	for (const fs::path &archive : archives) {
		const Verdict v = validateFile(archive);
		INFO(archive.filename().string() << " -> " << v.err);
		CHECK(v.code == MOD_ASSET_VALIDATE_OK);
		CHECK(v.err.empty());

		const std::string bytes = validateReadFile(archive);
		char err[160] = {};
		CHECK(modAssetValidateArchiveBytes(bytes.data(), static_cast<u32>(bytes.size()),
			archive.string().c_str(), err, sizeof(err)) == MOD_ASSET_VALIDATE_OK);
	}
}

TEST_CASE("in-game Validate reads the members release validation does not",
          "[modding][validate]") {
	ValidateTempDir dir;

	struct Case {
		const char *what;
		const char *relative;
		const char *id;
		const char *entry;
		std::string bytes;
		s32 expected;
		const char *names;     /* text the reason must contain */
	};

	const std::string vehicleIni =
		validateEntry(validateReadFile(validateExample(kVehicle)), "vehicle.ini");
	const std::string bodyIni =
		validateEntry(validateReadFile(validateExample(kBody)), "body.ini");
	std::string characterIni =
		validateEntry(validateReadFile(validateExample(kCharacter)), "character.ini");
	const std::string nameLine = "display_name = Triangle Character\n";
	REQUIRE(characterIni.find(nameLine) != std::string::npos);
	characterIni.insert(characterIni.find(nameLine), "display_name = Twice\n");
	std::string bodyLocked = bodyIni;
	const std::string bodyId = "catalog_id = example:tri_body\n";
	REQUIRE(bodyLocked.find(bodyId) != std::string::npos);
	bodyLocked.insert(bodyLocked.find(bodyId) + bodyId.size(), "requirefeature = 999\n");

	/* Well-formed JSON that the game's graph compiler refuses. */
	const std::string weaponArchive = validateReadFile(validateExample(kWeapon));
	const auto swapped = [](std::string text, const std::string &from, const std::string &to) {
		const size_t at = text.find(from);
		REQUIRE(at != std::string::npos);
		text.replace(at, from.size(), to);
		return text;
	};
	const std::string primaryGraph = validateEntry(weaponArchive, "behavior/primary.graph.json");
	const std::string secondaryGraph =
		validateEntry(weaponArchive, "behavior/secondary.graph.json");
	const std::string projectileGraph = validateEntry(
		validateEntry(weaponArchive, "dependencies/assets/projectiles/primary.pdprojectile"),
		"behavior.graph.json");
	const std::string primaryUnknownKind =
		swapped(primaryGraph, "\"spawn.fired_projectile\"", "\"fire.nonsense\"");
	const std::string secondaryDanglingLink =
		swapped(secondaryGraph, "\"to\": \"deploy_entity\"", "\"to\": \"nowhere\"");
	const std::string primaryUnknownVariable = swapped(primaryGraph,
		"\"function_type\": \"shoot_projectile\"",
		"\"function_type\": \"shoot_projectile\", \"damage\": \"$no_such_setting\"");
	const std::string projectileUnknownKind =
		swapped(projectileGraph, "\"projectile.motion\"", "\"projectile.nonsense\"");

	const Case cases[] = {
		{ "settings that are not JSON", kWeapon, "example:tri_weapon",
		  "behavior/settings.json", "{ not json", MOD_ASSET_VALIDATE_ERR_TEXT,
		  "tri_weapon_0.pdweapon::behavior/settings.json" },
		{ "a graph with a duplicate key", kWeapon, "example:tri_weapon",
		  "behavior/variables.json",
		  "{ \"schema\": \"pd.weapon_variables.v1\", \"schema\": \"again\", \"variables\": [] }",
		  MOD_ASSET_VALIDATE_ERR_TEXT, "behavior/variables.json" },
		{ "JSON with a byte order mark", kWeapon, "example:tri_weapon",
		  "bindings/presentation.json", "\xef\xbb\xbf{ \"schema\": \"pd.weapon.presentation.v1\" }",
		  MOD_ASSET_VALIDATE_ERR_TEXT, "bindings/presentation.json" },
		{ "JSON with trailing text", kWeapon, "example:tri_weapon",
		  "behavior/settings.json", "{ \"schema\": \"pd.weapon_settings.v1\" } trailing",
		  MOD_ASSET_VALIDATE_ERR_TEXT, "behavior/settings.json" },
		{ "a broken graph inside an embedded projectile", kWeapon, "example:tri_weapon",
		  "dependencies/assets/projectiles/primary.pdprojectile::behavior.graph.json",
		  "{ \"schema\": \"pd.projectile_graph.v1\", ", MOD_ASSET_VALIDATE_ERR_TEXT,
		  "dependencies/assets/projectiles/primary.pdprojectile::behavior.graph.json" },
		{ "a weapon function the game does not have", kWeapon, "example:tri_weapon",
		  "behavior/primary.graph.json", primaryUnknownKind, MOD_ASSET_VALIDATE_ERR_FAMILY,
		  "unsupported weapon graph module fire.nonsense" },
		{ "a weapon link to a node that is not there", kWeapon, "example:tri_weapon",
		  "behavior/secondary.graph.json", secondaryDanglingLink, MOD_ASSET_VALIDATE_ERR_FAMILY,
		  "behavior does not compile: edge references missing node" },
		{ "a weapon variable nothing defines", kWeapon, "example:tri_weapon",
		  "behavior/primary.graph.json", primaryUnknownVariable, MOD_ASSET_VALIDATE_ERR_FAMILY,
		  "unresolved weapon variable '$no_such_setting'" },
		{ "a projectile module the game does not have", kWeapon, "example:tri_weapon",
		  "dependencies/assets/projectiles/primary.pdprojectile::behavior.graph.json",
		  projectileUnknownKind, MOD_ASSET_VALIDATE_ERR_FAMILY,
		  "primary.pdprojectile::behavior.graph.json does not compile" },
		{ "a shared context cut short", kWeapon, "example:tri_weapon",
		  "behavior/shared-context.json", "{ \"schema\": \"pd.weapon_shared_context.v1\", \"contexts\": [",
		  MOD_ASSET_VALIDATE_ERR_TEXT, "behavior/shared-context.json" },
		{ "a descriptor line with no equals sign", kVehicle, "example:tri_vehicle",
		  "vehicle.ini", vehicleIni + "\nname: Colon Is Not Equals\n",
		  MOD_ASSET_VALIDATE_ERR_TEXT, "vehicle.ini line " },
		{ "a descriptor that is not UTF-8", kVehicle, "example:tri_vehicle",
		  "vehicle.ini", vehicleIni + "note = caf\xe9\n",
		  MOD_ASSET_VALIDATE_ERR_TEXT, "not valid UTF-8" },
		{ "a body field its reader refuses", kBody, "example:tri_body",
		  "body.ini", bodyLocked, MOD_ASSET_VALIDATE_ERR_FAMILY, "body reader" },
		{ "a character key given twice", kCharacter, "example:tri_character",
		  "character.ini", characterIni, MOD_ASSET_VALIDATE_ERR_FAMILY, "character reader" },
	};

	unsigned index = 0;
	for (const Case &c : cases) {
		const std::string ext = fs::path(c.relative).extension().string();
		const std::string leaf = fs::path(c.relative).stem().string() + "_" +
			std::to_string(index++) + ext;
		const fs::path archive =
			validateWithMember(dir, c.relative, c.id, c.entry, c.bytes, leaf.c_str());
		const Verdict v = validateFile(archive);
		INFO(c.what << " -> code " << v.code << ": " << v.err);
		CHECK(v.code == c.expected);
		CHECK(!v.err.empty());
		CHECK(v.err.find(c.names) != std::string::npos);
	}
}

TEST_CASE("in-game Validate refuses what is not a typed archive",
          "[modding][validate]") {
	ValidateTempDir dir;
	char err[160] = {};
	const std::string weapon = validateReadFile(validateExample(kWeapon));

	CHECK(modAssetValidateArchiveBytes(nullptr, 0, "x.pdweapon", err, sizeof(err))
		== MOD_ASSET_VALIDATE_ERR_ARGS);
	CHECK(modAssetValidateArchiveBytes(weapon.data(), static_cast<u32>(weapon.size()),
		"x.zip", err, sizeof(err)) == MOD_ASSET_VALIDATE_ERR_ARGS);
	CHECK(modAssetValidateArchiveBytes(weapon.data(), static_cast<u32>(weapon.size()),
		nullptr, err, sizeof(err)) == MOD_ASSET_VALIDATE_ERR_ARGS);
	CHECK(modAssetValidateArchiveFile(nullptr, err, sizeof(err)) == MOD_ASSET_VALIDATE_ERR_ARGS);
	CHECK(modAssetValidateArchiveFile((dir.path / "missing.pdweapon").string().c_str(),
		err, sizeof(err)) == MOD_ASSET_VALIDATE_ERR_ARGS);

	/* A weapon's bytes under a vehicle's name, and bytes that are no archive. */
	CHECK(modAssetValidateArchiveBytes(weapon.data(), static_cast<u32>(weapon.size()),
		"x.pdvehicle", err, sizeof(err)) == MOD_ASSET_VALIDATE_ERR_POLICY);
	CHECK(std::string(err).size() > 0);
	const std::string junk = "this is not a zip archive";
	CHECK(modAssetValidateArchiveBytes(junk.data(), static_cast<u32>(junk.size()),
		"x.pdweapon", err, sizeof(err)) == MOD_ASSET_VALIDATE_ERR_POLICY);

	/* The reason is optional. */
	CHECK(modAssetValidateArchiveBytes(weapon.data(), static_cast<u32>(weapon.size()),
		"x.pdweapon", nullptr, 0) == MOD_ASSET_VALIDATE_OK);
}

TEST_CASE("mod author refuses to save an archive whose content is malformed",
          "[modding][validate][author]") {
	ValidateTempDir mods;
	const std::string tmpl = validateExample(kWeapon);
	const std::string root = mods.path.string();
	auto result = std::make_unique<mod_author_result_t>();
	char err[320] = {};

	mod_author_request_t request{};
	request.template_path = tmpl.c_str();
	request.mods_root = root.c_str();
	request.display_name = "Checked Gun";
	REQUIRE(modAuthorSave(&request, result.get(), err, sizeof(err)) == MOD_AUTHOR_OK);
	const std::string archivePath = result->archive_path;
	const std::string manifestPath = result->manifest_path;
	const std::string archiveBefore = validateReadFile(archivePath);
	const std::string manifestBefore = validateReadFile(manifestPath);

	asset_archive_clone_override_t broken{};
	broken.entry = "behavior/settings.json";
	broken.data = "{ not json";
	broken.len = 10;
	request.overrides = &broken;
	request.override_count = 1;
	request.version = "9.9.9";
	const s32 code = modAuthorSave(&request, result.get(), err, sizeof(err));
	INFO("save error: " << err);
	CHECK(code == MOD_AUTHOR_ERR_ARCHIVE);
	CHECK(std::string(err).find("behavior/settings.json") != std::string::npos);

	/* The working mod is untouched and nothing staged is left beside it. */
	CHECK(validateReadFile(archivePath) == archiveBefore);
	CHECK(validateReadFile(manifestPath) == manifestBefore);
	size_t files = 0;
	for (const auto &entry : fs::recursive_directory_iterator(mods.path)) {
		if (entry.is_regular_file()) files++;
	}
	CHECK(files == 2);
}

TEST_CASE("pdmod packers refuse a typed archive whose content is malformed",
          "[modding][validate][pdmod]") {
	ValidateTempDir dir;
	const fs::path broken = validateWithMember(dir, kWeapon, "example:tri_weapon",
		"behavior/settings.json", "{ not json", "broken.pdweapon");
	const std::string manifest =
		"{ \"id\": \"mod.packcheck\", \"name\": \"Pack Check\", \"version\": \"1.0.0\" }\n";
	const fs::path folder = dir.path / "mod";
	fs::create_directories(folder);
	{
		std::ofstream out(folder / "mod.json", std::ios::binary);
		out << manifest;
	}
	fs::copy_file(broken, folder / "broken.pdweapon");
	const std::string pack = (dir.path / "out.pdmod").string();

	/* Folder packer. */
	CHECK(modpackPdmodFromFolder(folder.string().c_str(), pack.c_str())
		== MODPACK_PDMOD_ERR_LAYOUT);
	CHECK(std::string(modpackPdmodLastError()).find("behavior/settings.json")
		!= std::string::npos);
	CHECK(!fs::exists(pack));

	/* Single-archive writer. */
	const std::string bytes = validateReadFile(broken);
	modpack_entry_t entry{};
	entry.entry_name = "weapons/broken.pdweapon";
	entry.data = bytes.data();
	entry.len = static_cast<u32>(bytes.size());
	CHECK(modpackPdmodWriteSingle(pack.c_str(), manifest.data(),
		static_cast<u32>(manifest.size()), &entry, 1) == MODPACK_PDMOD_ERR_LAYOUT);
	CHECK(std::string(modpackPdmodLastError()).find("behavior/settings.json")
		!= std::string::npos);
	CHECK(!fs::exists(pack));

	/* Control: the same folder with the unbroken example packs. */
	fs::remove(folder / "broken.pdweapon");
	fs::copy_file(validateExample(kWeapon), folder / "good.pdweapon");
	const s32 packed = modpackPdmodFromFolder(folder.string().c_str(), pack.c_str());
	INFO("pack error: " << modpackPdmodLastError());
	CHECK(packed == MODPACK_PDMOD_OK);
	CHECK(fs::exists(pack));
}
