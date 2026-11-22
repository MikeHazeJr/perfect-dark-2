/*
 * The Combat Simulator mode modules and the original mode table, for the mode
 * programs (context/designs/modding/gamemode-graphs.md, slice 2). See
 * port/include/mode_rules.h for the contract.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <PR/ultratypes.h>
#include "constants.h"
#include "mission_graph_runner.h"
#include "mode_rules.h"

static const char *const s_SeamNames[MODE_SEAM_COUNT] = {
	"init", "num_props", "init_props", "tick", "tick_chr", "hud", "score",
	"radar_extra", "radar_chr", "highlight_prop", "spawn", "max_teams",
	"room_lights", "save", "stage_reset", "intro_pad", "token_picked_up",
	"token_dropped", "prop_activated", "player_died",
};

/* The mode programs' events, in kind order (mission_graph_runner.h). */
static const struct {
	const char *name;
	s32 kind;
} s_Events[] = {
	{ "event.mode.chosen", MISSION_GRAPH_EVENT_MODE_CHOSEN },
	{ "event.mode.stage_reset", MISSION_GRAPH_EVENT_MODE_STAGE_RESET },
	{ "event.mode.intro_pad", MISSION_GRAPH_EVENT_MODE_INTRO_PAD },
	{ "event.mode.props_init", MISSION_GRAPH_EVENT_MODE_PROPS_INIT },
	{ "event.mode.tick", MISSION_GRAPH_EVENT_MODE_TICK },
	{ "event.mode.chr_tick", MISSION_GRAPH_EVENT_MODE_CHR_TICK },
	{ "event.mode.score", MISSION_GRAPH_EVENT_MODE_SCORE },
	{ "event.mode.spawn", MISSION_GRAPH_EVENT_MODE_SPAWN },
	{ "event.mode.token_picked_up", MISSION_GRAPH_EVENT_MODE_TOKEN_PICKED_UP },
	{ "event.mode.token_dropped", MISSION_GRAPH_EVENT_MODE_TOKEN_DROPPED },
	{ "event.mode.prop_activated", MISSION_GRAPH_EVENT_MODE_PROP_ACTIVATED },
	{ "event.mode.player_died", MISSION_GRAPH_EVENT_MODE_PLAYER_DIED },
};
#define MODE_RULES_EVENT_COUNT ((s32)(sizeof(s_Events) / sizeof(s_Events[0])))

/*
 * One module per callback that g_MpScenarios[] (src/game/mplayer/scenarios.c)
 * gives a mode, and the two defaults its dispatchers run without one.
 * Combat's row has the options dialog alone, so Combat has no modules of its
 * own. pd-tests read scenarios.c and hold these rows to the original table.
 */
