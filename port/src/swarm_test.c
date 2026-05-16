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
#include "memsizes.h"  /* AMMO_TYPE_COUNT */
#include "system.h"
#include "data.h"
#include "bss.h"
#include "types.h"
#include "assetcatalog.h"
#include "lib/main.h"
#include "lib/memp.h"
#include "lib/model.h"
#include "lib/rng.h"
#include "lib/ailist.h"
#include "game/spawnpool.h"  /* spawnPoolCorrectPosition for ring spawn fix-up */
#include "game/chr.h"
#include "game/chraction.h"
#include "game/body.h"
#include "game/prop.h"
#include "game/inv.h"
#include "game/cheats.h"
#include "game/bondgun.h"  /* bgunEquipWeapon for power-weapon loadout */
#include "game/atan2f.h"
#include "game/botinvinit.h"
#include "actionmap.h"
#include "testscenarios.h"
#include "swarm_test.h"

/* Forward decls -- these come from src/game (no public header includes them
 * with the right linkage from C++/legacy headers). */
extern bool chrSetPos(struct chrdata *chr, struct coord *pos, RoomNum *rooms,
	f32 theta, bool findground);

/* GPU swarm module (port/fast3d/swarm_gpu.cpp). When that module is not
 * yet built, the stub below is used and swarmGpuAvailable() returns 0.
 *
 * B-308 first slice (c3807, 2026-05-15): swarmGpuStepAndApply now takes
 * a `method` argument and a `player_propnum`. `method` is one of
 * SWARM_METHOD_GPU_POS_ONLY or SWARM_METHOD_GPU_FULL (passing CPU here is
 * a no-op caller error); the GPU side gates the AI compute step on it.
 * player_propnum is the CPU's prop index for the local player; the GPU
 * writes it back into the per-bot target_propnum field so the CPU
 * consumer can correlate. */
extern s32  swarmGpuAvailable(void);
extern void swarmGpuStepAndApply(struct coord *player_pos,
                                 struct chrdata **chrs, s32 count,
                                 s32 method, s32 player_propnum);

/* ------------------------------------------------------------------
 * Cycle ladder (S594h-Unit-A, 2026-05-01).
 *
 * Two regimes:
 *  - Small-step probe range 4..256 (carried over from S593h, lets Mike
 *    see the curve bend at low counts).
 *  - 256-bot increments 512..4096 ("push the limits" per Mike's
 *    directive).
 *
 * The cycler is bidirectional: ACTION_TESTSCEN_CYCLE_COUNT advances to
 * the next-higher count; ACTION_TESTSCEN_CYCLE_PREV moves to the
 * next-lower count. Wrap is directional (top wraps to bottom on next,
 * bottom wraps to top on prev).
 * ------------------------------------------------------------------ */
const s32 SWARM_TEST_CYCLE[SWARM_TEST_CYCLE_STEPS] = {
	    4,    8,   16,   32,   48,   64,  128,  256,
	  512,  768, 1024, 1280, 1536, 1792, 2048, 2304,
	 2560, 2816, 3072, 3328, 3584, 3840, 4096
};

/* swarm_cycle_index removed in S594h-Unit-C: the cycler now advances
 * an explicit s_LadderIdx instead of inferring the position from the
 * actual count, so the count-to-index lookup is no longer needed. */

/* ------------------------------------------------------------------
 * Private chr table (escapes MAX_BOTS = 32)
 *
 * Each slot remembers the chosen scale + spawn ring position so the
 * S594h-Unit-A kill-respawn path can reuse them when a chr is killed
 * (replacement spawns at the same ring radius, with the same scale,
 * keeping the population stable at the target count).
 * ------------------------------------------------------------------ */
typedef struct {
	struct chrdata *chr;          /* NULL means slot is empty */
	u8              counted_kill; /* 1 once we've credited this slot's death */
	u8              team_idx;     /* 0 = team A (TEAM_ENEMY), 1 = team B (2-team mode only) */
	u16             respawn_delay;/* >0 means "respawn me in N frames"; 0 = idle */
	f32             scale_factor; /* the per-bot scale picked at spawn */
	struct coord    spawn_pos;    /* original ring position; reused on respawn */
	RoomNum         spawn_room;   /* room of original ring position; reused on respawn */
} swarm_slot_t;

static swarm_slot_t s_Swarm[TESTSCEN_SWARM_MAX_COUNT];
static s32          s_SwarmCount;       /* current armed count */
static s32          s_SwarmKills;       /* local kill counter */
static s32          s_SwarmInitialized; /* 1 after session-start work done */
static s32          s_SwarmLastStage;   /* detect stage transitions */
static s32          s_SwarmLogTickAcc;  /* throttle per-frame log to ~1 / 60 frames */
static s32          s_SwarmRespawnsThisCycle; /* count of replacement spawns since last cycle change */

/* S594h-A2 (2026-05-01): team mode + visibility mode module state. */
static swarm_team_mode_t s_TeamMode = SWARM_TEAMS_SIMS_VS_PLAYERS;
static swarm_vis_mode_t  s_VisMode  = SWARM_VIS_ALWAYS_SEE;

/* S594h-Unit-B (2026-05-01): spawn strategy module state. */
static swarm_spawn_strategy_t s_SpawnStrategy = SWARM_SPAWN_RING;

/* S594h-Unit-C (2026-05-01): explicit cycler index. The pre-fix path
 * inferred the ladder index from testScenarioGetCurrentSwarmCount(),
 * which returns the ACTUAL spawned count. When volume placement
 * failed for some slots (e.g. target=64, actual=60), the actual count
 * was not in the ladder, swarm_cycle_index returned 0 (fallback), and
 * the next press jumped to idx=1 (=8) regardless of where the user
 * actually was on the ladder. Mike's playtest log: 60 -> 8 (idx=1).
 *
 * The fix: track the LOGICAL ladder position directly, advance it on
 * each press, look up the new target count from SWARM_TEST_CYCLE[].
 * The actual spawned count is independent of the ladder position --
 * it just reflects how many bots the placement loop fit. */
static s32 s_LadderIdx = 0; /* 0 = lowest ladder count (4) */

/* S594h-Unit-A.5 (2026-05-01): MPOPTION_TEAMSENABLED snapshot for
 * restore at session-end. The "team colors visible" feature toggles
 * the bit while a 2-team swarm session is active; we must restore the
 * pre-session value when leaving so the user's normal MP setup is
 * unchanged. 0xff = sentinel "not snapshotted yet". */
static u8 s_PriorTeamsEnabledSnapshot = 0xff;

/* Frame-time accumulators for the BENCHMARK.SWARM.* summary line. */
static f32 s_FrameMsAcc;
static s32 s_FrameMsSamples;
static u32 s_LastFrameTick;

/* Private aibot pool -- escapes the MAX_BOTS=32 cap that gates the
 * normal botmgr/match path. Each swarm Skedar gets its own aibot
 * struct so chr->aibot is non-NULL and the bot AI in chraTick /
 * chraiExecute / botTick can drive seek/attack/dodge behaviour.
 *
 * Why we don't go through botmgrAllocateBot:
 *   - It gates on g_BotCount < MAX_BOTS=32 -- no path to 256.
 *   - It registers the bot in g_MpBotChrPtrs / g_MpAllChrPtrs which
 *     are match-scoring tables sized to MAX_MPCHRS=40. Beyond 40
 *     entries those overflow.
 *   - Our chrs are intentionally outside the match (no scoring, no
 *     respawn-pad allocation, no spawn-pool participation).
 *
 * What we still get from real bot AI:
 *   - Target selection (chr->target = player, set by chrInit).
 *   - Action dispatch via chraTick: ACT_STAND -> ACT_RUNPOS -> ACT_ATTACK -> ...
 *   - Collision-aware movement via chrTryStop / chrMoveAlongPath.
 *   - Animation, weapons, dodge, gunfire, audio. */
static struct aibot s_SwarmAibots[TESTSCEN_SWARM_MAX_COUNT];
static u8           s_SwarmAibotInUse[TESTSCEN_SWARM_MAX_COUNT];

/* Dedicated swarm bot config -- shared by every swarm aibot.
 *
 * Mike's playtest 2026-05-01 surfaced that swarm bots ran "aimlessly"
 * because the prior implementation set chr->team = 1 << 7 (TEAM_NONCOMBAT,
 * 0x80) AND used g_BotConfigsArray[0] as a shared config. The
 * non-combat team flag is a hard "do not engage" signal to the bot
 * AI; that single bit explained the no-aggression bug.
 *
 * The fix:
 *   1. swarm bots get their own config struct (this one), so we can
 *      set type/difficulty without polluting the match's bot configs.
 *   2. base.team = 1 -- becomes chr->team = 1 << 1 = 0x02 = TEAM_ENEMY,
 *      which is DIFFERENT from the player's TEAM_01 (the player chr is
 *      assigned 1 << g_PlayerConfigsArray[0].base.team = 1 << 0 = 0x01
 *      in playerreset.c:647). Different team -> AI engages.
 *   3. type = BOTTYPE_KAZE -- "does not keep distance" -- aggressive
 *      rusher AI. Closest match to Mike's "always aggressive" directive.
 *   4. difficulty = BOTDIFF_PERFECT -- gives 11.2x base speed in
 *      botCalculateMaxSpeed (vs 7.6x for NORMAL), i.e. ~1.47x normal
 *      speed. Closest standard bracket to Mike's "1.5x normal speed"
 *      directive without per-frame post-processing.
 *
 * MPOPTION_TEAMSENABLED stays unset in g_MpSetup.options for swarm
 * scenarios, which is what suppresses the radar/HUD team-colour
 * highlights Mike asked us to remove. Different team IDs still drive
 * AI hostility, but no UI surfaces the difference.
 */
static struct mpbotconfig s_SwarmBotConfig;
static s32                s_SwarmBotConfigInited;

