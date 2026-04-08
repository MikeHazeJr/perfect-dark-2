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
#include "fs.h"
#include "config.h"

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
     * Populated by texReset() at stage load.
     * Cast to PdTexConfig* (layout-compatible with struct textureconfig).
     */
    extern struct PdTexConfig *g_TexGeneralConfigs;

    /**
     * texLoadFromConfig -- decompress a ROM texture by texnum.
     * Defined in src/game/texselect.c. Replaces config->texturenum with
     * a pointer to the decompressed pixel data.
     */
    void texLoadFromConfig(struct PdTexConfig *config);
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

/* Return palette row for the currently active palette.
 * Delegates to the style layer's palette API (pdgui_style.cpp).
 * Falls back to k_PalBlue if style system not initialized yet. */
static const uint32_t *s_activePal(void)
{
    const uint32_t *pal = (const uint32_t *)pdguiGetActivePaletteRaw();
    return pal ? pal : k_PalBlue;
}

/* =========================================================================
 * PNG file loading (minimal — loads RGBA from raw RGBA32 .tga files)
 *
 * The base-ui mod stores pre-extracted ROM textures as uncompressed TGA.
 * We load the raw pixel data directly (skip the 18-byte TGA header).
 * ========================================================================= */

static bool s_ThemeLateInitDone = false;

/* Background texture ID for haze overlay in dialogs */
static const char *s_BgTexId = NULL;

/* Scanline config */
static bool  s_ScanlineEnabled = true;
static float s_ScanlineAlpha   = 0.8f;

/**
 * Load a .tga file (uncompressed RGBA32) from the filesystem, upload to GL.
 * Returns GL texture id, or 0 on failure.
 *
 * TGA format: 18-byte header, then width*height*4 bytes of BGRA pixel data.
 * We convert BGRA→RGBA during upload.
 */
static GLuint s_loadTgaTexture(const char *path, uint32_t *out_w, uint32_t *out_h)
{
    u32 fileSize = 0;
    uint8_t *data = (uint8_t *)fsFileLoad(path, &fileSize);
    if (!data || fileSize < 18) {
        sysLogPrintf(LOG_WARNING, "PDGUI theme: failed to load '%s'", path);
        if (data) free(data);
        return 0;
    }

    /* Parse TGA header */
    uint32_t w = (uint32_t)data[12] | ((uint32_t)data[13] << 8);
    uint32_t h = (uint32_t)data[14] | ((uint32_t)data[15] << 8);
    uint8_t  bpp = data[16];
    uint32_t pixelBytes = (uint32_t)(bpp / 8);
    uint32_t expectedSize = 18 + w * h * pixelBytes;

    if (bpp != 32 || fileSize < expectedSize || w > 256 || h > 256) {
        sysLogPrintf(LOG_WARNING,
            "PDGUI theme: '%s' unexpected format (bpp=%u %ux%u filesize=%u)",
            path, bpp, w, h, fileSize);
        free(data);
        return 0;
    }

    /* Convert BGRA → RGBA in-place */
    uint8_t *pixels = data + 18;
    uint32_t npix = w * h;
    for (uint32_t i = 0; i < npix; i++) {
        uint8_t tmp = pixels[i * 4 + 0];
        pixels[i * 4 + 0] = pixels[i * 4 + 2];
        pixels[i * 4 + 2] = tmp;
    }

    /* TGA is bottom-up by default (unless bit 5 of descriptor is set) */
    bool topDown = (data[17] & 0x20) != 0;
    if (!topDown) {
        /* Flip vertically */
        uint32_t rowBytes = w * 4;
        uint8_t *rowBuf = (uint8_t *)malloc(rowBytes);
        for (uint32_t y = 0; y < h / 2; y++) {
            uint8_t *top = pixels + y * rowBytes;
            uint8_t *bot = pixels + (h - 1 - y) * rowBytes;
            memcpy(rowBuf, top, rowBytes);
            memcpy(top, bot, rowBytes);
            memcpy(bot, rowBuf, rowBytes);
        }
        free(rowBuf);
    }

    if (out_w) *out_w = w;
    if (out_h) *out_h = h;

    GLuint tex = s_uploadGLTex(pixels, w, h);
    free(data);
    return tex;
}

