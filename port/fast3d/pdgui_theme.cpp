/**
 * pdgui_theme.cpp -- PD menu visual theme layer (D5.0)
 *
 * ROM texture decode pipeline + ImGui draw-list-based theme functions.
 * Replaces the D5.0a synthetic test-pattern from pdgui_backend.cpp.
 *
 * Pipeline:
 *   ROM textureconfig (N64 fmt+siz+ptr) → decodeN64Tex*() → RGBA32
 *   → s_uploadGLTex() → GLuint → ImTextureID
 *
 * Supported N64 formats (matching import_texture_* in gfx_pc.cpp):
 *   RGBA16, IA16, IA8, IA4, CI8, CI4
 *
 * Known ROM UI texture registrations (menugfx.c cross-reference):
 *   base:ui_bg_haze    g_TexGeneralConfigs[6]  menugfxRenderBgGreenHaze
 *   base:ui_particles  g_TexGeneralConfigs[1]  success-screen shimmer
 *
 * Null policy:
 *   textureptr == NULL  => LOG_ERROR + assert  (pipeline bug, not fallback)
 *   texnum-based cfg    => deferred; pdguiThemeGetTexture() asserts if called
 *                          before texture has been loaded by GBI pipeline
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 * Part of Phase D5: Menu System Visual Layer.
 *
 * IMPORTANT: Do NOT include types.h -- it #defines bool as s32, breaking C++.
 * Use extern "C" forward declarations for game symbols instead.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <string>
#include <unordered_map>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "glad/glad.h"
#include "imgui/imgui.h"
#include "pdgui_theme.h"
#include "pdgui_style.h"
#include "system.h"
#include "assetcatalog.h"

/* =========================================================================
 * textureconfig bridge
 *
 * Mirrors struct textureconfig from src/include/types.h.
 * The union is pointer-sized on 64-bit; texturenum occupies the low 32 bits.
 * Field layout must stay in sync with types.h.
 * ========================================================================= */

struct PdTexConfig {
    union {
        uint32_t        texnum;   /* ROM texture table index (small integer) */
        const uint8_t  *texptr;   /* Direct pointer to raw N64 pixel data    */
    };
    uint8_t width;    /* texture width  in pixels */
    uint8_t height;   /* texture height in pixels */
    uint8_t level;    /* mip level (0 = base) */
    uint8_t fmt;      /* N64 image format: PD_G_IM_FMT_* */
    uint8_t siz;      /* N64 image size:   PD_G_IM_SIZ_* */
    uint8_t s;        /* s-axis wrap/mirror flags */
    uint8_t t;        /* t-axis wrap/mirror flags */
    uint8_t pad;
};

/* N64 image format constants (gbi.h) */
enum {
    PD_G_IM_FMT_RGBA = 0,
    PD_G_IM_FMT_YUV  = 1,
    PD_G_IM_FMT_CI   = 2,
    PD_G_IM_FMT_IA   = 3,
    PD_G_IM_FMT_I    = 4,
};

/* N64 image size constants (gbi.h) */
enum {
    PD_G_IM_SIZ_4b  = 0,
    PD_G_IM_SIZ_8b  = 1,
    PD_G_IM_SIZ_16b = 2,
    PD_G_IM_SIZ_32b = 3,
};

extern "C" {
    /**
     * g_TexGeneralConfigs -- runtime array of general-use texture configs.
     * Defined in src/game/texdecompress.c; declared extern in bss.h.
     * Populated by texInit() at game startup.
     * Cast to PdTexConfig* (layout-compatible with struct textureconfig).
     */
    extern struct PdTexConfig *g_TexGeneralConfigs;
}

/* On a 64-bit build, a texnum like 0x0003 stored in the union gives
 * texptr = (uint8_t *)0x0000000000000003, which is clearly not a heap ptr.
 * A real data pointer will always be >= 64 KB. */
#define PDTEX_MIN_VALID_PTR ((uintptr_t)0x10000u)

static inline bool s_isRealPtr(const struct PdTexConfig *cfg)
{
    return ((uintptr_t)cfg->texptr >= PDTEX_MIN_VALID_PTR);
}

/* =========================================================================
 * Scale helpers -- mirror gfx_pc.cpp macros
 * ========================================================================= */

