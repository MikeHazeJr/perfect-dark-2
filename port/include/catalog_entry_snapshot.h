#ifndef PD_CATALOG_ENTRY_SNAPSHOT_H
#define PD_CATALOG_ENTRY_SNAPSHOT_H

#include <string.h>
#include "assetcatalog.h"

/* Address-independent transaction storage. A self marker is represented by
 * an explicit bit plus NULL; neither the original pool nor this snapshot's
 * allocation address is retained. Ordinary owned payload pointers stay exact. */
typedef struct catalog_entry_snapshot {
    asset_entry_t row;
    s32 runtime_self_marker;
} catalog_entry_snapshot_t;

static inline void catalogEntrySnapshotCapture(catalog_entry_snapshot_t *snapshot,
        const asset_entry_t *entry)
{
    snapshot->runtime_self_marker = entry->payload_kind == ASSET_PAYLOAD_RUNTIME_ACTIVE
        && entry->loaded_data == entry;
    memcpy(&snapshot->row, entry, sizeof(snapshot->row));
    if (snapshot->runtime_self_marker) snapshot->row.loaded_data = NULL;
}

static inline s32 catalogEntrySnapshotMatches(const catalog_entry_snapshot_t *snapshot,
        const asset_entry_t *entry)
{
    catalog_entry_snapshot_t current;
    catalogEntrySnapshotCapture(&current, entry);
    return current.runtime_self_marker == snapshot->runtime_self_marker
        && !memcmp(&current.row, &snapshot->row, sizeof(current.row));
}

/* Use only for a transaction path that preserves payload ownership (the
 * metadata-only nested mesh binder). Refuse changed lifecycle ownership so a
 * retired payload cannot be resurrected from the old snapshot. Ordinary
 * replacement rollback must continue using RestoreRetiredSnapshot instead. */
static inline s32 catalogEntrySnapshotRestorePreserved(asset_entry_t *entry,
        const catalog_entry_snapshot_t *snapshot)
{
    catalog_entry_snapshot_t current;
    const asset_entry_t *prior = &snapshot->row;
    catalogEntrySnapshotCapture(&current, entry);
    if (strcmp(entry->id, prior->id) || entry->type != prior->type
            || current.runtime_self_marker != snapshot->runtime_self_marker
            || current.row.loaded_data != prior->loaded_data
            || entry->payload_kind != prior->payload_kind
            || entry->load_state != prior->load_state
            || entry->ref_count != prior->ref_count
            || entry->stage_ref_count != prior->stage_ref_count
            || entry->data_size_bytes != prior->data_size_bytes) return 0;
    memcpy(entry, prior, sizeof(*entry));
    if (snapshot->runtime_self_marker) entry->loaded_data = entry;
    return 1;
}

#endif
