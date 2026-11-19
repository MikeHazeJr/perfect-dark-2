/*
 * gfx_bgfx.cpp - fast3d rendering backend on bgfx's OpenGL 4.3 renderer.
 *
 * Phase 2a step 1 of the 2.0 renderer (decision D5, refactor R10 in
 * context/designs/rendering/renderer-2.0-scope.md): classic Perfect Dark on
 * bgfx, pixel-identical to the classic GL renderer in gfx_opengl.cpp, which
 * stays the default. Design notes: context/designs/rendering/bgfx-classic-gl.md.
 *
 * How the GL backend's immediate calls map onto bgfx:
 *
 *   - bgfx records draws and executes the whole frame in bgfx::frame(). Every
 *     view runs in sequential mode, so draws execute in submission order.
 *   - A new view opens whenever fast3d changes render target or viewport, and
 *     for every clear and framebuffer copy, which bgfx can only do at the
 *     start of a view. View ids run 0..N in frame order.
 *   - fast3d's framebuffer 0 (the window in the GL backend) is an offscreen
 *     target here, so the game can copy from it (motion blur, blur menus);
 *     gfxBgfxResetForOverlay copies it to the window before ImGui draws.
 *   - Texture uploads always create a new bgfx texture: bgfx applies creates
 *     before a frame's draws and destroys after them, so a draw keeps seeing
 *     the pixels it was submitted with even when fast3d recycles the id.
 *   - Sampler state lives on the texture object, as GL texture parameters do.
 *   - The colour combiner is one precompiled uber-shader (fs_fast3d.sc).
 *   - Decal polygon offset uses PD_BGFX_STATE_POLYGON_OFFSET, a state bit the
 *     build patches into bgfx's GL renderer (gfx_bgfx_ext.h).
 *
 * bgfx renders through the GL context SDL created (bgfx::PlatformData with a
 * context and no window), single-threaded, and never swaps; SDL swaps.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SDL.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include "glad/glad.h"

#include "gfx_cc.h"
#include "gfx_rendering_api.h"
#include "gfx_pc.h"
#include "gfx_bgfx.h"
#include "gfx_bgfx_ext.h"
#include "gfx_opengl.h"
#include "gfx_tee.h"

#include "vs_fast3d.bin.h"
#include "fs_fast3d.bin.h"
#include "vs_pdcopy.bin.h"
#include "fs_pdcopy.bin.h"

extern "C" int gfxDebugCullModeGet(void);
extern "C" int gfxDebugWireframeGet(void);

namespace {

/* ------------------------------------------------------------------------ */
/* State                                                                     */
/* ------------------------------------------------------------------------ */

/* One combiner program. The struct name is local on purpose: the rendering
 * API passes `struct ShaderProgram *`, which each backend defines privately,
 * and two definitions of one class in one program would break the ODR. */
struct BgfxProgram {
    uint64_t id0;
    uint32_t id1;
    uint8_t num_inputs;
    bool used_textures[2];
    bgfx::VertexLayout layout;
    float sel[4][4];
    float shape[4];
    uint32_t flags;
    /* frame_count, noise_scale and the two three-point "linear" flags as the
     * GL backend last set them on this program. GL sets these uniforms only
     * when fast3d loads the program (gfx_opengl_set_uniforms from
     * gfx_opengl_load_shader), and a GL program keeps its uniform values, so
     * draws keep the values from the last load, not the live ones. */
    float frameU[4];
};

/* A sampleable texture: fast3d textures and framebuffer colour targets. */
struct BgfxTex {
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t sampler = 0;           /* bgfx sampler flags, like GL texture params */
    std::vector<uint8_t> rgba;      /* CPU copy of the last upload */
};

struct BgfxFb {
    /* Parameters as fast3d set them (update_framebuffer_parameters). */
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t msaa = 1;
    bool invert_y = false;
    bool has_depth = false;
    /* Parameters the bgfx objects were created with. */
    uint32_t made_width = 0;
    uint32_t made_height = 0;
    uint32_t made_msaa = 0;
    bool made_depth = false;
    bool failed = false;            /* bgfx refused these parameters; not retried */
    bgfx::FrameBufferHandle fb = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle color = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depth = BGFX_INVALID_HANDLE;
    /* GL texture parameters of the colour texture. The GL backend creates
     * it with GL_LINEAR filtering and the default GL_REPEAT wrap. */
    uint32_t sampler = 0;
};

/* What a texture unit has bound: a fast3d texture or a framebuffer colour. */
struct UnitBinding {
    bool is_fb = false;
    uint32_t id = 0;
};

bool s_active = false;
bool s_initialised = false;

/* The finished frame (the game image plus ImGui) is composed offscreen, in
 * this window-sized target, and then copied to the window. It plays the GL
 * window's front buffer: a framebuffer copy that reads GL_FRONT (the last
 * presented frame) runs, in view order, before this frame's present, so it
 * finds the previous frame here. In parity mode the target is also what a
 * capture reads back, and nothing is copied to the window, which belongs to
 * the classic renderer. */
bgfx::FrameBufferHandle s_finalFb = BGFX_INVALID_HANDLE;
bgfx::TextureHandle s_finalColor = BGFX_INVALID_HANDLE;
uint32_t s_finalWidth = 0;
uint32_t s_finalHeight = 0;
bool s_finalComposed = false;   /* gfxBgfxResetForOverlay ran this frame */

/* Renderer parity mode (gfx_tee.cpp): bgfx renders on its own context. */
bool s_tee = false;
SDL_Window *s_teeWindow = nullptr;
SDL_GLContext s_teeMainContext = nullptr;
SDL_GLContext s_teeBgfxContext = nullptr;
/* bgfx cannot read a render target back directly (BGFX_TEXTURE_READ_BACK
 * is refused on one): a capture blits the final image into this
 * BLIT_DST | READ_BACK copy and reads that. */
bgfx::TextureHandle s_teeReadback = BGFX_INVALID_HANDLE;
bool s_parityRequested = false;
bool s_parityReady = false;
uint32_t s_parityWidth = 0;
uint32_t s_parityHeight = 0;
std::vector<uint8_t> s_parityPixels;
/* GL texture names ImGui draws with in parity mode: framebuffer colours map
 * to the bgfx copy of that framebuffer, anything else is imported each frame. */
std::unordered_map<uint32_t, int> s_teeFbNames;
struct TeeImport {
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    uint32_t sampler = 0;
    uint32_t frame = 0;
};
std::unordered_map<uint32_t, TeeImport> s_teeImports;

std::map<std::pair<uint64_t, uint32_t>, BgfxProgram> s_programs;
BgfxProgram *s_program = nullptr;

std::unordered_map<uint32_t, BgfxTex> s_textures;
uint32_t s_nextTexture = 1;

/* pdgui textures (gfxBgfxPdguiTex*): what pdgui makes with glGenTextures in
 * the GL path. fast3d can bind one too (the skin editor's canvas override in
 * gfx_pc.cpp), so texture units resolve these ids as well. */
struct PdguiTex {
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t sampler = 0;
};
std::unordered_map<uint32_t, PdguiTex> s_pdguiTextures;
uint32_t s_nextPdguiTexture = 1;

std::vector<BgfxFb> s_fbs;
size_t s_currentFb = 0;
/* 1x1 colour and depth target for views whose frame buffer bgfx refused. */
bgfx::FrameBufferHandle s_sinkFb = BGFX_INVALID_HANDLE;

UnitBinding s_units[2];
int s_activeUnit = 0;
bool s_unitLinear[2] = { false, false };

FilteringMode s_filterMode = FILTER_LINEAR;

uint32_t s_frameCount = 0;
float s_noiseScale = 0.0f;

/* Draw state set by fast3d. */
uint64_t s_depthState = 0;
uint64_t s_blendState = 0;
int s_viewport[4] = { 0, 0, 1, 1 };
/* GL scissor state as the GL backend leaves it: set_scissor only moves the
 * box; the test is off after reset_for_overlay (and at start) and back on
 * after every clear, MSAA resolve and framebuffer copy. */
int s_scissor[4] = { 0, 0, 1, 1 };
bool s_scissorSet = false;

/* View sequencing for the current bgfx frame. Ids restart after each
 * bgfx::frame(), which is bgfx's frame boundary (not fast3d's start_frame:
 * pdgui can issue work between the two). */
uint16_t s_nextView = 0;
bool s_viewOpen = false;
bgfx::ViewId s_view = 0;
bool s_viewsExhaustedLogged = false;
/* The ImGui view is allocated on first use in the frame, so a pdgui bake
 * made while ImGui builds its frame runs before ImGui samples it, as the
 * GL backend's immediate bake does. */
uint16_t s_overlayView = 0;
bool s_overlayAllocated = false;
bgfx::FrameBufferHandle s_overlayTarget = BGFX_INVALID_HANDLE;
uint32_t s_overlayWidth = 1;
uint32_t s_overlayHeight = 1;
/* bgfx keeps a frame's scissor rects in a fixed cache; consecutive draws
 * with the same rect reuse its entry. Valid until bgfx::frame(). */
int s_cachedScissor[4] = { 0, 0, 0, 0 };
uint16_t s_cachedScissorIdx = 0;
bool s_cachedScissorValid = false;
uint32_t s_windowWidth = 1;
uint32_t s_windowHeight = 1;
uint32_t s_resetWidth = 0;
uint32_t s_resetHeight = 0;
uint32_t s_resetFlags = 0;

