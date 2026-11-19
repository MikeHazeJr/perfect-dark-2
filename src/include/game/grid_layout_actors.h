#ifndef GAME_GRID_LAYOUT_ACTORS_H
#define GAME_GRID_LAYOUT_ACTORS_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

struct colmesh;
typedef struct grid_layout_actors grid_layout_actors_t;

/* Prepare immediately before the host's private Combat world swap. The exact
 * candidate keeps room IDs, native pads and routes compatible. *out starts
 * NULL and is assigned only when every actor has a safe destination. */
s32 gridLayoutPrepareActors(const struct colmesh *collision, grid_layout_actors_t **out,
		char *err, size_t err_cap);

/* Apply the plan once after publishing the prepared banks, in the same tick
 * and without running any actor updates between preparation and commit. */
void gridLayoutCommitActors(grid_layout_actors_t *prepared);
void gridLayoutReleaseActors(grid_layout_actors_t *prepared);

#ifdef __cplusplus
}
#endif

#endif
