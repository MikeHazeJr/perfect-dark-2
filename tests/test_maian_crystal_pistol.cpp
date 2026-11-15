/**
 * test_maian_crystal_pistol.cpp -- the Maian crystal pistol recipe.
 *
 * dev-mods/maian_crystal_pistol/ is the source of the custom weapon mod: a
 * recipe for the in-game mod author plus the member files it puts in. This
 * case builds it the way `--mod-author` does (recipe file, modAuthorSave)
 * and checks what the game will compile from the saved archive, so the
 * recipe cannot rot unnoticed.
 *
 * When PD2_TEST_ARTIFACT_DIR is set, the built archive is copied to
 * <dir>/clones for tools/asset_archive_conformance.py.
 */
#include "catch.hpp"
extern "C" {
#include "asset_archive_clone.h"
#include "asset_archive_policy.h"
#include "assetcatalog.h"
#include "mod_asset_validate.h"
#include "mod_author.h"
#include "mod_author_recipe.h"
#include "modarchive.h"
#include "weapon_graph_runtime.h"
}
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>

namespace {

namespace fs = std::filesystem;

struct PistolTempDir {
	fs::path path;

	PistolTempDir() {
		const auto stamp =
			std::chrono::high_resolution_clock::now().time_since_epoch().count();
		path = fs::temp_directory_path() / ("pd2-crystal-pistol-" + std::to_string(stamp));
		fs::create_directories(path);
	}

