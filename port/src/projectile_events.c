/*
 * projectile_events.c -- record-only projectile events: see
 * projectile_events.h and context/designs/modding/projectile-events.md
 * (slice 1).
 */

#include <stdio.h>
#include <string.h>
#include <PR/ultratypes.h>
#include "types.h"
#include "constants.h"
#include "system.h"
#include "net/net.h"
#include "weapondata_authored.h"
#include "weapon_graph_runtime.h"
#include "mission_graph_runner.h"
#include "projectile_rules.h"
#include "projectile_events.h"

static const char *const s_ProjectileEventNames[PROJECTILE_EVENT_COUNT] = {
	"spawned", "bounced", "hit_target", "landed", "ended", "detonated",
};

static s32 s_ProjectileEventTotals[PROJECTILE_EVENT_COUNT];

/* The fuse rule's parity counters (projectileFuseRule). */
static s32 s_ProjectileFuseCompared;
static s32 s_ProjectileFuseDiffer;
/* The contact rules' (projectileRuleModules). */
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

/* The archive each projectile was launched from (projectileEventBind), by
 * its prop and that prop's generation, so a freed prop's entry never names a
 * later object. A ring: when a projectile outlives its entry (64 newer
 * launches) its own weapon and function identify it again, which is right
 * for everything thrown. A fired rocket, bolt or grenade round carries its
 * own weapon number and needs the entry; none lives that long. */
#define PROJECTILE_BIND_MAX 64
typedef struct projectile_bind {
	const struct prop *prop;
	u64 generation;
	s16 gunnum; /* the weapon whose function names the archive, or -1 */
	s8 func;
	char asset_id[CATALOG_ID_LEN];
} projectile_bind_t;
static projectile_bind_t s_ProjectileBinds[PROJECTILE_BIND_MAX];
static s32 s_ProjectileBindNext;

static projectile_bind_t *projectileBindFind(const struct defaultobj *obj)
{
	s32 i;

	if (!obj || !obj->prop || obj->prop->lifecycle_generation == 0) {
		return NULL;
	}
	for (i = 0; i < PROJECTILE_BIND_MAX; i++) {
		if (s_ProjectileBinds[i].prop == obj->prop
				&& s_ProjectileBinds[i].generation == obj->prop->lifecycle_generation) {
			return &s_ProjectileBinds[i];
		}
	}
	return NULL;
}

void projectileEventBind(struct defaultobj *obj, const char *asset_id)
{
	projectile_bind_t *bind;
	s32 weaponnum;
	s32 func;

	if (!obj || !obj->prop || obj->prop->lifecycle_generation == 0 || !asset_id || !asset_id[0]) {
		return;
	}
	bind = projectileBindFind(obj);
	if (!bind) {
		bind = &s_ProjectileBinds[s_ProjectileBindNext];
		s_ProjectileBindNext = (s_ProjectileBindNext + 1) % PROJECTILE_BIND_MAX;
	}
	memset(bind, 0, sizeof(*bind));
	bind->prop = obj->prop;
	bind->generation = obj->prop->lifecycle_generation;
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
	s32 not_launched;     /* a gun whose function launches something else: no archive describes it */
} projectile_identity_t;

/* B-1264: whether a weapon object with this weapon number and function is
 * what that function launches. A thrown item is (a grenade, a mine, a knife,
 * the Dragon's second function); so is a rocket, a bolt or a grenade round
 * that carries its own number. A gun that fires something else is not: a
 * rocket launcher, a Devastator or a crossbow lying on the floor, or
 * dropped by a guard, is a weapon object too, and its function's archive
 * describes the rocket, the round or the bolt. A mod weapon's objects carry
 * the weapon's own number whatever they are, so the number cannot say; the
 * launch binds those. */
