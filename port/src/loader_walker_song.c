/**
 * loader_walker_song.c -- Step 4 (2026-05-03).
 *
 * Walks data/<romid>/audio/music/*.pdsong and registers each as
 * ASSET_AUDIO with category=AUDIO_CAT_MUSIC.
 */

#include <stddef.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "loader_walker.h"
#include "loader_walker_common.h"

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    (void)pd_kind; (void)file_path;
    (void)manifest; (void)manifest_len;

    asset_entry_t *e = assetCatalogRegisterAudio(
        id, /* sound_id: */ 0,
        /* name: */ "",
        AUDIO_CAT_MUSIC,
        /* duration_ms: */ 0,
        /* file_path: */ "");
    return e ? 1 : -1;
}

void loaderWalkerScanSongs(const char *tier_dir,
                            loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "song", "audio/music", ".pdsong",
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}
