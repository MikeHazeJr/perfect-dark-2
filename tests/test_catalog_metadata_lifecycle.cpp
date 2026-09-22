/* B-1107 source contracts for the production catalog loader. pd-tests does
 * not link assetcatalog_load.c; these checks pin the shared transaction and
 * teardown boundaries. The V-006 ordinary-client Scenario harness supplies
 * the separate behavioral proof of activation, rollback, and prior ownership. */
#include "catch.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>

namespace {

std::string catalogSource()
{
    std::ifstream input("port/src/assetcatalog_load.c", std::ios::binary);
    REQUIRE(input.good());
    std::string source(std::istreambuf_iterator<char>(input), {});
    source.erase(std::remove(source.begin(), source.end(), '\r'), source.end());
    return source;
}

size_t position(const std::string &source, const char *text, size_t from = 0)
{
    const size_t found = source.find(text, from);
    INFO(text);
    REQUIRE(found != std::string::npos);
    return found;
}

std::string functionBody(const std::string &source, const char *signature)
{
    const size_t begin = position(source, signature);
    const size_t brace = position(source, "{", begin);
    size_t depth = 1;
    for (size_t i = brace + 1; i < source.size(); i++) {
        if (source[i] == '{') depth++;
        if (source[i] == '}' && --depth == 0) {
            return source.substr(brace + 1, i - brace - 1);
        }
    }
    FAIL("catalog function has no closing brace");
    return {};
}

std::string metadataBody(const std::string &source)
{
    return functionBody(source,
        "static s32 s_catalogLoadEntryMetadataPayload(asset_entry_t *entry)\n{");
}

} // namespace

TEST_CASE("metadata payload publication waits for graph and typed source activation",
    "[catalog][runtime][b1107][v006][static]")
{
    const std::string load = metadataBody(catalogSource());
    const size_t build = position(load, "modAssetCompilerBuildColmesh(");
    const size_t weapon = position(load, "s_catalogActivateWeaponGraphRuntime(");
    const size_t effect = position(load, "s_catalogActivateEffectGraphRuntime(");
    const size_t activate = position(load, "assetRuntimeActivateCatalogEntry(");
    const size_t hydrate = position(load, "assetRuntimeHydrateCatalogEntry(");
    const size_t success = position(load, "return 1;");
    REQUIRE(build < weapon);
    REQUIRE(weapon < activate);
    REQUIRE(effect < activate);
    REQUIRE(activate < hydrate);
    REQUIRE(hydrate < success);
    REQUIRE(load.find("return 1;", success + 1) == std::string::npos);

    // Every published ownership field has a single writer after hydration.
    // This rejects the original successful-colmesh early publication/return.
    const std::regex publication(
        R"(entry->(loaded_data|data_size_bytes|payload_kind|load_state|ref_count)\s*=)");
    size_t writes = 0;
    for (std::sregex_iterator it(load.begin(), load.end(), publication), end;
            it != end; ++it) {
        const size_t write = static_cast<size_t>(it->position());
        REQUIRE(write > hydrate);
        REQUIRE(write < success);
        writes++;
    }
    REQUIRE(writes == 5);
    REQUIRE(position(load, "mesh ? (void *)mesh : (void *)entry") > hydrate);
    REQUIRE(position(load, "? ASSET_PAYLOAD_COLMESH : ASSET_PAYLOAD_RUNTIME_ACTIVE")
        > hydrate);
}

