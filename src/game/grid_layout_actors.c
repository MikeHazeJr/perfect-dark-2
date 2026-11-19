#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <ultra64.h>
#include "constants.h"
#include "types.h"
#include "bss.h"
#include "data.h"
#include "game/grid_layout_actors.h"
#include "game/mplayer/mplayer.h"
#include "game/pad.h"
#include "game/prop.h"
#include "lib/collision.h"
#include "lib/meshraygrid.h"
#include "lib/model.h"
#include "smoke_harness.h"
#include "system.h"

extern void bootGridLayoutSmokeActorState(s32 moved);

/* The supported native room extension has two playable rooms, a bounded
 * flat collision mesh and at most 64 effective waypoints. */
enum {
	GRID_LAYOUT_ROOM_COUNT = 3,
	GRID_LAYOUT_MAX_TRIANGLES = 64,
	GRID_LAYOUT_MAX_WAYPOINTS = 64,
	GRID_LAYOUT_MAX_PADS = MAX_MPCHRS + GRID_LAYOUT_MAX_WAYPOINTS,
};

typedef struct grid_layout_position {
	struct coord feet;
	f32 ground;
	RoomNum room;
	u16 floorcol;
	u16 floorflags;
	u8 floortype;
} grid_layout_position_t;

typedef struct grid_layout_actor {
	struct chrdata *chr;
	struct prop *prop;
	struct player *player;
	struct modelrwdata_chrinfo *model_root;
	struct coord origin;
	grid_layout_position_t position;
	f32 radius;
	f32 ymin;
	f32 ymax;
	f32 root_height;
	RoomNum old_room;
	s32 assigned;
	s32 relocate;
} grid_layout_actor_t;

struct grid_layout_actors {
	s32 count;
	grid_layout_actor_t actors[MAX_MPCHRS];
};

static s32 gridLayoutActorError(char *err, size_t cap, const char *message)
{
	if (err && cap) snprintf(err, cap, "%s", message);
	return 0;
}

static s32 gridLayoutFinite(const struct coord *pos)
{
	return __builtin_isfinite(pos->x) && __builtin_isfinite(pos->y) && __builtin_isfinite(pos->z);
}

static s32 gridLayoutPlayableRoom(RoomNum room)
{
	return room > 0 && room < GRID_LAYOUT_ROOM_COUNT;
}

static s32 gridLayoutFindFloor(const struct colmesh *collision, const struct coord *feet,
		RoomNum room, grid_layout_position_t *out)
{
	struct coord above = *feet;
	struct coord below = *feet;
	above.y += 0.25f;
	below.y -= 499.75f;
	f32 nearest = 1.0f;
	const struct meshtri *floor = NULL;
	for (s32 i = 0; i < collision->numtris; ++i) {
		const struct meshtri *tri = &collision->tris[i];
		if (tri->roomnum != room || !(tri->flags & (GEOFLAG_FLOOR1 | GEOFLAG_FLOOR2))
				|| (tri->flags & GEOFLAG_DIE) || !(tri->normal.y > 0.0f)
				|| fabsf(tri->v0.y - tri->v1.y) > 0.25f || fabsf(tri->v0.y - tri->v2.y) > 0.25f
				|| fabsf(tri->v1.y - tri->v2.y) > 0.25f) continue;
		f32 fraction;
		if (!meshRayVsTri(&above, &below, tri, &fraction) || !(fraction < nearest)) continue;
		nearest = fraction;
		floor = tri;
	}
	if (!floor) return 0;
	*out = (grid_layout_position_t){.feet = *feet, .ground = above.y + nearest * (below.y - above.y),
		.room = room, .floorcol = floor->floorcol, .floorflags = floor->flags, .floortype = (u8)floor->floortype};
	return 1;
}

static s32 gridLayoutPositionClear(const struct colmesh *collision, const grid_layout_actor_t *actor,
		const struct coord *feet, RoomNum room, s32 relocate, grid_layout_position_t *out)
{
	grid_layout_position_t position;
	if (!gridLayoutPlayableRoom(room) || !gridLayoutFinite(feet)
			|| !gridLayoutFindFloor(collision, feet, room, &position)) return 0;
	if (relocate) position.feet.y = position.ground;
	struct coord probe = position.feet;
	probe.y += 0.25f; /* Include a foot position exactly on its floor. */
	if (!meshCheckFlatWalk(collision, &probe, room, &probe, room, actor->radius, actor->ymax)) return 0;
	/* The same doorway may have objects registered on either side. */
	RoomNum rooms[] = {1, 2, -1};
	if (!cdPropVolumeClear(&position.feet, actor->radius, rooms, actor->ymax,
			relocate ? 0.0f : actor->ymin)) return 0;
	*out = position;
	return 1;
}

