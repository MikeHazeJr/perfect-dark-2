/**
 * modvfs.c -- Priority M / B-238 in-memory VFS for `.pdmod` archives.
 *
 * Layered model:
 *
 *   modmgr.c              (mod registry + enable state)
 *      |
 *      v
 *   modvfs.c   <--- mounts archives, owns the LRU cache
 *      |
 *      v
 *   modarchive.c          (zip central directory + zlib inflate)
 *      |
 *      v
 *   <archive file>        (.pdmod / .zip on disk)
 *
 * The catalog stays the single source of truth for asset registration;
 * this file just answers "do you have these bytes?" lookups when fsFileLoad
 * comes calling.
 *
 * Cache layout:
 *
 *   - Per-mount entry table (linear, capped at MODVFS_MAX_PER_MOUNT). Holds
 *     decompressed buffers for entries that have been read at least once.
 *
 *   - Global doubly-linked LRU list. Head = most recently used. New entries
 *     prepend to the head. Tail entries are evicted when bytes_resident
 *     would exceed the cap.
 *
 *   - Lookups inside a mount are linear-scan by relPath. With the typical
 *     mod containing tens of distinct asset reads per session this is fine;
 *     a hash table can be slotted in later if profiling demands it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "system.h"
#include "fs.h"
#include "modarchive.h"
#include "modvfs.h"

#define MODVFS_MAX_MOUNTS        32     /* matches MODMGR_MAX_MODS */
#define MODVFS_MAX_PER_MOUNT     1024   /* per-mount cache slot cap */
#define MODVFS_DEFAULT_CAP_MB    256
#define MODVFS_MOD_ID_LEN        64     /* matches MODMGR_ID_LEN */

typedef struct vfs_cache_entry {
	struct vfs_mount      *mount;       /* parent mount (back-pointer) */
	char                  *relPath;     /* malloc'd, NUL terminated */
	u8                    *bytes;       /* malloc'd, size+1 (trailing NUL) */
	u32                    size;
	struct vfs_cache_entry *prevLRU;
	struct vfs_cache_entry *nextLRU;
} vfs_cache_entry_t;

typedef struct vfs_mount {
	char            mod_id[MODVFS_MOD_ID_LEN];
	mod_archive_t  *archive;            /* not owned -- modmgr keeps this open */
	vfs_cache_entry_t *entries[MODVFS_MAX_PER_MOUNT];
	s32             entry_count;
} vfs_mount_t;

/* ---------------------------------------------------------- File-scope state */

static vfs_mount_t       s_Mounts[MODVFS_MAX_MOUNTS];
static s32               s_MountCount = 0;

static vfs_cache_entry_t *s_LruHead = NULL;
static vfs_cache_entry_t *s_LruTail = NULL;

static u64               s_CapBytes = (u64)MODVFS_DEFAULT_CAP_MB * 1024ull * 1024ull;
static u64               s_BytesResident = 0;
static u64               s_BytesEvicted  = 0;
static u64               s_Hits          = 0;
static u64               s_Misses        = 0;
static s32               s_EntriesResident = 0;

/* ----------------------------------------------------------- LRU bookkeeping */

static void lruRemove(vfs_cache_entry_t *e)
{
	if (e->prevLRU) e->prevLRU->nextLRU = e->nextLRU; else s_LruHead = e->nextLRU;
	if (e->nextLRU) e->nextLRU->prevLRU = e->prevLRU; else s_LruTail = e->prevLRU;
	e->prevLRU = e->nextLRU = NULL;
}

static void lruPushFront(vfs_cache_entry_t *e)
{
	e->prevLRU = NULL;
	e->nextLRU = s_LruHead;
	if (s_LruHead) s_LruHead->prevLRU = e;
	s_LruHead = e;
	if (!s_LruTail) s_LruTail = e;
}

static void lruTouch(vfs_cache_entry_t *e)
{
	if (s_LruHead == e) return;
	lruRemove(e);
	lruPushFront(e);
}

/* Free an entry: remove from mount table, remove from LRU, release buffers. */
static void freeEntry(vfs_cache_entry_t *e)
{
	if (!e) return;
	vfs_mount_t *m = e->mount;
	if (m) {
		for (s32 i = 0; i < m->entry_count; i++) {
			if (m->entries[i] == e) {
				m->entries[i] = m->entries[m->entry_count - 1];
				m->entries[m->entry_count - 1] = NULL;
				m->entry_count--;
				break;
			}
		}
	}
	lruRemove(e);
	s_BytesResident -= e->size;
	s_BytesEvicted  += e->size;
	s_EntriesResident--;
	free(e->relPath);
	free(e->bytes);
	free(e);
}

