/**
 * pdgui_nineslice.cpp -- 9-slice (nine-patch) texture renderer (P4)
 *
 * Renders textures with preserved corners and stretched/tiled edges.
 * All rendering uses ImGui's ImDrawList for correct z-order compositing.
 *
 * The 9 patches are:
 *   Corners (4): drawn at fixed size, never stretched
 *   Edges (4):   stretched or tiled along one axis
 *   Center (1):  stretched or tiled in both axes
 *
 * UV coordinates are computed from the source texture dimensions and insets.
 *
 * IMPORTANT: Do NOT include types.h -- it #defines bool as s32, breaking C++.
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 */

#include <math.h>
#include <PR/ultratypes.h>

#include "imgui/imgui.h"
#include "pdgui_nineslice.h"
#include "system.h"

/* =========================================================================
 * Color conversion: 0xRRGGBBAA → ImU32 (ImGui ABGR)
 * ========================================================================= */

static inline ImU32 NsCol(u32 rgba)
{
    u8 r = (u8)((rgba >> 24) & 0xFFu);
    u8 g = (u8)((rgba >> 16) & 0xFFu);
    u8 b = (u8)((rgba >>  8) & 0xFFu);
    u8 a = (u8)((rgba >>  0) & 0xFFu);
    return IM_COL32(r, g, b, a);
}

/* =========================================================================
 * Internal: draw a single image quad with UV sub-rect
 * ========================================================================= */

static void drawQuad(ImDrawList *dl, ImTextureID tex,
                     float dx, float dy, float dw, float dh,
                     float u0, float v0, float u1, float v1,
                     ImU32 col)
{
    if (dw <= 0.0f || dh <= 0.0f) return;
    dl->AddImage(tex,
                 ImVec2(dx, dy), ImVec2(dx + dw, dy + dh),
                 ImVec2(u0, v0), ImVec2(u1, v1),
                 col);
}

/* Draw a tiled region by repeating the source UV rect */
static void drawTiled(ImDrawList *dl, ImTextureID tex,
                      float dx, float dy, float dw, float dh,
                      float u0, float v0, float u1, float v1,
                      float srcW, float srcH, ImU32 col)
{
    if (dw <= 0.0f || dh <= 0.0f || srcW <= 0.0f || srcH <= 0.0f) return;

    for (float ty = 0.0f; ty < dh; ty += srcH) {
        float th = (ty + srcH > dh) ? (dh - ty) : srcH;
        float tv1 = v0 + (v1 - v0) * (th / srcH);

        for (float tx = 0.0f; tx < dw; tx += srcW) {
            float tw = (tx + srcW > dw) ? (dw - tx) : srcW;
            float tu1 = u0 + (u1 - u0) * (tw / srcW);

            dl->AddImage(tex,
                         ImVec2(dx + tx, dy + ty),
                         ImVec2(dx + tx + tw, dy + ty + th),
                         ImVec2(u0, v0), ImVec2(tu1, tv1),
                         col);
        }
    }
}

/* =========================================================================
 * Public API
 * ========================================================================= */

