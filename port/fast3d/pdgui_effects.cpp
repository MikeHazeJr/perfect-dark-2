/**
 * pdgui_effects.cpp -- Animated overlay effects for UI elements (P4)
 *
 * Two effect systems:
 * 1. Caustic mask overlay: scrolling grayscale mask composited over UI elements
 * 2. Border effect mask: animated glow/sweep on element edges
 *
 * All effects use delta time for frame-rate independence.
 * Rendering is done via ImGui draw list API.
 *
 * IMPORTANT: Do NOT include types.h -- it #defines bool as s32, breaking C++.
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 * Part of Phase P4: UI Texture Mod.
 */

#include <string.h>
#include <math.h>
#include <PR/ultratypes.h>

#include "imgui/imgui.h"
#include "pdgui_effects.h"
#include "pdgui_theme.h"
#include "pdgui_style.h"
#include "system.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* =========================================================================
 * Active effect state
 * ========================================================================= */

#define CAUSTIC_TEX_ID_LEN 64

static PdguiCausticConfig  s_CausticCfg;
static PdguiBorderFxConfig s_BorderFxCfg;
static char                s_CausticTexId[CAUSTIC_TEX_ID_LEN] = {0};
static f32                 s_CausticTimeAccum = 0.0f;
static s32                 s_EffectsInitDone = 0;

static void s_ensureDefaults(void)
{
    if (s_EffectsInitDone) return;
    s_EffectsInitDone = 1;
    s_CausticCfg = pdguiCausticDefaultConfig();
    s_BorderFxCfg = pdguiBorderFxDefaultConfig();
}

/* =========================================================================
 * Caustic / Animated Mask Overlay
 * ========================================================================= */

