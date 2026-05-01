#include <stdlib.h>
#include <stdio.h>
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
#include "loader_pdbase.h"
#include "catalog_mgr_heads.h"
#include "game/stagetable.h"
#include "game/chr.h"

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

s32 g_FileAutoSelect = -1;

extern s32 g_StageNum;

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
	p2pInit();
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

	romdataInit();

	// C-1: ROM hash cache — verify ROM integrity and cache the hash so
	// mismatches (ROM replaced or corrupted) are logged on future boots.
	// Returns 1=verified/first-run, 0=hash changed, -1=I/O error.
	// We proceed in all cases; this is an integrity check, not a gate.
	catalogCacheVerifyRom(g_RomName, NULL);

	netInit();

	g_ValidGbcRomFound = romdataCheckGbcRom();

	gameInit();

	// Dynamic mod manager: scans mods/ directory, loads manifests, applies config.
	modmgrInit();

	// Model catalog: cache metadata from g_HeadsAndBodies (no heap needed).
	// Actual model validation is deferred to catalogValidateAll() after heap init.
	catalogInit();

	// Phase 2: Initialise heap-allocated stage table (copied from static initialiser).
	// Must run before assetCatalogRegisterBaseGame() which reads g_Stages[].
	stageTableInit();

	// D3R: Asset Catalog — string-keyed resolution for all game assets.
	// 1. Allocate hash table and entry pool
	// 2. Register base game assets (stages, bodies, heads) with "base:" IDs
	// 3. Scan mod _components/ directories and register INI-described assets
	assetCatalogInit();
	assetCatalogRegisterBaseGame();
	{
		const char *modsdir = modmgrGetModsDir();
		if (modsdir) {
			assetCatalogScanComponents(modsdir);
			// D3R-8: Also scan flat bot_variants/ for user-created presets
			assetCatalogScanBotVariants(modsdir);
		}
	}
	sysLogPrintf(LOG_NOTE, "Asset Catalog: %d entries registered", assetCatalogGetCount());

	// Catalog Gate 3 F1: head manager init. Builds the parallel
	// s_Heads[152] mirror from g_HeadsAndBodies[] for the parity-period
	// bridge. F12 swaps the data source to base/heads.pdbase pool.
	catalogManagerHeadInit();

	// S484 F13: scan base/*.pdbase + populate the catalog manager's typed
	// weapon pools. Manager accessors are pool-backed once
	// loaderPdbaseBuildWeaponManager succeeds. Parity check from F12 was
	// retired -- g_Weapons[] no longer exists to compare against.
	{
		loader_pdbase_result_t pdb_result;
		loaderPdbaseScan("base", &pdb_result);
		if (pdb_result.weapons_registered > 0) {
			loaderPdbaseBuildWeaponManager();
		}
	}

	// Phase 8: Build O(1) runtime→catalog-ID caches (mp body/head, stage, weapon, model).
	// Must run after all catalog entries are registered.
	catalogBuildRuntimeCaches();

	// C-4 prerequisite: build filenum/texnum/animnum/soundnum → pool-index reverse-index.
	// Must run after full catalog population (base game + mod scan).
	// catalogGetFileOverride() etc. return NULL until this is called.
	catalogLoadInit();

	// D3R-6: Restore per-component enable state from mods/.modstate.
	// Must run after scan so entries exist in the catalog to be disabled.
	modmgrLoadComponentState();

	// Signal modmgr that catalog is populated — rebuilds accessor caches
	// so modmgrGetArena() etc. read from catalog instead of static arrays.
	modmgrCatalogChanged();

	atexit(cleanup);

	bootCreateSched();

	g_OsMemSize = osGetMemSize();

	g_MempHeapSize = g_OsMemSize;
	g_MempHeap = sysMemZeroAlloc(g_MempHeapSize);
	if (!g_MempHeap) {
		sysFatalError("Could not alloc %u bytes for memp heap.", g_MempHeapSize);
	}

	sysLogPrintf(LOG_NOTE, "memp heap at %p - %p", g_MempHeap, g_MempHeap + g_MempHeapSize);
	sysLogPrintf(LOG_NOTE, "rom  file at %p - %p", g_RomFile, g_RomFile + g_RomFileSize);

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
