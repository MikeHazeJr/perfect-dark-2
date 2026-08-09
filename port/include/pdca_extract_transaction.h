/** Transactional extraction for received PDCA component archives. */
#ifndef _IN_PDCA_EXTRACT_TRANSACTION_H
#define _IN_PDCA_EXTRACT_TRANSACTION_H

#include <PR/ultratypes.h>

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
