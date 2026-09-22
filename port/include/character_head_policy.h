#ifndef PD_CHARACTER_HEAD_POLICY_H
#define PD_CHARACTER_HEAD_POLICY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum character_head_policy {
    CHARACTER_HEAD_POLICY_INVALID = -1,
    CHARACTER_HEAD_POLICY_UNSPECIFIED = 0,
    CHARACTER_HEAD_POLICY_FIXED,
    CHARACTER_HEAD_POLICY_RANDOM_GENDER,
    CHARACTER_HEAD_POLICY_INTEGRATED
} character_head_policy_e;

struct asset_entry;
struct ini_section;

const char *characterHeadPolicyName(character_head_policy_e policy);
/* NULL policy is the legacy fixed-head spelling; an explicitly empty or
 * unknown policy is invalid. No missing-head input implies randomness. */
character_head_policy_e characterHeadPolicyResolve(const char *policy,
    const char *body_id, const char *head_id,
    const char *bodyfile, const char *headfile);
/* Revalidates stored fields, including termination and the legacy zero enum. */
character_head_policy_e characterSourcePolicy(const struct asset_entry *entry);
/* Consumes already-qualified paths. Rejects conflicting aliases, duplicates,
 * and truncation. Publishes the character extension only on success. */
int characterSourceApplyIni(struct asset_entry *entry,
    const struct ini_section *ini);
/* Exact public child identity and, for integrated policy, body completeness.
 * is_head selects [head] versus [body]; require_complete only applies to body. */
int characterDependencySourceMatches(const struct ini_section *ini,
    const char *expected_id, int is_head, int require_complete);
/* Scanner implementation: preflight selected public archives, register missing
 * body/head rows through ordinary ingress, then commit typed ownership edges.
 * Returns dependency count, or -1 with all acquired registry state restored.
 * Catalog pool addresses can change on either result; callers must reacquire
 * their parent by a previously copied ID before any further row access. */
int assetCatalogRegisterCharacterDependencies(const struct asset_entry *entry,
    int bundled, char *error, size_t error_capacity);

#ifdef __cplusplus
}
#endif
#endif
