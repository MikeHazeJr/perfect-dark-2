/**
 * pdgui_hold_ring.h -- Shared circular hold-progress ring (ImGui foreground).
 *
 * Used anywhere we show "hold this button" affordance with the same track +
 * accent arc as the in-world interact prompt. Progress is 0..1 from the
 * actionmap (e.g. actionHoldProgress) or game-normalized values.
 *
 * C++ / ImGui only -- include from port fast3d TUs.
 */
#ifndef PDGUI_HOLD_RING_H
#define PDGUI_HOLD_RING_H

#include <PR/ultratypes.h>

#include "imgui/imgui.h"

/**
 * Draw a dim full-circle track + clockwise progress arc around the bounding
 * box of a key pill (or any rect). Arc starts at top (-90 deg) and sweeps
 * clockwise. Uses pdguiScale + theme accent colours for consistency.
 *
 * progress <= 0: track only. 0 < progress < 1: track + accent arc.
 * progress >= 1: track + bright "complete" arc colour.
 */
void pdguiDrawHoldProgressRingAroundBox(ImDrawList *dl, f32 boxMinX, f32 boxMinY,
		f32 boxW, f32 boxH, f32 progress);

#endif /* PDGUI_HOLD_RING_H */
