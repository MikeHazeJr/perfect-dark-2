/**
 * pdgui_glyphs.cpp -- S312 contextual input-prompt helper.
 *
 * Resolves bindings from the active InputMappingContext stack and
 * produces short on-screen labels + optional ImGui pill rendering.
 * Keyboard/mouse vs gamepad is driven by actionmapGetLastDevice()
 * with the same 500 ms debounce as the rest of the action map.
 *
 * C++ TU (ImGui is C++-only); the public header exposes extern "C"
 * so game code in src/game/*.c can call the lookup helpers.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>

#include "imgui/imgui.h"
#include "pdgui_glyphs.h"
#include "pdgui_hold_ring.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "actionmap.h"

/* VK_* ordinals — mirrors the canonical enum in port/include/input.h.
 * We cannot include input.h directly because it pulls <PR/os_cont.h>
 * which transitively uses OSThread (libultra) and breaks C++ TUs. */
#define PDG_VK_RETURN       40
#define PDG_VK_ESCAPE       41
#define PDG_VK_BACKSPACE    42
#define PDG_VK_SPACE        44
#define PDG_VK_MINUS        45
#define PDG_VK_EQUALS       46
#define PDG_VK_LEFTBRACKET  47
#define PDG_VK_RIGHTBRACKET 48
#define PDG_VK_BACKSLASH    49
#define PDG_VK_SEMICOLON    51
#define PDG_VK_APOS         52
#define PDG_VK_GRAVE        53
#define PDG_VK_COMMA        54
#define PDG_VK_PERIOD       55
#define PDG_VK_SLASH        56
#define PDG_VK_F1           58
#define PDG_VK_F12          69
#define PDG_VK_INSERT       73
#define PDG_VK_HOME         74
#define PDG_VK_PAGEUP       75
#define PDG_VK_DELETE       76
#define PDG_VK_END          77
#define PDG_VK_PAGEDOWN     78
#define PDG_VK_RIGHT        79
#define PDG_VK_LEFT         80
#define PDG_VK_DOWN         81
#define PDG_VK_UP           82
#define PDG_VK_LCTRL        224
#define PDG_VK_LSHIFT       225
#define PDG_VK_LALT         226
#define PDG_VK_RCTRL        228
#define PDG_VK_RSHIFT       229

#define PDG_VK_MOUSE_BEGIN     512
#define PDG_VK_MOUSE_LEFT      (PDG_VK_MOUSE_BEGIN + 0)
#define PDG_VK_MOUSE_MIDDLE    (PDG_VK_MOUSE_BEGIN + 1)
#define PDG_VK_MOUSE_RIGHT     (PDG_VK_MOUSE_BEGIN + 2)
#define PDG_VK_MOUSE_X1        (PDG_VK_MOUSE_BEGIN + 3)
#define PDG_VK_MOUSE_X2        (PDG_VK_MOUSE_BEGIN + 4)
#define PDG_VK_MOUSE_WHEEL_UP  (PDG_VK_MOUSE_BEGIN + 5)
#define PDG_VK_MOUSE_WHEEL_DN  (PDG_VK_MOUSE_BEGIN + 6)

#define PDG_VK_JOY_BEGIN       519 /* VK_JOY_BEGIN == VK_MOUSE_BEGIN + 7 in input.h */
#define PDG_INPUT_MAX_CONTROLLER_BUTTONS 32
#define PDG_VK_TOTAL_COUNT (PDG_VK_JOY_BEGIN + 4 * PDG_INPUT_MAX_CONTROLLER_BUTTONS)

/* Forward-declare just the one input.c entry point we need. */
extern "C" const char *inputGetKeyName(s32 vk);

static inline bool vkIsKbm(u32 vk)
{
	return vk > 0 && vk < (u32)PDG_VK_JOY_BEGIN;
}

static inline bool vkIsGamepad(u32 vk)
{
	return vk >= (u32)PDG_VK_JOY_BEGIN && vk < (u32)PDG_VK_TOTAL_COUNT;
}

/* Priority-sorted iteration: higher priority first (g_ImcTextInput
 * priority 30 is on top, gameplay 0 at the bottom).  For a prompt the
 * correct source is "what the player would hit right now", so we pick
 * the FIRST active IMC that maps the action — which is the topmost
 * context currently consulted by actionmapDispatch. */
