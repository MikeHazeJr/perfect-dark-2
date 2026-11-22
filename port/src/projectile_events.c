/*
 * projectile_events.c -- record-only projectile events: see
 * projectile_events.h and context/designs/modding/projectile-events.md
 * (slice 1).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>
#include "types.h"
#include "constants.h"
#include "bss.h"
#include "system.h"
#include "net/net.h"
#include "game/game_0b0fd0.h"
#include "weapondata_authored.h"
#include "weapon_graph_runtime.h"
#include "mission_graph_runner.h"
#include "projectile_rules.h"
#include "projectile_events.h"
#include "weapon_events.h"

static const char *const s_ProjectileEventNames[PROJECTILE_EVENT_COUNT] = {
	"spawned", "bounced", "hit_target", "landed", "ended", "detonated",
};

static s32 s_ProjectileEventTotals[PROJECTILE_EVENT_COUNT];

/* The fuse rule's parity counters (projectileFuseModules). */
static s32 s_ProjectileFuseCompared;
static s32 s_ProjectileFuseDiffer;
/* The other rules' (projectileRuleModulesWith). */
static s32 s_ProjectileRuleCompared;
static s32 s_ProjectileRuleDiffer;

/* The seen-table kind of the fuse rule's first-decision line, and of each
 * other rule event's. */
#define PROJECTILE_EVENT_SEEN_FUSE_RULE PROJECTILE_EVENT_COUNT
#define PROJECTILE_EVENT_SEEN_RULE(event_kind) \
	(PROJECTILE_EVENT_COUNT + 1 + ((event_kind) - MISSION_GRAPH_EVENT_PROJECTILE_SPAWNED))

/* The first event of each kind for a weapon and function logs a line;
 * later ones only count. Bounded, so an automatic launcher cannot flood
 * the log. */
#define PROJECTILE_EVENT_SEEN_MAX 384
typedef struct projectile_event_seen {
	s16 weaponnum;
	s8 func;
	s8 kind;
} projectile_event_seen_t;
static projectile_event_seen_t s_ProjectileEventSeen[PROJECTILE_EVENT_SEEN_MAX];
static s32 s_ProjectileEventSeenCount;

static const char *projectileEventWeaponId(s32 weaponnum)
{
	if (weaponnum >= 0 && weaponnum < g_WeaponDataCount
			&& g_WeaponDataCatalogIds[weaponnum]) {
		return g_WeaponDataCatalogIds[weaponnum];
	}
	return "?";
}

/* Whether this is the first event of the kind for the weapon and function;
 * remembers it if so. */
static s32 projectileEventFirst(s32 weaponnum, s32 func, s32 kind)
{
	s32 i;

	for (i = 0; i < s_ProjectileEventSeenCount; i++) {
		if (s_ProjectileEventSeen[i].weaponnum == weaponnum
				&& s_ProjectileEventSeen[i].func == func
				&& s_ProjectileEventSeen[i].kind == kind) {
			return 0;
		}
	}
	if (s_ProjectileEventSeenCount >= PROJECTILE_EVENT_SEEN_MAX) {
		return 0;
	}
	s_ProjectileEventSeen[s_ProjectileEventSeenCount].weaponnum = (s16)weaponnum;
	s_ProjectileEventSeen[s_ProjectileEventSeenCount].func = (s8)func;
	s_ProjectileEventSeen[s_ProjectileEventSeenCount].kind = (s8)kind;
	s_ProjectileEventSeenCount++;
	return 1;
}

/* What is kept for each prop of the stage: the archive a projectile was
 * launched from (projectileEventBind) and, for a weapon object, the arm it
 * chose (projectileArmModules). One record per prop, found by the prop's
 * place in the stage's prop array and valid while the prop's generation is
 * the one the record was made for, so a freed prop's record never names a
 * later object, nor another stage's. A record lasts as long as its prop: a
 * projectile keeps its archive however long it lives. (Slice 1 kept the last
 * 64 launches in a ring, and an object that outlived its entry was known by
 * its own weapon number alone.) The table grows to the largest stage seen. */
typedef struct projectile_record {
	u64 generation;
	u8 bound;          /* a launch named its archive: gunnum, func, asset_id */
	u8 armed;          /* arms is the answer for arm_weaponnum and arm_gunfunc */
	s8 func;
	s8 arm_gunfunc;
	s16 gunnum;        /* the weapon whose function names the archive, or -1 */
	s16 arm_weaponnum;
	u32 arms;
	u8 flew;           /* slice 7: flight is the answer for flight_weaponnum and flight_gunfunc */
	s8 flight_gunfunc;
	s16 flight_weaponnum;
	u32 flight;
	projectile_flight_t flightset;
	char asset_id[CATALOG_ID_LEN];
} projectile_record_t;
static projectile_record_t *s_ProjectileRecords;
static s32 s_ProjectileRecordCap;