static const struct {
	const char *kind;
	s32 mode;
	s32 seam;
} s_Modules[] = {
	{ "og.mode.default.score", MODE_RULES_DEFAULT, MODE_SEAM_SCORE },
	{ "og.mode.default.spawn", MODE_RULES_DEFAULT, MODE_SEAM_SPAWN },

	{ "og.mode.htb.init", MPSCENARIO_HOLDTHEBRIEFCASE, MODE_SEAM_INIT },
	{ "og.mode.htb.num_props", MPSCENARIO_HOLDTHEBRIEFCASE, MODE_SEAM_NUM_PROPS },
	{ "og.mode.htb.init_props", MPSCENARIO_HOLDTHEBRIEFCASE, MODE_SEAM_INIT_PROPS },
	{ "og.mode.htb.tick", MPSCENARIO_HOLDTHEBRIEFCASE, MODE_SEAM_TICK },
	{ "og.mode.htb.tick_chr", MPSCENARIO_HOLDTHEBRIEFCASE, MODE_SEAM_TICK_CHR },
	{ "og.mode.htb.hud", MPSCENARIO_HOLDTHEBRIEFCASE, MODE_SEAM_HUD },
	{ "og.mode.htb.score", MPSCENARIO_HOLDTHEBRIEFCASE, MODE_SEAM_SCORE },
	{ "og.mode.htb.radar_extra", MPSCENARIO_HOLDTHEBRIEFCASE, MODE_SEAM_RADAR_EXTRA },
	{ "og.mode.htb.radar_chr", MPSCENARIO_HOLDTHEBRIEFCASE, MODE_SEAM_RADAR_CHR },
	{ "og.mode.htb.highlight_prop", MPSCENARIO_HOLDTHEBRIEFCASE, MODE_SEAM_HIGHLIGHT_PROP },
	{ "og.mode.htb.stage_reset", MPSCENARIO_HOLDTHEBRIEFCASE, MODE_SEAM_STAGE_RESET },
	{ "og.mode.htb.intro_pad", MPSCENARIO_HOLDTHEBRIEFCASE, MODE_SEAM_INTRO_PAD },
	{ "og.mode.htb.token_picked_up", MPSCENARIO_HOLDTHEBRIEFCASE, MODE_SEAM_TOKEN_PICKED_UP },

	{ "og.mode.htm.init", MPSCENARIO_HACKERCENTRAL, MODE_SEAM_INIT },
	{ "og.mode.htm.num_props", MPSCENARIO_HACKERCENTRAL, MODE_SEAM_NUM_PROPS },
	{ "og.mode.htm.init_props", MPSCENARIO_HACKERCENTRAL, MODE_SEAM_INIT_PROPS },
	{ "og.mode.htm.tick", MPSCENARIO_HACKERCENTRAL, MODE_SEAM_TICK },
	{ "og.mode.htm.tick_chr", MPSCENARIO_HACKERCENTRAL, MODE_SEAM_TICK_CHR },
	{ "og.mode.htm.hud", MPSCENARIO_HACKERCENTRAL, MODE_SEAM_HUD },
	{ "og.mode.htm.score", MPSCENARIO_HACKERCENTRAL, MODE_SEAM_SCORE },
	{ "og.mode.htm.radar_extra", MPSCENARIO_HACKERCENTRAL, MODE_SEAM_RADAR_EXTRA },
	{ "og.mode.htm.radar_chr", MPSCENARIO_HACKERCENTRAL, MODE_SEAM_RADAR_CHR },
	{ "og.mode.htm.highlight_prop", MPSCENARIO_HACKERCENTRAL, MODE_SEAM_HIGHLIGHT_PROP },
	{ "og.mode.htm.stage_reset", MPSCENARIO_HACKERCENTRAL, MODE_SEAM_STAGE_RESET },
	{ "og.mode.htm.intro_pad", MPSCENARIO_HACKERCENTRAL, MODE_SEAM_INTRO_PAD },
	{ "og.mode.htm.token_picked_up", MPSCENARIO_HACKERCENTRAL, MODE_SEAM_TOKEN_PICKED_UP },
	{ "og.mode.htm.prop_activated", MPSCENARIO_HACKERCENTRAL, MODE_SEAM_PROP_ACTIVATED },

	{ "og.mode.pac.init", MPSCENARIO_POPACAP, MODE_SEAM_INIT },
	{ "og.mode.pac.init_props", MPSCENARIO_POPACAP, MODE_SEAM_INIT_PROPS },
	{ "og.mode.pac.tick", MPSCENARIO_POPACAP, MODE_SEAM_TICK },
	{ "og.mode.pac.hud", MPSCENARIO_POPACAP, MODE_SEAM_HUD },
	{ "og.mode.pac.score", MPSCENARIO_POPACAP, MODE_SEAM_SCORE },
	{ "og.mode.pac.radar_extra", MPSCENARIO_POPACAP, MODE_SEAM_RADAR_EXTRA },
	{ "og.mode.pac.radar_chr", MPSCENARIO_POPACAP, MODE_SEAM_RADAR_CHR },
	{ "og.mode.pac.highlight_prop", MPSCENARIO_POPACAP, MODE_SEAM_HIGHLIGHT_PROP },
	{ "og.mode.pac.player_died", MPSCENARIO_POPACAP, MODE_SEAM_PLAYER_DIED },

	{ "og.mode.koh.init", MPSCENARIO_KINGOFTHEHILL, MODE_SEAM_INIT },
	{ "og.mode.koh.init_props", MPSCENARIO_KINGOFTHEHILL, MODE_SEAM_INIT_PROPS },
	{ "og.mode.koh.tick", MPSCENARIO_KINGOFTHEHILL, MODE_SEAM_TICK },
	{ "og.mode.koh.hud", MPSCENARIO_KINGOFTHEHILL, MODE_SEAM_HUD },
	{ "og.mode.koh.score", MPSCENARIO_KINGOFTHEHILL, MODE_SEAM_SCORE },
	{ "og.mode.koh.radar_extra", MPSCENARIO_KINGOFTHEHILL, MODE_SEAM_RADAR_EXTRA },
	{ "og.mode.koh.room_lights", MPSCENARIO_KINGOFTHEHILL, MODE_SEAM_ROOM_LIGHTS },
	{ "og.mode.koh.save", MPSCENARIO_KINGOFTHEHILL, MODE_SEAM_SAVE },
	{ "og.mode.koh.stage_reset", MPSCENARIO_KINGOFTHEHILL, MODE_SEAM_STAGE_RESET },
	{ "og.mode.koh.intro_pad", MPSCENARIO_KINGOFTHEHILL, MODE_SEAM_INTRO_PAD },

	{ "og.mode.ctc.init", MPSCENARIO_CAPTURETHECASE, MODE_SEAM_INIT },
	{ "og.mode.ctc.num_props", MPSCENARIO_CAPTURETHECASE, MODE_SEAM_NUM_PROPS },
	{ "og.mode.ctc.init_props", MPSCENARIO_CAPTURETHECASE, MODE_SEAM_INIT_PROPS },
	{ "og.mode.ctc.tick", MPSCENARIO_CAPTURETHECASE, MODE_SEAM_TICK },
	{ "og.mode.ctc.tick_chr", MPSCENARIO_CAPTURETHECASE, MODE_SEAM_TICK_CHR },
	{ "og.mode.ctc.score", MPSCENARIO_CAPTURETHECASE, MODE_SEAM_SCORE },
	{ "og.mode.ctc.radar_extra", MPSCENARIO_CAPTURETHECASE, MODE_SEAM_RADAR_EXTRA },
	{ "og.mode.ctc.radar_chr", MPSCENARIO_CAPTURETHECASE, MODE_SEAM_RADAR_CHR },
	{ "og.mode.ctc.highlight_prop", MPSCENARIO_CAPTURETHECASE, MODE_SEAM_HIGHLIGHT_PROP },
	{ "og.mode.ctc.spawn", MPSCENARIO_CAPTURETHECASE, MODE_SEAM_SPAWN },
	{ "og.mode.ctc.max_teams", MPSCENARIO_CAPTURETHECASE, MODE_SEAM_MAX_TEAMS },
	{ "og.mode.ctc.room_lights", MPSCENARIO_CAPTURETHECASE, MODE_SEAM_ROOM_LIGHTS },
	{ "og.mode.ctc.stage_reset", MPSCENARIO_CAPTURETHECASE, MODE_SEAM_STAGE_RESET },
	{ "og.mode.ctc.intro_pad", MPSCENARIO_CAPTURETHECASE, MODE_SEAM_INTRO_PAD },
	{ "og.mode.ctc.token_picked_up", MPSCENARIO_CAPTURETHECASE, MODE_SEAM_TOKEN_PICKED_UP },
	{ "og.mode.ctc.token_dropped", MPSCENARIO_CAPTURETHECASE, MODE_SEAM_TOKEN_DROPPED },
};
#define MODE_RULES_MODULE_COUNT ((s32)(sizeof(s_Modules) / sizeof(s_Modules[0])))

