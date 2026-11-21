#include "catch.hpp"

#include "lib/swarm_capsule_sweep.h"
extern "C" {
#include "constants.h"
}

#include <limits>
#include <vector>

namespace {

coord point(f32 x, f32 y, f32 z)
{
	coord result{};
	result.x = x;
	result.y = y;
	result.z = z;
	return result;
}

meshtri triangle(const coord &a, const coord &b, const coord &c, u16 flags, RoomNum room)
{
	meshtri result{};
	result.v0 = a;
	result.v1 = b;
	result.v2 = c;
	result.flags = flags;
	result.roomnum = room;
	return result;
}

void wall(std::vector<meshtri> &tris, f32 x, f32 zmin, f32 zmax)
{
	const coord a = point(x, -8, zmin);
	const coord b = point(x, 8, zmin);
	const coord c = point(x, 8, zmax);
	const coord d = point(x, -8, zmax);
	tris.push_back(triangle(a, b, c, GEOFLAG_WALL, 12));
	tris.push_back(triangle(a, c, d, GEOFLAG_WALL, 12));
}

void horizontal(std::vector<meshtri> &tris, f32 y, f32 xmin, f32 xmax, u16 flags)
{
	const coord a = point(xmin, y, -5);
	const coord b = point(xmax, y, -5);
	const coord c = point(xmax, y, 5);
	const coord d = point(xmin, y, 5);
	tris.push_back(triangle(a, b, c, flags, 23));
	tris.push_back(triangle(a, c, d, flags, 23));
}

swarm_sweep_result sweep(const std::vector<meshtri> &tris, const swarm_capsule &capsule,
		const coord &move, swarm_sweep_contact &contact, u16 mask = 0, s32 limit = 128)
{
	return swarmCapsuleSweepTriangles(&capsule, &move, tris.data(),
		static_cast<s32>(tris.size()), mask, limit, &contact);
}

} // namespace

TEST_CASE("Swarm capsule cannot tunnel through a thin two-sided wall", "[swarm][sweep]")
{
	std::vector<meshtri> tris;
	wall(tris, 0, -5, 5);
	const swarm_capsule capsule{point(-3, 0, 0), point(0, 1, 0), 0.5f, 0.4f};
	const coord move = point(6, 0, 0);
	swarm_sweep_contact first{}, reversed{};
	REQUIRE(sweep(tris, capsule, move, first) == SWARM_SWEEP_HIT);
	CHECK(first.fraction == Approx(5.0 / 12.0).margin(0.00001));
	CHECK(first.fraction <= 5.0 / 12.0);
	CHECK(first.normal.x == Approx(-1));
	CHECK(first.room == 12);
	CHECK(first.flags == GEOFLAG_WALL);

	for (auto &tri : tris) {
		const coord original = tri.v1;
		tri.v1 = tri.v2;
		tri.v2 = original;
		tri.normal = point(0, 1, 0); // A cache normal supplies no sweep geometry.
	}
	REQUIRE(sweep(tris, capsule, move, reversed) == SWARM_SWEEP_HIT);
	CHECK(reversed.fraction == Approx(first.fraction));
	CHECK(reversed.normal.x == Approx(first.normal.x));
	CHECK(sweep(tris, capsule, move, reversed, GEOFLAG_FLOOR1) == SWARM_SWEEP_CLEAR);
	CHECK(reversed.fraction == 1);
}

TEST_CASE("Swarm capsule catches a grazing edge with a geometric contact normal", "[swarm][sweep]")
{
	std::vector<meshtri> tris;
	wall(tris, 0, 0, 5);
	swarm_capsule capsule{point(-3, 0, -0.3f), point(0, 1, 0), 0.5f, 0.4f};
	const coord move = point(6, 0, 0);
	swarm_sweep_contact contact{};
	REQUIRE(sweep(tris, capsule, move, contact) == SWARM_SWEEP_HIT);
	CHECK(contact.fraction == Approx(13.0 / 30.0).margin(0.00002));
	CHECK(contact.point.x == Approx(0).margin(0.00002));
	CHECK(contact.point.z == Approx(0).margin(0.00002));
	CHECK(contact.normal.x == Approx(-0.8).margin(0.0001));
	CHECK(contact.normal.z == Approx(-0.6).margin(0.0001));

	capsule.center.z = -0.51f;
	REQUIRE(sweep(tris, capsule, move, contact) == SWARM_SWEEP_CLEAR);
	CHECK(contact.fraction == 1);
}

