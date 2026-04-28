/*
 * tests/manifest_pure.c -- Pure manifest container/diff/serialize subset
 * extracted from port/src/net/netmanifest.c (as of 2026-04-26).
 *
 * Why a copy instead of compiling netmanifest.c directly: the file
 * defines manifestBuild / manifestBuildForHost / manifestBuildMission
 * et al. which reference 7 globals (g_MpSetup, g_NetClients,
 * g_BotConfigsArray, g_MatchConfig, g_Lobby, g_Vars, g_StageSetup,
 * g_NetMode, g_NetLocalClient, g_NetMsgRel) and call into 11+ subsystem
 * functions (chraiGetCommandLength, audioGetModTrackId,
 * assetCatalogIterateByType, catalogResolveStage, setupGetCmdLength,
 * catalogLoadAsset, catalogUnloadAsset, catalogGetBodyModeldef,
 * catalogGetHeadModeldef, modmgrFindMod, sha256ToHex,
 * netmsgClcManifestStatusWrite, netSend). Linking netmanifest.c into
 * the test binary cascades into a stub surface roughly the size of the
 * real subsystem. The pure subset (Clear/Free/Add/AddMod/Hash/Diff/
 * DiffFree/Serialize/Deserialize) doesn't need any of that.
 *
 * Drift audit: this file is structurally a verbatim copy of the
 * referenced ranges in netmanifest.c, with two changes:
 *   1. Function names get a `pdtest_` prefix.
 *   2. Type names get a `pdtest_` prefix (mirror types in manifest_pure.h).
 *   3. assetCatalogResolve is replaced with NULL (forces synthetic-
 *      hash fallback path — same behavior the test stubs would force).
 *   4. Serialize/Deserialize wire helpers operate on raw u8 buffers
 *      instead of netbuf, with a tiny inline netbuf-equivalent.
 *
 * If port/src/net/netmanifest.c diverges substantially, this file
 * should be re-synced. Search for "@SYNC" markers below for the live
 * source line references.
 */

#include "manifest_pure.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* @SYNC: port/src/net/netmanifest.c:88 (s_fnv1a) */
static uint32_t pdtest_fnv1a(const char *str)
{
    uint32_t h = 0x811c9dc5u;
    while (*str) {
        h ^= (unsigned char)*str++;
        h *= 0x01000193u;
    }
    return h;
}

/* @SYNC: port/src/net/netmanifest.c:104 (s_manifestGrow) */
static int pdtest_manifestGrow(pdtest_manifest_t *m)
{
    int new_cap;
    pdtest_manifest_entry_t *new_buf;

    if (!m->entries) {
        new_cap = PDTEST_MANIFEST_INITIAL_CAPACITY;
        if (new_cap > PDTEST_MANIFEST_MAX_ENTRIES) {
            new_cap = PDTEST_MANIFEST_MAX_ENTRIES;
        }
        new_buf = (pdtest_manifest_entry_t *)malloc(
            (size_t)new_cap * sizeof(pdtest_manifest_entry_t));
        if (!new_buf) {
            return 0;
        }
        m->entries  = new_buf;
        m->capacity = (uint16_t)new_cap;
        return 1;
    }

    if ((int)m->capacity >= PDTEST_MANIFEST_MAX_ENTRIES) {
        return 0;
    }

    new_cap = (int)m->capacity * 2;
    if (new_cap > PDTEST_MANIFEST_MAX_ENTRIES) {
        new_cap = PDTEST_MANIFEST_MAX_ENTRIES;
    }

    new_buf = (pdtest_manifest_entry_t *)realloc(
        m->entries, (size_t)new_cap * sizeof(pdtest_manifest_entry_t));
    if (!new_buf) {
        return 0;
    }
    m->entries  = new_buf;
    m->capacity = (uint16_t)new_cap;
    return 1;
}

/* @SYNC: port/src/net/netmanifest.c:243 (manifestClear) */
void pdtest_manifestClear(pdtest_manifest_t *m)
{
    m->num_entries   = 0;
    m->manifest_hash = 0;
}

/* @SYNC: port/src/net/netmanifest.c:251 (manifestFree) */
void pdtest_manifestFree(pdtest_manifest_t *m)
{
    free(m->entries);
    m->entries       = NULL;
    m->capacity      = 0;
    m->num_entries   = 0;
    m->manifest_hash = 0;
}

