#include <ultra64.h>
#include "constants.h"
#include "system.h"
#include "game/activemenu.h"
#include "game/bondbike.h"
#include "game/bondgrab.h"
#include "game/bondmove.h"
#include "game/bondwalk.h"
#include "game/cheats.h"
#include "game/chraction.h"
#include "game/footstep.h"
#include "game/game_006900.h"
#include "game/chr.h"
#include "game/prop.h"
#include "game/atan2f.h"
#include "game/quaternion.h"
#include "game/bondgun.h"
#include "game/game_0b0fd0.h"
#include "game/tex.h"
#include "game/camera.h"
#include "game/inv.h"
#include "game/player.h"
#include "game/bondcutscene.h"
#include "game/bondhead.h"
#include "game/playermgr.h"
#include "game/bg.h"
#include "game/lv.h"
#include "game/mplayer/ingame.h"
#include "game/mplayer/mplayer.h"
#include "game/options.h"
#include "game/propobj.h"
#include "bss.h"
#include "lib/lib_17ce0.h"
#include "lib/vi.h"
#include "lib/collision.h"
#include "lib/joy.h"
#include "lib/snd.h"
#include "lib/rng.h"
#include "lib/mtx.h"
#include "lib/anim.h"
#include "data.h"
#include "types.h"
#include <math.h>
#include "input.h"
#include "actionmap.h"
#include "video.h"
#include "system.h"
#include "utils.h"
#include "net/net.h"
#include "net/netmsg.h"
#include "weapon_graph_runtime.h"

#define BUTTON_JUMP CONT_4000

/* Tap/hold dual-action threshold in milliseconds. Press releases before this
 * count as TAP (fires the press action via actionWasTap); presses held past
 * this count as HOLD (fires the hold action and consumes the gesture so the
 * tap path stays silent on release). Same primitive as ACTION_USE per
 * Phase 2 fix #1 + menu-input-interaction-grammar.md Rule 4. */
#define BOND_TAP_HOLD_THRESH_MS 250

/* Per-player ACTION_USE double-tap detection. Records the 60Hz tick at
 * which the LAST tap-release was registered; on the next tap release, if
 * the gap is within BOND_DOUBLE_TAP_TICKS the second tap consumes the pair
 * instead of firing another reload. Zero means no prior tap recorded. We use
 * g_Vars.lvframe60 instead of SDL_GetTicks so this stays platform-neutral
 * with the rest of bondmove.c. */
static u32 s_BondLastUseTapReleaseFrame[MAX_PLAYERS] = {0};
#define BOND_DOUBLE_TAP_TICKS 15 /* ~250 ms at 60 Hz */

/* Per-player previous airborne state. Used by the crouch-jump mid-air
 * window detector in bondwalk: the moment ACTION_CROUCH is pressed while
 * airborne (going up from a fresh jump), latch a small lift on the
 * effective foot Y so the player can clear a slightly higher surface.
 * States:
 *   0 = inactive (no jump in progress)
 *   1 = jump pressed, still grounded (boost not yet fired)
 *   2 = airborne (jump in flight, boost may or may not have fired)
 * Reset to 0 on landing (Mike directive 2026-05-17: crouch posture set
 * during a crouch-jump is restored to STAND on landing unless the player
 * is still holding ACTION_CROUCH at the landing frame). */
s32 g_BondCrouchJumpActive[MAX_PLAYERS] = {0};

/* Tracks bondonground from the previous tick so we can detect the
 * airborne -> grounded transition (landing). Used by the crouch-jump
 * landing-stance restore in bmoveProcessInput. */
static s32 s_BondPrevOnGround[MAX_PLAYERS] = {1, 1, 1, 1, 1, 1, 1, 1};

/* B-246 round-9: F2 test-fire scheduler. Mike's remote-testing setup
 * (phone -> Windows RDP) does not let him use most game inputs, so F2
 * schedules a single one-frame Z_TRIG pulse for player 0 a configurable
 * number of ticks after the keypress. The pulse synthesises both
 * c1buttons and c1buttonsthisframe so the fire chain sees a normal
 * trigger rising edge. Multiple F2 presses queue independently up to
 * F2_TEST_FIRE_QUEUE_SIZE.
 *
 * The actual injection happens in the c1buttons assembly inside the
 * per-player input mux below; the schedule entry point is exported as
 * bmoveScheduleTestFire so the SDL key handler in pdgui_backend.cpp
 * can fill it without depending on game-side internals. */
#define F2_TEST_FIRE_QUEUE_SIZE 16
static s32 s_F2TestFireTargetFrame[F2_TEST_FIRE_QUEUE_SIZE];
static s32 s_F2TestFireQueueCount = 0;

void bmoveScheduleTestFire(s32 delay_ticks_60hz)
{
	s32 target;

	if (s_F2TestFireQueueCount >= F2_TEST_FIRE_QUEUE_SIZE) {
		return;
	}

	target = (s32)g_Vars.lvframenum + delay_ticks_60hz;
	s_F2TestFireTargetFrame[s_F2TestFireQueueCount] = target;
	s_F2TestFireQueueCount++;

	sysLogPrintf(LOG_NOTE,
		"LOG.WPN.DIAG: F2-test-fire scheduled at +1s (target_frame=%d cur_frame=%d queue_depth=%d)",
		target, (s32)g_Vars.lvframenum, s_F2TestFireQueueCount);
}

static bool bmoveTestFireDueThisFrame(void)
{
	s32 cur = (s32)g_Vars.lvframenum;
	s32 i;
	bool fire = false;

	for (i = 0; i < s_F2TestFireQueueCount; ) {
		if (s_F2TestFireTargetFrame[i] <= cur) {
			s32 j;
			fire = true;
			sysLogPrintf(LOG_NOTE,
				"LOG.WPN.DIAG: F2-test-fire triggered (1s elapsed) frame=%d",
				cur);
			for (j = i; j < s_F2TestFireQueueCount - 1; j++) {
				s_F2TestFireTargetFrame[j] = s_F2TestFireTargetFrame[j + 1];
			}
			s_F2TestFireQueueCount--;
		} else {
			i++;
		}
	}

	return fire;
}

/* Use hold duration: propGetActionUseHoldThresholdMs() — when an interact prompt is active,
 * equals propInteractPromptHoldThresholdMs() (effective ms + per-target extras in prop.c);
 * otherwise actionmapGetEffectiveHoldMs(ACTION_USE). See actionmap.h / pdgui-hold-ring.md. */

static void bgunProcessQuickDetonate(struct movedata *data, u32 c1buttons, u32 c1buttonsthisframe, u32 buttons1, u32 buttons2) {
	if ((((c1buttons & (buttons1)) && (c1buttonsthisframe & (buttons2)))
			|| ((c1buttons & (buttons2)) && (c1buttonsthisframe & (buttons1))))
			&& bgunGetWeaponNum(HAND_RIGHT) == WEAPON_REMOTEMINE) {
		data->detonating = true;
		data->weaponbackoffset = 0;
		data->weaponforwardoffset = 0;
		data->btapcount = 0;
		// prevent the previous slotnum
		// from causing Jo to switch weapons
		g_AmMenus[g_AmIndex].slotnum = 4;
		amClose();
		g_Vars.currentplayer->invdowntime = -2;
		g_Vars.currentplayer->usedowntime = -2;
	}
}

static s32 bmoveGetPressedWeaponSlotIndex(s32 playeridx)
{
	if (actionPressed(playeridx, ACTION_WEAPON_1)) {
		return 0;
	}
	if (actionPressed(playeridx, ACTION_WEAPON_2)) {
		return 1;
	}
	if (actionPressed(playeridx, ACTION_WEAPON_3)) {
		return 2;
	}
	if (actionPressed(playeridx, ACTION_WEAPON_4)) {
		return 3;
	}
	if (actionPressed(playeridx, ACTION_WEAPON_5)) {
		return 4;
	}
	if (actionPressed(playeridx, ACTION_WEAPON_6)) {
		return 5;
	}

	return -1;
}

static bool bmoveSelectInventoryWeaponIndex(s32 invindex)
{
	s32 weaponnum;
	s32 state;

	if (g_Vars.tickmode == TICKMODE_CUTSCENE || g_Vars.lvframenum < 10) {
		return false;
	}

	if (invindex < 0 || invindex >= invGetCount()) {
		return false;
	}

	weaponnum = invGetWeaponNumByIndex(invindex);

	if (!weaponnum) {
		return false;
	}

	state = currentPlayerGetDeviceState(weaponnum);

	if (state != DEVICESTATE_UNEQUIPPED) {
		currentPlayerSetDeviceActive(weaponnum, state == DEVICESTATE_INACTIVE);
		return true;
	}

	invSetCurrentIndex(invindex);

	if (invHasDoubleWeaponIncAllGuns(weaponnum, weaponnum)) {
		if (bgunGetWeaponNum(HAND_RIGHT) != weaponnum) {
			bgunEquipWeapon2(HAND_RIGHT, weaponnum);
		}

		if (bgunGetWeaponNum(HAND_LEFT) != weaponnum) {
			bgunEquipWeapon2(HAND_LEFT, weaponnum);
		}
	} else {
		if (bgunGetWeaponNum(HAND_RIGHT) != weaponnum) {
			bgunEquipWeapon2(HAND_RIGHT, weaponnum);
		}

		if (weaponnum == WEAPON_REMOTEMINE) {
			bgunEquipWeapon2(HAND_LEFT, weaponnum);
		} else if (bgunGetWeaponNum(HAND_LEFT) != WEAPON_NONE) {
			bgunEquipWeapon2(HAND_LEFT, WEAPON_NONE);
		}
	}

	return true;
}

static bool bmoveHandleDirectWeaponSelect(s32 playeridx)
{
	return bmoveSelectInventoryWeaponIndex(bmoveGetPressedWeaponSlotIndex(playeridx));
}

static void bgunProcessInputAltButton(struct movedata *data, s8 contpad, s32 i)
{
	/* M0.2: collapsed sub-frame to per-frame — was joyGetButtonsOnSample(i, contpad, 0xffffffff) */
	s32 playerIdx = (s32)contpad;
	s32 altHeld = actionHeld(playerIdx, ACTION_FIRE_MODE);       /* BUTTON_ALTMODE = L_TRIG */
	s32 cancelHeld = actionHeld(playerIdx, ACTION_CANCEL_USE);   /* BUTTON_CANCEL_USE = B_BUTTON */
	s32 acceptHeld = actionHeld(playerIdx, ACTION_USE);          /* BUTTON_ACCEPT_USE = A_BUTTON */
	s32 zHeld = actionHeld(playerIdx, ACTION_FIRE_PRIMARY);      /* Z_TRIG */
	if (altHeld) {
		if (g_Vars.currentplayer->altdowntime >= -1) {
			if (zHeld
					&& g_Vars.currentplayer->altdowntime >= 0
					&& bgunConsiderToggleGunFunction(g_Vars.currentplayer->altdowntime, true, false, true) != USETIMER_CONTINUE) {
				g_Vars.currentplayer->altdowntime = -3;
			}
			if (g_Vars.currentplayer->altdowntime != -4) {
				if (g_Vars.currentplayer->altdowntime <= 0) {
					g_Vars.currentplayer->altdowntime++;
				}
			}
		} else  {
			if (g_Vars.currentplayer->altdowntime == -2) {
				bgunConsiderToggleGunFunction(g_Vars.currentplayer->altdowntime, false, false, true);
				g_Vars.currentplayer->altdowntime = -4;
			}
		}
	} else if (cancelHeld || acceptHeld) {
		if (g_Vars.currentplayer->altdowntime >= -1) {
			if (zHeld
					&& g_Vars.currentplayer->altdowntime >= 0
					&& bgunConsiderToggleGunFunction(g_Vars.currentplayer->altdowntime, true, false, true) != USETIMER_CONTINUE) {
				g_Vars.currentplayer->altdowntime = -3;
			}
		}
	} else {
		// Released L
		if (g_Vars.currentplayer->altdowntime != 0) {
			const bool trigpressed = (g_Vars.currentplayer->altdowntime == -3);
			s32 result = bgunConsiderToggleGunFunction(g_Vars.currentplayer->altdowntime, trigpressed, false, true);
			if (result == USETIMER_STOP) {
				g_Vars.currentplayer->altdowntime = -1;
			} else if (result == USETIMER_REPEAT) {
				g_Vars.currentplayer->altdowntime = -2;
			}
		}
		g_Vars.currentplayer->altdowntime = 0;
		bgun0f0a8c50();
	}
}

static inline void bmoveProcessRemoteInput(const bool allowc1buttons)
{
	struct player *pl = g_Vars.currentplayer;
	if (!pl->client) {
		return;
	}

	struct netplayermove *inmove = &pl->client->inmove[0];
	struct netplayermove *inmoveprev = &pl->client->inmove[1];
	s32 moveticks = inmove->tick - inmoveprev->tick;
	if (moveticks > g_NetInterpTicks) {
		moveticks = g_NetInterpTicks;
	}

	const bool handled = (pl->client->inmovetick >= inmove->tick);

	if (!inmove->tick) {
		// no input
		inmove->pos = inmoveprev->pos = pl->prop->pos;
		inmove->angles[0] = inmoveprev->angles[0] = pl->vv_theta;
		inmove->angles[1] = inmoveprev->angles[1] = pl->vv_verta;
		inmove->ucmd = 0;
	}

	if (inmove->ucmd & UCMD_DUCK) {
		pl->crouchpos = CROUCHPOS_DUCK;
	} else if (inmove->ucmd & UCMD_SQUAT) {
		pl->crouchpos = CROUCHPOS_SQUAT;
	} else {
		pl->crouchpos = CROUCHPOS_STAND;
	}

	if (!handled && (inmove->ucmd & UCMD_JUMP)) {
		if (!pl->jumpconsumed) {
			pl->wantsjump = true;
		}
	} else {
		/* Button released — allow next press to trigger a jump */
		pl->jumpconsumed = false;
	}

	pl->bondactivateorreload = 0;

	pl->eyesshut = (inmove->ucmd & UCMD_EYESSHUT) != 0;

	// speedtheta is reset here because this is the network input apply path.
	// speedtheta is NOT transmitted over the network — it is computed locally each frame
	// by bwalkUpdateLateral() via speedthetacontrol (bondmove.c ~line 2304).
	// Zeroing it here is correct: it clears any stale carry-over before the frame recomputes it.
	pl->speedtheta = 0.f;
	pl->speedverta = 0.f;
	pl->crouchoffset = inmove->crouchofs;

	pl->oldcrosspos[0] = pl->crosspos[0];
	pl->oldcrosspos[1] = pl->crosspos[1];
	pl->crosspos[0] = inmove->crosspos[0];
	pl->crosspos[1] = inmove->crosspos[1];

	// denormalize crosspos x
	pl->crosspos[0] -= (f32)(SCREEN_WIDTH_LO / 2);
	pl->crosspos[0] = (f32)(SCREEN_WIDTH_LO / 2) + pl->crosspos[0] / pl->aspect * SCREEN_ASPECT;

	pl->crosspos2[0] = pl->crosspos[0];
	pl->crosspos2[1] = pl->crosspos[1];

	pl->insightaimmode = (inmove->ucmd & UCMD_AIMMODE) != 0;
	if (pl->insightaimmode) {
		pl->gunzoomfovs[0] = pl->gunzoomfovs[1] = pl->gunzoomfovs[2] = inmove->zoomfov;
	}

	f32 zoomfov = 0.f;
	if (pl->insightaimmode) {
		zoomfov = currentPlayerGetGunZoomFov();
	}
	if (bgunGetWeaponNum(HAND_RIGHT) == WEAPON_AR34 && pl->hands[HAND_RIGHT].gset.weaponfunc == FUNC_SECONDARY) {
		zoomfov = currentPlayerGetGunZoomFov();
	}
	if (zoomfov <= 0.f) {
		zoomfov = pl->client->settings.fovy;
	}
	playerTweenFovY(zoomfov);
	playerUpdateZoom();

	for (s32 h = 0; h < 2; ++h) {
		pl->hands[h].crosspos[0] = pl->crosspos[0];
		pl->hands[h].crosspos[1] = pl->crosspos[1];
	}

	if (inmove->ucmd & UCMD_SELECT) {
		pl->gunctrl.dualwielding = (inmove->ucmd & UCMD_SELECT_DUAL) != 0;
		if (inmove->weaponnum >= 0) {
			bgunEquipWeapon(inmove->weaponnum);
		}
	}

	if ((inmove->ucmd & UCMD_SECONDARY) && !bgunIsUsingSecondaryFunction()) {
		bgunConsiderToggleGunFunction(0, false, false, true);
	} else if (!(inmove->ucmd & UCMD_SECONDARY) && bgunIsUsingSecondaryFunction()) {
		// do not switch back to primary if in the process of throwing laptop
		if (!(bgunGetWeaponNum(HAND_RIGHT) == WEAPON_LAPTOPGUN && pl->hands[HAND_RIGHT].state == HANDSTATE_ATTACK)) {
			bgunConsiderToggleGunFunction(0, false, false, true);
		}
	}

	const bool fireguns = (inmove->ucmd & UCMD_FIRE) && !pl->waitforzrelease && allowc1buttons;

	bgunTickGameplay(fireguns);

	if (fireguns && g_NetMode == NETMODE_SERVER) {
		netmsgSvcPlayerStatsWrite(&g_NetMsgRel, pl->client);
	}

	if (!handled && (inmove->ucmd & UCMD_RELOAD)) {
		pl->bondactivateorreload |= JO_ACTION_RELOAD;
	}

	if (g_NetMode == NETMODE_SERVER) {
		if (!handled && (inmove->ucmd & UCMD_ACTIVATE)) {
			pl->bondactivateorreload |= JO_ACTION_ACTIVATE;
		}

		if (g_Vars.bondvisible && (bgunIsFiring(HAND_RIGHT) || bgunIsFiring(HAND_LEFT))) {
			f32 noiseradius = 0.f;
			if (bgunIsFiring(HAND_RIGHT) && bgunGetNoiseRadius(HAND_RIGHT) > noiseradius) {
				noiseradius = bgunGetNoiseRadius(HAND_RIGHT);
			}
			if (bgunIsFiring(HAND_LEFT) && bgunGetNoiseRadius(HAND_LEFT) > noiseradius) {
				noiseradius = bgunGetNoiseRadius(HAND_LEFT);
			}
			chrsCheckForNoise(noiseradius);
		}
	}

	bgunSetSightVisible(GUNSIGHTREASON_NOTAIMING, pl->insightaimmode);

	const bool forcepos = !inmoveprev->tick || !moveticks || (inmove->ucmd & UCMD_FL_FORCEANGLE);

	// lerp towards the current speeds and angles
	const f32 dt = (forcepos ? 1.f : (1.f / (f32)moveticks));
	f32 t = (forcepos ? 1.f : ((f32)pl->client->lerpticks / (f32)moveticks));
	if (t > 1.f) {
		t = 1.f;
	}
	pl->speedgo = pl->speedforwards = lerpf(inmoveprev->movespeed[0], inmove->movespeed[0], t);
	pl->speedstrafe = pl->speedsideways = lerpf(inmoveprev->movespeed[1], inmove->movespeed[1], t);
	pl->vv_theta = lerpanglef(pl->vv_theta, inmove->angles[0], dt);
	pl->vv_verta = lerpanglef(pl->vv_verta, inmove->angles[1], dt);

	if (pl->bondmovemode == MOVEMODE_GRAB) {
		bgrabUpdateSpeedTheta();
	} else if (pl->bondmovemode == MOVEMODE_WALK) {
		if (bmoveGetCrouchPos() != CROUCHPOS_STAND) {
			pl->speedmaxtime60 = 0;
		}
		bwalkSetSwayTargetf(inmove->leanofs);
	} else if (pl->bondmovemode == MOVEMODE_BIKE) {
		// Bike mode: no input-side state to apply here yet.
		// speedtheta for the hoverbike comes from bwalkUpdateLateral() → speedthetacontrol.
		// If/when bike mode is extended, add any per-input-frame bike state updates here
		// analogous to bwalkSetSwayTargetf() in walk mode.
	}

	if (inmove->movespeed[0] > 0.95f) {
		pl->speedmaxtime60 += g_Vars.lvupdate60;
	} else {
		pl->speedmaxtime60 = 0;
	}
}


