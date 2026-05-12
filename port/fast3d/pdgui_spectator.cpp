/**
 * pdgui_spectator.cpp -- spectator overlay HUD.
 *
 * Top-strip with subset / member / camera; bottom-left controls strip
 * with D-pad / R3 / Hold-Y hints; right-side scoreboard listing
 * participants in the current subset (focused row highlighted with
 * TitleGlow). Stop button bottom-right.
 *
 * The overlay also handles observer action-map camera controls documented
 * in spectator.h. Mouse delta drives free-fly yaw + pitch when free_fly
 * is active.
 */

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "imgui/imgui.h"

#include "pdgui_spectator.h"
#include "pdgui_style.h"

extern "C" {
#include "actionmap.h"
#include "inputctx.h"
#include "spectator.h"
#include "social.h"
#include "system.h"
}

extern "C" s32 pdguiSpectatorOverlayActive(void)
{
	return spectatorIsActive();
}

static void drawHints(s32 winW, s32 winH)
{
	const float pad = 18.0f;
	ImDrawList *dl = ImGui::GetForegroundDrawList();

	const char *hints[] = {
		"PgUp / PgDn -- cycle team subset",
		"Left / Right -- cycle member",
		"Tab -- 1st / 3rd person",
		"Hold R -- free-fly camera",
		"Esc -- stop spectating",
	};

	const float line_h = 18.0f;
	const float box_w = 320.0f;
	const float box_h = (float)(sizeof(hints) / sizeof(hints[0])) * line_h + 16.0f;
	const float x1 = pad;
	const float y1 = (float)winH - pad - box_h;
	const float x2 = x1 + box_w;
	const float y2 = y1 + box_h;

	dl->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2),
	                   IM_COL32(8, 12, 24, 200), 8.0f);
	dl->AddRect(ImVec2(x1, y1), ImVec2(x2, y2),
	             pdguiImU32TitleGlow(180), 8.0f, 0, 1.0f);

	for (size_t i = 0; i < sizeof(hints)/sizeof(hints[0]); i++) {
		dl->AddText(ImVec2(x1 + 12.0f, y1 + 8.0f + (float)i * line_h),
		             IM_COL32(200, 210, 220, 255), hints[i]);
	}
}

static void drawTopStrip(s32 winW)
{
	const spectator_state_t *s = spectatorGet();
	const spectator_participant_t *focused = spectatorFocused();
	const float pad = 18.0f;
	const float strip_h = 56.0f;

	ImDrawList *dl = ImGui::GetForegroundDrawList();
	dl->AddRectFilled(ImVec2(pad, pad), ImVec2((float)winW - pad, pad + strip_h),
	                   IM_COL32(8, 12, 24, 200), 8.0f);
	dl->AddRect(ImVec2(pad, pad), ImVec2((float)winW - pad, pad + strip_h),
	             pdguiImU32TitleGlow(180), 8.0f, 0, 1.5f);

	char left[160];
	snprintf(left, sizeof(left), "Spectating  |  %s",
	          spectatorSubsetName(s->subset));
	dl->AddText(ImVec2(pad + 16.0f, pad + 12.0f),
	             pdguiImU32TitleGlow(255), left);

	char center[192];
	if (focused) {
		snprintf(center, sizeof(center), "%s  (score %d)",
		          focused->name[0] ? focused->name : "?",
		          (int)focused->score);
	} else if (s->late_join_pending) {
		snprintf(center, sizeof(center), "Joining match...");
	} else {
		snprintf(center, sizeof(center), "(no focus)");
	}
	const float center_w = ImGui::CalcTextSize(center).x;
	dl->AddText(ImVec2(((float)winW - center_w) * 0.5f, pad + 14.0f),
	             IM_COL32(220, 230, 240, 255), center);

	const char *cam =
		(s->camera == SPECTATOR_CAM_FIRST_PERSON) ? "1st-person" :
		(s->camera == SPECTATOR_CAM_FREE_FLY)     ? "free-fly"   : "3rd-person";
	const float right_w = ImGui::CalcTextSize(cam).x + 16.0f;
	dl->AddText(ImVec2((float)winW - pad - right_w, pad + 14.0f),
	             pdguiImU32TintInfo(255), cam);
}