/* @SYNC: port/src/net/netmanifest.c:260 (manifestAddEntry) */
void pdtest_manifestAddEntry(pdtest_manifest_t *m, const char *id,
                             unsigned char type, unsigned char slot_index)
{
    int i;
    pdtest_manifest_entry_t *e;

    /* dedup by id string */
    for (i = 0; i < (int)m->num_entries; i++) {
        if (id && strncmp(m->entries[i].id, id, sizeof(m->entries[i].id)) == 0) {
            return;
        }
    }

    if ((int)m->num_entries >= (int)m->capacity) {
        if (!pdtest_manifestGrow(m)) {
            return;
        }
    }

    e = &m->entries[m->num_entries++];
    /* Synthetic-fallback path: no real catalog in the test, so net_hash
     * is FNV-1a of the id (deterministic). Mirrors what the live code
     * does when assetCatalogResolve(id) returns NULL. */
    e->net_hash   = id ? pdtest_fnv1a(id) : 0;
    e->type       = type;
    e->slot_index = slot_index;
    memset(e->sha256, 0, sizeof(e->sha256));
    if (id) {
        strncpy(e->id, id, sizeof(e->id) - 1);
        e->id[sizeof(e->id) - 1] = '\0';
    } else {
        e->id[0] = '\0';
    }
}

/* @SYNC: port/src/net/netmanifest.c:300 (manifestAddModEntry) */
void pdtest_manifestAddModEntry(pdtest_manifest_t *m, const char *id,
                                unsigned char slot_index, const unsigned char *sha256)
{
    int i;
    pdtest_manifest_entry_t *e;

    for (i = 0; i < (int)m->num_entries; i++) {
        if (id && strncmp(m->entries[i].id, id, sizeof(m->entries[i].id)) == 0) {
            return;
        }
    }

    if ((int)m->num_entries >= (int)m->capacity) {
        if (!pdtest_manifestGrow(m)) {
            return;
        }
    }

    e = &m->entries[m->num_entries++];
    e->net_hash   = id ? pdtest_fnv1a(id) : 0;
    e->type       = PDTEST_MANIFEST_TYPE_COMPONENT;
    e->slot_index = slot_index;
    if (sha256) {
        memcpy(e->sha256, sha256, sizeof(e->sha256));
    } else {
        memset(e->sha256, 0, sizeof(e->sha256));
    }
    if (id) {
        strncpy(e->id, id, sizeof(e->id) - 1);
        e->id[sizeof(e->id) - 1] = '\0';
    } else {
        e->id[0] = '\0';
    }
}

/* @SYNC: port/src/net/netmanifest.c:343 (manifestComputeHash) */
uint32_t pdtest_manifestComputeHash(pdtest_manifest_t *m)
{
    uint32_t h = 0x811c9dc5u;
    int i;
    for (i = 0; i < (int)m->num_entries; i++) {
        const pdtest_manifest_entry_t *e = &m->entries[i];
        const char *s = e->id;
        while (*s) { h ^= (unsigned char)*s++; h *= 0x01000193u; }
        h ^= e->type;        h *= 0x01000193u;
        h ^= e->slot_index;  h *= 0x01000193u;
    }
    m->manifest_hash = h;
    return h;
}

/* @SYNC: port/src/net/netmanifest.c:205 (s_diffGrow) */
static pdtest_manifest_diff_entry_t *pdtest_diffGrow(
    pdtest_manifest_diff_entry_t **arr, int *count, int *cap)
{
    int new_cap;
    pdtest_manifest_diff_entry_t *new_buf;

    if (*count < *cap) {
        return &(*arr)[(*count)++];
    }

    if (*cap <= 0) {
        new_cap = PDTEST_MANIFEST_INITIAL_CAPACITY;
    } else if (*cap >= PDTEST_MANIFEST_MAX_ENTRIES) {
        return NULL;
    } else {
        new_cap = *cap * 2;
        if (new_cap > PDTEST_MANIFEST_MAX_ENTRIES) {
            new_cap = PDTEST_MANIFEST_MAX_ENTRIES;
        }
    }

    new_buf = (pdtest_manifest_diff_entry_t *)realloc(
        *arr, (size_t)new_cap * sizeof(pdtest_manifest_diff_entry_t));
    if (!new_buf) return NULL;
    *arr = new_buf;
    *cap = new_cap;
    return &(*arr)[(*count)++];
}

