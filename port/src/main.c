#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <PR/ultratypes.h>
#include <PR/ultrasched.h>
#include <PR/os_message.h>

#include "lib/main.h"
#include "lib/mempc.h"
#include "bss.h"
#include "data.h"

#include "video.h"
#include "audio.h"
#include "input.h"
#include "inputctx.h"
#include "fs.h"
#include "romdata.h"
#include "romextract.h"
#include "romextract_pd.h"
#include "config.h"
#include "modmgr.h"
#include "modelcatalog.h"
#include "pdgui.h"
/* menumgr.h removed — P10 D5.7 OG Menu Removal */
#include "playerstats.h"
#include "achievements.h"
#include "system.h"
#include "console.h"
#include "utils.h"
#include "net/net.h"
#include "net/p2p.h"
#include "net/group_session.h"
#include "updater.h"
#include "actionmap.h"
#include "inputlayer.h"
#include "scene.h"
#include "savemigrate.h"
#include "savefile.h"
#include "testscenarios.h"
#include "net/matchsetup.h"
#include "prefs_agent.h"
#include "discord.h"
#include "identity.h"
#include "social.h"
#include "presence.h"
#include "chat.h"
#include "file_transfer.h"
#include "pdgui_toast.h"
#include "spectator.h"
#include "theater.h"
#include "listening_room.h"
#include "social_share.h"
#include "voice.h"
#include "assetcatalog.h"
#include "assetcatalog_scanner.h"
#include "assetcatalog_load.h"
#include "assetcatalog_cache.h"
#include "loader_pool.h"
#include "loader_walker.h"
#include "catalog_mgr_heads.h"
#include "catalog_mgr_bodies.h"
#include "catalog_mgr_arenas.h"
#include "game/stagetable.h"
#include "game/chr.h"

/* Engine Phase 2: thread pool + progress channel + boot overlay UI. */
#include "boot_pool.h"
#include "boot_progress.h"
#include "pdgui_bootoverlay.h"

/* Smoke verify harness (2026-05-11): client-only headless gate driven by
 * tools/smoke-verify/tests/*.json + tools/smoke-verify/run.ps1.  Inert
 * unless `--smoke <path>` is on the command line. */
#include "smoke_harness.h"

u32 g_OsMemSize = 0;
s32 g_OsMemSizeMb = 64;
s8 g_Resetting = false;
OSSched g_Sched;

OSMesgQueue g_MainMesgQueue;
OSMesg g_MainMesgBuf[32];

u8 *g_MempHeap = NULL;
u32 g_MempHeapSize = 0;

u32 g_VmNumTlbMisses = 0;
u32 g_VmNumPageMisses = 0;
u32 g_VmNumPageReplaces = 0;
u8 g_VmShowStats = 0;

s32 g_TickRateDiv = 1;
s32 g_TickExtraSleep = true;

s32 g_SkipIntro = false;

s32 g_JumpLoggingEnabled = 0;

/* Phase 2 fix #6 (input-menu pillar, 2026-05-01): when set, the HUD draws
 * an overlay showing the current interaction-cast half-angle (degrees)
 * + range so Mike can dial fix #5's tightening empirically. Settings ->
 * Debug Flags exposes the toggle + a slider. Default off. */
s32 g_InteractCastDebugDraw = 0;

s32 g_FileAutoSelect = -1;

extern s32 g_StageNum;

/* ---------------------------------------------------------------- *
 * Smoke-verify CLI fast-paths (c115, 2026-05-13).
 *
 * These flags exist so the smoke-verify pipeline (and Mike's manual
 * playtest dashboard) can drive directly to specific test points
 * without needing scripted menu navigation.  Each flag is parsed at
 * boot, captured into a static global below, and consumed at the
 * relevant lifecycle hook (pre-netInit, post-catalog-init, post-
 * setupCreateProps).  All flags are inert when absent from argv.
 *
 *   --no-net
 *       Gates netInit() entirely.  The network stack never binds,
 *       no UDP listener is created, ENet is never initialised.
 *       Closes the Windows Defender Firewall focus-steal class
 *       (smoke-verify-dispatch-findings-2026-05-13.md, S-3).
 *
 *   --main-menu
 *       Skips boot animation and lands at the title screen with
 *       the main menu auto-opened.  Behaviour: boots through
 *       STAGE_CITRAINING (same path --skip-intro takes) but arms
 *       g_PostExitMainMenuView so the main menu pops on the first
 *       in-CI frame.  --skip-intro is left untouched.
 *
 *   --launch-scenario <empty_map|swarm_cpu|swarm_gpu>
 *       After catalog init, calls testScenarioLaunch(...) for the
 *       named scenario.  empty_map -> TESTSCEN_EMPTY_MAP,
 *       swarm_cpu -> TESTSCEN_SWARM_CPU, swarm_gpu -> TESTSCEN_SWARM_GPU.
 *
 *   --launch-mission <stage_id> [--difficulty <agent|special|perfect>]
 *       stage_id can be hex (0x21) or a symbolic catalog ID
 *       (base:defection).  Seeds g_MissionConfig (difficulty,
 *       stage_id, stagenum) and routes the boot through the resolved
 *       stagenum.
 *
 *   --launch-mp-room <arena_id> <scenario_id> <bot_count>
 *       Seeds g_MatchConfig (stage_id, scenario_id, plus
 *       matchConfigAddBot * bot_count) so the Room screen renders
 *       with everything ready to launch.  Routes the boot through
 *       STAGE_CITRAINING with the Combat Sim room overlay auto-opened.
 *
 *   --debug-mount-bike
 *       Post-setupCreateProps hook: walks g_Vars.activeprops on the
 *       first frame after stage load and mounts player 0 on the
 *       first OBJTYPE_HOVERBIKE prop found.  One-shot per boot;
 *       no effect on stages without a hoverbike.
 * ---------------------------------------------------------------- */