extern "C" {

PdguiCausticConfig pdguiCausticDefaultConfig(void)
{
    PdguiCausticConfig cfg;
    cfg.scroll_speed_x = 0.02f;
    cfg.scroll_speed_y = 0.015f;
    cfg.scale = 1.0f;
    cfg.opacity = 0.15f;
    cfg.tint_color = 0xffffffffu;  /* no tint */
    cfg.blend_additive = 0;
    return cfg;
}

void pdguiCausticDraw(void *mask_tex, f32 x, f32 y, f32 w, f32 h,
                      const PdguiCausticConfig *config, f32 dt)
{
    if (!mask_tex || !config || config->opacity <= 0.0f) return;
    if (w <= 0.0f || h <= 0.0f) return;

    s_CausticTimeAccum += dt;

    ImDrawList *dl = ImGui::GetWindowDrawList();

    /* Compute scrolling UV offset */
    float uOff = s_CausticTimeAccum * config->scroll_speed_x;
    float vOff = s_CausticTimeAccum * config->scroll_speed_y;

    /* Wrap to [0, 1) */
    uOff = uOff - floorf(uOff);
    vOff = vOff - floorf(vOff);

    /* UV range: scale controls tiling density */
    float uScale = config->scale;
    float vScale = config->scale * (h / w);  /* maintain aspect */

    float u0 = uOff;
    float v0 = vOff;
    float u1 = uOff + uScale;
    float v1 = vOff + vScale;

    /* Compute tint with opacity applied */
    u32 tc = config->tint_color;
    u8 tr = (u8)((tc >> 24) & 0xff);
    u8 tg = (u8)((tc >> 16) & 0xff);
    u8 tb = (u8)((tc >>  8) & 0xff);
    u8 ta = (u8)(config->opacity * 255.0f);

    ImU32 col = IM_COL32(tr, tg, tb, ta);

    dl->AddImage((ImTextureID)mask_tex,
                 ImVec2(x, y), ImVec2(x + w, y + h),
                 ImVec2(u0, v0), ImVec2(u1, v1),
                 col);
}

s32 pdguiCausticDrawThemed(f32 x, f32 y, f32 w, f32 h)
{
    s_ensureDefaults();

    if (!s_CausticTexId[0]) return 0;
    if (s_CausticCfg.opacity <= 0.0f) return 0;

    void *tex = pdguiThemeGetTexture(s_CausticTexId);
    if (!tex) return 0;

    float dt = ImGui::GetIO().DeltaTime;
    pdguiCausticDraw(tex, x, y, w, h, &s_CausticCfg, dt);
    return 1;
}

/* =========================================================================
 * Border Effect Mask
 * ========================================================================= */

/**
 * Glow Pulse: soft glow that breathes on all 4 edges.
 * Uses sin(time) for smooth pulsing.
 */
static void s_drawGlowPulse(ImDrawList *dl, f32 x, f32 y, f32 w, f32 h,
                             const PdguiBorderFxConfig *cfg, s32 focused)
{
    float t = (float)ImGui::GetTime() * cfg->speed;
    float pulse = 0.5f + 0.5f * sinf(t * 3.0f);
    float intensity = cfg->intensity * (focused ? (0.6f + 0.4f * pulse) : (0.2f + 0.3f * pulse));

    /* Resolve color: use palette accent if config color is 0 */
    u32 cc = cfg->color;
    if (cc == 0) {
        cc = pdguiGetPaletteColor(PDPAL_BORDER2);
    }
    u8 cr = (u8)((cc >> 24) & 0xff);
    u8 cg = (u8)((cc >> 16) & 0xff);
    u8 cb = (u8)((cc >>  8) & 0xff);

    float bw = cfg->width;
    u8 alpha = (u8)(intensity * 200.0f);

    ImU32 glowCol = IM_COL32(cr, cg, cb, alpha);
    ImU32 glowDim = IM_COL32(cr, cg, cb, 0);

    /* Top edge glow */
    dl->AddRectFilledMultiColor(
        ImVec2(x, y - bw), ImVec2(x + w, y),
        glowDim, glowDim, glowCol, glowCol);

    /* Bottom edge glow */
    dl->AddRectFilledMultiColor(
        ImVec2(x, y + h), ImVec2(x + w, y + h + bw),
        glowCol, glowCol, glowDim, glowDim);

    /* Left edge glow */
    dl->AddRectFilledMultiColor(
        ImVec2(x - bw, y), ImVec2(x, y + h),
        glowDim, glowCol, glowCol, glowDim);

    /* Right edge glow */
    dl->AddRectFilledMultiColor(
        ImVec2(x + w, y), ImVec2(x + w + bw, y + h),
        glowCol, glowDim, glowDim, glowCol);
}

/**
 * Gradient Sweep: a bright gradient travels around the perimeter.
 * Similar to the existing shimmer but with configurable color.
 */
static void s_drawGradSweep(ImDrawList *dl, f32 x, f32 y, f32 w, f32 h,
                            const PdguiBorderFxConfig *cfg, s32 focused)
{
    float t = (float)ImGui::GetTime() * cfg->speed;
    float perim = 2.0f * (w + h);
    if (perim < 10.0f) return;

    float frac = t / 4.0f;  /* full sweep in 4 seconds */
    frac = frac - floorf(frac);
    float pos = frac * perim;

    /* Resolve color */
    u32 cc = cfg->color;
    if (cc == 0) {
        cc = pdguiGetPaletteColor(PDPAL_BORDER2);
    }
    u8 cr = (u8)((cc >> 24) & 0xff);
    u8 cg = (u8)((cc >> 16) & 0xff);
    u8 cb = (u8)((cc >>  8) & 0xff);

    float sweepLen = 40.0f;
    float intensity = cfg->intensity * (focused ? 1.0f : 0.5f);
    u8 peakAlpha = (u8)(intensity * 255.0f);

    ImU32 bright = IM_COL32(cr, cg, cb, peakAlpha);
    ImU32 dim    = IM_COL32(cr, cg, cb, 0);

    float bw = cfg->width;

    /* Compute which edge segment the sweep center is on */
    float seg0 = w;           /* top: left→right */
    float seg1 = seg0 + h;   /* right: top→bottom */
    float seg2 = seg1 + w;   /* bottom: right→left */
    /* seg3: left: bottom→top, remainder */

    if (pos < seg0) {
        /* Top edge */
        float cx = x + pos;
        float sx0 = cx - sweepLen * 0.5f;
        float sx1 = cx + sweepLen * 0.5f;
        if (sx0 < x) sx0 = x;
        if (sx1 > x + w) sx1 = x + w;
        if (sx1 > sx0) {
            dl->AddRectFilledMultiColor(
                ImVec2(sx0, y - bw), ImVec2(sx1, y),
                dim, bright, bright, dim);
        }
    } else if (pos < seg1) {
        /* Right edge */
        float cy = y + (pos - seg0);
        float sy0 = cy - sweepLen * 0.5f;
        float sy1 = cy + sweepLen * 0.5f;
        if (sy0 < y) sy0 = y;
        if (sy1 > y + h) sy1 = y + h;
        if (sy1 > sy0) {
            dl->AddRectFilledMultiColor(
                ImVec2(x + w, sy0), ImVec2(x + w + bw, sy1),
                dim, dim, bright, bright);
        }
    } else if (pos < seg2) {
        /* Bottom edge (right→left) */
        float cx = x + w - (pos - seg1);
        float sx0 = cx - sweepLen * 0.5f;
        float sx1 = cx + sweepLen * 0.5f;
        if (sx0 < x) sx0 = x;
        if (sx1 > x + w) sx1 = x + w;
        if (sx1 > sx0) {
            dl->AddRectFilledMultiColor(
                ImVec2(sx0, y + h), ImVec2(sx1, y + h + bw),
                bright, dim, dim, bright);
        }
    } else {
        /* Left edge (bottom→top) */
        float cy = y + h - (pos - seg2);
        float sy0 = cy - sweepLen * 0.5f;
        float sy1 = cy + sweepLen * 0.5f;
        if (sy0 < y) sy0 = y;
        if (sy1 > y + h) sy1 = y + h;
        if (sy1 > sy0) {
            dl->AddRectFilledMultiColor(
                ImVec2(x - bw, sy0), ImVec2(x, sy1),
                bright, bright, dim, dim);
        }
    }
}

/**
 * Energy Field: rapid flicker with random-ish alpha variation.
 */
static void s_drawEnergyField(ImDrawList *dl, f32 x, f32 y, f32 w, f32 h,
                              const PdguiBorderFxConfig *cfg, s32 focused)
{
    float t = (float)ImGui::GetTime() * cfg->speed;

    /* Pseudo-random flicker from layered sin waves */
    float flicker = 0.5f + 0.3f * sinf(t * 7.3f) + 0.2f * sinf(t * 13.1f);
    if (flicker < 0.0f) flicker = 0.0f;
    if (flicker > 1.0f) flicker = 1.0f;

    float intensity = cfg->intensity * flicker * (focused ? 1.0f : 0.4f);

    u32 cc = cfg->color;
    if (cc == 0) {
        cc = pdguiGetPaletteColor(PDPAL_BORDER2);
    }
    u8 cr = (u8)((cc >> 24) & 0xff);
    u8 cg = (u8)((cc >> 16) & 0xff);
    u8 cb = (u8)((cc >>  8) & 0xff);
    u8 alpha = (u8)(intensity * 180.0f);

    float bw = cfg->width;
    ImU32 col = IM_COL32(cr, cg, cb, alpha);

    /* Draw border rect (outline) */
    dl->AddRect(ImVec2(x - bw, y - bw), ImVec2(x + w + bw, y + h + bw),
                col, 0.0f, 0, bw);
}

PdguiBorderFxConfig pdguiBorderFxDefaultConfig(void)
{
    PdguiBorderFxConfig cfg;
    cfg.type = PDGUI_BORDERFX_GLOW_PULSE;
    cfg.intensity = 0.5f;
    cfg.speed = 1.0f;
    cfg.color = 0;  /* 0 = use palette accent */
    cfg.width = 2.0f;
    return cfg;
}

void pdguiBorderFxDraw(f32 x, f32 y, f32 w, f32 h,
                       const PdguiBorderFxConfig *config, s32 focused)
{
    if (!config || config->type == PDGUI_BORDERFX_NONE) return;
    if (config->intensity <= 0.0f) return;

    ImDrawList *dl = ImGui::GetWindowDrawList();

    switch (config->type) {
    case PDGUI_BORDERFX_GLOW_PULSE:
        s_drawGlowPulse(dl, x, y, w, h, config, focused);
        break;
    case PDGUI_BORDERFX_GRAD_SWEEP:
        s_drawGradSweep(dl, x, y, w, h, config, focused);
        break;
    case PDGUI_BORDERFX_ENERGY:
        s_drawEnergyField(dl, x, y, w, h, config, focused);
        break;
    default:
        break;
    }
}

void pdguiBorderFxDrawThemed(f32 x, f32 y, f32 w, f32 h, s32 focused)
{
    s_ensureDefaults();
    pdguiBorderFxDraw(x, y, w, h, &s_BorderFxCfg, focused);
}

/* =========================================================================
 * Theme Integration
 * ========================================================================= */

void pdguiEffectsSetCausticConfig(const PdguiCausticConfig *config)
{
    s_ensureDefaults();
    if (config) {
        s_CausticCfg = *config;
    }
}

void pdguiEffectsSetBorderFxConfig(const PdguiBorderFxConfig *config)
{
    s_ensureDefaults();
    if (config) {
        s_BorderFxCfg = *config;
    }
}

void pdguiEffectsSetCausticTexture(const char *catalog_id)
{
    s_ensureDefaults();
    if (catalog_id) {
        snprintf(s_CausticTexId, CAUSTIC_TEX_ID_LEN, "%s", catalog_id);
    } else {
        s_CausticTexId[0] = '\0';
    }
}

const char *pdguiEffectsGetCausticTexture(void)
{
    return s_CausticTexId[0] ? s_CausticTexId : NULL;
}

const PdguiCausticConfig *pdguiEffectsGetCausticConfig(void)
{
    s_ensureDefaults();
    return &s_CausticCfg;
}

const PdguiBorderFxConfig *pdguiEffectsGetBorderFxConfig(void)
{
    s_ensureDefaults();
    return &s_BorderFxCfg;
}

} /* extern "C" */
