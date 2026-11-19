#ifndef PD_WEAPON_GRAPH_V2_INGRESS_H
#define PD_WEAPON_GRAPH_V2_INGRESS_H
#include <stdint.h>
#include "weapon_graph_archive.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Version dispatch from captured public source. 0 is a v1 weapon, 1 is a
 * completely inspected v2 mode set, -1 rejects malformed/unsupported source.
 * No registry, slot or gameplay mutation. Production v2 requires explicit
 * neutral idle metadata and a selected public .pdmesh model. */
int wgV2ArchiveInspect(const void *bytes, uint32_t size,
    weapon_graph_archive_descriptor_t *out, char *error, size_t capacity);
/* The .pdweapon layout requires a variables member and a shared-context member,
 * and held_single_shot.v1 can use neither. A v2 archive may therefore declare
 * them only in the forms that ask nothing of the runtime: an empty variable
 * list, and exactly the four standard contexts every base weapon carries
 * (owner_player, owner_team, weapon_instance, damage_credit_player). Anything
 * else, and any presentation binding, would be authored behavior this runtime
 * silently ignores, so it is refused. Returns 1 when the declared bindings are
 * acceptable (or absent), 0 with a reason otherwise. */
int wgV2ArchiveUnusedBindingsAccepted(const void *bytes, uint32_t size,
    const weapon_graph_archive_descriptor_t *descriptor, char *error, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
