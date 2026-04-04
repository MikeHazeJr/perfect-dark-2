#ifndef _IN_MOD_H
#define _IN_MOD_H

#include <PR/ultratypes.h>

struct animtableentry;

s32 modTextureLoad(u16 num, void *dst, u32 dstSize);

s32 modAnimationLoadDescriptor(u16 num, struct animtableentry *anim);
void *modAnimationLoadData(u16 num);

/* C-6 supplement: returns loaded animation data if a catalog mod override
 * exists for this ROM-based animation (data != 0xffffffff). Returns NULL if
 * no override is registered — caller falls through to ROM DMA. */
void *modAnimationTryCatalogOverride(u16 num);

void *modSequenceLoad(u16 num, u32 *outSize);

#endif
