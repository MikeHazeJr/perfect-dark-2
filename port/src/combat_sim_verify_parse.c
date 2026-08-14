#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "combat_sim_verify.h"

const char *combatSimKillMatrixStatusString(
		combat_sim_kill_matrix_status_e status)
{
	switch (status) {
	case COMBAT_SIM_KILL_MATRIX_OK: return "ok";
	case COMBAT_SIM_KILL_MATRIX_INVALID_ARGUMENT: return "invalid_argument";
	case COMBAT_SIM_KILL_MATRIX_EMPTY: return "empty";
	case COMBAT_SIM_KILL_MATRIX_INVALID_FORMAT: return "invalid_format";
	case COMBAT_SIM_KILL_MATRIX_OUT_OF_RANGE: return "out_of_range";
	case COMBAT_SIM_KILL_MATRIX_TOO_MANY_ENTRIES: return "too_many_entries";
	case COMBAT_SIM_KILL_MATRIX_DUPLICATE_PAIR: return "duplicate_pair";
	case COMBAT_SIM_KILL_MATRIX_DEATH_TOTAL_OVERFLOW:
		return "death_total_overflow";
	default: return "unknown";
	}
}

combat_sim_kill_matrix_status_e combatSimKillMatrixParse(
		const char *text, combat_sim_kill_entry_t *out_entries,
		size_t out_capacity, size_t *out_count)
{
	combat_sim_kill_entry_t candidate[COMBAT_SIM_KILL_MATRIX_MAX_ENTRIES];
	int death_totals[COMBAT_SIM_KILL_MATRIX_MAX_PARTICIPANTS] = {0};
	const char *cursor = text;
	size_t count = 0;

	if (!text || !out_entries || !out_count || out_capacity == 0
			|| out_capacity > COMBAT_SIM_KILL_MATRIX_MAX_ENTRIES) {
		return COMBAT_SIM_KILL_MATRIX_INVALID_ARGUMENT;
	}
	if (!text[0]) {
		return COMBAT_SIM_KILL_MATRIX_EMPTY;
	}

	for (;;) {
		char *end = NULL;
		long attacker;
		long victim;
		long value;

		if (count >= out_capacity) {
			return COMBAT_SIM_KILL_MATRIX_TOO_MANY_ENTRIES;
		}
		if (!isdigit((unsigned char)*cursor)) {
			return COMBAT_SIM_KILL_MATRIX_INVALID_FORMAT;
		}
		attacker = strtol(cursor, &end, 10);
		if (!end || *end != ':') {
			return COMBAT_SIM_KILL_MATRIX_INVALID_FORMAT;
		}
		cursor = end + 1;
		if (!isdigit((unsigned char)*cursor)) {
			return COMBAT_SIM_KILL_MATRIX_INVALID_FORMAT;
		}
		victim = strtol(cursor, &end, 10);
		if (!end || *end != '=') {
			return COMBAT_SIM_KILL_MATRIX_INVALID_FORMAT;
		}
		cursor = end + 1;
		if (!isdigit((unsigned char)*cursor)) {
			return COMBAT_SIM_KILL_MATRIX_INVALID_FORMAT;
		}
		value = strtol(cursor, &end, 10);

		if (attacker < 0
				|| attacker >= COMBAT_SIM_KILL_MATRIX_MAX_PARTICIPANTS
				|| victim < 0
				|| victim >= COMBAT_SIM_KILL_MATRIX_MAX_PARTICIPANTS
				|| value <= 0 || value > INT16_MAX) {
			return COMBAT_SIM_KILL_MATRIX_OUT_OF_RANGE;
		}
		for (size_t i = 0; i < count; i++) {
			if (candidate[i].attacker == (uint8_t)attacker
					&& candidate[i].victim == (uint8_t)victim) {
				return COMBAT_SIM_KILL_MATRIX_DUPLICATE_PAIR;
			}
		}
		if (death_totals[victim] > INT16_MAX - value) {
			return COMBAT_SIM_KILL_MATRIX_DEATH_TOTAL_OVERFLOW;
		}
		death_totals[victim] += (int)value;
		candidate[count].attacker = (uint8_t)attacker;
		candidate[count].victim = (uint8_t)victim;
		candidate[count].count = (int16_t)value;
		count++;

		if (*end == '\0') {
			break;
		}
		if (*end != ',') {
			return COMBAT_SIM_KILL_MATRIX_INVALID_FORMAT;
		}
		cursor = end + 1;
		if (!*cursor) {
			return COMBAT_SIM_KILL_MATRIX_INVALID_FORMAT;
		}
	}

	memcpy(out_entries, candidate, count * sizeof(candidate[0]));
	*out_count = count;
	return COMBAT_SIM_KILL_MATRIX_OK;
}
