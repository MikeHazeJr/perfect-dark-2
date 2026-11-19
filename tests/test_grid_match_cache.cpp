#include "catch.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#include "asset_archive_clone.h"
#include "assetcatalog_deps.h"
#include "grid_match_cache.h"
#include "grid_match_package_io.h"
#include "modasset_material_order.h"
#include "modarchive.h"
#include "net/grid_match_wire.h"

/* The C netbuf header includes the project's C bool macro. */
struct netbuf {
	u8 *data;
	u32 size;
	u32 rp;
	u32 wp;
	u32 error;
};

namespace {

namespace fs = std::filesystem;

const char *const kOriginalPath = "examples/modding/typed-pdxxx-basic/arenas/tri_arena.pdarena";
const char *const kScenarioMember = "dependencies/assets/scenarios/tri_scenario.pdscenario";
const char *const kSyntheticPath = "synthetic.pdarena";
const grid_match_source_root_t kRoots[] = {
	{"example:tri_arena", ASSET_ARENA},
	{"example:tri_scenario", ASSET_SCENARIO},
	{"grid_cache:synthetic_arena", ASSET_ARENA},
};

struct CacheTempDir {
	fs::path path = fs::temp_directory_path() / ("pd2-grid-match-cache-" + std::to_string(
		std::chrono::high_resolution_clock::now().time_since_epoch().count()));

	CacheTempDir() { REQUIRE(fs::create_directory(path)); }
	~CacheTempDir()
	{
		std::error_code error;
		fs::remove_all(path, error);
	}
};

struct CacheTest {
	grid_match_sources_t plan{};
	grid_match_cache_t *cache = nullptr;
	char error[512]{};

	~CacheTest()
	{
		gridMatchCacheReset(&cache);
		gridMatchSourcesFree(&plan);
	}
};

struct OwnerBytes {
	std::string original;
	std::string synthetic;
};

std::string readBytes(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	REQUIRE(in.is_open());
	std::string bytes(std::istreambuf_iterator<char>(in), {});
	REQUIRE_FALSE(bytes.empty());
	return bytes;
}

OwnerBytes readFixtures(const CacheTempDir &directory)
{
	const auto path = (fs::path(PD_SOURCE_DIR) / kOriginalPath).string();
	const auto synthetic = (directory.path / kSyntheticPath).string();
	char error[512]{};
	const s32 result = assetArchiveCloneFile(path.c_str(), synthetic.c_str(),
		kRoots[2].id, "pd-tests", nullptr, error, sizeof(error));
	INFO(error);
	REQUIRE(result == ASSET_ARCHIVE_CLONE_OK);
	return {readBytes(path), readBytes(synthetic)};
}

s32 resolveFixture(const char *id, asset_type_e *type, void *)
{
	for (const auto &root : kRoots) {
		if (std::strcmp(root.id, id) == 0) {
			*type = root.type;
			return 1;
		}
	}
	return 0;
}

s32 inspectFixture(const grid_match_source_root_t *asset, grid_match_source_archive_t *archive,
	void *userdata, char *, size_t)
{
	const auto &owners = *static_cast<const OwnerBytes *>(userdata);
	const bool synthetic = std::strcmp(asset->id, kRoots[2].id) == 0;
	const std::string &bytes = synthetic ? owners.synthetic : owners.original;
	std::snprintf(archive->path, sizeof(archive->path), "%s", synthetic ? kSyntheticPath : kOriginalPath);
	archive->owner = synthetic ? kRoots[2] : kRoots[0];
	std::snprintf(archive->suffix, sizeof(archive->suffix), "%s", ".pdarena");
	if (asset->type == ASSET_SCENARIO) {
		std::snprintf(archive->member_chain, sizeof(archive->member_chain), "%s", kScenarioMember);
	}
	archive->size = static_cast<u32>(bytes.size());
	sha256Hash(bytes.data(), bytes.size(), archive->digest);
	return 1;
}

grid_match_offer_t fixtureOffer(CacheTest &test, OwnerBytes &bytes,
	const grid_match_source_root_t *roots, size_t root_count, u64 generation, u8 room = 2)
{
	const s32 result = gridMatchSourcesBuild(&test.plan, roots, root_count,
		resolveFixture, inspectFixture, &bytes, test.error, sizeof(test.error));
	INFO(test.error);
	REQUIRE(result);
	grid_match_offer_t offer{};
	offer.room_id = room;
	offer.generation = generation;
	std::memcpy(offer.manifest_digest, test.plan.digest, SHA256_HEX_SIZE);
	return offer;
}

grid_match_offer_t buildOffer(CacheTest &test, OwnerBytes &bytes)
{
	const auto offer = fixtureOffer(test, bytes, kRoots, std::size(kRoots), 31);
	REQUIRE(test.plan.entry_count == 3);
	REQUIRE(test.plan.package_count == 2);
	REQUIRE(test.plan.entries[0].package_index == test.plan.entries[1].package_index);
	return offer;
}

s32 accept(CacheTest &test, const grid_match_offer_t &offer, size_t index, const std::string &bytes)
{
	return gridMatchCacheAccept(test.cache, &offer, index, bytes.data(),
		static_cast<u32>(bytes.size()), test.error, sizeof(test.error));
}

} // namespace

TEST_CASE("Grid staging publishes the exact archive set together for one offer", "[grid-match-cache]")
{
	const CacheTempDir temp;
	OwnerBytes bytes = readFixtures(temp);
	REQUIRE(bytes.original != bytes.synthetic);
	CacheTest test;
	const auto offer = buildOffer(test, bytes);
	const size_t original = test.plan.entries[0].package_index;
	const size_t synthetic = test.plan.entries[2].package_index;
	for (size_t i = 0; i < test.plan.package_count; i++) test.plan.packages[i].path[0] = '\0';
	REQUIRE(gridMatchCacheBegin(&test.cache, &offer, &test.plan, test.error, sizeof(test.error)));

	auto other = offer;
	other.room_id++;
	CHECK_FALSE(accept(test, other, original, bytes.original));
	other = offer;
	other.generation++;
	CHECK_FALSE(accept(test, other, original, bytes.original));
	other = offer;
	other.manifest_digest[0] = other.manifest_digest[0] == 'a' ? 'b' : 'a';
	CHECK_FALSE(accept(test, other, original, bytes.original));
	CHECK_FALSE(accept(test, offer, test.plan.package_count, bytes.original));
	CHECK_FALSE(accept(test, offer, original, bytes.original.substr(1)));

	auto tampered = bytes.original;
	tampered[0] ^= 1;
	CHECK_FALSE(accept(test, offer, original, tampered));
	CHECK(std::strstr(test.error, "digest") != nullptr);
	auto delivery = bytes.original;
	REQUIRE(accept(test, offer, original, delivery));
	std::fill(delivery.begin(), delivery.end(), '\0');
	REQUIRE(accept(test, offer, original, bytes.original));
	CHECK_FALSE(accept(test, offer, original, tampered));
	CHECK_FALSE(gridMatchCacheReady(test.cache, &offer));
	u32 size = 1;
	CHECK(gridMatchCacheRead(test.cache, &offer, original, &size) == nullptr);
	CHECK(size == 0);
	CHECK(gridMatchCacheRead(test.cache, &offer, synthetic, nullptr) == nullptr);

	auto next = offer;
	++next.generation;
	CHECK_FALSE(gridMatchCacheBegin(&test.cache, &next, &test.plan, test.error, sizeof(test.error)));
	std::memset(test.plan.packages, 0, test.plan.package_count * sizeof(test.plan.packages[0]));
	std::memset(test.plan.entries, 0, test.plan.entry_count * sizeof(test.plan.entries[0]));
	gridMatchSourcesFree(&test.plan);
	const s32 accepted = accept(test, offer, synthetic, bytes.synthetic);
	INFO(test.error);
	REQUIRE(accepted);
	REQUIRE(gridMatchCacheReady(test.cache, &offer));
	REQUIRE(gridMatchCacheCopySources(test.cache, &offer, &test.plan, test.error, sizeof(test.error)));
	CHECK(gridMatchCacheValidateCombinedBudget(&test.plan, test.cache, &offer,
		test.cache, &offer, test.error, sizeof(test.error)));
	CHECK_FALSE(gridMatchCacheValidateCombinedBudget(&test.plan, test.cache, &offer,
		test.cache, &next, test.error, sizeof(test.error)));
	CHECK_FALSE(gridMatchCacheReady(test.cache, &next));
	CHECK(gridMatchCacheRead(test.cache, &next, original, nullptr) == nullptr);
	CHECK(gridMatchCacheRead(test.cache, &offer, 2, nullptr) == nullptr);

	const auto *originalBytes = gridMatchCacheRead(test.cache, &offer, original, &size);
	REQUIRE(originalBytes != nullptr);
	REQUIRE(size == bytes.original.size());
	CHECK(std::memcmp(originalBytes, bytes.original.data(), size) == 0);
	const auto *syntheticBytes = gridMatchCacheRead(test.cache, &offer, synthetic, &size);
	REQUIRE(syntheticBytes != nullptr);
	REQUIRE(size == bytes.synthetic.size());
	CHECK(std::memcmp(syntheticBytes, bytes.synthetic.data(), size) == 0);

	gridMatchCacheReset(&test.cache);
	CHECK(test.cache == nullptr);
	CHECK_FALSE(gridMatchCacheReady(test.cache, &offer));
	REQUIRE(buildOffer(test, bytes).generation == offer.generation);
	REQUIRE(gridMatchCacheBegin(&test.cache, &next, &test.plan, test.error, sizeof(test.error)));
	CHECK_FALSE(accept(test, offer, synthetic, bytes.synthetic));
	CHECK_FALSE(gridMatchCacheReady(test.cache, &next));
}

