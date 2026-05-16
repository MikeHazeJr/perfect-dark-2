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
 *
 *   --debug-spawn-at x,y,z,room
 *       Post-setupCreateProps hook: teleports player 0 to the given
 *       world coordinate + room id on the first frame after stage
 *       load (g_Vars.lvframenum >= 4).  Used by smoke tests that need
 *       deterministic positioning (wall-jump regression, physics
 *       coverage).  Four comma-separated tokens: x, y, z, room (all
 *       parsed as integers via strtol with float coercion for x/y/z;
 *       room is s16 / RoomNum).  One-shot per boot.
 *
 *   --launch-load-agent <name>
 *       Post-saveInit hook: invokes saveLoadAgent(name) on the first
 *       frame after stage load (g_Vars.lvframenum >= 4) to read the
 *       pre-staged <savedir>/agent_<safeName>.json fixture into
 *       g_GameFile.  Provides smoke-test coverage of the saveLoadAgent
 *       wire-format path -- the production code has zero call-sites
 *       today (Agent Select routes through gamefileLoad, not
 *       saveLoadAgent; see port/PHASE_D5_PLAN.md:89 for the intended
 *       wiring).  Name is limited to 63 chars (buffer size 64) and
 *       sanitized inside buildSavePath().  One-shot per boot.
 *
 *   --listen-bind <port>
 *       Post-netInit hook: invokes netStartServer(port, NET_MAX_CLIENTS)
 *       on the first mainTick frame so the client boots straight into a
 *       listen-server bound on the requested UDP port -- bypassing the
 *       menu nav (Multiplayer -> Host Game) that production code requires.
 *       Provides smoke-test coverage of the listen-host bind path. Pair
 *       with the natural --host log routing so a sister --connect-host
 *       client process writes to a distinct log. Port is parsed via
 *       strtol; out-of-range values (<=0 or >0xFFFF) leave the latch off
 *       with a WARNING. One-shot per boot.
 *
 *   --connect-host <addr>:<port>
 *       Post-netInit hook: invokes netStartClient(addr) on the first
 *       mainTick frame so the client boots straight into a connect-to-
 *       host attempt -- bypassing the menu nav (Multiplayer -> Join Game
 *       -> Enter Connect Code) that production code requires. addr is
 *       passed through unchanged to netParseAddr, so both "ip:port" and
 *       bare hostname forms work. Provides smoke-test coverage of the
 *       client connect path. One-shot per boot.
 *
 *   --dump-swarm-state <path>
 *       Track 2c (c3807, 2026-05-16): post-dispatch hook for the GPU
 *       swarm benchmark scenarios. Once per 60 frames (~1 Hz) after
 *       the player prop is positioned, calls
 *       swarmGpuReadbackTextureRows(0, 5, ...) to extract all 5 rows
 *       of the READ-side state texture (pos / vel / surface_up /
 *       AI ints / range) for SWARM_GPU_MAX bots, and appends the
 *       result to the configured file with a header of magic
 *       "PDSWARMv1", frame index, bot count, and row count. Stops
 *       after SWARM_DUMP_MAX_FRAMES (10) dumps so the file size is
 *       bounded at ~3.2 MB. Path is taken as-is and passed to fopen
 *       in binary mode; relative paths resolve to the process CWD.
 *       One-shot CLI capture; no live debug-key. Cheap no-op when a
 *       GPU swarm scenario is not active or the GL state texture is
 *       not armed. See context/designs/in-flight/gpu-swarm-and-test-
 *       scenarios.md Track 2c for the file format spec.
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

/* c115 (2026-05-14): --debug-spawn-at one-shot latch. Parsed at boot,
 * fired on the first frame where the player prop exists. Mirrors the
 * --debug-mount-bike pattern. RoomNum is s16 in src/include/types.h
 * but we store as s32 to avoid pulling types.h into this file; the
 * tick path narrows at the chrMoveToPos call. */
static s32         g_BootSpawnAtPending = 0;
static f32         g_BootSpawnAtX = 0.0f;
static f32         g_BootSpawnAtY = 0.0f;
static f32         g_BootSpawnAtZ = 0.0f;
static s32         g_BootSpawnAtRoom = 0;

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

