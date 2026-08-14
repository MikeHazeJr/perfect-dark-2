#ifndef PD_COMBAT_SIM_VERIFY_H
#define PD_COMBAT_SIM_VERIFY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime participant indices reserve 0..7 for players and 8..39 for bots.
 * combat_sim_verify.c compile-checks this pure parser boundary against
 * MAX_MPCHRS so future participant-capacity changes cannot drift silently. */
#define COMBAT_SIM_KILL_MATRIX_MAX_PARTICIPANTS 40
#define COMBAT_SIM_KILL_MATRIX_MAX_ENTRIES 64

typedef struct combat_sim_kill_entry {
	uint8_t attacker;
	uint8_t victim;
	int16_t count;
} combat_sim_kill_entry_t;

typedef enum combat_sim_kill_matrix_status {
	COMBAT_SIM_KILL_MATRIX_OK = 0,
	COMBAT_SIM_KILL_MATRIX_INVALID_ARGUMENT,
	COMBAT_SIM_KILL_MATRIX_EMPTY,
	COMBAT_SIM_KILL_MATRIX_INVALID_FORMAT,
	COMBAT_SIM_KILL_MATRIX_OUT_OF_RANGE,
	COMBAT_SIM_KILL_MATRIX_TOO_MANY_ENTRIES,
	COMBAT_SIM_KILL_MATRIX_DUPLICATE_PAIR,
	COMBAT_SIM_KILL_MATRIX_DEATH_TOTAL_OVERFLOW,
} combat_sim_kill_matrix_status_e;

/* Parse a complete deterministic kill matrix such as
 * "0:0=4,0:8=1,8:0=2". The destination and count are published only after
 * the entire expression passes syntax, range, duplicate, and death-total
 * validation. */
combat_sim_kill_matrix_status_e combatSimKillMatrixParse(
		const char *text, combat_sim_kill_entry_t *out_entries,
		size_t out_capacity, size_t *out_count);
const char *combatSimKillMatrixStatusString(
		combat_sim_kill_matrix_status_e status);

/* Opt-in ordinary-client verification lifecycle. All functions are inert
 * unless --debug-combat-sim-cycles or --debug-match-kill-matrix is present. */
void combatSimVerifyInit(void);
void combatSimVerifyTick(void);
void combatSimVerifyOnMatchStart(const char *path);
void combatSimVerifyPrepareAwards(void);
void combatSimVerifyOnMatchEnd(void);
void combatSimVerifyOnRoomReturn(void);
int combatSimVerifyConsumeRoomAutoStart(void);
void combatSimVerifyRecordAwardParticipant(int participant, int total_kills,
		int suicides, int deaths);
void combatSimVerifyRecordKillMaster(int participant, int total_kills,
		int awarded_human);

#ifdef __cplusplus
}
#endif

#endif /* PD_COMBAT_SIM_VERIFY_H */
