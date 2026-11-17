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
#include "projectile_events.h"

static const char *const s_ProjectileEventNames[PROJECTILE_EVENT_COUNT] = {
	"spawned", "bounced", "hit_target", "landed", "ended", "detonated",
};

static s32 s_ProjectileEventTotals[PROJECTILE_EVENT_COUNT];

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
		"PROJECTILE.EVENTS: %s spawned=%d bounced=%d hit_target=%d landed=%d ended=%d detonated=%d",
		why ? why : "report",
		s_ProjectileEventTotals[PROJECTILE_EVENT_SPAWNED],
		s_ProjectileEventTotals[PROJECTILE_EVENT_BOUNCED],
		s_ProjectileEventTotals[PROJECTILE_EVENT_HIT_TARGET],
		s_ProjectileEventTotals[PROJECTILE_EVENT_LANDED],
		s_ProjectileEventTotals[PROJECTILE_EVENT_ENDED],
		s_ProjectileEventTotals[PROJECTILE_EVENT_DETONATED]);
	memset(s_ProjectileEventTotals, 0, sizeof(s_ProjectileEventTotals));
	s_ProjectileEventSeenCount = 0;
}
