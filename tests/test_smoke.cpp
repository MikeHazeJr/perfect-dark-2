/*
 * tests/test_smoke.cpp -- Sanity check that the framework links and runs.
 *
 * If this case fails, the build is broken in a way that makes every other
 * test result meaningless. So this is the canary.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

static std::string read_text_file(const char *path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

TEST_CASE("smoke: arithmetic still works", "[smoke]") {
    REQUIRE(1 + 1 == 2);
    REQUIRE(2 * 3 == 6);
}

TEST_CASE("smoke: REQUIRE failure path is reachable (positive case)", "[smoke]") {
    /* Verifies that a passing REQUIRE actually evaluates the expression. */
    int x = 0;
    REQUIRE((x = 5) == 5);
    REQUIRE(x == 5);
}

TEST_CASE("smoke: SECTION isolation", "[smoke]") {
    int counter = 0;

    SECTION("first section starts at zero") {
        counter += 1;
        REQUIRE(counter == 1);
    }

    SECTION("second section also starts at zero") {
        counter += 2;
        REQUIRE(counter == 2);
    }
}

TEST_CASE("smoke: pd-tests vendors the winpthread runtime on Windows", "[smoke][build][static][b355]") {
    const std::string cmake = read_text_file("CMakeLists.txt");

    REQUIRE(cmake.find("PD_WINPTHREAD_STATIC_LIB") != std::string::npos);
    REQUIRE(cmake.find("PD_WINPTHREAD_RUNTIME_DLL") != std::string::npos);
    REQUIRE(cmake.find("copy_if_different") != std::string::npos);
    REQUIRE(cmake.find("libwinpthread-1.dll") != std::string::npos);
}

TEST_CASE("smoke: versioninfo.h names the commit being built", "[smoke][build][static][b1252]") {
    // B-1252: the commit was read at configure time only, so an incremental
    // build after a commit kept the old one in the version line.
    const std::string cmake = read_text_file("CMakeLists.txt");
    const std::string script = read_text_file("cmake/versioninfo.cmake");

    REQUIRE(cmake.find("add_custom_target(pd_versioninfo ALL") != std::string::npos);
    REQUIRE(cmake.find("-P \"${CMAKE_SOURCE_DIR}/cmake/versioninfo.cmake\"") != std::string::npos);
    REQUIRE(cmake.find("BYPRODUCTS \"${CMAKE_BINARY_DIR}/port/include/versioninfo.h\"") != std::string::npos);
    // Every target that compiles depends on it, and the loop runs after the
    // last target is defined.
    const size_t loop = cmake.find("get_property(_pd_all_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)");
    REQUIRE(loop != std::string::npos);
    REQUIRE(cmake.find("add_dependencies(${_pd_target} pd_versioninfo)", loop) != std::string::npos);
    REQUIRE(cmake.rfind("add_executable(") < loop);
    REQUIRE(cmake.rfind("add_library(") < loop);

    REQUIRE(script.find("COMMAND git rev-parse --short HEAD") != std::string::npos);
    REQUIRE(script.find("COMMAND git rev-parse --abbrev-ref HEAD") != std::string::npos);
    REQUIRE(script.find("configure_file(\"${SRC_DIR}/port/include/versioninfo.h.in\" \"${OUT_FILE}\" @ONLY)") !=
            std::string::npos);
}
