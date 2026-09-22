#ifndef PD_CATALOG_MODEL_GENERATION_H
#define PD_CATALOG_MODEL_GENERATION_H

#include <stddef.h>
#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

struct modeldef;
typedef struct catalog_model_generation catalog_model_generation_t;

/* Build from the selected public mesh source, retaining every file and texture
 * used by the native model. The caller owns one reference. An edited source
 * creates another generation; an old model remains valid until its last lease
 * retires. source may be a typed .pdmesh archive or a selected OBJ/glTF/GLB. */
catalog_model_generation_t *catalogModelGenerationAcquireSource(
    const asset_entry_t *entry, const char *source, char *error, size_t capacity);
void catalogModelGenerationRetain(catalog_model_generation_t *generation);
void catalogModelGenerationRelease(catalog_model_generation_t *generation);
const char *catalogModelGenerationHash(const catalog_model_generation_t *generation);
struct modeldef *catalogModelGenerationModeldef(catalog_model_generation_t *generation);
/* Borrowed lookup for the ordinary catalog unload path. */
catalog_model_generation_t *catalogModelGenerationForModeldef(const struct modeldef *modeldef);

#ifdef __cplusplus
}
#endif
#endif
