#include "catch.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

extern "C" {
#include "effect_dependencies.h"
#include "modarchive.h"
#include "pdeffect_source.h"
void testStubFsFileLoadWith(const char *path, const void *bytes, u32 size);
}

TEST_CASE("effect catalog identity uses the fixed-storage conformance contract",
	"[modding][pdxxx][effect_dependencies][t-assets-019]")
{
	const std::string maxId = "a:" + std::string(61, 'x');
	const std::string overId = "a:" + std::string(62, 'x');
	REQUIRE(maxId.size() == CATALOG_ID_LEN - 1);
	REQUIRE(overId.size() == CATALOG_ID_LEN);
	REQUIRE(pdEffectCatalogIdValid(maxId.c_str()) == 1);
	REQUIRE(pdEffectCatalogIdValid(overId.c_str()) == 0);
	REQUIRE(pdEffectCatalogIdValid("_bad:local") == 0);
	REQUIRE(pdEffectCatalogIdValid("good:-local") == 0);
	REQUIRE(pdEffectCatalogIdValid("good:local:extra") == 0);
	REQUIRE(pdEffectCatalogIdValid("Good_1:local.name-2") == 1);
	auto parseIdentity = [](const std::string &id) {
		const std::string descriptor = "[effect]\ncatalog_id = " + id
			+ "\nname = Identity\nschema = pd.effect_graph.v1\neffect_key = x\n"
			  "target_key = scene\neffect_file = effect.graph.json\n"
			  "timeline_file =\nshader_id =\nintensity = 1\n";
		const std::string graph = "{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\""
			+ id + "\",\"nodes\":[{\"id\":\"n\",\"kind\":\"effect.screen\",\"params\":{}}]}";
		pd_effect_source_info_t info = {};
		char error[256] = {};
		return pdEffectSourceParse(descriptor.data(), descriptor.size(), graph.data(),
			graph.size(), id.c_str(), &info, error, sizeof(error));
	};
	REQUIRE(parseIdentity(maxId) == 1);
	REQUIRE(parseIdentity(overId) == 0);
}

