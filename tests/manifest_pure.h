/*
 * tests/manifest_pure.h -- Header for the manifest pure-subset copy.
 *
 * See manifest_pure.c for the rationale and drift-audit instructions.
 *
 * The types here mirror port/include/net/netmanifest.h as of 2026-04-26.
 * If the live header changes layout, these copies must stay in sync.
 *
 * Function names retain a `pdtest_` prefix so the test binary can later
 * link against the real netmanifest.c (without colliding) if a future
 * refactor splits the pure vs. stateful concerns.
 */

#ifndef PDTEST_MANIFEST_PURE_H
#define PDTEST_MANIFEST_PURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* Forward to net/netbuf.h — same struct shape, separate name to avoid
 * collision with the real netbuf in the test binary. The savebuffer
 * tests do this same trick. The serialize/deserialize functions use
 * the test's pdtest_netbuf, which is just a tiny wrapper around an
 * external netbuf-shaped buffer. */

/* Test-side mirror of MANIFEST_* constants. Kept in sync with
 * port/include/net/netmanifest.h. */
#define PDTEST_MANIFEST_SLOT_MATCH       0xFF
#define PDTEST_MANIFEST_INITIAL_CAPACITY 64
#define PDTEST_MANIFEST_MAX_ENTRIES      4096

#define PDTEST_MANIFEST_TYPE_BODY        0
#define PDTEST_MANIFEST_TYPE_HEAD        1
#define PDTEST_MANIFEST_TYPE_STAGE       2
#define PDTEST_MANIFEST_TYPE_WEAPON      3
#define PDTEST_MANIFEST_TYPE_COMPONENT   4
#define PDTEST_MANIFEST_TYPE_MODEL       5
#define PDTEST_MANIFEST_TYPE_ANIM        6
#define PDTEST_MANIFEST_TYPE_TEXTURE     7
#define PDTEST_MANIFEST_TYPE_LANG        8
#define PDTEST_MANIFEST_TYPE_AUDIO       9
#define PDTEST_MANIFEST_TYPE_PROJECTILE 10
#define PDTEST_MANIFEST_TYPE_ENTITY     11

typedef struct {
    uint32_t      net_hash;
    char          id[64];
    unsigned char type;
    unsigned char slot_index;
    unsigned char sha256[32];
} pdtest_manifest_entry_t;

typedef struct {
    uint32_t                  manifest_hash;
    uint16_t                  num_entries;
    uint16_t                  capacity;
    pdtest_manifest_entry_t  *entries;
} pdtest_manifest_t;

typedef struct {
    char           id[64];
    uint32_t       net_hash;
    unsigned char  type;
} pdtest_manifest_diff_entry_t;

typedef struct {
    pdtest_manifest_diff_entry_t *to_load;
    int                           num_to_load;
    int                           cap_to_load;
    pdtest_manifest_diff_entry_t *to_unload;
    int                           num_to_unload;
    int                           cap_to_unload;
    pdtest_manifest_diff_entry_t *to_keep;
    int                           num_to_keep;
    int                           cap_to_keep;
} pdtest_manifest_diff_t;

/* Container ops */
void pdtest_manifestClear(pdtest_manifest_t *m);
void pdtest_manifestFree(pdtest_manifest_t *m);

/* Add: dedup by id string, fill net_hash with FNV-1a fallback */
void pdtest_manifestAddEntry(pdtest_manifest_t *m, const char *id,
                             unsigned char type, unsigned char slot_index);
void pdtest_manifestAddModEntry(pdtest_manifest_t *m, const char *id,
                                unsigned char slot_index, const unsigned char *sha256);

/* Hash: FNV-1a over (id, type, slot_index) per entry */
uint32_t pdtest_manifestComputeHash(pdtest_manifest_t *m);

/* Diff: classify needed vs current into to_load / to_unload / to_keep */
void pdtest_manifestDiff(const pdtest_manifest_t *current,
                         const pdtest_manifest_t *needed,
                         pdtest_manifest_diff_t *out);
void pdtest_manifestDiffFree(pdtest_manifest_diff_t *diff);

/* Serialize / deserialize helpers. These take a raw u8 buffer and
 * length; the test-side roundtrip writes into a buffer of known size
 * and reads back. The wire format mirrors the real manifestSerialize:
 *   u16 num_entries
 *   per entry:
 *     u8 type, u8 slot_index, [u16 strlen, str id], [u8[32] sha256 if COMPONENT] */
int pdtest_manifestSerialize(unsigned char *dst, size_t dst_size,
                             const pdtest_manifest_t *m, size_t *bytes_written);
int pdtest_manifestDeserialize(const unsigned char *src, size_t src_size,
                               pdtest_manifest_t *out, size_t *bytes_consumed);

#ifdef __cplusplus
}
#endif

#endif /* PDTEST_MANIFEST_PURE_H */
