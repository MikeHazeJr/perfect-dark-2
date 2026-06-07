/**
 * loader_walker_sfx.c -- Step 4 (2026-05-03).
 *
 * Walks data/<romid>/audio/sfx/*.pdsfx and registers each as ASSET_AUDIO
 * with category=AUDIO_CAT_SFX. Compound ZIP per Section 2.7; manifest
 * carries `format` + `sample_rate_hz` + `data_size` + provenance fields.
 */

#include <stddef.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "fs.h"
#include "loader_walker.h"
#include "loader_walker_common.h"

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    (void)pd_kind;

    s64 source_index = -1;
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
    loaderWalkerEnvelopeInt(manifest, manifest_len, "source_index", &source_index);
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

    asset_entry_t *e = assetCatalogRegisterAudio(
        id, (s32)source_index,
        /* name: */ "",
        AUDIO_CAT_SFX,
        /* duration_ms: */ 0,
        /* file_path: */ "");
    if (e) {
        loaderWalkerMarkBaseArchiveEntry(e);
        e->source_soundnum = (s32)source_index;
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
        if (loaderWalkerArchiveMemberPath(file_path, source_member,
                                          source_path, sizeof(source_path))) {
            catalogSetPrimaryFile(e, source_path);
        }
    }
    return e ? 1 : -1;
}

void loaderWalkerScanSfx(const char *tier_dir,
                          loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        .kind_str = "sfx",
        .subdir = "audio/sfx",
        .extension = ".pdsfx",
        .always_invoke = 1,
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}