void bmoveSetControlDef(u32 controldef)
{
	g_Vars.currentplayer->controldef = controldef;
}

void bmoveSetAutoMoveCentreEnabled(bool enabled)
{
	g_Vars.currentplayer->automovecentreenabled = enabled;
}

bool bmoveIsAutoMoveCentreEnabled(void)
{
	return g_Vars.currentplayer->automovecentreenabled;
}

void bmoveSetAutoAimY(bool enabled)
{
	g_Vars.currentplayer->autoyaimenabled = enabled;
}

bool bmoveIsAutoAimYEnabled(void)
{
	if (!g_Vars.normmplayerisrunning) {
		return g_Vars.currentplayer->autoyaimenabled;
	}

	if (g_MpSetup.options & MPOPTION_NOAUTOAIM) {
		return false;
	}

	return optionsGetAutoAim(g_Vars.currentplayerstats->mpindex);
}

bool bmoveIsAutoAimYEnabledForCurrentWeapon(void)
{
	const weapon_graph_held_function_t *graph =
		weaponGraphRuntimeGetHeldFunctionForGameplay(
			g_Vars.currentplayer->hands[HAND_RIGHT].gset.weaponnum,
			g_Vars.currentplayer->hands[HAND_RIGHT].gset.weaponfunc);
	struct weaponfunc *func = currentPlayerGetWeaponFunction(0);

	if (graph) {
		if (graph->flags & FUNCFLAG_NOAUTOAIM) {
			return false;
		}

		if ((graph->function_type_id & 0xff) == INVENTORYFUNCTYPE_MELEE) {
			return true;
		}
	}

	if (func) {
		if (func->flags & FUNCFLAG_NOAUTOAIM) {
			return false;
		}

		if ((func->type & 0xff) == INVENTORYFUNCTYPE_MELEE) {
			return true;
		}
	}

	return bmoveIsAutoAimYEnabled();
}

bool bmoveIsInSightAimMode(void)
{
	return g_Vars.currentplayer->insightaimmode;
}

void bmoveUpdateAutoAimYProp(struct prop *prop, f32 autoaimy)
{
	if (g_Vars.currentplayer->autoyaimtime60 >= 0) {
		g_Vars.currentplayer->autoyaimtime60 -= g_Vars.lvupdate60;
	}

	if (prop != g_Vars.currentplayer->autoyaimprop) {
		if (g_Vars.currentplayer->autoyaimtime60 < 0) {
			g_Vars.currentplayer->autoyaimtime60 = TICKS(30);
			g_Vars.currentplayer->autoyaimprop = prop;
		} else {
			return;
		}
	}

	g_Vars.currentplayer->autoaimy = autoaimy;
}

void bmoveSetAutoAimX(bool enabled)
{
	g_Vars.currentplayer->autoxaimenabled = enabled;
}

bool bmoveIsAutoAimXEnabled(void)
{
	if (!g_Vars.normmplayerisrunning) {
		return g_Vars.currentplayer->autoxaimenabled;
	}

	if (g_MpSetup.options & MPOPTION_NOAUTOAIM) {
		return false;
	}

	return optionsGetAutoAim(g_Vars.currentplayerstats->mpindex);
}

bool bmoveIsAutoAimXEnabledForCurrentWeapon(void)
{
	const weapon_graph_held_function_t *graph =
		weaponGraphRuntimeGetHeldFunctionForGameplay(
			g_Vars.currentplayer->hands[HAND_RIGHT].gset.weaponnum,
			g_Vars.currentplayer->hands[HAND_RIGHT].gset.weaponfunc);
	struct weaponfunc *func = currentPlayerGetWeaponFunction(0);

	if (graph) {
		if (graph->flags & FUNCFLAG_NOAUTOAIM) {
			return false;
		}

		if ((graph->function_type_id & 0xff) == INVENTORYFUNCTYPE_MELEE) {
			return true;
		}
	}

	if (func) {
		if (func->flags & FUNCFLAG_NOAUTOAIM) {
			return false;
		}

		if ((func->type & 0xff) == INVENTORYFUNCTYPE_MELEE) {
			return true;
		}
	}

	return bmoveIsAutoAimXEnabled();
}

void bmoveUpdateAutoAimXProp(struct prop *prop, f32 autoaimx)
{
	if (g_Vars.currentplayer->autoxaimtime60 >= 0) {
		g_Vars.currentplayer->autoxaimtime60 -= g_Vars.lvupdate60;
	}

	if (prop != g_Vars.currentplayer->autoxaimprop) {
		if (g_Vars.currentplayer->autoxaimtime60 < 0) {
			g_Vars.currentplayer->autoxaimtime60 = TICKS(30);
			g_Vars.currentplayer->autoxaimprop = prop;
		} else {
			return;
		}
	}

	g_Vars.currentplayer->autoaimx = autoaimx;
}

struct prop *bmoveGetHoverbike(void)
{
	if (g_Vars.currentplayer->bondmovemode == MOVEMODE_BIKE) {
		return g_Vars.currentplayer->hoverbike;
	}

	return NULL;
}

struct prop *bmoveGetGrabbedProp(void)
{
	if (g_Vars.currentplayer->bondmovemode == MOVEMODE_GRAB) {
		return g_Vars.currentplayer->grabbedprop;
	}

	return NULL;
}

void bmoveGrabProp(struct prop *prop)
{
	struct defaultobj *obj = prop->obj;

	if ((obj->hidden & OBJHFLAG_MOUNTED) == 0 && (obj->hidden & OBJHFLAG_GRABBED) == 0) {
		g_Vars.currentplayer->grabbedprop = prop;
		bgrabInit();
	}
}

void bmoveSetMode(u32 movemode)
{
	if (g_Vars.currentplayer->bondmovemode == MOVEMODE_GRAB) {
		bgrabExit();
	} else if (g_Vars.currentplayer->bondmovemode == MOVEMODE_BIKE) {
		bbikeExit();
	}

	if (movemode == MOVEMODE_BIKE) {
		bbikeInit();
	} else if (movemode == MOVEMODE_GRAB) {
		bgrabInit();
	} else if (movemode == MOVEMODE_CUTSCENE) {
		bcutsceneInit();
	} else if (movemode == MOVEMODE_WALK) {
		bwalkInit();
	}
}

void bmoveSetModeForAllPlayers(u32 movemode)
{
	u32 prevplayernum = g_Vars.currentplayernum;
	s32 i;

	for (i = 0; i < PLAYERCOUNT(); i++) {
		setCurrentPlayerNum(i);
		bmoveSetMode(movemode);
	}

	setCurrentPlayerNum(prevplayernum);
}

void bmoveHandleActivate(void)
{
	if (g_Vars.currentplayer->bondmovemode == MOVEMODE_BIKE) {
		bbikeHandleActivate();
	} else if (g_Vars.currentplayer->bondmovemode == MOVEMODE_GRAB) {
		bgrabHandleActivate();
	} else if (g_Vars.currentplayer->bondmovemode == MOVEMODE_WALK) {
		bwalkHandleActivate();
	}
}

void bmoveApplyMoveData(struct movedata *data)
{
	if (g_Vars.currentplayer->bondmovemode == MOVEMODE_BIKE) {
		bbikeApplyMoveData(data);
	} else if (g_Vars.currentplayer->bondmovemode == MOVEMODE_GRAB) {
		bgrabApplyMoveData(data);
	} else if (g_Vars.currentplayer->bondmovemode == MOVEMODE_WALK) {
		bwalkApplyMoveData(data);
	}

	// record some inputs if this is a local player
	if (g_NetMode && !g_Vars.currentplayer->isremote) {
		g_Vars.currentplayer->ucmd = 0;
		if (g_Vars.currentplayer->isdead) {
			if (g_NetMode == NETMODE_CLIENT) {
				/* M0.2: 0xb000 = A_BUTTON | Z_TRIG | START_BUTTON */
				s32 respawnPlayer = (s32)optionsGetContpadNum1(g_Vars.currentplayerstats->mpindex);
				if (actionHeld(respawnPlayer, ACTION_USE) || actionHeld(respawnPlayer, ACTION_FIRE_PRIMARY) || actionHeld(respawnPlayer, ACTION_PAUSE)) {
					g_Vars.currentplayer->ucmd |= UCMD_RESPAWN;
				}
			}
		} else {
			if (data->triggeron) {
				g_Vars.currentplayer->ucmd |= UCMD_FIRE;
			}
			if (data->aiming) {
				g_Vars.currentplayer->ucmd |= UCMD_AIMMODE;
			}
			if (g_Vars.currentplayer->bondactivateorreload & JO_ACTION_ACTIVATE) {
				g_Vars.currentplayer->ucmd |= UCMD_ACTIVATE;
			}
			if (g_Vars.currentplayer->bondactivateorreload & JO_ACTION_RELOAD) {
				g_Vars.currentplayer->ucmd |= UCMD_RELOAD;
			}
			if (data->eyesshut) {
				g_Vars.currentplayer->ucmd |= UCMD_EYESSHUT;
			}
			if (data->weaponbackoffset || data->weaponforwardoffset) {
				g_Vars.currentplayer->ucmd |= UCMD_SELECT;
			}
			/* Jump (PC-added ability) over the wire. wantsjump was set this frame
			 * by the local jump-input path (bmoveProcessInput ~line 2413) and is
			 * not consumed until bwalkUpdateVertical (physics), so it still holds
			 * the jump intent here. Without this, a client's jump was predicted
			 * locally but never reached the server, so the server never applied it
			 * and remote players never saw the jump (jump broken in netplay). The
			 * remote side already decodes it: bmoveProcessRemoteInput sets
			 * wantsjump from UCMD_JUMP, and bwalkUpdateVertical enforces the
			 * grounded check on both sides. */
			if (g_Vars.currentplayer->wantsjump) {
				g_Vars.currentplayer->ucmd |= UCMD_JUMP;
			}
		}
	}
}

void bmoveUpdateSpeedTheta(void)
{
	if (g_Vars.currentplayer->bondmovemode == MOVEMODE_BIKE) {
		// empty
	} else if (g_Vars.currentplayer->bondmovemode == MOVEMODE_GRAB) {
		bgrabUpdateSpeedTheta();
	} else if (g_Vars.currentplayer->bondmovemode == MOVEMODE_WALK) {
		bwalkUpdateSpeedTheta();
	}
}

f32 bmoveGetSpeedVertaLimit(f32 value)
{
	if (value > 0) {
		return (viGetFovY() * value * -0.7f) / 60.0f;
	}

	if (value < 0) {
		return (viGetFovY() * -value * 0.7f) / 60.0f;
	}

	return 0;
}

void bmoveUpdateSpeedVerta(f32 value)
{
	f32 mult = viGetFovY() / 60.0f;
	f32 limit = bmoveGetSpeedVertaLimit(value);

	if (value > 0) {
		if (g_Vars.currentplayer->speedverta > 0) {
			g_Vars.currentplayer->speedverta -= 0.05f * g_Vars.lvupdate60freal * mult;
		} else {
			g_Vars.currentplayer->speedverta -= 0.0125f * g_Vars.lvupdate60freal * mult;
		}

		if (g_Vars.currentplayer->speedverta < limit) {
			g_Vars.currentplayer->speedverta = limit;
		}
	} else if (value < 0) {
		if (g_Vars.currentplayer->speedverta < 0) {
			g_Vars.currentplayer->speedverta += 0.05f * g_Vars.lvupdate60freal * mult;
		} else {
			g_Vars.currentplayer->speedverta += 0.0125f * g_Vars.lvupdate60freal * mult;
		}

		if (g_Vars.currentplayer->speedverta > limit) {
			g_Vars.currentplayer->speedverta = limit;
		}
	} else {
		if (g_Vars.currentplayer->speedverta > limit) {
			g_Vars.currentplayer->speedverta -= 0.05f * g_Vars.lvupdate60freal * mult;

			if (g_Vars.currentplayer->speedverta < limit) {
				g_Vars.currentplayer->speedverta = limit;
			}
		} else {
			g_Vars.currentplayer->speedverta += 0.05f * g_Vars.lvupdate60freal * mult;

			if (g_Vars.currentplayer->speedverta > limit) {
				g_Vars.currentplayer->speedverta = limit;
			}
		}
	}
}

f32 bmoveGetSpeedThetaControlLimit(f32 value)
{
	if (value > 0) {
		return (viGetFovY() * value * -0.7f) / 60.0f;
	}

	if (value < 0) {
		return (viGetFovY() * -value * 0.7f) / 60.0f;
	}

	return 0;
}

void bmoveUpdateSpeedThetaControl(f32 value)
{
	f32 mult = viGetFovY() / 60.0f;
	f32 limit = bmoveGetSpeedThetaControlLimit(value);

	if (value > 0) {
		if (g_Vars.currentplayer->speedthetacontrol > 0) {
			g_Vars.currentplayer->speedthetacontrol -= 0.05f * g_Vars.lvupdate60freal * mult;
		} else {
			g_Vars.currentplayer->speedthetacontrol -= 0.0125f * g_Vars.lvupdate60freal * mult;
		}

		if (g_Vars.currentplayer->speedthetacontrol < limit) {
			g_Vars.currentplayer->speedthetacontrol = limit;
		}
	} else if (value < 0) {
		if (g_Vars.currentplayer->speedthetacontrol < 0.0f) {
			g_Vars.currentplayer->speedthetacontrol += 0.05f * g_Vars.lvupdate60freal * mult;
		} else {
			g_Vars.currentplayer->speedthetacontrol += 0.0125f * g_Vars.lvupdate60freal * mult;
		}

		if (g_Vars.currentplayer->speedthetacontrol > limit) {
			g_Vars.currentplayer->speedthetacontrol = limit;
		}
	} else {
		if (g_Vars.currentplayer->speedthetacontrol > limit) {
			g_Vars.currentplayer->speedthetacontrol -= 0.05f * g_Vars.lvupdate60freal * mult;

			if (g_Vars.currentplayer->speedthetacontrol < limit) {
				g_Vars.currentplayer->speedthetacontrol = limit;
			}
		} else {
			g_Vars.currentplayer->speedthetacontrol += 0.05f * g_Vars.lvupdate60freal * mult;

			if (g_Vars.currentplayer->speedthetacontrol > limit) {
				g_Vars.currentplayer->speedthetacontrol = limit;
			}
		}
	}
}

/**
 * Calculate the lookahead angle.
 *
 * The return value is the intended vertical angle to look at.
 * 90 = straight up
 * 0 = horizontal
 * -90 = straight down
 */
f32 bmoveCalculateLookahead(void)
{
	f32 result = -4.0f;
	f32 sp160 = 400.0f;
	f32 ground = g_Vars.currentplayer->vv_ground;
	struct coord sp150;
	f32 ymax;
	f32 ymin;
	f32 radius;
	u32 stack2;
	f32 angles[5];
	bool populated[5];
	s32 numpopulated = 0;
	u16 flags = 0;
	u32 stack3;
	struct coord sp100;
	u32 stack;
	struct coord spf0;
	RoomNum spe0[8];
	s32 i;
	f32 angle;
	f32 value;
	u32 stack4;
	u32 stack5;
	u32 stack6;
	struct coord spbc;
	struct coord spb0;
	RoomNum spa0[8];
	RoomNum sp90[8];
	RoomNum sp80[8];
	s32 j;
	f32 sp78;
	s32 indextoremove;
	f32 angletoremove;

	if (g_Vars.currentplayer->inlift) {
		return result;
	}

	playerGetBbox(g_Vars.currentplayer->prop, &radius, &ymax, &ymin);

	sp100.x = g_Vars.currentplayer->bond2.unk00.x;
	sp100.y = g_Vars.currentplayer->bond2.unk00.y;
	sp100.z = g_Vars.currentplayer->bond2.unk00.z;

	spf0.x = g_Vars.currentplayer->prop->pos.x;
	spf0.y = g_Vars.currentplayer->prop->pos.y - 30;
	spf0.z = g_Vars.currentplayer->prop->pos.z;

	portal00018148(&g_Vars.currentplayer->prop->pos, &spf0,
			g_Vars.currentplayer->prop->rooms, spe0, NULL, 0);

	sp150.x = sp100.x * 400 + spf0.x;
	sp150.y = sp100.y * 400 + spf0.y;
	sp150.z = sp100.z * 400 + spf0.z;

	if (cdExamLos08(&spf0, spe0, &sp150,
				CDTYPE_BG | CDTYPE_CLOSEDDOORS,
				GEOFLAG_FLOOR1 | GEOFLAG_FLOOR2 | GEOFLAG_WALL | GEOFLAG_BLOCK_SIGHT) == CDRESULT_COLLISION) {
		cdGetPos(&sp150, 455, "bondmove.c");
		flags = cdGetGeoFlags();

		sp160 = sqrtf((sp150.x - spf0.x) * (sp150.x - spf0.x)
				+ (sp150.y - spf0.y) * (sp150.y - spf0.y)
				+ (sp150.z - spf0.z) * (sp150.z - spf0.z));
	}

	if (sp160 > 60.0f || (flags & GEOFLAG_FLOOR1)) {
		for (i = 0; i < ARRAYCOUNT(populated); i++) {
			populated[i] = false;
			value = (i + 1) * sp160 * 0.2f;

			spbc.x = sp100.x * value + spf0.x;
			spbc.y = sp100.y * value + spf0.y;
			spbc.z = sp100.z * value + spf0.z;

			portal00018148(&spf0, &spbc, spe0, spa0, NULL, 0);

			spb0.x = spbc.x;
			spb0.y = spbc.y - 400;
			spb0.z = spbc.z;

			portal00018148(&spbc, &spb0, spa0, sp90, sp80, 7);

			if (
#if VERSION >= VERSION_NTSC_1_0
					cdFindFloorRoomYColourFlagsAtPos(&spbc, sp80, &sp78, NULL, NULL) > 0
#else
					cdFindFloorRoomYColourFlagsAtPos(&spbc, sp80, &sp78, NULL) > 0
#endif
					&& sp78 - ground < 200
					&& sp78 - ground > -200) {
				angle = atan2f(sp78 - g_Vars.currentplayer->vv_ground, value);
				angle = (angle * 360) / M_BADTAU + -4;

				if (angle >= 180) {
					angle -= 360;
				}

				if (angle >= -50 && angle <= 40) {
					populated[i] = true;
					angles[i] = angle;
					numpopulated++;
					ground = sp78;
				}
			}
		}

		for (i = 0; i < numpopulated - 1; i++) {
			indextoremove = -1;

			for (j = 0; j < ARRAYCOUNT(populated); j++) {
				if (populated[j]) {
					if (indextoremove < 0) {
						indextoremove = j;
						angletoremove = angles[j];
					} else if (i & 1) {
						if (angletoremove > angles[j]) {
							indextoremove = j;
							angletoremove = angles[j];
						}
					} else {
						if (angletoremove < angles[j]) {
							indextoremove = j;
							angletoremove = angles[j];
						}
					}
				}
			}

			if (indextoremove >= 0) {
				populated[indextoremove] = false;
			}
		}

		for (i = 0; i < ARRAYCOUNT(populated); i++) {
			if (populated[i]) {
				result = angles[i];
				break;
			}
		}
	}

	if (result > 0.0f) {
		result *= 0.86666667461395f;
	}

	return result;
}

