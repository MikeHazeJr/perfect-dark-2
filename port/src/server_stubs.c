/**
 * server_stubs.c — Comprehensive stubs for ALL game dependencies.
 *
 * Provides empty/minimal implementations of every game function and global
 * that the networking code references. Uses types.h/data.h/bss.h for correct
 * types — only the VALUES are stubbed, not the types.
 *
 * ONLY compiled into pd-server, never into the game client.
 */

#include <PR/ultratypes.h>
#include <PR/ultrasched.h>
#include <PR/os_message.h>
#include <PR/gbi.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Include types.h for struct definitions. Do NOT include bss.h — it only
 * has extern declarations that we need to DEFINE here. */
#include "types.h"
#include "constants.h"
#include "data.h"
#include "system.h"
#include "lib/main.h"
#include "scenario_save.h"  /* struct matchconfig for g_MatchConfig stub */
#include "net/netlobby.h"   /* g_Lobby.settings.numSimulants for mpStartMatch stub */

/* ========================================================================
 * Globals — definitions for everything bss.h declares as extern.
 * The game normally provides these from compiled game .c files.
 * The server stubs them with zero/default values.
 * ======================================================================== */

/* Main / scheduler */
u32 g_OsMemSize = 0;
u8 *g_MempHeap = NULL;
u32 g_MempHeapSize = 0;
s32 g_SkipIntro = 1;
s32 g_FileAutoSelect = 0;
u8  g_VmShowStats = 0;
s32 g_TickRateDiv = 1;
s32 g_TickExtraSleep = 1;
s8  g_Resetting = 0;
s32 g_OsMemSizeMb = 64;
OSSched g_Sched;
OSMesgQueue g_MainMesgQueue;
OSMesg g_MainMesgBuf[32];
s32 g_MainIsBooting = 0;
OSMesgQueue *g_PiMesgQueue = NULL;

/* Game state — types must match declarations in data.h / bss.h exactly */
struct g_vars g_Vars;
s32 g_StageNum = 0;                           /* data.h:582 */
struct mpsetup g_MpSetup;
struct missionconfig g_MissionConfig;
struct mpplayerconfig g_PlayerConfigsArray[MAX_MPPLAYERCONFIGS];
struct extplayerconfig g_PlayerExtCfg[MAX_PLAYERS];
s32 g_MpPlayerNum = 0;
u64 g_RngSeed = 0;                            /* data.h:583 */
u64 g_Rng2Seed = 0;                           /* data.h:584 */
u64 g_RngSeeds[2] = {0};
s32 g_NotLoadMod = 1;
const char *g_RomName = "";  /* dedicated server has no ROM — empty skips the ROM check in netmsgClcAuthRead */
s32 g_NumReasonsToEndMpMatch = 0;
s32 g_MainChangeToStageNum = -1;
struct matchconfig g_MatchConfig;  /* server stub — netmsg.c references this for CLC_LOBBY_START bot config */

/* Bot / chr */
u8 g_BotCount = 0;
struct mpbotconfig g_BotConfigsArray[MAX_BOTS];
struct chrdata *g_MpBotChrPtrs[MAX_MPCHRS];
struct chrdata *g_ChrSlots = NULL;             /* data.h:142 — pointer, not array */
s32 g_NumChrSlots = 0;

/* MP state */
struct mpchrconfig *g_MpAllChrConfigPtrs[MAX_MPCHRS];
s32 g_MpNumChrs = 0;

/* Objectives (co-op) */
struct objective g_Objectives[MAX_OBJECTIVES];
u8 g_ObjectiveStatuses[MAX_OBJECTIVES];
s32 g_ObjectiveLastIndex = 0;
u32 g_StageFlags = 0;
s32 g_AlarmTimer = 0;
s32 g_InCutscene = 0;

/* Menu / UI state */
struct menudata g_MenuData;
struct menu g_Menus[MAX_PLAYERS];
s32 g_MenuKeyboardPlayer = -1;
s32 g_SndDisabled = 1;

/* Rendering state — data.h types */
struct font *g_FontHandelGothicXs = NULL;      /* data.h:421 */
struct fontchar *g_CharsHandelGothicXs = NULL; /* data.h:422 */
struct modelstate g_ModelStates[NUM_MODELS];    /* data.h:341 */
OSViMode osViModeTable[1];                     /* data.h:81 — array, provide 1 entry */
void *g_ViBackData = NULL;

/* File lists */
struct filelist *g_FileLists[MAX_PLAYERS];      /* data.h:301 */
struct gamefile g_GameFile;
struct fileguid g_GameFileGuid;

