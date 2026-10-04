#ifndef PD_NATIVE_WRITE_POLICY_H
#define PD_NATIVE_WRITE_POLICY_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opt-in existing-install smoke mode. Initialize before any subsystem which
 * can write. Paths must name disjoint, existing, ordinary absolute directories.
 * Once active, native filesystem mutations are confined to profileRoot.
 * Authored source is never redirected. Missing or invalid base source fails.
 * This is a policy for the linked native client, not an OS permission change.
 * Inherited output refusal leaves the policy active: exit immediately without
 * logging on any initialization failure. */
bool nativeWritePolicyInit(const char *base, const char *profileRoot);
/* Checks every declared direct and imported entry point in the actual linked
 * executable. A build missing interception fails before policy activation. */
bool nativeWritePolicyLinkContract(void);
bool nativeWritePolicyActive(void);
/* Sticky device-admission failure: never interpret a suppressed device as a
 * free controller slot. Checked before smoke admission and every frame. */
bool nativeWritePolicyRefused(void);
const char *nativeWritePolicyProfile(void);
bool nativeWritePolicyAllowsPath(const char *path);

/* Rebuildable products only: deterministic identity hash within a private
 * cache namespace. Public typed-archive payloads remain the authored source. */
bool nativeWritePolicyCachePath(const char *kind, const char *identity,
    char *out, size_t outSize);

#ifdef PD_NATIVE_WRITE_POLICY_TESTING
void nativeWritePolicyResetForTests(void);
#endif

#ifdef __cplusplus
}
#endif
#endif
