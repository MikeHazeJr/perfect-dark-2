/*
 * swarm_test.c -- Swarm benchmark runtime (S483).
 *
 * Per-frame driver for the Swarm CPU and Swarm GPU test scenarios.
 * Allocates Skedar chrs in a private test-mode table (so MAX_BOTS = 32
 * does not bound us), runs a simple seek-player tick on the CPU path,
 * delegates to swarm_gpu.cpp for the GPU path, drives the cycler,
 * polls per-frame chr deaths, and emits BENCHMARK.SWARM.* logs.
 *
 * Per design doc context/designs/gpu-swarm-and-test-scenarios-2026-04-27.md
 * sections C.1 - C.6 + G.1.1 (chr-pool sizing via setup.c numchrs hook).
 */

#include <PR/ultratypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "constants.h"
#include "system.h"
#include "data.h"
#include "bss.h"
#include "types.h"
#include "assetcatalog.h"
#include "lib/main.h"
#include "lib/model.h"
#include "lib/rng.h"
#include "lib/ailist.h"
#include "game/chr.h"
#include "game/chraction.h"
#include "game/body.h"
#include "game/prop.h"
#include "game/inv.h"
#include "game/cheats.h"
#include "actionmap.h"
#include "testscenarios.h"
#include "swarm_test.h"

/* GPU swarm module (port/fast3d/swarm_gpu.cpp). When that module is not
 * yet built, the stub below is used and swarmGpuAvailable() returns 0. */
extern s32  swarmGpuAvailable(void);
extern void swarmGpuStepAndApply(struct coord *player_pos,
                                 struct chrdata **chrs, s32 count);

/* ------------------------------------------------------------------
 * Cycle ladder
 * ------------------------------------------------------------------ */
const s32 SWARM_TEST_CYCLE[SWARM_TEST_CYCLE_STEPS] = {
	4, 8, 16, 32, 64, 128, 256
};

static s32 swarm_cycle_index(s32 count)
{
	for (s32 i = 0; i < SWARM_TEST_CYCLE_STEPS; i++) {
		if (SWARM_TEST_CYCLE[i] == count) return i;
	}
	return 0;
}

/* ------------------------------------------------------------------
 * Private chr table (escapes MAX_BOTS = 32)
 * ------------------------------------------------------------------ */
typedef struct {
	struct chrdata *chr;        /* NULL means slot is empty */
	u8              counted_kill; /* 1 once we've credited this slot's death */
} swarm_slot_t;

static swarm_slot_t s_Swarm[TESTSCEN_SWARM_MAX_COUNT];
static s32          s_SwarmCount;       /* current armed count */
static s32          s_SwarmKills;       /* local kill counter */
static s32          s_SwarmInitialized; /* 1 after session-start work done */
static s32          s_SwarmLastStage;   /* detect stage transitions */
static s32          s_SwarmLogTickAcc;  /* throttle per-frame log to ~1 / 60 frames */

/* Frame-time accumulators for the BENCHMARK.SWARM.* summary line. */
static f32 s_FrameMsAcc;
static s32 s_FrameMsSamples;
static u32 s_LastFrameTick;

/* ------------------------------------------------------------------
 * Skedar body resolution (catalog ID -> bodynum / headnum).
 * Done once per session-start; results cached in s_SkedarBody/Head.
 * ------------------------------------------------------------------ */
static s32 s_SkedarBodyNum = -1;
static s32 s_SkedarHeadNum = -1;

static void resolve_skedar_assets(void)
{
	const asset_entry_t *body_e = assetCatalogResolve("base:skedar");
	if (body_e && body_e->type == ASSET_BODY) {
		s_SkedarBodyNum = (s32)body_e->ext.body.bodynum;
	} else {
		sysLogPrintf(LOG_WARNING,
			"TESTSCEN.SWARM: 'base:skedar' body unresolvable; falling back to bodynum 61");
		s_SkedarBodyNum = 61; /* known Skedar slot per assetcatalog_base.c:307 */
	}
	/* Head: pair the Skedar warrior head if registered, else let
	 * bodyAllocateModel pick a sensible default by passing -1 (the
	 * existing bot path falls back to the body's natural head). */
	const asset_entry_t *head_e = assetCatalogResolve("base:skedar_warrior");
	if (head_e && head_e->type == ASSET_HEAD) {
		s_SkedarHeadNum = (s32)head_e->ext.head.headnum;
	} else {
		s_SkedarHeadNum = -1;
	}
	sysLogPrintf(LOG_NOTE,
		"TESTSCEN.SWARM: resolved skedar body=%d head=%d",
		s_SkedarBodyNum, s_SkedarHeadNum);
}

