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

    char source_member[FS_MAXPATH];
    char layout_member[FS_MAXPATH];
    char nineslice_member[FS_MAXPATH];
    char source_path[FS_MAXPATH + 1];
    char text_value[64];
    s64 int_value = 0;
    asset_entry_t *e = assetCatalogGetMutable(id);
    if (e && e->type != ASSET_UI) {
        return -1;
    }
    if (!e) {
        e = assetCatalogRegister(id, ASSET_UI);
    }
    loaderWalkerMarkBaseArchiveEntry(e);
    memset(&e->ext.ui, 0, sizeof(e->ext.ui));
    if (!loaderWalkerEnvelopePathCopy(manifest, manifest_len, "file",
                                     source_member, sizeof(source_member))) {
        snprintf(source_member, sizeof(source_member), "texture.tga");
    }
    if (e && loaderWalkerArchiveMemberPath(file_path, source_member,
                                           source_path, sizeof(source_path))) {
        catalogSetPrimaryFile(e, source_path);
        strncpy(e->ext.ui.texture_file, source_path,
                sizeof(e->ext.ui.texture_file) - 1);
        e->ext.ui.texture_file[sizeof(e->ext.ui.texture_file) - 1] = '\0';
    }
    if (loaderWalkerEnvelopePathCopy(manifest, manifest_len, "layout_file",
                                    layout_member, sizeof(layout_member))
            && loaderWalkerArchiveMemberPath(file_path, layout_member,
                                             source_path, sizeof(source_path))) {
        strncpy(e->ext.ui.layout_file, source_path,
                sizeof(e->ext.ui.layout_file) - 1);
        e->ext.ui.layout_file[sizeof(e->ext.ui.layout_file) - 1] = '\0';
    }
    if (loaderWalkerEnvelopePathCopy(manifest, manifest_len, "nineslice_file",
                                    nineslice_member, sizeof(nineslice_member))
            && loaderWalkerArchiveMemberPath(file_path, nineslice_member,
                                             source_path, sizeof(source_path))) {
        strncpy(e->ext.ui.nineslice_file, source_path,
                sizeof(e->ext.ui.nineslice_file) - 1);
        e->ext.ui.nineslice_file[sizeof(e->ext.ui.nineslice_file) - 1] = '\0';
    }
    if (loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "name",
                                    text_value, sizeof(text_value))) {
        strncpy(e->ext.ui.texture_name, text_value,
                sizeof(e->ext.ui.texture_name) - 1);
        e->ext.ui.texture_name[sizeof(e->ext.ui.texture_name) - 1] = '\0';
    }
    if (loaderWalkerEnvelopeInt(manifest, manifest_len, "width", &int_value)) {
        e->ext.ui.width = (s32)int_value;
    }
    if (loaderWalkerEnvelopeInt(manifest, manifest_len, "height", &int_value)) {
        e->ext.ui.height = (s32)int_value;
    }
    if (loaderWalkerEnvelopeInt(manifest, manifest_len, "data_size", &int_value)) {
        e->ext.ui.data_size = (s32)int_value;
    }
    if (loaderWalkerEnvelopeInt(manifest, manifest_len, "left", &int_value)) {
        e->ext.ui.nineslice_left = (s32)int_value;
    }
    if (loaderWalkerEnvelopeInt(manifest, manifest_len, "right", &int_value)) {
        e->ext.ui.nineslice_right = (s32)int_value;
    }
    if (loaderWalkerEnvelopeInt(manifest, manifest_len, "top", &int_value)) {
        e->ext.ui.nineslice_top = (s32)int_value;
    }
    if (loaderWalkerEnvelopeInt(manifest, manifest_len, "bottom", &int_value)) {
        e->ext.ui.nineslice_bottom = (s32)int_value;
    }
    if (loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "edgeMode",
                                    text_value, sizeof(text_value))) {
        strncpy(e->ext.ui.nineslice_edge_mode, text_value,
                sizeof(e->ext.ui.nineslice_edge_mode) - 1);
        e->ext.ui.nineslice_edge_mode[
            sizeof(e->ext.ui.nineslice_edge_mode) - 1] = '\0';
    }
    if (loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "centerMode",
                                    text_value, sizeof(text_value))) {
        strncpy(e->ext.ui.nineslice_center_mode, text_value,
                sizeof(e->ext.ui.nineslice_center_mode) - 1);
        e->ext.ui.nineslice_center_mode[
            sizeof(e->ext.ui.nineslice_center_mode) - 1] = '\0';
    }
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
