/* Boot Overlay (Engine Phase 2)
 *
 * Centered progress modal plus phase + status label drawn directly via
 * ImGui DrawList primitives.  Reads boot_progress.h state each frame and
 * runs in a tight pump loop on the main thread while the boot worker
 * does catalog / extract / verify work.
 *
 * Independent of pdguiNewFrame / pdguiRender, both of which gate on
 * game state we don't have yet at boot time.  Owns its own ImGui frame
 * begin/end inside the gfx_run_boot_overlay_frame callback.
 *
 * Colors are pulled from the active palette via pdguiGetActivePaletteRaw
 * so theme switches automatically apply.  No textures are loaded — the
 * theme texture cache is not yet populated when boot runs.
 */

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "glad/glad.h"

/* gfx_api.h declares void gfx_run(Gfx* commands), so the translation unit
 * needs the Gfx type from PR/gbi.h before including the gfx API. */
#include <PR/ultratypes.h>
#include <PR/gbi.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_opengl3.h"

#include "pdgui_bootoverlay.h"
#include "pdgui_style.h"
#include "boot_progress.h"
#include "gfx_api.h"
#include "system.h"

extern "C" s32 g_NetDedicated;

namespace {

bool   s_Initialized = false;
double s_StartTime   = 0.0;
float  s_DisplayedProgress = 0.0f;  /* eased value for smoother bar */

/* 0xRRGGBBAA -> ImU32 */
ImU32 PdColRGBA(uint32_t rgba)
{
    uint8_t r = (uint8_t)((rgba >> 24) & 0xffu);
    uint8_t g = (uint8_t)((rgba >> 16) & 0xffu);
    uint8_t b = (uint8_t)((rgba >>  8) & 0xffu);
    uint8_t a = (uint8_t)((rgba >>  0) & 0xffu);
    return IM_COL32(r, g, b, a);
}

/* Fallback Blue palette (matches pdgui_theme.cpp k_PalBlue).  Used if
 * the style layer hasn't initialized its palette pointer yet. */
const uint32_t k_PaletteFallback[15] = {
    0x0060bf7fu, 0x0000507fu, 0x00f0ff7fu, 0xffffffffu,
    0x00002f9fu, 0x00006f7fu, 0x00ffffffu, 0x007f7fffu,
    0xffffffffu, 0x8fffffffu, 0x000044ffu, 0x000030ffu,
    0x7f7fffffu, 0xffffffffu, 0x6644ff7fu
};

const uint32_t *activePalette()
{
    const uint32_t *pal = (const uint32_t *)pdguiGetActivePaletteRaw();
    return pal ? pal : k_PaletteFallback;
}

/* Draws the overlay into the current ImGui frame.  Called from inside
 * the gfx_run_boot_overlay_frame callback after ImGui::NewFrame. */
void drawOverlay(float screenW, float screenH)
{
    boot_progress_snapshot_t snap;
    bootProgressSnapshot(&snap);

    /* Ease the displayed value toward target so per-file updates from a
     * tight loop don't strobe the bar.  ~30% per frame at 60Hz is gentle
     * but visible. */
    float target = snap.overall_progress;
    if (target > s_DisplayedProgress) {
        s_DisplayedProgress += (target - s_DisplayedProgress) * 0.30f;
    } else {
        s_DisplayedProgress = target;
    }

    const uint32_t *pal = activePalette();
    const ImU32 colBarFill   = PdColRGBA(pal[2]);   /* dialog_border2 -- bright cyan */
    const ImU32 colBarTrack  = PdColRGBA(pal[1]);   /* dialog_titlebg -- dark navy   */
    const ImU32 colBarBorder = PdColRGBA(pal[0]);   /* dialog_border1 -- medium blue */
    const ImU32 colTextMain  = PdColRGBA(pal[3]);   /* dialog_titlefg -- white       */
    const ImU32 colTextDim   = PdColRGBA(pal[7]);   /* item_disabled  -- muted blue  */

    /* Full-screen background fill (very dark navy with full alpha so the
     * overlay reads as a real loading screen, not a transparent strip). */
    {
        ImDrawList *bg = ImGui::GetBackgroundDrawList();
        bg->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(screenW, screenH),
                          IM_COL32(0, 8, 20, 255));
    }

    ImDrawList *fg = ImGui::GetForegroundDrawList();

    const float panelW = (screenW < 760.0f) ? screenW - 48.0f : 640.0f;
    const float panelH = 148.0f;
    const float panelX = (screenW - panelW) * 0.5f;
    const float panelY = (screenH - panelH) * 0.5f - screenH * 0.04f;
    const float padX = 28.0f;
    const float barH = 16.0f;
    const float barX = panelX + padX;
    const float barW = panelW - padX * 2.0f;
    const float barY = panelY + 88.0f;

