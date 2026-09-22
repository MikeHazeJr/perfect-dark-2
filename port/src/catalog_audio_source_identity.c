#include <string.h>
#include "catalog_audio_source_identity.h"

static s32 validCategory(s32 category)
{
    return category == AUDIO_CAT_SFX || category == AUDIO_CAT_VOICE
        || category == AUDIO_CAT_MUSIC;
}

s32 catalogAudioSourceIdentityPrepare(const asset_entry_t *prior,
    const char *id, s32 category, catalog_audio_source_identity_t *out)
{
    catalog_audio_source_identity_t candidate;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    out->sound_id = out->source_soundnum = out->source_filenum = -1;
    if (!id || !id[0] || !memchr(id, '\0', CATALOG_ID_LEN)
            || !validCategory(category)) return 0;
    candidate = *out;
    strcpy(candidate.id, id);
    candidate.category = category;
    if (prior) {
        if (!prior->occupied || prior->type != ASSET_AUDIO
                || !memchr(prior->id, '\0', sizeof(prior->id))
                || strcmp(prior->id, id) != 0
                || !validCategory(prior->ext.audio.category)
                /* SFX and VOICE share the sample identity domain. Music
                 * sound_id is a track index and must never cross that domain. */
                || ((prior->ext.audio.category == AUDIO_CAT_MUSIC)
                    != (category == AUDIO_CAT_MUSIC))) return 0;
        candidate.sound_id = prior->ext.audio.sound_id;
        candidate.source_soundnum = prior->source_soundnum;
        candidate.source_filenum = prior->source_filenum;
    }
    *out = candidate;
    return 1;
}

s32 catalogAudioSourceIdentityApply(asset_entry_t *entry,
    const catalog_audio_source_identity_t *identity)
{
    if (!entry || !identity || entry->type != ASSET_AUDIO
            || !entry->occupied || !identity->id[0]
            || !memchr(identity->id, '\0', sizeof(identity->id))
            || !memchr(entry->id, '\0', sizeof(entry->id))
            || strcmp(entry->id, identity->id) != 0
            || !validCategory(identity->category)) return 0;
    entry->ext.audio.category = identity->category;
    entry->ext.audio.sound_id = identity->sound_id;
    entry->source_soundnum = identity->source_soundnum;
    entry->source_filenum = identity->source_filenum;
    return 1;
}
