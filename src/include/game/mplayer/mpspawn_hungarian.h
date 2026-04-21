#ifndef IN_GAME_MPSPAWN_HUNGARIAN_H
#define IN_GAME_MPSPAWN_HUNGARIAN_H

#include <stdint.h>
#include "game/spawnpool.h"
#include "types.h"

/**
 * Min-cost perfect matching on an n x n cost matrix (row-major).
 * Rows 0..n_part-1: real costs; rows n_part..n-1 may be zero padding.
 * Fills match_out[i] = assigned column for row i (-1 if unset).
 */
int64_t mpHungarianMinSquare(s32 n, const int64_t *a_rowmajor, s32 *match_out);

#endif