void bmoveResetMoveData(struct movedata *data)
{
	data->canswivelgun = 0;
	data->canmanualaim = 0;
	data->triggeron = false;
	data->btapcount = 0;
	data->canlookahead = false;
	data->unk14 = 0;
	data->cannaturalturn = false;
	data->cannaturalpitch = false;
	data->digitalstepforward = false;
	data->digitalstepback = false;
	data->digitalstepleft = false;
	data->digitalstepright = false;
	data->weaponbackoffset = 0;
	data->weaponforwardoffset = 0;
	data->unk50 = 0;
	data->aiming = false;
	data->zooming = false;
	data->crouchdown = false;
	data->crouchup = false;
	data->rleanleft = false;
	data->rleanright = false;
	data->detonating = false;
	data->canautoaim = false;
	data->farsighttempautoseek = false;
	data->eyesshut = false;
	data->unk30 = 0;
	data->unk34 = 0;
	data->speedvertadown = 0;
	data->speedvertaup = 0;
	data->aimturnleftspeed = 0;
	data->aimturnrightspeed = 0;
	data->zoomoutfovpersec = 0;
	data->zoominfovpersec = 0;
	data->invertpitch = !optionsGetForwardPitch(g_Vars.currentplayerstats->mpindex);
	data->disablelookahead = false;
	data->c1stickxsafe = 0;
	data->c1stickysafe = 0;
	data->c1stickxraw = 0;
	data->c1stickyraw = 0;
	data->analogturn = 0;
	data->analogpitch = 0;
	data->analogstrafe = 0;
	data->analogwalk = 0;
	data->alt1tapcount = 0;
	data->freelookdx = 0.0f;
	data->freelookdy = 0.0f;
	data->analoglean = 0.0f;
}

/**
 * Called with these arguments:
 * 0, 0, 0, 1 = tickmode 6
 * 0, 0, 0, 1 = eyespy
 * 0, 0, 0, 1 = teleportstate 3
 * 0, 0, 0, 1 = slayerrocket
 * 0, 0, 0, 1 = tickmode normal without control
 * 1, 1, ?, 0 = tickmode normal with control
 * 1, 1, ?, 0 = tickmodes 0 and 5
 * 0, 0, 0, 1 = tickmode mpswirl
 * 0, 0, 0, 1 = tickmode warp
 * 1, 1, 0, 1 = autowalk
 */
