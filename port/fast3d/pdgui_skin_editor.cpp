/**
 * pdgui_skin_editor.cpp -- In-game character skin editor UI.
 *
 * Three-panel layout: 2D canvas (left), 3D preview (center), tool panel (right).
 * Renders as a tab in the Modding Hub (tab index 5).
 *
 * Design doc: context/designs/skin-editor-design.md
 * Batch S-1: 2D canvas view, Draw + Erase, color picker, layer panel
 * Batch S-2: 3D preview wiring (pdguiCharPreviewSetSkinOverride)
 * Batch S-3: Fill, Line, Brush Size, Undo/Redo
 *
 * IMPORTANT: C++ file — must NOT include types.h (#define bool s32 breaks C++).
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/ultratypes.h>

#include "imgui/imgui.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_skin_editor.h"
#include "pdgui_charpreview.h"
#include "pdgui_model_preview.h"
#include "assetcatalog.h"
#include "system.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ========================================================================
 * Forward declarations (C symbols)
 * ======================================================================== */

extern "C" {
void pdguiDrawButtonEdgeGlow(f32 x, f32 y, f32 w, f32 h, s32 isActive);
s32  viGetWidth(void);
s32  viGetHeight(void);
} /* extern "C" */

/* ========================================================================
 * PD-style button helper (consistent with Modding Hub)
 * ======================================================================== */

static bool PdButton(const char *label, const ImVec2 &size = ImVec2(0,0))
{
    bool clicked = ImGui::Button(label, size);
    if (clicked) pdguiPlaySound(PDGUI_SND_SELECT);
    if (ImGui::IsItemHovered() || ImGui::IsItemActive() || ImGui::IsItemFocused()) {
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        pdguiDrawButtonEdgeGlow(rmin.x, rmin.y,
                                rmax.x - rmin.x, rmax.y - rmin.y,
                                ImGui::IsItemActive() ? 1 : 0);
    }
    return clicked;
}

/* ========================================================================
 * Editor state
 * ======================================================================== */

static bool s_EditorActive   = false;  /* Canvas is open and editable */
static s32  s_CurrentTool    = SKIN_TOOL_DRAW;
static s32  s_BrushSize      = 1;      /* 1-8 pixels */

/* Drawing color (RGBA) */
static u8 s_ColorR = 255, s_ColorG = 0, s_ColorB = 0, s_ColorA = 255;

/* HSV representation for color picker */
static float s_Hue = 0.0f, s_Sat = 1.0f, s_Val = 1.0f;

/* Canvas view state */
static float s_Zoom       = 8.0f;    /* Pixels per canvas pixel */
static float s_PanX       = 0.0f;
static float s_PanY       = 0.0f;
static bool  s_ShowGrid   = true;
static bool  s_Dragging   = false;   /* Mouse-drag stroke in progress */

/* Line tool state (S-3) */
static bool s_LineStarted = false;
static s32  s_LineStartX  = 0;
static s32  s_LineStartY  = 0;

/* Recent colors */
#define MAX_RECENT_COLORS 8
static u32 s_RecentColors[MAX_RECENT_COLORS];
static s32 s_NumRecentColors = 0;

/* Character selection for preview (S-2) */
static char s_PreviewBodyId[64] = "";
static char s_PreviewHeadId[64] = "";
static float s_PreviewRotAngle  = 0.0f;

/* ========================================================================
 * Color helpers
 * ======================================================================== */

static void hsvToRgb(float h, float s, float v, u8 *r, u8 *g, u8 *b)
{
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rf, gf, bf;

    if      (h < 60)  { rf = c; gf = x; bf = 0; }
    else if (h < 120) { rf = x; gf = c; bf = 0; }
    else if (h < 180) { rf = 0; gf = c; bf = x; }
    else if (h < 240) { rf = 0; gf = x; bf = c; }
    else if (h < 300) { rf = x; gf = 0; bf = c; }
    else              { rf = c; gf = 0; bf = x; }

    *r = (u8)((rf + m) * 255.0f);
    *g = (u8)((gf + m) * 255.0f);
    *b = (u8)((bf + m) * 255.0f);
}