/* Uniforms and programs. */
bgfx::ProgramHandle s_fast3dProgram = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_uSel = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_uShape = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_uFlags = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_uFrame = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_sTex0 = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_sTex1 = BGFX_INVALID_HANDLE;

bgfx::ProgramHandle s_copyProgram = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_uCopyMap = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_uCopyDst = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_sCopySrc = BGFX_INVALID_HANDLE;
bgfx::VertexLayout s_copyLayout;

/* Logged-once divergence notes. */
bool s_loggedDepthRange = false;
bool s_loggedMirrorClamp = false;
bool s_loggedFrontRead = false;
bool s_loggedMsaaBlit = false;
bool s_loggedViewport = false;
bool s_loggedTransient = false;
bool s_loggedLayout = false;
bool s_loggedFbFailed = false;

constexpr uint32_t kMaxViews = 4096;

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

uint32_t tagTex(uint32_t index, uint32_t kind)
{
    return (index << 3) | kind;
}

bgfx::ShaderHandle loadShader(const uint8_t *data, uint32_t size, const char *name)
{
    bgfx::ShaderHandle handle = bgfx::createShader(bgfx::makeRef(data, size));
    if (!bgfx::isValid(handle)) {
        sysFatalError("bgfx: could not create shader %s", name);
    }
    bgfx::setName(handle, name);
    return handle;
}

uint32_t wrapFlags(uint32_t cm, bool isS)
{
    switch (cm) {
        case G_TX_NOMIRROR | G_TX_CLAMP:
            return isS ? BGFX_SAMPLER_U_CLAMP : BGFX_SAMPLER_V_CLAMP;
        case G_TX_MIRROR | G_TX_WRAP:
            return isS ? BGFX_SAMPLER_U_MIRROR : BGFX_SAMPLER_V_MIRROR;
        case G_TX_MIRROR | G_TX_CLAMP:
            /* GL_MIRROR_CLAMP_TO_EDGE has no bgfx sampler mode. fast3d moves
             * mirrored clamps into the shader (gfx_pc.cpp clears G_TX_CLAMP
             * when G_TX_MIRROR is set), so this is not reached in practice. */
            if (!s_loggedMirrorClamp) {
                s_loggedMirrorClamp = true;
                sysLogPrintf(LOG_WARNING, "bgfx: mirror-clamp sampler requested; using mirrored repeat");
            }
            return isS ? BGFX_SAMPLER_U_MIRROR : BGFX_SAMPLER_V_MIRROR;
        case G_TX_NOMIRROR | G_TX_WRAP:
        default:
            return 0; /* GL_REPEAT */
    }
}

uint32_t filterFlags(bool linear)
{
    return linear ? 0 : (BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
}

uint32_t msaaTextureFlags(uint32_t msaa)
{
    if (msaa <= 1) return 0;
    if (msaa <= 2) return BGFX_TEXTURE_RT_MSAA_X2;
    if (msaa <= 4) return BGFX_TEXTURE_RT_MSAA_X4;
    if (msaa <= 8) return BGFX_TEXTURE_RT_MSAA_X8;
    return BGFX_TEXTURE_RT_MSAA_X16;
}

/* Allocates the next view of this frame, reset to bgfx's defaults: bgfx keeps
 * a view's frame buffer, rect, scissor and clear from frame to frame, and a
 * view that sets only some of them (a blit-only view, say) must not pick up
 * last frame's clear. Views past the bgfx limit reuse the last one (logged
 * once): the frame still renders, in a merged order. */
bgfx::ViewId allocView(void)
{
    if (s_nextView >= kMaxViews) {
        if (!s_viewsExhaustedLogged) {
            s_viewsExhaustedLogged = true;
            sysLogPrintf(LOG_WARNING, "bgfx: frame needs more than %u views; later passes share the last view", kMaxViews);
        }
        return (bgfx::ViewId)(kMaxViews - 1);
    }
    const bgfx::ViewId view = (bgfx::ViewId)s_nextView++;
    bgfx::resetView(view);
    return view;
}

} // namespace

/* Parity mode: a GL texture name imported as a bgfx texture (defined below). */
static int teeResolveGlTexture(uint32_t glName, uint16_t *handleIdx, uint32_t *samplerFlags);

namespace {

/* The bgfx frame buffer behind fast3d framebuffer `fbId`, created or resized
 * to match its parameters. In tee mode framebuffer 0 is offscreen as well. */
void ensureFb(size_t fbId);

bgfx::FrameBufferHandle fbHandle(size_t fbId)
{
    ensureFb(fbId);
    return s_fbs[fbId].fb;
}

/* Opens a view on fast3d framebuffer fbId. rect is in GL window coordinates
 * (origin bottom left); bgfx wants the origin top left. */
bgfx::ViewId openView(size_t fbId, int x, int y, int w, int h, uint16_t clearFlags)
{
    bgfx::ViewId view = allocView();
    const BgfxFb &fb = s_fbs[fbId];
    const int targetH = (int)fb.height;
    int top = targetH - y - h;

    if (x < 0 || top < 0 || w < 0 || h < 0 || x > 0xffff || top > 0xffff || w > 0xffff || h > 0xffff) {
        if (!s_loggedViewport) {
            s_loggedViewport = true;
            sysLogPrintf(LOG_WARNING,
                "bgfx: viewport (%d,%d %dx%d) on a %ux%u target is outside bgfx's view range; clamped",
                x, y, w, h, fb.width, fb.height);
        }
        int x0 = std::max(0, x);
        int t0 = std::max(0, top);
        w = std::max(0, std::min(x + w, 0xffff) - x0);
        h = std::max(0, std::min(top + h, 0xffff) - t0);
        x = x0;
        top = t0;
    }

    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    const bgfx::FrameBufferHandle target = fbHandle(fbId);
    if (!bgfx::isValid(target)) {
        /* A frame buffer bgfx refused (logged in ensureFb). An invalid handle
         * would mean the window, which in parity mode is the window the GL
         * renderer presents, so the view draws into a 1x1 target nobody
         * reads instead (bgfx keeps view rects at least 1x1). */
        bgfx::setViewFrameBuffer(view, s_sinkFb);
        bgfx::setViewRect(view, 0, 0, 1, 1);
        bgfx::setViewClear(view, BGFX_CLEAR_NONE);
        return view;
    }
    bgfx::setViewFrameBuffer(view, target);
    bgfx::setViewRect(view, (uint16_t)x, (uint16_t)top, (uint16_t)w, (uint16_t)h);
    bgfx::setViewScissor(view, 0, 0, 0, 0);
    bgfx::setViewClear(view, clearFlags, 0x000000ff, 1.0f, 0);
    bgfx::setViewTransform(view, nullptr, nullptr);
    return view;
}

void ensureDrawView(void)
{
    if (!s_viewOpen) {
        s_view = openView(s_currentFb, s_viewport[0], s_viewport[1], s_viewport[2], s_viewport[3], BGFX_CLEAR_NONE);
        s_viewOpen = true;
    }
}

BgfxTex *unitTexture(int unit, uint32_t *samplerOut, bgfx::TextureHandle *handleOut)
{
    const UnitBinding &b = s_units[unit];
    if (b.is_fb) {
        if (b.id >= s_fbs.size()) return nullptr;
        ensureFb(b.id);
        *samplerOut = s_fbs[b.id].sampler;
        *handleOut = s_fbs[b.id].color;
        return nullptr;
    }
    *handleOut = BGFX_INVALID_HANDLE;
    switch (b.id & 7u) {
        case GFX_BGFX_TEXID_KIND_PDGUI: {
            /* A pdgui texture bound through fast3d (the skin canvas). */
            auto pit = s_pdguiTextures.find(b.id);
            if (pit != s_pdguiTextures.end()) {
                *samplerOut = pit->second.sampler;
                *handleOut = pit->second.handle;
            }
            return nullptr;
        }
        case GFX_BGFX_TEXID_KIND_GL_NAME: {
            /* Parity mode: a GL texture the GL backend binds that is not one
             * of fast3d's (gfx_tee.cpp), imported with its GL parameters. */
            uint16_t idx = 0;
            uint32_t sampler = 0;
            if (teeResolveGlTexture(b.id >> 3, &idx, &sampler)) {
                *samplerOut = sampler;
                *handleOut = bgfx::TextureHandle{ idx };
            }
            return nullptr;
        }
        default:
            break;
    }
    auto it = s_textures.find(b.id);
    if (it == s_textures.end()) {
        return nullptr;
    }
    *samplerOut = it->second.sampler;
    *handleOut = it->second.handle;
    return &it->second;
}

/* Sampler parameters land on whatever is bound to `unit`, exactly like
 * glTexParameteri on the active texture unit. (A parity-mode GL name keeps
 * the parameters the GL backend gives it; they are read when imported.) */
void setUnitSampler(int unit, uint32_t sampler)
{
    const UnitBinding &b = s_units[unit];
    if (b.is_fb) {
        if (b.id < s_fbs.size()) s_fbs[b.id].sampler = sampler;
        return;
    }
    if ((b.id & 7u) == GFX_BGFX_TEXID_KIND_PDGUI) {
        auto pit = s_pdguiTextures.find(b.id);
        if (pit != s_pdguiTextures.end()) pit->second.sampler = sampler;
        return;
    }
    auto it = s_textures.find(b.id);
    if (it != s_textures.end()) it->second.sampler = sampler;
}

/* bgfx's callbacks, routed to the game: a fatal error goes through
 * sysFatalError (the game's error path and log) instead of bgfx's stub,
 * which aborts; traces go to the log; the shader cache, screenshots and
 * captures are not used (bgfx's stub would write .tga files). */
struct PdBgfxCallback : public bgfx::CallbackI {
    ~PdBgfxCallback() override {}
    void fatal(const char *_filePath, uint16_t _line, bgfx::Fatal::Enum _code, const char *_str) override
    {
        if (_code == bgfx::Fatal::DebugCheck) {
            sysLogPrintf(LOG_WARNING, "bgfx: check failed at %s:%u: %s", _filePath, (unsigned)_line, _str);
            return;
        }
        sysFatalError("bgfx: fatal error 0x%08x at %s:%u: %s", (unsigned)_code, _filePath, (unsigned)_line, _str);
    }
    void traceVargs(const char *_filePath, uint16_t _line, const char *_format, va_list _argList) override
    {
        char text[1024];
        vsnprintf(text, sizeof(text), _format, _argList);
        size_t len = strlen(text);
        while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
            text[--len] = '\0';
        }
        sysLogPrintf(LOG_NOTE, "bgfx: %s (%s:%u)", text, _filePath, (unsigned)_line);
    }
    void profilerBegin(const char *, uint32_t, const char *, uint16_t) override {}
    void profilerBeginLiteral(const char *, uint32_t, const char *, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t) override { return 0; }
    bool cacheRead(uint64_t, void *, uint32_t) override { return false; }
    void cacheWrite(uint64_t, const void *, uint32_t) override {}
    void screenShot(const char *, uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, const void *, uint32_t,
                    bool) override {}
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void *, uint32_t) override {}
};

