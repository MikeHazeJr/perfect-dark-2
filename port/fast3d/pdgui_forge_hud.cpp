/**
 * pdgui_forge_hud.cpp -- Forge mode HUD overlay (Phase F0 shell).
 *
 * Renders only when a forge session is active.  Shows the current sub-mode
 * (NORMAL / FREEFLY) plus a freefly reticle and camera-readout.  Intentional
 * placeholder panels reserve the screen real-estate that later phases will
 * populate (catalog browser left, properties panel right).
 *
 * IMPORTANT: This is a C++ TU.  Do NOT include types.h -- the project's
 * `#define bool s32` breaks C++.  Forge module is reached via its
 * extern "C" interface in game/forgemode.h.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>

#include "imgui/imgui.h"
#include "pdgui_forge.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"

extern "C" {
#include "game/forgemode.h"
}

/* Local mirror of struct coord for the readout.  Same memory layout as the
 * game-side struct (three contiguous f32).  Avoids pulling in types.h here. */
struct forgehud_coord {
	f32 x, y, z;
};

/* ============================================================
 * Helpers
 * ============================================================ */

static const char *forgehudModeLabel(forge_session_state_t s)
{
	switch (s) {
	case FORGE_SESSION_FREEFLY: return "FREEFLY";
	case FORGE_SESSION_NORMAL:  return "NORMAL";
	default:                    return "INACTIVE";
	}
}

static ImU32 forgehudModeColor(forge_session_state_t s)
{
	switch (s) {
	case FORGE_SESSION_FREEFLY: return IM_COL32(120, 220, 255, 230); /* cyan -- editor cursor */
	case FORGE_SESSION_NORMAL:  return IM_COL32(180, 230, 140, 230); /* green -- play mode  */
	default:                    return IM_COL32(180, 180, 180, 200);
	}
}

static void forgehudDrawReticle(ImDrawList *dl, float cx, float cy, float scale)
{
	const ImU32 col = IM_COL32(200, 240, 255, 230);
	const float arm = 10.0f * scale;
	const float gap =  3.0f * scale;
	const float th  =  1.5f * scale;

	dl->AddLine(ImVec2(cx - arm, cy), ImVec2(cx - gap, cy), col, th);
	dl->AddLine(ImVec2(cx + gap, cy), ImVec2(cx + arm, cy), col, th);
	dl->AddLine(ImVec2(cx, cy - arm), ImVec2(cx, cy - gap), col, th);
	dl->AddLine(ImVec2(cx, cy + gap), ImVec2(cx, cy + arm), col, th);
	dl->AddCircleFilled(ImVec2(cx, cy), 1.5f * scale, col, 8);
}

/* ============================================================
 * Public API
 * ============================================================ */