static s32 projectileFunctionLaunchesItself(s32 weaponnum, s32 func)
{
	const struct weaponfunc *f;
	s32 objweapon = -1;
	s32 objfunc = -1;

	if (weaponnum >= WEAPON_CUSTOM_START) {
		return 1;
	}
	if (weaponnum < 0 || weaponnum >= g_WeaponDataCount || !g_WeaponData[weaponnum]
			|| func < FUNC_PRIMARY || func > FUNC_SECONDARY) {
		return 0;
	}
	f = (const struct weaponfunc *)g_WeaponData[weaponnum]->functions[func];
	if (!f) {
		return 0;
	}
	projectileRulesLaunchedObject(weaponnum, func, f->type == INVENTORYFUNCTYPE_THROW,
		(f->flags & FUNCFLAG_HOMINGROCKET) != 0, &objweapon, &objfunc);
	return objweapon == weaponnum && objfunc == func;
}

/* The object's binding; for a weapon object without one, its own weapon and
 * function (everything thrown carries those). 0 when neither says: an
 * unbound object that is no weapon is not a projectile of a weapon. */
static s32 projectileIdentify(struct defaultobj *obj, projectile_identity_t *out)
{
	const projectile_bind_t *bind = projectileBindFind(obj);

	out->gunnum = -1;
	out->func = FUNC_PRIMARY;
	out->asset_id = NULL;
	out->not_launched = 0;
	if (bind) {
		out->asset_id = bind->asset_id;
		if (bind->gunnum >= 0) {
			out->gunnum = bind->gunnum;
			out->func = bind->func;
			return 1;
		}
	}
	if (obj && obj->type == OBJTYPE_WEAPON) {
		struct weaponobj *weapon = (struct weaponobj *)obj;

		out->gunnum = weapon->gset.weaponnum;
		out->func = weapon->gset.weaponfunc;
		if (!out->asset_id) {
			if (projectileFunctionLaunchesItself(weapon->weaponnum, weapon->gunfunc)) {
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
}

/* What a projectile program's modules chose for one event. */
typedef struct projectile_rule {
	s32 fuse;               /* og.projectile.fuse_expired ran */
	u32 modules;            /* every module that ran (PROJECTILE_MODULE_*) */
	struct defaultobj *obj; /* the projectile, for the gates */
} projectile_rule_t;

/* The projectile programs' modules and gates (projectile_rules.h lists the
 * kinds, and weapon_graph_runtime.c's projectileProgramModuleKnown accepts
 * the same). A module is a choice the seam then acts on; the gate reads the
 * projectile. */
static s32 projectileProgramModuleRun(const char *kind, const char *node_id,
		const mission_graph_params_t *params, const mission_graph_event_t *event, void *user)
{
	projectile_rule_t *rule = (projectile_rule_t *)user;
	const u32 bit = projectileRulesModuleBit(kind);

	(void)node_id;
	(void)params;
	(void)event;
	if (!rule || !kind) {
		return 0;
	}
	if (strcmp(kind, PROJECTILE_GATE_FUSE_ARMED_KIND) == 0) {
		/* The fuse counts or is spent: the weapon object's timer is not idle. */
		return rule->obj && rule->obj->type == OBJTYPE_WEAPON
			&& ((struct weaponobj *)rule->obj)->timer240 >= 0 ? 1 : 0;
	}
	if (bit) {
		rule->modules |= bit;
		if (bit == PROJECTILE_MODULE_FUSE_EXPIRED) {
			rule->fuse = 1;
		}
		return 1;
	}
	return 0;
}

/* The program that decides for a projectile: the one of the archive it was
 * launched from, else its entity archive's (a deployed item). */
static const mission_graph_program_t *projectileProgramFor(const projectile_identity_t *who,
		const char **asset_id)
{
	const mission_graph_program_t *program = NULL;

	*asset_id = NULL;
	if (who->asset_id) {
		program = weaponGraphRuntimeProjectileProgram(who->asset_id);
		*asset_id = who->asset_id;
	}
	if (!program && who->gunnum >= 0 && !who->not_launched) {
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

s32 projectileFuseRule(struct defaultobj *obj)
{
	struct weaponobj *weapon;
	const mission_graph_program_t *program;
	const char *asset_id = NULL;
	mission_graph_event_t event;
	projectile_identity_t who;
	projectile_rule_t rule;

	if (!obj || obj->type != OBJTYPE_WEAPON || g_NetMode == NETMODE_CLIENT
			|| !projectileIdentify(obj, &who)) {
		return 1;
	}
	weapon = (struct weaponobj *)obj;
	program = projectileProgramFor(&who, &asset_id);
	if (!program) {
		return 1; /* a v1 archive: the native rule decides */
	}
	memset(&event, 0, sizeof(event));
	event.kind = MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED;
	event.player = (s32)((u32)obj->hidden >> 28);
	event.catalog_id = projectileEventWeaponId(who.gunnum);
	event.instance = obj;
	memset(&rule, 0, sizeof(rule));
	rule.obj = obj;
	if (missionGraphProgramFire(program, &event, projectileProgramModuleRun, &rule) < 0) {
		rule.fuse = 0;
	}
	s_ProjectileFuseCompared++;
	if (!rule.fuse) {
		s_ProjectileFuseDiffer++;
	}
	if (projectileEventFirst(who.gunnum, who.func, PROJECTILE_EVENT_SEEN_FUSE_RULE)) {
		sysLogPrintf(LOG_NOTE,
			"PROJECTILE.RULE: timer_expired weapon=%s func=%s object=%s program=%s fuse=%d",
			projectileEventWeaponId(who.gunnum), who.func == FUNC_SECONDARY ? "secondary" : "primary",
			projectileEventWeaponId(weapon->weaponnum), asset_id ? asset_id : "-", rule.fuse);
	}
	return rule.fuse;
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

u32 projectileRuleModules(s32 kind, struct defaultobj *obj, u32 native)
{
	const mission_graph_program_t *program;
	const char *asset_id = NULL;
	mission_graph_event_t event;
	projectile_identity_t who;
	projectile_rule_t rule;
	s32 handled;

	if (!obj || !projectileIdentify(obj, &who)) {
		return native;
	}
	program = projectileProgramFor(&who, &asset_id);
	if (!program) {
		return native; /* a v1 archive: the original rules decide */
	}
	memset(&rule, 0, sizeof(rule));
	rule.obj = obj;
	handled = missionGraphProgramHandlesEvent(program, kind);
	if (handled) {
		memset(&event, 0, sizeof(event));
		event.kind = kind;
		event.player = (s32)((u32)obj->hidden >> 28);
		event.catalog_id = projectileEventWeaponId(who.gunnum);
		event.instance = obj;
		if (missionGraphProgramFire(program, &event, projectileProgramModuleRun, &rule) < 0) {
			rule.modules = 0; /* a module refused: nothing is chosen */
		}
		rule.modules &= projectileRulesEventModules(kind);
	}
	if (g_NetMode != NETMODE_CLIENT && (handled || native)) {
		char chose[96];
		char original[96];

		s_ProjectileRuleCompared++;
		if (rule.modules != native) {
			s_ProjectileRuleDiffer++;
			if (s_ProjectileRuleDiffer <= 8) {
				sysLogPrintf(LOG_WARNING,
					"PROJECTILE.RULE differs: %s weapon=%s func=%s program=%s modules=%s native=%s",
					projectileRuleName(kind),
					projectileEventWeaponId(who.gunnum),
					who.func == FUNC_SECONDARY ? "secondary" : "primary", asset_id ? asset_id : "-",
					projectileModulesText(rule.modules, chose, sizeof(chose)),
					projectileModulesText(native, original, sizeof(original)));
			}
		}
		if (projectileEventFirst(who.gunnum, who.func, PROJECTILE_EVENT_SEEN_RULE(kind))) {
			sysLogPrintf(LOG_NOTE,
				"PROJECTILE.RULE: %s weapon=%s func=%s object=%s program=%s modules=%s native=%s",
				projectileRuleName(kind),
				projectileEventWeaponId(who.gunnum),
				who.func == FUNC_SECONDARY ? "secondary" : "primary",
				obj->type == OBJTYPE_WEAPON
					? projectileEventWeaponId(((struct weaponobj *)obj)->weaponnum) : "-",
				asset_id ? asset_id : "-",
				projectileModulesText(rule.modules, chose, sizeof(chose)),
				projectileModulesText(native, original, sizeof(original)));
		}
	}
	return rule.modules;
}