/* The prop's record; with `make`, a fresh one when it has none. NULL for a
 * prop outside the stage's array and when the table cannot grow. */
static projectile_record_t *projectileRecordOf(const struct prop *prop, s32 make)
{
	projectile_record_t *record;
	s32 index;

	if (!prop || prop->lifecycle_generation == 0 || !g_Vars.props || g_Vars.maxprops <= 0
			|| (uintptr_t)prop < (uintptr_t)g_Vars.props
			|| (uintptr_t)prop >= (uintptr_t)(g_Vars.props + g_Vars.maxprops)) {
		return NULL;
	}
	index = (s32)(prop - g_Vars.props);
	if (index >= s_ProjectileRecordCap) {
		projectile_record_t *grown;

		if (!make) {
			return NULL;
		}
		grown = realloc(s_ProjectileRecords, (size_t)g_Vars.maxprops * sizeof(*grown));
		if (!grown) {
			return NULL;
		}
		memset(grown + s_ProjectileRecordCap, 0,
			(size_t)(g_Vars.maxprops - s_ProjectileRecordCap) * sizeof(*grown));
		s_ProjectileRecords = grown;
		s_ProjectileRecordCap = g_Vars.maxprops;
	}
	record = &s_ProjectileRecords[index];
	if (record->generation != prop->lifecycle_generation) {
		if (!make) {
			return NULL;
		}
		memset(record, 0, sizeof(*record));
		record->generation = prop->lifecycle_generation;
		record->gunnum = -1;
	}
	return record;
}

static const projectile_record_t *projectileBindFind(const struct defaultobj *obj)
{
	const projectile_record_t *record = obj ? projectileRecordOf(obj->prop, 0) : NULL;

	return record && record->bound ? record : NULL;
}

void projectileEventBind(struct defaultobj *obj, const char *asset_id)
{
	projectile_record_t *bind;
	s32 weaponnum;
	s32 func;

	if (!obj || !obj->prop || obj->prop->lifecycle_generation == 0 || !asset_id || !asset_id[0]) {
		return;
	}
	bind = projectileRecordOf(obj->prop, 1);
	if (!bind) {
		return;
	}
	bind->bound = 1;
	bind->armed = 0; /* its archive is known now: it chooses its arm again */
	bind->flew = 0;  /* and its flight (slice 7) */
	bind->gunnum = -1;
	bind->func = FUNC_PRIMARY;
	snprintf(bind->asset_id, sizeof(bind->asset_id), "%s", asset_id);
	/* The weapon function that names this archive: the gun, for the log, the
	 * first-event table and a deployed item's entity archive. */
	for (weaponnum = 0; weaponnum < g_WeaponDataCount && bind->gunnum < 0; weaponnum++) {
		for (func = FUNC_PRIMARY; func <= FUNC_SECONDARY; func++) {
			const weapon_graph_held_function_t *held = weaponGraphRuntimeGetHeldFunction(weaponnum, func);

			if (held && held->valid && strcmp(held->projectile_ref, asset_id) == 0) {
				bind->gunnum = (s16)weaponnum;
				bind->func = (s8)func;
				break;
			}
		}
	}
}

/* Who launched an object, and from which archive. */
typedef struct projectile_identity {
	s32 gunnum;           /* the weapon whose function launched it, or -1 */
	s32 func;
	const char *asset_id; /* the projectile archive it was launched from, or NULL */
	s32 not_launched;     /* the function's archive describes another object: this one has no program */
} projectile_identity_t;

/* B-1264: whether a weapon object (weaponnum, gunfunc) is what the function
 * `func` of the gun `gunnum` launches. An archive's program describes that
 * object and no other. A thrown item is its own function's object (a
 * grenade, a mine, a knife, the Dragon's second function); so is a rocket,
 * a bolt or a grenade round that carries its own number. Two kinds of object
 * are not:
 * - a gun that fires something else. A rocket launcher, a Devastator or a
 *   crossbow lying on the floor, or dropped by a guard, is a weapon object
 *   too, and its function's archive describes the rocket, the round or the
 *   bolt;
 * - what a launch made in place of the usual object. A character firing the
 *   guards' launcher (WEAPON_ROCKETLAUNCHER_34) makes a rocket, and its
 *   archive is wired for one; a player firing it makes an object with the
 *   launcher's own number, which the original gives no rule.
 * For those the original rules answer. A mod weapon's objects carry the
 * weapon's own number whatever they are, so the number cannot say; the
 * launch's binding does. */
