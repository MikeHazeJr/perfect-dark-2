#include "catch.hpp"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "asset_path_contract.h"
#include "assetcatalog.h"
#include "weapon_graph_archive.h"
}

static std::string pathContractRead(const char *path)
{
	std::ifstream in(path, std::ios::binary);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

TEST_CASE("public asset path copy and join preserve boundaries or fail empty",
	"[catalog][pdxxx][path][T-CATALOG-003]")
{
	for (size_t length : {size_t(127), size_t(128), size_t(FS_MAXPATH - 1)}) {
		std::string source(length, 'p');
		char out[FS_MAXPATH];
		REQUIRE(assetPathCopyChecked(out, sizeof(out), source.c_str()) == 1);
		REQUIRE(std::strlen(out) == length);
		REQUIRE(std::string(out) == source);
	}

	std::string over(FS_MAXPATH, 'x');
	char rejected[FS_MAXPATH];
	std::memset(rejected, 'q', sizeof(rejected));
	REQUIRE(assetPathCopyChecked(rejected, sizeof(rejected), over.c_str()) == 0);
	REQUIRE(rejected[0] == '\0');

	for (size_t total : {size_t(127), size_t(128), size_t(FS_MAXPATH - 1)}) {
		const std::string member = "effect.json";
		const std::string archive(total - 2 - member.size(), 'a');
		char out[FS_MAXPATH];
		REQUIRE(assetPathJoinChecked(out, sizeof(out), archive.c_str(), "::",
			member.c_str()) == 1);
		REQUIRE(std::strlen(out) == total);
		REQUIRE(std::string(out).substr(total - member.size()) == member);
	}

	const std::string member = "effect.json";
	const std::string too_long(FS_MAXPATH - 2 - member.size(), 'a');
	REQUIRE(assetPathJoinChecked(rejected, sizeof(rejected), too_long.c_str(),
		"::", member.c_str()) == 0);
	REQUIRE(rejected[0] == '\0');
}

struct PathIngressFixture {
	const char *name;
	const char *separator;
	s32 archive;
};

static s32 qualifyFixturePath(const PathIngressFixture &fixture, char *out,
	size_t outCap, const std::string &root, const std::string &member)
{
	return fixture.archive
		? assetPathQualifyArchiveChecked(out, outCap, root.c_str(), member.c_str())
		: assetPathQualifyFilesystemChecked(out, outCap, root.c_str(), member.c_str());
}

TEST_CASE("standalone pdmod and received-network qualification preserve exact paths",
	"[catalog][network][pdmod][path][behavior][T-CATALOG-003]")
{
	const PathIngressFixture ingresses[] = {
		{"standalone typed archive", "/", 0},
		{"nested pdmod archive", "::", 1},
		{"received network component", "/", 0},
	};
	struct FamilyPath {
		const char *field;
		const char *key;
	};
	const FamilyPath familyPaths[] = {
		{"skin.skin_file", "skin_file"},
		{"skin.texture_file", "texture_file"},
		{"skin.swatches_file", "swatches_file"},
		{"weapon.model_file", "model_file"},
		{"weapon.behavior_graph", "behavior_graph"},
		{"weapon.primary_graph", "primary_graph"},
		{"weapon.secondary_graph", "secondary_graph"},
		{"weapon.shared_context", "shared_context"},
		{"weapon.settings_file", "settings_file"},
		{"weapon.variables_file", "variables_file"},
		{"weapon.presentation_file", "presentation_file"},
		{"projectile.model_file", "model_file"},
		{"projectile.behavior_graph", "behavior_graph"},
		{"entity.model_file", "model_file"},
		{"entity.behavior_graph", "behavior_graph"},
		{"texture.file_path", "file_path"},
		{"prop.prop_file", "prop_file"},
		{"prop.model_file", "model_file"},
		{"prop.behavior_graph", "behavior_graph"},
		{"gamemode.rules_file", "rules_file"},
		{"audio.file_path", "file_path"},
		{"hud.texture_file", "texture_file"},
		{"hud.layout_file", "layout_file"},
		{"effect.effect_file", "effect_file"},
		{"effect.timeline_file", "timeline_file"},
		{"material.material_file", "material_file"},
		{"vehicle.model_file", "model_file"},
		{"vehicle.physics_file", "physics_file"},
		{"vehicle.behavior_graph", "behavior_graph"},
		{"mission.objectives_file", "objectives_file"},
		{"mission.mission_graph_file", "mission_graph_file"},
		{"theme.theme_file", "theme_file"},
		{"lang.strings_file", "strings_file"},
		{"bot_profile.profile_file", "profile_file"},
	};
	REQUIRE(sizeof(familyPaths) / sizeof(familyPaths[0]) == 34);

	for (const PathIngressFixture &ingress : ingresses) {
		for (const FamilyPath &path : familyPaths) {
			INFO("ingress=" << ingress.name << " field=" << path.field);
			REQUIRE(assetPathKeyIsSource(path.key) == 1);
			for (size_t total : {size_t(127), size_t(128),
					size_t(FS_MAXPATH - 1)}) {
				const std::string member = "payload.bin";
				const size_t rootLength = total - std::strlen(ingress.separator)
					- member.size();
				const std::string root(rootLength, ingress.archive ? 'a' : 'r');
				const std::string expected = root + ingress.separator + member;
				char qualified[FS_MAXPATH];
				char catalogPath[FS_MAXPATH];
				char providerPath[FS_MAXPATH];
				char runtimePath[FS_MAXPATH];
				REQUIRE(qualifyFixturePath(ingress, qualified,
					sizeof(qualified), root, member) == 1);
				REQUIRE(std::string(qualified) == expected);
				/* These are the exact checked copies made by catalog field,
				 * FileProvider admission, and runtime binding respectively. */
				REQUIRE(assetPathCopyChecked(catalogPath, sizeof(catalogPath),
					qualified) == 1);
				REQUIRE(assetPathCopyChecked(providerPath, sizeof(providerPath),
					catalogPath) == 1);
				REQUIRE(assetPathCopyChecked(runtimePath, sizeof(runtimePath),
					providerPath) == 1);
				REQUIRE(std::string(catalogPath) == expected);
				REQUIRE(std::string(providerPath) == expected);
				REQUIRE(std::string(runtimePath) == expected);
			}

			const std::string member = "payload.bin";
			const size_t overTotal = FS_MAXPATH;
			const std::string root(overTotal - std::strlen(ingress.separator)
				- member.size(), 'x');
			char qualified[FS_MAXPATH];
			char catalogPath[FS_MAXPATH] = "catalog-before";
			char providerPath[FS_MAXPATH] = "provider-before";
			char runtimePath[FS_MAXPATH] = "runtime-before";
			REQUIRE(qualifyFixturePath(ingress, qualified, sizeof(qualified),
				root, member) == 0);
			REQUIRE(qualified[0] == '\0');
			REQUIRE(std::string(catalogPath) == "catalog-before");
			REQUIRE(std::string(providerPath) == "provider-before");
			REQUIRE(std::string(runtimePath) == "runtime-before");
		}
	}

	char checked[FS_MAXPATH];
	REQUIRE(assetPathQualifyFilesystemChecked(checked, sizeof(checked),
		"mods/component", "nested/source.bin") == 1);
	REQUIRE(std::string(checked) == "mods/component/nested/source.bin");
	REQUIRE(assetPathQualifyFilesystemChecked(checked, sizeof(checked),
		"mods/component", "../escape.bin") == 0);
	REQUIRE(assetPathQualifyArchiveChecked(checked, sizeof(checked),
		"mods/content.pdmod::assets/item.pdmesh", "../escape.bin") == 0);
	REQUIRE(assetPathQualifyArchiveChecked(checked, sizeof(checked),
		"ignored", "mods/content.pdmod::../escape.bin") == 0);
	REQUIRE(assetPathQualifyArchiveChecked(checked, sizeof(checked),
		"ignored", "mods/content.pdmod::..\\escape.bin") == 0);
}

TEST_CASE("all 34 catalog source fields use the repository path capacity",
	"[catalog][pdxxx][path][capacity][T-CATALOG-003]")
{
#define PATH_CAP(member) sizeof(((asset_entry_t *)nullptr)->ext.member)
	const std::vector<size_t> capacities = {
		PATH_CAP(skin.skin_file), PATH_CAP(skin.texture_file),
		PATH_CAP(skin.swatches_file), PATH_CAP(weapon.model_file),
		PATH_CAP(weapon.behavior_graph), PATH_CAP(weapon.primary_graph),
		PATH_CAP(weapon.secondary_graph), PATH_CAP(weapon.shared_context),
		PATH_CAP(weapon.settings_file), PATH_CAP(weapon.variables_file),
		PATH_CAP(weapon.presentation_file), PATH_CAP(projectile.model_file),
		PATH_CAP(projectile.behavior_graph), PATH_CAP(entity.model_file),
		PATH_CAP(entity.behavior_graph), PATH_CAP(texture.file_path),
		PATH_CAP(prop.prop_file), PATH_CAP(prop.model_file),
		PATH_CAP(prop.behavior_graph), PATH_CAP(gamemode.rules_file),
		PATH_CAP(audio.file_path), PATH_CAP(hud.texture_file),
		PATH_CAP(hud.layout_file), PATH_CAP(effect.effect_file),
		PATH_CAP(effect.timeline_file), PATH_CAP(material.material_file),
		PATH_CAP(vehicle.model_file), PATH_CAP(vehicle.physics_file),
		PATH_CAP(vehicle.behavior_graph), PATH_CAP(mission.objectives_file),
		PATH_CAP(mission.mission_graph_file), PATH_CAP(theme.theme_file),
		PATH_CAP(lang.strings_file), PATH_CAP(bot_profile.profile_file),
	};
#undef PATH_CAP
	REQUIRE(capacities.size() == 34);
	for (size_t capacity : capacities) REQUIRE(capacity == FS_MAXPATH);

	/* Descriptive metadata remains semantically sized. */
	REQUIRE(sizeof(((asset_entry_t *)nullptr)->ext.audio.voice_context) == 128);
	REQUIRE(sizeof(((asset_entry_t *)nullptr)->ext.effect.shader_id) == 64);
}

TEST_CASE("every supported ingress path key rejects an over-capacity value",
	"[catalog][network][pdmod][path][T-CATALOG-003]")
{
	const std::string over(FS_MAXPATH, 'n');
	char out[FS_MAXPATH];
	REQUIRE(ASSET_PATH_SOURCE_KEY_COUNT > 90);
	for (size_t i = 0; i < ASSET_PATH_SOURCE_KEY_COUNT; i++) {
		INFO("path key: " << g_AssetPathSourceKeys[i]);
		REQUIRE(assetPathKeyIsSource(g_AssetPathSourceKeys[i]) == 1);
		REQUIRE(assetPathCopyChecked(out, sizeof(out), over.c_str()) == 0);
		REQUIRE(out[0] == '\0');
	}
	for (const char *alias : {"bodyfile", "headfile", "behavior_graph",
			"primary_graph", "secondary_graph", "shared_context", "scene",
			"geometry", "file_path"}) {
		REQUIRE(assetPathKeyIsSource(alias) == 1);
	}
	REQUIRE(assetPathKeyIsSource("voice_context") == 0);
}

TEST_CASE("every public ingestion boundary uses checked path-specific APIs",
	"[catalog][pdxxx][path][production][T-CATALOG-003]")
{
	const std::string scanner = pathContractRead("port/src/assetcatalog_scanner.c");
	const std::string walker = pathContractRead("port/src/loader_walker_common.c");
	const std::string network = pathContractRead("port/src/net/netdistrib.c");
	const std::string runtime = pathContractRead("port/src/asset_runtime.c");
	const std::string provider = pathContractRead("port/src/assetprovider_file.c");
	const std::string compiler = pathContractRead("port/src/modasset_compiler.c");
	const std::string loader_pool = pathContractRead("port/src/loader_pool.c");
	const std::string body_manager = pathContractRead("port/src/catalog_mgr_bodies.c");
	const std::string head_manager = pathContractRead("port/src/catalog_mgr_heads.c");
	const std::string ingress_fixtures = pathContractRead(
		"tools/smoke-verify/lib/Catalog-Ingress-Fixtures.ps1");
	const std::string ingress_smoke = pathContractRead(
		"tools/smoke-verify/tests/catalog_three_ingress_boundaries_smoke.json");

	REQUIRE(scanner.find("assetPathJoinChecked") != std::string::npos);
	REQUIRE(scanner.find("ASSET.PATH.REJECT") != std::string::npos);
	REQUIRE(walker.find("loaderWalkerEnvelopePathCopy") != std::string::npos);
	REQUIRE(walker.find("assetPathJoinChecked") != std::string::npos);
	REQUIRE(network.find("DISTRIB.ASSET.PATH.REJECT") != std::string::npos);
	REQUIRE(network.find("static s32 populateExtFromIni") != std::string::npos);
	REQUIRE(network.find("distribQualifyIniSourcePaths(ini, dirpath)") !=
		std::string::npos);
	REQUIRE(network.find("assetPathCopyChecked(ini->pairs[i].value") !=
		std::string::npos);
	REQUIRE(network.find("pdcaExtractArchiveBegin") != std::string::npos);
	REQUIRE(network.find("pdcaExtractTransactionCommit") != std::string::npos);
	REQUIRE(network.find("pdcaExtractTransactionRollback") != std::string::npos);
	REQUIRE(network.find("assetCatalogScanExternalLayoutFolderDeferred") !=
		std::string::npos);
	REQUIRE(network.find("if (registered <= 0)") != std::string::npos);
	REQUIRE(network.find("catalogLoadInit();") != std::string::npos);
	REQUIRE(network.find("transactional extract failed") != std::string::npos);
	REQUIRE(network.find("destination='%s' preserved") != std::string::npos);
	REQUIRE(network.find("return assetPathKeyIsSource(key);") !=
		std::string::npos);
	REQUIRE(network.find("if (prior) *e = prior_entry;") != std::string::npos);
	REQUIRE(network.find("if (existing) *e = preserved_entry;") !=
		std::string::npos);
	REQUIRE(network.find("else assetCatalogUnregister(slot->id);") !=
		std::string::npos);
	REQUIRE(runtime.find("assetPathCopyChecked") != std::string::npos);
	REQUIRE(provider.find("rejecting over-capacity public asset path") !=
		std::string::npos);
	REQUIRE(compiler.find("(size_t)wrote >= out_n") != std::string::npos);

	/* B-1043: accepted siblings cannot erase a recognized rejection, and the
	 * scanner owns rollback of rows, edges, and provider-path interning before
	 * the network layer decides whether to commit the staged filesystem tree. */
	REQUIRE(scanner.find("external_scan_transaction_t") != std::string::npos);
	REQUIRE(scanner.find("externalScanTransactionRollback") != std::string::npos);
	REQUIRE(scanner.find("catalogDepSnapshotRestore") != std::string::npos);
	REQUIRE(scanner.find("fileProviderCheckpointRestore") != std::string::npos);
	REQUIRE(scanner.find("assetCatalogRestoreCustomWeaponSlots") !=
		std::string::npos);
	REQUIRE(scanner.find("stageTableSnapshotRestore") != std::string::npos);
	REQUIRE(scanner.find("loaderPoolSnapshotRestore") != std::string::npos);
	REQUIRE(scanner.find("catalogManagerBodySnapshotRestore") !=
		std::string::npos);
	REQUIRE(scanner.find("catalogManagerHeadSnapshotRestore") !=
		std::string::npos);
	REQUIRE(loader_pool.find("struct loader_pool_snapshot") !=
		std::string::npos);
	REQUIRE(loader_pool.find("memcpy(s_Guncmds, snapshot->guncmds") !=
		std::string::npos);
	REQUIRE(body_manager.find("struct catalog_manager_body_snapshot") !=
		std::string::npos);
	REQUIRE(head_manager.find("struct catalog_manager_head_snapshot") !=
		std::string::npos);
	REQUIRE(ingress_fixtures.find("Write-CatalogIngressPdcaEntries") !=
		std::string::npos);
	REQUIRE(ingress_fixtures.find("ingress:mixed_anim") !=
		std::string::npos);
	REQUIRE(ingress_fixtures.find("net_mixed.pdca") != std::string::npos);
	REQUIRE(ingress_fixtures.find("Get-CatalogIngressFamilySpecs") !=
		std::string::npos);
	REQUIRE(ingress_fixtures.find("$familySpecs.Count -ne 27") !=
		std::string::npos);
	REQUIRE(ingress_fixtures.find("probe-list.txt") != std::string::npos);
	REQUIRE(ingress_fixtures.find("27 families x 3 ingresses") !=
		std::string::npos);
	REQUIRE(ingress_smoke.find("B-1043") != std::string::npos);
	REQUIRE(ingress_smoke.find("loader_absent=1") != std::string::npos);
	REQUIRE(ingress_smoke.find("loaded 243 line") != std::string::npos);
	REQUIRE(scanner.find("return rejected ? -(count + 1) : count;") !=
		std::string::npos);
	REQUIRE(scanner.find("if (rejected)") != std::string::npos);
	REQUIRE(scanner.find("CATALOG.SCAN.TRANSACTION.ROLLBACK") !=
		std::string::npos);

	/* B-1049: BODY/HEAD admission must resolve an actually present public
	 * mesh member inside the same FS_MAXPATH contract used by the catalog,
	 * rather than preflighting a shorter assumed model.obj suffix and then
	 * publishing a row backed by an oversized temporary path. */
	REQUIRE(scanner.find("sourceModelPathFromMeshArchive(mesh,\n\t\t\t\tsource_probe, sizeof(source_probe))")
		!= std::string::npos);
	REQUIRE(scanner.find("char source_path[FS_MAXPATH + 32]") ==
		std::string::npos);
	REQUIRE(scanner.find("return assetPathJoinChecked(out, outsz, mesh_archive, \"::\", \"model.obj\")")
		== std::string::npos);
	REQUIRE(scanner.find("out[0] = '\\0';\n\treturn 0;") !=
		std::string::npos);
}