TEST_CASE("Swarm capsule distinguishes open clearance from a lowered tunnel roof", "[swarm][sweep]")
{
	std::vector<meshtri> tris;
	horizontal(tris, 0, -5, 5, GEOFLAG_FLOOR1);
	horizontal(tris, 2, -5, 5, GEOFLAG_WALL);
	const swarm_capsule capsule{point(-3, 1, 0), point(0, 1, 0), 0.5f, 0.4f};
	const coord move = point(6, 0, 0);
	swarm_sweep_contact contact{};
	REQUIRE(sweep(tris, capsule, move, contact) == SWARM_SWEEP_CLEAR);
	horizontal(tris, 1.75f, 0, 5, GEOFLAG_WALL);
	REQUIRE(sweep(tris, capsule, move, contact) == SWARM_SWEEP_HIT);
	CHECK(contact.fraction == Approx(0.440488).margin(0.0001));
	CHECK(contact.normal.y == Approx(-0.7).margin(0.0002));
	CHECK(contact.normal.x < 0);
}

TEST_CASE("Swarm capsule permits tangential ceiling motion and separation", "[swarm][sweep]")
{
	std::vector<meshtri> ceiling;
	horizontal(ceiling, 2, -5, 5, GEOFLAG_WALL);
	const swarm_capsule ceiling_actor{point(0, 1.1f, 0), point(0, -1, 0), 0.5f, 0.4f};
	swarm_sweep_contact contact{};
	REQUIRE(sweep(ceiling, ceiling_actor, point(2, 0, 0), contact) == SWARM_SWEEP_CLEAR);
	REQUIRE(sweep(ceiling, ceiling_actor, point(0, -1, 0), contact) == SWARM_SWEEP_CLEAR);
	REQUIRE(sweep(ceiling, ceiling_actor, point(0, 1, 0), contact) == SWARM_SWEEP_HIT);
	CHECK(contact.fraction == 0);
	CHECK(contact.normal.y == Approx(-1));
}

TEST_CASE("Swarm grid includes head and foot cells and deduplicates triangles", "[swarm][sweep]")
{
	auto tri = triangle(point(286, -8, -4), point(286, 8, 0),
		point(286, -8, 4), GEOFLAG_WALL, 8);
	s32 duplicate[] = {0, 0};
	meshgridcell cells[2]{};
	cells[1].triindices = duplicate;
	cells[1].count = 2;
	meshgrid grid{};
	grid.cells = cells;
	grid.cellsx = 2;
	grid.cellsz = 1;
	grid.tris = &tri;
	grid.numtris = 1;
	grid.ready = 1;
	u32 stamps[1]{};
	const coord none = point(0, 0, 0);
	const swarm_capsule horizontal_actor{point(100, 0, 0), point(1, 0, 0), 1, 200};
	swarm_sweep_contact contact{};
	REQUIRE(swarmCapsuleSweepGrid(&horizontal_actor, &none, &grid, 0, 1, stamps, 1, 1,
		&contact) == SWARM_SWEEP_START_OVERLAP);
	CHECK(contact.triangle == 0);
	const swarm_capsule upright_actor{point(100, 0, 0), point(0, 1, 0), 1, 200};
	REQUIRE(swarmCapsuleSweepGrid(&upright_actor, &none, &grid, 0, 1, stamps, 1, 2,
		&contact) == SWARM_SWEEP_CLEAR);
}

TEST_CASE("Swarm capsule does not silently clear penetration or a depleted query", "[swarm][sweep]")
{
	std::vector<meshtri> tris;
	wall(tris, 0, -5, 5);
	swarm_sweep_contact contact{};
	const swarm_capsule embedded{point(-0.15f, 0, 0), point(0, 1, 0), 0.5f, 0.4f};
	REQUIRE(sweep(tris, embedded, point(-1, 0, 0), contact) == SWARM_SWEEP_START_OVERLAP);
	CHECK(contact.fraction == 0);
	const swarm_capsule incoming{point(-3, 0, 0), point(0, 1, 0), 0.5f, 0.4f};
	REQUIRE(sweep(tris, incoming, point(6, 0, 0), contact, 0, 1) == SWARM_SWEEP_CANDIDATE_LIMIT);
	CHECK(contact.fraction == 0);
	CHECK(contact.triangle == -1);
}

