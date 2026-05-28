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

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    (void)pd_kind;

    s64 source_index = -1;
    char source_member[128];
    char source_path[FS_MAXPATH + 1];
    loaderWalkerEnvelopeInt(manifest, manifest_len, "source_index", &source_index);
    if (!loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "data",
                                     source_member, sizeof(source_member))) {
        strncpy(source_member, "sample.wav", sizeof(source_member) - 1);
        source_member[sizeof(source_member) - 1] = '\0';
    }

    asset_entry_t *e = assetCatalogRegisterAudio(
        id, (s32)source_index,
        /* name: */ "",
        AUDIO_CAT_VOICE,
        /* duration_ms: */ 0,
        /* file_path: */ "");
    if (e) {
        loaderWalkerMarkBaseArchiveEntry(e);
        e->source_soundnum = (s32)source_index;
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
        "voice", "audio/voice", ".pdvoice",
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}