static bool        g_BootNoNet            = false;
static bool        g_BootMainMenu         = false;
static const char *g_BootLaunchScenario   = NULL;
static const char *g_BootLaunchMission    = NULL;
static const char *g_BootLaunchDifficulty = NULL;
static const char *g_BootLaunchMpArena    = NULL;
static const char *g_BootLaunchMpScenario = NULL;
static s32         g_BootLaunchMpBotCount = 0;
static bool        g_BootMountBike        = false;
/* Latched one-shot for the bike-mount hook; tickled by pdmain.c's
 * mainTick once activeprops is populated and player 0 is alive. */
static s32         g_BootMountBikePending = 0;

/* c115 (2026-05-14): deferred launch-scenario state. Worker delta's
 * iter-2 re-run showed that calling testScenarioLaunch synchronously
 * from bootApplyCliFastPaths fires before the stage setup has placed
 * the player prop. The first match's respawn fails (player at
 * y=-2^31), the match ends instantly into endscreen_solo, imgui_menu
 * IMC pushes on top, and gameplay-gated actions (the swarm-cycler
 * ACTION_TESTSCEN_CYCLE_COUNT, ACTION_USE) get suppressed by
 * gameplayInputSuppressed(). Fix: latch the parsed scenario id at
 * boot, return without calling testScenarioLaunch, and dispatch it on
 * the first mainTick frame where g_Vars.lvframenum >= 4 -- the same
 * gate the --debug-mount-bike hook uses for the activeprops walk. */
static s32         g_BootLaunchScenarioPending = 0;
static s32         g_BootLaunchScenarioId      = 0;  /* TESTSCEN_NONE */

s32 bootGetMemSize(void)
{
	return (s32)g_OsMemSize;
}

void *bootAllocateStack(s32 threadid, s32 size)
{
	static u8 bruh[0x1000];
	return bruh;
}

void bootCreateSched(void)
{
	osCreateMesgQueue(&g_MainMesgQueue, g_MainMesgBuf, ARRAYCOUNT(g_MainMesgBuf));
	if (osTvType == OS_TV_MPAL) {
		osCreateScheduler(&g_Sched, NULL, OS_VI_MPAL_LAN1, 1);
	} else {
		osCreateScheduler(&g_Sched, NULL, OS_VI_NTSC_LAN1, 1);
	}
}

static void gameInit(void)
{
	osMemSize = g_OsMemSizeMb * 1024 * 1024;

	for (s32 i = 0; i < MAX_LOCAL_PLAYERS; ++i) {
		struct extplayerconfig *cfg = g_PlayerExtCfg + i;
		cfg->fovzoommult = cfg->fovzoom ? cfg->fovy / 60.0f : 1.0f;
	}

	if (g_HudCenter == HUDCENTER_NORMAL) {
		g_HudAlignModeL = G_ASPECT_CENTER_EXT;
		g_HudAlignModeR = G_ASPECT_CENTER_EXT;
	} else if (g_HudCenter == HUDCENTER_WIDE) {
		g_HudAlignModeL = G_ASPECT_LEFT_EXT | G_ASPECT_WIDE_EXT;
		g_HudAlignModeR = G_ASPECT_RIGHT_EXT | G_ASPECT_WIDE_EXT;
	}
}

/* Engine Phase 2 boot orchestrator (2026-05-03).
 *
 * Runs the full catalog / extract / verify / walker / emitter sequence
 * on a boot-pool worker thread while the main thread renders the boot
 * overlay.  Phase boundaries push begin / end events into the progress
 * channel; per-file updates inside romExtract* drive the bar fill.
 *
 * Single-worker enqueue per the Phase 2 plan (see
 * context/designs/engine/startup-acceleration.md): one job runs the
 * entire sequence serially.  Phase 3 will rewrite romExtractVerifyAll
 * to fan out per-file SHA-256 across the pool's worker threads. */