/* @SYNC: port/src/net/netmanifest.c:1427 (manifestDiff) */
void pdtest_manifestDiff(const pdtest_manifest_t *current,
                         const pdtest_manifest_t *needed,
                         pdtest_manifest_diff_t *out)
{
    int i, j, found;
    pdtest_manifest_diff_entry_t *de;

    out->num_to_load   = 0;
    out->num_to_unload = 0;
    out->num_to_keep   = 0;

    for (i = 0; i < (int)needed->num_entries; i++) {
        const pdtest_manifest_entry_t *ne = &needed->entries[i];
        found = 0;
        for (j = 0; j < (int)current->num_entries; j++) {
            if (ne->net_hash == current->entries[j].net_hash) {
                found = 1;
                break;
            }
        }
        if (found) {
            de = pdtest_diffGrow(&out->to_keep, &out->num_to_keep, &out->cap_to_keep);
            if (de) {
                de->net_hash = ne->net_hash;
                de->type     = ne->type;
                strncpy(de->id, ne->id, sizeof(de->id) - 1);
                de->id[sizeof(de->id) - 1] = '\0';
            }
        } else {
            de = pdtest_diffGrow(&out->to_load, &out->num_to_load, &out->cap_to_load);
            if (de) {
                de->net_hash = ne->net_hash;
                de->type     = ne->type;
                strncpy(de->id, ne->id, sizeof(de->id) - 1);
                de->id[sizeof(de->id) - 1] = '\0';
            }
        }
    }

    for (i = 0; i < (int)current->num_entries; i++) {
        const pdtest_manifest_entry_t *ce = &current->entries[i];
        found = 0;
        for (j = 0; j < (int)needed->num_entries; j++) {
            if (ce->net_hash == needed->entries[j].net_hash) {
                found = 1;
                break;
            }
        }
        if (!found) {
            de = pdtest_diffGrow(&out->to_unload, &out->num_to_unload, &out->cap_to_unload);
            if (de) {
                de->net_hash = ce->net_hash;
                de->type     = ce->type;
                strncpy(de->id, ce->id, sizeof(de->id) - 1);
                de->id[sizeof(de->id) - 1] = '\0';
            }
        }
    }
}

/* @SYNC: port/src/net/netmanifest.c:1492 (manifestDiffFree) */
void pdtest_manifestDiffFree(pdtest_manifest_diff_t *diff)
{
    free(diff->to_load);
    free(diff->to_unload);
    free(diff->to_keep);
    memset(diff, 0, sizeof(*diff));
}

/* ========================================================================
 * Serialize / Deserialize
 *
 * Mirror manifestSerialize/Deserialize at netmanifest.c:883/907 but
 * write to/read from a raw u8 buffer (skipping the netbuf indirection
 * since the netbuf primitives are tested separately).
 *
 * Wire format (matches the live one):
 *   u16  num_entries  (little-endian)
 *   per entry:
 *     u8  type
 *     u8  slot_index
 *     u16 strlen        (length-prefixed string, includes null)
 *     bytes id (strlen bytes, last byte null)
 *     [u8[32] sha256]   (only when type == COMPONENT)
 *
 * Returns 0 on success, non-zero on overflow / parse failure. Parse failure
 * rolls back entries appended by this call, matching the live deserializer.
 * ======================================================================== */

static void w_u8(unsigned char **p, unsigned char *end, unsigned char v, int *err) {
    if (*err) return;
    if (*p + 1 > end) { *err = 1; return; }
    **p = v; (*p)++;
}
static void w_u16le(unsigned char **p, unsigned char *end, uint16_t v, int *err) {
    if (*err) return;
    if (*p + 2 > end) { *err = 1; return; }
    (*p)[0] = (unsigned char)(v & 0xFF);
    (*p)[1] = (unsigned char)((v >> 8) & 0xFF);
    *p += 2;
}
static void w_bytes(unsigned char **p, unsigned char *end, const unsigned char *b, size_t n, int *err) {
    if (*err) return;
    if (*p + n > end) { *err = 1; return; }
    memcpy(*p, b, n); *p += n;
}
static void w_str(unsigned char **p, unsigned char *end, const char *s, int *err) {
    if (*err) return;
    size_t n = strlen(s) + 1;
    if (n > 0xFFFFu) n = 0xFFFFu;
    w_u16le(p, end, (uint16_t)n, err);
    w_bytes(p, end, (const unsigned char *)s, n, err);
    if (n > 0) *(*p - 1) = 0;  /* explicit null */
}

