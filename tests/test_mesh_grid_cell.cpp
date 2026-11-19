// B-1210: the static world mesh grid caps at MESHGRID_MAX_DIM cells of
// MESHGRID_CELL_SIZE units per axis. Crash Site and Deep Sea are wider than
// that on both axes (their stage loads log a 128x128 grid). meshWorldFinalize
// used to clamp only the far end of a triangle's cell span, so a triangle
// wholly past the cap was listed in no cell, and meshFindFloor and
// meshFindCeiling skipped every position past the cap: the floor fallback in
// cdFindGroundInfoAtCyl found nothing there. meshGridCell clamps both ends of
// every span and every lookup into the grid. These cases check the helper,
// the span-and-lookup rule built on it, and that every grid index in
// meshcollision.c goes through it.

#include "catch.hpp"

// Game headers before the C++ library headers, and not inside extern "C":
// os_libc.h redeclares sprintf, which clashes with MinGW's otherwise.
#include "lib/meshcollision.h"

#include <algorithm>
#include <fstream>
#include <limits>
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

// The grid build lists a triangle spanning [tmin, tmax] in the cells
// meshGridCell gives for its two ends; the floor and ceiling lookups test the
// 3x3 neighbourhood around meshGridCell of the position. One axis of that.
bool lookupFindsSpan(f32 x, f32 tmin, f32 tmax, f32 origin, s32 cells)
{
    const s32 c0 = meshGridCell(tmin, origin, cells);
    const s32 c1 = meshGridCell(tmax, origin, cells);
    const s32 c = meshGridCell(x, origin, cells);

    for (s32 d = -1; d <= 1; d++) {
        const s32 g = c + d;
        if (g < 0 || g >= cells) {
            continue;
        }
        if (g >= c0 && g <= c1) {
            return true;
        }
    }
    return false;
}

// The body of the function whose definition starts with signature.
std::string functionBody(const std::string &src, const char *signature)
{
    const size_t start = src.find(signature);
    if (start == std::string::npos) {
        return {};
    }
    const size_t end = src.find("\n}\n", start);
    return src.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

} // namespace

TEST_CASE("B-1210 meshGridCell keeps positions inside the grid on their own cell", "[mesh][grid][B-1210]")
{
    REQUIRE(meshGridCell(0.0f, 0.0f, 10) == 0);
    REQUIRE(meshGridCell(255.9f, 0.0f, 10) == 0);
    REQUIRE(meshGridCell(256.0f, 0.0f, 10) == 1);
    REQUIRE(meshGridCell(-700.0f, -1000.0f, 10) == 1);
    REQUIRE(meshGridCell(9.5f * MESHGRID_CELL_SIZE, 0.0f, 10) == 9);
}

TEST_CASE("B-1210 meshGridCell clamps past the cap and below the origin", "[mesh][grid][B-1210]")
{
    // 40000 units across needs 157 cells; the grid allows MESHGRID_MAX_DIM.
    REQUIRE(meshGridCell(40000.0f, 0.0f, MESHGRID_MAX_DIM) == MESHGRID_MAX_DIM - 1);
    REQUIRE(meshGridCell(MESHGRID_MAX_DIM * MESHGRID_CELL_SIZE, 0.0f, MESHGRID_MAX_DIM) == MESHGRID_MAX_DIM - 1);
    REQUIRE(meshGridCell(-300.0f, 0.0f, MESHGRID_MAX_DIM) == 0);
    REQUIRE(meshGridCell(1.0e30f, 0.0f, 8) == 7);
    REQUIRE(meshGridCell(-1.0e30f, 0.0f, 8) == 0);
    REQUIRE(meshGridCell(std::numeric_limits<f32>::quiet_NaN(), 0.0f, 8) == 0);
}

