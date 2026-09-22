#include "catch.hpp"
#include "modmgr_component_catalog.h"
#include "modmgr_component_state.h"
#include "assetcatalog_mutation.h"
#include "save_atomic.h"
#include "catalog_mutation_support.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {
struct Catalog {
    Catalog() { catalogMutationTestReset(); assetCatalogInit(); }
    ~Catalog() { catalogMutationTestReset(); assetCatalogClear(); }
    asset_entry_t *add(const char *id, int enabled = 1) {
        auto *entry = assetCatalogRegister(id, ASSET_FONT);
        REQUIRE(entry != nullptr);
        entry->enabled = enabled;
        return entry;
    }
};
struct File {
    std::filesystem::path dir, path;
    File() {
        static std::atomic<unsigned> serial{0};
        dir = std::filesystem::temp_directory_path() / ("pd2-component-catalog-" +
            std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) +
            "-" + std::to_string(serial++));
        std::filesystem::create_directory(dir);
        path = dir / ".modstate";
    }
    ~File() { std::error_code ignored; std::filesystem::remove_all(dir, ignored); }
    void write(const std::string &text) {
        std::ofstream out(path, std::ios::binary);
        out.write(text.data(), text.size());
        REQUIRE(out.good());
    }
    std::string read() {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::vector<std::string> ids() {
        modmgr_component_state_t *raw = nullptr;
        char error[256]{};
        REQUIRE(modmgrReadComponentState(path.string().c_str(), &raw, error, sizeof(error)) == 1);
        std::unique_ptr<modmgr_component_state_t, decltype(&modmgrFreeComponentState)>
            state(raw, modmgrFreeComponentState);
        std::vector<std::string> result;
        for (size_t i = 0; i < modmgrComponentStateCount(raw); ++i)
            result.emplace_back(modmgrComponentStateId(raw, i));
        return result;
    }
};
}

TEST_CASE("Catalog component save includes actual disabled entries and preserves unresolved choices", "[catalog][component-state]")
{
    Catalog catalog;
    File file;
    file.write("absent\nenabled\n");
    catalog.add("disabled", 0);
    catalog.add("enabled");
    catalog.add("bundled", 0)->bundled = 1;
    catalog.add("temporary", 0)->temporary = 1;
    modmgr_component_result_t result{};
    REQUIRE(modmgrSaveCatalogComponentState(file.path.string().c_str(), nullptr, 0, &result) == 1);
    REQUIRE(result.saved == 1);
    REQUIRE(result.examined == 4);
    REQUIRE(result.excluded == 2);
    REQUIRE(file.ids() == std::vector<std::string>{"absent", "disabled"});
    REQUIRE(assetCatalogResolveAny("disabled")->enabled == 0);
}

TEST_CASE("Catalog component persistence excludes reloaded session source trees with exact boundaries", "[catalog][component-state]")
{
    Catalog catalog;
    File file;
    file.write("session-existing\n");
    auto *session = catalog.add("session-existing");
    std::snprintf(session->descriptor_path, sizeof(session->descriptor_path),
        "C:/session/pack/nested/font.pdmod::font.json");
    auto *folder = catalog.add("session-new", 0);
    std::snprintf(folder->dirpath, sizeof(folder->dirpath), "C:/session/pack/loose");
    auto *neighbor = catalog.add("persistent-neighbor", 0);
    std::snprintf(neighbor->dirpath, sizeof(neighbor->dirpath), "C:/session/pack-other");
    const char *session_dirs[] = {"C:\\session\\pack\\"};
    modmgr_component_result_t result{};
    REQUIRE(modmgrSaveCatalogComponentState(file.path.string().c_str(), session_dirs, 1, &result) == 1);
    REQUIRE(result.excluded == 2);
    REQUIRE(file.ids() == std::vector<std::string>{"persistent-neighbor", "session-existing"});
    file.write("session-existing\nsession-new\npersistent-neighbor\n");
    REQUIRE(modmgrReplayCatalogComponentState(file.path.string().c_str(), session_dirs, 1, &result) == 1);
    REQUIRE(result.excluded == 2);
    REQUIRE(assetCatalogResolveAny("session-existing")->enabled == 1);
}