static s32 gridLayoutCaptureActors(const struct colmesh *collision, grid_layout_actors_t *plan)
{
	struct player *host = g_Vars.players[0];
	plan->count = mpGetNumChrs();
	if (PLAYERCOUNT() != 1 || !host || host != g_Vars.currentplayer || !host->prop
			|| plan->count != g_MpNumChrs || plan->count < 2 || plan->count > MAX_MPCHRS) return 0;
	for (s32 i = 0; i < plan->count; ++i) {
		struct chrdata *chr = g_MpAllChrPtrs[i];
		struct prop *prop = chr ? chr->prop : NULL;
		const u64 generation = propGetLifecycleGeneration(prop);
		if (!generation || prop->chr != chr || prop->parent
				|| (i == 0 ? (prop != host->prop || prop->type != PROPTYPE_PLAYER || chr->aibot)
					: (prop->type != PROPTYPE_CHR || !chr->aibot))) return 0;
		for (s32 j = 0; j < i; ++j) {
			if (plan->actors[j].prop == prop) return 0;
		}
		grid_layout_actor_t *actor = &plan->actors[i];
		*actor = (grid_layout_actor_t){.chr = chr, .prop = prop, .player = i == 0 ? host : NULL,
			.origin = prop->pos, .old_room = prop->rooms[0]};
		f32 ymin, ymax;
		propGetBbox(prop, &actor->radius, &ymax, &ymin);
		const f32 feet = actor->player ? actor->player->vv_manground : chr->manground;
		actor->ymin = ymin - feet;
		actor->ymax = ymax - feet;
		if (!(actor->radius > 0.0f && actor->radius <= 100.0f)
				|| !(actor->ymax > 0.0f && actor->ymax <= 400.0f)
				|| !__builtin_isfinite(actor->ymin) || actor->ymin > actor->ymax) return 0;
		const f32 relative_root = prop->pos.y - feet;
		const s32 root_valid = __builtin_isfinite(relative_root) && fabsf(relative_root) <= actor->ymax;
		const f32 eye_height = actor->player ? actor->player->vv_eyeheight : 0.0f;
		actor->root_height = root_valid ? relative_root
			: (eye_height > 0.0f && eye_height <= actor->ymax ? eye_height : 0.0f);
		struct coord feet_pos = prop->pos;
		feet_pos.y = feet;
		actor->assigned = root_valid && gridLayoutFinite(&prop->pos)
			&& gridLayoutPositionClear(collision, actor, &feet_pos, actor->old_room, 0, &actor->position);
	}
	return 1;
}

static void gridLayoutAddPad(s32 padnum, s32 *pads, s32 *count)
{
	if (padnum < 0 || padnum >= g_PadsFile->numpads) return;
	for (s32 i = 0; i < *count; ++i) {
		if (pads[i] == padnum) return;
	}
	pads[(*count)++] = padnum;
}

static s32 gridLayoutCollectPads(s32 pads[GRID_LAYOUT_MAX_PADS], s32 *count)
{
	if (!g_PadsFile || !g_StageSetup.padfiledata || !g_StageSetup.waypoints
			|| g_NumSpawnPoints < 0 || g_NumSpawnPoints > MAX_MPCHRS) return 0;
	for (s32 i = 0; i < g_NumSpawnPoints; ++i) gridLayoutAddPad(g_SpawnPoints[i], pads, count);
	for (s32 i = 0; i <= GRID_LAYOUT_MAX_WAYPOINTS; ++i) {
		const s32 padnum = g_StageSetup.waypoints[i].padnum;
		if (padnum < 0) return *count > 0;
		if (i == GRID_LAYOUT_MAX_WAYPOINTS) return 0;
		gridLayoutAddPad(padnum, pads, count);
	}
	return 0;
}