static void rgbToHsv(u8 r, u8 g, u8 b, float *h, float *s, float *v)
{
    float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
    float cmax = rf > gf ? (rf > bf ? rf : bf) : (gf > bf ? gf : bf);
    float cmin = rf < gf ? (rf < bf ? rf : bf) : (gf < bf ? gf : bf);
    float delta = cmax - cmin;

    *v = cmax;
    *s = (cmax > 0.0f) ? delta / cmax : 0.0f;

    if (delta < 0.001f) {
        *h = 0.0f;
    } else if (cmax == rf) {
        *h = 60.0f * fmodf((gf - bf) / delta, 6.0f);
    } else if (cmax == gf) {
        *h = 60.0f * ((bf - rf) / delta + 2.0f);
    } else {
        *h = 60.0f * ((rf - gf) / delta + 4.0f);
    }
    if (*h < 0.0f) *h += 360.0f;
}

static void syncColorFromHsv(void)
{
    hsvToRgb(s_Hue, s_Sat, s_Val, &s_ColorR, &s_ColorG, &s_ColorB);
}

static void syncHsvFromColor(void)
{
    rgbToHsv(s_ColorR, s_ColorG, s_ColorB, &s_Hue, &s_Sat, &s_Val);
}

static void pushRecentColor(void)
{
    u32 c = ((u32)s_ColorR << 24) | ((u32)s_ColorG << 16) |
            ((u32)s_ColorB << 8)  | (u32)s_ColorA;

    /* Check for duplicate */
    for (s32 i = 0; i < s_NumRecentColors; i++) {
        if (s_RecentColors[i] == c) return;
    }

    /* Shift and insert at front */
    if (s_NumRecentColors < MAX_RECENT_COLORS) s_NumRecentColors++;
    for (s32 i = s_NumRecentColors - 1; i > 0; i--) {
        s_RecentColors[i] = s_RecentColors[i - 1];
    }
    s_RecentColors[0] = c;
}

/* ========================================================================
 * Drawing primitives (used by tools)
 * ======================================================================== */

static void drawPixelBrush(s32 cx, s32 cy, u8 r, u8 g, u8 b, u8 a)
{
    s32 half = s_BrushSize / 2;
    for (s32 dy = -half; dy < s_BrushSize - half; dy++) {
        for (s32 dx = -half; dx < s_BrushSize - half; dx++) {
            skinCanvasSetPixel(cx + dx, cy + dy, r, g, b, a);
        }
    }
}

