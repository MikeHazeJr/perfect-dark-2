/**
 * pdgui_model_preview.cpp -- High-level model preview panel for ImGui (P6)
 *
 * Self-contained ImGui panel wrapping pdgui_charpreview's FBO render system.
 * Handles event-driven re-render, idle rotation, fallback placeholder,
 * and palette-derived styling.
 *
 * The underlying pdgui_charpreview.c manages the actual FBO creation and
 * GBI-phase model rendering. This module is the UI-level consumer.
 *
 * IMPORTANT: Do NOT include types.h -- it #defines bool as s32, breaking C++.
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <PR/ultratypes.h>

#include "imgui/imgui.h"
#include "pdgui_model_preview.h"
#include "pdgui_charpreview.h"
#include "pdgui_style.h"
#include "assetcatalog.h"
#include "system.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* =========================================================================
 * State — tracks last requested model to avoid redundant FBO re-renders
 * ========================================================================= */

#define PREV_ID_LEN 64

static char s_LastHeadId[PREV_ID_LEN] = "";
static char s_LastBodyId[PREV_ID_LEN] = "";
static f32  s_IdleAngle = 0.0f;

/* =========================================================================
 * Color helpers
 * ========================================================================= */

static inline ImU32 MpCol(u32 rgba)
{
    u8 r = (u8)((rgba >> 24) & 0xFFu);
    u8 g = (u8)((rgba >> 16) & 0xFFu);
    u8 b = (u8)((rgba >>  8) & 0xFFu);
    u8 a = (u8)((rgba >>  0) & 0xFFu);
    return IM_COL32(r, g, b, a);
}

/* =========================================================================
 * Body/Head display name helpers
 * ========================================================================= */

