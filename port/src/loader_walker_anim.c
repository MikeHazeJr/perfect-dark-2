/**
 * loader_walker_anim.c -- Catalog universality pivot Step 4 + 5
 * (2026-05-03).
 *
 * Walks animation .pdanim files. The .pdanim kind is a ZIP compound
 * per Section 2.6:
 *   - weapon_animation: descriptor selects editable commands.json; all catalog
 *     rows are registered before the command reader binds exact references.
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
    s32 uses_character_clip =
        assetCatalogAnimationCategoryUsesCharacterClip(category);
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
    }
    e = assetCatalogGetMutable(id);
    if (e) {
        loaderWalkerMarkBaseArchiveEntry(e);
        /* Bundled provenance must not replace the animation's semantic kind. */
        if (category[0]) assetCatalogSetCategoryById(id, category);
        if (!uses_character_clip) {
            /* A weapon_animation source is a loader-pool command graph. Its
             * source_index, when present in the editable descriptor, indexes
             * the authored command table and is not a character animnum. */
            e->ext.anim.anim_id = -1;
            e->source_animnum = -1;
            e->runtime_index = -1;
        } else if (source_index >= 0) {
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
        if (!loaderWalkerArchiveMemberPath(file_path, source_member,
                                          source_path, sizeof(source_path))) return -1;
        catalogSetPrimaryFile(e, source_path);
    } else return -1;

    return 1;
}

typedef struct animation_command_selection {
    const char *id;
    char category[CATALOG_CATEGORY_LEN];
    char path[FS_MAXPATH + 1];
    s32 found;
} animation_command_selection_t;

static void s_copyCommandSelection(const asset_entry_t *entry, void *opaque)
{
    animation_command_selection_t *selection = opaque;
    if (strcmp(entry->id, selection->id)) return;
    const char *path = fileProviderPath(entry->source.primary);
    if (!path || strlen(path) >= sizeof(selection->path)) return;
    strcpy(selection->category, entry->category);
    strcpy(selection->path, path);
    selection->found = 1;
}

static s32 s_compile(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id, const char *file_path)
{
    (void)pd_kind; (void)file_path;
    char expected_category[CATALOG_CATEGORY_LEN] = {0};
    loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "category",
        expected_category, sizeof(expected_category));
    animation_command_selection_t selected = {0};
    selected.id = id;
    assetCatalogIterateByType(ASSET_ANIMATION, s_copyCommandSelection, &selected);
    if (!selected.found || strcmp(selected.category, expected_category)) {
        sysLogPrintf(LOG_WARNING, "LOADER.POOL.ANIMATION.SOURCE_FAIL: id=%s selected category differs from source", id);
        return -1;
    }
    if (strcmp(selected.category, "weapon_animation") == 0) {
        u32 command_size = 0;
        char *command_json = (char *)fsFileLoad(selected.path, &command_size);
        if (!command_json || command_size == 0) {
            if (command_json) free(command_json);
            sysLogPrintf(LOG_WARNING,
                "LOADER.POOL.ANIMATION.SOURCE_FAIL: id=%s source=%s",
                id ? id : "", selected.path);
            return -1;
        }
        if (!loaderPoolParseAnimationCatalogSourceJson(command_json, command_size,
                selected.path, id)) {
            free(command_json);
            sysLogPrintf(LOG_WARNING, "LOADER.POOL.ANIMATION.SOURCE_FAIL: id=%s source=%s", id ? id : "", selected.path);
            return -1;
        }
        sysLogPrintf(LOG_NOTE,
            "LOADER.POOL.ANIMATION.SOURCE: id=%s source=%s bytes=%u",
            id ? id : "", selected.path, (unsigned)command_size);
        free(command_json);
        return 1;
    }

    return 0; /* Character clips have no command-pool payload. */
}


void loaderWalkerScanAnimations(const char *tier_dir,
                                 loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "animation", "animations", ".pdanim", /* always_invoke: */ 1,
    };
    loader_walker_kind_result_t registered = {0}, compiled = {0};
    loaderWalkerScanKind(tier_dir, &desc, s_register, &registered);
    if (!registered.envelope_failures && !registered.register_failures) {
        /* The first blocking pass finishes every catalog row before the second
         * binds includes, regardless of archive enumeration or worker order. */
        static const loader_walker_kind_desc_t commands = {
            "animation", "animations", ".pdanim", 1,
        };
        loaderWalkerScanKind(tier_dir, &commands, s_compile, &compiled);
        registered.envelope_failures += compiled.envelope_failures;
        registered.register_failures += compiled.register_failures;
        registered.entries_registered -= compiled.envelope_failures + compiled.register_failures;
    }
    if (out) *out = registered;
}