/* c118 (2026-05-15): --launch-load-agent one-shot. CLI fast-path that
 * invokes saveLoadAgent(name) directly at boot, mirroring the
 * --debug-mount-bike / --debug-spawn-at deferred-tick pattern. Provides
 * smoke-test coverage of the saveLoadAgent wire-format path without
 * scripted Agent Select menu nav (which routes through gamefileLoad,
 * not saveLoadAgent -- see port/PHASE_D5_PLAN.md:89 for the intended
 * wiring that was planned but never executed). The fixture is the v2
 * agent JSON pre-staged via run.ps1::Copy-SmokeFixtures. saveInit()
 * fires synchronously in main() at line ~957, well before any deferred
 * tick, so the save dir is wired when this fires. One-shot per boot. */
static s32         g_BootLoadAgentArmed = 0;
static char        g_BootLoadAgentName[64] = {0};

/* c118 (2026-05-15): --listen-bind <port> one-shot. CLI fast-path that
 * invokes netStartServer(port, NET_MAX_CLIENTS) on the first mainTick
 * frame so the client boots straight into a listen-server bound on the
 * requested UDP port -- bypassing the menu nav (Multiplayer -> Host
 * Game) that production code requires. Sister test fast-path for the
 * connectivity pillar's two-process peer-link smoke. Deferred to a tick
 * rather than fired from bootApplyCliFastPaths so netInit's threaded
 * boot-pool path has fully drained (g_NetInit is set inside the worker)
 * and so the smoke harness's SMOKE: scenario= sentinel lands before the
 * NET: created server log line -- which keeps assertion ordering
 * deterministic for the two-process smoke test reader.
 * One-shot per boot. */
static s32         g_BootListenBindArmed = 0;
static s32         g_BootListenBindPort  = 0;

/* c118 (2026-05-15): --connect-host <addr>:<port> one-shot. CLI
 * fast-path that invokes netStartClient(addr) on the first mainTick
 * frame so the client boots straight into a connect-to-host attempt --
 * bypassing the menu nav (Multiplayer -> Join Game -> Enter Connect
 * Code) that production code requires. Sister to --listen-bind for the
 * two-process peer-link smoke. addr is captured into a static buffer
 * (NET_MAX_ADDR = 64 in port/include/net/net.h, but we keep this file
 * independent of net.h's constants and use the same 64-byte limit
 * directly). Deferred to mainTick for the same reason --listen-bind is:
 * netInit must be complete before netStartClient is invoked, and
 * deferring lets the sentinel land before the connect log line.
 * One-shot per boot. */
static s32         g_BootConnectHostArmed   = 0;
static char        g_BootConnectHostAddr[64] = {0};

/* Track 2c (c3807, 2026-05-16): --dump-swarm-state <path> one-shot.
 * CLI fast-path that periodically extracts the GPU swarm state texture
 * and appends it to a file for offline inspection / replay. Path is
 * captured into a static buffer; the actual readback runs inside
 * mainTick once g_Vars.lvframenum >= 4 (player positioned), throttled
 * to 1 Hz via a frame counter. Latches off after SWARM_DUMP_MAX_FRAMES
 * dumps so the file is bounded. Cheap no-op when the flag wasn't on
 * the command line OR when no GPU swarm scenario is active OR when
 * the state texture isn't armed (driver missing glBindImageTexture).
 *
 * File format (little-endian, frame N is the N-th appended block):
 *   bytes [0..9):    magic = "PDSWARMv1" (9 ASCII chars, NO terminator)
 *   bytes [9..10):   pad byte (zero) for 4-byte alignment of u32 fields
 *   bytes [10..14):  reserved (zero)
 *   bytes [14..16):  pad
 *   bytes [16..20):  frame_idx (u32, dispatch counter monotonic 0..)
 *   bytes [20..24):  count     (u32, SWARM_GPU_MAX = 4096; mirrors the
 *                                texture width / bots-per-row capacity)
 *   bytes [24..28):  row_count (u32, 5; pos / vel / surface_up / AI /
 *                                range)
 *   bytes [28..32):  pad (zero)
 *   bytes [32..end): row_count * count * 4 floats, in row-major order
 *                    (row 0 then row 1 ...; each row is `count` 4-float
 *                    texels). Total per block = 5 * 4096 * 16 = 320 KB +
 *                    32 B header = 320032 bytes. After 10 dumps the
 *                    file is ~3.2 MB.
 *
 * Consumers should fread the 32 B header, validate the magic prefix,
 * then fread 5 * 4096 * 16 B of float data. A future replay / sync
 * tool maps the 4 ints out of row 3 via memcpy into a u32 + 3 spare. */
