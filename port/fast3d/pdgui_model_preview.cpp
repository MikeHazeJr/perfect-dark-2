/**
 * pdgui_model_preview.cpp -- High-level model preview panel for ImGui.
 *
 * Self-contained ImGui panel wrapping pdgui_charpreview's FBO render system.
 * Handles event-driven re-render, idle rotation, fallback placeholder,
 * palette-derived styling, and (Batch 0) type-parameterized model kinds:
 *
 *     CHARACTER  -- head+body pair (existing path, preserved)
 *     WEAPON     -- single weapon file
 *     VEHICLE    -- single vehicle file
 *     PROP       -- single prop file
 *
 * The underlying pdgui_charpreview.c manages the actual FBO creation and
 * GBI-phase model rendering.  This module is the UI-level consumer and is
 * the only place Batch 10 (training 3D) callers need to reach into.
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
 * State -- tracks last requested (kind, id1, id2) tuple to avoid redundant
 * FBO re-renders.  A single panel of state is adequate for every current
 * caller because all call sites show at most one preview at a time.  If a
 * future UI needs multiple panels with independent idle rotations, promote
 * this to a small named-slot table keyed by caller string.
 * ========================================================================= */

#define PREV_ID_LEN 64

static s32  s_LastKind                  = -1;
static char s_LastId1[PREV_ID_LEN]      = "";
static char s_LastId2[PREV_ID_LEN]      = "";
static f32  s_IdleAngle                 = 0.0f;

/* =========================================================================
 * Kind <-> preview-type mapping
 * ========================================================================= */

static PdguiPreviewType kindToPreviewType(ModelPreviewKind kind)
{
    switch (kind) {
    case PDGUI_MP_WEAPON:  return PDGUI_PREVIEW_WEAPON;
    case PDGUI_MP_VEHICLE: return PDGUI_PREVIEW_VEHICLE;
    case PDGUI_MP_PROP:    return PDGUI_PREVIEW_PROP;
    case PDGUI_MP_CHARACTER:
    default:               return PDGUI_PREVIEW_CHARACTER;
    }
}

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
 * Display-name helper
 * ========================================================================= */

static const char *getDisplayName(const char *catalog_id)
{
    if (!catalog_id || !catalog_id[0]) return "None";

    /* Strip namespace prefix for display: "base:dark_combat" -> "dark_combat" */
    const char *colon = strchr(catalog_id, ':');
    const char *name = colon ? colon + 1 : catalog_id;

    return name;
}

/* =========================================================================
 * Internal: issue the preview request for a given kind + ids.  Wraps the
 * CHARACTER vs single-filenum dispatch so the panel draw function does not
 * have to think about preview-type mapping.
 * ========================================================================= */

static void requestPreview(ModelPreviewKind kind, const char *id1, const char *id2)
{
    if (kind == PDGUI_MP_CHARACTER) {
        pdguiCharPreviewRequest(id1 ? id1 : "",
                                 id2 ? id2 : "");
    } else {
        pdguiCharPreviewRequestEx(kindToPreviewType(kind),
                                   id1 ? id1 : "",
                                   NULL);
    }
}

/* =========================================================================
 * Core draw implementation -- shared by pdguiModelPreviewDraw (character
 * shortcut) and pdguiModelPreviewDrawEx (any kind).
 * ========================================================================= */