static void bootRunCatalogWork(void *arg)
{
	(void)arg;

	bootProgressBeginPhase(BOOT_PHASE_EXTRACT_FILES);
	romdataInit();
	catalogCacheVerifyRom(g_RomName, NULL);
	romExtractAllFiles();
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_VERIFY_FILES);
	romExtractVerifyAll();
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EXTRACT_SEGS);
	romExtractAllSegments();
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_VERIFY_SEGS);
	romExtractVerifyAllSegments();
	romExtractEmitBootIntegrityReport();
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_RELEASE_ROM);
	romdataReleaseRom();
	bootProgressEndPhase();

	/* c115 (2026-05-13): --no-net gates the entire network stack.  When
	 * set, g_NetInit stays false, every net.c entry point early-returns
	 * cleanly, and Windows never raises the "Allow on networks?" prompt.
	 * Smoke-verify-dispatch-findings-2026-05-13.md S-3 explains why this
	 * matters: the firewall dialog steals SDL focus and breaks every
	 * scripted test that copies the binary into a fresh per-test dir. */
	if (!g_BootNoNet) {
		netInit();
	} else {
		sysLogPrintf(LOG_NOTE, "BOOT: --no-net set; netInit() skipped");
	}
	g_ValidGbcRomFound = romdataCheckGbcRom();
	gameInit();
	modmgrInit();

	bootProgressBeginPhase(BOOT_PHASE_CATALOG_INIT);
	catalogInit();
	stageTableInit();
	assetCatalogInit();
	assetCatalogRegisterBaseGame();
	assetCatalogRegisterStageSceneFiles();
	{
		const char *modsdir = modmgrGetModsDir();
		if (modsdir) {
			assetCatalogScanComponents(modsdir);
			assetCatalogScanBotVariants(modsdir);
		}
	}
	sysLogPrintf(LOG_NOTE, "Asset Catalog: %d entries registered", assetCatalogGetCount());
	catalogManagerHeadInit();
	catalogManagerBodyInit();
	catalogManagerArenaInit();
	bootProgressEndPhase();

	/* B-325 (2026-05-03): emitters run BEFORE the walker, not after. The
	 * walker reads `data/<romid>/<kind>/*.pd<ext>` and populates loader_pool
	 * typed payload (struct weapon, struct head, etc.). On a fresh install
	 * the per-asset dirs are empty until the emitters write them. Running
	 * the walker first leaves loader_pool inactive (s_LoaderActive=0), so
	 * catalogManagerGetWeaponByIndex returns NULL for every index -- which
	 * is what caused B-324 (bgunCalculateBlend AV: NULL weapon->sway).
	 *
	 * Order is now: emit -> walk -> register weapon model files (depends on
	 * the weapon pool being populated) -> build caches. */

	bootProgressBeginPhase(BOOT_PHASE_EMIT_WPN);
	(void)romExtractAllPdwpn(0);
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_MESH);
	(void)romExtractAllPdmesh(0);
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_ANIM);
	(void)romExtractAllPdanim(0);
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_HEAD);
	(void)romExtractAllPdhead(0);
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_BODY);
	(void)romExtractAllPdbody(0);
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_ARENA);
	(void)romExtractAllPdarena(0);
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_ANIMCHR);
	(void)romExtractAllPdanimChr(0);
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_SFX);
	(void)romExtractAllPdsfx(0);
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_VOICE);
	(void)romExtractAllPdvoice(0);
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_SONG);
	(void)romExtractAllPdsong(0);
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_FONT);
	(void)romExtractAllPdfont(0);
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_LANG);
	(void)romExtractAllPdlang(0);
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_UI);
	(void)romExtractAllPdui(0);
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_WALKER);
	{
		loader_walker_result_t walker_result;
		loaderWalkerLoadAll(&walker_result);
		(void)walker_result;
		if (loaderPoolIsActive()) {
			assetCatalogRegisterWeaponModelFiles();
		}
	}
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_BUILD_CACHES);
	catalogBuildRuntimeCaches();
	catalogLoadInit();
	modmgrLoadComponentState();
	modmgrCatalogChanged();
	bootProgressEndPhase();

	bootProgressMarkComplete();
}

static void cleanup(void)
{
	sysLogPrintf(LOG_NOTE, "shutdown");
	catalogLoadLogStats();

	// Signal all subsystems that we are exiting. Must be set before
	// netDisconnect so that blocking teardown paths (UPnP HTTP delete,
	// stage transitions) are skipped and the process exits quickly.
	g_AppQuitting = 1;

	discordShutdown();
	inputLayerShutdown();
	sceneShutdown();
	inputCtxShutdown();
	updaterShutdown();
	pdguiShutdown();
	netDisconnect();
	modmgrShutdown();
	actionmapSaveBinds();
	configSave(CONFIG_PATH);
	statsShutdown();
	videoShutdown();
	crashShutdown();

	/* H-7: Free persistent allocations AFTER all subsystems are shut down,
	 * so no subsystem dereferences freed memory during its teardown. */
	mempPCValidate("shutdown");
	mempPCFreeAll();

	/* SDL_Quit tears down all SDL subsystems (video, audio, timer, etc.)
	 * that were opened via SDL_Init / SDL_InitSubSystem during boot. */
	SDL_Quit();
}

/* ---------------------------------------------------------------- *
 * c115 smoke-verify CLI fast-paths.                                *
 * ---------------------------------------------------------------- */

/* Extern shims for game-side symbols not exposed via port/include
 * headers.  Keeping these inline (rather than including
 * src/include/game/*) avoids pulling the game's local CLAUDE.md
 * legacy-types contract into this PC-port file. */
extern void setCurrentPlayerNum(s32 playernum);
extern bool currentPlayerTryMountHoverbike(struct prop *prop);
extern s32 g_PostExitMainMenuView;

/* Map a --difficulty string to the DIFF_* constants used by g_MissionConfig
 * and lvSetDifficulty.  Returns DIFF_A on unknown / NULL input. */
static s32 bootResolveDifficulty(const char *name)
{
	if (!name || !name[0]) {
		return DIFF_A;
	}
	if (strcasecmp(name, "agent") == 0 || strcasecmp(name, "a") == 0) {
		return DIFF_A;
	}
	if (strcasecmp(name, "special") == 0
			|| strcasecmp(name, "sa") == 0
			|| strcasecmp(name, "special-agent") == 0) {
		return DIFF_SA;
	}
	if (strcasecmp(name, "perfect") == 0
			|| strcasecmp(name, "pa") == 0
			|| strcasecmp(name, "perfect-agent") == 0) {
		return DIFF_PA;
	}
	sysLogPrintf(LOG_WARNING,
		"BOOT: --difficulty '%s' unrecognised; defaulting to Agent", name);
	return DIFF_A;
}

/* Try to resolve a stage identifier (catalog ID like "base:defection" OR
 * hex like "0x21") into a stagenum.  Returns -1 on failure. */
static s32 bootResolveStageIdToNum(const char *id)
{
	if (!id || !id[0]) {
		return -1;
	}
	/* Hex / decimal numeric form. */
	if (id[0] == '0' && (id[1] == 'x' || id[1] == 'X')) {
		char *endp = NULL;
		long v = strtol(id, &endp, 16);
		if (endp && endp != id && v > 0 && v < 0x100) {
			return (s32)v;
		}
	}
	/* Catalog ID. */
	catalog_stage_result_t sresult;
	if (catalogResolveStage(id, &sresult)) {
		return sresult.stagenum;
	}
	return -1;
}

/* Apply --main-menu's post-CI main menu auto-open arm.  Must run after
 * catalog init (so g_PostExitMainMenuView is reachable in BSS).  No-op
 * unless --main-menu was on the command line. */
