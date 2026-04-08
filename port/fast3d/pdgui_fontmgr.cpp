/**
 * pdgui_fontmgr.cpp -- TTF font loading and text effects (P4)
 *
 * Manages mod-provided TTF fonts through ImGui's font API.
 * Features:
 *   - Register fonts from mod directories (TTF files)
 *   - Per-font glow radius+color and shadow offset+color
 *   - Fonts as catalog assets for the theme system
 *   - Fallback to built-in Handel Gothic if mod font missing
 *
 * Font atlas rebuild: After registering new fonts, call
 * pdguiFontMgrBuildAtlas() to rebuild ImGui's texture atlas.
 * This invalidates the old font texture — must be done before rendering.
 *
 * IMPORTANT: Do NOT include types.h -- it #defines bool as s32, breaking C++.
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <PR/ultratypes.h>

#include "imgui/imgui.h"
#include "pdgui_fontmgr.h"
#include "pdgui_style.h"
#include "assetcatalog.h"
#include "system.h"
#include "fs.h"

/* =========================================================================
 * State
 * ========================================================================= */

static pdgui_font_entry_t s_Fonts[FONTMGR_MAX_FONTS];
static s32 s_FontCount = 0;
static s32 s_FontMgrInitDone = 0;

/* =========================================================================
 * Color conversion
 * ========================================================================= */

static inline ImU32 FmCol(u32 rgba)
{
    u8 r = (u8)((rgba >> 24) & 0xFFu);
    u8 g = (u8)((rgba >> 16) & 0xFFu);
    u8 b = (u8)((rgba >>  8) & 0xFFu);
    u8 a = (u8)((rgba >>  0) & 0xFFu);
    return IM_COL32(r, g, b, a);
}

