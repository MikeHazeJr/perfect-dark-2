/**
 * pdgui_nineslice.cpp -- 9-slice (nine-patch) texture renderer
 *
 * Renders a texture stretched correctly at any size by splitting it into
 * 9 regions: 4 corners (fixed size), 4 edges (stretch one axis), 1 center
 * (stretch both axes). This prevents corner distortion when scaling UI
 * panels, buttons, borders, and dialog frames.
 *
 * Uses ImGui's draw list AddImage() with UV coordinates to slice the
 * source texture. Each of the 9 quads gets its own UV rect.
 *
 * IMPORTANT: Do NOT include types.h -- it #defines bool as s32, breaking C++.
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 * Part of Phase P4: UI Texture Mod.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <PR/ultratypes.h>

#include "imgui/imgui.h"
#include "pdgui_nineslice.h"
#include "pdgui_theme.h"
#include "system.h"

/* =========================================================================
 * Registry: maps catalog_id → insets + texture dimensions
 * ========================================================================= */

#define NINESLICE_MAX_ENTRIES  64
#define NINESLICE_ID_LEN      64

struct nineslice_entry {
    char            catalog_id[NINESLICE_ID_LEN];
    NineSliceInsets insets;
    f32             tex_w;
    f32             tex_h;
};

static struct nineslice_entry s_Registry[NINESLICE_MAX_ENTRIES];
static s32 s_RegistryCount = 0;

static struct nineslice_entry *s_findEntry(const char *catalog_id)
{
    for (s32 i = 0; i < s_RegistryCount; i++) {
        if (strcmp(s_Registry[i].catalog_id, catalog_id) == 0) {
            return &s_Registry[i];
        }
    }
    return nullptr;
}

/* =========================================================================
 * Core 9-slice rendering
 *
 * Given the screen-space rect (px, py, pw, ph) and texture-space insets,
 * compute 9 sub-rects and draw each with the correct UV mapping.
 *
 * UV layout (normalized 0..1):
 *   u0=0     u1=left/tw   u2=(tw-right)/tw   u3=1
 *   v0=0     v1=top/th    v2=(th-bot)/th      v3=1
 *
 * Screen layout:
 *   x0=px    x1=px+left   x2=px+pw-right      x3=px+pw
 *   y0=py    y1=py+top    y2=py+ph-bottom      y3=py+ph
 * ========================================================================= */

static void s_drawSlice(ImDrawList *dl, void *tex,
                        float sx0, float sy0, float sx1, float sy1,
                        float u0, float v0, float u1, float v1,
                        ImU32 tint)
{
    if (sx1 <= sx0 || sy1 <= sy0) return;
    dl->AddImage((ImTextureID)tex,
                 ImVec2(sx0, sy0), ImVec2(sx1, sy1),
                 ImVec2(u0, v0), ImVec2(u1, v1),
                 tint);
}

