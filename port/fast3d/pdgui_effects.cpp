/**
 * pdgui_effects.cpp -- Caustic mask + border overlay effects for PD2 UI (P4)
 *
 * Compositing order (from bottom to top):
 *   1. Base texture (drawn by nineslice or theme panel)
 *   2. Border effect mask (only on 9-slice edge/corner regions)
 *   3. Caustic animated overlay (over entire element)
 *   4. Text (drawn last by ImGui text rendering)
 *
 * Caustic animation: a horizontal spritesheet where each frame is the same
 * size. The current frame is selected by wall-clock time * speed.
 * The frame is drawn as a fullscreen overlay with the specified blend mode.
 *
 * Border effect: a texture mask scrolled/tinted and composited only onto
 * the border regions (as defined by 9-slice insets).
 *
 * IMPORTANT: Do NOT include types.h -- it #defines bool as s32, breaking C++.
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 */

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <PR/ultratypes.h>

#include "glad/glad.h"
#include "imgui/imgui.h"
#include "pdgui_effects.h"
#include "pdgui_theme.h"
#include "system.h"

/* =========================================================================
 * Constants
 * ========================================================================= */

#define FX_MAX_ELEMENTS  64
#define FX_ID_LEN        64

/* =========================================================================
 * Per-element effect storage
 * ========================================================================= */

struct fx_entry {
    char            element_id[FX_ID_LEN];

    /* Caustic effect */
    caustic_def_t   caustic;
    s32             has_caustic;

    /* Border effect */
    border_fx_def_t border_fx;
    s32             has_border_fx;
};

static struct fx_entry s_FxEntries[FX_MAX_ELEMENTS];
static s32 s_FxCount = 0;
static s32 s_FxInitDone = 0;

/* =========================================================================
 * Helpers
 * ========================================================================= */

static struct fx_entry *s_findFx(const char *id)
{
    for (s32 i = 0; i < s_FxCount; i++) {
        if (strcmp(s_FxEntries[i].element_id, id) == 0)
            return &s_FxEntries[i];
    }
    return nullptr;
}

static struct fx_entry *s_getOrCreate(const char *id)
{
    struct fx_entry *e = s_findFx(id);
    if (e) return e;

    if (s_FxCount >= FX_MAX_ELEMENTS) {
        sysLogPrintf(LOG_WARNING,
            "PDGUI effects: registry full (%d), cannot add '%s'",
            FX_MAX_ELEMENTS, id);
        return nullptr;
    }

    e = &s_FxEntries[s_FxCount++];
    memset(e, 0, sizeof(*e));
    snprintf(e->element_id, sizeof(e->element_id), "%s", id);
    return e;
}

/* 0xRRGGBBAA → ImU32 (ImGui packed ABGR) */
static inline ImU32 FxCol(u32 rgba)
{
    uint8_t r = (uint8_t)((rgba >> 24) & 0xffu);
    uint8_t g = (uint8_t)((rgba >> 16) & 0xffu);
    uint8_t b = (uint8_t)((rgba >>  8) & 0xffu);
    uint8_t a = (uint8_t)((rgba >>  0) & 0xffu);
    return IM_COL32(r, g, b, a);
}