static const char *const s_ModeNames[MODE_RULES_MODE_COUNT] = {
	NULL, "htb", "htm", "pac", "koh", "ctc",
};

const char *modeRulesModeName(s32 mode)
{
	if (mode == MODE_RULES_DEFAULT) return "default";
	if (mode < 0 || mode >= MODE_RULES_MODE_COUNT) return NULL;
	return s_ModeNames[mode];
}

const char *modeRulesSeamName(s32 seam)
{
	return seam >= 0 && seam < MODE_SEAM_COUNT ? s_SeamNames[seam] : NULL;
}

s32 modeRulesModuleCount(void)
{
	return MODE_RULES_MODULE_COUNT;
}

s32 modeRulesModuleAt(s32 index, const char **kind, s32 *mode, s32 *seam)
{
	if (index < 0 || index >= MODE_RULES_MODULE_COUNT) return 0;
	if (kind) *kind = s_Modules[index].kind;
	if (mode) *mode = s_Modules[index].mode;
	if (seam) *seam = s_Modules[index].seam;
	return 1;
}

static s32 s_moduleIndex(const char *kind)
{
	if (!kind) return -1;
	for (s32 i = 0; i < MODE_RULES_MODULE_COUNT; i++) {
		if (strcmp(s_Modules[i].kind, kind) == 0) return i;
	}
	return -1;
}