TEST_CASE("Grid staging refuses invalid typed data with a matching digest", "[grid-match-cache]")
{
	const CacheTempDir temp;
	OwnerBytes bytes = readFixtures(temp);
	/* Keep the typed ZIP and its descriptor, with one malformed public INI line. */
	const auto syntheticPath = (temp.path / kSyntheticPath).string();
	u32 descriptorSize = 0;
	void *rawDescriptor = modArchiveExtractMemAlloc(bytes.synthetic.data(),
		static_cast<u32>(bytes.synthetic.size()), "arena.ini", &descriptorSize);
	REQUIRE(rawDescriptor != nullptr);
	std::string descriptor(static_cast<const char *>(rawDescriptor), descriptorSize);
	std::free(rawDescriptor);
	descriptor += "\n[cache_fixture]\nmalformed line\n";
	REQUIRE(modArchiveReplaceFileMem(syntheticPath.c_str(), "arena.ini",
		descriptor.data(), static_cast<u32>(descriptor.size())) == MODARCHIVE_OK);
	bytes.synthetic = readBytes(syntheticPath);
	CacheTest test;
	const auto offer = buildOffer(test, bytes);
	REQUIRE(gridMatchCacheBegin(&test.cache, &offer, &test.plan, test.error, sizeof(test.error)));
	const size_t original = test.plan.entries[0].package_index;
	const size_t synthetic = test.plan.entries[2].package_index;
	REQUIRE(accept(test, offer, original, bytes.original));
	CHECK_FALSE(accept(test, offer, synthetic, bytes.synthetic));
	CHECK(std::strstr(test.error, "arena.ini") != nullptr);
	CHECK_FALSE(gridMatchCacheReady(test.cache, &offer));
	CHECK(gridMatchCacheRead(test.cache, &offer, original, nullptr) == nullptr);
}

TEST_CASE("Grid staging checks the offer and package budgets before allocating", "[grid-match-cache]")
{
	constexpr u32 mib = 1024 * 1024;
	grid_match_source_entry_t entries[5]{};
	grid_match_source_package_t packages[5]{};
	for (size_t i = 0; i < std::size(packages); i++) {
		auto &package = packages[i];
		std::strcpy(package.suffix, ".pdarena");
		std::snprintf(package.owner.id, sizeof(package.owner.id), "grid_cache:arena_%zu", i);
		package.owner.type = ASSET_ARENA;
		package.size = 50 * mib;
		package.digest[0] = static_cast<u8>(i + 1);
		entries[i].asset = package.owner;
		entries[i].package_index = i;
	}
	grid_match_sources_t plan{};
	plan.entries = entries;
	plan.entry_count = 1;
	plan.packages = packages;
	plan.package_count = 1;
	char error[256]{};
	REQUIRE(gridMatchSourcesSeal(&plan, error, sizeof(error)));
	grid_match_offer_t offer{};
	std::memcpy(offer.manifest_digest, plan.digest, SHA256_HEX_SIZE);
	grid_match_cache_t *cache = nullptr;

	CHECK_FALSE(gridMatchCacheBegin(&cache, &offer, &plan, error, sizeof(error)));
	offer.generation = 1;
	offer.room_id = 0xff;
	CHECK_FALSE(gridMatchCacheBegin(&cache, &offer, &plan, error, sizeof(error)));
	offer.room_id = 2;
	offer.manifest_digest[0] = 'F';
	CHECK_FALSE(gridMatchCacheBegin(&cache, &offer, &plan, error, sizeof(error)));
	offer.manifest_digest[0] = plan.digest[0] == 'e' ? 'f' : 'e';
	CHECK_FALSE(gridMatchCacheBegin(&cache, &offer, &plan, error, sizeof(error)));
	offer.manifest_digest[0] = plan.digest[0];
	packages[0].size++;
	plan.total_bytes++;
	CHECK_FALSE(gridMatchCacheBegin(&cache, &offer, &plan, error, sizeof(error)));
	CHECK(std::strstr(error, "package limit") != nullptr);
	packages[0].size--;
	plan.entry_count = std::size(entries);
	plan.package_count = std::size(packages);
	plan.total_bytes = 250 * mib;
	CHECK_FALSE(gridMatchCacheBegin(&cache, &offer, &plan, error, sizeof(error)));
	CHECK(std::strstr(error, "session budget") != nullptr);
	CHECK(cache == nullptr);
	gridMatchCacheReset(&cache);

	grid_match_sources_t gameplay{};
	gameplay.entries = entries;
	gameplay.entry_count = 3;
	gameplay.packages = packages;
	gameplay.package_count = 3;
	grid_match_sources_t arena{};
	arena.entries = entries + 3;
	arena.entry_count = 2;
	arena.packages = packages + 3;
	arena.package_count = 2;
	entries[3].package_index = 0;
	entries[4].package_index = 1;
	REQUIRE(gridMatchSourcesSeal(&gameplay, error, sizeof(error)));
	REQUIRE(gridMatchSourcesSeal(&arena, error, sizeof(error)));
	CacheTest combined;
	CHECK_FALSE(gridMatchSourcesCombine(&combined.plan, &gameplay, &arena, error, sizeof(error)));
	CHECK(std::strstr(error, "session budget") != nullptr);
	CHECK(combined.plan.entries == nullptr);
}

TEST_CASE("Grid checks the borrowed archive envelope before a native source probe", "[grid-match-cache]")
{
	const auto bytes = readBytes(fs::path(PD_SOURCE_DIR) / kOriginalPath);
	char error[256]{};
	REQUIRE(gridMatchCacheCheckArchiveBudget(bytes.data(), static_cast<u32>(bytes.size()), error, sizeof(error)));
	CHECK(error[0] == '\0');
	CHECK_FALSE(gridMatchCacheCheckArchiveBudget(bytes.data(), 50u * 1024u * 1024u + 1u, error, sizeof(error)));
	CHECK(std::strstr(error, "budget") != nullptr);
	CHECK_FALSE(gridMatchCacheCheckArchiveBudget(bytes.data(), 4, error, sizeof(error)));
	CHECK_FALSE(gridMatchCacheCheckArchiveBudget(nullptr, 1, error, sizeof(error)));
}

