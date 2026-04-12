/**
 * stb_vorbis.h — Minimal public API for OGG Vorbis decoding (Batch A-6)
 *
 * Wraps stb_vorbis (public domain, Sean Barrett) for use by the mod music
 * system. Only the decode-from-file API is exposed; the full stb_vorbis
 * header is in the implementation file.
 *
 * License: Public Domain / MIT (dual-licensed by original author)
 */

#ifndef _IN_STB_VORBIS_H
#define _IN_STB_VORBIS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Decode an entire OGG Vorbis file to interleaved S16 PCM.
 *
 * @param filename   Path to .ogg file
 * @param channels   Output: number of channels (1 or 2)
 * @param sample_rate Output: sample rate in Hz
 * @param output     Output: malloc'd buffer of interleaved S16 samples
 * @return           Total number of samples decoded (across all channels),
 *                   or -1 on failure. Caller must free(*output).
 */
int stb_vorbis_decode_filename(const char *filename, int *channels,
                               int *sample_rate, short **output);

#ifdef __cplusplus
}
#endif

#endif /* _IN_STB_VORBIS_H */
