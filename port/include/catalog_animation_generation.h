#ifndef PD_CATALOG_ANIMATION_GENERATION_H
#define PD_CATALOG_ANIMATION_GENERATION_H
#include <stddef.h>
#include <PR/ultratypes.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct catalog_animation_generation catalog_animation_generation_t;
/* Client animation-thread API. Compile the currently selected public source,
 * own its native bytes, and reserve a private slot until the last Release.
 * Catalog retirement/reset and subsequent source edits cannot change a retained
 * generation. Identical ID/source generations share their owned payload. */
catalog_animation_generation_t *catalogAnimationGenerationAcquire(
    const char *id, char *error, size_t capacity);
void catalogAnimationGenerationRetain(catalog_animation_generation_t *generation);
void catalogAnimationGenerationRelease(catalog_animation_generation_t *generation);
s32 catalogAnimationGenerationSlot(const catalog_animation_generation_t *generation);
u32 catalogAnimationGenerationSize(const catalog_animation_generation_t *generation);
const char *catalogAnimationGenerationHash(const catalog_animation_generation_t *generation);
/* Borrowed by the ordinary animation router while its caller retains an owner.
 * A generation has no mutable catalog row or provider-path dependency. */
const void *catalogAnimationGenerationData(s32 slot);
#ifdef __cplusplus
}
#endif
#endif
