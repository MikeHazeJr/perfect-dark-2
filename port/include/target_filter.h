#ifndef _IN_TARGET_FILTER_H
#define _IN_TARGET_FILTER_H

/*
 * held-weapon-events.md slice 6: the target filter. Every place that picks a
 * target (what the player looks at, the lock, homing, auto-aim, the
 * Farsight, the bot Slayer, proximity, sentries) asks one filter about a
 * candidate's facts, which the picker reads from the game; the filter says
 * whether the candidate passes. Each picker's original conditions are its
 * native filter (targetFilterNative), so pd-tests compare them with a copy
 * of the picker's inline tests over every combination of facts. Header-only
 * and free of game state, like weapon_rules.h, so pd-tests link it.
 *
 * Slice 6a puts the lock chain on the filter (what the player looks at, and
 * the lock that takes it); the other pickers follow, then programs choose
 * their filters (slice 6b) and vehicles become lockable (6c).
 */

#include <PR/ultratypes.h>
#include "constants.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A candidate's kind (one of these). */
#define TARGET_KIND_PLAYER   (1u << 0) /* a player (PROPTYPE_PLAYER) */
#define TARGET_KIND_SIMULANT (1u << 1) /* a chr with an aibot */
#define TARGET_KIND_GUARD    (1u << 2) /* any other chr */
#define TARGET_KIND_VEHICLE  (1u << 3) /* a hoverbike, hovercar or chopper */
#define TARGET_KIND_SENTRY   (1u << 4) /* an autogun or a deployed sentry */
#define TARGET_KIND_OBJECT   (1u << 5) /* any other object, weapon or door */
#define TARGET_KINDS_CHARACTERS (TARGET_KIND_PLAYER | TARGET_KIND_SIMULANT | TARGET_KIND_GUARD)
#define TARGET_KINDS_THINGS (TARGET_KIND_VEHICLE | TARGET_KIND_SENTRY | TARGET_KIND_OBJECT)
#define TARGET_KINDS_ALL (TARGET_KINDS_CHARACTERS | TARGET_KINDS_THINGS)

/* What a candidate is to the filter's owner. */
#define TARGET_RELATION_ANYONE   0
#define TARGET_RELATION_ENEMY    1
#define TARGET_RELATION_FRIEND   2
#define TARGET_RELATION_OWNER    3 /* the owner itself */
#define TARGET_RELATION_NONE     4 /* no side: an object, an empty vehicle */

/* Which cloaked candidates a filter sees. */
#define TARGET_SEES_CLOAKED_NEVER   0
#define TARGET_SEES_CLOAKED_WITH_IR 1 /* only while the owner uses the IR scanner */
#define TARGET_SEES_CLOAKED_ALWAYS  2

/* The facts a picker reads from the game about one candidate. */
typedef struct target_facts {
	u32 kind;              /* one TARGET_KIND_* bit, 0 for a prop that is none of them */
	s32 reacts_to_sight;   /* an object or vehicle with OBJFLAG3_REACTTOSIGHT */
	s32 training_target;   /* the CI training stage's targets and hubs (no flag, yet seen) */
	s32 occupied;          /* a vehicle with a rider */
	s32 relation;          /* TARGET_RELATION_* to the owner */
	s32 alive;
	s32 cloaked;
	s32 owner_ir;          /* the owner sees through cloaks (the IR scanner) */
	s32 drugged;           /* a character knocked out or falling from a sedative */
	s32 unexplodable;      /* a character the Farsight cannot blow up (CHRCFLAG_UNEXPLODABLE) */
} target_facts_t;

/* A filter: what passes. */
typedef struct target_filter {
	u32 kinds;               /* TARGET_KIND_* that may pass */
	s32 things_must_react;   /* an object or vehicle passes only if it reacts to sight */
	s32 training_targets;    /* the CI training targets pass though they do not react */
	s32 vehicles_occupied;   /* a vehicle passes only with a rider (Mike: "occupied only") */
	s32 relation;            /* TARGET_RELATION_ANYONE, _ENEMY, _FRIEND, _OWNER */
	s32 alive_only;
	s32 awake_only;          /* a drugged character does not pass */
	s32 explodable_only;     /* an unexplodable character does not pass */
	s32 sees_cloaked;        /* TARGET_SEES_CLOAKED_* */
} target_filter_t;

/* The pickers, for their native filters. */
#define TARGET_PICKER_LOOK 1 /* what the player looks at (lvRender's lookingatprop) */
#define TARGET_PICKER_LOCK 2 /* the lock that takes it (sightCanTargetProp) */
#define TARGET_PICKER_FARSIGHT 3 /* the Farsight's auto-target (farsightChooseTarget) */
#define TARGET_PICKER_BOT_SLAYER 4 /* what sets off a bot's Slayer rocket (rocketTickFbw) */
#define TARGET_PICKER_SENTRY 5 /* what a sentry or autogun with a team mask takes (autogunTick) */

