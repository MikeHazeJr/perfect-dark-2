#include <float.h>
#include <math.h>

#include "lib/swarm_capsule_sweep.h"

enum { SWEEP_ITERATIONS = 32 };

typedef struct { double x, y, z; } sweepvec;

struct sweep_pair {
	sweepvec spine;
	sweepvec surface;
	double distance_squared;
};

struct sweep_triangle {
	sweepvec vertices[3];
	sweepvec normal;
};

struct sweep_query {
	sweepvec center;
	sweepvec offset;
	sweepvec move;
	sweepvec low;
	sweepvec high;
	double radius;
	double tolerance;
	double fraction;
	u16 mask;
	s32 remaining;
	enum swarm_sweep_result result;
	struct swarm_sweep_contact contact;
};

static sweepvec as_vec(struct coord v)
{
	return (sweepvec){v.x, v.y, v.z};
}

static f32 as_float(double value)
{
	return (f32)fmin(FLT_MAX, fmax(-FLT_MAX, value));
}

static struct coord as_coord(sweepvec v)
{
	return (struct coord){.x = as_float(v.x), .y = as_float(v.y), .z = as_float(v.z)};
}

static sweepvec add(sweepvec a, sweepvec b)
{
	return (sweepvec){a.x + b.x, a.y + b.y, a.z + b.z};
}

static sweepvec sub(sweepvec a, sweepvec b)
{
	return (sweepvec){a.x - b.x, a.y - b.y, a.z - b.z};
}

static sweepvec scaled(sweepvec a, double n)
{
	return (sweepvec){a.x * n, a.y * n, a.z * n};
}

