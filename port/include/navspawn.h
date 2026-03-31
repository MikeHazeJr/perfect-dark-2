#ifndef NAVSPAWN_H
#define NAVSPAWN_H

#include "types.h"

/**
 * Navmesh-based spawn point fallback for SP maps used in MP.
 *
 * Picks random waypoint positions with spacing enforcement (rejection sampling).
 * For each position, probes 4 cardinal directions and sets yaw to face away from
 * the nearest wall. Falls back to random yaw if no walls are detected.
 *
 * Returns the number of successfully generated spawn points (up to max_count).
 * out_pos and out_yaw must have at least max_count elements.
 * Yaw values are in radians.
 */
s32 navspawnGeneratePoints(s32 max_count, struct coord *out_pos, f32 *out_yaw);

#endif /* NAVSPAWN_H */
