/** Transactional extraction for received PDCA component archives. */
#ifndef _IN_PDCA_EXTRACT_TRANSACTION_H
#define _IN_PDCA_EXTRACT_TRANSACTION_H

#include <PR/ultratypes.h>
#include "fs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PDCA_ARCHIVE_MAGIC 0x41434450u

typedef enum pdca_extract_result {
	PDCA_EXTRACT_OK = 1,
	PDCA_EXTRACT_OK_BACKUP_RETAINED = 2,
	PDCA_EXTRACT_INVALID = 0,
	PDCA_EXTRACT_PATH_REJECTED = -1,
	PDCA_EXTRACT_STAGE_FAILED = -2,
	PDCA_EXTRACT_OPEN_FAILED = -3,
	PDCA_EXTRACT_WRITE_FAILED = -4,
	PDCA_EXTRACT_PUBLISH_FAILED = -5,
	PDCA_EXTRACT_RECOVERY_REQUIRED = -6,
} pdca_extract_result_t;

/* Deterministic failure injection for rollback tests. Production passes NULL.
 * Indexes are zero-based; a negative index disables the fault. */
typedef struct pdca_extract_faults {
	s32 fail_open_index;
	s32 fail_write_index;
	s32 fail_publish;
} pdca_extract_faults_t;

typedef struct pdca_extract_transaction {
	char destdir[FS_MAXPATH];
	char backup[FS_MAXPATH];
	s32 active;
} pdca_extract_transaction_t;

/**
 * Publish a complete archive while retaining any prior destination as a
 * sibling backup. The caller must explicitly commit after catalog admission,
 * or roll back to remove the candidate and restore the prior install.
 */
pdca_extract_result_t pdcaExtractArchiveBegin(const u8 *data, u32 data_len,
	const char *destdir, const pdca_extract_faults_t *faults,
	pdca_extract_transaction_t *transaction);

/** Finalize a begun transaction and remove its retained backup. */
pdca_extract_result_t pdcaExtractTransactionCommit(
	pdca_extract_transaction_t *transaction);

/** Remove the published candidate and restore its retained backup. */
s32 pdcaExtractTransactionRollback(pdca_extract_transaction_t *transaction);

/**
 * Extract into a unique sibling of destdir, then publish by rename on the
 * same filesystem. An existing destination is renamed aside and restored if
 * publication fails.
 */
pdca_extract_result_t pdcaExtractArchiveTransactional(const u8 *data,
	u32 data_len, const char *destdir, const pdca_extract_faults_t *faults);

#ifdef __cplusplus
}
#endif

#endif
