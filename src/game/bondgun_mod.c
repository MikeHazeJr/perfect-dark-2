/*
 * Crystal pistol (T-MODDING-008): gameplay for public (mod) weapons that the
 * native weapon table cannot express. See game/bondgun_mod.h and
 * context/designs/modding/crystal-pistol-engine.md.
 */
#include <ultra64.h>
#include <string.h>
#include <math.h>
#include "constants.h"
#include "types.h"
#include "bss.h"
#include "data.h"
#include "game/bondgun.h"
#include "game/bondgun_mod.h"
#include "game/prop.h"
#include "game/propsnd.h"
#include "game/propobj.h"
#include "lib/model.h"
#include "lib/mtx.h"
#include "lib/snd.h"
#include "effect_gameplay_runtime.h"
#include "loader_pool.h"
#include "system.h"
#include "weapon_graph_runtime.h"
#include "weapon_rules.h"
#include "weapon_events.h"
#include "game/camera.h"
#include "game/game_0b0fd0.h"
#include "game/player.h"
#include "game/playermgr.h"
#include "game/mplayer/mplayer.h"
#include "game/chraction.h"
#include "game/acosfasinf.h" /* acosf for the charge fan log */
#include "game/chr.h"
#include "game/botact.h"
#include "catalog_mgr_weapons.h" /* bot preferences */
#include "lib/collision.h"
#include "lib/lib_317f0.h"
#include "net/net.h"
#include "net/netmsg.h"

/* ------------------------------------------------------------------------
 * Committed charge launch. The charge belongs to the match, not to the hand:
 * the hand only stays busy while it runs, and the release still happens at
 * the deadline when the owner has died (from the last socket transforms the
 * held model reported). One pending charge per player.
 * ------------------------------------------------------------------------ */

#define BGUN_MOD_CHARGE_ROUNDS 16

struct bgunmodcharge {
	bool active;
	s32 weaponnum;
	s32 funcnum;
	s32 ammoindex;
	s32 count;
	s32 startframe60;
	s32 releaseframe60;
	struct gset gset;
	struct prop *ownerprop;
	struct sndstate *chargesound;
	bool socketvalid[BGUN_MOD_CHARGE_ROUNDS];
	struct coord socketpos[BGUN_MOD_CHARGE_ROUNDS];
	struct coord socketdir[BGUN_MOD_CHARGE_ROUNDS];
	bool aimvalid;
	struct coord aimdir;
	struct coord aimup;
};

static struct bgunmodcharge s_BgunModCharges[MAX_PLAYERS];

/* Each player's eased gun roll in radians (bgunModRollTick). */
static f32 s_BgunModRoll[MAX_PLAYERS];

/* Bots' committed charges, found by owner prop (bgunModBotChargeShot). */
#define BGUN_MOD_BOT_CHARGES MAX_BOTS

static struct bgunmodcharge s_BgunModBotCharges[BGUN_MOD_BOT_CHARGES];
static s32 s_BgunModBotChargeHand[BGUN_MOD_BOT_CHARGES];
static s32 s_BgunModBotChargeFrame = -1;
/* By aibotnum: the bot's last mod volley was a charge (bgunModBotChooseFunction). */
static u8 s_BgunModBotChargedLast[MAX_BOTS];
/* A bot charges a target this far away: from just outside melee (150; in
 * game a bot fought its target from 135..232 units, so 300 never came) out to
 * 1500. Once on the charge function its explosive attack range (600..1200)
 * backs it off during the windup, and its own mines never trigger on it. */
#define BGUN_MOD_BOT_CHARGE_MIN_DIST 150.0f
#define BGUN_MOD_BOT_CHARGE_MAX_DIST 1500.0f
/* BONDGUN.BOT: choose lines, by aibotnum, and the stage's budget. */
static s32 s_BgunModBotChooseLogFrame[MAX_BOTS];
static s32 s_BgunModBotChooseLogBudget = 24;
/* By aibotnum and function: weaponnum + 1 of the last BONDGUN.BOT line. */
static s32 s_BgunModBotFireLogged[MAX_BOTS][2];

static struct bgunmodcharge *bgunModChargeForCurrentPlayer(void)
{
	s32 playernum = g_Vars.currentplayernum;

	if (playernum < 0 || playernum >= MAX_PLAYERS) {
		return NULL;
	}

	return &s_BgunModCharges[playernum];
}

/*
 * held-weapon-events.md slice 5 (PR 31 review): whether a custom weapon's
 * function is a committed charge launch: its plan's trigger is
 * og.weapon.charge with start on_press (a v1 record's charge_time_ticks60
 * is that plan), for the plan's time. The player's hand, bots and the gun's
 * look all ask here, so a node's time is everyone's.
 */
static s32 bgunModFunctionCharges(s32 weaponnum, s32 funcnum, s32 *out_time60)
{
	weapon_rule_trigger_t plan;

	if (weaponnum < WEAPON_CUSTOM_START || funcnum < 0 || funcnum > 1) {
		return false;
	}

	weaponFunctionPlan(weaponnum, funcnum, &plan);

	if (plan.module != WEAPON_MODULE_CHARGE || plan.charge_start != WEAPON_RULE_CHARGE_ON_PRESS) {
		return false;
	}

	if (out_time60) {
		*out_time60 = plan.charge_time > 0 ? plan.charge_time : 1;
	}

	return true;
}

/* The function of weaponnum that is a committed charge launch, or NULL. */
static const weapon_graph_held_function_t *bgunModChargeFunction(s32 weaponnum, s32 *out_funcnum)
{
	s32 f;

	if (weaponnum < WEAPON_CUSTOM_START) {
		return NULL;
	}

	for (f = 0; f < 2; f++) {
		const weapon_graph_held_function_t *held =
			weaponGraphRuntimeGetHeldFunctionForGameplay(weaponnum, f);

		if (held != NULL && bgunModFunctionCharges(weaponnum, f, NULL)) {
			if (out_funcnum) {
				*out_funcnum = f;
			}
			return held;
		}
	}

	return NULL;
}

static void bgunModChargeCaptureSockets(struct bgunmodcharge *charge,
		const weapon_graph_held_function_t *held, struct modeldef *modeldef, Mtxf *matrices);

const loader_pool_mod_ammo_t *bgunModAmmo(s32 weaponnum, s32 ammoindex)
{
	if (weaponnum < WEAPON_CUSTOM_START || ammoindex < 0 || ammoindex > 1) {
		return NULL;
	}
	return loaderPoolGetModAmmo(weaponnum, ammoindex);
}

bool bgunModAmmoIsSelfLoading(s32 weaponnum, s32 ammoindex)
{
	const loader_pool_mod_ammo_t *mod = bgunModAmmo(weaponnum, ammoindex);
	return mod != NULL && mod->selfloading;
}

bool bgunModGsetFiresSpentRounds(struct gset *gset)
{
	struct weaponfunc *func;

	if (gset == NULL || gset->weaponnum < WEAPON_CUSTOM_START) {
		return false;
	}

	func = gsetGetWeaponFunction(gset);
	return func != NULL && bgunModAmmoIsSelfLoading(gset->weaponnum, func->ammoindex);
}

static s32 bgunModRegrowFinish(s32 handnum, struct hand *hand, s32 lvupdate)
{
	hand->gs_int1 = 0;
	if (bgunSetState(handnum, HANDSTATE_IDLE)) {
		return lvupdate;
	}
	return 0;
}

s32 bgunModTickIncReloadRegrow(struct handweaponinfo *info, s32 handnum,
		struct hand *hand, s32 lvupdate, s32 ammoindex,
		const loader_pool_mod_ammo_t *mod)
{
	s32 clip = hand->clipsizes[ammoindex];

	if (hand->statecycles == 0) {
		/* gs_int1: 60 Hz ticks grown into the next round. */
		hand->gs_int1 = 0;
		hand->gs_int2 = 0;
		sysLogPrintf(LOG_NOTE, "BONDGUN.REGROW: start weapon=%d hand=%d slot=%d loaded=%d/%d",
			info->weaponnum, handnum, ammoindex, hand->loadedammo[ammoindex], clip);
	}

	if (hand->loadedammo[ammoindex] >= clip) {
		return bgunModRegrowFinish(handnum, hand, lvupdate);
	}

	/* Like the shotgun's shell-by-shell reload, firing ends the regrowth once
	 * a round is ready; the round still growing is dropped. A weapon switch
	 * ends it too, since a regrow can take several seconds. */
	if ((hand->triggeron && hand->loadedammo[ammoindex] > 0)
			|| g_Vars.currentplayer->gunctrl.switchtoweaponnum >= 0) {
		sysLogPrintf(LOG_NOTE, "BONDGUN.REGROW: interrupted weapon=%d slot=%d loaded=%d/%d by=%s",
			info->weaponnum, ammoindex, hand->loadedammo[ammoindex], clip,
			hand->triggeron ? "trigger" : "switch");
		return bgunModRegrowFinish(handnum, hand, lvupdate);
	}

	hand->gs_int1 += g_Vars.lvupdate60;

	while (hand->gs_int1 >= mod->regrow_ticks60 && hand->loadedammo[ammoindex] < clip) {
		hand->gs_int1 -= mod->regrow_ticks60;
		hand->loadedammo[ammoindex]++;

		if (mod->regrow_sound >= 0 && g_Vars.lvupdate240 > 0 && !g_Vars.currentplayer->isdead) {
			sndStart(var80095200, mod->regrow_sound, NULL, -1, -1, -1, -1, -1);
		}

		sysLogPrintf(LOG_NOTE, "BONDGUN.REGROW: round weapon=%d slot=%d loaded=%d/%d",
			info->weaponnum, ammoindex, hand->loadedammo[ammoindex], clip);
	}

	if (hand->loadedammo[ammoindex] >= clip) {
		sysLogPrintf(LOG_NOTE, "BONDGUN.REGROW: full weapon=%d slot=%d loaded=%d/%d",
			info->weaponnum, ammoindex, hand->loadedammo[ammoindex], clip);
		return bgunModRegrowFinish(handnum, hand, lvupdate);
	}

	return 0;
}

