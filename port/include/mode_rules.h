#ifndef _IN_MODE_RULES_H
#define _IN_MODE_RULES_H

#include <PR/ultratypes.h>
#include <stddef.h>

#include "mission_graph_runner.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The modules a Combat Simulator mode program can choose, the events that
 * choose them, and the original mode table that the base programs wire
 * (context/designs/modding/gamemode-graphs.md, slice 2). No game state here,
 * so the game, the extractor and pd-tests all read one table:
 *
 * - the extractor writes each base mode's mode.graph.json from it
 *   (modeRulesBaseProgramJson);
 * - registration compiles a mode archive's program with its modules
 *   (modeRulesModuleKnown) and refuses one that wires a module to an event
 *   that does not choose it, or a module of another mode
 *   (modeRulesCheckProgram);
 * - pd-tests hold the table to g_MpScenarios[] in scenarios.c.
 *
 * A module does not run code where the program fires it. It is a choice:
 * slice 3's dispatchers fire the event and run the original callback of each
 * module chosen, where that code has always run.
 */

/* What a mode is made of: one seam per callback of struct mpscenario
 * (scenarios.c), the room pair (isroomhighlightedfunc, highlightroomfunc) as
 * one seam and the setup file pair (readsavefunc, writesavefunc) as another.
 * The options dialog is the legacy menu's, not a rule. */
#define MODE_SEAM_INIT           0  /* initfunc: the mode sets up its options */
#define MODE_SEAM_NUM_PROPS      1  /* numpropsfunc: extra objects the stage load counts */
#define MODE_SEAM_INIT_PROPS     2  /* initpropsfunc: the mode makes its objects */
#define MODE_SEAM_TICK           3  /* tickfunc */
#define MODE_SEAM_TICK_CHR       4  /* tickchrfunc: a player or simulant */
#define MODE_SEAM_HUD            5  /* hudfunc */
#define MODE_SEAM_SCORE          6  /* calcscorefunc */
#define MODE_SEAM_RADAR_EXTRA    7  /* radarextrafunc */
#define MODE_SEAM_RADAR_CHR      8  /* radarchrfunc */
#define MODE_SEAM_HIGHLIGHT_PROP 9  /* highlightpropfunc */
#define MODE_SEAM_SPAWN          10 /* spawnfunc */
#define MODE_SEAM_MAX_TEAMS      11 /* maxteamsfunc */
#define MODE_SEAM_ROOM_LIGHTS    12 /* isroomhighlightedfunc and highlightroomfunc */
#define MODE_SEAM_SAVE           13 /* readsavefunc and writesavefunc */
/* Slice 4: the code that sat outside the table, now callbacks of its rows. */
#define MODE_SEAM_STAGE_RESET     14 /* resetfunc: a stage load clears the mode's state (scenarioReset) */
#define MODE_SEAM_INTRO_PAD       15 /* intropadfunc: a case or hill pad the stage intro lists */
#define MODE_SEAM_TOKEN_PICKED_UP 16 /* pickuptokenfunc: the mode's token is picked up */
#define MODE_SEAM_TOKEN_DROPPED   17 /* droppedtokenfunc: its carrier dropped it */
#define MODE_SEAM_PROP_ACTIVATED  18 /* activatedpropfunc: an object is used (the terminal) */
#define MODE_SEAM_PLAYER_DIED     19 /* playerdiedfunc: a player or simulant died */
#define MODE_SEAM_COUNT           20

/* The owner of a default module: the code a dispatcher runs for a mode
 * without the callback (kills minus suicides for the score, the general
 * spawn chooser for a spawn). */
#define MODE_RULES_DEFAULT (-1)

/* The base modes, MPSCENARIO_COMBAT .. MPSCENARIO_CAPTURETHECASE. */
#define MODE_RULES_MODE_COUNT 6

/* "htb" for MPSCENARIO_HOLDTHEBRIEFCASE, "default" for MODE_RULES_DEFAULT;
 * NULL for Combat (it has no modules of its own) and anything else. */
const char *modeRulesModeName(s32 mode);
/* "tick" for MODE_SEAM_TICK; NULL out of range. */
const char *modeRulesSeamName(s32 seam);

/* The module table: og.mode.<mode>.<seam> for each callback the original
 * table gives a mode, and og.mode.default.score and og.mode.default.spawn. */
s32 modeRulesModuleCount(void);
/* The index-th module's kind, mode (MODE_RULES_DEFAULT for a default) and
 * seam; 0 past the end. */
s32 modeRulesModuleAt(s32 index, const char **kind, s32 *mode, s32 *seam);
/* 1 when the kind is a mode module, else 0 (the runner's module_known for a
 * mode program). */
