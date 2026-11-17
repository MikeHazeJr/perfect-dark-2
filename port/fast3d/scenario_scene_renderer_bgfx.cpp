/*
 * scenario_scene_renderer_bgfx.cpp - the scenario scene renderer's draws with
 * --renderer bgfx (port/fast3d/gfx_bgfx.cpp).
 *
 * scenario_scene_renderer.cpp keeps the scene loading and decides what to
 * draw; with the bgfx backend active it calls these functions instead of its
 * raw-GL path. Same vertex buffer, textures, shader arithmetic
 * (vs_pdscene.sc, fs_pdscene.sc) and state as the GL path: viewport
 * (0, 0, width, height) on fast3d's current target, depth test LEQUAL, no
 * culling, an opaque pass with depth writes and a blend pass with
 * SRC_ALPHA / ONE_MINUS_SRC_ALPHA and no depth writes. The draws join
 * fast3d's view sequence at the point the GL path draws (gfxBgfxBeginPass).
 *
 * Handles cross the C API as handle index + 1 (0 = none).
 */

#include <stdint.h>
#include <string.h>

#include <bgfx/bgfx.h>

#include "glad/glad.h"

#include "gfx_bgfx.h"
#include "gfx_rendering_api.h"
#include "scenario_scene_renderer_bgfx.h"
#include "system.h"

#include "vs_pdscene.bin.h"
#include "fs_pdscene.bin.h"

namespace {

bgfx::ProgramHandle s_program = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_uVP = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_uParams = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_sTex = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_sTex2 = BGFX_INVALID_HANDLE;
bgfx::VertexLayout s_layout;
bgfx::ViewId s_view = 0;
float s_vp[16];

bool ensureProgram(void)
{
    if (bgfx::isValid(s_program)) {
        return true;
    }
    bgfx::ShaderHandle vs = bgfx::createShader(bgfx::makeRef(vs_pdscene_glsl, sizeof(vs_pdscene_glsl)));
    bgfx::ShaderHandle fs = bgfx::createShader(bgfx::makeRef(fs_pdscene_glsl, sizeof(fs_pdscene_glsl)));
    s_program = bgfx::createProgram(vs, fs, true);
    if (!bgfx::isValid(s_program)) {
        sysLogPrintf(LOG_ERROR, "SCENARIO.RENDER: bgfx scene program unavailable");
        return false;
    }
    s_uVP = bgfx::createUniform("u_sceneVP", bgfx::UniformType::Mat4);
    s_uParams = bgfx::createUniform("u_sceneParams", bgfx::UniformType::Vec4);
    s_sTex = bgfx::createUniform("s_sceneTex", bgfx::UniformType::Sampler);
    s_sTex2 = bgfx::createUniform("s_sceneTex2", bgfx::UniformType::Sampler);
    /* The scene renderer's Vertex: position, two UV sets, float colour. */
    s_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .end();
    return true;
}

/* GL wrap enum (scenario_scene_renderer.cpp glWrap) to bgfx sampler flags,
 * with the GL path's GL_LINEAR filtering. */
uint32_t wrapFlags(int glS, int glT)
{
    uint32_t flags = 0;
    if (glS == GL_CLAMP_TO_EDGE) flags |= BGFX_SAMPLER_U_CLAMP;
    else if (glS == GL_MIRRORED_REPEAT) flags |= BGFX_SAMPLER_U_MIRROR;
    if (glT == GL_CLAMP_TO_EDGE) flags |= BGFX_SAMPLER_V_CLAMP;
    else if (glT == GL_MIRRORED_REPEAT) flags |= BGFX_SAMPLER_V_MIRROR;
    return flags;
}

} // namespace

