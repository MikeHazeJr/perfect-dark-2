#include <ultra64.h>
#include "constants.h"
#include "bss.h"
#include "data.h"
#include "game/activemenu.h"
#include "game/atan2f.h"
#include "game/bg.h"
#include "game/body.h"
#include "game/bondgun.h"
#include "game/bondhead.h"
#include "game/bondmove.h"
#include "game/bondview.h"
#include "game/camdraw.h"
#include "game/casing.h"
#include "game/cheats.h"
#include "game/chr.h"
#include "game/chraction.h"
#include "game/credits.h"
#include "game/debug.h"
#include "game/dlights.h"
#include "game/explosions.h"
#include "game/filemgr.h"
#include "game/game_006900.h"
#include "game/game_00b820.h"
#include "game/gunfx.h"
#include "game/game_0b0fd0.h"
#include "game/modelmgr.h"
#include "game/portal.h"
#include "game/fmb.h"
#include "game/sky.h"
#include "game/game_13c510.h"
#include "game/game_1531a0.h"
#include "game/zbuf.h"
#include "game/challenge.h"
#include "game/chrmgr.h"
#include "game/env.h"
#include "game/effect_presentation_renderer.h"
#include "game/gfxmemory.h"
#include "game/gunfx.h"
#include "game/hudmsg.h"
#include "game/inv.h"
#include "game/lang.h"
#include "game/lv.h"
#include "game/corpsestore.h"

/* B-204/B-205: cap catch-up simulation ticks after tab-out / long frames so
 * CHR/bot work cannot spiral unbounded in one mainTick (stability + audio). */
#ifndef LV_UPDATE240_CATCHUP_CAP
#define LV_UPDATE240_CATCHUP_CAP TICKS(8)
#endif
#include "lib/meshcollision.h"
#include "game/menu.h"
#include "game/mplayer/mplayer.h"
#include "game/mplayer/mpspawn_orchestrate.h"
#include "game/mplayer/participant.h"
#include "game/mplayer/scenarios.h"
#include "game/mplayer/setup.h"
#include "game/music.h"
#include "modmusic.h"
#include "effect_instance_runtime.h"
#include "pdgui_charpreview.h"
#include "game/nbomb.h"
#include "game/objectives.h"
#include "game/pak.h"
#include "game/pdmode.h"
#include "game/player.h"
#include "game/playermgr.h"
#include "game/spawnpool.h"
#include "game/playerreset.h"
#include "game/prop.h"
#include "game/propobj.h"
#include "game/propobjstop.h"
#include "game/propsnd.h"
#include "game/room.h"
#include "game/savebuffer.h"
#include "game/setup.h"
#include "game/shards.h"
#include "game/sky.h"
#include "game/smoke.h"
#include "game/sparks.h"
#include "game/splat.h"
#include "game/stars.h"
#include "game/stubs/game_013540.h"
#include "game/stubs/game_015260.h"
#include "game/stubs/game_015270.h"
#include "game/stubs/game_0153f0.h"
#include "game/stubs/game_015400.h"
#include "game/stubs/game_015410.h"
#include "game/tex.h"
#include "game/texdecompress.h"
#include "game/tiles.h"
#include "game/title.h"
#include "game/training.h"
#include "game/utils.h"
#include "game/vtxstore.h"
#include "game/wallhit.h"
#include "game/weather.h"
#include "lib/anim.h"
#include "lib/args.h"
#include "lib/collision.h"
#include "lib/crash.h"
#include "lib/joy.h"
#include "actionmap.h"
#include "lib/lib_06440.h"
#include "lib/lib_317f0.h"
#include "lib/main.h"
#include "lib/mtx.h"
#include "lib/music.h"
#include "lib/rng.h"
#include "lib/sched.h"
#include "lib/snd.h"
#include "lib/vars.h"
#include "lib/vi.h"
#include "types.h"
#include "net/net.h"
#include "net/netmanifest.h"
#include "net/netmsg.h"
#include "video.h"
#include "system.h"
#include "crashbreadcrumb.h"
#include <string.h>
#include "assetcatalog_resolve.h"
#include "assetcatalog_load.h"
#include "asset_fallback_telemetry.h" /* c3849 Wave 1 */
#include "scenario_source_runtime.h"
#include "smoke_harness.h"

/* PC: persistent stats tracking */
extern void statIncrement(const char *key, u64 amount);

static asset_type_e lvCatalogAssetTypeForId(const char *assetId)
{
	const asset_entry_t *entry = assetCatalogResolve(assetId);
	return entry ? entry->type : ASSET_NONE;
}

static s32 lvAddLoadedCatalogColmeshes(const char *category,
	const char *exclude_id)
{
	s32 added = 0;
	s32 total = assetCatalogGetCount();

	if (!category || !category[0]) {
		return 0;
	}

	for (s32 i = 0; i < total; i++) {
		const asset_entry_t *entry = assetCatalogGetByIndex(i);
		struct colmesh *mesh;
		s32 before;

		if (!entry || !entry->occupied || entry->bundled) {
			continue;
		}

		if (exclude_id && exclude_id[0]
				&& strncmp(entry->id, exclude_id, CATALOG_ID_LEN) == 0) {
			continue;
		}

		if (category && category[0]
				&& strncmp(entry->category, category, CATALOG_CATEGORY_LEN) != 0) {
			continue;
		}

		mesh = catalogGetLoadedColmesh(entry->id);
		if (!mesh || mesh->numtris <= 0) {
			continue;
		}

		before = g_WorldMesh.numtris;
		meshWorldAddMesh(mesh, NULL);
		if (g_WorldMesh.numtris > before) {
			added++;
			sysLogPrintf(LOG_NOTE,
				"MESHCOL: added catalog OBJ mesh '%s' tris=%d",
				entry->id, g_WorldMesh.numtris - before);
		}
	}

	return added;
}

static s32 lvResolveStageForScenarioSource(s32 stagenum,
	catalog_stage_result_t *stage)
{
	const struct asset_entry *modMap = assetCatalogFindModMapByStagenum(stagenum);
	const char *stage_id = modMap ? modMap->id : catalogStageIdByStagenum(stagenum);

	if (!stage || !stage_id || !stage_id[0]) {
		return 0;
	}

	return catalogResolveStage(stage_id, stage);
}

static s32 lvAddScenarioSourceColmesh(s32 stagenum, s32 prefer_mp,
	char *out_scenario_id, size_t out_scenario_id_n)
{
	catalog_stage_result_t stage;
	const asset_entry_t *scenario;
	struct colmesh *mesh;
	s32 before;
	const char *source_path;

	if (out_scenario_id && out_scenario_id_n > 0) {
		out_scenario_id[0] = '\0';
	}

	if (!lvResolveStageForScenarioSource(stagenum, &stage)) {
		return 0;
	}

	scenario = scenarioSourceFindEntryForStage(&stage, prefer_mp);
	if (!scenario || !scenario->id[0]) {
		return 0;
	}

	if (!catalogLoadStageAsset(ASSET_SCENARIO, scenario->id)) {
		sysLogPrintf(LOG_WARNING,
			"SCENARIO.SOURCE: failed to activate scene source '%s'",
			scenario->id);
		return 0;
	}

	mesh = catalogGetLoadedColmesh(scenario->id);
	if (!mesh || mesh->numtris <= 0) {
		sysLogPrintf(LOG_WARNING,
			"SCENARIO.SOURCE: scene source '%s' produced no usable world mesh",
			scenario->id);
		return 0;
	}

	before = g_WorldMesh.numtris;
	meshWorldAddMesh(mesh, NULL);
	if (g_WorldMesh.numtris <= before) {
		return 0;
	}

	if (out_scenario_id && out_scenario_id_n > 0) {
		strncpy(out_scenario_id, scenario->id, out_scenario_id_n - 1);
		out_scenario_id[out_scenario_id_n - 1] = '\0';
	}

	source_path = scenario->ext.scenario.collision_file[0]
		? scenario->ext.scenario.collision_file
		: scenario->ext.scenario.scene_file;
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.SOURCE: added scene colmesh '%s' tris=%d source=%s",
		scenario->id, g_WorldMesh.numtris - before,
		source_path && source_path[0] ? source_path : "(none)");
	return 1;
}

/* M0.2: helper — returns true if player has any button or stick input.
 * Replaces the old joyGetButtons(contpad, 0xffffffff) + stick deadzone checks.
 * Threshold 0.125 matches the legacy 10/80 stick deadzone. */
static s32 lvPlayerHasAnyInput(s32 playerIdx)
{
	/* Check any digital action pressed this frame */
	for (s32 a = 0; a < ACTION_AXIS_MOVE_X; a++) {
		if (actionHeld(playerIdx, (InputAction)a)) {
			return 1;
		}
	}
	/* Check stick axes beyond deadzone (10/80 = 0.125) */
	f32 mx = actionValue(playerIdx, ACTION_AXIS_MOVE_X);
	f32 my = actionValue(playerIdx, ACTION_AXIS_MOVE_Y);
	if (mx > 0.125f || mx < -0.125f || my > 0.125f || my < -0.125f) {
		return 1;
	}
	return 0;
}

/* M0.2: helper — returns true if player pressed any button this frame (rising edge). */
static s32 lvPlayerAnyButtonPressedThisFrame(s32 playerIdx)
{
	for (s32 a = 0; a < ACTION_AXIS_MOVE_X; a++) {
		if (actionPressed(playerIdx, (InputAction)a)) {
			return 1;
		}
	}
	f32 mx = actionValue(playerIdx, ACTION_AXIS_MOVE_X);
	f32 my = actionValue(playerIdx, ACTION_AXIS_MOVE_Y);
	if (mx > 0.125f || mx < -0.125f || my > 0.125f || my < -0.125f) {
		return 1;
	}
	return 0;
}

struct sndstate *g_MiscSfxAudioHandles[3];
u32 var800aa5bc;
s32 g_MiscSfxActiveTypes[3];

u32 var80084010 = 0;
bool var80084014 = false;
f32 var80084018 = 1;
u32 var8008401c = 0x00000001;

s32 g_Difficulty = DIFF_A;

s32 g_StageTimeElapsed60 = 0;
s32 g_MpTimeLimit60 = SECSTOTIME60(60 * 10); // 10 minutes
s32 g_MpScoreLimit = 10;
s32 g_MpTeamScoreLimit = 20;
struct sndstate *g_MiscAudioHandle = NULL;
s32 g_NumReasonsToEndMpMatch = 0;
f32 g_StageTimeElapsed1f = 0;
bool var80084040 = true;

u32 g_MiscSfxSounds[] = {
	SFX_HEARTBEAT,
	SFX_SLAYER_WHIR,
	SFX_SLAYER_BEEP,
};

s32 var80084050 = 0;

s16 g_FadeNumFrames = 0;
f32 g_FadeFrac = -1;
u32 g_FadePrevColour = 0;
u32 g_FadeColour = 0;
s16 g_FadeDelay = 0;

u32 getVar80084040(void)
{
	return var80084040;
}

void setVar80084040(u32 value)
{
	var80084040 = value;
}

void lvInit(void)
{
	g_Vars.lockscreen = 0;
	g_Vars.joydisableframestogo = -1;
}

void lvResetMiscSfx(void)
{
	s32 i;

	for (i = 0; i != ARRAYCOUNT(g_MiscSfxAudioHandles); i++) {
		g_MiscSfxAudioHandles[i] = NULL;
		g_MiscSfxActiveTypes[i] = -1;
	}
}

s32 lvGetMiscSfxIndex(u32 type)
{
	s32 i;

	for (i = 0; i != ARRAYCOUNT(g_MiscSfxActiveTypes); i++) {
		if (g_MiscSfxActiveTypes[i] == type) {
			return i;
		}
	}

	return -1;
}

void lvSetMiscSfxState(u32 type, bool play)
{
	if (play) {
		if (lvGetMiscSfxIndex(type) == -1) {
			s32 index = lvGetMiscSfxIndex(-1);

#if VERSION >= VERSION_NTSC_1_0
			if (index != -1 && g_MiscSfxAudioHandles[index] == NULL)
#else
			if (index != -1)
#endif
			{
				sndStart(var80095200, g_MiscSfxSounds[type], &g_MiscSfxAudioHandles[index], -1, -1, -1, -1, -1);
				g_MiscSfxActiveTypes[index] = type;
			}
		}
	} else {
		u32 stack;
		s32 index = lvGetMiscSfxIndex(type);

		if (index != -1) {
			audioStop(g_MiscSfxAudioHandles[index]);
#if VERSION < VERSION_NTSC_1_0
			g_MiscSfxAudioHandles[index] = 0;
#endif
			g_MiscSfxActiveTypes[index] = -1;
		}
	}
}

void lvUpdateMiscSfx(void)
{
	s32 i;

	if (g_Vars.lvupdate240 == 0) {
		for (i = 0; i != ARRAYCOUNT(g_MiscSfxActiveTypes); i++) {
			lvSetMiscSfxState(i, false);
		}
	} else {
		bool usingboost = g_Vars.speedpillon
			&& lvGetSlowMotionType() == SLOWMOTION_OFF
			&& !playerAnyCutsceneInProgress();
		bool usingrocket;

		lvSetMiscSfxState(MISCSFX_BOOSTHEARTBEAT, usingboost);

		usingrocket = false;

		for (i = 0; i < PLAYERCOUNT(); i++) {
			if (!g_Vars.players[i]) continue;
			if (g_Vars.players[i]->visionmode == VISIONMODE_SLAYERROCKET) {
				usingrocket = true;
			}
		}

		lvSetMiscSfxState(MISCSFX_SLAYERROCKETHUM, usingrocket);
		lvSetMiscSfxState(MISCSFX_SLAYERROCKETBEEP, usingrocket);
	}

	if (g_Vars.lvupdate240 == 0 && g_MiscAudioHandle && sndGetState(g_MiscAudioHandle) != AL_STOPPED) {
		audioStop(g_MiscAudioHandle);
	}
}

