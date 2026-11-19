// B-1189: background hits on scenario-source stages ray-cast the static world
// collision mesh through meshGridRayCast, which walks only the grid cells a
// ray crosses. These cases check it against a brute-force cast over the same
// triangles, on grids built the way meshWorldFinalize builds the world grid.

#include "catch.hpp"

// Game headers before the C++ library headers: os_libc.h redeclares
// sprintf, which clashes once <random>/<cmath> have pulled in MinGW's.
#include "lib/meshraygrid.h"
extern "C" {
#include "constants.h"
}

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace {

struct TestGrid {
    std::vector<meshtri> tris;
    std::vector<std::vector<s32>> lists;
    std::vector<meshgridcell> cells;
    meshgrid grid{};
};

coord makeCoord(float x, float y, float z)
{
    coord c{};
    c.x = x;
    c.y = y;
    c.z = z;
    return c;
}

meshtri makeTri(const coord &a, const coord &b, const coord &c, u16 flags)
{
    meshtri t{};
    t.v0 = a;
    t.v1 = b;
    t.v2 = c;
    const float e1x = b.x - a.x, e1y = b.y - a.y, e1z = b.z - a.z;
    const float e2x = c.x - a.x, e2y = c.y - a.y, e2z = c.z - a.z;
    float nx = e1y * e2z - e1z * e2y;
    float ny = e1z * e2x - e1x * e2z;
    float nz = e1x * e2y - e1y * e2x;
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 0.0f) {
        nx /= len;
        ny /= len;
        nz /= len;
    }
    t.normal = makeCoord(nx, ny, nz);
    t.flags = flags;
    return t;
}

void addWalkFloor(std::vector<meshtri> &tris, RoomNum room, float xmin, float xmax,
                  float zmin, float zmax, float y, u16 flags = GEOFLAG_FLOOR1)
{
    const coord a = makeCoord(xmin, y, zmin);
    const coord b = makeCoord(xmin, y, zmax);
    const coord c = makeCoord(xmax, y, zmax);
    const coord d = makeCoord(xmax, y, zmin);
    auto &first = tris.emplace_back(makeTri(a, b, c, flags));
    first.roomnum = room;
    auto &second = tris.emplace_back(makeTri(a, c, d, flags));
    second.roomnum = room;
}

void addWalkPartition(std::vector<meshtri> &tris, float zmin, float zmax, float ymin, float ymax)
{
    const coord a = makeCoord(0, ymin, zmin);
    const coord b = makeCoord(0, ymax, zmin);
    const coord c = makeCoord(0, ymax, zmax);
    const coord d = makeCoord(0, ymin, zmax);
    tris.push_back(makeTri(a, b, c, GEOFLAG_WALL));
    tris.push_back(makeTri(a, c, d, GEOFLAG_WALL));
}

s32 checkWalk(std::vector<meshtri> &tris, const coord &from, RoomNum fromRoom,
              const coord &to, RoomNum toRoom)
{
    colmesh mesh{};
    mesh.tris = tris.data();
    mesh.numtris = static_cast<s32>(tris.size());
    return meshCheckFlatWalk(&mesh, &from, fromRoom, &to, toRoom, 30.0f, 185.0f);
}

