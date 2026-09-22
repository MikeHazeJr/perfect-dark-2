#ifndef IN_GAME_MPSPAWN_ORCHESTRATE_H
#define IN_GAME_MPSPAWN_ORCHESTRATE_H

#include "constants.h"
#include "types.h"

/**
 * After spawn pool build and MP chr allocation: team anchors (max-spread on
 * pool) + Voronoi soft costs, global min-cost assignment (Hungarian), optional
 * bottleneck swap refinement, origin/duplicate relax, then applies human
 * spawns. botSpawn() consumes g_MpOrchestrateBotPoolIdx per bot.
 * Deterministic ordering for lockstep (same host seed + roster + stage).
 */
enum mp_orchestrate_spawn_result {
	MP_ORCHESTRATE_SPAWN_OK = 0,
	MP_ORCHESTRATE_SPAWN_NOT_READY = -1,
	MP_ORCHESTRATE_SPAWN_INVALID_POOL = -2,
	MP_ORCHESTRATE_SPAWN_INVALID_ROSTER = -3,
	MP_ORCHESTRATE_SPAWN_NO_PARTICIPANTS = -4,
	MP_ORCHESTRATE_SPAWN_NO_ASSIGNMENT = -5,
	MP_ORCHESTRATE_SPAWN_PLAYER_PREPARE_FAILED = -6,
	MP_ORCHESTRATE_SPAWN_PLAYER_COMMIT_REJECTED = -7,
	MP_ORCHESTRATE_SPAWN_PLAYER_BATCH_OVERLAP = -8,
};

enum mp_orchestrate_spawn_result mpOrchestrateMatchStartSpawns(void);
const char *mpOrchestrateSpawnResultString(
	enum mp_orchestrate_spawn_result result);

void mpOrchestrateReset(void);

extern bool g_MpOrchestrateInitialSpawnDone;
extern s32 g_MpOrchestrateBotPoolIdx[MAX_BOTS];

#endif
