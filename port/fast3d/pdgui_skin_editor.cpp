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
 * Batch S-4: Save as Mod — TGA writer, skin.ini, catalog registration
 * Batch S-5: Image Import — stb_image, scale-to-fit, import as layer
 * Batch S-6: PD-Style Downrez — median-cut quantize + dithering
 *
 * IMPORTANT: C++ file — must NOT include types.h (#define bool s32 breaks C++).
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include "glad/glad.h"

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
#include "fs.h"
#include <errno.h>

/* S-5: stb_image for image import (public domain, vendored) */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_ONLY_JPEG
#include "../external/stb_image.h"

/* Export Template: stb_image_write for PNG output (public domain, vendored) */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../external/stb_image_write.h"

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
static bool  s_ShowUv     = false;   /* S-8: UV wireframe overlay */
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

/* S-4: Save dialog state */
static bool  s_SaveDialogOpen = false;
static char  s_SaveName[64]   = "";
static char  s_SaveStatus[128] = "";
static bool  s_SaveOk = false;

/* S-5: Import dialog state */
static bool s_ImportDialogOpen = false;
static char s_ImportPath[512]  = "";
static char s_ImportStatus[128] = "";

/* S-6: Downrez dialog state */
static bool s_DownrezDialogOpen = false;
static int  s_DownrezMaxColors  = 16;
static int  s_DownrezDitherMode = 1;
static u8  *s_DownrezPreview    = NULL;
static u32  s_DownrezPreviewTex = 0;

/* Export Template dialog state */
static bool s_ExportDialogOpen  = false;
static char s_ExportFilename[256] = "";
static char s_ExportStatus[128] = "";
static bool s_ExportOk          = false;
static int  s_ExportMode        = 0;  /* 0 = Template (with UV guide), 1 = Clean */

/* Character entries (for save dialog referencing selected char) */
struct CharEntry {
    char id[64];
    char name[64];
};

#define MAX_CHAR_ENTRIES 128
static CharEntry s_CharEntries[MAX_CHAR_ENTRIES];
static s32 s_NumCharEntries = 0;
static s32 s_SelectedChar   = -1;

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

    /* S-8: UV wireframe overlay */
    if (s_ShowUv) {
        s32 numUvLines = skinUvGetNumLines();
        for (s32 i = 0; i < numUvLines; i++) {
            f32 u0, v0, u1, v1;
            skinUvGetLine(i, &u0, &v0, &u1, &v1);
            ImVec2 p0(texMin.x + u0 * texW, texMin.y + v0 * texH);
            ImVec2 p1(texMin.x + u1 * texW, texMin.y + v1 * texH);
            dl->AddLine(p0, p1, IM_COL32(255, 128, 0, 100), 1.0f);
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

        /* S-8: UV overlay toggle */
        if (ImGui::IsKeyPressed(ImGuiKey_U)) s_ShowUv = !s_ShowUv;

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

        /* Opacity slider + blend mode for active layer */
        if (isActive) {
            float op = skinCanvasGetLayerOpacity(i);
            if (ImGui::SliderFloat("##op", &op, 0.0f, 1.0f, "%.2f")) {
                skinCanvasSetLayerOpacity(i, op);
            }

            /* S-7: Blend mode dropdown */
            static const char *blendNames[] = {
                "Normal", "Multiply", "Screen", "Hue", "Burn", "Saturation"
            };
            int bm = skinCanvasGetLayerBlendMode(i);
            if (ImGui::Combo("##blend", &bm, blendNames, SKIN_BLEND_COUNT)) {
                skinCanvasSetLayerBlendMode(i, bm);
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

    /* ---- Actions (S-4, S-5, S-6) ---- */
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("ACTIONS");

    float actionW = panelW - ImGui::GetStyle().WindowPadding.x * 2.0f;

    /* Export Template */
    if (PdButton("Export Template", ImVec2(actionW, 0))) {
        s_ExportDialogOpen = true;
        s_ExportStatus[0] = '\0';
        if (!s_ExportFilename[0] && s_SelectedChar >= 0) {
            snprintf(s_ExportFilename, sizeof(s_ExportFilename), "%s_template.png",
                     s_CharEntries[s_SelectedChar].name);
            /* Sanitize filename: replace spaces with underscores */
            for (char *p = s_ExportFilename; *p; p++) {
                if (*p == ' ') *p = '_';
            }
        }
    }

    /* S-5: Import Image */
    if (PdButton("Import Image", ImVec2(actionW, 0))) {
        s_ImportDialogOpen = true;
        s_ImportStatus[0] = '\0';
    }

    /* S-6: Convert to PD Style */
    if (PdButton("PD Style", ImVec2(actionW, 0))) {
        s_DownrezDialogOpen = true;
        s_DownrezMaxColors = 16;
        s_DownrezDitherMode = 1;
        if (s_DownrezPreview) { free(s_DownrezPreview); s_DownrezPreview = NULL; }
    }

    ImGui::Spacing();

    /* S-4: Save as Mod */
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.05f, 0.35f, 0.15f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f, 0.5f, 0.2f, 0.9f));
    if (PdButton("Save as Mod", ImVec2(actionW, 28.0f * scale))) {
        s_SaveDialogOpen = true;
        s_SaveStatus[0] = '\0';
        if (!s_SaveName[0] && s_SelectedChar >= 0) {
            snprintf(s_SaveName, sizeof(s_SaveName), "Custom %s",
                     s_CharEntries[s_SelectedChar].name);
        }
    }
    ImGui::PopStyleColor(2);

    /* Status messages from last save/export */
    if (s_SaveStatus[0]) {
        ImGui::TextColored(s_SaveOk ? ImVec4(0,1,0,1) : ImVec4(1,0.3f,0.3f,1),
                           "%s", s_SaveStatus);
    }
    if (s_ExportStatus[0]) {
        ImGui::TextColored(s_ExportOk ? ImVec4(0,1,0,1) : ImVec4(1,0.3f,0.3f,1),
                           "%s", s_ExportStatus);
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

        /* S-8: Extract UV wireframe from body model */
        skinUvExtract(s_PreviewBodyId, canvasW, canvasH);

        sysLogPrintf(LOG_NOTE, "skin_editor: new skin %dx%d for %s",
                     canvasW, canvasH, s_CharEntries[s_SelectedChar].id);
    }

    if (!canCreate) ImGui::EndDisabled();

    /* Close editor button */
    if (s_EditorActive) {
        if (PdButton("Close Editor", ImVec2(-1, 28.0f * scale))) {
            pdguiCharPreviewClearSkinOverride();
            skinUvClear();
            skinCanvasDestroy();
            s_EditorActive = false;
            s_PreviewBodyId[0] = '\0';
            s_PreviewHeadId[0] = '\0';
        }
    }

    ImGui::EndChild();
}

/* ========================================================================
 * S-4: Save as Mod — TGA writer + skin.ini + catalog registration
 * ======================================================================== */

/* Sanitize a display name into a filesystem-safe slug */
static void sanitizeSlug(const char *name, char *out, int maxLen)
{
    int len = 0;
    for (int i = 0; name[i] && len < maxLen - 1; i++) {
        char c = name[i];
        if (c == ' ') c = '-';
        else if (c >= 'A' && c <= 'Z') c = c + 32;
        else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            continue;
        out[len++] = c;
    }
    out[len] = '\0';
}

/* Write uncompressed TGA (type 2, RGBA) */
static bool writeTga(const char *path, const u8 *rgba, s32 w, s32 h)
{
    FILE *f = fsFileOpenWrite(path);
    if (!f) return false;

    u8 header[18];
    memset(header, 0, 18);
    header[2]  = 2;              /* uncompressed true-color */
    header[12] = (u8)(w & 0xFF);
    header[13] = (u8)((w >> 8) & 0xFF);
    header[14] = (u8)(h & 0xFF);
    header[15] = (u8)((h >> 8) & 0xFF);
    header[16] = 32;             /* 32 bits per pixel */
    header[17] = 0x28;           /* top-left origin + 8 alpha bits */
    fwrite(header, 1, 18, f);

    /* TGA stores BGRA, not RGBA */
    for (s32 i = 0; i < w * h; i++) {
        u8 bgra[4] = { rgba[i*4+2], rgba[i*4+1], rgba[i*4+0], rgba[i*4+3] };
        fwrite(bgra, 1, 4, f);
    }

    bool ok = !ferror(f);
    fclose(f);
    return ok;
}

static bool saveSkinAsMod(const char *displayName, const char *targetBodyId)
{
    if (!displayName || !displayName[0] || !targetBodyId || !targetBodyId[0]) return false;

    char slug[64];
    sanitizeSlug(displayName, slug, sizeof(slug));
    if (!slug[0]) return false;

    /* Create directories */
    char modDir[256];
    snprintf(modDir, sizeof(modDir), "mods/%s", slug);

    if (fsCreateDir("mods") < 0 && errno != EEXIST) {
        sysLogPrintf(LOG_WARNING, "skin_editor: cannot create 'mods/' (errno %d)", errno);
        return false;
    }
    if (fsCreateDir(modDir) < 0 && errno != EEXIST) {
        sysLogPrintf(LOG_WARNING, "skin_editor: cannot create '%s' (errno %d)", modDir, errno);
        return false;
    }

    /* Flatten canvas and write TGA */
    skinCanvasUpdate();
    const u8 *composite = skinCanvasGetCompositePixels();
    s32 cw = skinCanvasGetWidth();
    s32 ch = skinCanvasGetHeight();
    if (!composite || cw == 0 || ch == 0) return false;

    char tgaPath[280];
    snprintf(tgaPath, sizeof(tgaPath), "%s/texture.tga", modDir);
    if (!writeTga(tgaPath, composite, cw, ch)) {
        sysLogPrintf(LOG_WARNING, "skin_editor: failed to write '%s'", tgaPath);
        return false;
    }

    /* Write skin.ini */
    char iniPath[280];
    snprintf(iniPath, sizeof(iniPath), "%s/skin.ini", modDir);
    FILE *f = fsFileOpenWrite(iniPath);
    if (!f) {
        sysLogPrintf(LOG_WARNING, "skin_editor: cannot write '%s'", iniPath);
        return false;
    }
    fprintf(f, "[skin]\n");
    fprintf(f, "type = skin\n");
    fprintf(f, "name = %s\n", displayName);
    fprintf(f, "target = %s\n", targetBodyId);
    bool ok = !ferror(f);
    fclose(f);
    if (!ok) return false;

    /* Register in catalog immediately (hot reload) */
    char catalogId[128];
    snprintf(catalogId, sizeof(catalogId), "mod:%s", slug);

    asset_entry_t *entry = assetCatalogRegisterSkin(catalogId, targetBodyId);
    if (entry) {
        strncpy(entry->dirpath, modDir, FS_MAXPATH - 1);
        entry->dirpath[FS_MAXPATH - 1] = '\0';
        entry->enabled = 1;
        entry->bundled = 0;
        sysLogPrintf(LOG_NOTE, "skin_editor: registered '%s' targeting '%s'",
                     catalogId, targetBodyId);
    }

    sysLogPrintf(LOG_NOTE, "skin_editor: saved mod to '%s'", modDir);
    return true;
}

static void renderSaveDialog(float scale)
{
    if (!s_SaveDialogOpen) return;

    ImGui::OpenPopup("Save Skin as Mod");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(350.0f * scale, 0));

    if (ImGui::BeginPopupModal("Save Skin as Mod", &s_SaveDialogOpen,
            ImGuiWindowFlags_AlwaysAutoResize)) {

        ImGui::Text("Skin Name:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##skinname", s_SaveName, sizeof(s_SaveName));

        if (s_PreviewBodyId[0]) {
            ImGui::TextDisabled("Target: %s", s_PreviewBodyId);
        }

        ImGui::Spacing();

        if (PdButton("Save", ImVec2(120.0f * scale, 0))) {
            if (saveSkinAsMod(s_SaveName, s_PreviewBodyId)) {
                snprintf(s_SaveStatus, sizeof(s_SaveStatus),
                         "Saved as mod:%s", s_SaveName);
                s_SaveOk = true;
                s_SaveDialogOpen = false;
                pdguiPlaySound(PDGUI_SND_SUCCESS);
            } else {
                snprintf(s_SaveStatus, sizeof(s_SaveStatus), "Save failed!");
                s_SaveOk = false;
                pdguiPlaySound(PDGUI_SND_ERROR);
            }
        }
        ImGui::SameLine();
        if (PdButton("Cancel", ImVec2(120.0f * scale, 0))) {
            s_SaveDialogOpen = false;
        }

        if (s_SaveStatus[0]) {
            ImGui::TextColored(s_SaveOk ? ImVec4(0,1,0,1) : ImVec4(1,0.3f,0.3f,1),
                               "%s", s_SaveStatus);
        }

        ImGui::EndPopup();
    }
}

/* ========================================================================
 * S-5: Image Import — stb_image load + scale to canvas
 * ======================================================================== */

static void importImageToLayer(const char *path)
{
    if (!path || !path[0]) return;

    s32 imgW, imgH, imgC;
    u8 *imgData = stbi_load(path, &imgW, &imgH, &imgC, 4); /* force RGBA */
    if (!imgData) {
        snprintf(s_ImportStatus, sizeof(s_ImportStatus),
                 "Failed: %s", stbi_failure_reason());
        return;
    }

    s32 cw = skinCanvasGetWidth();
    s32 ch = skinCanvasGetHeight();

    /* Add a new layer for the import */
    s32 layerIdx = skinCanvasAddLayer();
    if (layerIdx < 0) {
        stbi_image_free(imgData);
        snprintf(s_ImportStatus, sizeof(s_ImportStatus), "Max layers reached");
        return;
    }

    /* Scale image to canvas dimensions (nearest-neighbor) */
    for (s32 y = 0; y < ch; y++) {
        for (s32 x = 0; x < cw; x++) {
            s32 srcX = (x * imgW) / cw;
            s32 srcY = (y * imgH) / ch;
            if (srcX >= imgW) srcX = imgW - 1;
            if (srcY >= imgH) srcY = imgH - 1;

            s32 srcOff = (srcY * imgW + srcX) * 4;
            skinCanvasSetPixel(x, y,
                imgData[srcOff], imgData[srcOff+1],
                imgData[srcOff+2], imgData[srcOff+3]);
        }
    }

    stbi_image_free(imgData);
    skinCanvasMarkDirty();
    snprintf(s_ImportStatus, sizeof(s_ImportStatus),
             "Imported %dx%d -> %dx%d", imgW, imgH, cw, ch);
    sysLogPrintf(LOG_NOTE, "skin_editor: imported '%s' (%dx%d -> %dx%d)",
                 path, imgW, imgH, cw, ch);
}

static void renderImportDialog(float scale)
{
    if (!s_ImportDialogOpen) return;

    ImGui::OpenPopup("Import Image");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400.0f * scale, 0));

    if (ImGui::BeginPopupModal("Import Image", &s_ImportDialogOpen,
            ImGuiWindowFlags_AlwaysAutoResize)) {

        ImGui::Text("Image path (PNG, TGA, BMP, JPG):");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##importpath", s_ImportPath, sizeof(s_ImportPath));

        ImGui::Spacing();

        if (PdButton("Import", ImVec2(120.0f * scale, 0))) {
            importImageToLayer(s_ImportPath);
            if (s_ImportStatus[0] && strncmp(s_ImportStatus, "Failed", 6) != 0) {
                s_ImportDialogOpen = false;
                pdguiPlaySound(PDGUI_SND_SUCCESS);
            } else {
                pdguiPlaySound(PDGUI_SND_ERROR);
            }
        }
        ImGui::SameLine();
        if (PdButton("Cancel", ImVec2(120.0f * scale, 0))) {
            s_ImportDialogOpen = false;
        }

        if (s_ImportStatus[0]) {
            bool ok = (strncmp(s_ImportStatus, "Failed", 6) != 0 &&
                       strncmp(s_ImportStatus, "Max", 3) != 0);
            ImGui::TextColored(ok ? ImVec4(0,1,0,1) : ImVec4(1,0.3f,0.3f,1),
                               "%s", s_ImportStatus);
        }

        ImGui::EndPopup();
    }
}