TEST_CASE("Swarm capsule accepts spheres and rejects invalid sweep inputs", "[swarm][sweep]")
{
	std::vector<meshtri> tris;
	wall(tris, 0, -5, 5);
	swarm_sweep_contact contact{};
	swarm_capsule capsule{point(-3, 0, 0), point(0, 0, 0), 0.5f, 0};
	REQUIRE(sweep(tris, capsule, point(6, 0, 0), contact) == SWARM_SWEEP_HIT);
	capsule.half_segment = 1;
	REQUIRE(sweep(tris, capsule, point(6, 0, 0), contact) == SWARM_SWEEP_INVALID);
	CHECK(contact.fraction == 0);
	capsule.axis = point(0, 1, 0);
	capsule.center.x = std::numeric_limits<f32>::quiet_NaN();
	REQUIRE(sweep(tris, capsule, point(6, 0, 0), contact) == SWARM_SWEEP_INVALID);
}

TEST_CASE("A very slow Swarm approach cannot deepen an existing tolerance contact", "[swarm][sweep]")
{
	std::vector<meshtri> tris;
	wall(tris, 0, -5, 5);
	const swarm_capsule capsule{point(-0.49999002f, 0, 0), point(0, 1, 0), 0.5f, 0.4f};
	swarm_sweep_contact contact{};
	REQUIRE(sweep(tris, capsule, point(0, 0, 0), contact) == SWARM_SWEEP_CLEAR);
	REQUIRE(sweep(tris, capsule, point(9.5e-8f, 0, 0), contact) == SWARM_SWEEP_HIT);
	CHECK(contact.fraction == 0);
}

TEST_CASE("Swarm grid returns the lower indexed same-time contact in either order", "[swarm][sweep]")
{
	const auto face = triangle(point(0, -5, -5), point(0, 5, 0),
		point(0, -5, 5), GEOFLAG_WALL, 17);
	meshtri tris[] = {face, face};
	tris[1].roomnum = 22;
	s32 indices[] = {1, 0};
	meshgridcell cell{};
	cell.triindices = indices;
	cell.count = 2;
	meshgrid grid{};
	grid.cells = &cell;
	grid.cellsx = 1;
	grid.cellsz = 1;
	grid.tris = tris;
	grid.numtris = 2;
	grid.ready = 1;
	u32 stamps[2]{};
	const swarm_capsule capsule{point(-3, 0, 0), point(0, 1, 0), 0.5f, 0.4f};
	const coord move = point(6, 0, 0);
	swarm_sweep_contact reversed{}, forward{};
	REQUIRE(swarmCapsuleSweepGrid(&capsule, &move, &grid, 0, 2, stamps, 2, 1, &reversed)
		== SWARM_SWEEP_HIT);
	indices[0] = 0;
	indices[1] = 1;
	REQUIRE(swarmCapsuleSweepGrid(&capsule, &move, &grid, 0, 2, stamps, 2, 2, &forward)
		== SWARM_SWEEP_HIT);
	CHECK(reversed.triangle == 0);
	CHECK(forward.triangle == reversed.triangle);
	CHECK(forward.fraction == reversed.fraction);
	CHECK(forward.room == 17);
}

TEST_CASE("Swarm starting overlap takes precedence over an equal-time touch", "[swarm][sweep]")
{
	std::vector<meshtri> tris;
	wall(tris, 0.5f, -5, 5);
	wall(tris, -0.1f, -5, 5);
	const swarm_capsule capsule{point(0, 0, 0), point(0, 1, 0), 0.5f, 0.4f};
	swarm_sweep_contact contact{};
	REQUIRE(sweep(tris, capsule, point(1, 0, 0), contact) == SWARM_SWEEP_START_OVERLAP);
	CHECK(contact.fraction == 0);
	CHECK(contact.triangle >= 2);
}
