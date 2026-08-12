#include "net/net_bandwidth.h"

#include <string.h>

void netUploadMeterReset(net_upload_meter_t *meter, u32 now_ms,
		u32 total_sent_bytes)
{
	if (!meter) return;
	meter->baseline_ms = now_ms;
	meter->baseline_bytes = total_sent_bytes;
	meter->ready = 1;
}

u32 netUploadMeterSample(net_upload_meter_t *meter, u32 now_ms,
		u32 total_sent_bytes)
{
	if (!meter) return 0;
	if (!meter->ready) {
		netUploadMeterReset(meter, now_ms, total_sent_bytes);
		return 0;
	}

	const u32 elapsed_ms = now_ms - meter->baseline_ms;
	if (elapsed_ms < NET_UPLOAD_SAMPLE_INTERVAL_MS) return 0;

	const u32 sent_bytes = total_sent_bytes - meter->baseline_bytes;
	netUploadMeterReset(meter, now_ms, total_sent_bytes);
	if (sent_bytes < NET_UPLOAD_SAMPLE_MIN_BYTES) return 0;

	u64 kbps64 = ((u64)sent_bytes * 8u) / elapsed_ms;
	if (kbps64 == 0) kbps64 = 1;
	if (kbps64 > NET_UPLOAD_KBPS_MAX) kbps64 = NET_UPLOAD_KBPS_MAX;
	return (u32)kbps64;
}

u32 netUploadKbpsIfFresh(u32 kbps, u32 measured_at_unix,
		u32 now_unix)
{
	if (kbps == 0 || measured_at_unix == 0) return 0;
	if (kbps > NET_UPLOAD_KBPS_MAX) return 0;
	if (now_unix < measured_at_unix) return 0;
	if (now_unix - measured_at_unix > NET_UPLOAD_REPORT_MAX_AGE_S) return 0;
	return kbps;
}

s32 netBandwidthChooseAuthority(const net_authority_candidate_t *candidates,
		size_t count, u32 initiator_handle, net_authority_choice_t *out)
{
	if (!candidates || count == 0 || !out) return 0;

	s32 best = -1;
	u32 best_kbps = 0;
	for (size_t i = 0; i < count; i++) {
		const net_authority_candidate_t *candidate = &candidates[i];
		if (!candidate->eligible || candidate->handle == 0) continue;

		if (best < 0 || candidate->kbps > best_kbps) {
			best = (s32)i;
			best_kbps = candidate->kbps;
			continue;
		}
		if (candidate->kbps != best_kbps) continue;

		const net_authority_candidate_t *current = &candidates[best];
		const s32 candidate_is_initiator = candidate->handle == initiator_handle;
		const s32 current_is_initiator = current->handle == initiator_handle;
		if (candidate_is_initiator != current_is_initiator) {
			if (candidate_is_initiator) best = (s32)i;
			continue;
		}
		if (candidate->handle < current->handle) best = (s32)i;
	}

	if (best < 0) return 0;
	memset(out, 0, sizeof(*out));
	out->index = best;
	out->handle = candidates[best].handle;
	out->kbps = candidates[best].kbps;
	out->is_local = candidates[best].is_local;
	return 1;
}
