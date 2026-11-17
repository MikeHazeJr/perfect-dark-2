#include "net/net_scenario_state_wire.h"
#include "net/netbuf.h"

#include <float.h>
#include <string.h>

/* scenario id + revision */
#define NET_SCENARIO_STATE_ENVELOPE_BYTES 5u
/* hill_index, hill_room, three floats, occupied_team, elapsed240, move_hill,
 * hill_time, contested, scoring_team, scores */
#define NET_SCENARIO_STATE_KOH_BYTES (2u + 2u + 12u + 1u + 2u + 1u + 1u + 1u + 1u + 2u)
/* victim, victim_serial, age240, then two s16 counts per character slot */
#define NET_SCENARIO_STATE_PAC_BYTES (1u + 1u + 2u + 4u * NET_SCENARIO_STATE_MAX_CHRS)
/* holder, present, three floats */
#define NET_SCENARIO_STATE_HTB_BYTES (1u + 1u + 12u)
/* uplink holder, present, three floats, dl_player, dl_terminal,
 * player_in_range, then a u16 and an s16 per character slot */
#define NET_SCENARIO_STATE_HTM_BYTES (1u + 1u + 12u + 1u + 1u + 1u + 4u * NET_SCENARIO_STATE_MAX_CHRS)
/* per base slot: team index, home pad, spawn pad count, spawn pads; per team:
 * holder, present, three floats */
#define NET_SCENARIO_STATE_CTC_BYTES (NET_SCENARIO_CTC_BASES * (1u + 2u + 1u + 2u * NET_SCENARIO_CTC_SPAWNPADS) \
		+ NET_SCENARIO_CTC_BASES * (1u + 1u + 12u))

u32 netScenarioStatePayloadSize(u8 scenario)
{
	switch (scenario) {
	case NET_SCENARIO_STATE_KINGOFTHEHILL:
		return NET_SCENARIO_STATE_ENVELOPE_BYTES + NET_SCENARIO_STATE_KOH_BYTES;
	case NET_SCENARIO_STATE_POPACAP:
		return NET_SCENARIO_STATE_ENVELOPE_BYTES + NET_SCENARIO_STATE_PAC_BYTES;
	case NET_SCENARIO_STATE_HOLDTHEBRIEFCASE:
		return NET_SCENARIO_STATE_ENVELOPE_BYTES + NET_SCENARIO_STATE_HTB_BYTES;
	case NET_SCENARIO_STATE_HACKERCENTRAL:
		return NET_SCENARIO_STATE_ENVELOPE_BYTES + NET_SCENARIO_STATE_HTM_BYTES;
	case NET_SCENARIO_STATE_CAPTURETHECASE:
		return NET_SCENARIO_STATE_ENVELOPE_BYTES + NET_SCENARIO_STATE_CTC_BYTES;
	default:
		return 0;
	}
}

static net_scenario_state_status_e netScenarioCtcValidate(const net_scenario_ctc_state_t *ctc)
{
	s32 seen[NET_SCENARIO_CTC_BASES] = { 0 };
	s32 i;
	s32 j;

	for (i = 0; i < NET_SCENARIO_CTC_BASES; i++) {
		if (ctc->teamindexes[i] < -1 || ctc->teamindexes[i] >= NET_SCENARIO_CTC_BASES) {
			return NET_SCENARIO_STATE_BAD_FIELD;
		}
		if (ctc->teamindexes[i] >= 0) {
			if (seen[ctc->teamindexes[i]]) {
				return NET_SCENARIO_STATE_BAD_FIELD;
			}
			seen[ctc->teamindexes[i]] = 1;
		}
		if (ctc->homepads[i] < -1 || ctc->numspawnpads[i] > NET_SCENARIO_CTC_SPAWNPADS) {
			return NET_SCENARIO_STATE_BAD_FIELD;
		}
		for (j = 0; j < NET_SCENARIO_CTC_SPAWNPADS; j++) {
			/* listed pads are real pads; the rest are -1 */
			if (j < ctc->numspawnpads[i] ? ctc->spawnpads[i][j] < 0 : ctc->spawnpads[i][j] != -1) {
				return NET_SCENARIO_STATE_BAD_FIELD;
			}
		}
		if (ctc->holders[i] < -1 || ctc->holders[i] >= NET_SCENARIO_STATE_MAX_CHRS) {
			return NET_SCENARIO_STATE_BAD_FIELD;
		}
		if (ctc->present[i] > 1 || (ctc->holders[i] >= 0 && !ctc->present[i])) {
			return NET_SCENARIO_STATE_BAD_FIELD;
		}
		for (j = 0; j < 3; j++) {
			if (!(ctc->pos[i][j] >= -FLT_MAX && ctc->pos[i][j] <= FLT_MAX)) {
				return NET_SCENARIO_STATE_BAD_FIELD;
			}
		}
	}
	return NET_SCENARIO_STATE_OK;
}

