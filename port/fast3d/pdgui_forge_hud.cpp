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
#include <initializer_list>
#include <utility>

#include "imgui/imgui.h"
#include "pdgui_forge.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_glyphs.h"

extern "C" {
#include "game/forgemode.h"
#include "forge/forge_core.h"
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
		snprintf(label, sizeof(label), "THE GRID  --  %s", forgehudModeLabel(state));
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

	/* ---- Center: ghost placement preview when a catalog pick is pending ---- */
	forge_placement_state_t *pp = forgeGetPlacement();
	if (pp && pp->ghost_active) {
		const float cx = (float)winW * 0.5f;
		const float cy = (float)winH * 0.5f;
		const ImU32 valid_col   = IM_COL32(120, 240, 140, 180);
		const ImU32 invalid_col = IM_COL32(240, 100, 100, 200);
		const ImU32 col = pp->ghost_valid ? valid_col : invalid_col;
		/* Semi-transparent square indicator */
		const float half = 32.0f * scale;
		fg->AddRect(ImVec2(cx - half, cy - half),
		            ImVec2(cx + half, cy + half),
		            col, 6.0f * scale, 0, 2.0f * scale);
		fg->AddCircle(ImVec2(cx, cy), half + 4.0f * scale, col, 24, 1.0f * scale);
		/* Label: object name + valid/invalid tag */
		char lbl[96];
		snprintf(lbl, sizeof(lbl), "%s%s",
				pp->pending_catalog_id[0] ? pp->pending_catalog_id : "(ghost)",
				pp->ghost_valid ? "" : "  [INVALID]");
		const ImVec2 ts = ImGui::CalcTextSize(lbl);
		fg->AddRectFilled(ImVec2(cx - ts.x * 0.5f - 6.0f * scale, cy + half + 4.0f * scale),
		                  ImVec2(cx + ts.x * 0.5f + 6.0f * scale, cy + half + 6.0f * scale + ts.y),
		                  IM_COL32(8, 14, 24, 220), 3.0f * scale);
		fg->AddText(ImVec2(cx - ts.x * 0.5f, cy + half + 5.0f * scale),
		            col, lbl);
	}

	/* ---- Top-center: grid snap indicator when grid snap is on ---- */
	{
		forge_editor_state_t *ed = forgeGetEditor();
		if (ed && ed->snap_grid_enabled && ed->grid_size > 0.0f) {
			char msg[64];
			snprintf(msg, sizeof(msg), "GRID SNAP  %.0fu", (double)ed->grid_size);
			const ImVec2 ts = ImGui::CalcTextSize(msg);
			const float boxW = ts.x + 18.0f * scale;
			const float boxH = ts.y + 10.0f * scale;
			const float x = ((float)winW - boxW) * 0.5f;
			const float y = 14.0f * scale;
			fg->AddRectFilled(ImVec2(x, y), ImVec2(x + boxW, y + boxH),
					IM_COL32(8, 14, 24, 180), 3.0f * scale);
			fg->AddRect(ImVec2(x, y), ImVec2(x + boxW, y + boxH),
					IM_COL32(120, 220, 255, 200), 3.0f * scale, 0, 1.0f * scale);
			fg->AddText(ImVec2(x + 9.0f * scale, y + 5.0f * scale),
					IM_COL32(220, 240, 255, 230), msg);

			/* Lightweight on-screen grid cue: 5x5 crosses spread around the
			 * center reticle at screen-space grid spacing (scaled for
			 * readability so authors get a clear rhythm of grid cells). */
			const float spacing = 48.0f * scale;
			const ImU32 gcol = IM_COL32(100, 180, 220, 70);
			const float arm = 3.0f * scale;
			for (int gy = -2; gy <= 2; ++gy) {
				for (int gx = -2; gx <= 2; ++gx) {
					if (gx == 0 && gy == 0) continue;
					float px = (float)winW * 0.5f + (float)gx * spacing;
					float py = (float)winH * 0.5f + (float)gy * spacing;
					fg->AddLine(ImVec2(px - arm, py), ImVec2(px + arm, py), gcol, 1.0f);
					fg->AddLine(ImVec2(px, py - arm), ImVec2(px, py + arm), gcol, 1.0f);
				}
			}
		}
	}

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

	/* ---- Bottom-right: Grid controls reminder (S312 glyph-driven) ----
	 * Rows of [KEY] Label pills that auto-follow the user's bindings and
	 * switch between KBM / gamepad short-labels per actionmapGetLastDevice. */
	{
		const float pillH   = ImGui::CalcTextSize("A").y + 8.0f * scale;
		const float rowGap  = 4.0f * scale;
		const float colGap  = 12.0f * scale;
		const float pad     = 14.0f * scale;
		const float rowH    = pillH + rowGap;
		const int   rowCount = 4;
		const float boxH    = rowH * (float)rowCount + 8.0f * scale;

		/* Measure row widths so we can size the background tightly. */
		auto measureRow = [&](std::initializer_list<std::pair<InputAction, const char *>> items) {
			float w = 0.0f;
			for (auto &p : items) {
				char key[24];
				pdguiGlyphGetActionLabel(p.first, key, (s32)sizeof(key));
				float pillW = ImGui::CalcTextSize(key).x + 16.0f * scale;
				float labelW = (p.second && p.second[0])
					? ImGui::CalcTextSize(p.second).x + 6.0f * scale
					: 0.0f;
				w += pillW + labelW + colGap;
			}
			return w;
		};

		float row0W = measureRow({
			{ ACTION_FORGE_TOGGLE, "toggle mode" },
			{ ACTION_FORGE_ASCEND, "ascend" },
			{ ACTION_FORGE_DESCEND, "descend" },
		});
		float row1W = measureRow({
			{ ACTION_FORGE_BOOST, "boost" },
			{ ACTION_FORGE_PRECISION, "precision" },
		});
		float row2W = measureRow({
			{ ACTION_MENU_TAB_PREV, "prev tab" },
			{ ACTION_MENU_TAB_NEXT, "next tab" },
		});
		float row3W = measureRow({
			{ ACTION_USE, "select / place" },
			{ ACTION_CANCEL_USE, "cancel" },
		});

		float contentW = row0W;
		if (row1W > contentW) contentW = row1W;
		if (row2W > contentW) contentW = row2W;
		if (row3W > contentW) contentW = row3W;
		const float boxW = contentW + 16.0f * scale;
		const float x = (float)winW - boxW - pad;
		const float y = (float)winH - boxH - pad;

		fg->AddRectFilled(ImVec2(x, y), ImVec2(x + boxW, y + boxH),
				IM_COL32(8, 14, 24, 180), 4.0f * scale);
		fg->AddRect(ImVec2(x, y), ImVec2(x + boxW, y + boxH),
				pdguiGetTitleGlow(), 4.0f * scale, 0, 1.0f * scale);

		auto drawRow = [&](float ry, std::initializer_list<std::pair<InputAction, const char *>> items) {
			float rx = x + 8.0f * scale;
			for (auto &p : items) {
				rx += pdguiDrawActionPrompt(p.first, rx, ry, p.second);
				rx += colGap;
			}
		};

		drawRow(y + 4.0f * scale + rowH * 0.0f, {
			{ ACTION_FORGE_TOGGLE, "toggle mode" },
			{ ACTION_FORGE_ASCEND, "ascend" },
			{ ACTION_FORGE_DESCEND, "descend" },
		});
		drawRow(y + 4.0f * scale + rowH * 1.0f, {
			{ ACTION_FORGE_BOOST, "boost" },
			{ ACTION_FORGE_PRECISION, "precision" },
		});
		drawRow(y + 4.0f * scale + rowH * 2.0f, {
			{ ACTION_MENU_TAB_PREV, "prev tab" },
			{ ACTION_MENU_TAB_NEXT, "next tab" },
		});
		drawRow(y + 4.0f * scale + rowH * 3.0f, {
			{ ACTION_USE, "select / place" },
			{ ACTION_CANCEL_USE, "cancel" },
		});
	}
}