static s32 projectileFunctionLaunches(s32 gunnum, s32 func, s32 weaponnum, s32 gunfunc)
{
	const struct weaponfunc *f;
	s32 objweapon = -1;
	s32 objfunc = -1;

	if (gunnum >= WEAPON_CUSTOM_START) {
		return 1;
	}
	if (gunnum < 0 || gunnum >= g_WeaponDataCount || !g_WeaponData[gunnum]
			|| func < FUNC_PRIMARY || func > FUNC_SECONDARY) {
		return 0;
	}
	f = (const struct weaponfunc *)g_WeaponData[gunnum]->functions[func];
	if (!f) {
		return 0;
	}
	projectileRulesLaunchedObject(gunnum, func, f->type == INVENTORYFUNCTYPE_THROW,
		(f->flags & FUNCFLAG_HOMINGROCKET) != 0, &objweapon, &objfunc);
	return objweapon == weaponnum && objfunc == gunfunc;
}

/* Whether a weapon object no launch bound is what its own function's archive
 * describes. For the original's weapons the numbers say (above). A mod
 * weapon's objects all carry the weapon's number: the gun on a pad, in a
 * hand or dropped, and what it fires. So only a function that throws the
 * item itself (a grenade, a mine) makes the unbound object its own
 * projectile; what a mod weapon fires is known by its launch's binding,
 * which lasts as long as the object does. Without this a mod launcher copied
 * from a base one would arm and fuse the gun itself, and a shot at the
 * pickup would blow it up. */
static s32 projectileUnboundIsItsProjectile(const struct weaponobj *weapon)
{
	if (weapon->weaponnum >= WEAPON_CUSTOM_START) {
		const struct weaponfunc *f = weaponGetFunctionById(weapon->weaponnum, weapon->gunfunc);

		return f && (f->type & 0xff) == INVENTORYFUNCTYPE_THROW;
	}
	return projectileFunctionLaunches(weapon->weaponnum, weapon->gunfunc,
		weapon->weaponnum, weapon->gunfunc);
}

/* The object's binding; for a weapon object without one, its own weapon and
 * function (everything thrown carries those). 0 when neither says: an
 * unbound object that is no weapon is not a projectile of a weapon. */
static s32 projectileIdentify(struct defaultobj *obj, projectile_identity_t *out)
{
	const projectile_record_t *bind = projectileBindFind(obj);

	out->gunnum = -1;
	out->func = FUNC_PRIMARY;
	out->asset_id = NULL;
	out->not_launched = 0;
	if (bind) {
		out->asset_id = bind->asset_id;
		if (bind->gunnum >= 0) {
			out->gunnum = bind->gunnum;
			out->func = bind->func;
			if (obj->type == OBJTYPE_WEAPON) {
				const struct weaponobj *made = (const struct weaponobj *)obj;

				/* The launch made something other than what the archive
				 * is wired for: the original rules answer for it. */
				if (!projectileFunctionLaunches(bind->gunnum, bind->func,
						made->weaponnum, made->gunfunc)) {
					out->not_launched = 1;
				}
			}
			return 1;
		}
	}
	if (obj && obj->type == OBJTYPE_WEAPON) {
		struct weaponobj *weapon = (struct weaponobj *)obj;

		out->gunnum = weapon->gset.weaponnum;
		out->func = weapon->gset.weaponfunc;
		if (!out->asset_id) {
			if (projectileUnboundIsItsProjectile(weapon)) {
				const weapon_graph_projectile_runtime_t *record =
					weaponGraphRuntimeGetProjectileForHeldFunction(
						weaponGraphRuntimeGetHeldFunctionForGameplay(out->gunnum, out->func));

				if (record && record->asset_id[0]) {
					out->asset_id = record->asset_id;
				}
			} else {
				/* B-1264: the gun itself, not what it fires. */
				out->not_launched = 1;
			}
		}
		return 1;
	}
	return bind != NULL;
}