static net_scenario_state_status_e netScenarioHtmValidate(const net_scenario_htm_state_t *htm)
{
	s32 i;

	if (htm->uplink_holder < -1 || htm->uplink_holder >= NET_SCENARIO_STATE_MAX_CHRS) {
		return NET_SCENARIO_STATE_BAD_FIELD;
	}
	if (htm->uplink_present > 1 || (htm->uplink_holder >= 0 && !htm->uplink_present)) {
		return NET_SCENARIO_STATE_BAD_FIELD;
	}
	for (i = 0; i < 3; i++) {
		if (!(htm->uplink_pos[i] >= -FLT_MAX && htm->uplink_pos[i] <= FLT_MAX)) {
			return NET_SCENARIO_STATE_BAD_FIELD;
		}
	}
	if (htm->dl_player < -1 || htm->dl_player >= NET_SCENARIO_STATE_MAX_CHRS) {
		return NET_SCENARIO_STATE_BAD_FIELD;
	}
	if (htm->dl_terminal < -1 || htm->dl_terminal >= NET_SCENARIO_HTM_MAX_TERMINALS) {
		return NET_SCENARIO_STATE_BAD_FIELD;
	}
	/* A download has both a player and a terminal, or neither. */
	if ((htm->dl_player < 0) != (htm->dl_terminal < 0)) {
		return NET_SCENARIO_STATE_BAD_FIELD;
	}
	if (htm->player_in_range < -1 || htm->player_in_range >= NET_SCENARIO_STATE_MAX_CHRS) {
		return NET_SCENARIO_STATE_BAD_FIELD;
	}
	for (i = 0; i < NET_SCENARIO_STATE_MAX_CHRS; i++) {
		if (htm->dltime240[i] > NET_SCENARIO_HTM_DOWNLOAD240 || htm->numpoints[i] < 0) {
			return NET_SCENARIO_STATE_BAD_FIELD;
		}
	}
	return NET_SCENARIO_STATE_OK;
}

static net_scenario_state_status_e netScenarioHtbValidate(const net_scenario_htb_state_t *htb)
{
	s32 i;

	if (htb->holder < -1 || htb->holder >= NET_SCENARIO_STATE_MAX_CHRS) {
		return NET_SCENARIO_STATE_BAD_FIELD;
	}
	if (htb->present > 1 || (htb->holder >= 0 && !htb->present)) {
		return NET_SCENARIO_STATE_BAD_FIELD;
	}
	for (i = 0; i < 3; i++) {
		if (!(htb->pos[i] >= -FLT_MAX && htb->pos[i] <= FLT_MAX)) {
			return NET_SCENARIO_STATE_BAD_FIELD;
		}
	}
	return NET_SCENARIO_STATE_OK;
}

