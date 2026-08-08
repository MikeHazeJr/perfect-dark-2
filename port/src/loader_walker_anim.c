/**
 * loader_walker_anim.c -- Catalog universality pivot Step 4 + 5
 * (2026-05-03).
 *
 * Walks animation .pdanim files. The .pdanim kind is a ZIP compound
 * per Section 2.6:
 *   - weapon_animation: _meta/manifest.json carries command source for
 *     loaderPoolParseAnimationJson; commands.json is the editable source.
 *   - character_animation: animation.gltf is the editable source; the
 *     catalog row carries enough envelope info for consumers; no pool payload.
 *
 * The walker scaffold auto-detects the container by 2-byte file magic so
 * both shapes resolve to a manifest envelope through the same callback.
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "fs.h"
#include "loader_pool.h"
#include "loader_walker.h"
#include "assetcatalog_anim_slots.h" /* c3849 Wave 2 */
#include "loader_walker_common.h"
#include "system.h"

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    (void)pd_kind;

    s64 frame_count = 0;
    s64 source_index = -1;
    s64 bytes_per_frame = 0;
    s64 header_len = 0;
    s64 framelen = 0;
    s64 flags = 0;
    loaderWalkerEnvelopeInt(manifest, manifest_len, "frame_count", &frame_count);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "source_index", &source_index);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "bytes_per_frame", &bytes_per_frame);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "header_len", &header_len);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "framelen", &framelen);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "flags", &flags);

    char category[32];
    char target_body[64];
    char source_member[FS_MAXPATH];
    char source_path[FS_MAXPATH + 1] = {0};
    loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "category",
                                 category, sizeof(category));
    loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "target_body",
                                 target_body, sizeof(target_body));
    if (!loaderWalkerEnvelopePathCopy(manifest, manifest_len, "animation",
                                     source_member, sizeof(source_member))) {
        if (strcmp(category, "weapon_animation") == 0) {
            if (!loaderWalkerEnvelopePathCopy(manifest, manifest_len, "command_source",
                                             source_member, sizeof(source_member))
                    && !loaderWalkerEnvelopePathCopy(manifest, manifest_len, "commands_file",
                                                    source_member, sizeof(source_member))) {
                strncpy(source_member, "commands.json", sizeof(source_member) - 1);
                source_member[sizeof(source_member) - 1] = '\0';
            }
        } else if (!loaderWalkerEnvelopePathCopy(manifest, manifest_len, "runtime_source",
                                                source_member, sizeof(source_member))) {
            strncpy(source_member, "animation.gltf", sizeof(source_member) - 1);
            source_member[sizeof(source_member) - 1] = '\0';
        }
    }

    /* Catalog row: only register if absent (preserves existing
     * registrations from in-binary baseline). */
    asset_entry_t *e = (asset_entry_t *)assetCatalogResolve(id);
    if (e == NULL) {
        e = assetCatalogRegisterAnimation(
            id, /* anim_id: */ -1,
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
    e = assetCatalogGetMutable(id);
    if (e) {
        loaderWalkerMarkBaseArchiveEntry(e);
        if (source_index >= 0) {
            e->ext.anim.anim_id = (s32)source_index;
            e->source_animnum = (s32)source_index;
            e->runtime_index = (s32)source_index;
        } else {
            /* c3849 Wave 2: net-new custom anim gets a catalog-owned private
             * slot (exhaustion already logged loud; row stays -1). */
            s32 slot = assetCatalogResolveAnimPrivateSlot(id);
            if (slot >= 0) {
                e->ext.anim.anim_id = slot;
                e->source_animnum = slot;
                e->runtime_index = slot;
            }
        }
        e->ext.anim.frame_count = (s32)frame_count;
        e->ext.anim.bytes_per_frame = (s32)bytes_per_frame;
        e->ext.anim.header_len = (s32)header_len;
        e->ext.anim.framelen = (s32)framelen;
        e->ext.anim.flags = (s32)flags;
        if (loaderWalkerArchiveMemberPath(file_path, source_member,
                                          source_path, sizeof(source_path))) {
            catalogSetPrimaryFile(e, source_path);
        }
    }

    /* Pool payload (Step 5): only weapon_animation envelopes carry the
     * command array that loaderPoolParseAnimationJson expects. Character
     * animations are byte-stream-only and have no pool slot. */
    if (strcmp(category, "weapon_animation") == 0) {
        u32 command_size = 0;
        char *command_json = (char *)fsFileLoad(source_path, &command_size);
        if (!command_json || command_size == 0) {
            if (command_json) free(command_json);
            sysLogPrintf(LOG_WARNING,
                "LOADER.POOL.ANIMATION.SOURCE_FAIL: id=%s source=%s",
                id ? id : "", source_path);
            return -1;
        }
        loaderPoolParseAnimationSourceJson(command_json, command_size,
            source_path);
        sysLogPrintf(LOG_NOTE,
            "LOADER.POOL.ANIMATION.SOURCE: id=%s source=%s bytes=%u",
            id ? id : "", source_path, (unsigned)command_size);
        free(command_json);
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
