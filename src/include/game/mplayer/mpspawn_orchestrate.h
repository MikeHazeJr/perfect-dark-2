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
void mpOrchestrateMatchStartSpawns(void);

void mpOrchestrateReset(void);

extern bool g_MpOrchestrateInitialSpawnDone;
extern s32 g_MpOrchestrateBotPoolIdx[MAX_BOTS];

#endif
