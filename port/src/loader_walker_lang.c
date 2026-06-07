/**
 * loader_walker_lang.c -- Step 4 (2026-05-03).
 *
 * Walks data/<romid>/lang/*.pdlang and registers each as ASSET_LANG.
 * The .pdlang manifest carries `locale` and `category` (stage / mp_ui /
 * system) at top level per Section 2.13 of the schema doc.
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

    asset_entry_t *e = assetCatalogRegister(id, ASSET_LANG);
    if (!e) return -1;
    loaderWalkerMarkBaseArchiveEntry(e);

    s64 source_bank = -1;
    char source_member[128];
    char source_path[FS_MAXPATH + 1];
    loaderWalkerEnvelopeInt(manifest, manifest_len, "source_bank", &source_bank);
    e->ext.lang.bank_id = (s32)source_bank;
    if (!loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "data",
                                     source_member, sizeof(source_member))) {
        strncpy(source_member, "strings.json", sizeof(source_member) - 1);
        source_member[sizeof(source_member) - 1] = '\0';
    }
    if (loaderWalkerArchiveMemberPath(file_path, source_member,
                                      source_path, sizeof(source_path))) {
        strncpy(e->ext.lang.strings_file, source_path,
                sizeof(e->ext.lang.strings_file) - 1);
        e->ext.lang.strings_file[sizeof(e->ext.lang.strings_file) - 1] = '\0';
        catalogSetPrimaryFile(e, source_path);
    }

    /* Engine Phase 4: lock-safe category fill via helper. */
    char category[32];
    if (loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "category",
                                     category, sizeof(category))) {
        assetCatalogSetCategoryById(id, category);
    }
    return 1;
}

void loaderWalkerScanLangs(const char *tier_dir,
                            loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "lang", "lang", ".pdlang",
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}
