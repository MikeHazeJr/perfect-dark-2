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
#include "game/propobj.h"
#include "lib/model.h"
#include "lib/mtx.h"
#include "lib/snd.h"
#include "effect_gameplay_runtime.h"
#include "loader_pool.h"
#include "system.h"
#include "weapon_graph_runtime.h"
#include "game/camera.h"
#include "game/game_0b0fd0.h"
#include "game/player.h"
#include "game/playermgr.h"
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

static struct bgunmodcharge *bgunModChargeForCurrentPlayer(void)
{
	s32 playernum = g_Vars.currentplayernum;

	if (playernum < 0 || playernum >= MAX_PLAYERS) {
		return NULL;
	}

	return &s_BgunModCharges[playernum];
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

		if (held != NULL && held->has_charge) {
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
			f32 ramp = chargefunc->charge_time_ticks60 > 0
				? (f32)elapsed / (f32)chargefunc->charge_time_ticks60 : 1.0f;

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

void bgunModResetStage(void)
{
	memset(s_BgunModLanded, 0, sizeof(s_BgunModLanded));
	bgunModChargeResetStage();
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

		dx = other->base.prop->pos.x - prop->pos.x;
		dy = other->base.prop->pos.y - prop->pos.y;
		dz = other->base.prop->pos.z - prop->pos.z;

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
		const weapon_graph_held_function_t *held)
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
		charge->releaseframe60 = g_Vars.lvframe60 + held->charge_time_ticks60;
		charge->gset = hand->gset;
		charge->ownerprop = g_Vars.currentplayer->prop;

		if (held->charge_sound >= 0 && g_Vars.lvupdate240 > 0) {
			sndStart(var80095200, held->charge_sound, &charge->chargesound, -1, -1, -1, -1, -1);
		}

		sysLogPrintf(LOG_NOTE, "BONDGUN.CHARGE: start weapon=%d player=%d func=%d count=%d release_in=%d",
			charge->weaponnum, g_Vars.currentplayernum, charge->funcnum, charge->count,
			held->charge_time_ticks60);
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

	aim = charge->aimvalid ? charge->aimdir : centre;

	if (!bgunModNormalise(&aim)) {
		aim.x = 0.0f;
		aim.y = 0.0f;
		aim.z = 1.0f;
	}

	if (!bgunModNormalise(&centre)) {
		centre = aim;
	}

	speed = held->launch_speed > 0.0f ? held->launch_speed : 20.0f;

	for (j = 0; j < charge->count; j++) {
		struct coord spawnpos;
		struct coord dir;
		struct coord velocity;
		RoomNum spawnrooms[8];
		Mtxf rot;
		struct defaultobj *obj;

		if (charge->socketvalid[j]) {
			spawnpos = charge->socketpos[j];
			dir = charge->socketdir[j];

			if (bgunModLength(&dir) < 0.5f) {
				dir = centre;
			}

			bgunModRotateFromTo(&centre, &aim, &dir);
		} else {
			spawnpos = ownerprop->pos;
			dir = aim;
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

	/* How the fan lies on screen: 1 = a vertical line (gun upright), 0 = a
	 * horizontal one (gun rolled a quarter turn). */
	if (charge->count >= 2 && charge->socketvalid[0] && charge->socketvalid[charge->count - 1]
			&& charge->aimvalid) {
		struct coord first = charge->socketdir[0];
		struct coord last = charge->socketdir[charge->count - 1];
		struct coord spread;

		bgunModRotateFromTo(&centre, &aim, &first);
		bgunModRotateFromTo(&centre, &aim, &last);
		spread.x = last.x - first.x;
		spread.y = last.y - first.y;
		spread.z = last.z - first.z;

		if (bgunModNormalise(&spread)) {
			sysLogPrintf(LOG_NOTE, "BONDGUN.CHARGE: fan weapon=%d vertical=%.2f roll=%.0f",
				charge->weaponnum, fabsf(bgunModDot(&spread, &charge->aimup)),
				bgunModGetRoll() * (180.0f / M_PI));
		}
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
 * Gun roll (ACTION_GUN_ROLL_LEFT / RIGHT). A weapon opts in with the
 * gun_roll_deg setting; base weapons never roll.
 * ------------------------------------------------------------------------ */

static f32 s_BgunModRoll[MAX_PLAYERS];
static s32 s_BgunModRollLogged[MAX_PLAYERS];

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
