#ifndef _IN_GAME_PLAYER_H
#define _IN_GAME_PLAYER_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

struct prop;

/** MP team spawns: fill `out` with same-team chr positions (excl. self). Returns count. */
s32 playerCollectMpTeammatePositions(struct prop *selfprop, struct coord *out, s32 max_out);

/** MP match-start orchestrator: teleport player to validated pool slot. */
void playerApplyOrchestratedSpawnFromPool(s32 playernum, s32 pool_idx);

f32 playerChooseSpawnLocation(f32 chrradius, struct coord *dstpos, RoomNum *dstrooms, struct prop *prop, s16 *spawnpads, s32 numspawnpads);
f32 playerChooseGeneralSpawnLocation(f32 chrradius, struct coord *pos, RoomNum *rooms, struct prop *prop);
void playerStartNewLife(void);
void playerLoadDefaults(void);
bool playerSpawnAnti(struct chrdata *chr, s32 param_2);

enum player_chrbody_result {
	PLAYER_CHRBODY_OK = 0,
	PLAYER_CHRBODY_DEFERRED = 1,
	PLAYER_CHRBODY_INVALID_STATE = -1,
	PLAYER_CHRBODY_INVALID_IDENTITY = -2,
	PLAYER_CHRBODY_BODY_SOURCE_FAILED = -3,
	PLAYER_CHRBODY_HEAD_SOURCE_FAILED = -4,
	PLAYER_CHRBODY_BODY_MODEL_FAILED = -5,
	PLAYER_CHRBODY_HEAD_MODEL_FAILED = -6,
	PLAYER_CHRBODY_GUNMEM_FAILED = -7,
	PLAYER_CHRBODY_WEAPON_MODEL_FAILED = -8,
	PLAYER_CHRBODY_BODY_INSTANTIATION_FAILED = -9,
	PLAYER_CHRBODY_CHARACTER_ALLOCATION_FAILED = -10,
	PLAYER_CHRBODY_WEAPON_ATTACHMENT_FAILED = -11,
	PLAYER_CHRBODY_FIRESLOT_FAILED = -12,
};

const char *playerChrBodyResultString(enum player_chrbody_result result);
enum player_chrbody_result playerChrBodyPreflight(s32 bodynum, s32 headnum);
enum player_chrbody_result playerSpawn(void);
void playerResetBond(struct playerbond *pb, struct coord *pos);
void playersTickAllChrBodies(void);
s32 playerChooseBodyAndHead(s32 *bodynum, s32 *headnum, bool *arg2);
enum player_chrbody_result playerTickChrBody(void);
void playerRemoveChrBody(void);
void playerTickMpSwirl(void);
void playerExecutePreparedWarp(void);
bool playerStartCutscene(s16 anim_id);
bool playerStartCutsceneForPresentation(s16 anim_id);
void playerReorientForCutsceneStop(s32 tweenduration60);
void playerTickCutscene(bool arg0);
f32 playerGetCutsceneBlurFrac(void);
void playerResetCutsceneState(s32 playernum);
void playerResetAllCutsceneStates(void);
u32 playerCutsceneGeneration(void);
bool playerSyncCutsceneGeneration(u32 generation);
void playerSetCutsceneActive(s32 playernum, bool active);
bool playerInCutscene(s32 playernum);
bool playerAnyInCutscene(void);
bool playerCurrentInCutscene(void);
u8 playerCutsceneActiveMask(void);
void playerSetCutsceneActiveMask(u8 player_mask, bool active);
void playerReplaceCutsceneActiveMask(u8 player_mask);
bool playerApplyAuthoritativeCutsceneState(u8 active, u8 player_mask,
		u32 generation);
/**
 * Retire receiver-local presentation from the preceding stage after a
 * validated network stage-start has established the new match authority.
 * Returns false outside that exact client-side lifecycle boundary.
 */