static void swarm_init_bot_config_once(void)
{
	if (s_SwarmBotConfigInited) return;
	memset(&s_SwarmBotConfig, 0, sizeof(s_SwarmBotConfig));
	s_SwarmBotConfig.base.team = 1; /* -> chr->team = TEAM_ENEMY */
	/* S593h (2026-05-01): bumped from BOTTYPE_KAZE+BOTDIFF_PERFECT
	 * to BOTTYPE_SPEED+BOTDIFF_DARK per Mike's directive. SPEED type
	 * applies a 14x multiplier in botCalculateMaxSpeed (vs 7.6x for
	 * NORMAL difficulty) -- bots noticeably zip toward the player.
	 * BOTDIFF_DARK is the hardest AI difficulty preset (Dark Agent
	 * tier), maxing reaction speed, accuracy, and aggression in
	 * the AI-decision paths that read difficulty. */
	s_SwarmBotConfig.type       = BOTTYPE_SPEED;
	s_SwarmBotConfig.difficulty = BOTDIFF_DARK;
	s_SwarmBotConfigInited = 1;
}

/* S593h (2026-05-01): per-spawn scale chooser. Returns a value in
 * [0.35, 0.65) weighted toward the small end via (rand01)^2. The
 * squaring of a uniform [0,1) random push the distribution low so
 * most bots are tiny with occasional larger ones. Per Mike's
 * directive: "I think it would be good to slightly randomize their
 * scale between 0.2 and 0.6, weighted towards smaller."
 *
 * S593h-followup (2026-05-02): Mike playtest "a bit too small".
 * Range bumped from [0.2, 0.6) to [0.35, 0.65). Squared bias
 * preserved so most bots cluster near 0.35-0.45 with occasional
 * larger silhouettes up to 0.65 for visual variety. */
static f32 swarm_pick_scale(void)
{
	f32 r = RANDOMFRAC();
	return 0.35f + 0.30f * (r * r);
}

/* Public API: returns 1 if `chr` is a swarm bot (marked with the
 * CHRHFLAG_00040000 swarm-lock bit at spawn). Used by chr.c and
 * chraction.c to gate behaviour: chrSetPerimEnabled refuses to
 * re-enable perim for swarm bots (so bot-bot collision stays off
 * per Mike's directive), and chrHasLosToChr branches on the
 * S594h-A2 visibility mode for swarm-bot perspectives. */
s32 swarmTestIsSwarmChr(struct chrdata *chr)
{
	if (!chr) return 0;
	return (chr->hidden & 0x00040000) ? 1 : 0;
}

/* Team / visibility accessors (S594h-A2). Header-declared. */
void swarmTestSetTeamMode(swarm_team_mode_t mode)
{
	if (mode < 0 || mode >= SWARM_TEAMS_COUNT) return;
	s_TeamMode = mode;
}

swarm_team_mode_t swarmTestGetTeamMode(void) { return s_TeamMode; }
swarm_vis_mode_t  swarmTestGetVisMode(void)  { return s_VisMode;  }

void swarmTestSetSpawnStrategy(swarm_spawn_strategy_t strategy)
{
	if (strategy < 0 || strategy >= SWARM_SPAWN_COUNT) return;
	s_SpawnStrategy = strategy;
}

swarm_spawn_strategy_t swarmTestGetSpawnStrategy(void) { return s_SpawnStrategy; }

/* Find an unused aibot slot. Returns NULL if all are taken. */
static struct aibot *swarm_alloc_aibot(s32 *out_index)
{
	for (s32 i = 0; i < TESTSCEN_SWARM_MAX_COUNT; i++) {
		if (!s_SwarmAibotInUse[i]) {
			s_SwarmAibotInUse[i] = 1;
			if (out_index) *out_index = i;
			return &s_SwarmAibots[i];
		}
	}
	return NULL;
}

/* Release an aibot slot back to the pool. The aibot's ammoheld
 * pointer points to MEMPOOL_STAGE memory which is freed at level
 * teardown -- we don't need to free it here. */
static void swarm_free_aibot(struct aibot *aibot)
{
	if (!aibot) return;
	uintptr_t base = (uintptr_t)&s_SwarmAibots[0];
	uintptr_t end  = (uintptr_t)&s_SwarmAibots[TESTSCEN_SWARM_MAX_COUNT];
	uintptr_t addr = (uintptr_t)aibot;
	if (addr >= base && addr < end) {
		s32 idx = (s32)((addr - base) / sizeof(struct aibot));
		s_SwarmAibotInUse[idx] = 0;
	}
}

/* Init an aibot struct so the bot AI can drive seek/attack/dodge.
 * Mirrors botmgrAllocateBot's aibot init block (botmgr.c:163-351),
 * minus the match-scoring registrations (g_MpBotChrPtrs etc.). Uses
 * the dedicated swarm config (BOTTYPE_KAZE / BOTDIFF_PERFECT) so we
 * get aggressive rusher behaviour at ~1.5x base speed without
 * polluting the match's g_BotConfigsArray[]. */
static void swarm_init_aibot(struct chrdata *chr, struct aibot *aibot)
{
	swarm_init_bot_config_once();
	memset(aibot, 0, sizeof(struct aibot));

	/* aibotnum = -1 signals "outside botmgr's slot management" so
	 * any code that gates on aibotnum >= 0 / < MAX_BOTS skips us. */
	aibot->aibotnum = -1;

	/* Dedicated swarm bot config -- BOTTYPE_KAZE + BOTDIFF_PERFECT.
	 * See `s_SwarmBotConfig` block above for the full rationale. */
	aibot->config = &s_SwarmBotConfig;

	aibot->ammoheld = mempAlloc(AMMO_TYPE_COUNT * sizeof(s32), MEMPOOL_STAGE);
	if (aibot->ammoheld) {
		for (s32 i = 0; i < AMMO_TYPE_COUNT; i++) {
			aibot->ammoheld[i] = 0;
		}
	}

	/* Difficulty / behaviour parameters. Use NORMAL difficulty defaults. */
	aibot->followchance = 20;

	/* Weapon: combat knife per Mike's S593h directive ("give the bots
	 * combat knife as a spawn weapon"). The bot AI sees ismeleeweapon=1
	 * and adjusts engagement style accordingly -- bots close-range and
	 * swing instead of trying to maintain ranged distance. Pairs well
	 * with BOTTYPE_SPEED + BOTDIFF_DARK for a "horde of melee skedars"
	 * feel. */
	aibot->weaponnum = WEAPON_COMBATKNIFE;
	aibot->ismeleeweapon = true;
	aibot->gunfunc = FUNC_PRIMARY;

	/* AIBOTCMD_ATTACK locks the bot into attack mode regardless of
	 * the AI's tactical pick (defend / follow / hold). Mike's
	 * 2026-05-01 directive: "ensure the bots are angry at me." Setting
	 * attackpropnum to the player is the directive's target -- the
	 * bot AI walks attackpropnum and commits to closing on it. */
	aibot->command = AIBOTCMD_ATTACK;
	if (g_Vars.currentplayer && g_Vars.currentplayer->prop) {
		aibot->attackpropnum = (s32)(g_Vars.currentplayer->prop - g_Vars.props);
	} else {
		aibot->attackpropnum = -1;
	}

	/* Sentinels for "no current target / no last-seen". */
	aibot->attackingplayernum = -1;
	aibot->followingplayernum = -1;
	aibot->dangerouspropnum = -1;
	aibot->distmode = -1;
	aibot->lastkilledbyplayernum = -1;
	aibot->feudplayernum = -1;
	aibot->random3ttl60 = -1;
	aibot->realignangleframe = -1;
	aibot->targetlastseen60 = -1;
	aibot->lastseenanytarget60 = -1;
	aibot->punchtimer60[HAND_LEFT] = -1;
	aibot->punchtimer60[HAND_RIGHT] = 0;

	/* Per-chr distance/visibility tables. -1 / U32_MAX = unknown. */
	for (s32 i = 0; i < MAX_MPCHRS; i++) {
		aibot->chrnumsbydistanceasc[i] = -1;
		aibot->chrdistances[i] = U32_MAX;
		aibot->chrsinsight[i] = 0;
		aibot->chrslastseen60[i] = -1;
		aibot->chrrooms[i] = -1;
	}

	aibot->roty = chr->model ? modelGetChrRotY(chr->model) : 0.0f;
	aibot->lookangle = aibot->roty;

	aibot->random1 = rngRandom();
	aibot->random2 = rngRandom();
	aibot->randomfrac = RANDOMFRAC();

	/* Defend-hold pos = spawn position, in case the AI ever falls
	 * back to defend mode. */
	if (chr->prop) {
		aibot->defendholdpos = chr->prop->pos;
	}
	aibot->hillpadnum  = -1;
	aibot->hillcovernum = -1;
	aibot->lastknownhill = -1;
}

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
 *
 * Per-tick re-assertion of the swarm test mode's player state:
 *   1. Invincibility (cheat bank + per-player flag).
 *   2. Bottomless ammo (cheat bank).
 *   3. Curated power-weapon loadout (one-shot at session start).
 *
 * S593h (2026-05-01): Mike's directive REPLACED the earlier
 * "all guns" approach with "give bots combat knife as a spawn
 * weapon. I will get power weapons, bottomless clip." `equipallguns`
 * is now FALSE; we explicitly give Mike the power weapons one by
 * one through invGiveSingleWeapon. This also addresses item 6
 * (weapon visibility) because explicit single weapons drive the
 * standard master-load path, which couples the gun model with the
 * hand model -- the previous all-guns mode left the hand model
 * unbound (visible only during punch/melee).
 *
 * The curated power-weapon list:
 *   FARSIGHT    -- through-wall scope rifle
 *   REAPER      -- minigun
 *   DEVASTATOR  -- 3-round grenade launcher
 *   SLAYER      -- guided rocket launcher
 *   MAULER      -- charging hand cannon
 *   RCP120      -- silenced SMG with cloak
 *
 * Bots get WEAPON_COMBATKNIFE in swarm_init_aibot. With the player
 * invincible and bottomless-clip on power weapons, the swarm's role
 * is to converge melee-style on the player (benchmark stress, not a
 * fair fight).
 * ------------------------------------------------------------------ */