static void drawBresenhamLine(s32 x0, s32 y0, s32 x1, s32 y1,
                              u8 r, u8 g, u8 b, u8 a)
{
    s32 dx = abs(x1 - x0);
    s32 dy = -abs(y1 - y0);
    s32 sx = x0 < x1 ? 1 : -1;
    s32 sy = y0 < y1 ? 1 : -1;
    s32 err = dx + dy;

    for (;;) {
        drawPixelBrush(x0, y0, r, g, b, a);
        if (x0 == x1 && y0 == y1) break;
        s32 e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Flood fill (S-3) — simple stack-based 4-connected */
static void floodFill(s32 startX, s32 startY, u8 fr, u8 fg, u8 fb, u8 fa)
{
    s32 w = skinCanvasGetWidth();
    s32 h = skinCanvasGetHeight();
    u8 *pixels = skinCanvasGetActivePixels();
    if (!pixels) return;

    s32 off = (startY * w + startX) * 4;
    u8 tr = pixels[off], tg = pixels[off+1], tb = pixels[off+2], ta = pixels[off+3];

    /* Don't fill if target == fill color */
    if (tr == fr && tg == fg && tb == fb && ta == fa) return;

    /* Simple coordinate stack */
    struct Pt { s32 x, y; };
    s32 maxStack = w * h;
    Pt *stack = (Pt *)malloc(sizeof(Pt) * maxStack);
    if (!stack) return;

    s32 top = 0;
    stack[top++] = {startX, startY};

    while (top > 0) {
        Pt p = stack[--top];
        if (p.x < 0 || p.x >= w || p.y < 0 || p.y >= h) continue;

        s32 o = (p.y * w + p.x) * 4;
        if (pixels[o] != tr || pixels[o+1] != tg ||
            pixels[o+2] != tb || pixels[o+3] != ta) continue;

        pixels[o]   = fr;
        pixels[o+1] = fg;
        pixels[o+2] = fb;
        pixels[o+3] = fa;

        if (top + 4 <= maxStack) {
            stack[top++] = {p.x + 1, p.y};
            stack[top++] = {p.x - 1, p.y};
            stack[top++] = {p.x, p.y + 1};
            stack[top++] = {p.x, p.y - 1};
        }
    }

    free(stack);
    skinCanvasMarkDirty();
}

/* ========================================================================
 * 2D Canvas rendering
 * ======================================================================== */

static void renderCanvas(float panelW, float panelH, float scale)
{
    if (!s_EditorActive) return;

    s32 cw = skinCanvasGetWidth();
    s32 ch = skinCanvasGetHeight();
    if (cw == 0 || ch == 0) return;

    ImGui::BeginChild("##skin_canvas", ImVec2(panelW, panelH), true,
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();

    /* Center the zoomed canvas */
    float texW = cw * s_Zoom;
    float texH = ch * s_Zoom;
    float offX = (canvasSize.x - texW) * 0.5f + s_PanX;
    float offY = (canvasSize.y - texH) * 0.5f + s_PanY;

    ImVec2 texMin(canvasOrigin.x + offX, canvasOrigin.y + offY);
    ImVec2 texMax(texMin.x + texW, texMin.y + texH);

    ImDrawList *dl = ImGui::GetWindowDrawList();

    /* Checkerboard background for transparency */
    dl->AddRectFilled(texMin, texMax, IM_COL32(40, 40, 40, 255));
    {
        float checkSize = s_Zoom * 2.0f;
        if (checkSize < 2.0f) checkSize = 2.0f;
        for (float cy = texMin.y; cy < texMax.y; cy += checkSize) {
            for (float cx = texMin.x; cx < texMax.x; cx += checkSize) {
                int ix = (int)((cx - texMin.x) / checkSize);
                int iy = (int)((cy - texMin.y) / checkSize);
                if ((ix + iy) % 2 == 0) {
                    float x2 = cx + checkSize; if (x2 > texMax.x) x2 = texMax.x;
                    float y2 = cy + checkSize; if (y2 > texMax.y) y2 = texMax.y;
                    dl->AddRectFilled(ImVec2(cx, cy), ImVec2(x2, y2),
                                      IM_COL32(60, 60, 60, 255));
                }
            }
        }
    }

    /* Draw the composited texture */
    u32 tex = skinCanvasGetGlTexture();
    if (tex != 0) {
        dl->AddImage((ImTextureID)(uintptr_t)tex, texMin, texMax,
                     ImVec2(0, 0), ImVec2(1, 1));
    }

    /* Grid overlay */
    if (s_ShowGrid && s_Zoom >= 4.0f) {
        for (s32 gx = 0; gx <= cw; gx++) {
            float x = texMin.x + gx * s_Zoom;
            dl->AddLine(ImVec2(x, texMin.y), ImVec2(x, texMax.y),
                        IM_COL32(255, 255, 255, 30));
        }
        for (s32 gy = 0; gy <= ch; gy++) {
            float y = texMin.y + gy * s_Zoom;
            dl->AddLine(ImVec2(texMin.x, y), ImVec2(texMax.x, y),
                        IM_COL32(255, 255, 255, 30));
        }
    }

    /* Canvas border */
    dl->AddRect(texMin, texMax, IM_COL32(0, 200, 255, 120), 0.0f, 0, 1.0f);

    /* ---- Input handling ---- */
    ImGui::SetCursorScreenPos(canvasOrigin);
    ImGui::InvisibleButton("##canvas_input", canvasSize);

    ImVec2 mousePos = ImGui::GetMousePos();
    s32 pixelX = (s32)((mousePos.x - texMin.x) / s_Zoom);
    s32 pixelY = (s32)((mousePos.y - texMin.y) / s_Zoom);
    bool inBounds = pixelX >= 0 && pixelX < cw && pixelY >= 0 && pixelY < ch;

    /* Cursor highlight */
    if (ImGui::IsItemHovered() && inBounds) {
        float px = texMin.x + pixelX * s_Zoom;
        float py = texMin.y + pixelY * s_Zoom;
        float bs = s_BrushSize * s_Zoom;
        float half = (s_BrushSize / 2) * s_Zoom;
        dl->AddRect(ImVec2(px - half, py - half),
                    ImVec2(px - half + bs, py - half + bs),
                    IM_COL32(255, 255, 0, 180), 0.0f, 0, 1.0f);
    }

    /* Zoom: scroll wheel */
    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel > 0.0f && s_Zoom < 32.0f) s_Zoom *= 1.25f;
        if (wheel < 0.0f && s_Zoom > 1.0f)  s_Zoom /= 1.25f;
    }

    /* Pan: middle mouse */
    if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        s_PanX += delta.x;
        s_PanY += delta.y;
    }

    /* Tool actions */
    if (ImGui::IsItemHovered() && inBounds) {
        bool leftDown  = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        bool leftClick = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        bool rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        bool altHeld   = ImGui::GetIO().KeyAlt;

        /* Eyedropper: Alt+click */
        if (altHeld && leftClick) {
            u8 pr, pg, pb, pa;
            skinCanvasGetCompositePixel(pixelX, pixelY, &pr, &pg, &pb, &pa);
            s_ColorR = pr; s_ColorG = pg; s_ColorB = pb; s_ColorA = pa;
            syncHsvFromColor();
            s_CurrentTool = SKIN_TOOL_DRAW; /* Return to draw after pick */
        }
        /* Draw tool */
        else if (s_CurrentTool == SKIN_TOOL_DRAW && leftDown) {
            if (!s_Dragging) { skinCanvasUndoPush(); s_Dragging = true; pushRecentColor(); }
            drawPixelBrush(pixelX, pixelY, s_ColorR, s_ColorG, s_ColorB, s_ColorA);
        }
        /* Erase tool (right click always erases, or explicit erase tool) */
        else if ((s_CurrentTool == SKIN_TOOL_ERASE && leftDown) || rightDown) {
            if (!s_Dragging) { skinCanvasUndoPush(); s_Dragging = true; }
            drawPixelBrush(pixelX, pixelY, 0, 0, 0, 0);
        }
        /* Eyedropper tool */
        else if (s_CurrentTool == SKIN_TOOL_EYEDROPPER && leftClick) {
            u8 pr, pg, pb, pa;
            skinCanvasGetCompositePixel(pixelX, pixelY, &pr, &pg, &pb, &pa);
            s_ColorR = pr; s_ColorG = pg; s_ColorB = pb; s_ColorA = pa;
            syncHsvFromColor();
            s_CurrentTool = SKIN_TOOL_DRAW;
        }
        /* Fill tool (S-3) */
        else if (s_CurrentTool == SKIN_TOOL_FILL && leftClick) {
            skinCanvasUndoPush();
            floodFill(pixelX, pixelY, s_ColorR, s_ColorG, s_ColorB, s_ColorA);
            pushRecentColor();
        }
        /* Line tool (S-3) */
        else if (s_CurrentTool == SKIN_TOOL_LINE && leftClick) {
            if (!s_LineStarted) {
                s_LineStartX = pixelX;
                s_LineStartY = pixelY;
                s_LineStarted = true;
            } else {
                skinCanvasUndoPush();
                drawBresenhamLine(s_LineStartX, s_LineStartY,
                                  pixelX, pixelY,
                                  s_ColorR, s_ColorG, s_ColorB, s_ColorA);
                s_LineStarted = false;
                pushRecentColor();
            }
        }
    }

    /* End stroke on mouse release */
    if (s_Dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        s_Dragging = false;
    }

    /* Keyboard shortcuts */
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        /* Brush size: [ and ] */
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket) && s_BrushSize > 1) s_BrushSize--;
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket) && s_BrushSize < 8) s_BrushSize++;

        /* Tool shortcuts */
        if (ImGui::IsKeyPressed(ImGuiKey_1)) s_CurrentTool = SKIN_TOOL_DRAW;
        if (ImGui::IsKeyPressed(ImGuiKey_2)) s_CurrentTool = SKIN_TOOL_ERASE;
        if (ImGui::IsKeyPressed(ImGuiKey_3)) s_CurrentTool = SKIN_TOOL_FILL;
        if (ImGui::IsKeyPressed(ImGuiKey_4)) s_CurrentTool = SKIN_TOOL_EYEDROPPER;
        if (ImGui::IsKeyPressed(ImGuiKey_5)) s_CurrentTool = SKIN_TOOL_LINE;

        /* Grid toggle */
        if (ImGui::IsKeyPressed(ImGuiKey_G)) s_ShowGrid = !s_ShowGrid;

        /* Undo/Redo (S-3) */
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            if (ImGui::GetIO().KeyShift)
                skinCanvasRedo();
            else
                skinCanvasUndo();
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
            skinCanvasRedo();
        }
    }

    /* Status bar */
    if (inBounds && ImGui::IsItemHovered()) {
        ImGui::SetCursorScreenPos(ImVec2(canvasOrigin.x + 4.0f * scale,
                                         canvasOrigin.y + canvasSize.y - 18.0f * scale));
        ImGui::TextDisabled("(%d, %d) Zoom: %.0fx", pixelX, pixelY, s_Zoom);
    }

    ImGui::EndChild();
}

