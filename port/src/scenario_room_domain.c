#include "scenario_room_domain.h"

#include <float.h>

scenario_room_domain_result_t scenarioRoomDomainResolve(
	int32_t declared_room_count,
	int32_t mesh_max_room,
	int32_t portal_max_room,
	int32_t *out_room_count)
{
	int32_t observed_max_room;
	int32_t resolved_count;

	if (!out_room_count) {
		return SCENARIO_ROOM_DOMAIN_INVALID_ARGUMENT;
	}

	*out_room_count = 0;

	if (declared_room_count < 0
			|| declared_room_count == 1
			|| declared_room_count > SCENARIO_ROOM_DOMAIN_MAX_COUNT) {
		return SCENARIO_ROOM_DOMAIN_INVALID_DECLARED_COUNT;
	}

	observed_max_room = mesh_max_room > portal_max_room
		? mesh_max_room : portal_max_room;

	if (observed_max_room >= SCENARIO_ROOM_DOMAIN_MAX_COUNT) {
		return SCENARIO_ROOM_DOMAIN_OBSERVED_ROOM_OUT_OF_RANGE;
	}

	if (declared_room_count > 0) {
		if (observed_max_room >= declared_room_count) {
			return SCENARIO_ROOM_DOMAIN_DECLARED_COUNT_TOO_SMALL;
		}
		resolved_count = declared_room_count;
	} else {
		resolved_count = observed_max_room + 1;
		if (resolved_count < SCENARIO_ROOM_DOMAIN_MIN_COUNT) {
			resolved_count = SCENARIO_ROOM_DOMAIN_MIN_COUNT;
		}
	}

	*out_room_count = resolved_count;
	return SCENARIO_ROOM_DOMAIN_OK;
}

int scenarioRoomDomainContains(int32_t room_count, int32_t roomnum)
{
	return room_count >= SCENARIO_ROOM_DOMAIN_MIN_COUNT
		&& room_count <= SCENARIO_ROOM_DOMAIN_MAX_COUNT
		&& roomnum >= 0
		&& roomnum < room_count;
}

const char *scenarioRoomDomainResultName(scenario_room_domain_result_t result)
{
	switch (result) {
	case SCENARIO_ROOM_DOMAIN_OK:
		return "ok";
	case SCENARIO_ROOM_DOMAIN_INVALID_ARGUMENT:
		return "invalid_argument";
	case SCENARIO_ROOM_DOMAIN_INVALID_DECLARED_COUNT:
		return "invalid_declared_count";
	case SCENARIO_ROOM_DOMAIN_OBSERVED_ROOM_OUT_OF_RANGE:
		return "observed_room_out_of_range";
	case SCENARIO_ROOM_DOMAIN_DECLARED_COUNT_TOO_SMALL:
		return "declared_count_too_small";
	default:
		return "unknown";
	}
}

int scenarioRoomBoundsValid(const scenario_room_bounds_t *box, int authored)
{
	if (!box) return 0;
	float diagonal_squared = 0.0f;
	for (int axis = 0; axis < 3; ++axis) {
		const float low = box->minimum[axis];
		const float high = box->maximum[axis];
		if (!__builtin_isfinite(low) || !__builtin_isfinite(high) || low > high || (authored && low == high)) return 0;
		/* Same float intermediates the native background uses. */
		const float center_sum = low + high;
		const float extent = high - low;
		if (!__builtin_isfinite(center_sum)) return 0;
		diagonal_squared += extent * extent;
	}
	return __builtin_isfinite(diagonal_squared) && (!authored || diagonal_squared > 0.0f);
}

static int scenarioRoomEdgeCovers(float outer, float inner, int maximum)
{
	/* Bounds can round at six decimal places during export. A few float
	 * ULPs also cover independent rounding in the native collision source. */
	const double largest = __builtin_fmax(__builtin_fabs((double)outer), __builtin_fabs((double)inner));
	const double tolerance = 1.0e-6 + 4.0 * FLT_EPSILON * largest;
	const double outside = maximum ? (double)inner - outer : (double)outer - inner;
	return outside <= tolerance;
}

int scenarioRoomBoundsContain(const scenario_room_bounds_t *outer,
	const scenario_room_bounds_t *inner)
{
	if (!outer || !inner) return 0;
	for (int axis = 0; axis < 3; ++axis) {
		if (!scenarioRoomEdgeCovers(outer->minimum[axis], inner->minimum[axis], 0)
				|| !scenarioRoomEdgeCovers(outer->maximum[axis], inner->maximum[axis], 1)) return 0;
	}
	return 1;
}

int scenarioRoomOriginValid(const float origin[3])
{
	if (!origin) return 0;
	for (int axis = 0; axis < 3; ++axis) {
		const double rounded = __builtin_roundf(origin[axis]);
		/* The light loader rounds to s32, then adds a native s16 local corner. */
		if (!__builtin_isfinite(rounded) || rounded < (double)INT32_MIN - INT16_MIN
				|| rounded > (double)INT32_MAX - INT16_MAX) return 0;
	}
	return 1;
}