void projectileEventRecord(s32 kind, struct defaultobj *obj)
{
	projectile_identity_t who;

	if (kind < 0 || kind >= PROJECTILE_EVENT_COUNT || !obj || g_NetMode == NETMODE_CLIENT
			|| !projectileIdentify(obj, &who)) {
		return;
	}
	s_ProjectileEventTotals[kind]++;

	if (!projectileEventFirst(who.gunnum, who.func, kind)) {
		return;
	}
	sysLogPrintf(LOG_NOTE,
		"PROJECTILE.EVENT: %s weapon=%s func=%s object=%s projectile=%s owner=%d flags=0x%08x",
		s_ProjectileEventNames[kind], projectileEventWeaponId(who.gunnum),
		who.func == FUNC_SECONDARY ? "secondary" : "primary",
		obj->type == OBJTYPE_WEAPON
			? projectileEventWeaponId(((struct weaponobj *)obj)->weaponnum) : "-",
		who.asset_id ? who.asset_id : "-",
		(s32)((u32)obj->hidden >> 28),
		(obj->hidden & OBJHFLAG_PROJECTILE) && obj->projectile
			? (u32)obj->projectile->flags : 0u);
}

void projectileEventsReport(const char *why)
{
	sysLogPrintf(LOG_NOTE,
		"PROJECTILE.EVENTS: %s spawned=%d bounced=%d hit_target=%d landed=%d ended=%d detonated=%d"
		" fuse.compared=%d fuse.differ=%d rules.compared=%d rules.differ=%d",
		why ? why : "report",
		s_ProjectileEventTotals[PROJECTILE_EVENT_SPAWNED],
		s_ProjectileEventTotals[PROJECTILE_EVENT_BOUNCED],
		s_ProjectileEventTotals[PROJECTILE_EVENT_HIT_TARGET],
		s_ProjectileEventTotals[PROJECTILE_EVENT_LANDED],
		s_ProjectileEventTotals[PROJECTILE_EVENT_ENDED],
		s_ProjectileEventTotals[PROJECTILE_EVENT_DETONATED],
		s_ProjectileFuseCompared, s_ProjectileFuseDiffer,
		s_ProjectileRuleCompared, s_ProjectileRuleDiffer);
	memset(s_ProjectileEventTotals, 0, sizeof(s_ProjectileEventTotals));
	s_ProjectileFuseCompared = 0;
	s_ProjectileFuseDiffer = 0;
	s_ProjectileRuleCompared = 0;
	s_ProjectileRuleDiffer = 0;
	s_ProjectileEventSeenCount = 0;
	/* held-weapon-events.md slice 2: the held weapon totals go out with the
	 * projectile totals, at the stage end and a smoke's scripted exit. */
	weaponEventsReport(why);
}

/* What a projectile program's modules chose for one event. */
typedef struct projectile_rule {
	u32 modules;            /* every module that ran (PROJECTILE_MODULE_*) */
	s32 explosion;          /* og.projectile.explode's explosion, or PROJECTILE_EXPLOSION_OWN */
	struct defaultobj *obj; /* the projectile, for the gates */
	const projectile_rule_context_t *context; /* what the event carries, or NULL */
} projectile_rule_t;

/* Whether a gate passes for this projectile and what its event carries
 * (projectile_rules.h says what each reads). A program's gate nodes and the
 * gates the original asks are answered here alike. */
static s32 projectileGatePasses(u32 gate, const struct defaultobj *obj,
		const projectile_rule_context_t *context)
{
	switch (gate) {
	case PROJECTILE_RULE_GATE_FUSE_ARMED:
		/* The fuse counts or is spent: the weapon object's timer is not idle. */
		return obj && obj->type == OBJTYPE_WEAPON
			&& ((const struct weaponobj *)obj)->timer240 >= 0;
	case PROJECTILE_RULE_GATE_TRIGGER_CLOSE:
		return context && context->trigger_close;
	case PROJECTILE_RULE_GATE_TRIGGER_CHARACTER:
		return context && context->trigger_character;
	case PROJECTILE_RULE_GATE_DAMAGE_NOT_BLAST:
		/* The game marks a blast's damage with the remote mine's number. */
		return context && context->damage_weaponnum != WEAPON_REMOTEMINE;
	default:
		return 0;
	}
}

/* The original's choice once the gates it asks are answered: its modules,
 * or none when one of them fails. */
static u32 projectileNativeChoice(u32 native, const struct defaultobj *obj,
		const projectile_rule_context_t *context)
{
	s32 g;

	for (g = 0; g < PROJECTILE_GATE_COUNT; g++) {
		const u32 gate = PROJECTILE_RULE_GATE_FUSE_ARMED << g;

		if ((native & gate) && !projectileGatePasses(gate, obj, context)) {
			return 0;
		}
	}
	return native & PROJECTILE_MODULE_ALL;
}

/* A node's text for a setting, for projectileRulesFlightSettings. */
static const char *projectileParamText(const char *key, void *user)
{
	return missionGraphParamText((const mission_graph_params_t *)user, key);
}

