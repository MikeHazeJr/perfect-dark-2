#include <string.h>
#include "asset_source_harness.h"
#include "asset_runtime.h"
#include "assetcatalog.h"
#include "assetcatalog_deps.h"
#include "assetcatalog_load.h"
#include "character_head_policy.h"
#include "player_identity.h"
#include "smoke_harness.h"
#include "system.h"

typedef struct character_edge_check {
    const char *body;
    const char *head;
    int bodies;
    int heads;
    int invalid;
} character_edge_check_t;

static void characterCheckEdge(const char *id, asset_type_e type, void *userdata)
{
    character_edge_check_t *check = userdata;
    if (type == ASSET_BODY) {
        check->bodies++;
        check->invalid |= strcmp(id, check->body) != 0;
    } else if (type == ASSET_HEAD) {
        check->heads++;
        check->invalid |= strcmp(id, check->head) != 0;
    }
}

/* Check every extracted multiplayer template through the real typed lifecycle.
 * This proves source policy/closure activation, not UI random selection, model
 * appearance, player spawning or multiplayer transmission of an empty head. */
int assetSourceCharacterHarnessRun(void)
{
    int total = 0, fixed = 0, random_gender = 0, integrated = 0, ok = 1;
    if (!smokeHarnessIsActive()) return -1;
    for (int i = 0; i < assetCatalogGetPoolSize(); ++i) {
        const asset_entry_t *entry = assetCatalogGetByIndex(i);
        char id[CATALOG_ID_LEN], body[CATALOG_ID_LEN], head[CATALOG_ID_LEN];
        const asset_runtime_binding_t *binding;
        character_head_policy_e policy;
        int loaded = 0, valid = 0;
        if (!entry || entry->type != ASSET_CHARACTER || strncmp(entry->id, "base:", 5) != 0) continue;
        policy = characterSourcePolicy(entry);
        if (policy == CHARACTER_HEAD_POLICY_INVALID) { ok = 0; break; }
        strcpy(id, entry->id);
        strcpy(body, entry->ext.character.body_id);
        strcpy(head, entry->ext.character.head_id);
        character_edge_check_t edges = {body, head, 0, 0, 0};
        catalogDepForEachTyped(id, characterCheckEdge, &edges);
        valid = !edges.invalid && edges.bodies == 1
            && edges.heads == (policy == CHARACTER_HEAD_POLICY_FIXED ? 1 : 0);
        if (valid) loaded = catalogLoadTypedAsset(ASSET_CHARACTER, id);
        binding = loaded ? assetRuntimeFind(id) : NULL;
        valid = valid && loaded && binding && binding->active && binding->source_hydrated
            && binding->character_head_policy == policy
            && strcmp(binding->character_body_id, body) == 0
            && strcmp(binding->character_head_id, head) == 0;
        if (valid && policy == CHARACTER_HEAD_POLICY_INTEGRATED) {
            player_identity_plan_t plan;
            valid = playerIdentityPrepare(body, "", &plan) == PLAYER_IDENTITY_OK
                && strcmp(plan.body_id, body) == 0 && !plan.head_id[0]
                && plan.runtime_headnum == -1;
        }
        sysLogPrintf(LOG_NOTE, "ASSET.SOURCE.CHARACTER: id=%s policy=%s body_edges=%d "
            "head_edges=%d hydrated=%d result=%s", id, characterHeadPolicyName(policy),
            edges.bodies, edges.heads, binding ? binding->source_hydrated : 0,
            valid ? "PASS" : "FAIL");
        if (loaded) catalogReleaseTypedAsset(ASSET_CHARACTER, id);
        if (!valid) { ok = 0; break; }
        ++total;
        if (policy == CHARACTER_HEAD_POLICY_FIXED) ++fixed;
        else if (policy == CHARACTER_HEAD_POLICY_RANDOM_GENDER) ++random_gender;
        else if (policy == CHARACTER_HEAD_POLICY_INTEGRATED) ++integrated;
    }
    ok = ok && total == 63 && fixed == 26 && random_gender == 35 && integrated == 2;
    sysLogPrintf(LOG_NOTE, "ASSET.SOURCE.CHARACTER: total=%d fixed=%d random_gender=%d "
        "integrated=%d result=%s", total, fixed, random_gender, integrated, ok ? "PASS" : "FAIL");
    return ok ? 0 : -1;
}
