/**
 * pdgui_effects.cpp -- Animated UI overlay effects (P4)
 *
 * Frame-rate independent animated effects composited via ImDrawList:
 *   - Caustic: UV-scrolling mask overlay for water/energy feel
 *   - Glow pulse: breathing border glow on focused elements
 *   - Gradient sweep: directional color sweep on selection
 *
 * All effects use accumulated time from ImGui::GetTime() for animation.
 * Delta time is passed via pdguiEffectsUpdate() for future use.
 *
 * IMPORTANT: Do NOT include types.h -- it #defines bool as s32, breaking C++.
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 */

#include <math.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "imgui/imgui.h"
#include "pdgui_effects.h"
#include "system.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* =========================================================================
 * State
 * ========================================================================= */

static f32 s_Time = 0.0f;         /* accumulated time for animations */
static f32 s_DeltaTime = 0.0f;    /* last frame delta */
static s32 s_EffectsInitDone = 0;

/* =========================================================================
 * Color conversion: 0xRRGGBBAA → ImU32 (ABGR)
 * ========================================================================= */

static inline ImU32 FxCol(u32 rgba)
{
    u8 r = (u8)((rgba >> 24) & 0xFFu);
    u8 g = (u8)((rgba >> 16) & 0xFFu);
    u8 b = (u8)((rgba >>  8) & 0xFFu);
    u8 a = (u8)((rgba >>  0) & 0xFFu);
    return IM_COL32(r, g, b, a);
}

