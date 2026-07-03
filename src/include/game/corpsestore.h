#ifndef _IN_GAME_CORPSESTORE_H
#define _IN_GAME_CORPSESTORE_H

#include <ultra64.h>
#include "data.h"
#include "types.h"

/**
 * Frozen corpse store (c132, 2026-07-03) -- PC campaign body persistence,
 * bake-to-static foundation.
 *
 * When an enemy corpse settles in solo campaign, its `struct model` is detached
 * from the `struct chrdata`, the chr slot is freed back to the pool, and the
 * model is rendered STATICALLY every frame (frozen death pose, no AI / physics /
 * anim advance / chr tick). This is the low-risk foundation that reuses the
 * real per-node display lists with the frozen matrices, so textures / combiner /
 * lighting are correct by construction; the GPU vertex-merge batching layer for
 * hundreds-of-corpses is a separate optimization on top.
 *
 * Gated: disabled unless corpseStoreSetEnabled(true). When disabled, the OG
 * corpse-fade / headroom-cap path (chraTickBg) owns corpses instead.
 *
 * Records live in MEMPOOL_STAGE and are cleared on stage load, so corpses
 * persist for the mission and never leak across a stage transition.
 */

/** Stage lifecycle: allocate/clear the record array. Call at stage load. */
void corpseStoreReset(void);

/** Runtime gate. Default disabled (OG fade path owns corpses). */
void corpseStoreSetEnabled(bool enabled);
bool corpseStoreIsEnabled(void);

/**
 * True when `chr` is a settled solo-campaign enemy corpse eligible to be frozen
 * (ACT_DEAD, not a bot, not fading, dead long enough for the death anim to
 * finish). Consulted by chraTickBg before calling corpseStoreFreeze.
 */
bool corpseStoreShouldFreeze(struct chrdata *chr);

/**
 * Freeze `chr` into a static corpse record: snapshot its model + world
 * placement + room list + render lighting, detach the model so the chr free
 * does not free it, and mark the chr for the normal safe reap (which returns
 * its slot to the pool). No-op (returns false) if the store is full or the chr
 * has no model; the caller then leaves the corpse on the OG path.
 */
bool corpseStoreFreeze(struct chrdata *chr);

/**
 * Render all frozen corpses whose room list includes `roomnum`, into `gdl`.
 * Opaque, z-buffered; call from the OPA world pass per room (bg.c). Returns the
 * advanced gdl pointer.
 */
Gfx *corpseStoreRenderRoom(Gfx *gdl, RoomNum roomnum);

/**
 * True if `model` has been claimed by the corpse store (a detached corpse).
 * The chr reap (chrRemove) calls this and SKIPS modelmgrFreeModel for owned
 * models, so a frozen corpse keeps its model + rwdata binding for rendering.
 */
bool corpseStoreOwnsModel(struct model *model);

/** Number of frozen corpses currently stored (diagnostics). */
s32 corpseStoreGetCount(void);

#endif