/* ========================================================================
 * Tool panel
 * ======================================================================== */

static void renderToolPanel(float panelW, float panelH, float scale)
{
    ImGui::BeginChild("##skin_tools", ImVec2(panelW, panelH), true);

    /* ---- Tool buttons ---- */
    ImGui::Text("TOOLS");
    ImGui::Separator();

    static const char *toolNames[] = { "Draw", "Erase", "Fill", "Eyedrop", "Line" };
    static const char *toolKeys[]  = { "[1]",  "[2]",   "[3]", "[4]",     "[5]"  };

    float btnW = (panelW - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 2.5f;
    float btnH = 24.0f * scale;

    for (s32 i = 0; i < SKIN_TOOL_COUNT; i++) {
        if (i > 0 && i % 2 != 0) ImGui::SameLine();

        bool active = (s_CurrentTool == i);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.4f, 0.7f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.5f, 0.8f, 0.95f));
        }

        char label[32];
        snprintf(label, sizeof(label), "%s %s", toolNames[i], toolKeys[i]);
        if (PdButton(label, ImVec2(btnW, btnH))) {
            s_CurrentTool = i;
            s_LineStarted = false;
        }

        if (active) ImGui::PopStyleColor(2);
    }

    /* ---- Brush size ---- */
    ImGui::Spacing();
    ImGui::Text("Brush Size: %d px", s_BrushSize);
    int bs = s_BrushSize;
    if (ImGui::SliderInt("##brush", &bs, 1, 8)) {
        s_BrushSize = bs;
    }

    /* ---- Color Picker ---- */
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("COLOR");

    /* Current color preview */
    ImVec2 cPos = ImGui::GetCursorScreenPos();
    float swatchSize = 32.0f * scale;
    ImGui::GetWindowDrawList()->AddRectFilled(
        cPos, ImVec2(cPos.x + swatchSize, cPos.y + swatchSize),
        IM_COL32(s_ColorR, s_ColorG, s_ColorB, s_ColorA));
    ImGui::GetWindowDrawList()->AddRect(
        cPos, ImVec2(cPos.x + swatchSize, cPos.y + swatchSize),
        IM_COL32(255, 255, 255, 180));
    ImGui::Dummy(ImVec2(swatchSize, swatchSize));

    /* HSV picker */
    float col[3] = { s_Hue / 360.0f, s_Sat, s_Val };
    ImGui::PushItemWidth(panelW - ImGui::GetStyle().WindowPadding.x * 2.0f);
    if (ImGui::ColorPicker3("##hsvpicker", col,
            ImGuiColorEditFlags_PickerHueBar |
            ImGuiColorEditFlags_NoSidePreview |
            ImGuiColorEditFlags_NoInputs |
            ImGuiColorEditFlags_NoLabel)) {
        s_Hue = col[0] * 360.0f;
        s_Sat = col[1];
        s_Val = col[2];
        syncColorFromHsv();
    }
    ImGui::PopItemWidth();

    /* Alpha slider */
    int alpha = s_ColorA;
    if (ImGui::SliderInt("Alpha", &alpha, 0, 255)) {
        s_ColorA = (u8)alpha;
    }

    /* Recent colors */
    if (s_NumRecentColors > 0) {
        ImGui::Spacing();
        ImGui::Text("Recent:");
        for (s32 i = 0; i < s_NumRecentColors; i++) {
            u32 c = s_RecentColors[i];
            u8 rr = (u8)(c >> 24), rg = (u8)((c >> 16) & 0xFF);
            u8 rb = (u8)((c >> 8) & 0xFF), ra = (u8)(c & 0xFF);
            ImVec2 sp = ImGui::GetCursorScreenPos();
            float sz = 16.0f * scale;

            ImGui::GetWindowDrawList()->AddRectFilled(
                sp, ImVec2(sp.x + sz, sp.y + sz),
                IM_COL32(rr, rg, rb, ra));

            char rid[16];
            snprintf(rid, sizeof(rid), "##rc%d", i);
            if (ImGui::InvisibleButton(rid, ImVec2(sz, sz))) {
                s_ColorR = rr; s_ColorG = rg; s_ColorB = rb; s_ColorA = ra;
                syncHsvFromColor();
            }
            if (i < s_NumRecentColors - 1) ImGui::SameLine();
        }
    }

    /* ---- Layer Panel ---- */
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("LAYERS");

    s32 numLayers = skinCanvasGetNumLayers();
    s32 activeLi  = skinCanvasGetActiveLayer();

    /* Render layers top-to-bottom (highest index first) */
    for (s32 i = numLayers - 1; i >= 0; i--) {
        bool isActive = (i == activeLi);
        bool visible  = skinCanvasGetLayerVisible(i) != 0;

        ImGui::PushID(i);

        /* Visibility toggle */
        if (ImGui::Checkbox("##vis", &visible)) {
            skinCanvasSetLayerVisible(i, visible ? 1 : 0);
        }
        ImGui::SameLine();

        /* Layer button (select) */
        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.3f, 0.6f, 0.9f));
        }
        if (ImGui::Button(skinCanvasGetLayerName(i), ImVec2(-1, 0))) {
            skinCanvasSetActiveLayer(i);
        }
        if (isActive) ImGui::PopStyleColor();

        /* Opacity slider for active layer */
        if (isActive) {
            float op = skinCanvasGetLayerOpacity(i);
            if (ImGui::SliderFloat("##op", &op, 0.0f, 1.0f, "%.2f")) {
                skinCanvasSetLayerOpacity(i, op);
            }
        }

        ImGui::PopID();
    }

    /* Layer management buttons */
    float layerBtnW = (panelW - ImGui::GetStyle().ItemSpacing.x * 3) / 2.0f;
    if (PdButton("+ Add", ImVec2(layerBtnW, 0))) {
        skinCanvasAddLayer();
    }
    ImGui::SameLine();
    if (PdButton("- Remove", ImVec2(layerBtnW, 0))) {
        skinCanvasRemoveLayer(activeLi);
    }

    ImGui::EndChild();
}