void bgunModApplyHeldModel(struct hand *hand, struct modeldef *modeldef, Mtxf *matrices)
{
	s32 weaponnum = hand->gset.weaponnum;
	s32 slot;

	const weapon_graph_held_function_t *chargefunc;
	struct bgunmodcharge *charge = bgunModChargeForCurrentPlayer();
	bool charging = false;
	f32 pulse = 1.0f;

	if (weaponnum < WEAPON_CUSTOM_START || modeldef == NULL || matrices == NULL) {
		return;
	}

	chargefunc = bgunModChargeFunction(weaponnum, NULL);

	if (chargefunc != NULL) {
		charging = charge != NULL && charge->active && charge->weaponnum == weaponnum;

		/* The toggles reset to visible every frame; pick the look each frame. */
		if (chargefunc->charge_hide_part >= 0) {
			bgunSetPartVisible(chargefunc->charge_hide_part, !charging, hand, modeldef);
		}

		if (chargefunc->charge_show_part >= 0) {
			bgunSetPartVisible(chargefunc->charge_show_part, charging, hand, modeldef);
		}

		if (charging) {
			s32 elapsed = g_Vars.lvframe60 - charge->startframe60;
			s32 chargetime = charge->releaseframe60 - charge->startframe60; /* the plan's time (slice 5) */
			f32 ramp = chargetime > 0 ? (f32)elapsed / (f32)chargetime : 1.0f;

			/* Sockets before any seat is scaled; the tips are unscaled children. */
			bgunModChargeCaptureSockets(charge, chargefunc, modeldef, matrices);

			if (ramp > 1.0f) {
				ramp = 1.0f;
			}

			pulse = 1.0f + (0.03f + 0.07f * ramp) * sinf(elapsed * 0.45f);
		}
	}

	for (slot = 0; slot < 2; slot++) {
		const loader_pool_mod_ammo_t *mod = bgunModAmmo(weaponnum, slot);
		s32 clip;
		s32 loaded;
		s32 j;

		if (mod == NULL || mod->seat_part_base < 0) {
			continue;
		}

		clip = hand->clipsizes[slot];
		loaded = hand->loadedammo[slot];

		for (j = 0; j < clip; j++) {
			struct modelnode *node = modelGetPart(modeldef, mod->seat_part_base + j);
			s32 mtxindex;
			f32 scale;

			/* Seats are POSITION nodes; anything else under that part number is
			 * not ours to scale. */
			if (node == NULL || (node->type & 0xff) != MODELNODETYPE_POSITION) {
				continue;
			}

			mtxindex = modelFindNodeMtxIndex(node, 0);

			if (mtxindex < 0 || mtxindex >= modeldef->nummatrices) {
				continue;
			}

			if (j < loaded) {
				scale = 1.0f;
			} else if (j == loaded && mod->regrow && hand->state == HANDSTATE_RELOAD) {
				scale = (f32)hand->gs_int1 / (f32)mod->regrow_ticks60;

				if (scale < 0.0f) {
					scale = 0.0f;
				}

				if (scale > 1.0f) {
					scale = 1.0f;
				}
			} else {
				scale = 0.0f;
			}

			/* Charged crystals pulse while the charge builds. */
			if (charging && j < charge->count && scale > 0.0f) {
				scale *= pulse;
			}

			if (scale != 1.0f) {
				/* About the seat: rows 0-2 only, so the seat itself stays put. */
				mtx00015f04(scale, &matrices[mtxindex]);
			}
		}
	}
}

/* ------------------------------------------------------------------------
 * The same gun seen in third person: the chr's held weapon object, drawn
 * from the hand node (chr0f022214 in chr.c). Its owner's state comes from
 * the player's hand (replicated to clients by SVC_PLAYER_STATS) or the bot's
 * clip (SVC_CHR_STATE), the glow from the owner's charge, the roll from the
 * owner's roll.
 * ------------------------------------------------------------------------ */

static s32 bgunModHeldHandnum(struct chrdata *chr, struct prop *weaponprop)
{
	return chr->weapons_held[HAND_LEFT] == weaponprop ? HAND_LEFT : HAND_RIGHT;
}

static s32 bgunModChrPlayernum(struct chrdata *chr)
{
	s32 playernum;

	if (chr == NULL || chr->prop == NULL || chr->prop->type != PROPTYPE_PLAYER) {
		return -1;
	}

	playernum = playermgrGetPlayerNumByProp(chr->prop);

	return playernum >= 0 && playernum < MAX_PLAYERS && g_Vars.players[playernum] ? playernum : -1;
}

static struct bgunmodcharge *bgunModBotChargeFind(struct prop *ownerprop, bool alloc);

static struct bgunmodcharge *bgunModChargeForChr(struct chrdata *chr)
{
	s32 playernum = bgunModChrPlayernum(chr);

	if (playernum >= 0) {
		return &s_BgunModCharges[playernum];
	}

	return chr != NULL && chr->aibot != NULL && chr->prop != NULL
		? bgunModBotChargeFind(chr->prop, false) : NULL;
}

static void bgunModSetModelPartVisible(struct model *model, s32 partnum, bool visible)
{
	struct modelnode *node = modelGetPart(model->definition, partnum);
	union modelrwdata *rwdata;

	if (node == NULL || (node->type & 0xff) != MODELNODETYPE_TOGGLE) {
		return;
	}

	rwdata = modelGetNodeRwData(model, node);

	if (rwdata != NULL) {
		rwdata->toggle.visible = visible;
	}
}

/* BONDGUN.MODEL3P: by MP chr index, the last third-person state logged
 * (loaded rounds, clip, charging, roll), so a line is written only when what
 * the view shows changes; at most 64 lines a stage. */
static s32 s_BgunModModel3PLogged[MAX_MPCHRS];
static s32 s_BgunModModel3PBudget = 64;

static void bgunModTraceModel3P(struct chrdata *chr, s32 weaponnum, s32 loaded, s32 clip,
		bool charging, f32 roll)
{
	s32 index = mpPlayerGetIndex(chr);
	s32 rolldeg = (s32)(roll * (180.0f / M_PI) + (roll < 0.0f ? -0.5f : 0.5f));
	s32 state;

	if (index < 0 || index >= MAX_MPCHRS || s_BgunModModel3PBudget <= 0) {
		return;
	}

	state = 1 + ((loaded + 1) & 0xff) + ((clip & 0xff) << 8) + (charging ? 1 << 16 : 0)
		+ (((rolldeg + 256) & 0x1ff) << 17);

	if (s_BgunModModel3PLogged[index] == state) {
		return;
	}

	s_BgunModModel3PLogged[index] = state;
	s_BgunModModel3PBudget--;
	sysLogPrintf(LOG_NOTE, "BONDGUN.MODEL3P: chrnum=%d mpindex=%d weapon=%d loaded=%d/%d charging=%d roll=%d",
		(s32)chr->chrnum, index, weaponnum, loaded, clip, charging ? 1 : 0, rolldeg);
}

bool bgunModHeldRoll3P(struct chrdata *chr, struct prop *weaponprop, Mtxf *in, Mtxf *out)
{
	s32 playernum = bgunModChrPlayernum(chr);
	struct coord rollrot = {0.0f, 0.0f, 0.0f};
	Mtxf rollmtx;

	if (playernum < 0 || weaponprop == NULL || weaponprop->type != PROPTYPE_WEAPON
			|| weaponprop->weapon == NULL || weaponprop->weapon->weaponnum < WEAPON_CUSTOM_START
			|| bgunModHeldHandnum(chr, weaponprop) != HAND_RIGHT
			|| s_BgunModRoll[playernum] == 0.0f) {
		return false;
	}

	/* About the model's own barrel axis, as in first person: the roll is
	 * applied to model space before the hand matrix. */
	rollrot.z = s_BgunModRoll[playernum];
	mtx4LoadRotation(&rollrot, &rollmtx);
	mtx00015be4(in, &rollmtx, out);

	return true;
}

