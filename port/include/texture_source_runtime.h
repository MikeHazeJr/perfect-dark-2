#ifndef PD_TEXTURE_SOURCE_RUNTIME_H
#define PD_TEXTURE_SOURCE_RUNTIME_H
#include "assetcatalog.h"
#include "texture_source_properties.h"
#ifdef __cplusplus
extern "C" {
#endif
struct texture;
/* Shared image/material selection; suppressed overlays select public base sources. */
int textureSourceRuntimeCatalogIndex(int number);
void textureSourceRuntimeInvalidate(void);
void textureSourceRuntimeResetStage(void);
const struct texture *textureSourceRuntimeDefinition(int number);
int textureSourceRuntimeReadSelected(const asset_entry_t *entry, const char *image_path,
    texture_source_properties_t *properties, char *error, size_t cap);
int textureSourceRuntimeStageGraph(const char *json, size_t size, int multiplayer,
    int bundled, char *error, size_t error_cap);
#ifdef __cplusplus
}
#endif
#endif