/* ========================================================================
 * 3D Preview panel (S-2 wiring)
 * ======================================================================== */

static void renderPreviewPanel(float panelW, float panelH, float scale)
{
    ImGui::BeginChild("##skin_preview", ImVec2(panelW, panelH), true);

    ImGui::Text("3D PREVIEW");
    ImGui::Separator();

    if (s_EditorActive && s_PreviewBodyId[0]) {
        /* Animate rotation */
        s_PreviewRotAngle += ImGui::GetIO().DeltaTime * 0.5f;
        if (s_PreviewRotAngle > 2.0f * (float)M_PI) s_PreviewRotAngle -= 2.0f * (float)M_PI;

        pdguiCharPreviewSetRotY(s_PreviewRotAngle);
        pdguiCharPreviewRequest(s_PreviewHeadId, s_PreviewBodyId);

        /* Display preview texture */
        u32 prevTex = pdguiCharPreviewGetTextureId();
        if (prevTex != 0) {
            float availW = ImGui::GetContentRegionAvail().x;
            float availH = ImGui::GetContentRegionAvail().y - 20.0f * scale;
            float side = availW < availH ? availW : availH;

            ImGui::Image((ImTextureID)(uintptr_t)prevTex,
                         ImVec2(side, side),
                         ImVec2(0, 1), ImVec2(1, 0));  /* UV-flip for GL */
        } else {
            ImGui::TextDisabled("(rendering...)");
        }
    } else {
        ImGui::TextDisabled("Select a character and\nclick 'New Skin' to begin.");
    }

    ImGui::EndChild();
}

