/*
 * testscenarios.c -- Test Scenarios runtime (S483).
 *
 * Owns the small g_TestScenario state machine that the Settings > Debug
 * dropdown writes into and the swarm_test runtime / setup-time hooks
 * read from.
 *
 * Design: context/designs/gpu-swarm-and-test-scenarios-2026-04-27.md
 * sections A.3, F.4, G.1 (decided 2026-04-27), plus the 2026-04-28
 * implementation correction for MP arena launch.
 *
 * Scope: this module owns the SCENARIO STATE and the LAUNCH dispatch.
 * The swarm runtime (player setup, bot allocation, cycler, HUD,
 * benchmark logging) lives in swarm_test.c (commit 4) and the GPU
 * compute path lives in port/fast3d/swarm_gpu.cpp (commit 5).
 *
 * Net mode: test scenarios are local-only. Launch is rejected when the
 * net layer is in any non-NONE mode. The dropdown surfaces the reason
 * via testScenarioWhyDisabled().
 */

#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>

#include "constants.h"
#include "system.h"
#include "assetcatalog.h"
#include "net/matchsetup.h"
#include "testscenarios.h"

/* Externs we need to drive a session. */
extern s32  pdguiForgeStartSessionOn(s32 stagenum);

/* Net-mode probe -- must be NETMODE_NONE (0) for local-only test mode. */
extern s32 g_NetMode;
#ifndef NETMODE_NONE
#define NETMODE_NONE 0
#endif

/* ------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------ */

typedef struct {
	test_scenario_t scen;
	swarm_method_t  method;
	char            map_id[64];     /* resolved at launch time */
	s32             stagenum;       /* resolved at launch time */
	s32             current_count;  /* current swarm size (cycler position) */
} test_scenario_state_t;

static test_scenario_state_t s_State = {
	TESTSCEN_NONE, SWARM_METHOD_CPU, {0}, -1, 0
};

/* ------------------------------------------------------------------
 * Launch gating
 * ------------------------------------------------------------------ */

s32 testScenarioCanLaunch(void)
{
	/* Local-only: refuse if any net mode is active. The launch transitions
	 * the local stage; doing that mid-session in netplay would desync the
	 * server's view of where the client is. */
	if (g_NetMode != NETMODE_NONE) {
		return 0;
	}
	return 1;
}

const char *testScenarioWhyDisabled(void)
{
	if (g_NetMode != NETMODE_NONE) {
		return "Local sessions only. Disconnect from the server first.";
	}
	return NULL;
}

/* ------------------------------------------------------------------
 * Resolve a catalog map ID to a stagenum.
 *
 * Used for the Swarm scenarios. Empty Map uses STAGE_CITRAINING
 * directly per F.4.A.
 *
 * Returns the stagenum on success, -1 on failure.
 * ------------------------------------------------------------------ */

static s32 resolve_map_stagenum(const char *map_id)
{
	if (!map_id || !map_id[0]) {
		return -1;
	}
	catalog_stage_result_t res;
	if (!catalogResolveStage(map_id, &res)) {
		sysLogPrintf(LOG_WARNING,
			"TESTSCEN: failed to resolve map_id='%s' via catalog", map_id);
		return -1;
	}
	return res.stagenum;
}

/* ------------------------------------------------------------------
 * Launch dispatch
 * ------------------------------------------------------------------ */

/* Default map for the Swarm scenarios. Felicity is the open beach
 * arena -- significantly more open than Skedar Ruins, which Mike
 * playtested as too cramped for a 256-bot swarm. The Debug Menu now
 * exposes an explicit arena selector (S593d, 2026-05-01) so this
 * default applies only when the caller passes NULL/empty map_id. */
#define TESTSCEN_DEFAULT_SWARM_MAP "base:mp_felicity"

