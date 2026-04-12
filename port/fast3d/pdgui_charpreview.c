/**
 * pdgui_charpreview.c -- Character model preview for ImGui menus.
 *
 * Renders a 3D character model (head + body) to an offscreen framebuffer
 * during the GBI render phase. The resulting texture can be displayed
 * in ImGui menus via ImGui::Image.
 *
 * The rendering hooks into menuRenderDialog: when a dialog is replaced
 * by ImGui (hotswap), but a character preview is requested, the menu
 * model is still rendered — but to our preview FBO instead of the screen.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.c in CMakeLists.txt.
 */

/* glad must come before any other OpenGL headers. */
#include "glad/glad.h"

/* PR/gbi.h must come before gfx_api.h because gfx_api.h uses the Gfx typedef.
 * gfx_api.h must come before types.h because types.h redefines bool as s32,
 * while gfx_rendering_api.h (included by gfx_api.h) uses stdbool.h's bool. */
#include <PR/ultratypes.h>
#include <PR/gbi.h>
#include "gfx_api.h"

#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "data.h"
#include "bss.h"
#include "video.h"
#include "gbiex.h"
#include "game/menu.h"
#include "system.h"
#include "constants.h"
#include "assetcatalog.h"
#include "modelcatalog.h"
#include "pdgui_charpreview.h"

/* ========================================================================
 * State
 * ======================================================================== */

#define CHARPREVIEW_WIDTH  256
#define CHARPREVIEW_HEIGHT 256

static s32 s_PreviewFb = -1;         /* Framebuffer ID, -1 = not created */
static s32 s_PreviewRequested = 0;   /* Non-zero if preview render needed */
static u8  s_PreviewHeadnum = 0;     /* Head to render (character mode) */
static u8  s_PreviewBodynum = 0;     /* Body to render (character mode) */
static u32 s_PreviewFilenum = 0;     /* Filenum (weapon/vehicle/prop mode) */
static s32 s_PreviewType    = PDGUI_PREVIEW_CHARACTER; /* Current model type */
static u32 s_PreviewTexId = 0;       /* GL texture ID of the rendered preview */
static s32 s_PreviewReady = 0;       /* Non-zero if texture has valid content */
static f32 s_PreviewRotY = 0.0f;     /* Y rotation in radians (set by caller) */
static Vp  s_PreviewVp;              /* Viewport for FBO render (must NOT be inline in display list) */

/* ========================================================================
 * Init / Shutdown
 * ======================================================================== */

/**
 * Initialize the character preview system.
 * Creates the offscreen framebuffer. Call after video/gfx init.
 */
void pdguiCharPreviewInit(void)
{
    if (s_PreviewFb >= 0) {
        return;  /* Already initialized */
    }

    if (!videoFramebuffersSupported()) {
        return;  /* FBOs not available */
    }

    /* Create a small FBO for the character preview.
     * upscale=0, autoresize=0 — fixed size, not tied to window. */
    s_PreviewFb = videoCreateFramebuffer(CHARPREVIEW_WIDTH, CHARPREVIEW_HEIGHT, 0, 0);

    if (s_PreviewFb >= 0) {
        /* Cache the GL texture ID — it's constant once the FBO exists.
         * The texture content updates each time we render to the FBO. */
        struct GfxRenderingAPI *rapi = gfx_get_current_rendering_api();
        if (rapi && rapi->get_framebuffer_texture_id) {
            s_PreviewTexId = (u32)(uintptr_t)rapi->get_framebuffer_texture_id(s_PreviewFb);
        }

        sysLogPrintf(LOG_NOTE, "pdgui_charpreview: Created FBO %d (%dx%d) texId=%u",
                     s_PreviewFb, CHARPREVIEW_WIDTH, CHARPREVIEW_HEIGHT, s_PreviewTexId);
    }
}

/* ========================================================================
 * Public API (called from C++ via extern "C")
 * ======================================================================== */

/**
 * Internal helper: write newparams + rotation into the menu model for the
 * current local player and mark the preview as requested.
 *
 * Shared by the character and single-filenum request paths so the render
 * hook sees the same "one pending request" state regardless of type.
 */
static void charPreviewSubmitParams(u32 params, s32 type)
{
    s_PreviewType = type;
    s_PreviewRequested = 1;

    s32 playernum = g_MpPlayerNum;
    if (playernum < 0) playernum = 0;
    if (playernum >= MAX_PLAYERS) playernum = 0;

    g_Menus[playernum].menumodel.newparams = params;
    g_Menus[playernum].menumodel.newroty  = s_PreviewRotY;
    g_Menus[playernum].menumodel.curroty  = s_PreviewRotY;
}