extern "C" uint32_t sceneBgfxCreateVertexBuffer(const void *vertices, uint32_t vertexCount, uint32_t vertexSize)
{
    if (!gfxBgfxRunning() || !ensureProgram() || !vertices || vertexCount == 0) {
        return 0;
    }
    if (vertexSize != s_layout.getStride()) {
        sysLogPrintf(LOG_ERROR, "SCENARIO.RENDER: bgfx vertex size %u does not match layout %u",
                     vertexSize, (unsigned)s_layout.getStride());
        return 0;
    }
    bgfx::VertexBufferHandle vb = bgfx::createVertexBuffer(
        bgfx::copy(vertices, vertexCount * vertexSize), s_layout);
    return bgfx::isValid(vb) ? (uint32_t)vb.idx + 1 : 0;
}

extern "C" void sceneBgfxDestroyVertexBuffer(uint32_t vb)
{
    if (vb != 0 && gfxBgfxRunning()) {
        bgfx::destroy(bgfx::VertexBufferHandle{ (uint16_t)(vb - 1) });
    }
}

extern "C" uint32_t sceneBgfxCreateTexture(int width, int height, const uint8_t *rgba)
{
    if (!gfxBgfxRunning() || width <= 0 || height <= 0 || width > 0xffff || height > 0xffff || !rgba) {
        return 0;
    }
    bgfx::TextureHandle tex = bgfx::createTexture2D((uint16_t)width, (uint16_t)height, false, 1,
        bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE, bgfx::copy(rgba, (uint32_t)width * (uint32_t)height * 4));
    return bgfx::isValid(tex) ? (uint32_t)tex.idx + 1 : 0;
}

extern "C" void sceneBgfxDestroyTexture(uint32_t tex)
{
    if (tex != 0 && gfxBgfxRunning()) {
        bgfx::destroy(bgfx::TextureHandle{ (uint16_t)(tex - 1) });
    }
}

extern "C" int sceneBgfxBeginPass(int width, int height, const float viewProjection[16])
{
    if (!ensureProgram()) {
        return 0;
    }
    /* glViewport(0, 0, width, height) on fast3d's current target. The GL
     * path leaves the scissor as it found it, the full window at this point
     * of the frame, so these draws are unscissored. */
    s_view = gfxBgfxBeginPass(0, 0, width, height, nullptr);
    memcpy(s_vp, viewProjection, sizeof(s_vp));
    return 1;
}

extern "C" void sceneBgfxDraw(uint32_t vb, uint32_t first, uint32_t count,
                              uint32_t tex, int wrapS, int wrapT,
                              uint32_t tex2, int wrap2S, int wrap2T,
                              int hasTex2, int primaryTexcoord, int secondaryTexcoord,
                              float alphaCutoff, int blendPass)
{
    if (vb == 0 || tex == 0 || tex2 == 0 || count == 0) {
        return;
    }
    const float params[4] = {
        (float)hasTex2, (float)primaryTexcoord, (float)secondaryTexcoord, alphaCutoff
    };
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA | BGFX_STATE_DEPTH_TEST_LEQUAL;
    if (blendPass) {
        state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    } else {
        state |= BGFX_STATE_WRITE_Z;
    }
    bgfx::setUniform(s_uVP, s_vp);
    bgfx::setUniform(s_uParams, params);
    bgfx::setTexture(0, s_sTex, bgfx::TextureHandle{ (uint16_t)(tex - 1) }, wrapFlags(wrapS, wrapT));
    bgfx::setTexture(1, s_sTex2, bgfx::TextureHandle{ (uint16_t)(tex2 - 1) }, wrapFlags(wrap2S, wrap2T));
    bgfx::setVertexBuffer(0, bgfx::VertexBufferHandle{ (uint16_t)(vb - 1) }, first, count);
    bgfx::setState(state);
    bgfx::submit(s_view, s_program);
}

extern "C" void sceneBgfxEndPass(void)
{
    gfxBgfxEndPass();
}

extern "C" struct GfxRenderingAPI *gfx_get_current_rendering_api(void);

extern "C" void sceneBgfxClearDepth(void)
{
    /* The GL path clears depth with the scissor off: the whole target. */
    gfx_get_current_rendering_api()->clear_framebuffer(false, true);
}
