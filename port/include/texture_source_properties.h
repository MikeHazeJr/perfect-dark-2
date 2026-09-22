#ifndef PD_TEXTURE_SOURCE_PROPERTIES_H
#define PD_TEXTURE_SOURCE_PROPERTIES_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Public texture.ini properties; source names never expose native texture IDs. */
#define TEXTURE_SOURCE_ALL_PROPERTIES 0x3fu
typedef struct texture_source_properties {
    unsigned char surface_type, sound_surface_type;
    unsigned char tile_column_offset, tile_row_offset;
    unsigned char mask_s_reduction, mask_t_reduction;
    unsigned char present_mask;
    unsigned char properties_version;
} texture_source_properties_t;
int textureSourceReadProperties(const char *ini, size_t size, const char *expected_id,
    texture_source_properties_t *out, char *error, size_t cap);
const char *textureSourceSurfaceName(unsigned value);
#ifdef __cplusplus
}
#endif
#endif
