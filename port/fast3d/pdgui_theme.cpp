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
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string>
#include <unordered_map>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "glad/glad.h"
#include "imgui/imgui.h"
#include "pdgui_theme.h"
#include "pdgui_style.h"
#include "pdgui_nineslice.h"
#include "pdgui_scaling.h"
#include "system.h"
#include "assetcatalog.h"
#include "fs.h"
#include "config.h"
extern "C" {
#include "modmgr.h"
}

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

/* RGBA32: N64 stores as R,G,B,A in big-endian order — 4 bytes per pixel,
 * same as our output format. Direct copy. */
static void decodeRgba32(const uint8_t *src, uint32_t w, uint32_t h,
                          uint8_t *out)
{
    memcpy(out, src, (size_t)w * (size_t)h * 4u);
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

    /* N64 textureconfig is u8 width/height so dims are hardware-capped at
     * 255×255 — but the decode buffer is now dynamically allocated so HD
     * mod textures going through the TGA path can grow beyond the N64
     * ceiling without touching this function. */
    if (!w || !h) {
        sysLogPrintf(LOG_ERROR,
            "PDGUI theme: bad texture dims %ux%u (zero dimension)", w, h);
        return 0;
    }

    size_t rgbaBytes = (size_t)w * (size_t)h * 4u;
    uint8_t *rgba32 = (uint8_t *)malloc(rgbaBytes);
    if (!rgba32) {
        sysLogPrintf(LOG_ERROR,
            "PDGUI theme: OOM decoding %ux%u texture (%zu bytes)",
            w, h, rgbaBytes);
        return 0;
    }

    switch (fmt) {
    case PD_G_IM_FMT_RGBA:
        if (siz == PD_G_IM_SIZ_16b) {
            decodeRgba16(src, w, h, rgba32);
        } else if (siz == PD_G_IM_SIZ_32b) {
            decodeRgba32(src, w, h, rgba32);
        } else {
            sysLogPrintf(LOG_ERROR,
                "PDGUI theme: unsupported RGBA siz=%u", siz);
            free(rgba32);
            return 0;
        }
        break;

    case PD_G_IM_FMT_IA:
        if      (siz == PD_G_IM_SIZ_16b) decodeIa16(src, w, h, rgba32);
        else if (siz == PD_G_IM_SIZ_8b)  decodeIa8 (src, w, h, rgba32);
        else if (siz == PD_G_IM_SIZ_4b)  decodeIa4 (src, w, h, rgba32);
        else {
            sysLogPrintf(LOG_ERROR,
                "PDGUI theme: unsupported IA siz=%u", siz);
            free(rgba32);
            return 0;
        }
        break;

    case PD_G_IM_FMT_CI:
        if (!pal) {
            sysLogPrintf(LOG_ERROR,
                "PDGUI theme: CI texture requires palette; none supplied");
            free(rgba32);
            return 0;
        }
        if      (siz == PD_G_IM_SIZ_8b)  decodeCi8(src, pal, is_ia16, w, h, rgba32);
        else if (siz == PD_G_IM_SIZ_4b)  decodeCi4(src, pal, is_ia16, w, h, rgba32);
        else {
            sysLogPrintf(LOG_ERROR,
                "PDGUI theme: unsupported CI siz=%u", siz);
            free(rgba32);
            return 0;
        }
        break;

    default:
        sysLogPrintf(LOG_ERROR,
            "PDGUI theme: unsupported N64 fmt=%u siz=%u", fmt, siz);
        free(rgba32);
        return 0;
    }

    GLuint tex = s_uploadGLTex(rgba32, w, h);
    free(rgba32);
    return tex;
}

/* =========================================================================
 * Texture registry
 *
 * catalog_id (string) → GL texture id (uint32_t)
 * Populated by pdguiThemeInit() for each known UI texture.
 * ========================================================================= */

static std::unordered_map<std::string, GLuint> s_ThemeTexCache;
/* Parallel map of texture dimensions (width, height) for catalog IDs.
 * Lets the chrome render path call pdguiNinesliceDrawEx with accurate
 * source texture size without depending on the asset catalog's
 * data_size_bytes heuristic. */
static std::unordered_map<std::string, std::pair<uint32_t, uint32_t>> s_ThemeTexDims;
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
        s_ThemeTexDims[catalog_id]  = { (uint32_t)cfg->width, (uint32_t)cfg->height };
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

struct chrome_style_entry {
    char id[64];
    char name[96];
};

#define PDGUI_MAX_CHROME_STYLES 32
static chrome_style_entry s_ChromeStyles[PDGUI_MAX_CHROME_STYLES];
static s32 s_ChromeStyleCount = 0;

/* Scanline config */
static bool  s_ScanlineEnabled = true;
static float s_ScanlineAlpha   = 0.5f;

/**
 * Load a .tga file (uncompressed RGBA32) from the filesystem, upload to GL.
 * Returns GL texture id, or 0 on failure.
 *
 * TGA format: 18-byte header, then width*height*4 bytes of BGRA pixel data.
 * We convert BGRA→RGBA during upload.
 */
/**
 * Load an uncompressed 24-bit or 32-bit TGA file into an OpenGL texture.
 *
 * Accepts arbitrary dimensions (HD mod assets welcome). RLE-compressed TGAs
 * (image type 10) are rejected — authors must export uncompressed.
 *
 * For 24-bit TGA the alpha channel is synthesized as 0xFF (fully opaque)
 * when expanding BGR -> RGBA. 32-bit TGAs preserve the file's alpha.
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
    uint8_t  idLen      = data[0];
    uint8_t  cmapType   = data[1];
    uint8_t  imageType  = data[2];
    uint32_t w          = (uint32_t)data[12] | ((uint32_t)data[13] << 8);
    uint32_t h          = (uint32_t)data[14] | ((uint32_t)data[15] << 8);
    uint8_t  bpp        = data[16];
    uint8_t  descriptor = data[17];
    uint32_t pixelBytes = (uint32_t)(bpp / 8);
    uint32_t pixelStart = 18u + (uint32_t)idLen;  /* skip optional image-id block */
    uint32_t expectedSize = pixelStart + w * h * pixelBytes;

    /* Hard rejects: RLE, colour-mapped, zero dims, non-24/32 bpp, short file. */
    if (imageType != 2 /* uncompressed truecolour */ ||
        cmapType != 0 ||
        !w || !h ||
        (bpp != 24 && bpp != 32) ||
        fileSize < expectedSize) {
        sysLogPrintf(LOG_WARNING,
            "PDGUI theme: '%s' unsupported TGA (type=%u cmap=%u bpp=%u %ux%u filesize=%u)",
            path, imageType, cmapType, bpp, w, h, fileSize);
        free(data);
        return 0;
    }

    /* Allocate destination RGBA32 buffer sized for the actual image. */
    size_t rgbaBytes = (size_t)w * (size_t)h * 4u;
    uint8_t *rgba = (uint8_t *)malloc(rgbaBytes);
    if (!rgba) {
        sysLogPrintf(LOG_ERROR,
            "PDGUI theme: '%s' OOM allocating %zu RGBA bytes (%ux%u)",
            path, rgbaBytes, w, h);
        free(data);
        return 0;
    }

    /* Expand TGA pixels (BGR / BGRA) into tightly packed RGBA32. */
    const uint8_t *src = data + pixelStart;
    uint32_t npix = w * h;
    if (bpp == 32) {
        for (uint32_t i = 0; i < npix; i++) {
            rgba[i * 4 + 0] = src[i * 4 + 2];  /* R <- B */
            rgba[i * 4 + 1] = src[i * 4 + 1];  /* G */
            rgba[i * 4 + 2] = src[i * 4 + 0];  /* B <- R */
            rgba[i * 4 + 3] = src[i * 4 + 3];  /* A */
        }
    } else /* bpp == 24 */ {
        for (uint32_t i = 0; i < npix; i++) {
            rgba[i * 4 + 0] = src[i * 3 + 2];  /* R <- B */
            rgba[i * 4 + 1] = src[i * 3 + 1];  /* G */
            rgba[i * 4 + 2] = src[i * 3 + 0];  /* B <- R */
            rgba[i * 4 + 3] = 0xFF;            /* opaque */
        }
    }

    /* TGA is bottom-up by default (origin in bottom-left).
     * Bit 5 of descriptor set => top-down (origin top-left). */
    bool topDown = (descriptor & 0x20) != 0;
    if (!topDown) {
        uint32_t rowBytes = w * 4;
        uint8_t *rowBuf = (uint8_t *)malloc(rowBytes);
        if (rowBuf) {
            for (uint32_t y = 0; y < h / 2; y++) {
                uint8_t *top = rgba + y * rowBytes;
                uint8_t *bot = rgba + (h - 1 - y) * rowBytes;
                memcpy(rowBuf, top, rowBytes);
                memcpy(top, bot, rowBytes);
                memcpy(bot, rowBuf, rowBytes);
            }
            free(rowBuf);
        }
    }

    if (out_w) *out_w = w;
    if (out_h) *out_h = h;

    GLuint tex = s_uploadGLTex(rgba, w, h);
    free(rgba);
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
    s_ThemeTexDims[catalog_id]  = { w, h };
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
    s_ThemeTexDims[catalog_id]  = { w, h };
}

/* =========================================================================
 * UI chrome style registry + mod.json chrome parser
 * ========================================================================= */

enum chrome_jtok_type {
    CJT_NONE = 0, CJT_LBRACE, CJT_RBRACE, CJT_LBRACKET, CJT_RBRACKET,
    CJT_COLON, CJT_COMMA, CJT_STRING, CJT_NUMBER, CJT_TRUE, CJT_FALSE,
    CJT_NULL_TOK, CJT_EOF, CJT_ERROR
};

struct chrome_jtok {
    const char *start;
    int len;
    chrome_jtok_type type;
};

struct chrome_jparse {
    const char *pos;
};

static void cjson_skip_ws(chrome_jparse *j)
{
    while (*j->pos && (*j->pos == ' ' || *j->pos == '\t' ||
           *j->pos == '\n' || *j->pos == '\r')) {
        j->pos++;
    }
}

