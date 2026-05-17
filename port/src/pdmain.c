#include <stdlib.h>
#include <SDL.h>

#include <ultra64.h>
#include <PR/ultrasched.h>
#include "lib/sched.h"
#include "lib/vars.h"
#include "constants.h"
#include "game/camdraw.h"
#include "game/cheats.h"
#include "game/debug.h"
#include "game/file.h"
#include "game/lang.h"
#include "game/race.h"
#include "game/body.h"
#include "game/stubs/game_000840.h"
#include "game/stubs/game_000850.h"
#include "game/stubs/game_000860.h"
#include "game/stubs/game_000870.h"
#include "game/smoke.h"
#include "game/stubs/game_0008e0.h"
#include "game/stubs/game_0008f0.h"
#include "game/stubs/game_000900.h"
#include "game/stubs/game_000910.h"
#include "game/tex.h"
#include "game/stubs/game_00b180.h"
#include "game/stubs/game_00b200.h"
#include "game/challenge.h"
#include "game/title.h"
#include "game/pdmode.h"
#include "game/objectives.h"
#include "game/endscreen.h"
#include "game/playermgr.h"
#include "game/game_1531a0.h"
#include "game/gfxmemory.h"
#include "game/lang.h"
#include "game/forgemode.h"
#include "forge/forge_core.h"
#include "pdgui_forge.h"
#include "swarm_test.h"
#include "actionmap.h"
#include "scene.h"
#include "smoke_harness.h"
#include "game/lv.h"
#include "game/options.h"
#include "game/timing.h"
#include "game/music.h"
#include "game/stubs/game_175f50.h"
#include "game/game_175f90.h"
#include "game/zbuf.h"
#include "game/game_1a78b0.h"
#include "game/mplayer/mplayer.h"
#include "game/mplayer/participant.h"
#include "game/pak.h"
#include "game/splat.h"
#include "game/utils.h"
#include "bss.h"
#include "lib/audiomgr.h"
#include "lib/args.h"
#include "lib/boot.h"
#include "lib/vm.h"
#include "lib/rzip.h"
#include "lib/vi.h"
#include "lib/fault.h"
#include "lib/crash.h"
#include "lib/dma.h"
#include "lib/joy.h"
#include "lib/main.h"
#include "lib/snd.h"
#include "lib/memp.h"
#include "lib/mema.h"
#include "lib/model.h"
#include "lib/profile.h"
#include "lib/videbug.h"
#include "lib/debughud.h"
#include "lib/anim.h"
#include "lib/rdp.h"
#include "lib/lib_34d0.h"
#include "lib/lib_2f490.h"
#include "lib/rmon.h"
#include "lib/rng.h"
#include "lib/str.h"
#include "data.h"
#include "types.h"
#include "system.h"
#include "console.h"
#include "net/net.h"
#include "net/netmsg.h"
#include "net/netmanifest.h"
#include "net/p2p.h"
#include "net/group_session.h"
#include "presence.h"
#include "chat.h"
#include "file_transfer.h"
#include "pdgui_toast.h"
#include "spectator.h"
#include "theater.h"
#include "listening_room.h"
#include "social_share.h"
#include "voice.h"
#include "modelcatalog.h"
#include "pdmain.h"
#include "pdgui_theme.h"
#include "audio.h"
#include "discord.h"

extern u8 *g_MempHeap;
extern u32 g_MempHeapSize;

/* M6: SDL mutex backing the memp thread-safety hooks. */
static SDL_mutex *s_MempMutex;
static void mempLockImpl(void)   { SDL_LockMutex(s_MempMutex); }
static void mempUnlockImpl(void) { SDL_UnlockMutex(s_MempMutex); }

void rngSetSeed(u32 seed);

s32 var8005d9b0 = 0;
s32 g_StageNum = STAGE_TITLE;
u32 g_MainMemaHeapSize = 1024 * 300;
s32 var8005d9bc = 0;
s32 var8005d9c0 = 0;
s32 var8005d9c4 = 0;
s32 g_MainGameLogicEnabled = 1;
u32 g_MainNumGfxTasks = 0;
s32 g_MainIsEndscreen = 0;
s32 g_DoBootPakMenu = 0;

u32 var8005dd3c = 0x00000000;
u32 var8005dd40 = 0x00000000;
u32 var8005dd44 = 0x00000000;
u32 var8005dd48 = 0x00000000;
u32 var8005dd4c = 0x00000000;
u32 var8005dd50 = 0x00000000;
s32 g_MainChangeToStageNum = -1;
s32 g_MainIsDebugMenuOpen = 0;

/* B-193: armed by mainLoop's stage-change completion block, consumed by
 * mainTick's render pass. Two-shot diag:
 *   1 = emit first-render snapshot (frame 0), then re-arm to 2
 *   2 = waiting for frame s_B193SettledFrameTarget to emit settled snapshot
 *   0 = idle
 * Phase 1 ruled out room/camera registration (state identical bad vs good
 * boot). Phase 2 extends coverage to the BG render path. */
static s32 s_B193FirstRenderDiagPending = 0;
static s32 s_B193SettledFrameTarget = 0;


