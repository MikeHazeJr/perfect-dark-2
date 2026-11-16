/**
 * test_maian_hover_vehicle.cpp -- the Maian hover vehicle recipe.
 *
 * dev-mods/maian_hover_vehicle/ is the source of the custom vehicle mod: a
 * recipe for the in-game mod author plus the model files it puts in
 * (build_vehicle_mesh.py, from the cleaned-up Tripo model; the October-study
 * stand-in it replaced is make_standin_mesh.py). This case builds it the way
 * `--mod-author` does (recipe file, modAuthorSave) and checks what the game
 * will read from the saved archive: a vehicle in the mod's namespace whose
 * model is an embedded typed OBJ mesh with the strict descriptor the vehicle
 * model binder reads (`[model]`, kind = mesh), a culling radius, the
 * hoverbike's bounding-box node, and the parts later steps drive (turret yaw
 * and pitch, the 18 ring seats and the muzzle seat, the crystal toggles).
 * Driving it is proven in game by
 * tools/smoke-verify/tests/maian_vehicle_drive_smoke.json.
 *
 * When PD2_TEST_ARTIFACT_DIR is set, the built archive is copied to
 * <dir>/clones for tools/asset_archive_conformance.py.
 */
#include "catch.hpp"

#include "asset_archive_policy.h"
#include "assetcatalog.h"
#include "body_head_source.h"
#include "mod_asset_validate.h"
#include "mod_author.h"
#include "mod_author_recipe.h"
#include "modarchive.h"

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

struct HoverTempDir {
	fs::path path;

	HoverTempDir() {
		const auto stamp =
			std::chrono::high_resolution_clock::now().time_since_epoch().count();
		path = fs::temp_directory_path() / ("pd2-maian-hover-" + std::to_string(stamp));
		fs::create_directories(path);
	}