PdBgfxCallback s_callback;

/* Shape of one combiner formula; see the table in fs_fast3d.sc. */
int ccShape(const uint8_t c[4], bool doSingle, bool doMultiply, bool doMix)
{
    const uint8_t B = c[1];
    const uint8_t C = c[2];
    const uint8_t D = c[3];
    if (doSingle) return 0;
    if (doMultiply) return 1;
    if (doMix) {
        const int b = B == SHADER_0 ? 0 : (B == SHADER_1 ? 1 : 2);
        return C == SHADER_1 ? 5 + b : 2 + b;
    }
    if (D == SHADER_0) return 8;
    if (D == SHADER_1) return C == SHADER_1 ? 10 : 9;
    return 11;
}

} // namespace

namespace {

void ensureFb(size_t fbId)
{
    BgfxFb &fb = s_fbs[fbId];
    const uint32_t w = std::max<uint32_t>(fb.width, 1);
    const uint32_t h = std::max<uint32_t>(fb.height, 1);
    const bool same = fb.made_width == w && fb.made_height == h && fb.made_msaa == fb.msaa
        && fb.made_depth == fb.has_depth;
    if (same && (bgfx::isValid(fb.fb) || fb.failed)) {
        return;
    }
    if (bgfx::isValid(fb.fb)) {
        /* Destroys run after this frame's draws, like a GL resize. */
        bgfx::destroy(fb.fb);
    }
    fb.fb = BGFX_INVALID_HANDLE;
    fb.color = BGFX_INVALID_HANDLE;
    fb.depth = BGFX_INVALID_HANDLE;
    fb.made_width = w;
    fb.made_height = h;
    fb.made_msaa = fb.msaa;
    fb.made_depth = fb.has_depth;
    fb.failed = false;

    /* The render-target bits are one 3-bit field: BGFX_TEXTURE_RT is value 1
     * and BGFX_TEXTURE_RT_MSAA_Xn the rest, so they are never ORed together.
     * The depth target must carry them too: write-only without them makes
     * bgfx's GL renderer create neither a texture nor a renderbuffer, which
     * leaves the frame buffer without depth. */
    const uint64_t msaa = msaaTextureFlags(fb.msaa);
    const uint64_t rt = msaa != 0 ? msaa : BGFX_TEXTURE_RT;
    bgfx::Attachment att[2];
    fb.color = bgfx::createTexture2D((uint16_t)w, (uint16_t)h, false, 1, bgfx::TextureFormat::RGBA8, rt);
    uint8_t num = 0;
    if (bgfx::isValid(fb.color)) {
        att[num++].init(fb.color);
    }
    if (fb.has_depth) {
        fb.depth = bgfx::createTexture2D((uint16_t)w, (uint16_t)h, false, 1, bgfx::TextureFormat::D24S8,
                                         BGFX_TEXTURE_RT_WRITE_ONLY | rt);
        if (bgfx::isValid(fb.depth)) {
            att[num++].init(fb.depth);
        }
    }
    const bool complete = bgfx::isValid(fb.color) && (!fb.has_depth || bgfx::isValid(fb.depth));
    if (complete) {
        fb.fb = bgfx::createFrameBuffer(num, att, true);
    }
    if (!bgfx::isValid(fb.fb)) {
        /* Not retried until the parameters change: each attempt would
         * otherwise leave two textures behind every call. A refused
         * createFrameBuffer does not take ownership of the textures. */
        if (bgfx::isValid(fb.color)) bgfx::destroy(fb.color);
        if (bgfx::isValid(fb.depth)) bgfx::destroy(fb.depth);
        fb.color = BGFX_INVALID_HANDLE;
        fb.depth = BGFX_INVALID_HANDLE;
        fb.failed = true;
        if (!s_loggedFbFailed) {
            s_loggedFbFailed = true;
            sysLogPrintf(LOG_ERROR, "bgfx: could not create framebuffer %zu (%ux%u, msaa %u, depth %d); its draws are dropped",
                         fbId, w, h, fb.msaa, fb.has_depth ? 1 : 0);
        }
    }
}

/* Draws a copy of a source rectangle with glBlitFramebuffer semantics into
 * the whole of framebuffer dstFb. src is the source colour texture; the map
 * follows the GL backend's copy (fs_pdcopy.sc). */
void copyRect(size_t dstFb, bgfx::TextureHandle src, float srcX0, float srcY0, float srcX1, float srcY1,
              float dstX0, float dstY0, float dstX1, float dstY1)
{
    const BgfxFb &dst = s_fbs[dstFb];
    if (!bgfx::isValid(src)) return;
    bgfx::ViewId view = openView(dstFb, 0, 0, (int)dst.width, (int)dst.height, BGFX_CLEAR_NONE);

    if (bgfx::getAvailTransientVertexBuffer(6, s_copyLayout) < 6) return;
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, 6, s_copyLayout);
    float *v = (float *)tvb.data;
    const float quad[6][4] = {
        { -1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 0.0f, 1.0f },
        { -1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 0.0f, 1.0f }, { -1.0f, 1.0f, 0.0f, 1.0f },
    };
    memcpy(v, quad, sizeof(quad));

    const float map[4] = { srcX0, srcY0, (srcX1 - srcX0) / (dstX1 - dstX0), (srcY1 - srcY0) / (dstY1 - dstY0) };
    const float dstOrigin[4] = { dstX0, dstY0, 0.0f, 0.0f };
    bgfx::setUniform(s_uCopyMap, map);
    bgfx::setUniform(s_uCopyDst, dstOrigin);
    bgfx::setTexture(0, s_sCopySrc, src, BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA);
    bgfx::submit(view, s_copyProgram);
    s_viewOpen = false;
}

} // namespace

/* ------------------------------------------------------------------------ */
/* GfxRenderingAPI                                                           */
/* ------------------------------------------------------------------------ */

static const char *gfx_bgfx_get_name(void)
{
    return "bgfx (OpenGL)";
}

static int gfx_bgfx_get_max_texture_size(void)
{
    const bgfx::Caps *caps = bgfx::getCaps();
    return caps ? (int)caps->limits.maxTextureSize : 4096;
}

static struct GfxClipParameters gfx_bgfx_get_clip_parameters(void)
{
    return { false, s_fbs[s_currentFb].invert_y };
}

static void gfx_bgfx_unload_shader(struct ShaderProgram *old_prg)
{
    (void)old_prg;
}

/* gfx_opengl_set_uniforms: the values a program draws with until it is
 * loaded again. */
static void snapshotProgramUniforms(BgfxProgram &prg)
{
    prg.frameU[0] = (float)s_frameCount;
    prg.frameU[1] = s_noiseScale;
    prg.frameU[2] = s_unitLinear[0] ? 1.0f : 0.0f;
    prg.frameU[3] = s_unitLinear[1] ? 1.0f : 0.0f;
}

static void gfx_bgfx_load_shader(struct ShaderProgram *new_prg)
{
    s_program = reinterpret_cast<BgfxProgram *>(new_prg);
    if (s_program) {
        snapshotProgramUniforms(*s_program);
    }
}

static struct ShaderProgram *gfx_bgfx_create_and_load_new_shader(uint64_t shader_id0, uint32_t shader_id1)
{
    struct CCFeatures cc = {};
    gfx_cc_get_features(shader_id0, shader_id1, &cc);

    BgfxProgram &prg = s_programs[std::make_pair(shader_id0, shader_id1)];
    prg.id0 = shader_id0;
    prg.id1 = shader_id1;
    prg.num_inputs = (uint8_t)cc.num_inputs;
    prg.used_textures[0] = cc.used_textures[0];
    prg.used_textures[1] = cc.used_textures[1];