s32 modeRulesModuleKnown(const char *kind)
{
	return s_moduleIndex(kind) >= 0 ? 1 : 0;
}

s32 modeRulesModuleSeam(const char *kind)
{
	const s32 index = s_moduleIndex(kind);
	return index >= 0 ? s_Modules[index].seam : -1;
}

s32 modeRulesModuleMode(const char *kind, s32 *mode)
{
	const s32 index = s_moduleIndex(kind);
	if (index < 0) return 0;
	if (mode) *mode = s_Modules[index].mode;
	return 1;
}

const char *modeRulesModuleKind(s32 mode, s32 seam)
{
	for (s32 i = 0; i < MODE_RULES_MODULE_COUNT; i++) {
		if (s_Modules[i].mode == mode && s_Modules[i].seam == seam) return s_Modules[i].kind;
	}
	return NULL;
}

s32 modeRulesSeamEvent(s32 seam)
{
	switch (seam) {
	case MODE_SEAM_INIT:
	case MODE_SEAM_NUM_PROPS:
	case MODE_SEAM_HUD:
	case MODE_SEAM_RADAR_EXTRA:
	case MODE_SEAM_RADAR_CHR:
	case MODE_SEAM_HIGHLIGHT_PROP:
	case MODE_SEAM_MAX_TEAMS:
	case MODE_SEAM_ROOM_LIGHTS:
	case MODE_SEAM_SAVE:
		return MISSION_GRAPH_EVENT_MODE_CHOSEN;
	case MODE_SEAM_INIT_PROPS: return MISSION_GRAPH_EVENT_MODE_PROPS_INIT;
	case MODE_SEAM_TICK: return MISSION_GRAPH_EVENT_MODE_TICK;
	case MODE_SEAM_TICK_CHR: return MISSION_GRAPH_EVENT_MODE_CHR_TICK;
	case MODE_SEAM_SCORE: return MISSION_GRAPH_EVENT_MODE_SCORE;
	case MODE_SEAM_SPAWN: return MISSION_GRAPH_EVENT_MODE_SPAWN;
	case MODE_SEAM_STAGE_RESET: return MISSION_GRAPH_EVENT_MODE_STAGE_RESET;
	case MODE_SEAM_INTRO_PAD: return MISSION_GRAPH_EVENT_MODE_INTRO_PAD;
	case MODE_SEAM_TOKEN_PICKED_UP: return MISSION_GRAPH_EVENT_MODE_TOKEN_PICKED_UP;
	case MODE_SEAM_TOKEN_DROPPED: return MISSION_GRAPH_EVENT_MODE_TOKEN_DROPPED;
	case MODE_SEAM_PROP_ACTIVATED: return MISSION_GRAPH_EVENT_MODE_PROP_ACTIVATED;
	case MODE_SEAM_PLAYER_DIED: return MISSION_GRAPH_EVENT_MODE_PLAYER_DIED;
	default: return -1;
	}
}

s32 modeRulesEventAt(s32 index)
{
	return index >= 0 && index < MODE_RULES_EVENT_COUNT ? s_Events[index].kind : -1;
}

const char *modeRulesEventName(s32 event_kind)
{
	for (s32 i = 0; i < MODE_RULES_EVENT_COUNT; i++) {
		if (s_Events[i].kind == event_kind) return s_Events[i].name;
	}
	return NULL;
}