static void drawPanelImpl(ModelPreviewKind kind,
                           const char *id1, const char *id2,
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

    /* Palette-derived frame colors */
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

    /* Detect selection change (kind OR id1 OR id2) */
    bool selChanged = false;
    if ((s32)kind != s_LastKind) selChanged = true;
    if (id1 && strcmp(id1, s_LastId1) != 0) selChanged = true;
    if (id2 && strcmp(id2, s_LastId2) != 0) selChanged = true;
    if (!id1 && s_LastId1[0]) selChanged = true;
    if (!id2 && s_LastId2[0]) selChanged = true;

    /* B-253 follow-up: pass the panel's display aspect to the FBO renderer
     * BEFORE any request fires this frame, so the projection matches the
     * pane and the model un-stretches correctly when ImGui scales the
     * (square) FBO texture into a non-square display rect.  The aspect
     * uses the model-content area only (height minus label rows), which
     * is what ImGui::Image actually fills. */
    {
        float labelHForAspect = 0.0f;
        if (o.showBodyName) labelHForAspect += 16.0f;
        if (o.showHeadName) labelHForAspect += 14.0f;
        float pad   = 2.0f;
        float aspW  = w - pad * 2.0f;
        float aspH  = h - pad * 2.0f - labelHForAspect;
        float aspect = (aspH > 0.5f) ? (aspW / aspH) : 1.0f;
        pdguiCharPreviewSetAspect(aspect);
    }

    if (selChanged) {
        s_LastKind = (s32)kind;
        if (id1) snprintf(s_LastId1, PREV_ID_LEN, "%s", id1);
        else     s_LastId1[0] = '\0';
        if (id2) snprintf(s_LastId2, PREV_ID_LEN, "%s", id2);
        else     s_LastId2[0] = '\0';

        /* Reset idle rotation on selection change */
        s_IdleAngle = 0.0f;

        requestPreview(kind, id1, id2);
    }

    /* Idle rotation -- only when the selection is static */
    if (o.idleRotation && !selChanged) {
        float dt = ImGui::GetIO().DeltaTime;
        s_IdleAngle += o.idleRotSpeed * dt;
        if (s_IdleAngle > (float)(2.0 * M_PI)) {
            s_IdleAngle -= (float)(2.0 * M_PI);
        }
        pdguiCharPreviewSetRotY(s_IdleAngle);

        /* Re-request with same model so the new rotation takes effect.
         * The charpreview request path is cheap when the model is
         * already loaded. */
        requestPreview(kind,
                        s_LastId1[0] ? s_LastId1 : NULL,
                        s_LastId2[0] ? s_LastId2 : NULL);
    }

    /* Content area (inside the frame, above the labels) */
    float pad = 2.0f;
    float contentX = x + pad;
    float contentY = y + pad;
    float contentW = w - pad * 2.0f;
    float contentH = h - pad * 2.0f;

    float labelH = 0.0f;
    if (o.showBodyName) labelH += 16.0f;
    if (o.showHeadName) labelH += 14.0f;
    contentH -= labelH;

    u32 texId = pdguiCharPreviewGetTextureId();
    if (texId != 0 && pdguiCharPreviewIsReady()) {
        /* FBO textures are vertically flipped -- UV0=(0,1), UV1=(1,0). */
        dl->AddImage((ImTextureID)(uintptr_t)texId,
                     ImVec2(contentX, contentY),
                     ImVec2(contentX + contentW, contentY + contentH),
                     ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    } else {
        /* Fallback placeholder: silhouette or icon */
        float cx = contentX + contentW * 0.5f;
        float cy = contentY + contentH * 0.4f;
        float r  = contentW * 0.15f;

        ImU32 silCol = pdguiPalImU32(PDPAL_BORDER1, 160);

        if (kind == PDGUI_MP_CHARACTER) {
            /* Head circle + body trapezoid */
            dl->AddCircleFilled(ImVec2(cx, cy), r, silCol, 24);
            dl->AddRectFilled(
                ImVec2(cx - contentW * 0.25f, cy + r * 0.8f),
                ImVec2(cx + contentW * 0.25f, cy + r * 0.8f + contentH * 0.25f),
                silCol, r * 0.5f);
        } else {
            /* Neutral box icon for weapon / vehicle / prop */
            dl->AddRect(ImVec2(cx - contentW * 0.25f, cy - contentH * 0.15f),
                        ImVec2(cx + contentW * 0.25f, cy + contentH * 0.15f),
                        silCol, r * 0.3f, 0, 2.0f);
        }

        const char *msg = (id1 || id2) ? "Loading..." : "No model";
        ImVec2 msgSize = ImGui::CalcTextSize(msg);
        dl->AddText(
            ImVec2(cx - msgSize.x * 0.5f, contentY + contentH * 0.75f),
            pdguiPalImU32(PDPAL_ITEM_DISABLED, 180), msg);
    }

    /* Labels -- choose based on kind */
    const char *primaryLabel = NULL;
    const char *secondaryLabel = NULL;
    if (kind == PDGUI_MP_CHARACTER) {
        if (o.showBodyName && id2 && id2[0]) primaryLabel = getDisplayName(id2);
        if (o.showHeadName && id1 && id1[0]) secondaryLabel = getDisplayName(id1);
    } else {
        if (o.showBodyName && id1 && id1[0]) primaryLabel = getDisplayName(id1);
    }

    if (primaryLabel) {
        ImVec2 sz = ImGui::CalcTextSize(primaryLabel);
        float lx = x + (w - sz.x) * 0.5f;
        float ly = y + h - labelH;
        dl->AddText(ImVec2(lx, ly),
                    pdguiPalImU32(PDPAL_ITEM_UNFOCUSED, 200), primaryLabel);
    }

    if (secondaryLabel) {
        ImVec2 sz = ImGui::CalcTextSize(secondaryLabel);
        float lx = x + (w - sz.x) * 0.5f;
        float ly = y + h - 14.0f;
        dl->AddText(ImVec2(lx, ly),
                    pdguiPalImU32(PDPAL_ITEM_DISABLED, 160), secondaryLabel);
    }
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
    drawPanelImpl(PDGUI_MP_CHARACTER, head_id, body_id, x, y, w, h, opts);
}

void pdguiModelPreviewDrawEx(ModelPreviewKind kind,
                              const char *id1, const char *id2,
                              f32 x, f32 y, f32 w, f32 h,
                              const ModelPreviewOpts *opts)
{
    drawPanelImpl(kind, id1, id2, x, y, w, h, opts);
}

void pdguiModelPreviewInvalidate(void)
{
    s_LastKind   = -1;
    s_LastId1[0] = '\0';
    s_LastId2[0] = '\0';
    s_IdleAngle  = 0.0f;
}

} /* extern "C" */