bool lvReset(s32 stagenum)
{
	s32 i;
	/* Stage-lifetime public effects cannot carry target/context state across
	 * teardown or catalog diff activation. */
	effectInstanceRuntimeClearAll();

	/* Invalid stagenum (e.g. 0x00) must never reach bg/setup/catalog paths. */
	stagenum = stageSanitizeLoadStagenum(stagenum);

	lvFadeReset();

	var80084014 = false;
	var80084010 = 0;

#if VERSION >= VERSION_NTSC_1_0
	joyLockCyclicPolling();

	g_Vars.joydisableframestogo = 10;
#else
	if (joyIsCyclicPollingEnabled()) {
		joyDisableCyclicPolling(760, "lv.c");

		g_Vars.joydisableframestogo = 10;
	}
#endif

	g_Vars.paksneededforgame = 0;
	g_Vars.paksneededformenu = 0;
	g_Vars.stagenum = stagenum;

	// D3R-5: Activate catalog component for this stage (if a mod map exists).
	// This gives the catalog's file resolver priority over the legacy mod system.
	// For base game stages, this is a no-op (deactivates the resolver).
	assetCatalogActivateStage(stagenum);

	// C-9 / MEM-3: Stage transition diff - unload mod assets no longer needed,
	// load mod assets required for the new stage.  Base-game (bundled) assets
	// are never touched.  If no mod map entry exists for this stagenum the diff
	// produces an empty toLoad list and unloads any lingering mod assets from the
	// previous stage. MP match manifests own their own lifecycle, so the old
	// stage-category diff must not release manifest-owned match assets.
	const char *stageAssetCategory = NULL;
	{
#define STAGE_DIFF_MAX 64
		const char *toLoad[STAGE_DIFF_MAX];
		const char *toUnload[STAGE_DIFF_MAX];
		s32 loadCount = 0;
		s32 unloadCount = 0;

		const struct asset_entry *modMap = assetCatalogFindModMapByStagenum(stagenum);
		const char *stageAssetId = modMap ? modMap->id : NULL;
		stageAssetCategory = (modMap && modMap->category[0]) ? modMap->category : NULL;

		if (g_ClientManifest.num_entries > 0) {
			sysLogPrintf(LOG_NOTE,
			             "CATALOG: stage 0x%02x diff skipped during MP manifest ownership (manifest=%d netmode=%d normmplay=%d mplay=%d)",
			             stagenum, (s32)g_ClientManifest.num_entries, g_NetMode,
			             g_Vars.normmplayerisrunning, g_Vars.mplayerisrunning);
		} else {
			s32 diffTotal = catalogComputeStageDiff(stageAssetId,
			                                        toLoad,  &loadCount,
			                                        toUnload, &unloadCount,
			                                        STAGE_DIFF_MAX);

			if (diffTotal > 0) {
				sysLogPrintf(LOG_NOTE,
				             "CATALOG: stage 0x%02x diff load:%d unload:%d",
				             stagenum, loadCount, unloadCount);
				for (s32 i = 0; i < unloadCount; i++) {
					catalogReleaseStageAsset(lvCatalogAssetTypeForId(toUnload[i]), toUnload[i]);
				}
				for (s32 i = 0; i < loadCount; i++) {
					catalogLoadStageAsset(lvCatalogAssetTypeForId(toLoad[i]), toLoad[i]);
				}
			}
		}
#undef STAGE_DIFF_MAX
	}

	// PC: When loading the Carrington Institute (main menu background) or title
	// screen, suppress mod file overlay so CI props, textures, and setup files
	// remain base-game originals. Without this, enabled mods (GEX, kakariko, etc.)
	// provide replacement files for CI props like Pcidoor1Z, Pci_liftdoorZ, and
	// dozens of others, making the CI environment look corrupted with foreign map
	// data overlaid. g_NotLoadMod is restored after lvReset completes via the
	// menu handlers that transition to solo (sets true) or multiplayer (sets false).
	if (stagenum == STAGE_CITRAINING
			|| stagenum == STAGE_TITLE
			|| stagenum == STAGE_BOOTPAKMENU
			|| stagenum == STAGE_CREDITS) {
		g_NotLoadMod = true;
	}

	cheatsReset();

	var80084040 = true;
	g_Vars.lvframenum = 0;
	var80084050 = 0;

	g_Vars.lvframe60 = 0;
	g_Vars.lvupdate240 = 4;

#if VERSION >= VERSION_NTSC_1_0
	g_Vars.lvupdate60f = 1.0f;
	g_Vars.lvupdate60frealprev = PALUPF(1);
#else
	g_Vars.lvupdate60frealprev = PALUPF(1);
	g_Vars.lvupdate60f = 1.0f;
#endif

	g_Vars.lvupdate60freal = g_Vars.lvupdate60frealprev;

	g_StageTimeElapsed60 = 0;
	g_StageTimeElapsed1f = 0;

	g_Vars.speedpilltime = 0;
	g_Vars.speedpillchange = 0;
	g_Vars.speedpillwant = 0;
	g_Vars.speedpillon = false;

	g_Vars.restartlevel = false;
	g_Vars.aibuddiesspawned = false;
	g_Vars.totalkills = 0;
	g_Vars.antiheadnum = -1;
	g_Vars.antibodynum = -1;
	g_Vars.dontplaynrg = false;
	/* B-1099: a reconnect can leave the CI menu's local cutscene tick mode
	 * alive until the next validated SVC_STAGE_START has already minted its
	 * match latch. Retire only that prior-stage presentation at this real load
	 * boundary; all non-client/non-authoritative loads retain the ordinary
	 * cutscene-state reset. */
	if (!playerApplyAuthoritativeStageStartPresentation()) {
		playerResetAllCutsceneStates();
	}
	g_Vars.autocutplaying = false;
	g_Vars.autocutfinished = false;
	g_Vars.autocutgroupskip = false;

	g_MiscAudioHandle = NULL;

	musicReset();
	modMusicStop(); /* Stop mod music to prevent stale PCM during stage load */
	modelmgrSetLvResetting(true);
	surfaceReset();
	texReset();
	textReset();
	hudmsgsReset();

	if (stagenum == STAGE_TEST_OLD) {
		titleReset();
	}

	if (stagenum == STAGE_TITLE) {
		titleReset();
	} else if (stagenum == STAGE_BOOTPAKMENU) {
		// empty
	} else if (stagenum == STAGE_CREDITS) {
		// empty
	} else {
		s32 i;
		s32 j;

		if (g_NetMode == NETMODE_CLIENT) {
			// if we're a client, now is the time to apply the server's RNG seeds
			netClientSyncRng();
		}
		/* B-1039: server stage start is emitted by the authoritative lobby/ready
		 * transition before this deferred load begins.  Re-emitting it here made
		 * every peer run mpStartMatch/mainChangeToStage twice and reset its freshly
		 * loaded stage.  Co-op/anti likewise broadcast in netServerCoopStageStart. */
		sysLogPrintf(LOG_NOTE, "LOAD: lv.c entering stage load sequence for stagenum=0x%02x", g_Vars.stagenum);

		/* Gate 5 (c3844): consume any catalog miss accumulated during the prior
		 * phase (menu / character preview / previous stage) before this stage
		 * loads fresh. catalogGet*ByIndex helpers set g_CatalogFailure on a
		 * miss but nothing read it; this checkpoint reports it loudly and, under
		 * source-only enforcement, hard-fails instead of tolerating the silent
		 * default substitution. No-op on a healthy install. */
		catalogAssertHealthy("stage-load-entry");
		/* c3849 Wave 1: consume the prior phase's asset fallbacks at the same
		 * checkpoint (ASSET.FALLBACK aggregate; silent when zero). */
		assetFallbackReportAndReset("stage-load-entry");

		tilesReset();
		bgReset(g_Vars.stagenum);
		sysLogPrintf(LOG_NOTE, "LOAD: bgReset done");
		bgBuildTables(g_Vars.stagenum);
		sysLogPrintf(LOG_NOTE, "LOAD: bgBuildTables done");
		meshWorldShutdown();
		meshWorldInit();
		{
			s32 meshrooms = 0;
			s32 source_mesh_added;
			s32 meshtris_before = 0;
			char sourceScenarioId[CATALOG_ID_LEN];

			source_mesh_added = lvAddScenarioSourceColmesh(g_Vars.stagenum,
				g_Vars.normmplayerisrunning, sourceScenarioId,
				sizeof(sourceScenarioId));
			if (!source_mesh_added) {
				for (i = 1; i < g_Vars.roomcount; i++) {
					meshtris_before = g_WorldMesh.numtris;
					if (meshWorldAddRenderedRoom(i) == 0) {
						meshWorldAddRoomGeo(i);
					}
					if (g_WorldMesh.numtris > meshtris_before) {
						meshrooms++;
					}
				}
			}
			meshrooms += lvAddLoadedCatalogColmeshes(stageAssetCategory,
				sourceScenarioId);
			meshWorldFinalize();
			sysLogPrintf(LOG_NOTE,
				"MESHCOL: ENABLED -- source=%d rooms=%d tris=%d",
				source_mesh_added, meshrooms, g_WorldMesh.numtris);
		}

		skyReset(g_Vars.stagenum);
		sysLogPrintf(LOG_NOTE, "LOAD: skyReset done");

		sysLogPrintf(LOG_NOTE, "LOAD: music init normmplay=%d stagenum=0x%02x players=%d/%d/%d/%d",
			g_Vars.normmplayerisrunning, stagenum,
			g_Vars.players[0] != NULL, g_Vars.players[1] != NULL,
			g_Vars.players[2] != NULL, g_Vars.players[3] != NULL);

		if (g_Vars.normmplayerisrunning) {
			musicSetStageAndStartMusic(stagenum);
		} else {
			musicSetStage(stagenum);
		}
		sysLogPrintf(LOG_NOTE, "LOAD: music set done normmplay=%d", g_Vars.normmplayerisrunning);

		if (g_Vars.normmplayerisrunning) {
			mpApplyLimits();
		}
		sysLogPrintf(LOG_NOTE, "LOAD: mpApplyLimits done");

		if (g_Vars.mplayerisrunning == false) {
			g_Vars.playerstats[0].mpindex = 0;
			g_PlayerConfigsArray[0].contpad1 = 0;
			g_PlayerConfigsArray[0].contpad2 = 1;
		}

		for (i = 0; i != ARRAYCOUNT(g_Vars.playerstats); i++) {
			g_Vars.playerstats[i].damagescale = 1;
			g_Vars.playerstats[i].drawplayercount = 0;
			g_Vars.playerstats[i].distance = 0;
			g_Vars.playerstats[i].backshotcount = 0;
			g_Vars.playerstats[i].armourcount = 0;
			g_Vars.playerstats[i].fastest2kills = S32_MAX;
			g_Vars.playerstats[i].slowest2kills = 0;
			g_Vars.playerstats[i].maxkills = 0;
			g_Vars.playerstats[i].maxsimulkills = 0;
			g_Vars.playerstats[i].longestlife = 0;
			g_Vars.playerstats[i].shortestlife = S32_MAX;
			g_Vars.playerstats[i].tokenheldtime = 0;
			g_Vars.playerstats[i].damreceived = 0;
			g_Vars.playerstats[i].damtransmitted = 0;
			/* Per-weapon / summary combat counters — must reset every stage load.
			 * Previously only kills[] was cleared; killcount/shotcount stayed stale,
			 * so pause menu / HUD could show phantom kills from the prior match
			 * (playtest 2026-04-13 false-kills report). */
			for (j = 0; j != ARRAYCOUNT(g_Vars.playerstats[i].shotcount); j++) {
				g_Vars.playerstats[i].shotcount[j] = 0;
			}
			g_Vars.playerstats[i].killcount = 0;
			g_Vars.playerstats[i].ggkillcount = 0;
			g_Vars.playerstats[i].unk64 = 0;
			g_Vars.playerstats[i].cloaktime = 0;
			g_Vars.playerstats[i].speedpillcount = 0;
			g_Vars.playerstats[i].scale_bg2gfx = 0;

			for (j = 0; j != ARRAYCOUNT(g_Vars.playerstats[i].kills); j++) {
				g_Vars.playerstats[i].kills[j] = 0;
			}
		}
		sysLogPrintf(LOG_NOTE, "LOAD: player stats init done");
	}

	mpSetDefaultNamesIfEmpty();
	sysLogPrintf(LOG_NOTE, "LOAD: mpSetDefaultNamesIfEmpty done");
	animsReset();
	sysLogPrintf(LOG_NOTE, "LOAD: animsReset done");
	objectivesReset();
	sysLogPrintf(LOG_NOTE, "LOAD: objectivesReset done");
	vtxstoreReset();
	sysLogPrintf(LOG_NOTE, "LOAD: vtxstoreReset done");
	modelmgrReset();
	sysLogPrintf(LOG_NOTE, "LOAD: modelmgrReset done");
	psReset();
	sysLogPrintf(LOG_NOTE, "LOAD: psReset done");
	sysLogPrintf(LOG_NOTE, "LOAD: about to call setupLoadFiles(0x%02x)", stagenum);
	setupLoadFiles(stagenum);
	sysLogPrintf(LOG_NOTE, "LOAD: setupLoadFiles done");

	/* Post-setup manifest rescan: now that g_StageSetup.props is populated,
	 * re-scan the setup file's CHR/prop entries and add any newly discovered
	 * assets to the SP manifest.  The pre-load manifestSPTransition() in
	 * mainChangeToStage() runs before setup files are loaded, so it only
	 * captures the stage map + Joanna.  This pass captures all NPCs, enemies,
	 * and prop models from the setup spawn list.  Fixes B-118. */
	manifestSPRescanSetup(stagenum);

	sysLogPrintf(LOG_NOTE, "LOAD: calling scenarioResetForStageLoad");
	scenarioResetForStageLoad(stagenum);
	sysLogPrintf(LOG_NOTE, "LOAD: calling varsReset");
	varsReset();
	sysLogPrintf(LOG_NOTE, "LOAD: calling propsReset");
	propsReset();
	sysLogPrintf(LOG_NOTE, "LOAD: calling chrmgrReset");
	chrmgrReset();
	/* PC (c132): re-acquire the frozen corpse store from the fresh stage pool so
	 * campaign corpses persist for this mission and never leak across a stage. */
	corpseStoreReset();
	sysLogPrintf(LOG_NOTE, "LOAD: calling bodiesReset stagenum=0x%02x", stagenum);
	bodiesReset(stagenum);
	sysLogPrintf(LOG_NOTE, "LOAD: calling setupCreateProps stagenum=0x%02x normmplayerisrunning=%d activeMask=0x%04llx g_MpNumChrs=%d",
		stagenum, g_Vars.normmplayerisrunning,
		(unsigned long long)mpParticipantsEncodeActiveMask(), g_MpNumChrs);
	setupCreateProps(stagenum);
	sysLogPrintf(LOG_NOTE, "LOAD: setupCreateProps done, calling reset functions");
	tagsReset();
	explosionsReset();
	smokeReset();
	sparksReset();
	weatherReset();
	lvResetMiscSfx();

	switch (g_Vars.stagenum) {
	case STAGE_ESCAPE:
	case STAGE_EXTRACTION:
	case STAGE_INFILTRATION:
	case STAGE_DEFECTION:
	case STAGE_ATTACKSHIP:
	case STAGE_TEST_OLD:
		starsReset();
		break;
	}

	func0f0099a4();
	boltbeamsReset();
	lasersightsReset();
	stub0f013540();
	shardsReset();
	frReset();

	if (g_Vars.stagenum == STAGE_TITLE) {
		// empty
	} else if (stagenum == STAGE_BOOTPAKMENU) {
		setCurrentPlayerNum(0);
		menuReset();
	} else if (stagenum == STAGE_CREDITS) {
		creditsReset();
	} else {
		s32 i;

		utilsReset();
		casingsReset();

		/* B-219: first pass loads intro weapons via playerReset (INTROCMD_*); second
		 * pass below calls playerSpawn (MP spawn-with-weapon). Keep those in sync. */
		for (i = 0; i < PLAYERCOUNT(); i++) {
			if (!g_Vars.players[i]) continue;
			setCurrentPlayerNum(i);
			g_Vars.currentplayer->usedowntime = 0;
			g_Vars.currentplayer->invdowntime = g_Vars.currentplayer->usedowntime;

			menuReset();
			amReset();
			invReset();
			bgunReset();
			playerLoadDefaults();
			sysLogPrintf(LOG_NOTE, "LOAD: playerLoadDefaults done for player %d, calling playerReset", i);
			{
				enum player_reset_result player_result = playerReset();
				if (player_result != PLAYER_RESET_OK) {
					sysLogPrintf(LOG_ERROR,
						"PLAYER.INIT.ROLLBACK phase=stage player=%d status=%s; aborting stage reset",
						i, playerResetResultString(player_result));
					modelmgrSetLvResetting(false);
					return false;
				}
			}
			sysLogPrintf(LOG_NOTE, "LOAD: playerReset done for player %d", i);
		}

		if (g_Vars.mplayerisrunning && spawnPoolIsReady()) {
			mpOrchestrateMatchStartSpawns();
		}

		for (i = 0; i < PLAYERCOUNT(); i++) {
			if (!g_Vars.players[i]) continue;
			setCurrentPlayerNum(i);
			sysLogPrintf(LOG_NOTE, "LOAD: calling playerSpawn for player %d", i);
			{
				enum player_chrbody_result chrbody_result = playerSpawn();
				if (chrbody_result != PLAYER_CHRBODY_OK) {
					sysLogPrintf(LOG_ERROR,
						"PLAYER.INIT.ROLLBACK phase=spawn player=%d status=%s; aborting stage reset",
						i, playerChrBodyResultString(chrbody_result));
					modelmgrSetLvResetting(false);
					return false;
				}
			}
			sysLogPrintf(LOG_NOTE, "LOAD: playerSpawn done for player %d, calling bheadReset", i);

			/* c3845 (2026-06-23): two-process match-smoke spawn milestone.
			 * This loop runs for every LOCAL player on whichever process owns
			 * it. The net client SLOT is the owning netclient's id (== its
			 * g_NetClients[] index): the listen-host's local player is client
			 * id 0 (-> slot=0), and the joined client's local player is client
			 * id 1 (-> slot=1, after netmsg.c:1158 sets g_NetLocalClient =
			 * &g_NetClients[id]). So the concatenated host+client log shows
			 * BOTH slot=0 and slot=1. Using player->client->id (not the
			 * playernum) is robust against netPlayersAllocate's client-side
			 * local-player->index-0 swap (net.c:2366-2372). -1 in solo. */
			if (g_Vars.normmplayerisrunning && g_Vars.currentplayer
					&& g_Vars.currentplayer->prop) {
				s32 netslot = -1;
				if (g_NetMode && g_Vars.currentplayer->client) {
					netslot = (s32)g_Vars.currentplayer->client->id;
				}
				sysLogPrintf(LOG_NOTE,
					"MATCH: player spawned slot=%d playernum=%d pos=(%.0f,%.0f,%.0f)",
					netslot, i,
					g_Vars.currentplayer->prop->pos.f[0],
					g_Vars.currentplayer->prop->pos.f[1],
					g_Vars.currentplayer->prop->pos.f[2]);
			}
			bheadReset();

			if (g_Vars.normmplayerisrunning && (g_MpSetup.options & MPOPTION_TEAMSENABLED)) {
				playermgrCalculateAiBuddyNums();
			}
		}

		acousticReset();
		portalsReset();
		lightsReset();
		setCurrentPlayerNum(0);
		sysLogPrintf(LOG_NOTE, "LOAD: player init loop complete");
	}

	if (g_Vars.lvmpbotlevel) {
		mpCalculateTeamIsOnlyAi();
	}

	paksReset();
	sndResetCurMp3();

	if (stagenum == STAGE_BOOTPAKMENU) {
		bootmenuReset();
	}

	pheadReset();

	if (g_NetMode) {
		netSyncIdsAllocate();
	}

	modelmgrSetLvResetting(false);
	var80084018 = 1;
	schedResetArtifacts();
	lvSetPaused(0);

#if PIRACYCHECKS
	{
		u32 checksum = 0;
		s32 *i = (s32 *)&lvGetSlowMotionType;
		s32 *end = (s32 *)&lvTick;

		while (i < end) {
			checksum += *i;
			i++;
		}

		if (checksum != CHECKSUM_PLACEHOLDER) {
			// This is writing a file to the start of the EEPROM data.
			// The file is PAKFILETYPE_TERMINATOR, which is used internally to
			// mark the end of the usable space. This effectively deletes all
			// save data on the game pak and makes it permanently unusable.
			u32 address = 0;
			u32 buffer[4];
			buffer[0] = 0xbb8b80bd;
			buffer[1] = 0xffffffff;
			buffer[2] = 0x020f0100;
			buffer[3] = 0xcd31100b;
			osEepromLongWrite(&g_PiMesgQueue, address, (u8 *)&buffer, 0x10);
			g_Paks[SAVEDEVICE_GAMEPAK].headercachecount = 0;
		}
	}
#endif
	return true;
}

