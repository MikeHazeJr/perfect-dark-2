/**
 * pdgui_hold_ring.cpp -- Shared hold-progress ring (see pdgui_hold_ring.h).
 */

#include <math.h>

#include "imgui/imgui.h"
#include "pdgui_hold_ring.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"

void pdguiDrawHoldProgressRingAroundBox(ImDrawList *dl, f32 boxMinX, f32 boxMinY,
		f32 boxW, f32 boxH, f32 progress)
{
	if (!dl) {
		return;
	}

	float p = progress;
	if (p > 1.0f) p = 1.0f;
	if (p < 0.0f) p = 0.0f;

	const float scale = pdguiScale(1.0f);
	const float PI = 3.14159265f;

	const float pillCx = boxMinX + boxW * 0.5f;
	const float pillCy = boxMinY + boxH * 0.5f;
	const float ringR = fmaxf(boxW, boxH) * 0.5f + 11.0f * scale;
	const float thickness = 5.5f * scale;
	const int segTrack = 64;
	const float a0 = -PI * 0.5f;

	const ImU32 trackCol = IM_COL32(20, 28, 44, 200);
	const ImU32 borderCol = pdguiGetTitleGlow();

	dl->PathClear();
	dl->PathArcTo(ImVec2(pillCx, pillCy), ringR, a0, a0 + 2.0f * PI, segTrack);
	dl->PathStroke(trackCol, 0, thickness);

	if (p > 0.001f) {
		const float a1 = a0 + p * 2.0f * PI;
		const int pseg = (int)fmaxf(12.0f, (float)segTrack * p + 2.0f);
		const ImU32 arcCol = (p >= 1.0f) ? IM_COL32(180, 240, 255, 255) : borderCol;
		dl->PathClear();
		dl->PathArcTo(ImVec2(pillCx, pillCy), ringR, a0, a1, pseg);
		dl->PathStroke(arcCol, 0, thickness);
	}
}
