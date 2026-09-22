#ifndef PD_TEXTURE_STAGE_SOURCE_H
#define PD_TEXTURE_STAGE_SOURCE_H
#include "texture_source_properties.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*texture_stage_property_visitor)(const char *id,
    const texture_source_properties_t *properties, void *context);
/* Validate the complete level graph material section before visiting any rows.
 * mode is all, solo or multiplayer. Versioned optional omissions mean no rules.
 * require_version distinguishes migrated bundled content from authored mods. */
int textureStageSourceRead(const char *json, size_t size, int multiplayer,
    int require_version, texture_stage_property_visitor visitor, void *context,
    char *error, size_t error_cap);
#ifdef __cplusplus
}
#endif
#endif