void bmoveProcessInput(bool allowc1x, bool allowc1y, bool allowc1buttons, bool ignorec2)
{
	struct movedata movedata;
	s32 controlmode;
	s32 weaponnum;
	bool canmanualzoom;
	s32 result;
	u32 c1buttons;
	u32 c1buttonsthisframe;
	u32 c1allowedbuttons;
	u32 c1inhibitedbuttons;
	u32 aimonhist[20];
	u32 aimoffhist[20];
	s32 numsamples;
	f32 tmp;
	f32 fVar25;
	s8 shootpad;
	s8 aimpad;
	u32 aimallowedbuttons;
	u32 shootallowedbuttons;
	s8 c2stickx;
	u32 c2buttons;
	u32 c2buttonsthisframe;
	s32 i;
	s32 tmpc2sticky;
	u32 c2allowedbuttons;
	s32 tmpc2stickx;
	s32 c2sticky;
	u32 shootbuttons;
	u32 aimbuttons;
	u32 invbuttons;
	bool zoomout;
	bool zoomin;
	f32 increment;
	f32 savedverta;
	f32 noiseradius;
	f32 zoomfov;
	f32 eraserfov;
	struct coord spa0;
	f32 crosspos[2];
	f32 lookahead;
	s8 contpad1;
	s8 contpad2;
	s8 c1stickx;
	s8 c1sticky;
	u32 inhibitedbuttons;
	bool offbike;
	bool cancycleweapons;
	u32 stack;
	f32 increment2;
	f32 newverta;
	const f32 mlookscale = g_Vars.lvupdate240 ? (4.f / (f32)g_Vars.lvupdate240) : 4.f;
	const bool allowmlook = (g_Vars.currentplayernum == 0) && (allowc1x || allowc1y);
	bool allowmcross = false;

	controlmode = optionsGetControlMode(g_Vars.currentplayerstats->mpindex);

	if (g_Vars.currentplayer->isremote || controlmode == CONTROLMODE_NA) {
		bmoveProcessRemoteInput(allowc1buttons);
		return;
	}

	weaponnum = bgunGetWeaponNum(HAND_RIGHT);
	canmanualzoom = weaponHasAimFlag(weaponnum, INVAIMFLAG_MANUALZOOM);
	contpad1 = optionsGetContpadNum1(g_Vars.currentplayerstats->mpindex);

	/* Action map is indexed by controller slot (contpad1), not by mpindex.
	 * In solo mode, mpindex=0 (fixed: lv.c now uses slot 0, not MAX_PLAYERS).
	 * In multiplayer, contpad1 matches the SDL controller index for this player. */
	s32 actionPlayer = (s32)contpad1;

	/* DIAG: log movement inputs every ~120 frames */
	if ((g_Vars.lvframenum % 120) == 1) {
		sysLogPrintf(LOG_NOTE, "BMOVE: allowc1x=%d allowc1y=%d allowc1buttons=%d actionPlayer=%d AXIS_MOVE=%.3f,%.3f pausemode=%d lvupdate240=%d lvIsPaused=%d",
			(s32)allowc1x, (s32)allowc1y, (s32)allowc1buttons, actionPlayer,
			actionValue(actionPlayer, ACTION_AXIS_MOVE_X),
			actionValue(actionPlayer, ACTION_AXIS_MOVE_Y),
			g_Vars.currentplayer->pausemode,
			g_Vars.lvupdate240,
			lvIsPaused());
	}

	/* C-2 fix: multiplier 127 matches original ±0x7F/0x80 range from inputReadController */
	c1stickx = allowc1x ? (s8)(actionValue(actionPlayer, ACTION_AXIS_MOVE_X) * 127.0f) : 0;
	c1sticky = allowc1y ? (s8)(actionValue(actionPlayer, ACTION_AXIS_MOVE_Y) * 127.0f) : 0;
	/* Aim input always read regardless of allowc1x/allowc1y — those gates
	 * are for movement suppression (e.g., during menu overlays), not aiming. */
	c2stickx = (s8)(actionValue(actionPlayer, ACTION_AXIS_AIM_X) * 127.0f);
	c2sticky = (s8)(actionValue(actionPlayer, ACTION_AXIS_AIM_Y) * 127.0f);
	/* Weapon radial reads AIM axes directly in activemenutick.c; suppress aim
	 * here so walk/camera/gun don't track RS while the radial is open (B-202). */
	if (g_Vars.currentplayer->activemenumode != AMMODE_CLOSED) {
		c2stickx = 0;
		c2sticky = 0;
	}

	/* M0.2: synthesize button bitmask from action queries for downstream mask logic.
	 *
	 * PC ACTION_USE (X face / Use key) - twin-stick:
	 *   - Short press / tap -> X_BUTTON (reload)
	 *   - Hold > use-hold-ms -> A_BUTTON + consume (interact)
	 * Reload uses ACTION_RELOAD (R) or USE tap. Interact is hold-only.
	 *
	 * ACTION_RELOAD (R kbd) is treated as a dedicated immediate-reload key. */
	{
		s32 pi = actionPlayer;
		c1buttons = 0;
		c1buttonsthisframe = 0;
		if (allowc1buttons) {
			/* B-246 round-5 diagnostic (2026-04-26): trace fire-input layer.
			 * Mike's playtest of f83ca230 shows triggeron_in always 0 in
			 * bgunTickGameplay (156/156 ticks).  Confirms input never reaches
			 * the move-side fire path.  Capture actionHeld for FIRE_PRIMARY +
			 * a few siblings every 60 frames so the next playtest log shows
			 * whether actionHeld returns 0 (input layer dead) or returns 1
			 * but the c1buttons -> triggeron path drops it.  Player 0 only. */
			if (pi == 0 && (g_Vars.lvframenum % 60) == 29) {
				s32 fp_held    = actionHeld(pi, ACTION_FIRE_PRIMARY);
				s32 fp_pressed = actionPressed(pi, ACTION_FIRE_PRIMARY);
				s32 use_held   = actionHeld(pi, ACTION_USE);
				s32 wnxt_held  = actionHeld(pi, ACTION_WEAPON_NEXT);
				sysLogPrintf(LOG_NOTE,
					"LOG.WPN.DIAG: fire-input pi=0 fire_held=%d fire_pressed=%d "
					"use_held=%d wnext_held=%d allowc1=%d frame=%d",
					fp_held, fp_pressed, use_held, wnxt_held,
					(s32)allowc1buttons, (s32)g_Vars.lvframenum);
			}
			/* Mike's directive 2026-05-17: "if a held option exists for the
			 * button, it should be based on button press vs released time, so
			 * I can have pressed inputs and held inputs both." Applied below
			 * to ACTION_WEAPON_NEXT (tap=cycle, hold=open weapon wheel) and
			 * ACTION_CROUCH (tap=toggle sustained-crouch lock, hold=momentary
			 * crouch while held). Threshold BOND_TAP_HOLD_THRESH_MS at file
			 * scope. */
			if (actionHeld(pi, ACTION_FIRE_PRIMARY))   c1buttons |= Z_TRIG;
			/* B-246 round-9: F2 test-fire injection. When a scheduled fire
			 * reaches its target lvframenum, synthesise a one-frame Z_TRIG
			 * pulse for player 0. Drains one queue entry per call. The
			 * c1buttonsthisframe OR happens at the matching actionPressed
			 * site below to provide a rising edge on the same frame. */
			if (pi == 0 && bmoveTestFireDueThisFrame()) {
				c1buttons |= Z_TRIG;
				c1buttonsthisframe |= Z_TRIG;
			}
			if (actionHeld(pi, ACTION_FIRE_SECONDARY)) c1buttons |= R_TRIG;
			if (actionHeld(pi, ACTION_FIRE_MODE))      c1buttons |= L_TRIG;
			if (actionHeld(pi, ACTION_CANCEL_USE))     c1buttons |= B_BUTTON;
			/* ACTION_WEAPON_NEXT: tap/hold dual-action. Held -> BUTTON_RADIAL
			 * (open the weapon wheel, BUTTON_RADIAL = D_JPAD per constants.h).
			 * Tap -> Y_BUTTON (cycle one slot), wired below on the release
			 * edge via actionWasTap. The Y_BUTTON held bit is NOT asserted
			 * here because the cycle is a one-shot on release, not a
			 * continuous press. */
			if (actionHeldForMs(pi, ACTION_WEAPON_NEXT, BOND_TAP_HOLD_THRESH_MS)
					&& !actionHoldConsumed(pi, ACTION_WEAPON_NEXT)) {
				c1buttons |= BUTTON_RADIAL;
			}
			if (actionHeld(pi, ACTION_PAUSE))          c1buttons |= START_BUTTON;
			if (actionHeld(pi, ACTION_CBUTTON_UP))     c1buttons |= U_CBUTTONS;
			if (actionHeld(pi, ACTION_CBUTTON_DOWN))   c1buttons |= D_CBUTTONS;
			if (actionHeld(pi, ACTION_CBUTTON_LEFT))   c1buttons |= L_CBUTTONS;
			if (actionHeld(pi, ACTION_CBUTTON_RIGHT))  c1buttons |= R_CBUTTONS;
			if (actionHeld(pi, ACTION_DPAD_UP))        c1buttons |= U_JPAD;
			if (actionHeld(pi, ACTION_DPAD_DOWN))      c1buttons |= D_JPAD;
			if (actionHeld(pi, ACTION_DPAD_LEFT))      c1buttons |= L_JPAD;
			if (actionHeld(pi, ACTION_DPAD_RIGHT))     c1buttons |= R_JPAD;
			/* ACTION_CROUCH 3-state model (Mike directive 2026-05-17):
			 *   STAND <-tap-> DUCK    (toggle between standing and crouched)
			 *   any   --hold-> SQUAT  (prone via long-hold)
			 *   SQUAT --tap-> DUCK    (one tap from prone -> crouched)
			 *   DUCK  --tap-> STAND   (next tap -> standing)
			 *   any   --jump-> STAND  (jumping resets stance)
			 * State transitions live in the rising-edge block below; here
			 * we just translate the current crouchpos to the legacy
			 * BUTTON_HALF_CROUCH / BUTTON_FULL_CROUCH bits the rest of the
			 * dispatcher reads (bondwalk capsule height, head tilt, etc.). */
			{
				const s32 cp = g_Vars.players[pi]->crouchpos;
				if (cp == CROUCHPOS_DUCK) {
					c1buttons |= BUTTON_HALF_CROUCH;
				} else if (cp == CROUCHPOS_SQUAT) {
					c1buttons |= BUTTON_FULL_CROUCH;
				}
			}
			if (actionHeld(pi, ACTION_JUMP))           c1buttons |= BUTTON_JUMP;
			/* R kbd: dedicated reload */
			if (actionHeld(pi, ACTION_RELOAD))         c1buttons |= X_BUTTON;

			if (actionPressed(pi, ACTION_FIRE_PRIMARY))   c1buttonsthisframe |= Z_TRIG;
			if (actionPressed(pi, ACTION_FIRE_SECONDARY)) c1buttonsthisframe |= R_TRIG;
			if (actionPressed(pi, ACTION_FIRE_MODE))      c1buttonsthisframe |= L_TRIG;
			if (actionPressed(pi, ACTION_CANCEL_USE))     c1buttonsthisframe |= B_BUTTON;
			/* ACTION_WEAPON_NEXT tap/hold rising edges: hold past threshold
			 * fires the BUTTON_RADIAL one-shot edge (radial wheel open),
			 * consumes the gesture so the tap path stays silent on release.
			 * The TAP cycle is handled directly by the actionWasTap call at
			 * the legacy PC inventory dispatch site below; no Y_BUTTON
			 * synthesis here, otherwise the inventory would advance twice
			 * per tap (once from actionWasTap, once from c1buttonsthisframe
			 * BUTTON_WPNFORWARD edge). */
			if (actionHeldForMs(pi, ACTION_WEAPON_NEXT, BOND_TAP_HOLD_THRESH_MS)
					&& !actionHoldConsumed(pi, ACTION_WEAPON_NEXT)) {
				c1buttonsthisframe |= BUTTON_RADIAL;
				actionConsumeHold(pi, ACTION_WEAPON_NEXT);
			}
			if (actionPressed(pi, ACTION_PAUSE))          c1buttonsthisframe |= START_BUTTON;
			if (actionPressed(pi, ACTION_CBUTTON_UP))     c1buttonsthisframe |= U_CBUTTONS;
			if (actionPressed(pi, ACTION_CBUTTON_DOWN))   c1buttonsthisframe |= D_CBUTTONS;
			if (actionPressed(pi, ACTION_CBUTTON_LEFT))   c1buttonsthisframe |= L_CBUTTONS;
			if (actionPressed(pi, ACTION_CBUTTON_RIGHT))  c1buttonsthisframe |= R_CBUTTONS;
			if (actionPressed(pi, ACTION_DPAD_UP))        c1buttonsthisframe |= U_JPAD;
			if (actionPressed(pi, ACTION_DPAD_DOWN))      c1buttonsthisframe |= D_JPAD;
			if (actionPressed(pi, ACTION_DPAD_LEFT))      c1buttonsthisframe |= L_JPAD;
			if (actionPressed(pi, ACTION_DPAD_RIGHT))     c1buttonsthisframe |= R_JPAD;
			/* ACTION_CROUCH 3-state state-machine edges (Mike 2026-05-17):
			 *   STAND tap -> DUCK,  DUCK tap -> STAND  (toggle pair)
			 *   any hold past threshold -> SQUAT (prone)
			 *   SQUAT tap -> DUCK
			 *   ACTION_JUMP press -> STAND (handled at the jump edge below)
			 * State writes are direct on currentplayer->crouchpos; the held
			 * block above translates the state back into BUTTON_*_CROUCH
			 * for downstream consumers. We do NOT fire BUTTON_HALF_CROUCH
			 * edges from this block because that would re-trigger the
			 * legacy crouchpos toggle dispatcher at bondmove.c:2150+. */
			{
				const s32 past_thresh = actionHeldForMs(pi, ACTION_CROUCH, BOND_TAP_HOLD_THRESH_MS);
				const s32 consumed    = actionHoldConsumed(pi, ACTION_CROUCH);
				if (past_thresh && !consumed) {
					g_Vars.players[pi]->crouchpos = CROUCHPOS_SQUAT;
					actionConsumeHold(pi, ACTION_CROUCH);
				} else if (actionReleased(pi, ACTION_CROUCH)
						&& actionWasTap(pi, ACTION_CROUCH, BOND_TAP_HOLD_THRESH_MS)) {
					const s32 cp = g_Vars.players[pi]->crouchpos;
					if (cp == CROUCHPOS_STAND) {
						g_Vars.players[pi]->crouchpos = CROUCHPOS_DUCK;
					} else if (cp == CROUCHPOS_DUCK) {
						g_Vars.players[pi]->crouchpos = CROUCHPOS_STAND;
					} else { /* CROUCHPOS_SQUAT */
						g_Vars.players[pi]->crouchpos = CROUCHPOS_DUCK;
					}
				}
			}
			if (actionPressed(pi, ACTION_JUMP)) {
				c1buttonsthisframe |= BUTTON_JUMP;
				/* Mike 2026-05-17: jumping resets stance to standing.
				 * Crouch-jump mid-air lift is set up here too -- latch the
				 * window so bondwalk's vertical pipeline can give the
				 * player a small extra reach when they hold crouch in
				 * the upward phase. */
				g_Vars.players[pi]->crouchpos = CROUCHPOS_STAND;
				g_BondCrouchJumpActive[pi] = 1;
			}

			/* Crouch-jump mid-air lift (Mike directive 2026-05-17).
			 * When the player taps ACTION_CROUCH while mid-jump (still
			 * going up, bdeltapos.y > 0) and the crouch-jump window is
			 * armed (g_BondCrouchJumpActive == 1, set at jump-press
			 * above), give a small extra upward impulse so the apex is
			 * a touch higher -- the player can clear a surface slightly
			 * above what their regular jump would reach. ~18% of the
			 * 8.2 FIXED_JUMP_IMPULSE so the boost is felt but not
			 * dramatic. Mark consumed so a held crouch does not stack
			 * the boost frame-by-frame. */
			if (g_BondCrouchJumpActive[pi] == 1
					&& actionPressed(pi, ACTION_CROUCH)
					&& g_Vars.players[pi]->bdeltapos.y > 0.0f) {
				g_Vars.players[pi]->bdeltapos.y += 1.5f;
				g_BondCrouchJumpActive[pi] = 2;
			}

			/* Crouch-jump landing-stance restore (Mike directive 2026-05-17).
			 * During a crouch-jump the player typically taps/holds CROUCH
			 * mid-air, which moves crouchpos -> DUCK (tap) or SQUAT (hold)
			 * via the stance state machine above. When they land, the
			 * crouched stance persists, leaving the player ducked even
			 * though they only crouched to grab the boost. Restore to
			 * STAND on landing unless ACTION_CROUCH is currently held.
			 *
			 * Trigger: g_BondCrouchJumpActive >= 1 (a jump is in progress)
			 * AND we just transitioned bondonground 0 -> non-zero (landed).
			 * The s_BondPrevOnGround tracker is updated at the bottom of
			 * the per-player block so the next tick's diff is correct. */
			{
				const s32 nowGround = g_Vars.players[pi]->bondonground != 0;
				const s32 wasAirborne = (s_BondPrevOnGround[pi] == 0);
				if (g_BondCrouchJumpActive[pi] >= 1 && nowGround && wasAirborne) {
					if (!actionHeld(pi, ACTION_CROUCH)) {
						g_Vars.players[pi]->crouchpos = CROUCHPOS_STAND;
					}
					g_BondCrouchJumpActive[pi] = 0;
				}
				s_BondPrevOnGround[pi] = nowGround;
			}
			if (actionPressed(pi, ACTION_RELOAD))         c1buttonsthisframe |= X_BUTTON;

			/* ACTION_USE split:
			 *   Tap (release before threshold)  -> X_BUTTON (reload) ALWAYS
			 *   Hold past threshold             -> A_BUTTON (interact) + consume
			 * A tap or double-tap must not synthesize A_BUTTON. Interact is
			 * hold-only so doors/terminals/pickups cannot fire on a quick tap. */
			{
				const s32 useThreshMs = propGetActionUseHoldThresholdMs();
				if (actionHeldForMs(pi, ACTION_USE, useThreshMs)
						&& !actionHoldConsumed(pi, ACTION_USE)) {
					c1buttons          |= A_BUTTON;
					c1buttonsthisframe |= A_BUTTON;
					actionConsumeHold(pi, ACTION_USE);
				} else if (actionReleased(pi, ACTION_USE)
						&& actionWasTap(pi, ACTION_USE, useThreshMs)) {
					const u32 now_frame   = (u32)g_Vars.lvframe60;
					const u32 prev_frame  = s_BondLastUseTapReleaseFrame[pi];
					const u32 gap_frames  = (prev_frame != 0) ? (now_frame - prev_frame) : 0xFFFFFFFFu;
					if (prev_frame != 0 && gap_frames <= BOND_DOUBLE_TAP_TICKS) {
						/* Double-tap is intentionally consumed as a tap pair,
						 * not promoted to interact. Clear the frame stamp so
						 * a triple-tap does not cascade into another reload. */
						s_BondLastUseTapReleaseFrame[pi] = 0;
					} else {
						/* Single tap: reload. Record frame so a quick
						 * follow-up tap can be consumed as a double-tap pair. */
						c1buttons          |= X_BUTTON;
						c1buttonsthisframe |= X_BUTTON;
						s_BondLastUseTapReleaseFrame[pi] = now_frame;
					}
				}
			}
		}
	}

	c1allowedbuttons = 0xffffffff;

	if (g_Vars.currentplayer->joybutinhibit & 0xffffffff) {
		inhibitedbuttons = g_Vars.currentplayer->joybutinhibit & 0xffffffff;
		c1allowedbuttons = ~inhibitedbuttons;
		inhibitedbuttons = c1buttons & inhibitedbuttons; /* M0.2: was joyGetButtons re-read, now uses synthesized mask */
		c1buttons &= ~inhibitedbuttons;
		c1buttonsthisframe &= ~inhibitedbuttons;
		g_Vars.currentplayer->joybutinhibit = (g_Vars.currentplayer->joybutinhibit & 0x0) | inhibitedbuttons;
	}

	numsamples = joyGetNumSamples();
	/* B-209: PC buttons/sticks come from the action map, but downstream logic still
	 * scales by numsamples = joyGetNumSamples(). When the joy ring has no span
	 * (curstart == curlast), numsamples is 0 and every numsamples-scaled loop is
	 * skipped — including reload (alt1tapcount) and use (btapcount) even when
	 * c1buttons from the hold/tap discriminator is valid. One logical sample
	 * matches the per-frame action-map snapshot. */
	if (controlmode == CONTROLMODE_PC && numsamples < 1) {
		numsamples = 1;
	}
	bmoveResetMoveData(&movedata);

	if (c1stickx < -5) {
		movedata.c1stickxsafe = c1stickx + 5;
	} else if (c1stickx > 5) {
		movedata.c1stickxsafe = c1stickx - 5;
	} else {
		movedata.c1stickxsafe = 0;
	}

	if (c1sticky < -5) {
		movedata.c1stickysafe = c1sticky + 5;
	} else if (c1sticky > 5) {
		movedata.c1stickysafe = c1sticky - 5;
	} else {
		movedata.c1stickysafe = 0;
	}

	movedata.c1stickxraw = c1stickx;
	movedata.c1stickyraw = c1sticky;

	// These are zeroed further down conditionally on control style
	movedata.analogturn = movedata.c1stickxsafe;
	movedata.analogstrafe = movedata.c1stickxsafe;
	movedata.analogpitch = movedata.c1stickysafe;
	movedata.analogwalk = movedata.c1stickysafe;

	if (allowmlook) {
		inputMouseGetScaledDelta(&movedata.freelookdx, &movedata.freelookdy);
		allowmcross = (PLAYER_EXTCFG().mouseaimmode == MOUSEAIM_CLASSIC) &&
			(movedata.freelookdx || movedata.freelookdy || g_Vars.currentplayer->swivelpos[0] || g_Vars.currentplayer->swivelpos[1]);
		if (movedata.invertpitch) {
			movedata.freelookdy = -movedata.freelookdy;
		}
	}
	/* B-124 fix: Escape→START synthesis removed. ACTION_PAUSE is bound to
	 * VK_ESCAPE in the action map — the parallel inputKeyJustPressed(VK_ESCAPE)
	 * path was a second input reader that caused double-fire race conditions.
	 * actionPressed(pi, ACTION_PAUSE) at line 979 already sets START_BUTTON. */

	// Pausing
	if (g_Vars.currentplayer->isdead == false) {
		if (c1buttonsthisframe & START_BUTTON) {
			sysLogPrintf(LOG_NOTE, "MENU: ACTION_PAUSE detected, pausemode=%d g_PlayersWithControl[0]=%d mplayerisrunning=%d lvframenum=%d",
				g_Vars.currentplayer->pausemode,
				(int)g_PlayersWithControl[g_Vars.currentplayernum],
				(int)g_Vars.mplayerisrunning,
				g_Vars.lvframenum);
		}
		if (g_Vars.currentplayer->pausemode == PAUSEMODE_UNPAUSED && (c1buttonsthisframe & START_BUTTON)) {
			if (g_Vars.mplayerisrunning == false) {
				if (g_Vars.lvframenum > 15) {
					playerPause(MENUROOT_MAINMENU);
				}
			} else {
				mpPushPauseDialog();
			}
		}
	} else {
		if (g_Vars.mplayerisrunning) {
			if (PLAYERCOUNT() == 1) {
				if (mpIsPaused() && (c1buttonsthisframe & START_BUTTON) && g_MpSetup.paused != MPPAUSEMODE_GAMEOVER) {
					mpSetPaused(MPPAUSEMODE_UNPAUSED);
				}
			} else {
				if (mpIsPaused() && (c1buttonsthisframe & START_BUTTON)) {
					mpPushPauseDialog();
				}
			}
		}
	}

	if (g_Vars.currentplayer->pausemode == PAUSEMODE_UNPAUSED) {
		if (g_Vars.currentplayer->isdead == false) {
			if (controlmode == CONTROLMODE_23 || controlmode == CONTROLMODE_24 || controlmode == CONTROLMODE_22 || controlmode == CONTROLMODE_21) {
				// 2.1: ctrl1 stick = walk/turn, z = fire, ctrl2 stick = look/strafe, z = aim
				// 2.2: ctrl1 stick = look,      z = fire, ctrl2 stick = walk/strafe, z = aim
				// 2.3: ctrl1 stick = walk/turn, z = aim,  ctrl2 stick = look/strafe, z = fire
				// 2.4: ctrl1 stick = look,      z = aim,  ctrl2 stick = walk/strafe, z = fire
				contpad2 = (s8) optionsGetContpadNum2(g_Vars.currentplayerstats->mpindex);
				/* M0.2: dual-controller sticks via action map */
				c2stickx = (s8)(actionValue((s32)contpad2, ACTION_AXIS_MOVE_X) * 80.0f);
				c2sticky = (s8)(actionValue((s32)contpad2, ACTION_AXIS_MOVE_Y) * 80.0f);
				/* M0.2: synthesize c2 button bitmask from action queries */
				{
					s32 pi2 = (s32)contpad2;
					c2buttons = 0;
					c2buttonsthisframe = 0;
					if (actionHeld(pi2, ACTION_FIRE_PRIMARY))   c2buttons |= Z_TRIG;
					if (actionHeld(pi2, ACTION_FIRE_SECONDARY)) c2buttons |= R_TRIG;
					if (actionHeld(pi2, ACTION_FIRE_MODE))      c2buttons |= L_TRIG;
					if (actionHeld(pi2, ACTION_USE))            c2buttons |= A_BUTTON;
					if (actionHeld(pi2, ACTION_CANCEL_USE))     c2buttons |= B_BUTTON;
					if (actionHeld(pi2, ACTION_RELOAD))         c2buttons |= X_BUTTON;
					if (actionHeld(pi2, ACTION_WEAPON_NEXT))    c2buttons |= Y_BUTTON;
					if (actionHeld(pi2, ACTION_PAUSE))          c2buttons |= START_BUTTON;
					if (actionHeld(pi2, ACTION_CBUTTON_UP))     c2buttons |= U_CBUTTONS;
					if (actionHeld(pi2, ACTION_CBUTTON_DOWN))   c2buttons |= D_CBUTTONS;
					if (actionHeld(pi2, ACTION_CBUTTON_LEFT))   c2buttons |= L_CBUTTONS;
					if (actionHeld(pi2, ACTION_CBUTTON_RIGHT))  c2buttons |= R_CBUTTONS;
					if (actionHeld(pi2, ACTION_DPAD_UP))        c2buttons |= U_JPAD;
					if (actionHeld(pi2, ACTION_DPAD_DOWN))      c2buttons |= D_JPAD;
					if (actionHeld(pi2, ACTION_DPAD_LEFT))      c2buttons |= L_JPAD;
					if (actionHeld(pi2, ACTION_DPAD_RIGHT))     c2buttons |= R_JPAD;

					if (actionPressed(pi2, ACTION_FIRE_PRIMARY))   c2buttonsthisframe |= Z_TRIG;
					if (actionPressed(pi2, ACTION_FIRE_SECONDARY)) c2buttonsthisframe |= R_TRIG;
					if (actionPressed(pi2, ACTION_FIRE_MODE))      c2buttonsthisframe |= L_TRIG;
					if (actionPressed(pi2, ACTION_USE))            c2buttonsthisframe |= A_BUTTON;
					if (actionPressed(pi2, ACTION_CANCEL_USE))     c2buttonsthisframe |= B_BUTTON;
					if (actionPressed(pi2, ACTION_RELOAD))         c2buttonsthisframe |= X_BUTTON;
					if (actionPressed(pi2, ACTION_WEAPON_NEXT))    c2buttonsthisframe |= Y_BUTTON;
					if (actionPressed(pi2, ACTION_PAUSE))          c2buttonsthisframe |= START_BUTTON;
					if (actionPressed(pi2, ACTION_CBUTTON_UP))     c2buttonsthisframe |= U_CBUTTONS;
					if (actionPressed(pi2, ACTION_CBUTTON_DOWN))   c2buttonsthisframe |= D_CBUTTONS;
					if (actionPressed(pi2, ACTION_CBUTTON_LEFT))   c2buttonsthisframe |= L_CBUTTONS;
					if (actionPressed(pi2, ACTION_CBUTTON_RIGHT))  c2buttonsthisframe |= R_CBUTTONS;
					if (actionPressed(pi2, ACTION_DPAD_UP))        c2buttonsthisframe |= U_JPAD;
					if (actionPressed(pi2, ACTION_DPAD_DOWN))      c2buttonsthisframe |= D_JPAD;
					if (actionPressed(pi2, ACTION_DPAD_LEFT))      c2buttonsthisframe |= L_JPAD;
					if (actionPressed(pi2, ACTION_DPAD_RIGHT))     c2buttonsthisframe |= R_JPAD;
				}

				tmpc2stickx = c2stickx;
				tmpc2sticky = c2sticky;

				c2allowedbuttons = 0xffffffff;

				// NOTE: joybutinhibit used to store two copies of the 16-bit inhibited mask for some reason
				//       now it only stores one mask because it is 32 bits in size
				if (g_Vars.currentplayer->joybutinhibit) {
					inhibitedbuttons = g_Vars.currentplayer->joybutinhibit;
					c2allowedbuttons = ~inhibitedbuttons;
					inhibitedbuttons = c2buttons & inhibitedbuttons; /* M0.2: was joyGetButtons re-read, now uses synthesized mask */
					c2buttons &= ~inhibitedbuttons;
					c2buttonsthisframe &= ~inhibitedbuttons;
					g_Vars.currentplayer->joybutinhibit |= inhibitedbuttons;
				}

				if (ignorec2) {
					c2stickx = 0;
					c2buttons = 0;
					tmpc2stickx = 0;
					tmpc2sticky = 0;
					c2buttonsthisframe = 0;
				}

				if (tmpc2stickx < -5) {
					tmpc2stickx += 5;
				} else if (tmpc2stickx > 5) {
					tmpc2stickx -= 5;
				} else {
					tmpc2stickx = 0;
				}

				if (tmpc2sticky < -5) {
					tmpc2sticky += 5;
				} else if (tmpc2sticky > 5) {
					tmpc2sticky -= 5;
				} else {
					tmpc2sticky = 0;
				}

				if (controlmode == CONTROLMODE_21 || controlmode == CONTROLMODE_23) {
					movedata.analogstrafe = tmpc2stickx;
					movedata.analogpitch = tmpc2sticky;
				} else {
					movedata.analogstrafe = tmpc2stickx;
					movedata.analogwalk = tmpc2sticky;
				}

				c2sticky = tmpc2sticky;

				if (g_Vars.tickmode == TICKMODE_AUTOWALK) {
					movedata.digitalstepforward = false;
					movedata.digitalstepback = false;
					movedata.analogstrafe = 0;
					movedata.analogwalk = g_Vars.currentplayer->autocontrol_y;
					movedata.analogturn = g_Vars.currentplayer->autocontrol_x;
					movedata.analogpitch = 0;
					movedata.freelookdx = 0.0f;
					movedata.freelookdy = 0.0f;
				}

				if (controlmode == CONTROLMODE_21 || controlmode == CONTROLMODE_22) {
					aimpad = contpad2;
					shootpad = contpad1;
					aimallowedbuttons = c2allowedbuttons;
					shootallowedbuttons = c1allowedbuttons;
				} else {
					aimpad = contpad1;
					shootpad = contpad2;
					aimallowedbuttons = c1allowedbuttons;
					shootallowedbuttons = c2allowedbuttons;
				}

				if (optionsGetAimControl(g_Vars.currentplayerstats->mpindex) == AIMCONTROL_HOLD) {
					for (i = 0; i < numsamples; i++) {
						aimonhist[i] = allowc1buttons && actionHeld((s32)aimpad, ACTION_FIRE_PRIMARY); /* M0.2: collapsed sub-frame to per-frame */
						aimoffhist[i] = !aimonhist[i];
					}

					if (numsamples > 0) {
						g_Vars.currentplayer->insightaimmode = aimonhist[numsamples - 1];
					}
				}

				if (!lvIsPaused()) {
					// Handle aiming
					if (optionsGetAimControl(g_Vars.currentplayerstats->mpindex) != AIMCONTROL_HOLD) {
						for (i = 0; i < numsamples; i++) {
							if (allowc1buttons && actionPressed((s32)aimpad, ACTION_FIRE_PRIMARY)) { /* M0.2: collapsed sub-frame to per-frame */
								g_Vars.currentplayer->insightaimmode = !g_Vars.currentplayer->insightaimmode;
							}

							aimonhist[i] = g_Vars.currentplayer->insightaimmode;
							aimoffhist[i] = !aimonhist[i];
						}
					}

					if (bgunGetWeaponNum(HAND_RIGHT) == WEAPON_HORIZONSCANNER) {
						g_Vars.currentplayer->insightaimmode = true;
					}

					movedata.canswivelgun = !g_Vars.currentplayer->insightaimmode;
					movedata.canmanualaim = g_Vars.currentplayer->insightaimmode;
					movedata.canautoaim = !g_Vars.currentplayer->insightaimmode;
					movedata.digitalstepforward = false;
					movedata.digitalstepback = false;
					movedata.digitalstepleft = false;
					movedata.digitalstepright = false;
					movedata.canlookahead = !g_Vars.currentplayer->insightaimmode;
					movedata.unk14 = 1;
					movedata.cannaturalturn = !g_Vars.currentplayer->insightaimmode;
					movedata.cannaturalpitch = !g_Vars.currentplayer->insightaimmode;

					// Handle turning while aiming
					if (g_Vars.currentplayer->insightaimmode && movedata.c1stickyraw > 60) {
						movedata.speedvertadown = (movedata.c1stickyraw - 60) / 10.0f;

						if (movedata.speedvertadown > 1) {
							movedata.speedvertadown = 1;
						}
					} else {
						movedata.speedvertadown = 0;
					}

					if (g_Vars.currentplayer->insightaimmode && movedata.c1stickyraw < -60) {
						movedata.speedvertaup = (-60 - movedata.c1stickyraw) / 10.0f;

						if (movedata.speedvertaup > 1) {
							movedata.speedvertaup = 1;
						}
					} else {
						movedata.speedvertaup = 0;
					}

					if (g_Vars.currentplayer->insightaimmode && movedata.c1stickxraw < -60) {
						movedata.aimturnleftspeed = (-60 - movedata.c1stickxraw) / 10.0f;

						if (movedata.aimturnleftspeed > 1) {
							movedata.aimturnleftspeed = 1;
						}
					} else {
						movedata.aimturnleftspeed = 0;
					}

					if (g_Vars.currentplayer->insightaimmode && movedata.c1stickxraw > 60) {
						movedata.aimturnrightspeed = (movedata.c1stickxraw - 60) / 10.0f;

						if (movedata.aimturnrightspeed > 1) {
							movedata.aimturnrightspeed = 1;
						}
					} else {
						movedata.aimturnrightspeed = 0;
					}

					// Handle weapon switching
					if (allowc1buttons) {
						if (g_Vars.currentplayer->invdowntime < -2) {
							g_Vars.currentplayer->invdowntime += numsamples;

							if (g_Vars.currentplayer->invdowntime > -3) {
								g_Vars.currentplayer->invdowntime = 0;
							}
						} else {
							for (i = 0; i < numsamples; i++) {
								if (controlmode == CONTROLMODE_PC) {
									/* M0.2: collapsed sub-frame to per-frame */
									if (i == 0) {
										if (bmoveHandleDirectWeaponSelect((s32)contpad1)) {
											g_Vars.currentplayer->invdowntime = -1;
										} else if ((c1allowedbuttons & BUTTON_WPNFORWARD)
												&& actionWasTap((s32)contpad1, ACTION_WEAPON_NEXT, BOND_TAP_HOLD_THRESH_MS)) {
											movedata.weaponforwardoffset++;
											g_Vars.currentplayer->invdowntime = -1;
										} else if ((c1allowedbuttons & BUTTON_WPNBACK)
												&& (actionPressed((s32)contpad1, ACTION_WEAPON_PREV)
													|| actionPressed((s32)contpad1, ACTION_DPAD_LEFT))) {
											movedata.weaponbackoffset++;
											g_Vars.currentplayer->invdowntime = -1;
										}
									}
									continue;
								}
								/* M0.2: collapsed sub-frame to per-frame */
								if (((c1allowedbuttons & A_BUTTON) && actionHeld((s32)contpad1, ACTION_USE))
										|| ((c2allowedbuttons & A_BUTTON) && actionHeld((s32)contpad2, ACTION_USE))) {
									if (g_Vars.currentplayer->invdowntime > -2) {
										if ((shootallowedbuttons & Z_TRIG) && actionPressed((s32)shootpad, ACTION_FIRE_PRIMARY)) {
											movedata.weaponbackoffset++;
											g_Vars.currentplayer->invdowntime = -1;
										}

										if (g_Vars.currentplayer->invdowntime > -1
												&& !((shootallowedbuttons & Z_TRIG) && actionHeld((s32)shootpad, ACTION_FIRE_PRIMARY))) {
											if (g_Vars.currentplayer->invdowntime > TICKS(15)) {
												amOpen();
												g_Vars.currentplayer->invdowntime = -1;
											} else {
												g_Vars.currentplayer->invdowntime += g_Vars.lvupdate60;
											}
										}
									}
								} else {
									if (g_Vars.currentplayer->invdowntime > 0 &&
											(!allowc1buttons || !((shootallowedbuttons & Z_TRIG) && actionHeld((s32)shootpad, ACTION_FIRE_PRIMARY)))) {
										movedata.weaponforwardoffset++;
									}

									g_Vars.currentplayer->invdowntime = 0;
								}
							}
						}
					}

					// Handle B button activation
					if (allowc1buttons && controlmode < CONTROLMODE_PC) {
						for (i = 0; i < numsamples; i++) {
							/* M0.2: collapsed sub-frame to per-frame */
							if (((c1allowedbuttons & B_BUTTON) && actionHeld((s32)contpad1, ACTION_CANCEL_USE))
									|| ((c2allowedbuttons & B_BUTTON) && actionHeld((s32)contpad2, ACTION_CANCEL_USE))) {
								if (g_Vars.currentplayer->usedowntime >= -1) {
									if ((shootallowedbuttons & Z_TRIG) && actionPressed((s32)shootpad, ACTION_FIRE_PRIMARY)
											&& g_Vars.currentplayer->usedowntime > -1
											&& bgunConsiderToggleGunFunction(g_Vars.currentplayer->usedowntime, true, false, 0) != USETIMER_CONTINUE) {
										g_Vars.currentplayer->usedowntime = -3;
									}

									if (g_Vars.currentplayer->usedowntime > -1) {
										if (g_Vars.currentplayer->usedowntime > TICKS(25)) {
											result = bgunConsiderToggleGunFunction(g_Vars.currentplayer->usedowntime, false, false, 0);

											if (result == USETIMER_STOP) {
												g_Vars.currentplayer->usedowntime = -1;
											} else if (result == USETIMER_REPEAT) {
												g_Vars.currentplayer->usedowntime = -2;
											} else {
												g_Vars.currentplayer->usedowntime++;
											}
										} else {
											g_Vars.currentplayer->usedowntime++;
										}
									}
								} else if (g_Vars.currentplayer->usedowntime >= -2) {
									bgunConsiderToggleGunFunction(g_Vars.currentplayer->usedowntime, false, false, 0);
								}
							} else {
								// Released B - activate or reload
								if (g_Vars.currentplayer->usedowntime > 0) {
									movedata.btapcount++;
								}

								g_Vars.currentplayer->usedowntime = 0;
								bgun0f0a8c50();
							}
						}
					}

					// Handle manual zoom in and out (sniper, farsight and horizon scanner)
					if (canmanualzoom && g_Vars.currentplayer->insightaimmode) {
						if (c2sticky < 0) {
							movedata.zoomoutfovpersec = -c2sticky / 70.0f;

							if (movedata.zoomoutfovpersec > 1) {
								movedata.zoomoutfovpersec = 1;
							}

							movedata.zoomoutfovpersec = movedata.zoomoutfovpersec + movedata.zoomoutfovpersec;
						}

						if (c2sticky > 0) {
							movedata.zoominfovpersec = c2sticky / 70.0f;

							if (movedata.zoominfovpersec > 1) {
								movedata.zoominfovpersec = 1;
							}

							movedata.zoominfovpersec = movedata.zoominfovpersec + movedata.zoominfovpersec;
						}
					}

					// Handle crouch and uncrouch
					if (allowc1buttons) {
						for (i = 0; i < numsamples; i++) {
							if (!canmanualzoom && aimonhist[i]) {
								/* M0.2: collapsed sub-frame stick edge detection to per-frame threshold.
								 * Was: joyGetStickYOnSample > 30 && joyGetStickYOnSampleIndex <= 30
								 * Now uses actionPressed for crouch actions on contpad2 */
								if (actionPressed((s32)contpad2, ACTION_CBUTTON_UP)) {
									if (movedata.crouchdown) {
										movedata.crouchdown--;
									} else {
										movedata.crouchup++;
									}

									g_Vars.currentplayer->aimtaptime = -1;
								}

								if (actionPressed((s32)contpad2, ACTION_CBUTTON_DOWN)) {
									if (movedata.crouchup) {
										movedata.crouchup--;
									} else {
										movedata.crouchdown++;
									}

									g_Vars.currentplayer->aimtaptime = -1;
								}
							}

							if (optionsGetAimControl(g_Vars.currentplayerstats->mpindex) == AIMCONTROL_HOLD) {
								if (aimonhist[i]) {
									if (g_Vars.currentplayer->aimtaptime > -1) {
										g_Vars.currentplayer->aimtaptime++;
									}
								} else {
									if (g_Vars.currentplayer->aimtaptime > 0
											&& g_Vars.currentplayer->aimtaptime < TICKS(15)) {
										if (movedata.crouchdown) {
											movedata.crouchdown--;
										} else {
											movedata.crouchup++;
										}
									}

									g_Vars.currentplayer->aimtaptime = 0;
								}
							}
						}
					}

					// Handle shutting eyes in multiplayer
					if (bmoveGetCrouchPos() == CROUCHPOS_SQUAT
							&& g_Vars.currentplayer->crouchoffset == -90
							&& g_Vars.mplayerisrunning
							&& g_Vars.coopplayernum < 0) {
						/* M0.2: was joyGetStickY(contpad2) — use c2sticky which was already read via actionValue */
						movedata.eyesshut = g_Vars.currentplayer->insightaimmode
							&& !canmanualzoom
							&& c2sticky < -30;
					}

					if (bgunGetWeaponNum(HAND_RIGHT) == WEAPON_FARSIGHT) {
						if (g_Vars.currentplayer->insightaimmode) {
							movedata.unk14 = 0;
						}

						movedata.farsighttempautoseek = g_Vars.currentplayer->insightaimmode
							&& (c2stickx < -30 || c2stickx > 30);
					}

					movedata.rleanleft = false;
					movedata.rleanright = false;
					movedata.analoglean = 0.f;

					// Handle mine detonation
					if ((((c1buttons & A_BUTTON) && (c1buttonsthisframe & B_BUTTON))
								|| ((c1buttons & B_BUTTON) && (c1buttonsthisframe & A_BUTTON))
								|| ((c2buttons & A_BUTTON) && (c2buttonsthisframe & B_BUTTON))
								|| ((c2buttons & B_BUTTON) && (c2buttonsthisframe & A_BUTTON)))
							&& weaponnum == WEAPON_REMOTEMINE) {
						movedata.detonating = true;
						movedata.weaponbackoffset = 0;
						movedata.weaponforwardoffset = 0;
						movedata.btapcount = 0;
						g_Vars.currentplayer->invdowntime = -2;
						g_Vars.currentplayer->usedowntime = -2;
					}
				}

				movedata.aiming = g_Vars.currentplayer->insightaimmode;
				movedata.zooming = g_Vars.currentplayer->insightaimmode;

				if (g_Vars.currentplayer->waitforzrelease
						&& !((shootallowedbuttons & Z_TRIG) && actionHeld((s32)shootpad, ACTION_FIRE_PRIMARY))) {
					g_Vars.currentplayer->waitforzrelease = false;
				}

				if (weaponHasFlag(bgunGetWeaponNum(HAND_RIGHT), WEAPONFLAG_FIRETOACTIVATE)) {
					if (allowc1buttons
							&& (shootallowedbuttons & Z_TRIG) && actionPressed((s32)shootpad, ACTION_FIRE_PRIMARY)
							&& g_Vars.currentplayer->pausemode == PAUSEMODE_UNPAUSED) {
						movedata.btapcount++;
					}
				} else {
					movedata.triggeron = g_Vars.currentplayer->waitforzrelease == false
						&& allowc1buttons
						&& (shootallowedbuttons & Z_TRIG) && actionHeld((s32)shootpad, ACTION_FIRE_PRIMARY)
						&& g_Vars.currentplayer->pausemode == PAUSEMODE_UNPAUSED
						&& (c1buttons & A_BUTTON) == 0
						&& (c2buttons & A_BUTTON) == 0;
				}

				movedata.disablelookahead = true;
			} else {
				// 1.x or PC control style
				if (controlmode == CONTROLMODE_PC) {
					shootbuttons = Z_TRIG;
					aimbuttons = R_TRIG;
					invbuttons = A_BUTTON;
				} else if (controlmode == CONTROLMODE_13 || controlmode == CONTROLMODE_14) {
					shootbuttons = A_BUTTON;
					aimbuttons = Z_TRIG;
					invbuttons = L_TRIG | R_TRIG;
				} else {
					shootbuttons = Z_TRIG;
					aimbuttons = L_TRIG | R_TRIG;
					invbuttons = A_BUTTON;
				}

				if (controlmode == CONTROLMODE_PC) {
					/* Modern twin-stick: left stick = move, right stick = aim.
					 * analogstrafe/analogwalk keep their c1 (left stick) values from init.
					 * Override analogturn/analogpitch to c2 (right stick).
					 * unk14 gates analog strafing in bondwalk — must be true unconditionally
					 * so the left stick can drive movement without requiring right stick input. */
					movedata.analogturn = c2stickx;
					movedata.analogpitch = c2sticky;
					movedata.unk14 = true;
				}

				if (optionsGetAimControl(g_Vars.currentplayerstats->mpindex) == AIMCONTROL_HOLD) {
					for (i = 0; i < numsamples; i++) {
						/* M0.2: collapsed sub-frame to per-frame — uses synthesized c1buttons */
						aimonhist[i] = allowc1buttons && (c1buttons & (aimbuttons & c1allowedbuttons));
						aimoffhist[i] = !aimonhist[i];
					}

					if (numsamples > 0) {
						g_Vars.currentplayer->insightaimmode = aimonhist[numsamples - 1];
					}
				}

				if (!lvIsPaused()) {
					// Handle aiming
					if (optionsGetAimControl(g_Vars.currentplayerstats->mpindex) != AIMCONTROL_HOLD) {
						for (i = 0; i < numsamples; i++) {
							/* M0.2: collapsed sub-frame to per-frame — uses synthesized c1buttonsthisframe */
							if (allowc1buttons && (c1buttonsthisframe & (aimbuttons & c1allowedbuttons))) {
								g_Vars.currentplayer->insightaimmode = !g_Vars.currentplayer->insightaimmode;
							}

							aimonhist[i] = g_Vars.currentplayer->insightaimmode;
							aimoffhist[i] = !aimonhist[i];
						}
					}

					if (bgunGetWeaponNum(HAND_RIGHT) == WEAPON_HORIZONSCANNER) {
						g_Vars.currentplayer->insightaimmode = true;
					}

					movedata.canswivelgun = !g_Vars.currentplayer->insightaimmode;
					movedata.canmanualaim = g_Vars.currentplayer->insightaimmode;
					movedata.canautoaim = !g_Vars.currentplayer->insightaimmode;

					// On N64 control schemes the d-pad does the same thing as the C buttons
					u32 slmask, srmask, sumask, sdmask;
					if (controlmode == CONTROLMODE_PC) {
						sumask = U_CBUTTONS;
						sdmask = D_CBUTTONS;
						slmask = L_CBUTTONS;
						srmask = R_CBUTTONS;
					} else {
						sumask = U_JPAD | U_CBUTTONS;
						sdmask = D_JPAD | D_CBUTTONS;
						slmask = L_JPAD | L_CBUTTONS;
						srmask = R_JPAD | R_CBUTTONS;
					}

					if (controlmode == CONTROLMODE_12 || controlmode == CONTROLMODE_14 || controlmode == CONTROLMODE_PC) {
						// Handle side stepping
						if (g_Vars.currentplayer->insightaimmode == false) {
							if (allowc1buttons) {
								movedata.digitalstepleft = joyCountButtonsOnSpecificSamples(aimoffhist, contpad1, c1allowedbuttons & slmask);
								movedata.digitalstepright = joyCountButtonsOnSpecificSamples(aimoffhist, contpad1, c1allowedbuttons & srmask);
							}
						} else {
							// Modern ADS: A/D strafes while aiming rather than triggering lean
							if (controlmode == CONTROLMODE_PC && allowc1buttons) {
								movedata.digitalstepleft  = (c1buttons & slmask) ? numsamples : 0;
								movedata.digitalstepright = (c1buttons & srmask) ? numsamples : 0;
							} else
							{
								// This doesn't appear to be r-leaning.
								// R-leaning still works when these are commented.
								if (c1buttons & slmask) {
									movedata.unk30 = 1;
								}

								if (c1buttons & srmask) {
									movedata.unk34 = 1;
								}
							}
						}

						// Modern ADS: allow W/S movement while aiming in PC mode;
						// mouse still drives turn/pitch, speed penalty applied in bondwalk.
						if (controlmode == CONTROLMODE_PC && g_Vars.currentplayer->insightaimmode) {
							movedata.digitalstepforward = (c1buttons & sumask);
							movedata.digitalstepback    = (c1buttons & sdmask);
							movedata.canlookahead       = true;
							movedata.cannaturalpitch    = false;
							movedata.speedvertadown     = 0;
							movedata.speedvertaup       = 0;
							movedata.cannaturalturn     = false;
						} else
						{
							movedata.digitalstepforward = !g_Vars.currentplayer->insightaimmode && (c1buttons & sumask);
							movedata.digitalstepback = !g_Vars.currentplayer->insightaimmode && (c1buttons & sdmask);
							movedata.canlookahead = (controlmode == CONTROLMODE_PC) && !g_Vars.currentplayer->insightaimmode;
							movedata.cannaturalpitch = !g_Vars.currentplayer->insightaimmode;
							movedata.speedvertadown = 0;
							movedata.speedvertaup = 0;
							movedata.cannaturalturn = !g_Vars.currentplayer->insightaimmode;
						}

						if (controlmode == CONTROLMODE_PC) {
							if ((g_Vars.currentplayer->devicesactive & DEVICE_EYESPY) || g_Vars.currentplayer->visionmode > 1 || g_Vars.tickmode != 1) {
								movedata.analogturn = 0;
								movedata.analogpitch = 0;
								movedata.analogstrafe = 0;
								movedata.analogwalk = 0;
								movedata.analoglean = 0.f;
							}
							if (PLAYER_EXTCFG().mouseaimmode == MOUSEAIM_LOCKED || bgunGetWeaponNum(HAND_RIGHT) == WEAPON_HORIZONSCANNER) {
								movedata.cannaturalpitch = movedata.cannaturalpitch || (movedata.freelookdy != 0.0f);
								movedata.cannaturalturn = movedata.cannaturalturn  || (movedata.freelookdx != 0.0f);
							}
						}

						if (g_Vars.tickmode == TICKMODE_AUTOWALK) {
							movedata.digitalstepforward = (g_Vars.currentplayer->autocontrol_y > 0);
							movedata.digitalstepback = (g_Vars.currentplayer->autocontrol_y < 0);
							movedata.analogstrafe = 0;
							movedata.analogwalk = 0;
							movedata.analogturn = g_Vars.currentplayer->autocontrol_x;
							movedata.analogpitch = 0;
							movedata.freelookdx = 0.0f;
							movedata.freelookdy = 0.0f;
							movedata.analoglean = 0.f;
						}
					} else {
						// 1.1 or 1.3
						if (c1buttons & (L_JPAD | L_CBUTTONS)) {
							movedata.unk30 = 1;
						}

						if (c1buttons & (R_JPAD | R_CBUTTONS)) {
							movedata.unk34 = 1;
						}

						if (!g_Vars.currentplayer->insightaimmode && allowc1buttons) {
							movedata.digitalstepleft = joyCountButtonsOnSpecificSamples(aimoffhist, contpad1, c1allowedbuttons & (L_JPAD | L_CBUTTONS));
							movedata.digitalstepright = joyCountButtonsOnSpecificSamples(aimoffhist, contpad1, c1allowedbuttons & (R_JPAD | R_CBUTTONS));
						}

						movedata.digitalstepforward = false;
						movedata.digitalstepback = false;
						movedata.canlookahead = !g_Vars.currentplayer->insightaimmode;
						movedata.cannaturalpitch = false;

						// Looking up/down
						if (!g_Vars.currentplayer->insightaimmode && (c1buttons & (U_JPAD | U_CBUTTONS))) {
							movedata.speedvertadown = 1;
						}

						if (!g_Vars.currentplayer->insightaimmode && (c1buttons & (D_JPAD | D_CBUTTONS))) {
							movedata.speedvertaup = 1;
						}

						movedata.cannaturalturn = !g_Vars.currentplayer->insightaimmode;
						movedata.unk14 = 0;

						if (g_Vars.tickmode == TICKMODE_AUTOWALK) {
							movedata.analogstrafe = 0;
							movedata.analogwalk = g_Vars.currentplayer->autocontrol_y;
							movedata.analogturn = g_Vars.currentplayer->autocontrol_x;
							movedata.analogpitch = 0;
							movedata.freelookdx = 0.0f;
							movedata.freelookdy = 0.0f;
							movedata.analoglean = 0.f;
						}
					}

					/* PC twin-stick: RS aims in ADS; do not map LS to aim turn / edge speeds. */
					if (controlmode != CONTROLMODE_PC) {
						// Handle looking up/down while aiming
						if (g_Vars.currentplayer->insightaimmode && movedata.c1stickyraw > 60) {
							movedata.speedvertadown = (movedata.c1stickyraw - 60) / 10.0f;

							if (movedata.speedvertadown > 1) {
								movedata.speedvertadown = 1;
							}
						} else if (g_Vars.currentplayer->insightaimmode && movedata.c1stickyraw < -60) {
							movedata.speedvertaup = (-60 - movedata.c1stickyraw) / 10.0f;

							if (movedata.speedvertaup > 1) {
								movedata.speedvertaup = 1;
							}
						}

						// Handle looking left/right while aiming
						if (g_Vars.currentplayer->insightaimmode && movedata.c1stickxraw < -60) {
							movedata.aimturnleftspeed = (-60 - movedata.c1stickxraw) / 10.0f;

							if (movedata.aimturnleftspeed > 1) {
								movedata.aimturnleftspeed = 1;
							}
						} else if (g_Vars.currentplayer->insightaimmode && movedata.c1stickxraw > 60) {
							movedata.aimturnrightspeed = (movedata.c1stickxraw - 60) / 10.0f;

							if (movedata.aimturnrightspeed > 1) {
								movedata.aimturnrightspeed = 1;
							}
						}
					}

					// Handle turning and looking up/down via mouselook when aiming
					if (g_Vars.currentplayer->insightaimmode && allowmcross && bgunGetWeaponNum(HAND_RIGHT) != WEAPON_HORIZONSCANNER) {
						if (g_Vars.currentplayer->swivelpos[0] > 0.9f) {
							movedata.aimturnrightspeed = (g_Vars.currentplayer->swivelpos[0] - 0.9f) / 0.1f;
							movedata.aimturnleftspeed = 0.f;
						} else if (g_Vars.currentplayer->swivelpos[0] < -0.9f) {
							movedata.aimturnleftspeed = (g_Vars.currentplayer->swivelpos[0] - -0.9f) / -0.1f;
							movedata.aimturnrightspeed = 0.f;
						}
						f32 vertaup = 0.f, vertadown = 0.f;
						if (g_Vars.currentplayer->swivelpos[1] > 0.9f) {
							vertaup = (g_Vars.currentplayer->swivelpos[1] - 0.9f) / 0.1f;
						} else if (g_Vars.currentplayer->swivelpos[1] < -0.9f) {
							vertadown = (g_Vars.currentplayer->swivelpos[1] - -0.9f) / -0.1f;
						}
						// Uninvert pitch if needed
						if (movedata.invertpitch) {
							movedata.speedvertaup = vertadown;
							movedata.speedvertadown = vertaup;
						} else {
							movedata.speedvertaup = vertaup;
							movedata.speedvertadown = vertadown;
						}
					} else {
						// Reset mouse aim position when not mouse aiming
						g_Vars.currentplayer->swivelpos[0] = 0.f;
						g_Vars.currentplayer->swivelpos[1] = 0.f;
					}

					// Handle A button
					if (allowc1buttons) {
						if (g_Vars.currentplayer->invdowntime < -2) {
							g_Vars.currentplayer->invdowntime += numsamples;

							if (g_Vars.currentplayer->invdowntime > -3) {
								g_Vars.currentplayer->invdowntime = 0;
							}
						} else {
							for (i = 0; i < numsamples; i++) {
								if (controlmode == CONTROLMODE_PC) {
									/* M0.2: collapsed sub-frame to per-frame — uses synthesized c1buttonsthisframe */
									if (i == 0) {
										if (bmoveHandleDirectWeaponSelect((s32)contpad1)) {
											g_Vars.currentplayer->invdowntime = -1;
										} else if ((c1allowedbuttons & BUTTON_WPNFORWARD)
												&& actionWasTap((s32)contpad1, ACTION_WEAPON_NEXT, BOND_TAP_HOLD_THRESH_MS)) {
											movedata.weaponforwardoffset++;
											g_Vars.currentplayer->invdowntime = -1;
										} else if ((c1allowedbuttons & BUTTON_WPNBACK)
												&& (actionPressed((s32)contpad1, ACTION_WEAPON_PREV)
													|| (c1buttonsthisframe & (c1allowedbuttons & BUTTON_WPNBACK)))) {
											movedata.weaponbackoffset++;
											g_Vars.currentplayer->invdowntime = -1;
										}
									}
									continue;
								}
								/* M0.2: collapsed sub-frame to per-frame — uses synthesized c1buttons/c1buttonsthisframe */
								if (c1buttons & (invbuttons & c1allowedbuttons)) {
									if (g_Vars.currentplayer->invdowntime > -2) {
										if (c1buttonsthisframe & (shootbuttons & c1allowedbuttons)) {
											movedata.weaponbackoffset++;
											g_Vars.currentplayer->invdowntime = -1;
										}

										if (g_Vars.currentplayer->invdowntime >= 0 && !(c1buttons & (shootbuttons & c1allowedbuttons))) {
											// Holding A and haven't pressed Z
											if (g_Vars.currentplayer->invdowntime > TICKS(15)) {
												amOpen();
												g_Vars.currentplayer->invdowntime = -1;
											} else {
												g_Vars.currentplayer->invdowntime += g_Vars.lvupdate60;
											}
										}
									}
								} else {
									// Wasn't holding A on this sample
									if (g_Vars.currentplayer->invdowntime > 0 &&
											(!allowc1buttons || !(c1buttons & (shootbuttons & c1allowedbuttons)))) {
										// But was on previous sample, so cycle weapon
										movedata.weaponforwardoffset++;
									}

									g_Vars.currentplayer->invdowntime = 0;
								}
							}
						}
					}

					// Handle B and use-like button
					/* PC mode: only BUTTON_ACCEPT_USE (= A_BUTTON = ACTION_USE) opens doors.
					 * BUTTON_CANCEL_USE == B_BUTTON (constants.h) — including either in this
					 * mask would trigger door-open on B-press (cancel/back). N64 mode keeps
					 * B_BUTTON as the legacy "use" trigger synthesized from ACTION_CANCEL_USE. */
					const u32 usemask = (controlmode == CONTROLMODE_PC) ?
						BUTTON_ACCEPT_USE :
						B_BUTTON;
					if (allowc1buttons) {
						for (i = 0; i < numsamples; i++) {
							/* M0.2: collapsed sub-frame to per-frame — uses synthesized c1buttons */
							if (c1buttons & (c1allowedbuttons & usemask)) {
								if (g_Vars.currentplayer->usedowntime >= -1) {
									if (controlmode < CONTROLMODE_PC) {
										if ((c1buttonsthisframe & (shootbuttons & c1allowedbuttons))
												&& g_Vars.currentplayer->usedowntime >= 0
												&& bgunConsiderToggleGunFunction(g_Vars.currentplayer->usedowntime, true, false, 0) != USETIMER_CONTINUE) {
											g_Vars.currentplayer->usedowntime = -3;
										}
									}

									if (g_Vars.currentplayer->usedowntime >= 0) {
										if (g_Vars.currentplayer->usedowntime > TICKS(25)) {
											s32 result = (controlmode == CONTROLMODE_PC) ?
												USETIMER_CONTINUE :
												bgunConsiderToggleGunFunction(g_Vars.currentplayer->usedowntime, false, false, 0);
											if (result == USETIMER_STOP) {
												g_Vars.currentplayer->usedowntime = -1;
											} else if (result == USETIMER_REPEAT) {
												g_Vars.currentplayer->usedowntime = -2;
											} else {
												g_Vars.currentplayer->usedowntime++;
											}
										} else {
											g_Vars.currentplayer->usedowntime++;
										}
									}
								} else {
									if ((controlmode < CONTROLMODE_PC) && g_Vars.currentplayer->usedowntime >= -2) {
										bgunConsiderToggleGunFunction(g_Vars.currentplayer->usedowntime, false, false, 0);
									}
								}
							} else {
								// Released B
								if (g_Vars.currentplayer->usedowntime > 0) {
									movedata.btapcount++;
								}

								g_Vars.currentplayer->usedowntime = 0;
								bgun0f0a8c50();
							}
						}
					}

					if (controlmode == CONTROLMODE_PC && allowc1buttons) {
						// handle L button : alt switching
						for (i = 0; i < numsamples; i++) {
							bgunProcessInputAltButton(&movedata, contpad1, i);
						}

						// Handle ALT1 / MI Reload Hack
						for (i = 0; i < numsamples; i++) {
							/* M0.2: collapsed sub-frame to per-frame — uses synthesized c1buttons */
							if (c1buttons & (c1allowedbuttons & BUTTON_RELOAD)) {
								movedata.alt1tapcount++;
							}
						}

						// Handle radial menu (D-Down)
						for (i = 0; i < numsamples; i++) {
							/* M0.2: collapsed sub-frame to per-frame — uses synthesized c1buttons/c1buttonsthisframe */
							if (c1buttons & (c1allowedbuttons & BUTTON_RADIAL)) {
								if (g_Vars.currentplayer->amdowntime < -2) {
									g_Vars.currentplayer->amdowntime += numsamples;

									if (g_Vars.currentplayer->amdowntime > -3) {
										g_Vars.currentplayer->amdowntime = 0;
									}
								} else {
									if (g_Vars.currentplayer->amdowntime >= 0) {
										if (c1buttonsthisframe & (c1allowedbuttons & BUTTON_RADIAL)) {
											amOpen();
											g_Vars.currentplayer->amdowntime = -1;
										} else {
											g_Vars.currentplayer->amdowntime++;
										}
									}
								}
							} else {
								g_Vars.currentplayer->amdowntime = 0;
							}
						}

						// Handle xbla-style crouch cycling
						const s32 oldcrouchpos = g_Vars.currentplayer->crouchpos;
						for (i = 0; i < numsamples; i++) {
							// handle 1964GEPD style crouch setting
							s32 crouchsample;
							if (PLAYER_EXTCFG().crouchmode & CROUCHMODE_TOGGLE) {
								// press to toggle crouch position
								/* M0.2: collapsed sub-frame to per-frame — uses synthesized c1buttonsthisframe */
								crouchsample = c1buttonsthisframe & BUTTON_CROUCH_CYCLE;
								if (crouchsample) {
									if (g_Vars.currentplayer->crouchpos <= 0) {
										g_Vars.currentplayer->crouchpos = CROUCHPOS_STAND;
									} else {
										g_Vars.currentplayer->crouchpos--;
									}
								}
								crouchsample = c1buttonsthisframe & ((c1allowedbuttons & ~BUTTON_JUMP) & BUTTON_HALF_CROUCH);
								if (crouchsample) {
									if (g_Vars.currentplayer->crouchpos == CROUCHPOS_DUCK) {
										g_Vars.currentplayer->crouchpos = CROUCHPOS_STAND;
									} else {
										g_Vars.currentplayer->crouchpos = CROUCHPOS_DUCK;
									}
								}
								crouchsample = c1buttonsthisframe & (c1allowedbuttons & BUTTON_FULL_CROUCH);
								if (crouchsample) {
									if (g_Vars.currentplayer->crouchpos == CROUCHPOS_SQUAT) {
										g_Vars.currentplayer->crouchpos = CROUCHPOS_STAND;
									} else {
										g_Vars.currentplayer->crouchpos = CROUCHPOS_SQUAT;
									}
								}
							} else if (PLAYER_EXTCFG().crouchmode == CROUCHMODE_HOLD) {
								// hold to crouch
								/* M0.2: collapsed sub-frame to per-frame — uses synthesized c1buttons */
								crouchsample = c1buttons & ((c1allowedbuttons & ~BUTTON_JUMP) & (BUTTON_FULL_CROUCH | BUTTON_HALF_CROUCH));
								if (!crouchsample) {
									g_Vars.currentplayer->crouchpos = CROUCHPOS_STAND;
								} else if (crouchsample & BUTTON_FULL_CROUCH) {
									g_Vars.currentplayer->crouchpos = CROUCHPOS_SQUAT;
								} else if (crouchsample & BUTTON_HALF_CROUCH) {
									g_Vars.currentplayer->crouchpos = CROUCHPOS_DUCK;
								}
							}
						}
						// prevent uncrouching if we don't fit
						while (g_Vars.currentplayer->crouchpos > oldcrouchpos && !bwalkCanUncrouch()) {
							g_Vars.currentplayer->crouchpos--;
						}
					}

					// Handle jump input — no isfalling guard here; bwalkUpdateVertical
					// enforces the grounded check so we just need the button to be held.
					if (controlmode == CONTROLMODE_PC && allowc1buttons) {
						bool jumpButtonHeld = false;
						for (i = 0; i < numsamples; i++) {
							/* M0.2: collapsed sub-frame to per-frame — uses synthesized c1buttonsthisframe */
							if (c1buttonsthisframe & (c1allowedbuttons & BUTTON_JUMP)) {
								jumpButtonHeld = true;
								break;
							}
						}

						if (jumpButtonHeld && !g_Vars.currentplayer->jumpconsumed) {
							g_Vars.currentplayer->wantsjump = true;
						} else if (!jumpButtonHeld) {
							// Button released — clear consumed flag so next press works.
							// This is critical for local (non-networked) play where
							// bmoveProcessRemoteInput doesn't run to clear the flag.
							g_Vars.currentplayer->jumpconsumed = false;
						}
					}

					// Handle manual zoom in and out (sniper, farsight and horizon scanner)
					if (canmanualzoom && g_Vars.currentplayer->insightaimmode) {
						increment = 1;
						zoomout = c1buttons & sdmask;
						zoomin = c1buttons & sumask;

						// @bug? Should this be HAND_RIGHT?
						if (bgunGetWeaponNum(HAND_LEFT) == WEAPON_FARSIGHT) {
							increment = 0.5f;
						}

						if (zoomout) {
							movedata.zoomoutfovpersec = increment;
						}

						if (zoomin) {
							movedata.zoominfovpersec = increment;
						}

						if (controlmode == CONTROLMODE_PC) {
							if (c2sticky < 0) {
								movedata.zoomoutfovpersec = -c2sticky / 70.0f;

								if (movedata.zoomoutfovpersec > 1) {
									movedata.zoomoutfovpersec = 1;
								}

								movedata.zoomoutfovpersec = movedata.zoomoutfovpersec + movedata.zoomoutfovpersec;
							}
							if (c2sticky > 0) {
								movedata.zoominfovpersec = c2sticky / 70.0f;

								if (movedata.zoominfovpersec > 1) {
									movedata.zoominfovpersec = 1;
								}

								movedata.zoominfovpersec = movedata.zoominfovpersec + movedata.zoominfovpersec;
							}
						}
					}

					// Handle C-button and analog crouch and uncrouch, if enabled
					if (allowc1buttons && (controlmode < CONTROLMODE_PC || (PLAYER_EXTCFG().crouchmode & CROUCHMODE_ANALOG))) {
						for (i = 0; i < numsamples; i++) {
							if (!canmanualzoom && aimonhist[i]) {
								/* M0.2: collapsed sub-frame to per-frame — uses synthesized c1buttonsthisframe + actionValue for rstick */
								bool goUp = (c1buttonsthisframe & (c1allowedbuttons & sumask)) != 0;
								if (controlmode == CONTROLMODE_PC) {
									goUp = goUp || (c2sticky > 30 && actionPressed((s32)contpad1, ACTION_CBUTTON_UP));
								}
								if (goUp) {
									if (movedata.crouchdown) {
										movedata.crouchdown--;
									} else {
										movedata.crouchup++;
									}

									g_Vars.currentplayer->aimtaptime = -1;
								}

								bool goDn = (c1buttonsthisframe & (c1allowedbuttons & sdmask)) != 0;
								if (controlmode == CONTROLMODE_PC) {
									goDn = goDn || (c2sticky < -30 && actionPressed((s32)contpad1, ACTION_CBUTTON_DOWN));
								}
								if (goDn) {
									if (movedata.crouchup) {
										movedata.crouchup--;
									} else {
										movedata.crouchdown++;
									}

									g_Vars.currentplayer->aimtaptime = -1;
								}
							}

							if (optionsGetAimControl(g_Vars.currentplayerstats->mpindex) == AIMCONTROL_HOLD) {
								if (aimonhist[i]) {
									if (g_Vars.currentplayer->aimtaptime >= 0) {
										g_Vars.currentplayer->aimtaptime++;
									}
								} else {
									// Released aim
									if (g_Vars.currentplayer->aimtaptime > 0 && g_Vars.currentplayer->aimtaptime < TICKS(15)) {
										// Was only a tap, so uncrouch
										if (movedata.crouchdown) {
											movedata.crouchdown--;
										} else {
											movedata.crouchup++;
										}
									}

									g_Vars.currentplayer->aimtaptime = 0;
								}
							}
						}
					}

					// Handle shutting eyes in multiplayer
					if (bmoveGetCrouchPos() == CROUCHPOS_SQUAT
							&& g_Vars.currentplayer->crouchoffset == -90
							&& g_Vars.mplayerisrunning
							&& g_Vars.coopplayernum <= -1) {
						movedata.eyesshut = g_Vars.currentplayer->insightaimmode
							&& !canmanualzoom
							&& (c1buttons & (c1allowedbuttons & sdmask)); /* M0.2: was joyGetButtons, now uses synthesized c1buttons */
					}

					if (bgunGetWeaponNum(HAND_RIGHT) == WEAPON_FARSIGHT) {
						movedata.farsighttempautoseek = g_Vars.currentplayer->insightaimmode && (c1buttons & (srmask | slmask));
						if (controlmode == CONTROLMODE_PC && g_Vars.currentplayer->insightaimmode) {
								movedata.unk14 = 1;
								movedata.analogstrafe = movedata.c1stickxsafe;
						}
					} else {
						movedata.rleanleft = g_Vars.currentplayer->insightaimmode && (c1buttons & slmask);
						movedata.rleanright = g_Vars.currentplayer->insightaimmode && (c1buttons & srmask);
						if (controlmode == CONTROLMODE_PC && g_Vars.currentplayer->insightaimmode) {
							movedata.analoglean = c2stickx / 127.f;
						}
					}

					// Handle mine detonation
					if (controlmode < CONTROLMODE_PC) {
						if ((((c1buttons & invbuttons) && (c1buttonsthisframe & B_BUTTON))
								|| ((c1buttons & B_BUTTON) && (c1buttonsthisframe & invbuttons)))
								&& weaponnum == WEAPON_REMOTEMINE) {
							movedata.detonating = true;
							movedata.weaponbackoffset = 0;
							movedata.weaponforwardoffset = 0;
							movedata.btapcount = 0;
							g_Vars.currentplayer->invdowntime = -2;
							g_Vars.currentplayer->usedowntime = -2;
						}
					} else {
						bgunProcessQuickDetonate(&movedata, c1buttons, c1buttonsthisframe, (BUTTON_CANCEL_USE | BUTTON_ACCEPT_USE), (BUTTON_WPNBACK | BUTTON_RADIAL | BUTTON_RELOAD));
					}
				}

				movedata.aiming = g_Vars.currentplayer->insightaimmode;
				movedata.zooming = g_Vars.currentplayer->insightaimmode;

				if (g_Vars.currentplayer->waitforzrelease
						&& (c1buttons & shootbuttons) == 0) {
					g_Vars.currentplayer->waitforzrelease = false;
				}

				if (weaponHasFlag(bgunGetWeaponNum(HAND_RIGHT), WEAPONFLAG_FIRETOACTIVATE)) {
					if ((c1buttonsthisframe & shootbuttons)
							&& g_Vars.currentplayer->pausemode == PAUSEMODE_UNPAUSED) {
						movedata.btapcount++;
					}
				} else {
					movedata.triggeron = g_Vars.currentplayer->waitforzrelease == false
						&& (c1buttons & shootbuttons)
						&& g_Vars.currentplayer->pausemode == PAUSEMODE_UNPAUSED;
					if (controlmode < CONTROLMODE_PC) {
						movedata.triggeron = movedata.triggeron && ((c1buttons & invbuttons) == 0);
					}
				}

				if (controlmode == CONTROLMODE_12 || controlmode == CONTROLMODE_14 || controlmode == CONTROLMODE_PC) {
					movedata.disablelookahead = true;
				}
			} // end 1.x
		}
	}

	g_Vars.currentplayer->bondactivateorreload = 0;

	g_Vars.currentplayer->pcinteractusekind = 0;

	s32 usereloads = (controlmode != CONTROLMODE_PC);
	usereloads = usereloads || PLAYER_EXTCFG().usereloads;
	/* ACTION_VEHICLE_USE is an optional direct on-foot activation binding.
	 * It feeds the same authoritative activation path as a completed
	 * ACTION_USE hold, so mounting a hoverbike still goes through
	 * currentPlayerTryMountHoverbike and all ordinary range/collision gates.
	 * While mounted, ACTION_VEHICLE_EXIT owns dismount instead. */
	if (controlmode == CONTROLMODE_PC
			&& g_Vars.currentplayer->bondmovemode != MOVEMODE_BIKE
			&& actionPressed(actionPlayer, ACTION_VEHICLE_USE)) {
		g_Vars.currentplayer->activatetimelast = g_Vars.currentplayer->activatetimethis;
		g_Vars.currentplayer->activatetimethis = g_Vars.lvframe60;
		g_Vars.currentplayer->pcinteractusekind = 2;
		g_Vars.currentplayer->bondactivateorreload |= JO_ACTION_ACTIVATE;
		bmoveHandleActivate();
	}
	if (controlmode == CONTROLMODE_PC && movedata.alt1tapcount) {
		/* PC: X_BUTTON here is ACTION_RELOAD (R) or long-USE no-prompt release — not USE tap. */
		g_Vars.currentplayer->bondactivateorreload |= JO_ACTION_RELOAD;
	}
	if (movedata.btapcount) {
		g_Vars.currentplayer->activatetimelast = g_Vars.currentplayer->activatetimethis;
		g_Vars.currentplayer->activatetimethis = g_Vars.lvframe60;
		s32 holdFire = (controlmode == CONTROLMODE_PC) &&
			actionHoldConsumed(actionPlayer, ACTION_USE);
		if (controlmode == CONTROLMODE_PC) {
			/* B-221.3 (CoWork): btapcount fires on frame 0 of USE (~16ms), so
			 * pcinteractusekind would always start as tap and lv.c would dispatch
			 * JO_ACTION_ACTIVATE before the hold threshold. Defer ACTIVATE until
			 * hold consumes (here) or USE release (block after this). */
			g_Vars.currentplayer->pcinteractusekind = holdFire ? 2 : 1;
			if (holdFire) {
				g_Vars.currentplayer->bondactivateorreload |= JO_ACTION_ACTIVATE;
				bmoveHandleActivate();
			}
		} else if (holdFire || !usereloads) {
			g_Vars.currentplayer->bondactivateorreload |= JO_ACTION_ACTIVATE;
		} else {
			g_Vars.currentplayer->bondactivateorreload |= JO_ACTION_ACTIVATE | JO_ACTION_RELOAD;
		}

		if (controlmode != CONTROLMODE_PC) {
			bmoveHandleActivate();
		}
	}

	/* PC ACTION_USE tap is reload-only. Interaction dispatch is owned by the
	 * hold-threshold branch above so a quick tap cannot activate props. */

	if (!movedata.invertpitch) {
		savedverta = movedata.speedvertadown;
		movedata.analogpitch = -movedata.analogpitch;
		movedata.c1stickyraw = -movedata.c1stickyraw;
		movedata.speedvertadown = movedata.speedvertaup;
		movedata.speedvertaup = savedverta;
	}

	/* PC: ADS uses separate sensitivity (SensAdsUi) vs hip look; slight movement slowdown. */
	if (controlmode == CONTROLMODE_PC && g_Vars.currentplayer->insightaimmode) {
		f32 hip = actionmapGetStickSensitivityAim();
		f32 ads = actionmapSensUiToMult(actionmapGetSensAdsUi());
		if (hip > 0.0001f) {
			f32 ratio = ads / hip;
			s32 nt = (s32)((f32)movedata.analogturn * ratio + (ratio >= 0.f ? 0.5f : -0.5f));
			s32 np = (s32)((f32)movedata.analogpitch * ratio + (ratio >= 0.f ? 0.5f : -0.5f));
			if (nt < -127) {
				nt = -127;
			}
			if (nt > 127) {
				nt = 127;
			}
			if (np < -127) {
				np = -127;
			}
			if (np > 127) {
				np = 127;
			}
			movedata.analogturn = nt;
			movedata.analogpitch = np;
		}
		{
			s32 ns = (s32)((f32)movedata.analogstrafe * 0.88f + (movedata.analogstrafe >= 0 ? 0.5f : -0.5f));
			s32 nw = (s32)((f32)movedata.analogwalk * 0.88f + (movedata.analogwalk >= 0 ? 0.5f : -0.5f));
			if (ns < -127) {
				ns = -127;
			}
			if (ns > 127) {
				ns = 127;
			}
			if (nw < -127) {
				nw = -127;
			}
			if (nw > 127) {
				nw = 127;
			}
			movedata.analogstrafe = ns;
			movedata.analogwalk = nw;
		}
	}

	bgunTickGameplay(movedata.triggeron);

	if (g_Vars.bondvisible && (bgunIsFiring(HAND_RIGHT) || bgunIsFiring(HAND_LEFT))) {
		noiseradius = 0;

		if (bgunIsFiring(HAND_RIGHT) && bgunGetNoiseRadius(HAND_RIGHT) > noiseradius) {
			noiseradius = bgunGetNoiseRadius(HAND_RIGHT);
		}

		if (bgunIsFiring(HAND_LEFT) && bgunGetNoiseRadius(HAND_LEFT) > noiseradius) {
			noiseradius = bgunGetNoiseRadius(HAND_LEFT);
		}

		chrsCheckForNoise(noiseradius);
	}

	bgunSetSightVisible(GUNSIGHTREASON_NOTAIMING, movedata.aiming);

	if (movedata.zoomoutfovpersec > 0) {
		currentPlayerZoomOut(movedata.zoomoutfovpersec);
	}

	if (movedata.zoominfovpersec > 0) {
		currentPlayerZoomIn(movedata.zoominfovpersec);
	}

	if (g_Vars.currentplayer->pausemode == PAUSEMODE_UNPAUSED && !g_MainIsEndscreen) {
		zoomfov = PLAYER_DEFAULT_FOV;

		// FarSight in secondary function
		if (bgunGetWeaponNum(HAND_RIGHT) == WEAPON_FARSIGHT
				&& g_Vars.currentplayer->insightaimmode
				&& (movedata.farsighttempautoseek || g_Vars.currentplayer->hands[HAND_RIGHT].gset.weaponfunc == FUNC_SECONDARY)
				&& g_Vars.currentplayer->autoeraserdist > 0) {
			eraserfov = cam0f0b49b8(500.0f / g_Vars.currentplayer->autoeraserdist);

			if (eraserfov > PLAYER_DEFAULT_FOV) {
				eraserfov = PLAYER_DEFAULT_FOV;
			}

			if (eraserfov < ADJUST_ZOOM_FOV(2)) {
				eraserfov = ADJUST_ZOOM_FOV(2);
			}

			g_Vars.currentplayer->gunzoomfovs[1] = eraserfov;

			mtx4TransformVec(camGetWorldToScreenMtxf(), &g_Vars.currentplayer->autoerasertarget->pos, &spa0);

			cam0f0b4eb8(&spa0, crosspos, eraserfov, g_Vars.currentplayer->c_perspaspect);

			if (crosspos[0] < (camGetScreenLeft() + camGetScreenWidth() * 0.5f) - 20.0f) {
				movedata.aimturnleftspeed = 0.25f;
			} else if (crosspos[0] > camGetScreenLeft() + camGetScreenWidth() * 0.5f + 20.0f) {
				movedata.aimturnrightspeed = 0.25f;
			}

			if (crosspos[1] < (camGetScreenTop() + camGetScreenHeight() * 0.5f) - 20.0f) {
				movedata.speedvertaup = 0.25f;
			} else if (crosspos[1] > camGetScreenTop() + camGetScreenHeight() * 0.5f + 20.0f) {
				movedata.speedvertadown = 0.25f;
			}
		}

		if (movedata.zooming) {
			zoomfov = currentPlayerGetGunZoomFov();
			if (controlmode == CONTROLMODE_PC) {
				zoomfov *= actionmapGetPcAdsZoomFovMul();
			}
		}

		if (bgunGetWeaponNum(HAND_RIGHT) == WEAPON_AR34
				&& g_Vars.currentplayer->hands[HAND_RIGHT].gset.weaponfunc == FUNC_SECONDARY) {
			zoomfov = currentPlayerGetGunZoomFov();
		}

		if (zoomfov <= 0) {
			zoomfov = PLAYER_DEFAULT_FOV;
		}

		playerTweenFovY(zoomfov);
		playerUpdateZoom();
	}

	bmoveApplyMoveData(&movedata);

	// Speed boost
	// After 3 seconds of holding forward at max speed, apply boost multiplier.
	// The multiplier starts at 1 and reaches 1.25 after about 0.1 seconds.
	if (g_Vars.currentplayer->speedmaxtime60 >= TICKS(180)) {
		if (g_Vars.currentplayer->speedboost < 1.25f) {
			g_Vars.currentplayer->speedboost += 0.01f * g_Vars.lvupdate60freal;
		}

		if (g_Vars.currentplayer->speedboost > 1.25f) {
#if PIRACYCHECKS
			piracyRestore();
#endif
			g_Vars.currentplayer->speedboost = 1.25f;
		}
	} else {
		if (g_Vars.currentplayer->speedboost > 1) {
			g_Vars.currentplayer->speedboost -= 0.01f * g_Vars.lvupdate60freal;
		}

		if (g_Vars.currentplayer->speedboost < 1) {
			g_Vars.currentplayer->speedboost = 1;
		}
	}

	// Look ahead
	if (g_Vars.currentplayer->pausemode == PAUSEMODE_UNPAUSED) {
		lookahead = -4;

		offbike = g_Vars.currentplayer->bondmovemode == MOVEMODE_WALK
			|| g_Vars.currentplayer->bondmovemode == MOVEMODE_GRAB;

		if (g_Vars.currentplayer->lookaheadcentreenabled) {
			if (g_Vars.lvframenum != g_Vars.currentplayer->lookaheadframe
					&& g_Vars.currentplayernum == (g_Vars.lvframenum & 3)) {
				g_Vars.currentplayer->cachedlookahead = bmoveCalculateLookahead();
			}

			lookahead = g_Vars.currentplayer->cachedlookahead;
		}

		if (g_Vars.currentplayer->movecentrerelease
				&& movedata.analogwalk < 40 && movedata.analogwalk > -40) {
			g_Vars.currentplayer->movecentrerelease = false;
		}

		if (offbike) {
			if (movedata.speedvertadown > 0 || movedata.speedvertaup > 0) {
				g_Vars.currentplayer->docentreupdown = false;
				g_Vars.currentplayer->prevupdown = true;
				g_Vars.currentplayer->automovecentre = false;
			} else {
				if (movedata.disablelookahead) {
					g_Vars.currentplayer->automovecentre = false;
				} else if (g_Vars.currentplayer->automovecentreenabled) {
					if (movedata.canlookahead && (movedata.analogwalk > 60 || movedata.analogwalk < -60)) {
						g_Vars.currentplayer->automovecentre = true;
					}

					if (g_Vars.currentplayer->automovecentre
							&& (g_Vars.currentplayer->vv_verta > lookahead + 5.0f || g_Vars.currentplayer->vv_verta < lookahead + -10.0f)
							&& g_Vars.currentplayer->movecentrerelease == false) {
						g_Vars.currentplayer->docentreupdown = true;
					}
				} else if (g_Vars.currentplayer->fastmovecentreenabled
						&& movedata.canlookahead
						&& (movedata.analogwalk > 60 || movedata.analogwalk < -60)
						&& (g_Vars.currentplayer->vv_verta > lookahead + 5.0f || g_Vars.currentplayer->vv_verta < lookahead + -10.0f)
						&& g_Vars.currentplayer->movecentrerelease == false) {
					g_Vars.currentplayer->docentreupdown = true;
				}

				g_Vars.currentplayer->prevupdown = false;
			}
		}

#if VERSION >= VERSION_NTSC_1_0
		if (g_Vars.currentplayer->bondmovemode == MOVEMODE_BIKE) {
			g_Vars.currentplayer->docentreupdown = false;
		}
#endif

		if (g_Vars.currentplayer->docentreupdown) {
			if (offbike) {
				// Determine direction for lookahead increment
				increment2 = (g_Vars.currentplayer->speedverta * g_Vars.currentplayer->speedverta * 0.5f) / 0.05f;

				if (g_Vars.currentplayer->vv_verta > lookahead + increment2) {
					bmoveUpdateSpeedVerta(1);
				} else if (g_Vars.currentplayer->vv_verta < lookahead - increment2) {
					bmoveUpdateSpeedVerta(-1);
				} else {
					bmoveUpdateSpeedVerta(0);
				}

				// Calculate new verta
				newverta = g_Vars.currentplayer->vv_verta + (g_Vars.currentplayer->speedverta * g_Vars.lvupdate60freal + g_Vars.currentplayer->speedverta * g_Vars.lvupdate60freal);

				if (g_Vars.currentplayer->vv_verta > lookahead && newverta > lookahead) {
					g_Vars.currentplayer->vv_verta = newverta;
				} else if (g_Vars.currentplayer->vv_verta < lookahead && newverta < lookahead) {
					g_Vars.currentplayer->vv_verta = newverta;
				} else {
					g_Vars.currentplayer->vv_verta = lookahead;
					g_Vars.currentplayer->speedverta = 0;

					if (g_Vars.currentplayer->prevupdown == false) {
						g_Vars.currentplayer->docentreupdown = false;
					}
				}
			}
		} else {
			if (movedata.cannaturalpitch) {
				tmp = viGetFovY() / 60.0f;
				fVar25 = movedata.analogpitch / 70.0f;

				if (fVar25 > 1) {
					fVar25 = 1;
				} else if (fVar25 < -1) {
					fVar25 = -1;
				}

				if (fVar25 >= 0) {
					fVar25 *= fVar25;
				} else {
					fVar25 *= -fVar25;
				}

				fVar25 += movedata.freelookdy * mlookscale;

				g_Vars.currentplayer->speedverta = -fVar25 * tmp;
			} else if (movedata.speedvertadown > 0) {
				bmoveUpdateSpeedVerta(movedata.speedvertadown);

				if (movedata.canlookahead && (movedata.analogwalk > 60 || movedata.analogwalk < -60)) {
					g_Vars.currentplayer->movecentrerelease = true;
				}
			} else if (movedata.speedvertaup > 0) {
				bmoveUpdateSpeedVerta(-movedata.speedvertaup);

				if (movedata.canlookahead && (movedata.analogwalk > 60 || movedata.analogwalk < -60)) {
					g_Vars.currentplayer->movecentrerelease = true;
				}
			} else {
				bmoveUpdateSpeedVerta(0);
			}

			g_Vars.currentplayer->vv_verta += g_Vars.currentplayer->speedverta * g_Vars.lvupdate60freal * 3.5f;
		}
	}

	if (movedata.cannaturalturn) {
		tmp = viGetFovY() / 60.0f;
		fVar25 = movedata.analogturn / 70.0f;

		if (fVar25 > 1) {
			fVar25 = 1;
		} else if (fVar25 < -1) {
			fVar25 = -1;
		}

		if (fVar25 >= 0) {
			fVar25 *= fVar25;
		} else {
			fVar25 *= -fVar25;
		}

		fVar25 += movedata.freelookdx * mlookscale;

		g_Vars.currentplayer->speedthetacontrol = fVar25 * tmp;
	} else if (movedata.aimturnleftspeed > 0) {
		bmoveUpdateSpeedThetaControl(movedata.aimturnleftspeed);
	} else if (movedata.aimturnrightspeed > 0) {
		bmoveUpdateSpeedThetaControl(-movedata.aimturnrightspeed);
	} else {
		bmoveUpdateSpeedThetaControl(0);
	}

	g_Vars.currentplayer->speedtheta = g_Vars.currentplayer->speedthetacontrol;
	bmoveUpdateSpeedTheta();

	if (movedata.detonating) {
		g_Vars.currentplayer->hands[HAND_RIGHT].mode = HANDMODE_NONE;
		g_Vars.currentplayer->hands[HAND_RIGHT].modenext = HANDMODE_NONE;
		/* c3849 Wave 5 Unit 6: quick-detonate provenance is the right-hand
		 * weapon (the same gate this path already keys on). */
		playerActivateRemoteMineDetonator(g_Vars.currentplayernum, bgunGetWeaponNum(HAND_RIGHT));
	}

	cancycleweapons = true;

	if (g_Vars.tickmode == TICKMODE_CUTSCENE) {
		cancycleweapons = false;
	}

	if (g_Vars.lvframenum < 10) {
		cancycleweapons = false;
	}

	if (cancycleweapons) {
		while (movedata.weaponbackoffset-- > 0) {
			bgunCycleBack();
		}

		while (movedata.weaponforwardoffset-- > 0) {
			bgunCycleForward();
		}
	}

	if (g_Vars.currentplayer->unk1c64) {
		g_Vars.currentplayer->unk1c64 = 0;
	} else if (movedata.canswivelgun) {
		f32 x;
		f32 y;

		bgunSetAimType(0);

		if (
				(
				 movedata.canautoaim
				 && (bmoveIsAutoAimXEnabledForCurrentWeapon() || bmoveIsAutoAimYEnabledForCurrentWeapon())
				 && g_Vars.currentplayer->autoxaimprop
				 && g_Vars.currentplayer->autoyaimprop
				 && weaponHasAimFlag(weaponnum, INVAIMFLAG_AUTOAIM)
				)
				|| (bgunGetWeaponNum(HAND_RIGHT) == WEAPON_CMP150 && g_Vars.currentplayer->hands[HAND_RIGHT].gset.weaponfunc == FUNC_SECONDARY)) {
			// Auto aim - move crosshair towards target
			s32 followlockon = false;

			if (bgunGetWeaponNum(HAND_RIGHT) == WEAPON_CMP150
					&& g_Vars.currentplayer->hands[HAND_RIGHT].gset.weaponfunc == FUNC_SECONDARY) {
				followlockon = true;
			}

			if (g_Vars.currentplayer->autoaimdamp > (PAL ? 0.955f : 0.963f)) {
				g_Vars.currentplayer->autoaimdamp -= (PAL ? 0.00037999986670911f : 0.00031999943894334f) * g_Vars.lvupdate60freal;
			}

			if (g_Vars.currentplayer->autoaimdamp < (PAL ? 0.955f : 0.963f)) {
				g_Vars.currentplayer->autoaimdamp = (PAL ? 0.955f : 0.963f);
			}

			x = g_Vars.currentplayer->autoaimx;
			y = g_Vars.currentplayer->autoaimy;

			if (followlockon) {
				bgunSwivel(x, y, PAL ? 0.899f : 0.915f, PAL ? 0.899f : 0.915f);
			} else {
				bgunSwivelWithDamp(x, y, g_Vars.currentplayer->autoaimdamp);
			}
		} else {
			// This code moves the crosshair as the player turns and makes
			// it return to the centre when not affected by anything else.
			if (g_Vars.currentplayer->autoaimdamp < (PAL ? 0.974f : 0.979f)) {
				g_Vars.currentplayer->autoaimdamp += (PAL ? 0.00037999986670911f : 0.00031999943894334f) * g_Vars.lvupdate60freal;
			}

			if (g_Vars.currentplayer->autoaimdamp > (PAL ? 0.974f : 0.979f)) {
				g_Vars.currentplayer->autoaimdamp = (PAL ? 0.974f : 0.979f);
			}

			f32 xscale, yscale;
			if (movedata.freelookdx || movedata.freelookdy) {
				xscale = PLAYER_EXTCFG().crosshairsway * 0.20f;
				yscale = PLAYER_EXTCFG().crosshairsway * 0.30f;
			} else {
				xscale = yscale = PLAYER_EXTCFG().crosshairsway;
			}
			x = g_Vars.currentplayer->speedtheta * 0.3f * xscale + g_Vars.currentplayer->gunextraaimx;
			y = -g_Vars.currentplayer->speedverta * 0.1f * yscale + g_Vars.currentplayer->gunextraaimy;

			bgunSwivelWithDamp(x, y, PAL ? 0.955f : 0.963f);
		}
	} else if (movedata.canmanualaim) {
		// Adjust crosshair's position on screen
		// when holding aim and moving stick
		bgunSetAimType(0);
		if (allowmcross) {
			// joystick is inactive, move crosshair using the mouse
			const f32 xcoeff = 320.f / 1080.f;
			const f32 ycoeff = 240.f / 1080.f;
			const f32 xscale = (PLAYER_EXTCFG().mouseaimspeedx * xcoeff) / g_Vars.currentplayer->aspect;
			const f32 yscale = PLAYER_EXTCFG().mouseaimspeedy * ycoeff;
			f32 x = g_Vars.currentplayer->swivelpos[0] + movedata.freelookdx * xscale;
			f32 y = g_Vars.currentplayer->swivelpos[1] + movedata.freelookdy * yscale;
			x = (x < -1.f) ? -1.f : ((x > 1.f) ? 1.f : x);
			y = (y < -1.f) ? -1.f : ((y > 1.f) ? 1.f : y);
			g_Vars.currentplayer->swivelpos[0] = x;
			g_Vars.currentplayer->swivelpos[1] = y;
			bgunSwivelWithDamp(x, y, 0.01f);
			return;
		}
		if (controlmode == CONTROLMODE_PC) {
			bgunSwivelWithoutDamp((movedata.analogturn * 0.65f) / 80.0f, (movedata.analogpitch * 0.65f) / 80.0f);
		} else {
			bgunSwivelWithoutDamp((movedata.c1stickxraw * 0.65f) / 80.0f, (movedata.c1stickyraw * 0.65f) / 80.0f);
		}
	}
}