// Mirrors meshWorldFinalize (src/lib/meshcollision.c): origin at the mesh
// minimum, MESHGRID_CELL_SIZE cells capped at MESHGRID_MAX_DIM per axis, and
// each triangle listed in every cell its XZ bounds overlap, both ends of the
// range clamped into the grid by meshGridCell (B-1210).
void buildGrid(TestGrid &g)
{
    float minx = g.tris[0].v0.x, maxx = minx, minz = g.tris[0].v0.z, maxz = minz;
    for (const meshtri &t : g.tris) {
        for (const coord *v : {&t.v0, &t.v1, &t.v2}) {
            minx = std::min(minx, v->x);
            maxx = std::max(maxx, v->x);
            minz = std::min(minz, v->z);
            maxz = std::max(maxz, v->z);
        }
    }

    g.grid.originx = minx;
    g.grid.originz = minz;
    g.grid.cellsx = std::min(static_cast<s32>((maxx - minx) / MESHGRID_CELL_SIZE) + 1, MESHGRID_MAX_DIM);
    g.grid.cellsz = std::min(static_cast<s32>((maxz - minz) / MESHGRID_CELL_SIZE) + 1, MESHGRID_MAX_DIM);
    g.lists.assign(static_cast<size_t>(g.grid.cellsx * g.grid.cellsz), {});

    for (s32 i = 0; i < static_cast<s32>(g.tris.size()); i++) {
        const meshtri &t = g.tris[static_cast<size_t>(i)];
        const float txmin = std::min({t.v0.x, t.v1.x, t.v2.x});
        const float txmax = std::max({t.v0.x, t.v1.x, t.v2.x});
        const float tzmin = std::min({t.v0.z, t.v1.z, t.v2.z});
        const float tzmax = std::max({t.v0.z, t.v1.z, t.v2.z});
        const s32 cx0 = meshGridCell(txmin, minx, g.grid.cellsx);
        const s32 cx1 = meshGridCell(txmax, minx, g.grid.cellsx);
        const s32 cz0 = meshGridCell(tzmin, minz, g.grid.cellsz);
        const s32 cz1 = meshGridCell(tzmax, minz, g.grid.cellsz);
        for (s32 cz = cz0; cz <= cz1; cz++) {
            for (s32 cx = cx0; cx <= cx1; cx++) {
                g.lists[static_cast<size_t>(cz * g.grid.cellsx + cx)].push_back(i);
            }
        }
    }

    g.cells.assign(g.lists.size(), meshgridcell{});
    for (size_t i = 0; i < g.lists.size(); i++) {
        g.cells[i].triindices = g.lists[i].empty() ? nullptr : g.lists[i].data();
        g.cells[i].count = static_cast<s32>(g.lists[i].size());
        g.cells[i].capacity = g.cells[i].count;
    }

    g.grid.tris = g.tris.data();
    g.grid.numtris = static_cast<s32>(g.tris.size());
    g.grid.tricapacity = g.grid.numtris;
    g.grid.cells = g.cells.data();
    g.grid.ready = 1;
}

struct CastCounts {
    int casts = 0;
    int hits = 0;
};

void requireSameAsBruteForce(TestGrid &g, std::vector<u32> &stamps, u32 &stamp,
                             const coord &from, const coord &to, u16 mask, CastCounts &counts)
{
    coord bruteNormal{}, gridNormal{};
    s32 bruteTri = -2, gridTri = -2;
    const float brute = meshTrisRayCast(g.tris.data(), static_cast<s32>(g.tris.size()),
                                        &from, &to, mask, &bruteNormal, &bruteTri);
    const float grid = meshGridRayCast(&g.grid, &from, &to, mask, stamps.data(), ++stamp,
                                       &gridNormal, &gridTri);
    INFO("from=(" << from.x << "," << from.y << "," << from.z << ") to=(" << to.x << ","
         << to.y << "," << to.z << ") mask=" << mask << " brute=" << brute << " tri=" << bruteTri
         << " grid=" << grid << " tri=" << gridTri);
    REQUIRE(grid == brute);
    counts.casts++;
    if (brute < 1.0f) {
        counts.hits++;
        REQUIRE(gridTri >= 0);
        const meshtri &t = g.tris[static_cast<size_t>(gridTri)];
        REQUIRE((mask == 0 || (t.flags & mask) != 0));
        REQUIRE(gridNormal.x == t.normal.x);
        REQUIRE(gridNormal.y == t.normal.y);
        REQUIRE(gridNormal.z == t.normal.z);
    } else {
        REQUIRE(gridTri == -1);
        REQUIRE(bruteTri == -1);
    }
}

