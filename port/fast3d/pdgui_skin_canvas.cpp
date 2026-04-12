/**
 * pdgui_skin_canvas.cpp -- Layer-based canvas system for the skin editor.
 *
 * In-memory RGBA32 pixel buffer stack with per-layer blend modes, opacity,
 * visibility.  Composites bottom-to-top and uploads to a GL texture each
 * frame when dirty.  The GL texture is used for both the 2D ImGui canvas
 * display and the live 3D preview override.
 *
 * Design doc: context/designs/skin-editor-design.md §3.2
 * Batch S-1: Core canvas, Normal blend, GL upload
 * Batch S-3: Undo/redo ring buffer
 * Batch S-7: Extended blend modes (Multiply, Screen, Hue, etc.)
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include "glad/glad.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/ultratypes.h>

#include "pdgui_skin_editor.h"
#include "system.h"

/* ========================================================================
 * Layer struct
 * ======================================================================== */

struct SkinLayer {
    u8  *pixels;            /* RGBA32 pixel data (w * h * 4) */
    char name[32];
    f32  opacity;           /* 0.0-1.0 */
    s32  blend_mode;        /* SKIN_BLEND_* */
    s32  visible;
    s32  locked;
};

/* ========================================================================
 * Canvas state
 * ======================================================================== */

static SkinLayer s_Layers[SKIN_MAX_LAYERS];
static s32  s_NumLayers   = 0;
static s32  s_ActiveLayer = 0;
static s32  s_Width       = 0;
static s32  s_Height      = 0;
static u8  *s_Composite   = NULL;   /* Flattened RGBA32 result */
static u32  s_GlTexture   = 0;
static s32  s_Dirty       = 0;
static s32  s_Initialized = 0;

/* ========================================================================
 * Undo/Redo ring buffer (S-3)
 * ======================================================================== */

#define UNDO_MAX_SLOTS 32

struct UndoSlot {
    u8 *pixels;      /* snapshot of active layer pixels at time of push */
    s32 layer_idx;   /* which layer this snapshot belongs to */
    s32 used;
};

static UndoSlot s_UndoStack[UNDO_MAX_SLOTS];
static s32 s_UndoHead  = 0;  /* next write position */
static s32 s_UndoCount = 0;  /* valid undo entries behind head */
static s32 s_RedoCount = 0;  /* valid redo entries ahead of head */

static void undoClearAll(void)
{
    for (s32 i = 0; i < UNDO_MAX_SLOTS; i++) {
        if (s_UndoStack[i].pixels) {
            free(s_UndoStack[i].pixels);
            s_UndoStack[i].pixels = NULL;
        }
        s_UndoStack[i].used = 0;
    }
    s_UndoHead  = 0;
    s_UndoCount = 0;
    s_RedoCount = 0;
}

/* ========================================================================
 * Compositing
 * ======================================================================== */

