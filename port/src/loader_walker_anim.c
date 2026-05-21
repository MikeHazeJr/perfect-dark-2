/**
 * loader_walker_anim.c -- Catalog universality pivot Step 4 + 5
 * (2026-05-03).
 *
 * Walks animation .pdanim files. The .pdanim kind is a ZIP compound
 * per Section 2.6:
 *   - weapon_animation: manifest.json carries gunscript opcodes for
 *     loaderPoolParseAnimationJson; opcodes.json is the editable source.
 *   - character_animation: manifest.json + header.tsv / frames.tsv;
 *     the catalog row carries enough envelope info for consumers; no
 *     pool payload (chr animation byte streams live in romextract segments).
 *
 * The walker scaffold auto-detects the container by 2-byte file magic so
 * both shapes resolve to a manifest envelope through the same callback.
 */

#include <stddef.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "loader_pool.h"
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

    /* Catalog row: only register if absent (preserves existing
     * registrations from in-binary baseline). */
    asset_entry_t *e = (asset_entry_t *)assetCatalogResolve(id);
    if (e == NULL) {
        e = assetCatalogRegisterAnimation(
            id, /* anim_id: */ 0,
            /* name: */ "",
            (s32)frame_count,
            target_body[0] ? target_body : NULL);
        if (!e) return -1;

        /* Engine Phase 4: walker callbacks may run from boot-pool
         * workers; re-resolve under-lock via the helper instead of
         * dereferencing `e` (could dangle if a concurrent register
         * triggered a realloc). */
        if (category[0]) {
            assetCatalogSetCategoryById(id, category);
        }
    }

    /* Pool payload (Step 5): only weapon_animation envelopes carry the
     * opcode array that loaderPoolParseAnimationJson expects. Character
     * animations are byte-stream-only and have no pool slot. */
    if (strcmp(category, "weapon_animation") == 0) {
        loaderPoolParseAnimationJson(manifest, manifest_len);
    }

    return 1;
}

void loaderWalkerScanAnimations(const char *tier_dir,
                                 loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "animation", "animations", ".pdanim", /* always_invoke: */ 1,
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}