#define SWARM_DUMP_MAX_FRAMES   10
#define SWARM_DUMP_INTERVAL_FRAMES  60   /* ~1 Hz at 60 fps */
static s32         g_BootDumpSwarmArmed   = 0;
static char        g_BootDumpSwarmPath[260] = {0};
static s32         g_BootDumpSwarmDumpsLeft = 0;
static s32         g_BootDumpSwarmTickCounter = 0;

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

/* c115 (2026-05-14): chrMoveToPos lives in src/game/chraction.c; declare
 * an extern shim rather than #include "game/chraction.h" to keep this
 * PC-port file outside the chr*.h dependency surface (which transitively
 * drags in PR/gbi.h and N64 micro-code defines). Signature must match
 * src/include/game/chraction.h:208 exactly. */
extern bool chrMoveToPos(struct chrdata *chr, struct coord *pos, RoomNum *rooms, f32 angle, bool ignorebg);

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

/* c115 (2026-05-14): Arm the --debug-spawn-at one-shot. Parses the
 * comma-separated tokens captured at CLI-parse time into x/y/z/room
 * floats + s32 and sets g_BootSpawnAtPending. The actual teleport
 * runs inside mainTick once the player prop exists; see
 * bootDebugSpawnAtTick() below. Malformed input (wrong token count,
 * unparseable numbers) leaves the latch off and emits a WARNING. */
static void bootApplyDebugSpawnAt(const char *arg)
{
	if (!arg || !arg[0]) {
		return;
	}

	/* Local copy so strtok_r can mutate it. */
	char buf[128];
	strncpy(buf, arg, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	/* strtok is fine here -- we run on the main thread before any
	 * subsystem has spun up worker threads, and we don't nest calls. */
	const char *tokX = strtok(buf, ",");
	const char *tokY = strtok(NULL, ",");
	const char *tokZ = strtok(NULL, ",");
	const char *tokR = strtok(NULL, ",");
	if (!tokX || !tokY || !tokZ || !tokR) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-spawn-at expects 4 comma-separated tokens (x,y,z,room); got: '%s'",
			arg);
		return;
	}

	g_BootSpawnAtX    = (f32)strtod(tokX, NULL);
	g_BootSpawnAtY    = (f32)strtod(tokY, NULL);
	g_BootSpawnAtZ    = (f32)strtod(tokZ, NULL);
	g_BootSpawnAtRoom = (s32)strtol(tokR, NULL, 0);
	g_BootSpawnAtPending = 1;

	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-spawn-at armed: pos=(%f,%f,%f) room=%d",
		g_BootSpawnAtX, g_BootSpawnAtY, g_BootSpawnAtZ, g_BootSpawnAtRoom);
}

/* c118 (2026-05-15): Arm the --launch-load-agent one-shot. Captures
 * the agent name into a static buffer; the actual saveLoadAgent call
 * runs inside mainTick once g_Vars.lvframenum >= 4 (same gate as the
 * other deferred ticks). Empty / missing arg leaves the latch off.
 * Name longer than 63 chars is rejected with a WARNING (the file-side
 * limit is SAVE_NAME_MAX but the CLI capture buffer is 64 to keep this
 * file independent of savefile.h's constants). */
static void bootApplyLaunchLoadAgent(const char *arg)
{
	if (!arg || !arg[0]) {
		return;
	}

	const size_t maxLen = sizeof(g_BootLoadAgentName) - 1;
	if (strlen(arg) > maxLen) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --launch-load-agent name too long (max %zu chars); got: '%s'",
			maxLen, arg);
		return;
	}

	strncpy(g_BootLoadAgentName, arg, maxLen);
	g_BootLoadAgentName[maxLen] = '\0';
	g_BootLoadAgentArmed = 1;

	sysLogPrintf(LOG_NOTE,
		"BOOT: --launch-load-agent armed: name='%s'",
		g_BootLoadAgentName);
}