static InputMappingContext *const kAllImcs[] = {
	&g_ImcTextInput,
	&g_ImcDebugOverlay,
	&g_ImcPauseMenu,
	&g_ImcMenu,
	&g_ImcObserver,
	&g_ImcVehicle,
	&g_ImcGameplay,
};
static const int kNumImcs = (int)(sizeof(kAllImcs) / sizeof(kAllImcs[0]));

/* Find the primary VK bound to `action` for the requested device.
 * Returns 0 if none is found.
 *
 * Walks active IMCs in priority order.  For the topmost IMC that
 * carries the mapping, takes the first trigger matching `device`.
 * If no trigger matches, falls through to the next IMC so a menu-only
 * context without gamepad bindings still surfaces the gameplay one. */
static u32 findPrimaryVk(InputAction action, pdgui_glyph_device_e device)
{
	if ((int)action < 0 || (int)action >= ACTION_COUNT) {
		return 0;
	}

	for (int i = 0; i < kNumImcs; i++) {
		InputMappingContext *ctx = kAllImcs[i];
		if (!ctx || !ctx->active) continue;
		if (!ctx->has_mapping[action]) continue;

		const InputMapping *m = &ctx->mappings[action];
		for (int t = 0; t < m->num_triggers; t++) {
			u32 vk = m->triggers[t].vk;
			if (vk == 0) continue;
			if (device == PDGUI_GLYPH_DEVICE_GAMEPAD && vkIsGamepad(vk)) {
				return vk;
			}
			if (device == PDGUI_GLYPH_DEVICE_KBM && vkIsKbm(vk)) {
				return vk;
			}
		}
	}

	/* Device-specific search failed.  Try the OTHER device so prompts
	 * don't show "?" when only one device is bound. */
	const pdgui_glyph_device_e fallback =
		(device == PDGUI_GLYPH_DEVICE_GAMEPAD)
			? PDGUI_GLYPH_DEVICE_KBM
			: PDGUI_GLYPH_DEVICE_GAMEPAD;

	for (int i = 0; i < kNumImcs; i++) {
		InputMappingContext *ctx = kAllImcs[i];
		if (!ctx || !ctx->active) continue;
		if (!ctx->has_mapping[action]) continue;

		const InputMapping *m = &ctx->mappings[action];
		for (int t = 0; t < m->num_triggers; t++) {
			u32 vk = m->triggers[t].vk;
			if (vk == 0) continue;
			if (fallback == PDGUI_GLYPH_DEVICE_GAMEPAD && vkIsGamepad(vk)) {
				return vk;
			}
			if (fallback == PDGUI_GLYPH_DEVICE_KBM && vkIsKbm(vk)) {
				return vk;
			}
		}
	}

	return 0;
}

/* Translate a VK to a short display label.  Keeps 1-3 character labels
 * for the most common keys; longer ones for semantic keys that readers
 * recognise by name. */