	~PistolTempDir() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

std::string pistolReadFile(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string pistolEntry(const std::string &archive, const char *entry)
{
	u32 size = 0;
	void *data = modArchiveExtractMemAlloc(archive.data(),
		static_cast<u32>(archive.size()), entry, &size);
	REQUIRE(data != nullptr);
	std::string out(static_cast<const char *>(data), size);
	free(data);
	return out;
}

struct PistolGraph {
	std::unique_ptr<weapon_graph_ir_t> ir = std::make_unique<weapon_graph_ir_t>();
	char err[256] = "";

	~PistolGraph() { weaponGraphIrFree(ir.get()); }

	const weapon_graph_ir_node_t *node(const char *id) const {
		for (s32 i = 0; i < ir->node_count; i++) {
			if (std::strcmp(ir->nodes[i].id, id) == 0) return &ir->nodes[i];
		}
		return nullptr;
	}

	const weapon_graph_ir_param_t *param(const char *id, const char *key) const {
		const weapon_graph_ir_node_t *n = node(id);
		if (!n) return nullptr;
		for (s32 i = 0; i < n->param_count; i++) {
			const weapon_graph_ir_param_t &p = ir->params[n->param_start + i];
			if (std::strcmp(p.key, key) == 0) return &p;
		}
		return nullptr;
	}

	std::string text(const char *id, const char *key) const {
		const weapon_graph_ir_param_t *p = param(id, key);
		return p ? std::string(p->value) : std::string("<missing>");
	}

	std::string exported(const char *name) const {
		for (s32 i = 0; i < ir->export_count; i++) {
			if (std::strcmp(ir->exports[i].name, name) == 0) {
				return ir->nodes[ir->exports[i].node].id;
			}
		}
		return std::string();
	}
};

struct PistolRecipeDeleter {
	void operator()(mod_author_recipe_t *recipe) const { modAuthorRecipeFree(recipe); }
};

} // namespace

TEST_CASE("the Maian crystal pistol recipe builds a weapon mod the game can compile",
          "[modding][author][recipe][crystal-pistol]") {
	const fs::path recipePath =
		fs::path(PD_SOURCE_DIR) / "dev-mods" / "maian_crystal_pistol" / "recipe.json";
	PistolTempDir mods;
	const std::string modsRoot = mods.path.string();

	char err[512] = "";
	std::unique_ptr<mod_author_recipe_t, PistolRecipeDeleter> recipe(
		modAuthorRecipeLoadFile(recipePath.string().c_str(), err, sizeof(err)));
	INFO("recipe: " << err);
	REQUIRE(recipe);

	mod_author_request_t request;
	modAuthorRecipeToRequest(recipe.get(), nullptr, modsRoot.c_str(), &request);
	auto result = std::make_unique<mod_author_result_t>();
	const s32 saved = modAuthorSave(&request, result.get(), err, sizeof(err));
	INFO("save: " << err);
	REQUIRE(saved == MOD_AUTHOR_OK);
	CHECK(std::string(result->catalog_id) == "mod:weapon_maian_crystal_pistol");
	CHECK(std::string(result->mod_id) == "mod.weapon_maian_crystal_pistol");

	if (const char *keep = std::getenv("PD2_TEST_ARTIFACT_DIR")) {
		if (keep[0]) {
			std::error_code ec;
			fs::create_directories(fs::path(keep) / "clones", ec);
			fs::copy_file(result->archive_path,
				fs::path(keep) / "clones" / fs::path(result->archive_path).filename(),
				fs::copy_options::overwrite_existing, ec);
		}
	}

	char reason[320] = "";
	const s32 verdict = modAssetValidateArchiveFile(result->archive_path, reason, sizeof(reason));
	INFO("validate: " << reason);
	REQUIRE(verdict == MOD_ASSET_VALIDATE_OK);

	const std::string archive = pistolReadFile(result->archive_path);
	const std::string ini = pistolEntry(archive, "weapon.ini");
	CHECK(ini.find("catalog_id = mod:weapon_maian_crystal_pistol") != std::string::npos);
	CHECK(ini.find("name = Maian Crystal Pistol") != std::string::npos);
	CHECK(ini.find("dual_wieldable = 0") != std::string::npos);
	CHECK(ini.find("muzzlez = 3.0") != std::string::npos);
	CHECK(ini.find("posx = 30.0") != std::string::npos);
	CHECK(ini.find("posy = -18.0") != std::string::npos);
	CHECK(ini.find("posz = -45.0") != std::string::npos);

	/* Ids: everything the weapon carries lives in the mod's own namespace. */
	std::string projectileId;
	std::string entityId;
	std::string crystalMeshId;
	for (u32 i = 0; i < result->ids.id_count; i++) {
		const std::string oldId = result->ids.ids[i].old_id;
		if (oldId == "example:tri_projectile") projectileId = result->ids.ids[i].new_id;
		if (oldId == "example:tri_entity") entityId = result->ids.ids[i].new_id;
		if (oldId == "mod_needler:needle") crystalMeshId = result->ids.ids[i].new_id;
	}
	REQUIRE_FALSE(projectileId.empty());
	REQUIRE_FALSE(entityId.empty());
	REQUIRE_FALSE(crystalMeshId.empty());
	CHECK(projectileId.compare(0, 31, "mod:weapon_maian_crystal_pistol") == 0);
	CHECK(crystalMeshId.compare(0, 31, "mod:weapon_maian_crystal_pistol") == 0);

	/* The weapon, as the game compiles it from the archive (with the
	 * variables file, so $crystal_damage has become a number). */
	PistolGraph weapon;
	const s32 compiled = weaponGraphCompileArchiveFile(result->archive_path, ASSET_WEAPON,
		weapon.ir.get(), weapon.err, sizeof(weapon.err));
	INFO("weapon graph: " << weapon.err);
	REQUIRE(compiled == 0);
	CHECK(std::string(weapon.ir->asset_id) == "mod:weapon_maian_crystal_pistol");

	CHECK(weapon.exported("primary") == "fire_crystal");
	REQUIRE(weapon.node("fire_crystal") != nullptr);
	CHECK(std::string(weapon.node("fire_crystal")->kind) == "spawn.fired_projectile");
	CHECK(weapon.text("fire_crystal", "function_type") == "shoot_projectile");
	CHECK(weapon.text("fire_crystal", "projectile_ref") == projectileId);
	const weapon_graph_ir_param_t *ammo = weapon.param("fire_crystal", "ammo_slot");
	REQUIRE(ammo != nullptr);
	CHECK(ammo->i_value == -1);
	const weapon_graph_ir_param_t *damage = weapon.param("fire_crystal", "damage");
	REQUIRE(damage != nullptr);
	CHECK(damage->type == WEAPON_GRAPH_PARAM_FLOAT);
	CHECK(damage->f_value == Approx(4.0f));

	CHECK(weapon.exported("secondary") == "launch_mine");
	REQUIRE(weapon.node("launch_mine") != nullptr);
	CHECK(std::string(weapon.node("launch_mine")->kind) == "spawn.thrown_physical");
	CHECK(weapon.text("launch_mine", "function_type") == "throw");
	CHECK(weapon.text("launch_mine", "entity_ref") == entityId);

	/* The crystal: flies, sticks where it lands, and carries its own mesh. */
	const std::string projectile =
		pistolEntry(archive, "dependencies/assets/projectiles/primary.pdprojectile");
	const std::string crystalGraph = pistolEntry(projectile, "behavior.graph.json");
	PistolGraph crystal;
	REQUIRE(weaponGraphCompileJson(ASSET_PROJECTILE, crystalGraph.data(),
		static_cast<u32>(crystalGraph.size()), crystal.ir.get(), crystal.err,
		sizeof(crystal.err)) == 0);
	CHECK(std::string(crystal.ir->asset_id) == projectileId);
	CHECK(crystal.text("spawn", "model_ref") == crystalMeshId);
	REQUIRE(crystal.node("stick") != nullptr);
	CHECK(std::string(crystal.node("stick")->kind) == "projectile.sticky_attach");
	CHECK(crystal.text("lifetime", "timer_starts") == "on_attach");
	CHECK(crystal.text("lifetime", "on_expire") == "delete");
	const std::string crystalMesh =
		pistolEntry(projectile, "dependencies/assets/models/crystal.pdmesh");
	CHECK(pistolEntry(crystalMesh, "mesh.ini").find("catalog_id = " + crystalMeshId)
		!= std::string::npos);

	/* The mine: a proximity trigger on the deployed entity. */
	const std::string entity =
		pistolEntry(archive, "dependencies/assets/entities/deployed.pdentity");
	const std::string mineGraph = pistolEntry(entity, "behavior.graph.json");
	PistolGraph mine;
	REQUIRE(weaponGraphCompileJson(ASSET_ENTITY, mineGraph.data(),
		static_cast<u32>(mineGraph.size()), mine.ir.get(), mine.err, sizeof(mine.err)) == 0);
	CHECK(std::string(mine.ir->asset_id) == entityId);
	REQUIRE(mine.node("proxy") != nullptr);
	CHECK(std::string(mine.node("proxy")->kind) == "entity.proxy_trigger");
	CHECK(mine.text("proxy", "on_trigger") == "detonate");
	CHECK(mine.text("proxy", "owner_filter") == "exclude_owner");

	/* No id of the template or of the mesh's first home is left behind. */
	for (const std::string &text : { pistolEntry(archive, "behavior/primary.graph.json"),
			pistolEntry(archive, "behavior/secondary.graph.json"), crystalGraph, mineGraph }) {
		CHECK(text.find("example:") == std::string::npos);
		CHECK(text.find("mod_needler:") == std::string::npos);
	}
}
