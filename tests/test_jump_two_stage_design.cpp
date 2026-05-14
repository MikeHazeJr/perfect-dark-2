/*
 * test_jump_two_stage_design.cpp -- design pin for the jump two-stage
 * capsule sweep upgrade.
 *
 * Pins the existence and minimum section structure of the design doc at
 * context/designs/physics-collision/jump-two-stage-sweep.md. The doc is
 * DESIGN-status, pending Mike's approval before any live jump code lands.
 * This test guards the doc against deletion before that approval -- if
 * the doc disappears or loses its required sections, this test fails.
 *
 * No live game code is touched; this is a pure source-grep against the
 * markdown file.
 *
 * @SYNC context/designs/physics-collision/jump-two-stage-sweep.md
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readTextFile(const char *path)
{
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f) {
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("design pin: jump two-stage sweep design doc present",
          "[physics][jump][design][static]")
{
    const char *path =
        "context/designs/physics-collision/jump-two-stage-sweep.md";

    const std::string doc = readTextFile(path);

    REQUIRE_FALSE(doc.empty());

    REQUIRE(doc.find("## Status") != std::string::npos);
    REQUIRE(doc.find("## Problem statement") != std::string::npos);
    REQUIRE(doc.find("## Proposed two-stage design") != std::string::npos);
    REQUIRE(doc.find("## API plan") != std::string::npos);
    REQUIRE(doc.find("## Risks") != std::string::npos);
    REQUIRE(doc.find("## Open questions for Mike") != std::string::npos);
    REQUIRE(doc.find("## Where to look") != std::string::npos);
}