void bgunModApplyHeldModel3P(struct chrdata *chr, struct prop *weaponprop, struct model *model)
{
	struct weaponobj *weapon;
	struct modeldef *modeldef;
	struct bgunmodcharge *charge;
	const weapon_graph_held_function_t *chargefunc;
	struct hand *hand = NULL;
	s32 weaponnum;
	s32 playernum;
	s32 handnum;
	bool charging;
	f32 pulse = 1.0f;
	s32 slot;
	s32 logloaded = -1;
	s32 logclip = 0;

	if (chr == NULL || weaponprop == NULL || model == NULL || model->definition == NULL
			|| model->matrices == NULL || weaponprop->type != PROPTYPE_WEAPON
			|| weaponprop->weapon == NULL) {
		return;
	}

	weapon = weaponprop->weapon;
	weaponnum = weapon->weaponnum;

	if (weaponnum < WEAPON_CUSTOM_START) {
		return;
	}

	modeldef = model->definition;
	handnum = bgunModHeldHandnum(chr, weaponprop);
	playernum = bgunModChrPlayernum(chr);

	if (playernum >= 0 && g_Vars.players[playernum]->hands[handnum].gset.weaponnum == weaponnum) {
		hand = &g_Vars.players[playernum]->hands[handnum];
	}

	charge = bgunModChargeForChr(chr);
	charging = charge != NULL && charge->active && charge->weaponnum == weaponnum;
	chargefunc = bgunModChargeFunction(weaponnum, NULL);

	if (chargefunc != NULL) {
		if (chargefunc->charge_hide_part >= 0) {
			bgunModSetModelPartVisible(model, chargefunc->charge_hide_part, !charging);
		}

		if (chargefunc->charge_show_part >= 0) {
			bgunModSetModelPartVisible(model, chargefunc->charge_show_part, charging);
		}

		if (charging) {
			s32 elapsed = g_Vars.lvframe60 - charge->startframe60;
			s32 chargetime = charge->releaseframe60 - charge->startframe60; /* the plan's time (slice 5) */
			f32 ramp = chargetime > 0 ? (f32)elapsed / (f32)chargetime : 1.0f;

			if (ramp > 1.0f) {
				ramp = 1.0f;
			}

			pulse = 1.0f + (0.03f + 0.07f * ramp) * sinf(elapsed * 0.45f);
		}
	}

	for (slot = 0; slot < 2; slot++) {
		const loader_pool_mod_ammo_t *mod = bgunModAmmo(weaponnum, slot);
		s32 clip;
		s32 loaded;
		f32 regrow = -1.0f;
		s32 j;

		if (mod == NULL || mod->seat_part_base < 0) {
			continue;
		}

		if (hand != NULL) {
			clip = hand->clipsizes[slot];
			loaded = hand->loadedammo[slot];

			if (mod->regrow && hand->state == HANDSTATE_RELOAD && mod->regrow_ticks60 > 0) {
				regrow = (f32)hand->gs_int1 / (f32)mod->regrow_ticks60;
			}
		} else if (chr->aibot != NULL && chr->aibot->weaponnum == weaponnum) {
			struct gset gset = {0};
			struct weaponfunc *func;

			gset.weaponnum = weaponnum;
			gset.weaponfunc = chr->aibot->gunfunc;
			func = gsetGetWeaponFunction(&gset);

			if (func == NULL || func->ammoindex != slot) {
				continue;
			}

			clip = botactGetClipCapacityByFunction(weaponnum, chr->aibot->gunfunc);
			loaded = chr->aibot->loadedammo[handnum];
		} else {
			/* No owner state (a guard holding it, say): leave it full. */
			continue;
		}

		if (logloaded < 0) {
			logloaded = loaded;
			logclip = clip;
		}

		for (j = 0; j < clip && j < BGUN_MOD_CHARGE_ROUNDS; j++) {
			struct modelnode *node = modelGetPart(modeldef, mod->seat_part_base + j);
			s32 mtxindex;
			f32 scale;

			if (node == NULL || (node->type & 0xff) != MODELNODETYPE_POSITION) {
				continue;
			}

			mtxindex = modelFindNodeMtxIndex(node, 0);

			if (mtxindex < 0 || mtxindex >= modeldef->nummatrices) {
				continue;
			}

			if (j < loaded) {
				scale = 1.0f;
			} else if (j == loaded && regrow >= 0.0f) {
				scale = regrow > 1.0f ? 1.0f : regrow;
			} else {
				scale = 0.0f;
			}

			if (charging && j < charge->count && scale > 0.0f) {
				scale *= pulse;
			}

			if (scale != 1.0f) {
				mtx00015f04(scale, &model->matrices[mtxindex]);
			}
		}
	}

	if (logloaded >= 0 || charging || (playernum >= 0 && s_BgunModRoll[playernum] != 0.0f)) {
		bgunModTraceModel3P(chr, weaponnum, logloaded, logclip, charging,
			playernum >= 0 ? s_BgunModRoll[playernum] : 0.0f);
	}
}

/* ------------------------------------------------------------------------
 * Landed mod projectiles: crystal clusters and mines that arm on landing.
 * Embedded objects are delisted children of what they hit, so the active
 * prop list cannot find them; landed crystals are kept here instead
 * (stage lifetime, forgotten in objFree).
 * ------------------------------------------------------------------------ */

#define BGUN_MOD_LANDED_MAX 64

static void bgunModChargeResetStage(void);

static struct weaponobj *s_BgunModLanded[BGUN_MOD_LANDED_MAX];

static const weapon_graph_held_function_t *bgunModHeldForWeaponObj(struct weaponobj *weapon)
{
	if (weapon == NULL || weapon->weaponnum < WEAPON_CUSTOM_START) {
		return NULL;
	}
	return weaponGraphRuntimeGetHeldFunctionForGameplay(weapon->weaponnum, weapon->gunfunc);
}

static s32 bgunModWeaponObjOwner(struct weaponobj *weapon)
{
	return (weapon->base.hidden & 0xf0000000) >> 28;
}

void bgunModProjectileForget(struct weaponobj *weapon)
{
	s32 i;

	for (i = 0; i < BGUN_MOD_LANDED_MAX; i++) {
		if (s_BgunModLanded[i] == weapon) {
			s_BgunModLanded[i] = NULL;
		}
	}
}

/* Budget for BONDGUN.PROJECTILE.CHRTEST lines per stage. */
static s32 s_BgunModChrTestBudget = 48;

void bgunModResetStage(void)
{
	memset(s_BgunModLanded, 0, sizeof(s_BgunModLanded));
	bgunModChargeResetStage();
	s_BgunModChrTestBudget = 48;
	memset(s_BgunModModel3PLogged, 0, sizeof(s_BgunModModel3PLogged));
	s_BgunModModel3PBudget = 64;
}

void bgunModTraceProjectileChrTest(struct prop *projectile, struct prop *chrprop,
		const struct coord *pos1, const struct coord *pos2, s32 result)
{
	struct defaultobj *obj;
	struct chrdata *chr;
	f32 sx;
	f32 sy;
	f32 sz;
	f32 len2;
	f32 t;
	f32 dx;
	f32 dy;
	f32 dz;
	f32 dist;

	if (s_BgunModChrTestBudget <= 0 || projectile == NULL || chrprop == NULL
			|| projectile->type != PROPTYPE_WEAPON || projectile->weapon == NULL
			|| projectile->weapon->weaponnum < WEAPON_CUSTOM_START
			|| chrprop->chr == NULL || pos1 == NULL || pos2 == NULL) {
		return;
	}

	obj = projectile->obj;
	chr = chrprop->chr;

	/* Distance from the character's position to this tick's segment. */
	sx = pos2->x - pos1->x;
	sy = pos2->y - pos1->y;
	sz = pos2->z - pos1->z;
	len2 = sx * sx + sy * sy + sz * sz;
	t = len2 > 0.0f
		? ((chrprop->pos.x - pos1->x) * sx + (chrprop->pos.y - pos1->y) * sy + (chrprop->pos.z - pos1->z) * sz) / len2
		: 0.0f;
	t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
	dx = chrprop->pos.x - (pos1->x + sx * t);
	dy = chrprop->pos.y - (pos1->y + sy * t);
	dz = chrprop->pos.z - (pos1->z + sz * t);
	dist = sqrtf(dx * dx + dy * dy + dz * dz);

	if (dist > 150.0f) {
		return;
	}

	s_BgunModChrTestBudget--;
	sysLogPrintf(LOG_NOTE,
		"BONDGUN.PROJECTILE.CHRTEST: weapon=%d func=%d chrnum=%d proptype=%d result=%s dist=%.0f radius=%.0f onscreen=%d perimoff=%d hidden=%d",
		projectile->weapon->weaponnum, projectile->weapon->gunfunc, (s32)chr->chrnum, chrprop->type,
		result < 0 ? "skipped" : (result ? "hit" : "miss"), dist, chrGetHitRadius(chr),
		(chrprop->flags & PROPFLAG_ONTHISSCREENTHISTICK) ? 1 : 0,
		(chr->hidden & CHRHFLAG_PERIMDISABLED) ? 1 : 0,
		(chr->chrflags & CHRCFLAG_HIDDEN) ? 1 : 0);
	(void)obj;
}

static bool bgunModLandedRegister(struct weaponobj *weapon)
{
	s32 i;

	for (i = 0; i < BGUN_MOD_LANDED_MAX; i++) {
		if (s_BgunModLanded[i] == weapon) {
			return true;
		}
	}

	for (i = 0; i < BGUN_MOD_LANDED_MAX; i++) {
		if (s_BgunModLanded[i] == NULL) {
			s_BgunModLanded[i] = weapon;
			return true;
		}
	}

	return false;
}