/* ------------------------------------------------------------------
 * Player setup
 * ------------------------------------------------------------------ */
static void apply_player_setup(void)
{
	if (!g_Vars.currentplayer) return;

	/* Invincible. Done both via the cheat bank (so any code path that
	 * reads cheatIsActive sees it) and via the per-player flag (the
	 * fast path for chraction.c::chrDamage gating). */
	g_CheatsActiveBank0 |= (1u << CHEAT_INVINCIBLE);
	g_CheatsActiveBank0 |= (1u << CHEAT_UNLIMITEDAMMO);
	g_CheatsActiveBank0 |= (1u << CHEAT_ALLGUNS);
	g_Vars.currentplayer->invincible = 1;

	/* Direct invSetAllGuns; the cheats.c gating around CHEAT_ALLGUNS
	 * requires PLAYERCOUNT() == 1 and !normmplayerisrunning, both of
	 * which are true for a local-only test scenario. We call it
	 * directly so the test mode is robust to the gate. */
	invSetAllGuns(true);

	sysLogPrintf(LOG_NOTE,
		"TESTSCEN.SWARM: player setup applied -- invincible, all guns, infinite ammo");
}

/* ------------------------------------------------------------------
 * Spawn a single Skedar chr at a position
 * ------------------------------------------------------------------ */
static struct chrdata *spawn_one_skedar(struct coord *pos, RoomNum *rooms)
{
	struct model *model = bodyAllocateModel(s_SkedarBodyNum,
		(s_SkedarHeadNum >= 0) ? s_SkedarHeadNum : 0, 0);
	if (model == NULL) {
		sysLogPrintf(LOG_WARNING,
			"TESTSCEN.SWARM: bodyAllocateModel returned NULL (skedar)");
		return NULL;
	}
	struct prop *prop = chrAllocate(model, pos, rooms, 0.0f,
		ailistFindById(GAILIST_IDLE));
	if (prop == NULL) {
		sysLogPrintf(LOG_WARNING,
			"TESTSCEN.SWARM: chrAllocate returned NULL (chr pool exhausted?)");
		return NULL;
	}
	propActivate(prop);
	propEnable(prop);

	struct chrdata *chr = prop->chr;
	chr->bodynum   = s_SkedarBodyNum;
	chr->headnum   = (s_SkedarHeadNum >= 0) ? s_SkedarHeadNum : 0;
	chr->race      = bodyGetRace(chr->bodynum);
	chr->team      = 1 << 7;          /* arbitrary non-default team */
	chr->maxdamage = 1.0f;            /* 1 HP per the directive */
	chr->damage    = 0.0f;
	chr->myaction  = (testScenarioActiveMethod() == SWARM_METHOD_GPU)
		? MA_SWARM_TEST_GPU_DRIVEN : MA_SWARM_TEST_SEEK;
	return chr;
}

/* ------------------------------------------------------------------
 * Despawn all swarm chrs
 * ------------------------------------------------------------------ */
static void despawn_all(void)
{
	for (s32 i = 0; i < TESTSCEN_SWARM_MAX_COUNT; i++) {
		if (s_Swarm[i].chr && s_Swarm[i].chr->prop) {
			chrRemove(s_Swarm[i].chr->prop, true);
		}
		s_Swarm[i].chr = NULL;
		s_Swarm[i].counted_kill = 0;
	}
	s_SwarmCount = 0;
}

/* ------------------------------------------------------------------
 * Spawn N skedars in a ring around the player
 * ------------------------------------------------------------------ */
#define SWARM_RING_RADIUS  600.0f

static void respawn_ring(s32 count)
{
	despawn_all();
	if (!g_Vars.currentplayer || !g_Vars.currentplayer->prop) {
		sysLogPrintf(LOG_WARNING,
			"TESTSCEN.SWARM: respawn_ring -- no player; skipping");
		return;
	}
	struct coord ppos = g_Vars.currentplayer->prop->pos;
	RoomNum prooms[2];
	prooms[0] = g_Vars.currentplayer->prop->rooms[0];
	prooms[1] = -1;

	if (count > TESTSCEN_SWARM_MAX_COUNT) count = TESTSCEN_SWARM_MAX_COUNT;
	if (count < 0) count = 0;

	s32 spawned = 0;
	for (s32 i = 0; i < count; i++) {
		f32 ang = (2.0f * 3.14159265f) * ((f32)i / (f32)count);
		struct coord pos = {
			ppos.x + SWARM_RING_RADIUS * cosf(ang),
			ppos.y,
			ppos.z + SWARM_RING_RADIUS * sinf(ang),
		};
		struct chrdata *chr = spawn_one_skedar(&pos, prooms);
		if (chr) {
			s_Swarm[i].chr = chr;
			s_Swarm[i].counted_kill = 0;
			spawned++;
		}
	}
	s_SwarmCount = spawned;
	testScenarioSetCurrentSwarmCount(spawned);
	sysLogPrintf(LOG_NOTE,
		"TESTSCEN.SWARM: respawn count=%d (target=%d) at player (%.0f,%.0f,%.0f)",
		spawned, count, ppos.x, ppos.y, ppos.z);
}

