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

#include <stdio.h>
#include <string.h>
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
/* Wanted only (a filter's, never a candidate's): slice 6b. */
#define TARGET_RELATION_NOT_OWNER  5 /* anyone but the owner */
#define TARGET_RELATION_NOT_FRIEND 6 /* anyone who is not the owner's friend: enemies, the owner, no side */

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
	s32 armed;             /* a character holding a gun, carrying one, or flagged for auto-aim */
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
	s32 armed_only;          /* an unarmed character does not pass */
	s32 sees_cloaked;        /* TARGET_SEES_CLOAKED_* */
} target_filter_t;

/* The pickers, for their native filters. */
#define TARGET_PICKER_LOOK 1 /* what the player looks at (lvRender's lookingatprop) */
#define TARGET_PICKER_LOCK 2 /* the lock that takes it (sightCanTargetProp) */
#define TARGET_PICKER_FARSIGHT 3 /* the Farsight's auto-target (farsightChooseTarget) */
#define TARGET_PICKER_BOT_SLAYER 4 /* what sets off a bot's Slayer rocket (rocketTickFbw) */
#define TARGET_PICKER_SENTRY 5 /* what a sentry or autogun with a team mask takes (autogunTick) */
#define TARGET_PICKER_AUTOAIM 6 /* what auto-aim considers (autoaimTick's standard auto-aim) */

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
	case TARGET_RELATION_NOT_OWNER:
		return relation != TARGET_RELATION_OWNER;
	case TARGET_RELATION_NOT_FRIEND:
		return relation != TARGET_RELATION_FRIEND;
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
	if (f->armed_only && !c->armed) {
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
 * - AUTOAIM (autoaimTick): a character or another player who is not a
 *   friend and is armed (holding or carrying a gun, or flagged for it);
 *   chrCalculateAutoAim then tests its state, its cloak and the screen.
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
	out->armed_only = 0;
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
	case TARGET_PICKER_AUTOAIM:
		out->kinds = TARGET_KINDS_CHARACTERS;
		out->relation = TARGET_RELATION_ENEMY;
		out->armed_only = 1;
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

/*
 * held-weapon-events.md slice 6b: the filter as a node's settings
 * (og.projectile.home, og.weapon.lock_on).
 *
 * - "kinds": names separated by commas: player, simulant, guard, vehicle,
 *   sentry, object, or characters, things, all.
 * - "sight_targets_only": true for a vehicle, sentry or object only if the
 *   stage marks it for the sight (OBJFLAG3_REACTTOSIGHT: the sight turns
 *   red on it); the CMP150's lock. The CI training targets are not marked.
 * - "vehicles": any (a vehicle its rider left passes), or occupied.
 * - "relation": anyone, enemy, friend, owner, not_owner (anyone but the
 *   owner) or not_friend (anyone who is not the owner's friend: enemies,
 *   the owner and those of no side). An empty vehicle or an object is
 *   nobody's friend: it passes enemy, anyone, not_owner and not_friend.
 * - "alive": true for the living only.
 * - "cloaked": never, with_ir (only while the owner uses the IR scanner) or
 *   always.
 *
 * A setting left out takes the "anything" filter's (every kind, things that
 * need not react to sight, any relation, the dead and the cloaked too).
 */
#define TARGET_FILTER_KINDS_PARAM "kinds"
#define TARGET_FILTER_SIGHT_TARGETS_PARAM "sight_targets_only"
#define TARGET_FILTER_VEHICLES_PARAM "vehicles"
#define TARGET_FILTER_RELATION_PARAM "relation"
#define TARGET_FILTER_ALIVE_PARAM "alive"
#define TARGET_FILTER_CLOAKED_PARAM "cloaked"

static inline void targetFilterAnything(target_filter_t *f)
{
	f->kinds = TARGET_KINDS_ALL;
	f->things_must_react = 0;
	f->training_targets = 1;
	f->vehicles_occupied = 0;
	f->relation = TARGET_RELATION_ANYONE;
	f->alive_only = 0;
	f->awake_only = 0;
	f->explodable_only = 0;
	f->armed_only = 0;
	f->sees_cloaked = TARGET_SEES_CLOAKED_ALWAYS;
}

/* Whether two filters pass the same candidates (the CI training targets
 * matter only to a filter whose things must react to sight). */
static inline s32 targetFilterSame(const target_filter_t *a, const target_filter_t *b)
{
	return a->kinds == b->kinds && a->things_must_react == b->things_must_react
		&& (!a->things_must_react || a->training_targets == b->training_targets)
		&& a->vehicles_occupied == b->vehicles_occupied
		&& a->relation == b->relation && a->alive_only == b->alive_only && a->awake_only == b->awake_only
		&& a->explodable_only == b->explodable_only && a->armed_only == b->armed_only
		&& a->sees_cloaked == b->sees_cloaked;
}

/* "player,vehicle" into kind bits; -1 for an empty list or a name it does not know. */
static inline s32 targetFilterKindsFromText(const char *text, u32 *out)
{
	static const struct {
		const char *name;
		u32 kinds;
	} names[] = {
		{ "player", TARGET_KIND_PLAYER }, { "simulant", TARGET_KIND_SIMULANT }, { "guard", TARGET_KIND_GUARD },
		{ "vehicle", TARGET_KIND_VEHICLE }, { "sentry", TARGET_KIND_SENTRY }, { "object", TARGET_KIND_OBJECT },
		{ "characters", TARGET_KINDS_CHARACTERS }, { "things", TARGET_KINDS_THINGS }, { "all", TARGET_KINDS_ALL },
	};
	u32 kinds = 0;
	const char *p = text;

	if (!text || !out) return -1;
	while (*p) {
		char word[16];
		s32 n = 0;
		s32 known = 0;
		u32 i;

		while (*p == ' ' || *p == ',') p++;
		if (!*p) break;
		while (*p && *p != ',' && *p != ' ') {
			if (n >= (s32)sizeof(word) - 1) return -1;
			word[n++] = *p++;
		}
		word[n] = '\0';
		for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
			if (strcmp(word, names[i].name) == 0) {
				kinds |= names[i].kinds;
				known = 1;
			}
		}
		if (!known) return -1;
	}
	if (!kinds) return -1;
	*out = kinds;
	return 0;
}