extern "C" {

void pdguiNineSliceDraw(void *tex, f32 pos_x, f32 pos_y,
                        f32 size_w, f32 size_h,
                        const NineSliceInsets *insets,
                        f32 tex_w, f32 tex_h, u32 tint)
{
    if (!tex || !insets || tex_w <= 0.0f || tex_h <= 0.0f) return;
    if (size_w <= 0.0f || size_h <= 0.0f) return;

    ImDrawList *dl = ImGui::GetWindowDrawList();

    /* Clamp insets to not exceed half the output size */
    float il = insets->left;
    float ir = insets->right;
    float it = insets->top;
    float ib = insets->bottom;

    if (il + ir > size_w) {
        float scale = size_w / (il + ir);
        il *= scale;
        ir *= scale;
    }
    if (it + ib > size_h) {
        float scale = size_h / (it + ib);
        it *= scale;
        ib *= scale;
    }

    /* Screen positions */
    float x0 = pos_x;
    float x1 = pos_x + il;
    float x2 = pos_x + size_w - ir;
    float x3 = pos_x + size_w;

    float y0 = pos_y;
    float y1 = pos_y + it;
    float y2 = pos_y + size_h - ib;
    float y3 = pos_y + size_h;

    /* UV positions (normalized) */
    float u0 = 0.0f;
    float u1 = insets->left / tex_w;
    float u2 = (tex_w - insets->right) / tex_w;
    float u3 = 1.0f;

    float v0 = 0.0f;
    float v1 = insets->top / tex_h;
    float v2 = (tex_h - insets->bottom) / tex_h;
    float v3 = 1.0f;

    ImU32 t = (ImU32)tint;

    /* Row 0: top-left, top, top-right */
    s_drawSlice(dl, tex, x0, y0, x1, y1, u0, v0, u1, v1, t);
    s_drawSlice(dl, tex, x1, y0, x2, y1, u1, v0, u2, v1, t);
    s_drawSlice(dl, tex, x2, y0, x3, y1, u2, v0, u3, v1, t);

    /* Row 1: left, center, right */
    s_drawSlice(dl, tex, x0, y1, x1, y2, u0, v1, u1, v2, t);
    s_drawSlice(dl, tex, x1, y1, x2, y2, u1, v1, u2, v2, t);
    s_drawSlice(dl, tex, x2, y1, x3, y2, u2, v1, u3, v2, t);

    /* Row 2: bottom-left, bottom, bottom-right */
    s_drawSlice(dl, tex, x0, y2, x1, y3, u0, v2, u1, v3, t);
    s_drawSlice(dl, tex, x1, y2, x2, y3, u1, v2, u2, v3, t);
    s_drawSlice(dl, tex, x2, y2, x3, y3, u2, v2, u3, v3, t);
}

void pdguiNineSliceDrawSimple(void *tex, f32 pos_x, f32 pos_y,
                              f32 size_w, f32 size_h,
                              const NineSliceInsets *insets,
                              f32 tex_w, f32 tex_h)
{
    pdguiNineSliceDraw(tex, pos_x, pos_y, size_w, size_h,
                       insets, tex_w, tex_h, IM_COL32(255, 255, 255, 255));
}

void pdguiNineSlicePanel(f32 x, f32 y, f32 w, f32 h,
                         const char *catalog_id)
{
    if (!catalog_id) {
        /* No 9-slice configured — fall back to solid panel */
        pdguiThemeDrawPanel(x, y, w, h, pdguiThemeGetBgTexId());
        return;
    }

    void *tex = pdguiThemeGetTexture(catalog_id);
    const NineSliceInsets *insets = pdguiNineSliceGetInsets(catalog_id);
    if (!tex || !insets) {
        /* Texture not loaded or no insets registered — fallback */
        pdguiThemeDrawPanel(x, y, w, h, pdguiThemeGetBgTexId());
        return;
    }

    f32 tw = 0.0f, th = 0.0f;
    if (!pdguiNineSliceGetTexSize(catalog_id, &tw, &th) || tw <= 0.0f || th <= 0.0f) {
        pdguiThemeDrawPanel(x, y, w, h, pdguiThemeGetBgTexId());
        return;
    }

    pdguiNineSliceDraw(tex, x, y, w, h, insets, tw, th,
                       IM_COL32(255, 255, 255, 255));
}

/* -----------------------------------------------------------------------
 * Insets helpers
 * --------------------------------------------------------------------- */

NineSliceInsets pdguiNineSliceDefaultInsets(void)
{
    NineSliceInsets ins = { 8.0f, 8.0f, 8.0f, 8.0f };
    return ins;
}

s32 pdguiNineSliceParseInsets(const char *json, NineSliceInsets *out)
{
    if (!json || !out) return 0;

    /* Minimal parser for: {"left":N,"right":N,"top":N,"bottom":N} */
    *out = pdguiNineSliceDefaultInsets();

    const char *p;
    auto readFloat = [](const char *key, const char *json) -> float {
        const char *f = strstr(json, key);
        if (!f) return -1.0f;
        f += strlen(key);
        while (*f && (*f == '"' || *f == ':' || *f == ' ')) f++;
        return (float)strtod(f, nullptr);
    };

    float v;
    v = readFloat("\"left\"", json);   if (v >= 0.0f) out->left = v;
    v = readFloat("\"right\"", json);  if (v >= 0.0f) out->right = v;
    v = readFloat("\"top\"", json);    if (v >= 0.0f) out->top = v;
    v = readFloat("\"bottom\"", json); if (v >= 0.0f) out->bottom = v;

    return 1;
}

/* -----------------------------------------------------------------------
 * Registration
 * --------------------------------------------------------------------- */

void pdguiNineSliceRegister(const char *catalog_id, const NineSliceInsets *insets,
                            f32 tex_w, f32 tex_h)
{
    if (!catalog_id || !insets) return;

    /* Update existing entry if found */
    struct nineslice_entry *e = s_findEntry(catalog_id);
    if (!e) {
        if (s_RegistryCount >= NINESLICE_MAX_ENTRIES) {
            sysLogPrintf(LOG_WARNING,
                "PDGUI nineslice: registry full, cannot register '%s'", catalog_id);
            return;
        }
        e = &s_Registry[s_RegistryCount++];
        snprintf(e->catalog_id, NINESLICE_ID_LEN, "%s", catalog_id);
    }

    e->insets = *insets;
    e->tex_w = tex_w;
    e->tex_h = tex_h;

    sysLogPrintf(LOG_NOTE,
        "PDGUI nineslice: registered '%s' insets=%.0f,%.0f,%.0f,%.0f tex=%.0fx%.0f",
        catalog_id, insets->left, insets->right, insets->top, insets->bottom,
        tex_w, tex_h);
}

const NineSliceInsets *pdguiNineSliceGetInsets(const char *catalog_id)
{
    if (!catalog_id) return nullptr;
    struct nineslice_entry *e = s_findEntry(catalog_id);
    return e ? &e->insets : nullptr;
}

s32 pdguiNineSliceGetTexSize(const char *catalog_id, f32 *out_w, f32 *out_h)
{
    if (!catalog_id) return 0;
    struct nineslice_entry *e = s_findEntry(catalog_id);
    if (!e) return 0;
    if (out_w) *out_w = e->tex_w;
    if (out_h) *out_h = e->tex_h;
    return 1;
}

} /* extern "C" */