/* The projectile programs' modules and gates (projectile_rules.h lists the
 * kinds, and weapon_graph_runtime.c's projectileProgramModuleKnown accepts
 * the same). A module is a choice the seam then acts on; a gate reads the
 * projectile or what the event carries. */
static s32 projectileProgramModuleRun(const char *kind, const char *node_id,
		const mission_graph_params_t *params, const mission_graph_event_t *event, void *user)
{
	projectile_rule_t *rule = (projectile_rule_t *)user;
	const u32 bit = projectileRulesModuleBit(kind);
	const u32 gate = projectileRulesGateBit(kind);

	(void)node_id;
	(void)event;
	if (!rule || !kind) {
		return 0;
	}
	if (gate) {
		return projectileGatePasses(gate, rule->obj, rule->context) ? 1 : 0;
	}
	if (bit) {
		rule->modules |= bit;
		if (bit == PROJECTILE_MODULE_EXPLODE) {
			/* Registration refused a node whose explosion_type is no type,
			 * so this is a type or none. The last explode reached names it. */
			const s32 type = projectileRulesExplosionFromText(
				missionGraphParamText(params, PROJECTILE_EXPLODE_TYPE_PARAM));

			rule->explosion = type >= 0 ? type : PROJECTILE_EXPLOSION_OWN;
		}
		if ((bit & PROJECTILE_MODULE_FLIGHT) && rule->context && rule->context->flight) {
			/* Slice 7: a flight module's settings over the original's.
			 * Registration refused malformed ones. */
			projectileRulesFlightSettings(bit, projectileParamText, (void *)params, rule->context->flight, NULL);
		}
		return 1;
	}
	return 0;
}

/* The program that decides a rule event for a projectile: the one of the
 * archive it was launched from. An object its archive does not describe has
 * none (B-1264). An entity archive carries its item's fuse alone, so a
 * deployed item whose projectile archive has no program gets its entity
 * archive's program for the fuse rule and the original rules for the rest. */
static const mission_graph_program_t *projectileProgramFor(const projectile_identity_t *who,
		s32 event_kind, const char **asset_id)
{
	const mission_graph_program_t *program = NULL;

	*asset_id = NULL;
	if (who->not_launched) {
		return NULL;
	}
	if (who->asset_id) {
		program = weaponGraphRuntimeProjectileProgram(who->asset_id);
		*asset_id = who->asset_id;
	}
	if (!program && who->gunnum >= 0
			&& event_kind == MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED) {
		const weapon_graph_entity_runtime_t *entity = weaponGraphRuntimeGetEntityForHeldFunction(
			weaponGraphRuntimeGetHeldFunctionForGameplay(who->gunnum, who->func));

		if (entity && entity->asset_id[0]) {
			program = weaponGraphRuntimeProjectileProgram(entity->asset_id);
			if (program) {
				*asset_id = entity->asset_id;
			}
		}
	}
	return program;
}

/* "contact" for MISSION_GRAPH_EVENT_PROJECTILE_CONTACT. */
static const char *projectileRuleName(s32 kind)
{
	const char *name = projectileRulesEventName(kind);

	return name ? name + strlen("event.projectile.") : "?";
}

/* "stick+pierce" for a set of modules, "-" for none. */
static const char *projectileModulesText(u32 modules, char *buf, size_t cap)
{
	size_t used = 0;
	s32 m;

	buf[0] = '\0';
	for (m = 0; m < PROJECTILE_MODULE_COUNT; m++) {
		const char *kind = (modules & (1u << m)) ? projectileRulesModuleKind(1u << m) : NULL;

		if (kind) {
			const int n = snprintf(buf + used, cap - used, "%s%s", used ? "+" : "",
				kind + strlen("og.projectile."));

			if (n < 0 || (size_t)n >= cap - used) {
				break;
			}
			used += (size_t)n;
		}
	}
	if (!used) {
		snprintf(buf, cap, "-");
	}
	return buf;
}

/* "own" for the weapon object's own explosion, else the type's number. */
static const char *projectileExplosionText(s32 explosion, char *buf, size_t cap)
{
	if (explosion < 0) {
		snprintf(buf, cap, "own");
	} else {
		snprintf(buf, cap, "%d", explosion);
	}
	return buf;
}

u32 projectileRuleModules(s32 kind, struct defaultobj *obj, u32 native)
{
	return projectileRuleModulesWith(kind, obj, native, NULL);
}

/* One rule event, for an object already identified (or, for a throw, for
 * the thrower: obj is NULL then and instance is its gset): the program's
 * choice, or the original's when the archive has no program. */