bool playerApplyAuthoritativeStageStartPresentation(void);
void playerSetCutsceneInProgress(s32 playernum, bool in_progress);
bool playerCutsceneInProgress(s32 playernum);
bool playerAnyCutsceneInProgress(void);
bool playerCurrentCutsceneInProgress(void);
bool playerPresentationCutsceneInProgress(void);
void playerSetCutsceneSkipRequested(s32 playernum, bool skiprequested);
bool playerRequestCutsceneSkip(s32 playernum, bool skipautocutgroup);
bool playerCutsceneSkipRequested(s32 playernum);
bool playerAnyCutsceneSkipRequested(void);
bool playerCurrentCutsceneSkipRequested(void);
void playerSetCutsceneAnimNum(s32 playernum, s16 animnum);
s16 playerCutsceneAnimNum(s32 playernum);
s16 playerCurrentCutsceneAnimNum(void);
void playerSetCutsceneCurAnimFrame60(s32 playernum, s32 frame60);
s32 playerCutsceneCurAnimFrame60(s32 playernum);
s32 playerCurrentCutsceneCurAnimFrame60(void);
void playerSetCutsceneCurTotalFrame60f(s32 playernum, f32 total60f);
f32 playerCutsceneCurTotalFrame60f(s32 playernum);
f32 playerCurrentCutsceneCurTotalFrame60f(void);
void playerClampGunZoomFovY(s32 playernum);
void playerSetZoomFovY(f32 fovy, f32 timemax);
f32 playerGetZoomFovY(void);
void playerTweenFovY(f32 targetfovy);
f32 playerGetTeleportFovY(void);
void playerUpdateZoom(void);
void playerStopAudioForPause(void);
void playerTickPauseMenu(void);
void playerPause(s32 root);
void playerUnpause(void);
Gfx *player0f0baf84(Gfx *gdl);
Gfx *playerDrawFade(Gfx *gdl, u32 r, u32 g, u32 b, f32 frac);
Gfx *playerDrawStoredFade(Gfx *gdl);
void playerUpdateColourScreenProperties(void);
void playerTickChrFade(void);
void playerDisplayHealth(void);
void playerTickDamageAndHealth(void);
void playerDisplayDamage(void);
Gfx *playerRenderHealthBar(Gfx *gdl);
void playerSurroundWithExplosions(s32 arg0);
void playerTickExplode(void);
s16 playerGetFbWidth(void);
s16 playerGetFbHeight(void);
bool playerHasSharedViewport(void);
s16 playerGetViewportWidth(void);
s16 playerGetViewportLeft(void);
s16 playerGetViewportHeight(void);
s16 playerGetViewportTop(void);
f32 player0f0bd358(void);
void playerUpdateShake(void);
void playerTickTeleport(f32 *arg0);
void playerConfigureVi(void);
void playerTick(bool arg0);
void playerAllocateMatrices(struct coord *cam_pos, struct coord *cam_look, struct coord *cam_up);
Gfx *playerUpdateShootRot(Gfx *gdl);
void playerDisplayShield(void);
Gfx *playerRenderShield(Gfx *gdl);
Gfx *playerRenderHud(Gfx *gdl);
void playerDie(bool force);
void playerDieByShooter(u32 shooter, bool force);
bool playerRestoreDeadStateFromSnapshot(void);
void playerCheckIfShotInBack(s32 attackerplayernum, f32 x, f32 z);
f32 playerGetHealthBarHeightFrac(void);
void player0f0c1840(struct coord *pos, struct coord *up, struct coord *look, struct coord *pos2, RoomNum *rooms);
void player0f0c1ba4(struct coord *pos, struct coord *up, struct coord *look, struct coord *memcampos, s32 memcamroom);
void player0f0c1bd8(struct coord *pos, struct coord *up, struct coord *look);
void playersClearMemCamRoom(void);
void playerSetPerimEnabled(struct prop *prop, bool enable);
bool playerUpdateGeometry(struct prop *prop, u8 **start, u8 **end);
void playerUpdatePerimInfo(void);
void playerGetBbox(struct prop *prop, f32 *radius, f32 *ymax, f32 *ymin);
f32 playerGetHealthFrac(void);
f32 playerGetShieldFrac(void);
void playerSetShieldFrac(f32 frac);
s32 playerGetMissionTime(void);
s32 playerTickBeams(struct prop *prop);
s32 playerTickThirdPerson(struct prop *prop);
void playerChooseThirdPersonAnimation(struct chrdata *chr, s32 crouchpos, f32 speedsideways, f32 speedforwards, f32 speedtheta, f32 *angleoffset, struct attackanimconfig **animcfg);
Gfx *playerRender(struct prop *prop, Gfx *gdl, bool xlupass);
Gfx *playerLoadMatrix(Gfx *gdl);
void player0f0c3320(Mtxf *matrices, s32 count);
bool playerSetTickMode(s32 tickmode);
void playerBeginGeFadeIn(void);
void playersBeginMpSwirl(void);
void player0f0b9a20(void);
void playerEndCutscene(void);
void playerPrepareWarpType1(s16 pad_id);
void playerPrepareWarpType2(struct warpparams *cmd, bool hasdir, s32 arg2);
void playerPrepareWarpType3(f32 posangle, f32 rotangle, f32 range, f32 height1, f32 height2, s32 padnum);
bool playerStartCutscene2(s16 anim_id);
void playerSetFadeColour(s32 r, s32 g, s32 b, f32 a);
void playerAdjustFade(f32 maxfadetime, s32 r, s32 g, s32 b, f32 frac);
void playerSetFadeFrac(f32 maxfadetime, f32 frac);
bool playerIsFadeComplete(void);
void playerStartChrFade(f32 duration60, f32 targetfrac);
void playerSetHiResEnabled(bool enable);
void playerAutoWalk(s16 aimpad, u8 walkspeed, u8 turnspeed, u8 lookup, u8 dist);
void playerLaunchSlayerRocket(struct weaponobj *rocket);
void playerSetGlobalDrawWorldOffset(s32 room);
void playerSetGlobalDrawCameraOffset(void);
bool playerIsHealthVisible(void);
void playerSetCameraMode(s32 mode);
void playerSetCamPropertiesWithRoom(struct coord *pos, struct coord *up, struct coord *look, s32 room);
void playerSetCamPropertiesWithoutRoom(struct coord *pos, struct coord *up, struct coord *look, s32 room);
void playerSetCamProperties(struct coord *pos, struct coord *up, struct coord *look, s32 room);
void playerClearMemCamRoom(void);

/** Dev (PD_DEV_BUILD): F7 toggles g_PlayerInvincible; HUD banner when active. */
void playerToggleDevInvincibility(void);
s32 playerDevInvincibilityHudActive(void);

struct sndstate *playerSndStart(s32 arg0, s16 sound, struct sndstate **handle, s32 playernum, f32 pitch, s32 fxbus, s32 fxmix);
#if MAX_PLAYERS > 4
s32 playerGetCount(void);
s32 playerGetLocalCount(void);
f32 playerGetDefaultFovY(s32 playernum);

f32 playerGetZoomFovMult(s32 playernum);

#endif

#endif
