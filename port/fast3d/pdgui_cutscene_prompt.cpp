/*
 * pdgui_cutscene_prompt.cpp -- contextual cutscene skip prompt.
 *
 * Cutscenes own input. The only gameplay prompt allowed while a cutscene is
 * active is the contextual hold-to-skip affordance; interact prompts such as
 * "Open door" are suppressed by pdguiCiIntroBlocksInteractPrompt().
 */

#include <PR/ultratypes.h>

#include "imgui/imgui.h"
#include "pdgui.h"
#include "pdgui_cutscene_prompt.h"
#include "pdgui_glyphs.h"
#include "pdgui_scaling.h"
#include "actionmap.h"

namespace {

static const InputAction kCutsceneSkipActions[] = {
	ACTION_SKIP_CUTSCENE,
	ACTION_USE,
	ACTION_CANCEL_USE,
	ACTION_FIRE_PRIMARY,
	ACTION_FIRE_SECONDARY,
	ACTION_PAUSE,
	ACTION_FIRE_MODE,
	ACTION_RELOAD,
	ACTION_WEAPON_NEXT,
};

static bool actionInProgress(s32 player, InputAction action, float *progress)
{
	const float p = actionHoldProgress(player, action, ACTION_SKIP_CUTSCENE_HOLD_THRESHOLD_MS);
	if (progress) {
		*progress = p;
	}

	return actionHeld(player, action) || p > 0.001f;
}

static bool findPromptAction(s32 player, InputAction *outAction, float *outProgress)
{
	InputAction bestAction = ACTION_SKIP_CUTSCENE;
	float bestProgress = 0.0f;
	bool found = false;

	for (int i = 0; i < (int)(sizeof(kCutsceneSkipActions) / sizeof(kCutsceneSkipActions[0])); i++) {
		float p = 0.0f;
		const InputAction action = kCutsceneSkipActions[i];
		if (!actionInProgress(player, action, &p)) {
			continue;
		}

		if (!found || p > bestProgress || actionHeld(player, action)) {
			found = true;
			bestAction = action;
			bestProgress = p;
		}
	}

	if (!found) {
		return false;
	}

	if (outAction) {
		*outAction = bestAction;
	}
	if (outProgress) {
		*outProgress = bestProgress;
	}
	return true;
}

} /* namespace */

extern "C" void pdguiCutsceneSkipPromptRender(s32 winW, s32 winH)
{
	static float s_SmoothedProgress = 0.0f;

	if (!pdguiCutsceneSkipPromptShouldRender()) {
		s_SmoothedProgress = 0.0f;
		return;
	}

	InputAction action = ACTION_SKIP_CUTSCENE;
	float target = 0.0f;
	const s32 player = pdguiCutsceneSkipPromptPlayer();
	if (!findPromptAction(player, &action, &target) && s_SmoothedProgress <= 0.001f) {
		return;
	}

	float dt = ImGui::GetIO().DeltaTime;
	if (dt <= 0.0f || dt > 0.1f) {
		dt = 0.016f;
	}

	const float tau = (target > s_SmoothedProgress) ? 14.0f : 5.0f;
	s_SmoothedProgress += (target - s_SmoothedProgress) * (dt * tau);
	if (s_SmoothedProgress < 0.001f) {
		s_SmoothedProgress = 0.0f;
	} else if (s_SmoothedProgress > 0.999f && target >= 0.999f) {
		s_SmoothedProgress = 1.0f;
	}

	const float cx = (float)winW * 0.5f;
	const float y = (float)winH - pdguiScale(118.0f);
	pdguiDrawActionPromptCenteredWithHold(action, cx, y, "Skip", s_SmoothedProgress);
}