static void bootApplyMainMenu(void)
{
	if (!g_BootMainMenu) {
		return;
	}
	g_PostExitMainMenuView = 0;
	sysLogPrintf(LOG_NOTE,
		"BOOT: --main-menu armed g_PostExitMainMenuView=0 (top-level)");
}

/* Arm the --launch-scenario one-shot. Resolves the scenario id string
 * to a test_scenario_t enum and latches it for bootLaunchScenarioTick
 * to consume on the first frame where the player prop is positioned
 * (lvframenum >= 4). Synchronous testScenarioLaunch here would fire
 * before stage setup placed the player, causing matchStart to bounce
 * straight into endscreen_solo. See g_BootLaunchScenarioPending
 * comment block above for the full failure mode. */
static void bootApplyLaunchScenario(void)
{
	if (!g_BootLaunchScenario || !g_BootLaunchScenario[0]) {
		return;
	}
	test_scenario_t scen = TESTSCEN_NONE;
	if (strcasecmp(g_BootLaunchScenario, "empty_map") == 0
			|| strcasecmp(g_BootLaunchScenario, "empty-map") == 0
			|| strcasecmp(g_BootLaunchScenario, "empty") == 0) {
		scen = TESTSCEN_EMPTY_MAP;
	} else if (strcasecmp(g_BootLaunchScenario, "swarm_cpu") == 0
			|| strcasecmp(g_BootLaunchScenario, "swarm-cpu") == 0) {
		scen = TESTSCEN_SWARM_CPU;
	} else if (strcasecmp(g_BootLaunchScenario, "swarm_gpu") == 0
			|| strcasecmp(g_BootLaunchScenario, "swarm-gpu") == 0) {
		scen = TESTSCEN_SWARM_GPU;
	} else {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --launch-scenario '%s' unknown (expected empty_map|swarm_cpu|swarm_gpu)",
			g_BootLaunchScenario);
		return;
	}
	g_BootLaunchScenarioId      = (s32)scen;
	g_BootLaunchScenarioPending = 1;
	sysLogPrintf(LOG_NOTE,
		"BOOT: --launch-scenario '%s' armed (will dispatch testScenarioLaunch(%d) once lvframenum >= 4)",
		g_BootLaunchScenario, (s32)scen);
}

/* Apply --launch-mission by seeding g_MissionConfig and pointing
 * g_StageNum at the resolved stagenum.  We do NOT route through
 * menuhandlerAcceptMission here because the menupool / IMC stack
 * isn't safe to push to from pre-mainProc state.  Instead we mirror
 * the subset of state menuhandlerAcceptMission writes that the
 * stage-load path actually consumes. */
static void bootApplyLaunchMission(void)
{
	if (!g_BootLaunchMission || !g_BootLaunchMission[0]) {
		return;
	}
	s32 stagenum = bootResolveStageIdToNum(g_BootLaunchMission);
	if (stagenum < 0) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --launch-mission '%s' unresolved (try 0xNN hex or base:* catalog ID)",
			g_BootLaunchMission);
		return;
	}
	s32 diff = bootResolveDifficulty(g_BootLaunchDifficulty);

	memset(&g_MissionConfig, 0, sizeof(g_MissionConfig));
	g_MissionConfig.difficulty = (u8)diff;
	g_MissionConfig.pdmode = 0;
	g_MissionConfig.iscoop = 0;
	g_MissionConfig.isanti = 0;
	g_MissionConfig.pdmodereaction = 0;
	g_MissionConfig.pdmodehealth = 128;
	g_MissionConfig.pdmodedamage = 128;
	g_MissionConfig.pdmodeaccuracy = 128;
	/* Stash the catalog ID when one was supplied so endscreen restart
	 * paths can re-resolve through the catalog.  Falls back to legacy
	 * stagenum when the input was numeric. */
	if (g_BootLaunchMission[0] != '0' || (g_BootLaunchMission[1] != 'x' && g_BootLaunchMission[1] != 'X')) {
		strncpy(g_MissionConfig.stage_id, g_BootLaunchMission,
			sizeof(g_MissionConfig.stage_id) - 1);
		g_MissionConfig.stage_id[sizeof(g_MissionConfig.stage_id) - 1] = '\0';
	}
	g_MissionConfig.stagenum = (u8)stagenum;
	g_MissionConfig.stageindex = 0;

	g_StageNum = stagenum;
	if (g_FileAutoSelect < 0) {
		g_FileAutoSelect = 0;
	}

	sysLogPrintf(LOG_NOTE,
		"BOOT: --launch-mission '%s' -> stagenum=0x%02x difficulty=%d",
		g_BootLaunchMission, (u32)stagenum, diff);
}

/* Apply --launch-mp-room by seeding g_MatchConfig (the lobby/Room
 * screen's input state) and arming the solo-room overlay so the
 * Room screen opens on first CI frame.  The actual match start
 * still requires the user to press Start Match in the Room screen
 * -- this fast-path only gets the smoke test to the screen, where
 * the existing keyboard-driven Start Match button is reachable. */
