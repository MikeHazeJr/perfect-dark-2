#include <stdlib.h>
#include <string.h>

#include <ultra64.h>

#include "constants.h"
#include "types.h"
#include "bss.h"
#include "data.h"
#include "system.h"
#include "combat_sim_verify.h"
#include "net/net.h"
#include "net/matchsetup.h"
#include "game/mplayer/mplayer.h"
#include "game/mplayer/participant.h"

_Static_assert(COMBAT_SIM_KILL_MATRIX_MAX_PARTICIPANTS == MAX_MPCHRS,
	"combat sim verifier participant domain must match MAX_MPCHRS");

static combat_sim_kill_entry_t s_KillEntries[
	COMBAT_SIM_KILL_MATRIX_MAX_ENTRIES];
static size_t s_KillEntryCount;
static s32 s_KillDelaySeconds = 18;
static s32 s_CycleTarget;
static s32 s_CycleStarts;
static s32 s_CycleReturns;
static s32 s_MatchActive;
static s32 s_MatrixReady;
static s32 s_MatrixApplied;
static s32 s_RoomAutoStartPending;
static s32 s_Enabled;

static s32 combatSimVerifyParseBoundedInt(const char *text, s32 min_value,
		s32 max_value, s32 *out)
{
	char *end = NULL;
	long value;

	if (!text || !text[0] || !out) {
		return 0;
	}
	value = strtol(text, &end, 10);
	if (!end || *end != '\0' || value < min_value || value > max_value) {
		return 0;
	}
	*out = (s32)value;
	return 1;
}

void combatSimVerifyInit(void)
{
	const char *cycles = sysArgGetString("--debug-combat-sim-cycles");
	const char *matrix = sysArgGetString("--debug-match-kill-matrix");
	const char *delay = sysArgGetString("--debug-match-kill-matrix-delay-sec");
	combat_sim_kill_matrix_status_e matrix_status =
		COMBAT_SIM_KILL_MATRIX_OK;

	if (!cycles && !matrix && !delay) {
		return;
	}

	if (cycles && !combatSimVerifyParseBoundedInt(cycles, 2, 8,
			&s_CycleTarget)) {
		sysLogPrintf(LOG_ERROR,
			"MATCH.CYCLE: config rejected reason=invalid_cycle_target value='%s'",
			cycles);
		return;
	}
	if (delay && !combatSimVerifyParseBoundedInt(delay, 1, 600,
			&s_KillDelaySeconds)) {
		sysLogPrintf(LOG_ERROR,
			"MATCH.CYCLE: config rejected reason=invalid_matrix_delay value='%s'",
			delay);
		return;
	}
	if (delay && !matrix) {
		sysLogPrintf(LOG_ERROR,
			"MATCH.CYCLE: config rejected reason=matrix_delay_without_matrix");
		return;
	}
	if (matrix) {
		matrix_status = combatSimKillMatrixParse(matrix, s_KillEntries,
			COMBAT_SIM_KILL_MATRIX_MAX_ENTRIES, &s_KillEntryCount);
		if (matrix_status != COMBAT_SIM_KILL_MATRIX_OK) {
			sysLogPrintf(LOG_ERROR,
				"MATCH.CYCLE: config rejected reason=invalid_kill_matrix status=%s",
				combatSimKillMatrixStatusString(matrix_status));
			return;
		}
	}
	if (!sysArgCheck("--no-net") || !sysArgCheck("--debug-auto-start-match")
			|| !sysArgGetString("--launch-mp-room")) {
		sysLogPrintf(LOG_ERROR,
			"MATCH.CYCLE: config rejected reason=requires_offline_launch_and_auto_start");
		return;
	}

	s_Enabled = 1;
	sysLogPrintf(LOG_NOTE,
		"MATCH.CYCLE: armed target=%d kill_entries=%u matrix_delay_sec=%d",
		s_CycleTarget, (unsigned)s_KillEntryCount, s_KillDelaySeconds);
}

void combatSimVerifyOnMatchStart(const char *path)
{
	if (!s_Enabled || g_NetMode != NETMODE_NONE) {
		return;
	}
	s_CycleStarts++;
	s_MatchActive = 1;
	s_MatrixReady = 0;
	s_MatrixApplied = 0;
	sysLogPrintf(LOG_NOTE,
		"MATCH.CYCLE: start ordinal=%d target=%d path=%s user_options=0x%08x forced=0x%08x",
		s_CycleStarts, s_CycleTarget, path ? path : "unknown",
		matchConfigGetUserOptions(), g_MatchConfig.options_engine_forced);
}

void combatSimVerifyOnMatchEnd(void)
{
	if (!s_Enabled || !s_MatchActive) {
		return;
	}
	sysLogPrintf(LOG_NOTE,
		"MATCH.CYCLE: end ordinal=%d target=%d matrix_applied=%d user_options=0x%08x forced=0x%08x",
		s_CycleStarts, s_CycleTarget, s_MatrixApplied,
		matchConfigGetUserOptions(), g_MatchConfig.options_engine_forced);
	s_MatchActive = 0;
}

void combatSimVerifyOnRoomReturn(void)
{
	if (!s_Enabled || s_CycleStarts <= 0) {
		return;
	}
	s_CycleReturns++;
	s_RoomAutoStartPending = s_CycleTarget > 0
		&& s_CycleStarts < s_CycleTarget;
	sysLogPrintf(LOG_NOTE,
		"MATCH.CYCLE: return ordinal=%d starts=%d target=%d rearm=%d user_options=0x%08x forced=0x%08x",
		s_CycleReturns, s_CycleStarts, s_CycleTarget,
		s_RoomAutoStartPending, matchConfigGetUserOptions(),
		g_MatchConfig.options_engine_forced);
}

