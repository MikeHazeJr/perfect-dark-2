/**
 * pdgui_fontmgr.cpp -- TTF font manager with glow/shadow for PD2 UI (P4)
 *
 * Manages multiple fonts loaded from mod packages. Provides text rendering
 * with shadow and glow effects using ImGui draw list primitives.
 *
 * Font atlas management:
 *   - Slot 0 is always the default Handel Gothic (loaded by pdgui_backend.cpp)
 *   - Additional fonts are loaded from TTF files and added to the atlas
 *   - Loading a new font requires atlas rebuild (invalidates textures)
 *   - Fonts are registered as catalog assets (base:font_*, mod:font_*)
 *
 * Glow rendering technique:
 *   Draw the text N times at increasing offsets in a circular pattern,
 *   each at reduced alpha. This creates a soft halo without shaders.
 *   The number of passes controls quality vs. performance.
 *
 * IMPORTANT: Do NOT include types.h -- it #defines bool as s32, breaking C++.
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <PR/ultratypes.h>

#include "imgui/imgui.h"
#include "pdgui_fontmgr.h"
#include "assetcatalog.h"
#include "system.h"
#include "fs.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* =========================================================================
 * Font slot storage
 * ========================================================================= */

struct font_slot {
    char     name[FONTMGR_NAME_LEN];
    char     catalog_id[64];
    char     path[FONTMGR_PATH_LEN];
    f32      size_px;
    ImFont  *imgui_font;    /* NULL for slot 0 (use io.FontDefault) */
    s32      valid;
};

static struct font_slot s_Fonts[FONTMGR_MAX_FONTS];
static s32 s_FontCount = 0;
static s32 s_ActiveSlot = 0;
static s32 s_FontMgrInitDone = 0;

/* Global effect settings */
static font_shadow_def_t s_Shadow = { 1.0f, 1.0f, 0x000000A0u };
static font_glow_def_t   s_Glow   = { 0.0f, 0.0f, 0x0080ffffu, 2 };

/* =========================================================================
 * Helpers
 * ========================================================================= */

/* 0xRRGGBBAA → ImU32 (ImGui packed ABGR) */
static inline ImU32 FmCol(u32 rgba)
{
    uint8_t r = (uint8_t)((rgba >> 24) & 0xffu);
    uint8_t g = (uint8_t)((rgba >> 16) & 0xffu);
    uint8_t b = (uint8_t)((rgba >>  8) & 0xffu);
    uint8_t a = (uint8_t)((rgba >>  0) & 0xffu);
    return IM_COL32(r, g, b, a);
}

static inline ImU32 FmColAlpha(u32 rgba, f32 alpha_scale)
{
    uint8_t r = (uint8_t)((rgba >> 24) & 0xffu);
    uint8_t g = (uint8_t)((rgba >> 16) & 0xffu);
    uint8_t b = (uint8_t)((rgba >>  8) & 0xffu);
    uint8_t a = (uint8_t)((rgba >>  0) & 0xffu);
    a = (uint8_t)((f32)a * alpha_scale);
    return IM_COL32(r, g, b, a);
}

/* =========================================================================
 * Public API
 * ========================================================================= */