/* Where a landed crystal is now: one embedded in a character or object
 * moves with it, and its own pos is only the point where it hit. */
static struct coord *bgunModLandedPos(struct weaponobj *weapon)
{
	struct prop *prop = weapon->base.prop;

	while (prop->parent != NULL) {
		prop = prop->parent;
	}

	return &prop->pos;
}

static bool bgunModLandedUsable(struct weaponobj *weapon)
{
	struct prop *prop = weapon->base.prop;

	return prop != NULL && (weapon->base.hidden & OBJHFLAG_DELETING) == 0;
}

void bgunModProjectileLanded(struct weaponobj *weapon)
{
	const weapon_graph_held_function_t *held = bgunModHeldForWeaponObj(weapon);
	const weapon_graph_projectile_runtime_t *projectile;
	const weapon_graph_entity_runtime_t *entity;
	struct weaponobj *members[BGUN_MOD_LANDED_MAX];
	struct prop *prop;
	s32 owner;
	s32 count;
	s32 i;

	if (held == NULL || weapon->base.prop == NULL) {
		return;
	}

	prop = weapon->base.prop;
	owner = bgunModWeaponObjOwner(weapon);

	/* A crystal mine arms where it lands; the launch only set it inert. */
	entity = weaponGraphRuntimeGetEntityForHeldFunction(held);

	if (entity != NULL && entity->has_proxy_trigger && entity->proxy_arm_on_land
			&& weapon->timer240 != 1) {
		weapon->timer240 = 1;
		weaponRegisterProxy(weapon);
		sysLogPrintf(LOG_NOTE, "BONDGUN.CRYSTAL: mine armed weapon=%d func=%d owner=%d at=(%.0f,%.0f,%.0f)",
			weapon->weaponnum, weapon->gunfunc, owner, prop->pos.x, prop->pos.y, prop->pos.z);
	}

	projectile = weaponGraphRuntimeGetProjectileForHeldFunction(held);

	if (projectile == NULL || projectile->cluster_count < 2) {
		return;
	}

	if (!bgunModLandedRegister(weapon)) {
		sysLogPrintf(LOG_WARNING, "BONDGUN.CRYSTAL: landed registry full; weapon=%d not counted", weapon->weaponnum);
		return;
	}

	/* Every landed crystal of the same asset and owner within the radius of
	 * the one that just landed, itself included. */
	count = 0;

	for (i = 0; i < BGUN_MOD_LANDED_MAX; i++) {
		struct weaponobj *other = s_BgunModLanded[i];
		f32 dx;
		f32 dy;
		f32 dz;

		if (other == NULL || !bgunModLandedUsable(other)
				|| other->weaponnum != weapon->weaponnum || other->gunfunc != weapon->gunfunc
				|| bgunModWeaponObjOwner(other) != owner) {
			continue;
		}

		dx = bgunModLandedPos(other)->x - bgunModLandedPos(weapon)->x;
		dy = bgunModLandedPos(other)->y - bgunModLandedPos(weapon)->y;
		dz = bgunModLandedPos(other)->z - bgunModLandedPos(weapon)->z;

		if (dx * dx + dy * dy + dz * dz <= projectile->cluster_radius * projectile->cluster_radius) {
			members[count++] = other;
		}
	}

	sysLogPrintf(LOG_NOTE, "BONDGUN.CRYSTAL: landed weapon=%d func=%d owner=%d at=(%.0f,%.0f,%.0f) embedded=%d near=%d/%d",
		weapon->weaponnum, weapon->gunfunc, owner, prop->pos.x, prop->pos.y, prop->pos.z,
		(weapon->base.hidden & OBJHFLAG_EMBEDDED) ? 1 : 0, count, projectile->cluster_count);

	if (count < projectile->cluster_count) {
		return;
	}

	sysLogPrintf(LOG_NOTE, "BONDGUN.CRYSTAL: cluster detonated weapon=%d func=%d owner=%d count=%d radius=%.0f",
		weapon->weaponnum, weapon->gunfunc, owner, count, projectile->cluster_radius);

	for (i = 0; i < count; i++) {
		struct weaponobj *member = members[i];

		bgunModProjectileForget(member);
		effectGameplayRuntimeTriggerPropExplosion(projectile->cluster_explosion_ref,
			member->base.prop, EXPLOSIONTYPE_PHOENIX);
		member->base.hidden |= OBJHFLAG_DELETING;
	}
}

/* ------------------------------------------------------------------------
 * Committed charge launch: start, socket capture and release.
 * ------------------------------------------------------------------------ */

static void bgunModChargeResetStage(void)
{
	memset(s_BgunModCharges, 0, sizeof(s_BgunModCharges));
	memset(s_BgunModBotCharges, 0, sizeof(s_BgunModBotCharges));
	memset(s_BgunModBotChargedLast, 0, sizeof(s_BgunModBotChargedLast));
	memset(s_BgunModBotFireLogged, 0, sizeof(s_BgunModBotFireLogged));
	memset(s_BgunModBotChooseLogFrame, 0, sizeof(s_BgunModBotChooseLogFrame));
	s_BgunModBotChooseLogBudget = 24;
	s_BgunModBotChargeFrame = -1;
}

static f32 bgunModLength(const struct coord *v)
{
	return sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
}

static bool bgunModNormalise(struct coord *v)
{
	f32 len = bgunModLength(v);

	if (len < 0.0001f) {
		return false;
	}

	v->x /= len;
	v->y /= len;
	v->z /= len;
	return true;
}

static void bgunModCross(const struct coord *a, const struct coord *b, struct coord *out)
{
	struct coord r;

	r.x = a->y * b->z - a->z * b->y;
	r.y = a->z * b->x - a->x * b->z;
	r.z = a->x * b->y - a->y * b->x;
	*out = r;
}

static f32 bgunModDot(const struct coord *a, const struct coord *b)
{
	return a->x * b->x + a->y * b->y + a->z * b->z;
}

/* Rotate v by the rotation that takes unit vector from onto unit vector to. */
static void bgunModRotateFromTo(const struct coord *from, const struct coord *to, struct coord *v)
{
	struct coord k;
	struct coord kv;
	f32 s;
	f32 c = bgunModDot(from, to);
	f32 kdotv;
	struct coord out;

	bgunModCross(from, to, &k);
	s = bgunModLength(&k);

	if (s < 0.00001f) {
		return;
	}

	k.x /= s;
	k.y /= s;
	k.z /= s;
	bgunModCross(&k, v, &kv);
	kdotv = bgunModDot(&k, v);

	out.x = v->x * c + kv.x * s + k.x * kdotv * (1.0f - c);
	out.y = v->y * c + kv.y * s + k.y * kdotv * (1.0f - c);
	out.z = v->z * c + kv.z * s + k.z * kdotv * (1.0f - c);
	*v = out;
}

/* Rotation whose third row is dir, as the thrown-object spawn expects. */
static void bgunModLoadBasis(Mtxf *m, const struct coord *dir)
{
	struct coord up = {0.0f, 1.0f, 0.0f};
	struct coord x;
	struct coord y;

	mtx4LoadIdentity(m);
	bgunModCross(&up, dir, &x);

	if (!bgunModNormalise(&x)) {
		x.x = 1.0f;
		x.y = 0.0f;
		x.z = 0.0f;
	}

	bgunModCross(dir, &x, &y);

	m->m[0][0] = x.x;
	m->m[0][1] = x.y;
	m->m[0][2] = x.z;
	m->m[1][0] = y.x;
	m->m[1][1] = y.y;
	m->m[1][2] = y.z;
	m->m[2][0] = dir->x;
	m->m[2][1] = dir->y;
	m->m[2][2] = dir->z;
}

/* World position of a POSITION part of the held gun (matrices are camera
 * space; camGetProjectionMtxF is camera to world, as for the muzzle). */
static bool bgunModPartWorldPos(struct modeldef *modeldef, Mtxf *matrices, s32 partnum, struct coord *out)
{
	struct modelnode *node = modelGetPart(modeldef, partnum);
	s32 mtxindex;

	if (node == NULL || (node->type & 0xff) != MODELNODETYPE_POSITION) {
		return false;
	}

	mtxindex = modelFindNodeMtxIndex(node, 0);

	if (mtxindex < 0 || mtxindex >= modeldef->nummatrices) {
		return false;
	}

	out->x = matrices[mtxindex].m[3][0];
	out->y = matrices[mtxindex].m[3][1];
	out->z = matrices[mtxindex].m[3][2];
	mtx4TransformVecInPlace(camGetProjectionMtxF(), out);
	return true;
}