static inline uint8_t scale58(uint32_t v) { return (uint8_t)((v * 0xffu) / 0x1fu); }
static inline uint8_t scale48(uint32_t v) { return (uint8_t)((v * 0xffu) / 0x0fu); }
static inline uint8_t scale38(uint32_t v) { return (uint8_t)((v * 0xffu) / 0x07u); }

/* =========================================================================
 * N64 → RGBA32 decoders
 *
 * Each function writes w*h RGBA32 pixels (4 bytes each) to `out`.
 * Caller must allocate at least w*h*4 bytes at `out`.
 * Adapted from import_texture_* in port/fast3d/gfx_pc.cpp.
 * ========================================================================= */

static void decodeRgba16(const uint8_t *src, uint32_t w, uint32_t h,
                          uint8_t *out)
{
    uint32_t n = w * h;
    for (uint32_t i = 0; i < n; i++, out += 4) {
        uint16_t col = ((uint16_t)src[i * 2] << 8) | src[i * 2 + 1];
        out[0] = scale58((col >> 11) & 0x1fu);
        out[1] = scale58((col >>  6) & 0x1fu);
        out[2] = scale58((col >>  1) & 0x1fu);
        out[3] = (col & 1u) ? 0xffu : 0x00u;
    }
}

static void decodeIa16(const uint8_t *src, uint32_t w, uint32_t h,
                        uint8_t *out)
{
    uint32_t n = w * h;
    for (uint32_t i = 0; i < n; i++, out += 4) {
        uint8_t intensity = src[i * 2];
        uint8_t alpha     = src[i * 2 + 1];
        out[0] = out[1] = out[2] = intensity;
        out[3] = alpha;
    }
}

static void decodeIa8(const uint8_t *src, uint32_t w, uint32_t h,
                       uint8_t *out)
{
    uint32_t n = w * h;
    for (uint32_t i = 0; i < n; i++, out += 4) {
        uint8_t intensity = scale48(src[i] >> 4);
        uint8_t alpha     = scale48(src[i] & 0xfu);
        out[0] = out[1] = out[2] = intensity;
        out[3] = alpha;
    }
}

static void decodeIa4(const uint8_t *src, uint32_t w, uint32_t h,
                       uint8_t *out)
{
    uint32_t n = w * h;
    for (uint32_t i = 0; i < n; i++, out += 4) {
        uint8_t byte = src[i / 2];
        uint8_t part = (i & 1u) ? (byte & 0xfu) : (byte >> 4u);
        uint8_t intensity = scale38(part >> 1u);
        uint8_t alpha     = (part & 1u) ? 0xffu : 0x00u;
        out[0] = out[1] = out[2] = intensity;
        out[3] = alpha;
    }
}

/* Convert a big-endian RGBA16 or IA16 palette entry to RGBA32. */
static void palEntryToRgba32(uint16_t entry, uint8_t *out, bool is_ia16)
{
    if (is_ia16) {
        /* IA16: byte 0 = intensity, byte 1 = alpha (big-endian) */
        out[0] = out[1] = out[2] = (uint8_t)(entry & 0xffu);
        out[3] = (uint8_t)(entry >> 8);
    } else {
        /* RGBA16: rrrrr ggggg bbbbb a  (big-endian, as stored in TMEM) */
        out[0] = scale58((entry >> 11) & 0x1fu);
        out[1] = scale58((entry >>  6) & 0x1fu);
        out[2] = scale58((entry >>  1) & 0x1fu);
        out[3] = (entry & 1u) ? 0xffu : 0x00u;
    }
}

static void decodeCi8(const uint8_t  *src,
                       const uint16_t *pal, bool is_ia16,
                       uint32_t w, uint32_t h, uint8_t *out)
{
    uint32_t n = w * h;
    for (uint32_t i = 0; i < n; i++, out += 4) {
        palEntryToRgba32(pal[src[i]], out, is_ia16);
    }
}

static void decodeCi4(const uint8_t  *src,
                       const uint16_t *pal, bool is_ia16,
                       uint32_t w, uint32_t h, uint8_t *out)
{
    uint32_t n = w * h;
    for (uint32_t i = 0; i < n; i++, out += 4) {
        uint8_t idx = (i & 1u) ? (src[i / 2] & 0xfu) : (src[i / 2] >> 4u);
        palEntryToRgba32(pal[idx], out, is_ia16);
    }
}

