/**
 * pdgui_fontmgr.cpp -- TTF font loading with glow/shadow support (P4)
 *
 * Manages custom fonts from mod directories. Each font can have glow and
 * shadow effects configured. Fonts are registered as catalog assets so
 * mods can provide custom fonts that override the built-in Handel Gothic.
 *
 * Font loading uses ImGui's AddFontFromMemoryTTF(). After loading all
 * mod fonts, the atlas must be rebuilt via pdguiFontMgrRebuildAtlas().
 *
 * IMPORTANT: Do NOT include types.h -- it #defines bool as s32, breaking C++.
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 * Part of Phase P4: UI Texture Mod.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <PR/ultratypes.h>

#include "imgui/imgui.h"
#include "pdgui_fontmgr.h"
#include "pdgui_style.h"
#include "assetcatalog.h"
#include "system.h"
#include "fs.h"

/* =========================================================================
 * Constants
 * ========================================================================= */

#define FONTMGR_MAX_FONTS     16
#define FONTMGR_ID_LEN        64
#define FONTMGR_NAME_LEN      64
#define FONTMGR_PATH_LEN     256
#define FONTMGR_DEFAULT_SIZE  24.0f

/* =========================================================================
 * Font registry
 * ========================================================================= */

struct font_entry {
    char            catalog_id[FONTMGR_ID_LEN];
    char            name[FONTMGR_NAME_LEN];
    char            filepath[FONTMGR_PATH_LEN];
    PdguiFontConfig config;
    ImFont         *imfont;       /* NULL if load failed */
    s32             loaded;
};

static struct font_entry s_Fonts[FONTMGR_MAX_FONTS];
static s32  s_FontCount = 0;
static s32  s_InitDone  = 0;

/* Track which font's config is active for effect rendering */
static const PdguiFontConfig *s_ActiveConfig = nullptr;
static PdguiFontConfig s_DefaultConfig;