static const s32 s_PowerWeapons[] = {
	WEAPON_FARSIGHT,
	WEAPON_REAPER,
	WEAPON_DEVASTATOR,
	WEAPON_SLAYER,
	WEAPON_MAULER,
	WEAPON_RCP120,
};

static s32 s_PlayerLoadoutGiven = 0;

static void apply_player_setup(void)
{
	if (!g_Vars.currentplayer) return;

	/* Cheat banks first so anything queried via cheatIsActive sees the
	 * right state. UNLIMITEDAMMO -> bottomless clip per Mike's
	 * directive; INVINCIBLE so 256 hostile bots don't kill him.
	 * CHEAT_ALLGUNS is INTENTIONALLY OMITTED (S593h): we hand-pick
	 * power weapons instead. */
	g_CheatsActiveBank0 |= (1u << CHEAT_INVINCIBLE);
	g_CheatsActiveBank0 |= (1u << CHEAT_UNLIMITEDAMMO);
	g_Vars.currentplayer->invincible = 1;

	/* Force equipallguns OFF so the weapon master-load runs through
	 * the per-weapon path (which binds the hand model). The all-guns
	 * mode was the suspect behind item 6 (weapon visible in UI but
	 * not rendered). */
	g_Vars.currentplayer->equipallguns = false;

	/* One-shot power-weapon loadout. invGiveSingleWeapon is idempotent
	 * (returns false if already present), but we still gate on
	 * s_PlayerLoadoutGiven to skip the inventory-walk cost on every
	 * tick. The flag resets at session end. */
	if (!s_PlayerLoadoutGiven) {
		const s32 n = (s32)(sizeof(s_PowerWeapons) / sizeof(s_PowerWeapons[0]));
		for (s32 i = 0; i < n; i++) {
			invGiveSingleWeapon(s_PowerWeapons[i]);
		}
		/* Equip the first power weapon (Farsight). bgunEquipWeapon
		 * triggers the master-load state machine, which loads the
		 * weapon model AND the hand model paired with the player's
		 * body. */
		bgunEquipWeapon(s_PowerWeapons[0]);
		s_PlayerLoadoutGiven = 1;
		sysLogPrintf(LOG_NOTE,
			"TESTSCEN.SWARM: applied power-weapon loadout (FARSIGHT/REAPER/DEVASTATOR/SLAYER/MAULER/RCP120) + bottomless clip + invincible");

		/* S594h-Unit-C (2026-05-01): post-equip diag to surface the
		 * weapon master-load state for Mike's "I was spawning with
		 * weapons but could only use melee" report. Hypothesis: the
		 * master-load HANDS state isn't completing (hands[].inuse stays
		 * false), so bgunGetWeaponNum returns WEAPON_NONE and the fire
		 * path falls back to melee. The fields surfaced here let the
		 * next playtest log identify whether the master-load is
		 * progressing or stalled, and whether the hand model filenum
		 * resolved cleanly through catalogGetBodyHandFilenum.
		 *
		 * NOTE: this is a CROSS-CUTTING regression that may not be
		 * swarm-test-specific (Mike's hypothesis: "overall character
		 * initialization issue, maybe related to our recent catalog
		 * weapon work"). The actual fix likely lives in body.c /
		 * bondgun.c / catalog* not here. This block is purely
		 * diagnostic. */
		struct player *p = g_Vars.currentplayer;
		if (p) {
			sysLogPrintf(LOG_NOTE,
				"TESTSCEN.SWARM.WPN: post-equip player=0 cur_wpn=%d switchto=%d "
				"masterload=%d gunloadstate=%d gunmemowner=%d gunmemtype=%d "
				"hands[L].inuse=%d hands[R].inuse=%d hands[L].state=%d hands[R].state=%d "
				"equipallguns=%d",
				(s32)p->gunctrl.weaponnum,
				(s32)p->gunctrl.switchtoweaponnum,
				(s32)p->gunctrl.masterloadstate,
				(s32)p->gunctrl.gunloadstate,
				(s32)p->gunctrl.gunmemowner,
				(s32)p->gunctrl.gunmemtype,
				(s32)p->hands[HAND_LEFT].inuse,
				(s32)p->hands[HAND_RIGHT].inuse,
				(s32)p->hands[HAND_LEFT].state,
				(s32)p->hands[HAND_RIGHT].state,
				(s32)p->equipallguns);
		}
	}
}

/* ------------------------------------------------------------------
 * Spawn a single Skedar chr at a position
 *
 * CPU mode (SWARM_METHOD_CPU): the chr is allocated as a REAL bot with
 *   chr->aibot pointing into our private pool, ailist =
 *   GAILIST_AIBOT_INIT, and chr->myaction = MA_AIBOTMAINLOOP. The AI
 *   then drives seek/attack/dodge/movement on the CPU. Position
 *   updates flow through chrTryStop / chrMoveAlongPath which handle
 *   collision, so chr-vs-chr and chr-vs-player no-clip is resolved
 *   naturally without extra collision wiring on our side.
 *
 * GPU mode (SWARM_METHOD_GPU): the chr is allocated as a passive prop
 *   with ailist = GAILIST_IDLE and no aibot. Position is driven by
 *   swarmGpuStepAndApply via chrSetPos. This is the position-only
 *   benchmark mode; full bot behaviour on GPU is a follow-up pillar
 *   (see context/designs/in-flight/gpu-swarm-bot-pipeline.md).
 * ------------------------------------------------------------------ */
static struct chrdata *spawn_one_skedar(struct coord *pos, RoomNum *rooms,
                                        s32 team_idx, f32 *out_scale)
{
	const swarm_method_t method = testScenarioActiveMethod();

	struct model *model = bodyAllocateModel(s_SkedarBodyNum,
		(s_SkedarHeadNum >= 0) ? s_SkedarHeadNum : 0, 0);
	if (model == NULL) {
		sysLogPrintf(LOG_WARNING,
			"TESTSCEN.SWARM: bodyAllocateModel returned NULL (skedar)");
		return NULL;
	}

	const s32 ailist_id = (method == SWARM_METHOD_CPU)
		? GAILIST_AIBOT_INIT
		: GAILIST_IDLE;

	struct prop *prop = chrAllocate(model, pos, rooms, 0.0f,
		ailistFindById(ailist_id));
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

	/* Team assignment depends on team_idx:
	 *   team_idx=0 -> TEAM_ENEMY (0x02)
	 *   team_idx=1 -> TEAM_04   (0x04)
	 * Both are hostile to the player's TEAM_01 and to each other (the
	 * bot AI's `chr->team == other->team` ally test returns false for
	 * any pair of distinct combat-class teams). In SIMS_VS_PLAYERS
	 * mode every bot is team_idx=0; in TWO_TEAMS_PLUS_PLAYER mode
	 * respawn_ring alternates 0/1. */
	chr->team = (team_idx == 1) ? TEAM_04 : TEAM_ENEMY;

	/* Half normal MP-bot health (8.0). botmgrAllocateBot defaults to
	 * 8.0 (botmgr.c:153) for MP simulants; halving gives 4.0. The
	 * S593 config used 1.0 ("1 HP per the directive") but Mike's
	 * 2026-05-01 directive supersedes that with "1/2 normal health". */
	chr->maxdamage = 4.0f;
	chr->damage    = 0.0f;

	/* Per-spawn random scale weighted toward smaller. Mike's S593h
	 * directive (2026-05-01): "randomize their scale between 0.2 and
	 * 0.6, weighted towards smaller". `swarm_pick_scale()` returns a
	 * value in [0.2, 0.6) using a squared-rand bias.
	 *
	 * The visual scale (chr->model->scale) and collision geometry
	 * (chr->radius, chr->height) MUST stay in sync -- S593e/S593f
	 * established that the engine reads chr->radius/height for
	 * collision and chr->model->scale only for rendering. Applying
	 * the same factor to all three keeps the visual and collider
	 * footprints matched. */
	const f32 rand_scale = swarm_pick_scale();
	chr->radius = (s32)(30.0f * rand_scale);
	if (chr->radius < 4) chr->radius = 4;
	chr->height = (s32)(185.0f * rand_scale);
	if (chr->height < 24) chr->height = 24;
	if (chr->model) {
		modelSetScale(chr->model, chr->model->scale * rand_scale);
	}
	if (out_scale) *out_scale = rand_scale;

	/* No bot-bot collision. Mike's directive: "Don't let them collide
	 * with each other either." We mark the chr with bit 0x00040000
	 * (formerly CHRHFLAG_00040000 "Not used"; commandeered as the
	 * swarm-lock marker S593h). chrSetPerimEnabled in chr.c gates
	 * the re-enable path on this bit, so the perim stays disabled
	 * permanently for swarm bots even though the engine's
	 * chrCalculatePushPos toggles enable/disable around its own push
	 * tests. With perim disabled, chr-vs-chr collision queries skip
	 * the swarm bot, so they can phase through each other.
	 *
	 * Side effect Mike accepted by directive: the player can also
	 * walk through swarm bots (since the player's bondwalk perim
	 * test reads the same flag). For a 256-bot swarm this is fine --
	 * bullets still hit, AI still attacks. World/BG collision is
	 * separate (cdFindGroundInfoAtCyl) and unaffected. */
	chr->hidden |= 0x00040000;          /* swarm-lock marker */
	chr->hidden |= CHRHFLAG_PERIMDISABLED; /* perim off (and stays off) */

	if (method == SWARM_METHOD_CPU) {
		/* Real bot AI path. Allocate aibot from our private pool;
		 * if the pool is exhausted we fall through to a no-AI chr
		 * which will just stand around -- visible failure mode
		 * rather than a crash. */
		s32 ai_index = -1;
		struct aibot *aibot = swarm_alloc_aibot(&ai_index);
		if (aibot) {
			swarm_init_aibot(chr, aibot);
			chr->aibot = aibot;
			chr->myaction = MA_AIBOTMAINLOOP;
			botinvInit(chr, 10);
		} else {
			sysLogPrintf(LOG_WARNING,
				"TESTSCEN.SWARM: aibot pool exhausted -- chr will be passive");
			chr->myaction = MA_NONE;
		}
	} else {
		/* GPU mode: chr is passive prop. Position is driven externally
		 * by swarmGpuStepAndApply. */
		chr->myaction = MA_NONE;
	}

	return chr;
}

