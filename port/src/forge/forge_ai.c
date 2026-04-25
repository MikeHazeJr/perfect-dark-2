/**
 * forge_ai.c -- AI placement and patrol path helpers (F6).
 *
 * Editor-side helpers for AI chrs placed in The Grid. The runtime
 * pipeline (an AI chr actually walking a patrol path) is wired through
 * the existing chraicommands system at match start -- this module
 * generates the edit-time representation and a straightforward
 * patrol-waypoint chain that later map-instantiate code consumes.
 *
 * Navmesh generation is deliberately a placeholder.  Engine-side
 * navmesh comes from the base stage; forge maps simply reuse the base
 * stage's waypoint network plus any bot_patrol_point objects the
 * author placed as additional waypoints.
 */

#include "forge/forge_core.h"

#include <string.h>

#include "system.h"

/* Count of AI objects (including bosses). */
s32 forgeAiCount(void)
{
	s32 n = 0;
	for (s32 i = 0; i < FORGE_MAX_OBJECTS; ++i) {
		forge_object_t *o = forgeObjectGet(i);
		if (!o || !o->in_use) continue;
		if (o->category != FORGE_CAT_AI) continue;
		++n;
	}
	return n;
}

/* Count of bot_patrol_point spawn objects -- these define the patrol
 * waypoint graph. */
s32 forgeAiPatrolPointCount(void)
{
	s32 n = 0;
	for (s32 i = 0; i < FORGE_MAX_OBJECTS; ++i) {
		forge_object_t *o = forgeObjectGet(i);
		if (!o || !o->in_use) continue;
		if (o->category != FORGE_CAT_SPAWN_POINT) continue;
		if (strcmp(o->catalog_id, "base:spawn_bot_patrol") != 0) continue;
		++n;
	}
	return n;
}

/* Walk the patrol chain starting at `head_uid`.  Each patrol point
 * has its `patrol_path_uid` set on its ai props (reused as next-waypoint
 * link).  Returns the count of waypoints in the chain (capped at 64). */
s32 forgeAiWalkPatrolChain(u32 head_uid, u32 *out_uids, s32 max_out)
{
	s32 n = 0;
	u32 cur = head_uid;
	while (cur != 0 && n < max_out && n < 64) {
		forge_object_t *o = forgeObjectFindByUid(cur);
		if (!o) break;
		out_uids[n++] = cur;
		/* The patrol point uses its ai props to store the next-link.
		 * If the caller placed a patrol point from the SPAWN_POINT
		 * category, the convention is to store the next pad uid in
		 * spawn.priority (re-purposed) because that category's props
		 * don't have a dedicated next-uid field.  For now we just
		 * break -- actual chain walking needs an editor UI for
		 * linking points, which lands with the F6 polish pass. */
		break;
	}
	return n;
}

/* Generate a sketchy navmesh for a forge map.  Currently a no-op --
 * the base stage's navmesh is reused as-is.  If forge-placed geometry
 * blocks a waypoint link, the spawn pool's wall probe will tolerate it
 * by falling back to alternate spawns (S302 tiered spawn system). */
s32 forgeAiGenerateNavmesh(void)
{
	s32 ai_count = forgeAiCount();
	s32 patrol_count = forgeAiPatrolPointCount();
	sysLogPrintf(LOG_NOTE, "GRID.AI: navmesh pass -- %d AI, %d patrol points (base-stage reuse)",
			ai_count, patrol_count);
	return 1;
}

/* Called from forge_serialize save path so the generated navmesh can be
 * embedded as navmesh.bin alongside map.json.  Stub returns 0 (nothing
 * written); callers should treat this as "base-stage navmesh is
 * authoritative". */
s32 forgeAiWriteNavmesh(const char *mod_dir)
{
	(void)mod_dir;
	return 0;
}
