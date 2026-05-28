/**
 * loader_walker_font.c -- Step 4 (2026-05-03).
 *
 * Walks data/<romid>/fonts/*.pdfont and registers each as ASSET_FONT.
 * The font face name is captured in the catalog row's `category` so
 * consumers can filter without re-parsing.
 */

#include <stddef.h>
#include <stdio.h>   /* snprintf */
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

    asset_entry_t *e = assetCatalogRegister(id, ASSET_FONT);
    if (!e) return -1;
    loaderWalkerMarkBaseArchiveEntry(e);

    char source_member[128];
    char source_path[FS_MAXPATH + 1];
    if (!loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "font",
                                     source_member, sizeof(source_member))
            && !loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "glyphs",
                                            source_member, sizeof(source_member))) {
        snprintf(source_member, sizeof(source_member), "font.otf");
    }
    if (loaderWalkerArchiveMemberPath(file_path, source_member,
                                      source_path, sizeof(source_path))) {
        catalogSetPrimaryFile(e, source_path);
    }

    /* Engine Phase 4: walker may run from boot-pool workers; build the
     * "font:<face>" string locally and route the field-fill through the
     * lock-safe helper. */
    char face[64];
    if (loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "face",
                                     face, sizeof(face))) {
        char category[CATALOG_CATEGORY_LEN];
        snprintf(category, sizeof(category), "font:%s", face);
        assetCatalogSetCategoryById(id, category);
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
