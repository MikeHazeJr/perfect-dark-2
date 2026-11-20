/*
 * scenario_scene_renderer_bgfx.cpp - the scenario scene renderer's draws with
 * --renderer bgfx (port/fast3d/gfx_bgfx.cpp).
 *
 * scenario_scene_renderer.cpp keeps the scene loading and decides what to
 * draw; with the bgfx backend active it calls these functions instead of its
 * raw-GL path. Same vertices, textures, shader arithmetic (vs_pdscene.sc,
 * fs_pdscene.sc) and state as the GL path: the viewport and scissor fast3d
 * has at the scene's display-list command, depth test LEQUAL, no culling, an
 * opaque pass with depth writes and a blend pass with SRC_ALPHA /
 * ONE_MINUS_SRC_ALPHA and no depth writes. The draws join fast3d's view
 * sequence at the point the GL path draws (gfxBgfxBeginPass).
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
bgfx::UniformHandle s_uSky = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_uFog = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_uFogColor = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_sTex = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_sTex2 = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_sRoomLight = BGFX_INVALID_HANDLE;
bgfx::TextureHandle s_noRoomLight = BGFX_INVALID_HANDLE;
bgfx::VertexLayout s_layout;
bgfx::ViewId s_view = 0;
float s_vp[16];
float s_sky[4];
float s_fog[4];
float s_fogColor[4];
/* This pass's room lighting texture, released at the end of the pass. */
bgfx::TextureHandle s_roomLight = BGFX_INVALID_HANDLE;
bool s_loggedRoomLight = false;

const uint32_t kRoomLightSampler = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;

bool ensureProgram(void)
{
    if (bgfx::isValid(s_program)) {
        return true;
    }
    /* The scene renderer's vertex in the bgfx layout: position, two UV sets,
     * float colour, the sky flag and the room. */
    s_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord2, 1, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord3, 1, bgfx::AttribType::Float)
        .end();
    if (s_layout.getStride() != SCENE_BGFX_VERTEX_FLOATS * sizeof(float)) {
        sysLogPrintf(LOG_ERROR, "SCENARIO.RENDER: bgfx scene layout is %u bytes, expected %u",
                     (unsigned)s_layout.getStride(), (unsigned)(SCENE_BGFX_VERTEX_FLOATS * sizeof(float)));
        return false;
    }
    /* Uniforms before the program: bgfx's GL renderer binds them by name
     * only when the program is created (see gfx_bgfx_init in gfx_bgfx.cpp). */
    s_uVP = bgfx::createUniform("u_sceneVP", bgfx::UniformType::Mat4);
    s_uParams = bgfx::createUniform("u_sceneParams", bgfx::UniformType::Vec4);
    s_uSky = bgfx::createUniform("u_sceneSky", bgfx::UniformType::Vec4);
    s_uFog = bgfx::createUniform("u_sceneFog", bgfx::UniformType::Vec4);
    s_uFogColor = bgfx::createUniform("u_sceneFogColor", bgfx::UniformType::Vec4);
    s_sTex = bgfx::createUniform("s_sceneTex", bgfx::UniformType::Sampler);
    s_sTex2 = bgfx::createUniform("s_sceneTex2", bgfx::UniformType::Sampler);
    s_sRoomLight = bgfx::createUniform("s_sceneRoomLight", bgfx::UniformType::Sampler);
    bgfx::ShaderHandle vs = bgfx::createShader(bgfx::makeRef(vs_pdscene_glsl, sizeof(vs_pdscene_glsl)));
    bgfx::ShaderHandle fs = bgfx::createShader(bgfx::makeRef(fs_pdscene_glsl, sizeof(fs_pdscene_glsl)));
    s_program = bgfx::createProgram(vs, fs, true);
    if (!bgfx::isValid(s_program)) {
        sysLogPrintf(LOG_ERROR, "SCENARIO.RENDER: bgfx scene program unavailable");
        return false;
    }
    /* Bound on the room lighting unit when a pass has no room texels; the
     * shader reads no texel then (room count 0), as the GL path's does. */
    const float zero[8] = {};
    s_noRoomLight = bgfx::createTexture2D(1, 2, false, 1, bgfx::TextureFormat::RGBA32F, BGFX_TEXTURE_NONE,
                                          bgfx::copy(zero, sizeof(zero)));
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

uint32_t vertexBytes(uint32_t vertexCount)
{
    return vertexCount * (uint32_t)(SCENE_BGFX_VERTEX_FLOATS * sizeof(float));
}

} // namespace

extern "C" uint32_t sceneBgfxCreateVertexBuffer(const float *vertices, uint32_t vertexCount)
{
    if (!gfxBgfxRunning() || !ensureProgram() || !vertices || vertexCount == 0) {
        return 0;
    }
    bgfx::DynamicVertexBufferHandle vb = bgfx::createDynamicVertexBuffer(
        bgfx::copy(vertices, vertexBytes(vertexCount)), s_layout);
    return bgfx::isValid(vb) ? (uint32_t)vb.idx + 1 : 0;
}

extern "C" void sceneBgfxUpdateVertices(uint32_t vb, uint32_t firstVertex, const float *vertices, uint32_t vertexCount)
{
    if (vb == 0 || !vertices || vertexCount == 0 || !gfxBgfxRunning()) {
        return;
    }
    /* bgfx applies the update before the frame's first view, as GL's
     * glBufferSubData lands before the scene's first draw of the frame. */
    bgfx::update(bgfx::DynamicVertexBufferHandle{ (uint16_t)(vb - 1) }, firstVertex,
                 bgfx::copy(vertices, vertexBytes(vertexCount)));
}