TEST_CASE("Grid staging verifies nested typed identity against the exact archive", "[grid-match-cache]")
{
	const CacheTempDir temp;
	OwnerBytes bytes = readFixtures(temp);
	CacheTest test;
	auto offer = buildOffer(test, bytes);
	auto &nested = test.plan.entries[1];
	const size_t original = nested.package_index;
	const size_t synthetic = test.plan.entries[2].package_index;
	SECTION("a typed member that is absent")
	{
		std::strcpy(nested.member_chain, "dependencies/missing.pdscenario");
	}
	SECTION("a typed member with a different catalog ID")
	{
		std::strcpy(nested.asset.id, "example:tri_scenario_other");
	}
	SECTION("an intermediate member without a typed archive suffix")
	{
		std::strcpy(nested.member_chain, "loose.zip::level.pdscenario");
		CHECK_FALSE(gridMatchSourcesSeal(&test.plan, test.error, sizeof(test.error)));
		return;
	}
	REQUIRE(gridMatchSourcesSeal(&test.plan, test.error, sizeof(test.error)));
	std::memcpy(offer.manifest_digest, test.plan.digest, SHA256_HEX_SIZE);
	REQUIRE(gridMatchCacheBegin(&test.cache, &offer, &test.plan, test.error, sizeof(test.error)));
	REQUIRE(accept(test, offer, synthetic, bytes.synthetic));
	CHECK_FALSE(accept(test, offer, original, bytes.original));
	CHECK(std::strstr(test.error, "exact typed asset") != nullptr);
	CHECK_FALSE(gridMatchCacheReady(test.cache, &offer));
	CHECK(gridMatchCacheRead(test.cache, &offer, synthetic, nullptr) == nullptr);
}


namespace {

const grid_match_source_root_t kCrystalWeapon = {"mod_crystal_pistol:crystal_pistol", ASSET_WEAPON};
const grid_match_source_root_t kCrystalMesh = {"mod_crystal_pistol:crystal_pistol_model", ASSET_MODEL};
const grid_match_source_root_t kCrystalTexture = {"mod_crystal_pistol:crystal_pistol_body", ASSET_TEXTURE};
const char *const kCrystalMeshMember = "dependencies/assets/models/crystal_pistol.pdmesh";
const char *const kCrystalWeaponPath = "dev-mods/crystal_pistol/crystal_pistol.pdweapon";
const char *const kCrystalTexturePath = "dev-mods/crystal_pistol/crystal_pistol_body.pdtexture";

struct DependencySnapshot {
	catalog_dep_snapshot_t edges{};
	DependencySnapshot() { REQUIRE(catalogDepSnapshotCreate(&edges)); }
	~DependencySnapshot()
	{
		catalogDepSnapshotRestore(&edges);
		catalogDepSnapshotDestroy(&edges);
	}
};

struct CrystalSources {
	std::string weapon = readBytes(fs::path(PD_SOURCE_DIR) / kCrystalWeaponPath);
	std::string texture = readBytes(fs::path(PD_SOURCE_DIR) / kCrystalTexturePath);
	asset_type_e texture_type = ASSET_TEXTURE;
	s32 model_inspections = 0;
	bool complete_owner = false;
	std::string other_owner_bytes;
};

std::string changedCrystalTexture(const fs::path &destination)
{
	const auto source_path = (fs::path(PD_SOURCE_DIR) / kCrystalTexturePath).string();
	std::unique_ptr<mod_archive_t, decltype(&modArchiveClose)> source(modArchiveOpen(source_path.c_str()),
		modArchiveClose);
	REQUIRE(source);
	std::unique_ptr<mod_archive_writer_t, decltype(&modArchiveAbort)> writer(
		modArchiveBegin(destination.string().c_str()), modArchiveAbort);
	REQUIRE(writer);
	for (s32 i = 0; i < modArchiveGetEntryCount(source.get()); i++) {
		const char *name = modArchiveGetEntryName(source.get(), i);
		u32 size = 0;
		std::unique_ptr<void, decltype(&std::free)> bytes(modArchiveExtractAlloc(source.get(), i, &size), std::free);
		REQUIRE(bytes);
		if (std::strcmp(name, "texture.ini") == 0) {
			std::string descriptor(static_cast<const char *>(bytes.get()), size);
			descriptor += "\n; revision used by the Grid digest regression\n";
			REQUIRE(modArchiveAddFileMem(writer.get(), name, descriptor.data(), static_cast<u32>(descriptor.size()))
				== MODARCHIVE_OK);
		} else {
			REQUIRE(modArchiveAddFileMem(writer.get(), name, bytes.get(), size) == MODARCHIVE_OK);
		}
	}
	REQUIRE(modArchiveFinish(writer.release()) == MODARCHIVE_OK);
	return readBytes(destination);
}

s32 resolveCrystal(const char *id, asset_type_e *type, void *userdata)
{
	const auto &sources = *static_cast<const CrystalSources *>(userdata);
	if (std::strcmp(id, kCrystalTexture.id) == 0 && sources.texture_type != ASSET_NONE) {
		*type = sources.texture_type;
		return 1;
	}
	for (const auto &root : {kCrystalWeapon, kCrystalMesh}) {
		if (std::strcmp(root.id, id) == 0) {
			*type = root.type;
			return 1;
		}
	}
	return 0;
}

void setCrystalArchive(grid_match_source_archive_t &archive, const grid_match_source_root_t &owner,
	const char *path, const char *suffix, const std::string &bytes)
{
	archive.owner = owner;
	std::snprintf(archive.path, sizeof(archive.path), "%s", path);
	std::snprintf(archive.suffix, sizeof(archive.suffix), "%s", suffix);
	archive.size = static_cast<u32>(bytes.size());
	sha256Hash(bytes.data(), bytes.size(), archive.digest);
}

s32 inspectCrystal(const grid_match_source_root_t *asset, grid_match_source_archive_t *archive,
	void *userdata, char *, size_t)
{
	const auto &sources = *static_cast<const CrystalSources *>(userdata);
	if (asset->type == ASSET_WEAPON) {
		const auto &bytes = sources.other_owner_bytes.empty() ? sources.weapon : sources.other_owner_bytes;
		setCrystalArchive(*archive, kCrystalWeapon, kCrystalWeaponPath, ".pdweapon", bytes);
		return 1;
	}
	if (asset->type == ASSET_TEXTURE) {
		setCrystalArchive(*archive, kCrystalTexture, kCrystalTexturePath, ".pdtexture", sources.texture);
		return 1;
	}
	return 0;
}

s32 inspectCrystalMesh(const grid_match_source_root_t *asset, grid_match_source_archive_t *archive,
	grid_match_source_texture_visit_fn visit, void *visit_data, void *userdata, char *err, size_t err_cap)
{
	if (std::strcmp(asset->id, kCrystalMesh.id) != 0) return 0;
	auto &sources = *static_cast<CrystalSources *>(userdata);
	sources.model_inspections++;
	using Buffer = std::unique_ptr<void, decltype(&std::free)>;
	u32 mesh_size = 0, mtl_size = 0;
	const Buffer mesh(gridMatchPackageReadMember(sources.weapon.data(), static_cast<u32>(sources.weapon.size()),
		kCrystalMeshMember, 1024 * 1024, &mesh_size, nullptr), std::free);
	if (!mesh) return 0;
	const Buffer mtl(gridMatchPackageReadMember(mesh.get(), mesh_size, "model.mtl", 64 * 1024, &mtl_size, nullptr),
		std::free);
	if (!mtl || !modAssetMaterialVisitCatalogTextures(static_cast<const char *>(mtl.get()),
			mtl_size, visit, visit_data, err, err_cap)) return 0;
	setCrystalArchive(*archive, kCrystalWeapon, kCrystalWeaponPath, ".pdweapon", sources.weapon);
	archive->complete_owner = sources.complete_owner;
	std::snprintf(archive->member_chain, sizeof(archive->member_chain), "%s", kCrystalMeshMember);
	return 1;
}

struct CrystalExtraEdges {
	const char *weapon_dependency = kCrystalMesh.id;
	asset_type_e weapon_dependency_type = ASSET_MODEL;
	bool cycle = false;
};

s32 appendCrystalExtraEdges(const char *id, asset_type_e type, catalog_dep_activation_add_fn add,
	void *add_context, void *userdata, char *, size_t)
{
	const auto &edges = *static_cast<const CrystalExtraEdges *>(userdata);
	if (type == ASSET_WEAPON && std::strcmp(id, kCrystalWeapon.id) == 0) {
		/* Two declarations of the same dependency still produce one row. */
		return add(edges.weapon_dependency, edges.weapon_dependency_type, add_context)
			&& add(edges.weapon_dependency, edges.weapon_dependency_type, add_context);
	}
	if (edges.cycle && type == ASSET_TEXTURE && std::strcmp(id, kCrystalTexture.id) == 0) {
		return add(kCrystalWeapon.id, ASSET_WEAPON, add_context);
	}
	return 1;
}

} // namespace