    /* The legacy vertex layout for this combiner (gfx_pc.cpp, the buf_vbo
     * packing in gfx_sp_tri1), mapped onto vs_fast3d.sc's attributes. */
    static const bgfx::Attrib::Enum kInputAttribs[7] = {
        bgfx::Attrib::Color2, bgfx::Attrib::Color3, bgfx::Attrib::TexCoord6, bgfx::Attrib::TexCoord7,
        bgfx::Attrib::Normal, bgfx::Attrib::Tangent, bgfx::Attrib::Bitangent,
    };
    prg.layout.begin();
    prg.layout.add(bgfx::Attrib::Position, 4, bgfx::AttribType::Float);
    for (int i = 0; i < 2; i++) {
        if (!cc.used_textures[i]) continue;
        prg.layout.add(i == 0 ? bgfx::Attrib::TexCoord0 : bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float);
        if (cc.clamp[i][0]) {
            prg.layout.add(i == 0 ? bgfx::Attrib::TexCoord2 : bgfx::Attrib::TexCoord4, 1, bgfx::AttribType::Float);
        }
        if (cc.clamp[i][1]) {
            prg.layout.add(i == 0 ? bgfx::Attrib::TexCoord3 : bgfx::Attrib::TexCoord5, 1, bgfx::AttribType::Float);
        }
    }
    if (cc.opt_fog) {
        prg.layout.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float);
    }
    if (cc.opt_grayscale) {
        prg.layout.add(bgfx::Attrib::Color1, 4, bgfx::AttribType::Float);
    }
    for (int i = 0; i < cc.num_inputs && i < 7; i++) {
        prg.layout.add(kInputAttribs[i], cc.opt_alpha ? 4 : 3, bgfx::AttribType::Float);
    }
    prg.layout.end();

    for (int cyc = 0; cyc < 2; cyc++) {
        for (int ch = 0; ch < 2; ch++) {
            for (int k = 0; k < 4; k++) {
                prg.sel[cyc * 2 + ch][k] = (float)cc.c[cyc][ch][k];
            }
            prg.shape[cyc * 2 + ch] = (float)ccShape(cc.c[cyc][ch], cc.do_single[cyc][ch],
                                                     cc.do_multiply[cyc][ch], cc.do_mix[cyc][ch]);
        }
    }
    prg.flags = (shader_id1 & 0x3fffu)
              | (cc.used_textures[0] ? (1u << 14) : 0)
              | (cc.used_textures[1] ? (1u << 15) : 0)
              /* The GL backend writes three-point filtering into the program
               * text when it creates it; a filter change clears all programs
               * (gfx_set_texture_filter). */
              | (s_filterMode == FILTER_THREE_POINT ? (1u << 16) : 0);

    /* gfx_opengl_create_and_load_new_shader ends in gfx_opengl_load_shader. */
    s_program = &prg;
    snapshotProgramUniforms(prg);
    return reinterpret_cast<struct ShaderProgram *>(&prg);
}

static struct ShaderProgram *gfx_bgfx_lookup_shader(uint64_t shader_id0, uint32_t shader_id1)
{
    auto it = s_programs.find(std::make_pair(shader_id0, shader_id1));
    return it == s_programs.end() ? nullptr : reinterpret_cast<struct ShaderProgram *>(&it->second);
}

static void gfx_bgfx_shader_get_info(struct ShaderProgram *p, uint8_t *num_inputs, bool used_textures[2])
{
    const BgfxProgram *prg = reinterpret_cast<const BgfxProgram *>(p);
    *num_inputs = prg->num_inputs;
    used_textures[0] = prg->used_textures[0];
    used_textures[1] = prg->used_textures[1];
}

static void gfx_bgfx_clear_shaders(void)
{
    s_program = nullptr;
    s_programs.clear();
}

static uint32_t gfx_bgfx_new_texture(void)
{
    uint32_t id = tagTex(s_nextTexture++, GFX_BGFX_TEXID_KIND_FAST3D_TEX);
    s_textures[id] = BgfxTex();
    return id;
}

static void gfx_bgfx_delete_texture(uint32_t texID)
{
    auto it = s_textures.find(texID);
    if (it == s_textures.end()) return;
    if (bgfx::isValid(it->second.handle)) bgfx::destroy(it->second.handle);
    s_textures.erase(it);
}

static void gfx_bgfx_select_texture(int tile, uint32_t texture_id, bool linear_filter)
{
    s_activeUnit = tile;
    s_units[tile].is_fb = false;
    s_units[tile].id = texture_id;
    s_unitLinear[tile] = linear_filter;
}

static void gfx_bgfx_upload_texture(const uint8_t *rgba32_buf, uint32_t width, uint32_t height)
{
    const UnitBinding &b = s_units[s_activeUnit];
    if (b.is_fb) return; /* glTexImage2D on a framebuffer texture: not done by fast3d */
    auto it = s_textures.find(b.id);
    if (it == s_textures.end()) return;
    BgfxTex &tex = it->second;
    if (bgfx::isValid(tex.handle)) bgfx::destroy(tex.handle);
    const uint32_t size = width * height * 4;
    tex.rgba.assign(rgba32_buf, rgba32_buf + size);
    tex.width = (uint16_t)width;
    tex.height = (uint16_t)height;
    tex.handle = bgfx::createTexture2D((uint16_t)width, (uint16_t)height, false, 1, bgfx::TextureFormat::RGBA8,
                                       BGFX_TEXTURE_NONE, bgfx::copy(rgba32_buf, size));
}

static void gfx_bgfx_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt)
{
    s_activeUnit = tile;
    const bool linear = linear_filter && s_filterMode == FILTER_LINEAR;
    setUnitSampler(tile, filterFlags(linear) | wrapFlags(cms, true) | wrapFlags(cmt, false));
}

static void gfx_bgfx_set_depth_mode(bool depth_test, bool depth_update, bool depth_compare, bool depth_source_prim,
                                    uint16_t zmode)
{
    if (!depth_test) {
        /* GL: depth test off, so no depth test and no depth writes. */
        s_depthState = 0;
        return;
    }
    uint64_t state = depth_update ? BGFX_STATE_WRITE_Z : 0;
    if (depth_compare) {
        switch (zmode) {
            case ZMODE_INTER:
                state |= BGFX_STATE_DEPTH_TEST_LEQUAL;
                break;
            case ZMODE_OPA:
            case ZMODE_XLU:
                state |= depth_source_prim ? BGFX_STATE_DEPTH_TEST_LEQUAL : BGFX_STATE_DEPTH_TEST_LESS;
                break;
            case ZMODE_DEC:
                state |= BGFX_STATE_DEPTH_TEST_LEQUAL | PD_BGFX_STATE_POLYGON_OFFSET;
                break;
        }
    } else {
        state |= BGFX_STATE_DEPTH_TEST_ALWAYS;
    }
    s_depthState = state;
}

static void gfx_bgfx_set_depth_range(float znear, float zfar)
{
    /* fast3d never calls this; bgfx has no depth range state. */
    if ((znear != 0.0f || zfar != 1.0f) && !s_loggedDepthRange) {
        s_loggedDepthRange = true;
        sysLogPrintf(LOG_WARNING, "bgfx: depth range %f..%f requested; not supported, using 0..1", znear, zfar);
    }
}

static void gfx_bgfx_set_viewport(int x, int y, int width, int height)
{
    if (s_viewport[0] != x || s_viewport[1] != y || s_viewport[2] != width || s_viewport[3] != height) {
        s_viewport[0] = x;
        s_viewport[1] = y;
        s_viewport[2] = width;
        s_viewport[3] = height;
        s_viewOpen = false;
    }
}

static void gfx_bgfx_set_scissor(int x, int y, int width, int height)
{
    /* glScissor: the box only; the test stays as it is. */
    s_scissor[0] = x;
    s_scissor[1] = y;
    s_scissor[2] = width;
    s_scissor[3] = height;
}

static void gfx_bgfx_set_use_alpha(bool use_alpha, bool modulate, bool additive)
{
    if (!use_alpha) {
        s_blendState = 0;
    } else if (additive) {
        s_blendState = BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
    } else if (modulate) {
        s_blendState = BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR, BGFX_STATE_BLEND_ZERO);
    } else {
        s_blendState = BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    }
}

/* The GL scissor test as fast3d left it: while it is on, the next submit is
 * clipped to the scissor box. Returns false when the box is empty, where GL
 * draws nothing. */
static bool applyScissor(void)
{
    if (!s_scissorSet) return true;
    const int targetH = (int)s_fbs[s_currentFb].height;
    int x0 = s_scissor[0];
    int t0 = targetH - s_scissor[1] - s_scissor[3];
    int x1 = x0 + s_scissor[2];
    int t1 = t0 + s_scissor[3];
    x0 = std::max(0, x0);
    t0 = std::max(0, t0);
    x1 = std::min(x1, 0xffff);
    t1 = std::min(t1, 0xffff);
    if (x1 <= x0 || t1 <= t0) return false;
    const int rect[4] = { x0, t0, x1 - x0, t1 - t0 };
    if (s_cachedScissorValid && memcmp(rect, s_cachedScissor, sizeof(rect)) == 0) {
        bgfx::setScissor(s_cachedScissorIdx);
    } else {
        s_cachedScissorIdx = bgfx::setScissor((uint16_t)rect[0], (uint16_t)rect[1], (uint16_t)rect[2],
                                              (uint16_t)rect[3]);
        memcpy(s_cachedScissor, rect, sizeof(rect));
        s_cachedScissorValid = true;
    }
    return true;
}