extern "C" {

void pdguiFontMgrInit(void)
{
    if (s_FontMgrInitDone) return;
    s_FontMgrInitDone = 1;

    memset(s_Fonts, 0, sizeof(s_Fonts));

    /* Slot 0: default font (Handel Gothic, already loaded by pdgui_backend) */
    struct font_slot *s0 = &s_Fonts[0];
    snprintf(s0->name, sizeof(s0->name), "Handel Gothic");
    snprintf(s0->catalog_id, sizeof(s0->catalog_id), "base:font_handelgothic");
    s0->size_px = 24.0f;
    s0->imgui_font = nullptr; /* uses io.FontDefault */
    s0->valid = 1;
    s_FontCount = 1;

    /* Register in catalog */
    asset_entry_t *ae = assetCatalogRegister("base:font_handelgothic", ASSET_UI);
    if (ae) {
        snprintf(ae->category, CATALOG_CATEGORY_LEN, "base");
        ae->bundled    = 1;
        ae->enabled    = 1;
        ae->load_state = ASSET_STATE_LOADED;
        ae->ref_count  = ASSET_REF_BUNDLED;
    }

    sysLogPrintf(LOG_NOTE, "PDGUI fontmgr: init — default font registered");
}

void pdguiFontMgrShutdown(void)
{
    /* Font memory is owned by ImGui atlas — don't free it.
     * Just clear our metadata. */
    s_FontCount = 0;
    s_ActiveSlot = 0;
    s_FontMgrInitDone = 0;
    sysLogPrintf(LOG_NOTE, "PDGUI fontmgr: shutdown");
}

s32 pdguiFontMgrLoadFont(const char *name, const char *ttf_path, f32 size_px)
{
    if (!name || !ttf_path) return 0;
    if (s_FontCount >= FONTMGR_MAX_FONTS) {
        sysLogPrintf(LOG_WARNING,
            "PDGUI fontmgr: max fonts reached (%d), cannot load '%s'",
            FONTMGR_MAX_FONTS, name);
        return 0;
    }

    /* Load TTF file */
    u32 fileSize = 0;
    void *data = fsFileLoad(ttf_path, &fileSize);
    if (!data || fileSize == 0) {
        sysLogPrintf(LOG_WARNING,
            "PDGUI fontmgr: failed to load '%s'", ttf_path);
        if (data) free(data);
        return 0;
    }

    /* ImGui's AddFontFromMemoryTTF takes ownership of the buffer.
     * We must use ImGui::MemAlloc for the copy. */
    void *fontCopy = ImGui::MemAlloc(fileSize);
    memcpy(fontCopy, data, fileSize);
    free(data);

    ImGuiIO &io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = true;
    snprintf(cfg.Name, sizeof(cfg.Name), "%s", name);
    cfg.OversampleV = 2;

    ImFont *font = io.Fonts->AddFontFromMemoryTTF(
        fontCopy, (int)fileSize, size_px, &cfg);

    if (!font) {
        sysLogPrintf(LOG_WARNING,
            "PDGUI fontmgr: ImGui failed to load font '%s' from '%s'",
            name, ttf_path);
        return 0;
    }

    /* NOTE: After adding a font, the atlas must be rebuilt.
     * ImGui_ImplOpenGL3_DestroyFontsTexture() + io.Fonts->Build() +
     * ImGui_ImplOpenGL3_CreateFontsTexture() must be called.
     * This is deferred to the next frame start. */

    s32 slot = s_FontCount;
    struct font_slot *fs = &s_Fonts[slot];
    snprintf(fs->name, sizeof(fs->name), "%s", name);
    snprintf(fs->path, sizeof(fs->path), "%s", ttf_path);
    snprintf(fs->catalog_id, sizeof(fs->catalog_id), "mod:font_%s", name);
    fs->size_px = size_px;
    fs->imgui_font = font;
    fs->valid = 1;
    s_FontCount++;

    /* Register in catalog */
    asset_entry_t *ae = assetCatalogRegister(fs->catalog_id, ASSET_UI);
    if (ae) {
        snprintf(ae->category, CATALOG_CATEGORY_LEN, "mod");
        ae->bundled    = 0;
        ae->enabled    = 1;
        ae->load_state = ASSET_STATE_LOADED;
        ae->ref_count  = 1;
    }

    sysLogPrintf(LOG_NOTE,
        "PDGUI fontmgr: loaded '%s' from '%s' (%.0fpx) → slot %d",
        name, ttf_path, size_px, slot);

    return slot;
}

s32 pdguiFontMgrGetCount(void)
{
    return s_FontCount;
}

const char *pdguiFontMgrGetName(s32 slot)
{
    if (slot < 0 || slot >= s_FontCount) return nullptr;
    return s_Fonts[slot].name;
}

const char *pdguiFontMgrGetCatalogId(s32 slot)
{
    if (slot < 0 || slot >= s_FontCount) return nullptr;
    return s_Fonts[slot].catalog_id;
}

void pdguiFontMgrSetActive(s32 slot)
{
    if (slot < 0 || slot >= s_FontCount) slot = 0;
    s_ActiveSlot = slot;
}

s32 pdguiFontMgrGetActive(void)
{
    return s_ActiveSlot;
}

void pdguiFontMgrPushFont(s32 slot)
{
    if (slot < 0 || slot >= s_FontCount) slot = 0;

    ImFont *font = s_Fonts[slot].imgui_font;
    if (font) {
        ImGui::PushFont(font);
    } else {
        /* Slot 0: use default font (pushing NULL would crash) */
        ImGuiIO &io = ImGui::GetIO();
        if (io.FontDefault) {
            ImGui::PushFont(io.FontDefault);
        }
    }
}

void pdguiFontMgrPopFont(void)
{
    ImGui::PopFont();
}

/* -----------------------------------------------------------------------
 * Text effect configuration
 * --------------------------------------------------------------------- */

void pdguiFontMgrSetShadow(const font_shadow_def_t *def)
{
    if (def) s_Shadow = *def;
}

const font_shadow_def_t *pdguiFontMgrGetShadow(void)
{
    return &s_Shadow;
}

void pdguiFontMgrSetGlow(const font_glow_def_t *def)
{
    if (def) s_Glow = *def;
}

const font_glow_def_t *pdguiFontMgrGetGlow(void)
{
    return &s_Glow;
}

/* -----------------------------------------------------------------------
 * Text rendering with effects
 * --------------------------------------------------------------------- */

void pdguiFontMgrDrawText(float x, float y, u32 color, const char *text)
{
    if (!text || !text[0]) return;

    ImDrawList *dl = ImGui::GetWindowDrawList();

    /* 1. Glow (bottom layer) */
    if (s_Glow.radius > 0.0f && s_Glow.intensity > 0.0f) {
        s32 passes = s_Glow.passes;
        if (passes < 1) passes = 1;
        if (passes > 4) passes = 4;

        /* Draw text in a circular pattern around the position */
        s32 steps = passes * 4;  /* 4 directions per pass */
        f32 alpha_per = s_Glow.intensity / (f32)steps;

        for (s32 pass = 0; pass < passes; pass++) {
            f32 r = s_Glow.radius * ((f32)(pass + 1) / (f32)passes);

            for (s32 dir = 0; dir < 4; dir++) {
                f32 angle = (f32)dir * (f32)(M_PI / 2.0);
                f32 ox = r * cosf(angle);
                f32 oy = r * sinf(angle);

                dl->AddText(ImVec2(x + ox, y + oy),
                            FmColAlpha(s_Glow.color, alpha_per),
                            text);
            }

            /* Diagonal directions for smoother glow */
            for (s32 dir = 0; dir < 4; dir++) {
                f32 angle = (f32)dir * (f32)(M_PI / 2.0) + (f32)(M_PI / 4.0);
                f32 ox = r * 0.707f * cosf(angle);
                f32 oy = r * 0.707f * sinf(angle);

                dl->AddText(ImVec2(x + ox, y + oy),
                            FmColAlpha(s_Glow.color, alpha_per * 0.7f),
                            text);
            }
        }
    }

    /* 2. Shadow (middle layer) */
    if (s_Shadow.offset_x != 0.0f || s_Shadow.offset_y != 0.0f) {
        u32 sc = s_Shadow.color;
        if ((sc & 0xffu) > 0) {  /* has alpha */
            dl->AddText(ImVec2(x + s_Shadow.offset_x, y + s_Shadow.offset_y),
                        FmCol(sc), text);
        }
    }

    /* 3. Text (top layer) */
    dl->AddText(ImVec2(x, y), FmCol(color), text);
}

void pdguiFontMgrDrawTextCentered(float x, float y, float w, float h,
                                  u32 color, const char *text)
{
    if (!text || !text[0]) return;

    ImVec2 sz = ImGui::CalcTextSize(text);
    float tx = x + (w - sz.x) * 0.5f;
    float ty = y + (h - sz.y) * 0.5f;

    pdguiFontMgrDrawText(tx, ty, color, text);
}

} /* extern "C" */