/* MP bodies/heads/arenas */
struct mpbody g_MpBodies[63];
struct mphead g_MpHeads[76];
/* Full arena table — stagenum values must match setup.c g_MpArenas[].
 * requirefeature and name are unused server-side; only stagenum matters
 * for catalog registration and match dispatch. */
struct mparena g_MpArenas[] = {
    /* Dark (0-12) */
    { STAGE_MP_SKEDAR,      0, 0 }, { STAGE_MP_PIPES,       0, 0 },
    { STAGE_MP_RAVINE,      0, 0 }, { STAGE_MP_G5BUILDING,  0, 0 },
    { STAGE_MP_SEWERS,      0, 0 }, { STAGE_MP_WAREHOUSE,   0, 0 },
    { STAGE_MP_GRID,        0, 0 }, { STAGE_MP_RUINS,       0, 0 },
    { STAGE_MP_AREA52,      0, 0 }, { STAGE_MP_BASE,        0, 0 },
    { STAGE_MP_FORTRESS,    0, 0 }, { STAGE_MP_VILLA,       0, 0 },
    { STAGE_MP_CARPARK,     0, 0 },
    /* Solo Missions (13-26) */
    { STAGE_DEFECTION,      0, 0 }, { STAGE_INVESTIGATION,  0, 0 },
    { STAGE_VILLA,          0, 0 }, { STAGE_CHICAGO,        0, 0 },
    { STAGE_G5BUILDING,     0, 0 }, { STAGE_INFILTRATION,   0, 0 },
    { STAGE_AIRBASE,        0, 0 }, { STAGE_AIRFORCEONE,    0, 0 },
    { STAGE_CRASHSITE,      0, 0 }, { STAGE_PELAGIC,        0, 0 },
    { STAGE_DEEPSEA,        0, 0 }, { STAGE_DEFENSE,        0, 0 },
    { STAGE_ATTACKSHIP,     0, 0 }, { STAGE_SKEDARRUINS,    0, 0 },
    /* Classic (27-31) */
    { STAGE_MP_TEMPLE,      0, 0 }, { STAGE_MP_COMPLEX,     0, 0 },
    { STAGE_TEST_MP6,       0, 0 }, { STAGE_TEST_MP2,       0, 0 },
    { STAGE_MP_FELICITY,    0, 0 },
    /* GoldenEye X (32-54, omitted from arena-list but stagenum still needed) */
    { STAGE_EXTRA6,         0, 0 }, { STAGE_EXTRA2,         0, 0 },
    { STAGE_EXTRA8,         0, 0 }, { STAGE_EXTRA9,         0, 0 },
    { STAGE_EXTRA13,        0, 0 }, { STAGE_EXTRA15,        0, 0 },
    { STAGE_EXTRA10,        0, 0 }, { STAGE_EXTRA11,        0, 0 },
    { STAGE_EXTRA4,         0, 0 }, { STAGE_EXTRA12,        0, 0 },
    { STAGE_EXTRA14,        0, 0 }, { STAGE_TEST_MP17,      0, 0 },
    { STAGE_EXTRA1,         0, 0 }, { STAGE_TEST_SILO,      0, 0 },
    { STAGE_TEST_MP16,      0, 0 }, { STAGE_TEST_MP14,      0, 0 },
    { STAGE_EXTRA3,         0, 0 }, { STAGE_TEST_MP18,      0, 0 },
    { STAGE_EXTRA5,         0, 0 }, { STAGE_TEST_MP20,      0, 0 },
    { STAGE_TEST_MP19,      0, 0 }, { STAGE_EXTRA7,         0, 0 },
    { STAGE_TEST_MP8,       0, 0 },
    /* Bonus (55-70) */
    { STAGE_24,             0, 0 }, { STAGE_TEST_MP7,       0, 0 },
    { STAGE_TEST_ARCH,      0, 0 }, { STAGE_TEST_DEST,      0, 0 },
    { STAGE_EXTRA16,        0, 0 }, { STAGE_EXTRA17,        0, 0 },
    { STAGE_EXTRA18,        0, 0 }, { STAGE_EXTRA19,        0, 0 },
    { STAGE_EXTRA20,        0, 0 }, { STAGE_EXTRA21,        0, 0 },
    { STAGE_EXTRA22,        0, 0 }, { STAGE_EXTRA23,        0, 0 },
    { STAGE_EXTRA24,        0, 0 }, { STAGE_EXTRA25,        0, 0 },
    { STAGE_EXTRA26,        0, 0 }, { STAGE_TEST_LAM,       0, 0 },
    /* Random (71-72) */
    { STAGE_MP_RANDOM_MULTI,0, 0 }, { STAGE_MP_RANDOM_SOLO, 0, 0 },
    /* 73-74: Random GEX + junk entry — omitted from arena-list, keep for index fidelity */
    { STAGE_MP_RANDOM_GEX,  0, 0 }, { 1,                    0, 0 },
};