static void gfx_bgfx_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris)
{
    if (!s_program || buf_vbo_num_tris == 0) return;
    const BgfxProgram &prg = *s_program;
    const uint32_t numVerts = (uint32_t)(3 * buf_vbo_num_tris);
    if (prg.layout.getStride() * numVerts != sizeof(float) * buf_vbo_len) {
        sysLogPrintf(LOG_ERROR, "bgfx: vertex data (%zu floats) does not match the combiner layout (%u bytes x %u)",
                     buf_vbo_len, (unsigned)prg.layout.getStride(), numVerts);
        return;
    }
    if (bgfx::getAvailTransientVertexBuffer(numVerts, prg.layout) < numVerts) {
        if (!s_loggedTransient) {
            s_loggedTransient = true;
            sysLogPrintf(LOG_ERROR, "bgfx: transient vertex buffer exhausted; draws dropped this frame");
        }
        return;
    }

    ensureDrawView();

    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, numVerts, prg.layout);
    if (!bgfx::isValid(tvb.layoutHandle)) {
        /* Out of vertex layout handles (BGFX_CONFIG_MAX_VERTEX_LAYOUTS): bgfx
         * keeps one per distinct transient layout. Submitting would make the
         * renderer index past its layout table. */
        if (!s_loggedLayout) {
            s_loggedLayout = true;
            sysLogPrintf(LOG_ERROR, "bgfx: no vertex layout handle left; combiner draws dropped");
        }
        return;
    }
    memcpy(tvb.data, buf_vbo, sizeof(float) * buf_vbo_len);

    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA | s_depthState | s_blendState;
    /* B-253 debug cull toggle, main framebuffer only (gfx_opengl_apply_debug_state). */
    if (s_currentFb == 0) {
        const int cull = gfxDebugCullModeGet();
        if (cull == 1) state |= BGFX_STATE_CULL_CW;
        else if (cull == 2) state |= BGFX_STATE_CULL_CCW;
    }

    if (!applyScissor()) return; /* empty scissor: GL draws nothing */

    for (int i = 0; i < 2; i++) {
        if (!prg.used_textures[i]) continue;
        uint32_t sampler = 0;
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        unitTexture(i, &sampler, &handle);
        if (bgfx::isValid(handle)) {
            bgfx::setTexture((uint8_t)i, i == 0 ? s_sTex0 : s_sTex1, handle, sampler);
        }
    }

    const float flags[4] = { (float)prg.flags, 0.0f, 0.0f, 0.0f };
    bgfx::setUniform(s_uSel, prg.sel, 4);
    bgfx::setUniform(s_uShape, prg.shape);
    bgfx::setUniform(s_uFlags, flags);
    bgfx::setUniform(s_uFrame, prg.frameU);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setState(state);
    bgfx::submit(s_view, s_fast3dProgram);
}

static void gfx_bgfx_init(void)
{
    /* Raw GL helpers that stay on the shared context in bgfx mode (the decal
     * polygon offset hook, the smoke screenshot, the GPU swarm) use glad.
     * gfxBgfxSelectRenderingApi has loaded it already. Loading it again
     * would also reset the extension flags gfx_opengl_init_extensions
     * patches (depth clamp, mirror clamp), which in parity mode would change
     * what the classic renderer draws. */
    if (glad_glGetString == NULL && !gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        sysFatalError("bgfx: could not load OpenGL entry points");
    }

    SDL_Window *window = SDL_GL_GetCurrentWindow();
    SDL_GLContext mainContext = SDL_GL_GetCurrentContext();
    SDL_GLContext bgfxContext = mainContext;
    if (s_tee) {
        /* Renderer parity mode (gfx_tee.cpp): bgfx gets its own GL context on
         * the same window, so the classic GL backend's state is untouched.
         * SDL makes the new context current; the main one comes back below. */
        bgfxContext = SDL_GL_CreateContext(window);
        if (!bgfxContext) {
            sysFatalError("bgfx: could not create the parity mode GL context: %s", SDL_GetError());
        }
        s_teeWindow = window;
        s_teeMainContext = mainContext;
        s_teeBgfxContext = bgfxContext;
    }

    bgfx::renderFrame(); /* before init: bgfx renders on this thread */

    bgfx::Init init;
    init.type = bgfx::RendererType::OpenGL;
    init.vendorId = BGFX_PCI_ID_NONE;
    init.platformData.nwh = nullptr;      /* no window: bgfx never swaps or switches contexts */
    init.platformData.context = bgfxContext;
    init.resolution.width = s_windowWidth;
    init.resolution.height = s_windowHeight;
    init.resolution.reset = BGFX_RESET_DEPTH_CLAMP;
    init.limits.maxTransientVbSize = 64u << 20;
    init.limits.maxTransientIbSize = 8u << 20;
    init.debug = false;
    init.profile = false;
    init.callback = &s_callback;
    if (!bgfx::init(init)) {
        sysFatalError("bgfx: init failed on the OpenGL context");
    }
    s_resetWidth = s_windowWidth;
    s_resetHeight = s_windowHeight;
    s_resetFlags = BGFX_RESET_DEPTH_CLAMP;

    const bgfx::Caps *caps = bgfx::getCaps();
    sysLogPrintf(LOG_NOTE, "bgfx: renderer %s, vendor 0x%04x device 0x%04x, max texture %u",
                 bgfx::getRendererName(caps->rendererType), caps->vendorId, caps->deviceId,
                 caps->limits.maxTextureSize);

    /* Uniforms first. For GLSL 4.30 shaderc stores no uniform table in the
     * shader binary, so createShader registers none, and bgfx's GL renderer
     * binds a program's uniforms by name only when the program is created
     * (ProgramGL::init). A uniform created after its program is never set,
     * and its samplers all read texture unit 0. */
    s_uSel = bgfx::createUniform("u_ccSel", bgfx::UniformType::Vec4, 4);
    s_uShape = bgfx::createUniform("u_ccShape", bgfx::UniformType::Vec4);
    s_uFlags = bgfx::createUniform("u_ccFlags", bgfx::UniformType::Vec4);
    s_uFrame = bgfx::createUniform("u_ccFrame", bgfx::UniformType::Vec4);
    s_sTex0 = bgfx::createUniform("s_tex0", bgfx::UniformType::Sampler);
    s_sTex1 = bgfx::createUniform("s_tex1", bgfx::UniformType::Sampler);
    s_uCopyMap = bgfx::createUniform("u_copyMap", bgfx::UniformType::Vec4);
    s_uCopyDst = bgfx::createUniform("u_copyDst", bgfx::UniformType::Vec4);
    s_sCopySrc = bgfx::createUniform("s_copySrc", bgfx::UniformType::Sampler);
    s_copyLayout.begin().add(bgfx::Attrib::Position, 4, bgfx::AttribType::Float).end();

    s_fast3dProgram = bgfx::createProgram(loadShader(vs_fast3d_glsl, sizeof(vs_fast3d_glsl), "vs_fast3d"),
                                          loadShader(fs_fast3d_glsl, sizeof(fs_fast3d_glsl), "fs_fast3d"), true);
    s_copyProgram = bgfx::createProgram(loadShader(vs_pdcopy_glsl, sizeof(vs_pdcopy_glsl), "vs_pdcopy"),
                                        loadShader(fs_pdcopy_glsl, sizeof(fs_pdcopy_glsl), "fs_pdcopy"), true);
    if (!bgfx::isValid(s_fast3dProgram) || !bgfx::isValid(s_copyProgram)) {
        sysFatalError("bgfx: could not link the fast3d programs");
    }
    {
        bgfx::Attachment sink[2];
        sink[0].init(bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT));
        sink[1].init(bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::D24S8,
                                           BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY));
        s_sinkFb = bgfx::createFrameBuffer(2, sink, true);
        if (!bgfx::isValid(s_sinkFb)) {
            sysFatalError("bgfx: could not create a 1x1 frame buffer");
        }
    }

    s_fbs.resize(1); /* framebuffer 0: the game's screen */
    s_fbs[0].has_depth = true;
    s_fbs[0].sampler = 0;
    s_active = true;
    s_initialised = true;

    if (s_tee) {
        SDL_GL_MakeCurrent(window, mainContext);
        sysLogPrintf(LOG_NOTE, "bgfx: renderer parity mode: the classic GL renderer presents, bgfx renders the same frames offscreen");
    }
}

static void gfx_bgfx_on_resize(void)
{
}

static void gfx_bgfx_start_frame(void)
{
    /* gfx_opengl_start_frame. View ids restart after bgfx::frame() instead
     * (gfxBgfxSubmitFrame), and the scissor test keeps its GL state. */
    s_frameCount++;
    const uint32_t debug = gfxDebugWireframeGet() ? BGFX_DEBUG_WIREFRAME : BGFX_DEBUG_NONE;
    bgfx::setDebug(debug);
}

static void gfx_bgfx_end_frame(void)
{
}

static void gfx_bgfx_finish_render(void)
{
}

static int gfx_bgfx_create_framebuffer(void)
{
    s_fbs.emplace_back();
    BgfxFb &fb = s_fbs.back();
    fb.sampler = 0; /* GL_LINEAR, GL_REPEAT (gfx_opengl_create_framebuffer) */
    return (int)(s_fbs.size() - 1);
}

