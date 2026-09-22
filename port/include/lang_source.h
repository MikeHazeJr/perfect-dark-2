#ifndef PD_LANG_SOURCE_H
#define PD_LANG_SOURCE_H

#include <stddef.h>
#include <stdint.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* langGet's private text ID bridge has a nine-bit string index. Logical
 * source extent is retained separately; all unused runtime offsets are zero. */
#define LANG_SOURCE_MAX_STRINGS 512u

typedef enum lang_source_encoding {
    LANG_SOURCE_LATIN1,
    LANG_SOURCE_JAPANESE_UNSUPPORTED
} lang_source_encoding_t;

typedef struct lang_source_bank {
    uintptr_t *data;
    u32 string_count;
    u32 data_size;
} lang_source_bank_t;

/* Canonical English and the non-Japanese region tables use Latin-1 in
 * tools/assetmgr/mklang. JPN-final Japanese uses a distinct packed glyph codec
 * that this unit deliberately does not claim to implement. */
lang_source_encoding_t langSourceEncodingForLocale(const char *rom_id,
    const char *locale);

/* Descriptor integer boundary: one complete nonnegative decimal count. */
s32 langSourceParseCount(const char *text, u32 *out_count);
/* Strict manifest fields, validated before catalog publication. A missing
 * count stays distinguishable from a declared zero for legacy sources. */
s32 langSourceParseManifestFields(const char *json, size_t size,
    s32 *out_bank, u32 *out_count, s32 *out_count_declared);

/* Complete, strict source JSON. Each index in [0, string_count) occurs once;
 * text:null retains a null offset, while text:"" is a nonnull empty string.
 * expected_count=-1 infers extent from the complete indexed rows. The caller
 * owns out->data with free(). Outputs are cleared on every rejection. */
s32 langSourceParseJson(const char *json, size_t size, s32 expected_count,
    lang_source_encoding_t encoding, lang_source_bank_t *out,
    const char **out_error);

/* Export the decompressed native big-endian offset table using the same text
 * codec. expected_count=-1 derives its extent from the first nonzero offset;
 * the canonical sixteen-zero-byte empty bank has extent zero. An explicit
 * count can disambiguate all-null tables. Caller owns *out_json with free(). */
s32 langSourceExportNative(const u8 *data, u32 size, s32 expected_count,
    lang_source_encoding_t encoding, char **out_json, u32 *out_size,
    u32 *out_count, const char **out_error);

#ifdef __cplusplus
}
#endif
#endif