/* ========================================================================
 * S-6: PD-Style Downrez — Median-cut quantize + dithering
 * ======================================================================== */

/* Forward declaration — implementation in pdgui_skin_quantize.cpp */
extern "C" void skinQuantize(const u8 *src, u8 *dst, s32 w, s32 h,
                             s32 max_colors, s32 dither_mode);

static void downrezGeneratePreview(void)
{
    s32 cw = skinCanvasGetWidth();
    s32 ch = skinCanvasGetHeight();
    if (cw == 0 || ch == 0) return;

    skinCanvasUpdate();
    const u8 *composite = skinCanvasGetCompositePixels();
    if (!composite) return;

    size_t bufSize = (size_t)cw * ch * 4;
    if (!s_DownrezPreview) {
        s_DownrezPreview = (u8 *)malloc(bufSize);
        if (!s_DownrezPreview) return;
    }

    skinQuantize(composite, s_DownrezPreview, cw, ch,
                 s_DownrezMaxColors, s_DownrezDitherMode);

    /* Upload to GL texture for preview */
    if (s_DownrezPreviewTex == 0) {
        glGenTextures(1, &s_DownrezPreviewTex);
    }
    glBindTexture(GL_TEXTURE_2D, s_DownrezPreviewTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cw, ch, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, s_DownrezPreview);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void renderDownrezDialog(float scale)
{
    if (!s_DownrezDialogOpen) return;

    ImGui::OpenPopup("Convert to PD Style");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500.0f * scale, 0));

    if (ImGui::BeginPopupModal("Convert to PD Style", &s_DownrezDialogOpen,
            ImGuiWindowFlags_AlwaysAutoResize)) {

        ImGui::Text("Palette size:");
        bool changed = false;
        changed |= ImGui::RadioButton("16 colors (CI4)", &s_DownrezMaxColors, 16);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("32 colors", &s_DownrezMaxColors, 32);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("256 colors (CI8)", &s_DownrezMaxColors, 256);

        ImGui::Text("Dithering:");
        changed |= ImGui::RadioButton("None", &s_DownrezDitherMode, 0);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Bayer 4x4", &s_DownrezDitherMode, 1);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Floyd-Steinberg", &s_DownrezDitherMode, 2);

        if (changed || !s_DownrezPreview) {
            downrezGeneratePreview();
        }

        /* Before/after preview */
        ImGui::Spacing();
        ImGui::Separator();

        s32 cw = skinCanvasGetWidth();
        s32 ch = skinCanvasGetHeight();
        float previewScale = 3.0f * scale;
        float pw = cw * previewScale;
        float ph = ch * previewScale;

        ImGui::Text("Before:");
        ImGui::SameLine(pw + 30.0f * scale);
        ImGui::Text("After:");

        u32 origTex = skinCanvasGetGlTexture();
        if (origTex != 0) {
            ImGui::Image((ImTextureID)(uintptr_t)origTex, ImVec2(pw, ph));
        }
        ImGui::SameLine();
        if (s_DownrezPreviewTex != 0) {
            ImGui::Image((ImTextureID)(uintptr_t)s_DownrezPreviewTex, ImVec2(pw, ph));
        }

        ImGui::Spacing();

        if (PdButton("Apply", ImVec2(120.0f * scale, 0))) {
            /* Replace active layer with quantized result */
            skinCanvasUndoPush();
            u8 *activePixels = skinCanvasGetActivePixels();
            if (activePixels && s_DownrezPreview) {
                memcpy(activePixels, s_DownrezPreview,
                       (size_t)cw * ch * 4);
                skinCanvasMarkDirty();
            }
            s_DownrezDialogOpen = false;
            pdguiPlaySound(PDGUI_SND_SUCCESS);
        }
        ImGui::SameLine();
        if (PdButton("Cancel", ImVec2(120.0f * scale, 0))) {
            s_DownrezDialogOpen = false;
        }

        ImGui::EndPopup();
    }

    /* Cleanup preview on close */
    if (!s_DownrezDialogOpen) {
        if (s_DownrezPreview) { free(s_DownrezPreview); s_DownrezPreview = NULL; }
        if (s_DownrezPreviewTex) {
            glDeleteTextures(1, &s_DownrezPreviewTex);
            s_DownrezPreviewTex = 0;
        }
    }
}