	~HoverTempDir() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

std::string hoverReadFile(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool hoverHasEntry(const std::string &archive, const char *entry)
{
	u32 size = 0;
	void *data = modArchiveExtractMemAlloc(archive.data(),
		static_cast<u32>(archive.size()), entry, &size);
	const bool present = data != nullptr;
	free(data);
	return present;
}

std::string hoverEntry(const std::string &archive, const char *entry)
{
	u32 size = 0;
	void *data = modArchiveExtractMemAlloc(archive.data(),
		static_cast<u32>(archive.size()), entry, &size);
	REQUIRE(data != nullptr);
	std::string out(static_cast<const char *>(data), size);
	free(data);
	return out;
}

/* A key of a section read by bodyHeadSourceReadIniBytes (iniGet lives in the
 * scanner, which this target does not link). */
std::string hoverIniValue(const ini_section_t &ini, const char *key)
{
	for (s32 i = 0; i < ini.count; i++) {
		if (std::strcmp(ini.pairs[i].key, key) == 0) return ini.pairs[i].value;
	}
	return std::string();
}

struct HoverRecipeDeleter {
	void operator()(mod_author_recipe_t *recipe) const { modAuthorRecipeFree(recipe); }
};

} // namespace

TEST_CASE("the Maian hover vehicle recipe builds a vehicle with its own typed model",
          "[modding][author][recipe][maian-vehicle]") {
	const fs::path recipePath =
		fs::path(PD_SOURCE_DIR) / "dev-mods" / "maian_hover_vehicle" / "recipe.json";
	HoverTempDir mods;
	const std::string modsRoot = mods.path.string();

	char err[512] = "";
	std::unique_ptr<mod_author_recipe_t, HoverRecipeDeleter> recipe(
		modAuthorRecipeLoadFile(recipePath.string().c_str(), err, sizeof(err)));
	INFO("recipe: " << err);
	REQUIRE(recipe);

	mod_author_request_t request;
	modAuthorRecipeToRequest(recipe.get(), nullptr, modsRoot.c_str(), &request);
	auto result = std::make_unique<mod_author_result_t>();
	const s32 saved = modAuthorSave(&request, result.get(), err, sizeof(err));
	INFO("save: " << err);
	REQUIRE(saved == MOD_AUTHOR_OK);
	CHECK(std::string(result->catalog_id) == "mod:vehicle_maian_hover");
	CHECK(std::string(result->mod_id) == "mod.vehicle_maian_hover");
	CHECK(fs::path(result->archive_path).filename().string() == "vehicle_maian_hover.pdvehicle");

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

	const std::string archive = hoverReadFile(result->archive_path);
	const std::string ini = hoverEntry(archive, "vehicle.ini");
	CHECK(ini.find("catalog_id = mod:vehicle_maian_hover") != std::string::npos);
	CHECK(ini.find("name = Maian Hover") != std::string::npos);
	CHECK(ini.find("model_file = dependencies/assets/models/maian_hover.pdmesh") != std::string::npos);
	/* The example's loose glTF model is gone: the vehicle binder only places
	 * vehicles whose model is an embedded typed mesh. */
	CHECK_FALSE(hoverHasEntry(archive, "model.gltf"));

	const std::string physics = hoverEntry(archive, "physics.json");
	CHECK(physics.find("\"catalog_id\": \"mod:vehicle_maian_hover\"") != std::string::npos);
	CHECK(physics.find("\"archetype\": \"hoverbike\"") != std::string::npos);

	/* The embedded model, in the mod's namespace. */
	std::string meshId;
	for (u32 i = 0; i < result->ids.id_count; i++) {
		if (std::string(result->ids.ids[i].old_id) == "example:tri_mesh") {
			meshId = result->ids.ids[i].new_id;
		}
	}
	REQUIRE_FALSE(meshId.empty());
	CHECK(meshId.compare(0, 23, "mod:vehicle_maian_hover") == 0);

	const std::string mesh = hoverEntry(archive, "dependencies/assets/models/maian_hover.pdmesh");
	REQUIRE(assetArchiveTypeForPath("dependencies/assets/models/maian_hover.pdmesh") == ASSET_MODEL);
	const std::string meshIni = hoverEntry(mesh, "mesh.ini");

	/* Read the way the vehicle model binder reads it (strict, [model]). */
	auto section = std::make_unique<ini_section_t>();
	char iniErr[256] = "";
	const s32 read = bodyHeadSourceReadIniBytes(meshIni.data(), static_cast<u32>(meshIni.size()),
		"model", section.get(), iniErr, sizeof(iniErr));
	INFO("mesh.ini: " << iniErr);
	REQUIRE(read == 1);
	CHECK(hoverIniValue(*section, "catalog_id") == meshId);
	CHECK(hoverIniValue(*section, "kind") == "mesh");
	CHECK(hoverIniValue(*section, "model_file") == "model.obj");
	const double radius = std::atof(hoverIniValue(*section, "model_scale").c_str());
	CHECK(radius > 1000.0);   /* a vehicle-sized culling radius, not the 1.0 default */
	CHECK(radius < 5000.0);

	/* The hoverbike's node layout: position root, bounding box, display list. */
	const std::string nodes = hoverEntry(mesh, "model.nodes.json");
	CHECK(nodes.find("\"type\": 2,") != std::string::npos);
	CHECK(nodes.find("\"type\": 10,") != std::string::npos);
	CHECK(nodes.find("\"type\": 24,") != std::string::npos);
	CHECK(nodes.find("\"type\": 18,") != std::string::npos);   /* the crystal toggles */
	CHECK(hoverHasEntry(mesh, "model.obj"));
	CHECK_FALSE(hoverHasEntry(mesh, "model.gltf"));             /* the template's glTF is gone */
	CHECK(hoverHasEntry(mesh, "model.faces.json"));
	CHECK(hoverHasEntry(mesh, "model.render.json"));

	/* The mesh's manifest names the OBJ, under the mod's id. */
	const std::string manifest = hoverEntry(mesh, "_meta/manifest.json");
	CHECK(manifest.find("\"model_file\": \"model.obj\"") != std::string::npos);
	CHECK(manifest.find("\"geometry\": \"model.obj\"") != std::string::npos);
	CHECK(manifest.find("\"catalog_id\": \"" + meshId + "\"") != std::string::npos);

	/* The parts later steps drive: turret yaw and pitch, the muzzle, the 18 ring
	 * seats (110-127) and the muzzle crystal's seat (128), the crystal toggles. */
	const std::string parts = hoverEntry(mesh, "model.parts.json");
	for (int part : {0, 1, 2, 50, 110, 127, 128, 140, 141}) {
		INFO("part " << part);
		CHECK(parts.find("\"partnum\": " + std::to_string(part) + ",") != std::string::npos);
	}
	CHECK(parts.find("\"partnum\": 129,") == std::string::npos);
}