static unsigned char r_u8(const unsigned char **p, const unsigned char *end, int *err) {
    if (*err || *p + 1 > end) { *err = 1; return 0; }
    unsigned char v = **p; (*p)++; return v;
}
static uint16_t r_u16le(const unsigned char **p, const unsigned char *end, int *err) {
    if (*err || *p + 2 > end) { *err = 1; return 0; }
    uint16_t v = (uint16_t)((*p)[0] | ((*p)[1] << 8));
    *p += 2; return v;
}
static const char *r_str(const unsigned char **p, const unsigned char *end, int *err) {
    if (*err) return NULL;
    uint16_t n = r_u16le(p, end, err);
    if (*err) return NULL;
    if (*p + n > end) { *err = 1; return NULL; }
    /* Force null termination of the string segment */
    if (n > 0 && (*p)[n - 1] != '\0') {
        ((unsigned char *)*p)[n - 1] = 0;
    }
    const char *s = (const char *)*p;
    *p += n;
    return s;
}
static void r_bytes(const unsigned char **p, const unsigned char *end, unsigned char *out, size_t n, int *err) {
    if (*err || *p + n > end) { *err = 1; return; }
    memcpy(out, *p, n); *p += n;
}

int pdtest_manifestSerialize(unsigned char *dst, size_t dst_size,
                             const pdtest_manifest_t *m, size_t *bytes_written)
{
    unsigned char *p = dst;
    unsigned char *end = dst + dst_size;
    int err = 0;

    w_u16le(&p, end, (uint16_t)m->num_entries, &err);
    for (int i = 0; i < (int)m->num_entries; i++) {
        const pdtest_manifest_entry_t *e = &m->entries[i];
        w_u8(&p, end, e->type, &err);
        w_u8(&p, end, e->slot_index, &err);
        w_str(&p, end, e->id, &err);
        if (e->type == PDTEST_MANIFEST_TYPE_COMPONENT) {
            w_bytes(&p, end, e->sha256, sizeof(e->sha256), &err);
        }
    }
    if (bytes_written) *bytes_written = (size_t)(p - dst);
    return err;
}

int pdtest_manifestDeserialize(const unsigned char *src, size_t src_size,
                               pdtest_manifest_t *out, size_t *bytes_consumed)
{
    const unsigned char *p = src;
    const unsigned char *end = src + src_size;
    uint16_t start_entries = out->num_entries;
    uint32_t start_hash = out->manifest_hash;
    int err = 0;

    uint16_t num = r_u16le(&p, end, &err);
    if (err || num > (uint16_t)PDTEST_MANIFEST_MAX_ENTRIES) {
        out->num_entries = start_entries;
        out->manifest_hash = start_hash;
        if (bytes_consumed) *bytes_consumed = (size_t)(p - src);
        return 1;
    }
    for (int i = 0; i < (int)num; i++) {
        unsigned char type       = r_u8(&p, end, &err);
        unsigned char slot_index = r_u8(&p, end, &err);
        const char *id           = r_str(&p, end, &err);
        if (err) {
            out->num_entries = start_entries;
            out->manifest_hash = start_hash;
            if (bytes_consumed) *bytes_consumed = (size_t)(p - src);
            return 1;
        }
        if (type == PDTEST_MANIFEST_TYPE_COMPONENT) {
            unsigned char sha256[32];
            r_bytes(&p, end, sha256, sizeof(sha256), &err);
            if (err) {
                out->num_entries = start_entries;
                out->manifest_hash = start_hash;
                if (bytes_consumed) *bytes_consumed = (size_t)(p - src);
                return 1;
            }
            /* SEC-5 / MASTER-H2: reject all-zero SHA-256 — see the
             * netmanifest.c:944 commentary. The test mirrors this so a
             * regression is loud. */
            static const unsigned char s_zero32[32] = {0};
            if (memcmp(sha256, s_zero32, sizeof(sha256)) == 0) {
                continue;  /* drop entry */
            }
            pdtest_manifestAddModEntry(out, id, slot_index, sha256);
        } else {
            pdtest_manifestAddEntry(out, id, type, slot_index);
        }
    }
    if (bytes_consumed) *bytes_consumed = (size_t)(p - src);
    return 0;
}