/* ------------------------------------------------------------------
 * Despawn all swarm chrs
 *
 * S483-followup (2026-04-30, B-264): chrRemove() alone is NOT enough.
 * It clears chr->model = NULL and chr->chrnum = -1, but leaves the
 * prop on the activeprops linked list and on the prop pool. The next
 * frame's propsTickPlayer iterates activeprops, calls chrTick on this
 * dead prop, chrTick reads chr->model (NULL) -> AV.
 *
 * The canonical full-free pattern (per chrmgrStop in chrmgrstop.c:21)
 * is:  chrRemove + propDelist + propDisable + propFree.  This removes
 * the prop from activeprops (delist), marks it inactive (disable), and
 * returns it to the freeprops pool (free) so a future propAllocate can
 * reuse it.
 *
 * Side effect this fixes:
 * - The crash at PC offset 0x3ab39f on slot=9 chrnum=-1 model=NULL.
 * - "Bots not cleared on count change" -- they were "cleared" from
 *   our s_Swarm slot table but the props/chrs still ticked.
 * - "Bots spawn inside player" -- new spawns picked fresh slots, but
 *   the OLD chrs lingered visually wherever they last were (often near
 *   the player from the previous seek frame), creating the impression
 *   of "stuck-to-me" overlap and greenish texture clipping.
 * - "Bots invisible after a few count cycles" -- accumulated stale
 *   chrs eventually exhausted the chr/model pools for new spawns.
 * ------------------------------------------------------------------ */
static void despawn_all(void)
{
	s32 freed = 0;
	for (s32 i = 0; i < TESTSCEN_SWARM_MAX_COUNT; i++) {
		struct chrdata *chr = s_Swarm[i].chr;
		if (chr && chr->prop) {
			struct prop *prop = chr->prop;
			/* Release the aibot slot BEFORE chrRemove so we don't
			 * lose the chr->aibot pointer needed to find our pool
			 * index. ammoheld points into MEMPOOL_STAGE which is
			 * freed at level teardown -- not our concern. */
			if (chr->aibot) {
				swarm_free_aibot(chr->aibot);
				chr->aibot = NULL;
			}
			chrRemove(prop, true);
			propDelist(prop);
			propDisable(prop);
			propFree(prop);
			freed++;
		}
		s_Swarm[i].chr = NULL;
		s_Swarm[i].counted_kill = 0;
	}
	if (freed > 0) {
		sysLogPrintf(LOG_NOTE,
			"TESTSCEN.SWARM: despawn_all freed %d chrs (was count=%d)",
			freed, s_SwarmCount);
	}
	s_SwarmCount = 0;
}

/* ------------------------------------------------------------------
 * Spawn N skedars in concentric rings around the player.
 *
 * S593f (2026-05-01): the prior single-ring implementation packed
 * every chr at radius=600 regardless of count. With 256 chrs and a
 * collision diameter of 30 (post-half-scale), the per-chr arc length
 * (600 * 2pi / 256 ~= 14.7) was less than the chr's footprint, and
 * every chr spawned overlapping its neighbours. The collision
 * resolver then could not find a non-overlap push direction, and
 * the bots froze in place -- Mike's "256 wave didn't apply
 * movement at all, just stuck where they were spawned" symptom.
 *
 * Multi-ring layout: each ring carries up to SWARM_PER_RING chrs at
 * a radius that grows in fixed steps. SWARM_PER_RING = 24 leaves
 * generous arc spacing (600 * 2pi / 24 ~= 157 per chr) for the
 * first ring; each subsequent ring is 200 units further out. At 256
 * chrs that gives 11 rings reaching out to ~2600 units, with
 * spacing always larger than the chr footprint. Bots from the
 * outer rings have farther to travel but the AI handles that
 * natively (chrTryStop / chrMoveAlongPath).
 * ------------------------------------------------------------------ */
#define SWARM_RING_RADIUS_BASE   600.0f
#define SWARM_RING_STEP          200.0f
#define SWARM_PER_RING           24

/* ------------------------------------------------------------------
 * Volume spawn (S594h-Unit-B).
 *
 * Picks a uniform-random position within a box centered on the player,
 * then runs the standard wall-correction (spawnPoolCorrectPosition) to
 * push it out of any wall it lands in. The box is sized to roughly
 * match the SPAWNPOOL_RAY_RANGE (2000 units) so most positions stay
 * within reachable arena geometry; at the cap they may be in dead-end
 * rooms or stuck behind doors, which is fine for a benchmark (the bot
 * AI just navigates from there).
 *
 * The volume is centered on the player's CURRENT position, captured
 * once per respawn_volume call. Box dimensions are X +/- 2000 / Y +/- 400 /
 * Z +/- 2000 in game units. Y range is small so spawns stay close to
 * floor level rather than drifting up into ceiling space.
 *
 * Why uniform-in-box rather than a more sophisticated distribution:
 * the kill-respawn loop calls swarm_pick_volume_position once per dead
 * slot; doing 4096 calls with a sophisticated distribution would
 * dominate the per-frame cost. Uniform-random + the existing 18-ray
 * wall correction is fast enough and produces visually plausible
 * scatter.
 * ------------------------------------------------------------------ */
#define SWARM_VOLUME_HALF_X      2000.0f
#define SWARM_VOLUME_HALF_Y       400.0f
#define SWARM_VOLUME_HALF_Z      2000.0f
/* S594h-Unit-C (2026-05-01): one-step volume growth on retry, per
 * Mike's directive ("grow the parent spawn volume a little, maybe 20%,
 * once"). Slots that fail at 1.0x get one second attempt at 1.2x;
 * persistent failures stay empty and the per-frame
 * death_poll_and_respawn path picks them up over subsequent ticks
 * (streaming refill). */
#define SWARM_VOLUME_RETRY_SCALE 1.2f

/* Per-cycle placement diagnostic counters. Reset in respawn_swarm,
 * surfaced via the cycle summary log so each playtest log shows
 * placement-health at a glance: how many fit at base size, how many
 * needed the 1.2x retry, how many fell through (left empty for the
 * streaming fill). */
static s32 s_PlacementOk;
static s32 s_PlacementGrown;
static s32 s_PlacementFailed;

/* Forward decl: respawn_ring is defined below respawn_swarm but
 * respawn_swarm needs to call it. */
static void respawn_ring(s32 count);

/* Pick a uniform-random position within a box around `center`, scaled
 * by `size_scale` (1.0 = base box, 1.2 = first growth, etc.), then
 * run the standard wall-correction. Returns 1 on success, 0 if the
 * candidate landed on a height failure or unfixable wall clip. */
static s32 swarm_pick_volume_position(const struct coord *center,
                                      RoomNum center_room,
                                      f32 chr_radius, f32 chr_height,
                                      f32 size_scale,
                                      struct coord *out_pos,
                                      RoomNum *out_room)
{
	const f32 hx = SWARM_VOLUME_HALF_X * size_scale;
	const f32 hy = SWARM_VOLUME_HALF_Y * size_scale;
	const f32 hz = SWARM_VOLUME_HALF_Z * size_scale;
	const f32 rx = (RANDOMFRAC() * 2.0f - 1.0f) * hx;
	const f32 ry = (RANDOMFRAC() * 2.0f - 1.0f) * hy;
	const f32 rz = (RANDOMFRAC() * 2.0f - 1.0f) * hz;
	struct coord pos = {
		center->x + rx,
		center->y + ry,
		center->z + rz,
	};
	RoomNum corrected_room = center_room;
	if (!spawnPoolCorrectPosition(&pos, &corrected_room,
			chr_radius, chr_height)) {
		return 0;
	}
	*out_pos  = pos;
	*out_room = corrected_room;
	return 1;
}

/* Two-attempt volume pick: base 1.0x, then one growth to 1.2x on
 * failure (per Mike's "grow once" directive). Tracks which path
 * succeeded into the per-cycle counters so the placement-health
 * log shows the breakdown. Returns 1 on success, 0 if both attempts
 * failed (caller should skip / queue / leave empty for streaming). */
static s32 swarm_pick_volume_with_retry(const struct coord *center,
                                        RoomNum center_room,
                                        f32 chr_radius, f32 chr_height,
                                        struct coord *out_pos,
                                        RoomNum *out_room)
{
	if (swarm_pick_volume_position(center, center_room,
			chr_radius, chr_height, 1.0f, out_pos, out_room)) {
		s_PlacementOk++;
		return 1;
	}
	if (swarm_pick_volume_position(center, center_room,
			chr_radius, chr_height,
			SWARM_VOLUME_RETRY_SCALE, out_pos, out_room)) {
		s_PlacementGrown++;
		return 1;
	}
	s_PlacementFailed++;
	return 0;
}