void lvConfigureFade(u32 color, s16 num_frames)
{
	g_FadeNumFrames = num_frames;
	g_FadePrevColour = g_FadeColour;

	if (g_FadeNumFrames == 0) {
		g_FadeColour = color;
		g_FadeFrac = -1;
		return;
	}

	g_FadeFrac = 0;
	g_FadeColour = color;
	g_FadeDelay = 2;
}

Gfx *lvRenderFade(Gfx *gdl)
{
	u32 colour = g_FadeColour;
	u32 inset = 0;

	if (g_Vars.stagenum == STAGE_TEST_OLD) {
		inset = 61;
	}

	if (g_FadeFrac >= 0) {
		if (g_FadeDelay > 0) {
			g_FadeDelay--;
		} else {
#if VERSION >= VERSION_PAL_BETA
			g_FadeFrac += g_Vars.diffframe60freal / g_FadeNumFrames;
#else
			g_FadeFrac += g_Vars.diffframe60f / g_FadeNumFrames;
#endif

			if (g_FadeFrac >= 1) {
				g_FadeFrac = -1;
			}
		}
	}

	if (g_FadeFrac < 0) {
		if ((g_FadeColour & 0xff) == 0) {
			return gdl;
		}
	} else {
		colour = colourBlend(g_FadeColour, g_FadePrevColour, g_FadeFrac * 255);
	}

	if ((colour & 0xff) == 0) {
		return gdl;
	}

	gDPPipeSync(gdl++);
	gDPSetRenderMode(gdl++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
	gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
	gDPSetPrimColorViaWord(gdl++, 0, 0, colour);

	gDPFillRectangle(gdl++,
			viGetViewLeft(),
			viGetViewTop() + inset,
			viGetViewLeft() + viGetViewWidth() + 1,
			viGetViewTop() + viGetViewHeight() - inset + 2);

	return text0f153838(gdl);
}

bool lvIsFadeActive(void)
{
	return g_FadeFrac >= 0;
}

void lvFadeReset(void)
{
	g_FadeNumFrames = 0;
	g_FadeFrac = -1;
	g_FadePrevColour = 0;
	g_FadeColour = 0;
	g_FadeDelay = 0;
}

bool lvUpdateTrackedProp(struct trackedprop *trackedprop, s32 index)
{
	f32 y1;
	f32 x1;
	f32 y2;
	f32 x2;
	struct prop *prop = trackedprop->prop;
	struct chrdata *chr;

	if (trackedprop->prop && prop->chr) {
		switch (trackedprop->prop->type) {
		case PROPTYPE_PLAYER:
			if (playermgrGetPlayerNumByProp(prop) == g_Vars.currentplayernum) {
				return false;
			}
			// fall through
		case PROPTYPE_CHR:
			chr = trackedprop->prop->chr;

			if (chrIsDead(trackedprop->prop->chr)) {
				if (index >= 0) {
					// Existing trackedprop
					if (g_Vars.currentplayer->targetset[index] < TICKS(129)) {
						g_Vars.currentplayer->targetset[index] = TICKS(129);
					}

					if (g_Vars.currentplayer->targetset[index] >= (PAL ? 146 : 175)) {
						trackedprop->prop = NULL;
						return false;
					}
				} else {
					// lookingatprop
					trackedprop->prop = NULL;
					return false;
				}
			}

			if ((trackedprop->prop->flags & PROPFLAG_ONTHISSCREENTHISTICK)
					&& (chr->chrflags & CHRCFLAG_NOAUTOAIM) == 0) {
				struct model *model = chr->model;
				x1 = -1;
				y1 = -1;
				x2 = -2;
				y2 = -2;

				if (modelGetScreenCoords(model, &x2, &x1, &y2, &y1)) {
					break;
				}
				return false;
			}
			return false;
		case PROPTYPE_OBJ:
		case PROPTYPE_WEAPON:
			if (trackedprop->prop->flags & PROPFLAG_ONTHISSCREENTHISTICK) {
				struct defaultobj *obj = trackedprop->prop->obj;
				struct model *model = obj->model;
				x1 = -1;
				y1 = -1;
				x2 = -2;
				y2 = -2;

				if (modelGetScreenCoords(model, &x2, &x1, &y2, &y1)) {
					break;
				}
				return false;
			}
			return false;
		case PROPTYPE_DOOR:
		case PROPTYPE_EYESPY:
		case PROPTYPE_EXPLOSION:
		case PROPTYPE_SMOKE:
		default:
			return false;
		}

		trackedprop->x1 = x1 - 2;
		trackedprop->x2 = x2 + 2;
		trackedprop->y1 = y1 - 2;
		trackedprop->y2 = y2 + 2;
	}

	return true;
}

#ifdef DEBUG
Gfx *lvRenderManPosIfEnabled(Gfx *gdl)
{
	char bufroom[16];
	char bufx[16];
	char bufy[16];
	char bufz[16];
	char bufdir[16];
	s32 x;
	s32 y;
	s32 y2;

	if (debugIsManPosEnabled()) {
		f32 xfrac = g_Vars.currentplayer->bond2.unk00.x;
		f32 zfrac = g_Vars.currentplayer->bond2.unk00.z;

		char directions[][3] = {
			{'n', '\0', '\0'},
			{'n', 'e',  '\0'},
			{'e', '\0', '\0'},
			{'s', 'e',  '\0'},
			{'s', '\0', '\0'},
			{'s', 'w',  '\0'},
			{'w', '\0', '\0'},
			{'n', 'w',  '\0'},
			{'n', '\0', '\0'},
		};

		s32 degrees = atan2f(-xfrac, zfrac) * 180.0f / M_PI;

		snprintf(bufroom, sizeof(bufroom), "R=%d(%d)", g_Vars.currentplayer->prop->rooms[0], g_Vars.currentplayer->cam_room);
		snprintf(bufx, sizeof(bufx), "%s%sx %4.0f", "", "", g_Vars.currentplayer->prop->pos.x);
		snprintf(bufy, sizeof(bufy), "%s%sy %4.0f", "", "", g_Vars.currentplayer->prop->pos.y);
		snprintf(bufz, sizeof(bufz), "%s%sz %4.0f", "", "", g_Vars.currentplayer->prop->pos.z);
		snprintf(bufdir, sizeof(bufdir), "%s %3d", &directions[(degrees + 22) / 45], degrees);

		x = viGetViewLeft() + 17;
		y = viGetViewTop() + 17;
		y2 = y + 10;
		gdl = text0f153628(gdl);
		gdl = text0f153a34(gdl, 0, y - 1, viGetWidth(), y2 + 1, 0x00000064);

		gdl = textRenderProjected(gdl, &x, &y, bufroom, g_CharsHandelGothicSm, g_FontHandelGothicSm, 0xffffffff, viGetWidth(), viGetHeight(), 0, 0);

		x = viGetViewLeft() + 87;
		gdl = textRenderProjected(gdl, &x, &y, bufx, g_CharsHandelGothicSm, g_FontHandelGothicSm, 0xffffffff, viGetWidth(), viGetHeight(), 0, 0);

		x = viGetViewLeft() + 141;
		gdl = textRenderProjected(gdl, &x, &y, bufy, g_CharsHandelGothicSm, g_FontHandelGothicSm, 0xffffffff, viGetWidth(), viGetHeight(), 0, 0);

		x = viGetViewLeft() + 195;
		gdl = textRenderProjected(gdl, &x, &y, bufz, g_CharsHandelGothicSm, g_FontHandelGothicSm, 0xffffffff, viGetWidth(), viGetHeight(), 0, 0);

		x = viGetViewLeft() + 249;
		gdl = textRenderProjected(gdl, &x, &y, bufdir, g_CharsHandelGothicSm, g_FontHandelGothicSm, 0xffffffff, viGetWidth(), viGetHeight(), 0, 0);

		gdl = text0f153780(gdl);
	}

	return gdl;
}
#endif

void lvFindThreatsForProp(struct prop *prop, bool inchild, struct coord *playerpos, bool *activeslots, f32 *distances)
{
	bool condition = true;
	struct defaultobj *obj;
	bool pass;
	f32 sp88;
	f32 sp84;
	f32 sp80;
	f32 sp76;
	s32 i;
	struct model *model;
	struct weaponobj *weapon;

	if (!inchild && prop->z < 0) {
		condition = false;
	}

	if (prop->obj
			&& (prop->flags & PROPFLAG_ONTHISSCREENTHISTICK)
			&& (prop->type == PROPTYPE_OBJ || prop->type == PROPTYPE_WEAPON)
			&& condition) {
		pass = false;
		obj = prop->obj;
		model = prop->obj->model;

		if (obj
				&& obj->type == OBJTYPE_AUTOGUN
				&& (obj->flags2 & (OBJFLAG2_AUTOGUN_MALFUNCTIONING1 | OBJFLAG2_AICANNOTUSE)) == 0) {
			pass = true;
		}

		if (obj && obj->modelnum == MODEL_SK_SHUTTLE) {
			pass = true;
		}

		weapon = (struct weaponobj *)prop->obj;

		if (weapon && prop->obj->type == OBJTYPE_WEAPON) {
			switch (weapon->weaponnum) {
			case WEAPON_GRENADE:
			case WEAPON_NBOMB:
			case WEAPON_TIMEDMINE:
			case WEAPON_PROXIMITYMINE:
			case WEAPON_REMOTEMINE:
				pass = true;
				break;
			case WEAPON_DRAGON:
				if (weapon->gunfunc == (u32)FUNC_SECONDARY) {
					pass = true;
				}
				break;
			}
		}

		if (obj->modelnum == MODEL_TARGET && frIsTargetOneHitExplodable(prop)) {
			pass = true;
		}

		if (pass) {
			for (i = 0; i < ARRAYCOUNT(g_Vars.currentplayer->trackedprops); i++) {
				if (g_Vars.currentplayer->trackedprops[i].prop == prop) {
					pass = false;
				}
			}
		}

		if (pass) {
			sp84 = -1;
			sp88 = -1;
			sp76 = -2;
			sp80 = -2;

			if (!modelGetScreenCoords(model, &sp76, &sp84, &sp80, &sp88)) {
				pass = false;
			}
		}

		if (pass) {
			f32 furtherestdist = 0;
			s32 index = -1;

			f32 sqdist =
				(prop->pos.f[0] - playerpos->f[0]) * (prop->pos.f[0] - playerpos->f[0]) +
				(prop->pos.f[1] - playerpos->f[1]) * (prop->pos.f[1] - playerpos->f[1]) +
				(prop->pos.f[2] - playerpos->f[2]) * (prop->pos.f[2] - playerpos->f[2]);

			for (i = 0; i < ARRAYCOUNT(g_Vars.currentplayer->trackedprops); i++) {
				if (!activeslots[i]) {
					index = i;
				}
			}

			if (index == -1) {
				// No slots available - consider replacing the furtherest
				for (i = 0; i < ARRAYCOUNT(g_Vars.currentplayer->trackedprops); i++) {
					if (distances[i] > furtherestdist) {
						furtherestdist = distances[i];
						index = i;
					}
				}

				if (sqdist >= furtherestdist) {
					index = -1;
				}
			}

			if (index >= 0) {
				g_Vars.currentplayer->trackedprops[index].prop = prop;
				g_Vars.currentplayer->trackedprops[index].x1 = sp84 - 2;
				g_Vars.currentplayer->trackedprops[index].x2 = sp76 + 2;
				g_Vars.currentplayer->trackedprops[index].y1 = sp88 - 2;
				g_Vars.currentplayer->trackedprops[index].y2 = sp80 + 2;
				g_Vars.currentplayer->targetset[index] = 0;
				activeslots[index] = true;
				distances[index] = sqdist;
			}
		}
	}

	if (prop->child) {
		lvFindThreatsForProp(prop->child, true, playerpos, activeslots, distances);
	}

	if (inchild && prop->next) {
		lvFindThreatsForProp(prop->next, inchild, playerpos, activeslots, distances);
	}
}

void func0f168f24(struct prop *prop, bool inchild, struct coord *playerpos, s32 *activeslots, f32 *distances)
{
	s32 i;
	f32 sp128;
	f32 sp124;
	f32 sp120;
	f32 sp116;
	struct model *model;

	for (i = 0; i != 4; i++) {
		if (g_Vars.currentplayer->trackedprops[i].prop == prop
				&& (prop->flags & PROPFLAG_ONTHISSCREENTHISTICK)) {
			model = NULL;

			if (prop->type == PROPTYPE_OBJ
					|| prop->type == PROPTYPE_WEAPON
					|| prop->type == PROPTYPE_DOOR) {
				model = g_Vars.currentplayer->trackedprops[i].prop->obj->model;
			} else {
				if (prop->type == PROPTYPE_CHR
						|| (prop->type == PROPTYPE_PLAYER
							&& playermgrGetPlayerNumByProp(prop) != g_Vars.currentplayernum)) {
					model = g_Vars.currentplayer->trackedprops[i].prop->chr->model;
				}
			}

			if (model) {
				sp124 = -1;
				sp128 = -1;
				sp116 = -2;
				sp120 = -2;

				if (modelGetScreenCoords(model, &sp116, &sp124, &sp120, &sp128)) {
					activeslots[i] = true;
					g_Vars.currentplayer->trackedprops[i].x1 = sp124 - 2;
					g_Vars.currentplayer->trackedprops[i].x2 = sp116 + 2;
					g_Vars.currentplayer->trackedprops[i].y1 = sp128 - 2;
					g_Vars.currentplayer->trackedprops[i].y2 = sp120 + 2;

					distances[i] =
						(prop->pos.f[0] - playerpos->f[0]) * (prop->pos.f[0] - playerpos->f[0]) +
						(prop->pos.f[1] - playerpos->f[1]) * (prop->pos.f[1] - playerpos->f[1]) +
						(prop->pos.f[2] - playerpos->f[2]) * (prop->pos.f[2] - playerpos->f[2]);
				}
			}
		}
	}

	if (prop->child) {
		func0f168f24(prop->child, true, playerpos, activeslots, distances);
	}

	if (inchild && prop->next) {
		func0f168f24(prop->next, inchild, playerpos, activeslots, distances);
	}
}

void lvFindThreats(void)
{
	s32 i;
	struct prop *prop;
	f32 distances[ARRAYCOUNT(g_Vars.currentplayer->trackedprops)] = {0};
	s32 activeslots[ARRAYCOUNT(g_Vars.currentplayer->trackedprops)] = {false};
	struct prop **propptr = g_Vars.endonscreenprops - 1;
	struct coord campos;

	campos.x = g_Vars.currentplayer->cam_pos.x;
	campos.y = g_Vars.currentplayer->cam_pos.y;
	campos.z = g_Vars.currentplayer->cam_pos.z;

	while (propptr >= g_Vars.onscreenprops) {
		prop = *propptr;

		if (prop) {
			func0f168f24(prop, false, &campos, activeslots, distances);
		}

		propptr--;
	}

	for (i = 0; i != ARRAYCOUNT(activeslots); i++) {
		if (!activeslots[i]) {
			g_Vars.currentplayer->trackedprops[i].prop = NULL;
			g_Vars.currentplayer->trackedprops[i].x1 = -1;
			g_Vars.currentplayer->trackedprops[i].x2 = -2;
		}
	}

	propptr = g_Vars.endonscreenprops - 1;

	while (propptr >= g_Vars.onscreenprops) {
		prop = *propptr;

		if (prop) {
			lvFindThreatsForProp(prop, false, &campos, activeslots, distances);
		}

		propptr--;
	}
}

Gfx *lvRenderFPS(Gfx *gdl)
{
	const f32 fps = videoGetAverageFPS();
	const u8 a = 160;
	s32 x = 27, y = 13;
	u32 color;
	char buffer[16];

	if (fps <= 30.f) {
		// red -> yellow
		color = 0xff000000 | a | ((u32)((fps / 30.f) * 255.f) << 16);
	} else if (fps <= 60.f) {
		// yellow -> green
		color = 0x00ff0000 | a | ((u32)((1.f - (fps - 30.f) / 30.f) * 255.f) << 24);
	} else if (fps <= 90.f) {
		// green -> cyan
		color = 0x00ff0000 | a | ((u32)(((fps - 60.f) / 30.f) * 255.f) << 8);
	} else {
		// cyan
		color = 0x00ffff00 | a;
	}

	if (g_CharsNumeric && g_FontNumeric) {
		snprintf(buffer, sizeof buffer, "%.2f", fps);

		gSPSetExtraGeometryModeEXT(gdl++, g_HudAlignModeL);

		gdl = text0f153628(gdl);
		gdl = textRender(gdl, &x, &y, buffer, g_CharsNumeric, g_FontNumeric, color, 0x000000a0, viGetWidth(), viGetHeight(), 0, 0);
		gdl = text0f153780(gdl);

		gSPClearExtraGeometryModeEXT(gdl++, g_HudAlignModeL);
	}

	return gdl;
}

/**
 * Renders a complete frame for all players, and also does some other game logic
 * that really doesn't belong here.
 *
 * This function is pretty big, so here's an overview of its structure:
 *
 * if (stage == STAGE_TITLE) {
 *     // title screen rendering
 * } else if (stage == STAGE_BOOTPAKMENU) {
 *     // boot pak menu rendering
 * } else if (stage == STAGE_CREDITS) {
 *     // credits rendering
 * } else {
 *     for (i = 0; i < numplayers; i++) {
 *         // rendering and logic per player
 *     }
 * }
 * // logic for auto-playing cutscene advancement
 *
 * The player loop takes up the majority of the function. In addition to
 * rendering the scene and HUD, it also handles the following logic:
 * - decreasing dizziness
 * - detecting if the prop being looked at is still valid
 * - pressing Z when using eyespy
 * - opening doors and reloading
 * - random static in the Infiltration intro cutscene
 * - combat boost activation and reverting
 */
Gfx *lvRender(Gfx *gdl)
{
	gSPSegment(gdl++, SPSEGMENT_PHYSICAL, 0x00000000);

#if VERSION >= VERSION_NTSC_1_0
	func0f0d5a7c();
#endif

	if (g_Vars.stagenum == STAGE_TITLE
			|| (g_Vars.stagenum == STAGE_TEST_OLD && titleIsKeepingMode())) {
		gSPDisplayList(gdl++, &var800613a0);

		if (debugIsZBufferDisabled()) {
			gSPDisplayList(gdl++, &var80061360);
		} else {
			gSPDisplayList(gdl++, &var80061380);
		}

		gdl = viPrepareZbuf(gdl);
		gdl = vi0000b1d0(gdl);

		gDPSetScissorFrac(gdl++, 0,
				viGetViewLeft() * 4.0f, viGetViewTop() * 4.0f,
				(viGetViewLeft() + viGetViewWidth()) * 4.0f,
				(viGetViewTop() + viGetViewHeight()) * 4.0f);

		gdl = titleRender(gdl);
		gdl = lvRenderFade(gdl);
	} else if (g_Vars.stagenum == STAGE_BOOTPAKMENU) {
		gSPClipRatio(gdl++, FRUSTRATIO_2);
		gSPDisplayList(gdl++, &var800613a0);
		gSPDisplayList(gdl++, &var80061380);

		setCurrentPlayerNum(0);
		viSetViewPosition(g_Vars.currentplayer->viewleft, g_Vars.currentplayer->viewtop);
		viSetFovAspectAndSize(g_Vars.currentplayer->fovy, g_Vars.currentplayer->aspect,
				g_Vars.currentplayer->viewwidth, g_Vars.currentplayer->viewheight);
		mtx00016748(1);

		gdl = vi0000b1d0(gdl);
		gdl = viRenderViewportEdges(gdl);
		gdl = bgScissorToViewport(gdl);
		gdl = menuRender(gdl);
	} else if (g_Vars.stagenum == STAGE_CREDITS) {
		gSPClipRatio(gdl++, FRUSTRATIO_2);
		gSPDisplayList(gdl++, &var800613a0);
		gSPDisplayList(gdl++, &var80061380);

		setCurrentPlayerNum(0);
		viSetViewPosition(g_Vars.currentplayer->viewleft, g_Vars.currentplayer->viewtop);
		viSetFovAspectAndSize(g_Vars.currentplayer->fovy, g_Vars.currentplayer->aspect,
				g_Vars.currentplayer->viewwidth, g_Vars.currentplayer->viewheight);
		mtx00016748(1);

		gdl = vi0000b1a8(gdl);
		gdl = vi0000b1d0(gdl);
		gdl = viRenderViewportEdges(gdl);
		gdl = creditsDraw(gdl);
	} else {
		// Normal stages
		s32 i;
		s32 playercount;
		Gfx *savedgdl;
#if VERSION >= VERSION_NTSC_1_0
		bool forcesingleplayer = (g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0)
			&& playerHasSharedViewport();
#else
		bool forcesingleplayer = (g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0)
			&& ((playerAnyInCutscene() && !g_MainIsEndscreen) || menuGetRoot() == MENUROOT_COOPCONTINUE);
#endif
		struct player *player;
		struct chrdata *chr;

		if (g_NetMode) {
			// tick all players, we'll skip the rendering
			forcesingleplayer = false;
		}

		playercount = forcesingleplayer ? 1 : PLAYERCOUNT();

		gSPClipRatio(gdl++, FRUSTRATIO_2);

		for (i = 0; i < playercount; i++) {
			bool islastplayer;
			u32 bluramount = 0;

			savedgdl = gdl;

			if (forcesingleplayer) {
				setCurrentPlayerNum(0);
				g_Vars.currentplayerindex = 0;
				islastplayer = true;
			} else {
				s32 nextplayernum = i + 1;
				setCurrentPlayerNum(playermgrGetPlayerAtOrder(i));
				islastplayer = playercount == nextplayernum;
			}

			// Calculate bluramount - this will be used later
			if (g_Vars.tickmode != TICKMODE_CUTSCENE) {
				player = g_Vars.currentplayer;
				chr = player->prop->chr;

				if (chr->blurdrugamount > 0
						&& !g_Vars.currentplayer->invincible
						&& !g_Vars.currentplayer->training) {
					bluramount = (chr->blurdrugamount * 130) / TICKS(5000) + 100;

					if (bluramount > 230) {
						bluramount = 230;
					}

					if (chr->blurdrugamount > TICKS(5000)) {
						chr->blurdrugamount = TICKS(5000);
					}

					chr->blurdrugamount -= g_Vars.lvupdate60 * (chr->blurnumtimesdied + 1);

					if (chr->blurdrugamount < 1) {
						chr->blurdrugamount = 0;
						chr->blurnumtimesdied = 0;
					}

					// reset the drug blur to 0 if it's disabled in MP settings
					if (g_Vars.mplayerisrunning && (g_MpSetup.options & MPOPTION_NODRUGBLUR)) {
						bluramount = 0;
					}
				}
			}

			bviewSetMotionBlur(bluramount);

			gSPDisplayList(gdl++, &var800613a0);

			if (debugIsZBufferDisabled()) {
				gSPDisplayList(gdl++, &var80061360);
			} else {
				gSPDisplayList(gdl++, &var80061380);
			}

			viSetViewPosition(g_Vars.currentplayer->viewleft, g_Vars.currentplayer->viewtop);
			viSetFovAspectAndSize(g_Vars.currentplayer->fovy, g_Vars.currentplayer->aspect,
					g_Vars.currentplayer->viewwidth, g_Vars.currentplayer->viewheight);
			mtx00016748(g_Vars.currentplayerstats->scale_bg2gfx);
			envTick();
			zbufSwap();
			gdl = viPrepareZbuf(gdl);
			gdl = vi0000b1d0(gdl);
			gdl = bgScissorToViewport(gdl);
			artifactsClear();

			if ((g_Vars.stagenum != STAGE_CITRAINING || (var80087260 <= 0 && g_MenuData.root != MENUROOT_MPSETUP))
					&& g_Vars.lvframenum <= 5
					&& !g_Vars.normmplayerisrunning
					&& g_Vars.tickmode != TICKMODE_CUTSCENE) {
				if (var80084050 < 6) {
					g_Vars.lockscreen = 1;
				}

				var80084050++;
			} else if (g_Vars.currentplayer->gunctrl.loadall
					&& var80075d60 == 2
					&& g_Vars.currentplayer->cameramode != CAMERAMODE_THIRDPERSON
					&& g_Vars.currentplayer->cameramode != CAMERAMODE_EYESPY
					&& var8009dfc0 == 0) {
				g_Vars.currentplayer->gunctrl.loadall = bgunLoadAll();
			}

			/* B-936/CI-menu-render DIAGNOSTIC (--debug-menu-render-state): at the
			 * lvRender decision point, the var8009dfc0 menu-bg branch (just below)
			 * SKIPS the live-world else block -- the only place props (desk PC /
			 * desk / chairs via bgRender->propsRender) and the player chrbody
			 * (chr0f028498/chrRender, i.e. Joanna) are submitted. This log prints
			 * WHY var8009dfc0 is true at the visible CI-menu frame: the menu-bg flag,
			 * the tickmode (the CI intro deliberately holds TICKMODE_CUTSCENE=6 but
			 * may settle out of it), the stagenum, and whether the scenario scene.glb
			 * backdrop is the active renderer. Player 0 only, fires a handful of times
			 * then stops so it never floods the log. */
			{
				extern s32 scenarioSceneRendererIsActive(void);
				static s32 s_menuRenderStateLogged = 0;
				static s32 s_menuRenderStateEnabled = -1;

				if (s_menuRenderStateEnabled < 0) {
					s_menuRenderStateEnabled = sysArgCheck("--debug-menu-render-state") ? 1 : 0;
				}

				if (s_menuRenderStateEnabled && g_Vars.currentplayernum == 0
						&& s_menuRenderStateLogged < 8) {
					s_menuRenderStateLogged++;
					sysLogPrintf(LOG_NOTE,
						"MENU.RENDER.STATE: var8009dfc0=%d tickmode=%d stagenum=0x%02x sceneActive=%d lockscreen=%d frame=%d branch=%s",
						(s32)var8009dfc0, (s32)g_Vars.tickmode, (s32)g_Vars.stagenum,
						scenarioSceneRendererIsActive(), (s32)g_Vars.lockscreen,
						(s32)g_Vars.lvframenum,
						g_Vars.lockscreen ? "lockscreen"
							: (var8009dfc0 ? "menu-bg(SKIPS props/chrs)" : "live-world(props/chrs)"));
				}
			}

			/* T-ENGINE-004 smoke observability for the render-side producer of
			 * pdgui_hotswap's per-frame queue. This remains inert outside a smoke
			 * and never changes branch selection. */
			if (smokeHarnessIsActive()
					&& g_MenuData.root == MENUROOT_MPENDSCREEN
					&& g_Vars.currentplayernum == 0) {
				static s32 s_mpEndscreenRenderTicks = 0;
				static s32 s_prevMenuActive = -1;
				static s32 s_prevBranch = -1;
				const s32 menuactive = g_Vars.currentplayer
					? g_Vars.currentplayer->menuisactive : -1;
				const s32 branch = g_Vars.lockscreen ? 1
					: (var8009dfc0 ? 2 : 3);

				s_mpEndscreenRenderTicks++;
				if (s_mpEndscreenRenderTicks == 1
						|| s_mpEndscreenRenderTicks == 5
						|| s_mpEndscreenRenderTicks == 30
						|| (s_mpEndscreenRenderTicks % 120) == 0
						|| s_prevMenuActive != menuactive
						|| s_prevBranch != branch) {
					sysLogPrintf(LOG_NOTE,
						"ENDSCREEN.RENDER: tick=%d player=%d mpindex=%d branch=%d lockscreen=%d render_bg=%d menu_active=%d count=%d cur=%p",
						s_mpEndscreenRenderTicks, g_Vars.currentplayernum,
						g_Vars.currentplayerstats
							? g_Vars.currentplayerstats->mpindex : -1,
						branch, g_Vars.lockscreen, var8009dfc0,
						menuactive, g_MenuData.count,
						(void *)g_Menus[0].curdialog);
				}
				s_prevMenuActive = menuactive;
				s_prevBranch = branch;
			}

			if (g_Vars.lockscreen) {
				gdl = bviewDrawMotionBlur(gdl, 0xffffffff, 255);
				g_Vars.lockscreen--;
			} else if (var8009dfc0) {
				gdl = viRenderViewportEdges(gdl);
				gdl = bgScissorToViewport(gdl);
				mtx00016748(1);

				if (g_Vars.currentplayer->menuisactive) {
					gdl = menuRender(gdl);
				} else {
					/* Charpreview FBO: render even when menus are inactive so
					 * standalone ImGui screens (modding hub, skin editor) get
					 * their 3D character previews.  menuRender calls it via
					 * menuRenderDialog, but when no menus are active we must
					 * call it directly.  Uses player 0's menu struct. */
					gdl = pdguiCharPreviewRenderGBI(gdl, &g_Menus[0]);
				}
			} else {
				if (var80075d60 == 2) {
					gdl = playerUpdateShootRot(gdl);
				}

				gdl = viRenderViewportEdges(gdl);

				/* FIX-C.1: Canonical GBI state reset at frame start.
				 * The GBI display list interpreter carries all state registers
				 * across frames.  Effects from the previous frame (sun flares,
				 * teleport beams, overexposure, translucent particles) can set
				 * blend modes, env color, prim color, and fog color that leak
				 * into the current frame's sky and early geometry.
				 *
				 * Emit a known-good baseline (5 commands, negligible cost) so
				 * that every frame starts from a deterministic render state.
				 * This eliminates the entire class of cross-frame state leakage
				 * documented as SP-10 in systemic-bugs.md. */
				gDPPipeSync(gdl++);
				gDPSetRenderMode(gdl++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
				gDPSetEnvColor(gdl++, 0xff, 0xff, 0xff, 0xff);
				gDPSetPrimColor(gdl++, 0, 0, 0xff, 0xff, 0xff, 0xff);
				gDPSetFogColor(gdl++, 0, 0, 0, 0);

				/* Sky renders first (before world geometry). Disable depth test
				 * and depth write so sky never interacts with the depth buffer.
				 * World geometry drawn after will correctly occlude sky via its
				 * own depth writes.  Clearing G_ZBUFFER ensures the PC renderer's
				 * depth_test flag is false (gfx_pc.cpp checks geometry_mode). */
				gSPClearGeometryMode(gdl++, G_ZBUFFER);
				/* B-936 money shot: when the scenario scene renderer is providing the
				 * CI room backdrop (held CI menu), skyRender's G_CYC_FILL fills the
				 * whole viewport with the environment sky colour (blue for CITRAINING)
				 * -- a fullscreen depth-off fill that draws AFTER the raw-GL scene
				 * renderer and would cover its tiled room. Skip it so the scene room is
				 * the backdrop. Outside the scene-renderer backdrop the sky fill is
				 * unchanged; during the playable range the live-world room fills the
				 * frame, so the skipped fill is not visible. */
				{
					extern s32 scenarioSceneRendererIsActive(void);
					if (!scenarioSceneRendererIsActive()) {
						gdl = skyRender(gdl);
					}
				}
				gSPSetGeometryMode(gdl++, G_ZBUFFER);
				bgTick();
				lightsTick();
				propsTickPlayer(islastplayer);
				scenarioTickChr(NULL);
				{
					extern s32 bootDebugPlaceBotNearPlayerPreRenderTick(void);
					(void)bootDebugPlaceBotNearPlayerPreRenderTick();
				}
				propsSort();
				autoaimTick();
				handsTickAttack();

				// glares calculated earlier on PC, before prop matrices turn into garbage
				bgCalculateGlaresForVisibleRooms();

				// Calculate lookingatprop
				if (PLAYERCOUNT() == 1
						|| g_Vars.coopplayernum >= 0
						|| g_Vars.antiplayernum >= 0
						|| (weaponHasFlag(bgunGetWeaponNum(HAND_RIGHT), WEAPONFLAG_AIMTRACK) && bmoveIsInSightAimMode())) {
					g_Vars.currentplayer->lookingatprop.prop = propFindAimingAt(HAND_RIGHT, false, FINDPROPCONTEXT_QUERY);

					if (g_Vars.currentplayer->lookingatprop.prop) {
						if (g_Vars.currentplayer->lookingatprop.prop->type == PROPTYPE_CHR
								|| g_Vars.currentplayer->lookingatprop.prop->type == PROPTYPE_PLAYER) {
							chr = g_Vars.currentplayer->lookingatprop.prop->chr;

							if ((chr->hidden & CHRHFLAG_CLOAKED) && !USINGDEVICE(DEVICE_IRSCANNER)) {
								g_Vars.currentplayer->lookingatprop.prop = NULL;
							}
						} else if (g_Vars.currentplayer->lookingatprop.prop->type == PROPTYPE_OBJ
								|| g_Vars.currentplayer->lookingatprop.prop->type == PROPTYPE_WEAPON
								|| g_Vars.currentplayer->lookingatprop.prop->type == PROPTYPE_DOOR) {
							struct defaultobj *obj = g_Vars.currentplayer->lookingatprop.prop->obj;

							if ((obj->flags3 & OBJFLAG3_REACTTOSIGHT) == 0) {
								if (g_Vars.stagenum != STAGE_CITRAINING
										|| (obj->modelnum != MODEL_TARGET
											&& obj->modelnum != MODEL_CIHUB
											&& obj->modelnum != MODEL_COMHUB)) {
									g_Vars.currentplayer->lookingatprop.prop = NULL;
								}
							}
						} else {
							g_Vars.currentplayer->lookingatprop.prop = NULL;
						}
					}
				} else {
					g_Vars.currentplayer->lookingatprop.prop = NULL;
				}

				if (gsetHasFunctionFlags(&g_Vars.currentplayer->hands[0].gset, FUNCFLAG_THREATDETECTOR)) {
					lvFindThreats();
				} else if (weaponHasFlag(bgunGetWeaponNum(HAND_RIGHT), WEAPONFLAG_AIMTRACK)) {
					s32 j;

					if (frIsInTraining()
							&& g_Vars.currentplayer->lookingatprop.prop
							&& bmoveIsInSightAimMode()) {
						func0f1a0924(g_Vars.currentplayer->lookingatprop.prop);
					} else if (lvUpdateTrackedProp(&g_Vars.currentplayer->lookingatprop, -1) == 0) {
						g_Vars.currentplayer->lookingatprop.prop = NULL;
					}

					for (j = 0; j < ARRAYCOUNT(g_Vars.currentplayer->trackedprops); j++) {
						if (!lvUpdateTrackedProp(&g_Vars.currentplayer->trackedprops[j], j)) {
							g_Vars.currentplayer->trackedprops[j].x1 = -1;
							g_Vars.currentplayer->trackedprops[j].x2 = -2;
						}
					}
				}

				// Handle eyespy Z presses
				if (g_Vars.currentplayer->eyespy
						&& (g_Vars.currentplayer->devicesactive & ~g_Vars.currentplayer->devicesinhibit & DEVICE_EYESPY)
						&& g_Vars.currentplayer->eyespy->camerabuttonheld) {
					if (g_Vars.currentplayer->eyespy->mode == EYESPYMODE_CAMSPY) {
						objectiveCheckHolograph(400);
						sndStart(var80095200, SFX_CAMSPY_SHUTTER, 0, -1, -1, -1, -1, -1);
					} else if (g_Vars.currentplayer->eyespy->mode == EYESPYMODE_DRUGSPY) {
						if (g_Vars.currentplayer->eyespydarts) {
							// Fire dart
							struct coord direction;
							sndStart(var80095200, SFX_DRUGSPY_FIREDART, 0, -1, -1, -1, -1, -1);
							g_Vars.currentplayer->eyespydarts--;

							direction.x = g_Vars.currentplayer->eyespy->look.x;
							direction.y = g_Vars.currentplayer->eyespy->look.y;
							direction.z = g_Vars.currentplayer->eyespy->look.z;

							projectileCreate(g_Vars.currentplayer->eyespy->prop, 0,
									&g_Vars.currentplayer->eyespy->prop->pos, &direction, WEAPON_TRANQUILIZER, NULL);
						} else {
							// No dart ammo
							sndStart(var80095200, SFX_FIREEMPTY, 0, -1, -1, -1, -1, -1);
						}
					} else { // EYESPYMODE_BOMBSPY
						struct coord vel = {0, 0, 0};
						struct gset gset = {WEAPON_GRENADE, 0, 0, FUNC_PRIMARY};
						explosionCreateSimple(g_Vars.currentplayer->eyespy->prop,
								&g_Vars.currentplayer->eyespy->prop->pos,
								g_Vars.currentplayer->eyespy->prop->rooms,
								EXPLOSIONTYPE_DRAGONBOMBSPY, 0);
						chrBeginDeath(g_Vars.currentplayer->eyespy->prop->chr, &vel, 0, 0, &gset, false, 0);
					}
				}

				// S311 per-frame interact-prompt refresh. `g_InteractProp`
				// is read every frame by the HUD overlay (pdguiInteractPromptRender
				// in port/fast3d/pdgui_interact_prompt.cpp) so the "[E] Pick up"
				// style pill can show/hide based on proximity + LOS to an
				// interactable prop. Before this call was moved here, the global
				// was only refreshed inside `currentPlayerInteract` -- i.e. on
				// the frame the activate button was pressed -- which meant the
				// prompt never appeared when walking up to a prop and never
				// disappeared when walking away. `propFindForInteract` itself
				// always starts by nulling `g_InteractProp`, so a per-frame call
				// is self-resetting (no stale pointer between ticks).
				propFindForInteract(false);

				// Handle opening doors and reloading
				if (g_Vars.currentplayer->bondactivateorreload & JO_ACTION_ACTIVATE) {
					if (!currentPlayerInteract(false)) {
						// n64 behavior: interact sucessful, cancel reload
						if (!PLAYER_EXTCFG().extcontrols || PLAYER_EXTCFG().usereloads) {
							g_Vars.currentplayer->bondactivateorreload = (g_Vars.currentplayer->bondactivateorreload & ~JO_ACTION_RELOAD);
						}
					}
				} else if (g_Vars.currentplayer->eyespy
						&& g_Vars.currentplayer->eyespy->active
						&& g_Vars.currentplayer->eyespy->opendoor) {
					currentPlayerInteract(true);
					g_Vars.currentplayer->bondactivateorreload = (g_Vars.currentplayer->bondactivateorreload & ~JO_ACTION_RELOAD);
				}

				if (g_Vars.currentplayer->bondactivateorreload & JO_ACTION_RELOAD) {
					if (g_Vars.currentplayer->hands[HAND_RIGHT].state != HANDSTATE_RELOAD) {
						bgunReloadIfPossible(HAND_RIGHT);
					}
					if (g_Vars.currentplayer->hands[HAND_LEFT].state != HANDSTATE_RELOAD) {
						bgunReloadIfPossible(HAND_LEFT);
					}
					g_Vars.currentplayer->bondactivateorreload = (g_Vars.currentplayer->bondactivateorreload & ~JO_ACTION_RELOAD);
				}

				propsTestForPickup();

				effectPresentationApplyLights();
				gdl = bgRender(gdl);
				chr0f028498(var80075d68 == 15 || g_AnimHostEnabled);
				gdl = propsRenderBeams(gdl);
				gdl = shardsRender(gdl);
				gdl = sparksRender(gdl);
				gdl = effectPresentationRenderWorld(gdl);
				gdl = weatherRender(gdl);

				if (g_NbombsActive) {
					gdl = nbombsRender(gdl);
				}

				/* B-246 round-5 diagnostic (2026-04-26): record which lvRender
				 * cascade branch fires (lockscreen vs menu-render vs normal).
				 * Mike's post-f83ca230 playtest log shows bgunRender silent;
				 * if the gate is here (menu-render path stealing the frame),
				 * this diag reveals it. Player 0 only, every 60 ticks. */
				if (g_Vars.currentplayernum == 0 && (g_Vars.lvframenum % 60) == 17) {
					sysLogPrintf(LOG_NOTE,
						"LOG.WPN.DIAG: lvRender cascade=normal (playerRenderHud about to fire) "
						"var80075d60=%d lockscreen=%d var8009dfc0=%d frame=%d",
						(s32)var80075d60, (s32)g_Vars.lockscreen, (s32)var8009dfc0,
						(s32)g_Vars.lvframenum);
				}
				if (var80075d60 == 2) {
					gdl = playerRenderHud(gdl);

#ifdef DEBUG
					gdl = lvRenderManPosIfEnabled(gdl);
#endif
				} else {
					gdl = boltbeamsRender(gdl);

					if (g_Vars.currentplayer->visionmode != VISIONMODE_XRAY) {
						gdl = bgRenderArtifacts(gdl);
					}
				}

				if (g_DebugScreenshotRgb <= 0) {
					static struct sndstate *g_CutsceneStaticAudioHandle = NULL;
					static s32 g_CutsceneStaticTimer = 100;
					static u8 g_CutsceneStaticActive = false;
					bool cutscenehasstatic = false;
					u32 alpha;

					if (g_Vars.tickmode == TICKMODE_CUTSCENE) {
						// This chunk of code is unreachable
						// (STAGE_TEST_OLD is not used)
#if VERSION < VERSION_PAL_BETA
						if (g_Vars.stagenum == STAGE_TEST_OLD) {
							f32 frac = 0;
							u32 colour;
							s32 cutsceneanimnum = playerCurrentCutsceneAnimNum();
							s32 cutsceneframe60 = playerCurrentCutsceneCurAnimFrame60();
							s32 endframe = animGetNumFrames(cutsceneanimnum) - 1;

							colour = 0;

							if (cutsceneframe60 < 90) {
								frac = 1.0f - (f32)cutsceneframe60 / 90.0f;
							}

							if (cutsceneanimnum != ANIM_CUT_OLD_TITLE_CAM_04) {
								if (cutsceneframe60 > endframe - 90) {
									frac = (cutsceneframe60 - endframe + 90) / 90.0f;
								}
							} else {
								if (cutsceneframe60 > endframe - 30) {
									colour = 0xffffff00;
									frac = (cutsceneframe60 - endframe + 30) / 30.0f;
								}
							}

							if (frac > 0) {
								alpha = 255 * frac;

								gDPPipeSync(gdl++);
								gDPSetRenderMode(gdl++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
								gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
								gDPSetPrimColorViaWord(gdl++, 0, 0, colour | alpha);

								gDPFillRectangle(gdl++,
									viGetViewLeft(),
									viGetViewTop(),
									viGetViewLeft() + viGetViewWidth(),
									viGetViewTop() + viGetViewHeight());

								gdl = text0f153838(gdl);
							}
						}
#endif

						// Handle visual effects in cutscenes
						switch (playerCurrentCutsceneAnimNum()) {
						case ANIM_CUT_CAVE_INTRO_CAM:
							// Horizon scanner in Air Base intro
							if (playerCurrentCutsceneCurAnimFrame60() > 839 && playerCurrentCutsceneCurAnimFrame60() < 1411) {
								gdl = bviewDrawHorizonScanner(gdl);
							}
							break;
						case ANIM_CUT_LUE_INTRO_CAM_01:
						case ANIM_CUT_LUE_INTRO_CAM_02:
						case ANIM_CUT_LUE_INTRO_CAM_03:
							{
								// Show static randomly in Infiltration intro
								s32 cutscenestatic = 0;
								cutscenehasstatic = true;

								if (g_CutsceneStaticAudioHandle == NULL) {
									sndStart(var80095200, SFX_INFIL_STATIC_LONG, &g_CutsceneStaticAudioHandle, -1, -1, -1, -1, -1);
								}

								g_CutsceneStaticTimer -= g_Vars.diffframe60;

								if (g_CutsceneStaticTimer < 0) {
									g_CutsceneStaticTimer = rngRandom() % TICKS(200) + TICKS(40);
									g_CutsceneStaticActive = false;
								}

								gdl = bviewDrawFilmInterlace(gdl, 0xffffffff, 0xffffffff);

								if (g_CutsceneStaticTimer < TICKS(15)) {
									if (g_CutsceneStaticActive == false) {
										g_CutsceneStaticActive = true;
										sndStart(var80095200, SFX_INFIL_STATIC_MEDIUM, NULL, -1, -1, -1, -1, -1);
									}

									cutscenestatic = 225 - g_CutsceneStaticTimer * PALUP(10);
								}

								// Consider a single frame of static, separate
								// to the main static above
								if (rngRandom() % 60 == 1) {
									cutscenestatic = 255;
									sndStart(var80095200, SFX_INFIL_STATIC_SHORT, NULL, -1, -1, -1, -1, -1);
								}

								if (cutscenestatic) {
									gdl = bviewDrawStatic(gdl, 0xffffffff, cutscenestatic);
								}
							}
							break;
						}
					}

					if (g_CutsceneStaticAudioHandle && !cutscenehasstatic) {
						audioStop(g_CutsceneStaticAudioHandle);
					}

					// Slayer rocket shows static when flying out of bounds
					if (g_Vars.currentplayer->visionmode == VISIONMODE_SLAYERROCKET
							&& g_Vars.tickmode != TICKMODE_CUTSCENE) {
						gdl = bviewDrawSlayerRocketInterlace(gdl, 0xffffffff, 0xffffffff);

						if (g_Vars.currentplayer->badrockettime > 0) {
							u32 slayerstatic = g_Vars.currentplayer->badrockettime * 255 / TICKS(90);

							if (slayerstatic > 255) {
								slayerstatic = 255;
							}

							gdl = bviewDrawStatic(gdl, 0x4fffffff, slayerstatic);
						}
					}

#if VERSION >= VERSION_NTSC_1_0
					if (g_Vars.currentplayer->visionmode == VISIONMODE_SLAYERROCKETSTATIC) {
						gdl = bviewDrawStatic(gdl, 0x4fffffff, 255);
						g_Vars.currentplayer->visionmode = VISIONMODE_NORMAL;
					}
#endif

					if (g_Vars.currentplayer->visionmode == VISIONMODE_XRAY
							&& g_Vars.tickmode != TICKMODE_CUTSCENE) {
						s32 xraything = 99;

						if (g_Vars.currentplayer->erasertime < TICKS(200)) {
#if PAL
							xraything = 249 - ((g_Vars.currentplayer->erasertime * 180 / 50) >> 2);
#else
							xraything = 249 - (g_Vars.currentplayer->erasertime * 3 >> 2);
#endif
						}

						gdl = bviewDrawZoomBlur(gdl, 0xffffffff, xraything, 1.05f, 1.05f);
					}

					// Handle combat boosts
					if ((g_Vars.speedpillchange > 0 && g_Vars.speedpillchange < (PAL ? 26 : 30))
							|| (g_Vars.speedpillwant && !g_Vars.speedpillon)
							|| (!g_Vars.speedpillwant && g_Vars.speedpillon)) {
						if (g_Vars.speedpillchange == (PAL ? 26 : 30) && !g_Vars.speedpillwant) {
							sndStart(var80095200, lvGetSlowMotionType() ? SFX_JO_BOOST_ACTIVATE : SFX_ARGH_JO_02AD, 0, -1, -1, -1, -1, -1);
						}

						if (g_Vars.speedpillchange < (PAL ? 13 : 15)) {
							gdl = bviewDrawZoomBlur(gdl, 0xffffffff,
									g_Vars.speedpillchange * 180 / (PAL ? 13 : 15),
									(f32)g_Vars.speedpillchange * (PAL ? 0.023076923564076f : 0.02000000141561f) + 1.1f,
									(f32)g_Vars.speedpillchange * (PAL ? 0.023076923564076f : 0.02000000141561f) + 1.1f);
							gdl = playerDrawFade(gdl, 0xff, 0xff, 0xff,
									g_Vars.speedpillchange * (PAL ? 0.0076923076994717f : 0.0066666668280959f));
						} else {
							gdl = bviewDrawZoomBlur(gdl, 0xffffffff,
									((PAL ? 26 : 30) - g_Vars.speedpillchange) * 180 / (PAL ? 13 : 15),
									(f32)((PAL ? 26 : 30) - g_Vars.speedpillchange) * (PAL ? 0.023076923564076f : 0.02000000141561f) + 1.1f,
									(f32)((PAL ? 26 : 30) - g_Vars.speedpillchange) * (PAL ? 0.023076923564076f : 0.02000000141561f) + 1.1f);
							gdl = playerDrawFade(gdl, 0xff, 0xff, 0xff,
									((PAL ? 26.0f : 30.0f) - g_Vars.speedpillchange) * (PAL ? 0.0076923076994717f : 0.0066666668280959f));
						}

						if (g_Vars.currentplayernum == 0) {
							if (g_Vars.speedpillwant) {
								g_Vars.speedpillchange++;
							} else {
								g_Vars.speedpillchange--;
							}
						}

						if (g_Vars.speedpillchange > (PAL ? 26 : 30)) {
							g_Vars.speedpillchange = (PAL ? 26 : 30);
						} else if (g_Vars.speedpillchange < 0) {
							g_Vars.speedpillchange = 0;
						}
					}

					if (g_Vars.speedpillchange > (PAL ? 13 : 15)) {
						g_Vars.speedpillon = true;
					} else {
						g_Vars.speedpillon = false;
					}

					if (bluramount) {
						bviewClearMotionBlur();
						gdl = bviewDrawMotionBlur(gdl, 0xffffffff, bluramount);
					}

					// Handle blur effect in cutscenes (Extraction intro?)
					if (g_Vars.tickmode == TICKMODE_CUTSCENE) {
						f32 cutsceneblurfrac = playerGetCutsceneBlurFrac();

						if (cutsceneblurfrac > 0) {
#if VERSION < VERSION_PAL_BETA
							u32 stack;
#endif
							gdl = bviewDrawMotionBlur(gdl, 0xffffff00, cutsceneblurfrac * 255);
						}
					}

#if VERSION >= VERSION_PAL_FINAL
					if (bluramount);
					if (bluramount);
					if (bluramount);
#elif VERSION >= VERSION_NTSC_1_0
					if (bluramount);
					if (bluramount);
#else
					if (bluramount);
					if (bluramount);
					if (bluramount);
#endif

					if (debugGetMotionBlur() == 1) {
						gdl = bviewDrawMotionBlur(gdl, 0xffffff00, 128);
					} else if (debugGetMotionBlur() == 2) {
						gdl = bviewDrawMotionBlur(gdl, 0xffffff00, 192);
					} else if (debugGetMotionBlur() == 3) {
						gdl = bviewDrawMotionBlur(gdl, 0xffffff00, 230);
					}

					// Render white when teleporting
					if (g_Vars.currentplayer->teleportstate > TELEPORTSTATE_INACTIVE) {
						alpha = 0;

						if (g_Vars.currentplayer->teleportstate == TELEPORTSTATE_WHITE) {
							alpha = 255;
						}

						if (g_Vars.currentplayer->teleportstate == TELEPORTSTATE_EXITING
								&& g_Vars.currentplayer->teleporttime < 16) {
							alpha = -g_Vars.currentplayer->teleporttime * 16 + 240;
						}

						if (g_Vars.currentplayer->teleportstate == TELEPORTSTATE_ENTERING) {
							if (g_Vars.currentplayer->teleporttime > 32) {
								alpha = g_Vars.currentplayer->teleporttime * 16 - 512;
							}

							if (g_Vars.currentplayer->teleporttime == 48) {
								alpha = 255;
							}
						}

						if (alpha) {
							gdl = text0f153628(gdl);
							gdl = text0f153a34(gdl,
									viGetViewLeft(), viGetViewTop(),
									viGetViewLeft() + viGetViewWidth(),
									viGetViewTop() + viGetViewHeight(), 0xffffff00 | alpha);
							gdl = text0f153780(gdl);
						}
					}
				}

#if VERSION >= VERSION_NTSC_1_0
				gdl = scenarioRenderHud(gdl);
				gdl = lvRenderFade(gdl);
#else
				gdl = lvRenderFade(gdl);
				gdl = scenarioRenderHud(gdl);
#endif

				if (g_FrIsValidWeapon) {
					gdl = frRenderHud(gdl);
				}

				if (debugGetTilesDebugMode() != 0
						|| debugGetPadsDebugMode() != 0
						|| debug0f11eea8()
						|| debug0f11ef80()
						|| debugIsChrStatsEnabled()
						|| debug0f11ee40()) {
#if VERSION < VERSION_NTSC_1_0
					RoomNum spc8[21];
					RoomNum spb0[11];
					RoomNum sp9c[10];
					s32 j;

					sp9c[0] = g_Vars.currentplayer->memcamroom;
					sp9c[1] = -1;

					for (j = 0; sp9c[j] != -1; j++) {
						spc8[j] = sp9c[j];
					}

					spc8[j] = -1;

					for (j = 0; sp9c[j] != -1; j++) {
						bgRoomGetNeighbours(sp9c[j], spb0, 10);
						roomsAppend(spb0, spc8, 20);
					}

					if (debugIsChrStatsEnabled()) {
						gdl = chrsRenderChrStats(gdl, spc8);
					}
#endif
				}

				gdl = skyRenderOverexposure(gdl);
				gdl = amRender(gdl);
				mtx00016748(1);

				if (g_Vars.currentplayer->menuisactive) {
					gdl = menuRender(gdl);
				}

				mtx00016748(g_Vars.currentplayerstats->scale_bg2gfx);

				if (g_Vars.mplayerisrunning) {
					gdl = mpRenderModalText(gdl);
				}

				if (g_Vars.currentplayer->dostartnewlife) {
					if (g_NetMode != NETMODE_CLIENT)
						playerStartNewLife();
				}
			}

			artifactsTick();

			if ((g_NetMode && i) || i >= MAX_LOCAL_PLAYERS) {
				gdl = savedgdl;
			}

			if ((g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0)
#if VERSION >= VERSION_NTSC_1_0
					&& playerHasSharedViewport()
#else
					&& ((playerAnyInCutscene() && !g_MainIsEndscreen) || menuGetRoot() == MENUROOT_COOPCONTINUE)
#endif
					&& g_Vars.currentplayernum != 0) {
				gdl = savedgdl;
			}
		} // end of player loop
	} // end of stage if-statements

	if (g_Vars.autocutplaying && g_Vars.autocutfinished) {
		g_Vars.autocutplaying = false;
		g_Vars.autocutfinished = false;

		if (g_Vars.autocutgroupskip) {
			g_Vars.autocutgroupcur = -1;
			g_Vars.autocutgroupleft = 0;
		}

		if (g_Vars.autocutgroupcur < 0 && g_Vars.autocutgroupleft <= 0) {
			mainChangeToStage(STAGE_TITLE);
		}
	}

	// Advance the cutscenes when autoplaying
	if (!g_Vars.autocutplaying && g_Vars.autocutgroupcur >= 0 && g_Vars.autocutgroupleft > 0) {
		hudmsgRemoveAll();

		g_Vars.autocutnum = g_Cutscenes[g_Vars.autocutgroupcur].scene;

#if VERSION < VERSION_NTSC_1_0
		if (mainGetStageNum() != g_Cutscenes[g_Vars.autocutgroupcur].stage)
#endif
		{
			g_MissionConfig.iscoop = false;
			g_Vars.mplayerisrunning = false;
			g_Vars.normmplayerisrunning = false;
			g_Vars.bondplayernum = 0;
			g_Vars.coopplayernum = -1;
			g_Vars.antiplayernum = -1;
			g_MissionConfig.isanti = false;
			setNumPlayers(1);
			titleSetNextMode(TITLEMODE_SKIP);
			g_MissionConfig.difficulty = DIFF_A;
			lvSetDifficulty(DIFF_A);
			g_MissionConfig.stageindex = g_Cutscenes[g_Vars.autocutgroupcur].mission;
			g_MissionConfig.stagenum = g_Cutscenes[g_Vars.autocutgroupcur].stage;
			/* Phase 2: populate PRIMARY catalog ID string field */
			{ const char *cid = catalogStageIdByStagenum(g_MissionConfig.stagenum); if (cid) { strncpy(g_MissionConfig.stage_id, cid, sizeof(g_MissionConfig.stage_id) - 1); g_MissionConfig.stage_id[sizeof(g_MissionConfig.stage_id) - 1] = '\0'; } else { g_MissionConfig.stage_id[0] = '\0'; } }
			titleSetNextStage(g_Cutscenes[g_Vars.autocutgroupcur].stage);
			mainChangeToStage(g_Cutscenes[g_Vars.autocutgroupcur].stage);
		}

		g_Vars.autocutgroupleft--;

		if (g_Vars.autocutgroupleft > 0) {
			g_Vars.autocutgroupcur++;
		} else {
			g_Vars.autocutgroupcur = -1;
		}
	}

	/* MASTER-C6 / REND-C1: Charpreview direct entry point.
	 * Fires the preview render hook once per frame from a stage-agnostic
	 * location, so standalone ImGui screens (modding hub on the title,
	 * pause/scorecard overlay during gameplay, skin editor) see a live 3D
	 * model preview instead of a permanent black FBO.  Idempotent — if the
	 * legacy menuRenderDialog path or the gameplay-no-menu branch above
	 * already fired the hook this frame, this call is a no-op. */
	gdl = pdguiCharPreviewRenderDirect(gdl);

	gDPSetScissor(gdl++, G_SC_NON_INTERLACE, 0, 0, viGetWidth(), viGetHeight());

	if (videoGetDisplayFPS()) {
		gdl = lvRenderFPS(gdl);
	}

#if VERSION < VERSION_NTSC_1_0
	if ((uintptr_t)gdl < (uintptr_t)g_GfxBuffers[g_GfxActiveBufferIndex]
			|| (uintptr_t)gdl > (uintptr_t)g_GfxBuffers[g_GfxActiveBufferIndex + 1]) {
		crashSetMessage("lv.c Master DL overrun!");
		CRASH();
	}
#endif

	return gdl;
}

const char var7f1b7730[] = "fr: %d\n";

u32 g_CutsceneTime240_60 = 0;

#if VERSION >= VERSION_NTSC_1_0
u32 var800840a8 = 0;
u32 var800840ac = 0;
u32 var800840b0 = 0;
#else
u32 var80086930nb = 0;
u32 var800840a8 = 0;
u32 var800840ac = 0;
u32 var800840b0 = 0;
#endif

u32 var800840b4 = 0;
u32 var800840b8 = 0;
u32 var800840bc = 0;

void lvUpdateSoloHandicaps(void)
{
	if (g_Vars.antiplayernum >= 0) {
		if (g_Difficulty == DIFF_A) {
			g_CctvWaitScale = 2;
			g_CctvDamageRxScale = 2;
			g_AutogunAccuracyScale = 0.5f;
			g_AutogunDamageTxScale = 0.5f;
			g_AutogunDamageRxScale = 2;
			g_EnemyAccuracyScale = 0.5f;
			g_PlayerDamageRxScale = 0.35f;
			g_PlayerDamageTxScale = 4;
			g_ExplosionDamageTxScale = 0.25f;
			g_AutoAimScale = 1.5f;
			g_AmmoQuantityScale = 3;
			g_AttackWalkDurationScale = 0.2f;
		} else if (g_Difficulty == DIFF_SA) {
			g_CctvWaitScale = 2;
			g_CctvDamageRxScale = 1.5f;
			g_AutogunAccuracyScale = 0.5f;
			g_AutogunDamageTxScale = 0.5f;
			g_AutogunDamageRxScale = 1.5f;
			g_EnemyAccuracyScale = 0.6f;
			g_PlayerDamageRxScale = 0.5f;
			g_PlayerDamageTxScale = 3;
			g_ExplosionDamageTxScale = 0.25f;
			g_AutoAimScale = 1.1f;
			g_AmmoQuantityScale = 2.5f;
			g_AttackWalkDurationScale = 0.5f;
		} else {
			g_CctvWaitScale = 2;
			g_CctvDamageRxScale = 1;
			g_AutogunAccuracyScale = 0.5f;
			g_AutogunDamageTxScale = 0.5f;
			g_AutogunDamageRxScale = 1;
			g_EnemyAccuracyScale = 0.7f;
			g_PlayerDamageRxScale = 0.65f;
			g_PlayerDamageTxScale = 2;
			g_ExplosionDamageTxScale = 0.25f;
			g_AutoAimScale = 0.75f;
			g_AmmoQuantityScale = 2;
			g_AttackWalkDurationScale = 1;
		}
	} else if (g_Vars.coopplayernum >= 0) {
		if (g_Difficulty == DIFF_A) {
			g_CctvWaitScale = 2;
			g_CctvDamageRxScale = 2;
			g_AutogunAccuracyScale = 0.5f;
			g_AutogunDamageTxScale = 0.5f;
			g_AutogunDamageRxScale = 2;
			g_EnemyAccuracyScale = 0.6f;
			g_PlayerDamageRxScale = 0.5f;
			g_PlayerDamageTxScale = 2;
			g_ExplosionDamageTxScale = 0.25f;
			g_AutoAimScale = 1.5f;
			g_AmmoQuantityScale = 2;
			g_AttackWalkDurationScale = 0.2f;
		} else if (g_Difficulty == DIFF_SA) {
			g_CctvWaitScale = 1;
			g_CctvDamageRxScale = 1;
			g_AutogunAccuracyScale = 0.75f;
			g_AutogunDamageTxScale = 1;
			g_AutogunDamageRxScale = 1;
			g_EnemyAccuracyScale = 0.75f;
			g_PlayerDamageRxScale = 1;
			g_PlayerDamageTxScale = 1;
			g_ExplosionDamageTxScale = 1;
#if VERSION >= VERSION_JPN_FINAL
			g_AutoAimScale = 0.75f;
#else
			g_AutoAimScale = g_Jpn ? 1.1f : 0.75f;
#endif
			g_AmmoQuantityScale = 1.5f;
			g_AttackWalkDurationScale = 0.5f;
		} else {
			g_CctvWaitScale = 1;
			g_CctvDamageRxScale = 1;
			g_AutogunAccuracyScale = 1;
			g_AutogunDamageTxScale = 1.5f;
			g_AutogunDamageRxScale = 1;
			g_EnemyAccuracyScale = 1.5f;
			g_PlayerDamageRxScale = 1.5f;
			g_PlayerDamageTxScale = 1;
			g_ExplosionDamageTxScale = 1.5f;
#if VERSION >= VERSION_JPN_FINAL
			g_AutoAimScale = 0.2f;
#else
			g_AutoAimScale = g_Jpn ? 0.75f : 0.2f;
#endif
			g_AmmoQuantityScale = 1;
			g_AttackWalkDurationScale = 1;
		}
	} else {
		if (g_Difficulty == DIFF_A) {
			f32 totalhealth;
			f32 frac = 1;

			if (g_Vars.coopplayernum < 0 && g_Vars.antiplayernum < 0) {
				totalhealth = playerGetHealthFrac() + playerGetShieldFrac();

				if (totalhealth <= 0.125f) {
					frac = 0.5f;
				} else if (totalhealth <= 0.6f) {
					frac = (totalhealth - 0.125f) * 0.5f / 0.47500002384186f + 0.5f;
				}
			}

			g_CctvWaitScale = 2;
			g_CctvDamageRxScale = 2;
			g_AutogunAccuracyScale = 0.5f * frac;
			g_AutogunDamageTxScale = 0.5f * frac;
			g_AutogunDamageRxScale = 2;
			g_EnemyAccuracyScale = 0.6f;
			g_PlayerDamageRxScale = 0.5f * frac;
			g_PlayerDamageTxScale = 2;
			g_ExplosionDamageTxScale = 0.25f * frac;
			g_AutoAimScale = 1.5f;
			g_AmmoQuantityScale = 2;
			g_AttackWalkDurationScale = 0.2f;
		} else if (g_Difficulty == DIFF_SA) {
			g_CctvWaitScale = 1;
			g_CctvDamageRxScale = 1;
			g_AutogunAccuracyScale = 0.75f;
			g_AutogunDamageTxScale = 0.75f;
			g_AutogunDamageRxScale = 1;
			g_EnemyAccuracyScale = 0.8f;
			g_PlayerDamageRxScale = 0.6f;
			g_PlayerDamageTxScale = 1;
			g_ExplosionDamageTxScale = 0.75f;
#if VERSION >= VERSION_JPN_FINAL
			g_AutoAimScale = 0.75f;
#else
			g_AutoAimScale = g_Jpn ? 1.1f : 0.75f;
#endif
			g_AmmoQuantityScale = 1.5f;
			g_AttackWalkDurationScale = 0.5f;
		} else if (g_Difficulty == DIFF_PA) {
			g_CctvWaitScale = 1;
			g_CctvDamageRxScale = 1;
			g_AutogunAccuracyScale = 1;
			g_AutogunDamageTxScale = 1;
			g_AutogunDamageRxScale = 1;
			g_EnemyAccuracyScale = 1.175f;
			g_PlayerDamageRxScale = 1;
			g_PlayerDamageTxScale = 1;
			g_ExplosionDamageTxScale = 1;
#if VERSION >= VERSION_JPN_FINAL
			g_AutoAimScale = 0.2f;
#else
			g_AutoAimScale = g_Jpn ? 0.75f : 0.2f;
#endif
			g_AmmoQuantityScale = 1;
			g_AttackWalkDurationScale = 1;
		} else if (g_Difficulty == DIFF_PD) {
			g_CctvWaitScale = 1;
			g_CctvDamageRxScale = 1;
			g_AutogunAccuracyScale = 1;
			g_AutogunDamageTxScale = 1;
			g_AutogunDamageRxScale = 1;
			g_EnemyAccuracyScale = 1.1f;
			g_PlayerDamageRxScale = 1;
			g_PlayerDamageTxScale = 1;
			g_ExplosionDamageTxScale = 1;
			g_AutoAimScale = 1;
			g_AmmoQuantityScale = 1;
			g_AttackWalkDurationScale = 1;
		}
	}
}

#if PIRACYCHECKS

#if PAL
#define SUBAMOUNT 6661
#else
#define SUBAMOUNT 54321
#endif

s32 sub54321(s32 value)
{
	return value - SUBAMOUNT;
}
#endif

void lvUpdateCutsceneTime(void)
{
	if (playerAnyCutsceneInProgress()) {
		g_CutsceneTime240_60 += g_Vars.lvupdate60;
		return;
	}

	g_CutsceneTime240_60 = 0;
}

s32 lvGetSlowMotionType(void)
{
#if PIRACYCHECKS
#if PAL
	u32 addr = sub54321(0xb0000340 + SUBAMOUNT);
	u32 actual;
	u32 expected = sub54321(0x0330c820 + SUBAMOUNT);
#else
	u32 addr = sub54321(0xb0000a5c + SUBAMOUNT);
	u32 actual;
	u32 expected = sub54321(0x1740fff9 + SUBAMOUNT);
#endif

	osPiReadIo(addr, &actual);

	if (actual != expected) {
		u32 *ptr = (u32 *)&rspbootTextStart;
		u32 *end = (u32 *)(uintptr_t)ptr + 1024;

		while (ptr < end) {
			*ptr += 8;
			ptr++;
		}
	}
#endif

	if (g_Vars.normmplayerisrunning) {
		if (g_MpSetup.options & MPOPTION_SLOWMOTION_ON) {
			return SLOWMOTION_ON;
		}
		if (g_MpSetup.options & MPOPTION_SLOWMOTION_SMART) {
			return SLOWMOTION_SMART;
		}
	} else {
		if (cheatIsActive(CHEAT_SLOMO)) {
			return SLOWMOTION_ON;
		}
		if (debugGetSlowMotion() == SLOWMOTION_ON) {
			return SLOWMOTION_ON;
		}
		if (debugGetSlowMotion() == SLOWMOTION_SMART) {
			return SLOWMOTION_SMART;
		}
	}

	return SLOWMOTION_OFF;
}

void lvTick(void)
{
	s32 j;
	s32 i;
	static s32 s_LvTickFirstRun = 1;
	static s32 s_LvTickLastStage = -1;
	/* L-static: reset first-run flag when stage changes */
	if (s_LvTickLastStage != (s32)g_Vars.stagenum) {
		s_LvTickFirstRun = 1;
		s_LvTickLastStage = (s32)g_Vars.stagenum;
	}
	if (s_LvTickFirstRun) {
		sysLogPrintf(LOG_NOTE, "TICK: lvTick enter tick=%d stagenum=0x%02x g_MpNumChrs=%d", g_Vars.lvframe60, g_Vars.stagenum, g_MpNumChrs);
		scenarioSourceMissionGraphRecordPhase("active", "lvTick.start");
		scenarioSourceLevelGraphRecordTick("lvTick.start");
		s_LvTickFirstRun = 0;
	}

	/* S301: per-frame lvTick breadcrumb. Distinct from the mainTick
	 * heartbeat because lvTick runs only when the stage is loaded — a
	 * crash between mainTick and lvTick will show the last mainTick
	 * breadcrumb with no matching LVTICK, which tells us the death was
	 * in the outer loop (sched / gfx / input). A pair means the death
	 * was somewhere inside lvTick. */
	crashBreadcrumbPush("LVTICK frame=%d stage=0x%02x update240=%d paused=%d",
		g_Vars.lvframe60, (u32)g_Vars.stagenum,
		g_Vars.lvupdate240, lvIsPaused());

	/* B-126: Periodic heartbeat log (every 30s / 1800 frames) to help
	 * diagnose silent crashes — last heartbeat before death pinpoints timing.
	 * S191: interval halved (60s→30s); added last-chr-tick index (B-112 probe)
	 * and full NET.WATCHDOG/NET.HEARTBEAT dump via netHeartbeatLog(). */
	if (g_Vars.lvframe60 > 0 && (g_Vars.lvframe60 % 1800) == 0) {
		sysLogPrintf(LOG_NOTE,
			"NET.HEARTBEAT: frame=%d (~%ds) chrs=%d stage=0x%02x last_chr_idx=%d",
			g_Vars.lvframe60, g_Vars.lvframe60 / 60,
			g_MpNumChrs, g_Vars.stagenum, g_ChrLastTickedIndex);
		netHeartbeatLog(); /* NET.WATCHDOG + per-peer RTT/silence; no-ops cleanly if no host */
	}

	lvCheckPauseStateChanged();

#if VERSION >= VERSION_NTSC_1_0
	if (g_Vars.pakstocheck) {
		paksTick();
	}
#endif

	if (g_Vars.joydisableframestogo > 0) {
		g_Vars.joydisableframestogo--;
	} else if (g_Vars.joydisableframestogo == 0) {
#if VERSION >= VERSION_NTSC_1_0
		joyUnlockCyclicPolling();
#else
		if (!joyIsCyclicPollingEnabled()) {
			joyEnableCyclicPolling(3278, "lv.c");
		}
#endif

		if (g_Vars.stagenum == STAGE_TITLE
				|| g_Vars.stagenum == STAGE_BOOTPAKMENU
				|| g_Vars.stagenum == STAGE_CREDITS) {
			g_Vars.paksneededforgame = 0;
		} else {
			g_Vars.paksneededforgame = 0x1f;
			pakEnableRumbleForAllPlayers();
		}

		g_Vars.joydisableframestogo = -1;
	}

	for (j = 0; j < PLAYERCOUNT(); j++) {
		if (!g_Vars.players[j]) continue;
		g_Vars.players[j]->hands[HAND_LEFT].hasdotinfo = false;
		g_Vars.players[j]->hands[HAND_RIGHT].hasdotinfo = false;
	}

	if (lvIsPaused()) {
		g_Vars.lvupdate240 = 0;
	} else if (mpIsPaused()) {
		g_Vars.lvupdate240 = 0;

		for (j = 0; j < PLAYERCOUNT(); j++) {
			if (!g_Vars.players[j]) continue;
			g_Vars.players[j]->joybutinhibit = 0xffffefff;
		}
	} else {
		s32 slowmo = lvGetSlowMotionType();
		g_Vars.lvupdate240 = g_Vars.diffframe240;

		if (slowmo == SLOWMOTION_ON) {
			if (g_Vars.speedpillon == false || playerAnyCutsceneInProgress()) {
				if (g_Vars.lvupdate240 > LV_SLOMO_TICK_CAP) {
					g_Vars.lvupdate240 = LV_SLOMO_TICK_RATE;
				}
			}
		} else if (slowmo == SLOWMOTION_SMART) {
			// Smart slow motion - activates if an enemy chr is nearby
			if (g_Vars.speedpillon == false || playerAnyCutsceneInProgress()) {
				if (g_Vars.mplayerisrunning) {
					bool foundnearbychr = false;
					s32 playernum;

					// Check if another player is in a nearby room
					for (playernum = 0; playernum < PLAYERCOUNT() && !foundnearbychr; playernum++) {
						if (!g_Vars.players[playernum] || !g_Vars.players[playernum]->prop) continue;
						if (g_Vars.players[playernum]->isdead == false) {
							RoomNum *rooms = g_Vars.players[playernum]->prop->rooms;
							s32 r;

							for (r = 0; rooms[r] != -1 && !foundnearbychr; r++) {
								s32 otherplayernum;
								for (otherplayernum = 0; otherplayernum < PLAYERCOUNT(); otherplayernum++) {
									if (!g_Vars.players[otherplayernum]) continue;
									if (playernum != otherplayernum
											&& g_Vars.players[otherplayernum]->isdead == false
											&& bgRoomIsOnPlayerScreen(rooms[r], otherplayernum)) {
										foundnearbychr = true;
									}
								}
							}
						}
					}

					if (foundnearbychr) {
						if (g_Vars.lvupdate240 > LV_SLOMO_TICK_CAP) {
							g_Vars.lvupdate240 = LV_SLOMO_TICK_RATE;
						}
					} else {
						if (g_Vars.lvupdate240 > TICKS(8)) {
							g_Vars.lvupdate240 = TICKS(8);
						}
					}
				} else {
					if (g_Vars.lvupdate240 > LV_SLOMO_TICK_CAP) {
						g_Vars.lvupdate240 = LV_SLOMO_TICK_RATE;
					}
				}
			}
		} else {
			// Slow motion settings are off
			if (g_Vars.speedpillon && !playerAnyCutsceneInProgress()) {
				if (g_Vars.lvupdate240 > LV_SLOMO_TICK_CAP) {
					g_Vars.lvupdate240 = LV_SLOMO_TICK_RATE;
				}
			}
		}
	}

	/* B-204/B-205: global catch-up cap (normal + slowmo paths above). */
	if (g_Vars.lvupdate240 > LV_UPDATE240_CATCHUP_CAP) {
		g_Vars.lvupdate240 = LV_UPDATE240_CATCHUP_CAP;
	}

	g_Vars.lvupdate60 = g_Vars.lvupdate240 + g_Vars.lvupdate240rem;
	g_Vars.lvupdate240rem = g_Vars.lvupdate60 & 3;
	g_Vars.lvupdate60 >>= 2;

	if (g_Vars.lvupdate240 > 0) {
		g_Vars.lvframenum++;
	}

	g_Vars.lvupdate60f = g_Vars.lvupdate240 * 0.25f;
	g_Vars.lvframe60 += g_Vars.lvupdate60;
	g_Vars.lvframe240 += g_Vars.lvupdate240;
	g_Vars.lvupdate60frealprev = g_Vars.lvupdate60freal;
	g_Vars.lvupdate60freal = PALUPF(g_Vars.lvupdate60f);

	bgunTickBoost();
	hudmsgsTick();

	g_NumReasonsToEndMpMatch = 0;

	// Handle MP match ending
	if (g_Vars.normmplayerisrunning && STAGE_IS_GAMEPLAY(g_Vars.stagenum)) {
		/* c3845 (2026-06-23): test-only seconds-granularity time-limit
		 * override for the two-process match smoke. The wire/config time
		 * limit (g_MpSetup.timelimit) is minutes-only (6-bit), so a clean
		 * ~35 s regression match is not expressible through it. When
		 * --match-timelimit-sec <n> was passed, force g_MpTimeLimit60 to
		 * SECSTOTIME60(n) frame-units here, once, so the comparison below
		 * fires at n seconds. No effect (returns 0) when the flag is unset. */
		{
			extern s32 bootGetMatchTimeLimitSec(void);
			s32 forced_sec = bootGetMatchTimeLimitSec();
			if (forced_sec > 0) {
				s32 forced60 = SECSTOTIME60(forced_sec);
				if (g_MpTimeLimit60 != forced60) {
					g_MpTimeLimit60 = forced60;
					sysLogPrintf(LOG_NOTE,
						"MATCH: timelimit override applied seconds=%d g_MpTimeLimit60=%d",
						forced_sec, g_MpTimeLimit60);
				}
			}
		}
		if (g_MpTimeLimit60 > 0) {
			s32 elapsed = g_StageTimeElapsed60;
			s32 nexttime = g_Vars.lvupdate60 + g_StageTimeElapsed60;
			s32 warntime = TICKS(g_MpTimeLimit60) - TICKS(3600);

			// Show HUD message at one minute remaining
			if (elapsed < warntime && nexttime >= warntime) {
				s32 i;

				for (i = 0; i < MAX_PLAYERS; i++) {
					if (!g_Vars.players[i]) {
						continue;
					}
					setCurrentPlayerNum(i);
					hudmsgCreate(langGet(L_MISC_068), HUDMSGTYPE_DEFAULT); // "One minute left."
				}
			}

			if (elapsed < TICKS(g_MpTimeLimit60) && nexttime >= TICKS(g_MpTimeLimit60)) {
				// Match is ending due to time limit reached
				if (g_NetMode != NETMODE_CLIENT) {
					/* c3845: match-smoke end milestone. Fires on the host
					 * (and solo); the networked client ends via SVC_STAGE_END
					 * because its own time-limit gate is NETMODE_CLIENT-guarded. */
					sysLogPrintf(LOG_NOTE, "MATCH: stage end reason=timelimit");
					mainEndStage();
				}
			}

			// Sound alarm at 10 seconds remaining
			if (nexttime >= TICKS(g_MpTimeLimit60) - TICKS(600)
					&& g_MiscAudioHandle == NULL
					&& !lvIsPaused()
					&& nexttime < TICKS(g_MpTimeLimit60)) {
				snd00010718(&g_MiscAudioHandle, 0, AL_VOL_FULL, AL_PAN_CENTER, SFX_ALARM_DEFAULT, 1, 1, -1, true);
			}
		}

		if (g_Vars.lvupdate240 != 0) {
			s32 numdying = 0;

			for (i = 0; i < PLAYERCOUNT(); i++) {
				if (!g_Vars.players[i]) continue;
				if (g_Vars.players[i]->isdead) {
					if (g_Vars.players[i]->redbloodfinished == false
							|| g_Vars.players[i]->deathanimfinished == false
							|| g_Vars.players[i]->colourfadetimemax60 >= 0) {
						numdying++;
					}
				}
			}

			for (i = 0; i < g_MpNumChrs; i++) {
				/* NULL guard: player slots (0..PLAYERCOUNT-1) are reserved by mpReset
				 * but only populated lazily by playerTickChrBody on first propsTick.
				 * On the very first game tick propsTick has not run yet, so player
				 * slots are NULL. Bot slots (PLAYERCOUNT..g_MpNumChrs-1) are always
				 * populated by botmgrAllocateBot during setupCreateProps. */
				if (g_MpAllChrPtrs[i] && g_MpAllChrPtrs[i]->actiontype == ACT_DIE) {
					numdying++;
				}
			}

			if (g_MpScoreLimit > 0) {
				struct ranking rankings[MAX_MPCHRS];
				s32 count = mpGetPlayerRankings(rankings);

				for (i = 0; i < count; i++) {
					if (rankings[i].score >= g_MpScoreLimit) {
						g_NumReasonsToEndMpMatch++;
					}
				}
			}

			if (g_MpTeamScoreLimit > 0) {
				struct ranking rankings[MAX_MPCHRS];
				s32 count = mpGetTeamRankings(rankings);

				for (i = 0; i < count; i++) {
					if (rankings[i].score >= g_MpTeamScoreLimit) {
						g_NumReasonsToEndMpMatch++;
					}
				}
			}

			if (g_NetMode == NETMODE_CLIENT) {
				g_NumReasonsToEndMpMatch = 0;
			}

			if (g_NumReasonsToEndMpMatch > 0 && numdying == 0) {
				mainEndStage();
			}
		}
	}

	g_StageTimeElapsed60 += g_Vars.lvupdate60;
	g_StageTimeElapsed1f = g_StageTimeElapsed60 / TICKS(60.0f);

	viSetUseZBuf(true);

	if (g_Vars.stagenum == STAGE_TEST_OLD) {
		titleTickOld();
		musicTick();
	}

	if (g_Vars.stagenum == STAGE_TITLE) {
		titleTick();
		langTick();
		musicTick();
	} else if (g_Vars.stagenum == STAGE_BOOTPAKMENU) {
		setCurrentPlayerNum(0);
#if VERSION >= VERSION_PAL_BETA
		playerConfigureVi();
#endif
		menuTick();
		musicTick();
		langTick();
		pakExecuteDebugOperations();
	} else if (g_Vars.stagenum == STAGE_CREDITS) {
		musicTick();
		langTick();
	} else {
		lvUpdateCutsceneTime();
		vtxstoreTick();
		lvUpdateSoloHandicaps();
		roomsTick();
		skyTick();
		casingsTick();
		shardsTick();
		sparksTick();
		/* Public .pdeffect time is real gameplay time in seconds. Paused frames
		 * naturally pass zero and do not advance retained instances. */
		effectInstanceRuntimeTick(g_Vars.lvupdate60freal / 60.0f);
		wallhitsTick();
		splatsTick();

		if (g_WeatherActive) {
			weatherTick();
		}

		if (g_NbombsActive) {
			nbombsTick();
		}

		lvUpdateMiscSfx();
		sndTick();
		pakExecuteDebugOperations();
		lightingTick();
		modelmgrPrintCounts();
		boltbeamsTick();
		amTick();
		menuTick();
		scenarioTick();

		if (!g_MainIsEndscreen) {
			propsTick();
		}

		musicTick();
		langTick();
		propsTickPadEffects();

		if (mainGetStageNum() == STAGE_CITRAINING) {
			struct trainingdata *trainingdata = dtGetData();

			if ((g_Vars.currentplayer->prop->rooms[0] < ROOM_DISH_HOLO1 || g_Vars.currentplayer->prop->rooms[0] > ROOM_DISH_HOLO4)
					&& g_Vars.currentplayer->prop->rooms[0] != ROOM_DISH_FIRINGRANGE
					&& (trainingdata == NULL || trainingdata->intraining == false)) {
				chrUnsetStageFlag(NULL, STAGEFLAG_CI_IN_TRAINING);
			}

			frTick();

			if (g_Vars.lvupdate240 != 0) {
				dtTick();
				htTick();
			}
		}
	}
}

const char var7f1b7738[] = "cutsceneframe: %d\n";
const char var7f1b774c[] = "pos:%s%s %.2f %.2f %.2f\n";
const char var7f1b7768[] = "";
const char var7f1b776c[] = "";

void lvTickPlayer(void)
{
	f32 xdiff;
	f32 zdiff;
	struct playerstats *stats;

	if (var80075d64 == 2) {
		if (var80075d68 == 2) {
			playerTick(true);
		} else {
			playerTick(false);
		}
	}

	if (!g_Vars.currentplayer || !g_Vars.currentplayer->prop
			|| g_Vars.currentplayernum < 0 || g_Vars.currentplayernum >= MAX_PLAYERS) {
		return;
	}

	stats = &g_Vars.playerstats[g_Vars.currentplayernum];

	if (g_Vars.currentplayerstats != stats) {
		sysLogPrintf(LOG_WARNING,
			"LV: distance stats using canonical player slot=%d currentstats=%p expected=%p",
			g_Vars.currentplayernum,
			(void *)g_Vars.currentplayerstats,
			(void *)stats);
	}

	xdiff = g_Vars.currentplayer->prop->pos.x - g_Vars.currentplayer->bondprevpos.x;
	zdiff = g_Vars.currentplayer->prop->pos.z - g_Vars.currentplayer->bondprevpos.z;

	stats->distance += sqrtf(xdiff * xdiff + zdiff * zdiff);

	/* PC: persistent distance stat — flush in chunks of 10000 game units
	 * to avoid hammering the hash-table on every tick.  g_Vars.playerstats
	 * is 1:1 with local player slot; only track local players (playernum<PLAYERCOUNT()). */
	if (g_Vars.currentplayernum < PLAYERCOUNT()) {
		static f32 s_StatDistanceAccum[MAX_PLAYERS] = { 0.0f };
		const f32 STAT_FLUSH_THRESHOLD = 10000.0f;
		s32 pn = g_Vars.currentplayernum;
		f32 step = sqrtf(xdiff * xdiff + zdiff * zdiff);
		s_StatDistanceAccum[pn] += step;
		while (s_StatDistanceAccum[pn] >= STAT_FLUSH_THRESHOLD) {
			s_StatDistanceAccum[pn] -= STAT_FLUSH_THRESHOLD;
			if (g_Vars.normmplayerisrunning) {
				statIncrement("mp.distance_units", 1);
			} else {
				statIncrement("solo.distance_units", 1);
			}
		}
	}
}

void lvStop(void)
{
	effectInstanceRuntimeClearAll();
	paksStop(true);

	if (g_MiscAudioHandle && sndGetState(g_MiscAudioHandle)) {
		audioStop(g_MiscAudioHandle);
	}

	if (STAGE_IS_GAMEPLAY(g_Vars.stagenum)) {
		s32 bank = langGetLangBankIndexFromStagenum(g_Vars.stagenum);
		langClearBank(bank);
		stub0f015270();
	}

	chrmgrStop();
	explosionsStop();
	smokeStop();
	stub0f015400();
	stub0f015410();
	shardsStop();
	stub0f0153f0();
	propsStop();
	objsStop();
	weatherStop();
	objectivesStop();
	stub0f015260();
	bgunStop();
	psStop();
	musicStop();
	hudmsgsStop();

	if (STAGE_IS_GAMEPLAY(g_Vars.stagenum)) {
		bgStop();
	}

	func00033dd8();

	if (g_FileState == FILESTATE_CHANGINGAGENT) {
		menuPlaySound(MENUSOUND_EXPLOSION);
		g_FileState = FILESTATE_UNSELECTED;
	}

#if VERSION >= VERSION_NTSC_1_0
	menuStop();
#endif
}

void lvCheckPauseStateChanged(void)
{
	u32 paused = mpIsPaused();

	if (paused != var80084010) {
		if (paused) {
			pakDisableRumbleForAllPlayers();
		} else {
			pakEnableRumbleForAllPlayers();
		}
	}

	var80084010 = paused;
}

void lvSetPaused(bool paused)
{
	if (paused) {
		pakDisableRumbleForAllPlayers();
		snd0000fe20();
	} else {
		snd0000fe50();
		pakEnableRumbleForAllPlayers();
	}

	var80084014 = paused;
}

bool lvIsPaused(void)
{
	return var80084014;
}

s32 lvGetDifficulty(void)
{
	return g_Difficulty;
}

void lvSetDifficulty(s32 difficulty)
{
	if (difficulty < DIFF_A || difficulty > DIFF_PD) {
		difficulty = DIFF_A;
	}

	g_Difficulty = difficulty;
}

void lvSetMpTimeLimit60(u32 limit)
{
	g_MpTimeLimit60 = limit;
}

void lvSetMpScoreLimit(u32 limit)
{
	g_MpScoreLimit = limit;
}

void lvSetMpTeamScoreLimit(u32 limit)
{
	g_MpTeamScoreLimit = limit;
}

f32 lvGetStageTimeInSeconds(void)
{
	return g_StageTimeElapsed1f;
}

s32 lvGetStageTime60(void)
{
	return g_StageTimeElapsed60;
}

u32 func0f16ce04(u32 arg0)
{
	return arg0;
}
