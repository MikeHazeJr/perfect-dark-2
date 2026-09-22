#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <SDL.h>
#include <PR/gbi.h>
#include "constants.h"
#include "types.h"
#include "assetcatalog.h"
#include "assetcatalog_texture_slots.h"
#include "assetprovider.h"
#include "catalog_texture_generation.h"
#include "texture_source_properties.h"
#include "mod.h"
#include "game/tex.h"
#include "fs.h"
#include "sha256.h"
#include "system.h"
#include "../fast3d/gfx_api.h"

struct catalog_texture_generation {
    struct catalog_texture_generation *next;
    char *id, hash[SHA256_HEX_SIZE];
    u32 references;
    s32 slot;
    u8 *allocation;
    struct tex texture;
    struct texture definition;
};
static catalog_texture_generation_t *s_Generations, *s_BySlot[TEXTURE_CUSTOM_END];
static SDL_threadID s_TextureThread;
static s32 onTextureThread(void)
{
    const SDL_threadID current = SDL_ThreadID();
    if (!s_TextureThread) s_TextureThread = current;
    if (current == s_TextureThread) return 1;
    sysLoudFailf("TEXTURE.GENERATION.THREAD", "texture generation access outside its client thread");
    return 0;
}
static void errorText(char *error, size_t cap, const char *text)
{
    if (error && cap) snprintf(error, cap, "%s", text);
}
static void hashBytes(sha256_ctx *hash, const void *bytes, u32 size)
{
    const u8 length[] = {(u8)(size >> 24), (u8)(size >> 16), (u8)(size >> 8), (u8)size};
    sha256Update(hash, length, sizeof(length));
    if (size) sha256Update(hash, bytes, size);
}
catalog_texture_generation_t *catalogTextureGenerationAcquireSource(
    const char *id, const void *image, u32 image_size,
    const char *descriptor, u32 descriptor_size, char *error, size_t cap)
{
    if (error && cap) error[0] = 0;
    if (!onTextureThread() || !id || !id[0] || !image || !image_size
            || ((!descriptor) != (!descriptor_size))) {
        errorText(error, cap, "invalid captured texture source"); return NULL;
    }
    texture_source_properties_t properties = {0};
    if (descriptor && !textureSourceReadProperties(descriptor, descriptor_size, id,
            &properties, error, cap)) return NULL;
    sha256_ctx hash; u8 digest[SHA256_DIGEST_SIZE]; char hex[SHA256_HEX_SIZE];
    const char version[] = "pd.texture.generation.rgba.v1";
    sha256Init(&hash); sha256Update(&hash, version, sizeof(version));
    hashBytes(&hash, image, image_size); hashBytes(&hash, descriptor, descriptor_size);
    sha256Final(&hash, digest); sha256ToHex(digest, hex);
    for (catalog_texture_generation_t *old = s_Generations; old; old = old->next) {
        if (strcmp(old->id, id) || strcmp(old->hash, hex)) continue;
        if (old->references == UINT_MAX) {
            errorText(error, cap, "texture generation reference count overflow"); return NULL;
        }
        ++old->references; return old;
    }
    mod_texture_rgba32_source_t rgba = {0};
    catalog_texture_generation_t *g = calloc(1, sizeof(*g));
    if (!g) { errorText(error, cap, "texture generation allocation failed"); return NULL; }
    g->slot = -1;
    g->id = strdup(id);
    if (!g->id) { errorText(error, cap, "texture identity allocation failed"); goto fail; }
    if (modTextureDecodeRgba32Source(image, image_size, &rgba, error, cap) <= 0) goto fail;
    if (rgba.data_size <= 0 || (u32)rgba.data_size > UINT_MAX - 16u) {
        errorText(error, cap, "texture native storage overflow"); goto fail;
    }
    /* Align pixels and preserve the native reverse-identity prefix at data-8. */
    g->allocation = sysMemAlloc((u32)rgba.data_size + 16u);
    if (!g->allocation) { errorText(error, cap, "texture pixel allocation failed"); goto fail; }
    memset(g->allocation, 0, 16);
    g->texture.data = g->allocation + 16;
    memcpy(g->texture.data, rgba.pixels, (size_t)rgba.data_size);
    g->texture.width = (u8)rgba.width; g->texture.height = (u8)rgba.height;
    g->texture.numlods = 1; g->texture.gbiformat = G_IM_FMT_RGBA;
    g->texture.depth = G_IM_SIZ_32b;
    g->texture.lutmodeindex = G_TT_NONE >> G_MDSFT_TEXTLUT;
    g->definition.surfacetype = properties.surface_type;
    g->definition.soundsurfacetype = properties.sound_surface_type;
    g->definition.unk04_00 = properties.tile_column_offset;
    g->definition.unk04_04 = properties.tile_row_offset;
    g->definition.unk04_08 = properties.mask_s_reduction;
    g->definition.unk04_0c = properties.mask_t_reduction;
    if (properties.mask_s_reduction > texDimensionToMask(g->texture.width)
            || properties.mask_t_reduction > texDimensionToMask(g->texture.height)
            || properties.tile_column_offset + texGetLineSizeInBytes(&g->texture, 0)
                * properties.tile_row_offset > 0x1ff) {
        errorText(error, cap, "texture tile properties exceed decoded image or native tile range"); goto fail;
    }
    g->slot = assetCatalogReserveTextureGenerationSlot();
    if (g->slot < 0) { errorText(error, cap, "no retained native texture slot available"); goto fail; }
    g->texture.texturenum = (u32)g->slot;
    *(s16 *)(g->texture.data - 8) = (s16)g->slot;
    strcpy(g->hash, hex); g->references = 1;
    g->next = s_Generations; s_Generations = g; s_BySlot[g->slot] = g;
    modTextureFreeRgba32Source(&rgba);
    return g;
fail:
    modTextureFreeRgba32Source(&rgba);
    if (g->allocation) sysMemFree(g->allocation);
    free(g->id); free(g);
    return NULL;
}