void bmoveFindEnteredRoomsByPos(struct player *player, struct coord *mid, RoomNum *rooms)
{
	struct coord bbmin;
	struct coord bbmax;
	f32 eyeheight = player->vv_eyeheight;
	f32 headheight = player->vv_headheight;

	bbmin.x = mid->x - 50;
	bbmin.y = mid->y - player->crouchheight - eyeheight - 10;
	bbmin.z = mid->z - 50;

	bbmax.x = mid->x + 50;
	bbmax.y = mid->y - player->crouchheight - eyeheight + headheight + 10;
	bbmax.z = mid->z + 50;

	bgFindEnteredRooms(&bbmin, &bbmax, rooms, 7, false);
}

void bmoveFindEnteredRooms(struct player *player, RoomNum *rooms)
{
	bmoveFindEnteredRoomsByPos(player, &player->prop->pos, rooms);
}

void bmoveUpdateRooms(struct player *player)
{
	propDeregisterRooms(player->prop);
	bmoveFindEnteredRooms(player, player->prop->rooms);
	propRegisterRooms(player->prop);
}

void bmove0f0cb904(struct coord *arg0)
{
	if (arg0->f[0] || arg0->f[2]) {
		f32 hypotenuse = sqrtf(arg0->f[0] * arg0->f[0] + arg0->f[2] * arg0->f[2]);
		s32 i;

		if (hypotenuse > 1.5f) {
			arg0->x *= 1.5f / hypotenuse;
			arg0->z *= 1.5f / hypotenuse;
			hypotenuse = 1.5f;
		}

		for (i = 0; i < 3; i++) {
			if (hypotenuse > 0.0001f) {
				if (arg0->f[i] != 0) {
					if (arg0->f[i] > 0) {
						arg0->f[i] -= (1.0f / 30.0f) * g_Vars.lvupdate60freal * arg0->f[i] / hypotenuse;

						if (arg0->f[i] < 0) {
							arg0->f[i] = 0;
						}
					} else if (arg0->f[i] < 0) {
						arg0->f[i] -= (1.0f / 30.0f) * g_Vars.lvupdate60freal * arg0->f[i] / hypotenuse;

						if (arg0->f[i] > 0) {
							arg0->f[i] = 0;
						}
					}
				}
			} else {
				arg0->f[i] = 0;
			}
		}
	}
}

