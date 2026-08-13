#include "scenario_room_domain.h"

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