/* Evict from tail until adding `incoming` bytes would fit under the cap.
 * Refuses to evict the entry it was about to be replaced by (caller's
 * concern -- this function only inspects the LRU list). */
static void evictUntilFits(u64 incoming)
{
	while (s_LruTail && s_BytesResident + incoming > s_CapBytes) {
		vfs_cache_entry_t *victim = s_LruTail;
		freeEntry(victim);
	}
}

/* ----------------------------------------------------------- Mount lookup */

static vfs_mount_t *findMount(const char *mod_id)
{
	if (!mod_id) return NULL;
	for (s32 i = 0; i < s_MountCount; i++) {
		if (s_Mounts[i].archive && strcmp(s_Mounts[i].mod_id, mod_id) == 0) {
			return &s_Mounts[i];
		}
	}
	return NULL;
}

static vfs_cache_entry_t *findCacheEntry(vfs_mount_t *m, const char *relPath)
{
	for (s32 i = 0; i < m->entry_count; i++) {
		if (strcmp(m->entries[i]->relPath, relPath) == 0) {
			return m->entries[i];
		}
	}
	return NULL;
}

/* ----------------------------------------------------------- Public API */

void modVfsInit(void)
{
	/* Idempotent. The cache cap is left as previously set (or default).
	 * modmgr's config layer hands the value through modVfsSetCacheCapMB
	 * once Mods.AssetCacheMB has been read from pd.ini. */
	(void)0;
}

void modVfsShutdown(void)
{
	modVfsUnmountAll();
}

s32 modVfsMount(const char *mod_id, mod_archive_t *archive)
{
	if (!mod_id || !mod_id[0] || !archive) return 0;
	if (findMount(mod_id)) {
		sysLogPrintf(LOG_WARNING, "modvfs: mount '%s' already exists -- ignoring", mod_id);
		return 0;
	}
	if (s_MountCount >= MODVFS_MAX_MOUNTS) {
		sysLogPrintf(LOG_WARNING, "modvfs: mount table full -- '%s' rejected", mod_id);
		return 0;
	}

	vfs_mount_t *m = &s_Mounts[s_MountCount++];
	memset(m, 0, sizeof(*m));
	strncpy(m->mod_id, mod_id, sizeof(m->mod_id) - 1);
	m->mod_id[sizeof(m->mod_id) - 1] = '\0';
	m->archive = archive;
	sysLogPrintf(LOG_NOTE,
		"modvfs: mounted '%s' -- %d archive entries indexed",
		mod_id, modArchiveGetEntryCount(archive));
	return 1;
}

void modVfsUnmount(const char *mod_id)
{
	vfs_mount_t *m = findMount(mod_id);
	if (!m) return;

	/* Free every cached entry owned by this mount. freeEntry compacts the
	 * mount->entries[] table in place so we re-read m->entry_count each
	 * iteration. */
	while (m->entry_count > 0) {
		freeEntry(m->entries[0]);
	}

	/* Compact the mount table by swapping the last entry into this slot. */
	s32 idx = (s32)(m - s_Mounts);
	if (idx < s_MountCount - 1) {
		s_Mounts[idx] = s_Mounts[s_MountCount - 1];
		/* Repoint cached entries' back-pointers if any survived (they did
		 * not -- we just freed them all -- but defensive nonetheless). */
		for (s32 i = 0; i < s_Mounts[idx].entry_count; i++) {
			s_Mounts[idx].entries[i]->mount = &s_Mounts[idx];
		}
	}
	memset(&s_Mounts[s_MountCount - 1], 0, sizeof(s_Mounts[0]));
	s_MountCount--;
	sysLogPrintf(LOG_NOTE, "modvfs: unmounted '%s' (%d mounts remain)", mod_id, s_MountCount);
}

void modVfsUnmountAll(void)
{
	while (s_MountCount > 0) {
		modVfsUnmount(s_Mounts[0].mod_id);
	}
}

s32 modVfsCanResolve(const char *relPath)
{
	if (!relPath) return 0;
	for (s32 i = 0; i < s_MountCount; i++) {
		if (modArchiveFindEntry(s_Mounts[i].archive, relPath) >= 0) return 1;
	}
	return 0;
}