typedef struct texture_selection {
    const char *id;
    char image[FS_MAXPATH + 1], descriptor[FS_MAXPATH + 1];
    s32 bundled, found;
} texture_selection_t;
static void captureTexture(const asset_entry_t *entry, void *context)
{
    texture_selection_t *selected = context;
    if (strcmp(entry->id, selected->id)) return;
    const asset_data_handle_t source = catalogEffectiveHandle(entry);
    if (source.provider != fileProvider()) return;
    const char *path = fileProviderPath(source);
    if (!path || !path[0] || strlen(path) >= sizeof(selected->image)) return;
    strcpy(selected->image, path);
    if (!entry->source.override.provider && entry->descriptor_path[0]) {
        if (strlen(entry->descriptor_path) >= sizeof(selected->descriptor)) return;
        strcpy(selected->descriptor, entry->descriptor_path);
    } else {
        /* Bundled image bindings predate descriptor_path. Resolve only the
         * public sibling in their selected archive, including nested archives. */
        const char *last = NULL;
        for (const char *p = strstr(path, "::"); p; p = strstr(p + 2, "::")) last = p;
        if (!last) return;
        size_t root = (size_t)(last + 2 - path);
        if (root + sizeof("texture.ini") > sizeof(selected->descriptor)) return;
        memcpy(selected->descriptor, path, root);
        memcpy(selected->descriptor + root, "texture.ini", sizeof("texture.ini"));
    }
    selected->bundled = entry->bundled; selected->found = 1;
}
catalog_texture_generation_t *catalogTextureGenerationAcquire(const char *id, char *error, size_t cap)
{
    if (error && cap) error[0] = 0;
    const char *colon = id ? strchr(id, ':') : NULL;
    if (!onTextureThread() || !colon || colon == id || !colon[1]
            || strchr(colon + 1, ':') || strlen(id) >= CATALOG_ID_LEN) {
        errorText(error, cap, "invalid texture catalog identity"); return NULL;
    }
    texture_selection_t selected = {0}; selected.id = id;
    assetCatalogIterateByType(ASSET_TEXTURE, captureTexture, &selected);
    if (!selected.found) { errorText(error, cap, "selected public texture source unavailable"); return NULL; }
    u32 ini_size = 0, image_size = 0;
    char *ini = fsFileLoad(selected.descriptor, &ini_size);
    void *image = fsFileLoad(selected.image, &image_size);
    catalog_texture_generation_t *g = NULL;
    texture_source_properties_t properties;
    if (!ini || !ini_size || !image || !image_size) errorText(error, cap, "selected texture descriptor or image unavailable");
    else if (textureSourceReadProperties(ini, ini_size, id, &properties, error, cap)) {
        /* Old base extraction omitted these properties. Reject incomplete
         * sources instead of borrowing mutable ROM/stage material metadata. */
        if (selected.bundled && !properties.properties_version
                && properties.present_mask != TEXTURE_SOURCE_ALL_PROPERTIES)
            errorText(error, cap, "base texture lacks public material properties; extraction upgrade required");
        else g = catalogTextureGenerationAcquireSource(id, image, image_size, ini, ini_size, error, cap);
    }
    free(ini); free(image); return g;
}
void catalogTextureGenerationRetain(catalog_texture_generation_t *g)
{
    if (!g || !onTextureThread()) return;
    if (g->references == UINT_MAX) { sysLoudFailf("TEXTURE.GENERATION.REFCOUNT", "texture generation reference count overflow"); return; }
    ++g->references;
}
void catalogTextureGenerationRelease(catalog_texture_generation_t *g)
{
    if (!g || !onTextureThread() || --g->references) return;
    /* Flush queued rendering and evict address-keyed cache entries before
     * freeing pixels or allowing this native identity to be reused. */
    gfx_texture_cache_delete(g->texture.data);
    catalog_texture_generation_t **link = &s_Generations;
    while (*link && *link != g) link = &(*link)->next;
    if (*link) *link = g->next;
    s_BySlot[g->slot] = NULL;
    assetCatalogReleaseTextureGenerationSlot(g->slot);
    sysMemFree(g->allocation); free(g->id); free(g);
}
const char *catalogTextureGenerationId(const catalog_texture_generation_t *g) { return g ? g->id : NULL; }
const char *catalogTextureGenerationHash(const catalog_texture_generation_t *g) { return g ? g->hash : NULL; }
s32 catalogTextureGenerationSlot(const catalog_texture_generation_t *g) { return g ? g->slot : -1; }
struct tex *catalogTextureGenerationTexture(catalog_texture_generation_t *g) { return g ? &g->texture : NULL; }
const struct texture *catalogTextureGenerationDefinition(const catalog_texture_generation_t *g) { return g ? &g->definition : NULL; }
catalog_texture_generation_t *catalogTextureGenerationForSlot(s32 slot)
{
    if (slot < TEXTURE_CUSTOM_START || slot >= TEXTURE_CUSTOM_END || !s_BySlot[slot]) return NULL;
    return onTextureThread() ? s_BySlot[slot] : NULL;
}