TEST_CASE("Grid sources bind the native Crystal Pistol's external OBJ texture", "[grid-match-cache][material-texture]")
{
	const DependencySnapshot registry;
	REQUIRE(catalogDepRegisterTyped(kCrystalWeapon.id, kCrystalMesh.id, ASSET_MODEL, 0));
	CrystalSources sources;
	CacheTest test;
	const grid_match_source_root_t roots[] = {kCrystalWeapon, kCrystalMesh};
	auto build = [&]() {
		return gridMatchSourcesBuildWithModelTextures(&test.plan, roots, std::size(roots), resolveCrystal,
			inspectCrystal, inspectCrystalMesh, &sources, test.error, sizeof(test.error));
	};
	const s32 initial = build();
	INFO(test.error);
	REQUIRE(initial);
	REQUIRE(test.plan.entry_count == 3);
	REQUIRE(test.plan.package_count == 2);
	REQUIRE(sources.model_inspections == 1);
	const auto &weapon = test.plan.entries[0];
	const auto &texture = test.plan.entries[1];
	const auto &mesh = test.plan.entries[2];
	REQUIRE(std::strcmp(weapon.asset.id, kCrystalWeapon.id) == 0);
	REQUIRE(std::strcmp(texture.asset.id, kCrystalTexture.id) == 0);
	REQUIRE(std::strcmp(mesh.asset.id, kCrystalMesh.id) == 0);
	CHECK(weapon.package_index == mesh.package_index);
	CHECK(std::strcmp(mesh.member_chain, kCrystalMeshMember) == 0);
	CHECK(texture.package_index != weapon.package_index);
	CHECK(texture.asset.type == ASSET_TEXTURE);
	CHECK_FALSE(catalogDepContains(kCrystalMesh.id, kCrystalTexture.id));

	const size_t texture_package = texture.package_index;
	u8 digest[SHA256_DIGEST_SIZE]{};
	sha256Hash(sources.texture.data(), sources.texture.size(), digest);
	CHECK(std::memcmp(test.plan.packages[texture_package].digest, digest, sizeof(digest)) == 0);
	const std::string initial_digest = test.plan.digest;
	grid_match_offer_t offer{};
	offer.room_id = 2;
	offer.generation = 1;
	std::memcpy(offer.manifest_digest, test.plan.digest, SHA256_HEX_SIZE);
	REQUIRE(gridMatchCacheBegin(&test.cache, &offer, &test.plan, test.error, sizeof(test.error)));
	REQUIRE(accept(test, offer, weapon.package_index, sources.weapon));
	CHECK_FALSE(gridMatchCacheReady(test.cache, &offer));
	const CacheTempDir temporary;
	const std::string exact_texture = sources.texture;
	sources.texture = changedCrystalTexture(temporary.path / "changed.pdtexture");
	REQUIRE(exact_texture != sources.texture);
	CHECK_FALSE(accept(test, offer, texture_package, sources.texture));
	CHECK_FALSE(gridMatchCacheReady(test.cache, &offer));
	REQUIRE(accept(test, offer, texture_package, exact_texture));
	CHECK(gridMatchCacheReady(test.cache, &offer));
	REQUIRE(build());
	CHECK(initial_digest != test.plan.digest);
	const std::string updated_digest = test.plan.digest;

	sources.texture_type = ASSET_NONE;
	CHECK_FALSE(build());
	CHECK(std::strstr(test.error, kCrystalTexture.id) != nullptr);
	CHECK(updated_digest == test.plan.digest);
	sources.texture_type = ASSET_MODEL;
	CHECK_FALSE(build());
	CHECK(std::strstr(test.error, "wrong type") != nullptr);
	CHECK(updated_digest == test.plan.digest);

	sources.texture_type = ASSET_TEXTURE;
	REQUIRE(catalogDepRegisterTyped(kCrystalTexture.id, kCrystalMesh.id, ASSET_MODEL, 0));
	CHECK_FALSE(build());
	CHECK(std::strstr(test.error, "cycle") != nullptr);
	CHECK(updated_digest == test.plan.digest);
}

TEST_CASE("Grid closes an eligible weapon archive when only its placed model was selected",
	"[grid-match-cache][archive-owner][material-texture]")
{
	const DependencySnapshot registry;
	REQUIRE(catalogDepRegisterTyped(kCrystalWeapon.id, kCrystalMesh.id, ASSET_MODEL, 0));
	CrystalSources sources;
	CacheTest placed, explicit_weapon;
	auto build = [&](CacheTest &test, const grid_match_source_root_t &root) {
		return gridMatchSourcesBuildWithModelTextures(&test.plan, &root, 1, resolveCrystal,
			inspectCrystal, inspectCrystalMesh, &sources, test.error, sizeof(test.error));
	};

	REQUIRE(build(placed, kCrystalMesh));
	REQUIRE(placed.plan.entry_count == 2);
	CHECK(std::strcmp(placed.plan.entries[0].asset.id, kCrystalTexture.id) == 0);
	CHECK(std::strcmp(placed.plan.entries[1].asset.id, kCrystalMesh.id) == 0);
	CHECK(placed.plan.entries[0].package_index == 0);
	const std::string installed_digest = placed.plan.digest;

	REQUIRE(build(explicit_weapon, kCrystalWeapon));
	sources.complete_owner = true;
	REQUIRE(build(placed, kCrystalMesh));
	REQUIRE(placed.plan.entry_count == 3);
	REQUIRE(placed.plan.package_count == 2);
	const auto &owner = placed.plan.entries[0];
	const auto &texture = placed.plan.entries[1];
	const auto &mesh = placed.plan.entries[2];
	CHECK(std::strcmp(owner.asset.id, kCrystalWeapon.id) == 0);
	CHECK(owner.asset.type == ASSET_WEAPON);
	CHECK(owner.member_chain[0] == '\0');
	CHECK(owner.package_index == 0);
	CHECK(std::strcmp(mesh.asset.id, kCrystalMesh.id) == 0);
	CHECK(std::strcmp(mesh.member_chain, kCrystalMeshMember) == 0);
	CHECK(mesh.package_index == owner.package_index);
	CHECK(std::strcmp(texture.asset.id, kCrystalTexture.id) == 0);
	CHECK(texture.package_index == 1);
	CHECK(gridMatchSourcesFiestaCount(&placed.plan) == 0);
	CHECK(std::strcmp(placed.plan.digest, explicit_weapon.plan.digest) == 0);
	CHECK(installed_digest != placed.plan.digest);
	const std::string complete_digest = placed.plan.digest;

	sources.other_owner_bytes = sources.weapon + "changed";
	CHECK_FALSE(build(placed, kCrystalMesh));
	CHECK(std::strstr(placed.error, "archive changed") != nullptr);
	CHECK(complete_digest == placed.plan.digest);
	sources.other_owner_bytes.clear();

	sources.complete_owner = false;
	REQUIRE(build(placed, kCrystalMesh));
	CHECK(installed_digest == placed.plan.digest);
}

