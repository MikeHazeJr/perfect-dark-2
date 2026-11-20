#include "catch.hpp"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
		"generatedModeldefLoadMaterialMetadata(NULL, source_path, &obj_mesh)") !=
		std::string::npos);
	REQUIRE(compiler.find(
		"generatedModeldefLoadMaterialMetadata(inputs, source_path, &mesh)") !=
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
		"#define MODASSET_COMPILER_MODELDEF_VERSION 15") !=
		std::string::npos);
}


TEST_CASE("Grid visits the native explicit catalog texture directives in declared materials",
	"[modding][model][material-texture][grid]")
{
	std::vector<std::string> ids;
	const auto visit = [](const char *id, void *context) -> s32 {
		static_cast<std::vector<std::string> *>(context)->push_back(id);
		return 1;
	};
	const std::string valid = "pd_texture_catalog = sample:unbound\r\n"
		"  newmtl first\r\n"
		"  pd_texture_catalog = sample:body # diffuse\r\n"
		"  map_Kd local/colour.png\n"
		"newmtl unused\n"
		"pd_secondary_texture_catalog = sample:glow\n";
	char err[128]{};
	REQUIRE(modAssetMaterialVisitCatalogTextures(valid.data(), valid.size(), visit, &ids, err, sizeof(err)));
	CHECK(ids == std::vector<std::string>{"sample:body", "sample:glow"});

	for (const std::string suffix : {"", "sample:", "body", "other:bad/id", "a:b:c"}) {
		const std::string malformed = "newmtl first\npd_texture_catalog = " + suffix + "\n";
		ids.clear();
		CHECK_FALSE(modAssetMaterialVisitCatalogTextures(malformed.data(), malformed.size(), visit, &ids, err, sizeof(err)));
		CHECK(ids.empty());
		CHECK(std::strstr(err, "catalog ID") != nullptr);
	}
}

// B-1290: a monitor's or TV's screen is drawn by tvscreenRender from the four
// corners of its model node, as triangles (0,1,2) and (0,2,3). The game's own
// node holds the corners as vertices 0 to 3. A model compiled from typed
// source holds a node's triangles flattened, three vertices each, so the
// screen is six vertices, A B C A C D, and vertex 3 is A again: the second
// triangle had no area and half of every screen was not drawn (the Carrington
// Institute computer face of the v0.1.105 reports).
TEST_CASE("a compiled model's flattened screen node gives the screen its fourth corner",
	"[modding][model][render][b-1290]")
{
	std::ifstream compiler_file("port/src/modasset_compiler.c", std::ios::binary);
	REQUIRE(compiler_file.good());
	std::ostringstream compiler_text;
	compiler_text << compiler_file.rdbuf();
	const std::string compiler = compiler_text.str();
	// The layout the reader below relies on: three vertices per triangle.
	CHECK(compiler.find("fillGeneratedTriVertices(&payload->vertices[emitted * 3],") != std::string::npos);
	CHECK(compiler.find("rodata->dl.vertices = payload->vertices;") != std::string::npos);
	CHECK(compiler.find("rodata->dl.numvertices = (s16)payload->vertex_count;") != std::string::npos);

	std::ifstream game_file("src/game/propobj.c", std::ios::binary);
	REQUIRE(game_file.good());
	std::ostringstream game_text;
	game_text << game_file.rdbuf();
	const std::string game = game_text.str();
	const size_t begin = game.find("Gfx *tvscreenRender(struct model *model, struct modelnode *node,");
	REQUIRE(begin != std::string::npos);
	const size_t end = game.find("\n}\n", begin);
	REQUIRE(end != std::string::npos);
	const std::string body = game.substr(begin, end - begin);

	// Six vertices whose fourth and fifth repeat the first and third: the
	// flattened pair (0,1,2), (0,2,3). Its fourth corner is vertex 5.
	CHECK(body.find("if (rodata->dl.numvertices == 6\n") != std::string::npos);
	CHECK(body.find("&& corners[3].x == corners[0].x && corners[3].y == corners[0].y") != std::string::npos);
	CHECK(body.find("&& corners[4].x == corners[2].x && corners[4].y == corners[2].y") != std::string::npos);
	CHECK(body.find("fourth = 5;") != std::string::npos);
	CHECK(body.find("vertices[3] = corners[fourth];") != std::string::npos);
	// The old read, which took vertex 3 whatever the layout, is gone, and
	// the draw is still the two triangles over corners 0 to 3.
	CHECK(body.find("vertices[3] = rodata->dl.vertices[3];") == std::string::npos);
	CHECK(body.find("gSPVertex(gdl++, SEGADDR(SPSEGMENT_MODEL_VTX << 24), 4, 0);\n\t\tgSPTri2(gdl++, 0, 1, 2, 0, 2, 3);") !=
		std::string::npos);
}