void bmove0f0cba88(f32 *a, f32 *b, struct coord *c, f32 mult1, f32 mult2)
{
	if (c->x != 0 || c->z != 0) {
		bmove0f0cb904(c);
		*a = c->z * mult2 + -c->x * mult1;
		*b = -c->x * mult2 - c->z * mult1;
	} else {
		*a = 0;
		*b = 0;
	}
}

void bmoveUpdateMoveInitSpeed(struct coord *newpos)
{
	if (g_Vars.currentplayer->moveinitspeed.x != 0) {
		if (g_Vars.currentplayer->moveinitspeed.x < 0.001f && g_Vars.currentplayer->moveinitspeed.x > -0.001f) {
			g_Vars.currentplayer->moveinitspeed.x = 0;
		} else {
			g_Vars.currentplayer->moveinitspeed.x *= 0.9f;
			newpos->x += g_Vars.currentplayer->moveinitspeed.x * g_Vars.lvupdate60freal;
		}
	}

	if (g_Vars.currentplayer->moveinitspeed.z != 0) {
		if (g_Vars.currentplayer->moveinitspeed.z < 0.001f && g_Vars.currentplayer->moveinitspeed.z > -0.001f) {
			g_Vars.currentplayer->moveinitspeed.z = 0;
		} else {
			g_Vars.currentplayer->moveinitspeed.z *= 0.9f;
			newpos->z += g_Vars.currentplayer->moveinitspeed.z * g_Vars.lvupdate60freal;
		}
	}
}

