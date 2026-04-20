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

extern "C" {
#include "actionmap.h"

/* Forward-declared game accessors -- declared in src/include/game/prop.h but
 * that header pulls in types.h which breaks C++ compilation.  The signatures
 * here match the game side exactly. */
const char *propInteractPromptLabel(void);
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
/* Hold-vs-tap discriminator threshold for ACTION_USE.  Must stay in
 * lockstep with BMOVE_USE_HOLD_THRESHOLD_MS in src/game/bondmove.c -- if
 * one moves the other has to follow or the prompt fill will desync from
 * the moment the interact actually fires. */
#define INTERACT_HOLD_THRESHOLD_MS 250

extern "C" void pdguiInteractPromptRender(s32 winW, s32 winH)
{
	if (pdguiIsActive()) return;

	const char *label = propInteractPromptLabel();
	if (!label) return;

	/* Float just below the centred reticle so the player's eye picks it up
	 * without leaving the crosshair.  offsetY tuned to clear the reticle
	 * pips + a few px of breathe. */
	float cx = (float)winW * 0.5f;
	float cy = (float)winH * 0.5f + pdguiScale(28.0f);

	/* While ACTION_USE is held but the hold has not yet fired, surface a
	 * fill bar so the player understands "hold to interact" vs "tap to
	 * reload".  Once the hold has fired (consumed) we keep the bar at 1.0
	 * for the brief moment the button stays down so the prompt does not
	 * flicker back to empty mid-hold. */
	f32 progress = actionHoldProgress(0, ACTION_USE, INTERACT_HOLD_THRESHOLD_MS);
	if (actionHoldConsumed(0, ACTION_USE)) progress = 1.0f;

	pdguiDrawActionPromptCenteredWithHold(ACTION_USE, cx, cy, label, progress);
}