/* ========================================================================
 * Character selector (for choosing which body to skin)
 * ======================================================================== */

struct CharEntry {
    char id[64];
    char name[64];
};

#define MAX_CHAR_ENTRIES 128
static CharEntry s_CharEntries[MAX_CHAR_ENTRIES];
static s32 s_NumCharEntries = 0;
static s32 s_SelectedChar   = -1;

static void charCollector(const asset_entry_t *e, void *ud)
{
    if (s_NumCharEntries >= MAX_CHAR_ENTRIES) return;
    CharEntry *ce = &s_CharEntries[s_NumCharEntries++];
    strncpy(ce->id, e->id, 63); ce->id[63] = '\0';
    /* Use catalog ID as display name (e.g. "base:joanna_dark") */
    strncpy(ce->name, e->id, 63); ce->name[63] = '\0';
}

static void refreshCharacterList(void)
{
    s_NumCharEntries = 0;
    assetCatalogIterateByType(ASSET_BODY, charCollector, NULL);
}

static void renderCharacterSelector(float w, float h, float scale)
{
    ImGui::BeginChild("##skin_charsel", ImVec2(w, h), true);

    ImGui::Text("CHARACTER");
    ImGui::Separator();

    /* Canvas size inputs */
    static int canvasW = SKIN_DEFAULT_WIDTH;
    static int canvasH = SKIN_DEFAULT_HEIGHT;

    ImGui::Text("Canvas: %dx%d", canvasW, canvasH);
    ImGui::SameLine();
    if (ImGui::SmallButton("32x64")) { canvasW = 32; canvasH = 64; }
    ImGui::SameLine();
    if (ImGui::SmallButton("64x64")) { canvasW = 64; canvasH = 64; }
    ImGui::SameLine();
    if (ImGui::SmallButton("128x128")) { canvasW = 128; canvasH = 128; }

    ImGui::Spacing();

    /* Character list */
    float listH = h - 120.0f * scale;
    if (ImGui::BeginListBox("##charlist", ImVec2(-1, listH))) {
        for (s32 i = 0; i < s_NumCharEntries; i++) {
            bool sel = (i == s_SelectedChar);
            if (ImGui::Selectable(s_CharEntries[i].name, sel)) {
                s_SelectedChar = i;
                strncpy(s_PreviewBodyId, s_CharEntries[i].id, 63);
                /* Resolve head from body's default head catalog ID */
                const char *headId = catalogGetBodyDefaultHead(s_CharEntries[i].id);
                if (headId && headId[0]) {
                    strncpy(s_PreviewHeadId, headId, 63);
                } else {
                    s_PreviewHeadId[0] = '\0';
                }
            }
        }
        ImGui::EndListBox();
    }

    /* New Skin button */
    ImGui::Spacing();
    bool canCreate = (s_SelectedChar >= 0);
    if (!canCreate) ImGui::BeginDisabled();

    if (PdButton("New Skin", ImVec2(-1, 28.0f * scale))) {
        skinCanvasCreate(canvasW, canvasH);

        /* Add a drawing layer above background */
        skinCanvasAddLayer();

        s_EditorActive = true;
        s_Zoom = 8.0f;
        s_PanX = 0.0f;
        s_PanY = 0.0f;
        s_CurrentTool = SKIN_TOOL_DRAW;
        s_BrushSize   = 1;
        s_LineStarted = false;

        sysLogPrintf(LOG_NOTE, "skin_editor: new skin %dx%d for %s",
                     canvasW, canvasH, s_CharEntries[s_SelectedChar].id);
    }

    if (!canCreate) ImGui::EndDisabled();

    /* Close editor button */
    if (s_EditorActive) {
        if (PdButton("Close Editor", ImVec2(-1, 28.0f * scale))) {
            pdguiCharPreviewClearSkinOverride();
            skinCanvasDestroy();
            s_EditorActive = false;
            s_PreviewBodyId[0] = '\0';
            s_PreviewHeadId[0] = '\0';
        }
    }

    ImGui::EndChild();
}

