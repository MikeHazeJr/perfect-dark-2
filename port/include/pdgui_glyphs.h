/**
 * pdgui_glyphs.h — S312 contextual input-prompt helper.
 *
 * Maps InputAction values to the currently-bound key or gamepad button
 * and renders compact on-screen prompts (e.g. "[E] Use", "[A] Use").
 * Auto-detects keyboard/mouse vs gamepad from the actionmap's
 * last-device tracker and re-resolves bindings when they change.
 *
 * The header is C-compatible; game code (src/game/*.c) can call the
 * lookup helpers.  The draw entry point requires an active ImGui
 * frame and is implemented in pdgui_glyphs.cpp.
 */
#ifndef PDGUI_GLYPHS_H
#define PDGUI_GLYPHS_H

#include <PR/ultratypes.h>
#include "actionmap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	PDGUI_GLYPH_DEVICE_KBM     = 0,
	PDGUI_GLYPH_DEVICE_GAMEPAD = 1,
} pdgui_glyph_device_e;

/**
 * Returns the preferred glyph device for the current frame.
 * Mirrors actionmapGetLastDevice() with a 500 ms debounce so prompts
 * don't flicker on incidental stick noise.
 */
pdgui_glyph_device_e pdguiGlyphGetDevice(void);

/**
 * Returns the primary VK (virtkey enum) bound to `action` for the
 * device returned by pdguiGlyphGetDevice().  Walks the active IMC
 * stack in priority order so the prompt reflects whatever context is
 * on top (gameplay IMC in-world, menu IMC in a dialog, etc.).
 * Returns 0 if no binding exists for the active device.
 */
u32 pdguiGlyphGetPrimaryVk(InputAction action);

/**
 * Writes a short human-friendly label for the primary binding of
 * `action` into `out` (null-terminated).  Examples:
 *   - Keyboard/mouse: "E", "Space", "LMB", "RMB", "MMB", "Enter"
 *   - Gamepad:        "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "←"
 * Falls back to "?" if no binding exists (so UI never shows blank).
 * Returns 1 if a real binding was found, 0 on fallback.
 */
s32 pdguiGlyphGetActionLabel(InputAction action, char *out, s32 outlen);

/**
 * Draw a compact prompt at (x, y) in screen pixels:
 *   [KEY]  or  [KEY] Label
 * Uses ImGui's foreground drawlist — must be called during an ImGui
 * frame.  Theme accent + text colour drive the pill styling.
 * Returns the total drawn width in pixels (so callers can stack
 * multiple prompts horizontally).
 *
 * If `label` is NULL or empty, only the key pill is drawn.
 */
f32 pdguiDrawActionPrompt(InputAction action, f32 x, f32 y, const char *label);

/**
 * Draw a prompt horizontally-centered at (cx, y).  Same as
 * pdguiDrawActionPrompt but x-centred on cx.  Returns drawn width.
 */
f32 pdguiDrawActionPromptCentered(InputAction action, f32 cx, f32 y, const char *label);

/**
 * Same as pdguiDrawActionPromptCentered, but also draws a fill / charge
 * indicator beneath the pill driven by `hold_progress` (0.0 -> 1.0).
 *
 *   hold_progress == 0  -> identical to the non-hold variant.
 *   0 < hold_progress < 1 -> a horizontal bar grows under the pill from
 *                            left to right, colour ramps toward accent.
 *   hold_progress >= 1  -> bar is fully drawn in a brighter "ready"
 *                          colour to signal the hold action will fire.
 *
 * Used by the interact prompt to telegraph "hold to interact" while the
 * tap meaning (reload) remains the short-press action.  Returns drawn
 * width (matches the non-hold variant so callers can stack prompts).
 */
f32 pdguiDrawActionPromptCenteredWithHold(InputAction action, f32 cx, f32 y,
		const char *label, f32 hold_progress);

#ifdef __cplusplus
}
#endif

#endif /* PDGUI_GLYPHS_H */
