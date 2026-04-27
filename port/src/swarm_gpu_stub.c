/*
 * swarm_gpu_stub.c -- transitional stub for the GPU swarm sim (S483).
 *
 * This file lands in the swarm_test runtime commit so the build links
 * cleanly before the real GPU module exists. When the strong
 * implementation in port/fast3d/swarm_gpu.cpp lands in the next
 * commit, THIS FILE IS DELETED to avoid duplicate-symbol errors at
 * link time.
 *
 * Both stub and real signatures live in extern "C" so the C++ TU and
 * this C TU can interchangeably provide them.
 */

#include <PR/ultratypes.h>
#include "types.h"

s32 swarmGpuAvailable(void)
{
	return 0;
}

void swarmGpuStepAndApply(struct coord *player_pos,
                          struct chrdata **chrs,
                          s32 count)
{
	(void)player_pos;
	(void)chrs;
	(void)count;
}