static u32 projectileRuleDecide(s32 kind, struct defaultobj *obj, void *instance,
		projectile_identity_t who, u32 original, const projectile_rule_context_t *context)
{
	const mission_graph_program_t *program;
	const char *asset_id = NULL;
	mission_graph_event_t event;
	projectile_rule_t rule;
	s32 handled;

	program = projectileProgramFor(&who, kind, &asset_id);
	if (!program) {
		return original; /* a v1 archive: the original rules decide */
	}
	memset(&rule, 0, sizeof(rule));
	rule.obj = obj;
	rule.context = context;
	rule.explosion = PROJECTILE_EXPLOSION_OWN;
	handled = missionGraphProgramHandlesEvent(program, kind);
	if (handled) {
		memset(&event, 0, sizeof(event));
		event.kind = kind;
		event.player = obj ? (s32)((u32)obj->hidden >> 28) : -1;
		event.catalog_id = projectileEventWeaponId(who.gunnum);
		event.instance = instance;
		if (missionGraphProgramFire(program, &event, projectileProgramModuleRun, &rule) < 0) {
			rule.modules = 0; /* a module refused: nothing is chosen */
		}
		rule.modules &= projectileRulesEventModules(kind);
	}
	if (g_NetMode != NETMODE_CLIENT && (handled || original)) {
		char chose[96];
		char orig[96];
		/* The parity probe compares with the original's rule for the weapon
		 * number. A mod weapon's number has none, so its program's choices
		 * are logged and not counted. */
		const s32 counted = who.gunnum < WEAPON_CUSTOM_START;

		s_ProjectileRuleCompared += counted;
		if (counted && rule.modules != original) {
			s_ProjectileRuleDiffer++;
			if (s_ProjectileRuleDiffer <= 8) {
				sysLogPrintf(LOG_WARNING,
					"PROJECTILE.RULE differs: %s weapon=%s func=%s program=%s modules=%s native=%s",
					projectileRuleName(kind),
					projectileEventWeaponId(who.gunnum),
					who.func == FUNC_SECONDARY ? "secondary" : "primary", asset_id ? asset_id : "-",
					projectileModulesText(rule.modules, chose, sizeof(chose)),
					projectileModulesText(original, orig, sizeof(orig)));
			}
		}
		if (projectileEventFirst(who.gunnum, who.func, PROJECTILE_EVENT_SEEN_RULE(kind))) {
			sysLogPrintf(LOG_NOTE,
				"PROJECTILE.RULE: %s weapon=%s func=%s object=%s program=%s modules=%s native=%s",
				projectileRuleName(kind),
				projectileEventWeaponId(who.gunnum),
				who.func == FUNC_SECONDARY ? "secondary" : "primary",
				obj && obj->type == OBJTYPE_WEAPON
					? projectileEventWeaponId(((struct weaponobj *)obj)->weaponnum) : "-",
				asset_id ? asset_id : "-",
				projectileModulesText(rule.modules, chose, sizeof(chose)),
				projectileModulesText(original, orig, sizeof(orig)));
		}
	}
	return rule.modules;
}

u32 projectileRuleModulesWith(s32 kind, struct defaultobj *obj, u32 native,
		const projectile_rule_context_t *context)
{
	const u32 original = projectileNativeChoice(native, obj, context);
	projectile_identity_t who;

	if (!obj || !projectileIdentify(obj, &who)) {
		return original;
	}
	return projectileRuleDecide(kind, obj, obj, who, original, context);
}

/* Slice 6: the arm a weapon object chose (event.projectile.armed), kept in
 * its prop's record so a tick costs one lookup. The answer stands for the
 * weapon number and function it was given for. Both can change on a live
 * object (a simulant switching function rewrites the weapon it holds; a
 * reconnect snapshot rewrites a prop), and the original read them every
 * tick, so a change asks again. */
u32 projectileArmModules(struct defaultobj *obj, u32 native)
{
	const u32 original = projectileNativeChoice(native, obj, NULL);
	const struct weaponobj *weapon = (const struct weaponobj *)obj;
	const char *asset_id = NULL;
	projectile_record_t *kept;
	projectile_identity_t who;
	u32 arms = original;

	if (!obj || obj->type != OBJTYPE_WEAPON || !obj->prop) {
		return original;
	}
	kept = projectileRecordOf(obj->prop, 1);
	if (kept && kept->armed && kept->arm_weaponnum == weapon->weaponnum
			&& kept->arm_gunfunc == (s8)weapon->gunfunc) {
		return kept->arms;
	}
	if (projectileIdentify(obj, &who)
			&& projectileProgramFor(&who, MISSION_GRAPH_EVENT_PROJECTILE_ARMED, &asset_id)) {
		/* Its archive has a program: the program arms it, and none of a mod
		 * weapon's record arms run for it. */
		arms = projectileRuleDecide(MISSION_GRAPH_EVENT_PROJECTILE_ARMED, obj, obj, who,
			original, NULL) | PROJECTILE_ARMS_PROGRAM;
	}
	if (kept) {
		kept->armed = 1;
		kept->arm_weaponnum = (s16)weapon->weaponnum;
		kept->arm_gunfunc = (s8)weapon->gunfunc;
		kept->arms = arms;
	}
	return arms;
}