/* ========================================================================
 * Export Template — composite base texture + optional UV wireframe → PNG
 * ======================================================================== */

static bool exportSkinTemplate(const char *filename, int mode)
{
    if (!filename || !filename[0]) return false;

    skinCanvasUpdate();
    const u8 *composite = skinCanvasGetCompositePixels();
    s32 cw = skinCanvasGetWidth();
    s32 ch = skinCanvasGetHeight();
    if (!composite || cw <= 0 || ch <= 0) return false;

    /* Allocate output RGBA buffer */
    s32 pixelCount = cw * ch;
    u8 *outBuf = (u8 *)malloc(pixelCount * 4);
    if (!outBuf) return false;

    /* Copy base composite */
    memcpy(outBuf, composite, pixelCount * 4);

    /* Mode 0 = Template (with UV guide overlay at ~40% opacity) */
    if (mode == 0 && s_PreviewBodyId[0]) {
        /* Ensure UV data is extracted for current body */
        skinUvExtract(s_PreviewBodyId, cw, ch);

        s32 numLines = skinUvGetNumLines();
        if (numLines > 0) {
            /* Draw UV wireframe lines onto the output buffer */
            for (s32 li = 0; li < numLines; li++) {
                f32 u0, v0, u1, v1;
                skinUvGetLine(li, &u0, &v0, &u1, &v1);

                /* Convert normalized UV to pixel coords */
                s32 x0 = (s32)(u0 * cw);
                s32 y0 = (s32)(v0 * ch);
                s32 x1 = (s32)(u1 * cw);
                s32 y1 = (s32)(v1 * ch);

                /* Bresenham line with alpha blending at ~40% */
                s32 dx = abs(x1 - x0);
                s32 dy = -abs(y1 - y0);
                s32 sx = x0 < x1 ? 1 : -1;
                s32 sy = y0 < y1 ? 1 : -1;
                s32 err = dx + dy;

                for (;;) {
                    if (x0 >= 0 && x0 < cw && y0 >= 0 && y0 < ch) {
                        s32 idx = (y0 * cw + x0) * 4;
                        /* Blend cyan wireframe at 40% opacity over existing pixel */
                        f32 alpha = 0.4f;
                        u8 wr = 0, wg = 255, wb = 255;  /* cyan */
                        outBuf[idx + 0] = (u8)(outBuf[idx + 0] * (1.0f - alpha) + wr * alpha);
                        outBuf[idx + 1] = (u8)(outBuf[idx + 1] * (1.0f - alpha) + wg * alpha);
                        outBuf[idx + 2] = (u8)(outBuf[idx + 2] * (1.0f - alpha) + wb * alpha);
                        outBuf[idx + 3] = 255;
                    }
                    if (x0 == x1 && y0 == y1) break;
                    s32 e2 = 2 * err;
                    if (e2 >= dy) { err += dy; x0 += sx; }
                    if (e2 <= dx) { err += dx; y0 += sy; }
                }
            }
        }
    }
    /* Mode 1 = Clean (base texture only) — outBuf already has the composite */

    /* Ensure exports/ directory exists */
    if (fsCreateDir("exports") < 0 && errno != EEXIST) {
        sysLogPrintf(LOG_WARNING, "skin_editor: cannot create 'exports/' (errno %d)", errno);
        free(outBuf);
        return false;
    }

    /* Build full path */
    char fullPath[512];
    snprintf(fullPath, sizeof(fullPath), "exports/%s", filename);

    /* Ensure filename ends with .png */
    s32 len = (s32)strlen(fullPath);
    if (len < 4 || strcmp(&fullPath[len - 4], ".png") != 0) {
        if (len + 4 < (s32)sizeof(fullPath)) {
            strcat(fullPath, ".png");
        }
    }

    /* Write PNG via stb_image_write */
    s32 result = stbi_write_png(fullPath, cw, ch, 4, outBuf, cw * 4);
    free(outBuf);

    if (result) {
        sysLogPrintf(LOG_NOTE, "skin_editor: exported template to '%s' (%dx%d)",
                     fullPath, cw, ch);
    } else {
        sysLogPrintf(LOG_WARNING, "skin_editor: failed to write '%s'", fullPath);
    }

    return result != 0;
}