static s32 gridLayoutActorSpaceClear(const grid_layout_actors_t *plan, const grid_layout_actor_t *actor,
		const grid_layout_position_t *position)
{
	for (s32 i = 0; i < plan->count; ++i) {
		const grid_layout_actor_t *other = &plan->actors[i];
		if (other == actor || !other->assigned) continue;
		const f32 other_bottom = other->position.feet.y + (other->relocate ? 0.0f : other->ymin);
		const f32 other_top = other->position.feet.y + other->ymax;
		if (position->feet.y > other_top || position->feet.y + actor->ymax < other_bottom) continue;
		const f64 dx = (f64)other->position.feet.x - position->feet.x;
		const f64 dz = (f64)other->position.feet.z - position->feet.z;
		const f64 separation = other->radius + actor->radius + 0.5f;
		if (dx * dx + dz * dz <= separation * separation) return 0;
	}
	return 1;
}

static s32 gridLayoutChoosePad(const struct colmesh *collision, const grid_layout_actors_t *plan,
		grid_layout_actor_t *actor, const s32 *pads, s32 count)
{
	const struct model *model = actor->chr->model;
	if ((actor->player && (actor->player->bondmovemode != MOVEMODE_WALK || actor->player->inlift))
			|| actor->chr->inlift || actor->chr->actiontype == ACT_SKJUMP) return 0;
	if (model) {
		struct modelnode *root = model->definition ? model->definition->rootnode : NULL;
		if (!root || !root->rodata || !model->rwdatas || (root->type & 0xff) != MODELNODETYPE_CHRINFO) return 0;
		actor->model_root = modelGetNodeRwData(actor->chr->model, root);
		if (!actor->model_root) return 0;
	}
	s32 choice = -1;
	s32 best_room = 0;
	f64 best_distance = 0.0;
	const s32 compare_distance = gridLayoutFinite(&actor->origin);
	for (s32 i = 0; i < count; ++i) {
		struct pad pad = {0};
		padUnpack(pads[i], PADFIELD_POS | PADFIELD_ROOM, &pad);
		grid_layout_position_t position;
		if (!gridLayoutPositionClear(collision, actor, &pad.pos, pad.room, 1, &position)
				|| !gridLayoutActorSpaceClear(plan, actor, &position)) continue;
		const s32 same_room = pad.room == actor->old_room;
		const f64 dx = compare_distance ? (f64)actor->origin.x - pad.pos.x : 0.0;
		const f64 dz = compare_distance ? (f64)actor->origin.z - pad.pos.z : 0.0;
		const f64 dist = dx * dx + dz * dz;
		if (choice >= 0 && (same_room < best_room || (same_room == best_room
				&& (dist > best_distance || (dist == best_distance && pads[i] >= choice))))) continue;
		choice = pads[i];
		best_room = same_room;
		best_distance = dist;
		actor->position = position;
	}
	actor->assigned = actor->relocate = choice >= 0;
	return actor->assigned;
}

static s32 gridLayoutRoomRegistrationFits(const grid_layout_actors_t *plan)
{
	s32 additions[GRID_LAYOUT_ROOM_COUNT] = {0};
	for (s32 i = 0; i < plan->count; ++i) {
		const grid_layout_actor_t *actor = &plan->actors[i];
		if (actor->relocate) additions[actor->position.room]++;
	}
	/* Reserve capacity conservatively without unlinking any live prop. */
	s32 needed = 0;
	for (s32 room = 1; room < GRID_LAYOUT_ROOM_COUNT; ++room) {
		needed += (additions[room] + MAX_PROPSPERROOMCHUNK - 1) / MAX_PROPSPERROOMCHUNK;
	}
	if (needed && !g_RoomPropListChunks) return 0;
	for (s32 i = 0; needed && i < MAX_ROOMPROPLISTCHUNKS; ++i) {
		if (g_RoomPropListChunks[i].propnums[0] == -2) --needed;
	}
	return needed == 0;
}