/* c118 (2026-05-15): Arm the --listen-bind <port> one-shot. Parses the
 * port string into an s32 and validates it's in the legal UDP range.
 * The actual netStartServer call runs inside mainTick once g_NetInit
 * is set; see bootListenBindTick() below. Out-of-range values leave
 * the latch off and emit a WARNING. */
static void bootApplyListenBind(const char *arg)
{
	if (!arg || !arg[0]) {
		return;
	}

	char *end = NULL;
	long portval = strtol(arg, &end, 0);
	if (!end || *end != '\0' || portval <= 0 || portval > 0xFFFF) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --listen-bind expects a UDP port in 1..65535; got: '%s'",
			arg);
		return;
	}

	g_BootListenBindPort = (s32)portval;
	g_BootListenBindArmed = 1;

	sysLogPrintf(LOG_NOTE,
		"BOOT: --listen-bind armed: port=%d",
		g_BootListenBindPort);
}

/* c118 (2026-05-15): Arm the --connect-host <addr>:<port> one-shot.
 * Captures the address+port string into a static buffer; the actual
 * netStartClient call runs inside mainTick once g_NetInit is set; see
 * bootConnectHostTick() below. Empty / missing arg leaves the latch
 * off. Buffer is 64 bytes (matches NET_MAX_ADDR in port/include/net/
 * net.h but we keep this file independent of that header's constants). */
static void bootApplyConnectHost(const char *arg)
{
	if (!arg || !arg[0]) {
		return;
	}

	const size_t maxLen = sizeof(g_BootConnectHostAddr) - 1;
	if (strlen(arg) > maxLen) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --connect-host address too long (max %zu chars); got: '%s'",
			maxLen, arg);
		return;
	}

	strncpy(g_BootConnectHostAddr, arg, maxLen);
	g_BootConnectHostAddr[maxLen] = '\0';
	g_BootConnectHostArmed = 1;

	sysLogPrintf(LOG_NOTE,
		"BOOT: --connect-host armed: addr='%s'",
		g_BootConnectHostAddr);
}

/* Track 2c (c3807, 2026-05-16): Arm the --dump-swarm-state <path>
 * one-shot. Captures the path into the static buffer and primes the
 * dump countdown to SWARM_DUMP_MAX_FRAMES. The actual readback +
 * append runs inside bootDumpSwarmStateTick() once the player prop
 * is positioned and the GPU dispatch loop has had a chance to populate
 * the texture. Path strings longer than the buffer or empty input
 * leave the latch off with a WARNING. */
static void bootApplyDumpSwarmState(const char *arg)
{
	if (!arg || !arg[0]) {
		return;
	}

	const size_t maxLen = sizeof(g_BootDumpSwarmPath) - 1;
	if (strlen(arg) > maxLen) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --dump-swarm-state path too long (max %zu chars); got: '%s'",
			maxLen, arg);
		return;
	}

	strncpy(g_BootDumpSwarmPath, arg, maxLen);
	g_BootDumpSwarmPath[maxLen] = '\0';
	g_BootDumpSwarmArmed       = 1;
	g_BootDumpSwarmDumpsLeft   = SWARM_DUMP_MAX_FRAMES;
	g_BootDumpSwarmTickCounter = 0;

	sysLogPrintf(LOG_NOTE,
		"BOOT: --dump-swarm-state armed: path='%s' max_dumps=%d interval=%d_frames",
		g_BootDumpSwarmPath, SWARM_DUMP_MAX_FRAMES, SWARM_DUMP_INTERVAL_FRAMES);
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
	bootApplyDebugSpawnAt(sysArgGetString("--debug-spawn-at"));
	bootApplyLaunchLoadAgent(sysArgGetString("--launch-load-agent"));
	bootApplyListenBind(sysArgGetString("--listen-bind"));
	bootApplyConnectHost(sysArgGetString("--connect-host"));
	bootApplyDumpSwarmState(sysArgGetString("--dump-swarm-state"));
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

