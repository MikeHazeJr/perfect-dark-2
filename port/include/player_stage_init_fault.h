#ifndef PD_PLAYER_STAGE_INIT_FAULT_H
#define PD_PLAYER_STAGE_INIT_FAULT_H

#include <PR/ultratypes.h>

/* This header is included by legacy game translation units after types.h,
 * where bool is intentionally an s32 macro. Keep the shared C/C++ boundary on
 * s32 so including the smoke seam cannot redefine unrelated game prototypes. */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum player_stage_init_fault_phase {
	PLAYER_STAGE_INIT_FAULT_NONE = 0,
	PLAYER_STAGE_INIT_FAULT_RESET,
	PLAYER_STAGE_INIT_FAULT_SPAWN,
} player_stage_init_fault_phase_t;

typedef enum player_stage_init_fault_status {
	PLAYER_STAGE_INIT_FAULT_OK = 0,
	PLAYER_STAGE_INIT_FAULT_DISABLED,
	PLAYER_STAGE_INIT_FAULT_INVALID_ARGUMENT,
	PLAYER_STAGE_INIT_FAULT_INVALID_PHASE,
	PLAYER_STAGE_INIT_FAULT_INVALID_PLAYER,
	PLAYER_STAGE_INIT_FAULT_TRAILING_DATA,
} player_stage_init_fault_status_t;

typedef struct player_stage_init_fault_plan {
	player_stage_init_fault_phase_t phase;
	s32 playernum;
	s32 armed;
	s32 consumed;
} player_stage_init_fault_plan_t;

/* Parse the exact smoke-only form "reset:<player>" or "spawn:<player>".
 * Rejection always leaves out_plan disabled. */
player_stage_init_fault_status_t playerStageInitFaultPlanParse(
	const char *spec, s32 player_limit, player_stage_init_fault_plan_t *out_plan);

/* Consume only an exact phase/player match. A mismatch leaves the one-shot
 * armed so the intended later stage can still reach it. */
s32 playerStageInitFaultPlanConsume(player_stage_init_fault_plan_t *plan,
	player_stage_init_fault_phase_t phase, s32 playernum);

/* Process-global wrapper used by the ordinary client. Configuration is
 * rejected unless the smoke harness is active. */
player_stage_init_fault_status_t playerStageInitFaultConfigure(
	const char *spec, s32 smoke_active, s32 player_limit);
s32 playerStageInitFaultConsume(player_stage_init_fault_phase_t phase,
	s32 playernum);
void playerStageInitFaultClear(void);

const char *playerStageInitFaultPhaseString(
	player_stage_init_fault_phase_t phase);
const char *playerStageInitFaultStatusString(
	player_stage_init_fault_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* PD_PLAYER_STAGE_INIT_FAULT_H */
