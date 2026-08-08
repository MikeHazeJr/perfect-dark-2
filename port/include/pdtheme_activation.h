#ifndef PD2_PDTHEME_ACTIVATION_H
#define PD2_PDTHEME_ACTIVATION_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PDTHEME_ACTIVATION_ID_CAP 64

typedef s32 (*pdtheme_activation_load_fn)(const char *catalog_id,
	void *userdata);
typedef void (*pdtheme_activation_release_fn)(const char *catalog_id,
	void *userdata);

typedef struct pdtheme_activation_state {
	char active_id[PDTHEME_ACTIVATION_ID_CAP];
	char candidate_id[PDTHEME_ACTIVATION_ID_CAP];
	s32 candidate_loaded;
} pdtheme_activation_state_t;

/* Begin owns exactly one candidate parent reference when it succeeds. The
 * caller must then commit or abort. Commit publishes the candidate and only
 * afterwards releases the previous parent, so failed swaps retain the last
 * good theme. Re-applying the same ID is balanced (load one, release one). */
s32 pdthemeActivationBegin(pdtheme_activation_state_t *state,
	const char *candidate_id, pdtheme_activation_load_fn load,
	void *userdata);
void pdthemeActivationAbort(pdtheme_activation_state_t *state,
	pdtheme_activation_release_fn release, void *userdata);
void pdthemeActivationCommit(pdtheme_activation_state_t *state,
	pdtheme_activation_release_fn release, void *userdata);
void pdthemeActivationShutdown(pdtheme_activation_state_t *state,
	pdtheme_activation_release_fn release, void *userdata);

#ifdef __cplusplus
}
#endif

#endif
