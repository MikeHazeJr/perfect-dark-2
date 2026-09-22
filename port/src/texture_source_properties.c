#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "texture_source_properties.h"
#include "body_head_source.h"
#include <stdint.h>

static const char *surface_names[] = {
    "default", "stone", "wood", "metal", "glass", "shallow_water", "snow",
    "dirt", "mud", "tile", "metal_object", "character", "glass_translucent",
    "none", "deep_water"
};
const char *textureSourceSurfaceName(unsigned value)
{
    return value < sizeof(surface_names) / sizeof(surface_names[0]) ? surface_names[value] : NULL;
}
static int propertyError(char *error, size_t cap, const char *key)
{
    if (error && cap) snprintf(error, cap, "invalid public texture property: %s", key);
    return 0;
}
static int surface(const ini_section_t *ini, const char *key, unsigned char *out,
    char *error, size_t cap)
{
    const char *value = iniGet(ini, key, NULL);
    *out = 0;
    if (!value) return 1;
    for (unsigned i = 0; i < sizeof(surface_names) / sizeof(surface_names[0]); ++i) {
        if (!strcmp(value, surface_names[i])) { *out = (unsigned char)i; return 1; }
    }
    return propertyError(error, cap, key);
}
static int nibble(const ini_section_t *ini, const char *key, unsigned char *out,
    char *error, size_t cap)
{
    const char *value = iniGet(ini, key, NULL);
    *out = 0;
    if (!value) return 1;
    /* Complete decimal spelling, not strtol's prefix or signed coercion. */
    if (!value[0]) return propertyError(error, cap, key);
    unsigned parsed = 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p < '0' || *p > '9') return propertyError(error, cap, key);
        parsed = parsed * 10 + (*p - '0');
        if (parsed > 15) return propertyError(error, cap, key);
    }
    *out = (unsigned char)parsed;
    return 1;
}
int textureSourceReadProperties(const char *text, size_t size, const char *expected_id,
    texture_source_properties_t *out, char *error, size_t cap)
{
    if (error && cap) error[0] = 0;
    if (!out) return propertyError(error, cap, "output");
    memset(out, 0, sizeof(*out));
    if (size > UINT32_MAX) return propertyError(error, cap, "descriptor size");
    ini_section_t *ini = calloc(1, sizeof(*ini));
    if (!ini) return propertyError(error, cap, "allocation");
    texture_source_properties_t candidate = {0};
    int ok = bodyHeadSourceReadIniBytes(text, size, "texture", ini, error, cap)
        && surface(ini, "surface_type", &candidate.surface_type, error, cap)
        && surface(ini, "sound_surface_type", &candidate.sound_surface_type, error, cap)
        && nibble(ini, "tile_column_offset", &candidate.tile_column_offset, error, cap)
        && nibble(ini, "tile_row_offset", &candidate.tile_row_offset, error, cap)
        && nibble(ini, "mask_s_reduction", &candidate.mask_s_reduction, error, cap)
        && nibble(ini, "mask_t_reduction", &candidate.mask_t_reduction, error, cap);
    if (ok && expected_id) {
        const char *id = iniGet(ini, "catalog_id", NULL);
        if (!id || strcmp(id, expected_id)) ok = propertyError(error, cap, "catalog_id");
    }
    if (ok) {
        const char *version = iniGet(ini, "properties_version", NULL);
        if (version && strcmp(version, "1")) ok = propertyError(error, cap, "properties_version");
        else candidate.properties_version = version ? 1 : 0;
    }
    if (ok) {
        static const char *keys[] = {"surface_type", "sound_surface_type",
            "tile_column_offset", "tile_row_offset", "mask_s_reduction", "mask_t_reduction"};
        for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
            if (iniGet(ini, keys[i], NULL)) candidate.present_mask |= 1u << i;
        *out = candidate;
    }
    free(ini);
    return ok;
}