TEST_CASE("B-1210 a triangle wholly past the cap is listed and a lookup over it finds it", "[mesh][grid][B-1210]")
{
    const s32 cells = MESHGRID_MAX_DIM;

    // The old rule clamped only the far end: this span was empty.
    const s32 old0 = static_cast<s32>(39000.0f / MESHGRID_CELL_SIZE);
    const s32 old1 = std::min(static_cast<s32>(39500.0f / MESHGRID_CELL_SIZE), cells - 1);
    REQUIRE(old0 > old1);

    REQUIRE(meshGridCell(39000.0f, 0.0f, cells) == cells - 1);
    REQUIRE(meshGridCell(39500.0f, 0.0f, cells) == cells - 1);

    // Over the triangle, past the cap.
    REQUIRE(lookupFindsSpan(39200.0f, 39000.0f, 39500.0f, 0.0f, cells));
    // Inside the grid next to the cap, a triangle that crosses it.
    REQUIRE(lookupFindsSpan(32700.0f, 32600.0f, 33000.0f, 0.0f, cells));
    // Far past the cap in the other direction of the same row.
    REQUIRE(lookupFindsSpan(60000.0f, 58000.0f, 61000.0f, 0.0f, cells));
    // Inside the cap nothing changes: near spans found, far spans not.
    REQUIRE(lookupFindsSpan(1000.0f, 900.0f, 1100.0f, 0.0f, cells));
    REQUIRE_FALSE(lookupFindsSpan(1000.0f, 5000.0f, 5100.0f, 0.0f, cells));
}

TEST_CASE("B-1210 every world grid index in meshcollision.c goes through meshGridCell", "[mesh][grid][B-1210][source]")
{
    const std::string src = readTextFile("src/lib/meshcollision.c");
    REQUIRE_FALSE(src.empty());

    // No unclamped index (an offset from the origin divided by the cell
    // size) is left; the grid's own cell counts still divide its extent.
    REQUIRE(src.find("originx) / MESHGRID_CELL_SIZE") == std::string::npos);
    REQUIRE(src.find("originz) / MESHGRID_CELL_SIZE") == std::string::npos);

    const char *const users[] = {
        "static s32 meshWorldBuildGrid(struct meshgrid *grid",
        "f32 meshSweepCapsuleWorld(struct coord *start",
        "f32 meshFindFloor(struct coord *pos",
        "f32 meshFindCeiling(struct coord *pos",
    };
    for (const char *signature : users) {
        INFO(signature);
        const std::string body = functionBody(src, signature);
        REQUIRE_FALSE(body.empty());
        REQUIRE(body.find("meshGridCell(") != std::string::npos);
    }
}

TEST_CASE("Grid layout builds detached collision through the native world grid", "[grid-layout-apply][meshcollision][source]")
{
    const std::string src = readTextFile("src/lib/meshcollision.c");
    const std::string finalize = functionBody(src, "void meshWorldFinalize(void)");
    const std::string prepare = functionBody(src, "s32 meshWorldPrepare(");
    REQUIRE(finalize.find("meshWorldBuildGrid(&g_WorldMesh, &s_WorldMeshHasShotFlags,") != std::string::npos);
    REQUIRE(prepare.find("memcpy(prepared->grid.tris, source->tris, tri_bytes);") != std::string::npos);
    REQUIRE(prepare.find("g_WorldMesh") == std::string::npos);
    REQUIRE(prepare.find("s_WorldRay") == std::string::npos);
    REQUIRE(prepare.find("s_WorldMeshHasShotFlags") == std::string::npos);

    const size_t build = prepare.find("if (!meshWorldBuildGrid(&prepared->grid, &prepared->has_shot_flags,");
    const size_t publish = prepare.find("*out = prepared;");
    REQUIRE(build != std::string::npos);
    REQUIRE(publish != std::string::npos);
    REQUIRE(build < publish);
    const std::string onFailure = prepare.substr(build, publish - build);
    REQUIRE(onFailure.find("meshWorldFreePrepared(prepared);") != std::string::npos);
    REQUIRE(onFailure.find("return 0;") != std::string::npos);
}

TEST_CASE("Grid world swap preserves the outgoing grid and its ray state", "[grid-layout-apply][meshcollision][source]")
{
    const std::string src = readTextFile("src/lib/meshcollision.c");
    const std::string swap = functionBody(src, "void meshWorldSwapPrepared(");
    const size_t capture = swap.find(".grid = g_WorldMesh, .ray = s_WorldRay, .has_shot_flags = s_WorldMeshHasShotFlags,");
    const size_t grid = swap.find("g_WorldMesh = prepared->grid;");
    const size_t ray = swap.find("s_WorldRay = prepared->ray;");
    const size_t shotFlags = swap.find("s_WorldMeshHasShotFlags = prepared->has_shot_flags;");
    const size_t outgoing = swap.find("*prepared = outgoing;");
    REQUIRE(capture < grid);
    REQUIRE(grid < ray);
    REQUIRE(ray < shotFlags);
    REQUIRE(shotFlags < outgoing);
    REQUIRE(outgoing != std::string::npos);
}
