/*
 * tests/test_loader_pdbase_scan.cpp -- S484 F10: pin the .pdbase
 * loader skeleton's scan / build invariants.
 *
 * Phase 2 (F10) state: the loader is a scaffold. The scan walk is
 * not yet implemented; calls produce zero records and emit the OK
 * log line. F11+ wires actual archive decode and replaces this test
 * with end-to-end record-count assertions.
 *
 * Source under test:
 *   port/src/loader_pdbase.c (scaffold, not linked in pd-tests).
 *
 * Static contract assertions:
 *   - Header declares loaderPdbaseScan + loaderPdbaseBuildWeaponManager.
 *   - loader_pdbase_result_t carries the documented count fields.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readFile(const char *path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  /* anonymous namespace */

TEST_CASE("F10: loader_pdbase.h declares the scaffold API",
          "[catalog-mgr-weapon][s484][f10]") {
    std::string hdr = readFile("port/include/loader_pdbase.h");
    REQUIRE(hdr.find("loaderPdbaseScan") != std::string::npos);
    REQUIRE(hdr.find("loaderPdbaseBuildWeaponManager") != std::string::npos);
    REQUIRE(hdr.find("loader_pdbase_result_t") != std::string::npos);
}

TEST_CASE("F10: result struct carries the documented count fields",
          "[catalog-mgr-weapon][s484][f10]") {
    std::string hdr = readFile("port/include/loader_pdbase.h");
    REQUIRE(hdr.find("archives_scanned") != std::string::npos);
    REQUIRE(hdr.find("weapons_registered") != std::string::npos);
    REQUIRE(hdr.find("scan_failures") != std::string::npos);
    REQUIRE(hdr.find("resolve_failures") != std::string::npos);
    REQUIRE(hdr.find("field_unknown") != std::string::npos);
}

TEST_CASE("F10: loader emits LOADER.PDBASE.WEAPON.* log channels",
          "[catalog-mgr-weapon][s484][f10]") {
    std::string src = readFile("port/src/loader_pdbase.c");
    /* Per-directive hierarchical channel naming is exercised by the
     * Phase 2 scaffold so end-to-end logs already have the right
     * prefix when F11+ wires the decoder in. */
    REQUIRE(src.find("LOADER.PDBASE.WEAPON.OK") != std::string::npos);
}
