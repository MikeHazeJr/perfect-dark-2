#include "catch.hpp"
#include "native_asset_consumer_trace_contract.h"

static std::string manifest() {
    return "schema=pd2.native-asset-consumer-manifest.v1\n"
        "binary_sha256=" + std::string(64, 'b') + "\nsource_sha256=" + std::string(64, 'c')
        + "\narchive_sha256=" + std::string(64, 'a') + "\ncatalog_id=base:model_test\n"
        "archive_origin=data/mesh.pdmesh\nrun_id=run-1\nmax_output_bytes=16777216\n";
}
TEST_CASE("native consumer manifest binds exact generation and bounded output", "[asset-consumer][c3842]") {
    pdtrace::Manifest out;
    REQUIRE(pdtrace::parse(manifest(), out));
    CHECK(out.binary == std::string(64, 'b'));
    CHECK(out.source == std::string(64, 'c'));
    CHECK(out.archive_hash == std::string(64, 'a'));
    CHECK(out.catalog == "base:model_test");
    CHECK(out.max_output == 16777216);
}
TEST_CASE("native consumer manifest rejects duplicate and unknown controls", "[asset-consumer][c3842]") {
    pdtrace::Manifest out;
    CHECK_FALSE(pdtrace::parse(manifest() + "catalog_id=base:other\n", out));
    CHECK_FALSE(pdtrace::parse(manifest() + "output_path=../outside\n", out));
    CHECK_FALSE(pdtrace::parse(std::string(8193, 'x'), out));
    auto text = manifest(); text[0] = '\0';
    CHECK_FALSE(pdtrace::parse(text, out));
}
TEST_CASE("native consumer relative members cannot cross archive ownership", "[asset-consumer][c3842]") {
    CHECK(pdtrace::relative("images/diffuse.png"));
    for (const char *path : {"../mesh.gltf", "a/../mesh.gltf", "/mesh.gltf", "a//mesh.gltf",
            "a\\mesh.gltf", "texture.pdtexture::texture.png", "C:stream"})
        CHECK_FALSE(pdtrace::relative(path));
    CHECK(pdtrace::origin("data/body.pdbody::mesh.pdmesh"));
    CHECK_FALSE(pdtrace::origin("data/body.pdbody::../mesh.pdmesh"));
    CHECK_FALSE(pdtrace::origin("data/model.obj"));
    CHECK(pdtrace::modelOrigin("data/mesh.pdmesh") == "data/mesh.pdmesh");
    CHECK(pdtrace::modelOrigin("data/mesh.pdmesh::model.obj") == "data/mesh.pdmesh");
    CHECK(pdtrace::modelOrigin("data/body.pdbody::mesh.pdmesh::model.gltf") == "data/body.pdbody::mesh.pdmesh");
    CHECK(pdtrace::modelOrigin("data/model.obj").empty());
    CHECK(pdtrace::sourceOrigin("data/texture.pdtexture::texture.png", ".pdtexture") == "data/texture.pdtexture");
    CHECK(pdtrace::sourceOrigin("data/package.pdmod::texture.pdtexture::texture.png", ".pdtexture") == "data/package.pdmod::texture.pdtexture");
    CHECK(pdtrace::sourceOrigin("data/texture.png", ".pdtexture").empty());
    CHECK(pdtrace::sourceOrigin("data/../texture.pdtexture::texture.png", ".pdtexture").empty());
}
TEST_CASE("native consumer manifest rejects oversized signed and noncanonical budgets", "[asset-consumer][c3842]") {
    for (const char *value : {"16777217", "0", "04096", "-4096", "+4096", "4e3"}) {
        auto text = manifest(); const auto pos = text.find("16777216");
        text.replace(pos, 8, value); pdtrace::Manifest out;
        CHECK_FALSE(pdtrace::parse(text, out));
    }
}
TEST_CASE("native consumer JSON escape preserves exact member names", "[asset-consumer][c3842]") {
    CHECK(pdtrace::quote("quoted\"slash\\line\n") == "\"quoted\\\"slash\\\\line\\u000a\"");
    CHECK(pdtrace::quote("model.gltf") == "\"model.gltf\"");
    CHECK(pdtrace::catalog("base:texture_a"));
    for (const char *id : {"base:../a", "base:/a", "base:a:b", "base:a*", "1base:a"})
        CHECK_FALSE(pdtrace::catalog(id));
    CHECK(pdtrace::textureBinding("base:texture_a", std::string(64, 'a'), 65534));
    CHECK_FALSE(pdtrace::textureBinding("base:../a", std::string(64, 'a'), 65534));
    CHECK_FALSE(pdtrace::textureBinding("base:texture_a", std::string(64, 'A'), 65534));
    CHECK_FALSE(pdtrace::textureBinding("base:texture_a", std::string(64, 'a'), -1));
    CHECK_FALSE(pdtrace::textureBinding("base:texture_a", std::string(64, 'a'), 65535));
}
