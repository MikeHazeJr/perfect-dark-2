#ifndef PD_ROMEXTRACT_TEXTURE_STAGE_H
#define PD_ROMEXTRACT_TEXTURE_STAGE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Caller frees the returned JSON with free(). */
char *romExtractStageTexturePropertiesJson(const char *stage_id, char *error, size_t error_cap);
int romExtractTextureStageProperties(const char *path, const char *member,
    const char *stage_id, char *error, size_t error_cap);
#ifdef __cplusplus
}
#endif
#endif
