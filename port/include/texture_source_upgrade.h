#ifndef PD_TEXTURE_SOURCE_UPGRADE_H
#define PD_TEXTURE_SOURCE_UPGRADE_H
#include "texture_source_properties.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Insert missing legacy properties without rewriting any original descriptor
 * bytes. Returns 1 and malloc-owned output for an upgrade, 0 for an already
 * versioned descriptor, or -1 on rejection. Outputs are empty on 0/-1. */
int textureSourceUpgradeDescriptor(const char *text, size_t size, const char *id,
    const texture_source_properties_t *defaults, char **output, size_t *output_size,
    char *error, size_t error_cap);
/* Atomic archive upgrade, including descriptor hash/inventory records. Other
 * members and archive comment survive; invalid input leaves the file unchanged.
 * Returns 1 upgraded, 0 already current, or -1 rejected. */
int textureSourceUpgradeArchive(const char *path, const char *id,
    const texture_source_properties_t *defaults, char *error, size_t error_cap);
/* Add stage-owned material rules to an existing level.graph.json, preserving
 * authored graph topology. A version marker makes later optional omissions
 * intentional. For an arena, member selects its embedded scenario archive. */
int textureSourceUpgradeStageArchive(const char *path, const char *member,
    const char *properties_json, char *error, size_t error_cap);
#ifdef __cplusplus
}
#endif
#endif