/* Solo stages */
struct solostage g_SoloStages[21];

void *bootAllocateStack(s32 threadid, s32 size) {
    static u8 stackbuf[0x1000];
    (void)threadid; (void)size;
    return stackbuf;
}

/* ========================================================================
 * Function stubs — empty implementations for linker satisfaction.
 * Grouped by subsystem.
 * ======================================================================== */

/* --- Player / Character --- */
void playerDie(s32 arg) { (void)arg; }
void playerDieByShooter(s32 shooter, s32 arg) { (void)shooter; (void)arg; }
void playerStartNewLife(void) {}
void setCurrentPlayerNum(s32 num) { (void)num; }
s32 playermgrGetPlayerNumByProp(struct prop *prop) { (void)prop; return 0; }
void chrSetPos(struct chrdata *chr, struct coord *pos, s16 *rooms, f32 angle, s32 onground) {
    (void)chr; (void)pos; (void)rooms; (void)angle; (void)onground;
}
s32 chrIsDead(struct chrdata *chr) { (void)chr; return 0; }
f32 chrGetInverseTheta(struct chrdata *chr) { (void)chr; return 0.0f; }
void chrSetLookAngle(struct chrdata *chr, f32 angle) { (void)chr; (void)angle; }
void chrDamage(struct prop *prop, f32 damage, struct coord *pos, s32 hitpart,
               s32 attackernum, s32 weaponnum, s32 arg6) {
    (void)prop; (void)damage; (void)pos; (void)hitpart;
    (void)attackernum; (void)weaponnum; (void)arg6;
}

/* --- Weapon / Equipment --- */
void bgunEquipWeapon(s32 weaponnum) { (void)weaponnum; }
void bgunEquipWeapon2(s32 hand, s32 weaponnum) { (void)hand; (void)weaponnum; }
f32 currentPlayerGetGunZoomFov(void) { return 60.0f; }
s32 bgunIsUsingSecondaryFunction(void) { return 0; }
s32 weaponHasFlag(s32 weaponnum, s32 flag) { (void)weaponnum; (void)flag; return 0; }
void weaponDeleteFromChr(struct chrdata *chr, s32 hand) { (void)chr; (void)hand; }
struct defaultobj *weaponCreate(struct prop *prop, struct model *model, s32 weaponnum) {
    (void)prop; (void)model; (void)weaponnum; return NULL;
}
void invRemoveItemByNum(s32 itemnum) { (void)itemnum; }

/* --- Match / Stage --- */
/*
 * mpStartMatch — dedicated server stub.
 *
 * The real implementation (mplayer.c) loads textures, calls mainChangeToStage(),
 * etc. — none of which are available on the server binary.  Instead, allocate
 * minimal chrdata/prop/aibot structs on the heap for each simulant so that:
 *
 *   1. g_BotCount > 0 → the SVC_CHR_MOVE broadcast loop in netEndFrame fires.
 *   2. Each stub has chr->prop and chr->aibot set → netmsgSvcChrMoveWrite passes
 *      its guard and writes a valid (if zeroed) position.
 *   3. When the authority client sends CLC_BOT_MOVE, netmsgClcBotMoveRead fills
 *      in prop->syncid and prop->pos so the relay carries real positions.
 */
