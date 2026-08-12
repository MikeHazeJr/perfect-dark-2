#ifndef _IN_NET_BANDWIDTH_H
#define _IN_NET_BANDWIDTH_H

#include "PR/ultratypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NET_UPLOAD_SAMPLE_INTERVAL_MS 2000u
#define NET_UPLOAD_SAMPLE_MIN_BYTES   4096u
#define NET_UPLOAD_KBPS_MAX           10000000u
#define NET_UPLOAD_REPORT_MAX_AGE_S   (30u * 24u * 60u * 60u)
#define GROUP_KBPS_FRESH_MS           90000u

typedef struct net_upload_meter_s {
	u32 baseline_ms;
	u32 baseline_bytes;
	u8  ready;
} net_upload_meter_t;

typedef struct net_authority_candidate_s {
	u32 handle;
	u32 kbps;
	u8  eligible;
	u8  is_local;
} net_authority_candidate_t;

typedef struct net_authority_choice_s {
	s32 index;
	u32 handle;
	u32 kbps;
	u8  is_local;
} net_authority_choice_t;

/** Start or restart a passive ENet upload sample window. */
void netUploadMeterReset(net_upload_meter_t *meter, u32 now_ms,
		u32 total_sent_bytes);

/**
 * Observe cumulative ENet bytes. Returns a measured kbps sample only after a
 * complete interval with enough real traffic; otherwise returns zero. u32
 * subtraction deliberately supports SDL tick and ENet byte-counter wrap.
 */
u32 netUploadMeterSample(net_upload_meter_t *meter, u32 now_ms,
		u32 total_sent_bytes);

/** Return a persisted measurement only while its wall-clock evidence is fresh. */
u32 netUploadKbpsIfFresh(u32 kbps, u32 measured_at_unix,
		u32 now_unix);

/**
 * Deterministic authority election over already-fresh candidates. Highest
 * measured upload wins; the match initiator wins an unavailable-data or exact
 * speed tie; the smallest public handle is the final stable tie-break.
 */
s32 netBandwidthChooseAuthority(const net_authority_candidate_t *candidates,
		size_t count, u32 initiator_handle, net_authority_choice_t *out);

#ifdef __cplusplus
}
#endif

#endif