s32 modeRulesNativeHas(s32 mode, s32 seam)
{
	return mode >= 0 && mode < MODE_RULES_MODE_COUNT && modeRulesModuleKind(mode, seam) != NULL;
}

s32 modeRulesNativeModules(s32 mode, s32 seam, const char *out[2])
{
	const char *own;
	s32 n = 0;

	if (!out || mode < 0 || mode >= MODE_RULES_MODE_COUNT || seam < 0 || seam >= MODE_SEAM_COUNT) {
		return 0;
	}
	own = modeRulesModuleKind(mode, seam);
	if (own) out[n++] = own;
	/* scenarioCalculatePlayerScore: the callback, else kills minus suicides. */
	if (seam == MODE_SEAM_SCORE && !own) {
		out[n++] = modeRulesModuleKind(MODE_RULES_DEFAULT, MODE_SEAM_SCORE);
	}
	/* scenarioChooseSpawnLocation: the callback when it finds a place, then
	 * the general chooser. */
	if (seam == MODE_SEAM_SPAWN) {
		out[n++] = modeRulesModuleKind(MODE_RULES_DEFAULT, MODE_SEAM_SPAWN);
	}
	return n;
}

/* Appends to out at *len, NUL-terminated; 0 when it does not fit. */
static s32 s_append(char *out, size_t cap, size_t *len, const char *fmt, ...)
{
	va_list ap;
	int written;

	if (*len >= cap) return 0;
	va_start(ap, fmt);
	written = vsnprintf(out + *len, cap - *len, fmt, ap);
	va_end(ap);
	if (written < 0 || (size_t)written >= cap - *len) return 0;
	*len += (size_t)written;
	return 1;
}

/* A catalog id the JSON can carry as it is: printable, no quote or
 * backslash. */
static s32 s_plainId(const char *id)
{
	if (!id || !id[0]) return 0;
	for (const char *c = id; *c; c++) {
		if ((unsigned char)*c < 0x20 || (unsigned char)*c >= 0x7f || *c == '"' || *c == '\\') return 0;
	}
	return 1;
}

s32 modeRulesBaseProgramJson(s32 mode, const char *catalog_id, char *out, size_t out_cap)
{
	size_t len = 0;
	s32 first_node = 1;
	s32 first_edge = 1;
	s32 e;
	s32 seam;
	s32 i;

	if (!out || !out_cap) return -1;
	out[0] = '\0';
	if (mode < 0 || mode >= MODE_RULES_MODE_COUNT || !s_plainId(catalog_id)) return -1;

	if (!s_append(out, out_cap, &len,
			"{\n  \"schema\": \"%s\",\n  \"catalog_id\": \"%s\",\n  \"nodes\": [",
			MODE_GRAPH_SCHEMA, catalog_id)) {
		return -1;
	}
	for (e = 0; e < MODE_RULES_EVENT_COUNT; e++) {
		const s32 kind = s_Events[e].kind;
		const char *event_short = s_Events[e].name + strlen("event.mode.");
		s32 wired = 0;
		for (seam = 0; seam < MODE_SEAM_COUNT; seam++) {
			const char *modules[2];
			if (modeRulesSeamEvent(seam) != kind) continue;
			const s32 n = modeRulesNativeModules(mode, seam, modules);
			for (i = 0; i < n; i++) {
				if (!wired) {
					if (!s_append(out, out_cap, &len,
							"%s\n    { \"id\": \"mode.event.%s\", \"kind\": \"%s\" }",
							first_node ? "" : ",", event_short, s_Events[e].name)) {
						return -1;
					}
					first_node = 0;
					wired = 1;
				}
				if (!s_append(out, out_cap, &len,
						",\n    { \"id\": \"mode.%s%s\", \"kind\": \"%s\" }",
						s_SeamNames[seam], i ? ".default" : "", modules[i])) {
					return -1;
				}
			}
		}
	}
	if (!s_append(out, out_cap, &len, "\n  ],\n  \"edges\": [")) return -1;
	for (e = 0; e < MODE_RULES_EVENT_COUNT; e++) {
		const s32 kind = s_Events[e].kind;
		const char *event_short = s_Events[e].name + strlen("event.mode.");
		for (seam = 0; seam < MODE_SEAM_COUNT; seam++) {
			const char *modules[2];
			if (modeRulesSeamEvent(seam) != kind) continue;
			const s32 n = modeRulesNativeModules(mode, seam, modules);
			for (i = 0; i < n; i++) {
				if (!s_append(out, out_cap, &len,
						"%s\n    { \"from\": \"mode.event.%s\", \"output\": \"out\", "
						"\"to\": \"mode.%s%s\", \"input\": \"exec\" }",
						first_edge ? "" : ",", event_short, s_SeamNames[seam],
						i ? ".default" : "")) {
					return -1;
				}
				first_edge = 0;
			}
		}
	}
	if (!s_append(out, out_cap, &len, "\n  ]\n}\n")) return -1;
	return (s32)len;
}