/* ========================================================================
 * Main render (called from Modding Hub)
 * ======================================================================== */

extern "C" {

void pdguiSkinEditorRefresh(void)
{
    refreshCharacterList();
}

void pdguiSkinEditorRender(float contentW, float contentH, float scale)
{
    /* Update canvas (recomposite + GL upload if dirty) */
    if (s_EditorActive) {
        skinCanvasUpdate();

        /* S-2: Set skin override so the 3D preview uses our canvas texture */
        u32 canvasTex = skinCanvasGetGlTexture();
        if (canvasTex != 0) {
            pdguiCharPreviewSetSkinOverride(canvasTex,
                skinCanvasGetWidth(), skinCanvasGetHeight());
        }
    } else {
        /* Clear override when editor is not active */
        pdguiCharPreviewClearSkinOverride();
    }

    if (!s_EditorActive) {
        /* Show character selector when no canvas is open */
        renderCharacterSelector(contentW, contentH, scale);
        return;
    }

    /* Three-panel layout: canvas (45%) | preview (25%) | tools (30%) */
    float canvasW  = contentW * 0.45f;
    float previewW = contentW * 0.25f;
    float toolsW   = contentW - canvasW - previewW -
                     ImGui::GetStyle().ItemSpacing.x * 2.0f;

    renderCanvas(canvasW, contentH, scale);
    ImGui::SameLine();
    renderPreviewPanel(previewW, contentH, scale);
    ImGui::SameLine();
    renderToolPanel(toolsW, contentH, scale);
}

} /* extern "C" */