/* =========================================================================
 * GL texture upload
 * ========================================================================= */

static GLuint s_uploadGLTex(const uint8_t *rgba32, uint32_t w, uint32_t h)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex) {
        sysLogPrintf(LOG_ERROR, "PDGUI theme: glGenTextures failed (%ux%u)", w, h);
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 (GLsizei)w, (GLsizei)h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

/**
 * Decode a PdTexConfig whose texptr is a valid data pointer, upload to GL.
 * pal and is_ia16 are only used for CI formats; pass NULL/false otherwise.
 * Returns GL texture id on success, 0 on failure.
 */
static GLuint s_decodeAndUpload(const struct PdTexConfig *cfg,
                                 const uint16_t *pal, bool is_ia16)
{
    uint32_t w   = (uint32_t)cfg->width;
    uint32_t h   = (uint32_t)cfg->height;
    uint32_t fmt = (uint32_t)cfg->fmt;
    uint32_t siz = (uint32_t)cfg->siz;
    const uint8_t *src = cfg->texptr;

    /* Max texture: 256×256×4 = 256 KB -- enough for any N64 UI texture */
    static uint8_t s_Rgba32Buf[256 * 256 * 4];

    if (!w || !h || w > 256 || h > 256) {
        sysLogPrintf(LOG_ERROR,
            "PDGUI theme: bad texture dims %ux%u (max 256x256)", w, h);
        return 0;
    }

    switch (fmt) {
    case PD_G_IM_FMT_RGBA:
        if (siz == PD_G_IM_SIZ_16b) {
            decodeRgba16(src, w, h, s_Rgba32Buf);
        } else {
            sysLogPrintf(LOG_ERROR,
                "PDGUI theme: unsupported RGBA siz=%u (only RGBA16 supported)", siz);
            return 0;
        }
        break;

    case PD_G_IM_FMT_IA:
        if      (siz == PD_G_IM_SIZ_16b) decodeIa16(src, w, h, s_Rgba32Buf);
        else if (siz == PD_G_IM_SIZ_8b)  decodeIa8 (src, w, h, s_Rgba32Buf);
        else if (siz == PD_G_IM_SIZ_4b)  decodeIa4 (src, w, h, s_Rgba32Buf);
        else {
            sysLogPrintf(LOG_ERROR,
                "PDGUI theme: unsupported IA siz=%u", siz);
            return 0;
        }
        break;

    case PD_G_IM_FMT_CI:
        if (!pal) {
            sysLogPrintf(LOG_ERROR,
                "PDGUI theme: CI texture requires palette; none supplied");
            return 0;
        }
        if      (siz == PD_G_IM_SIZ_8b)  decodeCi8(src, pal, is_ia16, w, h, s_Rgba32Buf);
        else if (siz == PD_G_IM_SIZ_4b)  decodeCi4(src, pal, is_ia16, w, h, s_Rgba32Buf);
        else {
            sysLogPrintf(LOG_ERROR,
                "PDGUI theme: unsupported CI siz=%u", siz);
            return 0;
        }
        break;

    default:
        sysLogPrintf(LOG_ERROR,
            "PDGUI theme: unsupported N64 fmt=%u siz=%u", fmt, siz);
        return 0;
    }

    return s_uploadGLTex(s_Rgba32Buf, w, h);
}

/* =========================================================================
 * Texture registry
 *
 * catalog_id (string) → GL texture id (uint32_t)
 * Populated by pdguiThemeInit() for each known UI texture.
 * ========================================================================= */

static std::unordered_map<std::string, GLuint> s_ThemeTexCache;
static bool s_ThemeInitDone = false;

/**
 * Try to decode and upload a textureconfig, register it in the catalog as
 * ASSET_UI, and insert into s_ThemeTexCache.
 *
 * If the config has a texnum (not a real pointer), the entry is still
 * registered in the catalog with source_texnum set, but no GL texture is
 * created.  pdguiThemeGetTexture() will assert if called before the GBI
 * pipeline has decoded the texture.
 */
static void s_registerTexConfig(const char        *catalog_id,
                                 const struct PdTexConfig *cfg,
                                 const uint16_t    *pal,
                                 bool               is_ia16)
{
    if (!cfg) {
        sysLogPrintf(LOG_ERROR,
            "PDGUI theme: NULL textureconfig for '%s' -- pipeline bug",
            catalog_id);
        assert(cfg != nullptr);
        return;
    }

    GLuint gl_id = 0;

    if (s_isRealPtr(cfg)) {
        /* Texture data resident in memory -- decode now */
        gl_id = s_decodeAndUpload(cfg, pal, is_ia16);
        if (!gl_id) {
            sysLogPrintf(LOG_ERROR,
                "PDGUI theme: decode failed for '%s' "
                "(fmt=%u siz=%u %ux%u) -- pipeline bug",
                catalog_id, cfg->fmt, cfg->siz, cfg->width, cfg->height);
            assert(gl_id != 0 && "ROM texture decode failed -- pipeline bug");
        } else {
            sysLogPrintf(LOG_NOTE,
                "PDGUI theme: decoded '%s' fmt=%u siz=%u %ux%u → GL %u",
                catalog_id, cfg->fmt, cfg->siz,
                cfg->width, cfg->height, gl_id);
        }
    } else {
        /* texnum-based config: data loaded lazily by GBI pipeline.
         * GL upload is deferred until the GBI has processed this texture.
         *
         * TODO (D5.1): hook gfx_pc.cpp texture cache to populate deferred
         * entries once the GBI pipeline processes them.
         * Until then, pdguiThemeGetTexture() will assert if called before
         * the texture has been loaded by a GBI render frame. */
        sysLogPrintf(LOG_NOTE,
            "PDGUI theme: '%s' texnum=%u -- GL upload deferred "
            "(texture loaded by GBI pipeline on first render frame)",
            catalog_id, cfg->texnum);
    }

    /* Register in asset catalog */
    {
        asset_entry_t *e = assetCatalogRegister(catalog_id, ASSET_UI);
        if (e) {
            snprintf(e->category, CATALOG_CATEGORY_LEN, "base");
            e->bundled       = 1;
            e->enabled       = 1;
            e->load_state    = gl_id ? ASSET_STATE_LOADED : ASSET_STATE_REGISTERED;
            e->ref_count     = ASSET_REF_BUNDLED;
            e->source_texnum = s_isRealPtr(cfg) ? -1 : (s32)cfg->texnum;
            if (gl_id) {
                e->loaded_data     = (void *)(uintptr_t)gl_id;
                e->data_size_bytes = (u32)(cfg->width * cfg->height * 4u);
            }
        } else {
            sysLogPrintf(LOG_ERROR,
                "PDGUI theme: catalog registration failed for '%s'",
                catalog_id);
        }
    }

    if (gl_id) {
        s_ThemeTexCache[catalog_id] = gl_id;
    }
}

/* =========================================================================
 * Color helpers -- palette values from pdgui_style.cpp (Blue = index 1)
 *
 * Format: 0xRRGGBBAA (matching struct pdgui_palette / g_MenuColours[]).
 * Only the Blue palette is embedded here; other palettes delegate to the
 * style layer (pdguiGetPalette() / pdguiApplyPdStyle()).
 * ========================================================================= */

/* Blue palette (index 1) from pdgui_style.cpp s_PaletteBlue */
static const uint32_t k_PalBlue[15] = {
    0x0060bf7fu, /* [0]  dialog_border1     -- medium blue, half-alpha        */
    0x0000507fu, /* [1]  dialog_titlebg     -- dark navy title bar            */
    0x00f0ff7fu, /* [2]  dialog_border2     -- bright cyan-blue accent        */
    0xffffffffu, /* [3]  dialog_titlefg     -- white title text               */
    0x00002f9fu, /* [4]  dialog_bodybg      -- very dark navy, ~62% alpha     */
    0x00006f7fu, /* [5]  unused                                               */
    0x00ffffffu, /* [6]  item_unfocused     -- white item text                */
    0x007f7fffu, /* [7]  item_disabled      -- muted grey-blue text           */
    0xffffffffu, /* [8]  item_focused_inner -- bright white when hovered      */
    0x8fffffffu, /* [9]  checkbox_checked                                     */
    0x000044ffu, /* [10] item_focused_outer -- dark blue focus bg             */
    0x000030ffu, /* [11] listgroup_headerbg                                   */
    0x7f7fffffu, /* [12] listgroup_headerfg                                   */
    0xffffffffu, /* [13]                                                      */
    0x6644ff7fu, /* [14]                                                      */
};

/* 0xRRGGBBAA → ImU32 (ImGui's packed ABGR) */
static inline ImU32 PdCol(uint32_t rgba)
{
    uint8_t r = (uint8_t)((rgba >> 24) & 0xffu);
    uint8_t g = (uint8_t)((rgba >> 16) & 0xffu);
    uint8_t b = (uint8_t)((rgba >>  8) & 0xffu);
    uint8_t a = (uint8_t)((rgba >>  0) & 0xffu);
    return IM_COL32(r, g, b, a);
}

/* Return palette row for current active palette (currently always Blue).
 * Extended in a follow-up when pdguiGetPalette() drives multi-palette. */
static const uint32_t *s_activePal(void)
{
    return k_PalBlue;
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

extern "C" {

void pdguiThemeInit(void)
{
    if (s_ThemeInitDone) {
        return;
    }
    s_ThemeInitDone = true;

    sysLogPrintf(LOG_NOTE,
        "PDGUI theme: D5.0 ROM texture decode layer initializing");

    if (!g_TexGeneralConfigs) {
        sysLogPrintf(LOG_ERROR,
            "PDGUI theme: g_TexGeneralConfigs is NULL -- "
            "texInit() has not been called. Pipeline bug.");
        assert(g_TexGeneralConfigs != nullptr &&
               "g_TexGeneralConfigs NULL: texInit() must precede pdguiThemeInit()");
        return;
    }

    /*
     * Register known OG PD menu textures from g_TexGeneralConfigs.
     *
     * Index cross-reference (menugfx.c):
     *   [1]  base:ui_particles  -- success-screen particle shimmer
     *                             (menugfx.c line ~1763: g_TexGeneralConfigs[1])
     *   [6]  base:ui_bg_haze    -- dual-layer rotating green haze background
     *                             (menugfx.c line 239:  g_TexGeneralConfigs[6])
     *
     * If texptr is a small integer (texnum-based), GL upload is deferred.
     * The GBI pipeline will decode on the first render frame that uses the
     * texture.  Until then, pdguiThemeGetTexture() will assert.
     *
     * Palette: NULL (no CI textures expected here; haze and particles are IA).
     */
    s_registerTexConfig("base:ui_bg_haze",   &g_TexGeneralConfigs[6], NULL, false);
    s_registerTexConfig("base:ui_particles", &g_TexGeneralConfigs[1], NULL, false);

    sysLogPrintf(LOG_NOTE,
        "PDGUI theme: registered %u UI texture(s) (%u decoded, %u deferred)",
        (unsigned)(s_ThemeTexCache.size() + 2u),
        (unsigned)s_ThemeTexCache.size(),
        (unsigned)(2u - s_ThemeTexCache.size()));
}

void pdguiThemeShutdown(void)
{
    for (auto &kv : s_ThemeTexCache) {
        if (kv.second) {
            GLuint id = kv.second;
            glDeleteTextures(1, &id);
        }
    }
    s_ThemeTexCache.clear();
    s_ThemeInitDone = false;
    sysLogPrintf(LOG_NOTE, "PDGUI theme: shutdown");
}

/* =========================================================================
 * pdguiThemeGetTexture
 *
 * Replaces pdguiGetUiTexture() D5.0a test-pattern with real decode output.
 * pdgui_backend.cpp's pdguiGetUiTexture() delegates here.
 * ========================================================================= */

void *pdguiThemeGetTexture(const char *catalog_id)
{
    if (!catalog_id || !catalog_id[0]) {
        sysLogPrintf(LOG_ERROR, "PDGUI theme: pdguiThemeGetTexture(NULL)");
        assert(false && "pdguiThemeGetTexture: NULL id");
        return nullptr;
    }

    /* Fast path: already decoded and cached */
    auto it = s_ThemeTexCache.find(catalog_id);
    if (it != s_ThemeTexCache.end() && it->second) {
        return (void *)(uintptr_t)it->second;
    }

    /* Check if registered but deferred (texnum-based, not yet GBI-decoded) */
    const asset_entry_t *e = assetCatalogResolve(catalog_id);
    if (e && e->type == ASSET_UI && e->source_texnum >= 0) {
        sysLogPrintf(LOG_ERROR,
            "PDGUI theme: '%s' (texnum=%d) not yet decoded -- "
            "GBI pipeline has not processed this texture. Pipeline bug.",
            catalog_id, e->source_texnum);
        assert(false && "PDGUI theme texture not decoded -- see LOG_ERROR");
        return nullptr;
    }

    /* Unknown catalog ID -- programming error */
    sysLogPrintf(LOG_ERROR,
        "PDGUI theme: unknown catalog ID '%s' -- "
        "add registration to pdguiThemeInit()",
        catalog_id);
    assert(false && "PDGUI theme: unknown catalog ID");
    return nullptr;
}

/* =========================================================================
 * pdguiThemeDrawPanel
 *
 * Dark semi-transparent navy body fill.
 * Optionally composites a ROM texture (e.g. "base:ui_bg_haze") over the fill.
 * ========================================================================= */

void pdguiThemeDrawPanel(float x, float y, float w, float h,
                          const char *bg_tex_id)
{
    ImDrawList          *dl  = ImGui::GetWindowDrawList();
    const uint32_t      *pal = s_activePal();

    /* Body fill -- dialog_bodybg (very dark navy) */
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), PdCol(pal[4]));

    /* Optional ROM texture overlay */
    if (bg_tex_id) {
        void *tex = pdguiThemeGetTexture(bg_tex_id);
        if (tex) {
            /* Tile the texture at 31% opacity -- matches menugfxRenderBgGreenHaze
             * alpha range (0x7f = 127 out of 255 ≈ 50%, divided by two layers) */
            dl->AddImage(
                (ImTextureID)(uintptr_t)tex,
                ImVec2(x, y), ImVec2(x + w, y + h),
                ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                IM_COL32(255, 255, 255, 80));
        }
    }
}

/* =========================================================================
 * pdguiThemeDrawBorder
 *
 * Faithful port of menugfxRenderDialogBackground() border section:
 *   right side  → dialog_border2
 *   left side   → dialog_border1
 *   bottom      → gradient border1→border2
 * 1px lines (original is a 1-pixel-wide quad).
 * ========================================================================= */

void pdguiThemeDrawBorder(float x, float y, float w, float h,
                           s32 palette_idx)
{
    (void)palette_idx;  /* reserved: multi-palette drive in D5.1 */
    ImDrawList     *dl  = ImGui::GetWindowDrawList();
    const uint32_t *pal = s_activePal();

    ImU32 col1 = PdCol(pal[0]);  /* dialog_border1 -- left / bottom */
    ImU32 col2 = PdCol(pal[2]);  /* dialog_border2 -- right         */

    /* Right border */
    dl->AddLine(ImVec2(x + w - 1.0f, y),
                ImVec2(x + w - 1.0f, y + h),
                col2, 1.0f);

    /* Left border */
    dl->AddLine(ImVec2(x, y),
                ImVec2(x, y + h),
                col1, 1.0f);

    /* Bottom border -- horizontal gradient col1→col2 */
    dl->AddRectFilledMultiColor(
        ImVec2(x,       y + h - 1.0f),
        ImVec2(x + w,   y + h),
        col1, col2, col2, col1);
}

/* =========================================================================
 * pdguiThemeDrawHeader
 *
 * Title gradient bar: dialog_titlebg (top) → dialog_bodybg (bottom).
 * Title text centered, white, with 1px drop shadow.
 * Mirrors menugfxRenderGradient() two-color top-to-bottom pass.
 * ========================================================================= */

void pdguiThemeDrawHeader(float x, float y, float w, float h,
                           const char *title, s32 palette_idx)
{
    (void)palette_idx;
    ImDrawList     *dl  = ImGui::GetWindowDrawList();
    const uint32_t *pal = s_activePal();

    ImU32 topCol = PdCol(pal[1]);   /* dialog_titlebg */
    ImU32 botCol = PdCol(pal[4]);   /* dialog_bodybg  */

    /* Vertical gradient: top color → bottom color */
    dl->AddRectFilledMultiColor(
        ImVec2(x, y), ImVec2(x + w, y + h),
        topCol, topCol,
        botCol, botCol);

    /* Title text centered in bar */
    if (title && title[0]) {
        ImVec2 sz  = ImGui::CalcTextSize(title);
        float  tx  = x + (w - sz.x) * 0.5f;
        float  ty  = y + (h - sz.y) * 0.5f;

        /* 1px drop shadow at half-alpha */
        dl->AddText(ImVec2(tx + 1.0f, ty + 1.0f),
                    IM_COL32(0, 0, 0, 160),
                    title);
        /* Title: dialog_titlefg (white) */
        dl->AddText(ImVec2(tx, ty),
                    PdCol(pal[3]),
                    title);
    }
}

/* =========================================================================
 * pdguiThemeDrawButton
 *
 * Focused: item_focused_outer fill + pdguiDrawButtonEdgeGlow animated rim.
 * Unfocused: transparent (panel body shows through).
 * ========================================================================= */

void pdguiThemeDrawButton(float x, float y, float w, float h, s32 focused)
{
    if (!focused) {
        return;  /* unfocused buttons are transparent */
    }

    ImDrawList     *dl  = ImGui::GetWindowDrawList();
    const uint32_t *pal = s_activePal();

    /* Focused background fill -- item_focused_outer */
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h),
                      PdCol(pal[10]));

    /* Animated edge glow -- delegates to pdgui_style.cpp */
    pdguiDrawButtonEdgeGlow(x, y, w, h, 1);
}