static void gfx_bgfx_update_framebuffer_parameters(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level,
                                                   bool opengl_invert_y, bool render_target, bool has_depth_buffer,
                                                   bool can_extract_depth)
{
    (void)render_target;
    (void)can_extract_depth;
    BgfxFb &fb = s_fbs[fb_id];
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    if (!gfx_framebuffers_enabled) {
        has_depth_buffer = false;
    }
    if (fb_id == 0) {
        /* The window. The game draws into an offscreen copy with a depth
         * buffer, like the window's own; bgfx's back buffer gets resized. */
        has_depth_buffer = true;
        msaa_level = 1;
        s_windowWidth = width;
        s_windowHeight = height;
        if (s_initialised && (s_resetWidth != width || s_resetHeight != height)) {
            bgfx::reset(width, height, s_resetFlags);
            s_resetWidth = width;
            s_resetHeight = height;
        }
    }
    fb.width = width;
    fb.height = height;
    fb.msaa = msaa_level;
    fb.invert_y = opengl_invert_y;
    fb.has_depth = has_depth_buffer;
}

static bool gfx_bgfx_start_draw_to_framebuffer(int fb_id, float noise_scale)
{
    if (gfx_framebuffers_enabled && fb_id < (int)s_fbs.size()) {
        if (noise_scale != 0.0f) {
            s_noiseScale = 1.0f / noise_scale;
        }
        if ((size_t)fb_id != s_currentFb) {
            s_viewOpen = false;
        }
        s_currentFb = (size_t)fb_id;
        return true;
    }
    return false;
}

static void gfx_bgfx_clear_framebuffer(bool clear_color, bool clear_depth)
{
    /* gfx_opengl_clear_framebuffer clears with the scissor test off and
     * always ends by turning it on. */
    s_scissorSet = true;
    uint16_t flags = 0;
    if (clear_color) flags |= BGFX_CLEAR_COLOR;
    if (clear_depth && s_fbs[s_currentFb].has_depth) flags |= BGFX_CLEAR_DEPTH;
    if (flags == 0) return;
    const BgfxFb &fb = s_fbs[s_currentFb];
    bgfx::ViewId view = openView(s_currentFb, 0, 0, (int)fb.width, (int)fb.height, flags);
    bgfx::touch(view);
    s_viewOpen = false;
}

static void gfx_bgfx_resolve_msaa_color_buffer(int fb_id_target, int fb_id_source)
{
    if (!gfx_framebuffers_enabled) return;
    /* bgfx resolves a multisampled target into its texture when the next view
     * switches target, so the copy below reads the resolved image. */
    const BgfxFb &src = s_fbs[fb_id_source];
    const BgfxFb &dst = s_fbs[fb_id_target];
    ensureFb((size_t)fb_id_source);
    copyRect((size_t)fb_id_target, src.color, 0.0f, 0.0f, (float)src.width, (float)src.height,
             0.0f, 0.0f, (float)dst.width, (float)dst.height);
    s_scissorSet = true; /* the GL resolve ends with glEnable(GL_SCISSOR_TEST) */
}

static void *gfx_bgfx_get_framebuffer_texture_id(int fb_id)
{
    return (void *)(uintptr_t)tagTex((uint32_t)fb_id, GFX_BGFX_TEXID_KIND_FAST3D_FB);
}

static void gfx_bgfx_select_texture_fb(int fb_id)
{
    s_activeUnit = 0;
    s_units[0].is_fb = true;
    s_units[0].id = (uint32_t)fb_id;
    s_unitLinear[0] = true;
}

static void gfx_bgfx_copy_framebuffer(int fb_dst, int fb_src, int left, int top, bool flip_y, bool use_back)
{
    if (!gfx_framebuffers_enabled || fb_dst >= (int)s_fbs.size() || fb_src >= (int)s_fbs.size()) {
        return;
    }
    const BgfxFb &src = s_fbs[fb_src];
    const BgfxFb &dst = s_fbs[fb_dst];

    float srcX0, srcY0, srcX1, srcY1;
    float dstX0 = 0.0f, dstY0 = 0.0f, dstX1 = (float)dst.width, dstY1 = (float)dst.height;
    if (left >= 0 && top >= 0) {
        srcX0 = (float)left;
        srcY0 = (float)top;
        srcX1 = (float)(left + (int)dst.width);
        srcY1 = (float)(top + (int)dst.height);
    } else {
        srcX0 = 0.0f;
        srcY0 = 0.0f;
        srcX1 = (float)src.width;
        srcY1 = (float)src.height;
    }
    if (flip_y) {
        std::swap(dstY0, dstY1);
    }
    /* The GL copy ends with glEnable(GL_SCISSOR_TEST), even when its blit
     * fails below. */
    s_scissorSet = true;

    if (src.msaa > 1) {
        /* glBlitFramebuffer from a multisampled read framebuffer requires
         * identical source and destination rectangles (GL 4.3 18.3.1); the
         * GL backend's flipped or offset copy fails with
         * GL_INVALID_OPERATION and copies nothing. Match that. */
        const bool same = srcX0 == dstX0 && srcX1 == dstX1 && srcY0 == dstY0 && srcY1 == dstY1;
        if (!same) {
            if (!s_loggedMsaaBlit) {
                s_loggedMsaaBlit = true;
                sysLogPrintf(LOG_WARNING,
                    "bgfx: multisampled framebuffer copy with a flip or offset skipped, as GL rejects it");
            }
            return;
        }
    }
    ensureFb((size_t)fb_src);
    bgfx::TextureHandle srcTex = src.color;
    if (fb_src == 0 && !use_back) {
        /* The GL backend reads GL_FRONT: the frame presented last, with its
         * ImGui. That is the final target (s_finalFb), which this frame's
         * present has not yet overwritten at this point in view order. */
        if (bgfx::isValid(s_finalColor)) {
            srcTex = s_finalColor;
        } else if (!s_loggedFrontRead) {
            s_loggedFrontRead = true;
            sysLogPrintf(LOG_WARNING, "bgfx: front-buffer copy before any frame was presented; copying the current frame");
        }
    }
    copyRect((size_t)fb_dst, srcTex, srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1);
}

static void gfx_bgfx_set_texture_filter(FilteringMode mode)
{
    s_filterMode = mode;
}

static FilteringMode gfx_bgfx_get_texture_filter(void)
{
    return s_filterMode;
}

struct GfxRenderingAPI gfx_bgfx_api = {
    gfx_bgfx_get_name,
    gfx_bgfx_get_max_texture_size,
    gfx_bgfx_get_clip_parameters,
    gfx_bgfx_unload_shader,
    gfx_bgfx_load_shader,
    gfx_bgfx_create_and_load_new_shader,
    gfx_bgfx_lookup_shader,
    gfx_bgfx_shader_get_info,
    gfx_bgfx_clear_shaders,
    gfx_bgfx_new_texture,
    gfx_bgfx_select_texture,
    gfx_bgfx_upload_texture,
    gfx_bgfx_set_sampler_parameters,
    gfx_bgfx_set_depth_mode,
    gfx_bgfx_set_depth_range,
    gfx_bgfx_set_viewport,
    gfx_bgfx_set_scissor,
    gfx_bgfx_set_use_alpha,
    gfx_bgfx_draw_triangles,
    gfx_bgfx_init,
    gfx_bgfx_on_resize,
    gfx_bgfx_start_frame,
    gfx_bgfx_end_frame,
    gfx_bgfx_finish_render,
    gfx_bgfx_create_framebuffer,
    gfx_bgfx_update_framebuffer_parameters,
    gfx_bgfx_start_draw_to_framebuffer,
    gfx_bgfx_copy_framebuffer,
    gfx_bgfx_clear_framebuffer,
    gfx_bgfx_resolve_msaa_color_buffer,
    gfx_bgfx_get_framebuffer_texture_id,
    gfx_bgfx_select_texture_fb,
    gfx_bgfx_delete_texture,
    gfx_bgfx_set_texture_filter,
    gfx_bgfx_get_texture_filter,
};

/* ------------------------------------------------------------------------ */
/* pdgui textures (the table is with the other state at the top)             */
/* ------------------------------------------------------------------------ */

extern "C" uint32_t gfxBgfxPdguiTexCreate(int width, int height, const uint8_t *rgba, int linear, int clampEdge)
{
    if (!s_active || width <= 0 || height <= 0 || width > 0xffff || height > 0xffff) return 0;
    PdguiTex tex;
    tex.width = (uint16_t)width;
    tex.height = (uint16_t)height;
    tex.sampler = filterFlags(linear != 0) | (clampEdge ? BGFX_SAMPLER_UVW_CLAMP : 0);
    /* Created without data so it stays updatable (bgfx makes textures with
     * initial data immutable). */
    tex.handle = bgfx::createTexture2D(tex.width, tex.height, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE);
    if (!bgfx::isValid(tex.handle)) return 0;
    if (rgba) {
        const uint32_t size = (uint32_t)width * (uint32_t)height * 4;
        bgfx::updateTexture2D(tex.handle, 0, 0, 0, 0, tex.width, tex.height, bgfx::copy(rgba, size));
    }
    const uint32_t id = tagTex(s_nextPdguiTexture++, GFX_BGFX_TEXID_KIND_PDGUI);
    s_pdguiTextures[id] = tex;
    return id;
}

extern "C" void gfxBgfxPdguiTexUpdate(uint32_t id, int x, int y, int width, int height, const uint8_t *rgba,
                                      int rowLengthPixels)
{
    auto it = s_pdguiTextures.find(id);
    if (it == s_pdguiTextures.end() || !rgba || width <= 0 || height <= 0) return;
    const int rowLength = rowLengthPixels > 0 ? rowLengthPixels : width;
    std::vector<uint8_t> packed((size_t)width * height * 4);
    for (int row = 0; row < height; row++) {
        memcpy(&packed[(size_t)row * width * 4], rgba + (size_t)row * rowLength * 4, (size_t)width * 4);
    }
    bgfx::updateTexture2D(it->second.handle, 0, 0, (uint16_t)x, (uint16_t)y, (uint16_t)width, (uint16_t)height,
                          bgfx::copy(packed.data(), (uint32_t)packed.size()));
}