/* The filter a node's settings name, over "anything": 1 when it names any
 * of them, 0 when it names none, -1 when one is malformed (*bad_key names
 * it; the filter keeps that setting's default). */
typedef const char *(*target_text_fn)(const char *key, void *user);
static inline s32 targetFilterFromSettings(target_text_fn get, void *user, target_filter_t *out, const char **bad_key)
{
	const char *text;
	s32 named = 0;
	s32 ok = 1;

	if (bad_key) *bad_key = NULL;
	targetFilterAnything(out);
	if (!get) return 0;
#define TARGET_FILTER_BAD(key) do { ok = 0; if (bad_key && !*bad_key) *bad_key = (key); } while (0)
	if ((text = get(TARGET_FILTER_KINDS_PARAM, user)) != NULL) {
		u32 kinds;

		named = 1;
		if (targetFilterKindsFromText(text, &kinds) == 0) out->kinds = kinds;
		else TARGET_FILTER_BAD(TARGET_FILTER_KINDS_PARAM);
	}
	if ((text = get(TARGET_FILTER_SIGHT_TARGETS_PARAM, user)) != NULL) {
		named = 1;
		if (strcmp(text, "true") == 0) {
			out->things_must_react = 1;
			out->training_targets = 0;
		} else if (strcmp(text, "false") == 0) {
			out->things_must_react = 0;
		} else {
			TARGET_FILTER_BAD(TARGET_FILTER_SIGHT_TARGETS_PARAM);
		}
	}
	if ((text = get(TARGET_FILTER_VEHICLES_PARAM, user)) != NULL) {
		named = 1;
		if (strcmp(text, "any") == 0) out->vehicles_occupied = 0;
		else if (strcmp(text, "occupied") == 0) out->vehicles_occupied = 1;
		else TARGET_FILTER_BAD(TARGET_FILTER_VEHICLES_PARAM);
	}
	if ((text = get(TARGET_FILTER_RELATION_PARAM, user)) != NULL) {
		named = 1;
		if (strcmp(text, "anyone") == 0) out->relation = TARGET_RELATION_ANYONE;
		else if (strcmp(text, "enemy") == 0) out->relation = TARGET_RELATION_ENEMY;
		else if (strcmp(text, "friend") == 0) out->relation = TARGET_RELATION_FRIEND;
		else if (strcmp(text, "owner") == 0) out->relation = TARGET_RELATION_OWNER;
		else if (strcmp(text, "not_owner") == 0) out->relation = TARGET_RELATION_NOT_OWNER;
		else if (strcmp(text, "not_friend") == 0) out->relation = TARGET_RELATION_NOT_FRIEND;
		else TARGET_FILTER_BAD(TARGET_FILTER_RELATION_PARAM);
	}
	if ((text = get(TARGET_FILTER_ALIVE_PARAM, user)) != NULL) {
		named = 1;
		if (strcmp(text, "true") == 0) out->alive_only = 1;
		else if (strcmp(text, "false") == 0) out->alive_only = 0;
		else TARGET_FILTER_BAD(TARGET_FILTER_ALIVE_PARAM);
	}
	if ((text = get(TARGET_FILTER_CLOAKED_PARAM, user)) != NULL) {
		named = 1;
		if (strcmp(text, "never") == 0) out->sees_cloaked = TARGET_SEES_CLOAKED_NEVER;
		else if (strcmp(text, "with_ir") == 0) out->sees_cloaked = TARGET_SEES_CLOAKED_WITH_IR;
		else if (strcmp(text, "always") == 0) out->sees_cloaked = TARGET_SEES_CLOAKED_ALWAYS;
		else TARGET_FILTER_BAD(TARGET_FILTER_CLOAKED_PARAM);
	}
#undef TARGET_FILTER_BAD
	return ok ? named : -1;
}