static chrome_jtok cjson_next(chrome_jparse *j)
{
    chrome_jtok tok = { nullptr, 0, CJT_NONE };
    cjson_skip_ws(j);
    if (!*j->pos) { tok.type = CJT_EOF; return tok; }

    tok.start = j->pos;
    char c = *j->pos;
    switch (c) {
    case '{': tok.type = CJT_LBRACE; tok.len = 1; j->pos++; break;
    case '}': tok.type = CJT_RBRACE; tok.len = 1; j->pos++; break;
    case '[': tok.type = CJT_LBRACKET; tok.len = 1; j->pos++; break;
    case ']': tok.type = CJT_RBRACKET; tok.len = 1; j->pos++; break;
    case ':': tok.type = CJT_COLON; tok.len = 1; j->pos++; break;
    case ',': tok.type = CJT_COMMA; tok.len = 1; j->pos++; break;
    case '"': {
        j->pos++;
        tok.start = j->pos;
        while (*j->pos && *j->pos != '"') {
            if (*j->pos == '\\') j->pos++;
            if (*j->pos) j->pos++;
        }
        tok.len = (int)(j->pos - tok.start);
        tok.type = CJT_STRING;
        if (*j->pos == '"') j->pos++;
        break;
    }
    default:
        if (c == '-' || (c >= '0' && c <= '9')) {
            if (c == '-') j->pos++;
            while (*j->pos >= '0' && *j->pos <= '9') j->pos++;
            /* Fractional part: accept '.' followed by digits so floats like
             * 1.000 don't trip the ERROR branch (which aborts cjson_skip_value
             * mid-value and corrupts the parser position for later top-level
             * keys). Essential for skipping the nine-slice tool's
             * chrome_authoring block (contains border_scale / inset_pct
             * floats) before components is read. */
            if (*j->pos == '.') {
                j->pos++;
                while (*j->pos >= '0' && *j->pos <= '9') j->pos++;
            }
            /* Exponent part (1e6, -2.5E-3). */
            if (*j->pos == 'e' || *j->pos == 'E') {
                j->pos++;
                if (*j->pos == '+' || *j->pos == '-') j->pos++;
                while (*j->pos >= '0' && *j->pos <= '9') j->pos++;
            }
            tok.len = (int)(j->pos - tok.start);
            tok.type = CJT_NUMBER;
        } else if (strncmp(j->pos, "true", 4) == 0) {
            tok.type = CJT_TRUE; tok.len = 4; j->pos += 4;
        } else if (strncmp(j->pos, "false", 5) == 0) {
            tok.type = CJT_FALSE; tok.len = 5; j->pos += 5;
        } else if (strncmp(j->pos, "null", 4) == 0) {
            tok.type = CJT_NULL_TOK; tok.len = 4; j->pos += 4;
        } else {
            tok.type = CJT_ERROR;
            j->pos++;
        }
        break;
    }
    return tok;
}

static void cjson_str(const chrome_jtok *tok, char *dst, int maxlen)
{
    if (tok->type != CJT_STRING || !tok->start || maxlen <= 0) {
        if (maxlen > 0) dst[0] = '\0';
        return;
    }
    int n = tok->len < (maxlen - 1) ? tok->len : (maxlen - 1);
    memcpy(dst, tok->start, n);
    dst[n] = '\0';
}

static s32 cjson_int(const chrome_jtok *tok, s32 def)
{
    if (tok->type != CJT_NUMBER || !tok->start) return def;
    return (s32)strtol(tok->start, nullptr, 10);
}

static bool cjson_key_eq(const chrome_jtok *tok, const char *key)
{
    int klen = (int)strlen(key);
    return tok->type == CJT_STRING && tok->len == klen &&
           memcmp(tok->start, key, klen) == 0;
}

static void cjson_skip_value(chrome_jparse *j)
{
    chrome_jtok tok = cjson_next(j);
    if (tok.type == CJT_LBRACE) {
        int depth = 1;
        while (depth > 0) {
            tok = cjson_next(j);
            if (tok.type == CJT_LBRACE) depth++;
            else if (tok.type == CJT_RBRACE) depth--;
            else if (tok.type == CJT_EOF || tok.type == CJT_ERROR) return;
        }
    } else if (tok.type == CJT_LBRACKET) {
        int depth = 1;
        while (depth > 0) {
            tok = cjson_next(j);
            if (tok.type == CJT_LBRACKET) depth++;
            else if (tok.type == CJT_RBRACKET) depth--;
            else if (tok.type == CJT_EOF || tok.type == CJT_ERROR) return;
        }
    }
}

/* =========================================================================
 * UI Texture Mod Override System (D5 Phase 4)
 *
 * Parses mod.json components[] for { "type": "ui" } entries and calls
 * s_registerModTexture for each catalog_id/path pair found.  Overrides
 * are applied on top of the base-ui textures already loaded by
 * pdguiThemeLateInit, and re-applied when modmgrApplyChanges runs.
 * ========================================================================= */

s32 pdguiThemeScanModUiTextures(const char *mod_dir)
{
    if (!mod_dir || !mod_dir[0]) return 0;

    char mod_json_path[FS_MAXPATH];
    snprintf(mod_json_path, sizeof(mod_json_path), "%s/mod.json", mod_dir);

    u32 size = 0;
    char *raw = (char *)fsFileLoad(mod_json_path, &size);
    if (!raw || !size) { if (raw) free(raw); return 0; }
    char *json = (char *)malloc(size + 1);
    if (!json) { free(raw); return 0; }
    memcpy(json, raw, size);
    json[size] = '\0';
    free(raw);

    s32 count = 0;
    chrome_jparse jp = { json };
    chrome_jtok tok = cjson_next(&jp);
    if (tok.type != CJT_LBRACE) { free(json); return 0; }

    /* Find "components" key at top level */
    bool found_components = false;
    while (!found_components) {
        tok = cjson_next(&jp);
        if (tok.type == CJT_RBRACE || tok.type == CJT_EOF) break;
        if (tok.type == CJT_COMMA) continue;
        if (tok.type != CJT_STRING) { cjson_skip_value(&jp); continue; }
        chrome_jtok k = tok;
        tok = cjson_next(&jp);
        if (tok.type != CJT_COLON) break;
        if (cjson_key_eq(&k, "components")) found_components = true;
        else cjson_skip_value(&jp);
    }
    if (!found_components) { free(json); return 0; }

    tok = cjson_next(&jp);
    if (tok.type != CJT_LBRACKET) { free(json); return 0; }

    /* Iterate component array objects — two-pass to handle key order */
    while (true) {
        tok = cjson_next(&jp);
        if (tok.type == CJT_RBRACKET || tok.type == CJT_EOF) break;
        if (tok.type == CJT_COMMA) continue;
        if (tok.type != CJT_LBRACE) { cjson_skip_value(&jp); continue; }

        const char *obj_brace   = tok.start;  /* points to '{' */
        const char *obj_content = jp.pos;      /* points to after '{' */

        /* Pass 1: extract "type" value */
        char comp_type[32] = "";
        {
            chrome_jparse p = { obj_content };
            while (true) {
                tok = cjson_next(&p);
                if (tok.type == CJT_RBRACE || tok.type == CJT_EOF) break;
                if (tok.type == CJT_COMMA) continue;
                if (tok.type != CJT_STRING) { cjson_skip_value(&p); continue; }
                chrome_jtok ck = tok;
                tok = cjson_next(&p);
                if (tok.type != CJT_COLON) break;
                if (cjson_key_eq(&ck, "type")) {
                    tok = cjson_next(&p);
                    cjson_str(&tok, comp_type, sizeof(comp_type));
                    break;
                }
                cjson_skip_value(&p);
            }
        }

        /* Pass 2: if type=="ui", walk the textures array */
        if (strcmp(comp_type, "ui") == 0) {
            chrome_jparse p = { obj_content };
            while (true) {
                tok = cjson_next(&p);
                if (tok.type == CJT_RBRACE || tok.type == CJT_EOF) break;
                if (tok.type == CJT_COMMA) continue;
                if (tok.type != CJT_STRING) { cjson_skip_value(&p); continue; }
                chrome_jtok ck = tok;
                tok = cjson_next(&p);
                if (tok.type != CJT_COLON) break;
                if (!cjson_key_eq(&ck, "textures")) { cjson_skip_value(&p); continue; }
                tok = cjson_next(&p);
                if (tok.type != CJT_LBRACKET) break;
                while (true) {
                    tok = cjson_next(&p);
                    if (tok.type == CJT_RBRACKET || tok.type == CJT_EOF) break;
                    if (tok.type == CJT_COMMA) continue;
                    if (tok.type != CJT_LBRACE) { cjson_skip_value(&p); continue; }
                    char cat_id[64]   = "";
                    char rel_path[256] = "";
                    while (true) {
                        tok = cjson_next(&p);
                        if (tok.type == CJT_RBRACE || tok.type == CJT_EOF) break;
                        if (tok.type == CJT_COMMA) continue;
                        if (tok.type != CJT_STRING) { cjson_skip_value(&p); continue; }
                        chrome_jtok tk = tok;
                        tok = cjson_next(&p);
                        if (tok.type != CJT_COLON) break;
                        tok = cjson_next(&p);
                        if (cjson_key_eq(&tk, "catalog_id"))
                            cjson_str(&tok, cat_id, sizeof(cat_id));
                        else if (cjson_key_eq(&tk, "path"))
                            cjson_str(&tok, rel_path, sizeof(rel_path));
                    }
                    if (cat_id[0] && rel_path[0]) {
                        char full[FS_MAXPATH];
                        snprintf(full, sizeof(full), "%s/%s", mod_dir, rel_path);
                        s_registerModTexture(cat_id, full);
                        count++;
                    }
                }
                break; /* only one textures array per ui component */
            }
        }

        /* Advance main parser past this object */
        {
            chrome_jparse skip = { obj_brace };
            cjson_skip_value(&skip);
            jp.pos = skip.pos;
        }
    }

    free(json);
    return count;
}

void pdguiThemeApplyEnabledModUiTextures(void)
{
    if (!s_ThemeLateInitDone) return;

    s32 n = modmgrGetCount();
    s32 total = 0;
    for (s32 i = 0; i < n; i++) {
        modinfo_t *m = modmgrGetMod(i);
        if (!m || !m->enabled || !m->dirpath[0]) continue;
        s32 added = pdguiThemeScanModUiTextures(m->dirpath);
        if (added > 0) {
            sysLogPrintf(LOG_NOTE,
                "PDGUI theme: mod '%s' provided %d UI texture override(s)",
                m->id, added);
            total += added;
        }
    }
    if (total > 0)
        sysLogPrintf(LOG_NOTE,
            "PDGUI theme: %d UI texture override(s) applied from mods", total);
}

static s32 s_parseFillModeToken(const chrome_jtok *tok)
{
    char mode[32];
    cjson_str(tok, mode, sizeof(mode));
    return strcmp(mode, "tile") == 0 ? NINESLICE_TILE : NINESLICE_STRETCH;
}

static void s_chromeStylesClear(void)
{
    s_ChromeStyleCount = 0;
}

/* S-6: Delete mod-provided chrome style GL textures and drop them from the
 * theme texture cache. The base "base:ui_chrome_frame" texture is owned by
 * pdguiThemeLateInit and must NOT be freed here. */