void addRandomTriangles(TestGrid &g, std::mt19937 &rng, int count, float extent)
{
    std::uniform_real_distribution<float> centre(-extent, extent);
    std::uniform_real_distribution<float> height(-500.0f, 500.0f);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    const u16 flagsets[] = {
        GEOFLAG_FLOOR1 | GEOFLAG_BLOCK_SIGHT | GEOFLAG_BLOCK_SHOOT,
        GEOFLAG_WALL | GEOFLAG_BLOCK_SIGHT | GEOFLAG_BLOCK_SHOOT,
        GEOFLAG_WALL,
    };

    for (int i = 0; i < count; i++) {
        const float size = chance(rng) < 0.7f ? 20.0f + 180.0f * chance(rng) : 200.0f + 1300.0f * chance(rng);
        const coord c = makeCoord(centre(rng), height(rng), centre(rng));
        const coord a = makeCoord(c.x + unit(rng) * size, c.y + unit(rng) * size, c.z + unit(rng) * size);
        const coord b = makeCoord(c.x + unit(rng) * size, c.y + unit(rng) * size, c.z + unit(rng) * size);
        const coord d = makeCoord(c.x + unit(rng) * size, c.y + unit(rng) * size, c.z + unit(rng) * size);
        g.tris.push_back(makeTri(a, b, d, flagsets[i % 3]));
    }
}

} // namespace

TEST_CASE("B-1189 grid-walked mesh ray cast matches a brute-force cast",
          "[meshcollision][raycast][b1189]")
{
    std::mt19937 rng(1189);
    TestGrid g;

    // A floor slab and two walls so axis-aligned and vertical rays meet
    // real surfaces, plus 600 random triangles, some spanning many cells.
    g.tris.push_back(makeTri(makeCoord(-3000, -763, -3000), makeCoord(3000, -763, -3000),
                             makeCoord(-3000, -763, 3000), GEOFLAG_FLOOR1 | GEOFLAG_BLOCK_SHOOT));
    g.tris.push_back(makeTri(makeCoord(3000, -763, -3000), makeCoord(3000, -763, 3000),
                             makeCoord(-3000, -763, 3000), GEOFLAG_FLOOR1 | GEOFLAG_BLOCK_SHOOT));
    g.tris.push_back(makeTri(makeCoord(1000, -800, -3000), makeCoord(1000, 400, -3000),
                             makeCoord(1000, -800, 3000), GEOFLAG_WALL | GEOFLAG_BLOCK_SHOOT));
    g.tris.push_back(makeTri(makeCoord(-1500, -800, 200), makeCoord(-1500, 400, 200),
                             makeCoord(2500, -800, 200), GEOFLAG_WALL));
    addRandomTriangles(g, rng, 600, 3000.0f);
    buildGrid(g);

    std::vector<u32> stamps(g.tris.size(), 0);
    u32 stamp = 0;
    CastCounts counts;
    std::uniform_real_distribution<float> pos(-4200.0f, 4200.0f);
    std::uniform_real_distribution<float> height(-900.0f, 700.0f);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    std::uniform_real_distribution<float> length(1.0f, 9000.0f);

    for (int r = 0; r < 4000; r++) {
        const coord from = makeCoord(pos(rng), height(rng), pos(rng));
        coord dir = makeCoord(unit(rng), unit(rng), unit(rng));
        switch (r % 8) {
        case 0: dir.x = 0.0f; dir.z = 0.0f; dir.y = dir.y < 0.0f ? -1.0f : 1.0f; break; // vertical
        case 1: dir.z = 0.0f; break;                                                // along X
        case 2: dir.x = 0.0f; break;                                                // along Z
        default: break;
        }
        const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (len < 0.001f) {
            continue;
        }
        const float l = length(rng);
        const coord to = makeCoord(from.x + dir.x / len * l, from.y + dir.y / len * l,
                                   from.z + dir.z / len * l);
        requireSameAsBruteForce(g, stamps, stamp, from, to, 0, counts);
        requireSameAsBruteForce(g, stamps, stamp, from, to, GEOFLAG_BLOCK_SHOOT, counts);
    }

    // Bullet-length rays (65536 units, as shotCalculateHits casts them).
    for (int r = 0; r < 300; r++) {
        const coord from = makeCoord(pos(rng) * 0.5f, height(rng), pos(rng) * 0.5f);
        coord dir = makeCoord(unit(rng), unit(rng) * 0.3f, unit(rng));
        const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        const coord to = makeCoord(from.x + dir.x / len * 65536.0f, from.y + dir.y / len * 65536.0f,
                                   from.z + dir.z / len * 65536.0f);
        requireSameAsBruteForce(g, stamps, stamp, from, to, GEOFLAG_BLOCK_SHOOT, counts);
    }

    REQUIRE(counts.casts > 8000);
    REQUIRE(counts.hits > 2000);
}