static net_scenario_state_status_e netScenarioPacValidate(const net_scenario_pac_state_t *pac)
{
	s32 i;

	if (pac->victim < -1 || pac->victim >= NET_SCENARIO_STATE_MAX_CHRS) {
		return NET_SCENARIO_STATE_BAD_FIELD;
	}
	if (pac->age240 > NET_SCENARIO_PAC_MAX_AGE240) {
		return NET_SCENARIO_STATE_BAD_FIELD;
	}
	for (i = 0; i < NET_SCENARIO_STATE_MAX_CHRS; i++) {
		if (pac->killcounts[i] < 0 || pac->survivalcounts[i] < 0) {
			return NET_SCENARIO_STATE_BAD_FIELD;
		}
	}
	return NET_SCENARIO_STATE_OK;
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
	if (koh->contested > 1) {
		return NET_SCENARIO_STATE_BAD_FIELD;
	}
	if (koh->scoring_team < -1 || koh->scoring_team >= NET_SCENARIO_STATE_MAX_TEAMS) {
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
	case NET_SCENARIO_STATE_POPACAP:
		return netScenarioPacValidate(&state->body.pac);
	case NET_SCENARIO_STATE_HOLDTHEBRIEFCASE:
		return netScenarioHtbValidate(&state->body.htb);
	case NET_SCENARIO_STATE_HACKERCENTRAL:
		return netScenarioHtmValidate(&state->body.htm);
	case NET_SCENARIO_STATE_CAPTURETHECASE:
		return netScenarioCtcValidate(&state->body.ctc);
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
		netbufWriteU8(dst, koh->contested);
		netbufWriteS8(dst, koh->scoring_team);
		netbufWriteU16(dst, koh->scores);
		break;
	}
	case NET_SCENARIO_STATE_POPACAP: {
		const net_scenario_pac_state_t *pac = &state->body.pac;
		s32 i;
		netbufWriteS8(dst, pac->victim);
		netbufWriteU8(dst, pac->victim_serial);
		netbufWriteU16(dst, pac->age240);
		for (i = 0; i < NET_SCENARIO_STATE_MAX_CHRS; i++) {
			netbufWriteS16(dst, pac->killcounts[i]);
		}
		for (i = 0; i < NET_SCENARIO_STATE_MAX_CHRS; i++) {
			netbufWriteS16(dst, pac->survivalcounts[i]);
		}
		break;
	}
	case NET_SCENARIO_STATE_HOLDTHEBRIEFCASE: {
		const net_scenario_htb_state_t *htb = &state->body.htb;
		netbufWriteS8(dst, htb->holder);
		netbufWriteU8(dst, htb->present);
		netbufWriteF32(dst, htb->pos[0]);
		netbufWriteF32(dst, htb->pos[1]);
		netbufWriteF32(dst, htb->pos[2]);
		break;
	}
	case NET_SCENARIO_STATE_HACKERCENTRAL: {
		const net_scenario_htm_state_t *htm = &state->body.htm;
		s32 i;
		netbufWriteS8(dst, htm->uplink_holder);
		netbufWriteU8(dst, htm->uplink_present);
		netbufWriteF32(dst, htm->uplink_pos[0]);
		netbufWriteF32(dst, htm->uplink_pos[1]);
		netbufWriteF32(dst, htm->uplink_pos[2]);
		netbufWriteS8(dst, htm->dl_player);
		netbufWriteS8(dst, htm->dl_terminal);
		netbufWriteS8(dst, htm->player_in_range);
		for (i = 0; i < NET_SCENARIO_STATE_MAX_CHRS; i++) {
			netbufWriteU16(dst, htm->dltime240[i]);
		}
		for (i = 0; i < NET_SCENARIO_STATE_MAX_CHRS; i++) {
			netbufWriteS16(dst, htm->numpoints[i]);
		}
		break;
	}
	case NET_SCENARIO_STATE_CAPTURETHECASE: {
		const net_scenario_ctc_state_t *ctc = &state->body.ctc;
		s32 i;
		s32 j;
		for (i = 0; i < NET_SCENARIO_CTC_BASES; i++) {
			netbufWriteS8(dst, ctc->teamindexes[i]);
			netbufWriteS16(dst, ctc->homepads[i]);
			netbufWriteU8(dst, ctc->numspawnpads[i]);
			for (j = 0; j < NET_SCENARIO_CTC_SPAWNPADS; j++) {
				netbufWriteS16(dst, ctc->spawnpads[i][j]);
			}
		}
		for (i = 0; i < NET_SCENARIO_CTC_BASES; i++) {
			netbufWriteS8(dst, ctc->holders[i]);
			netbufWriteU8(dst, ctc->present[i]);
			netbufWriteF32(dst, ctc->pos[i][0]);
			netbufWriteF32(dst, ctc->pos[i][1]);
			netbufWriteF32(dst, ctc->pos[i][2]);
		}
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
		koh->contested = netbufReadU8(src);
		koh->scoring_team = netbufReadS8(src);
		koh->scores = netbufReadU16(src);
		break;
	}
	case NET_SCENARIO_STATE_POPACAP: {
		net_scenario_pac_state_t *pac = &candidate.body.pac;
		s32 i;
		pac->victim = netbufReadS8(src);
		pac->victim_serial = netbufReadU8(src);
		pac->age240 = netbufReadU16(src);
		for (i = 0; i < NET_SCENARIO_STATE_MAX_CHRS; i++) {
			pac->killcounts[i] = netbufReadS16(src);
		}
		for (i = 0; i < NET_SCENARIO_STATE_MAX_CHRS; i++) {
			pac->survivalcounts[i] = netbufReadS16(src);
		}
		break;
	}
	case NET_SCENARIO_STATE_HOLDTHEBRIEFCASE: {
		net_scenario_htb_state_t *htb = &candidate.body.htb;
		htb->holder = netbufReadS8(src);
		htb->present = netbufReadU8(src);
		htb->pos[0] = netbufReadF32(src);
		htb->pos[1] = netbufReadF32(src);
		htb->pos[2] = netbufReadF32(src);
		break;
	}
	case NET_SCENARIO_STATE_HACKERCENTRAL: {
		net_scenario_htm_state_t *htm = &candidate.body.htm;
		s32 i;
		htm->uplink_holder = netbufReadS8(src);
		htm->uplink_present = netbufReadU8(src);
		htm->uplink_pos[0] = netbufReadF32(src);
		htm->uplink_pos[1] = netbufReadF32(src);
		htm->uplink_pos[2] = netbufReadF32(src);
		htm->dl_player = netbufReadS8(src);
		htm->dl_terminal = netbufReadS8(src);
		htm->player_in_range = netbufReadS8(src);
		for (i = 0; i < NET_SCENARIO_STATE_MAX_CHRS; i++) {
			htm->dltime240[i] = netbufReadU16(src);
		}
		for (i = 0; i < NET_SCENARIO_STATE_MAX_CHRS; i++) {
			htm->numpoints[i] = netbufReadS16(src);
		}
		break;
	}
	case NET_SCENARIO_STATE_CAPTURETHECASE: {
		net_scenario_ctc_state_t *ctc = &candidate.body.ctc;
		s32 i;
		s32 j;
		for (i = 0; i < NET_SCENARIO_CTC_BASES; i++) {
			ctc->teamindexes[i] = netbufReadS8(src);
			ctc->homepads[i] = netbufReadS16(src);
			ctc->numspawnpads[i] = netbufReadU8(src);
			for (j = 0; j < NET_SCENARIO_CTC_SPAWNPADS; j++) {
				ctc->spawnpads[i][j] = netbufReadS16(src);
			}
		}
		for (i = 0; i < NET_SCENARIO_CTC_BASES; i++) {
			ctc->holders[i] = netbufReadS8(src);
			ctc->present[i] = netbufReadU8(src);
			ctc->pos[i][0] = netbufReadF32(src);
			ctc->pos[i][1] = netbufReadF32(src);
			ctc->pos[i][2] = netbufReadF32(src);
		}
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