static void bgunModChargeCaptureSockets(struct bgunmodcharge *charge,
		const weapon_graph_held_function_t *held, struct modeldef *modeldef, Mtxf *matrices)
{
	struct coord forward = {0.0f, 0.0f, -1.0f};
	s32 j;

	for (j = 0; j < charge->count && j < BGUN_MOD_CHARGE_ROUNDS; j++) {
		struct coord seat;
		struct coord tip;

		if (held->launch_part_base < 0
				|| !bgunModPartWorldPos(modeldef, matrices, held->launch_part_base + j, &seat)) {
			continue;
		}

		charge->socketpos[j] = seat;
		charge->socketvalid[j] = true;

		if (held->launch_aim_part_base >= 0
				&& bgunModPartWorldPos(modeldef, matrices, held->launch_aim_part_base + j, &tip)) {
			charge->socketdir[j].x = tip.x - seat.x;
			charge->socketdir[j].y = tip.y - seat.y;
			charge->socketdir[j].z = tip.z - seat.z;

			if (!bgunModNormalise(&charge->socketdir[j])) {
				charge->socketdir[j].x = charge->socketdir[j].y = charge->socketdir[j].z = 0.0f;
			}
		}
	}

	/* The crosshair: camera forward (and up) in world space. */
	mtx4RotateVecInPlace(camGetProjectionMtxF(), &forward);

	if (bgunModNormalise(&forward)) {
		struct coord up = {0.0f, 1.0f, 0.0f};

		mtx4RotateVecInPlace(camGetProjectionMtxF(), &up);
		bgunModNormalise(&up);
		charge->aimdir = forward;
		charge->aimup = up;
		charge->aimvalid = true;
	}
}

bool bgunModTickIncAttackCharge(struct handweaponinfo *info, s32 handnum, struct hand *hand,
		const weapon_graph_held_function_t *held, s32 chargetime60)
{
	struct bgunmodcharge *charge = bgunModChargeForCurrentPlayer();
	struct weaponfunc *func = gsetGetWeaponFunction(&hand->gset);
	s32 ammoindex = func ? func->ammoindex : -1;

	if (charge == NULL || held == NULL) {
		return true;
	}

	if (hand->statecycles == 0 && !charge->active) {
		s32 loaded = (ammoindex == 0 || ammoindex == 1) ? hand->loadedammo[ammoindex] : 0;

		if (handnum != HAND_RIGHT || loaded <= 0) {
			return true;
		}

		memset(charge, 0, sizeof(*charge));
		charge->active = true;
		charge->weaponnum = hand->gset.weaponnum;
		charge->funcnum = hand->gset.weaponfunc;
		charge->ammoindex = ammoindex;
		charge->count = loaded < BGUN_MOD_CHARGE_ROUNDS ? loaded : BGUN_MOD_CHARGE_ROUNDS;
		charge->startframe60 = g_Vars.lvframe60;
		/* held-weapon-events.md slice 5: the charge's time is its plan's. */
		charge->releaseframe60 = g_Vars.lvframe60 + chargetime60;
		charge->gset = hand->gset;
		charge->ownerprop = g_Vars.currentplayer->prop;

		if (held->charge_sound >= 0 && g_Vars.lvupdate240 > 0) {
			sndStart(var80095200, held->charge_sound, &charge->chargesound, -1, -1, -1, -1, -1);
		}

		sysLogPrintf(LOG_NOTE, "BONDGUN.CHARGE: start weapon=%d player=%d func=%d count=%d release_in=%d",
			charge->weaponnum, g_Vars.currentplayernum, charge->funcnum, charge->count,
			chargetime60);
	}

	/* Committed: the hand stays busy (no fire, reload, mode or weapon change)
	 * until the match releases the charge. Death resets the hand, not this. */
	if (charge->active && charge->weaponnum == hand->gset.weaponnum) {
		return false;
	}

	return true;
}

static void bgunModChargeRelease(struct bgunmodcharge *charge)
{
	struct player *player = g_Vars.currentplayer;
	struct prop *ownerprop = charge->ownerprop;
	struct chrdata *chr = ownerprop ? ownerprop->chr : NULL;
	const weapon_graph_held_function_t *held =
		weaponGraphRuntimeGetHeldFunctionForGameplay(charge->weaponnum, charge->funcnum);
	const weapon_graph_entity_runtime_t *entity =
		held ? weaponGraphRuntimeGetEntityForHeldFunction(held) : NULL;
	const weapon_graph_projectile_runtime_t *projectile =
		held ? weaponGraphRuntimeGetProjectileForHeldFunction(held) : NULL;
	s32 dead = player->isdead ? 1 : 0;
	struct coord centre = {0.0f, 0.0f, 0.0f};
	struct coord aim;
	struct coord fanup;
	struct coord fanaxis;
	struct coord spine;
	struct coord fanfirst = {0.0f, 0.0f, 1.0f};
	struct coord fanlast = {0.0f, 0.0f, 1.0f};
	f32 speed;
	s32 sockets = 0;
	s32 launched = 0;
	s32 j;

	if (charge->chargesound && sndGetState(charge->chargesound) != AL_STOPPED) {
		audioStop(charge->chargesound);
	}

	charge->chargesound = NULL;

	if (held == NULL || chr == NULL || g_NetMode == NETMODE_CLIENT) {
		sysLogPrintf(LOG_NOTE, "BONDGUN.CHARGE: released weapon=%d player=%d count=%d launched=0 dead=%d skipped=%s",
			charge->weaponnum, g_Vars.currentplayernum, charge->count, dead,
			g_NetMode == NETMODE_CLIENT ? "client" : "no_owner");
		charge->active = false;
		return;
	}

	/* Centre the fan on the crosshair: the sockets' mean direction is the
	 * gun's forward, rolled with the gun; rotate it onto the aim. */
	for (j = 0; j < charge->count; j++) {
		if (charge->socketvalid[j]) {
			centre.x += charge->socketdir[j].x;
			centre.y += charge->socketdir[j].y;
			centre.z += charge->socketdir[j].z;
			sockets++;
		}
	}

	/* The aim and the sockets are captured while the first-person gun is
	 * drawn. A remote player's charge on the host has neither: aim along
	 * the player's look instead, and fan as the sockets would (below). */
	if (charge->aimvalid) {
		aim = charge->aimdir;
		fanup = charge->aimup;
	} else if (sockets > 0) {
		aim = centre;
		fanup = player->bond2.unk28;
	} else {
		aim = player->bond2.unk1c;
		fanup = player->bond2.unk28;
	}

	if (!bgunModNormalise(&aim)) {
		aim.x = 0.0f;
		aim.y = 0.0f;
		aim.z = 1.0f;
	}

	if (!bgunModNormalise(&centre)) {
		centre = aim;
	}

	/* The gun's up, square to the aim and rolled with the gun: the line the
	 * socket-less fan lies along. */
	{
		f32 along = bgunModDot(&fanup, &aim);
		struct coord side;
		f32 roll = bgunModGetRoll();

		fanup.x -= aim.x * along;
		fanup.y -= aim.y * along;
		fanup.z -= aim.z * along;

		if (!bgunModNormalise(&fanup)) {
			fanup.x = 0.0f;
			fanup.y = 1.0f;
			fanup.z = 0.0f;
		}

		side.x = aim.y * fanup.z - aim.z * fanup.y;
		side.y = aim.z * fanup.x - aim.x * fanup.z;
		side.z = aim.x * fanup.y - aim.y * fanup.x;
		fanaxis.x = fanup.x * cosf(roll) + side.x * sinf(roll);
		fanaxis.y = fanup.y * cosf(roll) + side.y * sinf(roll);
		fanaxis.z = fanup.z * cosf(roll) + side.z * sinf(roll);
	}

	/* The fan (launch_fan_deg; Mike's pick, plus or minus 15 degrees): the
	 * rounds spread evenly from -fan to +fan along the gun's spine. The
	 * spine's line is the authored socket fan, first seat's aim to last
	 * seat's, re-centred on the crosshair, so it rolls with the gun and keeps
	 * the authored order; without two aimed sockets it is the rolled gun's
	 * up. */
	spine = fanaxis;

	if (charge->count >= 2 && charge->socketvalid[0] && charge->socketvalid[charge->count - 1]
			&& bgunModLength(&charge->socketdir[0]) > 0.5f
			&& bgunModLength(&charge->socketdir[charge->count - 1]) > 0.5f) {
		struct coord first = charge->socketdir[0];
		struct coord last = charge->socketdir[charge->count - 1];
		struct coord line;
		f32 along;

		bgunModRotateFromTo(&centre, &aim, &first);
		bgunModRotateFromTo(&centre, &aim, &last);
		line.x = last.x - first.x;
		line.y = last.y - first.y;
		line.z = last.z - first.z;
		along = bgunModDot(&line, &aim);
		line.x -= aim.x * along;
		line.y -= aim.y * along;
		line.z -= aim.z * along;

		if (bgunModNormalise(&line)) {
			spine = line;
		}
	}

	speed = held->launch_speed > 0.0f ? held->launch_speed : 20.0f;

	for (j = 0; j < charge->count; j++) {
		f32 angle = weaponGraphChargeFanAngleDeg(j, charge->count, held->launch_fan_deg) * (M_PI / 180.0f);
		struct coord spawnpos;
		struct coord dir;
		struct coord velocity;
		RoomNum spawnrooms[8];
		Mtxf rot;
		struct defaultobj *obj;

		dir.x = aim.x * cosf(angle) + spine.x * sinf(angle);
		dir.y = aim.y * cosf(angle) + spine.y * sinf(angle);
		dir.z = aim.z * cosf(angle) + spine.z * sinf(angle);
		bgunModNormalise(&dir);

		if (j == 0) {
			fanfirst = dir;
		}

		fanlast = dir;

		if (charge->socketvalid[j]) {
			spawnpos = charge->socketpos[j];
		} else {
			/* No socket: from just in front of the owner's eyes, a little
			 * apart along the spine. */
			f32 offset = (f32)j - (charge->count - 1) * 0.5f;

			spawnpos.x = ownerprop->pos.x + aim.x * 20.0f + spine.x * offset * 3.0f;
			spawnpos.y = ownerprop->pos.y + aim.y * 20.0f + spine.y * offset * 3.0f;
			spawnpos.z = ownerprop->pos.z + aim.z * 20.0f + spine.z * offset * 3.0f;
		}

		/* Spawn at the socket only if the owner can see it (as a throw does
		 * from the muzzle); the line check also gives the socket's rooms. */
		playerSetPerimEnabled(ownerprop, false);

		if (cdTestLos11(&ownerprop->pos, ownerprop->rooms, &spawnpos, spawnrooms, CDTYPE_ALL) == CDRESULT_COLLISION) {
			spawnpos = ownerprop->pos;
			roomsCopy(ownerprop->rooms, spawnrooms);
		}

		playerSetPerimEnabled(ownerprop, true);

		velocity.x = dir.x * speed;
		velocity.y = dir.y * speed;
		velocity.z = dir.z * speed;
		bgunModLoadBasis(&rot, &dir);

		obj = bgunCreateThrownProjectile2(chr, &charge->gset, &spawnpos, spawnrooms, &rot, &velocity);

		if (obj == NULL) {
			continue;
		}

		/* A mine that arms on landing is inert in flight. */
		if (obj->type == OBJTYPE_WEAPON && entity != NULL && entity->has_proxy_trigger
				&& entity->proxy_arm_on_land) {
			((struct weaponobj *) obj)->timer240 = -1;
		}

		/* The thrown-object spawn applies no projectile scale; do it as the
		 * fired-projectile spawn does (model scale and the rotation basis). */
		if (projectile != NULL && projectile->has_scale && projectile->scale > 0.0f
				&& projectile->scale != 1.0f && obj->model != NULL) {
			Mtxf scaled;

			obj->model->scale *= projectile->scale;
			mtx3ToMtx4(obj->realrot, &scaled);
			mtx00015f04(projectile->scale, &scaled);
			mtx4ToMtx3(&scaled, obj->realrot);
		}

		if (g_NetMode == NETMODE_SERVER) {
			netmsgSvcPropSpawnWrite(&g_NetMsgRel, obj->prop);
			netmsgSvcPropMoveWrite(&g_NetMsgRel, obj->prop, NULL);
		}

		launched++;
		sysLogPrintf(LOG_NOTE, "BONDGUN.CHARGE: launch weapon=%d round=%d socket=%d at=(%.0f,%.0f,%.0f) dir=(%.3f,%.3f,%.3f)",
			charge->weaponnum, j, charge->socketvalid[j], spawnpos.x, spawnpos.y, spawnpos.z,
			dir.x, dir.y, dir.z);
	}

	/* How the fan lies on screen (vertical: 1 = a vertical line, gun upright;
	 * 0 = a horizontal one, gun rolled a quarter turn) and its full spread in
	 * degrees, first round to last. */
	if (charge->count >= 2) {
		struct coord line;
		struct coord screenup = charge->aimvalid ? charge->aimup : fanup;
		f32 cosspread = bgunModDot(&fanfirst, &fanlast);

		cosspread = cosspread > 1.0f ? 1.0f : (cosspread < -1.0f ? -1.0f : cosspread);
		line.x = fanlast.x - fanfirst.x;
		line.y = fanlast.y - fanfirst.y;
		line.z = fanlast.z - fanfirst.z;

		if (!bgunModNormalise(&line)) {
			line = spine;
		}

		sysLogPrintf(LOG_NOTE, "BONDGUN.CHARGE: fan weapon=%d vertical=%.2f roll=%.0f spread=%.0f sockets=%d",
			charge->weaponnum, fabsf(bgunModDot(&line, &screenup)),
			bgunModGetRoll() * (180.0f / M_PI), acosf(cosspread) * (180.0f / M_PI), sockets);
	}

	if (held->launch_sound >= 0 && g_Vars.lvupdate240 > 0) {
		sndStart(var80095200, held->launch_sound, NULL, -1, -1, -1, -1, -1);
	}

	/* The launched crystals leave their sockets. */
	if (!dead && player->hands[HAND_RIGHT].gset.weaponnum == charge->weaponnum
			&& (charge->ammoindex == 0 || charge->ammoindex == 1)) {
		struct hand *hand = &player->hands[HAND_RIGHT];

		hand->loadedammo[charge->ammoindex] -= charge->count;

		if (hand->loadedammo[charge->ammoindex] < 0) {
			hand->loadedammo[charge->ammoindex] = 0;
		}
	}

	sysLogPrintf(LOG_NOTE, "BONDGUN.CHARGE: released weapon=%d player=%d count=%d launched=%d dead=%d sockets=%d",
		charge->weaponnum, g_Vars.currentplayernum, charge->count, launched, dead, sockets);
	charge->active = false;
}