TEST_CASE("B-1189 shot mask passes movement-only clip walls",
          "[meshcollision][raycast][b1189]")
{
    // A movement-only clip wall (WALL, no BLOCK_SHOOT) at z 200 in front of a
    // solid wall at z 600: every-triangle casts stop at the clip wall, shot
    // casts (BLOCK_SHOOT) go through it to the solid wall.
    TestGrid g;
    g.tris.push_back(makeTri(makeCoord(-500, -800, 200), makeCoord(-500, 400, 200),
                             makeCoord(1500, -800, 200), GEOFLAG_WALL));
    g.tris.push_back(makeTri(makeCoord(-500, -800, 600), makeCoord(-500, 400, 600),
                             makeCoord(1500, -800, 600),
                             GEOFLAG_WALL | GEOFLAG_BLOCK_SIGHT | GEOFLAG_BLOCK_SHOOT));
    buildGrid(g);

    std::vector<u32> stamps(g.tris.size(), 0);
    coord n{};
    s32 tri = -1;
    const coord a = makeCoord(0.0f, -300.0f, 0.0f);
    const coord b = makeCoord(0.0f, -300.0f, 1000.0f);
    REQUIRE(meshGridRayCast(&g.grid, &a, &b, 0, stamps.data(), 1, &n, &tri) == Approx(0.2f));
    REQUIRE(tri == 0);
    REQUIRE(meshGridRayCast(&g.grid, &a, &b, GEOFLAG_BLOCK_SHOOT, stamps.data(), 2, &n, &tri) == Approx(0.6f));
    REQUIRE(tri == 1);
    const coord c = makeCoord(0.0f, -300.0f, 900.0f);
    REQUIRE(meshGridRayCast(&g.grid, &c, &b, GEOFLAG_BLOCK_SHOOT, stamps.data(), 3, &n, &tri) == 1.0f);
    REQUIRE(tri == -1);
}

TEST_CASE("B-1189 ray cast on a capped grid still finds every triangle",
          "[meshcollision][raycast][b1189]")
{
    // 40000 units across: more than MESHGRID_MAX_DIM cells. meshWorldFinalize
    // (mirrored by buildGrid) lists a triangle wholly past the cap in the last
    // row or column (B-1210), and meshGridRayCast walks such a grid like any
    // other; before B-1210 that triangle was in no cell, which this case found.
    std::mt19937 rng(11890);
    TestGrid g;
    addRandomTriangles(g, rng, 500, 20000.0f);
    buildGrid(g);
    REQUIRE(g.grid.cellsx == MESHGRID_MAX_DIM);

    std::vector<u32> stamps(g.tris.size(), 0);
    u32 stamp = 0;
    CastCounts counts;
    std::uniform_real_distribution<float> pos(-21000.0f, 21000.0f);
    std::uniform_real_distribution<float> height(-700.0f, 700.0f);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);

    for (int r = 0; r < 3000; r++) {
        const coord from = makeCoord(pos(rng), height(rng), pos(rng));
        coord dir = makeCoord(unit(rng), unit(rng) * 0.2f, unit(rng));
        const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (len < 0.001f) {
            continue;
        }
        const coord to = makeCoord(from.x + dir.x / len * 30000.0f, from.y + dir.y / len * 30000.0f,
                                   from.z + dir.z / len * 30000.0f);
        requireSameAsBruteForce(g, stamps, stamp, from, to, 0, counts);
    }

    REQUIRE(counts.hits > 200);
}

