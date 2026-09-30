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
#ifdef __cplusplus
}
#endif
#endif