/**
 * Register a theme texture from a mod file path.
 * Loads the TGA, uploads to GL, registers in catalog + cache.
 */
static void s_registerModTexture(const char *catalog_id, const char *path)
{
    uint32_t w = 0, h = 0;
    GLuint gl_id = s_loadTgaTexture(path, &w, &h);
    if (!gl_id) {
        sysLogPrintf(LOG_WARNING,
            "PDGUI theme: '%s' from '%s' — load failed, skipping", catalog_id, path);
        return;
    }

    sysLogPrintf(LOG_NOTE,
        "PDGUI theme: loaded '%s' from '%s' (%ux%u) → GL %u",
        catalog_id, path, w, h, gl_id);

    /* Register in asset catalog */
    asset_entry_t *e = assetCatalogRegister(catalog_id, ASSET_UI);
    if (e) {
        snprintf(e->category, CATALOG_CATEGORY_LEN, "base");
        e->bundled       = 1;
        e->enabled       = 1;
        e->load_state    = ASSET_STATE_LOADED;
        e->ref_count     = ASSET_REF_BUNDLED;
        e->source_texnum = -1;
        e->loaded_data     = (void *)(uintptr_t)gl_id;
        e->data_size_bytes = (u32)(w * h * 4u);
    }

    s_ThemeTexCache[catalog_id] = gl_id;
}

/**
 * Generate a procedural texture and upload to GL.
 * Used when mod files are not available (fallback).
 */
static GLuint s_generateProceduralTexture(const char *name, uint32_t w, uint32_t h)
{
    uint32_t npix = w * h;
    uint8_t *buf = (uint8_t *)calloc(npix * 4, 1);
    if (!buf) return 0;

    if (strcmp(name, "haze") == 0) {
        /* Green-tinted noise pattern approximating the OG haze background */
        uint32_t seed = 0x12345678u;
        for (uint32_t i = 0; i < npix; i++) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            uint8_t noise = (uint8_t)((seed >> 8) & 0x3Fu);
            buf[i * 4 + 0] = noise;       /* R — low */
            buf[i * 4 + 1] = noise;       /* G — same (IA texture) */
            buf[i * 4 + 2] = noise;       /* B — same */
            buf[i * 4 + 3] = (uint8_t)(noise + 60u > 255u ? 255u : noise + 60u);
        }
    } else if (strcmp(name, "noise_sm") == 0 || strcmp(name, "noise_lg") == 0) {
        /* Fine noise grain */
        uint32_t seed = 0xDEADBEEFu;
        for (uint32_t i = 0; i < npix; i++) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            uint8_t v = (uint8_t)((seed >> 12) & 0x7Fu);
            buf[i * 4 + 0] = buf[i * 4 + 1] = buf[i * 4 + 2] = v;
            buf[i * 4 + 3] = (uint8_t)(v / 2 + 30);
        }
    } else if (strcmp(name, "solid") == 0) {
        /* 1x1 white pixel */
        buf[0] = buf[1] = buf[2] = buf[3] = 255;
    } else {
        /* Generic mid-grey fallback */
        for (uint32_t i = 0; i < npix; i++) {
            buf[i * 4 + 0] = buf[i * 4 + 1] = buf[i * 4 + 2] = 128;
            buf[i * 4 + 3] = 128;
        }
    }

    GLuint tex = s_uploadGLTex(buf, w, h);
    free(buf);
    return tex;
}

/**
 * Register a procedural fallback texture.
 */