static inline u8 clamp_u8(s32 v)
{
    return (u8)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static void compositeAllLayers(void)
{
    if (!s_Composite || s_NumLayers == 0) return;

    const s32 totalPixels = s_Width * s_Height;

    /* Start with transparent black */
    memset(s_Composite, 0, (size_t)totalPixels * 4);

    for (s32 li = 0; li < s_NumLayers; li++) {
        SkinLayer *layer = &s_Layers[li];
        if (!layer->visible || !layer->pixels) continue;
        if (layer->opacity <= 0.0f) continue;

        const f32 layerOpacity = layer->opacity;
        const u8 *src = layer->pixels;
        u8 *dst = s_Composite;

        for (s32 i = 0; i < totalPixels; i++) {
            const u8 sr = src[0], sg = src[1], sb = src[2], sa = src[3];
            src += 4;

            if (sa == 0) { dst += 4; continue; }

            const f32 srcAlpha = (sa / 255.0f) * layerOpacity;
            const u8 dr = dst[0], dg = dst[1], db = dst[2], da = dst[3];

            /* Blend modes (S-7: full set) */
            u8 br = sr, bg = sg, bb = sb;

            switch (layer->blend_mode) {
            case SKIN_BLEND_NORMAL:
                break;
            case SKIN_BLEND_MULTIPLY:
                br = (u8)((dr * sr) / 255);
                bg = (u8)((dg * sg) / 255);
                bb = (u8)((db * sb) / 255);
                break;
            case SKIN_BLEND_SCREEN:
                br = (u8)(255 - ((255 - dr) * (255 - sr)) / 255);
                bg = (u8)(255 - ((255 - dg) * (255 - sg)) / 255);
                bb = (u8)(255 - ((255 - db) * (255 - sb)) / 255);
                break;
            case SKIN_BLEND_HUE: {
                /* RGB->HSL, take H from src, S+L from backdrop, HSL->RGB */
                f32 dr_f = dr / 255.0f, dg_f = dg / 255.0f, db_f = db / 255.0f;
                f32 sr_f = sr / 255.0f, sg_f = sg / 255.0f, sb_f = sb / 255.0f;

                /* Backdrop HSL */
                f32 dmax = dr_f > dg_f ? (dr_f > db_f ? dr_f : db_f) : (dg_f > db_f ? dg_f : db_f);
                f32 dmin = dr_f < dg_f ? (dr_f < db_f ? dr_f : db_f) : (dg_f < db_f ? dg_f : db_f);
                f32 d_l = (dmax + dmin) * 0.5f;
                f32 d_s = 0.0f;
                if (dmax != dmin) d_s = d_l < 0.5f ? (dmax - dmin) / (dmax + dmin)
                                                    : (dmax - dmin) / (2.0f - dmax - dmin);

                /* Source hue */
                f32 smax = sr_f > sg_f ? (sr_f > sb_f ? sr_f : sb_f) : (sg_f > sb_f ? sg_f : sb_f);
                f32 smin = sr_f < sg_f ? (sr_f < sb_f ? sr_f : sb_f) : (sg_f < sb_f ? sg_f : sb_f);
                f32 s_h = 0.0f;
                if (smax != smin) {
                    f32 sd = smax - smin;
                    if (smax == sr_f) s_h = fmodf((sg_f - sb_f) / sd, 6.0f);
                    else if (smax == sg_f) s_h = (sb_f - sr_f) / sd + 2.0f;
                    else s_h = (sr_f - sg_f) / sd + 4.0f;
                    s_h /= 6.0f;
                    if (s_h < 0.0f) s_h += 1.0f;
                }

                /* HSL->RGB with src hue, backdrop S+L */
                f32 h = s_h, s = d_s, l = d_l;
                f32 c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
                f32 x = c * (1.0f - fabsf(fmodf(h * 6.0f, 2.0f) - 1.0f));
                f32 m = l - c * 0.5f;
                f32 rf, gf, bf;
                s32 hi = (s32)(h * 6.0f) % 6;
                switch (hi) {
                case 0: rf = c; gf = x; bf = 0; break;
                case 1: rf = x; gf = c; bf = 0; break;
                case 2: rf = 0; gf = c; bf = x; break;
                case 3: rf = 0; gf = x; bf = c; break;
                case 4: rf = x; gf = 0; bf = c; break;
                default: rf = c; gf = 0; bf = x; break;
                }
                br = clamp_u8((s32)((rf + m) * 255.0f));
                bg = clamp_u8((s32)((gf + m) * 255.0f));
                bb = clamp_u8((s32)((bf + m) * 255.0f));
            } break;
            case SKIN_BLEND_BURN:
                br = clamp_u8(255 - ((255 - dr) * 255) / (sr + 1));
                bg = clamp_u8(255 - ((255 - dg) * 255) / (sg + 1));
                bb = clamp_u8(255 - ((255 - db) * 255) / (sb + 1));
                break;
            case SKIN_BLEND_SATURATION: {
                /* Take saturation from src, hue+luminosity from backdrop */
                f32 dr_f = dr / 255.0f, dg_f = dg / 255.0f, db_f = db / 255.0f;
                f32 sr_f = sr / 255.0f, sg_f = sg / 255.0f, sb_f = sb / 255.0f;

                /* Backdrop H+L */
                f32 dmax = dr_f > dg_f ? (dr_f > db_f ? dr_f : db_f) : (dg_f > db_f ? dg_f : db_f);
                f32 dmin = dr_f < dg_f ? (dr_f < db_f ? dr_f : db_f) : (dg_f < db_f ? dg_f : db_f);
                f32 d_l = (dmax + dmin) * 0.5f;
                f32 d_h = 0.0f;
                if (dmax != dmin) {
                    f32 dd = dmax - dmin;
                    if (dmax == dr_f) d_h = fmodf((dg_f - db_f) / dd, 6.0f);
                    else if (dmax == dg_f) d_h = (db_f - dr_f) / dd + 2.0f;
                    else d_h = (dr_f - dg_f) / dd + 4.0f;
                    d_h /= 6.0f;
                    if (d_h < 0.0f) d_h += 1.0f;
                }

                /* Source saturation */
                f32 smax = sr_f > sg_f ? (sr_f > sb_f ? sr_f : sb_f) : (sg_f > sb_f ? sg_f : sb_f);
                f32 smin = sr_f < sg_f ? (sr_f < sb_f ? sr_f : sb_f) : (sg_f < sb_f ? sg_f : sb_f);
                f32 s_l = (smax + smin) * 0.5f;
                f32 s_s = 0.0f;
                if (smax != smin) s_s = s_l < 0.5f ? (smax - smin) / (smax + smin)
                                                    : (smax - smin) / (2.0f - smax - smin);

                /* HSL->RGB with backdrop H+L, src S */
                f32 h = d_h, s = s_s, l = d_l;
                f32 c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
                f32 x = c * (1.0f - fabsf(fmodf(h * 6.0f, 2.0f) - 1.0f));
                f32 m = l - c * 0.5f;
                f32 rf, gf, bf;
                s32 hi = (s32)(h * 6.0f) % 6;
                switch (hi) {
                case 0: rf = c; gf = x; bf = 0; break;
                case 1: rf = x; gf = c; bf = 0; break;
                case 2: rf = 0; gf = c; bf = x; break;
                case 3: rf = 0; gf = x; bf = c; break;
                case 4: rf = x; gf = 0; bf = c; break;
                default: rf = c; gf = 0; bf = x; break;
                }
                br = clamp_u8((s32)((rf + m) * 255.0f));
                bg = clamp_u8((s32)((gf + m) * 255.0f));
                bb = clamp_u8((s32)((bf + m) * 255.0f));
            } break;
            default:
                break;
            }

            /* Alpha composite */
            const f32 invA = 1.0f - srcAlpha;
            dst[0] = clamp_u8((s32)(dr * invA + br * srcAlpha));
            dst[1] = clamp_u8((s32)(dg * invA + bg * srcAlpha));
            dst[2] = clamp_u8((s32)(db * invA + bb * srcAlpha));
            dst[3] = clamp_u8((s32)(da * invA + sa * srcAlpha) +
                              (s32)(da * srcAlpha));
            /* Simplified alpha: dst_a = src_a + dst_a * (1 - src_a) */
            {
                f32 da_f = da / 255.0f;
                f32 out_a = srcAlpha + da_f * invA;
                if (out_a > 1.0f) out_a = 1.0f;
                dst[3] = (u8)(out_a * 255.0f);
            }
            dst += 4;
        }
    }
}

/* ========================================================================
 * GL texture management
 * ======================================================================== */

static void glTextureCreate(void)
{
    if (s_GlTexture != 0) return;

    glGenTextures(1, &s_GlTexture);
    glBindTexture(GL_TEXTURE_2D, s_GlTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 s_Width, s_Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_Composite);
    glBindTexture(GL_TEXTURE_2D, 0);

    sysLogPrintf(LOG_NOTE, "skin_canvas: GL texture %u created (%dx%d)",
                 s_GlTexture, s_Width, s_Height);
}

static void glTextureUpload(void)
{
    if (s_GlTexture == 0) return;

    glBindTexture(GL_TEXTURE_2D, s_GlTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    s_Width, s_Height, GL_RGBA, GL_UNSIGNED_BYTE, s_Composite);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void glTextureDestroy(void)
{
    if (s_GlTexture != 0) {
        glDeleteTextures(1, &s_GlTexture);
        s_GlTexture = 0;
    }
}

/* ========================================================================
 * Public API — Canvas lifecycle
 * ======================================================================== */

extern "C" {

s32 skinCanvasCreate(s32 width, s32 height)
{
    if (s_Initialized) {
        skinCanvasDestroy();
    }

    if (width < 1 || width > SKIN_MAX_WIDTH ||
        height < 1 || height > SKIN_MAX_HEIGHT) {
        return -1;
    }

    s_Width  = width;
    s_Height = height;

    const size_t bufSize = (size_t)width * height * 4;

    /* Allocate composite buffer */
    s_Composite = (u8 *)calloc(1, bufSize);
    if (!s_Composite) return -1;

    /* Create initial layer (white background) */
    memset(s_Layers, 0, sizeof(s_Layers));
    s_Layers[0].pixels = (u8 *)calloc(1, bufSize);
    if (!s_Layers[0].pixels) {
        free(s_Composite); s_Composite = NULL;
        return -1;
    }

    /* Fill base layer with white */
    memset(s_Layers[0].pixels, 255, bufSize);

    snprintf(s_Layers[0].name, sizeof(s_Layers[0].name), "Background");
    s_Layers[0].opacity    = 1.0f;
    s_Layers[0].blend_mode = SKIN_BLEND_NORMAL;
    s_Layers[0].visible    = 1;
    s_Layers[0].locked     = 1;

    s_NumLayers   = 1;
    s_ActiveLayer = 0;
    s_Dirty       = 1;
    s_Initialized = 1;

    undoClearAll();

    sysLogPrintf(LOG_NOTE, "skin_canvas: created %dx%d, 1 layer", width, height);
    return 0;
}

void skinCanvasDestroy(void)
{
    for (s32 i = 0; i < SKIN_MAX_LAYERS; i++) {
        if (s_Layers[i].pixels) {
            free(s_Layers[i].pixels);
            s_Layers[i].pixels = NULL;
        }
    }

    if (s_Composite) { free(s_Composite); s_Composite = NULL; }

    glTextureDestroy();
    undoClearAll();

    s_NumLayers   = 0;
    s_ActiveLayer = 0;
    s_Width       = 0;
    s_Height      = 0;
    s_Dirty       = 0;
    s_Initialized = 0;
}

s32  skinCanvasGetWidth(void)  { return s_Width; }
s32  skinCanvasGetHeight(void) { return s_Height; }
s32  skinCanvasGetNumLayers(void) { return s_NumLayers; }

s32  skinCanvasGetActiveLayer(void) { return s_ActiveLayer; }
void skinCanvasSetActiveLayer(s32 idx) {
    if (idx >= 0 && idx < s_NumLayers) s_ActiveLayer = idx;
}

/* ========================================================================
 * Layer management
 * ======================================================================== */

s32 skinCanvasAddLayer(void)
{
    if (s_NumLayers >= SKIN_MAX_LAYERS) return -1;

    const size_t bufSize = (size_t)s_Width * s_Height * 4;
    s32 idx = s_NumLayers;

    s_Layers[idx].pixels = (u8 *)calloc(1, bufSize);
    if (!s_Layers[idx].pixels) return -1;

    snprintf(s_Layers[idx].name, sizeof(s_Layers[idx].name),
             "Layer %d", idx);
    s_Layers[idx].opacity    = 1.0f;
    s_Layers[idx].blend_mode = SKIN_BLEND_NORMAL;
    s_Layers[idx].visible    = 1;
    s_Layers[idx].locked     = 0;

    s_NumLayers++;
    s_ActiveLayer = idx;
    s_Dirty = 1;

    return idx;
}

s32 skinCanvasRemoveLayer(s32 idx)
{
    if (s_NumLayers <= 1) return -1;
    if (idx < 0 || idx >= s_NumLayers) return -1;

    free(s_Layers[idx].pixels);

    /* Shift layers down */
    for (s32 i = idx; i < s_NumLayers - 1; i++) {
        s_Layers[i] = s_Layers[i + 1];
    }
    memset(&s_Layers[s_NumLayers - 1], 0, sizeof(SkinLayer));
    s_NumLayers--;

    if (s_ActiveLayer >= s_NumLayers) {
        s_ActiveLayer = s_NumLayers - 1;
    }

    s_Dirty = 1;
    return 0;
}

s32  skinCanvasGetLayerVisible(s32 idx)  { return (idx >= 0 && idx < s_NumLayers) ? s_Layers[idx].visible : 0; }
void skinCanvasSetLayerVisible(s32 idx, s32 visible) {
    if (idx >= 0 && idx < s_NumLayers) { s_Layers[idx].visible = visible; s_Dirty = 1; }
}

f32  skinCanvasGetLayerOpacity(s32 idx)  { return (idx >= 0 && idx < s_NumLayers) ? s_Layers[idx].opacity : 0.0f; }
void skinCanvasSetLayerOpacity(s32 idx, f32 opacity) {
    if (idx >= 0 && idx < s_NumLayers) {
        if (opacity < 0.0f) opacity = 0.0f;
        if (opacity > 1.0f) opacity = 1.0f;
        s_Layers[idx].opacity = opacity; s_Dirty = 1;
    }
}

s32  skinCanvasGetLayerBlendMode(s32 idx) { return (idx >= 0 && idx < s_NumLayers) ? s_Layers[idx].blend_mode : 0; }
void skinCanvasSetLayerBlendMode(s32 idx, s32 mode) {
    if (idx >= 0 && idx < s_NumLayers && mode >= 0 && mode < SKIN_BLEND_COUNT) {
        s_Layers[idx].blend_mode = mode; s_Dirty = 1;
    }
}

const char *skinCanvasGetLayerName(s32 idx) {
    if (idx >= 0 && idx < s_NumLayers) return s_Layers[idx].name;
    return "";
}

/* ========================================================================
 * Pixel access
 * ======================================================================== */

void skinCanvasSetPixel(s32 x, s32 y, u8 r, u8 g, u8 b, u8 a)
{
    if (!s_Initialized) return;
    if (x < 0 || x >= s_Width || y < 0 || y >= s_Height) return;
    if (s_ActiveLayer < 0 || s_ActiveLayer >= s_NumLayers) return;

    SkinLayer *layer = &s_Layers[s_ActiveLayer];
    if (!layer->pixels || layer->locked) return;

    const s32 off = (y * s_Width + x) * 4;
    layer->pixels[off + 0] = r;
    layer->pixels[off + 1] = g;
    layer->pixels[off + 2] = b;
    layer->pixels[off + 3] = a;
    s_Dirty = 1;
}

void skinCanvasGetPixel(s32 x, s32 y, u8 *r, u8 *g, u8 *b, u8 *a)
{
    if (!s_Initialized || x < 0 || x >= s_Width || y < 0 || y >= s_Height) {
        if (r) *r = 0; if (g) *g = 0; if (b) *b = 0; if (a) *a = 0;
        return;
    }
    if (s_ActiveLayer < 0 || s_ActiveLayer >= s_NumLayers) return;

    const SkinLayer *layer = &s_Layers[s_ActiveLayer];
    if (!layer->pixels) return;

    const s32 off = (y * s_Width + x) * 4;
    if (r) *r = layer->pixels[off + 0];
    if (g) *g = layer->pixels[off + 1];
    if (b) *b = layer->pixels[off + 2];
    if (a) *a = layer->pixels[off + 3];
}

void skinCanvasGetCompositePixel(s32 x, s32 y, u8 *r, u8 *g, u8 *b, u8 *a)
{
    if (!s_Composite || x < 0 || x >= s_Width || y < 0 || y >= s_Height) {
        if (r) *r = 0; if (g) *g = 0; if (b) *b = 0; if (a) *a = 0;
        return;
    }
    const s32 off = (y * s_Width + x) * 4;
    if (r) *r = s_Composite[off + 0];
    if (g) *g = s_Composite[off + 1];
    if (b) *b = s_Composite[off + 2];
    if (a) *a = s_Composite[off + 3];
}

void skinCanvasMarkDirty(void) { s_Dirty = 1; }

void skinCanvasUpdate(void)
{
    if (!s_Initialized || !s_Dirty) return;

    compositeAllLayers();

    if (s_GlTexture == 0) {
        glTextureCreate();
    } else {
        glTextureUpload();
    }

    s_Dirty = 0;
}

u32  skinCanvasGetGlTexture(void)       { return s_GlTexture; }
u8  *skinCanvasGetActivePixels(void) {
    if (s_ActiveLayer >= 0 && s_ActiveLayer < s_NumLayers)
        return s_Layers[s_ActiveLayer].pixels;
    return NULL;
}
const u8 *skinCanvasGetCompositePixels(void) { return s_Composite; }

/* ========================================================================
 * Undo / Redo (S-3)
 * ======================================================================== */

void skinCanvasUndoPush(void)
{
    if (!s_Initialized) return;
    if (s_ActiveLayer < 0 || s_ActiveLayer >= s_NumLayers) return;

    const size_t bufSize = (size_t)s_Width * s_Height * 4;
    UndoSlot *slot = &s_UndoStack[s_UndoHead];

    if (!slot->pixels) {
        slot->pixels = (u8 *)malloc(bufSize);
        if (!slot->pixels) return;
    }

    memcpy(slot->pixels, s_Layers[s_ActiveLayer].pixels, bufSize);
    slot->layer_idx = s_ActiveLayer;
    slot->used = 1;

    s_UndoHead = (s_UndoHead + 1) % UNDO_MAX_SLOTS;
    if (s_UndoCount < UNDO_MAX_SLOTS) s_UndoCount++;

    /* New action invalidates redo history */
    s_RedoCount = 0;
}

s32 skinCanvasUndo(void)
{
    if (!s_Initialized || s_UndoCount <= 0) return -1;

    const size_t bufSize = (size_t)s_Width * s_Height * 4;

    /* Move head back to the last pushed slot */
    s_UndoHead = (s_UndoHead - 1 + UNDO_MAX_SLOTS) % UNDO_MAX_SLOTS;
    UndoSlot *slot = &s_UndoStack[s_UndoHead];
    if (!slot->used || !slot->pixels) return -1;

    s32 li = slot->layer_idx;
    if (li < 0 || li >= s_NumLayers || !s_Layers[li].pixels) return -1;

    /* Save current state to redo before overwriting */
    /* We reuse a temporary swap: store current into slot, copy slot's saved into layer */
    u8 *tmp = (u8 *)malloc(bufSize);
    if (!tmp) return -1;

    memcpy(tmp, s_Layers[li].pixels, bufSize);
    memcpy(s_Layers[li].pixels, slot->pixels, bufSize);
    memcpy(slot->pixels, tmp, bufSize);
    free(tmp);

    s_UndoCount--;
    s_RedoCount++;
    s_Dirty = 1;
    return 0;
}

s32 skinCanvasRedo(void)
{
    if (!s_Initialized || s_RedoCount <= 0) return -1;

    const size_t bufSize = (size_t)s_Width * s_Height * 4;

    UndoSlot *slot = &s_UndoStack[s_UndoHead];
    if (!slot->used || !slot->pixels) return -1;

    s32 li = slot->layer_idx;
    if (li < 0 || li >= s_NumLayers || !s_Layers[li].pixels) return -1;

    /* Swap current layer with redo slot */
    u8 *tmp = (u8 *)malloc(bufSize);
    if (!tmp) return -1;

    memcpy(tmp, s_Layers[li].pixels, bufSize);
    memcpy(s_Layers[li].pixels, slot->pixels, bufSize);
    memcpy(slot->pixels, tmp, bufSize);
    free(tmp);

    s_UndoHead = (s_UndoHead + 1) % UNDO_MAX_SLOTS;
    s_RedoCount--;
    s_UndoCount++;
    s_Dirty = 1;
    return 0;
}

} /* extern "C" */