static void bootApplyLaunchMpRoom(void)
{
	if (!g_BootLaunchMpArena || !g_BootLaunchMpArena[0]) {
		return;
	}
	matchConfigInit();
	strncpy(g_MatchConfig.stage_id, g_BootLaunchMpArena,
		sizeof(g_MatchConfig.stage_id) - 1);
	g_MatchConfig.stage_id[sizeof(g_MatchConfig.stage_id) - 1] = '\0';
	if (g_BootLaunchMpScenario && g_BootLaunchMpScenario[0]) {
		strncpy(g_MatchConfig.scenario_id, g_BootLaunchMpScenario,
			sizeof(g_MatchConfig.scenario_id) - 1);
		g_MatchConfig.scenario_id[sizeof(g_MatchConfig.scenario_id) - 1] = '\0';
	}
	/* Resolve stage_id -> stagenum so the room screen displays the
	 * right arena thumbnail even before matchStart() runs. */
	{
		const asset_entry_t *ae = assetCatalogResolve(g_MatchConfig.stage_id);
		if (ae && ae->type == ASSET_ARENA) {
			g_MatchConfig.stagenum = (u8)ae->ext.arena.stagenum;
		}
	}
	/* Add bots up to the requested count.  matchConfigAddBot enforces
	 * MATCH_MAX_SLOTS and the per-humans-cap clamp, so passing 31 on
	 * a malformed config is safe (excess slots silently dropped). */
	for (s32 i = 0; i < g_BootLaunchMpBotCount; ++i) {
		(void)matchConfigAddBot(BOTTYPE_GENERAL, BOTDIFF_NORMAL, NULL, NULL, NULL);
	}
	/* Arm CI -> main menu auto-pop with view=0 (top-level) so the
	 * smoke test's scripted keys can navigate from main menu ->
	 * Combat Simulator to reach the room.  A future enhancement
	 * could open the Room directly via pdguiSoloRoomOpen, but that
	 * pushes the s_SoloRoomActive flag which is only consumed inside
	 * pdguiLobbyRender's NETMODE_NONE branch -- safer to walk the
	 * normal main-menu path which the smoke harness already knows
	 * how to drive. */
	g_PostExitMainMenuView = 0;
	/* Drop into CI like the other Room entry points. */
	if (g_StageNum == STAGE_TITLE) {
		g_StageNum = STAGE_CITRAINING;
	}
	sysLogPrintf(LOG_NOTE,
		"BOOT: --launch-mp-room arena='%s' scenario='%s' bots=%d (seeded g_MatchConfig)",
		g_BootLaunchMpArena,
		g_BootLaunchMpScenario ? g_BootLaunchMpScenario : "(default)",
		g_BootLaunchMpBotCount);
}

/* Arm the --debug-mount-bike one-shot.  The actual mount runs inside
 * mainTick once activeprops is populated; see
 * bootDebugMountBikeTick() below. */
static void bootApplyDebugMountBike(void)
{
	if (!g_BootMountBike) {
		return;
	}
	g_BootMountBikePending = 1;
	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-mount-bike armed (will mount on first hoverbike prop after stage load)");
}

/* Dispatcher called once from main() after the catalog is fully
 * initialised. */
static void bootApplyCliFastPaths(void)
{
	bootApplyMainMenu();
	bootApplyLaunchScenario();
	bootApplyLaunchMission();
	bootApplyLaunchMpRoom();
	bootApplyDebugMountBike();
}

/* Called once per frame from pdmain.c's mainTick when the
 * --launch-scenario one-shot is armed. Dispatches testScenarioLaunch
 * once the stage setup has placed the player prop, so the scenario's
 * matchStart sees a positioned player and the first respawn succeeds
 * (instead of failing into endscreen_solo). Returns 1 when the
 * scenario was attempted, else 0.
 *
 * Gates:
 *   - g_BootLaunchScenarioPending must be set
 *   - g_Vars.lvframenum >= 4 (same gate --debug-mount-bike uses; load
 *     black frames are out of the way and the player prop has been
 *     spawned + positioned by setupCreateProps).
 *
 * One-shot: clears the latch on either success or failure so a
 * subsequent stage change does not re-fire the scenario. */
s32 bootLaunchScenarioTick(void)
{
	if (!g_BootLaunchScenarioPending) {
		return 0;
	}
	if (g_Vars.lvframenum < 4) {
		return 0;
	}
	test_scenario_t scen = (test_scenario_t)g_BootLaunchScenarioId;
	sysLogPrintf(LOG_NOTE,
		"BOOT: --launch-scenario consuming latch: testScenarioLaunch(%d) at lvframenum=%d",
		(s32)scen, (s32)g_Vars.lvframenum);
	if (!testScenarioLaunch(scen, NULL)) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --launch-scenario deferred dispatch failed; falling back to default boot stage");
	}
	g_BootLaunchScenarioPending = 0;
	return 1;
}

/* Called once per frame from pdmain.c's mainTick when the
 * --debug-mount-bike one-shot is armed.  Walks g_Vars.activeprops
 * looking for the first OBJTYPE_HOVERBIKE prop, and on match calls
 * currentPlayerTryMountHoverbike(prop) once, then clears the latch.
 *
 * Gates:
 *   - g_BootMountBikePending must be set (caller checks before calling)
 *   - g_Vars.lvframenum >= 4 (CI fly-in / load black frames out of the way)
 *   - g_Vars.players[0] must be alive and have a valid prop
 *   - prop must be PROPTYPE_OBJ + obj->type == OBJTYPE_HOVERBIKE
 *
 * Returns 1 when the mount was attempted (whether or not it succeeded),
 * else 0.  Caller clears g_BootMountBikePending on either outcome. */
