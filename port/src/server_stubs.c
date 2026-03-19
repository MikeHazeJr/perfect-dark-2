/**
 * server_stubs.c — Stub implementations of game functions for the dedicated server.
 *
 * The networking code (net.c, netmsg.c) references many game functions.
 * The dedicated server doesn't run game logic, so these are empty stubs
 * that satisfy the linker. As we migrate the networking code to use
 * net_interface.h callbacks, these stubs will be removed.
 *
 * This file is ONLY compiled into pd-server, not into the game client.
 */

#include <PR/ultratypes.h>
#include <PR/gbi.h>
#include <string.h>
#include "system.h"

/* Types we need for function signatures */
struct prop;
struct chrdata;
struct coord { f32 x, y, z; };
struct model;
struct modeldef;
struct textureconfig;
struct gamefile;
struct menuitem;
struct menudialogdef;
union handlerdata;
typedef s32 MenuItemHandlerResult;
typedef s32 bool32;

/* ========================================================================
 * Player / Character stubs
 * ======================================================================== */

void playerDie(s32 arg) { (void)arg; }
void playerDieByShooter(s32 shooter, s32 arg) { (void)shooter; (void)arg; }
void playerStartNewLife(void) {}
void setCurrentPlayerNum(s32 num) { (void)num; }
s32 playermgrGetPlayerNumByProp(struct prop *prop) { (void)prop; return 0; }
void chrSetPos(void *chr, void *pos, void *rooms, f32 angle, s32 onground) {
    (void)chr; (void)pos; (void)rooms; (void)angle; (void)onground;
}

/* ========================================================================
 * Weapon stubs
 * ======================================================================== */

void bgunEquipWeapon(s32 weaponnum) { (void)weaponnum; }
f32 currentPlayerGetGunZoomFov(void) { return 60.0f; }

/* ========================================================================
 * Match / Stage stubs
 * ======================================================================== */

void mpStartMatch(void) {
    sysLogPrintf(LOG_NOTE, "STUB: mpStartMatch called");
}
void mpSetPaused(s32 mode) { (void)mode; }
void mainChangeToStage(s32 stagenum) {
    sysLogPrintf(LOG_NOTE, "STUB: mainChangeToStage(0x%02x)", stagenum);
}
void mainEndStage(void) {
    sysLogPrintf(LOG_NOTE, "STUB: mainEndStage called");
}
void titleSetNextStage(s32 stagenum) { (void)stagenum; }
void titleSetNextMode(s32 mode) { (void)mode; }
void lvSetDifficulty(s32 diff) { (void)diff; }
void setNumPlayers(s32 count) { (void)count; }
void romdataFileFreeForSolo(void) {}
void objectivesDisableChecking(void) {}

/* ========================================================================
 * Menu stubs
 * ======================================================================== */

void menuPushDialog(struct menudialogdef *d) { (void)d; }
void menuPopDialog(void) {}
s32 menuIsDialogOpen(struct menudialogdef *d) { (void)d; return 0; }
void menuStop(void) {}
void menuCloseDialog(void) {}

MenuItemHandlerResult menuhandlerMainMenuCombatSimulator(s32 op, struct menuitem *item, union handlerdata *data) {
    (void)op; (void)item; (void)data; return 0;
}
MenuItemHandlerResult menuhandlerMpAdvancedSetup(s32 op, struct menuitem *item, union handlerdata *data) {
    (void)op; (void)item; (void)data; return 0;
}

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
 * Display / Audio stubs
 * ======================================================================== */

void viBlack(s32 black) { (void)black; }
s32 viGetWidth(void) { return 800; }
s32 viGetHeight(void) { return 500; }
char *langGet(s32 textid) { (void)textid; return ""; }
char *mpGetBodyName(u8 bodynum) { (void)bodynum; return "Default"; }
u32 mpGetNumBodies(void) { return 0; }
s32 mpGetMpheadnumByMpbodynum(s32 bodynum) { (void)bodynum; return 0; }

/* ========================================================================
 * Setup / Config stubs
 * ======================================================================== */

void mpsetupCopyAllFromPak(void) {}
void mpsetupLoadCurrentFile(void) {}
void netClientSettingsChanged(void) {}
void netQueryRecentServers(void) {}

/* ========================================================================
 * Model / Rendering stubs (server never renders game content)
 * ======================================================================== */

Gfx *menuRenderModel(Gfx *gdl, void *menumodel, s32 modeltype) {
    (void)menumodel; (void)modeltype; return gdl;
}

/* ========================================================================
 * HUD / Chat stubs
 * ======================================================================== */

void hudmsgCreate(const char *text, s32 type) { (void)text; (void)type; }
void hudmsgCreateWithFlags(const char *text, s32 type, s32 flags) {
    (void)text; (void)type; (void)flags;
}

/* ========================================================================
 * Game file stubs
 * ======================================================================== */

void gamefileLoadDefaults(struct gamefile *file) {
    if (file) memset(file, 0, 256); /* Clear enough for the struct */
}

/* ========================================================================
 * Misc stubs
 * ======================================================================== */

const char *fsGetModDir(void) { return ""; }
void conPrintLn(s32 showmsg, const char *text) { (void)showmsg; (void)text; }