s32 testScenarioLaunch(test_scenario_t scen, const char *map_id)
{
	if (!testScenarioCanLaunch()) {
		const char *why = testScenarioWhyDisabled();
		sysLogPrintf(LOG_WARNING,
			"TESTSCEN: launch rejected -- %s", why ? why : "(unknown)");
		return 0;
	}

	if (s_State.scen != TESTSCEN_NONE) {
		sysLogPrintf(LOG_WARNING,
			"TESTSCEN: launch rejected -- another scenario already armed (%d)",
			(int)s_State.scen);
		return 0;
	}

	switch (scen) {
	case TESTSCEN_EMPTY_MAP: {
		/* F.4.A: reuse STAGE_CITRAINING. No map_id needed; the directive
		 * says "test level if exists, or create one with just a
		 * procedural ground plane" -- we ship the existing stage now
		 * and revisit a procedural variant later if Mike's first run
		 * shows the CI-Training prop load muddies the empty-map number. */
		s_State.scen          = TESTSCEN_EMPTY_MAP;
		s_State.method        = SWARM_METHOD_CPU; /* not used */
		s_State.stagenum      = STAGE_CITRAINING;
		s_State.current_count = 0;
		s_State.map_id[0]     = '\0';

		matchConfigInit();
		strncpy(g_MatchConfig.scenario_id, "base:combat",
			sizeof(g_MatchConfig.scenario_id) - 1);
		g_MatchConfig.scenario_id[sizeof(g_MatchConfig.scenario_id) - 1] = '\0';
		g_MatchConfig.timelimit  = 0;
		g_MatchConfig.scorelimit = 0;

		sysLogPrintf(LOG_NOTE,
			"TESTSCEN.LAUNCH: Empty Map -- stagenum=0x%02x (CI Training)",
			(u32)STAGE_CITRAINING);

		if (!pdguiForgeStartSessionOn(STAGE_CITRAINING)) {
			sysLogPrintf(LOG_WARNING,
				"TESTSCEN: Empty Map handoff failed; resetting state");
			testScenarioReset();
			return 0;
		}
		return 1;
	}

	case TESTSCEN_SWARM_CPU:
	case TESTSCEN_SWARM_GPU: {
		const char *resolved_id = (map_id && map_id[0]) ? map_id
			: TESTSCEN_DEFAULT_SWARM_MAP;

		s32 stagenum = resolve_map_stagenum(resolved_id);
		if (stagenum < 0) {
			/* Fall back to Skedar; if that also fails, give up. */
			if (strcmp(resolved_id, TESTSCEN_DEFAULT_SWARM_MAP) != 0) {
				sysLogPrintf(LOG_WARNING,
					"TESTSCEN: '%s' unresolved; falling back to '%s'",
					resolved_id, TESTSCEN_DEFAULT_SWARM_MAP);
				resolved_id = TESTSCEN_DEFAULT_SWARM_MAP;
				stagenum = resolve_map_stagenum(resolved_id);
			}
			if (stagenum < 0) {
				sysLogPrintf(LOG_ERROR,
					"TESTSCEN: launch failed -- no resolvable map");
				return 0;
			}
		}

		/* S594h-Unit-C (2026-05-01): refuse to launch swarm on canvas-
		 * loadmode arenas (solo mission stages used as Grid build
		 * canvases). Mike's playtest crashed when launching swarm on
		 * Chicago (stage 0x1d, idx 13-26 = canvas-flagged). The MP
		 * matchStart path conflicts with the solo mission setup
		 * (mission scripts, NPCs, intros) baked into those stages,
		 * leading to crashes downstream. Until canvas-mode-aware
		 * swarm setup lands, reject at launch with a clear log. */
		s32 is_canvas = 0;
		const asset_entry_t *via_arena = assetCatalogResolve(resolved_id);
		if (via_arena && via_arena->type == ASSET_ARENA
				&& via_arena->ext.arena.load_mode == ARENA_LOADMODE_CANVAS) {
			is_canvas = 1;
		} else {
			/* Resolve via stagenum scan -- the dropdown supplies stage
			 * ids ("base:mp_*"), not arena ids ("base:arena_*"), so the
			 * direct resolve typically returns an ASSET_STAGE entry
			 * (no load_mode). The arena entry holds load_mode and
			 * carries stagenum, so we walk arenas to find the match. */
			s32 pool_size = assetCatalogGetPoolSize();
			for (s32 ai = 0; ai < pool_size; ai++) {
				const asset_entry_t *ae = assetCatalogGetByIndex(ai);
				if (ae && ae->type == ASSET_ARENA
						&& (s32)ae->ext.arena.stagenum == stagenum
						&& ae->ext.arena.load_mode == ARENA_LOADMODE_CANVAS) {
					is_canvas = 1;
					break;
				}
			}
		}
		if (is_canvas) {
			sysLogPrintf(LOG_WARNING,
				"TESTSCEN: launch refused -- arena '%s' (stagenum=0x%02x) is canvas-loadmode "
				"(solo mission stage). Pick an MP arena instead.",
				resolved_id, (u32)stagenum);
			return 0;
		}

		s_State.scen          = scen;
		s_State.method        = (scen == TESTSCEN_SWARM_GPU)
			? SWARM_METHOD_GPU : SWARM_METHOD_CPU;
		s_State.stagenum      = stagenum;
		s_State.current_count = TESTSCEN_SWARM_INITIAL_COUNT;
		strncpy(s_State.map_id, resolved_id, sizeof(s_State.map_id) - 1);
		s_State.map_id[sizeof(s_State.map_id) - 1] = '\0';

		matchConfigInit();
		strncpy(g_MatchConfig.stage_id, resolved_id,
			sizeof(g_MatchConfig.stage_id) - 1);
		g_MatchConfig.stage_id[sizeof(g_MatchConfig.stage_id) - 1] = '\0';
		g_MatchConfig.stagenum = (u8)stagenum;
		strncpy(g_MatchConfig.scenario_id, "base:combat",
			sizeof(g_MatchConfig.scenario_id) - 1);
		g_MatchConfig.scenario_id[sizeof(g_MatchConfig.scenario_id) - 1] = '\0';
		g_MatchConfig.timelimit      = 60;  /* no limit */
		g_MatchConfig.scorelimit     = 100; /* no limit */
		g_MatchConfig.teamscorelimit = 400; /* no limit */

		sysLogPrintf(LOG_NOTE,
			"TESTSCEN.LAUNCH: Swarm-%s map='%s' stagenum=0x%02x initial=%d via matchStart",
			(scen == TESTSCEN_SWARM_GPU) ? "GPU" : "CPU",
			resolved_id, (u32)stagenum, (s32)TESTSCEN_SWARM_INITIAL_COUNT);

		/* Swarm scenarios target MP arenas. The Grid/Forge handoff loads
		 * a stage directly, which leaves normmplayerisrunning false and
		 * setup.c selects the SP setup/manifest for arenas like
		 * base:mp_skedar. Use the normal match path so the MP
		 * setup/manifest path owns the load. */
		if (matchStart() != 0) {
			sysLogPrintf(LOG_WARNING,
				"TESTSCEN: Swarm matchStart failed; resetting state");
			testScenarioReset();
			return 0;
		}
		return 1;
	}

	default:
		sysLogPrintf(LOG_WARNING,
			"TESTSCEN: launch rejected -- unknown scenario %d", (int)scen);
		return 0;
	}
}