void mpStartMatch(void)
{
	s32 numBots = (s32)g_Lobby.settings.numSimulants;

	/* Free any stubs left over from a previous match */
	for (s32 i = 0; i < MAX_BOTS; i++) {
		if (g_MpBotChrPtrs[i]) {
			free(g_MpBotChrPtrs[i]->prop);
			free(g_MpBotChrPtrs[i]->aibot);
			free(g_MpBotChrPtrs[i]);
			g_MpBotChrPtrs[i] = NULL;
		}
	}
	g_BotCount = 0;

	if (numBots <= 0) {
		sysLogPrintf(LOG_NOTE, "SERVER: mpStartMatch: 0 simulants requested — no bot stubs allocated");
		return;
	}

	if (numBots > MAX_BOTS) {
		numBots = MAX_BOTS;
	}

	for (s32 i = 0; i < numBots; i++) {
		struct chrdata *chr   = calloc(1, sizeof(struct chrdata));
		struct prop    *prop  = calloc(1, sizeof(struct prop));
		struct aibot   *aibot = calloc(1, sizeof(struct aibot));

		if (!chr || !prop || !aibot) {
			sysLogPrintf(LOG_ERROR, "SERVER: mpStartMatch: allocation failed for bot stub %d", i);
			free(chr); free(prop); free(aibot);
			break;
		}

		/* Minimal wiring so netmsgSvcChrMoveWrite passes its guards */
		chr->prop  = prop;
		prop->chr  = chr;
		chr->aibot = aibot;

		aibot->aibotnum = (u8)i;

		/* prop->syncid and prop->pos are zero until the authority client sends
		 * the first CLC_BOT_MOVE update — the relay will begin carrying real
		 * data from that point onwards. */

		g_MpBotChrPtrs[i] = chr;
		g_BotCount = (u8)(i + 1);
	}

	sysLogPrintf(LOG_NOTE, "SERVER: mpStartMatch: allocated %u bot stubs (requested %d)", (u32)g_BotCount, numBots);
}
void mpSetPaused(s32 mode) { (void)mode; }
void mainChangeToStage(s32 stagenum) {
    sysLogPrintf(LOG_NOTE, "STUB: mainChangeToStage(0x%02x)", stagenum);
    /* netmsgSvcStageStartWrite reads g_MainChangeToStageNum to determine the
     * effective stage (falls back to g_StageNum when -1). On the game client
     * the real mainChangeToStage sets this; the stub must do the same so
     * SVC_STAGE_START carries the correct arena instead of 0x00. */
    g_MainChangeToStageNum = stagenum;
}
void mainEndStage(void) { sysLogPrintf(LOG_NOTE, "STUB: mainEndStage"); }
void titleSetNextStage(s32 stagenum) { (void)stagenum; }
void titleSetNextMode(s32 mode) { (void)mode; }
s32 lvGetDifficulty(void) { return 0; }
void lvSetDifficulty(s32 diff) { (void)diff; }
void setNumPlayers(s32 count) { (void)count; }
void objectivesDisableChecking(void) {}
s32 objectiveGetDifficultyBits(s32 idx) { (void)idx; return 0x7; }
void mpInit(void) {}

/* --- Prop / Object --- */
struct prop *propAllocate(void) { return NULL; }
void propActivate(struct prop *prop) { (void)prop; }
void propRegisterRooms(struct prop *prop) { (void)prop; }
void propDeregisterRooms(struct prop *prop) { (void)prop; }
void propEnable(struct prop *prop) { (void)prop; }
void propDisable(struct prop *prop) { (void)prop; }
void propPause(struct prop *prop) { (void)prop; }
void propReparent(struct prop *prop, struct prop *parent) { (void)prop; (void)parent; }
void propPickupByPlayer(struct prop *prop, s32 playernum) { (void)prop; (void)playernum; }
void propExecuteTickOperation(struct prop *prop) { (void)prop; }
void propobjInteract(struct prop *prop) { (void)prop; }
void objDamage(struct prop *prop, f32 damage, struct coord *pos,
               s32 weaponnum, s32 playernum) {
    (void)prop; (void)damage; (void)pos; (void)weaponnum; (void)playernum;
}
void objSetDropped(struct prop *prop, s32 arg) { (void)prop; (void)arg; }
void doorSetMode(struct prop *prop, s32 mode) { (void)prop; (void)mode; }
void roomsCopy(s16 *dst, s16 *src) { if (dst && src) memcpy(dst, src, 8 * sizeof(s16)); }
struct modeldef *setupLoadModeldef(s32 filenum) { (void)filenum; return NULL; }
struct stagesetup g_StageSetup; /* zero-initialised; props=NULL so manifest scan skips */
u32 setupGetCmdLength(u32 *cmd) { (void)cmd; return 1; } /* stub — never reached (props==NULL) */
struct model *modelmgrInstantiateModelWithoutAnim(struct modeldef *def) { (void)def; return NULL; }
void laptopDeploy(struct prop *prop) { (void)prop; }
struct prop *psCreate(void *a, void *b, void *c, void *d) {
    (void)a; (void)b; (void)c; (void)d; return NULL;
}
void modelSetScale(struct model *model, f32 scale) { (void)model; (void)scale; }
void func0f0685e4(struct prop *prop) { (void)prop; }
void func0f08adc8(void *arg) { (void)arg; }
void mtx4LoadRotation(Mtxf *mtx, f32 angle, struct coord *axis) { (void)mtx; (void)angle; (void)axis; }