TEST_CASE("Grid Fiesta membership stays bound to exact sources through union and cache reseal", "[grid-match-cache][fiesta]")
{
	const DependencySnapshot registry;
	REQUIRE(catalogDepRegisterTyped(kCrystalWeapon.id, kCrystalMesh.id, ASSET_MODEL, 0));
	CrystalSources sources;
	CacheTest unmarked, marked, combined, copied;
	for (auto *set : {&unmarked, &marked}) {
		REQUIRE(gridMatchSourcesBuildWithModelTextures(&set->plan, &kCrystalWeapon, 1, resolveCrystal,
			inspectCrystal, inspectCrystalMesh, &sources, set->error, sizeof(set->error)));
	}
	REQUIRE(gridMatchSourcesSetFiestaPool(&marked.plan, &kCrystalWeapon, 1, marked.error, sizeof(marked.error)));
	CHECK(gridMatchSourcesFiestaCount(&unmarked.plan) == 0);
	REQUIRE(gridMatchSourcesFiestaCount(&marked.plan) == 1);
	CHECK(std::strcmp(marked.plan.digest, unmarked.plan.digest) != 0);
	CHECK(gridMatchSourcesExactSubset(&unmarked.plan, &marked.plan, combined.error, sizeof(combined.error)));
	CHECK_FALSE(gridMatchSourcesExactSubset(&marked.plan, &unmarked.plan, combined.error, sizeof(combined.error)));
	REQUIRE(gridMatchSourcesCombine(&combined.plan, &unmarked.plan, &marked.plan, combined.error, sizeof(combined.error)));
	REQUIRE(gridMatchSourcesFiestaCount(&combined.plan) == 1);
	CHECK(std::strcmp(combined.plan.digest, marked.plan.digest) == 0);
	CHECK(combined.plan.entry_count == marked.plan.entry_count);
	CHECK(combined.plan.package_count == marked.plan.package_count);

	grid_match_offer_t marked_offer{2, 31, {}};
	std::memcpy(marked_offer.manifest_digest, marked.plan.digest, SHA256_HEX_SIZE);
	auto unmarked_offer = marked_offer;
	++unmarked_offer.generation;
	std::memcpy(unmarked_offer.manifest_digest, unmarked.plan.digest, SHA256_HEX_SIZE);
	REQUIRE(gridMatchCacheBegin(&marked.cache, &marked_offer, &marked.plan, marked.error, sizeof(marked.error)));
	REQUIRE(gridMatchCacheBegin(&unmarked.cache, &unmarked_offer, &unmarked.plan, unmarked.error, sizeof(unmarked.error)));
	for (size_t i = 0; i < marked.plan.package_count; i++) {
		const auto &bytes = marked.plan.packages[i].owner.type == ASSET_WEAPON ? sources.weapon : sources.texture;
		REQUIRE(accept(marked, marked_offer, i, bytes));
		REQUIRE(accept(unmarked, unmarked_offer, i, bytes));
	}
	REQUIRE(gridMatchCacheReady(marked.cache, &marked_offer));
	CHECK_FALSE(gridMatchCacheReady(marked.cache, &unmarked_offer));
	REQUIRE(gridMatchCacheReady(unmarked.cache, &unmarked_offer));
	CHECK_FALSE(gridMatchCacheCompose(&combined.cache, &marked_offer, &combined.plan,
		unmarked.cache, &unmarked_offer, unmarked.cache, &unmarked_offer, combined.error, sizeof(combined.error)));
	REQUIRE(combined.cache == nullptr);
	REQUIRE(gridMatchCacheCompose(&combined.cache, &marked_offer, &combined.plan,
		unmarked.cache, &unmarked_offer, marked.cache, &marked_offer, combined.error, sizeof(combined.error)));
	REQUIRE(gridMatchCacheCopySources(combined.cache, &marked_offer, &copied.plan, copied.error, sizeof(copied.error)));
	REQUIRE(gridMatchSourcesFiestaCount(&copied.plan) == 1);
	CHECK(std::strcmp(copied.plan.digest, marked.plan.digest) == 0);
	CHECK(gridMatchSourcesExactSubset(&marked.plan, &copied.plan, copied.error, sizeof(copied.error)));
	CHECK(gridMatchSourcesExactSubset(&copied.plan, &marked.plan, copied.error, sizeof(copied.error)));
	REQUIRE(gridMatchSourcesSetFiestaPool(&copied.plan, nullptr, 0, copied.error, sizeof(copied.error)));
	CHECK(std::strcmp(copied.plan.digest, unmarked.plan.digest) == 0);
}

TEST_CASE("Grid match offer versions bind Fiesta roles without changing legacy row framing", "[grid-match-cache][fiesta][wire]")
{
	const DependencySnapshot registry;
	REQUIRE(catalogDepRegisterTyped(kCrystalWeapon.id, kCrystalMesh.id, ASSET_MODEL, 0));
	CrystalSources sources;
	CacheTest authored, decoded;
	REQUIRE(gridMatchSourcesBuildWithModelTextures(&authored.plan, &kCrystalWeapon, 1, resolveCrystal,
		inspectCrystal, inspectCrystalMesh, &sources, authored.error, sizeof(authored.error)));
	REQUIRE(authored.plan.entry_count == 3);
	REQUIRE(authored.plan.entries[0].asset.type == ASSET_WEAPON);
	REQUIRE(authored.plan.entries[1].asset.type == ASSET_TEXTURE);

	grid_match_offer_t offer{2, 31, {}};
	grid_match_offer_t received{};
	std::memcpy(offer.manifest_digest, authored.plan.digest, SHA256_HEX_SIZE);
	std::array<u8, 1024> legacy{}, marked{};
	netbuf legacy_wire{legacy.data(), static_cast<u32>(legacy.size()), 0, 0, 0};
	REQUIRE(gridMatchOfferWrite(&legacy_wire, &offer, &authored.plan, authored.error, sizeof(authored.error)));
	REQUIRE(legacy[0] == 1);
	REQUIRE(gridMatchOfferRead(&legacy_wire, &received, &decoded.plan, decoded.error, sizeof(decoded.error)));
	CHECK(legacy_wire.rp == legacy_wire.wp);
	CHECK(gridMatchSourcesFiestaCount(&decoded.plan) == 0);
	CHECK(std::strcmp(received.manifest_digest, offer.manifest_digest) == 0);

	REQUIRE(gridMatchSourcesSetFiestaPool(&authored.plan, &kCrystalWeapon, 1, authored.error, sizeof(authored.error)));
	std::memcpy(offer.manifest_digest, authored.plan.digest, SHA256_HEX_SIZE);
	netbuf marked_wire{marked.data(), static_cast<u32>(marked.size()), 0, 0, 0};
	REQUIRE(gridMatchOfferWrite(&marked_wire, &offer, &authored.plan, authored.error, sizeof(authored.error)));
	REQUIRE(marked[0] == 2);
	REQUIRE(marked_wire.wp == legacy_wire.wp);
	REQUIRE(gridMatchOfferRead(&marked_wire, &received, &decoded.plan, decoded.error, sizeof(decoded.error)));
	CHECK(marked_wire.rp == marked_wire.wp);
	CHECK(gridMatchSourcesFiestaCount(&decoded.plan) == 1);
	CHECK(decoded.plan.entries[0].fiesta_pool == 1);
	CHECK(received.room_id == offer.room_id);
	CHECK(received.generation == offer.generation);
	CHECK(std::strcmp(received.manifest_digest, offer.manifest_digest) == 0);

	const size_t digest_end = 1 + 1 + sizeof(u64) + SHA256_HEX_SIZE - 1;
	size_t weapon_row = digest_end + 2 * sizeof(u16);
	for (size_t i = 0; i < authored.plan.package_count; i++) {
		const auto &package = authored.plan.packages[i];
		weapon_row += 2 + sizeof(u32) + SHA256_DIGEST_SIZE + 2 * (sizeof(u16) + 1)
			+ std::strlen(package.owner.id) + std::strlen(package.suffix);
	}
	const auto &weapon = authored.plan.entries[0];
	const size_t texture_row = weapon_row + 1 + sizeof(u16) + 2 * (sizeof(u16) + 1)
		+ std::strlen(weapon.asset.id) + std::strlen(weapon.member_chain);
	REQUIRE(texture_row < marked_wire.wp);
	REQUIRE(legacy[weapon_row] == ASSET_WEAPON);
	REQUIRE(marked[weapon_row] == (ASSET_WEAPON | GRID_MATCH_SOURCE_FIESTA_WIRE_BIT));
	REQUIRE(marked[texture_row] == ASSET_TEXTURE);
	CHECK(std::equal(legacy.begin() + 1, legacy.begin() + 1 + 1 + sizeof(u64), marked.begin() + 1));
	CHECK(std::equal(legacy.begin() + digest_end, legacy.begin() + weapon_row, marked.begin() + digest_end));
	CHECK(std::equal(legacy.begin() + weapon_row + 1, legacy.begin() + legacy_wire.wp, marked.begin() + weapon_row + 1));

	const auto *const previous_entries = decoded.plan.entries;
	const auto rejects = [&](auto bytes, const char *reason = nullptr) {
		netbuf input{bytes.data(), marked_wire.wp, 0, marked_wire.wp, 0};
		CHECK_FALSE(gridMatchOfferRead(&input, &received, &decoded.plan, decoded.error, sizeof(decoded.error)));
		if (reason) CHECK(std::strstr(decoded.error, reason) != nullptr);
		CHECK(input.rp == 0);
		CHECK(decoded.plan.entries == previous_entries);
		CHECK(gridMatchSourcesFiestaCount(&decoded.plan) == 1);
		CHECK(received.room_id == offer.room_id);
		CHECK(received.generation == offer.generation);
		CHECK(std::strcmp(received.manifest_digest, offer.manifest_digest) == 0);
	};
	auto tampered = marked;
	tampered[0] = 1;
	rejects(tampered);
	tampered = marked;
	tampered[weapon_row] = ASSET_WEAPON;
	rejects(tampered);
	tampered[0] = 1;
	rejects(tampered, "digest");
	tampered = marked;
	tampered[texture_row] |= GRID_MATCH_SOURCE_FIESTA_WIRE_BIT;
	rejects(tampered);
	tampered = marked;
	tampered[weapon_row] |= 0x40;
	rejects(tampered);
}