static void vkShortLabel(u32 vk, char *out, s32 outlen)
{
	if (outlen <= 0) return;
	out[0] = '\0';

	if (vk == 0) {
		snprintf(out, outlen, "?");
		return;
	}

	/* Letters A..Z at VK 4..29 (SDL scancode order) */
	if (vk >= 4 && vk <= 29) {
		char c = (char)('A' + (vk - 4));
		snprintf(out, outlen, "%c", c);
		return;
	}

	/* Digits */
	if (vk >= 30 && vk <= 38) { snprintf(out, outlen, "%c", (char)('1' + (vk - 30))); return; }
	if (vk == 39)             { snprintf(out, outlen, "0"); return; }

	/* Mouse buttons */
	switch (vk) {
	case PDG_VK_MOUSE_LEFT:     snprintf(out, outlen, "LMB");  return;
	case PDG_VK_MOUSE_MIDDLE:   snprintf(out, outlen, "MMB");  return;
	case PDG_VK_MOUSE_RIGHT:    snprintf(out, outlen, "RMB");  return;
	case PDG_VK_MOUSE_X1:       snprintf(out, outlen, "M4");   return;
	case PDG_VK_MOUSE_X2:       snprintf(out, outlen, "M5");   return;
	case PDG_VK_MOUSE_WHEEL_UP: snprintf(out, outlen, "WhUp"); return;
	case PDG_VK_MOUSE_WHEEL_DN: snprintf(out, outlen, "WhDn"); return;
	}

	/* Named keyboard keys (none of these overlap with the letter/digit
	 * ranges already handled above). */
	switch (vk) {
	case PDG_VK_RETURN:      snprintf(out, outlen, "Enter");  return;
	case PDG_VK_ESCAPE:      snprintf(out, outlen, "Esc");    return;
	case PDG_VK_BACKSPACE:   snprintf(out, outlen, "BkSp");   return;
	case PDG_VK_SPACE:       snprintf(out, outlen, "Space");  return;
	case PDG_VK_MINUS:       snprintf(out, outlen, "-");      return;
	case PDG_VK_EQUALS:      snprintf(out, outlen, "=");      return;
	case PDG_VK_LEFTBRACKET: snprintf(out, outlen, "[");      return;
	case PDG_VK_RIGHTBRACKET:snprintf(out, outlen, "]");      return;
	case PDG_VK_BACKSLASH:   snprintf(out, outlen, "\\");     return;
	case PDG_VK_SEMICOLON:   snprintf(out, outlen, ";");      return;
	case PDG_VK_APOS:        snprintf(out, outlen, "'");      return;
	case PDG_VK_GRAVE:       snprintf(out, outlen, "`");      return;
	case PDG_VK_COMMA:       snprintf(out, outlen, ",");      return;
	case PDG_VK_PERIOD:      snprintf(out, outlen, ".");      return;
	case PDG_VK_SLASH:       snprintf(out, outlen, "/");      return;
	case PDG_VK_INSERT:      snprintf(out, outlen, "Ins");    return;
	case PDG_VK_HOME:        snprintf(out, outlen, "Home");   return;
	case PDG_VK_PAGEUP:      snprintf(out, outlen, "PgUp");   return;
	case PDG_VK_DELETE:      snprintf(out, outlen, "Del");    return;
	case PDG_VK_END:         snprintf(out, outlen, "End");    return;
	case PDG_VK_PAGEDOWN:    snprintf(out, outlen, "PgDn");   return;
	case PDG_VK_RIGHT:       snprintf(out, outlen, "Right");  return;
	case PDG_VK_LEFT:        snprintf(out, outlen, "Left");   return;
	case PDG_VK_DOWN:        snprintf(out, outlen, "Down");   return;
	case PDG_VK_UP:          snprintf(out, outlen, "Up");     return;
	case PDG_VK_LCTRL:       snprintf(out, outlen, "LCtrl");  return;
	case PDG_VK_LSHIFT:      snprintf(out, outlen, "LShft");  return;
	case PDG_VK_LALT:        snprintf(out, outlen, "LAlt");   return;
	case PDG_VK_RCTRL:       snprintf(out, outlen, "RCtrl");  return;
	case PDG_VK_RSHIFT:      snprintf(out, outlen, "RShft");  return;
	}

	/* Function keys F1..F12 at VK 58..69 (SDL scancode range) */
	if (vk >= (u32)PDG_VK_F1 && vk <= (u32)PDG_VK_F12) {
		snprintf(out, outlen, "F%u", (unsigned)(vk - PDG_VK_F1 + 1));
		return;
	}

	/* Gamepad buttons (offset 0..31 within a pad's slot range).
	 * Only player-1 gets a short glyph label; other players fall back
	 * to the full JOY<n>_<name> form from input.c. */
	if (vkIsGamepad(vk)) {
		u32 off = vk - (u32)PDG_VK_JOY_BEGIN;
		u32 pad = off / (u32)PDG_INPUT_MAX_CONTROLLER_BUTTONS;
		u32 btn = off % (u32)PDG_INPUT_MAX_CONTROLLER_BUTTONS;

		const s32 inputClass = actionmapGetLastInputClass();
		if (inputClass != ACTIONMAP_INPUT_CLASS_CONTROLLER &&
		    inputClass != ACTIONMAP_INPUT_CLASS_MKB) {
			switch ((int)btn) {
			case 22: snprintf(out, outlen, "Axis1-"); return;
			case 23: snprintf(out, outlen, "Axis1+"); return;
			case 24: snprintf(out, outlen, "Axis2-"); return;
			case 25: snprintf(out, outlen, "Axis2+"); return;
			case 26: snprintf(out, outlen, "Axis3-"); return;
			case 27: snprintf(out, outlen, "Axis3+"); return;
			case 28: snprintf(out, outlen, "Axis4-"); return;
			case 29: snprintf(out, outlen, "Axis4+"); return;
			case 30: snprintf(out, outlen, "Axis5+"); return;
			case 31: snprintf(out, outlen, "Axis6+"); return;
			default:
				snprintf(out, outlen, "Btn%u", (unsigned)(btn + 1));
				return;
			}
		}

		if (pad == 0) {
			switch ((int)btn) {
			case  0: snprintf(out, outlen, "A");     return;
			case  1: snprintf(out, outlen, "B");     return;
			case  2: snprintf(out, outlen, "X");     return;
			case  3: snprintf(out, outlen, "Y");     return;
			case  4: snprintf(out, outlen, "Back");  return;
			case  5: snprintf(out, outlen, "Guide"); return;
			case  6: snprintf(out, outlen, "Start"); return;
			case  7: snprintf(out, outlen, "LS");    return; /* left-stick click */
			case  8: snprintf(out, outlen, "RS");    return; /* right-stick click */
			case  9: snprintf(out, outlen, "LB");    return;
			case 10: snprintf(out, outlen, "RB");    return;
			case 11: snprintf(out, outlen, "D-Up");     return;
			case 12: snprintf(out, outlen, "D-Down");   return;
			case 13: snprintf(out, outlen, "D-Left");   return;
			case 14: snprintf(out, outlen, "D-Right");  return;
			case 22: snprintf(out, outlen, "LS-L");  return;
			case 23: snprintf(out, outlen, "LS-R");  return;
			case 24: snprintf(out, outlen, "LS-U");  return;
			case 25: snprintf(out, outlen, "LS-D");  return;
			case 26: snprintf(out, outlen, "RS-L");  return;
			case 27: snprintf(out, outlen, "RS-R");  return;
			case 28: snprintf(out, outlen, "RS-U");  return;
			case 29: snprintf(out, outlen, "RS-D");  return;
			case 30: snprintf(out, outlen, "LT");    return;
			case 31: snprintf(out, outlen, "RT");    return;
			}
		}

		/* Fallback — use the input.c full name ("JOY2_A" etc.) */
		const char *name = inputGetKeyName((s32)vk);
		snprintf(out, outlen, "%s", name ? name : "?");
		return;
	}

	/* Keyboard fallback for unnamed scancodes */
	const char *name = inputGetKeyName((s32)vk);
	if (name && name[0] && strncmp(name, "UNKNOWN", 7) != 0) {
		snprintf(out, outlen, "%s", name);
	} else {
		snprintf(out, outlen, "?");
	}
}