static inline ImU32 FmColA(u32 rgba, u8 alpha)
{
    u8 r = (u8)((rgba >> 24) & 0xFFu);
    u8 g = (u8)((rgba >> 16) & 0xFFu);
    u8 b = (u8)((rgba >>  8) & 0xFFu);
    return IM_COL32(r, g, b, alpha);
}

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static pdgui_font_entry_t *findEntry(const char *catalog_id)
{
    if (!catalog_id) return nullptr;
    for (s32 i = 0; i < s_FontCount; i++) {
        if (strcmp(s_Fonts[i].catalog_id, catalog_id) == 0)
            return &s_Fonts[i];
    }
    return nullptr;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

extern "C" {

void pdguiFontMgrInit(void)
{
    if (s_FontMgrInitDone) return;
    s_FontMgrInitDone = 1;
    s_FontCount = 0;
    memset(s_Fonts, 0, sizeof(s_Fonts));

    /* Register the built-in Handel Gothic as the default font entry */
    pdgui_font_entry_t *builtin = &s_Fonts[0];
    snprintf(builtin->catalog_id, FONTMGR_CATID_LEN, "base:font_handelgothic");
    snprintf(builtin->name, FONTMGR_NAME_LEN, "Handel Gothic");
    builtin->filepath[0] = '\0'; /* empty = built-in */
    builtin->size = 24.0f;
    builtin->glowRadius = 0.0f;
    builtin->glowColor = 0x0080FF60u;
    builtin->shadowOffsetX = 1.0f;
    builtin->shadowOffsetY = 1.0f;
    builtin->shadowColor = 0x000000A0u;
    builtin->loaded = 1;
    builtin->imfont = (void *)ImGui::GetIO().FontDefault;
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

    sysLogPrintf(LOG_NOTE, "PDGUI font mgr: initialized (built-in: Handel Gothic)");
}

void pdguiFontMgrShutdown(void)
{
    /* ImGui owns font memory — we just clear our registry */
    s_FontCount = 0;
    s_FontMgrInitDone = 0;
    sysLogPrintf(LOG_NOTE, "PDGUI font mgr: shutdown");
}

s32 pdguiFontMgrRegister(const char *catalog_id, const char *name,
                          const char *filepath, f32 size)
{
    if (!catalog_id || !filepath) return -1;
    if (s_FontCount >= FONTMGR_MAX_FONTS) {
        sysLogPrintf(LOG_WARNING,
            "PDGUI font mgr: max fonts (%d) reached, cannot register '%s'",
            FONTMGR_MAX_FONTS, catalog_id);
        return -1;
    }

    /* Check for duplicate */
    if (findEntry(catalog_id)) {
        sysLogPrintf(LOG_WARNING,
            "PDGUI font mgr: '%s' already registered", catalog_id);
        return -1;
    }

    s32 idx = s_FontCount;
    pdgui_font_entry_t *e = &s_Fonts[idx];
    snprintf(e->catalog_id, FONTMGR_CATID_LEN, "%s", catalog_id);
    snprintf(e->name, FONTMGR_NAME_LEN, "%s", name ? name : catalog_id);
    snprintf(e->filepath, FONTMGR_PATH_LEN, "%s", filepath);
    e->size = (size > 0.0f) ? size : 24.0f;
    e->glowRadius = 0.0f;
    e->glowColor = 0x0080FF60u;
    e->shadowOffsetX = 1.0f;
    e->shadowOffsetY = 1.0f;
    e->shadowColor = 0x000000A0u;
    e->loaded = 0;
    e->imfont = nullptr;
    s_FontCount++;

    /* Register in catalog */
    asset_entry_t *ae = assetCatalogRegister(catalog_id, ASSET_UI);
    if (ae) {
        snprintf(ae->category, CATALOG_CATEGORY_LEN, "mod");
        ae->bundled    = 0;
        ae->enabled    = 1;
        ae->load_state = ASSET_STATE_REGISTERED;
    }

    sysLogPrintf(LOG_NOTE, "PDGUI font mgr: registered '%s' (%s, %.0fpt)",
                 catalog_id, filepath, e->size);
    return idx;
}

void pdguiFontMgrBuildAtlas(void)
{
    ImGuiIO &io = ImGui::GetIO();

    for (s32 i = 0; i < s_FontCount; i++) {
        pdgui_font_entry_t *e = &s_Fonts[i];
        if (e->loaded || !e->filepath[0]) continue;

        /* Load TTF from filesystem */
        u32 fileSize = 0;
        void *data = fsFileLoad(e->filepath, &fileSize);
        if (!data || fileSize == 0) {
            sysLogPrintf(LOG_WARNING,
                "PDGUI font mgr: failed to load '%s' from '%s' — using fallback",
                e->catalog_id, e->filepath);
            /* Fallback to default */
            e->imfont = (void *)io.FontDefault;
            e->loaded = 1;
            continue;
        }

        /* ImGui::AddFontFromMemoryTTF takes ownership of the buffer.
         * We must allocate via ImGui's allocator. */
        void *fontCopy = ImGui::MemAlloc(fileSize);
        memcpy(fontCopy, data, fileSize);
        free(data);

        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = true;
        snprintf(cfg.Name, sizeof(cfg.Name), "%s", e->name);
        cfg.OversampleV = 2;

        ImFont *font = io.Fonts->AddFontFromMemoryTTF(
            fontCopy, (int)fileSize, e->size, &cfg);

        if (font) {
            e->imfont = (void *)font;
            e->loaded = 1;
            sysLogPrintf(LOG_NOTE,
                "PDGUI font mgr: loaded '%s' (%u bytes, %.0fpt)",
                e->catalog_id, fileSize, e->size);

            /* Catalog state is updated at registration time;
             * the const resolve API doesn't allow mutation here. */
        } else {
            sysLogPrintf(LOG_WARNING,
                "PDGUI font mgr: ImGui rejected '%s' — using fallback",
                e->catalog_id);
            e->imfont = (void *)io.FontDefault;
            e->loaded = 1;
        }
    }

    /* Rebuild atlas texture */
    io.Fonts->Build();

    sysLogPrintf(LOG_NOTE, "PDGUI font mgr: atlas rebuilt (%d fonts)", s_FontCount);
}

void *pdguiFontMgrGetFont(const char *catalog_id)
{
    pdgui_font_entry_t *e = findEntry(catalog_id);
    if (e && e->loaded && e->imfont)
        return e->imfont;
    return (void *)ImGui::GetIO().FontDefault;
}

void *pdguiFontMgrGetDefault(void)
{
    return (void *)ImGui::GetIO().FontDefault;
}

s32 pdguiFontMgrGetCount(void)
{
    return s_FontCount;
}

const pdgui_font_entry_t *pdguiFontMgrGetEntry(s32 index)
{
    if (index < 0 || index >= s_FontCount) return nullptr;
    return &s_Fonts[index];
}

void pdguiFontMgrDrawText(const char *font_catalog_id,
                           f32 x, f32 y, u32 color,
                           const char *text)
{
    if (!text || !text[0]) return;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    pdgui_font_entry_t *e = font_catalog_id ? findEntry(font_catalog_id) : nullptr;
    ImFont *font = e ? (ImFont *)e->imfont : ImGui::GetIO().FontDefault;
    if (!font) font = ImGui::GetIO().FontDefault;

    ImU32 textCol = FmCol(color);

    /* Shadow pass */
    if (e && (e->shadowOffsetX != 0.0f || e->shadowOffsetY != 0.0f)) {
        ImU32 shadowCol = FmCol(e->shadowColor);
        dl->AddText(font, font->FontSize,
                    ImVec2(x + e->shadowOffsetX, y + e->shadowOffsetY),
                    shadowCol, text);
    }

    /* Glow pass */
    if (e && e->glowRadius > 0.0f) {
        ImU32 glowCol = FmCol(e->glowColor);
        ImVec2 textSize = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, text);

        /* Multi-pass glow (3 expanding layers) */
        for (int pass = 0; pass < 3; pass++) {
            float expand = e->glowRadius * (float)(pass + 1) * 0.4f;
            u8 ga = (u8)((e->glowColor & 0xFF) / (pass + 1));
            ImU32 gc = FmColA(e->glowColor, ga);

            dl->AddRectFilled(
                ImVec2(x - expand, y - expand),
                ImVec2(x + textSize.x + expand, y + textSize.y + expand),
                gc, expand * 0.5f);
        }
    }

    /* Main text */
    dl->AddText(font, font->FontSize, ImVec2(x, y), textCol, text);
}

void pdguiFontMgrSetGlow(const char *catalog_id, f32 radius, u32 color)
{
    pdgui_font_entry_t *e = findEntry(catalog_id);
    if (!e) return;
    e->glowRadius = radius;
    e->glowColor = color;
}

void pdguiFontMgrSetShadow(const char *catalog_id,
                            f32 offsetX, f32 offsetY, u32 color)
{
    pdgui_font_entry_t *e = findEntry(catalog_id);
    if (!e) return;
    e->shadowOffsetX = offsetX;
    e->shadowOffsetY = offsetY;
    e->shadowColor = color;
}

} /* extern "C" */