TEST_CASE("Grid exact source edges compose with native model textures in one closure", "[grid-match-cache]")
{
	const DependencySnapshot registry;
	(void)catalogDepUnregister(kCrystalWeapon.id, kCrystalMesh.id);
	(void)catalogDepUnregister(kCrystalMesh.id, kCrystalTexture.id);
	CrystalSources sources;
	CrystalExtraEdges edges;
	CacheTest test;
	auto build = [&](grid_match_source_model_fn inspect_model = inspectCrystalMesh) {
		return gridMatchSourcesBuildWithModelTexturesAndEdges(&test.plan, &kCrystalWeapon, 1, resolveCrystal,
			inspectCrystal, inspect_model, &sources, appendCrystalExtraEdges, &edges,
			test.error, sizeof(test.error));
	};

	/* Supplying exact edges does not require model inspection. */
	edges.weapon_dependency = kCrystalTexture.id;
	edges.weapon_dependency_type = ASSET_TEXTURE;
	REQUIRE(build(nullptr));
	REQUIRE(test.plan.entry_count == 2);
	CHECK(std::strcmp(test.plan.entries[1].asset.id, kCrystalTexture.id) == 0);
	CHECK(sources.model_inspections == 0);

	edges = {};
	REQUIRE(build());
	REQUIRE(test.plan.entry_count == 3);
	REQUIRE(test.plan.package_count == 2);
	CHECK(sources.model_inspections == 1);
	CHECK(std::strcmp(test.plan.entries[0].asset.id, kCrystalWeapon.id) == 0);
	CHECK(std::strcmp(test.plan.entries[1].asset.id, kCrystalTexture.id) == 0);
	CHECK(std::strcmp(test.plan.entries[2].asset.id, kCrystalMesh.id) == 0);
	CHECK(test.plan.entries[0].package_index == test.plan.entries[2].package_index);
	CHECK(test.plan.entries[0].package_index != test.plan.entries[1].package_index);
	CHECK_FALSE(catalogDepContains(kCrystalWeapon.id, kCrystalMesh.id));
	CHECK_FALSE(catalogDepContains(kCrystalMesh.id, kCrystalTexture.id));

	const auto *accepted_entries = test.plan.entries;
	const auto *accepted_packages = test.plan.packages;
	const std::string accepted_digest = test.plan.digest;

	SECTION("missing declared asset") {
		edges.weapon_dependency = "mod_crystal_pistol:missing_model";
		CHECK_FALSE(build());
		CHECK(std::strstr(test.error, edges.weapon_dependency) != nullptr);
	}
	SECTION("declared asset of the wrong type") {
		edges.weapon_dependency_type = ASSET_TEXTURE;
		CHECK_FALSE(build());
		CHECK(std::strstr(test.error, "wrong type") != nullptr);
	}
	SECTION("untyped exact edge") {
		edges.weapon_dependency_type = ASSET_NONE;
		CHECK_FALSE(build());
		CHECK(std::strstr(test.error, "could not include Grid source dependencies") != nullptr);
	}
	SECTION("cycle across declared and model texture edges") {
		edges.cycle = true;
		CHECK_FALSE(build());
		CHECK(std::strstr(test.error, "cycle") != nullptr);
	}
	CHECK(test.plan.entries == accepted_entries);
	CHECK(test.plan.packages == accepted_packages);
	CHECK(accepted_digest == test.plan.digest);
}

TEST_CASE("Grid source member read rejects expansion beyond its supplied bound", "[grid-match-cache]")
{
	const CrystalSources sources;
	u32 size = 10;
	s32 absent = 10;
	void *mesh = gridMatchPackageReadMember(sources.weapon.data(), static_cast<u32>(sources.weapon.size()),
		kCrystalMeshMember, 1, &size, &absent);
	REQUIRE(mesh == nullptr);
	CHECK(size == 0);
	CHECK(absent == 0);
	CHECK(gridMatchPackageReadMember(sources.weapon.data(), static_cast<u32>(sources.weapon.size()),
		"missing.pdmesh", 1024, &size, &absent) == nullptr);
	CHECK(absent == 1);
}

TEST_CASE("Grid source plans combine in canonical order and keep exact cache membership", "[grid-match-cache]")
{
	const CacheTempDir temp;
	OwnerBytes bytes = readFixtures(temp);
	CacheTest whole, gameplay, arena, subset, combined;
	const auto expected = buildOffer(whole, bytes);
	const grid_match_source_root_t gameplay_roots[] = {kRoots[2], kRoots[1]};
	REQUIRE(gridMatchSourcesBuild(&gameplay.plan, gameplay_roots, std::size(gameplay_roots),
		resolveFixture, inspectFixture, &bytes, gameplay.error, sizeof(gameplay.error)));
	REQUIRE(gridMatchSourcesBuild(&arena.plan, kRoots, 2,
		resolveFixture, inspectFixture, &bytes, arena.error, sizeof(arena.error)));
	REQUIRE(gridMatchSourcesBuild(&subset.plan, &kRoots[2], 1,
		resolveFixture, inspectFixture, &bytes, subset.error, sizeof(subset.error)));
	std::strcpy(gameplay.plan.packages[0].path, "other/tri_arena.pdarena");
	arena.plan.packages[0].path[0] = '\0';
	subset.plan.packages[0].path[0] = '\0';
	CHECK_FALSE(gridMatchSourcesExactSubset(&gameplay.plan, &arena.plan, combined.error, sizeof(combined.error)));
	CHECK(std::strstr(combined.error, kRoots[2].id) != nullptr);

	REQUIRE(gridMatchSourcesCombine(&combined.plan, &gameplay.plan, &arena.plan, combined.error, sizeof(combined.error)));
	REQUIRE(combined.plan.entry_count == 3);
	REQUIRE(combined.plan.package_count == 2);
	CHECK(std::strcmp(combined.plan.digest, expected.manifest_digest) == 0);
	CHECK(combined.plan.total_bytes == whole.plan.total_bytes);
	for (size_t i = 0; i < combined.plan.entry_count; i++) {
		CHECK(std::strcmp(combined.plan.entries[i].asset.id, kRoots[i].id) == 0);
	}
	CHECK(combined.plan.entries[0].package_index == 0);
	CHECK(combined.plan.entries[1].package_index == 0);
	CHECK(combined.plan.entries[2].package_index == 1);
	CHECK(std::strcmp(combined.plan.packages[0].path, gameplay.plan.packages[0].path) == 0);
	CHECK(gridMatchSourcesExactSubset(&gameplay.plan, &combined.plan, combined.error, sizeof(combined.error)));
	CHECK(gridMatchSourcesExactSubset(&arena.plan, &combined.plan, combined.error, sizeof(combined.error)));
	CHECK(gridMatchSourcesExactSubset(&subset.plan, &combined.plan, combined.error, sizeof(combined.error)));

	REQUIRE(gridMatchCacheBegin(&combined.cache, &expected, &combined.plan, combined.error, sizeof(combined.error)));
	REQUIRE(accept(combined, expected, 0, bytes.original));
	REQUIRE(accept(combined, expected, 1, bytes.synthetic));
	REQUIRE(gridMatchCacheReady(combined.cache, &expected));
	REQUIRE(gridMatchCacheCopySources(combined.cache, &expected, &whole.plan, whole.error, sizeof(whole.error)));
	CHECK(gridMatchSourcesExactSubset(&whole.plan, &combined.plan, whole.error, sizeof(whole.error)));
	CHECK(gridMatchSourcesExactSubset(&combined.plan, &whole.plan, whole.error, sizeof(whole.error)));
}