TEST_CASE("effect dependency enumeration is typed deduplicated public source",
	"[modding][pdxxx][effect_dependencies][t-assets-019]")
{
	const auto path = std::filesystem::temp_directory_path() /
		("pd2-effect-deps-" + std::to_string(
			std::chrono::high_resolution_clock::now().time_since_epoch().count()) +
			".pdeffect");
	const std::string descriptor =
		"[effect]\n"
		"catalog_id = modx:typed_fx\n"
		"name = Typed FX\n"
		"schema = pd.effect_graph.v1\n"
		"effect_key = impact\n"
		"target_key = scene\n"
		"effect_file = effect.graph.json\n"
		"timeline_file =\n"
		"shader_id =\n"
		"intensity = 1\n";
	const std::string graph =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:typed_fx\","
		"\"nodes\":["
		"{\"id\":\"g\",\"kind\":\"effect.glow\",\"params\":{"
		"\"material_ref\":\"modx:glow_mat\"}},"
		"{\"id\":\"g2\",\"kind\":\"effect.tint\",\"params\":{"
		"\"material_ref\":\"modx:glow_mat\"}},"
		"{\"id\":\"p\",\"kind\":\"effect.particle\",\"params\":{"
		"\"texture_ref\":\"modx:spark_tex\"}},"
		"{\"id\":\"x\",\"kind\":\"effect.explosion\",\"params\":{"
		"\"audio_catalog_id\":\"modx:blast_sfx\"}}],"
		"\"edges\":[{\"from\":\"g\",\"to\":\"g2\"},{\"from\":\"g2\",\"to\":\"p\"},{\"from\":\"p\",\"to\":\"x\"}]}";

	mod_archive_writer_t *writer = modArchiveBegin(path.string().c_str());
	REQUIRE(writer != nullptr);
	REQUIRE(modArchiveAddFileMem(writer, "effect.ini", descriptor.data(),
		static_cast<u32>(descriptor.size())) == MODARCHIVE_OK);
	REQUIRE(modArchiveAddFileMem(writer, "effect.graph.json", graph.data(),
		static_cast<u32>(graph.size())) == MODARCHIVE_OK);
	REQUIRE(modArchiveFinish(writer) == MODARCHIVE_OK);

	effect_dependency_list_t deps = {};
	char error[256] = {};
	const s32 count = effectDependenciesCollectArchiveFile(path.string().c_str(),
		"modx:typed_fx", &deps, error, sizeof(error));
	INFO(error);
	REQUIRE(count == 3);
	REQUIRE(deps.items[0].type == ASSET_MATERIAL);
	REQUIRE(std::string(deps.items[0].catalog_id) == "modx:glow_mat");
	REQUIRE(deps.items[1].type == ASSET_TEXTURE);
	REQUIRE(std::string(deps.items[1].catalog_id) == "modx:spark_tex");
	REQUIRE(deps.items[2].type == ASSET_AUDIO);
	REQUIRE(std::string(deps.items[2].catalog_id) == "modx:blast_sfx");

	/* Archive-qualified/VFS paths use the authoritative fs loader after the
	 * ordinary absolute-file attempt. */
	std::ifstream input(path, std::ios::binary);
	std::vector<char> archive((std::istreambuf_iterator<char>(input)), {});
	input.close();
	testStubFsFileLoadWith("outer.pdmod::nested.pdeffect", archive.data(),
		static_cast<u32>(archive.size()));
	effect_dependency_list_t nested = {};
	REQUIRE(effectDependenciesCollectArchiveFile(
		"outer.pdmod::nested.pdeffect", "modx:typed_fx", &nested,
		error, sizeof(error)) == 3);
	effectDependenciesFree(&nested);
	testStubFsFileLoadWith(nullptr, nullptr, 0);
	effectDependenciesFree(&deps);

	std::string wide =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:typed_fx\",\"nodes\":[";
	for (int i = 0; i < 50; i++) {
		if (i) wide += ',';
		wide += "{\"id\":\"n" + std::to_string(i)
			+ "\",\"kind\":\"effect.particle\",\"params\":{\"texture_ref\":\"modx:tex_"
			+ std::to_string(i) + "\",\"material_ref\":\"modx:mat_"
			+ std::to_string(i) + "\"}}";
	}
	wide += "]}";
	REQUIRE(modArchiveReplaceFileMem(path.string().c_str(), "effect.graph.json",
		wide.data(), static_cast<u32>(wide.size())) == MODARCHIVE_OK);
	effect_dependency_list_t wideDeps = {};
	REQUIRE(effectDependenciesCollectArchiveFile(path.string().c_str(),
		"modx:typed_fx", &wideDeps, error, sizeof(error)) == 100);
	REQUIRE(wideDeps.capacity >= 100);
	effectDependenciesFree(&wideDeps);

	const std::vector<std::string> rejected = {
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:typed_fx\",\"nodes\":[{\"id\":\"n\",\"kind\":\"effect.screen\",\"params\":{\"texture_ref\":\"modx:t\"}}]}",
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:typed_fx\",\"nodes\":[{\"id\":\"n\",\"kind\":\"effect.particle\",\"params\":{\"texture_id\":\"modx:t\"}}]}",
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:typed_fx\",\"nodes\":[{\"id\":\"n\",\"kind\":\"effect.particle\",\"params\":{\"texture_ref\":7}}]}",
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:typed_fx\",\"nodes\":[{\"id\":\"n\",\"kind\":\"effect.particle\",\"params\":{\"texture_ref\":\"invalid id\"}}]}",
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:typed_fx\",\"nodes\":[{\"id\":\"n\",\"kind\":\"effect.explosion\",\"params\":{\"audio_catalog_id\":\"\"}}]}",
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:typed_fx\",\"nodes\":[{\"id\":\"n\",\"kind\":\"effect.explosion\",\"params\":{\"audio_catalog_id\":null}}]}",
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:typed_fx\",\"nodes\":[{\"id\":\"n\",\"kind\":\"effect.glow\",\"params\":{\"material_ref\":\"\"}}]}",
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:typed_fx\",\"nodes\":[{\"id\":\"n\",\"kind\":\"effect.glow\",\"params\":{\"material_ref\":null}}]}",
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:typed_fx\",\"nodes\":[{\"id\":\"n\",\"kind\":\"effect.particle\",\"params\":{\"texture_ref\":\"\"}}]}",
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:typed_fx\",\"nodes\":[{\"id\":\"n\",\"kind\":\"effect.particle\",\"params\":{\"texture_ref\":null}}]}"
	};
	for (const std::string &bad : rejected) {
		REQUIRE(modArchiveReplaceFileMem(path.string().c_str(), "effect.graph.json",
			bad.data(), static_cast<u32>(bad.size())) == MODARCHIVE_OK);
		effect_dependency_list_t invalid = {};
		REQUIRE(effectDependenciesCollectArchiveFile(path.string().c_str(),
			"modx:typed_fx", &invalid, error, sizeof(error)) == -1);
		effectDependenciesFree(&invalid);
	}

	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}
