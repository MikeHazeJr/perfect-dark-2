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

/* Include types.h for struct definitions. Do NOT include bss.h — it only
 * has extern declarations that we need to DEFINE here. */
#include "types.h"
#include "constants.h"
#include "data.h"
#include "system.h"
#include "lib/main.h"

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
struct mparena g_MpArenas[1];                  /* data.h:461 — array, provide 1 entry */

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
void mpStartMatch(void) { sysLogPrintf(LOG_NOTE, "STUB: mpStartMatch"); }
void mpSetPaused(s32 mode) { (void)mode; }
void mainChangeToStage(s32 stagenum) { sysLogPrintf(LOG_NOTE, "STUB: mainChangeToStage(0x%02x)", stagenum); }
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

/* --- Setup / Config --- */
char *mpGetBodyName(u8 bodynum) { (void)bodynum; return "Default"; }
u32 mpGetNumBodies(void) { return 0; }
s32 mpGetMpheadnumByMpbodynum(s32 bodynum) { (void)bodynum; return 0; }
/* modmgr stubs — fs.c references these */
s32 modmgrGetCount(void) { return 0; }
void *modmgrGetMod(s32 idx) { (void)idx; return NULL; }
const char *modmgrResolvePath(const char *path) { return path; }
void modmgrInit(void) {}
void modmgrCatalogChanged(void) {}

/* assetcatalog_resolve stubs — fs.c and lv.c reference these */
const char *assetCatalogResolvePath(const char *path) { (void)path; return NULL; }
void assetCatalogActivateStage(s32 stagenum) { (void)stagenum; }
void assetCatalogDeactivateStage(void) {}
struct asset_entry; /* forward decl for return type */
const struct asset_entry *assetCatalogFindModMapByStagenum(s32 stagenum) { (void)stagenum; return NULL; }

/* --- Console (excluded from server build) --- */
void conInit(void) {}
void conPrintLn(s32 showmsg, const char *text) { if (text) printf("%s\n", text); }
