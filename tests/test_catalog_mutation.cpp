#include "catch.hpp"
#include "assetcatalog.h"
#include "assetcatalog_mutation.h"
#include "catalog_mutation_support.h"

namespace {
struct Catalog {
    Catalog() { catalogMutationTestReset(); assetCatalogInit(); }
    ~Catalog() { catalogMutationTestReset(); assetCatalogClear(); }
    asset_entry_t *add(const char *id, bool bundled = false) {
        auto *entry = assetCatalogRegister(id, ASSET_FONT);
        REQUIRE(entry != nullptr);
        entry->bundled = bundled ? 1 : 0;
        return entry;
    }
};
}

TEST_CASE("Checked catalog toggle distinguishes absent and already satisfied intent", "[catalog][mutation]")
{
    Catalog catalog;
    auto *entry = catalog.add("test:font");
    REQUIRE(assetCatalogSetEnabledChecked("missing", 0) == CATALOG_CHANGE_NOT_FOUND);
    REQUIRE(assetCatalogSetEnabledChecked(nullptr, 0) == CATALOG_CHANGE_NOT_FOUND);
    REQUIRE(assetCatalogSetEnabledChecked(entry->id, 1) == CATALOG_CHANGE_UNCHANGED);
    REQUIRE(g_CatalogMutationControls.invalidate_calls == 0);
    REQUIRE(g_CatalogMutationControls.teardown_calls == 0);
}

TEST_CASE("Checked catalog toggle rejects preflight without changing the selected bit", "[catalog][mutation]")
{
    Catalog catalog;
    auto *entry = catalog.add("test:font");
    g_CatalogMutationControls.preflight_ok = 0;
    REQUIRE(assetCatalogSetEnabledChecked(entry->id, 0) == CATALOG_CHANGE_PREFLIGHT_FAILED);
    REQUIRE(entry->enabled == 1);
    REQUIRE(g_CatalogMutationControls.invalidate_calls == 0);
    REQUIRE(g_CatalogMutationControls.teardown_calls == 0);
}

TEST_CASE("Checked catalog toggle reports dependent invalidation rejection", "[catalog][mutation]")
{
    Catalog catalog;
    auto *entry = catalog.add("test:font");
    g_CatalogMutationControls.invalidate_ok = 0;
    REQUIRE(assetCatalogSetEnabledChecked(entry->id, 0) == CATALOG_CHANGE_DEPENDENTS_FAILED);
    REQUIRE(entry->enabled == 1);
    REQUIRE(g_CatalogMutationControls.invalidate_calls == 1);
    REQUIRE(g_CatalogMutationControls.teardown_calls == 0);
}

TEST_CASE("Checked catalog toggle reports a changed entry instead of claiming its transaction", "[catalog][mutation]")
{
    Catalog catalog;
    auto *entry = catalog.add("test:font");
    g_CatalogMutationControls.conflict_on_invalidate = 1;
    REQUIRE(assetCatalogSetEnabledChecked(entry->id, 0) == CATALOG_CHANGE_CONFLICT);
    REQUIRE(entry->enabled == 0);
    REQUIRE(g_CatalogMutationControls.teardown_calls == 0);
}

TEST_CASE("Checked catalog teardown failure restores intent but still reports failure", "[catalog][mutation]")
{
    Catalog catalog;
    auto *entry = catalog.add("test:font");
    g_CatalogMutationControls.teardown_ok = 0;
    SECTION("dependent recovery succeeds") {}
    SECTION("dependent recovery also fails") { g_CatalogMutationControls.reload_ok = 0; }
    REQUIRE(assetCatalogSetEnabledChecked(entry->id, 0) == CATALOG_CHANGE_TEARDOWN_FAILED);
    REQUIRE(entry->enabled == 1);
    REQUIRE(g_CatalogMutationControls.reload_calls == 1);
}

TEST_CASE("Checked catalog enable reports partial failure after publishing enabled intent", "[catalog][mutation]")
{
    Catalog catalog;
    auto *entry = catalog.add("test:font");
    entry->enabled = 0;
    g_CatalogMutationControls.reload_ok = 0;
    REQUIRE(assetCatalogSetEnabledChecked(entry->id, 1) == CATALOG_CHANGE_RELOAD_FAILED);
    REQUIRE(entry->enabled == 1);
    REQUIRE(g_CatalogMutationControls.reload_calls == 1);
}

TEST_CASE("Checked catalog successful toggles retain compatibility wrapper behavior", "[catalog][mutation]")
{
    Catalog catalog;
    auto *entry = catalog.add("test:font");
    REQUIRE(assetCatalogSetEnabledChecked(entry->id, 0) == CATALOG_CHANGE_APPLIED);
    REQUIRE(entry->enabled == 0);
    REQUIRE(assetCatalogSetEnabledChecked(entry->id, 1) == CATALOG_CHANGE_APPLIED);
    REQUIRE(entry->enabled == 1);
    assetCatalogSetEnabled(entry->id, 0);
    REQUIRE(entry->enabled == 0);
    assetCatalogSetEnabled(entry->id, 1);
    REQUIRE(entry->enabled == 1);
}

TEST_CASE("Checked catalog clear refuses rejected retirement before deleting registry rows", "[catalog][mutation]")
{
    Catalog catalog;
    catalog.add("test:font");
    const auto generation = assetCatalogGetGeneration();
    SECTION("typed preflight") { g_CatalogMutationControls.preflight_ok = 0; }
    SECTION("dependent preflight") { g_CatalogMutationControls.dependent_preflight_ok = 0; }
    REQUIRE(assetCatalogClearModsChecked() == CATALOG_CHANGE_RETIRE_FAILED);
    REQUIRE(assetCatalogResolve("test:font") != nullptr);
    REQUIRE(assetCatalogGetGeneration() == generation);
    REQUIRE(g_CatalogMutationControls.teardown_calls == 0);
}

TEST_CASE("Checked catalog clear reports partial retirement without claiming cleared rows", "[catalog][mutation]")
{
    Catalog catalog;
    catalog.add("test:first");
    catalog.add("test:second");
    const auto generation = assetCatalogGetGeneration();
    g_CatalogMutationControls.fail_teardown_at = 2;
    REQUIRE(assetCatalogClearModsChecked() == CATALOG_CHANGE_RETIRE_FAILED);
    REQUIRE(g_CatalogMutationControls.teardown_calls == 2);
    REQUIRE(assetCatalogResolve("test:first") != nullptr);
    REQUIRE(assetCatalogResolve("test:second") != nullptr);
    REQUIRE(assetCatalogGetGeneration() == generation);
}

TEST_CASE("Checked catalog clear removes mods while retaining bundled registry rows", "[catalog][mutation]")
{
    Catalog catalog;
    catalog.add("test:base", true);
    catalog.add("test:mod");
    const auto generation = assetCatalogGetGeneration();
    REQUIRE(assetCatalogClearModsChecked() == CATALOG_CHANGE_APPLIED);
    REQUIRE(assetCatalogResolve("test:base") != nullptr);
    REQUIRE(assetCatalogResolve("test:mod") == nullptr);
    REQUIRE(assetCatalogGetGeneration() > generation);
    catalog.add("test:again");
    assetCatalogClearMods();
    REQUIRE(assetCatalogResolve("test:again") == nullptr);
    REQUIRE(assetCatalogResolve("test:base") != nullptr);
}
