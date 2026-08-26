#include "catch.hpp"

#include <string>

extern "C" {
#include "loader_walker_mesh_source.h"
}

TEST_CASE("SP-74 mesh source plan preserves typed nested ownership",
		"[B-1105][SP-74][modding][pdmesh]")
{
	const char manifest[] =
		"{\n"
		"  \"pd_kind\": \"mesh\",\n"
		"  \"id\": \"base:model_skpistol_hi\",\n"
		"  \"geometry\": \"model.obj\",\n"
		"  \"source_filenum_symbol\": \"FILE_GSKPISTOL\",\n"
		"  \"dependencies\": [{\"id\":\"base:material_skpistol\","
		"\"geometry\":\"ignored.obj\"}]\n"
		"}\n";
	loader_walker_mesh_source_plan_t plan{};
	char error[256]{};

	REQUIRE(loaderWalkerMeshSourcePlanManifest(manifest,
		sizeof(manifest) - 1, "base:model_skpistol_hi",
		"data/ntsc-final/weapons/base_mauler.pdweapon::"
		"dependencies/assets/models/held_hi.pdmesh",
		"model.obj", -1, &plan, error, sizeof(error)) == 1);
	REQUIRE(std::string(plan.geometry_member) == "model.obj");
	REQUIRE(std::string(plan.archive_path) ==
		"data/ntsc-final/weapons/base_mauler.pdweapon::"
		"dependencies/assets/models/held_hi.pdmesh");
	REQUIRE(std::string(plan.source_path) ==
		"data/ntsc-final/weapons/base_mauler.pdweapon::"
		"dependencies/assets/models/held_hi.pdmesh::model.obj");
	REQUIRE(plan.source_filenum > 0);
	REQUIRE(plan.source_symbol_present == 1);

	const s32 stable_filenum = plan.source_filenum;
	REQUIRE(loaderWalkerMeshSourcePlanManifest(manifest,
		sizeof(manifest) - 1, "base:model_skpistol_hi",
		"base_mauler.pdweapon::held_hi.pdmesh", "model.obj",
		stable_filenum, &plan, error, sizeof(error)) == 1);
	REQUIRE(plan.source_filenum == stable_filenum);

	REQUIRE(loaderWalkerMeshSourcePlanManifest(manifest,
		sizeof(manifest) - 1, "base:model_skpistol_hi",
		"base_mauler.pdweapon::held_hi.pdmesh", "model.obj",
		stable_filenum + 1, &plan, error, sizeof(error)) == 0);
	REQUIRE(std::string(error).find("conflicts with stable row") !=
		std::string::npos);
}

TEST_CASE("SP-74 top-level and nested mesh descriptors share alias precedence",
		"[B-1105][SP-74][modding][pdmesh]")
{
	const char *expected[] = {
		"model_file", "geometry_file", "model", "geometry", "file_path"
	};
	REQUIRE(loaderWalkerMeshPublicGeometryKeyCount() ==
		sizeof(expected) / sizeof(expected[0]));
	for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
		REQUIRE(std::string(loaderWalkerMeshPublicGeometryKey(i)) == expected[i]);
	}
	REQUIRE(loaderWalkerMeshPublicGeometryKey(
		loaderWalkerMeshPublicGeometryKeyCount()) == nullptr);
}

TEST_CASE("SP-74 mesh source plan defaults custom public geometry safely",
		"[B-1105][SP-74][modding][pdmesh]")
{
	loader_walker_mesh_source_plan_t plan{};
	char error[256]{};

	REQUIRE(loaderWalkerMeshSourcePlanManifest(nullptr, 0,
		"sample:burst_model", "sample_weapon.pdweapon::burst.pdmesh",
		"model.gltf", -1, &plan, error, sizeof(error)) == 1);
	REQUIRE(std::string(plan.geometry_member) == "model.gltf");
	REQUIRE(std::string(plan.source_path) ==
		"sample_weapon.pdweapon::burst.pdmesh::model.gltf");
	REQUIRE(plan.source_filenum == -1);
	REQUIRE(plan.source_symbol_present == 0);

	REQUIRE(loaderWalkerMeshSourcePlanManifest(nullptr, 0,
		"sample:default_model", "sample.pdmesh", nullptr, -1,
		&plan, error, sizeof(error)) == 1);
	REQUIRE(std::string(plan.geometry_member) == "model.obj");
	REQUIRE(std::string(plan.source_path) == "sample.pdmesh::model.obj");
}

