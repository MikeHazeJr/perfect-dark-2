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
#include "game/chr.h"
#include "game/chraction.h"
#include "game/body.h"
#include "game/prop.h"
#include "game/inv.h"
#include "game/cheats.h"
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
 * yet built, the stub below is used and swarmGpuAvailable() returns 0. */
extern s32  swarmGpuAvailable(void);
extern void swarmGpuStepAndApply(struct coord *player_pos,
                                 struct chrdata **chrs, s32 count);

/* ------------------------------------------------------------------
 * Cycle ladder
 * 4 -> 8 -> 16 -> 32 -> 48 -> 64 -> 128 -> 256 -> 4 (wrap).
 * 48 was added 2026-04-30 per Mike's directive: it's the "one-third of 128"
 * step that lets us see where the curve bends before doubling further.
 * ------------------------------------------------------------------ */
const s32 SWARM_TEST_CYCLE[SWARM_TEST_CYCLE_STEPS] = {
	4, 8, 16, 32, 48, 64, 128, 256
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
	s_SwarmBotConfig.type       = BOTTYPE_KAZE;
	s_SwarmBotConfig.difficulty = BOTDIFF_PERFECT;
	s_SwarmBotConfigInited = 1;
}

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

	/* Weapon: spawn unarmed; the AI's weapon-pick logic + invSetAllGuns
	 * machinery on the bot side will choose. WEAPON_FALCON2 is a safe
	 * default fallback if any path reads weaponnum before the AI runs. */
	aibot->weaponnum = WEAPON_FALCON2;
	aibot->ismeleeweapon = false;
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
 * Three pieces of per-player state must be active for the benchmark:
 *   1. Invincibility -- bots are aggressive, the player can't die.
 *   2. All guns       -- weapon-cycle the full arsenal under load.
 *   3. Bottomless ammo -- never reload, never run dry.
 *
 * Both the cheat banks AND the per-player flags are set, because
 * different code paths consult different sources:
 *   - chrDamage / playerDamage gate on g_Vars.currentplayer->invincible.
 *   - chrInflictDamage early-outs on cheatIsActive(CHEAT_INVINCIBLE).
 *   - bgun reload gates on cheatIsActive(CHEAT_UNLIMITEDAMMO).
 *   - inv current-weapon picker gates on equipallguns.
 *
 * S483-followup (2026-04-30): apply_player_setup is called from
 * EVERY swarmTestTick frame, not just the session-start tick.  Reasons:
 *   - cheatsReset() runs at level start AFTER our session-start tick
 *     would have run in the prior implementation, wiping the cheat bits.
 *   - playerSpawn / playerReset on death respawn re-initialises
 *     equipallguns and player->invincible.
 *   - Mid-frame inventory pickups / weapon switches can clear
 *     equipallguns transiently.
 * Re-asserting each tick is cheap (a handful of writes + a cached
 * invSetAllGuns no-op when equipallguns is already true) and is the
 * only way to keep the player benchmark-ready against the various
 * resets the engine performs.
 * ------------------------------------------------------------------ */