s32 bootDebugMountBikeTick(void)
{
	if (!g_BootMountBikePending) {
		return 0;
	}
	if (g_Vars.lvframenum < 4) {
		return 0;
	}
	if (!g_Vars.players[0] || !g_Vars.players[0]->prop) {
		return 0;
	}

	struct prop *prop = g_Vars.activeprops;
	while (prop) {
		if (prop->type == PROPTYPE_OBJ
				&& prop->obj
				&& prop->obj->type == OBJTYPE_HOVERBIKE
				&& (prop->obj->hidden & OBJHFLAG_MOUNTED) == 0) {
			/* currentPlayerTryMountHoverbike reads g_Vars.currentplayer +
			 * g_Vars.currentplayerstats, so make sure player 0 is current
			 * before we call.  Don't dereference unless setCurrentPlayerNum
			 * succeeded -- but it's a void function, so we trust the
			 * caller's invariant (lvFrame >= 4 implies player 0 alive). */
			setCurrentPlayerNum(0);
			bool ok = currentPlayerTryMountHoverbike(prop);
			sysLogPrintf(LOG_NOTE,
				"BOOT: --debug-mount-bike consumed: prop=%p stagenum=0x%02x result=%s",
				(void *)prop, (u32)g_Vars.stagenum, ok ? "MOUNTED" : "REJECTED");
			g_BootMountBikePending = 0;
			return 1;
		}
		prop = prop->next;
	}
	/* No hoverbike on this stage.  Clear the latch after the load
	 * settles so we don't keep scanning every frame for the entire
	 * session; a follow-up stage change won't re-fire (one-shot). */
	if (g_Vars.lvframenum > 120) {
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-mount-bike: no hoverbike prop on stagenum=0x%02x; one-shot consumed",
			(u32)g_Vars.stagenum);
		g_BootMountBikePending = 0;
		return 1;
	}
	return 0;
}