void bmoveTick(bool allowc1x, bool allowc1y, bool allowc1buttons, bool ignorec2)
{
	struct chrdata *chr;
	u8 foot;
	s32 sound;
	f32 xdiff;
	f32 ydiff;
	f32 zdiff;
	f32 distance;

	bmoveProcessInput(allowc1x, allowc1y, allowc1buttons, ignorec2);

	if (g_Vars.currentplayer->bondmovemode == MOVEMODE_BIKE) {
		bbikeTick();
	} else if (g_Vars.currentplayer->bondmovemode == MOVEMODE_GRAB) {
		bgrabTick();
	} else if (g_Vars.currentplayer->bondmovemode == MOVEMODE_CUTSCENE) {
		bcutsceneTick();
	} else if (g_Vars.currentplayer->bondmovemode == MOVEMODE_WALK) {
		bwalkTick();
	}

	// Update footstep sounds
	if ((g_Vars.currentplayer->bondmovemode == MOVEMODE_WALK || g_Vars.currentplayer->bondmovemode == MOVEMODE_GRAB)
			&& (g_Vars.currentplayer->speedforwards || g_Vars.currentplayer->speedsideways)
			&& (!g_Vars.normmplayerisrunning || PLAYERCOUNT() == 1)) {
		chr = g_Vars.currentplayer->prop->chr;

		if (g_Vars.currentplayer->cameramode == CAMERAMODE_DEFAULT
				&& g_Vars.currentplayer->bdeltapos.y >= -6.0f) {
			xdiff = g_Vars.currentplayer->bondprevpos.x - g_Vars.currentplayer->prop->pos.x;
			ydiff = g_Vars.currentplayer->bondprevpos.y - g_Vars.currentplayer->prop->pos.y;
			zdiff = g_Vars.currentplayer->bondprevpos.z - g_Vars.currentplayer->prop->pos.z;

			foot = 0;
			distance = sqrtf(xdiff * xdiff + ydiff * ydiff + zdiff * zdiff);

			g_Vars.currentplayer->footstepdist += distance;

			if (g_Vars.currentplayer->footstepdist >= 150.0f) {
				foot = 1;
				g_Vars.currentplayer->footstepdist = 0;
			}

			if (foot) {
				if (g_Vars.currentplayer->foot) {
					chr->footstep = 1;
				} else {
					chr->footstep = 2;
				}

				g_Vars.currentplayer->foot = 1 - g_Vars.currentplayer->foot;

				chr->floortype = g_Vars.currentplayer->floortype;

				sound = footstepChooseSound(chr, distance > 10);

				if (sound != -1) {
					snd00010718(0, 0, AL_VOL_FULL, AL_PAN_CENTER, sound, 1, 1, -1, true);
				}
			}
		}
	}
}