static void apply_player_setup(void)
{
	if (!g_Vars.currentplayer) return;

	/* Cheat banks first so anything queried via cheatIsActive sees the
	 * right state. */
	g_CheatsActiveBank0 |= (1u << CHEAT_INVINCIBLE);
	g_CheatsActiveBank0 |= (1u << CHEAT_UNLIMITEDAMMO);
	g_CheatsActiveBank0 |= (1u << CHEAT_ALLGUNS);

	/* Per-player invincibility. The g_Vars.currentplayer pointer is
	 * the canonical "Player 0" pointer in single-local-player mode. */
	g_Vars.currentplayer->invincible = 1;

	/* Equip all guns. invSetAllGuns sets equipallguns=true,
	 * recalculates the current-weapon index, and re-equips. The
	 * cheats.c gate around CHEAT_ALLGUNS (PLAYERCOUNT()==1 && !normmp)
	 * is bypassed by calling invSetAllGuns directly: the test mode is
	 * by definition local-single-player, so this is safe. */
	if (!g_Vars.currentplayer->equipallguns) {
		invSetAllGuns(true);
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
static struct chrdata *spawn_one_skedar(struct coord *pos, RoomNum *rooms)
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

	/* Hostile team. Player chr is on TEAM_01 (1 << 0 = 0x01), set in
	 * playerreset.c:647 from g_PlayerConfigsArray[mpindex].base.team.
	 * We put swarm chrs on TEAM_ENEMY (1 << 1 = 0x02) so the bot AI's
	 * `chr->team == other->team` ally check returns false against the
	 * player and the bots aggress. The prior TEAM_NONCOMBAT (0x80)
	 * was a "do not engage" flag and explained Mike's "running
	 * aimlessly" report. */
	chr->team = TEAM_ENEMY;

	/* Half normal MP-bot health (8.0). botmgrAllocateBot defaults to
	 * 8.0 (botmgr.c:153) for MP simulants; halving gives 4.0. The
	 * S593 config used 1.0 ("1 HP per the directive") but Mike's
	 * 2026-05-01 directive supersedes that with "1/2 normal health". */
	chr->maxdamage = 4.0f;
	chr->damage    = 0.0f;

	/* Skedar collision radius -- chrInit defaults to 20, halved by
	 * the 0.5x scale below this gives a 10-unit perim. Set to 30
	 * (player capsule size) before scaling so the half-scale chr ends
	 * up with a 15-unit perim, still meaningful for player + chr-vs-chr
	 * collision while matching the 0.5x visual size. */
	chr->radius = 30;

	/* Half-scale model. The scale stored on `model->scale` is NOT a
	 * "0.5 = half size" multiplier on its own -- it's combined with
	 * `model->definition->scale` (typically ~1000 for chr bodies) and
	 * `bodyAllocateModel` initializes `model->scale` to a small value
	 * (`scale = scaleRaw * 0.1` in body.c:204, then multiplied by
	 * body-specific height variation) so the FINAL `model->scale` is
	 * roughly 0.07-0.10 for a normal Skedar. Setting it to 0.5
	 * directly (the prior implementation) produced ~7x natural size,
	 * which Mike's playtest observed as "huge bots". The correct
	 * "half normal size" is to multiply whatever `bodyAllocateModel`
	 * left there by 0.5 -- preserves body-specific height variation
	 * + skel-class scaling, just halves it. */
	if (chr->model) {
		modelSetScale(chr->model, chr->model->scale * 0.5f);
	}

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

	/* Re-apply the full player setup every tick. cheatsReset, playerSpawn,
	 * and inventory pickups can each clobber pieces of it. See
	 * apply_player_setup() docblock for the full rationale. */
	apply_player_setup();

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
	if (g_Vars.currentplayer && g_Vars.currentplayer->prop) {
		player_pos = g_Vars.currentplayer->prop->pos;
	}
	if (testScenarioActiveMethod() == SWARM_METHOD_GPU) {
		if (swarmGpuAvailable()) {
			struct chrdata *chrs[TESTSCEN_SWARM_MAX_COUNT];
			for (s32 i = 0; i < TESTSCEN_SWARM_MAX_COUNT; i++) {
				chrs[i] = s_Swarm[i].chr;
			}
			swarmGpuStepAndApply(&player_pos, chrs, s_SwarmCount);
		} else {
			gpu_fallback_seek_tick(&player_pos);
		}
	}
	/* CPU mode: AI handles everything; nothing to do here. */

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
	/* Defensive: zero the in-use bitmap so a fresh session start (after
	 * a stage transition that didn't go through despawn_all) cannot
	 * inherit an old leak of "in use" markers. The aibot structs
	 * themselves live in BSS so memset is a no-op here -- the bitmap
	 * is the only state we need to clear. */
	memset(s_SwarmAibotInUse, 0, sizeof(s_SwarmAibotInUse));
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
