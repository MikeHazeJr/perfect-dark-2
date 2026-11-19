#include "catch.hpp"

#include "grid_archive_policy.h"
#include "grid_match_package_io.h"
#include "modarchive.h"
#include "sha256.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

namespace {

namespace fs = std::filesystem;
using Bytes = std::unique_ptr<void, decltype(&std::free)>;
using ArchiveWriter = std::unique_ptr<mod_archive_writer_t, decltype(&modArchiveAbort)>;

constexpr const char *kArena = "examples/modding/typed-pdxxx-basic/arenas/tri_arena.pdarena";
constexpr const char *kScenario = "dependencies/assets/scenarios/tri_scenario.pdscenario";
constexpr const char *kWrappedMember = "Maps/Tri_Arena.pdarena";

struct TemporaryPackage {
	const fs::path directory = fs::temp_directory_path() / ("pd2-grid-package-io-" + std::to_string(
		std::chrono::high_resolution_clock::now().time_since_epoch().count()));
	const fs::path file;

	explicit TemporaryPackage(const char *name = "Example.PDMOD") : file(directory / name)
	{
		REQUIRE(fs::create_directory(directory));
	}
	~TemporaryPackage()
	{
		std::error_code error;
		fs::remove(file, error);
		fs::remove(directory, error);
	}
};

} // namespace

TEST_CASE("Grid sharing admits only canonical native owner types and suffixes", "[grid-match-package-io]")
{
	CHECK(gridMatchPackageTypeShareable(ASSET_ARENA, ".pdarena"));
	CHECK(gridMatchPackageTypeShareable(ASSET_WEAPON, ".pdweapon"));
	CHECK(gridMatchPackageTypeShareable(ASSET_MODEL, ".pdmesh"));
	CHECK(gridMatchPackageTypeShareable(ASSET_AUDIO, ".pdsfx"));
	CHECK(gridMatchPackageTypeShareable(ASSET_AUDIO, ".pdsong"));
	CHECK(gridMatchPackageTypeShareable(ASSET_TEXTURE, ".pdtexture"));
	CHECK(gridMatchPackageTypeShareable(ASSET_VEHICLE, ".pdvehicle"));
	CHECK(gridMatchPackageTypeShareable(ASSET_BODY, ".pdbody"));
	CHECK(gridMatchPackageTypeShareable(ASSET_HEAD, ".pdhead"));
	CHECK_FALSE(gridMatchPackageTypeShareable(ASSET_BODY, ".pdhead"));
	CHECK_FALSE(gridMatchPackageTypeShareable(ASSET_HEAD, ".pdbody"));
	CHECK(gridMatchPackageTypeShareable(ASSET_EFFECT, ".pdeffect"));
	CHECK(gridMatchPackageTypeShareable(ASSET_PROJECTILE, ".pdprojectile"));
	CHECK(gridMatchPackageTypeShareable(ASSET_ENTITY, ".pdentity"));
	CHECK(gridMatchPackageTypeShareable(ASSET_MATERIAL, ".pdmaterial"));
	CHECK(gridMatchPackageTypeShareable(ASSET_UI, ".pdui"));
	CHECK_FALSE(gridMatchPackageTypeShareable(ASSET_SCENARIO, ".pdscenario"));
	CHECK_FALSE(gridMatchPackageTypeShareable(ASSET_SFX, ".pdsfx"));
	CHECK_FALSE(gridMatchPackageTypeShareable(ASSET_MODEL, ".pdsfx"));
	CHECK_FALSE(gridMatchPackageTypeShareable(ASSET_NONE, nullptr));
	CHECK_FALSE(gridMatchPackageTypeShareable(ASSET_TYPE_COUNT, ".pdmesh"));

	const u8 digest[SHA256_DIGEST_SIZE] = {};
	for (const auto *suffix : { ".PDMESH", "example.pdmesh", "../.pdmesh", ".pdvoice", ".pdmod" }) {
		CAPTURE(suffix);
		CHECK_FALSE(gridMatchPackageTypeShareable(ASSET_MODEL, suffix));
		char folder[FS_MAXPATH + 1] = "unchanged folder";
		char path[FS_MAXPATH + 1] = "unchanged path";
		CHECK_FALSE(gridMatchPackagePrivatePath(digest, suffix, folder, path));
		CHECK(std::string(folder) == "unchanged folder");
		CHECK(std::string(path) == "unchanged path");
	}
}

