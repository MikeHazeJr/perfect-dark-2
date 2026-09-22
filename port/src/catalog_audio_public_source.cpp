#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "catalog_audio_public_source.h"
#include "asset_path_contract.h"
#include "modasset_gltf_document.h"
#include "../external/imgui-node-editor/crude_json.h"

static const char *value(const ini_section_t *ini, const char *key)
{
    for (s32 i = 0; i < ini->count; i++)
        if (!strcmp(ini->pairs[i].key, key)) return ini->pairs[i].value;
    return NULL;
}

static s32 integer(const ini_section_t *ini, const char *key, int64_t minimum,
    int64_t maximum, int64_t fallback, int64_t *out)
{
    const char *text = value(ini, key);
    uint64_t magnitude = 0;
    s32 negative = 0;
    if (!text) { *out = fallback; return 1; }
    if (*text == '-' || *text == '+') negative = *text++ == '-';
    if (!*text) return 0;
    for (; *text; text++) {
        if (*text < '0' || *text > '9' || magnitude > (UINT32_MAX - (u32)(*text - '0')) / 10u) return 0;
        magnitude = magnitude * 10 + (u32)(*text - '0');
    }
    *out = negative ? -(int64_t)magnitude : (int64_t)magnitude;
    return *out >= minimum && *out <= maximum;
}

static s32 boolean(const ini_section_t *ini, const char *key, s32 fallback, s32 *out)
{
    const char *text = value(ini, key);
    if (!text) { *out = fallback; return 1; }
    if (!strcmp(text, "1") || !strcmp(text, "true") || !strcmp(text, "yes")) { *out = 1; return 1; }
    if (!strcmp(text, "0") || !strcmp(text, "false") || !strcmp(text, "no")) { *out = 0; return 1; }
    return 0;
}

static s32 copy(const ini_section_t *ini, const char *key, char *out, size_t cap, s32 path)
{
    const char *text = value(ini, key);
    if (path && text && assetPathHasParentTraversal(text)) return 0;
    return assetPathCopyChecked(out, cap, text);
}

s32 catalogAudioPublicSourceParse(const ini_section_t *ini, s32 require_file,
    catalog_audio_public_source_t *out)
{
    catalog_audio_public_source_t parsed;
    int64_t number;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!ini || ini->count < 0 || ini->count > INI_MAX_PAIRS) return 0;
    for (s32 i = 0; i < ini->count; i++) {
        if (!memchr(ini->pairs[i].key, '\0', sizeof(ini->pairs[i].key))
                || !memchr(ini->pairs[i].value, '\0', sizeof(ini->pairs[i].value))) return 0;
        for (s32 j = 0; j < i; j++)
            if (!strcmp(ini->pairs[i].key, ini->pairs[j].key)) return 0;
    }
    memset(&parsed, 0, sizeof(parsed));