static const char *getDisplayName(const char *catalog_id)
{
    if (!catalog_id || !catalog_id[0]) return "None";

    /* Strip namespace prefix for display: "base:dark_combat" → "dark_combat" */
    const char *colon = strchr(catalog_id, ':');
    const char *name = colon ? colon + 1 : catalog_id;

    /* Capitalize first letter for display */
    return name;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

extern "C" {

ModelPreviewOpts pdguiModelPreviewDefaultOpts(void)
{
    ModelPreviewOpts opts;
    memset(&opts, 0, sizeof(opts));
    opts.showBodyName  = 1;
    opts.showHeadName  = 0;
    opts.idleRotation  = 1;
    opts.idleRotSpeed  = 0.3f;
    opts.cornerRadius  = 4.0f;
    opts.bgColor       = 0;  /* 0 = derive from palette */
    opts.borderColor   = 0;
    return opts;
}

void pdguiModelPreviewDraw(const char *head_id, const char *body_id,
                            f32 x, f32 y, f32 w, f32 h,
                            const ModelPreviewOpts *opts)
{
    ModelPreviewOpts o;
    if (opts) {
        o = *opts;
    } else {
        o = pdguiModelPreviewDefaultOpts();
    }

    ImDrawList *dl = ImGui::GetWindowDrawList();

    /* Determine colors */
    ImU32 bgCol, borderCol;
    if (o.bgColor) {
        bgCol = MpCol(o.bgColor);
    } else {
        bgCol = pdguiPalImU32(PDPAL_BODYBG, 240);
    }
    if (o.borderColor) {
        borderCol = MpCol(o.borderColor);
    } else {
        borderCol = pdguiPalImU32(PDPAL_BORDER1, 200);
    }

    float cr = o.cornerRadius;

    /* Background frame */
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), bgCol, cr);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), borderCol, cr, 0, 2.0f);

    /* Check if model selection changed */
    bool selChanged = false;
    if (head_id && strcmp(head_id, s_LastHeadId) != 0) selChanged = true;
    if (body_id && strcmp(body_id, s_LastBodyId) != 0) selChanged = true;
    if (!head_id && s_LastHeadId[0]) selChanged = true;
    if (!body_id && s_LastBodyId[0]) selChanged = true;

    if (selChanged) {
        /* Store new selection */
        if (head_id) snprintf(s_LastHeadId, PREV_ID_LEN, "%s", head_id);
        else s_LastHeadId[0] = '\0';
        if (body_id) snprintf(s_LastBodyId, PREV_ID_LEN, "%s", body_id);
        else s_LastBodyId[0] = '\0';

        /* Reset idle rotation on selection change */
        s_IdleAngle = 0.0f;

        /* Request render from the FBO system */
        pdguiCharPreviewRequest(
            head_id ? head_id : "",
            body_id ? body_id : "");
    }

    /* Idle rotation */
    if (o.idleRotation && !selChanged) {
        float dt = ImGui::GetIO().DeltaTime;
        s_IdleAngle += o.idleRotSpeed * dt;
        if (s_IdleAngle > (float)(2.0 * M_PI)) {
            s_IdleAngle -= (float)(2.0 * M_PI);
        }
        pdguiCharPreviewSetRotY(s_IdleAngle);

        /* Re-request with same model to apply new rotation.
         * The charpreview system is smart enough to detect the same
         * head/body and only update the rotation. */
        pdguiCharPreviewRequest(
            s_LastHeadId[0] ? s_LastHeadId : "",
            s_LastBodyId[0] ? s_LastBodyId : "");
    }

    /* Draw preview content */
    float pad = 2.0f;
    float contentX = x + pad;
    float contentY = y + pad;
    float contentW = w - pad * 2.0f;
    float contentH = h - pad * 2.0f;

    /* Reserve space for labels at bottom */
    float labelH = 0.0f;
    if (o.showBodyName) labelH += 16.0f;
    if (o.showHeadName) labelH += 14.0f;
    contentH -= labelH;

    u32 texId = pdguiCharPreviewGetTextureId();
    if (texId != 0 && pdguiCharPreviewIsReady()) {
        /* Display the FBO texture.
         * FBO textures are vertically flipped — UV0=(0,1), UV1=(1,0). */
        dl->AddImage((ImTextureID)(uintptr_t)texId,
                     ImVec2(contentX, contentY),
                     ImVec2(contentX + contentW, contentY + contentH),
                     ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    } else {
        /* Fallback placeholder: silhouette */
        float cx = contentX + contentW * 0.5f;
        float cy = contentY + contentH * 0.4f;
        float headR = contentW * 0.15f;

        ImU32 silCol = pdguiPalImU32(PDPAL_BORDER1, 160);

        /* Head circle */
        dl->AddCircleFilled(ImVec2(cx, cy), headR, silCol, 24);

        /* Body trapezoid */
        dl->AddRectFilled(
            ImVec2(cx - contentW * 0.25f, cy + headR * 0.8f),
            ImVec2(cx + contentW * 0.25f, cy + headR * 0.8f + contentH * 0.25f),
            silCol, headR * 0.5f);

        /* "Loading..." or "No model" text */
        const char *msg = (head_id || body_id) ? "Loading..." : "No model";
        ImVec2 msgSize = ImGui::CalcTextSize(msg);
        dl->AddText(
            ImVec2(cx - msgSize.x * 0.5f, contentY + contentH * 0.75f),
            pdguiPalImU32(PDPAL_ITEM_DISABLED, 180), msg);
    }

    /* Labels */
    if (o.showBodyName && body_id && body_id[0]) {
        const char *bname = getDisplayName(body_id);
        ImVec2 sz = ImGui::CalcTextSize(bname);
        float lx = x + (w - sz.x) * 0.5f;
        float ly = y + h - labelH;
        dl->AddText(ImVec2(lx, ly),
                    pdguiPalImU32(PDPAL_ITEM_UNFOCUSED, 200), bname);
    }

    if (o.showHeadName && head_id && head_id[0]) {
        const char *hname = getDisplayName(head_id);
        ImVec2 sz = ImGui::CalcTextSize(hname);
        float lx = x + (w - sz.x) * 0.5f;
        float ly = y + h - 14.0f;
        dl->AddText(ImVec2(lx, ly),
                    pdguiPalImU32(PDPAL_ITEM_DISABLED, 160), hname);
    }
}

void pdguiModelPreviewInvalidate(void)
{
    s_LastHeadId[0] = '\0';
    s_LastBodyId[0] = '\0';
    s_IdleAngle = 0.0f;
}

} /* extern "C" */