/**
 * Request a character preview render for the given head/body catalog IDs.
 * Resolves catalog IDs to runtime indices internally for the render pipeline.
 *
 * This is the historical API -- kept as a thin wrapper so existing callers
 * (agent select, agent create, modding hub, room lobby) continue to work.
 */
void pdguiCharPreviewRequest(const char *head_id, const char *body_id)
{
    pdguiCharPreviewRequestEx(PDGUI_PREVIEW_CHARACTER, head_id, body_id);
}

/**
 * Generalized request: character (head+body) OR single-filenum
 * (weapon / vehicle / prop) by catalog id.
 *
 * For CHARACTER: id1 = head catalog id, id2 = body catalog id.
 * For WEAPON / VEHICLE / PROP: id1 = catalog id, id2 is ignored.
 *
 * Non-character paths resolve the catalog entry and use its source_filenum
 * as the menu model filenum.  If resolution fails, the request is silently
 * dropped so the caller's fallback placeholder (pdgui_model_preview
 * silhouette) remains visible.
 */
void pdguiCharPreviewRequestEx(PdguiPreviewType type,
                                const char *id1,
                                const char *id2)
{
    if (type == PDGUI_PREVIEW_CHARACTER) {
        /* Resolve catalog IDs → mpheadnum / mpbodynum for the render pipeline */
        u8 headnum = 0;
        u8 bodynum = 0;

        if (id2 && id2[0]) {
            const asset_entry_t *be = assetCatalogResolve(id2);
            if (be && be->type == ASSET_BODY && be->mp_index >= 0) {
                bodynum = (u8)be->mp_index;
            }
        }

        if (id1 && id1[0]) {
            const asset_entry_t *he = assetCatalogResolve(id1);
            if (he && he->type == ASSET_HEAD && he->mp_index >= 0) {
                headnum = (u8)he->mp_index;
            }
        }

        s_PreviewHeadnum = headnum;
        s_PreviewBodynum = bodynum;
        s_PreviewFilenum = 0;

        /* MENUMODELPARAMS_SET_MP_HEADBODY -- character sentinel 0xffff low
         * word + head in bits 16-23 + body in bits 24-31. */
        u32 params = 0xffff
            | ((u32)headnum << 16)
            | ((u32)bodynum << 24);

        charPreviewSubmitParams(params, PDGUI_PREVIEW_CHARACTER);
        return;
    }

    /* Single-filenum path (weapon / vehicle / prop).  Catalog entry's
     * source_filenum is the N64 fileSlots index for the model file. */
    u32 filenum = 0;
    if (id1 && id1[0]) {
        const asset_entry_t *e = assetCatalogResolve(id1);
        if (e && e->source_filenum > 0) {
            filenum = (u32)e->source_filenum;
        }
    }

    if (filenum == 0) {
        /* Resolution failed -- don't touch pending state; leave the
         * existing preview / placeholder on screen. */
        return;
    }

    pdguiCharPreviewRequestFilenum(type, filenum);
}

/**
 * Low-level filenum-based request: skip catalog resolution.  Intended for
 * callers that already hold a resolved file index (training screens that
 * iterate g_FrWeapons[] / g_DtItems[], etc.).
 */
void pdguiCharPreviewRequestFilenum(PdguiPreviewType type, u32 filenum)
{
    if (filenum == 0) return;

    s_PreviewHeadnum = 0;
    s_PreviewBodynum = 0;
    s_PreviewFilenum = filenum;

    /* MENUMODELPARAMS_SET_FILENUM just returns the filenum. */
    u32 params = filenum;

    charPreviewSubmitParams(params, type);
}

/**
 * Set the Y rotation angle (radians) used by the next preview request.
 * Call this each frame before pdguiCharPreviewRequest to animate rotation.
 */
void pdguiCharPreviewSetRotY(f32 rotY)
{
    s_PreviewRotY = rotY;
}

/**
 * Returns the GL texture ID of the character preview, or 0 if not ready.
 * This is an OpenGL texture that can be used with ImGui::Image.
 */
u32 pdguiCharPreviewGetTextureId(void)
{
    if (!s_PreviewReady || s_PreviewFb < 0) {
        return 0;
    }
    return s_PreviewTexId;
}

/**
 * Returns non-zero if a preview has been rendered and is ready to display.
 */
s32 pdguiCharPreviewIsReady(void)
{
    return s_PreviewReady;
}

/**
 * Returns the preview framebuffer dimensions.
 */
void pdguiCharPreviewGetSize(s32 *w, s32 *h)
{
    if (w) *w = CHARPREVIEW_WIDTH;
    if (h) *h = CHARPREVIEW_HEIGHT;
}

