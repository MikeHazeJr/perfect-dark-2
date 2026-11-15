/**
 * test_vehicle_model_slot.cpp -- a mod vehicle gets its own model number.
 *
 * The game finds a vehicle by its model number (the hoverbike code asks
 * assetRuntimeVehicleForModelnum for the binding whose runtime id is the
 * object's modelnum). So the scanner binds a mod vehicle's embedded mesh
 * through a private model slot and gives the vehicle row that slot
 * (assetcatalog_scanner.c registerVehicleModel), and --debug-swap-vehicle puts
 * such a vehicle in place of a stage's hoverbikes (vehicle_debug.c).
 *
 * Scanning needs the game's file system and catalog, so driving the vehicle
 * is proven in game by tools/smoke-verify/tests/mod_vehicle_drive_smoke.json.
 * These cases cover what runs without a catalog and pin the wiring.
 */
#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

#include "vehicle_debug.h"

namespace {

/* MODEL_HOVBIKE (src/include/constants.h), the base hoverbike's model. */
constexpr int kBaseHoverbikeModel = 0x3a;

std::string readTextFile(const char *path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());
	std::ostringstream ss;
	ss << in.rdbuf();
	std::string text = ss.str();
	std::string out;
	out.reserve(text.size());
	for (std::size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
		out += text[i];
	}
	return out;
}

/* The first definition or mention of `name` through its closing brace. */
std::string functionBlock(const std::string &text, const std::string &name)
{
	const std::size_t start = text.find(name);
	if (start == std::string::npos) return "";
	const std::size_t brace = text.find('{', start);
	if (brace == std::string::npos) return "";
	int depth = 0;
	for (std::size_t i = brace; i < text.size(); ++i) {
		if (text[i] == '{') depth++;
		else if (text[i] == '}' && --depth == 0) return text.substr(start, i - start + 1);
	}
	return "";
}

void requireInOrder(const std::string &text, const char *first, const char *second)
{
	const std::size_t a = text.find(first);
	const std::size_t b = text.find(second);
	INFO(first << " before " << second);
	REQUIRE(a != std::string::npos);
	REQUIRE(b != std::string::npos);
	REQUIRE(a < b);
}

} // namespace

TEST_CASE("vehicle swap changes nothing until a placeable vehicle is named",
	"[vehicle][modding][vehicle-model-slot]")
{
	vehicleDebugSetSwap(nullptr);
	CHECK(std::string(vehicleDebugSwapId()).empty());
	CHECK(vehicleDebugSwapModelnum(kBaseHoverbikeModel) == kBaseHoverbikeModel);

	/* A vehicle the catalog does not have leaves the hoverbike as it is. */
	vehicleDebugSetSwap("mod:vehicle_not_in_this_catalog");
	CHECK(std::string(vehicleDebugSwapId()) == "mod:vehicle_not_in_this_catalog");
	CHECK(vehicleDebugSwapModelnum(kBaseHoverbikeModel) == kBaseHoverbikeModel);

	vehicleDebugSetSwap("");
	CHECK(std::string(vehicleDebugSwapId()).empty());
	CHECK(vehicleDebugSwapModelnum(kBaseHoverbikeModel) == kBaseHoverbikeModel);
}

TEST_CASE("a mod vehicle's model is bound through a private model slot in both scan paths",
	"[vehicle][modding][vehicle-model-slot][static]")
{
	const std::string scanner = readTextFile("port/src/assetcatalog_scanner.c");
	const std::string bind = functionBlock(scanner, "static s32 registerVehicleModel");
	REQUIRE_FALSE(bind.empty());
	/* Base vehicles keep their native model (MODEL_HOVBIKE). */
	REQUIRE(bind.find("if (vehicle->bundled) return 1;") != std::string::npos);
	requireInOrder(bind, "meshSourcePreparePrivate(model_path", "externalScanTransactionBegin(&transaction)");
	requireInOrder(bind, "externalScanTransactionBegin(&transaction)",
		"bodyHeadSourceBindMesh(&mesh, 0, temporary, &filenum");
	requireInOrder(bind, "bodyHeadSourceBindMesh(&mesh, 0, temporary, &filenum",
		"row->runtime_index = model->runtime_index;");
	REQUIRE(bind.find("model->runtime_index < MODEL_CUSTOM_START") != std::string::npos);
	REQUIRE(bind.find("catalogDepRegisterTyped(id, mesh.id, ASSET_MODEL, 0)") != std::string::npos);
	REQUIRE(bind.find("externalScanTransactionRollback(&transaction)") != std::string::npos);

	const std::string folder = functionBlock(scanner, "static s32 registerTypedPdDescriptorFile");
	REQUIRE(folder.find("registerVehicleModel(vehicle_id, model_err") != std::string::npos);
	REQUIRE(folder.find("PDVEHICLE.MODEL.REJECT") != std::string::npos);

	const std::string archive = functionBlock(scanner, "s32 assetCatalogScanComponentsFromArchive");
	REQUIRE(archive.find("ini_type == ASSET_VEHICLE") != std::string::npos);
	REQUIRE(archive.find("registerVehicleModel(vehicle_id, model_err") != std::string::npos);
}

TEST_CASE("the private mesh preflight ignores the native file a cloned mesh names",
	"[vehicle][modding][vehicle-model-slot][static]")
{
	const std::string source = readTextFile("port/src/body_head_source_bind.c");
	const std::string impl = functionBlock(source, "static s32 bodyHeadSourcePrepareMeshImpl");
	REQUIRE_FALSE(impl.empty());
	REQUIRE(impl.find("force_private ? -1 : (existing ? existing->source_filenum : -1)") !=
		std::string::npos);
	REQUIRE(impl.find("candidate.plan.source_filenum = held >= 0 ? "
		"assetCatalogModelPrivateSourceFilenum(held) : -1;") != std::string::npos);
	REQUIRE(impl.find("mesh already has a native model number") != std::string::npos);

	const std::string wrapper = functionBlock(source, "s32 meshSourcePreparePrivate");
	REQUIRE(wrapper.find("bodyHeadSourcePrepareMeshImpl(archive_path, expected_id, 0, 1,") !=
		std::string::npos);
	const std::string body_head = functionBlock(source, "s32 bodyHeadSourcePrepareMesh(");
	REQUIRE(body_head.find("source_by_catalog_id, 0, out,") != std::string::npos);
}

TEST_CASE("stage setup gives a hoverbike the swap vehicle's model before the model loads",
	"[vehicle][modding][vehicle-model-slot][static]")
{
	const std::string setup = readTextFile("src/game/setup.c");
	const std::size_t hoverbike = setup.find("case OBJTYPE_HOVERBIKE:");
	REQUIRE(hoverbike != std::string::npos);
	const std::string body = setup.substr(hoverbike, 600);
	requireInOrder(body, "obj->modelnum = vehicleDebugSwapModelnum(obj->modelnum);",
		"setupCreateObject(obj, index);");

	const std::string main_c = readTextFile("port/src/main.c");
	REQUIRE(main_c.find("vehicleDebugSetSwap(sysArgGetString(\"--debug-swap-vehicle\"));") !=
		std::string::npos);
}