struct stageallocation g_StageAllocations8Mb[] = {
	{ STAGE_CITRAINING,    "-ml0 -me0 -mgfx120 -mvtx98 -ma400"             },
	{ STAGE_DEFECTION,     "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700" },
	{ STAGE_INVESTIGATION, "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700" },
	{ STAGE_EXTRACTION,    "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700" },
	{ STAGE_CHICAGO,       "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700" },
	{ STAGE_G5BUILDING,    "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700" },
	{ STAGE_VILLA,         "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma600" },
	{ STAGE_INFILTRATION,  "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma500" },
	{ STAGE_RESCUE,        "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma500" },
	{ STAGE_ESCAPE,        "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma500" },
	{ STAGE_AIRBASE,       "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700" },
	{ STAGE_AIRFORCEONE,   "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700" },
	{ STAGE_CRASHSITE,     "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700" },
	{ STAGE_PELAGIC,       "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700" },
	{ STAGE_DEEPSEA,       "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700" },
	{ STAGE_DEFENSE,       "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700" },
	{ STAGE_ATTACKSHIP,    "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700" },
	{ STAGE_SKEDARRUINS,   "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700" },
	{ STAGE_MP_SKEDAR,     "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_MP_RAVINE,     "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_MP_PIPES,      "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_MP_G5BUILDING, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_MP_SEWERS,     "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_MP_WAREHOUSE,  "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_MP_BASE,       "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_MP_COMPLEX,    "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_MP_TEMPLE,     "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_MP_FELICITY,   "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_MP_AREA52,     "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_MP_GRID,       "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_MP_CARPARK,    "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_MP_RUINS,      "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_MP_FORTRESS,   "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_MP_VILLA,      "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_TEST_RUN,      "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_TEST_MP2,      "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_TEST_MP6,      "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_TEST_MP7,      "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_TEST_MP8,      "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_TEST_MP14,     "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_TEST_MP16,     "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_TEST_MP17,     "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_TEST_MP18,     "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_TEST_MP19,     "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_TEST_MP20,     "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_TEST_ASH,      "-ml0 -me0 -mgfx120 -mvtx98 -ma400"             },
	{ STAGE_28,            "-ml0 -me0 -mgfx120 -mvtx98 -ma400"             },
	{ STAGE_MBR,           "-ml0 -me0 -mgfx120 -mvtx100 -ma700"            },
	{ STAGE_TEST_SILO,     "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_24,            "-ml0 -me0 -mgfx120 -mvtx98 -ma400"             },
	{ STAGE_MAIANSOS,      "-ml0 -me0 -mgfx120 -mvtx100 -ma500"            },
	{ STAGE_RETAKING,      "-ml0 -me0 -mgfx120 -mvtx98 -ma400"             },
	{ STAGE_TEST_DEST,     "-ml0 -me0 -mgfx120 -mvtx98 -ma400"             },
	{ STAGE_2B,            "-ml0 -me0 -mgfx120 -mvtx98 -ma400"             },
	{ STAGE_WAR,           "-ml0 -me0 -mgfx120 -mvtx98 -ma400"             },
	{ STAGE_TEST_UFF,      "-ml0 -me0 -mgfx120 -mvtx98 -ma400"             },
	{ STAGE_TEST_OLD,      "-ml0 -me0 -mgfx120 -mvtx98 -ma400"             },
	{ STAGE_DUEL,          "-ml0 -me0 -mgfx120 -mvtx100 -ma700"            },
	{ STAGE_TEST_LAM,      "-ml0 -me0 -mgfx120 -mvtx98 -ma400"             },
	{ STAGE_TEST_ARCH,     "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_TEST_LEN,      "-ml0 -me0 -mgfx120 -mvtx98 -ma300"             },
	// GoldenEye X Mod stages
	{ STAGE_EXTRA1,        "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA2,        "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA3,        "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA4,        "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA5,        "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA6,        "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA7,        "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA8,        "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA9,        "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA10,       "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA11,       "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA12,       "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA13,       "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA14,       "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA15,       "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA16,       "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA17,       "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	// Kakariko Village Mod stages
	{ STAGE_EXTRA18,       "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA19,       "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	// Goldfinger 64 Mod stages
	{ STAGE_EXTRA20,       "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA21,       "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA22,       "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA23,       "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	// Additional
	{ STAGE_EXTRA24,       "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA25,       "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_EXTRA26,       "-ml0 -me0 -mgfx200 -mvtx200 -ma400"            },
	{ STAGE_TITLE,         "-ml0 -me0 -mgfx80 -mvtx20 -ma001"              },
	{ 0,                   "-ml0 -me0 -mgfx120 -mvtx98 -ma300"             },
};

struct stageallocation g_StageAllocations4Mb[] = {
	{ STAGE_MP_SKEDAR,     "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_MP_PIPES,      "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_MP_AREA52,     "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_MP_RAVINE,     "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_MP_G5BUILDING, "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_MP_SEWERS,     "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_MP_WAREHOUSE,  "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_MP_BASE,       "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_MP_COMPLEX,    "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_MP_TEMPLE,     "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_MP_FELICITY,   "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_MP_GRID,       "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_TEST_RUN,      "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_MP_CARPARK,    "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_MP_RUINS,      "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_MP_FORTRESS,   "-ml0 -me0 -mgfx96 -mvtx96 -ma130"              },
	{ STAGE_MP_VILLA,      "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_TEST_MP2,      "-ml0 -me0 -mgfx96 -mvtx96 -ma115"              },
	{ STAGE_TEST_MP6,      "-ml0 -me0 -mgfx96 -mvtx96 -ma115"              },
	{ STAGE_TEST_MP7,      "-ml0 -me0 -mgfx96 -mvtx96 -ma115"              },
	{ STAGE_TEST_MP8,      "-ml0 -me0 -mgfx96 -mvtx96 -ma115"              },
	{ STAGE_TEST_MP14,     "-ml0 -me0 -mgfx96 -mvtx96 -ma115"              },
	{ STAGE_TEST_MP16,     "-ml0 -me0 -mgfx96 -mvtx96 -ma115"              },
	{ STAGE_TEST_MP17,     "-ml0 -me0 -mgfx96 -mvtx96 -ma115"              },
	{ STAGE_TEST_MP18,     "-ml0 -me0 -mgfx96 -mvtx96 -ma115"              },
	{ STAGE_TEST_MP19,     "-ml0 -me0 -mgfx96 -mvtx96 -ma115"              },
	{ STAGE_TEST_MP20,     "-ml0 -me0 -mgfx96 -mvtx96 -ma115"              },
	{ STAGE_TEST_LEN,      "-ml0 -me0 -mgfx100 -mvtx96 -ma120"             },
	{ STAGE_EXTRA1,        "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA2,        "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA3,        "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA4,        "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA5,        "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA6,        "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA7,        "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA8,        "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA9,        "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA10,       "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA11,       "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA12,       "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA13,       "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA14,       "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA15,       "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA16,       "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA17,       "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA18,       "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA19,       "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA20,       "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA21,       "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA22,       "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA23,       "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA24,       "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA25,       "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_EXTRA26,       "-ml0 -me0 -mgfx96 -mvtx96 -ma140"              },
	{ STAGE_TITLE,         "-ml0 -me0 -mgfx80 -mvtx20 -ma001"              },
	{ 0,                   "-ml0 -me0 -mgfx100 -mvtx96 -ma300"             },
};

Gfx var8005dcc8[] = {
	gsSPSegment(0x00, 0x00000000),
	gsSPDisplayList(&var800613a0),
	gsSPDisplayList(&var80061380),
	gsDPFullSync(),
	gsSPEndDisplayList(),
};

s32 g_MainIsBooting = 1;

/* FIX-PLAYTEST-4: C accessor so C++ port code can check the stage frame counter
 * without including types.h (which #defines bool as s32, breaking C++ bool). */
s32 pdmainGetLvFrame60(void)
{
	return (s32)g_Vars.lvframe60;
}

s32 pdmainGetInteractPromptActionPlayer(void)
{
	if (!g_Vars.currentplayerstats) {
		return 0;
	}
	return (s32)optionsGetContpadNum1(g_Vars.currentplayerstats->mpindex);
}

void mainInit(void)
{
	s32 x;
	s32 i;
	s32 j;
	u32 addr;

	faultInit();
	dmaInit();
	amgrInit();
	varsInit();
	mempInit();
	memaInit();
	joyInit();
	joyReset();

	var8005d9b0 = rmonIsDisabled();

	g_VmShowStats = 0;

	// no copyright screen
	viSetMode(VIMODE_HI);
	viConfigureForLegal();
	viBlack(1);
	viUpdateMode();

	filesInit();

	if (var8005d9b0) {
		argSetString("          -ml0 -me0 -mgfx100 -mvtx50 -mt700 -ma400");
	}

	mempSetHeap(g_MempHeap, g_MempHeapSize);

	/* M6: register SDL mutex so mempAlloc/mempResetPool are thread-safe. */
	s_MempMutex = SDL_CreateMutex();
	mempSetLockFns(mempLockImpl, mempUnlockImpl);

	/* NOTE: catalogValidateAll() was previously called here, but model
	 * loading depends on subsystems initialized later in this function
	 * (texInit, langInit, etc.). Loading at this point triggers ACCESS
	 * VIOLATION for ALL 151 models because the texture/skeleton systems
	 * aren't ready yet. Validation is deferred to on-demand: models are
	 * validated lazily via catalogGetSafeBody/Head() when first accessed
	 * during gameplay, by which point all subsystems are initialized. */

	sysLogPrintf(LOG_VERBOSE, "INIT: mempResetPool...");
	mempResetPool(MEMPOOL_8);
	mempResetPool(MEMPOOL_PERMANENT);
	sysLogPrintf(LOG_VERBOSE, "INIT: crashReset...");
	crashReset();
	sysLogPrintf(LOG_VERBOSE, "INIT: challengesInit...");
	challengesInit();
	sysLogPrintf(LOG_VERBOSE, "INIT: utilsInit...");
	utilsInit();
	sysLogPrintf(LOG_VERBOSE, "INIT: texInit...");
	texInit();
	sysLogPrintf(LOG_VERBOSE, "INIT: pdguiThemeLateInit...");
	pdguiThemeLateInit();
	sysLogPrintf(LOG_VERBOSE, "INIT: langInit...");
	langInit();
	sysLogPrintf(LOG_VERBOSE, "INIT: lvInit...");
	lvInit();
	sysLogPrintf(LOG_VERBOSE, "INIT: cheatsInit...");
	cheatsInit();
	sysLogPrintf(LOG_VERBOSE, "INIT: textInit...");
	textInit();
	sysLogPrintf(LOG_VERBOSE, "INIT: dhudInit...");
	dhudInit();
	sysLogPrintf(LOG_VERBOSE, "INIT: playermgrInit...");
	playermgrInit();
	sysLogPrintf(LOG_VERBOSE, "INIT: frametimeInit...");
	frametimeInit();
	sysLogPrintf(LOG_VERBOSE, "INIT: profileInit...");
	profileInit();
	sysLogPrintf(LOG_VERBOSE, "INIT: smokesInit...");
	smokesInit();
	sysLogPrintf(LOG_VERBOSE, "INIT: mpInit...");
	mpInit(1);
	sysLogPrintf(LOG_VERBOSE, "INIT: pheadInit...");
	pheadInit();
	sysLogPrintf(LOG_VERBOSE, "INIT: paksInit...");
	paksInit();
	sysLogPrintf(LOG_VERBOSE, "INIT: pheadInit2...");
	pheadInit2();
	sysLogPrintf(LOG_VERBOSE, "INIT: animsInit...");
	animsInit();
	sysLogPrintf(LOG_VERBOSE, "INIT: racesInit...");
	racesInit();
	sysLogPrintf(LOG_VERBOSE, "INIT: bodiesInit...");
	bodiesInit();
	sysLogPrintf(LOG_VERBOSE, "INIT: titleInit...");
	titleInit();
	sysLogPrintf(LOG_VERBOSE, "INTRO: mainInit - titleInit() done, g_StageNum=0x%02x", g_StageNum);

	modelSetDistanceChecksDisabled(1); // don't use LODs

	g_MainIsBooting = 0;
	sysLogPrintf(LOG_VERBOSE, "INTRO: mainInit complete, g_MainIsBooting=0");
}

void mainProc(void)
{
	mainInit();
	rdpInit();
	sndInit();
	audioNotifyEngineReady();

	while (1) {
		mainLoop();
	}
}

/**
 * It's suspected that this function would have allowed developers to override
 * the value of variables while the game is running in order to view their
 * effects immediately rather than having to recompile the game each time.
 *
 * The developers would have used rmon to create a table of name/value pairs,
 * then this function would have looked up the given variable name in the table
 * and written the new value to the variable's address.
 */
void mainOverrideVariable(char *name, void *value)
{
	// empty
}

/**
 * This function enters an infinite loop which iterates once per stage load.
 * Within this loop is an inner loop which runs very frequently and decides
 * whether to run mainTick on each iteration.
 *
 * NTSC beta checks two shorts at an offset 64MB into the development board
 * and refuses to continue if they are not any of the allowed values.
 * Decomp patches these reads in its build system so it can be played
 * without the development board.
 */
void mainLoop(void)
{
	s32 ending = 0;
	s32 index;
	s32 numplayers;
	u32 stack;

	func0f175f98();

	var8005d9c4 = 0;
	argGetLevel(&g_StageNum);
	sysLogPrintf(LOG_VERBOSE, "INTRO: mainLoop - after argGetLevel, g_StageNum=0x%02x, g_DoBootPakMenu=%d",
		g_StageNum, g_DoBootPakMenu);

	if (g_DoBootPakMenu) {
		g_Vars.pakstocheck = 0xfd;
		g_StageNum = STAGE_BOOTPAKMENU;
		sysLogPrintf(LOG_VERBOSE, "INTRO: mainLoop - boot pak menu override, g_StageNum=BOOTPAKMENU");
	}

	if (g_StageNum != STAGE_TITLE) {
		sysLogPrintf(LOG_VERBOSE, "INTRO: mainLoop - g_StageNum != STAGE_TITLE, calling titleSetNextStage");
		titleSetNextStage(g_StageNum);

		if (STAGE_IS_GAMEPLAY(g_StageNum)) {
			func0f01b148(0);

			if (argFindByPrefix(1, "-hard")) {
				lvSetDifficulty(argFindByPrefix(1, "-hard")[0] - '0');
			}
		}
	}

	rngSetSeed(osGetCount());

	// Outer loop - this is infinite because ending is never changed
	while (!ending) {
		g_MainNumGfxTasks = 0;
		g_MainGameLogicEnabled = 1;
		g_MainIsEndscreen = 0;

		if (var8005d9b0 && var8005d9c4 == 0) {
			index = -1;

			if (STAGE_IS_GAMEPLAY(g_StageNum) && getNumPlayers() >= 2) {
				index = 0;
				while (g_StageAllocations8Mb[index].stagenum) {
					if (g_StageNum + 400 == g_StageAllocations8Mb[index].stagenum) {
						break;
					}
					index++;
				}

				if (g_StageAllocations8Mb[index].stagenum == 0) {
					index = -1;
				}
			}

			if (index < 0) {
				index = 0;

				while (g_StageAllocations8Mb[index].stagenum) {
					if (g_StageNum == g_StageAllocations8Mb[index].stagenum) {
						break;
					}

					index++;
				}
			}

			argSetString(g_StageAllocations8Mb[index].string);
			sysLogPrintf(LOG_NOTE, "LOAD: 8Mb alloc index=%d stagenum_in_table=0x%02x string=\"%s\"",
				index, g_StageAllocations8Mb[index].stagenum, g_StageAllocations8Mb[index].string);
		}

		var8005d9c4 = 0;

		mempResetPool(MEMPOOL_7);
		mempResetPool(MEMPOOL_STAGE);
		filesStop(4);

		if (argFindByPrefix(1, "-ma")) {
			g_MainMemaHeapSize = strtol(argFindByPrefix(1, "-ma"), NULL, 0) * 1024;
			if (g_NetMode && g_NetMaxClients > MAX_LOCAL_PLAYERS) {
				g_MainMemaHeapSize *= MAX_PLAYERS / MAX_LOCAL_PLAYERS;
			}
		}

		sysLogPrintf(LOG_NOTE, "LOAD: memaHeapSize=%d bytes, calling memaReset", g_MainMemaHeapSize);
		memaReset(mempAlloc(g_MainMemaHeapSize, MEMPOOL_STAGE), g_MainMemaHeapSize);
		sysLogPrintf(LOG_NOTE, "LOAD: calling langReset(0x%02x)", g_StageNum);
		langReset(g_StageNum);
		playermgrReset();
		sysLogPrintf(LOG_NOTE, "LOAD: playermgrReset done, numplayers check next");

		if (STAGE_IS_SYSTEM(g_StageNum)) {
			numplayers = 0;
		} else {
			if (argFindByPrefix(1, "-play")) {
				numplayers = strtol(argFindByPrefix(1, "-play"), NULL, 0);
			} else {
				numplayers = 1;
			}

			if (getNumPlayers() >= 2) {
				numplayers = getNumPlayers();
			}
		}

		if (numplayers < 2) {
			g_Vars.bondplayernum = 0;
			g_Vars.coopplayernum = -1;
			g_Vars.antiplayernum = -1;
		} else if (argFindByPrefix(1, "-coop")) {
			g_Vars.bondplayernum = 0;
			g_Vars.coopplayernum = 1;
			g_Vars.antiplayernum = -1;
		} else if (argFindByPrefix(1, "-anti")) {
			g_Vars.bondplayernum = 0;
			g_Vars.coopplayernum = -1;
			g_Vars.antiplayernum = 1;
		}

		playermgrAllocatePlayers(numplayers);

		if (argFindByPrefix(1, "-mpbots")) {
			g_Vars.lvmpbotlevel = 1;
		}

		if (g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0) {
			mpReset();
			if (g_Vars.antiplayernum < 0) {
				// Counter-Operative now uses a different approach which allows more than 2 players.
				// Co-Operative, on the other hand, is currently limited to 2 players.
				mpAddParticipantAt(0, PARTICIPANT_LOCAL, 0, 0, 0);
				mpAddParticipantAt(1, PARTICIPANT_LOCAL, 0, 0, 1);
			}
		} else if (g_Vars.perfectbuddynum) {
			mpReset();
		} else if (g_Vars.mplayerisrunning == 0
				&& (numplayers >= 2 || g_Vars.lvmpbotlevel || argFindByPrefix(1, "-play"))) {
			g_MpSetup.stagenum = g_StageNum;
			mpReset();
			for (s32 i = 0; i < numplayers && i < MAX_LOCAL_PLAYERS; ++i) {
				mpAddParticipantAt(i, PARTICIPANT_LOCAL, 0, 0, (u8)i);
			}
		}

		gfxReset();
		joyReset();
		dhudReset();
		zbufReset(g_StageNum);
		lvReset(g_StageNum);
		viReset(g_StageNum);
		sysLogPrintf(LOG_VERBOSE, "INTRO: mainLoop - entering tick loop with g_StageNum=0x%02x, g_Vars.stagenum=0x%02x",
			g_StageNum, g_Vars.stagenum);
		frametimeCalculate();
		profileReset();

		/* Priority J (2026-04-25): activate the scene-scope IMC for the
		 * new stage. Mission XOR CombatSim, mutually exclusive at this
		 * gate. lvReset / mpReset above have already set the
		 * g_Vars.{normmplayerisrunning,coopplayernum,antiplayernum}
		 * fields the dispatch reads.  System stages (title, intro)
		 * leave both inactive so menu IMCs own the surface.
		 *
		 * See context/designs/contextual-input-schemes.md. */
		if (STAGE_IS_SYSTEM(g_StageNum)) {
			imcSceneClearGameplay();
		} else if (g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0) {
			/* Co-op + anti-counter-op run the Mission scheme. */
			imcSceneSetMission();
		} else if (g_Vars.normmplayerisrunning) {
			imcSceneSetCombatSim();
		} else {
			/* Solo mission / generic gameplay. */
			imcSceneSetMission();
		}
		if (STAGE_IS_SYSTEM(g_StageNum)) {
			sceneFire(SCENE_EVENT_STAGE_TEARDOWN, NULL);
		} else {
			sceneFire(SCENE_EVENT_STAGE_READY, NULL);
		}

		while (g_MainChangeToStageNum < 0) {
			const s32 cycles = osGetCount() - g_Vars.thisframestartt;
			if (!g_Vars.mininc60 || (cycles >= g_Vars.mininc60 * CYCLES_PER_FRAME - CYCLES_PER_FRAME / 2)) {
				schedStartFrame(&g_Sched);
				mainTick();
				schedEndFrame(&g_Sched);
			}
			if (g_TickExtraSleep) {
				sysSleep(EXTRA_SLEEP_TIME);
			}
		}

		/* Priority J (2026-04-25): scene unloading -- drop both
		 * gameplay-scope IMCs (and Vehicle, defensively) before the next
		 * stage's load activates the right one. */
		sceneFire(SCENE_EVENT_STAGE_TEARDOWN, NULL);
		imcSceneClearGameplay();

		lvStop();
		mempDisablePool(MEMPOOL_STAGE);
		mempDisablePool(MEMPOOL_7);
		filesStop(4);
		viBlack(1);
		pak0f116994();

		g_StageNum = g_MainChangeToStageNum;
		g_MainChangeToStageNum = -1;

		/* B-193: arm first-render state dump. Consumed in mainTick before
		 * the first lvRender after this transition. Absent LV.DIAG line on
		 * a repro = stage-change didn't reach the render pass; a line with
		 * roomcount=0, player_room=-1, or camera_room=0 pins the hypothesis. */
		s_B193FirstRenderDiagPending = 1;
	}
}

void mainTick(void)
{
	Gfx *gdl = NULL;
	Gfx *gdlstart = NULL;
	OSScMsg msg = {OS_SC_DONE_MSG};
	s32 i;

	/* Smoke verify harness tick (2026-05-11): scheduled SDL_PushEvent
	 * dispatch + timeout watchdog. Cheap no-op when --smoke is absent.
	 * Runs at the top of mainTick so any pushed events land before the
	 * input dispatch downstream picks them up this frame. */
	smokeHarnessTick();

	/* c115 (2026-05-14): --launch-scenario one-shot. Dispatches the
	 * latched testScenarioLaunch on the first frame where the player
	 * prop is positioned (lvframenum >= 4), so matchStart sees a
	 * valid player and the first respawn succeeds. Worker delta's
	 * iter-2 re-run showed that calling testScenarioLaunch
	 * synchronously from bootApplyCliFastPaths fires before stage
	 * setup, causing the match to end instantly into endscreen_solo
	 * and the swarm cycler to be suppressed by gameplayInputSuppressed.
	 * Cheap no-op when the flag wasn't on the command line. */
	{
		extern s32 bootLaunchScenarioTick(void);
		(void)bootLaunchScenarioTick();
	}

	/* c115 (2026-05-13): --debug-mount-bike one-shot. Walks
	 * g_Vars.activeprops once the load black-frame is over, mounts
	 * player 0 on the first OBJTYPE_HOVERBIKE, then clears its latch.
	 * Cheap no-op when the flag wasn't on the command line (early
	 * return on g_BootMountBikePending == 0). */
	{
		extern s32 bootDebugMountBikeTick(void);
		(void)bootDebugMountBikeTick();
	}

	/* c115 (2026-05-14): --debug-spawn-at one-shot. Teleports player 0
	 * to a (x,y,z,room) target once setupCreateProps has placed the
	 * player prop, then clears its latch. Cheap no-op when the flag
	 * wasn't on the command line. */
	{
		extern s32 bootDebugSpawnAtTick(void);
		(void)bootDebugSpawnAtTick();
	}

	/* c118 (2026-05-15): --launch-load-agent one-shot. Invokes
	 * saveLoadAgent(name) on the first frame past the load gate to read
	 * a pre-staged agent JSON fixture into g_GameFile. Provides smoke
	 * coverage of saveLoadAgent's wire-format path -- the production
	 * code has zero call-sites today (Agent Select routes through
	 * gamefileLoad). Cheap no-op when the flag wasn't on the command
	 * line. */
	{
		extern s32 bootLaunchLoadAgentTick(void);
		(void)bootLaunchLoadAgentTick();
	}

	/* c118 (2026-05-15): --listen-bind one-shot. Invokes netStartServer
	 * on the first frame where g_NetInit is true so the client boots
	 * straight into a listen-host UDP bind. Sister to --connect-host
	 * for the two-process peer-link smoke. Cheap no-op when the flag
	 * wasn't on the command line. */
	{
		extern s32 bootListenBindTick(void);
		(void)bootListenBindTick();
	}

	/* c118 (2026-05-15): --connect-host one-shot. Invokes netStartClient
	 * on the first frame where g_NetInit is true so the client boots
	 * straight into a connect-to-host attempt. Sister to --listen-bind
	 * for the two-process peer-link smoke. Cheap no-op when the flag
	 * wasn't on the command line. */
	{
		extern s32 bootConnectHostTick(void);
		(void)bootConnectHostTick();
	}

	/* Track 2c (c3807, 2026-05-16): --dump-swarm-state one-shot.
	 * Periodically extracts the GPU swarm state texture and appends
	 * to the configured file once a GPU swarm scenario is dispatching.
	 * Cheap no-op when the flag wasn't on the command line OR before
	 * the player prop is positioned (lvframenum >= 4 gate) OR when no
	 * GPU swarm scenario is active. Disarms after SWARM_DUMP_MAX_FRAMES
	 * captures so the file stays bounded. */
	{
		extern s32 bootDumpSwarmStateTick(void);
		(void)bootDumpSwarmStateTick();
	}

	/* Phase 1 connectivity layer: drives LAN broadcast, direct UDP probes,
	 * STUN/UPnP/ICE/TURN tier polling, and pair-state escalation. Runs
	 * every frame regardless of stage state so presence stays alive across
	 * stage transitions and title screens. Internal guard returns early
	 * before p2pInit() completes. */
	p2pTick();
	presenceTick();
	groupSessionTick();
	chatTick();
	fileTransferTick();
	pdguiToastTick();
	spectatorTick();
	theaterTick();
	listeningRoomTick();
	shareTick();
	voiceTick();

	if (g_MainChangeToStageNum < 0) {
		frametimeCalculate();
		profileReset();
		profileSetMarker(PROFILE_MAINTICK_START);
		joyDebugJoy();
		schedSetCrashEnable2(0);

		if (g_MainGameLogicEnabled) {
			gdl = gdlstart = gfxGetMasterDisplayList();

			gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0x0000, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
			gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_4b, 0, 0x0100, 6, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);

			lvTick();
			playermgrShuffle();

			/* Forge level editor (F0+): drives session state, mode toggle,
			 * and the freefly editor camera.  Must run before the per-player
			 * gameplay loop so a freefly bondmovemode override is in place
			 * before bmoveTick dispatches. */
			forgeTick();

			/* The Grid editor runtime (F1-F8): resets per-frame flags,
			 * advances logic graph runtime, wave spawner, etc.  Cheap when
			 * no objects exist (early-outs on empty pools). */
			forgeCoreTick();

			/* S313 -- drive ghost placement update so the HUD reticle
			 * follows the freefly camera every frame while a catalog
			 * pick is pending.  Early-outs when FREEFLY is inactive or
			 * no ghost is active. */
			pdguiForgeEditorTick();

			/* S483 -- Swarm benchmark tick. Idempotent + state-machine
			 * driven; early-outs when no swarm scenario is armed.
			 * Drives the per-frame seek-player AI for CPU mode, the
			 * GPU compute dispatch + readback for GPU mode, the cycler,
			 * death poll, and BENCHMARK.SWARM.* logging. */
			swarmTestTick();

			if (STAGE_IS_GAMEPLAY(g_StageNum)) {
				for (i = 0; i < PLAYERCOUNT(); i++) {
					setCurrentPlayerNum(playermgrGetPlayerAtOrder(i));

					if (g_StageNum != STAGE_TEST_OLD || !titleIsKeepingMode()) {
						viSetViewPosition(g_Vars.currentplayer->viewleft, g_Vars.currentplayer->viewtop);
						viSetFovAspectAndSize(
								g_Vars.currentplayer->fovy, g_Vars.currentplayer->aspect,
								g_Vars.currentplayer->viewwidth, g_Vars.currentplayer->viewheight);
					}

					lvTickPlayer();
				}
			}

			/* B-193 diagnostic: first-render snapshot after a stage change.
			 * Armed by mainLoop's transition block; cleared here so it fires
			 * once per transition. Dumps the state that matters for the "CI
			 * invisible — sky only" class: roomcount, primary room from the
			 * player prop, camera room, player pos, and whether the player
			 * prop has been registered to any room yet.
			 *
			 * Phase 2: prior diagnostic found identical world state on bad
			 * vs good boots (room 16, camera room 16, roomcount=141). Added
			 * rendering-side state: BG primary data ptr, the current room's
			 * loaded240 flag + gfxdata ptr + flags word, and number of
			 * pending room-load candidates. Also fires a second snapshot at
			 * frame=30 to separate "state correct on frame 0" from "state
			 * correct but stays wrong after settle". */
			if (s_B193FirstRenderDiagPending == 1 && STAGE_IS_GAMEPLAY(g_StageNum) && g_Vars.currentplayer) {
				struct prop *pprop = g_Vars.currentplayer->prop;
				s32 firstRoom = (pprop && pprop->rooms[0] != (RoomNum)-1) ? pprop->rooms[0] : -1;
				s32 camRoom = g_Vars.currentplayer->cam_room;
				f32 px = pprop ? pprop->pos.x : 0.0f;
				f32 py = pprop ? pprop->pos.y : 0.0f;
				f32 pz = pprop ? pprop->pos.z : 0.0f;
				/* Pull BG-render state so we can distinguish "room registered
				 * but not loaded" vs "room loaded but no gfxdata" vs "BG data
				 * pool never allocated" from the log. */
				extern u8 *g_BgPrimaryData;
				extern struct room *g_Rooms;
				extern s32 g_BgNumRoomLoadCandidates;
				const struct room *pr = (g_Rooms && camRoom >= 0 && camRoom < g_Vars.roomcount)
					? &g_Rooms[camRoom] : NULL;
				sysLogPrintf(LOG_VERBOSE,
					"LV.DIAG: first-render stage=0x%02x roomcount=%d player_prop=%p player_room=%d camera_room=%d pos=(%.0f,%.0f,%.0f) frame=%d bg_primary=%p cam_loaded240=%d cam_flags=0x%04x cam_gfxdata=%p load_cands=%d cam_pos=(%.0f,%.0f,%.0f) tickmode=%d",
					(u32)g_StageNum,
					g_Vars.roomcount,
					(void *)pprop,
					firstRoom,
					camRoom,
					(double)px, (double)py, (double)pz,
					g_Vars.lvframe60,
					(void *)g_BgPrimaryData,
					pr ? (int)pr->loaded240 : -1,
					pr ? (unsigned)pr->flags : 0,
					(void *)(pr ? pr->gfxdata : NULL),
					g_BgNumRoomLoadCandidates,
					(double)g_Vars.currentplayer->cam_pos.x,
					(double)g_Vars.currentplayer->cam_pos.y,
					(double)g_Vars.currentplayer->cam_pos.z,
					(int)g_Vars.tickmode);
				/* Arm the settled snapshot — fires once ~0.5s later. */
				s_B193FirstRenderDiagPending = 2;  /* 2 = waiting for frame 30 */
				s_B193SettledFrameTarget = g_Vars.lvframe60 + 30;
			} else if (s_B193FirstRenderDiagPending == 2 &&
					STAGE_IS_GAMEPLAY(g_StageNum) && g_Vars.currentplayer &&
					g_Vars.lvframe60 >= s_B193SettledFrameTarget) {
				struct prop *pprop = g_Vars.currentplayer->prop;
				s32 firstRoom = (pprop && pprop->rooms[0] != (RoomNum)-1) ? pprop->rooms[0] : -1;
				s32 camRoom = g_Vars.currentplayer->cam_room;
				f32 px = pprop ? pprop->pos.x : 0.0f;
				f32 py = pprop ? pprop->pos.y : 0.0f;
				f32 pz = pprop ? pprop->pos.z : 0.0f;
				extern u8 *g_BgPrimaryData;
				extern struct room *g_Rooms;
				extern s32 g_BgNumRoomLoadCandidates;
				extern s32 g_BgNumDrawSlots;
				/* B-193 Phase 4: portal-walker branch state. */
				extern struct bgcmd *g_BgCommands;
				extern struct bgportal *g_BgPortals;
				extern u32 g_BgRoomTestsDisabled;
				extern s32 g_BgNumForceOnscreenRooms;
				const struct room *pr = (g_Rooms && camRoom >= 0 && camRoom < g_Vars.roomcount)
					? &g_Rooms[camRoom] : NULL;
				/* B-193 Phase 3: count loaded / onscreen rooms + list first 16
				 * onscreen room numbers. Confirmed hypothesis A on earlier
				 * bad-boot log: `loaded=3 onscreen=3 drawslots=3
				 * onscreen_list=[13,15,16]`. Only 3 of 141 rooms reach
				 * ROOMFLAG_ONSCREEN, which explains CI rendering as sky
				 * with a few pieces. Phase 4 adds the branch-state needed
				 * to pin WHY the walker bails:
				 *   bg_commands      = g_BgCommands ptr (NULL → no bg cmds)
				 *   portals0_vtx     = g_BgPortals[0].verticesoffset
				 *                      (0 → fallback enumerate-all path,
                                          else → snake-walk from g_CamRoom)
				 *   tests_disabled   = g_BgRoomTestsDisabled after
				 *                      bgCmdExecute (1 → fallback skipped)
				 *   force_onscreen   = g_BgNumForceOnscreenRooms (how
				 *                      many rooms BGCMD_IFRESULT_SHOWROOM
				 *                      marked onscreen; if 3 with tests
				 *                      disabled, the cmd script shows
				 *                      only 3 and skips the snake) */
				s32 loaded_count = 0;
				s32 onscreen_count = 0;
				char onscreen_list[128] = {0};
				s32 listpos = 0;
				if (g_Rooms) {
					for (s32 ri = 1; ri < g_Vars.roomcount; ri++) {
						if (g_Rooms[ri].loaded240) loaded_count++;
						if (g_Rooms[ri].flags & ROOMFLAG_ONSCREEN) {
							onscreen_count++;
							if (onscreen_count <= 16 && listpos < (s32)sizeof(onscreen_list) - 8) {
								listpos += snprintf(onscreen_list + listpos,
									sizeof(onscreen_list) - listpos,
									"%s%d",
									listpos ? "," : "",
									ri);
							}
						}
					}
				}
				sysLogPrintf(LOG_VERBOSE,
					"LV.DIAG: settled stage=0x%02x roomcount=%d player_room=%d camera_room=%d pos=(%.0f,%.0f,%.0f) frame=%d bg_primary=%p cam_loaded240=%d cam_flags=0x%04x cam_gfxdata=%p load_cands=%d loaded=%d onscreen=%d drawslots=%d onscreen_list=[%s] bg_commands=%p portals0_vtx=%u tests_disabled=%d force_onscreen=%d cam_pos=(%.0f,%.0f,%.0f) tickmode=%d",
					(u32)g_StageNum,
					g_Vars.roomcount,
					firstRoom,
					camRoom,
					(double)px, (double)py, (double)pz,
					g_Vars.lvframe60,
					(void *)g_BgPrimaryData,
					pr ? (int)pr->loaded240 : -1,
					pr ? (unsigned)pr->flags : 0,
					(void *)(pr ? pr->gfxdata : NULL),
					g_BgNumRoomLoadCandidates,
					loaded_count,
					onscreen_count,
					g_BgNumDrawSlots,
					onscreen_list,
					(void *)g_BgCommands,
					(unsigned)(g_BgPortals ? g_BgPortals[0].verticesoffset : 0xFFFFFFFFu),
					(int)g_BgRoomTestsDisabled,
					g_BgNumForceOnscreenRooms,
					(double)g_Vars.currentplayer->cam_pos.x,
					(double)g_Vars.currentplayer->cam_pos.y,
					(double)g_Vars.currentplayer->cam_pos.z,
					(int)g_Vars.tickmode);
				s_B193FirstRenderDiagPending = 0;
			}

			gdl = lvRender(gdl);

			if (debugGetProfileMode() >= 2) {
				gdl = profileRender(gdl);
			}

			gdl = conRender(gdl);
			gdl = netDebugRender(gdl);

			gDPFullSync(gdl++);
			gSPEndDisplayList(gdl++);
		}

		if (g_MainGameLogicEnabled) {
			gfxSwapBuffers();
			viUpdateMode();
		}

		rdpCreateTask(gdlstart, gdl, 0, (uintptr_t) &msg);
		memaPrint();
		profileSetMarker(PROFILE_MAINTICK_END);
		discordTick();
	}
}

void mainEndStage(void)
{
	sndStopNosedive();
	sceneFire(SCENE_EVENT_CUTSCENE_END, NULL);

	if (!g_MainIsEndscreen) {
		pak0f11c6d0();
		joyDisableTemporarily();

		if (g_Vars.coopplayernum >= 0) {
			s32 prevplayernum = g_Vars.currentplayernum;
			s32 i;

			for (i = 0; i < LOCALPLAYERCOUNT(); i++) {
				setCurrentPlayerNum(i);
				endscreenPushCoop();
			}

			setCurrentPlayerNum(prevplayernum);
			musicStartMenu();
		} else if (g_Vars.antiplayernum >= 0) {
			s32 prevplayernum = g_Vars.currentplayernum;
			s32 i;

			for (i = 0; i < LOCALPLAYERCOUNT(); i++) {
				setCurrentPlayerNum(i);
				endscreenPushAnti();
			}

			setCurrentPlayerNum(prevplayernum);
			musicStartMenu();
		} else if (g_Vars.normmplayerisrunning) {
			mpEndMatch();
		} else {
			endscreenPrepare();
			musicStartMenu();
		}

		netServerStageEnd();
	}

	g_MainIsEndscreen = 1;
}

/**
 * Change to the given stage at the end of the current frame.
 */
void mainChangeToStage(s32 stagenum)
{
	pak0f11c6d0();

	stagenum = stageSanitizeLoadStagenum(stagenum);

	/* Phase 1: diff-based asset lifecycle — build/diff/apply before the
	 * stage is committed.
	 *
	 * MP path: g_ClientManifest was populated by SVC_MATCH_MANIFEST from
	 * the server; use it directly as the "needed" manifest.
	 *
	 * SP path: g_ClientManifest is empty (pure SP, no active server
	 * manifest); build the mission manifest from catalog + setup data.
	 * Note: g_StageSetup.props is NULL at this point (setup files not yet
	 * loaded).  The setup-props scan runs post-load via
	 * manifestSPRescanSetup() called from lvInit().
	 *
	 * Menu/system path: build an all-character manifest so the Skin Editor,
	 * Agent Select, Bot Setup, and Modding Hub can preview any character.
	 * Also clears the stale client manifest so the next SP/MP transition
	 * diffs correctly. */
	/* Mike directive 2026-05-17: social-hub coexistence. Update the
	 * presence local state on every stage transition so friends see
	 * accurate "in-match" / "online" status while we move between
	 * gameplay, The Grid, and menus. The state flip is cheap and does
	 * NOT tear down the presence socket -- friends stay reachable.
	 *
	 * Bug fix 2026-05-17 (Mike playtest): STAGE_IS_GAMEPLAY returns
	 * true for STAGE_CITRAINING which is ALSO the OG Main Menu's
	 * backdrop stage, so a fresh boot would show "in-match" while
	 * the user is actually in the menu. The correct predicate
	 * additionally requires (looksLikeMP || mission_active) -- the
	 * same signals the existing manifest-classification block at
	 * line 1083 uses to disambiguate menu vs match. */
	if (presenceIsAgentLoaded()) {
		const bool looksLikeMP = (g_NetMode != NETMODE_NONE)
			|| g_MissionConfig.iscoop || g_MissionConfig.isanti
			|| g_Vars.normmplayerisrunning;
		const bool inMission = (g_MissionConfig.stageindex >= 0);
		if (STAGE_IS_GAMEPLAY(stagenum) && (looksLikeMP || inMission)) {
			presenceSetLocalState(PRESENCE_IN_MATCH);
		} else {
			presenceSetLocalState(PRESENCE_ONLINE_IDLE);
		}
	}

	if (STAGE_IS_GAMEPLAY(stagenum)) {
		if (g_ClientManifest.num_entries > 0) {
			/* S303: log every MP transition with its asset counts so the
			 * playtest trail shows exactly what was diffed. If the local
			 * session is supposed to be SP (netmode==NONE, !iscoop, !isanti)
			 * the manifest should not have entries here — emit a WARNING so
			 * leaked MP state from a prior session is obvious in pd.log. */
			const bool looksLikeMP = (g_NetMode != NETMODE_NONE)
				|| g_MissionConfig.iscoop || g_MissionConfig.isanti
				|| g_Vars.normmplayerisrunning;
			if (!looksLikeMP) {
				sysLogPrintf(LOG_WARNING,
					"GAMELOOP.MANIFEST: stale MP manifest (%d entries) leaked into SP transition to 0x%02x — routing via MPTransition",
					g_ClientManifest.num_entries, (u32)stagenum);
			} else {
				sysLogPrintf(LOG_NOTE,
					"GAMELOOP.MANIFEST: MP transition to 0x%02x (manifest=%d netmode=%d iscoop=%d isanti=%d)",
					(u32)stagenum, g_ClientManifest.num_entries,
					(int)g_NetMode, g_MissionConfig.iscoop, g_MissionConfig.isanti);
			}
			manifestMPTransition();
		} else {
			sysLogPrintf(LOG_NOTE,
				"GAMELOOP.MANIFEST: SP transition to 0x%02x (coop=%d anti=%d)",
				(u32)stagenum, g_MissionConfig.iscoop, g_MissionConfig.isanti);
			manifestSPTransition(stagenum);
		}
	} else {
		sysLogPrintf(LOG_NOTE,
			"GAMELOOP.MANIFEST: menu transition to 0x%02x — clearing manifest (%d entries)",
			(u32)stagenum, g_ClientManifest.num_entries);
		manifestClear(&g_ClientManifest);
		manifestMenuTransition();
	}

	if (g_MainChangeToStageNum >= 0 && g_MainChangeToStageNum != stagenum) {
		sysLogPrintf(LOG_WARNING,
			"MAIN: replacing pending stage change 0x%02x -> 0x%02x",
			(u32)g_MainChangeToStageNum, (u32)stagenum);
	}

	g_MainChangeToStageNum = stagenum;
}

s32 mainGetStageNum(void)
{
	return g_StageNum;
}

void func0000e990(void)
{
	objectivesCheckAll();
	objectivesDisableChecking();
	mainEndStage();
}