/* ------------------------------------------------------------------
 * CPU seek-player tick (mirrors the GPU shader's per-boid step)
 * ------------------------------------------------------------------ */
#define SWARM_MAX_SPEED      18.0f
#define SWARM_SEEK_WEIGHT    0.0050f
#define SWARM_DT             (1.0f / 60.0f)

static void cpu_seek_tick(struct coord *player_pos)
{
	for (s32 i = 0; i < s_SwarmCount; i++) {
		struct chrdata *chr = s_Swarm[i].chr;
		if (!chr || !chr->prop || chr->actiontype == ACT_DEAD
				|| chr->actiontype == ACT_DIE) {
			continue;
		}
		struct prop *prop = chr->prop;
		f32 dx = player_pos->x - prop->pos.x;
		f32 dy = 0.0f; /* keep ground-locked; no Y seek */
		f32 dz = player_pos->z - prop->pos.z;
		f32 d2 = dx * dx + dz * dz;
		if (d2 < 1.0f) continue;
		f32 d = sqrtf(d2);
		f32 vx = (dx / d) * SWARM_MAX_SPEED;
		f32 vz = (dz / d) * SWARM_MAX_SPEED;
		prop->pos.x += vx * (60.0f * SWARM_DT);
		prop->pos.y += dy;
		prop->pos.z += vz * (60.0f * SWARM_DT);
	}
}

/* ------------------------------------------------------------------
 * Death poll: count newly-dead swarm chrs as kills
 * ------------------------------------------------------------------ */
static void death_poll(void)
{
	for (s32 i = 0; i < s_SwarmCount; i++) {
		struct chrdata *chr = s_Swarm[i].chr;
		if (!chr) continue;
		if (s_Swarm[i].counted_kill) continue;
		if (chr->actiontype == ACT_DEAD || chr->actiontype == ACT_DIE
				|| !chr->prop) {
			s_Swarm[i].counted_kill = 1;
			s_SwarmKills++;
		}
	}
}

/* ------------------------------------------------------------------
 * Cycler: D-pad-down or KEY_0 advances the cycle
 * ------------------------------------------------------------------ */
static void cycler_tick(void)
{
	if (!actionPressed(0, ACTION_TESTSCEN_CYCLE_COUNT)) return;
	s32 idx = swarm_cycle_index(testScenarioGetCurrentSwarmCount());
	idx = (idx + 1) % SWARM_TEST_CYCLE_STEPS;
	s32 next = SWARM_TEST_CYCLE[idx];
	sysLogPrintf(LOG_NOTE,
		"TESTSCEN.SWARM: cycle %d -> %d", s_SwarmCount, next);
	respawn_ring(next);
	/* End-of-cycle summary log for the just-finished count. */
	sysLogPrintf(LOG_NOTE,
		"BENCHMARK.SWARM.%s: SUMMARY count=%d kills=%d frame_avg_ms=%.3f",
		(testScenarioActiveMethod() == SWARM_METHOD_GPU) ? "GPU" : "CPU",
		s_SwarmCount, s_SwarmKills,
		(s_FrameMsSamples > 0)
			? (s_FrameMsAcc / (f32)s_FrameMsSamples) : 0.0f);
	s_FrameMsAcc = 0.0f;
	s_FrameMsSamples = 0;
	s_SwarmKills = 0;
}

/* ------------------------------------------------------------------
 * Per-frame entry
 * ------------------------------------------------------------------ */
