/**
 * test_vehicle_model_parts.cpp -- the toggle parts of a vehicle's typed mesh
 * (port/include/vehicle_model_parts.h).
 *
 * A vehicle graph's og.vehicle.part_visible nodes may name only toggle parts
 * of the vehicle's model. The catalog checks that when the vehicle registers,
 * from the mesh's part table and node table, before any model is built.
 */
#include "catch.hpp"

#include "vehicle_model_parts.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string partsReadFile(const char *relative)
{
	const std::filesystem::path path = std::filesystem::path(PD_SOURCE_DIR) / relative;
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct Parts {
	vehicle_model_parts_t *parts = nullptr;
	std::string error;

	Parts(const std::string &partsJson, const std::string &nodesJson)
	{
		char err[256] = { 0 };
		parts = vehicleModelPartsRead(partsJson.data(), partsJson.size(),
			nodesJson.data(), nodesJson.size(), err, sizeof(err));
		error = err;
	}
	~Parts() { vehicleModelPartsFree(parts); }
	Parts(const Parts &) = delete;
	Parts &operator=(const Parts &) = delete;
};

/* Three nodes: a position root (2), a display list (24) and a toggle (18). */
const char *const kNodes = R"({ "schema": "pd2.mesh.nodes.v1", "nodes": [
  { "id": 0, "type": 2 }, { "id": 1, "type": 24 }, { "id": 2, "type": 18 } ] })";

std::string partsTable(const std::string &rows)
{
	return "{ \"schema\": \"pd2.mesh.parts.v1\", \"parts\": [" + rows + "] }";
}

} // namespace

TEST_CASE("The Maian vehicle's crystal parts are toggles and its other parts are not",
          "[vehicle-model-parts]")
{
	Parts maian(partsReadFile("dev-mods/maian_hover_vehicle/meshes/vehicle/model.parts.json"),
		partsReadFile("dev-mods/maian_hover_vehicle/meshes/vehicle/model.nodes.json"));
	INFO(maian.error);
	REQUIRE(maian.parts != nullptr);
	// The seafoam crystals and the black ones inside them.
	CHECK(vehicleModelPartsIsToggle(maian.parts, 140) == 1);
	CHECK(vehicleModelPartsIsToggle(maian.parts, 141) == 1);
	CHECK(vehicleModelPartsToggleCount(maian.parts) == 2);
	// Parts the model has that are not toggles: turret yaw, cannon pitch, the
	// muzzle, a ring seat, and the two riders' eye points (160 the driver's,
	// 161 the gunner's).
	for (int part : { 0, 1, 2, 50, 110, 127, 128, 160, 161 }) {
		INFO("part " << part);
		CHECK(vehicleModelPartsIsToggle(maian.parts, part) == 0);
	}
	// A part the model does not have.
	CHECK(vehicleModelPartsIsToggle(maian.parts, 999) == 0);
	CHECK(vehicleModelPartsIsToggle(maian.parts, -1) == 0);
	// 25 parts of the hull, turret and crystals, and the two seat parts.
	CHECK(vehicleModelPartsCount(maian.parts) == 27);
	// No tables, no parts.
	CHECK(vehicleModelPartsIsToggle(nullptr, 140) == 0);
	CHECK(vehicleModelPartsCount(nullptr) == 0);
}

TEST_CASE("A mesh's part tables are read the way the model compiler reads them",
          "[vehicle-model-parts]")
{
	// A part's node is an index into the node array.
	Parts ok(partsTable("{ \"partnum\": 7, \"node\": 2 }, { \"partnum\": 8, \"node\": 1 }"), kNodes);
	INFO(ok.error);
	REQUIRE(ok.parts != nullptr);
	CHECK(vehicleModelPartsIsToggle(ok.parts, 7) == 1);
	CHECK(vehicleModelPartsIsToggle(ok.parts, 8) == 0);
	CHECK(vehicleModelPartsCount(ok.parts) == 2);

	// A part whose node could not be resolved is not a part.
	Parts unresolved(partsTable("{ \"partnum\": 7, \"node\": -1, \"node_unresolved\": true }, "
		"{ \"partnum\": 9, \"node\": 2 }"), kNodes);
	REQUIRE(unresolved.parts != nullptr);
	CHECK(vehicleModelPartsCount(unresolved.parts) == 1);
	CHECK(vehicleModelPartsIsToggle(unresolved.parts, 7) == 0);
	CHECK(vehicleModelPartsIsToggle(unresolved.parts, 9) == 1);

	// An empty part table is a model with no parts.
	Parts none(partsTable(""), kNodes);
	REQUIRE(none.parts != nullptr);
	CHECK(vehicleModelPartsCount(none.parts) == 0);
}

TEST_CASE("Broken part tables are refused with the reason", "[vehicle-model-parts]")
{
	const auto refused = [](const std::string &parts, const std::string &nodes) {
		Parts p(parts, nodes);
		CHECK(p.parts == nullptr);
		return p.error;
	};
	CHECK(refused("[1]", kNodes).find("model.parts.json is not one JSON object") != std::string::npos);
	CHECK(refused(partsTable(""), "nope").find("model.nodes.json is not one JSON object") !=
		std::string::npos);
	CHECK(refused("{ \"schema\": \"pd2.mesh.parts.v2\", \"parts\": [] }", kNodes)
		.find("model.parts.json needs \"schema\": \"pd2.mesh.parts.v1\"") != std::string::npos);
	CHECK(refused(partsTable(""), "{ \"schema\": \"pd2.mesh.nodes.v1\" }")
		.find("model.nodes.json has no nodes array") != std::string::npos);
	CHECK(refused(partsTable("{ \"partnum\": 7 }"), kNodes).find("needs a partnum and a node") !=
		std::string::npos);
	CHECK(refused(partsTable("{ \"partnum\": 7.5, \"node\": 2 }"), kNodes)
		.find("needs a partnum and a node") != std::string::npos);
	CHECK(refused(partsTable("{ \"partnum\": 7, \"node\": 3 }"), kNodes)
		.find("part 7 names node 3, which the mesh does not have") != std::string::npos);
	CHECK(refused(partsTable("{ \"partnum\": 7, \"node\": -1 }"), kNodes)
		.find("part 7 names node -1, which the mesh does not have") != std::string::npos);
	CHECK(refused(partsTable("{ \"partnum\": 7, \"node\": 2 }, { \"partnum\": 7, \"node\": 1 }"), kNodes)
		.find("lists part 7 twice") != std::string::npos);
	CHECK(refused(partsTable("{ \"partnum\": 7, \"node\": 0 }"),
		"{ \"schema\": \"pd2.mesh.nodes.v1\", \"nodes\": [ { \"id\": 0 } ] }")
		.find("model.nodes.json node 0 has no type") != std::string::npos);
	CHECK(refused("", kNodes).find("model.parts.json is not one JSON object") != std::string::npos);
}
