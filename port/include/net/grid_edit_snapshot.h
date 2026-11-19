#ifndef PD_GRID_EDIT_SNAPSHOT_H
#define PD_GRID_EDIT_SNAPSHOT_H

#include "net/grid_edit_wire.h"
#include "sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GRID_EDIT_SNAPSHOT_WIRE_VERSION 1u

typedef struct grid_edit_snapshot_identity {
	grid_edit_wire_identity_t room;
	u64 revision;
	char arena_id[CATALOG_ID_LEN];
	u8 arena_sha256[SHA256_DIGEST_SIZE];
} grid_edit_snapshot_identity_t;

/* Private payload. The caller authorizes the creator and associates this
 * snapshot with the host's room grant. Return free()-owned bytes. The digest
 * covers the canonical identity, member metadata and native texts and is
 * appended to the payload. Both outputs stay unchanged on refusal. */
void *gridEditSnapshotEncode(const grid_edit_snapshot_identity_t *identity,
	forge_level_doc_t *doc, u32 *out_size, u8 out_sha256[SHA256_DIGEST_SIZE],
	char *err, size_t err_cap);

/* Decode against an authenticated offer's identity and snapshot digest.
 * Expected revision zero accepts any nonzero host revision. If supplied,
 * received changes only after the entire payload validates and opens.
 * The caller owns the returned native document and frees it normally. */
forge_level_doc_t *gridEditSnapshotDecode(const void *bytes, u32 size,
	const grid_edit_snapshot_identity_t *expected,
	const u8 expected_snapshot_sha256[SHA256_DIGEST_SIZE],
	grid_edit_snapshot_identity_t *received, char *err, size_t err_cap);

/* Restore the shared draft and retain offline-only native members. The
 * caller must first authenticate baseline against expected's arena hash. */
forge_level_doc_t *gridEditSnapshotDecodeWithBaseline(const void *bytes, u32 size,
	const grid_edit_snapshot_identity_t *expected,
	const u8 expected_snapshot_sha256[SHA256_DIGEST_SIZE],
	forge_level_doc_t *baseline, grid_edit_snapshot_identity_t *received, char *err, size_t err_cap);

#ifdef __cplusplus
}
#endif

#endif