TEST_CASE("Catalog component replay uses checked toggles and skips bundled temporary and missing IDs", "[catalog][component-state]")
{
    Catalog catalog;
    File file;
    catalog.add("enabled");
    catalog.add("already-disabled", 0);
    catalog.add("bundled")->bundled = 1;
    catalog.add("temporary")->temporary = 1;
    file.write("enabled\nalready-disabled\nbundled\ntemporary\nmissing\n");
    modmgr_component_result_t result{};
    REQUIRE(modmgrReplayCatalogComponentState(file.path.string().c_str(), nullptr, 0, &result) == 1);
    REQUIRE(result.applied == 1);
    REQUIRE(result.unchanged == 1);
    REQUIRE(result.excluded == 2);
    REQUIRE(result.unresolved == 1);
    REQUIRE(assetCatalogResolveAny("enabled")->enabled == 0);
    REQUIRE(assetCatalogResolveAny("bundled")->enabled == 1);
    REQUIRE(assetCatalogResolveAny("temporary")->enabled == 1);
    REQUIRE(g_CatalogMutationControls.teardown_calls == 1);
}

TEST_CASE("Catalog component replay reports refused toggle and partial earlier effects", "[catalog][component-state]")
{
    Catalog catalog;
    File file;
    catalog.add("first");
    catalog.add("second");
    file.write("first\nsecond\n");
    g_CatalogMutationControls.fail_teardown_at = 2;
    modmgr_component_result_t result{};
    REQUIRE(modmgrReplayCatalogComponentState(file.path.string().c_str(), nullptr, 0, &result) == 0);
    REQUIRE(result.applied == 1);
    REQUIRE(result.catalog_result == CATALOG_CHANGE_TEARDOWN_FAILED);
    REQUIRE(std::string(result.id) == "second");
    REQUIRE(assetCatalogResolveAny("first")->enabled == 0);
    REQUIRE(assetCatalogResolveAny("second")->enabled == 1);
    REQUIRE(result.error[0] != '\0');
    REQUIRE(file.read() == "first\nsecond\n");
}

TEST_CASE("Catalog component replay fully validates saved input before actual catalog mutation", "[catalog][component-state]")
{
    Catalog catalog;
    File file;
    catalog.add("first");
    file.write("first\n" + std::string(CATALOG_ID_LEN, 'x'));
    modmgr_component_result_t result{};
    REQUIRE(modmgrReplayCatalogComponentState(file.path.string().c_str(), nullptr, 0, &result) == 0);
    REQUIRE(result.applied == 0);
    REQUIRE(assetCatalogResolveAny("first")->enabled == 1);
    REQUIRE(g_CatalogMutationControls.invalidate_calls == 0);
}

TEST_CASE("Catalog component state distinguishes no persistent destination from empty optional state", "[catalog][component-state]")
{
    Catalog catalog;
    File file;
    catalog.add("bundled")->bundled = 1;
    modmgr_component_result_t result{};
    REQUIRE(modmgrSaveCatalogComponentState(nullptr, nullptr, 0, &result) == 1);
    REQUIRE(result.source_missing == 1);
    REQUIRE(result.saved == 0);
    catalog.add("persistent", 0);
    REQUIRE(modmgrSaveCatalogComponentState(nullptr, nullptr, 0, &result) == 0);
    REQUIRE(result.error[0] != '\0');
    REQUIRE(modmgrReplayCatalogComponentState(file.path.string().c_str(), nullptr, 0, &result) == 1);
    REQUIRE(result.source_missing == 1);
    REQUIRE(modmgrSaveCatalogComponentState(file.path.string().c_str(), nullptr, 1, &result) == 0);
}

TEST_CASE("Prepared component plan saves desired intent before changing actual catalog", "[catalog][component-plan]")
{
    Catalog catalog; File file;
    catalog.add("disable"); catalog.add("enable", 0);
    file.write("enable\nunresolved\n");
    modmgr_component_override_t changes[] = {{"disable", 0}, {"enable", 1}};
    modmgr_component_plan_t *raw = nullptr;
    modmgr_component_result_t result{};
    REQUIRE(modmgrPrepareCatalogComponentPlan(file.path.string().c_str(), nullptr, 0,
        changes, 2, &raw, &result) == 1);
    std::unique_ptr<modmgr_component_plan_t, decltype(&modmgrFreeCatalogComponentPlan)>
        plan(raw, modmgrFreeCatalogComponentPlan);
    REQUIRE(file.read() == "enable\nunresolved\n");
    REQUIRE(modmgrSaveCatalogComponentPlan(plan.get(), &result) == 1);
    REQUIRE(file.ids() == std::vector<std::string>{"disable", "unresolved"});
    REQUIRE(assetCatalogResolveAny("disable")->enabled == 1);
    REQUIRE(assetCatalogResolveAny("enable")->enabled == 0);
    REQUIRE(g_CatalogMutationControls.teardown_calls == 0);
}

