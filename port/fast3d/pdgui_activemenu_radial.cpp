/**
 * pdgui_activemenu_radial.cpp -- Themed ImGui overlay for the active menu wheel.
 *
 * Draws on the foreground draw list so gameplay input routing is unchanged.
 * Uses palette colors from pdgui_style / theme (same as other PD UI).
 */

#include <float.h>
#include <string.h>

#include <PR/ultratypes.h>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "pdgui_activemenu_radial.h"
#include "pdgui_scaling.h"
#include "pdgui_style.h"

/* constants.h AMSLOT* / AMSLOTFLAG* -- avoid including game headers in C++ */
#define PD_AMSLOTMODE_DEFAULT 0
#define PD_AMSLOTMODE_FOCUSED 1
#define PD_AMSLOTMODE_CURRENT 2
#define PD_AMSLOTFLAG_CURRENT 0x02u
#define PD_AMSLOTFLAG_ACTIVE  0x08u
#define PD_AMSLOTFLAG_NOAMMO  0x10u

static ImU32 pdguiU32ToImCol(u32 c, f32 alphaMul)
{
	u8 r = (u8)((c >> 24) & 0xff);
	u8 g = (u8)((c >> 16) & 0xff);
	u8 b = (u8)((c >> 8) & 0xff);
	u8 a = (u8)(c & 0xff);
	a = (u8)ImClamp((int)((f32)a * alphaMul + 0.5f), 0, 255);
	return IM_COL32(r, g, b, a);
}