static s32 s_fail(char *err, size_t err_cap, const char *fmt, ...)
{
	va_list ap;
	if (err && err_cap) {
		va_start(ap, fmt);
		vsnprintf(err, err_cap, fmt, ap);
		va_end(ap);
	}
	return 0;
}

struct mode_rules_owner_check {
	s32 mode;
	const char *foreign; /* the first module of another mode, or NULL */
};

static s32 s_checkModuleOwner(const char *kind, const mission_graph_params_t *params, void *userdata)
{
	struct mode_rules_owner_check *check = (struct mode_rules_owner_check *)userdata;
	s32 owner;

	(void)params;
	if (kind && modeRulesModuleMode(kind, &owner)
			&& owner != MODE_RULES_DEFAULT && owner != check->mode) {
		check->foreign = kind;
		return 0;
	}
	return 1;
}

s32 modeRulesCheckProgram(const mission_graph_program_t *program, s32 mode,
	char *err, size_t err_cap)
{
	struct mode_rules_owner_check owner_check;
	s32 spawn_reached = 0;

	if (err && err_cap) err[0] = '\0';
	if (!program) return s_fail(err, err_cap, "no program");
	if (mode < 0 || mode >= MODE_RULES_MODE_COUNT) {
		return s_fail(err, err_cap, "mode %d is not one of the six base modes", (int)mode);
	}

	/* Until slice 5 gives each mode's state its own place, the modes share one
	 * union (g_ScenarioData), so a program runs its own mode's callbacks and
	 * the defaults alone. */
	owner_check.mode = mode;
	owner_check.foreign = NULL;
	if (!missionGraphProgramVisitModuleParams(program, s_checkModuleOwner, &owner_check)
			&& owner_check.foreign) {
		return s_fail(err, err_cap,
			"module '%s' is another mode's; a mode program runs its own mode's modules "
			"and og.mode.default.*", owner_check.foreign);
	}

	/* A module is a choice the seam of its event acts on: wired to any other
	 * event, nothing would run it. */
	for (s32 e = 0; e < MODE_RULES_EVENT_COUNT; e++) {
		const s32 event_kind = s_Events[e].kind;
		for (s32 m = 0; m < MODE_RULES_MODULE_COUNT; m++) {
			if (!missionGraphProgramEventReachesModule(program, event_kind, s_Modules[m].kind)) {
				continue;
			}
			if (modeRulesSeamEvent(s_Modules[m].seam) != event_kind) {
				return s_fail(err, err_cap, "%s has no effect on %s: its seam (%s) is chosen by %s",
					s_Modules[m].kind, s_Events[e].name, s_SeamNames[s_Modules[m].seam],
					modeRulesEventName(modeRulesSeamEvent(s_Modules[m].seam)));
			}
			if (s_Modules[m].seam == MODE_SEAM_SPAWN) spawn_reached = 1;
		}
	}
	if (!spawn_reached) {
		return s_fail(err, err_cap,
			"event.mode.spawn reaches no spawn module: a mode must place its players "
			"(og.mode.default.spawn is the general chooser)");
	}
	return 1;
}

void modeRulesPlanClear(mode_rules_plan_t *plan)
{
	if (!plan) return;
	for (s32 seam = 0; seam < MODE_SEAM_COUNT; seam++) {
		for (s32 k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
			plan->owner[seam][k] = MODE_PLAN_NONE;
		}
	}
	plan->modules = 0;
}

