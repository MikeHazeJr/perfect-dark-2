#ifndef PD_ASSETPROVIDER_CHECKPOINT_H
#define PD_ASSETPROVIDER_CHECKPOINT_H

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

#ifdef __cplusplus
}
#endif
#endif
