/**
 * assetcatalog.c -- PC port asset catalog implementation
 *
 * String-keyed hash table for asset resolution. Implements:
 * - Open addressing with linear probing for read-heavy lookup
 * - FNV-1a hash for table slot distribution
 * - CRC32 for network identity
 * - Dynamic growth of both hash table and entry pool
 * - Type-specific registration and resolution
 *
 * Architecture:
 *   s_HashTable[slot] -> entry pool index (or SENTINEL for empty)
 *   s_EntryPool[poolIdx] -> asset_entry_t with all metadata
 *
 * This allows realloc() of the entry pool without invalidating hash table.
 * Hash collisions use linear probing — O(1) average, O(n) worst case.
 * At 256-512 entries with 2048 slots, load factor ~12-25%, probing is fast.
 *
 * ADR-003 reference: context/ADR-003-asset-catalog-core.md
 */

#include <PR/ultratypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "types.h"
#include "assetcatalog.h"

/* ========================================================================
 * Configuration Constants
 * ======================================================================== */

#define HASH_TABLE_INITIAL  2048  /* power of 2 */
#define ENTRY_POOL_INITIAL  512
#define SENTINEL            (-1)   /* hash table slot marker for empty */
#define LOAD_FACTOR_LIMIT   70     /* rehash when occupied > (size * 70 / 100) */

/* ========================================================================
 * Static State
 * ======================================================================== */

static s32 *s_HashTable = NULL;        /* hash table: [slot] -> pool index or SENTINEL */
static s32 s_HashTableSize = 0;        /* current size (always power of 2) */
static s32 s_HashTableCapacity = 0;    /* allocated capacity */

static asset_entry_t *s_EntryPool = NULL;      /* array of entries */
static s32 s_EntryPoolSize = 0;                /* current count */
static s32 s_EntryPoolCapacity = 0;            /* allocated capacity */

/* ========================================================================
 * Hash Functions
 * ======================================================================== */

/**
 * FNV-1a hash function for hash table slot selection.
 * Fast, good avalanche properties, minimal collisions.
 * Returns full 32-bit hash; caller masks to table size.
 */
static u32 fnv1a(const char *str)
{
    u32 hash = 0x811c9dc5u;  /* FNV offset basis */
    while (*str) {
        hash ^= (u8)*str++;
        hash *= 0x01000193u;  /* FNV prime */
    }
    return hash;
}

/**
 * CRC32 hash function for network identity.
 * Matches existing modmgrHashString() protocol expectations.
 * Table-based implementation for speed.
 *
 * Standard CRC32 polynomial: 0x04C11DB7
 * Reflected bit order (LSB-first) with pre-computed lookup table.
 */
static const u32 crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

static u32 crc32(const char *str)
{
    u32 crc = 0xFFFFFFFFu;
    while (*str) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ (u8)*str++) & 0xFF];
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ========================================================================
 * Internal Utilities
 * ======================================================================== */

/**
 * Find a slot in the hash table via linear probing.
 * Returns the slot index (may be empty or occupied).
 * Sets *out_poolidx to the pool index at that slot (or SENTINEL if empty).
 */
static s32 findSlot(u32 id_hash, const char *id, s32 *out_poolidx)
{
    s32 slot = id_hash & (s_HashTableSize - 1);
    s32 tries = 0;
    s32 max_probes = s_HashTableSize;  /* safety: don't loop forever */

    while (tries < max_probes) {
        s32 pool_idx = s_HashTable[slot];
        *out_poolidx = pool_idx;

        if (pool_idx == SENTINEL) {
            /* Empty slot found */
            return slot;
        }

        /* Check if this is the right entry (verify full string match) */
        if (pool_idx >= 0 && pool_idx < s_EntryPoolSize) {
            if (strcmp(s_EntryPool[pool_idx].id, id) == 0) {
                /* Found exact match */
                return slot;
            }
        }

        /* Collision or wrong entry — linear probe */
        slot = (slot + 1) & (s_HashTableSize - 1);
        tries++;
    }

    /* Hash table is completely full (shouldn't happen with growth strategy) */
    *out_poolidx = SENTINEL;
    return -1;
}

/**
 * Rehash the entire entry pool into a new hash table.
 * Called when load factor exceeds LOAD_FACTOR_LIMIT.
 */