static void s_chromeStylesFreeModTextures(void)
{
    for (s32 i = 0; i < s_ChromeStyleCount; i++) {
        const char *id = s_ChromeStyles[i].id;
        if (!id || !id[0]) continue;
        if (strcmp(id, "base:ui_chrome_frame") == 0) continue;

        auto it = s_ThemeTexCache.find(id);
        if (it != s_ThemeTexCache.end()) {
            if (it->second) {
                GLuint tex = it->second;
                glDeleteTextures(1, &tex);
            }
            s_ThemeTexCache.erase(it);
        }
    }
}

static bool s_chromeStyleHasId(const char *id)
{
    for (s32 i = 0; i < s_ChromeStyleCount; i++) {
        if (strcmp(s_ChromeStyles[i].id, id) == 0) return true;
    }
    return false;
}

static void s_chromeStyleAdd(const char *id, const char *name)
{
    if (!id || !id[0]) return;
    if (s_ChromeStyleCount >= PDGUI_MAX_CHROME_STYLES) return;
    if (s_chromeStyleHasId(id)) return;

    chrome_style_entry *e = &s_ChromeStyles[s_ChromeStyleCount++];
    snprintf(e->id, sizeof(e->id), "%s", id);
    snprintf(e->name, sizeof(e->name), "%s", (name && name[0]) ? name : id);
}

static s32 s_parseChromeManifest(const char *json,
                                 char *out_name, s32 out_name_len,
                                 char *out_tex_id, s32 out_tex_id_len,
                                 char *out_tex_file, s32 out_tex_file_len,
                                 nineslice_def_t *out_ns);

static s32 s_registerChromeStyleFromModDir(const char *mod_dir,
                                           const char *fallback_name,
                                           s32 allow_existing,
                                           char *out_style_id,
                                           s32 out_style_id_len)
{
    if (out_style_id && out_style_id_len > 0) {
        out_style_id[0] = '\0';
    }
    if (!mod_dir || !mod_dir[0]) return 0;

    char mod_json[FS_MAXPATH];
    snprintf(mod_json, sizeof(mod_json), "%s/mod.json", mod_dir);
    u32 size = 0;
    char *raw = (char *)fsFileLoad(mod_json, &size);
    if (!raw || !size) {
        if (raw) free(raw);
        return 0;
    }

    char *json = (char *)malloc(size + 1);
    if (!json) {
        free(raw);
        return 0;
    }
    memcpy(json, raw, size);
    json[size] = '\0';
    free(raw);

    char style_name[96];
    char tex_id[64];
    char tex_file[256];
    nineslice_def_t ns;
    s32 ok = s_parseChromeManifest(json, style_name, sizeof(style_name),
                                   tex_id, sizeof(tex_id),
                                   tex_file, sizeof(tex_file), &ns);
    if (!ok) {
        free(json);
        return 0;
    }

    if (out_style_id && out_style_id_len > 0) {
        snprintf(out_style_id, out_style_id_len, "%s", tex_id);
    }

    if (s_chromeStyleHasId(tex_id) && !allow_existing) {
        free(json);
        return 0;
    }

    char tex_path[FS_MAXPATH];
    snprintf(tex_path, sizeof(tex_path), "%s/%s", mod_dir, tex_file);
    s_registerModTexture(tex_id, tex_path);
    pdguiNinesliceRegister(tex_id, &ns);
    s_chromeStyleAdd(tex_id, style_name[0] ? style_name :
                              (fallback_name && fallback_name[0]) ? fallback_name : tex_id);
    sysLogPrintf(LOG_NOTE, "UI.CHROME: registered style '%s' (%s)", tex_id, tex_path);

    free(json);
    return 1;
}

static s32 s_parseChromeManifest(const char *json,
                                 char *out_name, s32 out_name_len,
                                 char *out_tex_id, s32 out_tex_id_len,
                                 char *out_tex_file, s32 out_tex_file_len,
                                 nineslice_def_t *out_ns)
{
    if (!json || !out_name || !out_tex_id || !out_tex_file || !out_ns) return 0;

    out_name[0] = '\0';
    out_tex_id[0] = '\0';
    out_tex_file[0] = '\0';
    memset(out_ns, 0, sizeof(*out_ns));
    out_ns->edge_mode = NINESLICE_STRETCH;
    out_ns->center_mode = NINESLICE_STRETCH;
    out_ns->top_mode = NINESLICE_STRETCH;
    out_ns->bottom_mode = NINESLICE_STRETCH;
    out_ns->left_mode = NINESLICE_STRETCH;
    out_ns->right_mode = NINESLICE_STRETCH;

    chrome_jparse jp = { json };
    chrome_jtok tok = cjson_next(&jp);
    if (tok.type != CJT_LBRACE) return 0;

    bool has_nineslice = false;
    bool has_chrome_tag = false;

    while (true) {
        tok = cjson_next(&jp);
        if (tok.type == CJT_RBRACE || tok.type == CJT_EOF) break;
        if (tok.type == CJT_COMMA) continue;
        if (tok.type != CJT_STRING) { cjson_skip_value(&jp); continue; }

        chrome_jtok key = tok;
        tok = cjson_next(&jp);
        if (tok.type != CJT_COLON) break;

        if (cjson_key_eq(&key, "name")) {
            tok = cjson_next(&jp);
            cjson_str(&tok, out_name, out_name_len);
        } else if (cjson_key_eq(&key, "tags")) {
            tok = cjson_next(&jp);
            if (tok.type == CJT_LBRACKET) {
                while (true) {
                    tok = cjson_next(&jp);
                    if (tok.type == CJT_RBRACKET || tok.type == CJT_EOF) break;
                    if (tok.type == CJT_COMMA) continue;
                    if (tok.type != CJT_STRING) { cjson_skip_value(&jp); continue; }
                    char tag[32];
                    cjson_str(&tok, tag, sizeof(tag));
                    if (strcmp(tag, "chrome") == 0) {
                        has_chrome_tag = true;
                    }
                }
            } else {
                cjson_skip_value(&jp);
            }
        } else if (cjson_key_eq(&key, "components")) {
            tok = cjson_next(&jp);
            if (tok.type != CJT_LBRACE) { cjson_skip_value(&jp); continue; }

            while (true) {
                tok = cjson_next(&jp);
                if (tok.type == CJT_RBRACE || tok.type == CJT_EOF) break;
                if (tok.type == CJT_COMMA) continue;
                if (tok.type != CJT_STRING) { cjson_skip_value(&jp); continue; }

                chrome_jtok ckey = tok;
                tok = cjson_next(&jp);
                if (tok.type != CJT_COLON) break;

                if (cjson_key_eq(&ckey, "textures")) {
                    tok = cjson_next(&jp);
                    if (tok.type != CJT_LBRACKET) { cjson_skip_value(&jp); continue; }
                    while (true) {
                        tok = cjson_next(&jp);
                        if (tok.type == CJT_RBRACKET || tok.type == CJT_EOF) break;
                        if (tok.type == CJT_COMMA) continue;
                        if (tok.type != CJT_LBRACE) { cjson_skip_value(&jp); continue; }

                        char tex_id[64] = "";
                        char tex_file[256] = "";
                        while (true) {
                            tok = cjson_next(&jp);
                            if (tok.type == CJT_RBRACE || tok.type == CJT_EOF) break;
                            if (tok.type == CJT_COMMA) continue;
                            if (tok.type != CJT_STRING) { cjson_skip_value(&jp); continue; }
                            chrome_jtok tkey = tok;
                            tok = cjson_next(&jp);
                            if (tok.type != CJT_COLON) break;
                            tok = cjson_next(&jp);
                            if (cjson_key_eq(&tkey, "id")) cjson_str(&tok, tex_id, sizeof(tex_id));
                            else if (cjson_key_eq(&tkey, "file")) cjson_str(&tok, tex_file, sizeof(tex_file));
                        }
                        if (tex_id[0] && tex_file[0] && !out_tex_id[0]) {
                            snprintf(out_tex_id, out_tex_id_len, "%s", tex_id);
                            snprintf(out_tex_file, out_tex_file_len, "%s", tex_file);
                        }
                    }
                } else if (cjson_key_eq(&ckey, "nineslice")) {
                    tok = cjson_next(&jp);
                    if (tok.type != CJT_LBRACKET) { cjson_skip_value(&jp); continue; }
                    while (true) {
                        tok = cjson_next(&jp);
                        if (tok.type == CJT_RBRACKET || tok.type == CJT_EOF) break;
                        if (tok.type == CJT_COMMA) continue;
                        if (tok.type != CJT_LBRACE) { cjson_skip_value(&jp); continue; }

                        char ns_id[64] = "";
                        nineslice_def_t ns = *out_ns;
                        while (true) {
                            tok = cjson_next(&jp);
                            if (tok.type == CJT_RBRACE || tok.type == CJT_EOF) break;
                            if (tok.type == CJT_COMMA) continue;
                            if (tok.type != CJT_STRING) { cjson_skip_value(&jp); continue; }
                            chrome_jtok nkey = tok;
                            tok = cjson_next(&jp);
                            if (tok.type != CJT_COLON) break;

                            if (cjson_key_eq(&nkey, "id")) {
                                tok = cjson_next(&jp);
                                cjson_str(&tok, ns_id, sizeof(ns_id));
                            } else if (cjson_key_eq(&nkey, "src_inset")) {
                                tok = cjson_next(&jp);
                                if (tok.type == CJT_LBRACE) {
                                    while (true) {
                                        tok = cjson_next(&jp);
                                        if (tok.type == CJT_RBRACE || tok.type == CJT_EOF) break;
                                        if (tok.type == CJT_COMMA) continue;
                                        if (tok.type != CJT_STRING) { cjson_skip_value(&jp); continue; }
                                        chrome_jtok skey = tok;
                                        tok = cjson_next(&jp); if (tok.type != CJT_COLON) break;
                                        tok = cjson_next(&jp);
                                        if (cjson_key_eq(&skey, "top")) ns.src_top = cjson_int(&tok, ns.src_top);
                                        else if (cjson_key_eq(&skey, "bottom")) ns.src_bottom = cjson_int(&tok, ns.src_bottom);
                                        else if (cjson_key_eq(&skey, "left")) ns.src_left = cjson_int(&tok, ns.src_left);
                                        else if (cjson_key_eq(&skey, "right")) ns.src_right = cjson_int(&tok, ns.src_right);
                                    }
                                } else {
                                    cjson_skip_value(&jp);
                                }
                            } else if (cjson_key_eq(&nkey, "dst_corner_px")) {
                                tok = cjson_next(&jp);
                                if (tok.type == CJT_LBRACE) {
                                    ns.has_split = 1;
                                    while (true) {
                                        tok = cjson_next(&jp);
                                        if (tok.type == CJT_RBRACE || tok.type == CJT_EOF) break;
                                        if (tok.type == CJT_COMMA) continue;
                                        if (tok.type != CJT_STRING) { cjson_skip_value(&jp); continue; }
                                        chrome_jtok dkey = tok;
                                        tok = cjson_next(&jp); if (tok.type != CJT_COLON) break;
                                        tok = cjson_next(&jp);
                                        if (cjson_key_eq(&dkey, "top")) ns.dst_top = cjson_int(&tok, ns.dst_top);
                                        else if (cjson_key_eq(&dkey, "bottom")) ns.dst_bottom = cjson_int(&tok, ns.dst_bottom);
                                        else if (cjson_key_eq(&dkey, "left")) ns.dst_left = cjson_int(&tok, ns.dst_left);
                                        else if (cjson_key_eq(&dkey, "right")) ns.dst_right = cjson_int(&tok, ns.dst_right);
                                    }
                                } else {
                                    cjson_skip_value(&jp);
                                }
                            } else if (cjson_key_eq(&nkey, "top_mode")) {
                                tok = cjson_next(&jp);
                                ns.top_mode = s_parseFillModeToken(&tok);
                                ns.has_per_edge_mode = 1;
                            } else if (cjson_key_eq(&nkey, "bottom_mode")) {
                                tok = cjson_next(&jp);
                                ns.bottom_mode = s_parseFillModeToken(&tok);
                                ns.has_per_edge_mode = 1;
                            } else if (cjson_key_eq(&nkey, "left_mode")) {
                                tok = cjson_next(&jp);
                                ns.left_mode = s_parseFillModeToken(&tok);
                                ns.has_per_edge_mode = 1;
                            } else if (cjson_key_eq(&nkey, "right_mode")) {
                                tok = cjson_next(&jp);
                                ns.right_mode = s_parseFillModeToken(&tok);
                                ns.has_per_edge_mode = 1;
                            } else if (cjson_key_eq(&nkey, "center_mode")) {
                                tok = cjson_next(&jp);
                                ns.center_mode = s_parseFillModeToken(&tok);
                            } else {
                                cjson_skip_value(&jp);
                            }
                        }

                        if (!out_tex_id[0] && ns_id[0]) {
                            snprintf(out_tex_id, out_tex_id_len, "%s", ns_id);
                        }
                        if (ns_id[0] && out_tex_id[0] && strcmp(ns_id, out_tex_id) != 0) {
                            /* Current chrome draw path expects a shared texture/nineslice ID. */
                            continue;
                        }
                        *out_ns = ns;
                        has_nineslice = true;
                    }
                } else {
                    cjson_skip_value(&jp);
                }
            }
        } else {
            cjson_skip_value(&jp);
        }
    }

    return (has_chrome_tag && out_tex_id[0] && out_tex_file[0] && has_nineslice) ? 1 : 0;
}

