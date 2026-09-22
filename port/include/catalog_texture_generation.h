#ifndef PD_CATALOG_TEXTURE_GENERATION_H
#define PD_CATALOG_TEXTURE_GENERATION_H
#include <stddef.h>
#include <PR/ultratypes.h>
#ifdef __cplusplus
extern "C" {
#endif
struct tex;
struct texture;
typedef struct catalog_texture_generation catalog_texture_generation_t;
/* Main client thread. Exact selected FileProvider descriptor/image inputs;
 * no catalog row, stage-pool pointer or ROM source is retained. */
catalog_texture_generation_t *catalogTextureGenerationAcquire(
    const char *catalog_id, char *error, size_t capacity);
/* Borrow exact captured source bytes for this call. A NULL/zero descriptor
 * denotes a plain standard image with default material properties. source_id
 * is a logical caller-owned identity, never a native slot or renderer pointer. */
catalog_texture_generation_t *catalogTextureGenerationAcquireSource(
    const char *source_id, const void *image, u32 image_size,
    const char *descriptor, u32 descriptor_size, char *error, size_t capacity);
void catalogTextureGenerationRetain(catalog_texture_generation_t *generation);
void catalogTextureGenerationRelease(catalog_texture_generation_t *generation);
const char *catalogTextureGenerationId(const catalog_texture_generation_t *generation);
const char *catalogTextureGenerationHash(const catalog_texture_generation_t *generation);
s32 catalogTextureGenerationSlot(const catalog_texture_generation_t *generation);
/* Borrowed native views remain valid while a generation lease is held. */
struct tex *catalogTextureGenerationTexture(catalog_texture_generation_t *generation);
const struct texture *catalogTextureGenerationDefinition(const catalog_texture_generation_t *generation);
catalog_texture_generation_t *catalogTextureGenerationForSlot(s32 slot);
#ifdef __cplusplus
}
#endif
#endif
