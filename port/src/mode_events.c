/*
 * mode_events.c -- record-only Combat Simulator mode events: see
 * mode_events.h and context/designs/modding/gamemode-graphs.md (slice 1).
 */

#include <string.h>
#include <PR/ultratypes.h>
#include "types.h"
#include "constants.h"
#include "bss.h"
#include "system.h"
#include "assetcatalog.h"
#include "game/mplayer/scenarios.h"
#include "mode_events.h"

static const char *const s_ModeEventNames[MODE_EVENT_COUNT] = {
	"chosen", "stage_reset", "intro_pad", "props_init", "tick", "chr_tick",
	"score", "spawn", "token_picked_up", "token_dropped", "prop_activated",
	"player_died",
};

static s32 s_ModeEventTotals[MODE_EVENT_COUNT];

/* The first event of each kind for a mode logs a line; a match can change
 * mode between its chosen and its stage events, so the seen table keys on
 * both. Six base modes today; a slot past them is the catch-all. */
#define MODE_EVENT_SEEN_MODES 8
static u8 s_ModeEventSeen[MODE_EVENT_SEEN_MODES][MODE_EVENT_COUNT];

const char *modeEventKindName(s32 kind)
{
	if (kind < 0 || kind >= MODE_EVENT_COUNT) {
		return NULL;
	}
	return s_ModeEventNames[kind];
}

s32 modeEventTotal(s32 kind)
{
	if (kind < 0 || kind >= MODE_EVENT_COUNT) {
		return 0;
	}
	return s_ModeEventTotals[kind];
}

static const char *modeEventModeId(s32 scenario)
{
	const char *id = catalogGameModeIdByScenarioIndex(scenario);
	return id && id[0] ? id : "-";
}

void modeEventRecord(s32 kind, s32 who)
{
	const s32 scenario = (s32)g_MpSetup.scenario;
	s32 seen;

	if (kind < 0 || kind >= MODE_EVENT_COUNT) {
		sysLogPrintf(LOG_WARNING, "MODE.EVENT: unknown kind %d", kind);
		return;
	}
	/* The peer that decides the outcome records: offline, or the listen host. */
	if (!scenarioNetIsAuthority()) {
		return;
	}

	s_ModeEventTotals[kind]++;

	seen = scenario >= 0 && scenario < MODE_EVENT_SEEN_MODES - 1
		? scenario : MODE_EVENT_SEEN_MODES - 1;
	if (!s_ModeEventSeen[seen][kind]) {
		s_ModeEventSeen[seen][kind] = 1;
		if (who >= 0) {
			sysLogPrintf(LOG_NOTE, "MODE.EVENT: %s mode=%s scenario=%d who=%d",
				s_ModeEventNames[kind], modeEventModeId(scenario), scenario, who);
		} else {
			sysLogPrintf(LOG_NOTE, "MODE.EVENT: %s mode=%s scenario=%d who=-",
				s_ModeEventNames[kind], modeEventModeId(scenario), scenario);
		}
	}
}

void modeEventsReport(const char *why)
{
	const s32 scenario = (s32)g_MpSetup.scenario;
	s32 any = 0;

	for (s32 i = 0; i < MODE_EVENT_COUNT; i++) {
		any |= s_ModeEventTotals[i] != 0;
	}
	if (!any) {
		memset(s_ModeEventSeen, 0, sizeof(s_ModeEventSeen));
		return;
	}

	sysLogPrintf(LOG_NOTE,
		"MODE.EVENTS: %s mode=%s chosen=%d stage_reset=%d intro_pad=%d props_init=%d"
		" tick=%d chr_tick=%d score=%d spawn=%d token_picked_up=%d token_dropped=%d"
		" prop_activated=%d player_died=%d",
		why ? why : "report", modeEventModeId(scenario),
		s_ModeEventTotals[MODE_EVENT_CHOSEN],
		s_ModeEventTotals[MODE_EVENT_STAGE_RESET],
		s_ModeEventTotals[MODE_EVENT_INTRO_PAD],
		s_ModeEventTotals[MODE_EVENT_PROPS_INIT],
		s_ModeEventTotals[MODE_EVENT_TICK],
		s_ModeEventTotals[MODE_EVENT_CHR_TICK],
		s_ModeEventTotals[MODE_EVENT_SCORE],
		s_ModeEventTotals[MODE_EVENT_SPAWN],
		s_ModeEventTotals[MODE_EVENT_TOKEN_PICKED_UP],
		s_ModeEventTotals[MODE_EVENT_TOKEN_DROPPED],
		s_ModeEventTotals[MODE_EVENT_PROP_ACTIVATED],
		s_ModeEventTotals[MODE_EVENT_PLAYER_DIED]);
	memset(s_ModeEventTotals, 0, sizeof(s_ModeEventTotals));
	memset(s_ModeEventSeen, 0, sizeof(s_ModeEventSeen));
}