s32 gridLayoutPrepareActors(const struct colmesh *collision, grid_layout_actors_t **out,
		char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!out || *out || !collision || !collision->tris || collision->numtris <= 0
			|| collision->numtris > GRID_LAYOUT_MAX_TRIANGLES || g_Vars.roomcount != GRID_LAYOUT_ROOM_COUNT) {
		return gridLayoutActorError(err, err_cap, "The private layout has no compatible actor collision.");
	}
	grid_layout_actors_t plan = {0};
	if (!gridLayoutCaptureActors(collision, &plan)) {
		return gridLayoutActorError(err, err_cap, "The live player and bots are not ready for the layout change.");
	}
	s32 pads[GRID_LAYOUT_MAX_PADS];
	s32 pad_count = 0;
	for (s32 i = 0; i < plan.count; ++i) {
		grid_layout_actor_t *actor = &plan.actors[i];
		if (actor->assigned) continue;
		if ((!pad_count && !gridLayoutCollectPads(pads, &pad_count))
				|| !gridLayoutChoosePad(collision, &plan, actor, pads, pad_count)) {
			return gridLayoutActorError(err, err_cap, "The layout would leave a player or bot without a safe position.");
		}
	}
	if (!gridLayoutRoomRegistrationFits(&plan)) {
		return gridLayoutActorError(err, err_cap, "The new actor rooms cannot be registered safely.");
	}
	grid_layout_actors_t *prepared = malloc(sizeof(*prepared));
	if (!prepared) return gridLayoutActorError(err, err_cap, "Could not prepare the live actors.");
	*prepared = plan;
	*out = prepared;
	return 1;
}

static void gridLayoutMoveActor(grid_layout_actor_t *actor)
{
	struct chrdata *chr = actor->chr;
	struct prop *prop = actor->prop;
	struct player *player = actor->player;
	const grid_layout_position_t *position = &actor->position;
	struct coord root = position->feet;
	root.y += actor->root_height;
	RoomNum rooms[] = {position->room, -1};
	prop->pos = root;
	propSetRooms(prop, rooms);
	roomsCopySafe(rooms, chr->oldrooms, ARRAYCOUNT(chr->oldrooms));
	chr->prevpos = root;
	chr->ground = chr->manground = position->ground;
	chr->sumground = position->ground * (PAL ? 8.4175090789795f : 9.999998f);
	chr->floorcol = position->floorcol;
	chr->floortype = position->floortype;
	chr->floorroom = position->room;
	chr->chrflags |= CHRCFLAG_FORCETOGROUND;
	chr->fallspeed = (struct coord){0};
	chr->onladder = 0;
	if (actor->model_root) {
		modelSetRootPosition(chr->model, &root);
		actor->model_root->ground = position->ground;
	}
	if (player) {
		player->bondprevpos = root;
		roomsCopySafe(rooms, player->bondprevrooms, ARRAYCOUNT(player->bondprevrooms));
		player->vv_ground = player->vv_manground = position->ground;
		player->sumground = position->ground / (PAL ? 0.054400026798248f : 0.045499980449677f);
		player->floorcol = position->floorcol;
		player->floorflags = position->floorflags;
		player->floortype = position->floortype;
		player->floorroom = position->room;
		player->bdeltapos = (struct coord){0};
		player->isfalling = false;
		player->fallstart = 0;
		player->onladder = false;
		player->memcamroom = -1;
		player->unk1c64 = 1;
	}
	if (chr->aibot) {
		/* Only a relocated bot's cached path is now stale. The regular bot
		 * tick rebuilds it from the same command and live goals. */
		chr->aibot->waypoints[0] = NULL;
		chr->aibot->numwaystepstotarget = 0;
		chr->aibot->forcemainloop = true;
		if (chr->actiontype == ACT_GOPOS) {
			chr->act_gopos = (struct act_gopos){0};
			chr->actiontype = ACT_NULL;
		}
	}
}

void gridLayoutCommitActors(grid_layout_actors_t *prepared)
{
	if (!prepared || !prepared->count) return;
	s32 relocated = 0;
	for (s32 i = 0; i < prepared->count; ++i) {
		grid_layout_actor_t *actor = &prepared->actors[i];
		if (!actor->relocate) continue;
		const s32 witness = i == 0 && smokeHarnessIsActive() && sysArgCheck("--debug-grid-private-layout-relocate");
		if (witness) bootGridLayoutSmokeActorState(0);
		gridLayoutMoveActor(actor);
		if (witness) bootGridLayoutSmokeActorState(1);
		++relocated;
	}
	sysLogPrintf(LOG_NOTE, "GRID.LAYOUT.ACTORS: checked=%d relocated=%d", prepared->count, relocated);
	prepared->count = 0;
}

void gridLayoutReleaseActors(grid_layout_actors_t *prepared)
{
	free(prepared);
}
