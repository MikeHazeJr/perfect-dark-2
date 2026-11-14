/**
 * test_mod_author_recipe.cpp -- a mod described in a file.
 *
 * A recipe is read strictly and turned into the same request the creator
 * screens build. These cases cover the reader, its refusals, and one whole
 * trip: a recipe file on disk becomes a saved, release-valid weapon mod.
 */
#include "catch.hpp"
extern "C" {
#include "asset_archive_clone.h"
#include "asset_archive_policy.h"
#include "mod_author.h"
#include "mod_author_recipe.h"
#include "mod_manifest_validate.h"
#include "modarchive.h"
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
#include <vector>

namespace {

namespace fs = std::filesystem;

const char *const kRecipeWeaponTemplate =
	"examples/modding/typed-pdxxx-basic/weapons/tri_weapon.pdweapon";

struct RecipeTempDir {
	fs::path path;

	RecipeTempDir() {
		static unsigned counter = 0;
		const auto stamp =
			std::chrono::high_resolution_clock::now().time_since_epoch().count();
		path = fs::temp_directory_path()
			/ ("pd2-mod-recipe-" + std::to_string(stamp) + "-" + std::to_string(counter++));
		fs::create_directories(path);
	}

	~RecipeTempDir() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

std::string recipeReadFile(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void recipeWriteFile(const fs::path &path, const std::string &bytes)
{
	fs::create_directories(path.parent_path());
	std::ofstream out(path, std::ios::binary);
	out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

struct RecipeDeleter {
	void operator()(mod_author_recipe_t *recipe) const { modAuthorRecipeFree(recipe); }
};
using RecipePtr = std::unique_ptr<mod_author_recipe_t, RecipeDeleter>;

struct RecipeParse {
	RecipePtr recipe;
	std::string err;
};

RecipeParse recipeParse(const std::string &json, const fs::path &dir)
{
	RecipeParse out;
	char err[256];
	std::memset(err, 'x', sizeof(err));
	err[sizeof(err) - 1] = '\0';
	const std::string dirText = dir.string();
	out.recipe.reset(modAuthorRecipeParse(json.data(), json.size(), dirText.c_str(),
		err, sizeof(err)));
	out.err = err;
	return out;
}

std::string recipeEntry(const std::string &archive, const char *entry)
{
	u32 size = 0;
	void *data = modArchiveExtractMemAlloc(archive.data(),
		static_cast<u32>(archive.size()), entry, &size);
	REQUIRE(data != nullptr);
	std::string out(static_cast<const char *>(data), size);
	free(data);
	return out;
}

} // namespace

TEST_CASE("mod author recipe reads a full recipe into a request",
          "[modding][author][recipe]") {
	RecipeTempDir dir;
	recipeWriteFile(dir.path / "parts" / "settings.json", "{ \"from\": \"file\" }\n");

	const std::string json = R"({
  "schema": "pd2.mod_author.recipe.v1",
  "template": "base:falcon2",
  "name": "Rivet Carbine",
  "author": "Mike",
  "version": "1.2.0",
  "description": "Dock yard burst carbine.",
  "descriptor": { "posx": "30.0", "muzzlez": "3.0", "old_key": null },
  "overrides": [
    { "entry": "behavior/settings.json", "file": "parts/settings.json" },
    { "entry": "bindings/presentation.json", "text": "{ \"from\": \"text\" }" },
    { "entry": "dependencies/assets/ui/reticle.pdui", "remove": true },
    { "entry": "notes/empty.json", "text": "", "remove": false }
  ]
})";
	const RecipeParse parsed = recipeParse(json, dir.path);
	INFO("recipe error: " << parsed.err);
	REQUIRE(parsed.recipe);
	CHECK(parsed.err.empty());
	CHECK(std::string(modAuthorRecipeTemplateId(parsed.recipe.get())) == "base:falcon2");
	CHECK(std::string(modAuthorRecipeTemplateFile(parsed.recipe.get())).empty());

	mod_author_request_t request;
	std::memset(&request, 0x5a, sizeof(request));
	modAuthorRecipeToRequest(parsed.recipe.get(), "C:/game/data/weapons/base_falcon2.pdweapon",
		"C:/game/mods", &request);
	CHECK(std::string(request.template_path) == "C:/game/data/weapons/base_falcon2.pdweapon");
	CHECK(std::string(request.mods_root) == "C:/game/mods");
	CHECK(std::string(request.display_name) == "Rivet Carbine");
	CHECK(std::string(request.author) == "Mike");
	CHECK(std::string(request.version) == "1.2.0");
	CHECK(std::string(request.description) == "Dock yard burst carbine.");
	CHECK(std::string(request.tool) == "mod-author-recipe");
	REQUIRE(request.override_count == 4);
	REQUIRE(request.overrides != nullptr);

	CHECK(std::string(request.overrides[0].entry) == "behavior/settings.json");
	CHECK(std::string(static_cast<const char *>(request.overrides[0].data),
		request.overrides[0].len) == "{ \"from\": \"file\" }\n");
	CHECK(request.overrides[0].remove == 0);

	CHECK(std::string(request.overrides[1].entry) == "bindings/presentation.json");
	CHECK(std::string(static_cast<const char *>(request.overrides[1].data),
		request.overrides[1].len) == "{ \"from\": \"text\" }");

	CHECK(std::string(request.overrides[2].entry) == "dependencies/assets/ui/reticle.pdui");
	CHECK(request.overrides[2].remove == 1);
	CHECK(request.overrides[2].len == 0);

	CHECK(std::string(request.overrides[3].entry) == "notes/empty.json");
	CHECK(request.overrides[3].remove == 0);
	CHECK(request.overrides[3].len == 0);
	CHECK(request.overrides[3].data != nullptr);

	/* Descriptor keys arrive in alphabetical order; null means remove. */
	REQUIRE(request.descriptor_value_count == 3);
	REQUIRE(request.descriptor_values != nullptr);
	CHECK(std::string(request.descriptor_values[0].key) == "muzzlez");
	CHECK(std::string(request.descriptor_values[0].value) == "3.0");
	CHECK(std::string(request.descriptor_values[1].key) == "old_key");
	CHECK(request.descriptor_values[1].value == nullptr);
	CHECK(std::string(request.descriptor_values[2].key) == "posx");
	CHECK(std::string(request.descriptor_values[2].value) == "30.0");

	/* The smallest recipe: a template and a name. */
	const RecipeParse minimal = recipeParse(
		"{\"schema\":\"pd2.mod_author.recipe.v1\",\"template_file\":\"sub/gun.pdweapon\","
		"\"name\":\"Plain\"}", dir.path);
	INFO("minimal error: " << minimal.err);
	REQUIRE(minimal.recipe);
	CHECK(std::string(modAuthorRecipeTemplateId(minimal.recipe.get())).empty());
	CHECK(fs::path(modAuthorRecipeTemplateFile(minimal.recipe.get())).lexically_normal()
		== (dir.path / "sub" / "gun.pdweapon").lexically_normal());
	modAuthorRecipeToRequest(minimal.recipe.get(), nullptr, "mods", &request);
	CHECK(fs::path(request.template_path).lexically_normal()
		== (dir.path / "sub" / "gun.pdweapon").lexically_normal());
	CHECK(request.author == nullptr);
	CHECK(request.version == nullptr);
	CHECK(request.description == nullptr);
	CHECK(request.overrides == nullptr);
	CHECK(request.override_count == 0);
	CHECK(request.descriptor_values == nullptr);
	CHECK(request.descriptor_value_count == 0);

	/* Nothing to read is handled, not dereferenced. */
	modAuthorRecipeFree(nullptr);
	CHECK(std::string(modAuthorRecipeTemplateId(nullptr)).empty());
	CHECK(std::string(modAuthorRecipeTemplateFile(nullptr)).empty());
	modAuthorRecipeToRequest(nullptr, nullptr, nullptr, &request);
	CHECK(request.display_name == nullptr);
	modAuthorRecipeToRequest(parsed.recipe.get(), nullptr, nullptr, nullptr);
}

TEST_CASE("mod author recipe refuses anything it does not fully understand",
          "[modding][author][recipe]") {
	RecipeTempDir dir;
	recipeWriteFile(dir.path / "settings.json", "{}\n");
	recipeWriteFile(dir.path.parent_path() / (dir.path.filename().string() + "-outside.json"),
		"{}\n");

	const std::string head =
		"{\"schema\":\"pd2.mod_author.recipe.v1\",\"template\":\"base:falcon2\",\"name\":\"Gun\"";

	struct Case { const char *what; std::string json; const char *expect; };
	const std::string manyOverrides = [&] {
		std::string json = head + ",\"overrides\":[";
		for (int i = 0; i < 128; i++) {
			if (i) json += ",";
			json += "{\"entry\":\"notes/n" + std::to_string(i) + ".json\",\"text\":\"{}\"}";
		}
		return json + "]}";
	}();
	const Case cases[] = {
		{ "empty", "", "empty" },
		{ "not json", "this is not json", "JSON" },
		{ "byte order mark", "\xef\xbb\xbf" + head + "}", "JSON" },
		{ "duplicate key", head + ",\"name\":\"Other\"}", "JSON" },
		{ "trailing text", head + "} extra", "JSON" },
		{ "array root", "[]", "object" },
		{ "no schema", "{\"template\":\"base:falcon2\",\"name\":\"Gun\"}", "schema" },
		{ "wrong schema", "{\"schema\":\"pd2.mod_author.recipe.v2\",\"template\":\"base:falcon2\","
			"\"name\":\"Gun\"}", "schema" },
		{ "schema not a string", "{\"schema\":1,\"template\":\"base:falcon2\",\"name\":\"Gun\"}",
			"schema" },
		{ "unknown key", head + ",\"overides\":[]}", "overides" },
		{ "no template", "{\"schema\":\"pd2.mod_author.recipe.v1\",\"name\":\"Gun\"}", "template" },
		{ "both templates", head + ",\"template_file\":\"gun.pdweapon\"}", "exactly one" },
		{ "empty template", "{\"schema\":\"pd2.mod_author.recipe.v1\",\"template\":\"\","
			"\"name\":\"Gun\"}", "template" },
		{ "template not a string", "{\"schema\":\"pd2.mod_author.recipe.v1\",\"template\":7,"
			"\"name\":\"Gun\"}", "template" },
		{ "no name", "{\"schema\":\"pd2.mod_author.recipe.v1\",\"template\":\"base:falcon2\"}",
			"name" },
		{ "name not a string", "{\"schema\":\"pd2.mod_author.recipe.v1\","
			"\"template\":\"base:falcon2\",\"name\":[\"Gun\"]}", "name" },
		{ "author not a string", head + ",\"author\":null}", "author" },
		{ "version not a string", head + ",\"version\":1.0}", "version" },
		{ "description not a string", head + ",\"description\":{}}", "description" },
		{ "template file with a drive", "{\"schema\":\"pd2.mod_author.recipe.v1\","
			"\"template_file\":\"C:/gun.pdweapon\",\"name\":\"Gun\"}", "inside" },
		{ "template file from the root", "{\"schema\":\"pd2.mod_author.recipe.v1\","
			"\"template_file\":\"/gun.pdweapon\",\"name\":\"Gun\"}", "inside" },
		{ "template file from a share", "{\"schema\":\"pd2.mod_author.recipe.v1\","
			"\"template_file\":\"\\\\\\\\host\\\\gun.pdweapon\",\"name\":\"Gun\"}", "inside" },
		{ "template file climbing out", "{\"schema\":\"pd2.mod_author.recipe.v1\","
			"\"template_file\":\"sub/../../gun.pdweapon\",\"name\":\"Gun\"}", "inside" },
		{ "empty template file", "{\"schema\":\"pd2.mod_author.recipe.v1\","
			"\"template_file\":\"\",\"name\":\"Gun\"}", "inside" },
		{ "descriptor not an object", head + ",\"descriptor\":[]}", "descriptor" },
		{ "descriptor value not a string", head + ",\"descriptor\":{\"posx\":3}}", "posx" },
		{ "descriptor value an object", head + ",\"descriptor\":{\"posx\":{}}}", "posx" },
		{ "overrides not an array", head + ",\"overrides\":{}}", "array" },
		{ "override not an object", head + ",\"overrides\":[\"x\"]}", "override 1" },
		{ "override unknown key", head + ",\"overrides\":[{\"entry\":\"a.json\",\"text\":\"{}\","
			"\"mode\":\"x\"}]}", "mode" },
		{ "override without entry", head + ",\"overrides\":[{\"text\":\"{}\"}]}", "entry" },
		{ "override empty entry", head + ",\"overrides\":[{\"entry\":\"\",\"text\":\"{}\"}]}",
			"entry" },
		{ "override entry not a string", head + ",\"overrides\":[{\"entry\":3,\"text\":\"{}\"}]}",
			"entry" },
		{ "override with no source", head + ",\"overrides\":[{\"entry\":\"a.json\"}]}",
			"exactly one" },
		{ "override remove false only", head + ",\"overrides\":[{\"entry\":\"a.json\","
			"\"remove\":false}]}", "exactly one" },
		{ "override file and text", head + ",\"overrides\":[{\"entry\":\"a.json\","
			"\"file\":\"settings.json\",\"text\":\"{}\"}]}", "exactly one" },
		{ "override text and remove", head + ",\"overrides\":[{\"entry\":\"a.json\","
			"\"text\":\"{}\",\"remove\":true}]}", "exactly one" },
		{ "override remove not boolean", head + ",\"overrides\":[{\"entry\":\"a.json\","
			"\"remove\":1}]}", "remove" },
		{ "override text not a string", head + ",\"overrides\":[{\"entry\":\"a.json\","
			"\"text\":{}}]}", "text" },
		{ "override file climbing out", head + ",\"overrides\":[{\"entry\":\"a.json\","
			"\"file\":\"../" + dir.path.filename().string() + "-outside.json\"}]}", "inside" },
		{ "override file with a drive", head + ",\"overrides\":[{\"entry\":\"a.json\","
			"\"file\":\"C:/Windows/win.ini\"}]}", "inside" },
		{ "override file missing", head + ",\"overrides\":[{\"entry\":\"a.json\","
			"\"file\":\"missing.json\"}]}", "could not read" },
		{ "second override bad", head + ",\"overrides\":[{\"entry\":\"a.json\",\"text\":\"{}\"},"
			"{\"entry\":\"b.json\"}]}", "override 2" },
		{ "same entry twice", head + ",\"overrides\":[{\"entry\":\"a.json\",\"text\":\"{}\"},"
			"{\"entry\":\"a.json\",\"remove\":true}]}", "already" },
		{ "too many overrides", manyOverrides, "more than" },
	};

	for (const Case &c : cases) {
		const RecipeParse parsed = recipeParse(c.json, dir.path);
		INFO(c.what << " -> " << parsed.err);
		CHECK(!parsed.recipe);
		CHECK(!parsed.err.empty());
		CHECK(parsed.err.find(c.expect) != std::string::npos);
	}

	/* One fewer override than the limit is accepted. */
	{
		std::string json = head + ",\"overrides\":[";
		for (int i = 0; i < MOD_AUTHOR_RECIPE_MAX_OVERRIDES; i++) {
			if (i) json += ",";
			json += "{\"entry\":\"notes/n" + std::to_string(i) + ".json\",\"text\":\"{}\"}";
		}
		json += "]}";
		const RecipeParse parsed = recipeParse(json, dir.path);
		INFO("limit error: " << parsed.err);
		CHECK(parsed.recipe);
	}

	/* Too large to be a recipe. */
	{
		std::string json = head + ",\"description\":\"";
		json.append(MOD_AUTHOR_RECIPE_MAX_BYTES, 'd');
		json += "\"}";
		const RecipeParse parsed = recipeParse(json, dir.path);
		CHECK(!parsed.recipe);
		CHECK(parsed.err.find("1 MiB") != std::string::npos);
	}

	/* A missing file, and no error buffer at all. */
	char err[128] = {};
	CHECK(modAuthorRecipeLoadFile((dir.path / "none.json").string().c_str(), err, sizeof(err))
		== nullptr);
	CHECK(std::string(err).find("could not be read") != std::string::npos);
	CHECK(modAuthorRecipeLoadFile(nullptr, err, sizeof(err)) == nullptr);
	CHECK(modAuthorRecipeParse("[]", 2, nullptr, nullptr, 0) == nullptr);

	std::error_code ec;
	fs::remove(dir.path.parent_path() / (dir.path.filename().string() + "-outside.json"), ec);
}

TEST_CASE("mod author recipe file becomes a saved, release-valid mod",
          "[modding][author][recipe]") {
	RecipeTempDir work;
	RecipeTempDir mods;
	const fs::path recipeDir = work.path / "rivet";
	fs::create_directories(recipeDir / "templates");
	fs::copy_file(fs::path(PD_SOURCE_DIR) / kRecipeWeaponTemplate,
		recipeDir / "templates" / "tri_weapon.pdweapon");

	const std::string settings =
		"{\n  \"schema\": \"pd.weapon_settings.v1\",\n"
		"  \"fire_cadence\": { \"value\": 9, \"unit\": \"centiseconds\" }\n}\n";
	recipeWriteFile(recipeDir / "settings.json", settings);
	recipeWriteFile(recipeDir / "recipe.json", R"({
  "schema": "pd2.mod_author.recipe.v1",
  "template_file": "templates/tri_weapon.pdweapon",
  "name": "Recipe Carbine",
  "author": "Recipe Test",
  "version": "0.9.0",
  "description": "Made from a recipe file.",
  "descriptor": { "posx": "30.0", "posz": "-45.0" },
  "overrides": [
    { "entry": "behavior/settings.json", "file": "settings.json" }
  ]
})");

	char err[256] = {};
	RecipePtr recipe(modAuthorRecipeLoadFile((recipeDir / "recipe.json").string().c_str(),
		err, sizeof(err)));
	INFO("recipe error: " << err);
	REQUIRE(recipe);

	const std::string modsRoot = mods.path.string();
	mod_author_request_t request;
	modAuthorRecipeToRequest(recipe.get(), nullptr, modsRoot.c_str(), &request);
	auto result = std::make_unique<mod_author_result_t>();
	const s32 code = modAuthorSave(&request, result.get(), err, sizeof(err));
	INFO("save error: " << err);
	REQUIRE(code == MOD_AUTHOR_OK);
	CHECK(std::string(result->catalog_id) == "mod:weapon_recipe_carbine");
	CHECK(std::string(result->mod_id) == "mod.weapon_recipe_carbine");

	REQUIRE(assetArchiveValidateFile(result->archive_path, ASSET_ARCHIVE_VALIDATE_RELEASE,
		err, sizeof(err)) == 0);
	const std::string archive = recipeReadFile(result->archive_path);
	CHECK(recipeEntry(archive, "behavior/settings.json") == settings);
	CHECK(recipeEntry(archive, "weapon.ini").find("name = Recipe Carbine") != std::string::npos);
	CHECK(recipeEntry(archive, "weapon.ini").find("posx = 30.0\nposz = -45.0\n")
		!= std::string::npos);

	const std::string manifest = recipeReadFile(result->manifest_path);
	char field[256] = {};
	REQUIRE(modManifestReadString(manifest.data(), manifest.size(), "author", field,
		sizeof(field)) == 1);
	CHECK(std::string(field) == "Recipe Test");
	REQUIRE(modManifestReadString(manifest.data(), manifest.size(), "version", field,
		sizeof(field)) == 1);
	CHECK(std::string(field) == "0.9.0");
	REQUIRE(modManifestReadString(manifest.data(), manifest.size(), "description", field,
		sizeof(field)) == 1);
	CHECK(std::string(field) == "Made from a recipe file.");

	/* The provenance records the tool, and no folder of the machine it was
	 * made on. */
	const std::string provenance = recipeEntry(archive, "_meta/provenance.json");
	CHECK(provenance.find("mod-author-recipe") != std::string::npos);
	CHECK(provenance.find(work.path.filename().string()) == std::string::npos);

	/* A recipe that names a catalog id cannot be saved without a path. */
	const RecipeParse byId = recipeParse(
		"{\"schema\":\"pd2.mod_author.recipe.v1\",\"template\":\"base:falcon2\",\"name\":\"X\"}",
		recipeDir);
	REQUIRE(byId.recipe);
	modAuthorRecipeToRequest(byId.recipe.get(), nullptr, modsRoot.c_str(), &request);
	CHECK(request.template_path == nullptr);
	CHECK(modAuthorSave(&request, result.get(), err, sizeof(err)) == MOD_AUTHOR_ERR_ARGS);
}