    fg->AddRectFilled(ImVec2(panelX, panelY),
                      ImVec2(panelX + panelW, panelY + panelH),
                      IM_COL32(0, 18, 38, 236), 8.0f);
    fg->AddRect(ImVec2(panelX, panelY),
                ImVec2(panelX + panelW, panelY + panelH),
                colBarBorder, 8.0f, 0, 1.0f);

    /* Track */
    fg->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH),
                      colBarTrack, 4.0f);

    /* Fill */
    float fillW = barW * s_DisplayedProgress;
    if (fillW < 0.0f) fillW = 0.0f;
    if (fillW > barW) fillW = barW;
    if (fillW > 0.0f) {
        fg->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + fillW, barY + barH),
                          colBarFill, 4.0f);
    }

    /* Border (1px outline) */
    fg->AddRect(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH),
                colBarBorder, 4.0f, 0, 1.0f);

    /* Phase label: top of the modal, left aligned to the bar. Falls back
     * to "Loading..." until the worker reports its first phase. */
    const char *phaseLabel =
        snap.phase_label[0] ? snap.phase_label : "Loading...";

    /* Status label: above the bar, left aligned.  Empty when the phase
     * isn't iterated.  When iterated, append "(N / M)". */
    char statusBuf[BOOT_PROGRESS_LABEL_LEN + 64];
    if (snap.current_total > 0) {
        if (snap.status_label[0]) {
            snprintf(statusBuf, sizeof(statusBuf), "%s  (%d / %d)",
                     snap.status_label, snap.current_value, snap.current_total);
        } else {
            snprintf(statusBuf, sizeof(statusBuf), "%d / %d",
                     snap.current_value, snap.current_total);
        }
    } else {
        snprintf(statusBuf, sizeof(statusBuf), "%s",
                 snap.status_label[0] ? snap.status_label : "");
    }

    /* Use ImGui text rendering via the foreground draw list so we don't
     * need a Begin/End window for what is structurally a HUD overlay. */
    ImFont *font = ImGui::GetFont();
    if (!font) {
        return;
    }

    const float fontSizePhase = 22.0f;
    const float fontSizeStatus = 16.0f;

    ImVec2 phasePos(barX, panelY + 22.0f);
    fg->AddText(font, fontSizePhase, phasePos, colTextMain, phaseLabel);

    if (statusBuf[0]) {
        ImVec2 statusPos(barX, barY - fontSizeStatus - 10.0f);
        fg->AddText(font, fontSizeStatus, statusPos, colTextDim, statusBuf);
    }

    /* Percentage on the right side of the phase line */
    char pctBuf[16];
    snprintf(pctBuf, sizeof(pctBuf), "%d%%",
             (int)(s_DisplayedProgress * 100.0f + 0.5f));
    ImVec2 pctSize = font->CalcTextSizeA(fontSizePhase, FLT_MAX, 0.0f, pctBuf);
    ImVec2 pctPos(barX + barW - pctSize.x, panelY + 22.0f);
    fg->AddText(font, fontSizePhase, pctPos, colTextMain, pctBuf);
}

void overlayDrawCallback(void *user)
{
    (void)user;

    /* Begin ImGui frame (matches the begin/end ownership pattern of
     * gfx_run -> pdguiNewFrame/Render). */
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGuiIO &io = ImGui::GetIO();
    float screenW = (io.DisplaySize.x > 0.0f) ? io.DisplaySize.x : 1280.0f;
    float screenH = (io.DisplaySize.y > 0.0f) ? io.DisplaySize.y : 720.0f;

    drawOverlay(screenW, screenH);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

} /* namespace */

extern "C" {

void pdguiBootOverlayInit(void)
{
    if (s_Initialized) {
        return;
    }
    s_Initialized = true;
    s_StartTime = (double)SDL_GetTicks() / 1000.0;
    s_DisplayedProgress = 0.0f;
    sysLogPrintf(LOG_NOTE, "BOOT_OVERLAY: ready");
}

void pdguiBootOverlayShutdown(void)
{
    if (!s_Initialized) {
        return;
    }
    s_Initialized = false;
    double elapsed = ((double)SDL_GetTicks() / 1000.0) - s_StartTime;
    sysLogPrintf(LOG_NOTE, "BOOT_OVERLAY: dismissed (visible for %.2fs)", elapsed);
}

void pdguiBootOverlayPump(void)
{
    if (g_NetDedicated) {
        /* No window, no overlay; just sleep briefly so the boot worker
         * isn't fighting the main thread for the run queue. */
        SDL_Delay(16);
        return;
    }

    if (!s_Initialized) {
        SDL_Delay(16);
        return;
    }

    gfx_run_boot_overlay_frame(overlayDrawCallback, NULL);

    /* gfx_end_frame: finish_render + swap_buffers_end.  videoEndFrame
     * also updates the window title once a second; that path is fine
     * here.  Direct call avoids the FPS counter side-effects of
     * videoEndFrame, which we don't want during boot. */
    extern void gfx_end_frame(void);
    gfx_end_frame();
}

} /* extern "C" */
