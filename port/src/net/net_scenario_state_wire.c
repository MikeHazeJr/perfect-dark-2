#include "net/net_scenario_state_wire.h"
#include "net/netbuf.h"

#include <float.h>
#include <string.h>

/* scenario id + revision */
#define NET_SCENARIO_STATE_ENVELOPE_BYTES 5u
/* hill_index, hill_room, three floats, occupied_team, elapsed240, move_hill,
 * hill_time */
#define NET_SCENARIO_STATE_KOH_BYTES (2u + 2u + 12u + 1u + 2u + 1u + 1u)

u32 netScenarioStatePayloadSize(u8 scenario)
{
	switch (scenario) {
	case NET_SCENARIO_STATE_KINGOFTHEHILL:
		return NET_SCENARIO_STATE_ENVELOPE_BYTES + NET_SCENARIO_STATE_KOH_BYTES;
	default:
		return 0;
	}
}

static net_scenario_state_status_e netScenarioKohValidate(const net_scenario_koh_state_t *koh)
{
	s32 i;

	if (koh->hill_index < -1 || koh->hill_index >= NET_SCENARIO_KOH_MAX_HILLS) {
		return NET_SCENARIO_STATE_BAD_FIELD;
	}
	if (koh->hill_room < -1) {
		return NET_SCENARIO_STATE_BAD_FIELD;
	}
	for (i = 0; i < 3; i++) {
		/* Finite check without <math.h>: the game tree ships its own math.h
		 * ahead of the system one. NaN fails both comparisons, infinities
		 * fail one. Same idiom as net_player_move_wire.c. */
		if (!(koh->hill_pos[i] >= -FLT_MAX && koh->hill_pos[i] <= FLT_MAX)) {
			return NET_SCENARIO_STATE_BAD_FIELD;
		}
	}
	if (koh->occupied_team < -1 || koh->occupied_team >= NET_SCENARIO_STATE_MAX_TEAMS) {
		return NET_SCENARIO_STATE_BAD_FIELD;
	}
	if (koh->elapsed240 < 0) {
		return NET_SCENARIO_STATE_BAD_FIELD;
	}
	if (koh->move_hill > 1) {
		return NET_SCENARIO_STATE_BAD_FIELD;
	}
	return NET_SCENARIO_STATE_OK;
}

net_scenario_state_status_e netScenarioStateValidate(const net_scenario_state_t *state)
{
	if (!state) {
		return NET_SCENARIO_STATE_BAD_ARGUMENT;
	}
	switch (state->scenario) {
	case NET_SCENARIO_STATE_KINGOFTHEHILL:
		return netScenarioKohValidate(&state->body.koh);
	default:
		return NET_SCENARIO_STATE_BAD_SCENARIO;
	}
}

s32 netScenarioStatePayloadWrite(struct netbuf *dst, const net_scenario_state_t *state)
{
	u32 required;

	if (!dst || !state || dst->error) {
		return 0;
	}
	if (netScenarioStateValidate(state) != NET_SCENARIO_STATE_OK) {
		dst->error = 1;
		return 0;
	}
	required = netScenarioStatePayloadSize(state->scenario);
	if (required == 0 || netbufWriteLeft(dst) < 0
			|| required > (u32)netbufWriteLeft(dst)) {
		dst->error = 1;
		return 0;
	}

	netbufWriteU8(dst, state->scenario);
	netbufWriteU32(dst, state->revision);

	switch (state->scenario) {
	case NET_SCENARIO_STATE_KINGOFTHEHILL: {
		const net_scenario_koh_state_t *koh = &state->body.koh;
		netbufWriteS16(dst, koh->hill_index);
		netbufWriteS16(dst, koh->hill_room);
		netbufWriteF32(dst, koh->hill_pos[0]);
		netbufWriteF32(dst, koh->hill_pos[1]);
		netbufWriteF32(dst, koh->hill_pos[2]);
		netbufWriteS8(dst, koh->occupied_team);
		netbufWriteS16(dst, koh->elapsed240);
		netbufWriteU8(dst, koh->move_hill);
		netbufWriteU8(dst, koh->hill_time);
		break;
	}
	default:
		/* Unreachable: validation and the size lookup rejected it above. */
		dst->error = 1;
		return 0;
	}

	return !dst->error;
}

net_scenario_state_status_e netScenarioStatePayloadRead(struct netbuf *src,
		net_scenario_state_t *out)
{
	net_scenario_state_t candidate;
	net_scenario_state_status_e status;
	u32 required;

	if (!src || !out || src->error) {
		return NET_SCENARIO_STATE_BAD_ARGUMENT;
	}

	memset(&candidate, 0, sizeof(candidate));

	if (netbufReadLeft(src) < 1) {
		src->error = 1;
		return NET_SCENARIO_STATE_TRUNCATED;
	}
	candidate.scenario = netbufReadU8(src);
	required = netScenarioStatePayloadSize(candidate.scenario);
	if (required == 0) {
		src->error = 1;
		return NET_SCENARIO_STATE_BAD_SCENARIO;
	}
	/* The id byte is already consumed. Check the rest fits before reading it,
	 * so a short payload is reported as such rather than as bad fields. */
	if (netbufReadLeft(src) < 0 || (u32)netbufReadLeft(src) < required - 1u) {
		src->error = 1;
		return NET_SCENARIO_STATE_TRUNCATED;
	}
	candidate.revision = netbufReadU32(src);

	switch (candidate.scenario) {
	case NET_SCENARIO_STATE_KINGOFTHEHILL: {
		net_scenario_koh_state_t *koh = &candidate.body.koh;
		koh->hill_index = netbufReadS16(src);
		koh->hill_room = netbufReadS16(src);
		koh->hill_pos[0] = netbufReadF32(src);
		koh->hill_pos[1] = netbufReadF32(src);
		koh->hill_pos[2] = netbufReadF32(src);
		koh->occupied_team = netbufReadS8(src);
		koh->elapsed240 = netbufReadS16(src);
		koh->move_hill = netbufReadU8(src);
		koh->hill_time = netbufReadU8(src);
		break;
	}
	default:
		src->error = 1;
		return NET_SCENARIO_STATE_BAD_SCENARIO;
	}

	if (src->error) {
		return NET_SCENARIO_STATE_TRUNCATED;
	}

	status = netScenarioStateValidate(&candidate);
	if (status != NET_SCENARIO_STATE_OK) {
		src->error = 1;
		return status;
	}

	*out = candidate;
	return NET_SCENARIO_STATE_OK;
}