TEST_CASE("Grid reads exact new package provenance apart from legacy distribution", "[grid-match-package-io][grid-provenance]")
{
	struct Example {
		std::string marker;
		grid_archive_origin_e expected;
	};
	const Example examples[] = {
		{ "", GRID_ARCHIVE_LEGACY },
		{ "grid_provenance = original\n", GRID_ARCHIVE_ORIGINAL },
		{ "grid_provenance = byor\n", GRID_ARCHIVE_BYOR },
		{ "grid_provenance = unverified\n", GRID_ARCHIVE_UNVERIFIED },
		{ "grid_provenance = other\n", GRID_ARCHIVE_INVALID },
		{ "grid_provenance = original\ngrid_provenance = original\n", GRID_ARCHIVE_INVALID },
		{ std::string("grid_provenance = original\n") + '\0' + "grid_provenance = byor\n", GRID_ARCHIVE_INVALID },
	};
	for (const auto &example : examples) {
		CAPTURE(example.marker);
		const TemporaryPackage temporary("proof.pdmesh");
		const std::string path = temporary.file.string();
		const std::string descriptor = "[mesh]\nid = proof:mesh\n" + example.marker;
		ArchiveWriter writer(modArchiveBegin(path.c_str()), modArchiveAbort);
		REQUIRE(writer != nullptr);
		REQUIRE(modArchiveAddFileMem(writer.get(), "mesh.ini", descriptor.data(),
			static_cast<u32>(descriptor.size())) == MODARCHIVE_OK);
		REQUIRE(modArchiveFinish(writer.release()) == MODARCHIVE_OK);

		u32 size = 0;
		const Bytes bytes(gridMatchPackageLoad(path.c_str(), &size), std::free);
		REQUIRE(bytes != nullptr);
		CHECK(gridMatchPackageOrigin("proof.pdmesh", bytes.get(), size) == example.expected);
		CHECK(gridMatchPackageOrigin("proof.pdweapon", bytes.get(), size) == GRID_ARCHIVE_INVALID);
		CHECK(gridMatchPackageOrigin("proof.pdmod", bytes.get(), size) == GRID_ARCHIVE_INVALID);
		CHECK(gridArchiveOrigin("proof.pdmesh", bytes.get(), size) == GRID_ARCHIVE_LEGACY);
	}
}

TEST_CASE("Grid locates the first typed owner of a canonical descriptor", "[grid-match-package-io]")
{
	const char loose[] = "D:\\Native Assets\\Tri.PDARENA::arena.ini";
	char root[1025] = {};
	char chain[1025] = {};
	const char *leaf = nullptr;
	REQUIRE(gridMatchPackageLocation(loose, root, sizeof(root), chain, sizeof(chain), &leaf));
	CHECK(std::strcmp(root, "D:\\Native Assets\\Tri.PDARENA") == 0);
	CHECK(chain[0] == '\0');
	CHECK(leaf == std::strrchr(loose, ':') + 1);
	CHECK(std::strcmp(leaf, "arena.ini") == 0);

	const char wrapped[] = "D:\\Mods\\Typed.PDMOD::Maps/Tri.PDARENA::"
		"dependencies/assets/scenarios/Tri.pdscenario::scenario.ini";
	REQUIRE(gridMatchPackageLocation(wrapped, root, sizeof(root), chain, sizeof(chain), &leaf));
	CHECK(std::strcmp(root, "D:\\Mods\\Typed.PDMOD::Maps/Tri.PDARENA") == 0);
	CHECK(std::strcmp(chain, "dependencies/assets/scenarios/Tri.pdscenario") == 0);
	CHECK(leaf == std::strrchr(wrapped, ':') + 1);
	CHECK(std::strcmp(leaf, "scenario.ini") == 0);

	const char twice_nested[] = "D:/Mods/Typed.pdmod::Maps/Tri.pdarena::deps/scene.pdscenario::"
		"deps/intro.pdai::ai.ini";
	REQUIRE(gridMatchPackageLocation(twice_nested, root, sizeof(root), chain, sizeof(chain), nullptr));
	CHECK(std::strcmp(root, "D:/Mods/Typed.pdmod::Maps/Tri.pdarena") == 0);
	CHECK(std::strcmp(chain, "deps/scene.pdscenario::deps/intro.pdai") == 0);
}

