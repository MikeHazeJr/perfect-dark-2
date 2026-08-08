#include "pdtheme_activation.h"

#include <string.h>

static void clear_candidate(pdtheme_activation_state_t *state)
{
	state->candidate_id[0] = '\0';
	state->candidate_loaded = 0;
}

s32 pdthemeActivationBegin(pdtheme_activation_state_t *state,
		const char *candidate_id, pdtheme_activation_load_fn load,
		void *userdata)
{
	if (!state || !candidate_id || !candidate_id[0] || !load
			|| state->candidate_loaded
			|| strlen(candidate_id) >= sizeof(state->candidate_id)) {
		return 0;
	}
	if (!load(candidate_id, userdata)) {
		return 0;
	}
	strncpy(state->candidate_id, candidate_id,
		sizeof(state->candidate_id) - 1);
	state->candidate_id[sizeof(state->candidate_id) - 1] = '\0';
	state->candidate_loaded = 1;
	return 1;
}

void pdthemeActivationAbort(pdtheme_activation_state_t *state,
		pdtheme_activation_release_fn release, void *userdata)
{
	if (!state) return;
	if (state->candidate_loaded && release) {
		release(state->candidate_id, userdata);
	}
	clear_candidate(state);
}

void pdthemeActivationCommit(pdtheme_activation_state_t *state,
		pdtheme_activation_release_fn release, void *userdata)
{
	char previous[PDTHEME_ACTIVATION_ID_CAP];
	if (!state || !state->candidate_loaded) return;
	strncpy(previous, state->active_id, sizeof(previous) - 1);
	previous[sizeof(previous) - 1] = '\0';
	strncpy(state->active_id, state->candidate_id,
		sizeof(state->active_id) - 1);
	state->active_id[sizeof(state->active_id) - 1] = '\0';
	clear_candidate(state);
	if (previous[0] && release) {
		release(previous, userdata);
	}
}

void pdthemeActivationShutdown(pdtheme_activation_state_t *state,
		pdtheme_activation_release_fn release, void *userdata)
{
	if (!state) return;
	pdthemeActivationAbort(state, release, userdata);
	if (state->active_id[0] && release) {
		release(state->active_id, userdata);
	}
	memset(state, 0, sizeof(*state));
}
