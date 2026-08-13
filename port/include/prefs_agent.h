#ifndef _IN_PREFS_AGENT_H
#define _IN_PREFS_AGENT_H

/**
 * prefs_agent.h -- runtime adapter for unified Agent Profile preferences.
 *
 * Current per-agent preferences are serialized only inside the versioned
 * Agent Profile JSON. pd.ini remains the unsigned-in machine baseline.
 * Legacy prefs_<agent>.ini files are read only by the v2 migration adapter.
 */

#include <PR/ultratypes.h>
#include "agent_profile_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Capture the machine baseline after config, catalog, and UI init. */
void prefsAgentInit(void);

/** Copy the machine-default candidate used by new and legacy profiles. */
void prefsAgentGetBaselineSnapshot(struct agent_profile_preferences *out);

/** Capture current user-facing values for the unified profile writer. */
void prefsAgentCaptureSnapshot(struct agent_profile_preferences *out);

/** Pure pre-commit validation. This function never changes runtime state. */
s32 prefsAgentPrepareSnapshot(const struct agent_profile_preferences *candidate,
		char *error, size_t error_size);

/**
 * Apply a prepared snapshot. Missing optional mod assets use deterministic
 * machine fallbacks while the requested IDs remain retained in the profile.
 */
void prefsAgentCommitSnapshot(const struct agent_profile_preferences *candidate);

/** Publish active identity and online presence after game and prefs commit. */
void prefsAgentPublishActive(const char *agent_name);

/** Active Agent identity, or an empty string before successful activation. */
const char *prefsAgentGetActive(void);

/**
 * Apply the complete machine baseline before the first Agent signs in. This
 * does not publish or clear an identity and does not write any file.
 */
void prefsAgentApplyMachineBaseline(void);

/** Re-capture machine visuals after a pre-sign-in pd.ini change. */
void prefsAgentRefreshVisualsBaseline(void);

/** Save changed active preferences through the unified Agent JSON writer. */
s32 prefsAgentSave(void);

/**
 * Read and validate the optional legacy sidecar into a candidate. A missing
 * file succeeds with found=0. No runtime or disk state changes occur.
 */
s32 prefsAgentReadLegacySidecar(const char *agent_name,
		const struct agent_profile_preferences *defaults,
		struct agent_profile_preferences *out, s32 *found,
		char *error, size_t error_size);

/** Remove a stale legacy sidecar after the unified JSON is authoritative. */
void prefsAgentRetireLegacySidecar(const char *agent_name);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PREFS_AGENT_H */