static s32 rehashTable(void)
{
    s32 new_size = s_HashTableSize * 2;
    s32 *new_table = (s32 *)malloc(new_size * sizeof(s32));

    if (new_table == NULL) {
        return 0;  /* allocation failed */
    }

    /* Initialize new table with SENTINEL */
    for (s32 i = 0; i < new_size; i++) {
        new_table[i] = SENTINEL;
    }

    /* Re-insert all entries from the pool */
    for (s32 i = 0; i < s_EntryPoolSize; i++) {
        if (!s_EntryPool[i].occupied) {
            continue;  /* skip empty slots */
        }

        const char *id = s_EntryPool[i].id;
        u32 id_hash = fnv1a(id);
        s32 slot = id_hash & (new_size - 1);

        /* Linear probe in new table */
        while (new_table[slot] != SENTINEL) {
            slot = (slot + 1) & (new_size - 1);
        }

        new_table[slot] = i;
    }

    /* Swap tables */
    free(s_HashTable);
    s_HashTable = new_table;
    s_HashTableSize = new_size;
    s_HashTableCapacity = new_size;

    return 1;
}

/**
 * Grow the entry pool when it's full.
 * Realloc to 2x capacity. Hash table remains valid (stores indices).
 */
static s32 growEntryPool(void)
{
    s32 new_capacity = s_EntryPoolCapacity * 2;
    asset_entry_t *new_pool = (asset_entry_t *)realloc(s_EntryPool,
                                                         new_capacity * sizeof(asset_entry_t));

    if (new_pool == NULL) {
        return 0;  /* allocation failed */
    }

    /* Zero the newly allocated entries */
    memset(&new_pool[s_EntryPoolCapacity], 0,
           (new_capacity - s_EntryPoolCapacity) * sizeof(asset_entry_t));

    s_EntryPool = new_pool;
    s_EntryPoolCapacity = new_capacity;

    return 1;
}

/**
 * Calculate the load factor percentage: (occupied_slots / total_slots) * 100
 */
static s32 getLoadFactor(void)
{
    if (s_HashTableSize == 0) {
        return 0;
    }

    s32 occupied = 0;
    for (s32 i = 0; i < s_HashTableSize; i++) {
        if (s_HashTable[i] != SENTINEL) {
            occupied++;
        }
    }

    return (occupied * 100) / s_HashTableSize;
}

/* ========================================================================
 * Public API: Lifecycle
 * ======================================================================== */

void assetCatalogInit(void)
{
    /* Allocate hash table */
    if (s_HashTable != NULL) {
        free(s_HashTable);
    }
    s_HashTableSize = HASH_TABLE_INITIAL;
    s_HashTableCapacity = HASH_TABLE_INITIAL;
    s_HashTable = (s32 *)malloc(s_HashTableSize * sizeof(s32));

    if (s_HashTable == NULL) {
        s_HashTableSize = 0;
        s_HashTableCapacity = 0;
        return;
    }

    /* Initialize with SENTINEL */
    for (s32 i = 0; i < s_HashTableSize; i++) {
        s_HashTable[i] = SENTINEL;
    }

    /* Allocate entry pool */
    if (s_EntryPool != NULL) {
        free(s_EntryPool);
    }
    s_EntryPoolCapacity = ENTRY_POOL_INITIAL;
    s_EntryPoolSize = 0;
    s_EntryPool = (asset_entry_t *)malloc(s_EntryPoolCapacity * sizeof(asset_entry_t));

    if (s_EntryPool == NULL) {
        s_EntryPoolCapacity = 0;
        return;
    }

    /* Zero the pool */
    memset(s_EntryPool, 0, s_EntryPoolCapacity * sizeof(asset_entry_t));
}

void assetCatalogClear(void)
{
    /* Reset hash table to empty */
    if (s_HashTable != NULL) {
        for (s32 i = 0; i < s_HashTableSize; i++) {
            s_HashTable[i] = SENTINEL;
        }
    }

    /* Reset entry pool (don't deallocate, just zero and reset count) */
    if (s_EntryPool != NULL) {
        memset(s_EntryPool, 0, s_EntryPoolCapacity * sizeof(asset_entry_t));
    }

    s_EntryPoolSize = 0;
}

