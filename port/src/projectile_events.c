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

void projectileEventRecord(s32 kind, struct defaultobj *obj)
{
	struct weaponobj *weapon;
	const weapon_graph_projectile_runtime_t *record;
	s32 gunnum;
	s32 func;

	if (kind < 0 || kind >= PROJECTILE_EVENT_COUNT || !obj
			|| obj->type != OBJTYPE_WEAPON || g_NetMode == NETMODE_CLIENT) {
		return;
	}
	weapon = (struct weaponobj *)obj;
	s_ProjectileEventTotals[kind]++;

	gunnum = weapon->gset.weaponnum;
	func = weapon->gset.weaponfunc;
	if (!projectileEventFirst(gunnum, func, kind)) {
		return;
	}
	record = weaponGraphRuntimeGetProjectileForHeldFunction(
		weaponGraphRuntimeGetHeldFunctionForGameplay(gunnum, func));
	sysLogPrintf(LOG_NOTE,
		"PROJECTILE.EVENT: %s weapon=%s func=%s object=%s projectile=%s owner=%d flags=0x%08x",
		s_ProjectileEventNames[kind], projectileEventWeaponId(gunnum),
		func == FUNC_SECONDARY ? "secondary" : "primary",
		projectileEventWeaponId(weapon->weaponnum),
		record && record->asset_id[0] ? record->asset_id : "-",
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

/* The program that decides for this weapon object: its function's
 * projectile archive's, else its entity archive's (a deployed item). */
static const mission_graph_program_t *projectileProgramForWeapon(struct weaponobj *weapon,
		const char **asset_id)
{
	const weapon_graph_held_function_t *held = weaponGraphRuntimeGetHeldFunctionForGameplay(
		weapon->gset.weaponnum, weapon->gset.weaponfunc);
	const weapon_graph_projectile_runtime_t *projectile =
		weaponGraphRuntimeGetProjectileForHeldFunction(held);
	const weapon_graph_entity_runtime_t *entity =
		weaponGraphRuntimeGetEntityForHeldFunction(held);
	const mission_graph_program_t *program = NULL;

	*asset_id = NULL;
	if (projectile && projectile->asset_id[0]) {
		program = weaponGraphRuntimeProjectileProgram(projectile->asset_id);
		*asset_id = projectile->asset_id;
	}
	if (!program && entity && entity->asset_id[0]) {
		program = weaponGraphRuntimeProjectileProgram(entity->asset_id);
		if (program) {
			*asset_id = entity->asset_id;
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
	projectile_rule_t rule;
	s32 gunnum;
	s32 func;

	if (!obj || obj->type != OBJTYPE_WEAPON || g_NetMode == NETMODE_CLIENT) {
		return 1;
	}
	weapon = (struct weaponobj *)obj;
	program = projectileProgramForWeapon(weapon, &asset_id);
	if (!program) {
		return 1; /* a v1 archive: the native rule decides */
	}
	gunnum = weapon->gset.weaponnum;
	func = weapon->gset.weaponfunc;
	memset(&event, 0, sizeof(event));
	event.kind = MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED;
	event.player = (s32)((u32)obj->hidden >> 28);
	event.catalog_id = projectileEventWeaponId(gunnum);
	event.instance = obj;
	memset(&rule, 0, sizeof(rule));
	if (missionGraphProgramFire(program, &event, projectileProgramModuleRun, &rule) < 0) {
		rule.fuse = 0;
	}
	s_ProjectileFuseCompared++;
	if (!rule.fuse) {
		s_ProjectileFuseDiffer++;
	}
	if (projectileEventFirst(gunnum, func, PROJECTILE_EVENT_SEEN_FUSE_RULE)) {
		sysLogPrintf(LOG_NOTE,
			"PROJECTILE.RULE: timer_expired weapon=%s func=%s object=%s program=%s fuse=%d",
			projectileEventWeaponId(gunnum), func == FUNC_SECONDARY ? "secondary" : "primary",
			projectileEventWeaponId(weapon->weaponnum), asset_id ? asset_id : "-", rule.fuse);
	}
	return rule.fuse;
}