extern "C" void gfxBgfxPdguiTexSetSampler(uint32_t id, int linear, int clampEdge)
{
    auto it = s_pdguiTextures.find(id);
    if (it == s_pdguiTextures.end()) return;
    it->second.sampler = filterFlags(linear != 0) | (clampEdge ? BGFX_SAMPLER_UVW_CLAMP : 0);
}

extern "C" void gfxBgfxPdguiTexDestroy(uint32_t id)
{
    auto it = s_pdguiTextures.find(id);
    if (it == s_pdguiTextures.end()) return;
    if (bgfx::isValid(it->second.handle)) bgfx::destroy(it->second.handle);
    s_pdguiTextures.erase(it);
}

/* Copies fast3d framebuffer fbId's colour into a new pdgui texture, in frame
 * order (after everything drawn into it so far this frame), like the GL
 * backend's glGetTexImage + glTexImage2D bake. */
extern "C" uint32_t gfxBgfxPdguiTexFromFramebuffer(int fbId, int linear, int clampEdge)
{
    if (!s_active || fbId < 0 || fbId >= (int)s_fbs.size()) return 0;
    ensureFb((size_t)fbId);
    const BgfxFb &fb = s_fbs[fbId];
    PdguiTex tex;
    tex.width = (uint16_t)fb.width;
    tex.height = (uint16_t)fb.height;
    tex.sampler = filterFlags(linear != 0) | (clampEdge ? BGFX_SAMPLER_UVW_CLAMP : 0);
    tex.handle = bgfx::createTexture2D(tex.width, tex.height, false, 1, bgfx::TextureFormat::RGBA8,
                                       BGFX_TEXTURE_BLIT_DST);
    if (!bgfx::isValid(tex.handle)) return 0;
    bgfx::ViewId view = allocView();
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    /* A blit-only view; the 1x1 target keeps it off the window. */
    bgfx::setViewFrameBuffer(view, s_sinkFb);
    bgfx::setViewRect(view, 0, 0, 1, 1);
    bgfx::blit(view, tex.handle, 0, 0, fb.color, 0, 0, tex.width, tex.height);
    bgfx::touch(view);
    s_viewOpen = false;
    const uint32_t id = tagTex(s_nextPdguiTexture++, GFX_BGFX_TEXID_KIND_PDGUI);
    s_pdguiTextures[id] = tex;
    return id;
}

/* ------------------------------------------------------------------------ */
/* C entry points                                                            */
/* ------------------------------------------------------------------------ */

extern "C" int gfxBgfxRequested(void)
{
    const char *renderer = sysArgGetString("--renderer");
    return renderer && SDL_strcasecmp(renderer, "bgfx") == 0;
}

extern "C" struct GfxRenderingAPI *gfxBgfxSelectRenderingApi(struct GfxRenderingAPI *requested)
{
    if (requested != &gfx_bgfx_api && requested != &gfx_tee_api) {
        return requested;
    }
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress) || glGetString == NULL) {
        sysLogPrintf(LOG_WARNING, "bgfx: could not query the OpenGL context; using the classic OpenGL renderer");
        return &gfx_opengl_api;
    }
    const char *version = (const char *)glGetString(GL_VERSION);
    int major = GLVersion.major;
    int minor = GLVersion.minor;
    /* --debug-bgfx-assume-gl <major.minor> treats the context as that
     * version, so the fallback below can be tested on a newer driver
     * (tools/smoke-verify/tests/renderer_bgfx_gl_fallback_smoke.json). */
    const char *assume = sysArgGetString("--debug-bgfx-assume-gl");
    int assumeMajor = 0, assumeMinor = 0;
    if (assume && sscanf(assume, "%d.%d", &assumeMajor, &assumeMinor) == 2) {
        sysLogPrintf(LOG_NOTE, "bgfx: --debug-bgfx-assume-gl: treating OpenGL %d.%d as OpenGL %d.%d",
                     major, minor, assumeMajor, assumeMinor);
        major = assumeMajor;
        minor = assumeMinor;
    }
    if (major < 4 || (major == 4 && minor < 3)) {
        sysLogPrintf(LOG_WARNING,
            "bgfx: the bgfx renderer needs OpenGL 4.3, but this context is OpenGL %d.%d (%s); "
            "using the classic OpenGL renderer",
            major, minor, version ? version : "unknown");
        return &gfx_opengl_api;
    }
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(SDL_GL_GetCurrentWindow(), &w, &h);
    s_windowWidth = (uint32_t)std::max(w, 1);
    s_windowHeight = (uint32_t)std::max(h, 1);
    sysLogPrintf(LOG_NOTE, "bgfx: using the bgfx renderer (OpenGL backend) on OpenGL %d.%d (%s)",
                 GLVersion.major, GLVersion.minor, version ? version : "unknown");
    return requested;
}

extern "C" int gfxBgfxActive(void)
{
    /* bgfx is the presenting backend (--renderer bgfx). */
    return s_active && !s_tee ? 1 : 0;
}

extern "C" int gfxBgfxRunning(void)
{
    /* bgfx is initialised: presenting, or rendering alongside in parity mode. */
    return s_active ? 1 : 0;
}

extern "C" int gfxBgfxTeeActive(void)
{
    return s_active && s_tee ? 1 : 0;
}

extern "C" void gfxBgfxSetTeeMode(int enable)
{
    s_tee = enable != 0;
}

extern "C" void gfxBgfxTeeNoteFramebufferTexture(uint32_t glTexture, int fbId)
{
    if (glTexture != 0) {
        s_teeFbNames[glTexture] = fbId;
    }
}

extern "C" void gfxBgfxRequestParityCapture(void)
{
    if (s_active && s_tee) {
        s_parityRequested = true;
    }
}

extern "C" int gfxBgfxTakeParityPixels(const uint8_t **rgba, int *width, int *height)
{
    if (!s_parityReady) return 0;
    s_parityReady = false;
    *rgba = s_parityPixels.data();
    *width = (int)s_parityWidth;
    *height = (int)s_parityHeight;
    return 1;
}

/* Opens a view on `target` (BGFX_INVALID_HANDLE: the window) covering
 * width x height and copies texture `src` into it pixel for pixel. */
