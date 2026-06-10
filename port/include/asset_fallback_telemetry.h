#ifndef PD_ASSET_FALLBACK_TELEMETRY_H
#define PD_ASSET_FALLBACK_TELEMETRY_H

#include <PR/ultratypes.h>

#include "assetcatalog.h" /* asset_type_e */

/*
 * c3849 Wave 1: normal-play asset fallback telemetry.
 *
 * The c3844 constraint says any runtime path that must fall back to ROM/cache/
 * legacy data because public source is missing or failed is an asset-chain
 * failure: it must FAIL LOUDLY and BE TRACKED as a migration defect. Before
 * this module, six families fell back silently (LOG_VERBOSE/NOTE) or with a
 * lone per-site WARNING that nothing aggregated.
 *
 * assetFallbackRecord() is a pure O(1) counter (NO logging -- call sites own
 * per-hit loudness; audio/texture-rate sites must stay cheap). The aggregate
 * is consumed at the stage-load checkpoint beside catalogAssertHealthy():
 * assetFallbackReportAndReset() logs one ASSET.FALLBACK summary line plus one
 * line per family with a count and the first-offender snapshot, then clears.
 *
 * Telemetry deliberately does NOT reset with the catalog slot allocators: it
 * tracks play-session events, not catalog identity. The only reset is the
 * checkpoint consumer. Game-thread only; counters are not atomic.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Record one fallback event. family is bucketed (out-of-range -> ASSET_NONE);
 * num is the family-local numeric id (filenum/texnum/animnum/soundnum/...);
 * what is a short static description ("clip compile failed -> ROM segment"). */
void assetFallbackRecord(asset_type_e family, s32 num, const char *what);

/* Log the aggregate (silent when zero, mirroring catalogAssertHealthy's
 * quiet-healthy contract), clear all state, return the consumed total. */
s32 assetFallbackReportAndReset(const char *checkpoint);

/* Test pins / dashboards. */
s32 assetFallbackPendingTotal(void);
s32 assetFallbackCountFor(asset_type_e family);

#ifdef __cplusplus
}
#endif

#endif /* PD_ASSET_FALLBACK_TELEMETRY_H */
