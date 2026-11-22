#ifndef _IN_MODE_EVENTS_H
#define _IN_MODE_EVENTS_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Combat Simulator mode events, record-only
 * (context/designs/modding/gamemode-graphs.md, slice 1). The mode dispatchers
 * in scenarios.c and the token, terminal and death routines outside the mode
 * table call modeEventRecord at their seams, after the native decision;
 * nothing here changes the game. Each event counts toward the match's totals,
 * and the first of each kind for a mode logs one line:
 *
 *   MODE.EVENT: <kind> mode=<catalog id> scenario=<n> who=<n or ->
 *
 * who is the seam's own number for the event's player or simulant (a chrnum
 * where the seam has a chr, the Combat Simulator index for a score or a
 * death), or - when the event has none. Events are recorded only where this peer decides a match's outcome
 * (scenarioNetIsAuthority: offline, or the listen host); a networked client
 * records none, as with projectile events. modeEventsReport logs the totals
 * and starts the next match's counts. Slice 2 gives a mode archive a program
 * whose events these are; slice 3 lets the program choose what the seams run.
 */

#define MODE_EVENT_CHOSEN           0  /* scenarioInit: the mode is chosen, or a match starts */
#define MODE_EVENT_STAGE_RESET      1  /* scenarioReset: a stage load clears the mode's state */
#define MODE_EVENT_INTRO_PAD        2  /* scenarioReset: a case or hill pad the stage intro lists */
#define MODE_EVENT_PROPS_INIT       3  /* scenarioInitProps: the mode makes its objects */
#define MODE_EVENT_TICK             4  /* scenarioTick, once a frame while a match runs */
#define MODE_EVENT_CHR_TICK         5  /* scenarioTickChr: a player or simulant */
#define MODE_EVENT_SCORE            6  /* scenarioCalculatePlayerScore */
#define MODE_EVENT_SPAWN            7  /* scenarioChooseSpawnLocation */
#define MODE_EVENT_TOKEN_PICKED_UP  8  /* scenarioPickUpBriefcase, scenarioPickUpUplink */
#define MODE_EVENT_TOKEN_DROPPED    9  /* scenarioHandleDroppedToken */
#define MODE_EVENT_PROP_ACTIVATED  10  /* scenarioHandleActivatedProp: the Hacker Central terminal */
#define MODE_EVENT_PLAYER_DIED     11  /* mpstatsRecordDeathByRuntimeIndex */
#define MODE_EVENT_BOT_COMMAND     12  /* scenarioBotChooseCommand (slice 7) */
#define MODE_EVENT_BOT_IDLE        13  /* scenarioBotIdleAction (slice 7) */
#define MODE_EVENT_COUNT           14

/* who: the seam's number for the event's player or simulant, or -1. */
void modeEventRecord(s32 kind, s32 who);

/* "player_died", or NULL for a kind out of range. */
const char *modeEventKindName(s32 kind);

/* The match's count of one kind so far (0 for a kind out of range). */
s32 modeEventTotal(s32 kind);

/* gamemode-graphs.md slice 3: one seam call compared with the original mode
 * table (g_MpScenarios[]); differ when the mode's plan ran it differently.
 * The report logs "MODE.PARITY: <why> mode=<catalog id> compared=N differ=M"
 * first, on every peer, and starts the counts again. */
void modeParityRecord(s32 differ);
/* The differences so far this match. */
s32 modeParityDiffer(void);

/* Logs "MODE.EVENTS: <why> mode=<catalog id> chosen=N stage_reset=N
 * intro_pad=N props_init=N tick=N chr_tick=N score=N spawn=N
 * token_picked_up=N token_dropped=N prop_activated=N player_died=N" and starts
 * the counts again. A match with no events (a mission, the CI stage) logs
 * nothing. */
void modeEventsReport(const char *why);

#ifdef __cplusplus
}
#endif

#endif /* _IN_MODE_EVENTS_H */
