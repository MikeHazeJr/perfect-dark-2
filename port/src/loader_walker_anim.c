/**
 * loader_walker_anim.c -- Step 4 (2026-05-03).
 *
 * Walks data/<romid>/animations/*.pdanim and registers each as
 * ASSET_ANIMATION. The .pdanim kind is dual-shape per Section 2.6:
 *   - weapon_animation: plain JSON file (gunscript opcodes)
 *   - character_animation: ZIP compound (manifest.json + frames.bin)
 *
 * The walker scaffold auto-detects the container by 2-byte file magic so
 * both shapes resolve to a manifest envelope through the same callback.
 * Existing rows registered earlier in the boot path are skipped at the
 * scaffold layer; this callback only fires for new IDs (the ~1208 chr
 * animations that the in-binary tables do not enumerate).
 */

#include <stddef.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "loader_walker.h"
#include "loader_walker_common.h"

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    (void)pd_kind; (void)file_path;

    s64 frame_count = 0;
    loaderWalkerEnvelopeInt(manifest, manifest_len, "frame_count", &frame_count);

    char category[32];
    char target_body[64];
    loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "category",
                                 category, sizeof(category));
    loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "target_body",
                                 target_body, sizeof(target_body));

    asset_entry_t *e = assetCatalogRegisterAnimation(
        id, /* anim_id: */ 0,
        /* name: */ "",
        (s32)frame_count,
        target_body[0] ? target_body : NULL);
    if (!e) return -1;

    /* Carry the .pdanim category on the catalog row so consumers can
     * filter weapon vs character animations without re-parsing. */
    if (category[0]) {
        size_t n = strlen(category);
        if (n >= sizeof(e->category)) n = sizeof(e->category) - 1;
        memcpy(e->category, category, n);
        e->category[n] = '\0';
    }
    return 1;
}

void loaderWalkerScanAnimations(const char *tier_dir,
                                 loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "animation", "animations", ".pdanim",
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}