/* ============================================================
 * Public API
 * ============================================================ */

extern "C" pdgui_glyph_device_e pdguiGlyphGetDevice(void)
{
	return (actionmapGetLastDevice() == ACTIONMAP_DEVICE_GAMEPAD)
		? PDGUI_GLYPH_DEVICE_GAMEPAD
		: PDGUI_GLYPH_DEVICE_KBM;
}

extern "C" u32 pdguiGlyphGetPrimaryVk(InputAction action)
{
	return findPrimaryVk(action, pdguiGlyphGetDevice());
}

extern "C" s32 pdguiGlyphGetActionLabel(InputAction action, char *out, s32 outlen)
{
	if (!out || outlen <= 0) return 0;
	out[0] = '\0';

	u32 vk = findPrimaryVk(action, pdguiGlyphGetDevice());
	if (vk == 0) {
		snprintf(out, outlen, "?");
		return 0;
	}
	vkShortLabel(vk, out, outlen);
	return 1;
}

/* ============================================================
 * Draw helpers
 * ============================================================ */

/* Shared draw routine: key pill + optional label (no hold indicator). */
static f32 drawPromptInternal(InputAction action, f32 x, f32 y, const char *label)
{
	ImDrawList *fg = ImGui::GetForegroundDrawList();
	if (!fg) return 0.0f;

	const float scale = pdguiScale(1.0f);

	char keyText[24];
	pdguiGlyphGetActionLabel(action, keyText, (s32)sizeof(keyText));

	/* Key pill: accent-bordered dark box with centered text. */
	const ImVec2 ts = ImGui::CalcTextSize(keyText);
	const float padX = 8.0f * scale;
	const float padY = 4.0f * scale;
	const float pillW = ts.x + padX * 2.0f;
	const float pillH = ts.y + padY * 2.0f;
	const float rounding = 3.0f * scale;

	const ImU32 bgCol     = IM_COL32(8, 14, 24, 230);
	const ImU32 borderCol = pdguiGetTitleGlow();
	const ImU32 keyFg     = IM_COL32(235, 245, 255, 240);
	const ImU32 labelFg   = IM_COL32(210, 225, 240, 230);

	fg->AddRectFilled(ImVec2(x, y), ImVec2(x + pillW, y + pillH), bgCol, rounding);
	fg->AddRect(ImVec2(x, y), ImVec2(x + pillW, y + pillH), borderCol, rounding, 0, 1.2f * scale);
	fg->AddText(ImVec2(x + padX, y + padY), keyFg, keyText);

	f32 width = pillW;

	if (label && label[0]) {
		const float gap = 6.0f * scale;
		const ImVec2 lts = ImGui::CalcTextSize(label);
		fg->AddText(ImVec2(x + pillW + gap, y + padY), labelFg, label);
		width += gap + lts.x;
	}

	return width;
}