/* Scan a single directory for chrome-style mods. Each child directory is
 * attempted as a mod. If registration fails (no mod.json or no chrome
 * components inside), the child is treated as a category folder and its
 * own children are scanned one level deeper. Depth is capped at the single
 * recursion below to avoid walking arbitrary user directory trees. */
static void s_scanChromeStylesInDir(const char *dir_path, int allow_recurse)
{
    if (!dir_path || !dir_path[0]) return;
    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (!ent->d_name || ent->d_name[0] == '.') continue;
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;

        char child[FS_MAXPATH];
        snprintf(child, sizeof(child), "%s/%s", dir_path, ent->d_name);
        struct stat st;
        if (stat(child, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        /* Attempt registration; if the child has no parseable chrome manifest
         * and we're allowed to recurse (depth 0), treat it as a category. */
        int registered = s_registerChromeStyleFromModDir(child, ent->d_name, 0,
                                                         nullptr, 0);
        if (!registered && allow_recurse) {
            s_scanChromeStylesInDir(child, 0);
        }
    }
    closedir(d);
}

static void s_scanModChromeStyles(void)
{
    const char *roots[] = {
        fsFullPath("$E/../mods"),
        "mods",
        fsFullPath("$E/mods"),
        fsFullPath("mods"),
    };

    for (int ri = 0; ri < 4; ri++) {
        s_scanChromeStylesInDir(roots[ri], 1);
    }
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

extern "C" {

/* Config-backed scanline setting (persisted to pd.ini) */
static s32 s_CfgScanlineEnabled = 1;

/* Config-backed UI chrome setting.
 * 0 = Procedural (default, pixel-for-pixel S195 behaviour)
 * 1 = Classic (base-game test chrome — visible via Settings toggle) */
static s32 s_CfgUiChromeEnabled = 0;
static char s_CfgUiChromeStyleId[64] = "base:ui_chrome_frame";

/* Config-backed title-bar style (S297; registered in pdguiThemeInit). */
static s32 s_CfgTitleBarStyle = 0;  /* PDGUI_TITLEBAR_CLASSIC */

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

    /* Register scanline config in pd.ini */
    configRegisterInt("Video.Scanlines", &s_CfgScanlineEnabled, 0, 1);
    configRegisterFloat("Video.ScanlineAlpha", &s_ScanlineAlpha, 0.0f, 1.0f);
    s_ScanlineEnabled = (s_CfgScanlineEnabled != 0);

    /* Register UI chrome config.  The actual toggle applies later, after
     * pdguiChromeInitializeBaseMod has registered the nineslice. */
    configRegisterInt("Video.UiChromeEnabled", &s_CfgUiChromeEnabled, 0, 1);
    configRegisterString("Video.UiChromeStyleId", s_CfgUiChromeStyleId,
                         sizeof(s_CfgUiChromeStyleId));

    /* Register title-bar procedural style (S297). Clamped on getter/setter. */
    configRegisterInt("Video.UiTitleBarStyle", &s_CfgTitleBarStyle,
                      0, PDGUI_TITLEBAR_STYLE_COUNT - 1);

    sysLogPrintf(LOG_NOTE,
        "PDGUI theme: D5.0 early init (scanlines=%s alpha=%.0f%% chrome=%s, textures deferred)",
        s_ScanlineEnabled ? "ON" : "OFF", s_ScanlineAlpha * 100.0f,
        s_CfgUiChromeEnabled ? "ON" : "OFF");
}

/* Accessor used by pdguiChromeInitializeBaseMod to apply the config-loaded
 * chrome setting once the nineslice registration has run. */
static s32 s_getCfgUiChromeEnabled(void)
{
    return s_CfgUiChromeEnabled;
}

/* Mutator used by renderSettingsVideo to persist user selection. */
void pdguiThemeSetUiChromeEnabled(s32 enabled)
{
    s_CfgUiChromeEnabled = enabled ? 1 : 0;
}

s32 pdguiThemeGetUiChromeEnabled(void)
{
    return s_CfgUiChromeEnabled;
}

void pdguiThemeSetUiChromeStyleId(const char *catalog_id)
{
    if (!catalog_id || !catalog_id[0]) {
        snprintf(s_CfgUiChromeStyleId, sizeof(s_CfgUiChromeStyleId),
                 "%s", "base:ui_chrome_frame");
        return;
    }
    snprintf(s_CfgUiChromeStyleId, sizeof(s_CfgUiChromeStyleId), "%s", catalog_id);
}

const char *pdguiThemeGetUiChromeStyleId(void)
{
    return s_CfgUiChromeStyleId;
}

s32 pdguiThemeGetChromeStyleCount(void)
{
    return s_ChromeStyleCount;
}

const char *pdguiThemeGetChromeStyleId(s32 index)
{
    if (index < 0 || index >= s_ChromeStyleCount) return "";
    return s_ChromeStyles[index].id;
}

const char *pdguiThemeGetChromeStyleName(s32 index)
{
    if (index < 0 || index >= s_ChromeStyleCount) return "";
    return s_ChromeStyles[index].name;
}

s32 pdguiThemeRegisterChromeModDir(const char *mod_dir, s32 activate_now)
{
    char style_id[64];
    if (!s_registerChromeStyleFromModDir(mod_dir, nullptr, 1,
                                         style_id, sizeof(style_id))) {
        return 0;
    }

    if (activate_now) {
        pdguiThemeSetUiChromeStyleId(style_id);
        pdguiThemeSetUiChromeEnabled(1);
        pdguiSetPanelNineSlice(style_id);
        pdguiChromeSetEnabled(1);
        configSave("pd.ini");
        sysLogPrintf(LOG_NOTE,
            "UI.CHROME: activated newly-registered style '%s'", style_id);
    }

    return 1;
}

void pdguiThemeRescanChromeStyles(void)
{
    const char *active_id = pdguiThemeGetUiChromeStyleId();
    /* S-6: free mod-provided GL textures before clearing so they don't leak
     * across repeated rescans. Base chrome texture is preserved. */
    s_chromeStylesFreeModTextures();
    s_chromeStylesClear();
    s_chromeStyleAdd("base:ui_chrome_frame", "Classic (base-game)");
    s_scanModChromeStyles();

    if (active_id && active_id[0] && s_chromeStyleHasId(active_id)) {
        pdguiSetPanelNineSlice(active_id);
    } else {
        pdguiThemeSetUiChromeStyleId("base:ui_chrome_frame");
        pdguiSetPanelNineSlice("base:ui_chrome_frame");
    }
}

/* =========================================================================
 * Content inset API (S297)
 *
 * Runtime answer to "how many pixels do I need to pull content in from the
 * outer dialog rect to avoid overlapping the border artwork?".  Drives
 * menus that paint inside a PD dialog so inputs/lists/images don't clip
 * into the frame.
 *
 * When chrome is on and an active nineslice resolves, returns the dst_*
 * corner pixels from that def (what the frame actually draws).  When
 * chrome is off, returns the procedural-border fallback (2 px) so a
 * single code path works for both modes.
 * ========================================================================= */

void pdguiThemeGetContentInset(float *out_l, float *out_r,
                               float *out_t, float *out_b)
{
    float l = 2.0f, r = 2.0f, t = 2.0f, b = 2.0f;

    if (s_CfgUiChromeEnabled && s_CfgUiChromeStyleId[0]) {
        const nineslice_def_t *def = pdguiNinesliceGet(s_CfgUiChromeStyleId);
        void *tex = pdguiThemeGetTexture(s_CfgUiChromeStyleId);
        if (def && tex) {
            /* dst_* is the render-side corner size in screen pixels. */
            l = (float)def->dst_left;
            r = (float)def->dst_right;
            t = (float)def->dst_top;
            b = (float)def->dst_bottom;

            /* Guarantee a small safety margin so widgets never butt up
             * directly against the inner edge of the frame. */
            if (l < 2.0f) l = 2.0f;
            if (r < 2.0f) r = 2.0f;
            if (t < 2.0f) t = 2.0f;
            if (b < 2.0f) b = 2.0f;
        }
    }

    if (out_l) *out_l = l;
    if (out_r) *out_r = r;
    if (out_t) *out_t = t;
    if (out_b) *out_b = b;
}

void pdguiThemeApplyContentInset(float *x, float *y, float *w, float *h)
{
    if (!x || !y || !w || !h) return;
    float l, r, t, b;
    pdguiThemeGetContentInset(&l, &r, &t, &b);
    *x += l;
    *y += t;
    *w -= (l + r);
    *h -= (t + b);
    if (*w < 0.0f) *w = 0.0f;
    if (*h < 0.0f) *h = 0.0f;
}

/* Resolve padding that clears the nineslice chrome border plus 8px breathe.
 * Returns max(base, inset + breathe) on each edge; UI-scaled internally. */
void pdguiThemeResolveContentPad(float title_h,
                                 float base_l, float base_t,
                                 float base_r, float base_b,
                                 float *out_l, float *out_t,
                                 float *out_r, float *out_b)
{
    float insL = 0.0f, insR = 0.0f, insT = 0.0f, insB = 0.0f;
    pdguiThemeGetContentInset(&insL, &insR, &insT, &insB);
    float breathe = pdguiScale(8.0f);
    float padL = base_l;
    float padT = title_h + base_t;
    float padR = base_r;
    float padB = base_b;
    if (insL + breathe > padL) padL = insL + breathe;
    if (title_h + insT + breathe > padT) padT = title_h + insT + breathe;
    if (insR + breathe > padR) padR = insR + breathe;
    if (insB + breathe > padB) padB = insB + breathe;
    if (out_l) *out_l = padL;
    if (out_t) *out_t = padT;
    if (out_r) *out_r = padR;
    if (out_b) *out_b = padB;
}

/* Shorthand: set ImGui cursor below title bar, clear of chrome border. */
#include "imgui/imgui.h"
void pdguiSetCursorBelowTitle(float title_h)
{
    float padL, padT, padR, padB;
    float baseX = ImGui::GetStyle().WindowPadding.x;
    float baseY = ImGui::GetStyle().WindowPadding.y;
    pdguiThemeResolveContentPad(title_h, baseX, baseY, baseX, baseY,
                                &padL, &padT, &padR, &padB);
    (void)padR; (void)padB;
    ImGui::SetCursorPos(ImVec2(padL, padT));
}

/* =========================================================================
 * Title-bar style (S297)
 *
 * Persisted via `Video.UiTitleBarStyle`.  Classic gradient (0) is the PD
 * default; a handful of simple procedural variants let users pick a look
 * without shipping a chrome mod.
 * ========================================================================= */

/* (s_CfgTitleBarStyle declared at file scope earlier; registered in pdguiThemeInit.) */

void pdguiThemeSetTitleBarStyle(s32 style)
{
    if (style < 0 || style >= PDGUI_TITLEBAR_STYLE_COUNT) {
        style = PDGUI_TITLEBAR_CLASSIC;
    }
    s_CfgTitleBarStyle = style;
}

s32 pdguiThemeGetTitleBarStyle(void)
{
    if (s_CfgTitleBarStyle < 0 || s_CfgTitleBarStyle >= PDGUI_TITLEBAR_STYLE_COUNT) {
        s_CfgTitleBarStyle = PDGUI_TITLEBAR_CLASSIC;
    }
    return s_CfgTitleBarStyle;
}

const char *pdguiThemeGetTitleBarStyleName(s32 style)
{
    switch (style) {
        case PDGUI_TITLEBAR_CLASSIC:      return "Classic (gradient)";
        case PDGUI_TITLEBAR_SOLID:        return "Solid";
        case PDGUI_TITLEBAR_VERT_BARS:    return "Vertical Bars";
        case PDGUI_TITLEBAR_SCANLINES:    return "Scanlines";
        case PDGUI_TITLEBAR_DIAG_STRIPES: return "Diagonal Stripes";
        default:                          return "Classic (gradient)";
    }
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
            s_ThemeTexDims[k_UiTextures[i].catalog_id]  = { w, h };
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

    /* D5 Phase 4: apply UI texture overrides from enabled mods */
    pdguiThemeApplyEnabledModUiTextures();

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

/* Look up the source texture dimensions (width, height) in pixels for a
 * cached theme texture.  Used by the chrome renderer to compute UVs.
 * Returns 1 on success (out_w/out_h populated), 0 if the texture is not
 * cached or the catalog_id is unknown. */
s32 pdguiThemeGetTextureSize(const char *catalog_id, u32 *out_w, u32 *out_h)
{
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (!catalog_id || !catalog_id[0]) return 0;
    if (!s_ThemeLateInitDone) return 0;

    auto it = s_ThemeTexDims.find(catalog_id);
    if (it == s_ThemeTexDims.end()) return 0;

    if (out_w) *out_w = it->second.first;
    if (out_h) *out_h = it->second.second;
    return 1;
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
 * Horizontal scanlines at 2px intervals for a retro-CRT overlay.
 * alpha=1.0 → max darkening ~55% (140/255 per line).
 * ========================================================================= */

/* S309: scanline overlay sizes itself against the display resolution so
 * the gap-between-lines stays visually consistent from 720p to 4K.
 * Reference is the same 720p baseline that pdguiScale() uses — at 720p
 * we draw a 1px line every 2px; at 4K we draw a 3px line every 6px.
 * Returns { line_thickness, line_stride } in UI pixels (both >= 1). */
static void pdguiResolveScanlineMetrics(float *out_thick, float *out_stride)
{
    float scale = pdguiScaleFactor();
    if (scale < 0.5f) scale = 0.5f;
    if (scale > 8.0f) scale = 8.0f;

    float thick  = scale;
    float stride = scale * 2.0f;
    if (thick  < 1.0f) thick  = 1.0f;
    if (stride < thick + 1.0f) stride = thick + 1.0f;

    if (out_thick)  *out_thick  = thick;
    if (out_stride) *out_stride = stride;
}

void pdguiThemeDrawScanline(float x, float y, float w, float h, float alpha)
{
    if (alpha <= 0.0f) {
        return;
    }

    ImDrawList *dl = ImGui::GetWindowDrawList();

    /* Scale alpha into visible range: 0..1 → 0..140 (≈55% max darkening).
     * Previous cap of 40 was nearly invisible even at full alpha. */
    uint8_t a   = (uint8_t)(alpha * 140.0f);
    ImU32   col = IM_COL32(0, 0, 0, a);

    float thick, stride;
    pdguiResolveScanlineMetrics(&thick, &stride);
    for (float ry = y; ry < y + h; ry += stride) {
        dl->AddLine(ImVec2(x, ry), ImVec2(x + w, ry), col, thick);
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
    uint8_t a = (uint8_t)(s_ScanlineAlpha * 140.0f);
    ImU32 col = IM_COL32(0, 0, 0, a);

    float thick, stride;
    pdguiResolveScanlineMetrics(&thick, &stride);
    for (float ry = y; ry < y + h; ry += stride) {
        fg->AddLine(ImVec2(x, ry), ImVec2(x + w, ry), col, thick);
    }
}

/* =========================================================================
 * ROM Texture Extraction Tool
 *
 * Extracts UI textures from ROM via texLoadFromConfig(), decodes to RGBA32,
 * and writes as uncompressed TGA files to mods/base-ui/textures/.
 * Also writes PNG files (minimal uncompressed PNG, no zlib dependency)
 * and default 9-slice JSON definitions for panel/button textures.
 *
 * Called with --extract-ui-textures CLI flag, after texReset() has populated
 * g_TexGeneralConfigs and loaded the ROM texture data into memory.
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * Minimal PNG writer (unfiltered, uncompressed DEFLATE stored blocks)
 *
 * Produces valid PNG files without requiring zlib. Uses DEFLATE stored
 * blocks (non-compressed) which are slightly larger but always correct.
 * Suitable for small UI textures (max 256x256).
 * ------------------------------------------------------------------------- */

static uint32_t s_crc32Table[256];
static bool s_crc32Init = false;

static void s_initCrc32(void)
{
    if (s_crc32Init) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        s_crc32Table[i] = c;
    }
    s_crc32Init = true;
}

static uint32_t s_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++)
        crc = s_crc32Table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static void s_writeBE32(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v >> 24);
    dst[1] = (uint8_t)(v >> 16);
    dst[2] = (uint8_t)(v >> 8);
    dst[3] = (uint8_t)(v);
}

static void s_writeLE16(uint8_t *dst, uint16_t v)
{
    dst[0] = (uint8_t)(v);
    dst[1] = (uint8_t)(v >> 8);
}

static bool s_writePng(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h)
{
    s_initCrc32();

    FILE *f = fsFileOpenWrite(path);
    if (!f) {
        sysLogPrintf(LOG_ERROR, "PDGUI extract: cannot write PNG '%s'", path);
        return false;
    }

    /* PNG signature */
    static const uint8_t sig[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    fwrite(sig, 1, 8, f);

    /* IHDR chunk */
    {
        uint8_t ihdr[25]; /* 4 type + 13 data + 4 crc (length written separately) */
        uint8_t len_buf[4];
        s_writeBE32(len_buf, 13);
        fwrite(len_buf, 1, 4, f);

        memcpy(ihdr, "IHDR", 4);
        s_writeBE32(ihdr + 4, w);
        s_writeBE32(ihdr + 8, h);
        ihdr[12] = 8;  /* bit depth */
        ihdr[13] = 6;  /* color type: RGBA */
        ihdr[14] = 0;  /* compression */
        ihdr[15] = 0;  /* filter */
        ihdr[16] = 0;  /* interlace */

        fwrite(ihdr, 1, 17, f);
        uint32_t crc = s_crc32(ihdr, 17);
        uint8_t crc_buf[4];
        s_writeBE32(crc_buf, crc);
        fwrite(crc_buf, 1, 4, f);
    }

    /* IDAT chunk: uncompressed DEFLATE (stored blocks)
     *
     * Each scanline: filter byte (0 = None) + w*4 RGBA bytes
     * Raw data size: h * (1 + w*4)
     * DEFLATE stored blocks: max 65535 bytes each
     * zlib wrapper: 2 bytes header + raw blocks + 4 bytes adler32
     */
    {
        uint32_t row_bytes = 1 + w * 4;  /* filter byte + pixel data */
        uint32_t raw_size = h * row_bytes;

        /* Build the unfiltered image data */
        uint8_t *raw = (uint8_t *)malloc(raw_size);
        if (!raw) { fclose(f); return false; }

        for (uint32_t y = 0; y < h; y++) {
            raw[y * row_bytes] = 0;  /* filter: None */
            memcpy(raw + y * row_bytes + 1, rgba + y * w * 4, w * 4);
        }

        /* Calculate Adler-32 of raw data */
        uint32_t a1 = 1, a2 = 0;
        for (uint32_t i = 0; i < raw_size; i++) {
            a1 = (a1 + raw[i]) % 65521;
            a2 = (a2 + a1) % 65521;
        }
        uint32_t adler = (a2 << 16) | a1;

        /* Count DEFLATE stored blocks needed */
        uint32_t num_blocks = (raw_size + 65534) / 65535;
        /* zlib: 2 header + sum of (5 + min(65535, remaining)) per block + 4 adler */
        uint32_t zlib_size = 2 + raw_size + num_blocks * 5 + 4;

        uint8_t *zlib = (uint8_t *)malloc(zlib_size);
        if (!zlib) { free(raw); fclose(f); return false; }

        uint32_t zp = 0;
        zlib[zp++] = 0x78; /* CMF: deflate, window 32K */
        zlib[zp++] = 0x01; /* FLG: no dict, check bits */

        uint32_t remaining = raw_size;
        uint32_t src_off = 0;
        while (remaining > 0) {
            uint32_t block_len = remaining > 65535 ? 65535 : remaining;
            uint8_t is_final = (remaining <= 65535) ? 1 : 0;

            zlib[zp++] = is_final;  /* BFINAL + BTYPE=00 (stored) */
            s_writeLE16(zlib + zp, (uint16_t)block_len); zp += 2;
            s_writeLE16(zlib + zp, (uint16_t)(~block_len)); zp += 2;

            memcpy(zlib + zp, raw + src_off, block_len);
            zp += block_len;
            src_off += block_len;
            remaining -= block_len;
        }

        /* Adler-32 (big-endian) */
        s_writeBE32(zlib + zp, adler);
        zp += 4;

        free(raw);

        /* Write IDAT chunk */
        uint8_t len_buf[4];
        s_writeBE32(len_buf, zp);
        fwrite(len_buf, 1, 4, f);

        /* Type + data for CRC */
        uint8_t *idat_for_crc = (uint8_t *)malloc(4 + zp);
        memcpy(idat_for_crc, "IDAT", 4);
        memcpy(idat_for_crc + 4, zlib, zp);
        fwrite(idat_for_crc, 1, 4 + zp, f);

        uint32_t crc = s_crc32(idat_for_crc, 4 + zp);
        uint8_t crc_buf[4];
        s_writeBE32(crc_buf, crc);
        fwrite(crc_buf, 1, 4, f);

        free(idat_for_crc);
        free(zlib);
    }

    /* IEND chunk */
    {
        uint8_t iend[12];
        s_writeBE32(iend, 0); /* length = 0 */
        memcpy(iend + 4, "IEND", 4);
        uint32_t crc = s_crc32(iend + 4, 4);
        s_writeBE32(iend + 8, crc);
        fwrite(iend, 1, 12, f);
    }

    fclose(f);
    return true;
}

/**
 * Write a default 9-slice JSON definition for a texture.
 * Uses 25% insets as a reasonable starting point for panel textures.
 */
static bool s_writeNinesliceJson(const char *path, uint32_t w, uint32_t h)
{
    FILE *f = fsFileOpenWrite(path);
    if (!f) return false;

    /* Default insets: ~25% from each edge, minimum 2px */
    int l = (int)(w * 0.25f); if (l < 2) l = 2;
    int r = l;
    int t = (int)(h * 0.25f); if (t < 2) t = 2;
    int b = t;

    fprintf(f,
        "{\n"
        "    \"left\": %d,\n"
        "    \"right\": %d,\n"
        "    \"top\": %d,\n"
        "    \"bottom\": %d,\n"
        "    \"edgeMode\": \"stretch\",\n"
        "    \"centerMode\": \"stretch\"\n"
        "}\n",
        l, r, t, b);

    fclose(f);
    return true;
}

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

    /* Load the textures we want to extract */
    static const int k_IndicesToLoad[] = {
        0, 1, 2, 3, 4, 6, 7, 10, 11, 34, 35, 36, 37, 38
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

        /* s_decodeAndUpload allocates a private decode buffer and frees it
         * on return, so we re-decode into our own s_ExtractBuf for the
         * TGA writer below. */
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

        /* Also write PNG for mod portability */
        char png_path[256];
        snprintf(png_path, sizeof(png_path), "mods/base-ui/textures/%s.png", k_Extracts[i].filename);
        if (s_writePng(png_path, s_ExtractBuf, w, h)) {
            sysLogPrintf(LOG_NOTE,
                "PDGUI extract: [%d] '%s' → %s (PNG)", idx, k_Extracts[i].filename, png_path);
        }

        /* Write default 9-slice definition for panel-sized textures */
        if (w >= 16 && h >= 16) {
            char ns_path[256];
            snprintf(ns_path, sizeof(ns_path), "mods/base-ui/textures/%s.9slice.json",
                     k_Extracts[i].filename);
            if (s_writeNinesliceJson(ns_path, w, h)) {
                sysLogPrintf(LOG_NOTE,
                    "PDGUI extract: [%d] '%s' → %s (9-slice def)",
                    idx, k_Extracts[i].filename, ns_path);
            }
        }
    }

    sysLogPrintf(LOG_NOTE,
        "PDGUI extract: ROM pass done — %u textures extracted to mods/base-ui/textures/",
        extracted);

    /* Second pass: generate procedural fallbacks for any TGAs that ROM
     * extraction didn't produce.  This handles edge cases like textures
     * whose ROM configs have zero dimensions, are in unsupported formats,
     * or whose texLoadFromConfig() call didn't fully decompress the data. */
    static const struct {
        const char *filename;     /* same as k_Extracts[].filename */
        const char *proc_name;    /* procedural generator name */
        uint32_t    w, h;         /* procedural dimensions */
    } k_Fallbacks[] = {
        { "ui_noise_sm",    "noise_sm", 16, 16 },
        { "ui_particles",   "solid",     1,  1 },
        { "ui_noise_lg",    "noise_lg", 16, 16 },
        { "ui_grad_bar",    "solid",     2,  8 },
        { "ui_mirror_tile", "solid",     8,  8 },
        { "ui_bg_haze",     "haze",     64, 64 },
        { "ui_dot_tile",    "solid",     8,  8 },
        { "ui_nuke",        "noise_lg", 64, 64 },
        { "ui_bg_alt",      "noise_lg", 64, 64 },
        { "ui_icon_a",      "solid",    14, 14 },
        { "ui_icon_b",      "solid",    11, 11 },
        { "ui_icon_c",      "solid",    14, 14 },
        { "ui_deco",        "solid",    32, 32 },
        { "ui_stars",       "solid",    16, 16 },
    };

    unsigned fallback_count = 0;
    for (unsigned i = 0; i < sizeof(k_Fallbacks) / sizeof(k_Fallbacks[0]); i++) {
        char path[256];
        snprintf(path, sizeof(path), "mods/base-ui/textures/%s.tga",
                 k_Fallbacks[i].filename);

        /* Check if TGA was already written by the ROM extraction pass */
        u32 probe_sz = 0;
        void *probe = fsFileLoad(path, &probe_sz);
        if (probe && probe_sz > 0) {
            free(probe);
            continue;  /* Already exists — skip */
        }
        if (probe) free(probe);

        /* Generate procedural fallback */
        uint32_t fw = k_Fallbacks[i].w;
        uint32_t fh = k_Fallbacks[i].h;
        GLuint fb_tex = s_generateProceduralTexture(
            k_Fallbacks[i].proc_name, fw, fh);
        if (!fb_tex) {
            sysLogPrintf(LOG_WARNING,
                "PDGUI extract: fallback generation failed for '%s'",
                k_Fallbacks[i].filename);
            continue;
        }

        /* Read back the GL texture pixels and write TGA */
        uint8_t *fb_pixels = (uint8_t *)malloc(fw * fh * 4);
        if (fb_pixels) {
            glBindTexture(GL_TEXTURE_2D, fb_tex);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, fb_pixels);
            if (s_writeTga(path, fb_pixels, fw, fh)) {
                sysLogPrintf(LOG_NOTE,
                    "PDGUI extract: fallback '%s' %ux%u (%s) → %s",
                    k_Fallbacks[i].filename, fw, fh,
                    k_Fallbacks[i].proc_name, path);
                fallback_count++;
            }
            free(fb_pixels);
        }
        glDeleteTextures(1, &fb_tex);
    }

    if (fallback_count > 0) {
        sysLogPrintf(LOG_NOTE,
            "PDGUI extract: generated %u procedural fallback TGA(s) for "
            "textures ROM extraction couldn't produce", fallback_count);
    }
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
 * List of all expected base-ui TGA filenames (must stay in sync with
 * k_UiTextures[] in pdguiThemeLateInit and k_Extracts[] in
 * pdguiThemeExtractRomTextures).
 */
static const char *k_ExpectedBaseUiTgas[] = {
    "mods/base-ui/textures/ui_bg_haze.tga",
    "mods/base-ui/textures/ui_particles.tga",
    "mods/base-ui/textures/ui_noise_sm.tga",
    "mods/base-ui/textures/ui_noise_lg.tga",
    "mods/base-ui/textures/ui_grad_bar.tga",
    "mods/base-ui/textures/ui_mirror_tile.tga",
    "mods/base-ui/textures/ui_dot_tile.tga",
    "mods/base-ui/textures/ui_nuke.tga",
    "mods/base-ui/textures/ui_bg_alt.tga",
    "mods/base-ui/textures/ui_deco.tga",
    "mods/base-ui/textures/ui_icon_a.tga",
    "mods/base-ui/textures/ui_icon_b.tga",
    "mods/base-ui/textures/ui_icon_c.tga",
};
static const unsigned k_NumExpectedBaseUiTgas =
    sizeof(k_ExpectedBaseUiTgas) / sizeof(k_ExpectedBaseUiTgas[0]);

/**
 * Check if ALL base-ui mod textures exist.  Returns true only if every
 * expected TGA file is present and non-empty.
 */
static bool s_baseUiTexturesExist(void)
{
    for (unsigned i = 0; i < k_NumExpectedBaseUiTgas; i++) {
        u32 sz = 0;
        void *probe = fsFileLoad(k_ExpectedBaseUiTgas[i], &sz);
        if (!probe || sz == 0) {
            if (probe) free(probe);
            return false;
        }
        free(probe);
    }
    return true;
}

/**
 * Check which individual TGA files are missing and return a count.
 * Fills `missing_mask` bitfield (bit i set = file i missing).
 * max 64 files tracked (more than enough for our 13).
 */
static unsigned s_countMissingBaseUiTgas(uint64_t *missing_mask)
{
    uint64_t mask = 0;
    unsigned count = 0;
    for (unsigned i = 0; i < k_NumExpectedBaseUiTgas && i < 64; i++) {
        u32 sz = 0;
        void *probe = fsFileLoad(k_ExpectedBaseUiTgas[i], &sz);
        if (!probe || sz == 0) {
            mask |= (1ull << i);
            count++;
        }
        if (probe) free(probe);
    }
    if (missing_mask) *missing_mask = mask;
    return count;
}

/* =========================================================================
 * Base-game UI Chrome template mod (S196)
 *
 * Generates a hand-authored test chrome mod at mods/base-game/ui-chrome/
 * containing a composite nineslice source texture, a template-flagged
 * mod.json, and a README warning users not to edit.  The test chrome is
 * a 64x64 greyscale+alpha composite with all 9 nineslice regions baked
 * into a single texture:
 *
 *     (0..16, 0..16)   TL quarter-circle arc ring
 *     (48..64, 0..16)  TR quarter-circle arc ring
 *     (0..16, 48..64)  BL quarter-circle arc ring
 *     (48..64, 48..64) BR quarter-circle arc ring
 *     (16..48, 0..16)  top edge strip
 *     (16..48, 48..64) bottom edge strip
 *     (0..16, 16..48)  left edge strip
 *     (48..64, 16..48) right edge strip
 *     (16..48, 16..48) center crosshatch (faint)
 *
 * Greyscale+alpha: each pixel is B=G=R=intensity with a separate alpha,
 * letting the theme tint recolour the whole frame.  The 16-pixel insets
 * carve the frame into the standard 9 regions.
 *
 * Init flow:
 *   1. mkdir mods/base-game/ui-chrome
 *   2. If .tga missing: generate pixel buffer, write uncompressed 32bpp TGA
 *   3. Always write mod.json (template:true) and README.md (idempotent)
 *   4. Load the texture into the theme cache as "base:ui_chrome_frame"
 *   5. Register the nineslice under the same catalog id
 *
 * After init the Settings Video "UI Chrome Style" dropdown can flip
 * pdguiChromeSetEnabled(true) + pdguiSetPanelNineSlice("base:ui_chrome_frame")
 * and pdguiDrawPdDialog will draw the nineslice instead of the procedural
 * body.  Toggle off to return to procedural rendering.
 * ========================================================================= */

/* Write an uncompressed 32-bit top-down TGA file from a BGRA pixel buffer. */
static bool s_writeTgaFile(const char *path, uint32_t w, uint32_t h,
                            const uint8_t *bgra)
{
    FILE *f = fsFileOpenWrite(path);
    if (!f) {
        sysLogPrintf(LOG_WARNING,
            "UI.CHROME: could not open '%s' for write", path);
        return false;
    }

    uint8_t hdr[18] = {0};
    hdr[2]  = 2;                             /* uncompressed truecolour      */
    hdr[12] = (uint8_t)(w & 0xff);
    hdr[13] = (uint8_t)((w >> 8) & 0xff);
    hdr[14] = (uint8_t)(h & 0xff);
    hdr[15] = (uint8_t)((h >> 8) & 0xff);
    hdr[16] = 32;                            /* 32bpp                        */
    hdr[17] = 0x28;                          /* top-down, 8 bits alpha       */

    fwrite(hdr, 1, 18, f);
    fwrite(bgra, 1, (size_t)w * (size_t)h * 4u, f);
    fclose(f);

    sysLogPrintf(LOG_NOTE,
        "UI.CHROME: wrote %ux%u TGA '%s' (%zu bytes)",
        w, h, path, (size_t)(18u + (size_t)w * (size_t)h * 4u));
    return true;
}

/* Generate the 64x64 composite chrome frame texture as a BGRA buffer.
 *
 * PD-authentic look: metallic blue-cyan gradient border with bevel highlight
 * on outer edge, dark navy body interior, and subtle inner glow.  Matches the
 * PD N64 dialog chrome aesthetic (dialog_border1 / dialog_border2 colours from
 * the Blue palette with a specular highlight gradient).
 *
 * TODO(D5.CHROME): Replace with real ROM texture extraction once the specific
 * ROM addresses for PD's dialog chrome source art are identified.  The OG
 * chrome is assembled by menugfxRenderDialogBackground() compositing palette
 * colors + textured strips — there is no single source texture.  This
 * procedural version faithfully reconstructs that look.
 *
 * Nine-slice layout (16px insets):
 *   Corners (0-16, 0-16 etc): rounded metallic bevel
 *   Edges: metallic gradient strip with specular highlight
 *   Center: semi-transparent dark navy with subtle noise
 */
static void s_generateChromeFrameBgra(uint8_t *out)
{
    const int W = 64;
    const int H = 64;
    const int inset = 16;       /* corner+edge border thickness (9-slice) */
    const int borderW = 3;      /* outer border pixel width */
    const int glowW   = 5;      /* inner glow gradient width */

    /* PD Blue palette colors (RGBA order, matching k_PalBlue) */
    const float border1_r = 0.00f, border1_g = 0.376f, border1_b = 0.749f; /* #0060BF */
    const float border2_r = 0.00f, border2_g = 0.941f, border2_b = 1.00f;  /* #00F0FF cyan */
    const float body_r    = 0.00f, body_g    = 0.00f,  body_b    = 0.184f;  /* #00002F navy */
    const float hilite_r  = 0.56f, hilite_g  = 1.00f,  hilite_b  = 1.00f;  /* #8FFFFF specular */

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int idx = (y * W + x) * 4;
            float r = 0, g = 0, b = 0, a = 0;

            /* Distance from each edge */
            int dLeft   = x;
            int dRight  = W - 1 - x;
            int dTop    = y;
            int dBottom = H - 1 - y;
            int dMinH   = dLeft < dRight  ? dLeft : dRight;   /* min horiz */
            int dMinV   = dTop  < dBottom ? dTop  : dBottom;   /* min vert */
            int dEdge   = dMinH < dMinV   ? dMinH : dMinV;     /* min from any edge */

            bool inCornerZone = (dMinH < inset && dMinV < inset);
            bool inEdgeZone   = (dEdge < inset);

            if (inCornerZone) {
                /* --- Corner region: rounded bevel --- */
                /* Center of curvature for this corner */
                int cx = (x < inset) ? inset - 1 : (W - inset);
                int cy = (y < inset) ? inset - 1 : (H - inset);
                int dx = x - cx;
                int dy = y - cy;
                float dist = sqrtf((float)(dx * dx + dy * dy));
                float maxR = (float)(inset - 1);

                if (dist > maxR + 1.0f) {
                    /* Outside the corner arc — fully transparent */
                    a = 0;
                } else if (dist > maxR - (float)borderW) {
                    /* Outer border ring: bright cyan with specular gradient */
                    float ring_t = (maxR - dist) / (float)borderW;
                    if (ring_t < 0) ring_t = 0;
                    if (ring_t > 1) ring_t = 1;
                    /* Blend border2 (cyan) → hilite at outer edge */
                    float spec = 1.0f - ring_t;  /* specular at outermost */
                    r = border2_r + (hilite_r - border2_r) * spec * 0.6f;
                    g = border2_g + (hilite_g - border2_g) * spec * 0.6f;
                    b = border2_b + (hilite_b - border2_b) * spec * 0.6f;
                    /* Anti-alias outer edge */
                    float edgeFade = (maxR + 1.0f - dist);
                    if (edgeFade > 1.0f) edgeFade = 1.0f;
                    a = 0.86f * edgeFade;
                } else if (dist > maxR - (float)(borderW + glowW)) {
                    /* Inner glow: gradient from border1 (blue) → body (navy) */
                    float glow_t = (maxR - (float)borderW - dist) / (float)glowW;
                    if (glow_t < 0) glow_t = 0;
                    if (glow_t > 1) glow_t = 1;
                    r = border1_r * (1.0f - glow_t) + body_r * glow_t;
                    g = border1_g * (1.0f - glow_t) + body_g * glow_t;
                    b = border1_b * (1.0f - glow_t) + body_b * glow_t;
                    a = 0.62f * (1.0f - glow_t * 0.3f);
                } else {
                    /* Interior: dark navy body */
                    r = body_r; g = body_g; b = body_b;
                    a = 0.62f;
                }
            } else if (inEdgeZone) {
                /* --- Edge strip: metallic gradient border --- */
                if (dEdge < borderW) {
                    /* Outer border: bright cyan → specular highlight */
                    float t = (float)dEdge / (float)borderW;
                    /* Top/left edges get highlight, bottom/right get darker */
                    float spec = 0;
                    if (dTop < borderW || dLeft < borderW) {
                        spec = (1.0f - t) * 0.7f;  /* bright highlight */
                    } else {
                        spec = (1.0f - t) * 0.3f;  /* subtle highlight */
                    }
                    r = border2_r + (hilite_r - border2_r) * spec;
                    g = border2_g + (hilite_g - border2_g) * spec;
                    b = border2_b + (hilite_b - border2_b) * spec;
                    a = 0.86f;
                } else if (dEdge < borderW + glowW) {
                    /* Inner glow: border1 (medium blue) → body (navy) */
                    float glow_t = (float)(dEdge - borderW) / (float)glowW;
                    r = border1_r * (1.0f - glow_t) + body_r * glow_t;
                    g = border1_g * (1.0f - glow_t) + body_g * glow_t;
                    b = border1_b * (1.0f - glow_t) + body_b * glow_t;
                    a = 0.62f * (1.0f - glow_t * 0.3f);
                } else {
                    /* Rest of edge region: dark navy body */
                    r = body_r; g = body_g; b = body_b;
                    a = 0.62f;
                }
            } else {
                /* --- Center region: semi-transparent dark navy + subtle noise --- */
                /* XorShift noise for subtle texture (deterministic per pixel) */
                uint32_t seed = (uint32_t)(x * 7919 + y * 6271 + 0xA5A5A5A5u);
                seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                float noise = (float)((seed >> 16) & 0xFF) / 255.0f;
                float nv = 0.02f * (noise - 0.5f);  /* +/- 1% intensity variation */
                r = body_r + nv;
                g = body_g + nv;
                b = body_b + nv;
                a = 0.62f;
            }

            /* Clamp and write BGRA */
            if (r < 0) r = 0; if (r > 1) r = 1;
            if (g < 0) g = 0; if (g > 1) g = 1;
            if (b < 0) b = 0; if (b > 1) b = 1;
            if (a < 0) a = 0; if (a > 1) a = 1;
            out[idx + 0] = (uint8_t)(b * 255.0f);  /* B */
            out[idx + 1] = (uint8_t)(g * 255.0f);  /* G */
            out[idx + 2] = (uint8_t)(r * 255.0f);  /* R */
            out[idx + 3] = (uint8_t)(a * 255.0f);  /* A */
        }
    }
}