static inline ImU32 FxColA(u32 rgba, u8 alpha)
{
    u8 r = (u8)((rgba >> 24) & 0xFFu);
    u8 g = (u8)((rgba >> 16) & 0xFFu);
    u8 b = (u8)((rgba >>  8) & 0xFFu);
    return IM_COL32(r, g, b, alpha);
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

extern "C" {

void pdguiEffectsInit(void)
{
    if (s_EffectsInitDone) return;
    s_EffectsInitDone = 1;
    s_Time = 0.0f;
    s_DeltaTime = 0.0f;
    sysLogPrintf(LOG_NOTE, "PDGUI effects: initialized");
}

void pdguiEffectsUpdate(f32 deltaTime)
{
    s_DeltaTime = deltaTime;
    s_Time += deltaTime;
}

void pdguiEffectsShutdown(void)
{
    s_EffectsInitDone = 0;
    sysLogPrintf(LOG_NOTE, "PDGUI effects: shutdown");
}

/* =========================================================================
 * Default configs
 * ========================================================================= */

pdeffect_caustic_t pdguiEffectDefaultCaustic(void)
{
    pdeffect_caustic_t c;
    memset(&c, 0, sizeof(c));
    c.scrollSpeedX = 0.05f;
    c.scrollSpeedY = 0.03f;
    c.scale        = 1.0f;
    c.opacity      = 0.15f;
    c.tint         = 0xFFFFFF80u;
    c.enabled      = 1;
    return c;
}

pdeffect_glow_pulse_t pdguiEffectDefaultGlowPulse(void)
{
    pdeffect_glow_pulse_t g;
    memset(&g, 0, sizeof(g));
    g.pulseSpeed = 2.0f;
    g.minAlpha   = 0.1f;
    g.maxAlpha   = 0.6f;
    g.glowSize   = 3.0f;
    g.color      = 0x00C0FF80u;   /* cyan-blue glow */
    g.enabled    = 1;
    return g;
}

pdeffect_grad_sweep_t pdguiEffectDefaultGradSweep(void)
{
    pdeffect_grad_sweep_t s;
    memset(&s, 0, sizeof(s));
    s.sweepSpeed  = 200.0f;
    s.sweepWidth  = 40.0f;
    s.opacity     = 0.3f;
    s.color       = 0xFFFFFF40u;
    s.horizontal  = 1;
    s.enabled     = 1;
    return s;
}

/* =========================================================================
 * Caustic overlay
 *
 * If a mask texture is provided, it's drawn with scrolling UVs to create
 * a water/energy caustic effect. Without a mask, we draw a procedural
 * animated gradient approximation.
 * ========================================================================= */

void pdguiEffectDrawCaustic(f32 x, f32 y, f32 w, f32 h,
                            void *maskTex,
                            const pdeffect_caustic_t *cfg)
{
    if (!cfg || !cfg->enabled || cfg->opacity <= 0.0f) return;
    if (w <= 0.0f || h <= 0.0f) return;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    float t = (float)ImGui::GetTime();

    u8 alpha = (u8)(cfg->opacity * 255.0f);
    ImU32 col = FxColA(cfg->tint, alpha);

    if (maskTex) {
        /* Textured caustic: scroll UVs over time */
        float uOff = t * cfg->scrollSpeedX;
        float vOff = t * cfg->scrollSpeedY;
        float sc   = cfg->scale;

        /* Fractional UV offset for seamless scrolling */
        uOff = uOff - floorf(uOff);
        vOff = vOff - floorf(vOff);

        float u0 = uOff;
        float v0 = vOff;
        float u1 = uOff + (w / 64.0f) * sc;
        float v1 = vOff + (h / 64.0f) * sc;

        dl->AddImage((ImTextureID)(uintptr_t)maskTex,
                     ImVec2(x, y), ImVec2(x + w, y + h),
                     ImVec2(u0, v0), ImVec2(u1, v1),
                     col);
    } else {
        /* Procedural caustic: two overlapping semi-transparent gradients
         * scrolling in different directions. Creates a rough shimmer. */
        float phase1 = sinf(t * 1.5f) * 0.5f + 0.5f;
        float phase2 = cosf(t * 1.1f + 1.0f) * 0.5f + 0.5f;

        u8 a1 = (u8)(alpha * phase1 * 0.5f);
        u8 a2 = (u8)(alpha * phase2 * 0.5f);

        ImU32 c1 = FxColA(cfg->tint, a1);
        ImU32 c2 = FxColA(cfg->tint, a2);
        ImU32 c0 = IM_COL32(0, 0, 0, 0);

        /* Diagonal gradient pass 1 */
        dl->AddRectFilledMultiColor(
            ImVec2(x, y), ImVec2(x + w, y + h),
            c1, c0, c1, c0);

        /* Diagonal gradient pass 2 (rotated) */
        dl->AddRectFilledMultiColor(
            ImVec2(x, y), ImVec2(x + w, y + h),
            c0, c2, c0, c2);
    }
}

/* =========================================================================
 * Border glow pulse
 *
 * Draws expanding/contracting glow rectangles around the border,
 * with alpha modulated by a sine wave for a breathing effect.
 * ========================================================================= */

void pdguiEffectDrawGlowPulse(f32 x, f32 y, f32 w, f32 h,
                              const pdeffect_glow_pulse_t *cfg)
{
    if (!cfg || !cfg->enabled) return;
    if (w <= 0.0f || h <= 0.0f) return;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    float t = (float)ImGui::GetTime();

    /* Sine wave breathing: oscillates between minAlpha and maxAlpha */
    float phase = sinf(t * cfg->pulseSpeed * (float)(2.0 * M_PI)) * 0.5f + 0.5f;
    float alpha = cfg->minAlpha + (cfg->maxAlpha - cfg->minAlpha) * phase;

    u8 a = (u8)(alpha * 255.0f);
    ImU32 col = FxColA(cfg->color, a);

    float gs = cfg->glowSize * (0.8f + 0.2f * phase); /* subtle size pulse */

    /* Draw glow as expanding border rects (2 passes for soft falloff) */
    for (int pass = 0; pass < 2; pass++) {
        float expand = gs * (float)(pass + 1) * 0.6f;
        u8 pa = (u8)(a / (pass + 1));
        ImU32 pc = FxColA(cfg->color, pa);

        /* Top */
        dl->AddRectFilled(
            ImVec2(x - expand, y - expand),
            ImVec2(x + w + expand, y), pc);
        /* Bottom */
        dl->AddRectFilled(
            ImVec2(x - expand, y + h),
            ImVec2(x + w + expand, y + h + expand), pc);
        /* Left */
        dl->AddRectFilled(
            ImVec2(x - expand, y),
            ImVec2(x, y + h), pc);
        /* Right */
        dl->AddRectFilled(
            ImVec2(x + w, y),
            ImVec2(x + w + expand, y + h), pc);
    }
}

/* =========================================================================
 * Gradient sweep
 *
 * A bright gradient band that travels across the element, creating a
 * selection/highlight sweep effect. Wraps around when it reaches the end.
 * ========================================================================= */

void pdguiEffectDrawGradSweep(f32 x, f32 y, f32 w, f32 h,
                              const pdeffect_grad_sweep_t *cfg)
{
    if (!cfg || !cfg->enabled || cfg->opacity <= 0.0f) return;
    if (w <= 0.0f || h <= 0.0f) return;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    float t = (float)ImGui::GetTime();

    float extent = cfg->horizontal ? w : h;
    float totalTravel = extent + cfg->sweepWidth;

    /* Position within the sweep cycle */
    float pos = fmodf(t * cfg->sweepSpeed, totalTravel) - cfg->sweepWidth;

    u8 peakA = (u8)(cfg->opacity * 255.0f);
    ImU32 bright = FxColA(cfg->color, peakA);
    ImU32 dim    = IM_COL32(0, 0, 0, 0);

    if (cfg->horizontal) {
        float sx = x + pos;
        float ex = sx + cfg->sweepWidth;

        /* Clip to element bounds */
        if (sx < x) sx = x;
        if (ex > x + w) ex = x + w;
        if (sx >= ex) return;

        dl->AddRectFilledMultiColor(
            ImVec2(sx, y), ImVec2(ex, y + h),
            bright, dim, dim, bright);
    } else {
        float sy = y + pos;
        float ey = sy + cfg->sweepWidth;

        if (sy < y) sy = y;
        if (ey > y + h) ey = y + h;
        if (sy >= ey) return;

        dl->AddRectFilledMultiColor(
            ImVec2(x, sy), ImVec2(x + w, ey),
            bright, bright, dim, dim);
    }
}

} /* extern "C" */
