#include <string.h>

#include "player_stage_init_fault.h"

static player_stage_init_fault_plan_t s_PlayerStageInitFault;

static void playerStageInitFaultPlanClear(player_stage_init_fault_plan_t *plan)
{
	if (plan != NULL) {
		memset(plan, 0, sizeof(*plan));
	}
}

player_stage_init_fault_status_t playerStageInitFaultPlanParse(
		const char *spec, s32 player_limit,
		player_stage_init_fault_plan_t *out_plan)
{
	player_stage_init_fault_plan_t candidate;
	const char *digits;
	s32 playernum = 0;

	playerStageInitFaultPlanClear(out_plan);
	playerStageInitFaultPlanClear(&candidate);

	if (spec == NULL || spec[0] == '\0' || out_plan == NULL
			|| player_limit <= 0) {
		return PLAYER_STAGE_INIT_FAULT_INVALID_ARGUMENT;
	}

	if (strncmp(spec, "reset:", 6) == 0) {
		candidate.phase = PLAYER_STAGE_INIT_FAULT_RESET;
		digits = spec + 6;
	} else if (strncmp(spec, "spawn:", 6) == 0) {
		candidate.phase = PLAYER_STAGE_INIT_FAULT_SPAWN;
		digits = spec + 6;
	} else {
		return PLAYER_STAGE_INIT_FAULT_INVALID_PHASE;
	}

	if (*digits < '0' || *digits > '9') {
		return PLAYER_STAGE_INIT_FAULT_INVALID_PLAYER;
	}
	if (*digits == '0' && digits[1] != '\0') {
		return PLAYER_STAGE_INIT_FAULT_INVALID_PLAYER;
	}

	while (*digits >= '0' && *digits <= '9') {
		const s32 digit = *digits - '0';

		if (playernum > (player_limit - 1) / 10) {
			return PLAYER_STAGE_INIT_FAULT_INVALID_PLAYER;
		}
		playernum = playernum * 10 + digit;
		if (playernum >= player_limit) {
			return PLAYER_STAGE_INIT_FAULT_INVALID_PLAYER;
		}
		digits++;
	}

	if (*digits != '\0') {
		return PLAYER_STAGE_INIT_FAULT_TRAILING_DATA;
	}
	candidate.playernum = playernum;
	candidate.armed = 1;
	*out_plan = candidate;
	return PLAYER_STAGE_INIT_FAULT_OK;
}

s32 playerStageInitFaultPlanConsume(player_stage_init_fault_plan_t *plan,
		player_stage_init_fault_phase_t phase, s32 playernum)
{
	if (plan == NULL || !plan->armed || plan->consumed
			|| plan->phase != phase || plan->playernum != playernum) {
		return 0;
	}

	plan->consumed = 1;
	plan->armed = 0;
	return 1;
}

player_stage_init_fault_status_t playerStageInitFaultConfigure(
		const char *spec, s32 smoke_active, s32 player_limit)
{
	playerStageInitFaultClear();
	if (!smoke_active) {
		return PLAYER_STAGE_INIT_FAULT_DISABLED;
	}
	return playerStageInitFaultPlanParse(spec, player_limit,
		&s_PlayerStageInitFault);
}

s32 playerStageInitFaultConsume(player_stage_init_fault_phase_t phase,
		s32 playernum)
{
	return playerStageInitFaultPlanConsume(&s_PlayerStageInitFault, phase,
		playernum);
}

void playerStageInitFaultClear(void)
{
	playerStageInitFaultPlanClear(&s_PlayerStageInitFault);
}

const char *playerStageInitFaultPhaseString(
		player_stage_init_fault_phase_t phase)
{
	switch (phase) {
	case PLAYER_STAGE_INIT_FAULT_RESET: return "reset";
	case PLAYER_STAGE_INIT_FAULT_SPAWN: return "spawn";
	default: return "none";
	}
}

const char *playerStageInitFaultStatusString(
		player_stage_init_fault_status_t status)
{
	switch (status) {
	case PLAYER_STAGE_INIT_FAULT_OK: return "ok";
	case PLAYER_STAGE_INIT_FAULT_DISABLED: return "disabled";
	case PLAYER_STAGE_INIT_FAULT_INVALID_ARGUMENT: return "invalid_argument";
	case PLAYER_STAGE_INIT_FAULT_INVALID_PHASE: return "invalid_phase";
	case PLAYER_STAGE_INIT_FAULT_INVALID_PLAYER: return "invalid_player";
	case PLAYER_STAGE_INIT_FAULT_TRAILING_DATA: return "trailing_data";
	default: return "unknown";
	}
}