static struct font_entry *s_findFont(const char *catalog_id)
{
    for (s32 i = 0; i < s_FontCount; i++) {
        if (strcmp(s_Fonts[i].catalog_id, catalog_id) == 0) {
            return &s_Fonts[i];
        }
    }
    return nullptr;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

extern "C" {

PdguiFontConfig pdguiFontConfigDefault(void)
{
    PdguiFontConfig cfg;
    cfg.glow_radius = 3.0f;
    cfg.glow_color = 0x0080ff80u;  /* blue glow at 50% */
    cfg.shadow_offset_x = 1.0f;
    cfg.shadow_offset_y = 1.0f;
    cfg.shadow_color = 0x000000a0u;  /* black shadow at ~63% */
    cfg.size_pt = 0.0f;  /* 0 = use default */
    return cfg;
}

void pdguiFontMgrInit(void)
{
    if (s_InitDone) return;
    s_InitDone = 1;
    s_FontCount = 0;
    s_DefaultConfig = pdguiFontConfigDefault();
    s_ActiveConfig = &s_DefaultConfig;

    sysLogPrintf(LOG_NOTE, "PDGUI fontmgr: initialized (max %d custom fonts)",
                 FONTMGR_MAX_FONTS);
}

void pdguiFontMgrShutdown(void)
{
    /* ImGui owns the font atlas memory — we just clear our registry */
    s_FontCount = 0;
    s_InitDone = 0;
    s_ActiveConfig = nullptr;
}

s32 pdguiFontMgrLoadFont(const char *catalog_id, const char *filepath,
                         const PdguiFontConfig *config)
{
    if (!catalog_id || !filepath) return 0;
    if (s_FontCount >= FONTMGR_MAX_FONTS) {
        sysLogPrintf(LOG_WARNING,
            "PDGUI fontmgr: registry full, cannot load '%s'", catalog_id);
        return 0;
    }

    /* Load TTF file */
    u32 fileSize = 0;
    void *data = fsFileLoad(filepath, &fileSize);
    if (!data || fileSize == 0) {
        sysLogPrintf(LOG_WARNING,
            "PDGUI fontmgr: failed to load '%s' from '%s'", catalog_id, filepath);
        return 0;
    }

    /* ImGui takes ownership of the font data buffer via AddFontFromMemoryTTF.
     * We must provide a malloc'd copy because fsFileLoad uses its own allocator. */
    void *fontCopy = ImGui::MemAlloc(fileSize);
    memcpy(fontCopy, data, fileSize);
    free(data);

    float sizePt = (config && config->size_pt > 0.0f)
                   ? config->size_pt
                   : FONTMGR_DEFAULT_SIZE;

    ImFontConfig imcfg;
    imcfg.FontDataOwnedByAtlas = true;
    snprintf(imcfg.Name, sizeof(imcfg.Name), "Mod: %s", catalog_id);
    imcfg.OversampleV = 2;

    ImGuiIO &io = ImGui::GetIO();
    ImFont *font = io.Fonts->AddFontFromMemoryTTF(
        fontCopy, (int)fileSize, sizePt, &imcfg);

    /* Register in our table regardless of success */
    struct font_entry *e = &s_Fonts[s_FontCount++];
    snprintf(e->catalog_id, FONTMGR_ID_LEN, "%s", catalog_id);
    snprintf(e->filepath, FONTMGR_PATH_LEN, "%s", filepath);
    e->config = config ? *config : pdguiFontConfigDefault();
    e->imfont = font;
    e->loaded = (font != nullptr) ? 1 : 0;

    /* Extract name from catalog_id (after the ':') */
    const char *nameStart = strchr(catalog_id, ':');
    snprintf(e->name, FONTMGR_NAME_LEN, "%s",
             nameStart ? (nameStart + 1) : catalog_id);

    /* Register as catalog asset */
    asset_entry_t *ae = assetCatalogRegister(catalog_id, ASSET_UI);
    if (ae) {
        snprintf(ae->category, CATALOG_CATEGORY_LEN, "font");
        ae->bundled    = 0;
        ae->enabled    = 1;
        ae->load_state = font ? ASSET_STATE_LOADED : ASSET_STATE_REGISTERED;
        ae->ref_count  = 1;
    }

    if (font) {
        sysLogPrintf(LOG_NOTE,
            "PDGUI fontmgr: loaded '%s' from '%s' (%.0fpt)",
            catalog_id, filepath, sizePt);
    } else {
        sysLogPrintf(LOG_WARNING,
            "PDGUI fontmgr: '%s' — ImGui AddFont failed, will use fallback",
            catalog_id);
    }

    return font ? 1 : 0;
}

void pdguiFontMgrRebuildAtlas(void)
{
    /* Tell the ImGui OpenGL3 backend to rebuild the font atlas texture */
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Build();

    sysLogPrintf(LOG_NOTE,
        "PDGUI fontmgr: atlas rebuilt with %d fonts (%d custom)",
        io.Fonts->Fonts.Size, s_FontCount);
}

s32 pdguiFontMgrPushFont(const char *catalog_id)
{
    if (!catalog_id) return 0;

    struct font_entry *e = s_findFont(catalog_id);
    if (!e || !e->imfont) return 0;

    ImGui::PushFont(e->imfont);
    s_ActiveConfig = &e->config;
    return 1;
}

void pdguiFontMgrPopFont(void)
{
    ImGui::PopFont();
    s_ActiveConfig = &s_DefaultConfig;
}

/* =========================================================================
 * Text rendering with effects
 *
 * Draw order (back to front):
 *   1. Glow: blurred colored rect behind text (reuses pdguiDrawTextGlow pattern)
 *   2. Shadow: offset copy of text in shadow color
 *   3. Text: final text in requested color
 * ========================================================================= */

/** PD 0xRRGGBBAA → ImU32 */
static inline ImU32 s_pdColToImU32(u32 rgba)
{
    u8 r = (u8)((rgba >> 24) & 0xff);
    u8 g = (u8)((rgba >> 16) & 0xff);
    u8 b = (u8)((rgba >>  8) & 0xff);
    u8 a = (u8)((rgba >>  0) & 0xff);
    return IM_COL32(r, g, b, a);
}

void pdguiFontMgrDrawTextWithEffects(f32 x, f32 y, const char *text, u32 text_color)
{
    const PdguiFontConfig *cfg = s_ActiveConfig ? s_ActiveConfig : &s_DefaultConfig;
    pdguiFontMgrDrawTextEx(x, y, text, text_color, cfg);
}

void pdguiFontMgrDrawTextEx(f32 x, f32 y, const char *text, u32 text_color,
                            const PdguiFontConfig *config)
{
    if (!text || !text[0]) return;
    if (!config) config = &s_DefaultConfig;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 textSize = ImGui::CalcTextSize(text);

    /* 1. Glow */
    if (config->glow_radius > 0.0f) {
        u32 gc = config->glow_color;
        u8 gr = (u8)((gc >> 24) & 0xff);
        u8 gg = (u8)((gc >> 16) & 0xff);
        u8 gb = (u8)((gc >>  8) & 0xff);
        u8 ga = (u8)((gc >>  0) & 0xff);

        /* Multi-pass soft glow (3 layers at increasing radius) */
        for (int pass = 0; pass < 3; pass++) {
            float expand = config->glow_radius * (float)(pass + 1) / 3.0f;
            u8 alpha = (u8)(ga / (pass + 1));

            dl->AddRectFilled(
                ImVec2(x - expand, y - expand),
                ImVec2(x + textSize.x + expand, y + textSize.y + expand),
                IM_COL32(gr, gg, gb, alpha),
                expand * 0.5f);
        }
    }

    /* 2. Shadow */
    if (config->shadow_offset_x != 0.0f || config->shadow_offset_y != 0.0f) {
        dl->AddText(
            ImVec2(x + config->shadow_offset_x, y + config->shadow_offset_y),
            s_pdColToImU32(config->shadow_color),
            text);
    }

    /* 3. Text */
    dl->AddText(ImVec2(x, y), s_pdColToImU32(text_color), text);
}

/* =========================================================================
 * Registry query
 * ========================================================================= */

s32 pdguiFontMgrGetCount(void)
{
    return s_FontCount;
}

const char *pdguiFontMgrGetId(s32 index)
{
    if (index < 0 || index >= s_FontCount) return nullptr;
    return s_Fonts[index].catalog_id;
}

const char *pdguiFontMgrGetName(s32 index)
{
    if (index < 0 || index >= s_FontCount) return nullptr;
    return s_Fonts[index].name;
}

const PdguiFontConfig *pdguiFontMgrGetConfig(const char *catalog_id)
{
    if (!catalog_id) return nullptr;
    struct font_entry *e = s_findFont(catalog_id);
    return e ? &e->config : nullptr;
}

} /* extern "C" */
