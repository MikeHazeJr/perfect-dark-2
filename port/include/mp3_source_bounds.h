#ifndef PD_MP3_SOURCE_BOUNDS_H
#define PD_MP3_SOURCE_BOUNDS_H

#include <PR/ultratypes.h>

/* The retained audio DMA cache always fills one complete window even when its
 * caller requests fewer bytes. Exact public-source allocations therefore own
 * one zeroed guard window after the authored MP3 bytes. */
#define MP3_SOURCE_DMA_WINDOW 0x400u

#ifdef __cplusplus
extern "C" {
#endif

/* Returns nonzero and writes the required guarded allocation size when the
 * exact source size fits the signed decoder domain. Failure leaves out_size
 * untouched. */
s32 mp3SourceAllocationSize(u32 source_size, u32 *out_size);

/* Clamp one logical read or prefetch to the exact authored source. Invalid,
 * negative, and end-of-file requests return zero without arithmetic wrap. */
s32 mp3SourceClampRead(s32 source_size, s32 offset, s32 requested);

#ifdef __cplusplus
}
#endif

#endif /* PD_MP3_SOURCE_BOUNDS_H */
