#ifndef PD_TEST_CATALOG_MUTATION_SUPPORT_H
#define PD_TEST_CATALOG_MUTATION_SUPPORT_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct catalog_mutation_controls {
    int preflight_ok, dependent_preflight_ok, invalidate_ok, teardown_ok, reload_ok;
    int conflict_on_invalidate, fail_teardown_at;
    int invalidate_calls, teardown_calls, reload_calls;
} catalog_mutation_controls_t;
extern catalog_mutation_controls_t g_CatalogMutationControls;
void catalogMutationTestReset(void);
#ifdef __cplusplus
}
#endif
#endif