/* Slice 7: the flight a weapon object chose (event.projectile.in_flight),
 * kept in its prop's record beside its arm, with the settings its modules'
 * nodes name. Asked again when its weapon number or function changes, as the
 * arm is. */
u32 projectileFlightModules(struct defaultobj *obj, u32 native)
{
	const u32 original = projectileNativeChoice(native, obj, NULL);
	const struct weaponobj *weapon = (const struct weaponobj *)obj;
	const char *asset_id = NULL;
	projectile_record_t *kept;
	projectile_identity_t who;
	projectile_flight_t nativeset;
	projectile_flight_t set;
	u32 flight = original;

	if (!obj || obj->type != OBJTYPE_WEAPON || !obj->prop) {
		return original;
	}
	kept = projectileRecordOf(obj->prop, 1);
	if (kept && kept->flew && kept->flight_weaponnum == weapon->weaponnum
			&& kept->flight_gunfunc == (s8)weapon->gunfunc) {
		return kept->flight;
	}
	projectileRulesNativeFlight(weapon->weaponnum, weapon->gunfunc, &nativeset);
	set = nativeset;
	if (projectileIdentify(obj, &who)
			&& projectileProgramFor(&who, MISSION_GRAPH_EVENT_PROJECTILE_IN_FLIGHT, &asset_id)) {
		projectile_rule_context_t context;

		memset(&context, 0, sizeof(context));
		context.flight = &set;
		flight = projectileRuleDecide(MISSION_GRAPH_EVENT_PROJECTILE_IN_FLIGHT, obj, obj, who,
			original, &context) | PROJECTILE_FLIGHT_PROGRAM;
		/* The original's modules with other settings differ too (a base
		 * archive's equal the original by construction). */
		if (g_NetMode != NETMODE_CLIENT && who.gunnum < WEAPON_CUSTOM_START
				&& (flight & PROJECTILE_MODULE_ALL) == original
				&& !projectileRulesFlightSame(original, &set, &nativeset)) {
			s_ProjectileRuleDiffer++;
			if (s_ProjectileRuleDiffer <= 8) {
				sysLogPrintf(LOG_WARNING,
					"PROJECTILE.RULE differs: in_flight weapon=%s func=%s program=%s settings",
					projectileEventWeaponId(who.gunnum),
					who.func == FUNC_SECONDARY ? "secondary" : "primary", asset_id ? asset_id : "-");
			}
		}
	}
	if (kept) {
		kept->flew = 1;
		kept->flight_weaponnum = (s16)weapon->weaponnum;
		kept->flight_gunfunc = (s8)weapon->gunfunc;
		kept->flight = flight;
		kept->flightset = set;
	}
	return flight;
}

const struct projectile_flight *projectileFlightSettings(const struct defaultobj *obj)
{
	static projectile_flight_t s_Native;
	const projectile_record_t *kept = obj && obj->prop ? projectileRecordOf(obj->prop, 0) : NULL;

	if (kept && kept->flew) {
		return &kept->flightset;
	}
	if (obj && obj->type == OBJTYPE_WEAPON) {
		const struct weaponobj *weapon = (const struct weaponobj *)obj;

		projectileRulesNativeFlight(weapon->weaponnum, weapon->gunfunc, &s_Native);
	} else {
		projectileRulesNativeFlight(-1, FUNC_PRIMARY, &s_Native);
	}
	return &s_Native;
}

/* The archive a sentry rule is asked of, named by the caller: a throw has no
 * object yet, and a sentry is no weapon object and outlives its binding. */
static void projectileIdentityOf(projectile_identity_t *who, s32 gunnum, s32 func, const char *asset_id)
{
	who->gunnum = gunnum;
	who->func = func;
	who->asset_id = asset_id && asset_id[0] ? asset_id : NULL;
	who->not_launched = 0;
}