s32 modeRulesModuleKnown(const char *kind);
/* The kind's seam and owner; -1 and 0 when it is no mode module. */
s32 modeRulesModuleSeam(const char *kind);
s32 modeRulesModuleMode(const char *kind, s32 *mode);
/* The module of this mode and seam ("og.mode.ctc.spawn"), or NULL when the
 * original table gives the mode no such callback. mode may be
 * MODE_RULES_DEFAULT. */
const char *modeRulesModuleKind(s32 mode, s32 seam);

/* The event that chooses a seam's modules (MISSION_GRAPH_EVENT_MODE_CHOSEN
 * for init and what every peer needs to know about the mode: its extra
 * objects, its team count, its setup file data and its presenters). */
s32 modeRulesSeamEvent(s32 seam);

/* The mode events, in kind order: the index-th event's kind, or -1 past the
 * end; and "event.mode.tick" for a kind, NULL for no mode event. */
s32 modeRulesEventAt(s32 index);
const char *modeRulesEventName(s32 event_kind);

/* Whether the original table gives the mode the seam's callback. */
s32 modeRulesNativeHas(s32 mode, s32 seam);

/*
 * The modules a base program wires at the seam, in the order the original
 * dispatcher tries them: the mode's own callback, then the default where
 * the dispatcher falls back to it (a mode's spawn chooser may find no place,
 * and the general chooser then runs). out holds up to two kinds. Returns how
 * many (0 when the seam is empty for the mode).
 */
s32 modeRulesNativeModules(s32 mode, s32 seam, const char *out[2]);

/*
 * The base program of a mode (pd.gamemode_graph.v1): an event node for each
 * event its seams are chosen by, and a module node for each module
 * modeRulesNativeModules gives, joined by execution edges, in seam order.
 * Writes NUL-terminated JSON; returns its length, or -1 when the mode is out
 * of range or the buffer is too small.
 */
#define MODE_GRAPH_SCHEMA "pd.gamemode_graph.v1"
#define MODE_GRAPH_MEMBER "mode.graph.json"
s32 modeRulesBaseProgramJson(s32 mode, const char *catalog_id, char *out, size_t out_cap);

/*
 * Registration's check of a compiled mode program for an archive of this
 * mode (its rules' mode_key): every mode module is the archive's own mode's
 * or a default (until slice 5 gives each mode's state its own place, two
 * modes' modules would share one), every module an event reaches is one the
 * event chooses, and event.mode.spawn reaches a spawn module (a mode must
 * place its players). Returns 1, or 0 with the reason in err.
 */
s32 modeRulesCheckProgram(const mission_graph_program_t *program, s32 mode,
	char *err, size_t err_cap);

/*
 * gamemode-graphs.md slice 3: a mode's plan, what each seam runs, in the
 * order the seam runs it: the owner of each module chosen (a mode,
 * MPSCENARIO_*, or MODE_RULES_DEFAULT), MODE_PLAN_NONE past the last. The
 * dispatchers in scenarios.c run the callback of each owner's row in
 * g_MpScenarios[] (the default module: the dispatcher's own default code).
 */
#define MODE_PLAN_NONE (-2)
#define MODE_PLAN_MAX_PER_SEAM 2
typedef struct mode_rules_plan {
	s8 owner[MODE_SEAM_COUNT][MODE_PLAN_MAX_PER_SEAM];
	s32 modules; /* modules chosen in all */
} mode_rules_plan_t;

/* Empties the plan: no seam runs anything. */
void modeRulesPlanClear(mode_rules_plan_t *plan);
/*
 * The plan a program chooses: each choosing event (modeRulesSeamEvent) is
 * fired into the program once, and every mode module it reaches is added to
 * its seam in walk order (breadth first, authored order). A seam keeps at most
 * MODE_PLAN_MAX_PER_SEAM modules and a module once. A mode program's gates
 * read nothing of the game (none of the mission gates passes for a mode
 * event), so one walk per event is the program's whole answer. Returns 1, or
 * 0 with an empty plan when the program is NULL or a walk fails.
 */
s32 modeRulesPlanFromProgram(const mission_graph_program_t *program, mode_rules_plan_t *plan);
/* The plan of the original table for a mode (modeRulesNativeModules for
 * every seam); empty for a mode out of range. */
void modeRulesNativePlan(s32 mode, mode_rules_plan_t *plan);
/* Whether the plan runs a module of this owner at the seam. */
s32 modeRulesPlanRuns(const mode_rules_plan_t *plan, s32 seam, s32 owner);
/* Whether two plans run the same modules, in the same order, at the seam. */
s32 modeRulesPlanSeamEqual(const mode_rules_plan_t *a, const mode_rules_plan_t *b, s32 seam);
/* gamemode-graphs.md slice 5: the mode states the plan's modules use, one
 * bit per mode (1u << MPSCENARIO_*); the defaults use none. */
u32 modeRulesPlanStates(const mode_rules_plan_t *plan);

#ifdef __cplusplus
}
#endif

#endif /* _IN_MODE_RULES_H */
