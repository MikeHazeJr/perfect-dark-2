/**
 * stb_vorbis.h — C ABI for the vendored upstream Ogg Vorbis decoder.
 *
 * Wraps stb_vorbis (public domain, Sean Barrett) for use by the mod music
 * system. The complete upstream implementation and license are retained in
 * port/external/stb_vorbis.c (commit 1ee679ca2ef753a528db5ba6801e1067b40481b8).
 *
 * License: Public Domain / MIT (dual-licensed by original author)
 */

#ifndef _IN_STB_VORBIS_H
#define _IN_STB_VORBIS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stb_vorbis stb_vorbis;

/* Exact layouts from the pinned upstream public header. */
typedef struct {
    char *alloc_buffer;
    int alloc_buffer_length_in_bytes;
} stb_vorbis_alloc;

typedef struct {
    unsigned int sample_rate;
    int channels;
    unsigned int setup_memory_required;
    unsigned int setup_temp_memory_required;
    unsigned int temp_memory_required;
    int max_frame_size;
} stb_vorbis_info;

/* Memory input remains owned by the caller and must outlive the decoder.
 * Filename input is opened and closed by the decoder. NULL means failure. */
stb_vorbis *stb_vorbis_open_memory(const unsigned char *data, int data_len,
    int *error, const stb_vorbis_alloc *alloc_buffer);
stb_vorbis *stb_vorbis_open_filename(const char *filename, int *error,
    const stb_vorbis_alloc *alloc_buffer);
stb_vorbis_info stb_vorbis_get_info(stb_vorbis *decoder);
void stb_vorbis_close(stb_vorbis *decoder);

/* Decode into caller-owned S16 storage. num_shorts is the total interleaved
 * capacity. The return value is frames per channel, not interleaved samples;
 * zero means no further frames. Check get_error after each call. */
int stb_vorbis_get_samples_short_interleaved(stb_vorbis *decoder,
    int channels, short *buffer, int num_shorts);

/* Return and clear the decoder's last error; zero means no reported error. */
int stb_vorbis_get_error(stb_vorbis *decoder);

#ifdef __cplusplus
}
#endif

#endif /* _IN_STB_VORBIS_H */
