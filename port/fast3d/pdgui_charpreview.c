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

/* PR/gbi.h must come before gfx_api.h because gfx_api.h uses the Gfx typedef.
 * gfx_api.h must come before types.h because types.h redefines bool as s32,
 * while gfx_rendering_api.h (included by gfx_api.h) uses stdbool.h's bool. */
#include <PR/ultratypes.h>
#include <PR/gbi.h>
#include "gfx_api.h"

#include <string.h>
#include "types.h"
#include "data.h"
#include "bss.h"
#include "video.h"
#include "gbiex.h"
#include "game/menu.h"
#include "system.h"

/* ========================================================================
 * State
 * ======================================================================== */

#define CHARPREVIEW_WIDTH  256
#define CHARPREVIEW_HEIGHT 256

static s32 s_PreviewFb = -1;         /* Framebuffer ID, -1 = not created */
static s32 s_PreviewRequested = 0;   /* Non-zero if preview render needed */
static u8  s_PreviewHeadnum = 0;     /* Head to render */
static u8  s_PreviewBodynum = 0;     /* Body to render */
static u32 s_PreviewTexId = 0;       /* GL texture ID of the rendered preview */
static s32 s_PreviewReady = 0;       /* Non-zero if texture has valid content */

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
 * Request a character preview render for the given head/body.
 * The actual render happens during the next GBI frame in menuRenderDialog.
 */
void pdguiCharPreviewRequest(u8 headnum, u8 bodynum)
{
    s_PreviewHeadnum = headnum;
    s_PreviewBodynum = bodynum;
    s_PreviewRequested = 1;

    /* Also update the menu model params so the game loads the right model.
     * This uses the same mechanism as the MP character select screen. */
    s32 playernum = g_MpPlayerNum;
    if (playernum < 0) playernum = 0;
    if (playernum >= MAX_PLAYERS) playernum = 0;

    u32 params = 0xffff
        | ((u32)headnum << 16)
        | ((u32)bodynum << 24);

    g_Menus[playernum].menumodel.newparams = params;
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

    /* Only render if the menu model has loaded (curparams matches newparams) */
    if (menu->menumodel.curparams == 0) {
        return gdl;
    }

    /* Switch render target to our preview FBO */
    gDPSetFramebufferTargetEXT(gdl++, 0, 0, 0, s_PreviewFb);

    /* Set up viewport for the small FBO */
    {
        Vp *vp = (Vp *)gdl;
        gdl = (Gfx *)((u8 *)gdl + sizeof(Vp));

        vp->vp.vscale[0] = CHARPREVIEW_WIDTH * 2;
        vp->vp.vscale[1] = CHARPREVIEW_HEIGHT * 2;
        vp->vp.vscale[2] = G_MAXZ / 2;
        vp->vp.vscale[3] = 0;
        vp->vp.vtrans[0] = CHARPREVIEW_WIDTH * 2;
        vp->vp.vtrans[1] = CHARPREVIEW_HEIGHT * 2;
        vp->vp.vtrans[2] = G_MAXZ / 2;
        vp->vp.vtrans[3] = 0;

        gSPViewport(gdl++, vp);
    }

    /* Set scissor to the FBO size */
    gDPSetScissor(gdl++, G_SC_NON_INTERLACE,
                  0, 0, CHARPREVIEW_WIDTH, CHARPREVIEW_HEIGHT);

    /* Enable Z-buffer for model rendering */
    gSPSetGeometryMode(gdl++, G_ZBUFFER);

    /* Render the character model */
    gdl = menuRenderModel(gdl, &menu->menumodel, MENUMODELTYPE_DEFAULT);

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
