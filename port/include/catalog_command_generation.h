#ifndef PD_CATALOG_COMMAND_GENERATION_H
#define PD_CATALOG_COMMAND_GENERATION_H
#include <stddef.h>
#include "weapon_graph_v2_native.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct catalog_command_generation catalog_command_generation_t;
/* Client-thread owned public commands.json closure. Includes stable native
 * command arrays and retained generations for all referenced clips and sounds.
 * Catalog retirement, slot resets and source edits cannot mutate this lease. */
catalog_command_generation_t *catalogCommandGenerationAcquire(const char *id, char *error, size_t cap);
void catalogCommandGenerationRetain(catalog_command_generation_t *);
void catalogCommandGenerationRelease(catalog_command_generation_t *);
const char *catalogCommandGenerationId(const catalog_command_generation_t *);
const char *catalogCommandGenerationHash(const catalog_command_generation_t *);
const struct guncmd *catalogCommandGenerationCommands(const catalog_command_generation_t *);
/* Production catalog/provider adapter for wgV2NativePrepare and equipped
 * preparation. host is unused. Success transfers one complete dependency lease. */
int catalogGraphNativeResolve(void *host, wg_v2_dependency_kind kind,
    const char *id, wg_v2_native_dependency *out, char *error, size_t cap);
#ifdef __cplusplus
}
#endif
#endif