void pdguiForgeHudRender(s32 winW, s32 winH)
{
	if (!forgeSessionIsActive()) {
		return;
	}

	const forge_session_state_t state = forgeGetSessionState();
	const float scale = pdguiScale(1.0f);

	ImDrawList *fg = ImGui::GetForegroundDrawList();

	/* ---- Top-left: mode indicator badge ---- */
	{
		const float pad = 14.0f * scale;
		const float badgeH = 28.0f * scale;
		const float badgeW = 220.0f * scale;
		const float x = pad;
		const float y = pad;

		fg->AddRectFilled(ImVec2(x, y), ImVec2(x + badgeW, y + badgeH),
				IM_COL32(8, 14, 24, 200), 4.0f * scale);
		fg->AddRect(ImVec2(x, y), ImVec2(x + badgeW, y + badgeH),
				forgehudModeColor(state), 4.0f * scale, 0, 1.5f * scale);

		char label[64];
		snprintf(label, sizeof(label), "FORGE  --  %s", forgehudModeLabel(state));
		const ImVec2 ts = ImGui::CalcTextSize(label);
		fg->AddText(ImVec2(x + (badgeW - ts.x) * 0.5f,
		                   y + (badgeH - ts.y) * 0.5f),
				IM_COL32(240, 248, 255, 240), label);
	}

	if (state != FORGE_SESSION_FREEFLY) {
		return;
	}

	/* ---- Center: freefly placement reticle ---- */
	forgehudDrawReticle(fg, (float)winW * 0.5f, (float)winH * 0.5f, scale);

	/* ---- Bottom-left: camera readout (pos + look + speed scale) ---- */
	{
		struct forgehud_coord pos = { 0.0f, 0.0f, 0.0f };
		forgeGetCameraPos((struct coord *)&pos);
		const f32 yaw   = forgeGetCameraYawDeg();
		const f32 pitch = forgeGetCameraPitchDeg();
		const f32 sx    = forgeGetCurrentSpeedScale();

		const char *speedTag =
			(sx >= 2.5f) ? "BOOST" :
			(sx <= 0.4f) ? "PRECISION" :
			               "NORMAL";

		char line1[96];
		char line2[96];
		snprintf(line1, sizeof(line1), "pos  %7.0f  %7.0f  %7.0f", pos.x, pos.y, pos.z);
		snprintf(line2, sizeof(line2), "look yaw %6.1f  pitch %6.1f  spd %s",
				yaw, pitch, speedTag);

		const float pad = 14.0f * scale;
		const float lh = ImGui::GetTextLineHeight();
		const float rowH = lh + 4.0f * scale;
		const float boxW = 320.0f * scale;
		const float boxH = rowH * 2.0f + 10.0f * scale;
		const float x = pad;
		const float y = (float)winH - boxH - pad;

		fg->AddRectFilled(ImVec2(x, y), ImVec2(x + boxW, y + boxH),
				IM_COL32(8, 14, 24, 180), 4.0f * scale);
		fg->AddRect(ImVec2(x, y), ImVec2(x + boxW, y + boxH),
				IM_COL32(60, 100, 150, 200), 4.0f * scale, 0, 1.0f * scale);

		fg->AddText(ImVec2(x + 8.0f * scale, y + 5.0f * scale),
				IM_COL32(220, 235, 250, 230), line1);
		fg->AddText(ImVec2(x + 8.0f * scale, y + 5.0f * scale + rowH),
				IM_COL32(180, 210, 240, 220), line2);
	}

	/* ---- Right edge: placeholder Object Catalog panel (F1) ---- */
	{
		const float pad = 14.0f * scale;
		const float panelW = 240.0f * scale;
		const float panelH = (float)winH * 0.55f;
		const float x = (float)winW - panelW - pad;
		const float y = pad + 40.0f * scale; /* below mode badge area */

		fg->AddRectFilled(ImVec2(x, y), ImVec2(x + panelW, y + panelH),
				IM_COL32(8, 14, 24, 110), 4.0f * scale);
		fg->AddRect(ImVec2(x, y), ImVec2(x + panelW, y + panelH),
				IM_COL32(60, 100, 150, 160), 4.0f * scale, 0, 1.0f * scale);

		const char *title = "Object Catalog";
		const char *hint  = "(F1: not yet implemented)";
		const ImVec2 tts = ImGui::CalcTextSize(title);
		fg->AddText(ImVec2(x + (panelW - tts.x) * 0.5f, y + 8.0f * scale),
				IM_COL32(140, 200, 240, 220), title);
		const ImVec2 hts = ImGui::CalcTextSize(hint);
		fg->AddText(ImVec2(x + (panelW - hts.x) * 0.5f,
		                   y + 8.0f * scale + tts.y + 4.0f * scale),
				IM_COL32(120, 140, 170, 180), hint);
	}

	/* ---- Bottom-right: forge controls reminder ---- */
	{
		const char *help =
			"F7  toggle mode\n"
			"WASD  move    Q/E  down/up\n"
			"Mouse  look   Shift  boost   Ctrl  precision";
		const float pad = 14.0f * scale;
		const ImVec2 ts = ImGui::CalcTextSize(help);
		const float boxW = ts.x + 16.0f * scale;
		const float boxH = ts.y + 12.0f * scale;
		const float x = (float)winW - boxW - pad;
		const float y = (float)winH - boxH - pad;

		fg->AddRectFilled(ImVec2(x, y), ImVec2(x + boxW, y + boxH),
				IM_COL32(8, 14, 24, 180), 4.0f * scale);
		fg->AddRect(ImVec2(x, y), ImVec2(x + boxW, y + boxH),
				IM_COL32(60, 100, 150, 200), 4.0f * scale, 0, 1.0f * scale);
		fg->AddText(ImVec2(x + 8.0f * scale, y + 6.0f * scale),
				IM_COL32(220, 235, 250, 230), help);
	}
}