/* Volume spawn: parallel to respawn_ring but distributes positions
 * uniformly through a box rather than along concentric rings. Each
 * candidate is wall-corrected; rejected candidates are skipped (the
 * spawn count for that frame may be lower than asked, exactly as the
 * ring path does). Kill-respawn (item 5) shares the same volume picker
 * via the strategy switch in respawn_slot. */
static void respawn_volume(s32 count)
{
	despawn_all();
	if (!g_Vars.currentplayer || !g_Vars.currentplayer->prop) {
		sysLogPrintf(LOG_WARNING,
			"TESTSCEN.SWARM: respawn_volume -- no player; skipping");
		return;
	}
	const struct coord ppos = g_Vars.currentplayer->prop->pos;
	const RoomNum proom = g_Vars.currentplayer->prop->rooms[0];

	if (count > TESTSCEN_SWARM_MAX_COUNT) count = TESTSCEN_SWARM_MAX_COUNT;
	if (count < 0) count = 0;

	/* Per-cycle placement counters: reset before the loop, surfaced
	 * via the post-respawn diag log (and the post-cycle BENCHMARK
	 * line) so future playtests show how many slots fit at base size,
	 * how many needed the 1.2x growth retry, and how many fell
	 * through to the streaming-fill path (kill-respawn empty-slot
	 * retry). */
	s_PlacementOk     = 0;
	s_PlacementGrown  = 0;
	s_PlacementFailed = 0;

	s32 spawned = 0;
	for (s32 i = 0; i < count; i++) {
		struct coord pos;
		RoomNum corrected_room;
		if (!swarm_pick_volume_with_retry(&ppos, proom,
				30.0f, 180.0f, &pos, &corrected_room)) {
			/* Both attempts failed. Leave the slot empty -- the
			 * per-frame death_poll_and_respawn path will pick it up
			 * and try again next tick (streaming refill). */
			s_Swarm[i].chr           = NULL;
			s_Swarm[i].counted_kill  = 0;
			s_Swarm[i].team_idx      = (u8)((s_TeamMode == SWARM_TEAMS_TWO_TEAMS_PLUS_PLAYER)
				? (i & 1) : 0);
			s_Swarm[i].respawn_delay = 0;
			s_Swarm[i].scale_factor  = 0.5f;
			/* Use player position as the fallback spawn anchor for
			 * the streaming retry; the volume picker re-randomizes
			 * around the player on each retry call regardless. */
			s_Swarm[i].spawn_pos     = ppos;
			s_Swarm[i].spawn_room    = proom;
			continue;
		}
		RoomNum spawn_rooms[2] = { corrected_room, -1 };
		const s32 team_idx = (s_TeamMode == SWARM_TEAMS_TWO_TEAMS_PLUS_PLAYER)
			? (i & 1) : 0;
		f32 chosen_scale = 0.5f;
		struct chrdata *chr = spawn_one_skedar(&pos, spawn_rooms,
			team_idx, &chosen_scale);
		if (chr) {
			s_Swarm[i].chr           = chr;
			s_Swarm[i].counted_kill  = 0;
			s_Swarm[i].team_idx      = (u8)team_idx;
			s_Swarm[i].respawn_delay = 0;
			s_Swarm[i].scale_factor  = chosen_scale;
			s_Swarm[i].spawn_pos     = pos;
			s_Swarm[i].spawn_room    = corrected_room;
			spawned++;
		}
	}
	/* s_SwarmCount = TARGET, not actual. Empty slots get filled by the
	 * streaming-fill path; the cycler must not see "actual" because
	 * the explicit s_LadderIdx is what advances the ladder. The HUD
	 * derives in-play from a slot scan via swarmTestGetInPlayCount,
	 * so it shows the streaming progression naturally. */
	s_SwarmCount = count;
	testScenarioSetCurrentSwarmCount(count);
	s_SwarmRespawnsThisCycle = 0;
	sysLogPrintf(LOG_NOTE,
		"TESTSCEN.SWARM: respawn_volume target=%d spawned=%d ok=%d grown=%d failed=%d at player (%.0f,%.0f,%.0f)",
		count, spawned, s_PlacementOk, s_PlacementGrown,
		s_PlacementFailed, ppos.x, ppos.y, ppos.z);
}

/* Strategy dispatcher. Used by swarmTestTick session-start and by
 * cycler_tick. Single entry point for "respawn the swarm at the
 * current target count". */
static void respawn_swarm(s32 count)
{
	if (s_SpawnStrategy == SWARM_SPAWN_VOLUME) {
		respawn_volume(count);
	} else {
		respawn_ring(count);
	}
}

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

	/* S594h-Unit-C: per-cycle placement counters. Ring spawn doesn't
	 * have a "growth" path (positions are pre-computed), so grown stays 0. */
	s_PlacementOk     = 0;
	s_PlacementGrown  = 0;
	s_PlacementFailed = 0;

	s32 spawned = 0;
	for (s32 i = 0; i < count; i++) {
		const s32 ring     = i / SWARM_PER_RING;
		const s32 in_ring  = i % SWARM_PER_RING;
		/* Distribute the LAST ring's chrs evenly when the ring isn't
		 * full; otherwise lock to SWARM_PER_RING positions. */
		s32 ring_size = SWARM_PER_RING;
		const s32 last_ring = (count - 1) / SWARM_PER_RING;
		if (ring == last_ring) {
			const s32 rem = count - ring * SWARM_PER_RING;
			if (rem > 0) ring_size = rem;
		}
		const f32 r   = SWARM_RING_RADIUS_BASE + (f32)ring * SWARM_RING_STEP;
		const f32 ang = (2.0f * 3.14159265f) * ((f32)in_ring / (f32)ring_size);
		struct coord pos = {
			ppos.x + r * cosf(ang),
			ppos.y,
			ppos.z + r * sinf(ang),
		};

		const s32 team_idx = (s_TeamMode == SWARM_TEAMS_TWO_TEAMS_PLUS_PLAYER)
			? (i & 1) : 0;

		/* S594h-A (2026-05-01): active wall-correction. Push the ring
		 * position out of any wall it lands in (along the wall's normal
		 * by chr_radius), wiggle if a perpendicular wall blocks the
		 * push, and skip this position entirely if the chr is too tall
		 * for the spot (height failure). */
		RoomNum corrected_room = prooms[0];
		if (!spawnPoolCorrectPosition(&pos, &corrected_room, 30.0f, 180.0f)) {
			/* S594h-Unit-C: leave the slot empty for the streaming-fill
			 * path. death_poll_and_respawn picks up empty slots over
			 * subsequent ticks. Store the INTENDED ring position so
			 * respawn_slot retries the same spot (geometry may have
			 * shifted by then) -- if it never validates, the slot
			 * stays empty (acceptable per Mike's "ok to skip one bot
			 * spawn for a brief time" directive). */
			s_Swarm[i].chr           = NULL;
			s_Swarm[i].counted_kill  = 0;
			s_Swarm[i].team_idx      = (u8)team_idx;
			s_Swarm[i].respawn_delay = 0;
			s_Swarm[i].scale_factor  = 0.5f;
			s_Swarm[i].spawn_pos     = pos; /* pre-correction; retry will re-correct */
			s_Swarm[i].spawn_room    = prooms[0];
			s_PlacementFailed++;
			continue;
		}
		RoomNum spawn_rooms[2] = { corrected_room, -1 };
		f32 chosen_scale = 0.5f;
		struct chrdata *chr = spawn_one_skedar(&pos, spawn_rooms,
			team_idx, &chosen_scale);
		if (chr) {
			s_Swarm[i].chr           = chr;
			s_Swarm[i].counted_kill  = 0;
			s_Swarm[i].team_idx      = (u8)team_idx;
			s_Swarm[i].respawn_delay = 0;
			s_Swarm[i].scale_factor  = chosen_scale;
			s_Swarm[i].spawn_pos     = pos;
			s_Swarm[i].spawn_room    = corrected_room;
			s_PlacementOk++;
			spawned++;
		} else {
			/* Spawn allocator returned NULL (chr/model pool full). Same
			 * empty-slot fallback as above. */
			s_Swarm[i].chr           = NULL;
			s_Swarm[i].counted_kill  = 0;
			s_Swarm[i].team_idx      = (u8)team_idx;
			s_Swarm[i].respawn_delay = 0;
			s_Swarm[i].scale_factor  = chosen_scale;
			s_Swarm[i].spawn_pos     = pos;
			s_Swarm[i].spawn_room    = corrected_room;
			s_PlacementFailed++;
		}
	}
	/* TARGET, not actual. See respawn_volume comment for rationale. */
	s_SwarmCount = count;
	testScenarioSetCurrentSwarmCount(count);
	s_SwarmRespawnsThisCycle = 0;
	sysLogPrintf(LOG_NOTE,
		"TESTSCEN.SWARM: respawn_ring target=%d spawned=%d ok=%d failed=%d at player (%.0f,%.0f,%.0f)",
		count, spawned, s_PlacementOk, s_PlacementFailed,
		ppos.x, ppos.y, ppos.z);
}

/* ------------------------------------------------------------------
 * GPU-mode position fallback
 *
 * Used ONLY when the active scenario is SWARM_METHOD_GPU but the GPU
 * compute path is unavailable (no GL >= 4.3, compute shader compile
 * failed, etc.). We need SOME source of motion in that case so the
 * benchmark still produces visible activity, otherwise GPU mode shows
 * a static ring of skedars and Mike has nothing to compare CPU mode
 * against.
 *
 * The CPU-mode bots run real AI and don't need this -- their motion
 * comes from chraTick / chraiExecute via the AIBOT_INIT ailist.
 * ------------------------------------------------------------------ */
/* 1.5x normal seek-toward-player speed for the GPU-fallback path.
 * Matches the BOTDIFF_PERFECT speed bump for CPU-mode bots
 * (botCalculateMaxSpeed multiplies by 11.2 for PERFECT vs 7.6 for
 * NORMAL; ratio 1.47 ~= 1.5x). 27 = 18 * 1.5. */
