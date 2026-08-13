#ifndef PD_SCENARIO_ROOM_DOMAIN_H
#define PD_SCENARIO_ROOM_DOMAIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RoomNum is signed 16-bit. A count of 32768 therefore covers every
 * representable non-negative room index without inventing a smaller ceiling. */
#define SCENARIO_ROOM_DOMAIN_MIN_COUNT 2
#define SCENARIO_ROOM_DOMAIN_MAX_COUNT 32768

typedef enum scenario_room_domain_result {
	SCENARIO_ROOM_DOMAIN_OK = 0,
	SCENARIO_ROOM_DOMAIN_INVALID_ARGUMENT,
	SCENARIO_ROOM_DOMAIN_INVALID_DECLARED_COUNT,
	SCENARIO_ROOM_DOMAIN_OBSERVED_ROOM_OUT_OF_RANGE,
	SCENARIO_ROOM_DOMAIN_DECLARED_COUNT_TOO_SMALL,
} scenario_room_domain_result_t;

scenario_room_domain_result_t scenarioRoomDomainResolve(
	int32_t declared_room_count,
	int32_t mesh_max_room,
	int32_t portal_max_room,
	int32_t *out_room_count);

int scenarioRoomDomainContains(int32_t room_count, int32_t roomnum);
const char *scenarioRoomDomainResultName(scenario_room_domain_result_t result);

#ifdef __cplusplus
}
#endif

#endif
