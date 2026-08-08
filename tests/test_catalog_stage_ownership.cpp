#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

extern "C" {
#include "assetcatalog.h"
#include "catalog_stage_ownership.h"
}

static std::string stageOwnershipReadText(const char *path)
{
	std::ifstream in(path, std::ios::binary);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

TEST_CASE("stage owner ledger is idempotent and cannot release another owner",
	"[catalog][lifecycle][ownership][T-CATALOG-002]")
{
	asset_entry_t entry = {};
	entry.ref_count = 1; /* explicit UI/theme/manifest owner */

	REQUIRE_FALSE(catalogStageOwnershipHasRef(&entry));
	REQUIRE(catalogStageOwnershipRelease(&entry) == 0);
	REQUIRE(entry.ref_count == 1);

	REQUIRE(catalogStageOwnershipAcquire(&entry) == 1);
	REQUIRE(catalogStageOwnershipHasRef(&entry));
	REQUIRE(catalogStageOwnershipAcquire(&entry) == 0);
	REQUIRE(entry.stage_ref_count == 1);
	REQUIRE(entry.ref_count == 1);

	REQUIRE(catalogStageOwnershipRelease(&entry) == 1);
	REQUIRE_FALSE(catalogStageOwnershipHasRef(&entry));
	REQUIRE(catalogStageOwnershipRelease(&entry) == 0);
	REQUIRE(entry.ref_count == 1);
}

TEST_CASE("explicit pdtheme dependency closure is outside stage unload ownership",
	"[catalog][pdtheme][lifecycle][ownership][T-CATALOG-002]")
{
	asset_entry_t closure[5] = {};

	/* Parent theme, UI, font, SFX, and music each hold one explicit lifecycle
	 * reference after transactional activation. A base-stage transition owns
	 * none of them and therefore cannot select or decrement any closure row. */
	for (auto &entry : closure) {
		entry.ref_count = 1;
		REQUIRE_FALSE(catalogStageOwnershipHasRef(&entry));
		REQUIRE(catalogStageOwnershipRelease(&entry) == 0);
		REQUIRE(entry.ref_count == 1);
	}

	/* If a stage independently acquires an overlapping row, dropping only that
	 * owner leaves the explicit reference durable. */
	closure[0].ref_count++;
	REQUIRE(catalogStageOwnershipAcquire(&closure[0]) == 1);
	REQUIRE(catalogStageOwnershipRelease(&closure[0]) == 1);
	closure[0].ref_count--;
	REQUIRE(closure[0].ref_count == 1);
}

TEST_CASE("production stage diff uses only its owner-scoped lifecycle API",
	"[catalog][lifecycle][ownership][production][T-CATALOG-002]")
{
	const std::string load = stageOwnershipReadText("port/src/assetcatalog_load.c");
	const std::string header = stageOwnershipReadText("port/include/assetcatalog_load.h");
	const std::string lv = stageOwnershipReadText("src/game/lv.c");
	const auto diff = load.find("s32 catalogComputeStageDiff(");

	REQUIRE(diff != std::string::npos);
	REQUIRE(load.substr(diff).find("catalogStageOwnershipHasRef(e)") !=
		std::string::npos);
	REQUIRE(load.substr(diff).find("e->load_state >= ASSET_STATE_LOADED") ==
		std::string::npos);
	REQUIRE(load.find("s32 catalogLoadStageAsset(") != std::string::npos);
	REQUIRE(load.find("void catalogReleaseStageAsset(") != std::string::npos);
	REQUIRE(load.find(
		"entry->type == ASSET_THEME || entry->type == ASSET_WEAPON") !=
		std::string::npos);
	REQUIRE(load.find("per_ref_deps_released") != std::string::npos);
	REQUIRE(header.find("loaded owners never enter toUnload") !=
		std::string::npos);
	REQUIRE(lv.find("catalogLoadStageAsset(lvCatalogAssetTypeForId") !=
		std::string::npos);
	REQUIRE(lv.find("catalogReleaseStageAsset(lvCatalogAssetTypeForId") !=
		std::string::npos);
}

TEST_CASE("lazy stage payload consumers join the same owner ledger",
	"[catalog][lifecycle][ownership][propagation][T-CATALOG-002]")
{
	const char *stage_consumers[] = {
		"src/game/bg.c",
		"src/game/lv.c",
		"src/game/bondgun.c",
		"src/game/modeldef.c",
		"src/game/player.c",
		"port/src/catalog_mgr_bodies.c",
		"port/src/catalog_mgr_heads.c",
		"port/src/mod.c",
		"port/src/scenario_source_runtime.c",
	};

	for (const char *path : stage_consumers) {
		const std::string source = stageOwnershipReadText(path);
		INFO(path);
		REQUIRE(source.find("catalogLoadStageAsset(") != std::string::npos);
		REQUIRE(source.find("catalogLoadTypedAsset(") == std::string::npos);
	}

	/* Explicit owners keep the generic paired lifecycle boundary. */
	const std::string theme =
		stageOwnershipReadText("port/fast3d/pdgui_theme_loader.cpp");
	const std::string screens = stageOwnershipReadText("port/src/screenmfst.c");
	const std::string manifests =
		stageOwnershipReadText("port/src/net/netmanifest.c");
	REQUIRE(theme.find("catalogLoadTypedAsset(ASSET_THEME") != std::string::npos);
	REQUIRE(theme.find("catalogReleaseTypedAsset(ASSET_THEME") != std::string::npos);
	REQUIRE(screens.find("catalogLoadTypedAsset(") != std::string::npos);
	REQUIRE(screens.find("catalogReleaseTypedAsset(") != std::string::npos);
	REQUIRE(manifests.find("catalogLoadTypedAsset(") != std::string::npos);
	REQUIRE(manifests.find("catalogReleaseTypedAsset(") != std::string::npos);
}

TEST_CASE("ordinary theme lifecycle proves transition and shuts down before backend teardown",
	"[catalog][pdtheme][lifecycle][production][T-CATALOG-002]")
{
	const std::string backend =
		stageOwnershipReadText("port/fast3d/pdgui_backend.cpp");
	const std::string loader =
		stageOwnershipReadText("port/fast3d/pdgui_theme_loader.cpp");
	const std::string main = stageOwnershipReadText("port/src/main.c");
	const std::string pdmain = stageOwnershipReadText("port/src/pdmain.c");
	const auto loader_shutdown = backend.find("pdguiThemeLoaderShutdown();");
	const auto theme_shutdown = backend.find("pdguiThemeShutdown();");

	REQUIRE(loader_shutdown != std::string::npos);
	REQUIRE(theme_shutdown != std::string::npos);
	REQUIRE(loader_shutdown < theme_shutdown);
	REQUIRE(loader.find("pdthemeActivationShutdown(&s_ActivationState") !=
		std::string::npos);
	REQUIRE(loader.find(
		"pdguiThemeLoaderLogActiveOwnership(\"shutdown_release_before\")") !=
		std::string::npos);
	REQUIRE(loader.find("CATALOG.OWNER.PROOF: label=%s id=%s ref=%d stage_ref=%d") !=
		std::string::npos);
	REQUIRE(main.find("--debug-theme-stage-transition-proof") !=
		std::string::npos);
	REQUIRE(main.find("mainChangeToStage(STAGE_CREDITS);") !=
		std::string::npos);
	REQUIRE(main.find("\"post_stage_transition\"") != std::string::npos);
	REQUIRE(pdmain.find("bootDebugThemeStageTransitionProofTick") !=
		std::string::npos);
}