void assetCatalogClearMods(void)
{
    if (s_EntryPool == NULL || s_HashTable == NULL) {
        return;
    }

    /* Mark all non-bundled entries as unoccupied */
    for (s32 i = 0; i < s_EntryPoolSize; i++) {
        if (!s_EntryPool[i].occupied || s_EntryPool[i].bundled) {
            continue;
        }
        s_EntryPool[i].occupied = 0;
    }

    /* Rebuild hash table with remaining entries */
    for (s32 i = 0; i < s_HashTableSize; i++) {
        s_HashTable[i] = SENTINEL;
    }

    for (s32 i = 0; i < s_EntryPoolSize; i++) {
        if (!s_EntryPool[i].occupied) {
            continue;
        }

        u32 id_hash = fnv1a(s_EntryPool[i].id);
        s32 slot = id_hash & (s_HashTableSize - 1);

        /* Linear probe */
        while (s_HashTable[slot] != SENTINEL) {
            slot = (slot + 1) & (s_HashTableSize - 1);
        }

        s_HashTable[slot] = i;
    }
}

s32 assetCatalogGetCount(void)
{
    s32 count = 0;
    for (s32 i = 0; i < s_EntryPoolSize; i++) {
        if (s_EntryPool[i].occupied) {
            count++;
        }
    }
    return count;
}

s32 assetCatalogGetCountByType(asset_type_e type)
{
    s32 count = 0;
    for (s32 i = 0; i < s_EntryPoolSize; i++) {
        if (s_EntryPool[i].occupied && s_EntryPool[i].type == type) {
            count++;
        }
    }
    return count;
}

/* ========================================================================
 * Public API: Registration
 * ======================================================================== */

asset_entry_t *assetCatalogRegister(const char *id, asset_type_e type)
{
    if (id == NULL || s_HashTable == NULL || s_EntryPool == NULL) {
        return NULL;
    }

    /* Compute both hashes */
    u32 id_hash = fnv1a(id);
    u32 net_hash = crc32(id);

    /* Find slot (may be empty or existing entry) */
    s32 pool_idx = 0;
    s32 slot = findSlot(id_hash, id, &pool_idx);

    if (slot < 0) {
        /* Hash table completely full (shouldn't happen) */
        return NULL;
    }

    asset_entry_t *entry = NULL;

    if (pool_idx != SENTINEL) {
        /* Existing entry — reuse it (last-write-wins override) */
        entry = &s_EntryPool[pool_idx];
        memset(entry, 0, sizeof(asset_entry_t));
    } else {
        /* New entry — allocate from pool */
        if (s_EntryPoolSize >= s_EntryPoolCapacity) {
            /* Pool full — grow it */
            if (!growEntryPool()) {
                return NULL;
            }
        }

        pool_idx = s_EntryPoolSize++;
        entry = &s_EntryPool[pool_idx];
        memset(entry, 0, sizeof(asset_entry_t));

        /* Insert into hash table */
        s_HashTable[slot] = pool_idx;

        /* Check if rehash is needed (load factor check) */
        if (getLoadFactor() > LOAD_FACTOR_LIMIT) {
            if (!rehashTable()) {
                /* Rehash failed, but entry is still registered */
                return entry;
            }
        }
    }

    /* Fill in the entry */
    strncpy(entry->id, id, CATALOG_ID_LEN - 1);
    entry->id[CATALOG_ID_LEN - 1] = '\0';
    entry->id_hash = id_hash;
    entry->net_hash = net_hash;
    entry->type = type;
    entry->model_scale = 1.0f;
    entry->enabled = 1;  /* enabled by default */
    entry->temporary = 0;
    entry->bundled = 0;
    entry->runtime_index = -1;
    entry->occupied = 1;

    return entry;
}

asset_entry_t *assetCatalogRegisterMap(const char *id, s32 stagenum,
                                        const char *dirpath)
{
    asset_entry_t *entry = assetCatalogRegister(id, ASSET_MAP);
    if (entry == NULL) {
        return NULL;
    }

    entry->ext.map.stagenum = stagenum;
    entry->ext.map.mode = 0;  /* caller will set */

    if (dirpath != NULL) {
        strncpy(entry->dirpath, dirpath, FS_MAXPATH - 1);
        entry->dirpath[FS_MAXPATH - 1] = '\0';
    }

    return entry;
}

asset_entry_t *assetCatalogRegisterCharacter(const char *id,
                                             const char *bodyfile,
                                             const char *headfile)
{
    asset_entry_t *entry = assetCatalogRegister(id, ASSET_CHARACTER);
    if (entry == NULL) {
        return NULL;
    }

    if (bodyfile != NULL) {
        strncpy(entry->ext.character.bodyfile, bodyfile, FS_MAXPATH - 1);
        entry->ext.character.bodyfile[FS_MAXPATH - 1] = '\0';
    }
    if (headfile != NULL) {
        strncpy(entry->ext.character.headfile, headfile, FS_MAXPATH - 1);
        entry->ext.character.headfile[FS_MAXPATH - 1] = '\0';
    }

    return entry;
}

