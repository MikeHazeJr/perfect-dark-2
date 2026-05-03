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
#include "loader_walker.h"
#include "catalog_mgr_heads.h"
#include "catalog_mgr_bodies.h"
#include "catalog_mgr_arenas.h"
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

/* Phase 2 fix #6 (input-menu pillar, 2026-05-01): when set, the HUD draws
 * an overlay showing the current interaction-cast half-angle (degrees)
 * + range so Mike can dial fix #5's tightening empirically. Settings ->
 * Debug Flags exposes the toggle + a slider. Default off. */
s32 g_InteractCastDebugDraw = 0;

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

	/* Phase 3 Pass A.2 (2026-05-02): first-launch ROM file extraction.
	 * Walks the ROM file table and writes each non-empty file slot to
	 * data/<romid>/files/<name>.bin.  Idempotent (skips files already
	 * extracted with matching size).  Subsequent Phase 3 slices migrate
	 * per-class catalog bindings from RomProvider to FileProvider so
	 * the runtime reads from disk; once every class is on disk Pass C
	 * retires the runtime ROM mapping entirely.
	 *
	 * Order: AFTER romdataInit (g_RomFile + fileSlots populated) and
	 * BEFORE assetCatalogScanComponents (so extracted bytes are pure
	 * ROM, not mod-overridden).  Pass A.4 will add SHA-256 verification
	 * + quarantine + re-extract on top of the size-only idempotency
	 * check that ships here. */
	romExtractAllFiles();

	/* Phase 3 Pass A.4 (2026-05-02): hash-verify-on-launch self-heal.
	 * Scans data/<romid>/files/ sidecars, checks each .bin against its
	 * stored SHA-256, quarantines + re-extracts mismatches.  Emits
	 * LOUDFAIL.LOAD on any corruption found.  Idempotent and cheap on
	 * a clean install. */
	romExtractVerifyAll();

	/* Phase 3 Pass B Slices 2/5/6/8/11 (2026-05-02): segment extraction.
	 * Walk every loaded ROM segment (sfxctl/sfxtbl, seqctl/seqtbl,
	 * sequences, animations, fonts, mp* tables, textures, copyright)
	 * and write to data/<romid>/segs/<name>.bin.  romdataInitSegment
	 * already prefers the per-romid path on subsequent boots so the
	 * runtime reads segments from disk.  This covers Slice 2 (SFX
	 * bank), Slice 5 (character sounds), Slice 6 (animations), Slice 8
	 * (prop sounds), Slice 11 (music sequences) in one infrastructure
	 * push because they all share the segment loader path.
	 *
	 * Note: must run AFTER romdataInit (segments populated) but the
	 * order vs assetCatalogRegisterBaseGame doesn't matter for segments
	 * because segments are not catalog-bound at the per-asset level
	 * (they're loaded en bloc by the segment loader). */
	romExtractAllSegments();
	romExtractVerifyAllSegments();

	/* Phase 3 Pass D (2026-05-02): emit the aggregated boot integrity
	 * report.  Single LOG_NOTE line summarising files+segs verified,
	 * re-extracted, and unrecoverable.  If non-zero corruption was
	 * found, defers a system toast that romExtractToastDrain (called
	 * after gameInit) will surface to the player.  Pure read of the
	 * counters populated by the verify pair above; safe to run before
	 * the Pass C ROM release. */
	romExtractEmitBootIntegrityReport();

	/* Phase 3 Pass C (2026-05-02): drop the in-memory ROM mapping.
	 * Pass A.2/A.4 + Pass B segment extract/verify guarantee every byte
	 * needed by the runtime is already on disk under data/<romid>/.
	 * romdataReleaseRom migrates SRC_ROM segments to heap-backed copies
	 * loaded from disk, NULLs out the lazy fileSlot pointers that
	 * referenced g_RomFile + ofs, then frees g_RomFile.  Subsequent
	 * romdataFileLoad calls route SRC_UNLOADED slots through the
	 * per-romid extracted file path; the legacy SRC_ROM fallback
	 * LOUD-FAILs because g_RomFile is NULL.  Architectural finish line
	 * for catalog migration: the runtime never touches the ROM directly. */
	romdataReleaseRom();

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
	/* Catalog coverage audit (2026-05-01) Section 3.A closure: register
	 * the per-stage scene file IDs (bg / tile / pads / setup / mpsetup)
	 * carried on g_Stages[] as ASSET_MODEL entries with source_filenum
	 * binding. Without this, romdataFileLoad's catalogResolveFile path
	 * cannot route mod overrides for stage scene files (a player ship
	 * a custom map cannot replace its BG geometry, collision data, or
	 * mission setup script through the standard mod mechanism). See
	 * assetCatalogRegisterStageSceneFiles docblock for full rationale.
	 *
	 * Order: AFTER assetCatalogRegisterBaseGame so g_Stages is populated
	 * AND ASSET_MAP entries exist; BEFORE catalogLoadInit so the new
	 * source_filenum bindings land in the s_FilenumOverride[] reverse
	 * index on its single build pass. */
	assetCatalogRegisterStageSceneFiles();
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

	// Catalog Gate 3 Bodies F1: body manager init. Builds the parallel
	// s_Bodies[152] mirror from g_HeadsAndBodies[] for the parity-period
	// bridge. F12 swaps the data source to base/bodies.pdbase pool.
	catalogManagerBodyInit();

	// Catalog Gate 3 Arenas F1: arena manager init. Walks ASSET_ARENA
	// catalog rows and populates s_Arenas[47] for the parity-period
	// bridge. F12 swaps the data source to base/arenas.pdbase pool.
	catalogManagerArenaInit();

	// S484 F13: scan base/*.pdbase + populate the catalog manager's typed
	// weapon pools. Manager accessors are pool-backed once
	// loaderPdbaseBuildWeaponManager succeeds. Parity check from F12 was
	// retired -- g_Weapons[] no longer exists to compare against.
	//
	// Catalog Gate 3 F9: loaderPdbaseScan also looks for heads.pdbase
	// in the same dir; F12 makes this populate the heads pool too.
	//
	// Step 4 positioning (2026-05-03): this loaderPdbaseScan + the four
	// loaderPdbaseBuild*Manager calls below are the parity-bounded fallback
	// for the heavyweight loader_pdbase pool population (s_Weapons[] full
	// records, s_HeadsPool[], s_BodiesPool[], s_ArenasPool[]). Catalog row
	// registration moves onto the universal walker (loaderWalkerLoadAll)
	// later in this boot path; this block stays primary for pool fill until
	// Step 5 retires the .pdbase tier entirely.
	{
		loader_pdbase_result_t pdb_result;
		loaderPdbaseScan("base", &pdb_result);
		if (pdb_result.weapons_registered > 0) {
			loaderPdbaseBuildWeaponManager();
			/* S484-followup (2026-05-01): now that the loader has populated
			 * the weapon pool, register each weapon's hi_model / lo_model
			 * filenum as ASSET_MODEL so the bgun load chain's catalog
			 * lookup (catalogHandleByModelSourceFilenum) actually finds
			 * them. Without this, weapon-switch loads stall in FLUX
			 * forever and the fire path falls through to melee. See
			 * assetCatalogRegisterWeaponModelFiles docblock for full
			 * rationale. */
			assetCatalogRegisterWeaponModelFiles();
		}
		if (pdb_result.heads_registered > 0) {
			loaderPdbaseBuildHeadManager();
		}
		if (pdb_result.bodies_registered > 0) {
			loaderPdbaseBuildBodyManager();
		}
		if (pdb_result.arenas_registered > 0) {
			loaderPdbaseBuildArenaManager();
			/* Catalog Gate 3 Arenas F12: compare loader pool fields
			 * against ASSET_ARENA catalog rows (which were populated
			 * from g_MpArenas[] + s_ArenaNames[] + s_ArenaGroupMap[]).
			 * Mismatches log LOADER.PDBASE.ARENA.PARITY_FAIL: lines.
			 * F13 retires the parity bridge once Mike's playtest
			 * confirms PASS. */
			loaderPdbaseRunParityCheckArenas();
		}
	}

	/* Catalog universality pivot Step 1 (2026-05-02): emit per-asset
	 * .pdwpn / .pdmesh / .pdanim files at data/<romid>/<class>/.
	 *
	 * Reads from the loader_pdbase pool populated above; writes per-asset
	 * compound files in the universality format. Idempotent on subsequent
	 * boots (existing files skipped via size check). Per Mike's Q-5 ruling
	 * the parity check runs immediately after to validate the emit; the
	 * parity period closes at Step 5 when base/*.pdbase retires.
	 *
	 * Boot order: loader pools must be populated (above) AND the source
	 * .bin files must exist on disk (Pass A.2 ran earlier in romdataInit
	 * area). Both conditions hold here. */
	{
		s32 wpn_emitted = romExtractAllPdwpn(0);
		s32 mesh_emitted = romExtractAllPdmesh(0);
		s32 anim_emitted = romExtractAllPdanim(0);
		s32 parity_failures = romExtractParityCheckPdwpn();
		(void)wpn_emitted; (void)mesh_emitted; (void)anim_emitted;
		(void)parity_failures;
	}

	/* Catalog universality pivot Step 2 (2026-05-03): emit per-asset
	 * .pdhead / .pdbody / .pdarena JSON files plus the unified
	 * .pdscenario ZIP per Q-1 (one ZIP per arena's playable stage)
	 * at data/<romid>/heads/, /bodies/, /arenas/, /scenarios/.
	 *
	 * Reads from the loader_pdbase head/body/arena pools populated above
	 * AND from g_Stages[] (populated by stageTableInit earlier in this
	 * boot path). Per-arena scenario ZIPs reference the per-stage .bin
	 * files extracted by Pass A.2 / romExtractAllFiles.
	 *
	 * Idempotent: existing files skipped via size check. Per Mike's Q-5
	 * ruling each parity check runs immediately after to validate the
	 * emit; the parity period closes at Step 5 when base/*.pdbase
	 * retires. Ship Step 2 fully per Mike's "catalog must be COMPLETE"
	 * directive (2026-05-03). */
	{
		s32 head_emitted    = romExtractAllPdhead(0);
		s32 body_emitted    = romExtractAllPdbody(0);
		s32 arena_emitted   = romExtractAllPdarena(0);
		s32 head_failures   = romExtractParityCheckPdhead();
		s32 body_failures   = romExtractParityCheckPdbody();
		s32 arena_failures  = romExtractParityCheckPdarena();
		(void)head_emitted; (void)body_emitted; (void)arena_emitted;
		(void)head_failures; (void)body_failures; (void)arena_failures;
	}

	/* Catalog universality pivot Step 3a (2026-05-03): emit one .pdanim
	 * ZIP compound per chr animation entry in the segs/animations.bin
	 * lump, alongside the Step 1 weapon-animation .pdanim files (those
	 * are plain JSON, category="weapon_animation"; chr anims are ZIPs,
	 * category="character_animation").
	 *
	 * Reads from the byte-swapped in-memory animation segment + table
	 * pointers established by preprocessAnimations during romdataInit.
	 * Idempotent on subsequent boots (existing files skipped via size
	 * check). Per Mike's Q-3 ruling (2026-05-02): "Catalog is not
	 * complete unless it is COMPLETE. IT IS FOUNDATIONAL TO EVERYTHING."
	 *
	 * Boot order: must run AFTER romdataInit (segments + table pointers
	 * populated and byte-swapped) and AFTER romExtractAllSegments (so
	 * data/<romid>/segs/animations.bin exists on disk for self-heal
	 * round trips). Both conditions hold here. */
	{
		s32 chr_anim_emitted  = romExtractAllPdanimChr(0);
		s32 chr_anim_failures = romExtractParityCheckPdanimChr();
		(void)chr_anim_emitted; (void)chr_anim_failures;
	}

	/* Catalog universality pivot Step 3 audio half (2026-05-03): emit
	 * per-asset .pdsfx / .pdvoice / .pdsong ZIP compounds at
	 * data/<romid>/audio/{sfx,voice,music}/.
	 *
	 * Walks the leaf SFX bank (sfxctl + sfxtbl segments) for sfx + voice;
	 * a leaf goes to .pdsfx if no russ-mapping points it at a voice
	 * audioconfig slot, .pdvoice otherwise (Slice 10 retag predicate).
	 * Walks the seqtable in the sequences segment for songs.
	 *
	 * Reads from disk-migrated segments populated by romdataInit; the
	 * preprocess stage (segaudio.c::preprocessALBankFile +
	 * preprocessSequences) byte-swaps the bank file + seqtable to native
	 * before this emitter sees them. Idempotent on subsequent boots
	 * (existing files skipped via size check).
	 *
	 * Per Mike's Q-3 ruling: "Catalog is not complete unless it is
	 * COMPLETE." This block closes the audio side of Step 3.
	 *
	 * Per Mike's Q-2: a weapon's shootsound accepts a .pdvoice ID just
	 * as readily as a .pdsfx one. Voice classification at extract time
	 * is recoverable -- the playback layer reads pd_kind at resolve
	 * time and routes to the right decoder. */
	{
		s32 sfx_emitted   = romExtractAllPdsfx(0);
		s32 voice_emitted = romExtractAllPdvoice(0);
		s32 song_emitted  = romExtractAllPdsong(0);
		s32 sfx_failures   = romExtractParityCheckPdsfx();
		s32 voice_failures = romExtractParityCheckPdvoice();
		s32 song_failures  = romExtractParityCheckPdsong();
		(void)sfx_emitted; (void)voice_emitted; (void)song_emitted;
		(void)sfx_failures; (void)voice_failures; (void)song_failures;
	}

	/* Catalog universality pivot Step 3b part 1 (2026-05-03): emit
	 * per-asset .pdfont and .pdlang ZIP compounds at
	 * data/<romid>/fonts/ and data/<romid>/lang/.
	 *
	 * Both wrap raw bytes that already exist on disk after Pass A:
	 *   .pdfont reads data/<romid>/segs/<face>.bin (10 NTSC faces).
	 *   .pdlang reads data/<romid>/files/<sanitized>.bin per
	 *           g_LangFiles[bank] for bank in [1..68] (English locale
	 *           in this NTSC ship; PAL/JPN extension is a follow-up).
	 *
	 * The runtime preprocess (preprocessFont, preprocessLangFile) runs
	 * on the bytes at load time; this emitter does not duplicate that
	 * pass so the .pd<kind> byte payload matches what's already on
	 * disk. Idempotent on subsequent boots. */
	{
		s32 font_emitted   = romExtractAllPdfont(0);
		s32 lang_emitted   = romExtractAllPdlang(0);
		s32 font_failures  = romExtractParityCheckPdfont();
		s32 lang_failures  = romExtractParityCheckPdlang();
		(void)font_emitted; (void)lang_emitted;
		(void)font_failures; (void)lang_failures;
	}

	/* Catalog universality pivot Step 3b part 2 (2026-05-03): emit
	 * per-asset .pdui ZIP compounds at data/<romid>/ui/. Closes the
	 * Step 3 / 3a / 3b series at 13 of 13 universality kinds (weapon,
	 * mesh, animation, head, body, arena, scenario, sfx, voice, song,
	 * font, lang, ui).
	 *
	 * Cross-cut from part 1: the .pdui emitter depends on
	 * g_TexGeneralConfigs (populated by texInit/texReset in pdmain.c
	 * mainInit later in the boot sequence). At THIS wiring point the
	 * texture system is not yet ready -- the call returns 0 cleanly
	 * (no .pdui files emitted at boot main.c). The actual emit fires
	 * from the render-loop fallback trigger inside
	 * pdguiThemeCheckExtract() once GL is up. Subsequent boots find
	 * the .pdui files already on disk and the call is an idempotent
	 * skip. The wire here is the structural placeholder that mirrors
	 * the part 1 / Step 3 / Step 2 / Step 1 emit + parity convention
	 * so future texture-init reorderings can pick up the emit at
	 * boot without re-architecting.
	 *
	 * Parity at this point similarly skips entries whose .pdui ZIPs
	 * are not yet on disk; on the second-and-subsequent boots it
	 * verifies envelope + id + texture_count + source_index round-trip
	 * the canonical descriptor. */
	{
		s32 ui_emitted   = romExtractAllPdui(0);
		s32 ui_failures  = romExtractParityCheckPdui();
		(void)ui_emitted;
		(void)ui_failures;
	}

	/* Catalog universality pivot Step 4 (2026-05-03): universal directory
	 * walker. Now that every per-asset emitter has fired (Steps 1 / 2 / 3a /
	 * 3 / 3b parts 1+2), data/<romid>/<class>/*.pd<ext> holds the canonical
	 * per-asset content for all 13 universality kinds.  loaderWalkerLoadAll
	 * walks each per-class subdirectory, opens each .pd* file (auto-detecting
	 * plain JSON vs ZIP compound), parses the manifest envelope, and ensures
	 * a catalog row exists for every disk-registered ID via the existing
	 * assetCatalogRegister* API.
	 *
	 * Non-destructive overlay (Step 4 scope): the scaffold short-circuits
	 * via assetCatalogResolve before re-registering, so entries already
	 * created by assetCatalogRegisterBaseGame + RegisterStageSceneFiles +
	 * RegisterWeaponModelFiles + ScanComponents above are counted as
	 * "registered" without touching their existing fields. New entries (the
	 * ~1208 chr animations + any disk-only IDs) get fresh registrations.
	 *
	 * .pdbase parser positioning: loaderPdbaseScan + loaderPdbaseBuild*Manager
	 * earlier in this boot path remain the parity-bounded fallback for the
	 * heavyweight loader_pdbase pool population (s_Weapons[] full records,
	 * s_HeadsPool[], s_BodiesPool[], s_ArenasPool[]). The walker handles row
	 * registration here; pool population stays on .pdbase until Step 5 also
	 * migrates pool fill onto a per-asset path.
	 *
	 * Boot order requirement: AFTER all romExtractAllPd* emitters fire on
	 * first boot (so the .pd* files exist on disk when the walker scans),
	 * AFTER assetCatalogRegisterBaseGame (so existing in-binary entries are
	 * the bootstrap fallback), and BEFORE catalogBuildRuntimeCaches (so the
	 * O(1) caches see any walker-added rows). */
	{
		loader_walker_result_t walker_result;
		loaderWalkerLoadAll(&walker_result);
		(void)walker_result;
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