#define SWARM_MAX_SPEED      27.0f

static void gpu_fallback_seek_tick(struct coord *player_pos)
{
	for (s32 i = 0; i < s_SwarmCount; i++) {
		struct chrdata *chr = s_Swarm[i].chr;
		if (!chr || !chr->prop || chr->chrnum < 0 || chr->model == NULL
				|| chr->actiontype == ACT_DEAD
				|| chr->actiontype == ACT_DIE) {
			continue;
		}
		struct prop *prop = chr->prop;
		f32 dx = player_pos->x - prop->pos.x;
		f32 dz = player_pos->z - prop->pos.z;
		f32 d2 = dx * dx + dz * dz;
		if (d2 < 1.0f) continue;
		f32 d = sqrtf(d2);
		f32 step = SWARM_MAX_SPEED;
		if (step > d) step = d;
		f32 vx = (dx / d) * step;
		f32 vz = (dz / d) * step;

		struct coord newpos;
		newpos.x = prop->pos.x + vx;
		newpos.y = prop->pos.y;
		newpos.z = prop->pos.z + vz;

		f32 face_deg = atan2f(dx, dz) * (180.0f / 3.14159265f);
		if (face_deg < 0.0f) face_deg += 360.0f;

		RoomNum rooms[8];
		for (s32 r = 0; r < 8; r++) {
			rooms[r] = prop->rooms[r];
		}
		chrSetPos(chr, &newpos, rooms, face_deg, true);
	}
}

/* ------------------------------------------------------------------
 * Despawn a single swarm slot (mirrors despawn_all's per-slot work).
 * Used by the kill-respawn path (item 5) so we can free a single
 * dead chr without disturbing the rest of the swarm.
 * ------------------------------------------------------------------ */
static void despawn_slot(s32 i)
{
	struct chrdata *chr = s_Swarm[i].chr;
	if (chr && chr->prop) {
		struct prop *prop = chr->prop;
		if (chr->aibot) {
			swarm_free_aibot(chr->aibot);
			chr->aibot = NULL;
		}
		chrRemove(prop, true);
		propDelist(prop);
		propDisable(prop);
		propFree(prop);
	}
	s_Swarm[i].chr = NULL;
}

/* ------------------------------------------------------------------
 * Spawn a replacement Skedar for a slot whose previous chr was killed.
 * Strategy-aware (S594h-Unit-B):
 *   RING:   reuses the slot's stored spawn_pos / spawn_room so the
 *           swarm population stays anchored at its original ring
 *           positions (static formation, replenishing in place).
 *   VOLUME: picks a fresh random box position via swarm_pick_volume_position,
 *           giving a "horde keeps coming" feel rather than a static
 *           formation -- new bots appear from anywhere in the volume.
 * Returns 1 on success, 0 if the spawn failed (chr/model pool full or
 * picker rejected every candidate).
 * ------------------------------------------------------------------ */
static s32 respawn_slot(s32 i)
{
	struct coord pos;
	RoomNum corrected_room;

	if (s_SpawnStrategy == SWARM_SPAWN_VOLUME) {
		if (!g_Vars.currentplayer || !g_Vars.currentplayer->prop) return 0;
		const struct coord ppos = g_Vars.currentplayer->prop->pos;
		const RoomNum proom = g_Vars.currentplayer->prop->rooms[0];
		/* S594h-Unit-C: two-attempt pick (base then 1.2x grown), then
		 * give up for this tick. Counters track placement health. */
		if (!swarm_pick_volume_with_retry(&ppos, proom,
				30.0f, 180.0f, &pos, &corrected_room)) {
			return 0;
		}
	} else {
		pos = s_Swarm[i].spawn_pos;
		corrected_room = s_Swarm[i].spawn_room;
		/* Re-validate the position. If the world changed (door closed,
		 * geometry shifted) the original spot might now be inside a wall.
		 * Conservative chr_radius/height -- spawn_one_skedar picks the
		 * actual scale, so this gates with a slight over-estimate. */
		if (!spawnPoolCorrectPosition(&pos, &corrected_room, 30.0f, 180.0f)) {
			s_PlacementFailed++;
			return 0;
		}
		s_PlacementOk++;
	}
	RoomNum spawn_rooms[2] = { corrected_room, -1 };
	const s32 team_idx = (s32)s_Swarm[i].team_idx;
	f32 chosen_scale = 0.5f;
	struct chrdata *chr = spawn_one_skedar(&pos, spawn_rooms,
		team_idx, &chosen_scale);
	if (!chr) return 0;
	s_Swarm[i].chr           = chr;
	s_Swarm[i].counted_kill  = 0;
	s_Swarm[i].respawn_delay = 0;
	s_Swarm[i].scale_factor  = chosen_scale;
	s_Swarm[i].spawn_pos     = pos;
	s_Swarm[i].spawn_room    = corrected_room;
	s_SwarmRespawnsThisCycle++;
	return 1;
}

/* ------------------------------------------------------------------
 * Death poll + kill-respawn + streaming refill (S594h-Unit-A item 5
 * + S594h-Unit-C streaming flow).
 *
 * Three concerns per slot:
 *   1. Empty slot (chr == NULL): the last respawn attempt failed. Try
 *      again, but only up to a per-frame budget so we don't burn raycast
 *      budget on placement at high counts. The cycler's initial-spawn
 *      loop deliberately leaves placement failures empty for this path
 *      to fill over the next several ticks ("flow rather than all at
 *      once" per Mike's S594h-Unit-C directive).
 *   2. Dying chr (counted_kill = 0, actiontype = ACT_DEAD/ACT_DIE):
 *      credit the kill, set respawn_delay = 60 (~1s anim time).
 *   3. Awaiting respawn (counted_kill = 1, respawn_delay > 0):
 *      decrement; at 1, despawn-free + respawn-fresh.
 *
 * Per-frame retry budget: cap empty-slot respawn attempts at
 * SWARM_REFILL_PER_FRAME. At target=4096 with all slots empty, the
 * initial filler does 16/frame ~= 4 sec to fill -- visually feels
 * like a streaming flow rather than an instant pop, and keeps
 * spawnPoolCorrectPosition's ~18 raycasts/call to a sane budget.
 * Kill-respawns run unconditionally (not budget-gated) since they
 * occur sparsely once the population is steady-state.
 * ------------------------------------------------------------------ */
#define SWARM_REFILL_PER_FRAME 16

static void death_poll_and_respawn(void)
{
	s32 refill_budget = SWARM_REFILL_PER_FRAME;
	for (s32 i = 0; i < s_SwarmCount; i++) {
		struct chrdata *chr = s_Swarm[i].chr;
		if (!chr) {
			/* Streaming refill: spend budget on empty slots. When the
			 * budget runs out for this frame, leave the rest for
			 * subsequent ticks -- they wait silently in the empty
			 * state. */
			if (refill_budget > 0) {
				if (respawn_slot(i)) refill_budget--;
				else                 refill_budget--;
			}
			continue;
		}

		/* Phase 1: detect newly-dead chr. */
		if (!s_Swarm[i].counted_kill) {
			if (chr->actiontype == ACT_DEAD || chr->actiontype == ACT_DIE
					|| !chr->prop) {
				s_Swarm[i].counted_kill = 1;
				s_SwarmKills++;
				s_Swarm[i].respawn_delay = 60;
			}
			continue;
		}

		/* Phase 2: counted_kill == 1, waiting to respawn. */
		if (s_Swarm[i].respawn_delay > 1) {
			s_Swarm[i].respawn_delay--;
			continue;
		}
		if (s_Swarm[i].respawn_delay == 1) {
			s_Swarm[i].respawn_delay = 0;
			despawn_slot(i);
			respawn_slot(i);
		}
	}
}

/* ------------------------------------------------------------------
 * Bidirectional cycler (S594h-Unit-A items 1 + 2).
 *
 * NEXT (PgUp / DPAD_DOWN / KEY_0): advance to the next-higher count.
 *   Wraps from the top of the ladder (4096) back to the bottom (4).
 * PREV (PgDn / DPAD_UP):          retreat to the next-lower count.
 *   Wraps from the bottom (4) back to the top (4096).
 *
 * Wrap-bug diagnostic (per Mike's bug report on the prior single-
 * direction cycler): the post-cycle log surfaces target / actual /
 * alive counts so any mismatch (e.g. despawn budget short, spawn
 * loop bailout, off-by-one) shows up in the log immediately rather
 * than as a visual "label says 4 but I see 256". Mismatch interpretation:
 *   target != actual: respawn_ring spawned fewer than asked (pool
 *     exhaustion / spawnPoolCorrectPosition rejection both increment
 *     a "skipped" counter on the next tick).
 *   actual != alive:  some chrs already counted dead at spawn time
 *     (shouldn't happen this frame; would mean despawn ran late).
 * ------------------------------------------------------------------ */