void bgunModChargeTick(void)
{
	struct bgunmodcharge *charge = bgunModChargeForCurrentPlayer();

	if (charge == NULL || !charge->active) {
		return;
	}

	if (g_Vars.lvframe60 - charge->releaseframe60 < 0) {
		return;
	}

	bgunModChargeRelease(charge);
}

/* ------------------------------------------------------------------------
 * Bots and the committed charge. A bot "fires" through chrTickShoot; for a
 * charge function the shot commits every loaded round instead, and the
 * release launches them as a fan of mines from the bot's gun toward its
 * target: vertical, since bots do not roll. Like a player's, it cannot be
 * cancelled and fires even if the bot dies first.
 * ------------------------------------------------------------------------ */

static struct bgunmodcharge *bgunModBotChargeFind(struct prop *ownerprop, bool alloc)
{
	s32 i;
	s32 freeslot = -1;

	for (i = 0; i < BGUN_MOD_BOT_CHARGES; i++) {
		if (s_BgunModBotCharges[i].active && s_BgunModBotCharges[i].ownerprop == ownerprop) {
			return &s_BgunModBotCharges[i];
		}

		if (!s_BgunModBotCharges[i].active && freeslot < 0) {
			freeslot = i;
		}
	}

	return alloc && freeslot >= 0 ? &s_BgunModBotCharges[freeslot] : NULL;
}

bool bgunModBotChargeShot(struct chrdata *chr, s32 handnum, struct gset *gset)
{
	const weapon_graph_held_function_t *held;
	struct bgunmodcharge *charge;
	struct weaponfunc *func;
	s32 loaded;
	s32 chargetime60 = 0;

	if (chr == NULL || chr->aibot == NULL || chr->prop == NULL || gset == NULL
			|| gset->weaponnum < WEAPON_CUSTOM_START) {
		return false;
	}

	held = weaponGraphRuntimeGetHeldFunctionForGameplay(gset->weaponnum, gset->weaponfunc);

	/* slice 5 (review): the function's plan, as the player's hand reads it. */
	if (held == NULL || !bgunModFunctionCharges(gset->weaponnum, gset->weaponfunc, &chargetime60)) {
		return false;
	}

	/* Charging already: the trigger is held, nothing else fires. */
	if (bgunModBotChargeFind(chr->prop, false) != NULL) {
		return true;
	}

	loaded = chr->aibot->loadedammo[handnum];

	if (loaded <= 0) {
		return true;
	}

	charge = bgunModBotChargeFind(chr->prop, true);

	if (charge == NULL) {
		return true;
	}

	func = gsetGetWeaponFunction(gset);
	memset(charge, 0, sizeof(*charge));
	charge->active = true;
	charge->weaponnum = gset->weaponnum;
	charge->funcnum = gset->weaponfunc;
	charge->ammoindex = func ? func->ammoindex : -1;
	charge->count = loaded < BGUN_MOD_CHARGE_ROUNDS ? loaded : BGUN_MOD_CHARGE_ROUNDS;
	charge->startframe60 = g_Vars.lvframe60;
	charge->releaseframe60 = g_Vars.lvframe60 + chargetime60;
	charge->gset = *gset;
	charge->ownerprop = chr->prop;
	s_BgunModBotChargeHand[charge - s_BgunModBotCharges] = handnum;

	if (held->charge_sound >= 0 && g_Vars.lvupdate240 > 0) {
		psCreate(NULL, chr->prop, held->charge_sound, -1, -1, 0, 0, PSTYPE_NONE, NULL, -1.0f, NULL, -1, -1.0f, -1.0f, -1.0f);
	}

	sysLogPrintf(LOG_NOTE, "BONDGUN.CHARGE: start weapon=%d bot=%d func=%d count=%d release_in=%d",
		charge->weaponnum, (s32)chr->chrnum, charge->funcnum, charge->count, chargetime60);

	return true;
}