/* ========================================================================
 * Thumbnail bake API
 * ======================================================================== */

/**
 * Bake the currently-rendered preview to a new standalone GL texture.
 *
 * Reads pixels from the preview FBO color texture via glGetTexImage, creates a
 * fresh GL texture from those pixels, clears s_PreviewReady, and returns the
 * new texture ID.  The caller owns the texture and must call
 * pdguiCharPreviewFreeTexture() to release it.
 *
 * Returns 0 if the preview is not ready or GL calls fail.
 */
u32 pdguiCharPreviewBakeToTexture(void)
{
    if (!s_PreviewReady || s_PreviewTexId == 0) {
        return 0;
    }

    const s32 w = CHARPREVIEW_WIDTH;
    const s32 h = CHARPREVIEW_HEIGHT;

    /* Read the FBO color texture into a temporary RGBA pixel buffer.
     * The underlying texture is GL_RGB8; OpenGL fills alpha=255. */
    u8 *pixels = (u8 *)malloc((size_t)w * h * 4);
    if (!pixels) {
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, s_PreviewTexId);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* Allocate a new, independent GL texture from those pixels. */
    u32 newTex = 0;
    glGenTextures(1, &newTex);
    if (newTex != 0) {
        glBindTexture(GL_TEXTURE_2D, newTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    free(pixels);
    s_PreviewReady = 0;
    return newTex;
}

/**
 * Delete a GL texture previously returned by pdguiCharPreviewBakeToTexture.
 * Safe to call with texId == 0 (no-op).
 */
void pdguiCharPreviewFreeTexture(u32 texId)
{
    if (texId != 0) {
        glDeleteTextures(1, &texId);
    }
}

/* ========================================================================
 * Skin Override (Batch S-2)
 * ======================================================================== */

static u32 s_SkinOverrideTexId  = 0;
static s32 s_SkinOverrideWidth  = 0;
static s32 s_SkinOverrideHeight = 0;
static s32 s_SkinOverrideActive = 0;

/* ========================================================================
 * Skin Texture Capture (Batch S-9)
 *
 * State machine for capturing the original body texture from the GBI
 * pipeline.  The flow:
 *   1. Caller requests capture (pdguiCharPreviewRequestSkinCapture)
 *   2. Next render: override is suppressed, gfx_pc capture mode is active
 *   3. After FBO pass: we read back the rendered model from the FBO
 *   4. Caller polls pdguiCharPreviewSkinCaptureReady() and retrieves pixels
 * ======================================================================== */

#define SKIN_CAPTURE_IDLE       0
#define SKIN_CAPTURE_WAITING    1  /* waiting for model to load (loaddelay) */
#define SKIN_CAPTURE_RENDERING  2  /* model loaded, capture pass in progress */
#define SKIN_CAPTURE_COMPLETE   3  /* pixels captured, ready to read */

static s32  s_SkinCaptureState = SKIN_CAPTURE_IDLE;
static s32  s_SkinCaptureDelay = 0;  /* frames to wait for model load */
static u8  *s_SkinCapturePixels = NULL;
static s32  s_SkinCaptureSrcW = 0;
static s32  s_SkinCaptureSrcH = 0;

/* gfx_pc.cpp capture API — extern declarations */
extern void gfxSkinCaptureRequest(void);
extern void gfxSkinCaptureFinalizeFbo(u32 fboTexId);
extern s32  gfxSkinCaptureIsComplete(void);
extern void gfxSkinCaptureGetResult(u32 *texId, s32 *texW, s32 *texH);
extern void gfxSkinCaptureClear(void);

void pdguiCharPreviewSetSkinOverride(u32 glTexId, s32 texWidth, s32 texHeight)
{
    s_SkinOverrideTexId  = glTexId;
    s_SkinOverrideWidth  = texWidth;
    s_SkinOverrideHeight = texHeight;
    s_SkinOverrideActive = (glTexId != 0) ? 1 : 0;
}

void pdguiCharPreviewClearSkinOverride(void)
{
    s_SkinOverrideTexId  = 0;
    s_SkinOverrideWidth  = 0;
    s_SkinOverrideHeight = 0;
    s_SkinOverrideActive = 0;
}

s32 pdguiCharPreviewHasSkinOverride(void)
{
    return s_SkinOverrideActive;
}

u32 pdguiCharPreviewGetSkinOverrideTexId(void)
{
    return s_SkinOverrideTexId;
}

/* ========================================================================
 * Skin Texture Capture API (S-9)
 * ======================================================================== */

void pdguiCharPreviewRequestSkinCapture(void)
{
    /* Free any previous capture */
    if (s_SkinCapturePixels) {
        free(s_SkinCapturePixels);
        s_SkinCapturePixels = NULL;
    }
    s_SkinCaptureSrcW = 0;
    s_SkinCaptureSrcH = 0;

    /* Start the delay — need 2 frames for the model to load.
     * The charpreview render hook calls menuRenderModel which has a
     * loaddelay phase.  We wait a few frames then trigger capture. */
    s_SkinCaptureState = SKIN_CAPTURE_WAITING;
    s_SkinCaptureDelay = 3;  /* 3 frames should cover loaddelay */

    gfxSkinCaptureClear();

    sysLogPrintf(LOG_NOTE, "skin_capture: capture requested, waiting %d frames",
                 s_SkinCaptureDelay);
}

s32 pdguiCharPreviewSkinCaptureReady(void)
{
    return (s_SkinCaptureState == SKIN_CAPTURE_COMPLETE) ? 1 : 0;
}

u8 *pdguiCharPreviewSkinCaptureGetPixels(s32 *outW, s32 *outH)
{
    if (s_SkinCaptureState != SKIN_CAPTURE_COMPLETE || !s_SkinCapturePixels) {
        if (outW) *outW = 0;
        if (outH) *outH = 0;
        return NULL;
    }

    if (outW) *outW = s_SkinCaptureSrcW;
    if (outH) *outH = s_SkinCaptureSrcH;
    return s_SkinCapturePixels;
}

void pdguiCharPreviewSkinCaptureConsume(void)
{
    /* Caller takes ownership — we just reset state */
    if (s_SkinCapturePixels) {
        free(s_SkinCapturePixels);
        s_SkinCapturePixels = NULL;
    }
    s_SkinCaptureSrcW = 0;
    s_SkinCaptureSrcH = 0;
    s_SkinCaptureState = SKIN_CAPTURE_IDLE;
    gfxSkinCaptureClear();
}

/* ========================================================================
 * GBI-Phase Render Hook
 * ======================================================================== */

/**
 * Called from menuRenderDialog when a dialog is being replaced by ImGui
 * but a character preview is needed. This injects GBI commands to render
 * the menu model to the preview FBO.
 *
 * Must be called within the GBI command list building phase — the FBO
 * switch commands are written as GBI extensions that get processed by
 * the port's gfx_run_dl().
 *
 * Returns the updated display list pointer.
 */
Gfx *pdguiCharPreviewRenderGBI(Gfx *gdl, struct menu *menu)
{
    if (!s_PreviewRequested || s_PreviewFb < 0) {
        return gdl;
    }

    /* Skin capture state machine: count down delay frames (S-9) */
    if (s_SkinCaptureState == SKIN_CAPTURE_WAITING) {
        s_SkinCaptureDelay--;
        if (s_SkinCaptureDelay <= 0) {
            s_SkinCaptureState = SKIN_CAPTURE_RENDERING;
            gfxSkinCaptureRequest();
            sysLogPrintf(LOG_NOTE, "skin_capture: delay elapsed, capture pass active");
        }
    }

    /* menuRenderModel handles two phases:
     *   1. Loading: when newparams != 0 && newparams != curparams, it loads
     *      the model file and sets curparams = newparams. This takes 1-2
     *      frames due to loaddelay.
     *   2. Rendering: when curparams != 0 and the model is loaded, it
     *      actually renders geometry.
     *
     * We must call menuRenderModel even when curparams == 0 so the loading
     * path can execute. But we only set up the FBO render target and mark
     * the preview as ready when the model has actually loaded (curparams != 0
     * after the call). If the model is still loading, we keep
     * s_PreviewRequested alive so the next frame tries again. */

    s32 renderModelType = MENUMODELTYPE_DEFAULT;
    if (s_PreviewType == PDGUI_PREVIEW_WEAPON) {
        renderModelType = MENUMODELTYPE_HUDPIECE;
    }

    if (menu->menumodel.curparams == 0) {
        /* Model not loaded yet — let menuRenderModel run the loading path
         * without FBO setup. Keep s_PreviewRequested alive for next frame. */
        gdl = menuRenderModel(gdl, &menu->menumodel, renderModelType);
        return gdl;
    }

    /* Model is loaded — render to the preview FBO */

    /* Switch render target to our preview FBO */
    gDPSetFramebufferTargetEXT(gdl++, 0, 0, 0, s_PreviewFb);

    /* Set up viewport for the small FBO.
     * IMPORTANT: The Vp must NOT be inline in the display list — the GBI
     * interpreter walks the list sequentially and would try to execute
     * the Vp data as a command (B-132: opcode 0x02 = vscale[0] bytes). */
    s_PreviewVp.vp.vscale[0] = CHARPREVIEW_WIDTH * 2;
    s_PreviewVp.vp.vscale[1] = CHARPREVIEW_HEIGHT * 2;
    s_PreviewVp.vp.vscale[2] = G_MAXZ / 2;
    s_PreviewVp.vp.vscale[3] = 0;
    s_PreviewVp.vp.vtrans[0] = CHARPREVIEW_WIDTH * 2;
    s_PreviewVp.vp.vtrans[1] = CHARPREVIEW_HEIGHT * 2;
    s_PreviewVp.vp.vtrans[2] = G_MAXZ / 2;
    s_PreviewVp.vp.vtrans[3] = 0;

    gSPViewport(gdl++, &s_PreviewVp);

    /* Set scissor to the FBO size */
    gDPSetScissor(gdl++, G_SC_NON_INTERLACE,
                  0, 0, CHARPREVIEW_WIDTH, CHARPREVIEW_HEIGHT);

    /* Enable Z-buffer for model rendering */
    gSPSetGeometryMode(gdl++, G_ZBUFFER);

    /* Render the loaded model */
    gdl = menuRenderModel(gdl, &menu->menumodel, renderModelType);

    /* Disable Z-buffer */
    gSPClearGeometryMode(gdl++, G_ZBUFFER);

    /* Switch back to the main framebuffer */
    gDPSetFramebufferTargetEXT(gdl++, 0, 0, 0, 0);

    /* Mark preview as ready. The texture ID was cached at init time.
     * The GBI commands above will be processed by gfx_run_dl before
     * the ImGui phase, so the texture will have valid content. */
    s_PreviewReady = 1;
    s_PreviewRequested = 0;

    return gdl;
}

/**
 * Poll the skin capture state machine.  Must be called during the ImGui
 * phase (after gfx_run has processed GBI commands) so the FBO texture
 * contains valid rendered data.
 *
 * When the capture FBO pass completed this frame, reads back the FBO
 * texture pixels and transitions to SKIN_CAPTURE_COMPLETE.
 */
void pdguiCharPreviewSkinCapturePoll(void)
{
    if (s_SkinCaptureState != SKIN_CAPTURE_RENDERING) {
        return;
    }

    /* The capture pass was active this frame.  By the time ImGui renders,
     * gfx_run_dl has processed the GBI and the preview FBO texture
     * (s_PreviewTexId) contains the model rendered with the ORIGINAL body
     * texture (since gfx_pc's capture mode suppressed the override).
     *
     * Read back the FBO color texture.  gfx_pc recorded the N64 body
     * texture tile dimensions — we read the whole FBO and the caller can
     * scale as needed. */

    if (s_PreviewTexId == 0 || !s_PreviewReady) {
        /* FBO didn't render yet — stay in RENDERING, try next frame */
        return;
    }

    const s32 w = CHARPREVIEW_WIDTH;
    const s32 h = CHARPREVIEW_HEIGHT;

    u8 *pixels = (u8 *)malloc((size_t)w * h * 4);
    if (!pixels) {
        sysLogPrintf(LOG_WARNING, "skin_capture: malloc failed for %dx%d readback", w, h);
        s_SkinCaptureState = SKIN_CAPTURE_IDLE;
        gfxSkinCaptureClear();
        return;
    }

    glBindTexture(GL_TEXTURE_2D, s_PreviewTexId);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* Get the N64 body texture dimensions from the capture result.
     * If gfx_pc didn't record them (model didn't render a texture),
     * use the FBO dimensions as fallback. */
    s32 srcW = 0, srcH = 0;
    gfxSkinCaptureGetResult(NULL, &srcW, &srcH);
    if (srcW <= 0 || srcH <= 0) {
        srcW = w;
        srcH = h;
    }

    /* Store the FBO pixels (full 256x256 render).  The caller will use
     * the N64 texture dimensions for proper scaling into the canvas. */
    if (s_SkinCapturePixels) {
        free(s_SkinCapturePixels);
    }
    s_SkinCapturePixels = pixels;
    s_SkinCaptureSrcW = w;  /* FBO dimensions — full rendered frame */
    s_SkinCaptureSrcH = h;

    s_SkinCaptureState = SKIN_CAPTURE_COMPLETE;
    gfxSkinCaptureClear();

    sysLogPrintf(LOG_NOTE, "skin_capture: readback complete %dx%d (body tex %dx%d)",
                 w, h, srcW, srcH);
}