/**
 * Initialize the base-game UI chrome template mod.  Creates the directory
 * tree at mods/base-game/ui-chrome/, generates a composite frame TGA
 * programmatically, writes the mod.json manifest + README.md, loads the
 * texture into the theme cache, and registers the nineslice definition.
 *
 * Idempotent: safe to call multiple times.  File generation runs only if
 * the .tga is missing.  mod.json + README always get rewritten so schema
 * drift is impossible and users who tampered with either file see their
 * edits reverted on next launch (the template protection policy).
 */
void pdguiChromeInitializeBaseMod(void)
{
    static bool s_done = false;
    if (s_done) return;

    s_chromeStylesClear();

    /* Ensure the directory tree exists. */
    fsCreateDir("mods");
    fsCreateDir("mods/base-game");
    fsCreateDir("mods/base-game/ui-chrome");

    /* Generate + write the composite chrome frame TGA.
     * Always regenerate: the template mod is owned by the game engine and
     * gets rewritten on launch (template protection policy).  This ensures
     * users always get the latest procedural chrome even after upgrades. */
    {
        sysLogPrintf(LOG_NOTE,
            "UI.CHROME: base-game chrome missing — generating PD-authentic chrome frame");
        uint8_t pixels[64 * 64 * 4];
        s_generateChromeFrameBgra(pixels);
        s_writeTgaFile("mods/base-game/ui-chrome/ui_chrome_frame.tga",
                       64, 64, pixels);
    }

    /* mod.json — always overwrite to enforce the template:true contract. */
    {
        static const char k_ChromeModJson[] =
            "{\n"
            "    \"id\": \"base.ui-chrome\",\n"
            "    \"name\": \"Base Game UI Chrome\",\n"
            "    \"version\": \"1.1.0\",\n"
            "    \"description\": \"PD-authentic metallic blue chrome frame with beveled borders. Generated procedurally from PD palette colors.\",\n"
            "    \"author\": \"PD2 Team\",\n"
            "    \"tags\": [\"base-game\", \"template\", \"chrome\"],\n"
            "    \"template\": true,\n"
            "    \"bundled\": true,\n"
            "    \"enabled\": true,\n"
            "    \"components\": {\n"
            "        \"textures\": [\n"
            "            { \"id\": \"base:ui_chrome_frame\", \"file\": \"ui_chrome_frame.tga\" }\n"
            "        ],\n"
            "        \"nineslice\": [\n"
            "            {\n"
            "                \"id\": \"base:ui_chrome_frame\",\n"
            "                \"texture\": \"base:ui_chrome_frame\",\n"
            "                \"src_inset\": { \"top\": 16, \"bottom\": 16, \"left\": 16, \"right\": 16 },\n"
            "                \"dst_corner_px\": { \"top\": 16, \"bottom\": 16, \"left\": 16, \"right\": 16 },\n"
            "                \"top_mode\": \"stretch\",\n"
            "                \"bottom_mode\": \"stretch\",\n"
            "                \"left_mode\": \"stretch\",\n"
            "                \"right_mode\": \"stretch\",\n"
            "                \"center_mode\": \"tile\"\n"
            "            }\n"
            "        ]\n"
            "    }\n"
            "}\n";
        FILE *jf = fsFileOpenWrite("mods/base-game/ui-chrome/mod.json");
        if (jf) {
            fwrite(k_ChromeModJson, 1, sizeof(k_ChromeModJson) - 1, jf);
            fclose(jf);
            sysLogPrintf(LOG_NOTE,
                "UI.CHROME: wrote mods/base-game/ui-chrome/mod.json");
        } else {
            sysLogPrintf(LOG_WARNING,
                "UI.CHROME: could not write mods/base-game/ui-chrome/mod.json");
        }
    }

    /* README — always overwrite so the template warning stays authoritative. */
    {
        static const char k_ChromeReadme[] =
            "# Base Game UI Chrome -- Base-Game Template Mod\n"
            "\n"
            "This is a **base-game template mod**. Its contents are generated by the game\n"
            "at startup and will be regenerated if the file set is incomplete or missing.\n"
            "\n"
            "## Do not edit this mod directly.\n"
            "\n"
            "Any changes you make to the files in this directory will be silently\n"
            "overwritten the next time the game launches and validates its base-game\n"
            "mod set. This protection exists to keep a stable reference copy that\n"
            "other mods can build on top of.\n"
            "\n"
            "## To customize this mod\n"
            "\n"
            "Open the Mod Manager (Main Menu -> Mods -> Modding Hub) and use **Save As**\n"
            "to create a user mod from this template. Your user mod will live in\n"
            "`mods/user/<your-name>/` and is yours to edit freely.\n"
            "\n"
            "## Why this exists\n"
            "\n"
            "The base-game template system lets the game ship a default look and\n"
            "feel that mods can descend from. The template is the canonical source;\n"
            "user mods are editable copies. Keeping the two separated prevents\n"
            "accidental overwrites of the baseline when the extractor runs again.\n";
        FILE *rf = fsFileOpenWrite("mods/base-game/ui-chrome/README.md");
        if (rf) {
            fwrite(k_ChromeReadme, 1, sizeof(k_ChromeReadme) - 1, rf);
            fclose(rf);
            sysLogPrintf(LOG_NOTE,
                "UI.CHROME: wrote mods/base-game/ui-chrome/README.md");
        }
    }

    /* Load the chrome texture into the theme cache (registers it in the
     * asset catalog and s_ThemeTexDims so the chrome render branch can
     * resolve it by catalog id). */
    s_registerModTexture("base:ui_chrome_frame",
                         "mods/base-game/ui-chrome/ui_chrome_frame.tga");

    /* Register the nineslice definition under the same catalog id as the
     * texture.  The render path uses this id for both lookups. */
    nineslice_def_t nsdef;
    memset(&nsdef, 0, sizeof(nsdef));
    nsdef.src_top    = 16;
    nsdef.src_bottom = 16;
    nsdef.src_left   = 16;
    nsdef.src_right  = 16;
    nsdef.dst_top    = 16;
    nsdef.dst_bottom = 16;
    nsdef.dst_left   = 16;
    nsdef.dst_right  = 16;
    nsdef.has_split  = 1;
    nsdef.top_mode    = NINESLICE_STRETCH;
    nsdef.bottom_mode = NINESLICE_STRETCH;
    nsdef.left_mode   = NINESLICE_STRETCH;
    nsdef.right_mode  = NINESLICE_STRETCH;
    nsdef.center_mode = NINESLICE_TILE;
    nsdef.has_per_edge_mode = 1;
    pdguiNinesliceRegister("base:ui_chrome_frame", &nsdef);
    s_chromeStyleAdd("base:ui_chrome_frame", "Classic (base-game)");

    /* Discover additional chrome mods that follow the extracted template
     * schema (components.textures + components.nineslice in mod.json). */
    s_scanModChromeStyles();

    /* Apply persisted chrome enable state from pd.ini now that the
     * nineslice + texture are registered and resolvable. */
    const char *style_id = s_CfgUiChromeStyleId[0]
        ? s_CfgUiChromeStyleId
        : "base:ui_chrome_frame";
    if (!s_chromeStyleHasId(style_id)) {
        style_id = "base:ui_chrome_frame";
        pdguiThemeSetUiChromeStyleId(style_id);
    }

    if (s_getCfgUiChromeEnabled()) {
        pdguiSetPanelNineSlice(style_id);
        pdguiChromeSetEnabled(1);
        sysLogPrintf(LOG_NOTE,
            "UI.CHROME: auto-activated on startup (Video.UiChromeEnabled=1, style=%s)",
            style_id);
    } else {
        /* Ensure the active chrome id is set even when disabled, so flipping
         * the toggle to ON later doesn't require re-selecting a mod. */
        pdguiSetPanelNineSlice(style_id);
    }

    sysLogPrintf(LOG_NOTE,
        "UI.CHROME: base-game chrome template mod initialized");
    s_done = true;
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

    /* Auto-extract: if ANY base-ui textures are missing, run extraction from
     * ROM data.  This handles both first-launch (all missing) and partial
     * extraction (e.g. ui_particles.tga missing while others exist).
     * The extraction pipeline writes all TGAs, then we reload the theme. */
    {
        uint64_t missing_mask = 0;
        unsigned n_missing = s_countMissingBaseUiTgas(&missing_mask);

        if (n_missing > 0) {
            sysLogPrintf(LOG_NOTE,
                "PDGUI theme: %u of %u base-ui TGAs missing (mask=0x%llx) — "
                "auto-extracting from ROM",
                n_missing, k_NumExpectedBaseUiTgas,
                (unsigned long long)missing_mask);

            /* Log which specific files are missing */
            for (unsigned i = 0; i < k_NumExpectedBaseUiTgas && i < 64; i++) {
                if (missing_mask & (1ull << i)) {
                    sysLogPrintf(LOG_NOTE, "  MISSING: %s", k_ExpectedBaseUiTgas[i]);
                }
            }

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
                    "        \"scanline_alpha\": 0.5,\n"
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

            /* Extract ROM textures to TGA files (writes ALL textures, not
             * just missing ones — idempotent overwrites are fine) */
            pdguiThemeExtractRomTextures();

            /* Verify extraction and report per-file results */
            uint64_t still_missing = 0;
            unsigned n_still = s_countMissingBaseUiTgas(&still_missing);
            if (n_still == 0) {
                sysLogPrintf(LOG_NOTE,
                    "PDGUI theme: extraction verified — all %u TGAs present, "
                    "reloading theme textures", k_NumExpectedBaseUiTgas);
            } else {
                sysLogPrintf(LOG_WARNING,
                    "PDGUI theme: extraction ran but %u TGA(s) still missing "
                    "(mask=0x%llx) — check ROM data and file permissions",
                    n_still, (unsigned long long)still_missing);
                for (unsigned i = 0; i < k_NumExpectedBaseUiTgas && i < 64; i++) {
                    if (still_missing & (1ull << i)) {
                        sysLogPrintf(LOG_WARNING,
                            "  STILL MISSING: %s", k_ExpectedBaseUiTgas[i]);
                    }
                }
            }

            /* Reload theme textures now that TGA files exist (or have been
             * updated).  Reset the late-init flag so pdguiThemeLateInit()
             * re-runs and picks up the newly extracted files. */
            s_ThemeLateInitDone = false;
            pdguiThemeLateInit();
        }
    }

    /* CLI flags for manual re-extract / modern UI generation */
    if (sysArgCheck("--extract-ui-textures")) {
        pdguiThemeExtractRomTextures();
    }

    if (sysArgCheck("--generate-modern-ui")) {
        s_generateModernUiTextures();
    }

    /* Initialize the base-game chrome template mod on first frame.  This
     * generates the test chrome assets, registers the texture + nineslice,
     * and makes "base:ui_chrome_frame" available for the Settings toggle. */
    pdguiChromeInitializeBaseMod();
}

} /* extern "C" */
