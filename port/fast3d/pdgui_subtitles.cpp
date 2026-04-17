/**
 * pdgui_subtitles.cpp -- ImGui-based subtitle renderer.
 *
 * Draws HUDMSGTYPE_INGAMESUBTITLE and HUDMSGTYPE_CUTSCENESUBTITLE entries
 * from g_HudMessages[] as a bottom-center panel with a dim backdrop.
 * Text/opacity snapshotted via pdguiSubtitlesSnapshot in pdgui_bridge.c so
 * this file does not have to #include types.h (bool conflict).
 *
 * Scales with pdguiScale so the panel is proportional on any resolution.
 * Does not steal focus, does not capture input.  Rendered on the
 * foreground so cutscene letterbox bars do not occlude it.
 *
 * IMPORTANT: C++ file -- must NOT include types.h.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>

#include "imgui/imgui.h"
#include "pdgui_subtitles.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"

extern "C" {

/* Shape mirrored in pdgui_bridge.c.  Keep in sync. */
struct pdguiSubtitleEntry {
    const char *text;
    u32 textcolour;
    u32 glowcolour;
    u8  opacity;
    u8  is_cutscene;
    u8  _pad[2];
};

s32 pdguiSubtitlesSnapshot(struct pdguiSubtitleEntry *out, s32 maxOut);

} /* extern "C" */

/* Maximum simultaneous subtitle lines to render.  Rare is 2 in practice
 * (in-game ambient + cutscene line); 4 leaves headroom. */
#define PDGUI_SUBTITLES_MAX 4

static ImU32 rgba32ToImU32(u32 rgba, u8 opacity)
{
    u32 r = (rgba >> 24) & 0xFF;
    u32 g = (rgba >> 16) & 0xFF;
    u32 b = (rgba >>  8) & 0xFF;
    u32 a = (rgba >>  0) & 0xFF;
    /* Modulate by external opacity (0-255) */
    a = (a * (u32)opacity) / 255;
    return IM_COL32(r, g, b, a);
}

void pdguiSubtitlesRender(s32 winW, s32 winH)
{
    if (winW <= 0 || winH <= 0) {
        return;
    }

    pdguiSubtitleEntry entries[PDGUI_SUBTITLES_MAX];
    s32 count = pdguiSubtitlesSnapshot(entries, PDGUI_SUBTITLES_MAX);
    if (count <= 0) {
        return;
    }

    /* Layout: bottom-center panel, width ~ 60% of screen (clamped to 800px).
     * Lines stack vertically, cutscene subtitles share the same panel so
     * an in-game ambient line does not shift when a cutscene line appears. */
    const float panelWidth    = pdguiScale(560.0f);
    const float panelPadX     = pdguiScale(18.0f);
    const float panelPadY     = pdguiScale(8.0f);
    const float lineSpacing   = pdguiScale(4.0f);
    const float bottomMargin  = pdguiScale(46.0f);
    const float bgAlpha       = 0.68f;

    /* Measure wrapped height so panel grows with content.  Using ImGui's
     * default font (theme/font swap also applies here). */
    ImFont *font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize();
    const float wrapWidth = panelWidth - panelPadX * 2.0f;

    float totalTextHeight = 0.0f;
    for (s32 i = 0; i < count; i++) {
        const char *txt = entries[i].text ? entries[i].text : "";
        if (!txt[0]) continue;
        ImVec2 sz = font->CalcTextSizeA(fontSize, FLT_MAX, wrapWidth, txt);
        totalTextHeight += sz.y;
        if (i + 1 < count) totalTextHeight += lineSpacing;
    }

    if (totalTextHeight <= 0.0f) {
        return;
    }

    const float panelHeight = totalTextHeight + panelPadY * 2.0f;
    const float panelX = ((float)winW - panelWidth) * 0.5f;
    const float panelY = (float)winH - bottomMargin - panelHeight;

    /* Draw directly to the foreground draw list so cutscene letterbox
     * bars do not cover the panel.  No ImGui::Begin() so we do not push
     * an interactive window / waste a window id each frame. */
    ImDrawList *dl = ImGui::GetForegroundDrawList();

    /* Backdrop: rounded, dim. */
    dl->AddRectFilled(
        ImVec2(panelX, panelY),
        ImVec2(panelX + panelWidth, panelY + panelHeight),
        IM_COL32(0, 0, 0, (int)(bgAlpha * 255.0f)),
        pdguiScale(6.0f));

    /* Optional subtle border — theme accent with low alpha so the panel
     * feels like part of the ImGui menu chrome family. */
    dl->AddRect(
        ImVec2(panelX, panelY),
        ImVec2(panelX + panelWidth, panelY + panelHeight),
        IM_COL32(255, 255, 255, 32),
        pdguiScale(6.0f),
        0, 1.0f);

    /* Lines. */
    float cursorY = panelY + panelPadY;
    const float textX = panelX + panelPadX;

    for (s32 i = 0; i < count; i++) {
        const char *txt = entries[i].text ? entries[i].text : "";
        if (!txt[0]) continue;

        ImVec2 sz = font->CalcTextSizeA(fontSize, FLT_MAX, wrapWidth, txt);

        /* Shadow for readability (behind text). */
        ImU32 shadow = rgba32ToImU32(entries[i].glowcolour, entries[i].opacity);
        ImU32 fg     = rgba32ToImU32(entries[i].textcolour, entries[i].opacity);

        /* Horizontally center the wrapped block within the padded area. */
        const float lineX = textX + (wrapWidth - sz.x) * 0.5f;

        /* 1-px drop shadow.  Cheap and legible. */
        dl->AddText(font, fontSize,
                    ImVec2(lineX + 1.0f, cursorY + 1.0f),
                    shadow, txt, nullptr, wrapWidth);

        dl->AddText(font, fontSize,
                    ImVec2(lineX, cursorY),
                    fg, txt, nullptr, wrapWidth);

        cursorY += sz.y;
        if (i + 1 < count) cursorY += lineSpacing;
    }
}
