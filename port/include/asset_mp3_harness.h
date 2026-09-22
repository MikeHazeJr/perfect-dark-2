#ifndef _IN_ASSET_MP3_HARNESS_H
#define _IN_ASSET_MP3_HARNESS_H
#include <PR/ultratypes.h>

typedef struct asset_mp3_witness {
    const s16 *pcm;
    u32 frames;
    s32 playing;
    s16 soundnum;
    s32 response_timer240;
} asset_mp3_witness_t;

/* Read-only production state witness and dormant repeat-control exercise;
 * both are available only during an explicitly active smoke run. */
s32 sndMp3HarnessWitness(asset_mp3_witness_t *out);
s32 sndMp3HarnessRepeat(s32 enabled);
int assetSourceMp3HarnessRun(void);
#endif