static void bgunModBotChargeRelease(struct bgunmodcharge *charge, s32 handnum)
{
	struct prop *ownerprop = charge->ownerprop;
	struct chrdata *chr = ownerprop ? ownerprop->chr : NULL;
	const weapon_graph_held_function_t *held =
		weaponGraphRuntimeGetHeldFunctionForGameplay(charge->weaponnum, charge->funcnum);
	const weapon_graph_entity_runtime_t *entity =
		held ? weaponGraphRuntimeGetEntityForHeldFunction(held) : NULL;
	const weapon_graph_projectile_runtime_t *projectile =
		held ? weaponGraphRuntimeGetProjectileForHeldFunction(held) : NULL;
	struct prop *target;
	struct coord gunpos;
	struct coord aim;
	struct coord up = {0.0f, 1.0f, 0.0f};
	struct coord right;
	struct coord fanup;
	struct coord fanfirst = {0.0f, 0.0f, 1.0f};
	struct coord fanlast = {0.0f, 0.0f, 1.0f};
	f32 speed;
	s32 dead;
	s32 launched = 0;
	s32 j;

	charge->active = false;

	if (held == NULL || chr == NULL || chr->aibot == NULL || g_NetMode == NETMODE_CLIENT) {
		sysLogPrintf(LOG_NOTE, "BONDGUN.CHARGE: released weapon=%d bot=%d count=%d launched=0 skipped=%s",
			charge->weaponnum, chr ? (s32)chr->chrnum : -1, charge->count,
			g_NetMode == NETMODE_CLIENT ? "client" : "no_owner");
		return;
	}

	dead = chrIsDead(chr) ? 1 : 0;

	if (!chrGetGunPos(chr, handnum, &gunpos)) {
		gunpos = ownerprop->pos;
		gunpos.y += 30.0f;
	}

	/* At the target's chest if it has one, else straight along the bot's
	 * aim. */
	target = dead ? NULL : chrGetTargetProp(chr);

	if (target != NULL) {
		aim.x = target->pos.x - gunpos.x;
		aim.y = target->pos.y - 25.0f - gunpos.y;
		aim.z = target->pos.z - gunpos.z;
	} else {
		f32 roty = chrGetAimAngle(chr);

		aim.x = sinf(roty);
		aim.y = 0.0f;
		aim.z = cosf(roty);
	}

	if (!bgunModNormalise(&aim)) {
		aim.x = 0.0f;
		aim.y = 0.0f;
		aim.z = 1.0f;
	}

	right.x = aim.y * up.z - aim.z * up.y;
	right.y = aim.z * up.x - aim.x * up.z;
	right.z = aim.x * up.y - aim.y * up.x;

	if (!bgunModNormalise(&right)) {
		right.x = 1.0f;
		right.y = 0.0f;
		right.z = 0.0f;
	}

	fanup.x = right.y * aim.z - right.z * aim.y;
	fanup.y = right.z * aim.x - right.x * aim.z;
	fanup.z = right.x * aim.y - right.y * aim.x;
	bgunModNormalise(&fanup);

	speed = held->launch_speed > 0.0f ? held->launch_speed : 20.0f;

	for (j = 0; j < charge->count; j++) {
		/* The function's fan (launch_fan_deg, plus or minus 15 by default),
		 * centred on the aim, one above the other: bots do not roll. */
		f32 angle = weaponGraphChargeFanAngleDeg(j, charge->count, held->launch_fan_deg) * (M_PI / 180.0f);
		struct coord dir;
		struct coord spawnpos;
		struct coord velocity;
		RoomNum spawnrooms[8];
		Mtxf rot;
		struct defaultobj *obj;

		dir.x = aim.x * cosf(angle) + fanup.x * sinf(angle);
		dir.y = aim.y * cosf(angle) + fanup.y * sinf(angle);
		dir.z = aim.z * cosf(angle) + fanup.z * sinf(angle);
		bgunModNormalise(&dir);

		if (j == 0) {
			fanfirst = dir;
		}

		fanlast = dir;

		spawnpos.x = gunpos.x + fanup.x * (j * 3.0f);
		spawnpos.y = gunpos.y + fanup.y * (j * 3.0f);
		spawnpos.z = gunpos.z + fanup.z * (j * 3.0f);

		propSetPerimEnabled(ownerprop, false);

		if (cdTestLos11(&ownerprop->pos, ownerprop->rooms, &spawnpos, spawnrooms, CDTYPE_ALL) == CDRESULT_COLLISION) {
			spawnpos = ownerprop->pos;
			roomsCopy(ownerprop->rooms, spawnrooms);
		}

		propSetPerimEnabled(ownerprop, true);

		velocity.x = dir.x * speed;
		velocity.y = dir.y * speed;
		velocity.z = dir.z * speed;
		bgunModLoadBasis(&rot, &dir);

		obj = bgunCreateThrownProjectile2(chr, &charge->gset, &spawnpos, spawnrooms, &rot, &velocity);

		if (obj == NULL) {
			continue;
		}

		if (obj->type == OBJTYPE_WEAPON && entity != NULL && entity->has_proxy_trigger
				&& entity->proxy_arm_on_land) {
			((struct weaponobj *) obj)->timer240 = -1;
		}

		if (projectile != NULL && projectile->has_scale && projectile->scale > 0.0f
				&& projectile->scale != 1.0f && obj->model != NULL) {
			Mtxf scaled;

			obj->model->scale *= projectile->scale;
			mtx3ToMtx4(obj->realrot, &scaled);
			mtx00015f04(projectile->scale, &scaled);
			mtx4ToMtx3(&scaled, obj->realrot);
		}

		if (g_NetMode == NETMODE_SERVER) {
			netmsgSvcPropSpawnWrite(&g_NetMsgRel, obj->prop);
			netmsgSvcPropMoveWrite(&g_NetMsgRel, obj->prop, NULL);
		}

		launched++;
	}

	if (held->launch_sound >= 0 && g_Vars.lvupdate240 > 0) {
		psCreate(NULL, ownerprop, held->launch_sound, -1, -1, 0, 0, PSTYPE_NONE, NULL, -1.0f, NULL, -1, -1.0f, -1.0f, -1.0f);
	}

	if (charge->count >= 2) {
		f32 cosspread = bgunModDot(&fanfirst, &fanlast);

		cosspread = cosspread > 1.0f ? 1.0f : (cosspread < -1.0f ? -1.0f : cosspread);
		sysLogPrintf(LOG_NOTE, "BONDGUN.CHARGE: fan weapon=%d vertical=1.00 roll=0 spread=%.0f bot=%d",
			charge->weaponnum, acosf(cosspread) * (180.0f / M_PI), (s32)chr->chrnum);
	}

	/* The launched crystals leave the bot's clip, if the bot still holds the
	 * gun they came from. */
	if (chr->aibot->weaponnum == charge->weaponnum) {
		chr->aibot->loadedammo[handnum] -= charge->count;

		if (chr->aibot->loadedammo[handnum] < 0) {
			chr->aibot->loadedammo[handnum] = 0;
		}
	}

	/* The next charge waits for a primary magazine (bgunModBotFired). */
	if (chr->aibot->aibotnum >= 0 && chr->aibot->aibotnum < MAX_BOTS) {
		s_BgunModBotChargedLast[chr->aibot->aibotnum] = 1;
	}

	sysLogPrintf(LOG_NOTE, "BONDGUN.CHARGE: released weapon=%d bot=%d count=%d launched=%d dead=%d",
		charge->weaponnum, (s32)chr->chrnum, charge->count, launched, dead);
}

void bgunModBotChargeTick(void)
{
	s32 i;

	/* lv.c calls this once per player view; run once a frame. */
	if (s_BgunModBotChargeFrame == g_Vars.lvframe60) {
		return;
	}

	s_BgunModBotChargeFrame = g_Vars.lvframe60;

	for (i = 0; i < BGUN_MOD_BOT_CHARGES; i++) {
		struct bgunmodcharge *charge = &s_BgunModBotCharges[i];

		if (charge->active && g_Vars.lvframe60 - charge->releaseframe60 >= 0) {
			bgunModBotChargeRelease(charge, s_BgunModBotChargeHand[i]);
		}
	}
}

bool bgunModFunctionsShareSelfLoadingAmmo(s32 weaponnum, s32 funcnum1, s32 funcnum2)
{
	struct weaponfunc *func1;
	struct weaponfunc *func2;

	if (weaponnum < WEAPON_CUSTOM_START) {
		return false;
	}

	func1 = weaponGetFunctionById(weaponnum, funcnum1);
	func2 = weaponGetFunctionById(weaponnum, funcnum2);

	return func1 != NULL && func2 != NULL && func1->ammoindex >= 0
		&& func1->ammoindex == func2->ammoindex
		&& bgunModAmmoIsSelfLoading(weaponnum, func1->ammoindex);
}

