/**
 * loader_walker_ui.c -- Step 4 (2026-05-03).
 *
 * Walks data/<romid>/ui/*.pdui and registers each as ASSET_UI. .pdui
 * compounds carry per-texture nineslice metadata + a baked .tga payload
 * inside the ZIP; the universal walker only registers the catalog row
 * here. Texture decode + upload remains owned by pdgui_theme.cpp's
 * pdguiThemeLateInit reader.
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

    asset_entry_t *e = assetCatalogRegister(id, ASSET_UI);
    return e ? 1 : -1;
}

void loaderWalkerScanUi(const char *tier_dir,
                         loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "ui", "ui", ".pdui",
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}