static void s_planAdd(mode_rules_plan_t *plan, s32 seam, s32 owner)
{
	if (seam < 0 || seam >= MODE_SEAM_COUNT) return;
	for (s32 k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
		if (plan->owner[seam][k] == owner) return;
		if (plan->owner[seam][k] == MODE_PLAN_NONE) {
			plan->owner[seam][k] = (s8)owner;
			plan->modules++;
			return;
		}
	}
}

struct mode_rules_plan_walk {
	mode_rules_plan_t *plan;
	s32 event_kind;
};

/* The walk's module table: a mode module joins the plan when its seam is the
 * one the event chooses (registration refused any other wiring). */
static s32 s_planRun(const char *kind, const char *node_id, const mission_graph_params_t *params,
	const mission_graph_event_t *event, void *user)
{
	struct mode_rules_plan_walk *walk = (struct mode_rules_plan_walk *)user;
	const s32 seam = modeRulesModuleSeam(kind);
	s32 owner;

	(void)node_id;
	(void)params;
	(void)event;
	if (seam < 0 || !modeRulesModuleMode(kind, &owner)) return 0;
	if (modeRulesSeamEvent(seam) == walk->event_kind) {
		s_planAdd(walk->plan, seam, owner);
	}
	return 1;
}

s32 modeRulesPlanFromProgram(const mission_graph_program_t *program, mode_rules_plan_t *plan)
{
	if (!plan) return 0;
	modeRulesPlanClear(plan);
	if (!program) return 0;
	for (s32 e = 0; e < MODE_RULES_EVENT_COUNT; e++) {
		mission_graph_event_t event;
		struct mode_rules_plan_walk walk;
		s32 chooses = 0;

		for (s32 seam = 0; seam < MODE_SEAM_COUNT; seam++) {
			if (modeRulesSeamEvent(seam) == s_Events[e].kind) chooses = 1;
		}
		if (!chooses) continue;
		memset(&event, 0, sizeof(event));
		event.kind = s_Events[e].kind;
		event.seam = MISSION_GRAPH_SEAM_NONE;
		event.player = -1;
		walk.plan = plan;
		walk.event_kind = event.kind;
		if (missionGraphProgramFire(program, &event, s_planRun, &walk) < 0) {
			modeRulesPlanClear(plan);
			return 0;
		}
	}
	return 1;
}

void modeRulesNativePlan(s32 mode, mode_rules_plan_t *plan)
{
	if (!plan) return;
	modeRulesPlanClear(plan);
	if (mode < 0 || mode >= MODE_RULES_MODE_COUNT) return;
	for (s32 seam = 0; seam < MODE_SEAM_COUNT; seam++) {
		const char *modules[2];
		const s32 n = modeRulesNativeModules(mode, seam, modules);
		for (s32 i = 0; i < n; i++) {
			s32 owner;
			if (modeRulesModuleMode(modules[i], &owner)) s_planAdd(plan, seam, owner);
		}
	}
}

s32 modeRulesPlanRuns(const mode_rules_plan_t *plan, s32 seam, s32 owner)
{
	if (!plan || seam < 0 || seam >= MODE_SEAM_COUNT) return 0;
	for (s32 k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
		if (plan->owner[seam][k] == owner) return 1;
	}
	return 0;
}

s32 modeRulesPlanSeamEqual(const mode_rules_plan_t *a, const mode_rules_plan_t *b, s32 seam)
{
	if (!a || !b || seam < 0 || seam >= MODE_SEAM_COUNT) return 0;
	for (s32 k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
		if (a->owner[seam][k] != b->owner[seam][k]) return 0;
	}
	return 1;
}

u32 modeRulesPlanStates(const mode_rules_plan_t *plan)
{
	u32 states = 0;

	if (!plan) return 0;
	for (s32 seam = 0; seam < MODE_SEAM_COUNT; seam++) {
		for (s32 k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
			const s32 owner = plan->owner[seam][k];
			if (owner >= 0 && owner < MODE_RULES_MODE_COUNT) states |= 1u << owner;
		}
	}
	return states;
}