/* --- Menu --- */
void menuPushDialog(struct menudialogdef *d) { (void)d; }
void menuPopDialog(void) {}
s32 menuIsDialogOpen(struct menudialogdef *d) { (void)d; return 0; }
void menuStop(void) {}

/* --- Display / Rendering --- */
void viBlack(s32 black) { (void)black; }
s32 viGetWidth(void) { return 800; }
s32 viGetHeight(void) { return 500; }
char *langGet(s32 textid) { (void)textid; return ""; }
Gfx *text0f153628(Gfx *gdl) { return gdl; }
Gfx *text0f153780(Gfx *gdl) { return gdl; }
Gfx *textRenderProjected(Gfx *gdl, s32 *x, s32 *y, const char *text,
                          void *chars, void *font, u32 colour, s32 w, s32 h, s32 arg, s32 arg2) {
    (void)x; (void)y; (void)text; (void)chars; (void)font; (void)colour;
    (void)w; (void)h; (void)arg; (void)arg2; return gdl;
}
void hudmsgCreate(const char *text, s32 type) { (void)text; (void)type; }
void hudmsgCreateWithFlags(const char *text, s32 type, s32 flags) { (void)text; (void)type; (void)flags; }
void hudmsgRenderBox(void) {}
Gfx *menuRenderModel(Gfx *gdl, struct menumodel *menumodel, s32 modeltype) {
    (void)menumodel; (void)modeltype; return gdl;
}
void alarmStopAudio(void) {}

/* --- Input --- */
s32 inputKeyPressed(s32 key) { (void)key; return 0; }
void inputStartTextInput(void) {}
void inputStopTextInput(void) {}
s32 inputTextHandler(char *buf, s32 maxlen, s32 *cursor, s32 multiline) {
    (void)buf; (void)maxlen; (void)cursor; (void)multiline; return 0;
}
void inputClearLastKey(void) {}
void inputClearLastTextChar(void) {}

/* --- File / ROM --- */
void gamefileLoadDefaults(struct gamefile *file) { if (file) memset(file, 0, sizeof(*file)); }
void romdataFileFreeForSolo(void) {}
s32  romdataFileGetNumForName(const char *name) { (void)name; return -1; } /* server: no ROM */

/* --- Setup / Config --- */
char *mpGetBodyName(u8 bodynum) { (void)bodynum; return "Default"; }
u32 mpGetNumBodies(void) { return 0; }
s32 mpGetMpheadnumByMpbodynum(s32 bodynum) { (void)bodynum; return 0; }
/* modmgr stubs — dedicated server has no mod registry */
s32 modmgrGetCount(void) { return 0; }
void *modmgrGetMod(s32 idx) { (void)idx; return NULL; }
void *modmgrFindMod(const char *id) { (void)id; return NULL; }
const char *modmgrResolvePath(const char *path) { return path; }
void modmgrInit(void) {}
void modmgrCatalogChanged(void) {}

/* assetcatalog_resolve stubs — fs.c and lv.c reference these */
const char *assetCatalogResolvePath(const char *path) { (void)path; return NULL; }
void assetCatalogActivateStage(s32 stagenum) { (void)stagenum; }
void assetCatalogDeactivateStage(void) {}
struct asset_entry; /* forward decl for return type */
const struct asset_entry *assetCatalogFindModMapByStagenum(s32 stagenum) { (void)stagenum; return NULL; }

/* --- Participant system (B-12) --- */
/* The server doesn't link participant.c (that's game-client code), but
 * netmsg.c calls mpParticipantsFromLegacyChrslots() in the SVC_STAGE_START
 * client path.  The server never enters that path, so an empty stub is fine. */
void mpParticipantsFromLegacyChrslots(u64 chrslots) { (void)chrslots; }

/* --- Console (excluded from server build) --- */
void conInit(void) {}
void conPrintLn(s32 showmsg, const char *text) { if (text) printf("%s\n", text); }

/* --- Game data tables (assetcatalog_base needs these; server has no real data) --- */
struct headorbody g_HeadsAndBodies[152];    /* zero-initialised; no model data on server */
struct stagetableentry *g_Stages = NULL;    /* no stage table on server */
s32 g_NumStages = 0;

/* --- assetcatalog_load stubs — server has no game asset filesystem --- */
s32  catalogLoadAsset(const char *assetId)   { (void)assetId; return 1; }
void catalogUnloadAsset(const char *assetId) { (void)assetId; }
