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

// Mirrors meshWorldFinalize (src/lib/meshcollision.c): origin at the mesh
// minimum, MESHGRID_CELL_SIZE cells capped at MESHGRID_MAX_DIM per axis, and
// each triangle listed in every cell its XZ bounds overlap, indices clamped.
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
        s32 cx0 = static_cast<s32>((txmin - minx) / MESHGRID_CELL_SIZE);
        s32 cx1 = static_cast<s32>((txmax - minx) / MESHGRID_CELL_SIZE);
        s32 cz0 = static_cast<s32>((tzmin - minz) / MESHGRID_CELL_SIZE);
        s32 cz1 = static_cast<s32>((tzmax - minz) / MESHGRID_CELL_SIZE);
        cx0 = std::max(cx0, 0);
        cz0 = std::max(cz0, 0);
        cx1 = std::min(cx1, g.grid.cellsx - 1);
        cz1 = std::min(cz1, g.grid.cellsz - 1);
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
    // (mirrored by buildGrid) lists no cell for a triangle wholly past the cap
    // (B-1210), so meshGridRayCast tests every triangle on such a grid; this
    // case found that gap.
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