TEST_CASE("A composed Grid cache keeps native bytes ready after donor and outgoing handles reset", "[grid-match-cache]")
{
	const CacheTempDir temp;
	OwnerBytes bytes = readFixtures(temp);
	CacheTest first, second, candidate, other_room, outgoing;
	const auto first_offer = fixtureOffer(first, bytes, &kRoots[2], 1, 40);
	const auto second_offer = fixtureOffer(second, bytes, kRoots, 2, 41);
	const auto candidate_offer = buildOffer(candidate, bytes);
	REQUIRE(gridMatchCacheBegin(&first.cache, &first_offer, &first.plan, first.error, sizeof(first.error)));
	REQUIRE(accept(first, first_offer, 0, bytes.synthetic));
	REQUIRE(gridMatchCacheBegin(&second.cache, &second_offer, &second.plan, second.error, sizeof(second.error)));
	const auto compose = [&](const grid_match_offer_t &offer, const grid_match_offer_t &donor_offer) {
		return gridMatchCacheCompose(&candidate.cache, &offer, &candidate.plan,
			first.cache, &first_offer, second.cache, &donor_offer, candidate.error, sizeof(candidate.error));
	};
	CHECK_FALSE(gridMatchCacheRetainReady(&outgoing.cache, second.cache,
		&second_offer, outgoing.error, sizeof(outgoing.error)));
	REQUIRE(outgoing.cache == nullptr);
	CHECK_FALSE(compose(candidate_offer, second_offer));
	REQUIRE(candidate.cache == nullptr);
	REQUIRE(accept(second, second_offer, 0, bytes.original));
	auto stale = second_offer;
	++stale.generation;
	CHECK_FALSE(compose(candidate_offer, stale));
	CHECK_FALSE(gridMatchCacheRetainReady(&outgoing.cache, second.cache, &stale, outgoing.error, sizeof(outgoing.error)));
	REQUIRE(candidate.cache == nullptr);
	REQUIRE(outgoing.cache == nullptr);
	auto wrong_digest = candidate_offer;
	wrong_digest.manifest_digest[0] = wrong_digest.manifest_digest[0] == 'a' ? 'b' : 'a';
	CHECK_FALSE(compose(wrong_digest, second_offer));
	REQUIRE(candidate.cache == nullptr);

	const auto other_offer = fixtureOffer(other_room, bytes, kRoots, 2, 41, 3);
	REQUIRE(gridMatchCacheBegin(&other_room.cache, &other_offer, &other_room.plan, other_room.error, sizeof(other_room.error)));
	REQUIRE(accept(other_room, other_offer, 0, bytes.original));
	REQUIRE(gridMatchCacheReady(other_room.cache, &other_offer));
	CHECK_FALSE(gridMatchCacheCompose(&candidate.cache, &candidate_offer, &candidate.plan,
		first.cache, &first_offer, other_room.cache, &other_offer, candidate.error, sizeof(candidate.error)));
	REQUIRE(candidate.cache == nullptr);
	gridMatchCacheReset(&other_room.cache);

	const void *original = gridMatchCacheRead(second.cache, &second_offer, 0, nullptr);
	const void *synthetic = gridMatchCacheRead(first.cache, &first_offer, 0, nullptr);
	REQUIRE(original);
	REQUIRE(synthetic);
	gridMatchSourcesFree(&first.plan);
	gridMatchSourcesFree(&second.plan);
	REQUIRE(compose(candidate_offer, second_offer));
	auto *const composed = candidate.cache;
	CHECK_FALSE(compose(candidate_offer, second_offer));
	REQUIRE(candidate.cache == composed);
	const auto check_bytes = [&](const grid_match_cache_t *cache) {
		REQUIRE(gridMatchCacheReady(cache, &candidate_offer));
		u32 size = 0;
		REQUIRE(gridMatchCacheRead(cache, &candidate_offer, 0, &size) == original);
		REQUIRE(size == bytes.original.size());
		CHECK(std::memcmp(original, bytes.original.data(), size) == 0);
		REQUIRE(gridMatchCacheRead(cache, &candidate_offer, 1, &size) == synthetic);
		REQUIRE(size == bytes.synthetic.size());
		CHECK(std::memcmp(synthetic, bytes.synthetic.data(), size) == 0);
	};
	check_bytes(candidate.cache);
	REQUIRE(gridMatchCacheRetainReady(&outgoing.cache, second.cache,
		&second_offer, outgoing.error, sizeof(outgoing.error)));
	REQUIRE(outgoing.cache == second.cache);
	gridMatchCacheReset(&first.cache);
	check_bytes(candidate.cache);
	gridMatchCacheReset(&second.cache);
	REQUIRE(gridMatchCacheReady(outgoing.cache, &second_offer));
	CHECK(gridMatchCacheRead(outgoing.cache, &second_offer, 0, nullptr) == original);
	gridMatchCacheReset(&outgoing.cache);
	CHECK(outgoing.cache == nullptr);
	check_bytes(candidate.cache);
	REQUIRE(gridMatchCacheCopySources(candidate.cache, &candidate_offer, &outgoing.plan,
		outgoing.error, sizeof(outgoing.error)));
	CHECK(gridMatchSourcesExactSubset(&outgoing.plan, &candidate.plan, outgoing.error, sizeof(outgoing.error)));
	CHECK(gridMatchSourcesExactSubset(&candidate.plan, &outgoing.plan, outgoing.error, sizeof(outgoing.error)));
	gridMatchSourcesFree(&candidate.plan);

	REQUIRE(gridMatchCacheRetainReady(&outgoing.cache, candidate.cache,
		&candidate_offer, outgoing.error, sizeof(outgoing.error)));
	REQUIRE(outgoing.cache == candidate.cache);
	CHECK_FALSE(gridMatchCacheRetainReady(&outgoing.cache, candidate.cache,
		&candidate_offer, outgoing.error, sizeof(outgoing.error)));
	REQUIRE(outgoing.cache == candidate.cache);
	gridMatchCacheReset(&candidate.cache);
	CHECK(candidate.cache == nullptr);
	check_bytes(outgoing.cache);
	gridMatchCacheReset(&outgoing.cache);
	CHECK(outgoing.cache == nullptr);
}