/* ------------------------------------------------------------------
 * State accessors
 * ------------------------------------------------------------------ */

test_scenario_t testScenarioActive(void)
{
	return s_State.scen;
}

swarm_method_t testScenarioActiveMethod(void)
{
	return s_State.method;
}

s32 testScenarioIsSwarmActive(void)
{
	return (s_State.scen == TESTSCEN_SWARM_CPU
		|| s_State.scen == TESTSCEN_SWARM_GPU) ? 1 : 0;
}

s32 testScenarioGetSwarmMaxCount(void)
{
	return testScenarioIsSwarmActive() ? TESTSCEN_SWARM_MAX_COUNT : 0;
}

s32 testScenarioGetCurrentSwarmCount(void)
{
	return s_State.current_count;
}

void testScenarioSetCurrentSwarmCount(s32 count)
{
	s_State.current_count = count;
}

void testScenarioReset(void)
{
	if (s_State.scen != TESTSCEN_NONE) {
		sysLogPrintf(LOG_NOTE,
			"TESTSCEN.RESET: clearing scenario %d (was at count=%d)",
			(int)s_State.scen, (int)s_State.current_count);
	}
	s_State.scen          = TESTSCEN_NONE;
	s_State.method        = SWARM_METHOD_CPU;
	s_State.stagenum      = -1;
	s_State.current_count = 0;
	s_State.map_id[0]     = '\0';
}