TEST_CASE("Grid finds the immediate native owner of an embedded effect texture",
	"[grid-match-package-io][effect-texture]")
{
	constexpr const char *texture_member = "dependencies/assets/textures/tri_texture.pdtexture";
	const auto source_path = (fs::path(PD_SOURCE_DIR) /
		"examples/modding/typed-pdxxx-basic/textures/tri_texture.pdtexture").string();
	u32 size = 0;
	const Bytes texture(gridMatchPackageLoad(source_path.c_str(), &size), std::free);
	REQUIRE(texture);
	REQUIRE(size > 0);

	const TemporaryPackage temporary("effect.pdeffect");
	const auto path = temporary.file.string();
	ArchiveWriter writer(modArchiveBegin(path.c_str()), modArchiveAbort);
	REQUIRE(writer);
	REQUIRE(modArchiveAddFileMem(writer.get(), texture_member, texture.get(), size) == MODARCHIVE_OK);
	REQUIRE(modArchiveFinish(writer.release()) == MODARCHIVE_OK);

	char root[FS_MAXPATH + 1] = {}, chain[FS_MAXPATH + 1] = {};
	const std::string locator = path + "::" + texture_member + "::texture.ini";
	REQUIRE(gridMatchPackageLocation(locator.c_str(), root, sizeof(root), chain, sizeof(chain), nullptr));
	asset_type_e owner = ASSET_NONE;
	const char *member = nullptr;
	REQUIRE(gridMatchPackageMemberOwner(ASSET_EFFECT, chain, &owner, &member));
	CHECK(owner == ASSET_EFFECT);
	CHECK(member == chain);
	CHECK(std::strcmp(member, texture_member) == 0);
	u32 parent_size = 0, nested_size = 0;
	const Bytes parent(gridMatchPackageLoad(root, &parent_size), std::free);
	REQUIRE(parent);
	const Bytes nested(gridMatchPackageReadMember(parent.get(), parent_size, member, size, &nested_size, nullptr),
		std::free);
	REQUIRE(nested);
	REQUIRE(nested_size == size);
	CHECK(std::memcmp(nested.get(), texture.get(), size) == 0);

	const std::string deep = std::string("projectile.pdprojectile::impact.pdeffect::") + texture_member;
	REQUIRE(gridMatchPackageMemberOwner(ASSET_WEAPON, deep.c_str(), &owner, &member));
	CHECK(owner == ASSET_EFFECT);
	CHECK(std::strcmp(member, texture_member) == 0);
	const std::string outside_effect = std::string("impact.pdeffect::material.pdmaterial::") + texture_member;
	REQUIRE(gridMatchPackageMemberOwner(ASSET_WEAPON, outside_effect.c_str(), &owner, &member));
	CHECK(owner == ASSET_MATERIAL);
	CHECK(std::strcmp(member, texture_member) == 0);

	constexpr const char *invalid[] = {
		"", "impact.pdeffect::", "impact.pdeffect::::spark.pdtexture",
		"impact.pdeffect::../spark.pdtexture", "deps/../impact.pdeffect::spark.pdtexture",
		"impact.pdeffect::deps//spark.pdtexture", "impact.pdeffect::C:/spark.pdtexture",
		"deps\\impact.pdeffect::spark.pdtexture", "impact.zip::spark.pdtexture",
		"impact.pdeffect::spark.png",
		"a.pdeffect::b.pdeffect::c.pdeffect::d.pdeffect::e.pdeffect::f.pdeffect::spark.pdtexture",
	};
	for (const auto *bad : invalid) {
		CAPTURE(bad);
		const char *unchanged = member;
		CHECK_FALSE(gridMatchPackageMemberOwner(ASSET_WEAPON, bad, &owner, &member));
		CHECK(owner == ASSET_MATERIAL);
		CHECK(member == unchanged);
	}
}

