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
