/**
 * server_stubs.c — Comprehensive stubs for ALL game dependencies.
 *
 * This file provides empty/minimal implementations of every game function
 * and global variable that the networking code (net.c, netmsg.c) references.
 * The dedicated server links ONLY against this + networking + system code.
 *
 * As we migrate the networking code to use net_interface.h callbacks,
 * stubs here will be replaced with proper callback invocations.
 *
 * ONLY compiled into pd-server, never into the game client.
 */

#include <PR/ultratypes.h>
#include <PR/gbi.h>
#include <string.h>
#include <stdio.h>
#include "system.h"

/* We need types.h for struct definitions referenced by net.c/netmsg.c,
 * but it redefines bool. Include it since this is a C file. */
#include "types.h"
#include "constants.h"
#include "data.h"

/* ========================================================================
 * Global variables referenced by networking code
 * ======================================================================== */

/* Game state — net.c reads these extensively */
struct vars g_Vars = {0};
u32 g_StageNum = 0;
struct mpsetup g_MpSetup = {0};
struct missionconfig g_MissionConfig = {0};
struct mpplayerconfig g_PlayerConfigsArray[MAX_MPPLAYERCONFIGS] = {0};
struct extplayerconfig g_PlayerExtCfg[MAX_PLAYERS] = {0};
s32 g_MpPlayerNum = 0;
u32 g_RngSeed = 0;
u32 g_Rng2Seed = 0;
u64 g_RngSeeds[2] = {0};
s32 g_NotLoadMod = 0;
char g_RomName[64] = "pd-server";
s32 g_NumReasonsToEndMpMatch = 0;

/* Server main globals */
u32 g_OsMemSize = 0;
u8 *g_MempHeap = NULL;
u32 g_MempHeapSize = 0;
s32 g_SkipIntro = 1;
s32 g_FileAutoSelect = 0;
u8  g_VmShowStats = 0;
s32 g_TickRateDiv = 1;
s32 g_TickExtraSleep = 1;
s8  g_Resetting = 0;

void *bootAllocateStack(s32 threadid, s32 size) {
    static u8 stackbuf[0x1000];
    (void)threadid; (void)size;
    return stackbuf;
}

/* Bot/chr state */
u8 g_BotCount = 0;
struct chrdata *g_MpBotChrPtrs[MAX_MPCHRS] = {0};
struct chrslot g_ChrSlots[MAX_MPCHRS] = {0};
s32 g_NumChrSlots = 0;

/* MP state */
struct mpchrconfig *g_MpAllChrConfigPtrs[MAX_MPCHRS] = {0};
s32 g_MpNumChrs = 0;

/* Objectives (co-op) */
struct objective g_Objectives[MAX_OBJECTIVES] = {0};
u8 g_ObjectiveStatuses[MAX_OBJECTIVES] = {0};
s32 g_ObjectiveLastIndex = 0;
u32 g_StageFlags = 0;
s32 g_AlarmTimer = 0;
s32 g_InCutscene = 0;

/* Model state — netmsg.c references for prop sync */
void *g_ModelStates = NULL;

/* Menu state — referenced by various port code */
struct menudata g_MenuData = {0};
struct menu g_Menus[MAX_PLAYERS] = {0};
s32 g_MenuKeyboardPlayer = -1;

/* File lists */
struct filelist *g_FileLists[4] = {0};

/* Game file */
struct gamefile g_GameFile = {0};
struct fileguid g_GameFileGuid = {0};

/* Rendering stubs */
void *g_FontHandelGothicXs = NULL;
void *g_CharsHandelGothicXs = NULL;
s32 g_MainIsBooting = 0;
void *g_PiMesgQueue = NULL;
s32 g_SndDisabled = 1;  /* Sound disabled on server */
void *g_ViBackData = NULL;
void *osViModeTable = NULL;

/* MP bodies/heads/arenas — referenced by modmgr which is excluded,
 * but netmenu.c or net.c may reference indirectly */
struct mpbody g_MpBodies[63] = {0};
struct mphead g_MpHeads[76] = {0};
void *g_MpArenas = NULL;

/* Solo stages — referenced by netmenu.c for co-op */
struct solostage g_SoloStages[21] = {0};

/* ========================================================================
 * Player / Character function stubs
 * ======================================================================== */

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

/* ========================================================================
 * Weapon / Equipment stubs
 * ======================================================================== */

void bgunEquipWeapon(s32 weaponnum) { (void)weaponnum; }
void bgunEquipWeapon2(s32 hand, s32 weaponnum) { (void)hand; (void)weaponnum; }
f32 currentPlayerGetGunZoomFov(void) { return 60.0f; }
s32 bgunIsUsingSecondaryFunction(void) { return 0; }
s32 weaponHasFlag(s32 weaponnum, s32 flag) { (void)weaponnum; (void)flag; return 0; }
void weaponDeleteFromChr(struct chrdata *chr, s32 hand) { (void)chr; (void)hand; }
void *weaponCreate(void *prop, void *model, s32 weaponnum) {
    (void)prop; (void)model; (void)weaponnum; return NULL;
}
void invRemoveItemByNum(s32 itemnum) { (void)itemnum; }