int main(int argc, const char **argv)
{
	sysInitArgs(argc, argv);

	/* D13: Apply pending update VERY EARLY — before any subsystem init.
	 * If an update was downloaded previously, this renames the .update file
	 * into place and re-execs. If no pending update, this is a no-op.
	 * NOTE: sysLogPrintf is safe to call before sysInit (uses static buffers).
	 * detectExePath uses only stdlib — no SDL or game init required. */
	updaterApplyPending();

	if (!sysArgCheck("--no-crash-handler")) {
		crashInit();
	}

	/* FIX-A.1/A.4: Capture stack base for chraTick depth monitoring.
	 * Called from main()'s frame so the base address is near the top
	 * of the 8 MB stack.  Must be before the game loop starts. */
	chrTickStackInit();

	/* Parse --dedicated early so the window title is correct in videoInit */
	if (sysArgCheck("--dedicated")) {
		extern s32 g_NetDedicated;
		g_NetDedicated = 1;
	}

	/* c115 (2026-05-13): smoke-verify CLI fast-paths.  Parse here so
	 * the netInit gate (--no-net) and the post-catalog hooks
	 * (--launch-scenario, --launch-mission, --launch-mp-room) all see
	 * the same captured values.  Each flag is opt-in; absent flags
	 * leave the corresponding global at its zero-init default. */
	g_BootNoNet            = sysArgCheck("--no-net") ? true : false;
	g_BootMainMenu         = sysArgCheck("--main-menu") ? true : false;
	g_BootLaunchScenario   = sysArgGetString("--launch-scenario");
	g_BootLaunchMission    = sysArgGetString("--launch-mission");
	g_BootLaunchDifficulty = sysArgGetString("--difficulty");
	g_BootLaunchMpArena    = sysArgGetString("--launch-mp-room");
	g_BootMountBike        = sysArgCheck("--debug-mount-bike") ? true : false;
	/* --launch-mp-room takes three positional args: <arena> <scenario>
	 * <bot_count>.  sysArgGetString returns the slot immediately after
	 * the flag; we scan argv linearly for the next two.  Unset on
	 * malformed input so the post-init hook degrades to no-op. */
	if (g_BootLaunchMpArena) {
		s32 found = -1;
		for (s32 i = 1; i < argc - 1; ++i) {
			if (argv[i] && strcmp(argv[i], "--launch-mp-room") == 0) {
				found = i;
				break;
			}
		}
		if (found >= 0 && (found + 3) < argc) {
			g_BootLaunchMpScenario = argv[found + 2];
			g_BootLaunchMpBotCount = (s32)strtol(argv[found + 3], NULL, 0);
			if (g_BootLaunchMpBotCount < 0) {
				g_BootLaunchMpBotCount = 0;
			} else if (g_BootLaunchMpBotCount > 31) {
				g_BootLaunchMpBotCount = 31;
			}
		} else {
			g_BootLaunchMpArena = NULL;
		}
	}

	conInit();
	sysInit();
	fsInit();
	/* Priority M / B-238 / M-1.7: optional perf harness for the .pdmod
	 * archive layer. Runs when --bench-pdmod is passed; exits cleanly when
	 * done. No effect otherwise. */
	{
		extern void modArchiveRunBenchmark(void);
		modArchiveRunBenchmark();
	}
	/* M0.2 Phase B: register pd.ini keys BEFORE configLoad (called inside configInit) */
	actionmapInit();
	configInit();
	netConfigSanitizeLoadedAddresses();
	/* M0.2 Phase B: parse bind strings that configLoad just populated */
	actionmapLoadBinds();
	inputLayerInit();
	sceneInit();

	/* D13: Initialize update system + save migration after filesystem is ready */
	updaterInit();
	saveMigrateInit();
	saveInit(); /* B-129: wire save dir into savefile.c — must follow fsInit() */

	/* Phase 1 connectivity: identity + social storage. Identity gives us a
	 * stable device UUID; the social store derives a 4-word connect code
	 * from that UUID and loads friends/blocks/visibility from disk. Must run
	 * before netInit() so any presence-aware net code can read the local
	 * identity. The dedicated server initialises identity through hubInit()
	 * instead, and does not load social state. */
	identityInit();
	socialInit();
	/* c115 (2026-05-13): --no-net also gates p2pInit() because
	 * p2pLanStart() binds UDP 27101 for LAN-broadcast discovery -- that
	 * bind alone is enough to trigger the Windows Defender Firewall
	 * dialog and steal SDL focus, defeating the smoke harness.
	 * Skipping p2pInit leaves s_Initialised=false, which makes every
	 * p2pTick()/p2pLanTick()/p2pStartProbe() entry early-return as a
	 * cheap no-op. */
	if (!g_BootNoNet) {
		p2pInit();
	} else {
		sysLogPrintf(LOG_NOTE, "BOOT: --no-net set; p2pInit() skipped");
	}
	presenceInit();
	groupSessionInit();
	chatInit();
	fileTransferInit();
	pdguiToastInit();
	spectatorInit();
	theaterInit();
	listeningRoomInit();
	shareInit();
	voiceInit();

	/* D13: Start background update check (non-blocking) */
	if (!sysArgCheck("--no-update-check")) {
		updaterCheckAsync();
	}
	videoInit();
	pdguiInit(videoGetWindowHandle());
	/* menuMgrInit() removed — P10 D5.7 OG Menu Removal */
	statsInit();
	achievementsInit();
	/* S309: per-agent preferences sidecar.  prefsAgentLoad runs on agent
	 * switch; this init just marks the subsystem live. */
	prefsAgentInit();

	/* D7: Discord Rich Presence — connects to Discord IPC pipe if running.
	 * Fails silently if Discord is not open. */
	discordInit();
	inputInit();

	/* Input context stack: must init after inputInit() (SDL event watch)
	 * and push g_CtxGameplay as the base context before any menu/GUI code
	 * that might reference the context stack. */
	inputCtxInit();
	inputCtxPush(&g_CtxGameplay);

	audioInit();

	/* Dedicated server: mute ALL audio — music and sound effects.
	 * The server has no player, no need for any audio output. */
	if (g_NetDedicated) {
		extern void optionsSetMusicVolume(s32 vol);
		extern void sndSetSfxVolume(s32 vol);
		optionsSetMusicVolume(0);
		sndSetSfxVolume(0);
	}

	/* Smoke verify harness init (2026-05-11): must come after SDL is up
	 * (videoInit + pdguiInit) so SDL_GetTicks() is valid for time
	 * scheduling, and after configInit/sysLogSet* registration so the
	 * test-declared channel mask + verbose flag override pd.ini cleanly.
	 * No-op when --smoke is not present. */
	(void)smokeHarnessInit();

	/* Engine Phase 2 boot orchestrator (2026-05-03): the catalog work
	 * block (romdataInit through modmgrCatalogChanged) runs on a worker
	 * thread inside bootRunCatalogWork while the main thread drives the
	 * boot overlay's render loop.  Window stays responsive throughout.
	 *
	 * Single-worker enqueue per Phase 2 design: one job runs the full
	 * sequence serially.  Phase 3 will fan out the verify pass per file.
	 *
	 * Server build path is the same; bootPumpOverlay no-ops on
	 * g_NetDedicated and the worker still runs through the orchestrator. */
	bootProgressInit();
	bootPoolInit();
	pdguiBootOverlayInit();

	bootPoolEnqueue(bootRunCatalogWork, NULL);

	while (!bootProgressIsComplete()) {
		pdguiBootOverlayPump();
		/* Smoke harness timeout coverage during boot: if the catalog work
		 * thread hangs we still want to fire the timeout exit rather than
		 * wait for some upstream watchdog. Cheap no-op when not in smoke
		 * mode. Does not push input events here -- the boot overlay frame
		 * does not consume SDL key events. */
		smokeHarnessTick();
		/* Cooperative pause when not in dedicated mode; the dedicated
		 * branch already inserts SDL_Delay inside the no-op pump path. */
	}

	bootPoolWaitIdle();

	pdguiBootOverlayShutdown();
	bootPoolShutdown();
	bootProgressShutdown();

	atexit(cleanup);

	bootCreateSched();

	g_OsMemSize = osGetMemSize();

	g_MempHeapSize = g_OsMemSize;
	g_MempHeap = sysMemZeroAlloc(g_MempHeapSize);
	if (!g_MempHeap) {
		sysFatalError("Could not alloc %u bytes for memp heap.", g_MempHeapSize);
	}

	sysLogPrintf(LOG_NOTE, "memp heap at %p - %p", g_MempHeap, g_MempHeap + g_MempHeapSize);
	if (g_RomFile) {
		sysLogPrintf(LOG_NOTE, "rom  file at %p - %p", g_RomFile, g_RomFile + g_RomFileSize);
	} else {
		sysLogPrintf(LOG_NOTE, "rom  file released (Phase 3 Pass C): runtime reads disk-only");
	}

	/* NOTE: catalogValidateAll() was previously here, but it calls
	 * modeldefLoadToNew() -> mempAlloc() which requires the pool system.
	 * mempSetHeap() isn't called until mainInit() (pdmain.c), so loading
	 * models here crashed with access violations on every entry.
	 * Moved to pdmain.c after mempSetHeap(). */

	g_SndDisabled = sysArgCheck("--no-sound");

	g_StageNum = sysArgGetInt("--boot-stage", STAGE_TITLE);

	g_FileAutoSelect = sysArgGetInt("--profile", -1);

	if (g_StageNum == STAGE_TITLE && (sysArgCheck("--skip-intro") || g_SkipIntro)) {
		// shorthand for --boot-stage 0x26
		g_StageNum = STAGE_CITRAINING;
	} else if (g_StageNum < 0x01 || g_StageNum > 0x5d) {
		// stage num out of range
		g_StageNum = STAGE_TITLE;
	}

	/* c115 --main-menu: corrected --skip-intro semantics.  Boot through
	 * STAGE_CITRAINING (same path --skip-intro takes), then arm the
	 * post-exit main menu auto-pop so the player lands at the main menu
	 * on the first in-CI frame.  Distinct from --skip-intro (which
	 * drops the player into CI free-roam with no menu).  See
	 * smoke-verify-dispatch-findings-2026-05-13.md S-5. */
	if (g_BootMainMenu && g_StageNum == STAGE_TITLE) {
		g_StageNum = STAGE_CITRAINING;
	}

	if (g_NetJoinLatch || g_NetHostLatch) {
		if (g_FileAutoSelect < 0) {
			// default to profile 0 if going into a net game
			g_FileAutoSelect = 0;
		}
		if (g_NetDedicated) {
			/* Dedicated server: load game defaults immediately so we don't
			 * need an agent file. The server doesn't play — it just hosts. */
			extern struct gamefile g_GameFile;
			extern void gamefileLoadDefaults(struct gamefile *file);
			gamefileLoadDefaults(&g_GameFile);
		}
		// skip the intro if going into a net game
		g_StageNum = STAGE_CITRAINING;
	}

	if (g_StageNum != STAGE_TITLE) {
		sysLogPrintf(LOG_NOTE, "boot stage set to 0x%02x", g_StageNum);
	}

	if (g_FileAutoSelect >= 0) {
		sysLogPrintf(LOG_NOTE, "player profile set to %d", g_FileAutoSelect);
	}


	/* Set FORWARDPITCH on by default for all players if not already set. */
	for (s32 i = 0; i < MAX_PLAYERS; ++i) {
		if (!(g_PlayerConfigsArray[i].options & ~(OPTION_FORWARDPITCH))) {
			/* options is still at zero (first-run default) — set our defaults */
			g_PlayerConfigsArray[i].options |= OPTION_FORWARDPITCH;
		}
	}

	/* Phase 3 Pass D (2026-05-02): drain the boot-deferred toast queue.
	 * romExtractVerifyAll / romExtractVerifyAllSegments populated the
	 * queue if they detected hash-mismatch + recovery / unrecoverable
	 * outcomes earlier in boot.  romExtractEmitBootIntegrityReport may
	 * have queued an aggregate toast as well.  Replay them now with
	 * fresh enqueued_ms timestamps so the toast renderer (kicks in once
	 * mainProc enters its loop) shows them as fresh notifications.
	 * Server build: no-op. */
	romExtractToastDrain();

	/* c115 (2026-05-13): apply smoke-verify CLI fast-paths now that the
	 * catalog is fully initialised and the game state is ready for
	 * scenario / match seeding.  Each fast-path is self-contained; on
	 * failure they log a warning and the boot continues to whatever
	 * stage g_StageNum points at. */
	bootApplyCliFastPaths();

	mainProc();

	return 0;
}