/* What a filter setting takes, for refusals; NULL for a key that is none. */
static inline const char *targetFilterSettingHelp(const char *key)
{
	if (!key) return NULL;
	if (strcmp(key, TARGET_FILTER_KINDS_PARAM) == 0)
		return "player, simulant, guard, vehicle, sentry, object, characters, things or all, separated by commas";
	if (strcmp(key, TARGET_FILTER_SIGHT_TARGETS_PARAM) == 0) return "true or false";
	if (strcmp(key, TARGET_FILTER_VEHICLES_PARAM) == 0) return "any or occupied";
	if (strcmp(key, TARGET_FILTER_RELATION_PARAM) == 0) return "anyone, enemy, friend, owner, not_owner or not_friend";
	if (strcmp(key, TARGET_FILTER_ALIVE_PARAM) == 0) return "true or false";
	if (strcmp(key, TARGET_FILTER_CLOAKED_PARAM) == 0) return "never, with_ir or always";
	return NULL;
}

/* The filter as JSON members (", \"kinds\": \"vehicle\", ..."); the length,
 * or -1 when it does not fit. */
static inline s32 targetFilterSettingsText(const target_filter_t *f, char *buf, s32 cap)
{
	static const struct {
		const char *name;
		u32 kind;
	} kinds[] = {
		{ "player", TARGET_KIND_PLAYER }, { "simulant", TARGET_KIND_SIMULANT }, { "guard", TARGET_KIND_GUARD },
		{ "vehicle", TARGET_KIND_VEHICLE }, { "sentry", TARGET_KIND_SENTRY }, { "object", TARGET_KIND_OBJECT },
	};
	char list[96];
	s32 len = 0;
	s32 n;
	u32 i;
	s32 group;

	if (!f || !buf || cap <= 0) return -1;
	list[0] = '\0';
	/* A whole group by its name. */
	group = f->kinds == TARGET_KINDS_ALL || f->kinds == TARGET_KINDS_CHARACTERS || f->kinds == TARGET_KINDS_THINGS;
	if (group) {
		snprintf(list, sizeof(list), "%s", f->kinds == TARGET_KINDS_ALL ? "all"
			: f->kinds == TARGET_KINDS_CHARACTERS ? "characters" : "things");
	}
	for (i = 0; !group && i < sizeof(kinds) / sizeof(kinds[0]); i++) {
		if (f->kinds & kinds[i].kind) {
			n = snprintf(list + len, sizeof(list) - (size_t)len, "%s%s", len ? "," : "", kinds[i].name);
			if (n < 0 || n >= (s32)sizeof(list) - len) return -1;
			len += n;
		}
	}
	n = snprintf(buf, (size_t)cap,
		", \"" TARGET_FILTER_KINDS_PARAM "\": \"%s\", \"" TARGET_FILTER_SIGHT_TARGETS_PARAM "\": %s, \""
		TARGET_FILTER_VEHICLES_PARAM "\": \"%s\", \""
		TARGET_FILTER_RELATION_PARAM "\": \"%s\", \"" TARGET_FILTER_ALIVE_PARAM "\": %s, \""
		TARGET_FILTER_CLOAKED_PARAM "\": \"%s\"",
		list, f->things_must_react ? "true" : "false", f->vehicles_occupied ? "occupied" : "any",
		f->relation == TARGET_RELATION_ENEMY ? "enemy" : f->relation == TARGET_RELATION_FRIEND ? "friend"
			: f->relation == TARGET_RELATION_OWNER ? "owner" : f->relation == TARGET_RELATION_NOT_OWNER ? "not_owner"
			: f->relation == TARGET_RELATION_NOT_FRIEND ? "not_friend" : "anyone",
		f->alive_only ? "true" : "false",
		f->sees_cloaked == TARGET_SEES_CLOAKED_NEVER ? "never"
			: f->sees_cloaked == TARGET_SEES_CLOAKED_WITH_IR ? "with_ir" : "always");
	return n < 0 || n >= cap ? -1 : n;
}

/*
 * held-weapon-events.md slice 6b: a mod mine's filter from its entity record
 * (bgunModProxyDecision), for a character within its radius: its owner
 * filter (1: the owner never sets it off, 2: only the owner does), its team
 * filter and its target filter (either 1: the owner's friends do not; the
 * target filter also ignores moving objects, the record's other branch).
 * The record's quirk is kept: with no owner filter, "hostile only" still lets
 * the owner set off its own mine (not_friend). Its facts are as the record
 * reads them: the owner by player number, a friend by COMPARE_ENEMIES.
 */
static inline void targetFilterModMine(s32 owner_mode, s32 team_mode, s32 target_mode, target_filter_t *out)
{
	const s32 hostile = team_mode == 1 || target_mode == 1;

	targetFilterAnything(out);
	out->kinds = TARGET_KINDS_CHARACTERS;
	if (owner_mode == 2) {
		out->relation = TARGET_RELATION_OWNER;
	} else if (hostile) {
		out->relation = owner_mode == 1 ? TARGET_RELATION_ENEMY : TARGET_RELATION_NOT_FRIEND;
	} else if (owner_mode == 1) {
		out->relation = TARGET_RELATION_NOT_OWNER;
	}
}

#ifdef __cplusplus
}
#endif

#endif