static void submitFullCopy(bgfx::FrameBufferHandle target, bgfx::TextureHandle src, uint32_t width, uint32_t height)
{
    bgfx::ViewId view = allocView();
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    bgfx::setViewFrameBuffer(view, target);
    bgfx::setViewRect(view, 0, 0, (uint16_t)width, (uint16_t)height);
    bgfx::setViewScissor(view, 0, 0, 0, 0);
    bgfx::setViewClear(view, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(view, nullptr, nullptr);
    if (!bgfx::isValid(src) || bgfx::getAvailTransientVertexBuffer(6, s_copyLayout) < 6) {
        bgfx::touch(view);
        return;
    }
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, 6, s_copyLayout);
    const float quad[6][4] = {
        { -1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 0.0f, 1.0f },
        { -1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 0.0f, 1.0f }, { -1.0f, 1.0f, 0.0f, 1.0f },
    };
    memcpy(tvb.data, quad, sizeof(quad));
    const float map[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    const float dst[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    bgfx::setUniform(s_uCopyMap, map);
    bgfx::setUniform(s_uCopyDst, dst);
    bgfx::setTexture(0, s_sCopySrc, src, BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA);
    bgfx::submit(view, s_copyProgram);
}

extern "C" void gfxBgfxResetForOverlay(int width, int height)
{
    if (!s_active) return;
    /* gfx_opengl_reset_for_overlay binds the window and resets state; here
     * the game image is copied from framebuffer 0 into the offscreen final
     * target, where ImGui then draws (see s_finalFb). */
    if (!bgfx::isValid(s_finalFb) || s_finalWidth != (uint32_t)width || s_finalHeight != (uint32_t)height) {
        if (bgfx::isValid(s_finalFb)) bgfx::destroy(s_finalFb); /* and its colour texture */
        if (bgfx::isValid(s_teeReadback)) bgfx::destroy(s_teeReadback);
        s_teeReadback = BGFX_INVALID_HANDLE;
        s_finalColor = bgfx::createTexture2D((uint16_t)width, (uint16_t)height, false, 1,
            bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
        if (s_tee) {
            s_teeReadback = bgfx::createTexture2D((uint16_t)width, (uint16_t)height, false, 1,
                bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        }
        s_finalFb = BGFX_INVALID_HANDLE;
        if (bgfx::isValid(s_finalColor)) {
            s_finalFb = bgfx::createFrameBuffer(1, &s_finalColor, true);
        }
        if (!bgfx::isValid(s_finalFb) || (s_tee && !bgfx::isValid(s_teeReadback))) {
            /* Never fall back to the window: in parity mode it is the window
             * the classic renderer presents. */
            sysFatalError("bgfx: could not create the %dx%d offscreen frame target", width, height);
        }
        s_finalWidth = (uint32_t)width;
        s_finalHeight = (uint32_t)height;
    }
    const bgfx::FrameBufferHandle window = s_finalFb;
    ensureFb(0);
    submitFullCopy(window, s_fbs[0].color, (uint32_t)width, (uint32_t)height);
    s_finalComposed = true;

    /* gfx_opengl_reset_for_overlay: glScissor(0, 0, w, h) and the test off,
     * and that is the state fast3d's next frame starts in. */
    s_scissor[0] = 0;
    s_scissor[1] = 0;
    s_scissor[2] = width;
    s_scissor[3] = height;
    s_scissorSet = false;

    /* The ImGui view opens on first use (gfxBgfxOverlayView), after any
     * pdgui bake made while ImGui builds this frame. */
    s_overlayTarget = window;
    s_overlayWidth = (uint32_t)width;
    s_overlayHeight = (uint32_t)height;
    s_overlayAllocated = false;
    s_viewOpen = false;
}

extern "C" void gfxBgfxSubmitFrame(void)
{
    if (!s_active) return;
    if (!s_tee && s_finalComposed) {
        /* The finished frame (game image and ImGui) goes to the window last. */
        submitFullCopy(BGFX_INVALID_HANDLE, s_finalColor, s_finalWidth, s_finalHeight);
    }
    const bool capture = s_tee && s_parityRequested && bgfx::isValid(s_finalColor)
        && bgfx::isValid(s_teeReadback);
    if (capture) {
        /* After the frame's last view: copy the final image into the
         * read-back texture and read that. Single-threaded, bgfx::frame()
         * renders, blits and then runs the read before it returns. */
        const bgfx::ViewId view = allocView();
        bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
        bgfx::setViewFrameBuffer(view, s_finalFb);
        bgfx::setViewRect(view, 0, 0, (uint16_t)s_finalWidth, (uint16_t)s_finalHeight);
        bgfx::blit(view, s_teeReadback, 0, 0, s_finalColor, 0, 0, (uint16_t)s_finalWidth,
                   (uint16_t)s_finalHeight);
        bgfx::touch(view);
        s_parityWidth = s_finalWidth;
        s_parityHeight = s_finalHeight;
        s_parityPixels.assign((size_t)s_parityWidth * s_parityHeight * 4, 0);
        bgfx::readTexture(s_teeReadback, s_parityPixels.data());
    }
    if (s_tee) SDL_GL_MakeCurrent(s_teeWindow, s_teeBgfxContext);
    bgfx::frame();
    if (s_tee) SDL_GL_MakeCurrent(s_teeWindow, s_teeMainContext);
    /* bgfx's frame boundary: view ids and scissor cache entries start over. */
    s_nextView = 0;
    s_viewOpen = false;
    s_cachedScissorValid = false;
    s_overlayAllocated = false;
    s_finalComposed = false;
    if (capture) {
        s_parityRequested = false;
        s_parityReady = true;
    }
}

extern "C" uint16_t gfxBgfxBeginPass(int x, int y, int width, int height, uint32_t *targetHeight)
{
    bgfx::ViewId view = openView(s_currentFb, x, y, width, height, BGFX_CLEAR_NONE);
    if (targetHeight) *targetHeight = s_fbs[s_currentFb].height;
    s_viewOpen = false;
    return view;
}

extern "C" void gfxBgfxEndPass(void)
{
    s_viewOpen = false;
}

extern "C" int gfxBgfxApplyScissor(void)
{
    return applyScissor() ? 1 : 0;
}

extern "C" uint16_t gfxBgfxOverlayView(uint32_t *targetHeight)
{
    if (!s_overlayAllocated) {
        s_overlayView = allocView();
        bgfx::setViewMode(s_overlayView, bgfx::ViewMode::Sequential);
        bgfx::setViewFrameBuffer(s_overlayView, s_overlayTarget);
        bgfx::setViewRect(s_overlayView, 0, 0, (uint16_t)s_overlayWidth, (uint16_t)s_overlayHeight);
        bgfx::setViewScissor(s_overlayView, 0, 0, 0, 0);
        bgfx::setViewClear(s_overlayView, BGFX_CLEAR_NONE);
        bgfx::setViewTransform(s_overlayView, nullptr, nullptr);
        s_overlayAllocated = true;
        s_viewOpen = false;
    }
    if (targetHeight) *targetHeight = s_overlayHeight;
    return s_overlayView;
}

/* Parity mode: ImGui draws with GL texture names (the classic renderer
 * presents). A framebuffer colour maps to bgfx's own copy of that
 * framebuffer; any other texture is imported once per frame from the GL
 * object, pixels and sampler parameters both, while the main context is
 * current (ImGui renders before the frame is submitted). */
static int teeResolveGlTexture(uint32_t glName, uint16_t *handleIdx, uint32_t *samplerFlags)
{
    auto fbIt = s_teeFbNames.find(glName);
    if (fbIt != s_teeFbNames.end() && fbIt->second >= 0 && (size_t)fbIt->second < s_fbs.size()) {
        ensureFb((size_t)fbIt->second);
        *handleIdx = s_fbs[fbIt->second].color.idx;
        *samplerFlags = s_fbs[fbIt->second].sampler;
        return 1;
    }

    TeeImport &imp = s_teeImports[glName];
    if (imp.frame == s_frameCount && bgfx::isValid(imp.handle)) {
        *handleIdx = imp.handle.idx;
        *samplerFlags = imp.sampler;
        return 1;
    }

    GLint prevActive = GL_TEXTURE0, prevTex = 0, prevPack = 4;
    GLint w = 0, h = 0, minFilter = GL_LINEAR, magFilter = GL_LINEAR, wrapS = GL_REPEAT, wrapT = GL_REPEAT;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGetIntegerv(GL_PACK_ALIGNMENT, &prevPack);
    glBindTexture(GL_TEXTURE_2D, glName);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minFilter);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &magFilter);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &wrapS);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &wrapT);
    std::vector<uint8_t> pixels;
    if (w > 0 && h > 0 && w <= 0xffff && h <= 0xffff) {
        pixels.resize((size_t)w * (size_t)h * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    }
    glPixelStorei(GL_PACK_ALIGNMENT, prevPack);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    glActiveTexture((GLenum)prevActive);
    if (pixels.empty()) {
        return 0;
    }

    if (bgfx::isValid(imp.handle)) {
        bgfx::destroy(imp.handle);
    }
    imp.handle = bgfx::createTexture2D((uint16_t)w, (uint16_t)h, false, 1, bgfx::TextureFormat::RGBA8,
                                       BGFX_TEXTURE_NONE, bgfx::copy(pixels.data(), (uint32_t)pixels.size()));
    uint32_t sampler = 0;
    if (minFilter == GL_NEAREST) sampler |= BGFX_SAMPLER_MIN_POINT;
    if (magFilter == GL_NEAREST) sampler |= BGFX_SAMPLER_MAG_POINT;
    if (wrapS == GL_CLAMP_TO_EDGE) sampler |= BGFX_SAMPLER_U_CLAMP;
    else if (wrapS == GL_MIRRORED_REPEAT) sampler |= BGFX_SAMPLER_U_MIRROR;
    if (wrapT == GL_CLAMP_TO_EDGE) sampler |= BGFX_SAMPLER_V_CLAMP;
    else if (wrapT == GL_MIRRORED_REPEAT) sampler |= BGFX_SAMPLER_V_MIRROR;
    imp.sampler = sampler;
    imp.frame = s_frameCount;
    *handleIdx = imp.handle.idx;
    *samplerFlags = imp.sampler;
    return bgfx::isValid(imp.handle) ? 1 : 0;
}

extern "C" int gfxBgfxResolveTextureId(uint64_t texid, uint16_t *handleIdx, uint32_t *samplerFlags)
{
    if (s_tee) {
        return teeResolveGlTexture((uint32_t)texid, handleIdx, samplerFlags);
    }
    const uint32_t id = (uint32_t)texid;
    const uint32_t kind = id & 7u;
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    uint32_t sampler = 0;
    if (kind == GFX_BGFX_TEXID_KIND_FAST3D_TEX) {
        auto it = s_textures.find(id);
        if (it != s_textures.end()) {
            handle = it->second.handle;
            sampler = it->second.sampler;
        }
    } else if (kind == GFX_BGFX_TEXID_KIND_FAST3D_FB) {
        const uint32_t fb = id >> 3;
        if (fb < s_fbs.size()) {
            ensureFb(fb);
            handle = s_fbs[fb].color;
            sampler = s_fbs[fb].sampler;
        }
    } else if (kind == GFX_BGFX_TEXID_KIND_PDGUI) {
        auto it = s_pdguiTextures.find(id);
        if (it != s_pdguiTextures.end()) {
            handle = it->second.handle;
            sampler = it->second.sampler;
        }
    }
    if (!bgfx::isValid(handle)) return 0;
    *handleIdx = handle.idx;
    *samplerFlags = sampler;
    return 1;
}

extern "C" int gfxBgfxReadFast3dTexture(uint32_t texid, uint8_t **outRgba, int *outW, int *outH)
{
    auto it = s_textures.find(texid);
    if (it == s_textures.end() || it->second.rgba.empty()) return 0;
    const BgfxTex &tex = it->second;
    uint8_t *copy = (uint8_t *)malloc(tex.rgba.size());
    if (!copy) return 0;
    memcpy(copy, tex.rgba.data(), tex.rgba.size());
    *outRgba = copy;
    *outW = tex.width;
    *outH = tex.height;
    return 1;
}

/* The polygon offset hook the patched bgfx GL renderer calls
 * (gfx_bgfx_ext.h). Same calls as gfx_opengl_set_depth_mode. */
extern "C" void pdBgfxGlPolygonOffset(int enable)
{
    if (enable) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-2, -2);
    } else {
        glDisable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(0, 0);
    }
}