TEST_CASE("Grid refuses malformed package locators without publishing a prefix", "[grid-match-package-io]")
{
	constexpr const char *cases[] = {
		"D:/Native/../other.pdarena::arena.ini",
		"D:/Native/Tri.pdarena::../Tri.pdscenario::scenario.ini",
		"D:/Native/Tri.pdarena::deps/../Tri.pdscenario::scenario.ini",
		"D:/Native/Tri.pdarena::deps/./Tri.pdscenario::scenario.ini",
		"D:/Native/Tri.pdarena::deps\\Tri.pdscenario::scenario.ini",
		"D:/Native/Tri.pdarena::deps//Tri.pdscenario::scenario.ini",
		"D:/Native/Tri.pdarena::D:/Tri.pdscenario::scenario.ini",
		"D:/Native/Tri.pdarena::/Tri.pdscenario::scenario.ini",
		"D:/Native/Tri.pdarena::",
		"D:/Native/Tri.pdarena::::arena.ini",
		"D:/Native/Tri.pdarena::arena.ini::",
		"D:/Native/Tri.pdarena::scenario.ini",
		"D:/Mods/Outer.pdmod::Inner.pdmod::Maps/Tri.pdarena::arena.ini",
		"D:/Mods/Outer.zip::Maps/Tri.pdarena::arena.ini",
		"D:/Mods/Outer.pdmod::Maps/Tri.pdarena::deps/archive.zip::scenario.ini",
		"D:/Mods/Outer.pdmod::Maps\\Tri.pdarena::arena.ini",
		"D:/Mods/Outer.pdmod::../Maps/Tri.pdarena::arena.ini",
		"D:/Mods/Outer.pdmod::Maps/Tri.pdarena",
	};
	char root[1025] = "previous root";
	char chain[1025] = "previous chain";
	const char old_leaf[] = "previous leaf";
	const char *leaf = old_leaf;
	for (const char *descriptor : cases) {
		CAPTURE(descriptor);
		CHECK_FALSE(gridMatchPackageLocation(descriptor, root, sizeof(root), chain, sizeof(chain), &leaf));
		CHECK(std::strcmp(root, "previous root") == 0);
		CHECK(std::strcmp(chain, "previous chain") == 0);
		CHECK(leaf == old_leaf);
	}

	const char valid[] = "D:/Mods/Outer.pdmod::Maps/Tri.pdarena::deps/Tri.pdscenario::scenario.ini";
	CHECK_FALSE(gridMatchPackageLocation(valid, root, 4, chain, sizeof(chain), &leaf));
	CHECK_FALSE(gridMatchPackageLocation(valid, root, sizeof(root), chain, 4, &leaf));
	CHECK(std::strcmp(root, "previous root") == 0);
	CHECK(std::strcmp(chain, "previous chain") == 0);
	CHECK(leaf == old_leaf);
}

TEST_CASE("Grid reads identical native arena bytes from loose and packed owners", "[grid-match-package-io]")
{
	const std::string loose_path = (fs::path(PD_SOURCE_DIR) / kArena).string();
	REQUIRE(fs::path(loose_path).is_absolute());
	u8 file_digest[SHA256_DIGEST_SIZE] = {};
	REQUIRE(sha256HashFile(loose_path.c_str(), file_digest) == 0);
	u32 loose_size = 0;
	const Bytes loose(gridMatchPackageLoad(loose_path.c_str(), &loose_size), std::free);
	REQUIRE(loose != nullptr);
	REQUIRE(loose_size == fs::file_size(loose_path));
	u8 loose_digest[SHA256_DIGEST_SIZE] = {};
	sha256Hash(loose.get(), loose_size, loose_digest);
	CHECK(std::memcmp(file_digest, loose_digest, SHA256_DIGEST_SIZE) == 0);

	const TemporaryPackage temporary;
	const std::string wrapper = temporary.file.string();
	ArchiveWriter writer(modArchiveBegin(wrapper.c_str()), modArchiveAbort);
	REQUIRE(writer != nullptr);
	REQUIRE(modArchiveAddFileMem(writer.get(), kWrappedMember, loose.get(), loose_size) == MODARCHIVE_OK);
	REQUIRE(modArchiveFinish(writer.release()) == MODARCHIVE_OK);

	const std::string package = wrapper + "::" + kWrappedMember;
	const std::string descriptor = package + "::" + kScenario + "::scenario.ini";
	char root[1025] = {};
	char chain[1025] = {};
	const char *leaf = nullptr;
	REQUIRE(gridMatchPackageLocation(descriptor.c_str(), root, sizeof(root), chain, sizeof(chain), &leaf));
	CHECK(std::string(root) == package);
	CHECK(std::strcmp(chain, kScenario) == 0);
	CHECK(std::strcmp(leaf, "scenario.ini") == 0);

	u32 wrapped_size = 0;
	const Bytes wrapped(gridMatchPackageLoad(root, &wrapped_size), std::free);
	REQUIRE(wrapped != nullptr);
	REQUIRE(wrapped_size == loose_size);
	u8 wrapped_digest[SHA256_DIGEST_SIZE] = {};
	sha256Hash(wrapped.get(), wrapped_size, wrapped_digest);
	CHECK(std::memcmp(file_digest, wrapped_digest, SHA256_DIGEST_SIZE) == 0);
	CHECK(std::memcmp(loose.get(), wrapped.get(), loose_size) == 0);

	const std::string missing_paths[] = {
		wrapper + "::Maps/Missing.pdarena",
		(temporary.directory / "missing.pdarena").string(),
		wrapper,
		descriptor,
	};
	for (const auto &missing : missing_paths) {
		CAPTURE(missing);
		u32 size = 73;
		const Bytes failed(gridMatchPackageLoad(missing.c_str(), &size), std::free);
		CHECK(failed == nullptr);
		CHECK(size == 0);
	}
}