static void cycler_tick(void)
{
	const s32 next_pressed = actionPressed(0, ACTION_TESTSCEN_CYCLE_COUNT);
	const s32 prev_pressed = actionPressed(0, ACTION_TESTSCEN_CYCLE_PREV);
	if (!next_pressed && !prev_pressed) return;

	const s32 dir = next_pressed ? +1 : -1;
	const s32 N = SWARM_TEST_CYCLE_STEPS;
	/* S594h-Unit-C (2026-05-01): advance the EXPLICIT ladder index, not
	 * the count-derived one. See s_LadderIdx docblock for the bug this
	 * fixes (placement failures masking the index lookup). */
	s_LadderIdx = ((s_LadderIdx + dir) % N + N) % N;
	const s32 idx  = s_LadderIdx;
	const s32 next = SWARM_TEST_CYCLE[idx];

	sysLogPrintf(LOG_NOTE,
		"TESTSCEN.SWARM: cycle %s prev_idx=%d -> idx=%d target=%d (alive_was=%d)",
		(dir > 0) ? "NEXT" : "PREV",
		((idx - dir) % N + N) % N, idx, next, s_SwarmCount);

	respawn_swarm(next);

	/* Post-respawn diagnostic: target / actual / alive. If target != actual
	 * the respawn loop dropped some slots (height failure, pool exhausted,
	 * spawnPoolCorrectPosition rejection); if actual != alive the death-
	 * poll already credited some kills before the next frame ticks. Either
	 * mismatch surfaces here so wrap-style bugs cannot hide. */
	const s32 alive = s_SwarmCount - s_SwarmKills;
	sysLogPrintf(LOG_NOTE,
		"TESTSCEN.SWARM: post-cycle target=%d actual=%d alive=%d kills=%d",
		next, s_SwarmCount, alive, s_SwarmKills);

	/* End-of-cycle summary log for the just-finished count. The label
	 * stays "CPU" / "GPU" so the smoke-test regex BENCHMARK\\.SWARM\\.GPU
	 * keeps matching across GPU_POS_ONLY and GPU_FULL. The AI sub-method
	 * is logged separately via TESTSCEN.SWARM lines and the
	 * BENCHMARK.SWARM.GPU.AI summary in swarm_gpu.cpp. */
	sysLogPrintf(LOG_NOTE,
		"BENCHMARK.SWARM.%s: SUMMARY count=%d kills=%d frame_avg_ms=%.3f respawns=%d",
		(testScenarioActiveMethod() != SWARM_METHOD_CPU) ? "GPU" : "CPU",
		s_SwarmCount, s_SwarmKills,
		(s_FrameMsSamples > 0)
			? (s_FrameMsAcc / (f32)s_FrameMsSamples) : 0.0f,
		s_SwarmRespawnsThisCycle);
	s_FrameMsAcc = 0.0f;
	s_FrameMsSamples = 0;
	s_SwarmKills = 0;
	s_SwarmRespawnsThisCycle = 0;
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

		/* S594h-Unit-A.5: snapshot + apply MPOPTION_TEAMSENABLED. The
		 * bit drives team-colour overlays at activemenu.c:612 and the
		 * team-aware AI checks in bot.c. Set it for 2-team mode (so
		 * Mike sees the visual distinction); clear it for sims-vs-
		 * players (so the radar/HUD treats the swarm as a single
		 * hostile team). Snapshot is restored in swarmTestOnSessionEnd. */
		s_PriorTeamsEnabledSnapshot =
			(g_MpSetup.options & MPOPTION_TEAMSENABLED) ? 1 : 0;
		if (s_TeamMode == SWARM_TEAMS_TWO_TEAMS_PLUS_PLAYER) {
			g_MpSetup.options |= MPOPTION_TEAMSENABLED;
		} else {
			g_MpSetup.options &= ~MPOPTION_TEAMSENABLED;
		}

		/* Reset ladder position to the initial-count entry (idx 0 = 4). */
		s_LadderIdx = 0;
		respawn_swarm(TESTSCEN_SWARM_INITIAL_COUNT);
		s_SwarmInitialized = 1;
		{
		const swarm_method_t armed_method = testScenarioActiveMethod();
		const char *method_label = "CPU";
		if (armed_method == SWARM_METHOD_GPU_POS_ONLY) method_label = "GPU_POS_ONLY";
		else if (armed_method == SWARM_METHOD_GPU_FULL) method_label = "GPU_FULL";
		sysLogPrintf(LOG_NOTE,
			"TESTSCEN.SWARM: session armed -- method=%s count=%d team=%s vis=%s spawn=%s",
			method_label,
			s_SwarmCount,
			(s_TeamMode == SWARM_TEAMS_TWO_TEAMS_PLUS_PLAYER)
				? "2teams+player" : "sims_vs_players",
			(s_VisMode == SWARM_VIS_INVISIBLE) ? "invisible"
				: (s_VisMode == SWARM_VIS_NORMAL) ? "normal" : "always_see",
			(s_SpawnStrategy == SWARM_SPAWN_VOLUME) ? "volume" : "ring");

		/* S594h-Unit-C (2026-05-01): dump the ladder array at session
		 * start so playtest logs let us verify the actual binary's
		 * ladder content. Mike's last playtest hypothesis was that
		 * the ladder might have been a different length than claimed;
		 * the dump answers that question without inspecting the .exe. */
		{
			char buf[256];
			int off = 0;
			off += snprintf(buf + off, sizeof(buf) - off,
				"TESTSCEN.SWARM.LADDER: STEPS=%d entries=[",
				SWARM_TEST_CYCLE_STEPS);
			for (s32 li = 0; li < SWARM_TEST_CYCLE_STEPS
					&& off < (s32)sizeof(buf) - 8; li++) {
				off += snprintf(buf + off, sizeof(buf) - off,
					"%s%d", (li > 0) ? "," : "",
					(s32)SWARM_TEST_CYCLE[li]);
			}
			snprintf(buf + off, sizeof(buf) - off, "]");
			sysLogPrintf(LOG_NOTE, "%s", buf);
		}
		}  /* close armed_method scope (B-308 c3807 method_label block) */
	}

	/* Re-apply the full player setup every tick. cheatsReset, playerSpawn,
	 * and inventory pickups can each clobber pieces of it. See
	 * apply_player_setup() docblock for the full rationale. */
	apply_player_setup();

	/* S594h-Unit-A item 7: visibility-mode toggle. Cycles
	 * NORMAL -> ALWAYS_SEE -> INVISIBLE on each press of
	 * ACTION_TESTSCEN_VIS_TOGGLE (default I key). The mode is consumed
	 * by chraction.c::chrHasLosToChr (3-way branch on the swarm-bot
	 * marker) and by the per-frame target re-assertion below. */
	if (actionPressed(0, ACTION_TESTSCEN_VIS_TOGGLE)) {
		s_VisMode = (swarm_vis_mode_t)(((s32)s_VisMode + 1) % SWARM_VIS_COUNT);
		const char *label = "?";
		switch (s_VisMode) {
		case SWARM_VIS_NORMAL:     label = "NORMAL";     break;
		case SWARM_VIS_ALWAYS_SEE: label = "ALWAYS_SEE"; break;
		case SWARM_VIS_INVISIBLE:  label = "INVISIBLE";  break;
		default: break;
		}
		sysLogPrintf(LOG_NOTE,
			"TESTSCEN.SWARM: visibility mode -> %s", label);
	}

	/* B-308 first slice (c3807, 2026-05-15): GPU sub-method toggle.
	 * KEY_O flips the active GPU swarm scenario between GPU_POS_ONLY
	 * (position-only seek, the legacy S593d behaviour) and GPU_FULL
	 * (compute kernel writes AI decisions; CPU readback consumer logs
	 * them so the round trip is observable). No-op in CPU swarm.
	 * testScenarioCycleGpuSubmode handles the scenario gating itself
	 * and logs the new mode. No respawn needed -- both sub-methods
	 * use the same passive chr layout. */
	if (actionPressed(0, ACTION_TESTSCEN_GPU_FULL_TOGGLE)) {
		testScenarioCycleGpuSubmode();
	}

	/* S593h (2026-05-01): force CPU-mode swarm bots to be aware of the
	 * player every frame. Mike's playtest report:
	 *   "They should also be aware of my location, and be set to perfect
	 *    or dark agent mode."
	 * Without this, the bot AI's targeting check at bot.c:2306 was
	 * dropping target=player when chrHasLosToChr returned false (player
	 * occluded by walls / bot peers / arena geometry). For a 256-bot
	 * benchmark we want unconditional aggression on the player; the LOS
	 * short-circuit in chraction.c::chrHasLosToChr (gated on the same
	 * 0x00040000 swarm marker) makes any swarm bot's LOS check return
	 * true. We additionally re-set chr->target and aibot->attackingplayernum
	 * each frame as defence in depth -- if any code path drops the
	 * target, the next frame restores it.
	 *
	 * Player is always at g_MpAllChrPtrs[0] in normmplayerisrunning
	 * matches with one local human (PLAYERCOUNT()==1). The player's
	 * prop index is `g_Vars.currentplayer->prop - g_Vars.props`.
	 *
	 * S594h-Unit-A item 7: the per-frame re-assertion is gated on
	 * s_VisMode != SWARM_VIS_INVISIBLE. When invisible, swarm bots must
	 * actually drop the player as their target so they wander instead
	 * of homing in -- otherwise the LOS short-circuit becomes the only
	 * gate and we'd need a second invisibility check there too.
	 *
	 * Applies only to CPU mode -- GPU bots have no aibot and run no AI
	 * targeting, so this is a no-op for them. The directive that
	 * "all changes apply to both modes" is satisfied for items 1, 5
	 * (chr-level) at spawn. The AI-side refinements (this one + the
	 * BOTDIFF_DARK / BOTTYPE_SPEED config) require the GPU bot pipeline
	 * filed at context/designs/in-flight/gpu-swarm-bot-pipeline.md. */
	if (testScenarioActiveMethod() == SWARM_METHOD_CPU
			&& g_Vars.currentplayer && g_Vars.currentplayer->prop
			&& s_VisMode != SWARM_VIS_INVISIBLE) {
		const s32 player_propnum = (s32)(g_Vars.currentplayer->prop - g_Vars.props);
		const s32 lvframe = g_Vars.lvframe60;
		for (s32 i = 0; i < s_SwarmCount; i++) {
			struct chrdata *chr = s_Swarm[i].chr;
			if (!chr || !chr->aibot || chr->chrnum < 0) continue;
			chr->target = player_propnum;
			chr->aibot->attackingplayernum = 0;
			chr->aibot->command = AIBOTCMD_ATTACK;
			chr->aibot->attackpropnum = player_propnum;
			chr->aibot->targetinsight = 1;
			chr->aibot->targetlastseen60 = lvframe;
			chr->aibot->lastseenanytarget60 = lvframe;
			if (player_propnum >= 0) {
				/* aibot->chrsinsight[] is sized to MAX_MPCHRS; index 0
				 * is the local human player. Force "in sight" so the
				 * cache-update pass at bot.c:2244 can't undo us. */
				chr->aibot->chrsinsight[0] = 1;
				chr->aibot->chrslastseen60[0] = lvframe;
			}
			/* Re-assert the perim-disabled lock. chrCalculatePushPos
			 * toggles the bit during its own push test; the chr.c gate
			 * on 0x00040000 prevents the re-enable, but if some other
			 * caller cleared the marker we restore it here too. */
			chr->hidden |= 0x00040000;
			chr->hidden |= CHRHFLAG_PERIMDISABLED;
		}
	} else if (testScenarioActiveMethod() == SWARM_METHOD_CPU
			&& s_VisMode == SWARM_VIS_INVISIBLE) {
		/* INVISIBLE mode: drop the player target so swarm bots stop
		 * pursuing. They keep their AIBOTCMD_ATTACK config but with
		 * attackpropnum=-1 the AI's target-walk falls through to its
		 * idle/wander path. Still re-assert perim-disabled for the
		 * stress-test reasons (bot-bot phasing). */
		for (s32 i = 0; i < s_SwarmCount; i++) {
			struct chrdata *chr = s_Swarm[i].chr;
			if (!chr || !chr->aibot || chr->chrnum < 0) continue;
			chr->target = -1;
			chr->aibot->attackingplayernum = -1;
			chr->aibot->attackpropnum = -1;
			chr->aibot->targetinsight = 0;
			chr->aibot->chrsinsight[0] = 0;
			chr->hidden |= 0x00040000;
			chr->hidden |= CHRHFLAG_PERIMDISABLED;
		}
	}

	/* Drive per-method simulation.
	 *
	 * CPU mode -- bots run their own AI through chraTick (driven by
	 * GAILIST_AIBOT_INIT + chr->aibot), so we don't drive positions
	 * from here at all. Movement, attack, and collision are handled
	 * by the bot AI naturally.
	 *
	 * GPU mode -- positions come from the compute shader if it's
	 * available, otherwise we fall back to a simple seek so Mike
	 * still gets motion to look at while the GPU pipeline is brought
	 * online. The GPU path's "real bot behaviour" is a follow-up
	 * pillar (see context/designs/in-flight/gpu-swarm-bot-pipeline.md). */
	struct coord player_pos = {0, 0, 0};
	s32 player_propnum = -1;
	if (g_Vars.currentplayer && g_Vars.currentplayer->prop) {
		player_pos     = g_Vars.currentplayer->prop->pos;
		player_propnum = (s32)(g_Vars.currentplayer->prop - g_Vars.props);
	}
	/* B-308 first slice (c3807, 2026-05-15): both GPU_POS_ONLY and
	 * GPU_FULL run the same SSBO-driven seek path. The method argument
	 * just gates the AI compute step inside the shader (do_ai uniform)
	 * and the readback consumer's logging. From the CPU's POV the
	 * data-flow shape is identical. */
	const swarm_method_t method_now = testScenarioActiveMethod();
	if (method_now == SWARM_METHOD_GPU_POS_ONLY
			|| method_now == SWARM_METHOD_GPU_FULL) {
		if (swarmGpuAvailable()) {
			struct chrdata *chrs[TESTSCEN_SWARM_MAX_COUNT];
			for (s32 i = 0; i < TESTSCEN_SWARM_MAX_COUNT; i++) {
				chrs[i] = s_Swarm[i].chr;
			}
			swarmGpuStepAndApply(&player_pos, chrs, s_SwarmCount,
				(s32)method_now, player_propnum);
		} else {
			gpu_fallback_seek_tick(&player_pos);
		}
	}
	/* CPU mode: AI handles everything; nothing to do here. */

	death_poll_and_respawn();
	cycler_tick();

	/* Throttle per-frame log to ~1 line per 60 frames. */
	s_SwarmLogTickAcc++;
	if (s_SwarmLogTickAcc >= 60) {
		s_SwarmLogTickAcc = 0;
		const s32 in_play = swarmTestGetInPlayCount();
		const s32 pending = swarmTestGetPendingRespawnCount();
		const s32 empty   = s_SwarmCount - in_play - pending;
		sysLogPrintf(LOG_NOTE,
			"BENCHMARK.SWARM.%s: target=%d in_play=%d dying=%d empty=%d kills=%d",
			(testScenarioActiveMethod() != SWARM_METHOD_CPU) ? "GPU" : "CPU",
			s_SwarmCount, in_play, pending, empty, s_SwarmKills);

		/* S594h-Unit-C diag: surface the first swarm bot's targeting
		 * state so Mike's "bots not targeting me" report can be
		 * diagnosed from the log alone. We pick the first slot with a
		 * live aibot. The fields surfaced are exactly what bot.c reads
		 * to decide whether to engage the player. */
		if (testScenarioActiveMethod() == SWARM_METHOD_CPU
				&& g_Vars.currentplayer && g_Vars.currentplayer->prop) {
			const s32 player_propnum =
				(s32)(g_Vars.currentplayer->prop - g_Vars.props);
			s32 player_team = -1;
			if (g_Vars.currentplayer->prop->chr) {
				player_team = (s32)g_Vars.currentplayer->prop->chr->team;
			}
			struct chrdata *probe = NULL;
			s32 probe_idx = -1;
			for (s32 si = 0; si < s_SwarmCount; si++) {
				if (s_Swarm[si].chr && s_Swarm[si].chr->aibot
						&& s_Swarm[si].chr->chrnum >= 0) {
					probe = s_Swarm[si].chr;
					probe_idx = si;
					break;
				}
			}
			const s32 teams_enabled =
				(g_MpSetup.options & MPOPTION_TEAMSENABLED) ? 1 : 0;
			if (probe && probe->aibot) {
				sysLogPrintf(LOG_NOTE,
					"TESTSCEN.SWARM.PROBE: slot=%d chrnum=%d team=0x%02x "
					"target=%d cmd=%d attackpropnum=%d targetinsight=%d "
					"chrsinsight[0]=%d player_propnum=%d player_team=0x%02x "
					"vis=%d teams_en=%d",
					probe_idx, (s32)probe->chrnum, (u32)probe->team,
					(s32)probe->target, (s32)probe->aibot->command,
					(s32)probe->aibot->attackpropnum,
					(s32)probe->aibot->targetinsight,
					(s32)probe->aibot->chrsinsight[0],
					player_propnum, (u32)player_team,
					(s32)s_VisMode, teams_enabled);
			} else {
				sysLogPrintf(LOG_NOTE,
					"TESTSCEN.SWARM.PROBE: no live swarm chr with aibot (count=%d)",
					s_SwarmCount);
			}
		}
	}
}

