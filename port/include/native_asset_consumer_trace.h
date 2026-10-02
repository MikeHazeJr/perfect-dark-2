#ifndef PD_NATIVE_ASSET_CONSUMER_TRACE_H
#define PD_NATIVE_ASSET_CONSUMER_TRACE_H
#include <stddef.h>
#ifdef __cplusplus
#define PD_TRACE_NOEXCEPT noexcept
extern "C" {
#else
#define PD_TRACE_NOEXCEPT
#endif

struct native_asset_consumer_model_read {
    const char *path;
    const void *bytes;
    size_t size;
};
struct native_asset_consumer_texture_binding {
    const char *catalog_id;
    const char *source_hash;
    int native_slot;
};
int nativeAssetConsumerModelRequested(const char *catalog_id, const char *selected_source) PD_TRACE_NOEXCEPT;
void nativeAssetConsumerExpectTexture(const char *parent_id, const char *model_source,
    const char *texture_id) PD_TRACE_NOEXCEPT;
int nativeAssetConsumerTextureRequested(const char *catalog_id) PD_TRACE_NOEXCEPT;
/* Borrow the exact descriptor/image buffers just consumed by the public-source
 * generation path. Pixel data belongs to that successfully retained generation. */
void nativeAssetConsumerEmitTexture(const char *catalog_id, const char *image_path,
    const void *image, size_t image_size, const char *descriptor_path,
    const void *descriptor, size_t descriptor_size, const char *source_hash,
    int native_slot, unsigned width, unsigned height,
    const void *pixels, size_t pixel_size) PD_TRACE_NOEXCEPT;
/* Optional operational evidence only. Called after stable native publication;
 * input buffers are the actual compiler/texture callback snapshots. Never
 * changes model success, asset selection or generation lifetime. */
void nativeAssetConsumerEmitModel(const char *catalog_id, const char *source,
    const void *archive_bytes, size_t archive_size,
    const struct native_asset_consumer_model_read *reads, size_t read_count,
    const char *native_closure_hash, const char *const *pending_dependencies,
    size_t dependency_count, const struct native_asset_consumer_texture_binding *bindings,
    size_t binding_count) PD_TRACE_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#undef PD_TRACE_NOEXCEPT
#endif