extern "C" void sceneBgfxDestroyVertexBuffer(uint32_t vb)
{
    if (vb != 0 && gfxBgfxRunning()) {
        bgfx::destroy(bgfx::DynamicVertexBufferHandle{ (uint16_t)(vb - 1) });
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

extern "C" int sceneBgfxBeginPass(const scene_bgfx_pass_t *pass)
{
    if (!pass || !pass->view_projection || !ensureProgram()) {
        return 0;
    }
    /* glViewport(x, y, width, height) on fast3d's current target; each draw
     * takes fast3d's scissor (gfxBgfxApplyScissor), which the GL path leaves
     * as the display list set it. */
    s_view = gfxBgfxBeginPass(pass->x, pass->y, pass->width, pass->height, nullptr);
    memcpy(s_vp, pass->view_projection, sizeof(s_vp));
    s_sky[0] = pass->sky_offset[0];
    s_sky[1] = pass->sky_offset[1];
    s_sky[2] = pass->sky_offset[2];
    s_sky[3] = 0.0f;
    s_fog[0] = pass->fog_enabled ? 1.0f : 0.0f;
    s_fog[1] = pass->fog_mul;
    s_fog[2] = pass->fog_offset;
    s_fog[3] = 0.0f;
    s_fogColor[0] = pass->fog_color[0];
    s_fogColor[1] = pass->fog_color[1];
    s_fogColor[2] = pass->fog_color[2];
    s_fogColor[3] = 0.0f;
    /* The GL path re-uploads one texture before each view's draws. bgfx runs
     * texture updates before the frame's first view, so one texture updated
     * per view would light every view like the last; each pass gets its own,
     * which bgfx releases once the frame has drawn. */
    if (pass->rooms > 0 && pass->rooms <= 0xffff && pass->room_texels) {
        s_roomLight = bgfx::createTexture2D((uint16_t)pass->rooms, 2, false, 1, bgfx::TextureFormat::RGBA32F,
            BGFX_TEXTURE_NONE, bgfx::copy(pass->room_texels, (uint32_t)pass->rooms * 2 * 4 * sizeof(float)));
        if (bgfx::isValid(s_roomLight)) {
            s_fog[3] = (float)pass->rooms;
        } else if (!s_loggedRoomLight) {
            s_loggedRoomLight = true;
            sysLogPrintf(LOG_ERROR, "SCENARIO.RENDER: bgfx room lighting texture unavailable (rooms=%d)",
                         pass->rooms);
        }
    }
    return 1;
}

extern "C" void sceneBgfxDraw(uint32_t vb, uint32_t first, uint32_t count,
                              uint32_t tex, int wrapS, int wrapT,
                              uint32_t tex2, int wrap2S, int wrap2T,
                              int hasTex2, int primaryTexcoord, int secondaryTexcoord,
                              float alphaCutoff, int blendPass, int cull)
{
    if (vb == 0 || tex == 0 || tex2 == 0 || count == 0) {
        return;
    }
    if (!gfxBgfxApplyScissor()) {
        return; /* empty scissor: GL draws nothing */
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
    /* B-1267: on bgfx's OpenGL backend (the one phase 2a runs) the front
     * face is GL_CW and BGFX_STATE_CULL_CW culls it, so CULL_CW removes the
     * triangles that read clockwise in GL's clip space and CULL_CCW the
     * others. A backend whose screen space is flipped against its clip
     * space (Direct3D) wants this looked at again with its first capture. */
    if (cull == 1) {
        state |= BGFX_STATE_CULL_CW;
    } else if (cull == 2) {
        state |= BGFX_STATE_CULL_CCW;
    }
    bgfx::setUniform(s_uVP, s_vp);
    bgfx::setUniform(s_uParams, params);
    bgfx::setUniform(s_uSky, s_sky);
    bgfx::setUniform(s_uFog, s_fog);
    bgfx::setUniform(s_uFogColor, s_fogColor);
    bgfx::setTexture(0, s_sTex, bgfx::TextureHandle{ (uint16_t)(tex - 1) }, wrapFlags(wrapS, wrapT));
    bgfx::setTexture(1, s_sTex2, bgfx::TextureHandle{ (uint16_t)(tex2 - 1) }, wrapFlags(wrap2S, wrap2T));
    if (bgfx::isValid(s_roomLight)) {
        bgfx::setTexture(2, s_sRoomLight, s_roomLight, kRoomLightSampler);
    } else {
        bgfx::setTexture(2, s_sRoomLight, s_noRoomLight, kRoomLightSampler);
    }
    bgfx::setVertexBuffer(0, bgfx::DynamicVertexBufferHandle{ (uint16_t)(vb - 1) }, first, count);
    bgfx::setState(state);
    bgfx::submit(s_view, s_program);
}

extern "C" void sceneBgfxEndPass(void)
{
    if (bgfx::isValid(s_roomLight)) {
        /* Freed after the frame that draws with it. */
        bgfx::destroy(s_roomLight);
        s_roomLight = BGFX_INVALID_HANDLE;
    }
    gfxBgfxEndPass();
}

extern "C" void sceneBgfxClearDepth(void)
{
    /* The GL path clears depth with the scissor off: the whole target. This
     * clears bgfx's depth only; in parity mode the GL path clears its own,
     * at its own point in the frame. */
    gfx_bgfx_api.clear_framebuffer(false, true);
}
