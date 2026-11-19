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
#include "projectile_events.h"

static const char *const s_ProjectileEventNames[PROJECTILE_EVENT_COUNT] = {
	"spawned", "bounced", "hit_target", "landed", "ended", "detonated",
};

static s32 s_ProjectileEventTotals[PROJECTILE_EVENT_COUNT];

/* The fuse rule's parity counters (projectileFuseRule). */
static s32 s_ProjectileFuseCompared;
static s32 s_ProjectileFuseDiffer;

/* The seen-table kind of the fuse rule's first-decision line. */
#define PROJECTILE_EVENT_SEEN_FUSE_RULE PROJECTILE_EVENT_COUNT

/* The first event of each kind for a weapon and function logs a line;
 * later ones only count. Bounded, so an automatic launcher cannot flood
 * the log. */
#define PROJECTILE_EVENT_SEEN_MAX 192
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
} projectile_identity_t;

/* The object's binding; for a weapon object without one, its own weapon and
 * function (everything thrown carries those). 0 when neither says: an
 * unbound object that is no weapon is not a projectile of a weapon. */
static s32 projectileIdentify(struct defaultobj *obj, projectile_identity_t *out)
{
	const projectile_bind_t *bind = projectileBindFind(obj);

	out->gunnum = -1;
	out->func = FUNC_PRIMARY;
	out->asset_id = NULL;
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
			const weapon_graph_projectile_runtime_t *record =
				weaponGraphRuntimeGetProjectileForHeldFunction(
					weaponGraphRuntimeGetHeldFunctionForGameplay(out->gunnum, out->func));

			if (record && record->asset_id[0]) {
				out->asset_id = record->asset_id;
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
		" fuse.compared=%d fuse.differ=%d",
		why ? why : "report",
		s_ProjectileEventTotals[PROJECTILE_EVENT_SPAWNED],
		s_ProjectileEventTotals[PROJECTILE_EVENT_BOUNCED],
		s_ProjectileEventTotals[PROJECTILE_EVENT_HIT_TARGET],
		s_ProjectileEventTotals[PROJECTILE_EVENT_LANDED],
		s_ProjectileEventTotals[PROJECTILE_EVENT_ENDED],
		s_ProjectileEventTotals[PROJECTILE_EVENT_DETONATED],
		s_ProjectileFuseCompared, s_ProjectileFuseDiffer);
	memset(s_ProjectileEventTotals, 0, sizeof(s_ProjectileEventTotals));
	s_ProjectileFuseCompared = 0;
	s_ProjectileFuseDiffer = 0;
	s_ProjectileEventSeenCount = 0;
}

/* What a projectile program's modules decided for one event. */
typedef struct projectile_rule {
	s32 fuse; /* og.projectile.fuse_expired ran */
} projectile_rule_t;

/* The projectile programs' native modules (weapon_graph_runtime.c's
 * projectileProgramModuleKnown lists the same kinds). */
static s32 projectileProgramModuleRun(const char *kind, const char *node_id,
		const mission_graph_params_t *params, const mission_graph_event_t *event, void *user)
{
	projectile_rule_t *rule = (projectile_rule_t *)user;

	(void)node_id;
	(void)params;
	(void)event;
	if (rule && kind && strcmp(kind, "og.projectile.fuse_expired") == 0) {
		rule->fuse = 1;
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
	if (!program && who->gunnum >= 0) {
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
