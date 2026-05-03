/**
 * loader_walker_font.c -- Step 4 (2026-05-03).
 *
 * Walks data/<romid>/fonts/*.pdfont and registers each as ASSET_UI.
 * No dedicated font asset_type_e exists; the closest semantic bucket is
 * UI (font is a UI element). The font face name is captured in the
 * catalog row's `category` so consumers can filter without re-parsing.
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

    asset_entry_t *e = assetCatalogRegister(id, ASSET_UI);
    if (!e) return -1;

    char face[64];
    if (loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "face",
                                     face, sizeof(face))) {
        const char *prefix = "font:";
        size_t plen = strlen(prefix);
        size_t flen = strlen(face);
        if (plen + flen >= sizeof(e->category)) flen = sizeof(e->category) - plen - 1;
        memcpy(e->category, prefix, plen);
        memcpy(e->category + plen, face, flen);
        e->category[plen + flen] = '\0';
    }
    return 1;
}

void loaderWalkerScanFonts(const char *tier_dir,
                            loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "font", "fonts", ".pdfont",
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}