void swarmTestTick(void)
{
	if (!testScenarioIsSwarmActive()) {
		if (s_SwarmInitialized) {
			swarmTestOnSessionEnd();
		}
		return;
	}

	/* Detect stage transition (different stagenum from last tick). If the
	 * user exits to a system stage (title / pak menu / credits) treat
	 * as a full session-end and clear scenario state. Otherwise just
	 * reset internal counters so the new stage starts fresh. */
	if (g_StageNum != s_SwarmLastStage) {
		s32 newStage = g_StageNum;
		const s32 wasInitialized = s_SwarmInitialized;
		s_SwarmLastStage   = newStage;
		s_SwarmInitialized = 0;
		s_SwarmKills       = 0;
		s_FrameMsAcc       = 0.0f;
		s_FrameMsSamples   = 0;
		s_SwarmLogTickAcc  = 0;

		/* Mirror of STAGE_IS_SYSTEM (constants.h) but inlined to avoid
		 * pulling the macro chain. STAGE_TITLE=0x5a, STAGE_BOOTPAKMENU=0x5b,
		 * STAGE_CREDITS=0x5c. */
		const bool exitedToSystem = (newStage == 0x5a || newStage == 0x5b
			|| newStage == 0x5c);
		if (wasInitialized && exitedToSystem) {
			sysLogPrintf(LOG_NOTE,
				"TESTSCEN.SWARM: exit to system stage 0x%02x; ending session",
				(u32)newStage);
			swarmTestOnSessionEnd();
			return;
		}
	}

	/* Frame-time accumulator (delta from last tick in ms). */
	if (s_LastFrameTick != 0) {
		u32 delta = g_Vars.lvframenum - s_LastFrameTick;
		f32 ms = (f32)delta * (1000.0f / 60.0f);
		s_FrameMsAcc += ms;
		s_FrameMsSamples++;
	}
	s_LastFrameTick = g_Vars.lvframenum;

	/* Session-start work the first tick we run after a stage load. */
	if (!s_SwarmInitialized) {
		if (!g_Vars.currentplayer || !g_Vars.currentplayer->prop) {
			/* Player not yet alive; wait for next tick. */
			return;
		}
		resolve_skedar_assets();
		apply_player_setup();
		respawn_ring(TESTSCEN_SWARM_INITIAL_COUNT);
		s_SwarmInitialized = 1;
		sysLogPrintf(LOG_NOTE,
			"TESTSCEN.SWARM: session armed -- method=%s count=%d",
			(testScenarioActiveMethod() == SWARM_METHOD_GPU)
				? "GPU" : "CPU",
			s_SwarmCount);
	}

	/* Player invincibility re-asserted each tick (mid-game inventory
	 * pickup or other path could clear it). */
	if (g_Vars.currentplayer) {
		g_Vars.currentplayer->invincible = 1;
	}

	/* Drive per-method simulation. GPU path is delegated to swarm_gpu;
	 * its availability is reported via swarmGpuAvailable() and falls
	 * back to CPU seek when compute shaders are unavailable or the
	 * module is not built yet. */
	struct coord player_pos = {0, 0, 0};
	if (g_Vars.currentplayer && g_Vars.currentplayer->prop) {
		player_pos = g_Vars.currentplayer->prop->pos;
	}
	if (testScenarioActiveMethod() == SWARM_METHOD_GPU
			&& swarmGpuAvailable()) {
		struct chrdata *chrs[TESTSCEN_SWARM_MAX_COUNT];
		for (s32 i = 0; i < TESTSCEN_SWARM_MAX_COUNT; i++) {
			chrs[i] = s_Swarm[i].chr;
		}
		swarmGpuStepAndApply(&player_pos, chrs, s_SwarmCount);
	} else {
		cpu_seek_tick(&player_pos);
	}

	death_poll();
	cycler_tick();

	/* Throttle per-frame log to ~1 line per 60 frames. */
	s_SwarmLogTickAcc++;
	if (s_SwarmLogTickAcc >= 60) {
		s_SwarmLogTickAcc = 0;
		sysLogPrintf(LOG_NOTE,
			"BENCHMARK.SWARM.%s: count=%d kills=%d alive=%d",
			(testScenarioActiveMethod() == SWARM_METHOD_GPU) ? "GPU" : "CPU",
			s_SwarmCount, s_SwarmKills,
			s_SwarmCount - s_SwarmKills);
	}
}

/* ------------------------------------------------------------------
 * Cleanup
 * ------------------------------------------------------------------ */
void swarmTestOnSessionEnd(void)
{
	despawn_all();
	s_SwarmKills        = 0;
	s_SwarmInitialized  = 0;
	s_SwarmLastStage    = -1;
	s_SwarmLogTickAcc   = 0;
	s_FrameMsAcc        = 0.0f;
	s_FrameMsSamples    = 0;
	s_LastFrameTick     = 0;
	testScenarioReset();
}

/* ------------------------------------------------------------------
 * Read accessors (HUD + tests)
 * ------------------------------------------------------------------ */
s32 swarmTestGetActiveCount(void) { return s_SwarmCount; }
s32 swarmTestGetKillCount(void)   { return s_SwarmKills; }