/* =========================================================================
 * pdguiThemeDrawStars
 *
 * PD-style star rating: 5-pointed filled (gold) or outline (dim) stars.
 * Each star is 10-vertex convex polygon approximating a pentagram star.
 * ========================================================================= */

void pdguiThemeDrawStars(float x, float y, s32 filled, s32 total)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();

    static const float kOuter  = 6.0f;   /* outer radius (pixels) */
    static const float kInner  = 2.7f;   /* inner radius (concave tip) */
    static const float kGap    = 2.0f;   /* gap between stars */
    static const float kStep   = kOuter * 2.0f + kGap;

    ImU32 colFilled = IM_COL32(255, 200,  20, 255);  /* gold  */
    ImU32 colEmpty  = IM_COL32( 80,  80,  80, 128);  /* dim   */

    for (s32 i = 0; i < total; i++) {
        float cx = x + (float)i * kStep + kOuter;
        float cy = y + kOuter;

        /* 10-point star: alternating outer/inner radii at 36° each */
        ImVec2 pts[10];
        for (int j = 0; j < 10; j++) {
            float r     = (j & 1) ? kInner : kOuter;
            float angle = (float)j * (float)(M_PI / 5.0) - (float)(M_PI / 2.0);
            pts[j] = ImVec2(cx + r * cosf(angle), cy + r * sinf(angle));
        }

        if (i < filled) {
            dl->AddConvexPolyFilled(pts, 10, colFilled);
        } else {
            /* Outline only for unfilled stars */
            dl->AddPolyline(pts, 10, colEmpty, ImDrawFlags_Closed, 1.0f);
        }
    }
}

/* =========================================================================
 * pdguiThemeDrawScanline
 *
 * Subtle horizontal scanlines at 2px intervals for a retro-CRT overlay.
 * alpha=1.0 → max darkening ~16% (40/255 per line).
 * ========================================================================= */

void pdguiThemeDrawScanline(float x, float y, float w, float h, float alpha)
{
    if (alpha <= 0.0f) {
        return;
    }

    ImDrawList *dl = ImGui::GetWindowDrawList();

    /* Cap at 40 alpha (≈16% darkening) to keep it subtle */
    uint8_t a   = (uint8_t)(alpha * 40.0f);
    ImU32   col = IM_COL32(0, 0, 0, a);

    for (float ry = y; ry < y + h; ry += 2.0f) {
        dl->AddLine(ImVec2(x, ry), ImVec2(x + w, ry), col, 1.0f);
    }
}

} /* extern "C" */