extern "C" {

NineSliceDef pdguiNineSliceMakeDef(f32 texW, f32 texH,
                                    f32 insetL, f32 insetT,
                                    f32 insetR, f32 insetB)
{
    NineSliceDef def;
    def.insets.left   = insetL;
    def.insets.top    = insetT;
    def.insets.right  = insetR;
    def.insets.bottom = insetB;
    def.edgeMode = NINESLICE_STRETCH;
    def.fillMode = NINESLICE_STRETCH;
    def.texW = texW;
    def.texH = texH;
    return def;
}

void pdguiNineSliceDraw(void *tex, f32 x, f32 y, f32 w, f32 h,
                        const NineSliceDef *def, u32 tint)
{
    if (!tex || !def || w <= 0.0f || h <= 0.0f) return;
    if (def->texW <= 0.0f || def->texH <= 0.0f) return;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImTextureID imTex = (ImTextureID)(uintptr_t)tex;
    ImU32 col = NsCol(tint);

    float tw = def->texW;
    float th = def->texH;
    float il = def->insets.left;
    float it = def->insets.top;
    float ir = def->insets.right;
    float ib = def->insets.bottom;

    /* Clamp insets so they don't exceed output size */
    float maxH = w * 0.5f;
    float maxV = h * 0.5f;
    if (il > maxH) il = maxH;
    if (ir > maxH) ir = maxH;
    if (it > maxV) it = maxV;
    if (ib > maxV) ib = maxV;

    /* UV boundaries in texture space */
    float ul = il / tw;           /* left edge UV */
    float ur = 1.0f - ir / tw;   /* right edge UV */
    float vt = it / th;           /* top edge UV */
    float vb = 1.0f - ib / th;   /* bottom edge UV */

    /* Destination regions */
    float cx = x + il;            /* center X start */
    float cy = y + it;            /* center Y start */
    float cw = w - il - ir;       /* center width */
    float ch = h - it - ib;       /* center height */

    /* Source pixel sizes for tiling */
    float srcEdgeW = tw - il - ir;
    float srcEdgeH = th - it - ib;

    bool tile_edges = (def->edgeMode == NINESLICE_TILE);
    bool tile_fill  = (def->fillMode == NINESLICE_TILE);

    /* --- Corners (always fixed size, never stretched) --- */
    /* Top-left */
    drawQuad(dl, imTex, x, y, il, it,
             0.0f, 0.0f, ul, vt, col);
    /* Top-right */
    drawQuad(dl, imTex, x + w - ir, y, ir, it,
             ur, 0.0f, 1.0f, vt, col);
    /* Bottom-left */
    drawQuad(dl, imTex, x, y + h - ib, il, ib,
             0.0f, vb, ul, 1.0f, col);
    /* Bottom-right */
    drawQuad(dl, imTex, x + w - ir, y + h - ib, ir, ib,
             ur, vb, 1.0f, 1.0f, col);

    /* --- Edges --- */
    if (tile_edges) {
        /* Top edge */
        drawTiled(dl, imTex, cx, y, cw, it,
                  ul, 0.0f, ur, vt, srcEdgeW, it, col);
        /* Bottom edge */
        drawTiled(dl, imTex, cx, y + h - ib, cw, ib,
                  ul, vb, ur, 1.0f, srcEdgeW, ib, col);
        /* Left edge */
        drawTiled(dl, imTex, x, cy, il, ch,
                  0.0f, vt, ul, vb, il, srcEdgeH, col);
        /* Right edge */
        drawTiled(dl, imTex, x + w - ir, cy, ir, ch,
                  ur, vt, 1.0f, vb, ir, srcEdgeH, col);
    } else {
        /* Top edge (stretch) */
        drawQuad(dl, imTex, cx, y, cw, it,
                 ul, 0.0f, ur, vt, col);
        /* Bottom edge */
        drawQuad(dl, imTex, cx, y + h - ib, cw, ib,
                 ul, vb, ur, 1.0f, col);
        /* Left edge */
        drawQuad(dl, imTex, x, cy, il, ch,
                 0.0f, vt, ul, vb, col);
        /* Right edge */
        drawQuad(dl, imTex, x + w - ir, cy, ir, ch,
                 ur, vt, 1.0f, vb, col);
    }

    /* --- Center fill --- */
    if (tile_fill) {
        drawTiled(dl, imTex, cx, cy, cw, ch,
                  ul, vt, ur, vb, srcEdgeW, srcEdgeH, col);
    } else {
        drawQuad(dl, imTex, cx, cy, cw, ch,
                 ul, vt, ur, vb, col);
    }
}

void pdguiNineSliceDrawSimple(void *tex, f32 x, f32 y, f32 w, f32 h,
                              f32 texW, f32 texH,
                              f32 insetL, f32 insetT, f32 insetR, f32 insetB,
                              u32 tint)
{
    NineSliceDef def = pdguiNineSliceMakeDef(texW, texH,
                                              insetL, insetT, insetR, insetB);
    pdguiNineSliceDraw(tex, x, y, w, h, &def, tint);
}

} /* extern "C" */