TEST_CASE("SP-74 mesh source manifest parser validates the complete JSON root",
		"[B-1105][SP-74][modding][pdmesh]")
{
	loader_walker_mesh_source_plan_t plan{};
	char error[256]{};
	const char nested_identity_text[] =
		"{\"note\":\"text { [ \\\"geometry\\\":\\\"ignored.obj\\\" ] }\","
		"\"dependencies\":[{\"id\":\"nested:id\","
		"\"geometry\":\"ignored.obj\"}]}";
	REQUIRE(loaderWalkerMeshSourcePlanManifest(nested_identity_text,
		sizeof(nested_identity_text) - 1, "sample:model", "sample.pdmesh",
		"model.obj", -1, &plan, error, sizeof(error)) == 1);
	REQUIRE(std::string(plan.geometry_member) == "model.obj");

	const char truncated[] = "{\"geometry\":\"model.obj\"";
	REQUIRE(loaderWalkerMeshSourcePlanManifest(truncated,
		sizeof(truncated) - 1, "sample:model", "sample.pdmesh",
		"model.obj", -1, &plan, error, sizeof(error)) == 0);

	const char trailing[] = "{\"geometry\":\"model.obj\"} trailing";
	REQUIRE(loaderWalkerMeshSourcePlanManifest(trailing,
		sizeof(trailing) - 1, "sample:model", "sample.pdmesh",
		"model.obj", -1, &plan, error, sizeof(error)) == 0);

	const char escaped_identity[] = "{\"geometry\":\"model\\u002eobj\"}";
	REQUIRE(loaderWalkerMeshSourcePlanManifest(escaped_identity,
		sizeof(escaped_identity) - 1, "sample:model", "sample.pdmesh",
		"model.obj", -1, &plan, error, sizeof(error)) == 0);

	const char escaped_key[] = "{\"geo\\u006detry\":\"model.obj\"}";
	REQUIRE(loaderWalkerMeshSourcePlanManifest(escaped_key,
		sizeof(escaped_key) - 1, "sample:model", "sample.pdmesh",
		"model.obj", -1, &plan, error, sizeof(error)) == 0);
}

TEST_CASE("SP-74 mesh source changes encode the legacy pseudo-loaded exception",
		"[B-1105][SP-74][modding][pdmesh][lifecycle]")
{
	REQUIRE(loaderWalkerMeshSourceChangeAllowed(ASSET_STATE_REGISTERED,
		0, 0, 0, ASSET_PAYLOAD_NONE) == 1);
	REQUIRE(loaderWalkerMeshSourceChangeAllowed(ASSET_STATE_ENABLED,
		0, 0, 0, ASSET_PAYLOAD_NONE) == 1);
	REQUIRE(loaderWalkerMeshSourceChangeAllowed(ASSET_STATE_LOADED,
		1, ASSET_REF_BUNDLED, 0, ASSET_PAYLOAD_NONE) == 1);

	REQUIRE(loaderWalkerMeshSourceChangeAllowed(ASSET_STATE_LOADED,
		0, 0, 0, ASSET_PAYLOAD_NONE) == 0);
	REQUIRE(loaderWalkerMeshSourceChangeAllowed(ASSET_STATE_ACTIVE,
		1, ASSET_REF_BUNDLED, 0, ASSET_PAYLOAD_NONE) == 0);
	REQUIRE(loaderWalkerMeshSourceChangeAllowed(ASSET_STATE_REGISTERED,
		0, 0, 1, ASSET_PAYLOAD_NONE) == 0);
	REQUIRE(loaderWalkerMeshSourceChangeAllowed(ASSET_STATE_REGISTERED,
		0, 0, 0, ASSET_PAYLOAD_STAGE_MODELDEF) == 0);
	REQUIRE(loaderWalkerMeshSourceChangeAllowed((asset_load_state_t)99,
		1, ASSET_REF_BUNDLED, 0, ASSET_PAYLOAD_NONE) == 0);
}

TEST_CASE("SP-74 mesh source plan rejects ambiguous or unsafe ownership",
		"[B-1105][SP-74][modding][pdmesh]")
{
	loader_walker_mesh_source_plan_t plan{};
	char error[256]{};
	const char mismatched_geometry[] =
		"{\"pd_kind\":\"mesh\",\"id\":\"sample:model\","
		"\"geometry\":\"model.obj\"}";
	REQUIRE(loaderWalkerMeshSourcePlanManifest(mismatched_geometry,
		sizeof(mismatched_geometry) - 1, "sample:model", "sample.pdmesh",
		"model.gltf", -1, &plan, error, sizeof(error)) == 0);

	const char unknown_symbol[] =
		"{\"pd_kind\":\"mesh\",\"id\":\"sample:model\","
		"\"source_filenum_symbol\":\"FILE_NOT_REAL_SP74\"}";
	REQUIRE(loaderWalkerMeshSourcePlanManifest(unknown_symbol,
		sizeof(unknown_symbol) - 1, "sample:model", "sample.pdmesh",
		"model.obj", -1, &plan, error, sizeof(error)) == 0);

	const char wrong_id[] =
		"{\"pd_kind\":\"mesh\",\"id\":\"other:model\"}";
	REQUIRE(loaderWalkerMeshSourcePlanManifest(wrong_id,
		sizeof(wrong_id) - 1, "sample:model", "sample.pdmesh",
		"model.obj", -1, &plan, error, sizeof(error)) == 0);

	const char duplicate_geometry[] =
		"{\"geometry\":\"model.obj\",\"geometry\":\"other.obj\"}";
	REQUIRE(loaderWalkerMeshSourcePlanManifest(duplicate_geometry,
		sizeof(duplicate_geometry) - 1, "sample:model", "sample.pdmesh",
		nullptr, -1, &plan, error, sizeof(error)) == 0);

	REQUIRE(loaderWalkerMeshSourcePlanManifest(nullptr, 0, "sample:model",
		"sample.pdmesh", "../outside.obj", -1, &plan, error,
		sizeof(error)) == 0);
	REQUIRE(loaderWalkerMeshSourcePlanManifest(nullptr, 0, "sample:model",
		"sample.pdmesh", "nested::other.obj", -1, &plan, error,
		sizeof(error)) == 0);
}