/*
 * The candidate's relation passes the filter's. An empty vehicle and an
 * object have no side: they are nobody's friend, so they pass "enemy" and
 * "anyone" and fail "friend" and "owner" (slice 6 plan: under "any vehicle"
 * a bike its rider left stays a target).
 */
static inline s32 targetFilterRelationPasses(s32 wanted, s32 relation)
{
	switch (wanted) {
	case TARGET_RELATION_ANYONE:
		return 1;
	case TARGET_RELATION_ENEMY:
		return relation == TARGET_RELATION_ENEMY || relation == TARGET_RELATION_NONE;
	case TARGET_RELATION_FRIEND:
		return relation == TARGET_RELATION_FRIEND;
	case TARGET_RELATION_OWNER:
		return relation == TARGET_RELATION_OWNER;
	}
	return 0;
}

static inline s32 targetFilterAccepts(const target_filter_t *f, const target_facts_t *c)
{
	if (!f || !c || !(c->kind & f->kinds)) {
		return 0;
	}
	if ((c->kind & TARGET_KINDS_THINGS) && f->things_must_react && !c->reacts_to_sight
			&& !(f->training_targets && c->training_target)) {
		return 0;
	}
	if ((c->kind & TARGET_KIND_VEHICLE) && f->vehicles_occupied && !c->occupied) {
		return 0;
	}
	if (!targetFilterRelationPasses(f->relation, c->relation)) {
		return 0;
	}
	if (f->alive_only && !c->alive) {
		return 0;
	}
	if (f->awake_only && c->drugged) {
		return 0;
	}
	if (f->explodable_only && c->unexplodable) {
		return 0;
	}
	if (c->cloaked) {
		if (f->sees_cloaked == TARGET_SEES_CLOAKED_NEVER) return 0;
		if (f->sees_cloaked == TARGET_SEES_CLOAKED_WITH_IR && !c->owner_ir) return 0;
	}
	return 1;
}

/*
 * The original's filter of a picker; for the lock, of the gun that locks.
 *
 * - LOOK (lvRender): a character or player, cloaked ones only through the
 *   IR scanner; an object, weapon or door only if it reacts to sight, or
 *   is one of the CI training stage's targets; nothing else (a hoverbike
 *   is an object that does not react, so it is never looked at).
 * - LOCK (sightCanTargetProp): a character, a player, or an object that
 *   reacts to sight (the training targets do not count here); but the
 *   rocket launcher locks whatever the player looks at.
 * - FARSIGHT (farsightChooseTarget): the Farsight's alone; an enemy
 *   character or another player, alive and awake, not cloaked even through
 *   the IR scanner, and not one it cannot blow up. Every other gun has none.
 * - BOT_SLAYER (rocketTickFbw): an enemy of the bot that flies it, alive,
 *   and not invisible to bots (its facts as the bot reads them:
 *   COMPARE_ENEMIES, chrIsDead, botIsTargetInvisible as the cloak).
 * - SENTRY (autogunTick): a character of a team it attacks (its search
 *   picks the team), alive, neither hidden nor cloaked.
 */
static inline void targetFilterNative(s32 picker, s32 weaponnum, target_filter_t *out)
{
	out->kinds = 0;
	out->things_must_react = 1;
	out->training_targets = 0;
	out->vehicles_occupied = 0;
	out->relation = TARGET_RELATION_ANYONE;
	out->alive_only = 0;
	out->awake_only = 0;
	out->explodable_only = 0;
	out->sees_cloaked = TARGET_SEES_CLOAKED_ALWAYS;
	switch (picker) {
	case TARGET_PICKER_LOOK:
		out->kinds = TARGET_KINDS_ALL;
		out->training_targets = 1;
		out->sees_cloaked = TARGET_SEES_CLOAKED_WITH_IR;
		break;
	case TARGET_PICKER_LOCK:
		out->kinds = TARGET_KINDS_ALL;
		if (weaponnum == WEAPON_ROCKETLAUNCHER) {
			out->things_must_react = 0;
		}
		break;
	case TARGET_PICKER_BOT_SLAYER:
		out->kinds = TARGET_KINDS_CHARACTERS;
		out->relation = TARGET_RELATION_ENEMY;
		out->alive_only = 1;
		out->sees_cloaked = TARGET_SEES_CLOAKED_NEVER;
		break;
	case TARGET_PICKER_SENTRY:
		out->kinds = TARGET_KINDS_CHARACTERS;
		out->alive_only = 1;
		out->sees_cloaked = TARGET_SEES_CLOAKED_NEVER;
		break;
	case TARGET_PICKER_FARSIGHT:
		if (weaponnum == WEAPON_FARSIGHT) {
			out->kinds = TARGET_KINDS_CHARACTERS;
			out->relation = TARGET_RELATION_ENEMY;
			out->alive_only = 1;
			out->awake_only = 1;
			out->explodable_only = 1;
			out->sees_cloaked = TARGET_SEES_CLOAKED_NEVER;
		}
		break;
	}
}

#ifdef __cplusplus
}
#endif

#endif