extern "C" f32 pdguiDrawActionPrompt(InputAction action, f32 x, f32 y, const char *label)
{
	return drawPromptInternal(action, x, y, label);
}

extern "C" f32 pdguiDrawActionPromptCentered(InputAction action, f32 cx, f32 y, const char *label)
{
	/* Measure first so we can shift x by half of drawn width. */
	char keyText[24];
	pdguiGlyphGetActionLabel(action, keyText, (s32)sizeof(keyText));

	const float scale = pdguiScale(1.0f);
	const ImVec2 ts = ImGui::CalcTextSize(keyText);
	const float pillW = ts.x + 16.0f * scale;
	float total = pillW;
	if (label && label[0]) {
		const ImVec2 lts = ImGui::CalcTextSize(label);
		total += 6.0f * scale + lts.x;
	}

	return drawPromptInternal(action, cx - total * 0.5f, y, label);
}

extern "C" f32 pdguiDrawActionPromptCenteredWithHold(InputAction action, f32 cx, f32 y,
		const char *verb, f32 hold_progress)
{
	ImDrawList *fg = ImGui::GetForegroundDrawList();
	if (!fg) return 0.0f;

	const float scale = pdguiScale(1.0f);

	char keyText[24];
	pdguiGlyphGetActionLabel(action, keyText, (s32)sizeof(keyText));

	/* hold_progress < 0: tap / "Press" prompts (vehicles) — no hold ring. */
	const bool pressMode = (hold_progress < 0.0f);
	const char *holdPrefix = pressMode ? "Press " : "Hold ";
	const float gap = 6.0f * scale;
	const ImVec2 holdSz = ImGui::CalcTextSize(holdPrefix);
	const ImVec2 keyTs = ImGui::CalcTextSize(keyText);
	const float padX = 8.0f * scale;
	const float padY = 4.0f * scale;
	const float pillW = keyTs.x + padX * 2.0f;
	const float pillH = keyTs.y + padY * 2.0f;
	const float rounding = 3.0f * scale;

	ImVec2 verbSz(0.0f, 0.0f);
	if (verb && verb[0]) {
		verbSz = ImGui::CalcTextSize(verb);
	}

	const float totalW = holdSz.x + gap + pillW + gap + verbSz.x;
	const float x0 = cx - totalW * 0.5f;

	const ImU32 bgCol   = IM_COL32(8, 14, 24, 230);
	const ImU32 borderCol = pdguiGetTitleGlow();
	const ImU32 keyFg   = IM_COL32(235, 245, 255, 240);
	const ImU32 labelFg = IM_COL32(210, 225, 240, 230);

	fg->AddText(ImVec2(x0, y + padY), labelFg, holdPrefix);

	const float pillX = x0 + holdSz.x + gap;
	fg->AddRectFilled(ImVec2(pillX, y), ImVec2(pillX + pillW, y + pillH), bgCol, rounding);
	fg->AddRect(ImVec2(pillX, y), ImVec2(pillX + pillW, y + pillH), borderCol, rounding, 0, 1.2f * scale);
	fg->AddText(ImVec2(pillX + padX, y + padY), keyFg, keyText);

	if (verb && verb[0]) {
		fg->AddText(ImVec2(pillX + pillW + gap, y + padY), labelFg, verb);
	}

	if (!pressMode) {
		float p = hold_progress;
		if (p > 1.0f) {
			p = 1.0f;
		}
		if (p < 0.0f) {
			p = 0.0f;
		}
		pdguiDrawHoldProgressRingAroundBox(fg, pillX, y, pillW, pillH, p);
	}

	return totalW;
}