TEST_CASE("Detached flat walk clears the Grid doorway and rejects obstruction",
          "[meshcollision][grid][navigation]")
{
    std::vector<meshtri> tris;
    addWalkFloor(tris, 1, -1550, 0, -820, 820, 0);
    addWalkFloor(tris, 2, 0, 1550, -820, 820, 0);
    addWalkPartition(tris, -820, -220, 0, 520);
    addWalkPartition(tris, 220, 820, 0, 520);
    addWalkPartition(tris, -220, 220, 340, 520);

    const coord left = makeCoord(-360, 50, -120);
    const coord right = makeCoord(360, 50, -120);
    REQUIRE(checkWalk(tris, left, 1, right, 2));

    SECTION("An oblique route and a standing footprint span the room seam")
    {
        const coord offset = makeCoord(415, 50, 60);
        CHECK(checkWalk(tris, offset, 2, left, 1));
        const coord seam = makeCoord(-12, 50, 0);
        CHECK(checkWalk(tris, seam, 1, seam, 1));
        CHECK_FALSE(checkWalk(tris, left, 2, right, 1));
    }

    SECTION("The diagonal between bundled starts clears the far outer wall")
    {
        // The wall overlaps both projected ranges on this diagonal. The
        // actual wall at x = -1550 stays outside the swept footprint.
        const coord a = makeCoord(-1550, 0, -820);
        const coord b = makeCoord(-1550, 520, -820);
        const coord c = makeCoord(-1550, 520, 820);
        const coord d = makeCoord(-1550, 0, 820);
        tris.push_back(makeTri(a, b, c, GEOFLAG_WALL));
        tris.push_back(makeTri(a, c, d, GEOFLAG_WALL));
        const coord first = makeCoord(-880, 50, 400);
        const coord second = makeCoord(-1150, 50, 0);
        CHECK(checkWalk(tris, first, 1, second, 1));
        CHECK(checkWalk(tris, second, 1, first, 1));
    }

    SECTION("The wall and even a thin lateral graze stop the walk")
    {
        CHECK_FALSE(checkWalk(tris, makeCoord(-360, 50, -420), 1,
            makeCoord(360, 50, -420), 2));
        addWalkPartition(tris, -90, -89, 0, 80);
        CHECK_FALSE(checkWalk(tris, left, 1, right, 2));
    }

    SECTION("The actual pad also needs standing headroom")
    {
        addWalkFloor(tris, 1, -410, -310, -170, -70, 210, GEOFLAG_WALL);
        CHECK_FALSE(checkWalk(tris, left, 1, left, 1));
        CHECK_FALSE(checkWalk(tris, left, 1, right, 2));
    }

    SECTION("An overlapping same-level hazard is unsafe")
    {
        addWalkFloor(tris, 2, 90, 170, -160, -80, 0, GEOFLAG_FLOOR2 | GEOFLAG_DIE);
        CHECK_FALSE(checkWalk(tris, left, 1, right, 2));
    }
}

TEST_CASE("Detached flat walk requires level floor at its ends and across the footprint",
          "[meshcollision][grid][navigation]")
{
    std::vector<meshtri> tris;
    const coord left = makeCoord(-360, 50, -120);
    const coord right = makeCoord(360, 50, -120);

    SECTION("A gap between valid room floors has no support")
    {
        addWalkFloor(tris, 1, -1550, 0, -820, 820, 0);
        addWalkFloor(tris, 2, 64, 1550, -820, 820, 0);
        CHECK_FALSE(checkWalk(tris, left, 1, right, 2));
    }

    SECTION("A supported center line still needs both lateral rails")
    {
        addWalkFloor(tris, 1, -1550, 0, -820, 820, 0);
        addWalkFloor(tris, 2, 0, 1550, -130, 820, 0);
        CHECK_FALSE(checkWalk(tris, left, 1, right, 2));
    }

    SECTION("Unequal ground levels and excessive drop are outside the flat profile")
    {
        addWalkFloor(tris, 1, -1550, 0, -820, 820, 0);
        addWalkFloor(tris, 2, 0, 1550, -820, 820, 40);
        CHECK_FALSE(checkWalk(tris, left, 1, right, 2));
        const coord high = makeCoord(-360, 501, -120);
        CHECK_FALSE(checkWalk(tris, high, 1, high, 1));
    }

    SECTION("An endpoint requires an upward, nonlethal floor")
    {
        addWalkFloor(tris, 1, -1550, 0, -820, 820, 0, GEOFLAG_FLOOR1 | GEOFLAG_DIE);
        CHECK_FALSE(checkWalk(tris, left, 1, left, 1));
        for (auto &tri : tris) {
            tri.flags = GEOFLAG_FLOOR1;
            std::swap(tri.v1, tri.v2);
            tri.normal.y = -1.0f;
        }
        CHECK_FALSE(checkWalk(tris, left, 1, left, 1));
    }
}