asset_entry_t *assetCatalogRegisterSkin(const char *id,
                                        const char *target_id)
{
    asset_entry_t *entry = assetCatalogRegister(id, ASSET_SKIN);
    if (entry == NULL) {
        return NULL;
    }

    if (target_id != NULL) {
        strncpy(entry->ext.skin.target_id, target_id, CATALOG_ID_LEN - 1);
        entry->ext.skin.target_id[CATALOG_ID_LEN - 1] = '\0';
    }

    return entry;
}

asset_entry_t *assetCatalogRegisterBotVariant(const char *id,
                                              const char *base_type,
                                              f32 accuracy,
                                              f32 reaction_time,
                                              f32 aggression)
{
    asset_entry_t *entry = assetCatalogRegister(id, ASSET_BOT_VARIANT);
    if (entry == NULL) {
        return NULL;
    }

    if (base_type != NULL) {
        strncpy(entry->ext.bot_variant.base_type, base_type, 31);
        entry->ext.bot_variant.base_type[31] = '\0';
    }
    entry->ext.bot_variant.accuracy = accuracy;
    entry->ext.bot_variant.reaction_time = reaction_time;
    entry->ext.bot_variant.aggression = aggression;

    return entry;
}

asset_entry_t *assetCatalogRegisterArena(const char *id, s32 stagenum,
                                          u8 requirefeature, s32 name_langid)
{
    asset_entry_t *entry = assetCatalogRegister(id, ASSET_ARENA);
    if (entry == NULL) {
        return NULL;
    }

    entry->ext.arena.stagenum = stagenum;
    entry->ext.arena.requirefeature = requirefeature;
    entry->ext.arena.name_langid = name_langid;

    return entry;
}

asset_entry_t *assetCatalogRegisterBody(const char *id, s16 bodynum,
                                         s16 name_langid, s16 headnum,
                                         u8 requirefeature)
{
    asset_entry_t *entry = assetCatalogRegister(id, ASSET_BODY);
    if (entry == NULL) {
        return NULL;
    }

    entry->ext.body.bodynum = bodynum;
    entry->ext.body.name_langid = name_langid;
    entry->ext.body.headnum = headnum;
    entry->ext.body.requirefeature = requirefeature;

    return entry;
}

asset_entry_t *assetCatalogRegisterHead(const char *id, s16 headnum,
                                         u8 requirefeature)
{
    asset_entry_t *entry = assetCatalogRegister(id, ASSET_HEAD);
    if (entry == NULL) {
        return NULL;
    }

    entry->ext.head.headnum = headnum;
    entry->ext.head.requirefeature = requirefeature;

    return entry;
}

/* ========================================================================
 * Public API: Resolution
 * ======================================================================== */

const asset_entry_t *assetCatalogResolve(const char *id)
{
    if (id == NULL || s_HashTable == NULL) {
        return NULL;
    }

    u32 id_hash = fnv1a(id);
    s32 pool_idx = 0;
    s32 slot = findSlot(id_hash, id, &pool_idx);

    if (slot < 0 || pool_idx == SENTINEL) {
        return NULL;  /* not found */
    }

    /* Verify the entry exists and is enabled */
    if (pool_idx < 0 || pool_idx >= s_EntryPoolSize) {
        return NULL;
    }

    asset_entry_t *entry = &s_EntryPool[pool_idx];
    if (!entry->occupied || !entry->enabled) {
        return NULL;
    }

    return entry;
}

s32 assetCatalogResolveBodyIndex(const char *id)
{
    const asset_entry_t *entry = assetCatalogResolve(id);
    if (entry == NULL || entry->type != ASSET_CHARACTER) {
        return -1;
    }
    return entry->runtime_index;
}

s32 assetCatalogResolveStageIndex(const char *id)
{
    const asset_entry_t *entry = assetCatalogResolve(id);
    if (entry == NULL || entry->type != ASSET_MAP) {
        return -1;
    }
    return entry->runtime_index;
}