extern "C" void pdguiActiveMenuRadialRender(s32 winW, s32 winH)
{
	if (!pdguiActiveMenuShouldSkipLegacyWheel()) {
		return;
	}

	const f32 alphaFrac = pdguiActiveMenuRadialGetAlphaFrac();
	if (alphaFrac <= 0.01f) {
		return;
	}

	const u32 *pal = (const u32 *)pdguiGetActivePaletteRaw();
	const ImU32 bodyBg = pdguiU32ToImCol(pal[PDPAL_BODYBG], alphaFrac);
	const ImU32 borderHi = pdguiU32ToImCol(pal[PDPAL_BORDER1], alphaFrac);
	const ImU32 focusBg = pdguiU32ToImCol(pal[PDPAL_FOCUS_BG], alphaFrac * 0.95f);
	const ImU32 textDef = pdguiU32ToImCol(pal[PDPAL_ITEM_UNFOCUSED], alphaFrac);
	const ImU32 textHi = pdguiU32ToImCol(pal[PDPAL_ITEM_FOCUSED], alphaFrac);
	const ImU32 textWarn = pdguiGetTextWarning();

	ImDrawList *dl = ImGui::GetForegroundDrawList();
	const float sw = pdguiScale(1.0f);
	ImFont *font = ImGui::GetFont();
	/* N64 slotwidth maps to very wide pills at HD resolutions; tighten toward
	 * the diamond center and cap width. Text uses menu-tier size vs default. */
	const float radialLabelPx = pdguiScale(22.0f);
	const float kPillHalfFrac = 0.44f;
	const float maxPillHalf = pdguiScale(86.0f);

	float ox[4];
	float oy[4];
	pdguiActiveMenuRadialGetOuterDiamondScreen(ox, oy, winW, winH);

	float ix[4];
	float iy[4];
	{
		const float tmp2 = (ox[1] - ox[3]) / 8.0f;
		const float tmp1 = (oy[2] - oy[0]) / 8.0f;
		ix[0] = ox[0];
		iy[0] = oy[0] + tmp1;
		ix[1] = ox[1] - tmp2;
		iy[1] = oy[1];
		ix[2] = ox[2];
		iy[2] = oy[2] - tmp1;
		ix[3] = ox[3] + tmp2;
		iy[3] = oy[3];
	}

	{
		ImVec2 o[4] = { ImVec2(ox[0], oy[0]), ImVec2(ox[1], oy[1]), ImVec2(ox[2], oy[2]), ImVec2(ox[3], oy[3]) };
		dl->AddConvexPolyFilled(o, 4, IM_COL32(20, 20, 20, (int)(79 * alphaFrac)));

		ImVec2 inner[4] = { ImVec2(ix[0], iy[0]), ImVec2(ix[1], iy[1]), ImVec2(ix[2], iy[2]), ImVec2(ix[3], iy[3]) };
		dl->AddConvexPolyFilled(inner, 4, bodyBg);
	}

	{
		ImVec2 o[5] = { ImVec2(ox[0], oy[0]), ImVec2(ox[1], oy[1]), ImVec2(ox[2], oy[2]), ImVec2(ox[3], oy[3]), ImVec2(ox[0], oy[0]) };
		dl->AddPolyline(o, 5, borderHi, ImDrawFlags_Closed, 1.5f * sw);
	}

	const s32 slotHalfW = pdguiActiveMenuRadialGetSlotWidthPx(winW) / 2;
	const float pillHalfW = ImMin((float)slotHalfW * kPillHalfFrac, maxPillHalf);
	const s32 playercount = pdguiActiveMenuRadialGetLocalPlayerCount();
	const float padTop = ImMax(5.0f * sw, radialLabelPx * 0.42f);
	const float padBot = (playercount >= 2) ? ImMax(3.0f * sw, radialLabelPx * 0.32f)
						: ImMax(5.0f * sw, radialLabelPx * 0.42f);

	for (s32 slot = 0; slot < 9; slot++) {
		float cx;
		float cy;
		u32 flags = 0;
		char label[32];
		s32 mode = PD_AMSLOTMODE_DEFAULT;

		pdguiActiveMenuRadialQuerySlot(slot, winW, winH, &cx, &cy, &flags, label, &mode);

		const s32 isCenter = (slot == 4);
		if (isCenter && pdguiActiveMenuRadialIsCramped()) {
			continue;
		}

		if (!isCenter && label[0] == '\0') {
			continue;
		}

		const float halfw = pillHalfW;
		const ImVec2 rmin(cx - halfw + sw, cy - padTop + sw);
		const ImVec2 rmax(cx + halfw - sw, cy + padBot - sw);

		ImU32 fill = bodyBg;
		if (pdguiActiveMenuRadialIsEditMode()) {
			fill = IM_COL32(0, 0, 0, (int)(111 * alphaFrac));
		} else if (mode == PD_AMSLOTMODE_FOCUSED) {
			fill = focusBg;
		} else if (mode == PD_AMSLOTMODE_CURRENT || (flags & PD_AMSLOTFLAG_CURRENT)) {
			fill = IM_COL32(0, 0, 0, (int)(140 * alphaFrac));
		}

		if (flags & PD_AMSLOTFLAG_NOAMMO) {
			fill = IM_COL32(0, 0, 0, (int)(80 * alphaFrac));
		}

		dl->AddRectFilled(rmin, rmax, fill, 2.0f * sw);

		ImU32 borderCol = IM_COL32(255, 0, 0, (int)(79 * alphaFrac));
		if (flags & PD_AMSLOTFLAG_NOAMMO) {
			borderCol = IM_COL32(0, 0, 0, (int)(60 * alphaFrac));
		} else if (mode == PD_AMSLOTMODE_CURRENT || (flags & PD_AMSLOTFLAG_CURRENT)) {
			borderCol = IM_COL32(255, 255, 255, (int)(143 * alphaFrac));
		}

		dl->AddRect(rmin, rmax, borderCol, 2.0f * sw, 0, 1.25f * sw);

		ImU32 textCol = textDef;
		if (mode == PD_AMSLOTMODE_CURRENT || (flags & PD_AMSLOTFLAG_CURRENT)) {
			textCol = textHi;
		}
		if (flags & PD_AMSLOTFLAG_NOAMMO) {
			textCol = pdguiU32ToImCol(textWarn, alphaFrac);
		}
		if (flags & PD_AMSLOTFLAG_ACTIVE) {
			textCol = IM_COL32(255, 175, 143, (int)(255 * alphaFrac));
		}

		ImVec2 ts = font->CalcTextSizeA(radialLabelPx, FLT_MAX, 0.0f, label, NULL);
		dl->AddText(font, radialLabelPx, ImVec2(cx - ts.x * 0.5f, cy - ts.y * 0.5f), textCol, label);
	}

	{
		float scx;
		float scy;
		pdguiActiveMenuRadialGetSelectionCenterScreen(&scx, &scy, winW, winH);

		s32 halfwidth = (s32)(pillHalfW + 0.5f);
		s32 above = (playercount >= 2) ? 5 : 6;
		s32 below = (playercount >= 2) ? 3 : 6;

		if (pdguiActiveMenuRadialGetSlotNum() == 4 && pdguiActiveMenuRadialIsCramped()) {
			halfwidth = 1;
			above = 2;
			below = 0;
		}

		u8 rr, gg, bb, aa;
		pdguiActiveMenuRadialGetSelectionPulseRGBA(&rr, &gg, &bb, &aa);
		const ImU32 selCol = IM_COL32(rr, gg, bb, (int)((f32)aa * alphaFrac));

		const float ax = (float)above * sw;
		const float bx = (float)below * sw;
		const float hw = (float)halfwidth;

		dl->AddRectFilled(ImVec2(scx - hw, scy - ax), ImVec2(scx + hw + 1.0f, scy - ax + 1.0f), selCol);
		dl->AddRectFilled(ImVec2(scx - hw, scy + bx), ImVec2(scx + hw + 1.0f, scy + bx + 1.0f), selCol);
		dl->AddRectFilled(ImVec2(scx - hw, scy - ax + 1.0f), ImVec2(scx - hw + 1.0f, scy + bx), selCol);
		dl->AddRectFilled(ImVec2(scx + hw, scy - ax + 1.0f), ImVec2(scx + hw + 1.0f, scy + bx), selCol);
	}
}