static void renderExportDialog(float scale)
{
    if (!s_ExportDialogOpen) return;

    ImGui::OpenPopup("Export Template");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(380.0f * scale, 0));

    if (ImGui::BeginPopupModal("Export Template", &s_ExportDialogOpen,
            ImGuiWindowFlags_AlwaysAutoResize)) {

        /* Character name display */
        if (s_SelectedChar >= 0 && s_SelectedChar < s_NumCharEntries) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
                               "Character: %s", s_CharEntries[s_SelectedChar].name);
        }
        ImGui::Spacing();

        /* Export mode radio buttons */
        ImGui::Text("Export Mode:");
        if (ImGui::RadioButton("Template (with UV guide)", s_ExportMode == 0)) {
            s_ExportMode = 0;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("  Base + wireframe overlay");

        if (ImGui::RadioButton("Clean (base texture only)", s_ExportMode == 1)) {
            s_ExportMode = 1;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("  Texture only");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        /* Filename input */
        ImGui::Text("Filename:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##export_fn", s_ExportFilename, sizeof(s_ExportFilename));
        ImGui::TextDisabled("Saved to exports/ folder");

        ImGui::Spacing();

        /* Export / Cancel buttons */
        float btnW = 140.0f * scale;
        if (PdButton("Export", ImVec2(btnW, 0))) {
            if (exportSkinTemplate(s_ExportFilename, s_ExportMode)) {
                snprintf(s_ExportStatus, sizeof(s_ExportStatus),
                         "Exported to exports/%s", s_ExportFilename);
                s_ExportOk = true;
                s_ExportDialogOpen = false;
                pdguiPlaySound(PDGUI_SND_SUCCESS);
            } else {
                snprintf(s_ExportStatus, sizeof(s_ExportStatus), "Export failed!");
                s_ExportOk = false;
                pdguiPlaySound(PDGUI_SND_ERROR);
            }
        }
        ImGui::SameLine();
        if (PdButton("Cancel", ImVec2(btnW, 0))) {
            s_ExportDialogOpen = false;
        }

        if (s_ExportStatus[0]) {
            ImGui::TextColored(s_ExportOk ? ImVec4(0,1,0,1) : ImVec4(1,0.3f,0.3f,1),
                               "%s", s_ExportStatus);
        }

        ImGui::EndPopup();
    }
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

    /* S-4/S-5/S-6 + Export: Popup dialogs (rendered after panels) */
    renderSaveDialog(scale);
    renderImportDialog(scale);
    renderDownrezDialog(scale);
    renderExportDialog(scale);

    /* Ctrl+S shortcut for save */
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
        s_SaveDialogOpen = true;
        s_SaveStatus[0] = '\0';
    }
}

} /* extern "C" */