s32 modVfsGetSize(const char *relPath)
{
	if (!relPath) return -1;
	for (s32 i = 0; i < s_MountCount; i++) {
		s32 idx = modArchiveFindEntry(s_Mounts[i].archive, relPath);
		if (idx >= 0) return (s32)modArchiveGetEntrySize(s_Mounts[i].archive, idx);
	}
	return -1;
}

/* Cache-store + return a fresh copy. The caller-facing buffer is independent
 * of the cached buffer so the caller may free it without invalidating the
 * cache. The cache copy is what survives across calls. */
static void *resolveFromMount(vfs_mount_t *m, const char *relPath, u32 *outSize)
{
	vfs_cache_entry_t *e = findCacheEntry(m, relPath);
	if (e) {
		s_Hits++;
		lruTouch(e);
		void *copy = malloc(e->size + 1);
		if (!copy) return NULL;
		memcpy(copy, e->bytes, e->size);
		((u8 *)copy)[e->size] = 0;
		if (outSize) *outSize = e->size;
		return copy;
	}

	/* Cache miss: pull from the archive. */
	s32 idx = modArchiveFindEntry(m->archive, relPath);
	if (idx < 0) return NULL;

	u32 sz = 0;
	void *raw = modArchiveExtractAlloc(m->archive, idx, &sz);
	if (!raw) return NULL;
	s_Misses++;

	/* Cache store -- make a separate cached copy so that the caller-owned
	 * buffer can be freed without affecting the cache. */
	if (m->entry_count < MODVFS_MAX_PER_MOUNT) {
		evictUntilFits((u64)sz);
		vfs_cache_entry_t *ce = (vfs_cache_entry_t *)calloc(1, sizeof(*ce));
		if (ce) {
			ce->bytes = (u8 *)malloc((size_t)sz + 1);
			ce->relPath = strdup(relPath);
			if (ce->bytes && ce->relPath) {
				memcpy(ce->bytes, raw, sz);
				ce->bytes[sz] = 0;
				ce->size = sz;
				ce->mount = m;
				m->entries[m->entry_count++] = ce;
				lruPushFront(ce);
				s_BytesResident += sz;
				s_EntriesResident++;
			} else {
				free(ce->bytes);
				free(ce->relPath);
				free(ce);
			}
		}
	}

	/* The buffer modArchiveExtractAlloc handed us is already malloc'd with
	 * size bytes; we need it to be size+1 (trailing NUL) for parity with the
	 * cache-hit return shape. Realloc to add the NUL byte. */
	void *out = realloc(raw, (size_t)sz + 1);
	if (!out) {
		free(raw);
		return NULL;
	}
	((u8 *)out)[sz] = 0;
	if (outSize) *outSize = sz;
	return out;
}

void *modVfsResolveAllocFromMount(const char *mod_id, const char *relPath, u32 *outSize)
{
	vfs_mount_t *m = findMount(mod_id);
	if (!m) return NULL;
	return resolveFromMount(m, relPath, outSize);
}

void *modVfsResolveAnyAlloc(const char *relPath, u32 *outSize,
                            char *outModId, s32 outModIdCap)
{
	if (!relPath) return NULL;
	for (s32 i = 0; i < s_MountCount; i++) {
		void *r = resolveFromMount(&s_Mounts[i], relPath, outSize);
		if (r) {
			if (outModId && outModIdCap > 0) {
				strncpy(outModId, s_Mounts[i].mod_id, outModIdCap - 1);
				outModId[outModIdCap - 1] = '\0';
			}
			return r;
		}
	}
	return NULL;
}

void modVfsGetStats(mod_vfs_stats_t *out)
{
	if (!out) return;
	out->hits = s_Hits;
	out->misses = s_Misses;
	out->bytes_resident = s_BytesResident;
	out->bytes_evicted_total = s_BytesEvicted;
	out->cap_bytes = s_CapBytes;
	out->entries_resident = s_EntriesResident;
	out->mounts_active = s_MountCount;
}

void modVfsSetCacheCapMB(s32 mb)
{
	if (mb < 0) mb = 0;
	s_CapBytes = (u64)mb * 1024ull * 1024ull;
	/* If the new cap is smaller than what is currently resident, evict
	 * down immediately so subsequent accesses see a consistent state. */
	evictUntilFits(0);
}