const asset_entry_t *assetCatalogResolveByNetHash(u32 net_hash)
{
    /* Linear scan of entry pool (infrequent, connection-time only) */
    for (s32 i = 0; i < s_EntryPoolSize; i++) {
        if (s_EntryPool[i].occupied && s_EntryPool[i].net_hash == net_hash) {
            return &s_EntryPool[i];
        }
    }
    return NULL;
}

/* ========================================================================
 * Public API: Iteration
 * ======================================================================== */

void assetCatalogIterateByType(asset_type_e type, asset_iter_fn fn,
                                void *userdata)
{
    if (fn == NULL || s_EntryPool == NULL) {
        return;
    }

    for (s32 i = 0; i < s_EntryPoolSize; i++) {
        if (s_EntryPool[i].occupied && s_EntryPool[i].type == type) {
            fn(&s_EntryPool[i], userdata);
        }
    }
}

void assetCatalogIterateByCategory(const char *category, asset_iter_fn fn,
                                    void *userdata)
{
    if (category == NULL || fn == NULL || s_EntryPool == NULL) {
        return;
    }

    for (s32 i = 0; i < s_EntryPoolSize; i++) {
        if (s_EntryPool[i].occupied &&
            strcmp(s_EntryPool[i].category, category) == 0) {
            fn(&s_EntryPool[i], userdata);
        }
    }
}

/* ========================================================================
 * Public API: Query
 * ======================================================================== */

s32 assetCatalogHasEntry(const char *id)
{
    if (id == NULL || s_HashTable == NULL) {
        return 0;
    }

    u32 id_hash = fnv1a(id);
    s32 pool_idx = 0;
    s32 slot = findSlot(id_hash, id, &pool_idx);

    if (slot < 0 || pool_idx == SENTINEL) {
        return 0;
    }

    if (pool_idx < 0 || pool_idx >= s_EntryPoolSize) {
        return 0;
    }

    return s_EntryPool[pool_idx].occupied;
}

s32 assetCatalogIsEnabled(const char *id)
{
    const asset_entry_t *entry = assetCatalogResolve(id);
    return entry != NULL;
}

s32 assetCatalogGetSkinsForTarget(const char *target_id,
                                   const asset_entry_t **out, s32 maxout)
{
    if (target_id == NULL || out == NULL || maxout <= 0) {
        return 0;
    }

    s32 count = 0;
    for (s32 i = 0; i < s_EntryPoolSize && count < maxout; i++) {
        if (!s_EntryPool[i].occupied || s_EntryPool[i].type != ASSET_SKIN) {
            continue;
        }

        if (strcmp(s_EntryPool[i].ext.skin.target_id, target_id) == 0) {
            out[count++] = &s_EntryPool[i];
        }
    }

    return count;
}

/* ========================================================================
 * Public API: Write (D3R-6)
 * ======================================================================== */

void assetCatalogSetEnabled(const char *id, s32 enabled)
{
    if (id == NULL || s_HashTable == NULL || s_EntryPool == NULL) {
        return;
    }

    u32 id_hash = fnv1a(id);
    s32 pool_idx = 0;
    s32 slot = findSlot(id_hash, id, &pool_idx);

    if (slot < 0 || pool_idx == SENTINEL) {
        return;  /* not found */
    }

    if (pool_idx >= 0 && pool_idx < s_EntryPoolSize &&
        s_EntryPool[pool_idx].occupied) {
        s_EntryPool[pool_idx].enabled = enabled ? 1 : 0;
    }
}

s32 assetCatalogGetUniqueCategories(char out[][CATALOG_CATEGORY_LEN], s32 maxout)
{
    if (out == NULL || maxout <= 0 || s_EntryPool == NULL) {
        return 0;
    }

    s32 count = 0;
    for (s32 i = 0; i < s_EntryPoolSize; i++) {
        if (!s_EntryPool[i].occupied) {
            continue;
        }

        const char *cat = s_EntryPool[i].category;

        /* Skip base category and empty — not user-manageable */
        if (cat[0] == '\0' || strcmp(cat, "base") == 0) {
            continue;
        }

        /* Skip if already in output list */
        s32 found = 0;
        for (s32 j = 0; j < count; j++) {
            if (strcmp(out[j], cat) == 0) {
                found = 1;
                break;
            }
        }

        if (!found && count < maxout) {
            strncpy(out[count], cat, CATALOG_CATEGORY_LEN - 1);
            out[count][CATALOG_CATEGORY_LEN - 1] = '\0';
            count++;
        }
    }

    return count;
}