/* c115 (2026-05-14): Called once per frame from pdmain.c's mainTick
 * when the --debug-spawn-at one-shot is armed. Teleports the current
 * player (player 0) to the JSON-parsed (x,y,z,room) using chrMoveToPos
 * once the stage setup has placed the player prop.
 *
 * Gates (mirror the bike-mount hook):
 *   - g_BootSpawnAtPending must be set
 *   - g_Vars.lvframenum >= 4 (load black frame out of the way, player
 *     prop spawned by setupCreateProps)
 *   - g_Vars.players[0] must be alive and have a valid prop->chr
 *
 * One-shot: clears the latch regardless of chrMoveToPos's success/fail
 * so a subsequent stage change does not re-teleport. */
s32 bootDebugSpawnAtTick(void)
{
	if (!g_BootSpawnAtPending) {
		return 0;
	}
	if (g_Vars.lvframenum < 4) {
		return 0;
	}
	if (!g_Vars.players[0] || !g_Vars.players[0]->prop || !g_Vars.players[0]->prop->chr) {
		return 0;
	}

	struct prop *prop = g_Vars.players[0]->prop;
	struct chrdata *chr = prop->chr;

	struct coord target;
	target.x = g_BootSpawnAtX;
	target.y = g_BootSpawnAtY;
	target.z = g_BootSpawnAtZ;

	/* chrMoveToPos wants a RoomNum[] terminated by -1 (-2 in some
	 * call sites means "any room"); the AI-script call site at
	 * chraicommands.c:5215 uses [room, -1] which is the canonical
	 * single-room form. */
	RoomNum rooms[2];
	rooms[0] = (RoomNum)g_BootSpawnAtRoom;
	rooms[1] = -1;

	/* Pass angle=0.0f -- the smoke tests that use this flag set up
	 * their own look state via subsequent scripted input. The force
	 * flag (final arg) is true so chrAdjustPosForSpawn doesn't refuse
	 * the teleport because of bg-collision near the target. */
	setCurrentPlayerNum(0);
	bool ok = chrMoveToPos(chr, &target, rooms, 0.0f, true);

	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-spawn-at consumed: prop=%p stagenum=0x%02x result=%s pos=(%f,%f,%f) room=%d",
		(void *)prop, (u32)g_Vars.stagenum, ok ? "OK" : "FAILED",
		g_BootSpawnAtX, g_BootSpawnAtY, g_BootSpawnAtZ, g_BootSpawnAtRoom);

	g_BootSpawnAtPending = 0;
	return 1;
}

/* c118 (2026-05-15): Called once per frame from pdmain.c's mainTick
 * when the --launch-load-agent one-shot is armed. Invokes
 * saveLoadAgent(name) which reads the pre-staged JSON fixture from
 * <savedir>/agent_<safeName>.json and populates g_GameFile.
 *
 * Gates (mirror the other deferred-tick hooks):
 *   - g_BootLoadAgentArmed must be set
 *   - g_Vars.lvframenum >= 4 (deferred just like spawn-at / mount-bike;
 *     even though saveLoadAgent doesn't need stage/player props,
 *     deferring keeps the harness assertion ordering deterministic so
 *     the smoke test sees the SAVE: log lines after the SAVE: initialized
 *     line from saveInit())
 *
 * saveLoadAgent returns 0 on success, -1 on failure. Logs OK or FAILED
 * based on the return; saveLoadAgent itself emits its own SAVE: log
 * lines (loaded / failed to load / refusing to load) which the smoke
 * test can additionally assert on.
 *
 * One-shot: clears the latch regardless of result so a subsequent
 * stage change does not re-fire. */
s32 bootLaunchLoadAgentTick(void)
{
	if (!g_BootLoadAgentArmed) {
		return 0;
	}
	if (g_Vars.lvframenum < 4) {
		return 0;
	}

	s32 result = saveLoadAgent(g_BootLoadAgentName);

	sysLogPrintf(LOG_NOTE,
		"BOOT: --launch-load-agent consumed: name='%s' result=%s",
		g_BootLoadAgentName, result == 0 ? "OK" : "FAILED");

	g_BootLoadAgentArmed = 0;
	return 1;
}

