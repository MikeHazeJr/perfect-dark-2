#include <stdlib.h>

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
#include "game/lv.h"
#include "game/timing.h"
#include "game/music.h"
#include "game/stubs/game_175f50.h"
#include "game/game_175f90.h"
#include "game/zbuf.h"
#include "game/game_1a78b0.h"
#include "game/mplayer/mplayer.h"
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
#include "modelcatalog.h"

extern u8 *g_MempHeap;
extern u32 g_MempHeapSize;

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
	{ STAGE_4MBMENU,       "-mgfx100 -mvtx50 -ma50"                        },
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

	if (g_StageNum == STAGE_CITRAINING && IS4MB()) {
		g_StageNum = STAGE_4MBMENU;
	}

	rngSetSeed(osGetCount());

	// Outer loop - this is infinite because ending is never changed
	while (!ending) {
		g_MainNumGfxTasks = 0;
		g_MainGameLogicEnabled = 1;
		g_MainIsEndscreen = 0;

		if (var8005d9b0 && var8005d9c4 == 0) {
			index = -1;

			if (IS4MB()) {
				if (STAGE_IS_GAMEPLAY(g_StageNum) && getNumPlayers() >= 2) {
					index = 0; \
					while (g_StageAllocations4Mb[index].stagenum) { \
						if (g_StageAllocations4Mb[index].stagenum == g_StageNum + 400) { \
							break; \
						} \
						index++;
					}

					if (g_StageAllocations4Mb[index].stagenum == 0) {
						index = -1;
					}
				}

				if (index);

				if (index < 0) {
					index = 0;
					while (g_StageAllocations4Mb[index].stagenum) {
						if (g_StageNum == g_StageAllocations4Mb[index].stagenum) {
							break;
						}

						index++;
					}
				}

				argSetString(g_StageAllocations4Mb[index].string);
			} else {
				// 8MB
				if (STAGE_IS_GAMEPLAY(g_StageNum) && getNumPlayers() >= 2) {
					index = 0; \
					while (g_StageAllocations8Mb[index].stagenum) { \
						if (g_StageNum + 400 == g_StageAllocations8Mb[index].stagenum) { \
							break; \
						} \
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
			if (g_Vars.antiplayernum < 0) {
				// Counter-Operative now uses a different approach which allows more than 2 players.
				// Co-Operative, on the other hand, is currently limited to 2 players.
				g_MpSetup.chrslots = 0x03;
			}
			mpReset();
		} else if (g_Vars.perfectbuddynum) {
			mpReset();
		} else if (g_Vars.mplayerisrunning == 0
				&& (numplayers >= 2 || g_Vars.lvmpbotlevel || argFindByPrefix(1, "-play"))) {
			g_MpSetup.chrslots = 1;

			for (s32 i = 1; i < numplayers; ++i) {
				g_MpSetup.chrslots |= 1u << i;
			}

			g_MpSetup.stagenum = g_StageNum;
			mpReset();
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

		lvStop();
		mempDisablePool(MEMPOOL_STAGE);
		mempDisablePool(MEMPOOL_7);
		filesStop(4);
		viBlack(1);
		pak0f116994();

		g_StageNum = g_MainChangeToStageNum;
		g_MainChangeToStageNum = -1;
	}
}

void mainTick(void)
{
	Gfx *gdl = NULL;
	Gfx *gdlstart = NULL;
	OSScMsg msg = {OS_SC_DONE_MSG};
	s32 i;

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
	}
}

void mainEndStage(void)
{
	sndStopNosedive();

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

	/* Phase 1: diff-based asset lifecycle — build/diff/apply before the
	 * stage is committed.  Only fires for gameplay stages (not title,
	 * credits, menus).
	 *
	 * MP path: g_ClientManifest was populated by SVC_MATCH_MANIFEST from
	 * the server; use it directly as the "needed" manifest.
	 *
	 * SP path: g_ClientManifest is empty (pure SP, no active server
	 * manifest); build the mission manifest from catalog + setup data.
	 *
	 * Non-gameplay: clear any stale client manifest so the next SP
	 * mission takes the SP path, not a leftover MP manifest. */
	if (STAGE_IS_GAMEPLAY(stagenum)) {
		if (g_ClientManifest.num_entries > 0) {
			manifestMPTransition();
		} else {
			manifestSPTransition(stagenum);
		}
	} else {
		manifestClear(&g_ClientManifest);
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