static float s_getTime(void)
{
    return (float)SDL_GetTicks() / 1000.0f;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

extern "C" {

void pdguiEffectsInit(void)
{
    if (s_FxInitDone) return;
    s_FxInitDone = 1;
    s_FxCount = 0;
    sysLogPrintf(LOG_NOTE, "PDGUI effects: init");
}

void pdguiEffectsShutdown(void)
{
    s_FxCount = 0;
    s_FxInitDone = 0;
    sysLogPrintf(LOG_NOTE, "PDGUI effects: shutdown");
}

s32 pdguiEffectsSetCaustic(const char *element_id, const caustic_def_t *def)
{
    if (!element_id || !def) return 0;
    struct fx_entry *e = s_getOrCreate(element_id);
    if (!e) return 0;

    e->caustic = *def;
    e->has_caustic = 1;

    sysLogPrintf(LOG_NOTE,
        "PDGUI effects: caustic on '%s' (tex='%s' frames=%d speed=%.1f opacity=%.2f)",
        element_id, def->texture_id, def->frame_count, def->speed, def->opacity);
    return 1;
}

s32 pdguiEffectsSetBorderFx(const char *element_id, const border_fx_def_t *def)
{
    if (!element_id || !def) return 0;
    struct fx_entry *e = s_getOrCreate(element_id);
    if (!e) return 0;

    e->border_fx = *def;
    e->has_border_fx = 1;

    sysLogPrintf(LOG_NOTE,
        "PDGUI effects: border fx on '%s' (mask='%s' opacity=%.2f)",
        element_id, def->mask_texture_id, def->opacity);
    return 1;
}

void pdguiEffectsClear(const char *element_id)
{
    if (!element_id) return;
    struct fx_entry *e = s_findFx(element_id);
    if (!e) return;

    e->has_caustic = 0;
    e->has_border_fx = 0;
}

const caustic_def_t *pdguiEffectsGetCaustic(const char *element_id)
{
    if (!element_id) return nullptr;
    struct fx_entry *e = s_findFx(element_id);
    return (e && e->has_caustic) ? &e->caustic : nullptr;
}

const border_fx_def_t *pdguiEffectsGetBorderFx(const char *element_id)
{
    if (!element_id) return nullptr;
    struct fx_entry *e = s_findFx(element_id);
    return (e && e->has_border_fx) ? &e->border_fx : nullptr;
}

/* =========================================================================
 * Rendering
 * ========================================================================= */

void pdguiEffectsDrawCaustic(const char *element_id,
                             float x, float y, float w, float h)
{
    if (!element_id) return;
    struct fx_entry *e = s_findFx(element_id);
    if (!e || !e->has_caustic) return;

    const caustic_def_t *cd = &e->caustic;
    if (cd->opacity <= 0.0f || cd->frame_count <= 0) return;

    void *tex = pdguiThemeGetTexture(cd->texture_id);
    if (!tex) return;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImTextureID tid = (ImTextureID)(uintptr_t)tex;

    /* Calculate current frame from time */
    float t = s_getTime();
    s32 frame = (s32)(t * cd->speed) % cd->frame_count;
    if (frame < 0) frame += cd->frame_count;

    /* UV: horizontal strip, each frame = 1/frame_count width */
    float frame_u = (float)frame / (float)cd->frame_count;
    float frame_w = 1.0f / (float)cd->frame_count;

    /* Scale UV by the scale factor */
    float su = frame_w / (cd->scale > 0.01f ? cd->scale : 1.0f);

    /* Apply blend mode via alpha and tint color */
    uint8_t alpha = (uint8_t)(cd->opacity * 255.0f);
    ImU32 col;

    switch (cd->blend_mode) {
    case FX_BLEND_ADDITIVE:
        /* Additive: draw bright overlay, white tinted */
        col = IM_COL32(255, 255, 255, alpha / 2);
        break;
    case FX_BLEND_SCREEN:
        /* Screen: lighter overlay */
        col = IM_COL32(255, 255, 255, alpha / 3);
        break;
    case FX_BLEND_MULTIPLY:
    default:
        /* Multiply: darken overlay */
        col = IM_COL32(180, 180, 180, alpha);
        break;
    }

    dl->AddImage(tid,
                 ImVec2(x, y), ImVec2(x + w, y + h),
                 ImVec2(frame_u, 0.0f),
                 ImVec2(frame_u + su, 1.0f),
                 col);
}

void pdguiEffectsDrawBorder(const char *element_id,
                            float x, float y, float w, float h,
                            float left, float right, float top, float bottom)
{
    if (!element_id) return;
    struct fx_entry *e = s_findFx(element_id);
    if (!e || !e->has_border_fx) return;

    const border_fx_def_t *bd = &e->border_fx;
    if (bd->opacity <= 0.0f) return;

    void *tex = pdguiThemeGetTexture(bd->mask_texture_id);
    if (!tex) return;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImTextureID tid = (ImTextureID)(uintptr_t)tex;

    /* Scrolling UV offset */
    float t = s_getTime();
    float u_off = t * bd->scroll_speed_x * 0.01f;
    float v_off = t * bd->scroll_speed_y * 0.01f;

    /* Tint with opacity */
    u32 tint = bd->tint_color;
    uint8_t base_a = (uint8_t)(tint & 0xffu);
    uint8_t final_a = (uint8_t)((float)base_a * bd->opacity);
    ImU32 col = FxCol((tint & 0xffffff00u) | final_a);

    /* Draw border regions only (4 edge strips) */

    /* Top edge */
    if (top > 0.0f) {
        dl->AddImage(tid,
                     ImVec2(x, y), ImVec2(x + w, y + top),
                     ImVec2(u_off, v_off),
                     ImVec2(u_off + 1.0f, v_off + top / h),
                     col);
    }

    /* Bottom edge */
    if (bottom > 0.0f) {
        dl->AddImage(tid,
                     ImVec2(x, y + h - bottom), ImVec2(x + w, y + h),
                     ImVec2(u_off, v_off + (h - bottom) / h),
                     ImVec2(u_off + 1.0f, v_off + 1.0f),
                     col);
    }

    /* Left edge (excluding corners already covered by top/bottom) */
    if (left > 0.0f) {
        float ey = y + top;
        float eh = h - top - bottom;
        if (eh > 0.0f) {
            dl->AddImage(tid,
                         ImVec2(x, ey), ImVec2(x + left, ey + eh),
                         ImVec2(u_off, v_off + top / h),
                         ImVec2(u_off + left / w, v_off + (h - bottom) / h),
                         col);
        }
    }

    /* Right edge */
    if (right > 0.0f) {
        float ey = y + top;
        float eh = h - top - bottom;
        if (eh > 0.0f) {
            dl->AddImage(tid,
                         ImVec2(x + w - right, ey), ImVec2(x + w, ey + eh),
                         ImVec2(u_off + (w - right) / w, v_off + top / h),
                         ImVec2(u_off + 1.0f, v_off + (h - bottom) / h),
                         col);
        }
    }
}

void pdguiEffectsDrawAll(const char *element_id,
                         float x, float y, float w, float h,
                         float border_l, float border_r,
                         float border_t, float border_b)
{
    /* Compositing order: border effect first, then caustic on top */
    pdguiEffectsDrawBorder(element_id, x, y, w, h,
                           border_l, border_r, border_t, border_b);
    pdguiEffectsDrawCaustic(element_id, x, y, w, h);
}

} /* extern "C" */