TEST_CASE("metadata rejection rolls back acquired adapters and private collision payload",
    "[catalog][runtime][b1107][v006][static]")
{
    const std::string source = catalogSource();
    const std::string load = metadataBody(source);
    const size_t rollback = position(load, "rollback_adapters:");
    const size_t detach = position(load,
        "s_catalogDetachRuntimeAdapters(entry, entry->id)", rollback);
    const size_t payload = position(load, "rollback_payload:", detach);
    const size_t meshFree = position(load, "meshFree(mesh)", payload);
    const size_t free = position(load, "free(mesh)", meshFree);
    REQUIRE(rollback < detach);
    REQUIRE(detach < payload);
    REQUIRE(meshFree < free);
    REQUIRE(free < position(load, "return 0;", free));

    const char *gates[] = {
        "s_catalogTypeUsesWeaponGraphRuntime(entry->type)",
        "s_catalogTypeUsesEffectGraphRuntime(entry->type)",
        "assetRuntimeActivateCatalogEntry(entry, source_path)",
        "assetRuntimeHydrateCatalogEntry(entry)"
    };
    for (const char *gate : gates) {
        const size_t start = position(load, gate);
        const size_t end = position(load, "}", start);
        const std::string branch = load.substr(start, end - start);
        REQUIRE(branch.find("goto rollback_adapters;") != std::string::npos);
        REQUIRE(branch.find("return 0;") == std::string::npos);
    }

    // An early source/mesh rejection owns no runtime registration to detach.
    const size_t meshFailure = position(load, "if (mesh_result < 0)");
    const size_t meshFailureEnd = position(load, "}", meshFailure);
    const std::string meshBranch = load.substr(meshFailure,
        meshFailureEnd - meshFailure);
    REQUIRE(meshBranch.find("goto rollback_payload;") != std::string::npos);
    REQUIRE(meshBranch.find("rollback_adapters") == std::string::npos);
    REQUIRE(position(load, "calloc(1, sizeof(*mesh))") < meshFailure);
    REQUIRE(load.substr(rollback).find("entry->ref_count") == std::string::npos);
    REQUIRE(load.substr(rollback).find("entry->load_state") == std::string::npos);

    const std::string adapters = functionBody(source,
        "static void s_catalogDetachRuntimeAdapters(asset_entry_t *entry,\n"
        "        const char *assetId)\n{");
    REQUIRE(adapters.find("s_catalogClearWeaponGraphRuntime(entry, assetId)")
        != std::string::npos);
    REQUIRE(adapters.find("s_catalogClearEffectGraphRuntime(entry, assetId)")
        != std::string::npos);
    REQUIRE(adapters.find("assetRuntimeReleaseCatalogEntry(assetId)")
        != std::string::npos);
}

TEST_CASE("collision teardown waits for final ownership and detaches before freeing",
    "[catalog][runtime][b1107][v006][static]")
{
    const std::string source = catalogSource();
    const std::string unload = functionBody(source,
        "static void s_catalogUnloadEntryRawMode(const char *assetId,\n"
        "        asset_entry_t *entry, s32 force_bundled)\n{");
    const size_t pinned = position(unload, "if (!force_bundled");
    const size_t finalRef = position(unload, "if (new_ref <= 0 && entry->loaded_data)");
    const size_t colmesh = position(unload, "entry->payload_kind == ASSET_PAYLOAD_COLMESH");
    const size_t detach = position(unload,
        "s_catalogDetachRuntimeAdapters(entry, assetId)", colmesh);
    const size_t meshFree = position(unload,
        "meshFree((struct colmesh *)entry->loaded_data)", colmesh);
    const size_t free = position(unload, "free(entry->loaded_data)", colmesh);
    REQUIRE(pinned < finalRef);
    REQUIRE(finalRef < colmesh);
    REQUIRE(colmesh < detach);
    REQUIRE(detach < meshFree);
    REQUIRE(meshFree < free);

    const std::string load = functionBody(source,
        "static s32 s_catalogLoadEntry(asset_entry_t *entry, asset_type_e expected_type)\n{");
    const size_t retain = position(load,
        "if (entry->load_state >= ASSET_STATE_LOADED && entry->loaded_data)");
    const size_t retained = position(load, "return 1;", retain);
    const size_t metadata = position(load, "s_catalogLoadEntryMetadataPayload(entry)");
    REQUIRE(retain < retained);
    REQUIRE(retained < metadata);
    REQUIRE(position(load, "entry->ref_count++", retain) < retained);
}
