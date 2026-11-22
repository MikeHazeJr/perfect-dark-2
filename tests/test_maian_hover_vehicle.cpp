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
#include "vehicle_graph_runner.h"

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

	/* Physics v3: the v2 keys without the boost keys (a v1 graph holds them). */
	const std::string physics = hoverEntry(archive, "physics.json");
	CHECK(physics.find("\"schema\": \"pd2.vehicle.physics.v3\"") != std::string::npos);
	CHECK(physics.find("\"catalog_id\": \"mod:vehicle_maian_hover\"") != std::string::npos);
	CHECK(physics.find("\"archetype\": \"hoverbike\"") != std::string::npos);
	CHECK(physics.find("boost_speed") == std::string::npos);
	CHECK(physics.find("boost_time_ticks60") == std::string::npos);

	/* Boost and heat (V3) come from its pd.vehicle_graph.v1 graph and the
	 * values file; the mod author gave both the mod's catalog id. */
	const std::string variables = hoverEntry(archive, "behavior/variables.json");
	CHECK(variables.find("\"schema\": \"pd.vehicle_variables.v1\"") != std::string::npos);
	CHECK(variables.find("\"overheat_after\": { \"value\": 240, \"unit\": \"ticks60\" }") != std::string::npos);
	CHECK(variables.find("\"cool_crystal_part\": 140") != std::string::npos);
	const std::string graph = hoverEntry(archive, "behavior.graph.json");
	CHECK(graph.find("\"schema\": \"pd.vehicle_graph.v1\"") != std::string::npos);
	CHECK(graph.find("\"catalog_id\": \"mod:vehicle_maian_hover\"") != std::string::npos);
	CHECK(graph.find("example:tri_vehicle") == std::string::npos);
	char graphErr[256] = { 0 };
	vehicle_graph_program_t *program = vehicleGraphProgramCompile(graph.data(), graph.size(),
		variables.data(), variables.size(), "mod:vehicle_maian_hover", nullptr, nullptr,
		graphErr, sizeof(graphErr));
	INFO(graphErr);
	REQUIRE(program != nullptr);
	CHECK(vehicleGraphProgramPartCount(program) == 1);
	CHECK(vehicleGraphProgramPart(program, 0) == 140);
	const vehicle_boost_heat_params_t *heat = vehicleGraphProgramBoostHeatParams(program);
	REQUIRE(heat != nullptr);
	CHECK(heat->boost_speed == 7.0f);
	CHECK(heat->overheat_ticks240 == 960.0f);
	CHECK(vehicleGraphProgramEventReachesModule(program, VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED,
		"og.vehicle.mount_seat") == 1);
	/* Two seats (V4): a mount, a dismount and a switch each reach both the
	 * driver's and the gunner's seat module, which is how the game knows the
	 * vehicle has a gunner seat and that a rider may switch. */
	const u32 bothSeats = (1u << VEHICLE_GRAPH_SEAT_DRIVER) | (1u << VEHICLE_GRAPH_SEAT_GUNNER);
	CHECK(vehicleGraphProgramEventSeats(program, VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED) == bothSeats);
	CHECK(vehicleGraphProgramEventSeats(program, VEHICLE_GRAPH_EVENT_DISMOUNT_REQUESTED) == bothSeats);
	CHECK(vehicleGraphProgramEventSeats(program, VEHICLE_GRAPH_EVENT_SWITCH_SEAT_REQUESTED) == bothSeats);
	CHECK(vehicleGraphProgramEventSeats(program, VEHICLE_GRAPH_EVENT_TICK) == 0u);
	CHECK(vehicleGraphProgramNodeCount(program) == 26);
	CHECK(vehicleGraphProgramExecEdgeCount(program) == 21);
	vehicleGraphProgramFree(program);

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
	 * seats (110-127) and the muzzle crystal's seat (128), the crystal toggles,
	 * and the two riders' eye points (160 the driver's, 161 the gunner's; the
	 * game reads them through hoverbikeSeatModelPos). */
	const std::string parts = hoverEntry(mesh, "model.parts.json");
	for (int part : {0, 1, 2, 50, 110, 127, 128, 140, 141, 160, 161}) {
		INFO("part " << part);
		CHECK(parts.find("\"partnum\": " + std::to_string(part) + ",") != std::string::npos);
	}
	CHECK(parts.find("\"partnum\": 129,") == std::string::npos);
}

/* The bundled sample (dev-mods/vehicle_maian_hover, a dev-mods.json release
 * entry) is exactly what the game's mod author builds from the recipe, so
 * the shipped package can never drift from its source. To regenerate it
 * after changing the recipe or its files, run this case with
 * PD2_TEST_ARTIFACT_DIR set and copy <dir>/vehicle_maian_hover/ over
 * dev-mods/vehicle_maian_hover/. */
TEST_CASE("the bundled Maian hover vehicle package is what its recipe builds",
          "[modding][author][recipe][maian-vehicle][bundled][T-RELEASE-007]") {
	const fs::path source = fs::path(PD_SOURCE_DIR) / "dev-mods";
	HoverTempDir mods;
	const std::string modsRoot = mods.path.string();

	char err[512] = "";
	std::unique_ptr<mod_author_recipe_t, HoverRecipeDeleter> recipe(
		modAuthorRecipeLoadFile((source / "maian_hover_vehicle" / "recipe.json").string().c_str(),
			err, sizeof(err)));
	INFO("recipe: " << err);
	REQUIRE(recipe);
	mod_author_request_t request;
	modAuthorRecipeToRequest(recipe.get(), nullptr, modsRoot.c_str(), &request);
	auto result = std::make_unique<mod_author_result_t>();
	REQUIRE(modAuthorSave(&request, result.get(), err, sizeof(err)) == MOD_AUTHOR_OK);

	const std::string builtArchive = hoverReadFile(result->archive_path);
	const std::string builtManifest = hoverReadFile(result->manifest_path);
	REQUIRE_FALSE(builtArchive.empty());
	REQUIRE_FALSE(builtManifest.empty());

	/* The mod author's own folder is a package: its manifest and the archive. */
	size_t files = 0;
	for (const auto &entry : fs::recursive_directory_iterator(fs::path(result->archive_path).parent_path())) {
		if (entry.is_regular_file()) files++;
	}
	CHECK(files == 2);

	const fs::path bundled = source / "vehicle_maian_hover";
	const std::string shippedArchive = hoverReadFile(bundled / "vehicle_maian_hover.pdvehicle");
	const std::string shippedManifest = hoverReadFile(bundled / "mod.json");
	const bool same = shippedArchive == builtArchive && shippedManifest == builtManifest;
	if (!same) {
		if (const char *keep = std::getenv("PD2_TEST_ARTIFACT_DIR")) {
			if (keep[0]) {
				std::error_code ec;
				const fs::path out = fs::path(keep) / "vehicle_maian_hover";
				fs::create_directories(out, ec);
				fs::copy_file(result->archive_path, out / "vehicle_maian_hover.pdvehicle",
					fs::copy_options::overwrite_existing, ec);
				fs::copy_file(result->manifest_path, out / "mod.json",
					fs::copy_options::overwrite_existing, ec);
			}
		}
	}
	INFO("dev-mods/vehicle_maian_hover differs from what the recipe builds; see the comment above");
	CHECK(shippedArchive == builtArchive);
	CHECK(shippedManifest == builtManifest);
	CHECK(shippedManifest.find("\"id\": \"mod.vehicle_maian_hover\"") != std::string::npos);
}
