/**
 * pdgui_interact_prompt.cpp -- S311 in-world interact prompt renderer.
 *
 * Reads the game-side `g_InteractProp` tracker (populated each frame by
 * `propFindForInteract` / `objTestForInteract` / `doorTestForInteract`) and
 * draws a compact "[E] Pick up" style pill near the reticle so the player
 * knows the USE key will do something before pressing it.
 *
 * The label comes from `propInteractPromptLabel()` (src/game/prop.c) which
 * classifies the target as weapon / door / terminal / generic object.  The
 * key glyph follows the active input device via pdgui_glyphs.h (S312), so
 * controller users see "[A] Pick up" without any conditional UI code here.
 *
 * Rendered near the HUD reticle; hidden automatically whenever:
 *   - no interact prop is currently tracked
 *   - the player is dead, in a cutscene, or the gameplay HUD is suppressed
 *   - pdguiNewFrame/pdguiRender skipped the ImGui overlay (see pdgui_backend:
 *     solo gameplay used to hit the "no menus open" early-return before S397)
 *
 * IMPORTANT: C++ TU -- do NOT include types.h (#define bool s32 breaks C++).
 * Game-side accessors declared extern "C" in their respective headers.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>

#include "imgui/imgui.h"
#include "pdgui.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_glyphs.h"
#include "pdgui_interact_prompt.h"
#include "pdmain.h"

extern "C" {
#include "actionmap.h"

/* Forward-declared game accessors -- declared in src/include/game/prop.h but
 * that header pulls in types.h which breaks C++ compilation.  The signatures
 * here match the game side exactly. */
const char *propInteractPromptLabel(void);
s32 propInteractPromptHoldThresholdMs(void);
s32 propInteractPromptPreferPressStyle(void);
}

/**
 * Render the prompt.  Called from pdguiRender's gameplay HUD path in
 * pdgui_backend.cpp AFTER the reticle so the pill floats just beneath it.
 *
 * No-op if no interact target is currently tracked (label == NULL), or if
 * any non-gameplay input context is on top (menus, pause, debug overlay) —
 * B-189: the prompt belongs to the gameplay HUD layer and must not bleed
 * through to ImGui menu overlays.  pdguiIsActive() returns non-zero whenever
 * inputCtxGetTop() != &g_CtxGameplay, which is the same authority predicate
 * used by the gameplay HUD layer.
 */
extern "C" void pdguiInteractPromptRender(s32 winW, s32 winH)
{
	/* The smoothed ring value persists across calls. On any early-return
	 * (menus opened mid-hold, CI intro blocks, prop label flips NULL because
	 * the player moved out of the interact cone), we still need to decay the
	 * smoothed value so a re-entry starts visually fresh instead of flashing
	 * from a stale 0.4 or 0.7. The decay rate matches the fall tau used in
	 * the live render path below (tau=5.0f). */
	static float s_IpHoldRingSmoothed = 0.0f;

	auto decay_smoothed_to_zero = []() {
		float dt = ImGui::GetIO().DeltaTime;
		if (dt <= 0.0f || dt > 0.1f) {
			dt = 0.016f;
		}
		s_IpHoldRingSmoothed -= s_IpHoldRingSmoothed * (dt * 5.0f);
		if (s_IpHoldRingSmoothed < 0.001f) {
			s_IpHoldRingSmoothed = 0.0f;
		}
	};

	if (pdguiIsActive()) { decay_smoothed_to_zero(); return; }

	if (pdguiCiIntroBlocksInteractPrompt()) {
		decay_smoothed_to_zero();
		return;
	}

	const char *label = propInteractPromptLabel();
	if (!label) { decay_smoothed_to_zero(); return; }

	const s32 actionPlayer = pdmainGetInteractPromptActionPlayer();

	/* Float just below the centred reticle so the player's eye picks it up
	 * without leaving the crosshair.  offsetY tuned to clear the reticle
	 * pips + a few px of breathe. */
	float cx = (float)winW * 0.5f;
	float cy = (float)winH * 0.5f + pdguiScale(28.0f);

	if (propInteractPromptPreferPressStyle()) {
		pdguiDrawActionPromptCenteredWithHold(ACTION_USE, cx, cy, label, -1.0f);
		return;
	}

	/* Radial fill: player slot + per-target hold ms (settings + propInteract*
	 * extras) match bondmove propGetActionUseHoldThresholdMs().
	 * B-221.2: smooth toward target so release does not snap the ring off;
	 * actionHoldProgress adds a short post-release grace + post-consume pin. */
	const s32 holdMs = propInteractPromptHoldThresholdMs();
	f32 target = actionHoldProgress(actionPlayer, ACTION_USE, holdMs);
	/* PC: bondmove can synthesize A while USE is physically held before consume;
	 * actionHoldProgress sometimes stayed at 0 — drive the ring from raw down time. */
	if (target <= 0.0f && actionHeld(actionPlayer, ACTION_USE)
			&& !actionHoldConsumed(actionPlayer, ACTION_USE)) {
		u32 t0 = actionHoldPressStartMs(actionPlayer, ACTION_USE);
		if (t0 != 0) {
			s32 elapsed = (s32)(SDL_GetTicks() - t0);
			if (elapsed < 0) {
				elapsed = 0;
			}
			target = (f32)elapsed / (f32)holdMs;
			if (target > 1.0f) {
				target = 1.0f;
			}
		}
	}
	float dt = ImGui::GetIO().DeltaTime;
	if (dt <= 0.0f || dt > 0.1f) {
		dt = 0.016f;
	}
	float tau = (target > s_IpHoldRingSmoothed) ? 14.0f : 5.0f;
	s_IpHoldRingSmoothed += (target - s_IpHoldRingSmoothed) * (dt * tau);
	if (s_IpHoldRingSmoothed < 0.001f) {
		s_IpHoldRingSmoothed = 0.0f;
	} else if (s_IpHoldRingSmoothed > 0.999f && target >= 0.999f) {
		s_IpHoldRingSmoothed = 1.0f;
	}

	pdguiDrawActionPromptCenteredWithHold(ACTION_USE, cx, cy, label, s_IpHoldRingSmoothed);
}