void bmoveUpdateVerta(void)
{
	while (g_Vars.currentplayer->vv_verta < -180) {
		g_Vars.currentplayer->vv_verta += 360;
	}

	while (g_Vars.currentplayer->vv_verta >= 180) {
		g_Vars.currentplayer->vv_verta -= 360;
	}

	if (g_Vars.currentplayer->vv_verta > 90) {
		g_Vars.currentplayer->vv_verta = 90;
	} else if (g_Vars.currentplayer->vv_verta < -90) {
		g_Vars.currentplayer->vv_verta = -90;
	}

	g_Vars.currentplayer->vv_costheta = cosf(BADDEG2RAD(g_Vars.currentplayer->vv_theta));
	g_Vars.currentplayer->vv_sintheta = sinf(BADDEG2RAD(g_Vars.currentplayer->vv_theta));

	g_Vars.currentplayer->vv_verta360 = g_Vars.currentplayer->vv_verta;

	if (g_Vars.currentplayer->vv_verta360 < 0) {
		g_Vars.currentplayer->vv_verta360 += 360;
	}

	g_Vars.currentplayer->vv_cosverta = cosf(BADDEG2RAD(g_Vars.currentplayer->vv_verta360));
	g_Vars.currentplayer->vv_sinverta = sinf(BADDEG2RAD(g_Vars.currentplayer->vv_verta360));

	g_Vars.currentplayer->bond2.unk00.x = -g_Vars.currentplayer->vv_sintheta;
	g_Vars.currentplayer->bond2.unk00.y = 0;
	g_Vars.currentplayer->bond2.unk00.z = g_Vars.currentplayer->vv_costheta;

	if (g_Vars.currentplayer->prop) {
		struct chrdata *chr = g_Vars.currentplayer->prop->chr;

		if (chr && chr->model) {
			chrSetLookAngle(chr, BADDEG2RAD(360 - g_Vars.currentplayer->vv_theta));
		}
	}
}

void bmove0f0cc19c(struct coord *arg)
{
	f32 min;

	g_Vars.currentplayer->bond2.unk10.x = arg->x;
	g_Vars.currentplayer->bond2.unk10.y = arg->y;
	g_Vars.currentplayer->bond2.unk10.z = arg->z;

	if (g_Vars.currentplayer->isdead && g_Vars.currentplayer->bondleandown > 0) {
		g_Vars.currentplayer->bondleandown -= 0.25f;

		if (g_Vars.currentplayer->bondleandown < 0) {
			g_Vars.currentplayer->bondleandown = 0;
		}
	}

	if (g_Vars.currentplayer->vv_verta < 0) {
		g_Vars.currentplayer->bond2.unk10.y += -(1.0f - g_Vars.currentplayer->vv_cosverta) * g_Vars.currentplayer->bondleandown;
	}

#if VERSION >= VERSION_NTSC_1_0
	min = g_Vars.currentplayer->vv_ground + 10;

	if (g_Vars.currentplayer->bond2.unk10.y < min) {
		g_Vars.currentplayer->bond2.unk10.y = min;
	}
#endif
}

void bmoveUpdateHead(f32 arg0, f32 arg1, f32 arg2, Mtxf *arg3, f32 arg4)
{
	f32 sp244 = 0;
	Mtxf sp180;
	Mtxf sp116;
	f32 sp100[4];
	f32 sp84[4];
	f32 sp68[4];

	if (g_Vars.currentplayer->isdead == false) {
		bheadAdjustAnimation(arg0);

		if (arg0 != 0) {
			sp244 = arg1 / arg0;
		} else if (arg1 == 0) {
			arg0 = 0;
		}
	} else {
		if (g_Vars.currentplayer->startnewbonddie) {
			bheadStartDeathAnimation(g_DeathAnimations[rngRandom() % g_NumDeathAnimations], rngRandom() % 2, 0, 1);
			g_Vars.currentplayer->startnewbonddie = false;
		}

		bheadSetSpeed(0.5);
		arg2 = 0;
	}

	bheadUpdate(sp244, arg2);
	mtx4LoadXRotation(BADDEG2RAD(360 - g_Vars.currentplayer->vv_verta360), &sp180);

	// Headroll is suppressed for living players in network games.
	// In netplay, head orientation (headlook/headup) is server-authoritative and derived from
	// synchronized position/angle data. Computing a local headroll rotation on top of that
	// would diverge from the server's view, causing visible jitter in third-person and spectate
	// perspectives. Death animations are exempt because the head is in a deterministic scripted
	// state where local roll cannot conflict with server state.
	// To enable headroll for living networked players, head orientation would need to be
	// reconciled server-side before applying it here.
	if (optionsGetHeadRoll(g_Vars.currentplayerstats->mpindex) && (!g_NetMode || g_Vars.currentplayer->isdead)) {
		mtx00016d58(&sp116,
				0, 0, 0,
				-g_Vars.currentplayer->headlook.x, -g_Vars.currentplayer->headlook.y, -g_Vars.currentplayer->headlook.z,
				g_Vars.currentplayer->headup.x, g_Vars.currentplayer->headup.y, g_Vars.currentplayer->headup.z);
		mtx4MultMtx4InPlace(&sp116, &sp180);
	}

	mtx4LoadYRotation(BADDEG2RAD(360 - g_Vars.currentplayer->vv_theta), &sp116);
	mtx4MultMtx4InPlace(&sp116, &sp180);

	if (arg3) {
		quaternion0f097044(&sp180, sp100);
		quaternion0f097044(arg3, sp84);
		quaternion0f0976c0(sp100, sp84);
		quaternionSlerp(sp100, sp84, arg4, sp68);
		quaternionToMtx(sp68, &sp180);
	}

	g_Vars.currentplayer->bond2.unk1c.x = sp180.m[2][0];
	g_Vars.currentplayer->bond2.unk1c.y = sp180.m[2][1];
	g_Vars.currentplayer->bond2.unk1c.z = sp180.m[2][2];
	g_Vars.currentplayer->bond2.unk28.x = sp180.m[1][0];
	g_Vars.currentplayer->bond2.unk28.y = sp180.m[1][1];
	g_Vars.currentplayer->bond2.unk28.z = sp180.m[1][2];
}

void bmove0f0cc654(f32 arg0, f32 arg1, f32 arg2)
{
	bmoveUpdateHead(arg0, arg1, arg2, NULL, 0);
}

s32 bmoveGetCrouchPos(void)
{
	return (g_Vars.currentplayer->crouchpos < g_Vars.currentplayer->autocrouchpos)
		? g_Vars.currentplayer->crouchpos
		: g_Vars.currentplayer->autocrouchpos;
}

s32 bmoveGetCrouchPosByPlayer(s32 playernum)
{
	return (g_Vars.players[playernum]->crouchpos < g_Vars.players[playernum]->autocrouchpos)
		? g_Vars.players[playernum]->crouchpos
		: g_Vars.players[playernum]->autocrouchpos;
}