static double dot(sweepvec a, sweepvec b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

static sweepvec cross(sweepvec a, sweepvec b)
{
	return (sweepvec){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x};
}

static s32 finite_vec(sweepvec a)
{
	return isfinite(a.x) && isfinite(a.y) && isfinite(a.z);
}

static sweepvec unit(sweepvec v)
{
	const double length_squared = dot(v, v);
	return length_squared > 0.0 ? scaled(v, 1.0 / sqrt(length_squared)) : v;
}

static sweepvec minima(sweepvec a, sweepvec b)
{
	return (sweepvec){fmin(a.x, b.x), fmin(a.y, b.y), fmin(a.z, b.z)};
}

static sweepvec maxima(sweepvec a, sweepvec b)
{
	return (sweepvec){fmax(a.x, b.x), fmax(a.y, b.y), fmax(a.z, b.z)};
}

static sweepvec point_on_segment(sweepvec point, sweepvec a, sweepvec b)
{
	const sweepvec line = sub(b, a);
	const double length_squared = dot(line, line);
	if (length_squared == 0.0) return a;
	const double fraction = dot(sub(point, a), line) / length_squared;
	return add(a, scaled(line, fmin(1.0, fmax(0.0, fraction))));
}

static void closest(struct sweep_pair *pair, sweepvec spine, sweepvec surface)
{
	const sweepvec separation = sub(spine, surface);
	const double distance_squared = dot(separation, separation);
	if (distance_squared < pair->distance_squared) {
		*pair = (struct sweep_pair){spine, surface, distance_squared};
	}
}

static void consider_edge(struct sweep_pair *pair, sweepvec a, sweepvec b, sweepvec c, sweepvec d)
{
	closest(pair, a, point_on_segment(a, c, d));
	closest(pair, b, point_on_segment(b, c, d));
	closest(pair, point_on_segment(c, a, b), c);
	closest(pair, point_on_segment(d, a, b), d);

	/* The endpoints cover any boundary minimum. The infinite lines add the
	 * interior minimum when both parameters lie inside their segments. */
	const sweepvec u = sub(b, a);
	const sweepvec v = sub(d, c);
	const sweepvec w = sub(a, c);
	const sweepvec perpendicular = cross(u, v);
	const double determinant = dot(perpendicular, perpendicular);
	if (determinant > 0.0) {
		const double s = dot(cross(v, w), perpendicular) / determinant;
		const double t = dot(cross(u, w), perpendicular) / determinant;
		if (s >= 0.0 && s <= 1.0 && t >= 0.0 && t <= 1.0) {
			closest(pair, add(a, scaled(u, s)), add(c, scaled(v, t)));
		}
	}
}

static s32 in_triangle(sweepvec point, const struct sweep_triangle *triangle)
{
	for (s32 i = 0; i < 3; i++) {
		const sweepvec a = triangle->vertices[i];
		const sweepvec b = triangle->vertices[(i + 1) % 3];
		if (dot(cross(sub(b, a), sub(point, a)), triangle->normal) < 0.0) return 0;
	}
	return 1;
}

static struct sweep_pair closest_to_triangle(sweepvec a, sweepvec b,
		const struct sweep_triangle *triangle)
{
	struct sweep_pair pair = {a, triangle->vertices[0], DBL_MAX};
	const sweepvec normal = triangle->normal;

	if (dot(normal, normal) > 0.0) {
		const sweepvec origin = triangle->vertices[0];
		const double from = dot(sub(a, origin), normal);
		const double to = dot(sub(b, origin), normal);
		const double direction = from - to;
		if (direction != 0.0) {
			const double fraction = from / direction;
			if (fraction >= 0.0 && fraction <= 1.0) {
				const sweepvec crossing = add(a, scaled(sub(b, a), fraction));
				if (in_triangle(crossing, triangle)) {
					return (struct sweep_pair){crossing, crossing, 0.0};
				}
			}
		}
		const sweepvec p = sub(a, scaled(normal, from));
		const sweepvec q = sub(b, scaled(normal, to));
		if (in_triangle(p, triangle)) closest(&pair, a, p);
		if (in_triangle(q, triangle)) closest(&pair, b, q);
	}
	for (s32 i = 0; i < 3; i++) {
		consider_edge(&pair, a, b, triangle->vertices[i], triangle->vertices[(i + 1) % 3]);
	}
	return pair;
}

static void keep_contact(struct sweep_query *query, enum swarm_sweep_result result,
		double fraction, s32 index, const struct meshtri *triangle, sweepvec point, sweepvec normal)
{
	if (query->result != SWARM_SWEEP_CLEAR && fraction == query->fraction) {
		/* Penetration takes priority over a touching surface. The lowest
		 * triangle index then makes ties independent of grid traversal. */
		if (query->result == SWARM_SWEEP_START_OVERLAP && result != SWARM_SWEEP_START_OVERLAP) return;
		if (result != SWARM_SWEEP_START_OVERLAP || query->result == SWARM_SWEEP_START_OVERLAP) {
			if (index >= query->contact.triangle) return;
		}
	}
	query->result = result;
	query->fraction = fraction;
	const f32 rounded = (f32)fraction;
	query->contact.fraction = rounded > fraction ? nextafterf(rounded, 0.0f) : rounded;
	query->contact.point = as_coord(point);
	query->contact.normal = as_coord(normal);
	query->contact.triangle = index;
	query->contact.room = triangle->roomnum;
	query->contact.flags = triangle->flags;
}

static sweepvec overlap_normal(const struct sweep_query *query,
		const struct sweep_triangle *triangle, sweepvec center)
{
	sweepvec normal = triangle->normal;
	if (dot(normal, normal) == 0.0) {
		normal = dot(query->move, query->move) > 0.0 ? unit(scaled(query->move, -1.0))
			: (sweepvec){0.0, 1.0, 0.0};
	} else if (dot(sub(center, triangle->vertices[0]), normal) < 0.0) {
		normal = scaled(normal, -1.0);
	}
	return normal;
}

static s32 test_triangle(struct sweep_query *query, const struct meshtri *source, s32 index)
{
	if (query->mask && !(source->flags & query->mask)) return 1;
	struct sweep_triangle triangle = {{as_vec(source->v0), as_vec(source->v1), as_vec(source->v2)}, {0}};
	if (!finite_vec(triangle.vertices[0]) || !finite_vec(triangle.vertices[1])
			|| !finite_vec(triangle.vertices[2])) {
		query->result = SWARM_SWEEP_INVALID;
		return 0;
	}
	const sweepvec v0 = triangle.vertices[0];
	const sweepvec v1 = triangle.vertices[1];
	const sweepvec v2 = triangle.vertices[2];
	const sweepvec low = minima(v0, minima(v1, v2));
	const sweepvec high = maxima(v0, maxima(v1, v2));
	if (high.x < query->low.x || low.x > query->high.x
			|| high.y < query->low.y || low.y > query->high.y
			|| high.z < query->low.z || low.z > query->high.z) return 1;

	if (query->remaining == 0) {
		query->result = SWARM_SWEEP_CANDIDATE_LIMIT;
		return 0;
	}
	query->remaining--;
	triangle.normal = unit(cross(sub(v1, v0), sub(v2, v0)));

	double fraction = 0.0;
	double previous_fraction = 0.0;
	for (s32 iteration = 0; iteration < SWEEP_ITERATIONS; iteration++) {
		const sweepvec center = add(query->center, scaled(query->move, fraction));
		const struct sweep_pair pair = closest_to_triangle(sub(center, query->offset),
			add(center, query->offset), &triangle);
		const double distance = sqrt(pair.distance_squared);
		const double gap = distance - query->radius;
		const sweepvec normal = distance > 0.0 ? scaled(sub(pair.spine, pair.surface), 1.0 / distance)
			: overlap_normal(query, &triangle, center);

		if (gap < -query->tolerance) {
			const enum swarm_sweep_result result = fraction == 0.0
				? SWARM_SWEEP_START_OVERLAP : SWARM_SWEEP_ITERATION_LIMIT;
			keep_contact(query, result, previous_fraction, index, source, pair.surface, normal);
			return 1;
		}
		const double closing = -dot(normal, query->move);
		if (closing <= 0.0) return 1;
		/* Ignore numerical closing on a tangent only if its projected
		 * penetration also stays within tolerance through the move. */
		if (closing <= query->tolerance * 0.01
				&& gap - closing * (query->fraction - fraction) >= -query->tolerance) return 1;
		if (gap <= query->tolerance) {
			keep_contact(query, SWARM_SWEEP_HIT, fraction, index, source, pair.surface, normal);
			return 1;
		}

		/* The nearest pair defines a supporting separation plane for this
		 * fixed-axis translated convex shape. Advancing to that plane
		 * cannot step over a face, edge, or vertex contact. */
		const double next = fraction + (gap - query->tolerance * 0.5) / closing;
		if (next > query->fraction) return 1;
		if (next <= fraction || iteration + 1 == SWEEP_ITERATIONS) {
			keep_contact(query, SWARM_SWEEP_ITERATION_LIMIT, fraction, index, source, pair.surface, normal);
			return 1;
		}
		previous_fraction = fraction;
		fraction = next;
	}
	return 1;
}

static s32 init_query(struct sweep_query *query, const struct swarm_capsule *capsule,
		const struct coord *translation, u16 mask, s32 candidate_limit)
{
	if (!capsule || !translation || candidate_limit <= 0) return 0;

	const sweepvec center = as_vec(capsule->center);
	const sweepvec axis = as_vec(capsule->axis);
	const sweepvec move = as_vec(*translation);
	if (!finite_vec(center) || !finite_vec(axis) || !finite_vec(move)
			|| !isfinite(capsule->radius) || !isfinite(capsule->half_segment)
			|| capsule->radius <= 0.0f || capsule->half_segment < 0.0f
			|| (capsule->half_segment > 0.0f && dot(axis, axis) == 0.0)) return 0;

	const sweepvec offset = scaled(unit(axis), capsule->half_segment);
	const double tolerance = fmin(capsule->radius * 0.01, fmax(0.00001, capsule->radius * 0.000001));
	const double extent = capsule->radius + tolerance;
	const sweepvec extents = {fabs(offset.x) + extent, fabs(offset.y) + extent, fabs(offset.z) + extent};
	const sweepvec end = add(center, move);
	*query = (struct sweep_query){
		.center = center, .offset = offset, .move = move,
		.low = sub(minima(center, end), extents), .high = add(maxima(center, end), extents),
		.radius = capsule->radius, .tolerance = tolerance, .fraction = 1.0,
		.mask = mask, .remaining = candidate_limit, .result = SWARM_SWEEP_CLEAR,
		.contact = {.fraction = 1.0f, .triangle = -1, .room = -1},
	};
	return 1;
}

static struct swarm_sweep_contact empty_contact(void)
{
	return (struct swarm_sweep_contact){.triangle = -1, .room = -1};
}

static enum swarm_sweep_result finish(struct sweep_query *query, struct swarm_sweep_contact *contact)
{
	if (query->result == SWARM_SWEEP_INVALID || query->result == SWARM_SWEEP_CANDIDATE_LIMIT) {
		*contact = empty_contact();
	} else {
		*contact = query->contact;
	}
	return query->result;
}

enum swarm_sweep_result swarmCapsuleSweepTriangles(const struct swarm_capsule *capsule,
		const struct coord *translation, const struct meshtri *triangles,
		s32 count, u16 mask, s32 candidate_limit, struct swarm_sweep_contact *contact)
{
	struct sweep_query query;
	if (!contact) return SWARM_SWEEP_INVALID;
	*contact = empty_contact();
	if (count < 0 || (count > 0 && !triangles)
			|| !init_query(&query, capsule, translation, mask, candidate_limit)) return SWARM_SWEEP_INVALID;

	for (s32 i = 0; i < count; i++) {
		if (!test_triangle(&query, &triangles[i], i)) break;
	}
	return finish(&query, contact);
}

/* Round the broadphase outwards so a double endpoint just across a cell
 * edge cannot round back into the adjacent cell during the float lookup. */
static f32 grid_bound(double value, s32 upper)
{
	const f32 rounded = as_float(value);
	if (upper && rounded < value && rounded < FLT_MAX) return nextafterf(rounded, FLT_MAX);
	if (!upper && rounded > value && rounded > -FLT_MAX) return nextafterf(rounded, -FLT_MAX);
	return rounded;
}

enum swarm_sweep_result swarmCapsuleSweepGrid(const struct swarm_capsule *capsule,
		const struct coord *translation, const struct meshgrid *grid,
		u16 mask, s32 candidate_limit, u32 *stamps, s32 stamp_count,
		u32 mark, struct swarm_sweep_contact *contact)
{
	struct sweep_query query;
	if (!contact) return SWARM_SWEEP_INVALID;
	*contact = empty_contact();
	if (!grid || !grid->ready || !grid->tris || !grid->cells || !stamps || !mark
			|| grid->numtris < 0 || stamp_count < grid->numtris
			|| grid->cellsx < 1 || grid->cellsx > MESHGRID_MAX_DIM
			|| grid->cellsz < 1 || grid->cellsz > MESHGRID_MAX_DIM
			|| !isfinite(grid->originx) || !isfinite(grid->originz)
			|| !init_query(&query, capsule, translation, mask, candidate_limit)) return SWARM_SWEEP_INVALID;

	const s32 x0 = meshGridCell(grid_bound(query.low.x, 0), grid->originx, grid->cellsx);
	const s32 x1 = meshGridCell(grid_bound(query.high.x, 1), grid->originx, grid->cellsx);
	const s32 z0 = meshGridCell(grid_bound(query.low.z, 0), grid->originz, grid->cellsz);
	const s32 z1 = meshGridCell(grid_bound(query.high.z, 1), grid->originz, grid->cellsz);

	for (s32 z = z0; z <= z1; z++) {
		for (s32 x = x0; x <= x1; x++) {
			const struct meshgridcell *cell = &grid->cells[z * grid->cellsx + x];
			if (cell->count < 0 || (cell->count > 0 && !cell->triindices)) {
				query.result = SWARM_SWEEP_INVALID;
				return finish(&query, contact);
			}
			for (s32 i = 0; i < cell->count; i++) {
				const s32 index = cell->triindices[i];
				if (index < 0 || index >= grid->numtris) {
					query.result = SWARM_SWEEP_INVALID;
					return finish(&query, contact);
				}
				if (stamps[index] == mark) continue;
				stamps[index] = mark;
				if (!test_triangle(&query, &grid->tris[index], index)) return finish(&query, contact);
			}
		}
	}
	return finish(&query, contact);
}