u32 projectileThrowModules(const struct gset *gset, const char *asset_id, u32 native)
{
	const u32 original = projectileNativeChoice(native, NULL, NULL);
	projectile_identity_t who;

	if (!gset || !asset_id || !asset_id[0]) {
		return original;
	}
	projectileIdentityOf(&who, gset->weaponnum, gset->weaponfunc, asset_id);
	return projectileRuleDecide(MISSION_GRAPH_EVENT_PROJECTILE_THROWN, NULL, (void *)gset, who,
		original, NULL);
}

u32 projectileSentryModules(s32 kind, struct defaultobj *obj, s32 gunnum, s32 func,
		const char *asset_id, u32 native)
{
	const u32 original = projectileNativeChoice(native, obj, NULL);
	projectile_identity_t who;

	if (!obj || !asset_id || !asset_id[0]) {
		return original;
	}
	projectileIdentityOf(&who, gunnum, func, asset_id);
	return projectileRuleDecide(kind, obj, obj, who, original, NULL);
}

u32 projectileFuseModules(struct defaultobj *obj, u32 native, s32 native_explosion,
		s32 *explosion)
{
	const u32 original = projectileNativeChoice(native, obj, NULL);
	const mission_graph_program_t *program;
	const char *asset_id = NULL;
	mission_graph_event_t event;
	projectile_identity_t who;
	projectile_rule_t rule;
	char chose[96];
	char orig[96];
	char chosetype[16];
	char origtype[16];
	s32 differs;

	if (explosion) {
		*explosion = native_explosion;
	}
	if (!obj || obj->type != OBJTYPE_WEAPON || g_NetMode == NETMODE_CLIENT
			|| !projectileIdentify(obj, &who)) {
		return original;
	}
	program = projectileProgramFor(&who, MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED, &asset_id);
	if (!program) {
		return original; /* a v1 archive: the original fuse decides */
	}
	memset(&event, 0, sizeof(event));
	event.kind = MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED;
	event.player = (s32)((u32)obj->hidden >> 28);
	event.catalog_id = projectileEventWeaponId(who.gunnum);
	event.instance = obj;
	memset(&rule, 0, sizeof(rule));
	rule.obj = obj;
	rule.explosion = PROJECTILE_EXPLOSION_OWN;
	if (missionGraphProgramFire(program, &event, projectileProgramModuleRun, &rule) < 0) {
		rule.modules = 0; /* a module refused: nothing goes off */
	}
	rule.modules &= projectileRulesEventModules(MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED);
	if (!(rule.modules & PROJECTILE_MODULE_EXPLODE)) {
		rule.explosion = PROJECTILE_EXPLOSION_OWN;
	}
	differs = rule.modules != original
		|| ((original & PROJECTILE_MODULE_EXPLODE) && rule.explosion != native_explosion);
	/* A mod weapon's number has no original fuse to compare with. */
	if (who.gunnum >= WEAPON_CUSTOM_START) {
		differs = 0;
	} else {
		s_ProjectileFuseCompared++;
	}
	if (differs) {
		s_ProjectileFuseDiffer++;
		if (s_ProjectileFuseDiffer <= 8) {
			sysLogPrintf(LOG_WARNING,
				"PROJECTILE.RULE differs: timer_expired weapon=%s func=%s program=%s modules=%s native=%s"
				" explosion=%s native_explosion=%s",
				projectileEventWeaponId(who.gunnum),
				who.func == FUNC_SECONDARY ? "secondary" : "primary", asset_id ? asset_id : "-",
				projectileModulesText(rule.modules, chose, sizeof(chose)),
				projectileModulesText(original, orig, sizeof(orig)),
				projectileExplosionText(rule.explosion, chosetype, sizeof(chosetype)),
				projectileExplosionText(native_explosion, origtype, sizeof(origtype)));
		}
	}
	if (projectileEventFirst(who.gunnum, who.func, PROJECTILE_EVENT_SEEN_FUSE_RULE)) {
		sysLogPrintf(LOG_NOTE,
			"PROJECTILE.RULE: timer_expired weapon=%s func=%s object=%s program=%s modules=%s native=%s"
			" explosion=%s native_explosion=%s",
			projectileEventWeaponId(who.gunnum), who.func == FUNC_SECONDARY ? "secondary" : "primary",
			projectileEventWeaponId(((struct weaponobj *)obj)->weaponnum), asset_id ? asset_id : "-",
			projectileModulesText(rule.modules, chose, sizeof(chose)),
			projectileModulesText(original, orig, sizeof(orig)),
			projectileExplosionText(rule.explosion, chosetype, sizeof(chosetype)),
			projectileExplosionText(native_explosion, origtype, sizeof(origtype)));
	}
	if (explosion) {
		*explosion = rule.explosion;
	}
	return rule.modules;
}
