#ifndef _IN_CAMPAIGN_EVIDENCE_H
#define _IN_CAMPAIGN_EVIDENCE_H

#include <PR/ultratypes.h>
#include "campaign_run.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct campaign_evidence_options {
	const char *path;
	const char *status;
	s32 verify_only;
	const u16 *persisted_besttimes;
	s32 persisted_besttime_count;
} campaign_evidence_options_t;

/**
 * Atomically publish a bounded JSON snapshot of a campaign run. The caller
 * supplies persisted best times separately so verification after a process
 * restart uses the same schema as the live run.
 */
s32 campaignEvidenceWrite(const campaign_run_t *run,
	const campaign_evidence_options_t *options,
	char *error,
	s32 error_capacity);

#ifdef __cplusplus
}
#endif

#endif /* _IN_CAMPAIGN_EVIDENCE_H */