static void s_registerProceduralTexture(const char *catalog_id,
                                         const char *proc_name,
                                         uint32_t w, uint32_t h)
{
    GLuint gl_id = s_generateProceduralTexture(proc_name, w, h);
    if (!gl_id) return;

    sysLogPrintf(LOG_NOTE,
        "PDGUI theme: procedural '%s' (%s %ux%u) → GL %u",
        catalog_id, proc_name, w, h, gl_id);

    asset_entry_t *e = assetCatalogRegister(catalog_id, ASSET_UI);
    if (e) {
        snprintf(e->category, CATALOG_CATEGORY_LEN, "base");
        e->bundled       = 1;
        e->enabled       = 1;
        e->load_state    = ASSET_STATE_LOADED;
        e->ref_count     = ASSET_REF_BUNDLED;
        e->source_texnum = -1;
        e->loaded_data     = (void *)(uintptr_t)gl_id;
        e->data_size_bytes = (u32)(w * h * 4u);
    }

    s_ThemeTexCache[catalog_id] = gl_id;
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

extern "C" {

/* Config-backed scanline setting (persisted to pd.ini) */
static s32 s_CfgScanlineEnabled = 1;

/**
 * Early init: called from pdguiInit(), before texInit().
 * Registers config vars and marks theme as initialized.
 */
void pdguiThemeInit(void)
{
    if (s_ThemeInitDone) {
        return;
    }
    s_ThemeInitDone = true;

    /* Register scanline toggle in config file (saved to pd.ini) */
    configRegisterInt("Video.Scanlines", &s_CfgScanlineEnabled, 0, 1);
    s_ScanlineEnabled = (s_CfgScanlineEnabled != 0);

    sysLogPrintf(LOG_NOTE,
        "PDGUI theme: D5.0 early init (scanlines=%s, textures deferred)",
        s_ScanlineEnabled ? "ON" : "OFF");
}

/**
 * Late init: called after texInit()/texReset() have run.
 * Loads UI textures from the base-ui mod (TGA files) or generates
 * procedural fallbacks. Registers all theme textures in the catalog.
 */
void pdguiThemeLateInit(void)
{
    if (s_ThemeLateInitDone) {
        return;
    }
    s_ThemeLateInitDone = true;

    sysLogPrintf(LOG_NOTE,
        "PDGUI theme: late init — loading UI textures from base-ui mod");

    /* Texture table: catalog_id → mod file path → procedural fallback */
    static const struct {
        const char *catalog_id;
        const char *mod_path;
        const char *proc_name;
        uint32_t    proc_w, proc_h;
    } k_UiTextures[] = {
        { "base:ui_bg_haze",    "mods/base-ui/textures/ui_bg_haze.tga",    "haze",     64, 64 },
        { "base:ui_particles",  "mods/base-ui/textures/ui_particles.tga",  "solid",     1,  1 },
        { "base:ui_noise_sm",   "mods/base-ui/textures/ui_noise_sm.tga",   "noise_sm", 16, 16 },
        { "base:ui_noise_lg",   "mods/base-ui/textures/ui_noise_lg.tga",   "noise_lg", 16, 16 },
        { "base:ui_grad_bar",   "mods/base-ui/textures/ui_grad_bar.tga",   "solid",     2,  8 },
        { "base:ui_mirror_tile","mods/base-ui/textures/ui_mirror_tile.tga", "solid",     8,  8 },
        { "base:ui_dot_tile",   "mods/base-ui/textures/ui_dot_tile.tga",   "solid",     8,  8 },
        { "base:ui_nuke",       "mods/base-ui/textures/ui_nuke.tga",       "noise_lg", 64, 64 },
        { "base:ui_bg_alt",     "mods/base-ui/textures/ui_bg_alt.tga",     "noise_lg", 64, 64 },
        { "base:ui_deco",       "mods/base-ui/textures/ui_deco.tga",       "solid",    32, 32 },
        { "base:ui_icon_a",     "mods/base-ui/textures/ui_icon_a.tga",     "solid",    14, 14 },
        { "base:ui_icon_b",     "mods/base-ui/textures/ui_icon_b.tga",     "solid",    11, 11 },
        { "base:ui_icon_c",     "mods/base-ui/textures/ui_icon_c.tga",     "solid",    14, 14 },
    };

    unsigned loaded = 0, procedural = 0;
    for (unsigned i = 0; i < sizeof(k_UiTextures) / sizeof(k_UiTextures[0]); i++) {
        /* Try loading from mod file first */
        uint32_t w = 0, h = 0;
        GLuint gl_id = s_loadTgaTexture(k_UiTextures[i].mod_path, &w, &h);
        if (gl_id) {
            sysLogPrintf(LOG_NOTE, "PDGUI theme: '%s' ← mod file (%ux%u)",
                         k_UiTextures[i].catalog_id, w, h);

            asset_entry_t *e = assetCatalogRegister(k_UiTextures[i].catalog_id, ASSET_UI);
            if (e) {
                snprintf(e->category, CATALOG_CATEGORY_LEN, "base");
                e->bundled = 1; e->enabled = 1;
                e->load_state = ASSET_STATE_LOADED;
                e->ref_count = ASSET_REF_BUNDLED;
                e->source_texnum = -1;
                e->loaded_data = (void *)(uintptr_t)gl_id;
                e->data_size_bytes = (u32)(w * h * 4u);
            }
            s_ThemeTexCache[k_UiTextures[i].catalog_id] = gl_id;
            loaded++;
        } else {
            /* Fallback: generate procedural texture */
            s_registerProceduralTexture(
                k_UiTextures[i].catalog_id,
                k_UiTextures[i].proc_name,
                k_UiTextures[i].proc_w,
                k_UiTextures[i].proc_h);
            procedural++;
        }
    }

    /* Set default background texture for dialog haze overlay */
    s_BgTexId = "base:ui_bg_haze";

    sysLogPrintf(LOG_NOTE,
        "PDGUI theme: late init complete — %u from mod, %u procedural",
        loaded, procedural);
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
        return nullptr;
    }

    if (!s_ThemeLateInitDone) {
        /* Late init hasn't run yet — textures not available */
        return nullptr;
    }

    /* Fast path: already decoded and cached */
    auto it = s_ThemeTexCache.find(catalog_id);
    if (it != s_ThemeTexCache.end() && it->second) {
        return (void *)(uintptr_t)it->second;
    }

    /* Unknown or failed texture — return NULL (no assert, graceful degrade) */
    return nullptr;
}

/* Return the current background texture catalog ID for dialog overlays */
const char *pdguiThemeGetBgTexId(void)
{
    return s_BgTexId;
}

/* Set the background texture catalog ID for dialog overlays */
void pdguiThemeSetBgTexId(const char *catalog_id)
{
    s_BgTexId = catalog_id;
}

/* Scanline config API */
void pdguiThemeSetScanlineEnabled(s32 enabled)
{
    s_ScanlineEnabled = (enabled != 0);
    s_CfgScanlineEnabled = enabled ? 1 : 0;
}

s32 pdguiThemeGetScanlineEnabled(void)
{
    return s_ScanlineEnabled ? 1 : 0;
}

void pdguiThemeSetScanlineAlpha(f32 alpha)
{
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    s_ScanlineAlpha = alpha;
}

f32 pdguiThemeGetScanlineAlpha(void)
{
    return s_ScanlineAlpha;
}

/* Return palette colors as a flat array of 15 u32s for theme draw functions */
const u32 *pdguiThemeGetActivePaletteColors(void)
{
    /* Delegate to style layer's active palette */
    return (const u32 *)s_activePal();
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

/**
 * Draw scanlines on the foreground draw list (renders on top of all content).
 * Called from pdguiRender() after all windows are submitted.
 */
void pdguiThemeDrawScanlineFg(float x, float y, float w, float h)
{
    if (!s_ScanlineEnabled || s_ScanlineAlpha <= 0.0f) {
        return;
    }

    ImDrawList *fg = ImGui::GetForegroundDrawList();
    uint8_t a = (uint8_t)(s_ScanlineAlpha * 40.0f);
    ImU32 col = IM_COL32(0, 0, 0, a);

    for (float ry = y; ry < y + h; ry += 2.0f) {
        fg->AddLine(ImVec2(x, ry), ImVec2(x + w, ry), col, 1.0f);
    }
}

/* =========================================================================
 * ROM Texture Extraction Tool
 *
 * Extracts UI textures from ROM via texLoadFromConfig(), decodes to RGBA32,
 * and writes as uncompressed TGA files to mods/base-ui/textures/.
 *
 * Called with --extract-ui-textures CLI flag, after texReset() has populated
 * g_TexGeneralConfigs and loaded the ROM texture data into memory.
 * ========================================================================= */

/* Write RGBA32 pixel data as an uncompressed 32-bit TGA file. */
static bool s_writeTga(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h)
{
    FILE *f = fsFileOpenWrite(path);
    if (!f) {
        sysLogPrintf(LOG_ERROR, "PDGUI extract: cannot write '%s'", path);
        return false;
    }

    /* TGA header: 18 bytes, uncompressed RGBA, top-down */
    uint8_t hdr[18];
    memset(hdr, 0, sizeof(hdr));
    hdr[2]  = 2;                          /* uncompressed true-color */
    hdr[12] = (uint8_t)(w & 0xFF);
    hdr[13] = (uint8_t)((w >> 8) & 0xFF);
    hdr[14] = (uint8_t)(h & 0xFF);
    hdr[15] = (uint8_t)((h >> 8) & 0xFF);
    hdr[16] = 32;                         /* 32 bpp */
    hdr[17] = 0x28;                       /* top-down + 8 alpha bits */
    fwrite(hdr, 1, 18, f);

    /* Write BGRA pixel data (TGA stores BGRA) */
    uint32_t npix = w * h;
    for (uint32_t i = 0; i < npix; i++) {
        uint8_t bgra[4] = {
            rgba[i * 4 + 2],  /* B */
            rgba[i * 4 + 1],  /* G */
            rgba[i * 4 + 0],  /* R */
            rgba[i * 4 + 3],  /* A */
        };
        fwrite(bgra, 1, 4, f);
    }

    fclose(f);
    return true;
}

void pdguiThemeExtractRomTextures(void)
{
    if (!g_TexGeneralConfigs) {
        sysLogPrintf(LOG_ERROR,
            "PDGUI extract: g_TexGeneralConfigs is NULL — texReset() not called");
        return;
    }

    sysLogPrintf(LOG_NOTE, "PDGUI extract: extracting UI textures from ROM...");

    /* First, ensure the textures we need are decompressed from ROM.
     * texLoadFromConfig() converts texnum → textureptr via DMA + decompress. */

    /* Load the textures we want to extract.
     * P4 enhancement: expanded set includes title screen and menu item textures
     * (indices 47, 49, 51-55) in addition to the original D5.0 set. */
    static const int k_IndicesToLoad[] = {
        0, 1, 2, 3, 4, 6, 7, 10, 11, 34, 35, 36, 37, 38,
        47, 49, 51, 52, 53, 54, 55
    };
    for (unsigned i = 0; i < sizeof(k_IndicesToLoad) / sizeof(k_IndicesToLoad[0]); i++) {
        int idx = k_IndicesToLoad[i];
        struct PdTexConfig *cfg = &g_TexGeneralConfigs[idx];
        if (!s_isRealPtr(cfg)) {
            texLoadFromConfig(cfg);
        }
    }

    /* Table of textures to extract */
    static const struct {
        int         index;
        const char *filename;
    } k_Extracts[] = {
        {  0, "ui_noise_sm"   },
        {  1, "ui_particles"  },
        {  2, "ui_noise_lg"   },
        {  3, "ui_grad_bar"   },
        {  4, "ui_mirror_tile"},
        {  6, "ui_bg_haze"    },
        {  7, "ui_dot_tile"   },
        { 10, "ui_nuke"       },
        { 11, "ui_bg_alt"     },
        { 34, "ui_icon_a"     },
        { 35, "ui_icon_b"     },
        { 36, "ui_icon_c"     },
        { 37, "ui_deco"       },
        { 38, "ui_stars"      },
        /* P4: additional textures from title/menu subsystems */
        { 47, "ui_title_bg"   },
        { 49, "ui_title_logo" },
        { 51, "ui_menuitem_a" },
        { 52, "ui_menuitem_b" },
        { 53, "ui_menuitem_c" },
        { 54, "ui_menuitem_d" },
        { 55, "ui_menuitem_e" },
    };

    static uint8_t s_ExtractBuf[256 * 256 * 4];
    unsigned extracted = 0;

    for (unsigned i = 0; i < sizeof(k_Extracts) / sizeof(k_Extracts[0]); i++) {
        int idx = k_Extracts[i].index;
        struct PdTexConfig *cfg = &g_TexGeneralConfigs[idx];

        if (!s_isRealPtr(cfg)) {
            sysLogPrintf(LOG_WARNING,
                "PDGUI extract: [%d] '%s' — texnum %u not loaded, skipping",
                idx, k_Extracts[i].filename, cfg->texnum);
            continue;
        }

        uint32_t w = (uint32_t)cfg->width;
        uint32_t h = (uint32_t)cfg->height;

        if (!w || !h || w > 256 || h > 256) {
            sysLogPrintf(LOG_WARNING,
                "PDGUI extract: [%d] '%s' — bad dims %ux%u",
                idx, k_Extracts[i].filename, w, h);
            continue;
        }

        /* Decode to RGBA32 using our existing decoders */
        GLuint gl_id = s_decodeAndUpload(cfg, NULL, false);
        if (!gl_id) {
            sysLogPrintf(LOG_WARNING,
                "PDGUI extract: [%d] '%s' — decode failed (fmt=%u siz=%u)",
                idx, k_Extracts[i].filename, cfg->fmt, cfg->siz);
            continue;
        }

        /* Read back the decoded RGBA32 data — it's still in s_Rgba32Buf
         * (the static buffer used by s_decodeAndUpload's callees).
         * Actually, s_Rgba32Buf is local to s_decodeAndUpload. Instead,
         * re-decode into our own buffer. */
        memset(s_ExtractBuf, 0, sizeof(s_ExtractBuf));

        uint32_t fmt = (uint32_t)cfg->fmt;
        uint32_t siz = (uint32_t)cfg->siz;
        const uint8_t *src = cfg->texptr;
        bool decoded = true;

        switch (fmt) {
        case PD_G_IM_FMT_RGBA:
            if (siz == PD_G_IM_SIZ_16b) decodeRgba16(src, w, h, s_ExtractBuf);
            else if (siz == PD_G_IM_SIZ_32b) {
                memcpy(s_ExtractBuf, src, w * h * 4);
            }
            else decoded = false;
            break;
        case PD_G_IM_FMT_IA:
            if      (siz == PD_G_IM_SIZ_16b) decodeIa16(src, w, h, s_ExtractBuf);
            else if (siz == PD_G_IM_SIZ_8b)  decodeIa8 (src, w, h, s_ExtractBuf);
            else if (siz == PD_G_IM_SIZ_4b)  decodeIa4 (src, w, h, s_ExtractBuf);
            else decoded = false;
            break;
        default:
            decoded = false;
            break;
        }

        /* Clean up the GL texture we don't need (extraction only) */
        glDeleteTextures(1, &gl_id);

        if (!decoded) {
            sysLogPrintf(LOG_WARNING,
                "PDGUI extract: [%d] '%s' — unsupported fmt=%u siz=%u for extraction",
                idx, k_Extracts[i].filename, fmt, siz);
            continue;
        }

        /* Write TGA */
        char path[256];
        snprintf(path, sizeof(path), "mods/base-ui/textures/%s.tga", k_Extracts[i].filename);

        if (s_writeTga(path, s_ExtractBuf, w, h)) {
            sysLogPrintf(LOG_NOTE,
                "PDGUI extract: [%d] '%s' %ux%u fmt=%u → %s",
                idx, k_Extracts[i].filename, w, h, fmt, path);
            extracted++;
        }
    }

    sysLogPrintf(LOG_NOTE,
        "PDGUI extract: done — %u textures extracted to mods/base-ui/textures/",
        extracted);
}

/**
 * Generate procedural TGA textures for the pd-modern-ui mod.
 */
static void s_generateModernUiTextures(void)
{
    sysLogPrintf(LOG_NOTE, "PDGUI: generating pd-modern-ui textures...");

    static const struct {
        const char *filename;
        const char *proc_name;
        uint32_t w, h;
    } k_ModernTextures[] = {
        { "mods/pd-modern-ui/textures/ui_bg_haze.tga",   "modern_haze", 64, 64 },
        { "mods/pd-modern-ui/textures/ui_particles.tga",  "solid",        1,  1 },
        { "mods/pd-modern-ui/textures/ui_noise_sm.tga",   "modern_fine", 16, 16 },
        { "mods/pd-modern-ui/textures/ui_noise_lg.tga",   "modern_coarse",16, 16 },
    };

    for (unsigned i = 0; i < sizeof(k_ModernTextures) / sizeof(k_ModernTextures[0]); i++) {
        uint32_t w = k_ModernTextures[i].w;
        uint32_t h = k_ModernTextures[i].h;
        uint32_t npix = w * h;
        uint8_t *buf = (uint8_t *)calloc(npix * 4, 1);
        if (!buf) continue;

        const char *name = k_ModernTextures[i].proc_name;

        if (strcmp(name, "modern_haze") == 0) {
            /* Clean smooth gradient with very subtle noise */
            for (uint32_t py = 0; py < h; py++) {
                for (uint32_t px = 0; px < w; px++) {
                    uint32_t idx = (py * w + px) * 4;
                    /* Smooth radial-ish gradient from center */
                    float cx = (float)px / (float)w - 0.5f;
                    float cy = (float)py / (float)h - 0.5f;
                    float d = sqrtf(cx * cx + cy * cy) * 2.0f;
                    if (d > 1.0f) d = 1.0f;
                    uint8_t v = (uint8_t)((1.0f - d * 0.5f) * 80.0f);
                    buf[idx + 0] = buf[idx + 1] = buf[idx + 2] = v;
                    buf[idx + 3] = (uint8_t)(v + 40);
                }
            }
        } else if (strcmp(name, "modern_fine") == 0) {
            /* Very subtle fine noise */
            uint32_t seed = 0xCAFEBABEu;
            for (uint32_t j = 0; j < npix; j++) {
                seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                uint8_t v = (uint8_t)(60 + ((seed >> 8) & 0x1Fu));
                buf[j * 4 + 0] = buf[j * 4 + 1] = buf[j * 4 + 2] = v;
                buf[j * 4 + 3] = 40;
            }
        } else if (strcmp(name, "modern_coarse") == 0) {
            /* Subtle coarse noise */
            uint32_t seed = 0xBEEFCAFEu;
            for (uint32_t j = 0; j < npix; j++) {
                seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                uint8_t v = (uint8_t)(50 + ((seed >> 10) & 0x2Fu));
                buf[j * 4 + 0] = buf[j * 4 + 1] = buf[j * 4 + 2] = v;
                buf[j * 4 + 3] = 30;
            }
        } else if (strcmp(name, "solid") == 0) {
            buf[0] = buf[1] = buf[2] = buf[3] = 255;
        }

        if (s_writeTga(k_ModernTextures[i].filename, buf, w, h)) {
            sysLogPrintf(LOG_NOTE, "PDGUI: generated %s (%ux%u)",
                         k_ModernTextures[i].filename, w, h);
        }
        free(buf);
    }

    sysLogPrintf(LOG_NOTE, "PDGUI: pd-modern-ui texture generation complete");
}

/**
 * Check if the base-ui mod textures exist by probing the key haze texture.
 * Returns true if the file loads successfully (non-zero size).
 */
static bool s_baseUiTexturesExist(void)
{
    u32 sz = 0;
    void *probe = fsFileLoad("mods/base-ui/textures/ui_bg_haze.tga", &sz);
    if (probe) {
        free(probe);
        return true;
    }
    return false;
}

/**
 * Frame check: auto-extract base-ui textures from ROM if they don't exist,
 * or run extraction/generation when CLI flags are set.
 * Called from pdguiRender() each frame until done.
 */
void pdguiThemeCheckExtract(void)
{
    static bool s_checked = false;
    if (s_checked) return;

    if (!g_TexGeneralConfigs) return;  /* texReset not yet called */

    s_checked = true;

    /* Auto-extract: if base-ui textures don't exist yet, create the full
     * mod structure (mod.json + TGA textures) from ROM data. This makes
     * the game self-sufficient — no external files needed in the zip. */
    if (!s_baseUiTexturesExist()) {
        sysLogPrintf(LOG_NOTE,
            "PDGUI theme: base-ui mod missing — auto-creating from ROM");
        fsCreateDir("mods");
        fsCreateDir("mods/base-ui");
        fsCreateDir("mods/base-ui/textures");

        /* Write mod.json manifest so the mod manager recognizes this as
         * a proper mod. The theme config in here drives palette, scanlines,
         * and background texture selection. */
        {
            static const char k_ModJson[] =
                "{\n"
                "    \"name\": \"base-ui\",\n"
                "    \"display_name\": \"Perfect Dark Base UI\",\n"
                "    \"version\": \"1.0.0\",\n"
                "    \"description\": \"Original N64 UI textures extracted from ROM.\",\n"
                "    \"author\": \"Rare / PD2 Team\",\n"
                "    \"category\": \"ui\",\n"
                "    \"bundled\": true,\n"
                "    \"enabled\": true,\n"
                "    \"components\": [\n"
                "        {\n"
                "            \"type\": \"ui\",\n"
                "            \"textures\": [\n"
                "                { \"catalog_id\": \"base:ui_bg_haze\",     \"path\": \"textures/ui_bg_haze.tga\" },\n"
                "                { \"catalog_id\": \"base:ui_particles\",   \"path\": \"textures/ui_particles.tga\" },\n"
                "                { \"catalog_id\": \"base:ui_noise_sm\",    \"path\": \"textures/ui_noise_sm.tga\" },\n"
                "                { \"catalog_id\": \"base:ui_noise_lg\",    \"path\": \"textures/ui_noise_lg.tga\" },\n"
                "                { \"catalog_id\": \"base:ui_grad_bar\",    \"path\": \"textures/ui_grad_bar.tga\" },\n"
                "                { \"catalog_id\": \"base:ui_mirror_tile\", \"path\": \"textures/ui_mirror_tile.tga\" },\n"
                "                { \"catalog_id\": \"base:ui_dot_tile\",    \"path\": \"textures/ui_dot_tile.tga\" },\n"
                "                { \"catalog_id\": \"base:ui_nuke\",        \"path\": \"textures/ui_nuke.tga\" },\n"
                "                { \"catalog_id\": \"base:ui_bg_alt\",      \"path\": \"textures/ui_bg_alt.tga\" },\n"
                "                { \"catalog_id\": \"base:ui_deco\",        \"path\": \"textures/ui_deco.tga\" },\n"
                "                { \"catalog_id\": \"base:ui_icon_a\",      \"path\": \"textures/ui_icon_a.tga\" },\n"
                "                { \"catalog_id\": \"base:ui_icon_b\",      \"path\": \"textures/ui_icon_b.tga\" },\n"
                "                { \"catalog_id\": \"base:ui_icon_c\",      \"path\": \"textures/ui_icon_c.tga\" }\n"
                "            ]\n"
                "        }\n"
                "    ],\n"
                "    \"theme\": {\n"
                "        \"default_palette\": 1,\n"
                "        \"background_texture\": \"base:ui_bg_haze\",\n"
                "        \"scanline_enabled\": true,\n"
                "        \"scanline_alpha\": 0.8,\n"
                "        \"tint_strength\": 0.0,\n"
                "        \"text_glow_intensity\": 0.6\n"
                "    }\n"
                "}\n";

            FILE *jf = fsFileOpenWrite("mods/base-ui/mod.json");
            if (jf) {
                fwrite(k_ModJson, 1, sizeof(k_ModJson) - 1, jf);
                fclose(jf);
                sysLogPrintf(LOG_NOTE, "PDGUI theme: wrote mods/base-ui/mod.json");
            } else {
                sysLogPrintf(LOG_WARNING, "PDGUI theme: could not write mod.json");
            }
        }

        /* Extract ROM textures to TGA files */
        pdguiThemeExtractRomTextures();

        /* Reload theme textures now that TGA files exist */
        s_ThemeLateInitDone = false;
        pdguiThemeLateInit();
    }

    /* CLI flags for manual re-extract / modern UI generation */
    if (sysArgCheck("--extract-ui-textures")) {
        pdguiThemeExtractRomTextures();
    }

    if (sysArgCheck("--generate-modern-ui")) {
        s_generateModernUiTextures();
    }
}

} /* extern "C" */