/* c118 (2026-05-15): Called once per frame from pdmain.c's mainTick
 * when the --listen-bind one-shot is armed. Invokes netStartServer
 * with the parsed port and NET_MAX_CLIENTS so the client boots
 * straight into a listen-server bound on the requested UDP port --
 * sister to bootConnectHostTick() for the two-process peer-link
 * smoke. The actual ENet bind log line ("NET: created server on port
 * %u") comes from netStartServer itself; the BOOT: consumed line is
 * the smoke-runner's wait-for marker.
 *
 * Gates:
 *   - g_BootListenBindArmed must be set
 *   - g_NetInit must be true (netInit() drained inside the boot pool's
 *     bootRunCatalogWork worker; by the time mainTick fires the boot
 *     overlay shutdown is done and the worker has joined). If the
 *     binary was launched with --no-net the latch will sit armed
 *     forever -- intentional: --no-net + --listen-bind is nonsense
 *     and the operator should be told via a single WARNING. We log
 *     that warning once on the first tick where netInit is missing.
 *
 * One-shot: clears the latch regardless of netStartServer's result so
 * a subsequent stage change does not re-bind. */
s32 bootListenBindTick(void)
{
	if (!g_BootListenBindArmed) {
		return 0;
	}

	extern s32 g_NetInit;
	if (!g_NetInit) {
		static s32 warned = 0;
		if (!warned) {
			sysLogPrintf(LOG_WARNING,
				"BOOT: --listen-bind cannot fire: g_NetInit is false (likely --no-net was also passed)");
			warned = 1;
		}
		/* Hold the latch; if netInit eventually completes we'll fire.
		 * If --no-net was passed the latch sits forever, which is fine
		 * for a smoke test -- the assertion will catch the missing
		 * NET: created server log. */
		return 0;
	}

	extern s32 netStartServer(u16 port, s32 maxclients);
	extern s32 g_NetMaxClients;
	s32 rc = netStartServer((u16)g_BootListenBindPort, g_NetMaxClients);

	sysLogPrintf(LOG_NOTE,
		"BOOT: --listen-bind consumed: port=%d maxclients=%d result=%s rc=%d",
		g_BootListenBindPort, g_NetMaxClients,
		rc == 0 ? "OK" : "FAILED", (int)rc);

	g_BootListenBindArmed = 0;
	return 1;
}

/* c118 (2026-05-15): Called once per frame from pdmain.c's mainTick
 * when the --connect-host one-shot is armed. Invokes netStartClient
 * with the captured "addr:port" string so the client boots straight
 * into a connect-to-host attempt -- sister to bootListenBindTick() for
 * the two-process peer-link smoke. The actual ENet connect log line
 * ("NET: connecting to %s...") comes from netStartClient itself; the
 * BOOT: consumed line is the smoke-runner's wait-for marker.
 *
 * Gates:
 *   - g_BootConnectHostArmed must be set
 *   - g_NetInit must be true (same constraint as bootListenBindTick).
 *
 * One-shot: clears the latch regardless of netStartClient's result so
 * a subsequent stage change does not re-connect. */
s32 bootConnectHostTick(void)
{
	if (!g_BootConnectHostArmed) {
		return 0;
	}

	extern s32 g_NetInit;
	if (!g_NetInit) {
		static s32 warned = 0;
		if (!warned) {
			sysLogPrintf(LOG_WARNING,
				"BOOT: --connect-host cannot fire: g_NetInit is false (likely --no-net was also passed)");
			warned = 1;
		}
		return 0;
	}

	extern s32 netStartClient(const char *addr);
	s32 rc = netStartClient(g_BootConnectHostAddr);

	sysLogPrintf(LOG_NOTE,
		"BOOT: --connect-host consumed: addr='%s' result=%s rc=%d",
		g_BootConnectHostAddr, rc == 0 ? "OK" : "FAILED", (int)rc);

	g_BootConnectHostArmed = 0;
	return 1;
}