PD_CONSTRUCTOR static void gameConfigInit(void)
{
	configRegisterInt("Game.MemorySize", &g_OsMemSizeMb, 64, 2048);
	configRegisterInt("Game.CenterHUD", &g_HudCenter, 0, 2);
	configRegisterInt("Game.MenuMouseControl", &g_MenuMouseControl, 0, 1);
	configRegisterFloat("Game.ScreenShakeIntensity", &g_ViShakeIntensityMult, 0.f, 10.f);
	configRegisterInt("Game.TickRateDivisor", &g_TickRateDiv, 0, 10);
	configRegisterInt("Game.ExtraSleep", &g_TickExtraSleep, 0, 1);
	configRegisterInt("Game.SkipIntro", &g_SkipIntro, 0, 1);
	configRegisterInt("Game.DisableMpDeathMusic", &g_MusicDisableMpDeath, 0, 1);
	configRegisterInt("Game.GEMuzzleFlashes", &g_BgunGeMuzzleFlashes, 0, 1);
	configRegisterInt("Debug.JumpLogging", &g_JumpLoggingEnabled, 0, 1);
	configRegisterInt("Debug.InteractCastDebugDraw", &g_InteractCastDebugDraw, 0, 1);
	for (s32 j = 0; j < MAX_LOCAL_PLAYERS; ++j) {
		const s32 i = j + 1;
		configRegisterFloat(strFmt("Game.Player%d.FovY", i), &g_PlayerExtCfg[j].fovy, 5.f, 175.f);
		configRegisterInt(strFmt("Game.Player%d.FovAffectsZoom", i), &g_PlayerExtCfg[j].fovzoom, 0, 1);
		configRegisterInt(strFmt("Game.Player%d.MouseAimMode", i), &g_PlayerExtCfg[j].mouseaimmode, 0, 1);
		configRegisterFloat(strFmt("Game.Player%d.MouseAimSpeedX", i), &g_PlayerExtCfg[j].mouseaimspeedx, 0.f, 10.f);
		configRegisterFloat(strFmt("Game.Player%d.MouseAimSpeedY", i), &g_PlayerExtCfg[j].mouseaimspeedy, 0.f, 10.f);
		configRegisterFloat(strFmt("Game.Player%d.RadialMenuSpeed", i), &g_PlayerExtCfg[j].radialmenuspeed, 0.f, 10.f);
		configRegisterFloat(strFmt("Game.Player%d.CrosshairSway", i), &g_PlayerExtCfg[j].crosshairsway, 0.f, 10.f);
		configRegisterInt(strFmt("Game.Player%d.CrouchMode", i), &g_PlayerExtCfg[j].crouchmode, 0, CROUCHMODE_TOGGLE_ANALOG);
		configRegisterInt(strFmt("Game.Player%d.ExtendedControls", i), &g_PlayerExtCfg[j].extcontrols, 0, 1);
		configRegisterUInt(strFmt("Game.Player%d.CrosshairColour", i), &g_PlayerExtCfg[j].crosshaircolour, 0, 0xFFFFFFFF);
		configRegisterUInt(strFmt("Game.Player%d.CrosshairSize", i), &g_PlayerExtCfg[j].crosshairsize, 0, 4);
		configRegisterInt(strFmt("Game.Player%d.CrosshairHealth", i), &g_PlayerExtCfg[j].crosshairhealth, 0, CROSSHAIR_HEALTH_ON_WHITE);
		configRegisterInt(strFmt("Game.Player%d.UseKeyReloads", i), &g_PlayerExtCfg[j].usereloads, 0, 0);
		configRegisterFloat(strFmt("Game.Player%d.JumpHeight", i), &g_PlayerExtCfg[j].jumpheight, 0.f, 20.f);
	}
}
