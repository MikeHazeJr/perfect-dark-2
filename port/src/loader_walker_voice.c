/**
 * loader_walker_voice.c -- Step 4 (2026-05-03).
 *
 * Walks data/<romid>/audio/voice/*.pdvoice and registers each as
 * ASSET_AUDIO with category=AUDIO_CAT_VOICE. Per Q-2 type-tolerance the
 * playback layer accepts a .pdvoice catalog ID anywhere a .pdsfx ID is
 * expected; the discriminator is the registered ext.audio.category.
 */

#include <stddef.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "fs.h"
#include "loader_walker.h"
#include "loader_walker_common.h"
#include "system.h"

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    (void)pd_kind;

    s64 source_index = -1;
    s64 source_filenum = -1;
    s64 sample_rate_hz = 0;
    s64 decoded_sample_count = 0;
    s64 key_min = 0;
    s64 key_max = 127;
    s64 key_base = 60;
    s64 key_detune = 0;
    s64 velocity_min = 0;
    s64 velocity_max = 0;
    s64 sample_pan = 64;
    s64 sample_volume = 127;
    s64 has_loop = 0;
    s64 loop_start_samples = 0;
    s64 loop_end_samples = 0;
    s64 loop_count = 0;
    s64 attack_time_us = 0;
    s64 decay_time_us = 0;
    s64 release_time_us = 0;
    s64 attack_volume = 127;
    s64 decay_volume = 127;
    s32 has_envelope = 0;
    char source_member[128];
    char source_path[FS_MAXPATH + 1];
    char *voice_ini = NULL;
    size_t voice_ini_len = 0;
    char actor[64] = "";
    char transcript[512] = "";
    char language[16] = "";
    char context[128] = "";
    char subtitle_member[128] = "";
    char fallback_locale[16] = "";
    char locale_members[6][128] = {{0}};
    loaderWalkerEnvelopeInt(manifest, manifest_len, "source_index", &source_index);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "source_filenum", &source_filenum);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "sample_rate_hz", &sample_rate_hz);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "decoded_sample_count", &decoded_sample_count);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "key_min", &key_min);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "key_max", &key_max);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "key_base", &key_base);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "key_detune", &key_detune);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "velocity_min", &velocity_min);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "velocity_max", &velocity_max);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "sample_pan", &sample_pan);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "sample_volume", &sample_volume);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "loop_start_samples", &loop_start_samples);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "loop_end_samples", &loop_end_samples);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "loop_count", &loop_count);
    has_loop = loop_end_samples > loop_start_samples || loop_count != 0;
    has_envelope = loaderWalkerEnvelopeInt(manifest, manifest_len, "attack_time_us", &attack_time_us);
    has_envelope = loaderWalkerEnvelopeInt(manifest, manifest_len, "decay_time_us", &decay_time_us) || has_envelope;
    has_envelope = loaderWalkerEnvelopeInt(manifest, manifest_len, "release_time_us", &release_time_us) || has_envelope;
    has_envelope = loaderWalkerEnvelopeInt(manifest, manifest_len, "attack_volume", &attack_volume) || has_envelope;
    has_envelope = loaderWalkerEnvelopeInt(manifest, manifest_len, "decay_volume", &decay_volume) || has_envelope;
    if (!loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "data",
                                     source_member, sizeof(source_member))) {
        strncpy(source_member, "sample.wav", sizeof(source_member) - 1);
        source_member[sizeof(source_member) - 1] = '\0';
    }
    if (loaderWalkerArchiveTextMember(file_path, "voice.ini",
                                      &voice_ini, &voice_ini_len)) {
        loaderWalkerIniValueCopy(voice_ini, voice_ini_len, "voice", "actor",
                                 actor, sizeof(actor));
        loaderWalkerIniValueCopy(voice_ini, voice_ini_len, "voice", "transcript",
                                 transcript, sizeof(transcript));
        loaderWalkerIniValueCopy(voice_ini, voice_ini_len, "voice", "language",
                                 language, sizeof(language));
        loaderWalkerIniValueCopy(voice_ini, voice_ini_len, "voice", "context",
                                 context, sizeof(context));
        loaderWalkerIniValueCopy(voice_ini, voice_ini_len, "voice", "subtitle_file",
                                 subtitle_member, sizeof(subtitle_member));
        loaderWalkerIniValueCopy(voice_ini, voice_ini_len, "voice", "fallback_locale",
                                 fallback_locale, sizeof(fallback_locale));
        {
            static const char *locale_keys[6] = {
                "locale_en_file", "locale_fr_file", "locale_de_file",
                "locale_it_file", "locale_es_file", "locale_ja_file"
            };
            for (s32 i = 0; i < 6; i++) {
                loaderWalkerIniValueCopy(voice_ini, voice_ini_len, "voice",
                    locale_keys[i], locale_members[i], sizeof(locale_members[i]));
            }
        }
        sysMemFree(voice_ini);
    }

    asset_entry_t *e = assetCatalogRegisterAudio(
        id, (s32)source_index,
        /* name: */ "",
        AUDIO_CAT_VOICE,
        (sample_rate_hz > 0 && decoded_sample_count > 0)
            ? (s32)((decoded_sample_count * 1000 + sample_rate_hz / 2) / sample_rate_hz)
            : 0,
        /* file_path: */ "");
    if (e) {
        loaderWalkerMarkBaseArchiveEntry(e);
        e->source_soundnum = (s32)source_index;
        e->source_filenum = (s32)source_filenum;
        e->ext.audio.has_keymap = 1;
        e->ext.audio.key_min = (s32)key_min;
        e->ext.audio.key_max = (s32)key_max;
        e->ext.audio.key_base = (s32)key_base;
        e->ext.audio.key_detune = (s32)key_detune;
        e->ext.audio.velocity_min = (s32)velocity_min;
        e->ext.audio.velocity_max = (s32)velocity_max;
        e->ext.audio.sample_pan = (s32)sample_pan;
        e->ext.audio.sample_volume = (s32)sample_volume;
        e->ext.audio.has_loop = (s32)has_loop;
        e->ext.audio.loop_start_samples = (u32)loop_start_samples;
        e->ext.audio.loop_end_samples = (u32)loop_end_samples;
        e->ext.audio.loop_count = (u32)loop_count;
        e->ext.audio.has_envelope = has_envelope;
        e->ext.audio.attack_time_us = (u32)attack_time_us;
        e->ext.audio.decay_time_us = (u32)decay_time_us;
        e->ext.audio.release_time_us = (u32)release_time_us;
        e->ext.audio.attack_volume = (s32)attack_volume;
        e->ext.audio.decay_volume = (s32)decay_volume;
        strncpy(e->ext.audio.voice_actor, actor,
                sizeof(e->ext.audio.voice_actor) - 1);
        strncpy(e->ext.audio.voice_transcript, transcript,
                sizeof(e->ext.audio.voice_transcript) - 1);
        strncpy(e->ext.audio.voice_language, language,
                sizeof(e->ext.audio.voice_language) - 1);
        strncpy(e->ext.audio.voice_context, context,
                sizeof(e->ext.audio.voice_context) - 1);
        strncpy(e->ext.audio.fallback_locale, fallback_locale,
                sizeof(e->ext.audio.fallback_locale) - 1);
        if (subtitle_member[0]) {
            loaderWalkerArchiveMemberPath(file_path, subtitle_member,
                e->ext.audio.subtitle_file, sizeof(e->ext.audio.subtitle_file));
        }
        for (s32 i = 0; i < 6; i++) {
            if (locale_members[i][0]) {
                loaderWalkerArchiveMemberPath(file_path, locale_members[i],
                    e->ext.audio.locale_audio_files[i],
                    sizeof(e->ext.audio.locale_audio_files[i]));
            }
        }
        if (loaderWalkerArchiveMemberPath(file_path, source_member,
                                          source_path, sizeof(source_path))) {
            catalogSetPrimaryFile(e, source_path);
        }
    }
    return e ? 1 : -1;
}

void loaderWalkerScanVoices(const char *tier_dir,
                             loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        .kind_str = "voice",
        .subdir = "audio/voice",
        .extension = ".pdvoice",
        .always_invoke = 1,
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}
