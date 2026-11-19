#ifndef PD_ASSETPROVIDER_CHECKPOINT_H
#define PD_ASSETPROVIDER_CHECKPOINT_H

#include <stddef.h>
#include <PR/ultratypes.h>

/* Internal file-source admission checkpoints, independent of ROM bridges. */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct file_provider_checkpoint {
	s32 pool_used;
	s32 path_count;
	s32 warned;
} file_provider_checkpoint_t;

/* Scanner admission can intern several source paths before a later sibling
 * rejects. These internal checkpoints make that append-only mutation part of
 * the same catalog transaction. */
s32 fileProviderCheckpointCreate(file_provider_checkpoint_t *checkpoint);
s32 fileProviderCheckpointRestore(const file_provider_checkpoint_t *checkpoint);

/* Check whether a batch fits current path and byte capacity. Exact existing
 * and repeated spellings consume no extra capacity. This is a query only. */
s32 fileProviderCanInternPaths(const char *const *paths, size_t count);

#ifdef __cplusplus
}
#endif
#endif
