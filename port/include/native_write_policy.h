#ifndef PD_NATIVE_WRITE_POLICY_H
#define PD_NATIVE_WRITE_POLICY_H

#include <stddef.h>
#include <PR/ultratypes.h>

/* SP-75: this header is included by C and C++ files, so its boolean results
 * are spelled s32 (0 or 1) rather than `bool`, whose width differs between
 * the two languages and, in C, with include order. Never include
 * <stdbool.h> here: it would switch `bool` to the 1-byte _Bool for the rest
 * of every C file that includes this header. */

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
s32 nativeWritePolicyInit(const char *base, const char *profileRoot);
/* Checks every declared direct and imported entry point in the actual linked
 * executable. A build missing interception fails before policy activation. */
s32 nativeWritePolicyLinkContract(void);
s32 nativeWritePolicyActive(void);
/* Sticky device-admission failure: never interpret a suppressed device as a
 * free controller slot. Checked before smoke admission and every frame. */
s32 nativeWritePolicyRefused(void);
const char *nativeWritePolicyProfile(void);
s32 nativeWritePolicyAllowsPath(const char *path);

/* Rebuildable products only: deterministic identity hash within a private
 * cache namespace. Public typed-archive payloads remain the authored source. */
s32 nativeWritePolicyCachePath(const char *kind, const char *identity,
    char *out, size_t outSize);

#ifdef PD_NATIVE_WRITE_POLICY_TESTING
void nativeWritePolicyResetForTests(void);
#endif

#ifdef __cplusplus
}
#endif
#endif