/* ------------------------------------------------------------------
 * Cleanup
 * ------------------------------------------------------------------ */
void swarmTestOnSessionEnd(void)
{
	despawn_all();
	/* Defensive: zero the in-use bitmap so a fresh session start (after
	 * a stage transition that didn't go through despawn_all) cannot
	 * inherit an old leak of "in use" markers. The aibot structs
	 * themselves live in BSS so memset is a no-op here -- the bitmap
	 * is the only state we need to clear. */
	memset(s_SwarmAibotInUse, 0, sizeof(s_SwarmAibotInUse));

	/* S594h-Unit-A.5: restore MPOPTION_TEAMSENABLED to its pre-session
	 * value so the user's normal MP team config isn't perturbed. */
	if (s_PriorTeamsEnabledSnapshot != 0xff) {
		if (s_PriorTeamsEnabledSnapshot) {
			g_MpSetup.options |= MPOPTION_TEAMSENABLED;
		} else {
			g_MpSetup.options &= ~MPOPTION_TEAMSENABLED;
		}
		s_PriorTeamsEnabledSnapshot = 0xff;
	}

	s_SwarmKills             = 0;
	s_SwarmRespawnsThisCycle = 0;
	s_SwarmInitialized       = 0;
	s_SwarmLastStage         = -1;
	s_SwarmLogTickAcc        = 0;
	s_PlayerLoadoutGiven     = 0; /* re-give power weapons on next session */
	s_FrameMsAcc             = 0.0f;
	s_FrameMsSamples         = 0;
	s_LastFrameTick          = 0;
	testScenarioReset();
}

/* ------------------------------------------------------------------
 * Read accessors (HUD + tests)
 * ------------------------------------------------------------------ */
s32 swarmTestGetActiveCount(void) { return s_SwarmCount; }
s32 swarmTestGetKillCount(void)   { return s_SwarmKills; }

s32 swarmTestGetInPlayCount(void)
{
	s32 n = 0;
	for (s32 i = 0; i < s_SwarmCount; i++) {
		if (s_Swarm[i].chr != NULL && !s_Swarm[i].counted_kill) n++;
	}
	return n;
}

s32 swarmTestGetPendingRespawnCount(void)
{
	s32 n = 0;
	for (s32 i = 0; i < s_SwarmCount; i++) {
		if (s_Swarm[i].counted_kill && s_Swarm[i].respawn_delay > 0) n++;
	}
	return n;
}

s32 swarmTestGetRespawnsThisCycle(void) { return s_SwarmRespawnsThisCycle; }

/* swarmTestRenderHud is provided by the C++ side (port/fast3d/pdgui_backend.cpp);
 * the actual ImGui rendering is inlined into the per-frame ImGui pass next to
 * the other in-game banners. This C-side body is a no-op so any direct caller
 * still links cleanly. */
void swarmTestRenderHud(void) { /* C++-side inline render */ }