TEST_CASE("Grid cache composition requires exact donor admission for every typed row", "[grid-match-cache]")
{
	const CacheTempDir temp;
	OwnerBytes bytes = readFixtures(temp);
	CacheTest first, second, candidate;
	const grid_match_source_root_t packages[] = {kRoots[0], kRoots[2]};
	const auto first_offer = fixtureOffer(first, bytes, packages, std::size(packages), 50);
	const auto arena_only = fixtureOffer(second, bytes, &kRoots[0], 1, 51);
	const auto candidate_offer = buildOffer(candidate, bytes);
	REQUIRE(gridMatchCacheBegin(&first.cache, &first_offer, &first.plan, first.error, sizeof(first.error)));
	REQUIRE(accept(first, first_offer, 0, bytes.original));
	REQUIRE(accept(first, first_offer, 1, bytes.synthetic));
	REQUIRE(gridMatchCacheBegin(&second.cache, &arena_only, &second.plan, second.error, sizeof(second.error)));
	REQUIRE(accept(second, arena_only, 0, bytes.original));
	REQUIRE(gridMatchCacheReady(first.cache, &first_offer));
	REQUIRE(gridMatchCacheReady(second.cache, &arena_only));
	u32 raw_size = 0;
	const void *owner_bytes = gridMatchCacheRead(first.cache, &first_offer, 0, &raw_size);
	REQUIRE(owner_bytes);
	u32 source_size = 0;
	const std::unique_ptr<void, decltype(&std::free)> embedded(gridMatchPackageReadMember(owner_bytes,
		raw_size, kScenarioMember, 1024u * 1024u, &source_size, nullptr), std::free);
	REQUIRE(embedded);
	CHECK(source_size > 0);
	CHECK_FALSE(gridMatchCacheCompose(&candidate.cache, &candidate_offer, &candidate.plan,
		first.cache, &first_offer, second.cache, &arena_only, candidate.error, sizeof(candidate.error)));
	REQUIRE(candidate.cache == nullptr);
	gridMatchCacheReset(&second.cache);

	/* One donor may prove the scenario row in the very same archive even
	 * though the composed cache borrows its bytes from the first donor. */
	const auto scenario_offer = fixtureOffer(second, bytes, &kRoots[1], 1, 52);
	REQUIRE(gridMatchCacheBegin(&second.cache, &scenario_offer, &second.plan, second.error, sizeof(second.error)));
	REQUIRE(accept(second, scenario_offer, 0, bytes.original));
	const void *another_copy = gridMatchCacheRead(second.cache, &scenario_offer, 0, nullptr);
	REQUIRE(another_copy);
	REQUIRE(another_copy != owner_bytes);
	CacheTest conflict;
	auto conflict_offer = buildOffer(conflict, bytes);
	conflict.plan.packages[0].bundled = 1;
	REQUIRE(gridMatchSourcesSeal(&conflict.plan, conflict.error, sizeof(conflict.error)));
	std::memcpy(conflict_offer.manifest_digest, conflict.plan.digest, SHA256_HEX_SIZE);
	CHECK_FALSE(gridMatchCacheCompose(&conflict.cache, &conflict_offer, &conflict.plan,
		first.cache, &first_offer, second.cache, &scenario_offer, conflict.error, sizeof(conflict.error)));
	CHECK(conflict.cache == nullptr);
	REQUIRE(gridMatchCacheCompose(&candidate.cache, &candidate_offer, &candidate.plan,
		first.cache, &first_offer, second.cache, &scenario_offer, candidate.error, sizeof(candidate.error)));
	CHECK(gridMatchCacheRead(candidate.cache, &candidate_offer, 0, nullptr) == owner_bytes);
	gridMatchCacheReset(&first.cache);
	gridMatchCacheReset(&second.cache);
	REQUIRE(gridMatchCacheRead(candidate.cache, &candidate_offer, 0, &raw_size) == owner_bytes);
	REQUIRE(raw_size == bytes.original.size());
	CHECK(std::memcmp(owner_bytes, bytes.original.data(), raw_size) == 0);
}

TEST_CASE("Grid source operations refuse conflicting or unsealed rows transactionally", "[grid-match-cache]")
{
	const CacheTempDir temp;
	OwnerBytes bytes = readFixtures(temp);
	CacheTest result, gameplay, arena;
	const auto previous = buildOffer(result, bytes);
	const auto *entries = result.plan.entries;
	const auto *packages = result.plan.packages;
	REQUIRE(gridMatchSourcesBuild(&gameplay.plan, &kRoots[1], 1,
		resolveFixture, inspectFixture, &bytes, gameplay.error, sizeof(gameplay.error)));
	REQUIRE(gridMatchSourcesBuild(&arena.plan, &kRoots[1], 1,
		resolveFixture, inspectFixture, &bytes, arena.error, sizeof(arena.error)));
	const grid_match_sources_t *other = &arena.plan;
	SECTION("different member chain") {
		std::strcpy(arena.plan.entries[0].member_chain, "dependencies/other.pdscenario");
		REQUIRE(gridMatchSourcesSeal(&arena.plan, arena.error, sizeof(arena.error)));
	}
	SECTION("different type") {
		arena.plan.entries[0].asset.type = ASSET_MODEL;
		std::strcpy(arena.plan.entries[0].member_chain, "dependencies/model.pdmesh");
		REQUIRE(gridMatchSourcesSeal(&arena.plan, arena.error, sizeof(arena.error)));
	}
	SECTION("different archive revision") {
		arena.plan.packages[0].digest[0] ^= 1;
		REQUIRE(gridMatchSourcesSeal(&arena.plan, arena.error, sizeof(arena.error)));
	}
	SECTION("different bundled policy") {
		arena.plan.packages[0].bundled = 1;
		REQUIRE(gridMatchSourcesSeal(&arena.plan, arena.error, sizeof(arena.error)));
	}
	SECTION("different package size") {
		arena.plan.packages[0].size++;
		REQUIRE(gridMatchSourcesSeal(&arena.plan, arena.error, sizeof(arena.error)));
	}
	SECTION("stale seal") {
		arena.plan.packages[0].size++;
	}
	SECTION("invalid package index") {
		arena.plan.entries[0].package_index = 1;
	}
	SECTION("unterminated local locator") {
		auto &path = arena.plan.packages[0].path;
		std::memset(path, 'a', sizeof(path));
	}
	SECTION("missing plan") {
		other = nullptr;
	}
	CHECK_FALSE(gridMatchSourcesExactSubset(&gameplay.plan, other, result.error, sizeof(result.error)));
	CHECK_FALSE(gridMatchSourcesCombine(&result.plan, &gameplay.plan, other, result.error, sizeof(result.error)));
	CHECK(result.plan.entries == entries);
	CHECK(result.plan.packages == packages);
	CHECK(std::strcmp(result.plan.digest, previous.manifest_digest) == 0);
}

TEST_CASE("Grid source union refuses inconsistent archive owners across disjoint assets", "[grid-match-cache]")
{
	const CacheTempDir temp;
	OwnerBytes bytes = readFixtures(temp);
	CacheTest gameplay, arena, result;
	REQUIRE(gridMatchSourcesBuild(&gameplay.plan, &kRoots[1], 1,
		resolveFixture, inspectFixture, &bytes, gameplay.error, sizeof(gameplay.error)));
	SECTION("same owner with different bytes") {
		REQUIRE(gridMatchSourcesBuild(&arena.plan, &kRoots[0], 1,
			resolveFixture, inspectFixture, &bytes, arena.error, sizeof(arena.error)));
		arena.plan.packages[0].digest[0] ^= 1;
	}
	SECTION("same bytes with a different owner") {
		REQUIRE(gridMatchSourcesBuild(&arena.plan, &kRoots[2], 1,
			resolveFixture, inspectFixture, &bytes, arena.error, sizeof(arena.error)));
		std::memcpy(arena.plan.packages[0].digest, gameplay.plan.packages[0].digest, SHA256_DIGEST_SIZE);
	}
	REQUIRE(gridMatchSourcesSeal(&arena.plan, arena.error, sizeof(arena.error)));
	CHECK_FALSE(gridMatchSourcesCombine(&result.plan, &gameplay.plan, &arena.plan, result.error, sizeof(result.error)));
	CHECK(std::strstr(result.error, "archive ownership") != nullptr);
	CHECK(result.plan.entries == nullptr);
	CHECK(result.plan.packages == nullptr);
}
