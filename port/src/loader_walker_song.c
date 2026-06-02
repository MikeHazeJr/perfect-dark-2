/**
 * loader_walker_song.c -- Step 4 (2026-05-03).
 *
 * Walks data/<romid>/audio/music/*.pdsong and registers each as
 * ASSET_AUDIO with category=AUDIO_CAT_MUSIC.
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
    if (!loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "midi",
                                     source_member, sizeof(source_member))
            && !loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "data",
                                            source_member, sizeof(source_member))) {
        strncpy(source_member, "sequence.mid", sizeof(source_member) - 1);
        source_member[sizeof(source_member) - 1] = '\0';
    }

    asset_entry_t *e = assetCatalogRegisterAudio(
        id, (s32)source_index,
        /* name: */ "",
        AUDIO_CAT_MUSIC,
        /* duration_ms: */ 0,
        /* file_path: */ "");
    if (e) {
        loaderWalkerMarkBaseArchiveEntry(e);
        if (loaderWalkerArchiveMemberPath(file_path, source_member,
                                          source_path, sizeof(source_path))) {
            catalogSetPrimaryFile(e, source_path);
        }
    }
    return e ? 1 : -1;
}

void loaderWalkerScanSongs(const char *tier_dir,
                            loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        .kind_str = "song",
        .subdir = "audio/music",
        .extension = ".pdsong",
        .always_invoke = 1,
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}