s32 bgunModBotChooseFunction(struct chrdata *chr, s32 weaponnum, s32 funcnum)
{
	const weapon_graph_held_function_t *charge;
	const struct aibotweaponpreference *pref;
	struct prop *target;
	s32 slot;
	s32 clip;
	s32 mpindex;
	f32 dist;

	if (chr == NULL || chr->aibot == NULL || chr->prop == NULL
			|| weaponnum < WEAPON_CUSTOM_START || funcnum != FUNC_PRIMARY) {
		return funcnum;
	}

	charge = weaponGraphRuntimeGetHeldFunctionForGameplay(weaponnum, FUNC_SECONDARY);
	pref = catalogManagerGetWeaponBotPref(weaponnum);

	if (charge == NULL || !bgunModFunctionCharges(weaponnum, FUNC_SECONDARY, NULL) || pref == NULL
			|| !pref->hassecammogoal) {
		return funcnum;
	}

	/* A committed charge keeps its function until it is released. */
	if (bgunModBotChargeFind(chr->prop, false) != NULL) {
		return FUNC_SECONDARY;
	}

	slot = chr->aibot->aibotnum;

	/* Charged last time: a primary magazine first. Only a held gun's
	 * magazine counts. */
	if (slot < 0 || slot >= MAX_BOTS || s_BgunModBotChargedLast[slot]
			|| chr->aibot->weaponnum != weaponnum) {
		return funcnum;
	}

	clip = botactGetClipCapacityByFunction(weaponnum, FUNC_SECONDARY);

	if (clip <= 0 || chr->aibot->loadedammo[HAND_RIGHT] < clip) {
		return funcnum;
	}

	target = chr->target != -1 ? chrGetTargetProp(chr) : NULL;

	if (target == NULL || target->chr == NULL) {
		return funcnum;
	}

	mpindex = mpPlayerGetIndex(target->chr);

	if (mpindex < 0 || mpindex >= MAX_MPCHRS) {
		return funcnum;
	}

	dist = chr->aibot->chrdistances[mpindex];

	/* With a full magazine: one line a second at most, per bot. */
	if (s_BgunModBotChooseLogBudget > 0
			&& g_Vars.lvframe60 - s_BgunModBotChooseLogFrame[slot] >= 60) {
		s_BgunModBotChooseLogBudget--;
		s_BgunModBotChooseLogFrame[slot] = g_Vars.lvframe60;
		sysLogPrintf(LOG_NOTE, "BONDGUN.BOT: choose weapon=%d bot=%d loaded=%d/%d target=%d dist=%.0f charge=%d",
			weaponnum, (s32)chr->chrnum, chr->aibot->loadedammo[HAND_RIGHT], clip,
			(s32)target->chr->chrnum, dist,
			dist >= BGUN_MOD_BOT_CHARGE_MIN_DIST && dist <= BGUN_MOD_BOT_CHARGE_MAX_DIST ? 1 : 0);
	}

	return dist >= BGUN_MOD_BOT_CHARGE_MIN_DIST && dist <= BGUN_MOD_BOT_CHARGE_MAX_DIST
		? FUNC_SECONDARY : funcnum;
}

void bgunModBotFired(struct chrdata *chr, s32 handnum, struct gset *gset)
{
	const weapon_graph_held_function_t *held;
	s32 slot;
	s32 func;

	if (chr == NULL || chr->aibot == NULL || gset == NULL
			|| gset->weaponnum < WEAPON_CUSTOM_START) {
		return;
	}

	slot = chr->aibot->aibotnum;
	func = gset->weaponfunc == FUNC_SECONDARY ? 1 : 0;

	if (slot < 0 || slot >= MAX_BOTS) {
		return;
	}

	held = weaponGraphRuntimeGetHeldFunctionForGameplay(gset->weaponnum, gset->weaponfunc);

	if (held != NULL && !bgunModFunctionCharges(gset->weaponnum, gset->weaponfunc, NULL)) {
		s_BgunModBotChargedLast[slot] = 0;
	}

	if (s_BgunModBotFireLogged[slot][func] != gset->weaponnum + 1) {
		s_BgunModBotFireLogged[slot][func] = gset->weaponnum + 1;
		sysLogPrintf(LOG_NOTE, "BONDGUN.BOT: fired weapon=%d func=%d bot=%d hand=%d loaded=%d",
			gset->weaponnum, gset->weaponfunc, (s32)chr->chrnum, handnum,
			chr->aibot->loadedammo[handnum]);
	}
}

/* ------------------------------------------------------------------------
 * Gun roll (ACTION_GUN_ROLL_LEFT / RIGHT). A weapon opts in with the
 * gun_roll_deg setting; base weapons never roll.
 * ------------------------------------------------------------------------ */

static s32 s_BgunModRollLogged[MAX_PLAYERS];
/* The roll actions as last read for each player: bit 0 left, bit 1 right.
 * A local player's go out in its move (v63 UCMD_GUNROLL_*); a remote
 * player's come from there. */
static u8 s_BgunModRollInput[MAX_PLAYERS];

void bgunModRollTick(bool left, bool right)
{
	s32 playernum = g_Vars.currentplayernum;
	s32 weaponnum;
	f32 maxroll;
	f32 target = 0.0f;
	f32 step;
	f32 *roll;
	s32 state;

	if (playernum < 0 || playernum >= MAX_PLAYERS) {
		return;
	}

	s_BgunModRollInput[playernum] = (left ? 1 : 0) | (right ? 2 : 0);
	roll = &s_BgunModRoll[playernum];
	weaponnum = bgunGetWeaponNum(HAND_RIGHT);
	maxroll = loaderPoolGetPublicWeaponRollDeg(weaponnum) * (M_PI / 180.0f);

	if (maxroll > 0.0f && !g_Vars.currentplayer->isdead) {
		if (right && !left) {
			target = maxroll;
		} else if (left && !right) {
			target = -maxroll;
		}
	}

	/* About 600 degrees a second: a quarter turn in 0.15 s. */
	step = (600.0f * (M_PI / 180.0f)) * LVUPDATE60FREAL() / 60.0f;

	if (*roll < target) {
		*roll = *roll + step > target ? target : *roll + step;
	} else if (*roll > target) {
		*roll = *roll - step < target ? target : *roll - step;
	}

	state = *roll == target ? (target > 0.0f ? 1 : target < 0.0f ? -1 : 0) : 2;

	if (state != 2 && state != s_BgunModRollLogged[playernum]) {
		s_BgunModRollLogged[playernum] = state;
		sysLogPrintf(LOG_NOTE, "BONDGUN.ROLL: player=%d weapon=%d roll=%.0f",
			playernum, weaponnum, *roll * (180.0f / M_PI));
	}
}

f32 bgunModGetRoll(void)
{
	s32 playernum = g_Vars.currentplayernum;

	if (playernum < 0 || playernum >= MAX_PLAYERS) {
		return 0.0f;
	}

	return s_BgunModRoll[playernum];
}

u32 bgunModRollUcmd(void)
{
	s32 playernum = g_Vars.currentplayernum;
	u32 ucmd = 0;

	if (playernum < 0 || playernum >= MAX_PLAYERS) {
		return 0;
	}

	if (s_BgunModRollInput[playernum] & 1) {
		ucmd |= UCMD_GUNROLL_LEFT;
	}

	if (s_BgunModRollInput[playernum] & 2) {
		ucmd |= UCMD_GUNROLL_RIGHT;
	}

	return ucmd;
}

/* ------------------------------------------------------------------------
 * Mod mines and the native proxy sweep (MODS pistol ask 6). Bots, NPCs and
 * players set proxies off through coordTriggerProxies with a fixed 250 range
 * and no filters; a mod mine's entity record has its own radius and owner,
 * team and target filters.
 * ------------------------------------------------------------------------ */

s32 bgunModProxyDecision(struct weaponobj *weapon, struct chrdata *chr, struct coord *pos)
{
	const weapon_graph_held_function_t *held = bgunModHeldForWeaponObj(weapon);
	const weapon_graph_entity_runtime_t *entity =
		held ? weaponGraphRuntimeGetEntityForHeldFunction(held) : NULL;
	struct prop *prop = weapon->base.prop;
	f32 radius;
	f32 dx;
	f32 dy;
	f32 dz;
	s32 owner;
	s32 index;

	if (entity == NULL || !entity->has_proxy_trigger || prop == NULL) {
		return -1;
	}

	/* A mine for hostile characters ignores passing objects, including the
	 * crystals still flying from the same volley. */
	if (chr == NULL) {
		return entity->proxy_target_filter_mode == 1 ? 0 : -1;
	}

	radius = entity->proxy_radius > 0.0f ? entity->proxy_radius : 250.0f;
	dx = pos->x - prop->pos.x;
	dy = pos->y - prop->pos.y;
	dz = pos->z - prop->pos.z;

	if (dx * dx + dy * dy + dz * dz >= radius * radius) {
		return 0;
	}

	owner = (weapon->base.hidden & 0xf0000000) >> 28;
	index = g_Vars.normmplayerisrunning ? mpPlayerGetIndex(chr)
		: (chr->prop ? playermgrGetPlayerNumByProp(chr->prop) : -1);

	if (entity->proxy_owner_filter_mode == 1 && index == owner) {
		return 0;
	}

	if (entity->proxy_owner_filter_mode == 2 && index != owner) {
		return 0;
	}

	if (entity->proxy_team_filter_mode == 1 || entity->proxy_target_filter_mode == 1) {
		struct chrdata *ownerchr = NULL;

		if (g_Vars.normmplayerisrunning) {
			ownerchr = mpGetChrFromPlayerIndex(owner);
		} else if (owner >= 0 && owner < MAX_PLAYERS && g_Vars.players[owner] && g_Vars.players[owner]->prop) {
			ownerchr = g_Vars.players[owner]->prop->chr;
		}

		if (ownerchr != NULL && ownerchr != chr && !chrCompareTeams(ownerchr, chr, COMPARE_ENEMIES)) {
			return 0;
		}
	}

	sysLogPrintf(LOG_NOTE, "BONDGUN.CRYSTAL: mine triggered weapon=%d func=%d owner=%d by=%d radius=%.0f",
		weapon->weaponnum, weapon->gunfunc, owner, index, radius);
	return 1;
}