int combatSimVerifyConsumeRoomAutoStart(void)
{
	if (!s_Enabled || !s_RoomAutoStartPending) {
		return 0;
	}
	s_RoomAutoStartPending = 0;
	sysLogPrintf(LOG_NOTE,
		"MATCH.CYCLE: consuming room auto-start next_ordinal=%d target=%d",
		s_CycleStarts + 1, s_CycleTarget);
	return 1;
}

static s32 combatSimVerifyCollectActive(s32 *active, s32 *active_count,
		const char *phase)
{
	for (s32 i = mpParticipantFirst(); i >= 0; i = mpParticipantNext(i)) {
		if (i >= 0 && i < COMBAT_SIM_KILL_MATRIX_MAX_PARTICIPANTS) {
			active[i] = 1;
			(*active_count)++;
		}
	}
	for (size_t i = 0; i < s_KillEntryCount; i++) {
		if (!active[s_KillEntries[i].attacker]
				|| !active[s_KillEntries[i].victim]) {
			sysLogPrintf(LOG_ERROR,
				"MATCH.STATS.FIXTURE: rejected cycle=%d reason=inactive_participant attacker=%u victim=%u",
				s_CycleStarts, (unsigned)s_KillEntries[i].attacker,
				(unsigned)s_KillEntries[i].victim);
			sysLogPrintf(LOG_ERROR,
				"MATCH.STATS.FIXTURE: rejected cycle=%d phase=%s",
				s_CycleStarts, phase ? phase : "unknown");
			return 0;
		}
	}
	return 1;
}

void combatSimVerifyTick(void)
{
	s32 active[COMBAT_SIM_KILL_MATRIX_MAX_PARTICIPANTS] = {0};
	s32 active_count = 0;

	if (!s_Enabled || !s_MatchActive || s_MatrixReady || s_MatrixApplied
			|| s_KillEntryCount == 0 || g_NetMode != NETMODE_NONE
			|| !g_Vars.mplayerisrunning
			|| g_Vars.stagenum != g_MpSetup.stagenum
			|| !g_Vars.players[0] || !g_Vars.players[0]->prop
			|| g_Vars.lvframenum < s_KillDelaySeconds * 60) {
		return;
	}
	if (!combatSimVerifyCollectActive(active, &active_count, "ready")) {
		s_MatrixApplied = -1;
		return;
	}
	s_MatrixReady = 1;
	sysLogPrintf(LOG_NOTE,
		"MATCH.STATS.FIXTURE: ready cycle=%d entries=%u active=%d stage=0x%02x lvframe=%d",
		s_CycleStarts, (unsigned)s_KillEntryCount, active_count,
		(unsigned)g_Vars.stagenum, g_Vars.lvframenum);
}

void combatSimVerifyPrepareAwards(void)
{
	s32 active[COMBAT_SIM_KILL_MATRIX_MAX_PARTICIPANTS] = {0};
	s32 active_count = 0;

	if (!s_Enabled || !s_MatchActive || s_MatrixApplied
			|| s_KillEntryCount == 0) {
		return;
	}
	if (!s_MatrixReady) {
		sysLogPrintf(LOG_ERROR,
			"MATCH.STATS.FIXTURE: rejected cycle=%d reason=matrix_not_ready lvframe=%d",
			s_CycleStarts, g_Vars.lvframenum);
		s_MatrixApplied = -1;
		return;
	}
	if (!combatSimVerifyCollectActive(active, &active_count, "commit")) {
		s_MatrixApplied = -1;
		return;
	}

	for (s32 i = 0; i < COMBAT_SIM_KILL_MATRIX_MAX_PARTICIPANTS; i++) {
		if (active[i]) {
			memset(MPCHR(i)->killcounts, 0, sizeof(MPCHR(i)->killcounts));
			MPCHR(i)->numdeaths = 0;
		}
	}
	for (size_t i = 0; i < s_KillEntryCount; i++) {
		const combat_sim_kill_entry_t *entry = &s_KillEntries[i];
		MPCHR(entry->attacker)->killcounts[entry->victim] = entry->count;
		MPCHR(entry->victim)->numdeaths += entry->count;
		sysLogPrintf(LOG_NOTE,
			"MATCH.STATS.FIXTURE: cycle=%d attacker=%u victim=%u count=%d",
			s_CycleStarts, (unsigned)entry->attacker,
			(unsigned)entry->victim, (s32)entry->count);
	}
	s_MatrixApplied = 1;
	sysLogPrintf(LOG_NOTE,
		"MATCH.STATS.FIXTURE: committed cycle=%d entries=%u active=%d lvframe=%d",
		s_CycleStarts, (unsigned)s_KillEntryCount, active_count,
		g_Vars.lvframenum);
}

void combatSimVerifyRecordAwardParticipant(int participant, int total_kills,
		int suicides, int deaths)
{
	if (!s_Enabled || !s_MatchActive) {
		return;
	}
	sysLogPrintf(LOG_NOTE,
		"MATCH.AWARDS: cycle=%d participant=%d total_kills=%d suicides=%d deaths=%d",
		s_CycleStarts, participant, total_kills, suicides, deaths);
}

void combatSimVerifyRecordKillMaster(int participant, int total_kills,
		int awarded_human)
{
	if (!s_Enabled || !s_MatchActive) {
		return;
	}
	sysLogPrintf(LOG_NOTE,
		"MATCH.AWARDS: cycle=%d killmaster winner=%d total_kills=%d awarded_human=%d",
		s_CycleStarts, participant, total_kills, awarded_human);
}
