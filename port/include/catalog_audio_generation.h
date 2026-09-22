#ifndef PD_CATALOG_AUDIO_GENERATION_H
#define PD_CATALOG_AUDIO_GENERATION_H
#include <stddef.h>
#include "audio.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct catalog_audio_generation catalog_audio_generation_t;
/* Main client/audio-update thread. Own exact public source PCM, parameters
 * and a leaf sound slot until final Release; source edits create generations. */
catalog_audio_generation_t *catalogAudioGenerationAcquire(const char *id, char *error, size_t cap);
void catalogAudioGenerationRetain(catalog_audio_generation_t *);
void catalogAudioGenerationRelease(catalog_audio_generation_t *);
s32 catalogAudioGenerationSlot(const catalog_audio_generation_t *);
const char *catalogAudioGenerationHash(const catalog_audio_generation_t *);
/* Borrowed until caller releases its retained generation. */
const s16 *catalogAudioGenerationPcm(const catalog_audio_generation_t *, u32 *frames, s32 *source_rate);
catalog_audio_generation_t *catalogAudioGenerationForSlot(s32 slot);
/* Ordinary voices own a PCM copy, so final generation release cannot stop or
 * invalidate a voice already playing. No catalog/path lookup occurs here. */
struct sndstate *catalogAudioGenerationStart(const catalog_audio_generation_t *,
    u16 volume, u8 pan, f32 pitch, u8 fxmix, u8 fxbus, struct sndstate **handle);
#ifdef __cplusplus
}
#endif
#endif
