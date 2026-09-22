#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <SDL.h>
#include "assetcatalog.h"
#include "assetcatalog_anim_slots.h"
#include "assetprovider.h"
#include "catalog_animation_generation.h"
#include "modasset_compiler.h"
#include "lib/anim.h"
#include "data.h"
#include "system.h"

struct catalog_animation_generation {
    struct catalog_animation_generation *next;
    char id[CATALOG_ID_LEN];
    char hash[65];
    u32 references;
    s32 slot;
    struct animtableentry animation;
    u8 *data;
    u32 size;
};

static catalog_animation_generation_t *s_Generations;
static catalog_animation_generation_t *s_BySlot[ANIM_CUSTOM_END_SLOT];
static SDL_threadID s_AnimationThread;

static s32 onAnimationThread(void)
{
    const SDL_threadID current = SDL_ThreadID();
    if (!s_AnimationThread) s_AnimationThread = current;
    if (s_AnimationThread == current) return 1;
    sysLoudFailf("ANIMATION.GENERATION.THREAD", "animation generation access outside its client thread");
    return 0;
}

static void setError(char *error, size_t capacity, const char *text)
{
    if (error && capacity) snprintf(error, capacity, "%s", text);
}

typedef struct selected_clip {
    const char *id;
    asset_entry_t entry;
    char path[FS_MAXPATH + 1];
    s32 found;
} selected_clip_t;

static void captureClip(const asset_entry_t *entry, void *userdata)
{
    selected_clip_t *selected = userdata;
    if (strcmp(entry->id, selected->id) || !assetCatalogAnimationCategoryUsesCharacterClip(entry->category)) return;
    const asset_data_handle_t source = catalogEffectiveHandle(entry);
    if (source.provider != fileProvider()) return;
    const char *path = fileProviderPath(source);
    if (!path || !modAssetCompilerIsAnimationSource(path) || strlen(path) >= sizeof(selected->path)) return;
    selected->entry = *entry;
    strcpy(selected->path, path);
    selected->found = 1;
}

catalog_animation_generation_t *catalogAnimationGenerationAcquire(
    const char *id, char *error, size_t capacity)
{
    if (error && capacity) error[0] = 0;
    if (!onAnimationThread() || !id || !strchr(id, ':') || strlen(id) >= CATALOG_ID_LEN || !g_Anims) {
        setError(error, capacity, "animation runtime or catalog identity unavailable");
        return NULL;
    }
    selected_clip_t *selected = calloc(1, sizeof(*selected));
    catalog_animation_generation_t *candidate = calloc(1, sizeof(*candidate));
    if (!selected || !candidate) {
        free(selected); free(candidate);
        setError(error, capacity, "animation generation allocation failed");
        return NULL;
    }
    strcpy(candidate->id, id);
    selected->id = candidate->id;
    assetCatalogIterateByType(ASSET_ANIMATION, captureClip, selected);
    if (!selected->found || modAssetCompilerBuildAnimationClipHashed(&selected->entry,
            selected->path, &candidate->animation, &candidate->data, &candidate->size, candidate->hash) <= 0
            || !candidate->data || !candidate->size) {
        setError(error, capacity, "selected public animation source could not compile");
        goto fail;
    }
    /* Replacement decoding borrows these owned bytes directly; the fixed base
     * DMA scratch dimensions do not constrain compiled source generations. */
    for (catalog_animation_generation_t *p = s_Generations; p; p = p->next) {
        if (strcmp(p->id, candidate->id) || strcmp(p->hash, candidate->hash)) continue;
        catalogAnimationGenerationRetain(p);
        modAssetCompilerFreeAnimationClip(candidate->data);
        free(candidate); free(selected);
        return p;
    }
    candidate->slot = assetCatalogReserveAnimGenerationSlot();
    if (candidate->slot < 0 || candidate->slot >= animGetTotalCount()) {
        if (candidate->slot >= 0) assetCatalogReleaseAnimGenerationSlot(candidate->slot);
        setError(error, capacity, "no private native animation generation slot available");
        goto fail;
    }
    candidate->references = 1;
    animInvalidateCacheEntry((s16)candidate->slot);
    g_Anims[candidate->slot] = candidate->animation;
    g_Anims[candidate->slot].data = 0xffffffff;
    candidate->next = s_Generations;
    s_Generations = candidate;
    s_BySlot[candidate->slot] = candidate;
    free(selected);
    return candidate;
fail:
    modAssetCompilerFreeAnimationClip(candidate->data);
    free(candidate); free(selected);
    return NULL;
}

void catalogAnimationGenerationRetain(catalog_animation_generation_t *generation)
{
    if (!generation || !onAnimationThread()) return;
    if (generation->references == UINT_MAX) {
        sysLoudFailf("ANIMATION.GENERATION.REFCOUNT", "animation generation reference count overflow");
        return;
    }
    ++generation->references;
}

void catalogAnimationGenerationRelease(catalog_animation_generation_t *generation)
{
    if (!generation || !onAnimationThread() || --generation->references) return;
    catalog_animation_generation_t **link = &s_Generations;
    while (*link && *link != generation) link = &(*link)->next;
    if (*link) *link = generation->next;
    s_BySlot[generation->slot] = NULL;
    animInvalidateCacheEntry((s16)generation->slot);
    if (g_Anims && generation->slot < animGetTotalCount())
        memset(&g_Anims[generation->slot], 0, sizeof(g_Anims[generation->slot]));
    assetCatalogReleaseAnimGenerationSlot(generation->slot);
    modAssetCompilerFreeAnimationClip(generation->data);
    free(generation);
}

s32 catalogAnimationGenerationSlot(const catalog_animation_generation_t *generation)
{
    return generation ? generation->slot : -1;
}

u32 catalogAnimationGenerationSize(const catalog_animation_generation_t *generation)
{
    return generation ? generation->size : 0;
}

const char *catalogAnimationGenerationHash(const catalog_animation_generation_t *generation)
{
    return generation ? generation->hash : NULL;
}

const void *catalogAnimationGenerationData(s32 slot)
{
    if (slot < ANIM_CUSTOM_START || slot >= ANIM_CUSTOM_END_SLOT || !onAnimationThread()) return NULL;
    return s_BySlot[slot] ? s_BySlot[slot]->data : NULL;
}