static void drawScoreboard(s32 winW, s32 winH)
{
	const spectator_state_t *s = spectatorGet();
	const float pad = 18.0f;
	const float w = 260.0f;
	const float h = (float)winH * 0.5f;
	const float x1 = (float)winW - pad - w;
	const float y1 = 96.0f;
	const float x2 = x1 + w;
	const float y2 = y1 + h;

	ImDrawList *dl = ImGui::GetForegroundDrawList();
	dl->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2),
	                   IM_COL32(8, 12, 24, 200), 8.0f);
	dl->AddRect(ImVec2(x1, y1), ImVec2(x2, y2),
	             pdguiImU32TitleGlow(180), 8.0f, 0, 1.0f);

	float row_y = y1 + 12.0f;
	dl->AddText(ImVec2(x1 + 12.0f, row_y),
	             pdguiImU32TitleGlow(255),
	             spectatorSubsetName(s->subset));
	row_y += 22.0f;

	for (s32 i = 0; i < SPECTATOR_MAX_PARTICIPANTS; i++) {
		const spectator_participant_t *p = &s->participants[i];
		if (!p->in_use) continue;
		bool in_subset = false;
		switch (s->subset) {
			case SPECTATOR_SUBSET_PLAYERS:    in_subset = !p->is_bot; break;
			case SPECTATOR_SUBSET_ALL:        in_subset = true; break;
			case SPECTATOR_SUBSET_TEAM_RED:   in_subset = (p->team == 0); break;
			case SPECTATOR_SUBSET_TEAM_GREEN: in_subset = (p->team == 1); break;
			case SPECTATOR_SUBSET_TEAM_BLUE:  in_subset = (p->team == 2); break;
			case SPECTATOR_SUBSET_TEAM_GOLD:  in_subset = (p->team == 3); break;
			default: break;
		}
		if (!in_subset) continue;
		const ImU32 col = (i == s->focus_idx)
		                    ? pdguiImU32TitleGlow(255)
		                    : IM_COL32(200, 210, 220, 255);
		char row[96];
		snprintf(row, sizeof(row), "%s%s  %d",
		          p->is_bot ? "[bot] " : "",
		          p->name[0] ? p->name : "?",
		          (int)p->score);
		dl->AddText(ImVec2(x1 + 12.0f, row_y), col, row);
		row_y += 18.0f;
		if (row_y > y2 - 12.0f) break;
	}
}

static void handleKeyboard(void)
{
	/* s036-05 (c036): use the single suppression predicate instead of
	 * the raw ImGui WantCaptureKeyboard. The predicate folds in menu
	 * push, focus loss, and the 50ms focus-settle window; ImGui's gate
	 * only covered the first case and missed the others. */
	if (gameplayInputSuppressed()) return;
	ImGuiIO &io = ImGui::GetIO();

	if (actionPressed(0, ACTION_OBSERVER_SUBSET_PREV))   spectatorCycleSubset(-1);
	if (actionPressed(0, ACTION_OBSERVER_SUBSET_NEXT))   spectatorCycleSubset(+1);
	if (actionPressed(0, ACTION_OBSERVER_MEMBER_PREV))   spectatorCycleMember(-1);
	if (actionPressed(0, ACTION_OBSERVER_MEMBER_NEXT))   spectatorCycleMember(+1);
	if (actionPressed(0, ACTION_OBSERVER_CAMERA_TOGGLE)) spectatorToggleFirstPerson();
	if (actionPressed(0, ACTION_OBSERVER_FREEFLY))       spectatorBeginFreeFly();
	if (actionReleased(0, ACTION_OBSERVER_FREEFLY))      spectatorEndFreeFly();
	if (actionPressed(0, ACTION_OBSERVER_STOP))          spectatorStop();

	if (spectatorGet()->free_fly_active) {
		const f32 dt = io.DeltaTime;
		f32 dx = 0.0f, dy = 0.0f, dz = 0.0f;
		f32 move_x = 0.0f;
		f32 move_y = 0.0f;
		actionAxis(0, ACTION_AXIS_MOVE_X, &move_x, &move_y);
		dx += move_x;
		dz -= move_y;
		if (actionHeld(0, ACTION_OBSERVER_DESCEND)) dy -= 1.0f;
		if (actionHeld(0, ACTION_OBSERVER_ASCEND)) dy += 1.0f;
		const f32 dyaw   = io.MouseDelta.x * 0.005f;
		const f32 dpitch = io.MouseDelta.y * 0.005f;
		spectatorFreeFlyInput(dx, dy, dz, dyaw, dpitch, dt);
	}
}

extern "C" void pdguiSpectatorRender(s32 winW, s32 winH)
{
	if (!spectatorIsActive()) return;

	handleKeyboard();
	drawTopStrip(winW);
	drawHints(winW, winH);
	drawScoreboard(winW, winH);
}