/* Track 2c (c3807, 2026-05-16): --dump-swarm-state deferred tick.
 *
 * Called once per frame from pdmain.c's mainTick when the latch is
 * armed. Throttles itself to SWARM_DUMP_INTERVAL_FRAMES so the file
 * grows at ~1 Hz. On fire:
 *   1. Calls swarmGpuReadbackTextureRows(0, 5, ...) to pull the full
 *      READ-side state texture.
 *   2. Appends a 32 B header + 320 KB float payload to the configured
 *      path (binary append; file is created on the first write).
 *   3. Decrements the dumps-left counter; clears the latch once zero.
 *
 * Gates:
 *   - g_BootDumpSwarmArmed must be set
 *   - g_Vars.lvframenum >= 4 (player positioned, stage settled)
 *   - swarmGpuReadbackTextureRows must return a non-zero texel count
 *     (otherwise the GPU swarm isn't armed; skip silently this frame)
 *
 * The throttle counter increments every gated frame (not every armed
 * frame) so the spacing between dumps reflects gameplay frames, not
 * wall-clock. At 60 fps that's 1 second between dumps. Returns 1 on
 * any decision (dump fired OR throttled), 0 if the latch was off. */
s32 bootDumpSwarmStateTick(void)
{
	extern int swarmGpuReadbackTextureRows(int row_start, int row_count,
		float *out_buf, int out_capacity);

	if (!g_BootDumpSwarmArmed) {
		return 0;
	}
	if (g_Vars.lvframenum < 4) {
		return 0;
	}

	g_BootDumpSwarmTickCounter++;
	if (g_BootDumpSwarmTickCounter < SWARM_DUMP_INTERVAL_FRAMES) {
		return 1;
	}
	g_BootDumpSwarmTickCounter = 0;

	/* SWARM_GPU_MAX = TESTSCEN_SWARM_MAX_COUNT = 4096. 5 rows * 4096
	 * texels * 4 floats = 81920 floats = 320 KB. Stack would be tight;
	 * use a static so we don't blow the 8 MB main stack. The buffer
	 * is rewritten on every dump call so no aliasing between dumps. */
	static float s_DumpBuf[5 * 4096 * 4];
	const int got = swarmGpuReadbackTextureRows(0, 5,
		s_DumpBuf, (int)(sizeof(s_DumpBuf) / sizeof(float)));
	if (got <= 0) {
		/* GPU swarm not armed this frame (e.g. scenario not launched
		 * yet, or compute unavailable). Silently skip; we'll try again
		 * next interval. */
		return 1;
	}

	FILE *f = fopen(g_BootDumpSwarmPath, "ab");
	if (!f) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --dump-swarm-state fopen failed for '%s'; disarming",
			g_BootDumpSwarmPath);
		g_BootDumpSwarmArmed = 0;
		return 1;
	}

	/* 32 B header. Magic is 9 ASCII chars + 1 pad + 22 B reserved/data
	 * to land the payload at offset 32 (16-byte-aligned for RGBA32F).
	 * Layout MUST match the docblock above g_BootDumpSwarmPath. */
	unsigned char header[32];
	memset(header, 0, sizeof(header));
	memcpy(header + 0, "PDSWARMv1", 9);
	const u32 frame_idx = (u32)g_Vars.lvframenum;
	const u32 count_w   = 4096;   /* SWARM_GPU_MAX; mirrors texture width */
	const u32 row_count = 5;
	memcpy(header + 16, &frame_idx, sizeof(u32));
	memcpy(header + 20, &count_w,   sizeof(u32));
	memcpy(header + 24, &row_count, sizeof(u32));

	size_t hwrite = fwrite(header, 1, sizeof(header), f);
	size_t pwrite = fwrite(s_DumpBuf, sizeof(float), (size_t)got * 4, f);
	fclose(f);

	const s32 dump_no = SWARM_DUMP_MAX_FRAMES - g_BootDumpSwarmDumpsLeft + 1;
	sysLogPrintf(LOG_NOTE,
		"BOOT: --dump-swarm-state dump %d/%d wrote frame=%u texels=%d "
		"header_bytes=%zu payload_floats=%zu path='%s'",
		(int)dump_no, (int)SWARM_DUMP_MAX_FRAMES,
		(unsigned)frame_idx, got, hwrite, pwrite,
		g_BootDumpSwarmPath);

	g_BootDumpSwarmDumpsLeft--;
	if (g_BootDumpSwarmDumpsLeft <= 0) {
		sysLogPrintf(LOG_NOTE,
			"BOOT: --dump-swarm-state consumed: max_dumps reached; disarming");
		g_BootDumpSwarmArmed = 0;
	}
	return 1;
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