/* ========================================================================
 * Match / Stage stubs
 * ======================================================================== */

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

/* ========================================================================
 * Prop / Object stubs
 * ======================================================================== */

void *propAllocate(void) { return NULL; }
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
void *setupLoadModeldef(s32 filenum) { (void)filenum; return NULL; }
void *modelmgrInstantiateModelWithoutAnim(void *modeldef) { (void)modeldef; return NULL; }
void laptopDeploy(struct prop *prop) { (void)prop; }
void *psCreate(void *arg0, void *arg1, void *arg2, void *arg3) {
    (void)arg0; (void)arg1; (void)arg2; (void)arg3; return NULL;
}
void modelSetScale(void *model, f32 scale) { (void)model; (void)scale; }

/* ========================================================================
 * Math stubs
 * ======================================================================== */

void mtx4LoadRotation(void *mtx, f32 angle, void *axis) { (void)mtx; (void)angle; (void)axis; }
void func0f0685e4(struct prop *prop) { (void)prop; }
void func0f08adc8(void *arg) { (void)arg; }

/* ========================================================================
 * Menu stubs
 * ======================================================================== */

void menuPushDialog(struct menudialogdef *d) { (void)d; }
void menuPopDialog(void) {}
s32 menuIsDialogOpen(struct menudialogdef *d) { (void)d; return 0; }
void menuStop(void) {}

/* ========================================================================
 * Display / Rendering stubs
 * ======================================================================== */

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
void hudmsgCreateWithFlags(const char *text, s32 type, s32 flags) {
    (void)text; (void)type; (void)flags;
}
void hudmsgRenderBox(void) {}
Gfx *menuRenderModel(Gfx *gdl, void *menumodel, s32 modeltype) {
    (void)menumodel; (void)modeltype; return gdl;
}
void alarmStopAudio(void) {}

/* ========================================================================
 * Input stubs
 * ======================================================================== */

s32 inputKeyPressed(s32 key) { (void)key; return 0; }
void inputStartTextInput(void) {}
void inputStopTextInput(void) {}
s32 inputTextHandler(char *buf, s32 maxlen, s32 *cursor, s32 multiline) {
    (void)buf; (void)maxlen; (void)cursor; (void)multiline; return 0;
}
void inputClearLastKey(void) {}
void inputClearLastTextChar(void) {}

/* ========================================================================
 * File / ROM stubs
 * ======================================================================== */

void gamefileLoadDefaults(struct gamefile *file) {
    if (file) memset(file, 0, sizeof(struct gamefile));
}
void romdataFileFreeForSolo(void) {}

/* ========================================================================
 * Setup / Config stubs
 * ======================================================================== */

char *mpGetBodyName(u8 bodynum) { (void)bodynum; return "Default"; }
u32 mpGetNumBodies(void) { return 0; }
s32 mpGetMpheadnumByMpbodynum(s32 bodynum) { (void)bodynum; return 0; }
void netClientSettingsChanged(void) {}
void modConfigLoad(const char *fname) { (void)fname; }

/* ========================================================================
 * Console stubs (console.c is excluded from server)
 * ======================================================================== */

void conInit(void) {}
void conPrintLn(s32 showmsg, const char *text) {
    /* Print to stdout since we don't have the game console */
    if (text) printf("%s\n", text);
}

/* ========================================================================
 * LibUltra stubs (libultra.c excluded — too many video/audio deps)
 * ======================================================================== */

u32 osMemSize = 64 * 1024 * 1024;
s32 osTvType = 1;  /* NTSC */
u64 osClockRate = 62500000ULL;
s32 osResetType = 0;
s32 osViClock = 48681812;

void osCreateMesgQueue(void *mq, void *msg, s32 count) { (void)mq; (void)msg; (void)count; }
void osCreateScheduler(void *sc, void *thread, s32 mode, s32 pri) {
    (void)sc; (void)thread; (void)mode; (void)pri;
}
u32 osGetMemSize(void) { return osMemSize; }
u64 osGetTime(void) { return 0; }
u32 osGetCount(void) { return 0; }
void osRecvMesg(void *mq, void *msg, s32 flags) { (void)mq; (void)msg; (void)flags; }
void osSendMesg(void *mq, void *msg, s32 flags) { (void)mq; (void)msg; (void)flags; }
s32 osEepromLongRead(void *mq, u8 addr, u8 *buf, s32 len) {
    (void)mq; (void)addr; (void)buf; (void)len; return 0;
}
s32 osEepromLongWrite(void *mq, u8 addr, u8 *buf, s32 len) {
    (void)mq; (void)addr; (void)buf; (void)len; return 0;
}