TEST_CASE("Prepared component plan survives catalog retirement and failed atomic save retry", "[catalog][component-plan]")
{
    Catalog catalog; File file;
    catalog.add("disable"); file.write("unresolved\n");
    modmgr_component_override_t change{"disable", 0};
    modmgr_component_plan_t *raw = nullptr;
    modmgr_component_result_t result{};
    REQUIRE(modmgrPrepareCatalogComponentPlan(file.path.string().c_str(), nullptr, 0,
        &change, 1, &raw, &result) == 1);
    std::unique_ptr<modmgr_component_plan_t, decltype(&modmgrFreeCatalogComponentPlan)>
        plan(raw, modmgrFreeCatalogComponentPlan);
    std::memset(&change, 0, sizeof(change));
    assetCatalogClear();
    saveAtomicDebugFailNextCommit();
    REQUIRE(modmgrSaveCatalogComponentPlan(plan.get(), &result) == 0);
    REQUIRE(result.saved == 0);
    REQUIRE(file.read() == "unresolved\n");
    REQUIRE(modmgrSaveCatalogComponentPlan(plan.get(), &result) == 1);
    REQUIRE(result.saved == 1);
    REQUIRE(file.ids() == std::vector<std::string>{"disable", "unresolved"});
}

TEST_CASE("Prepared component plan rejects foreign missing and duplicate intent read-only", "[catalog][component-plan]")
{
    Catalog catalog; File file;
    catalog.add("owned"); catalog.add("base")->bundled = 1;
    catalog.add("temporary")->temporary = 1;
    auto *session = catalog.add("session");
    std::snprintf(session->dirpath, sizeof(session->dirpath), "C:/session/pack/member");
    const char *dirs[] = {"C:/session/pack"};
    file.write("retained\n");
    for (const char *id : {"base", "temporary", "session", "missing"}) {
        modmgr_component_override_t change{};
        std::snprintf(change.id, sizeof(change.id), "%s", id);
        modmgr_component_plan_t *raw = nullptr; modmgr_component_result_t result{};
        REQUIRE(modmgrPrepareCatalogComponentPlan(file.path.string().c_str(), dirs, 1,
            &change, 1, &raw, &result) == 0);
        REQUIRE(raw == nullptr);
        REQUIRE(std::string(result.id) == id);
    }
    modmgr_component_override_t duplicate[] = {{"owned", 0}, {"owned", 1}};
    modmgr_component_plan_t *raw = nullptr; modmgr_component_result_t result{};
    REQUIRE(modmgrPrepareCatalogComponentPlan(file.path.string().c_str(), dirs, 1,
        duplicate, 2, &raw, &result) == 0);
    REQUIRE(raw == nullptr);
    duplicate[0].enabled = 2;
    REQUIRE(modmgrPrepareCatalogComponentPlan(file.path.string().c_str(), dirs, 1,
        duplicate, 1, &raw, &result) == 0);
    REQUIRE(file.read() == "retained\n");
    REQUIRE(assetCatalogResolveAny("owned")->enabled == 1);
    REQUIRE(g_CatalogMutationControls.teardown_calls == 0);
}

TEST_CASE("Prepared component plan distinguishes absent destination from absent preferences", "[catalog][component-plan]")
{
    Catalog catalog;
    modmgr_component_plan_t *raw = nullptr; modmgr_component_result_t result{};
    REQUIRE(modmgrPrepareCatalogComponentPlan(nullptr, nullptr, 0, nullptr, 0, &raw, &result) == 1);
    std::unique_ptr<modmgr_component_plan_t, decltype(&modmgrFreeCatalogComponentPlan)>
        plan(raw, modmgrFreeCatalogComponentPlan);
    REQUIRE(modmgrSaveCatalogComponentPlan(plan.get(), &result) == 1);
    REQUIRE(result.saved == 0);
    REQUIRE(result.source_missing == 1);
    catalog.add("owned"); raw = nullptr;
    REQUIRE(modmgrPrepareCatalogComponentPlan(nullptr, nullptr, 0, nullptr, 0, &raw, &result) == 0);
    REQUIRE(raw == nullptr);
}