#define NUMBER(field, minimum, maximum, fallback) \
    if (!integer(ini, #field, minimum, maximum, fallback, &number)) return 0; \
    parsed.field = (s32)number
#define UNSIGNED(field, minimum) \
    if (!integer(ini, #field, minimum, UINT32_MAX, 0, &number)) return 0; \
    parsed.field = (u32)number
    /* SFX-bank ALKeyMap names are historical: these are packed control bytes,
     * not MIDI note/velocity ranges. key_min carries volume/chain bits;
     * key_max carries flags/fxmix; velocity_min/max carry chain/delay data. */
    NUMBER(key_min, 0, 255, 0);
    NUMBER(key_max, 0, 255, 127);
    NUMBER(key_base, 0, 127, 60);
    NUMBER(key_detune, -128, 127, 0);
    NUMBER(velocity_min, 0, 255, 0);
    NUMBER(velocity_max, 0, 255, 0);
    NUMBER(sample_pan, 0, 127, 64);
    NUMBER(sample_volume, 0, 127, 127);
    NUMBER(attack_volume, 0, 127, 127);
    NUMBER(decay_volume, 0, 127, 127);
    UNSIGNED(loop_start_samples, 0);
    UNSIGNED(loop_end_samples, 0);
    UNSIGNED(loop_count, -1);
    UNSIGNED(attack_time_us, -1);
    UNSIGNED(decay_time_us, -1);
    UNSIGNED(release_time_us, -1);
#undef NUMBER
#undef UNSIGNED
    if (!boolean(ini, "has_keymap", value(ini, "key_base") != NULL, &parsed.has_keymap)
            || !boolean(ini, "has_loop", 0, &parsed.has_loop)
            || !boolean(ini, "has_envelope", 0, &parsed.has_envelope)) return 0;
    parsed.has_loop = parsed.has_loop || parsed.loop_end_samples > parsed.loop_start_samples || parsed.loop_count;
    if (parsed.has_loop && parsed.loop_end_samples <= parsed.loop_start_samples) return 0;
    parsed.has_envelope = parsed.has_envelope || parsed.attack_time_us || parsed.decay_time_us
        || parsed.release_time_us || parsed.attack_volume != 127 || parsed.decay_volume != 127;
#define COPY(key, field, is_path) \
    if (!copy(ini, key, parsed.field, sizeof(parsed.field), is_path)) return 0
    COPY("file_path", file_path, 1);
    if (require_file && !parsed.file_path[0]) return 0;
    COPY("actor", voice_actor, 0);
    COPY("transcript", voice_transcript, 0);
    COPY("language", voice_language, 0);
    COPY("context", voice_context, 0);
    COPY("subtitle_file", subtitle_file, 1);
    COPY("fallback_locale", fallback_locale, 0);
    COPY("locale_en_file", locale_audio_files[0], 1);
    COPY("locale_fr_file", locale_audio_files[1], 1);
    COPY("locale_de_file", locale_audio_files[2], 1);
    COPY("locale_it_file", locale_audio_files[3], 1);
    COPY("locale_es_file", locale_audio_files[4], 1);
    COPY("locale_ja_file", locale_audio_files[5], 1);
#undef COPY
    *out = parsed;
    return 1;
}

void catalogAudioPublicSourceApply(asset_entry_t *entry,
    const catalog_audio_public_source_t *source)
{
    if (!entry || entry->type != ASSET_AUDIO || !source) return;
#define APPLY(field) entry->ext.audio.field = source->field
    APPLY(has_keymap); APPLY(key_min); APPLY(key_max); APPLY(key_base); APPLY(key_detune);
    APPLY(velocity_min); APPLY(velocity_max); APPLY(sample_pan); APPLY(sample_volume);
    APPLY(has_loop); APPLY(loop_start_samples); APPLY(loop_end_samples); APPLY(loop_count);
    APPLY(has_envelope); APPLY(attack_time_us); APPLY(decay_time_us); APPLY(release_time_us);
    APPLY(attack_volume); APPLY(decay_volume);
#undef APPLY
#define COPY(field) memcpy(entry->ext.audio.field, source->field, sizeof(source->field))
    COPY(file_path); COPY(voice_actor); COPY(voice_transcript); COPY(voice_language); COPY(voice_context);
    COPY(subtitle_file); COPY(fallback_locale); COPY(locale_audio_files);
#undef COPY
}


s32 catalogAudioNativeIdentityParse(const char *manifest, size_t length,
    s32 *soundnum, s32 *filenum)
{
    if (soundnum) *soundnum = -1;
    if (filenum) *filenum = -1;
    if (!soundnum || !filenum) return 0;
    crude_json::value root;
    if (!modAssetJsonReadValue(manifest, length, root) || !root.is_object()) return 0;
    s32 values[2] = {-1, -1};
    const char *keys[2] = {"source_index", "source_filenum"};
    for (s32 i = 0; i < 2; i++) {
        if (!root.contains(keys[i])) continue;
        const auto &item = root[keys[i]];
        if (!item.is_number()) return 0;
        const double number = item.get<crude_json::number>();
        if (!(number >= -1 && number <= (i == 0 ? 65535 : 2047))) return 0;
        values[i] = (s32)number;
        if ((double)values[i] != number || (i == 1 && values[i] == 0)) return 0;
    }
    if (values[0] < 0 && values[1] < 0) return 0;
    *soundnum = values[0];
    *filenum = values[1];
    return 1;
}
