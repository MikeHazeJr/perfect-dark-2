#include "catch.hpp"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

extern "C" {
#include "modasset_material_order.h"
}

TEST_CASE("MTL declaration order retains unused indexed material slots",
	"[modding][model][material-order][B-1105]")
{
	const char *mtl =
		"newmtl mat_first\n"
		"Kd 1 1 1\n"
		"newmtl mat_declared_unused\n"
		"newmtl mat_last\n";
	const char *obj_names[] = {"pd_default", "mat_last", "mat_first"};
	const u8 obj_used[] = {0, 1, 1};
	modasset_material_order_t order{};
	char error[128]{};

	REQUIRE(modAssetMaterialOrderBuild(mtl, obj_names, obj_used, 3,
		&order, error, sizeof(error)) == 1);
	REQUIRE(order.count == 3);
	REQUIRE(std::strcmp(order.names[0], "mat_first") == 0);
	REQUIRE(std::strcmp(order.names[1], "mat_declared_unused") == 0);
	REQUIRE(std::strcmp(order.names[2], "mat_last") == 0);
	REQUIRE(order.obj_to_declared[0] == -1);
	REQUIRE(order.obj_to_declared[1] == 2);
	REQUIRE(order.obj_to_declared[2] == 0);
	modAssetMaterialOrderFree(&order);
}

TEST_CASE("MTL material identity rejects duplicate declarations",
	"[modding][model][material-order][B-1105]")
{
	const char *obj_names[] = {"mat_a"};
	const u8 obj_used[] = {1};
	modasset_material_order_t order{};
	char error[128]{};

	REQUIRE(modAssetMaterialOrderBuild(
		"newmtl mat_a\nnewmtl mat_a\n", obj_names, obj_used, 1,
		&order, error, sizeof(error)) == 0);
	REQUIRE(std::strcmp(error, "material_declaration_duplicate") == 0);
	REQUIRE(order.names == nullptr);
	REQUIRE(order.obj_to_declared == nullptr);
}

TEST_CASE("MTL material identity requires every face material declaration",
	"[modding][model][material-order][B-1105]")
{
	const char *obj_names[] = {"mat_declared", "mat_missing"};
	const u8 used_missing[] = {1, 1};
	const u8 unused_missing[] = {1, 0};
	modasset_material_order_t order{};
	char error[128]{};

	REQUIRE(modAssetMaterialOrderBuild("newmtl mat_declared\n",
		obj_names, used_missing, 2, &order, error, sizeof(error)) == 0);
	REQUIRE(std::strcmp(error, "obj_used_material_not_declared") == 0);

	REQUIRE(modAssetMaterialOrderBuild("newmtl mat_declared\n",
		obj_names, unused_missing, 2, &order, error, sizeof(error)) == 1);
	REQUIRE(order.count == 1);
	REQUIRE(order.obj_to_declared[0] == 0);
	REQUIRE(order.obj_to_declared[1] == -1);
	modAssetMaterialOrderFree(&order);
}

TEST_CASE("MTL material identity rejects missing and oversized names",
	"[modding][model][material-order][B-1105]")
{
	modasset_material_order_t order{};
	char error[128]{};
	const std::string oversized(64, 'x');

	REQUIRE(modAssetMaterialOrderBuild("newmtl   # empty\n",
		nullptr, nullptr, 0, &order, error, sizeof(error)) == 0);
	REQUIRE(std::strcmp(error, "material_declaration_missing_name") == 0);

	const std::string mtl = "newmtl " + oversized + "\n";
	REQUIRE(modAssetMaterialOrderBuild(mtl.c_str(), nullptr, nullptr, 0,
		&order, error, sizeof(error)) == 0);
	REQUIRE(std::strcmp(error, "material_declaration_name_too_long") == 0);
}

TEST_CASE("readable cache and runtime modeldef share material reconciliation",
	"[modding][model][material-order][cache][B-1105]")
{
	std::ifstream compiler_file("port/src/modasset_compiler.c", std::ios::binary);
	REQUIRE(compiler_file.good());
	std::ostringstream compiler_contents;
	compiler_contents << compiler_file.rdbuf();
	const std::string compiler = compiler_contents.str();

	REQUIRE(compiler.find(
		"generatedModeldefLoadMaterialMetadata(source_path, &obj_mesh)") !=
		std::string::npos);
	REQUIRE(compiler.find(
		"generatedModeldefLoadMaterialMetadata(source_path, &mesh)") !=
		std::string::npos);
	REQUIRE(compiler.find(
		"row.material < 0 || row.material >= mesh->material_count") !=
		std::string::npos);

	std::ifstream header_file("port/include/modasset_compiler.h",
		std::ios::binary);
	REQUIRE(header_file.good());
	std::ostringstream header_contents;
	header_contents << header_file.rdbuf();
	REQUIRE(header_contents.str().find(
		"#define MODASSET_COMPILER_MODELDEF_VERSION 12") !=
		std::string::npos);
}
