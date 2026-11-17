/*
 * imgui_impl_bgfx.cpp - Dear ImGui renderer on bgfx for the bgfx backend
 * (port/fast3d/gfx_bgfx.cpp, --renderer bgfx).
 *
 * Draws exactly what imgui_impl_opengl3.cpp draws, through bgfx:
 *   - the same vertex data, orthographic matrix and shader arithmetic
 *     (vs_pdimgui.sc, fs_pdimgui.sc);
 *   - glBlendFuncSeparate(SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ONE, ONE_MINUS_SRC_ALPHA),
 *     no culling, no depth, scissor per command with the same integer rounding;
 *   - the viewport (0, 0, fb_width, fb_height) and the font atlas as RGBA32
 *     with linear filtering and clamp-to-edge.
 * Texture ids are the tagged ids from gfx_bgfx.h, resolved at draw time.
 */

#include <stdint.h>
#include <string.h>

#include <bgfx/bgfx.h>

#include "imgui.h"
#include "imgui_impl_bgfx.h"

#include "gfx_bgfx.h"
#include "system.h"

#include "vs_pdimgui.bin.h"
#include "fs_pdimgui.bin.h"

namespace {

bgfx::ProgramHandle s_program = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_uProj = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_sTex = BGFX_INVALID_HANDLE;
bgfx::VertexLayout s_layout;
uint32_t s_fontTex = 0;
bool s_loggedTransient = false;

bgfx::ShaderHandle loadShader(const uint8_t *data, uint32_t size, const char *name)
{
    bgfx::ShaderHandle handle = bgfx::createShader(bgfx::makeRef(data, size));
    if (!bgfx::isValid(handle)) {
        sysFatalError("bgfx: could not create shader %s", name);
    }
    bgfx::setName(handle, name);
    return handle;
}

} // namespace

bool ImGui_ImplBgfx_Init(void)
{
    ImGuiIO &io = ImGui::GetIO();
    /* In parity mode the GL renderer owns the ImGui backend fields and the
     * font atlas; this side draws the same data with GL texture names. */
    const bool parity = gfxBgfxTeeActive() != 0;
    if (!parity) {
        io.BackendRendererName = "imgui_impl_bgfx";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    }

    s_program = bgfx::createProgram(loadShader(vs_pdimgui_glsl, sizeof(vs_pdimgui_glsl), "vs_pdimgui"),
                                    loadShader(fs_pdimgui_glsl, sizeof(fs_pdimgui_glsl), "fs_pdimgui"), true);
    s_uProj = bgfx::createUniform("u_imguiProj", bgfx::UniformType::Mat4);
    s_sTex = bgfx::createUniform("s_imguiTex", bgfx::UniformType::Sampler);
    s_layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    if (s_layout.getStride() != sizeof(ImDrawVert)) {
        sysFatalError("bgfx: ImDrawVert layout mismatch (%u vs %u)", (unsigned)s_layout.getStride(),
                      (unsigned)sizeof(ImDrawVert));
    }
    return parity ? true : ImGui_ImplBgfx_CreateFontsTexture();
}

void ImGui_ImplBgfx_Shutdown(void)
{
    if (!gfxBgfxTeeActive()) {
        ImGui_ImplBgfx_DestroyFontsTexture();
    }
    if (bgfx::isValid(s_uProj)) bgfx::destroy(s_uProj);
    if (bgfx::isValid(s_sTex)) bgfx::destroy(s_sTex);
    if (bgfx::isValid(s_program)) bgfx::destroy(s_program);
    s_uProj = BGFX_INVALID_HANDLE;
    s_sTex = BGFX_INVALID_HANDLE;
    s_program = BGFX_INVALID_HANDLE;
    if (!gfxBgfxTeeActive()) {
        ImGuiIO &io = ImGui::GetIO();
        io.BackendRendererName = nullptr;
    }
}

void ImGui_ImplBgfx_NewFrame(void)
{
    if (s_fontTex == 0) {
        ImGui_ImplBgfx_CreateFontsTexture();
    }
}

bool ImGui_ImplBgfx_CreateFontsTexture(void)
{
    ImGuiIO &io = ImGui::GetIO();
    unsigned char *pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    s_fontTex = gfxBgfxPdguiTexCreate(width, height, pixels, 1, 1);
    io.Fonts->SetTexID((ImTextureID)(uintptr_t)s_fontTex);
    return s_fontTex != 0;
}

void ImGui_ImplBgfx_DestroyFontsTexture(void)
{
    if (s_fontTex != 0) {
        gfxBgfxPdguiTexDestroy(s_fontTex);
        ImGui::GetIO().Fonts->SetTexID(0);
        s_fontTex = 0;
    }
}

void ImGui_ImplBgfx_RenderDrawData(ImDrawData *draw_data)
{
    const int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    const int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0 || !bgfx::isValid(s_program)) {
        return;
    }

    uint32_t targetHeight = 1;
    const bgfx::ViewId view = gfxBgfxOverlayView(&targetHeight);

    /* glViewport(0, 0, fb_width, fb_height) in GL window coordinates. */
    const int top = (int)targetHeight - fb_height;
    bgfx::setViewRect(view, 0, (uint16_t)(top > 0 ? top : 0), (uint16_t)fb_width, (uint16_t)fb_height);

    const float L = draw_data->DisplayPos.x;
    const float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    const float T = draw_data->DisplayPos.y;
    const float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
    const float ortho_projection[4][4] = {
        { 2.0f / (R - L), 0.0f, 0.0f, 0.0f },
        { 0.0f, 2.0f / (T - B), 0.0f, 0.0f },
        { 0.0f, 0.0f, -1.0f, 0.0f },
        { (R + L) / (L - R), (T + B) / (B - T), 0.0f, 1.0f },
    };

    const ImVec2 clip_off = draw_data->DisplayPos;
    const ImVec2 clip_scale = draw_data->FramebufferScale;
    const uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA
        | BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                         BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);

    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList *draw_list = draw_data->CmdLists[n];
        const uint32_t numVertices = (uint32_t)draw_list->VtxBuffer.Size;
        const uint32_t numIndices = (uint32_t)draw_list->IdxBuffer.Size;
        if (numVertices == 0 || numIndices == 0) continue;

        if (bgfx::getAvailTransientVertexBuffer(numVertices, s_layout) < numVertices
                || bgfx::getAvailTransientIndexBuffer(numIndices, false) < numIndices) {
            if (!s_loggedTransient) {
                s_loggedTransient = true;
                sysLogPrintf(LOG_ERROR, "bgfx: ImGui transient buffers exhausted; overlay draws dropped");
            }
            break;
        }
        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        bgfx::allocTransientVertexBuffer(&tvb, numVertices, s_layout);
        bgfx::allocTransientIndexBuffer(&tib, numIndices, false);
        memcpy(tvb.data, draw_list->VtxBuffer.Data, numVertices * sizeof(ImDrawVert));
        memcpy(tib.data, draw_list->IdxBuffer.Data, numIndices * sizeof(ImDrawIdx));

        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd *pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr) {
                if (pcmd->UserCallback != ImDrawCallback_ResetRenderState) {
                    pcmd->UserCallback(draw_list, pcmd);
                }
                continue;
            }

            const ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
            const ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
            if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) {
                continue;
            }

            /* glScissor((int)clip_min.x, (int)(fb_height - clip_max.y),
             *           (int)(clip_max.x - clip_min.x), (int)(clip_max.y - clip_min.y))
             * in GL window coordinates, then into bgfx's top-left space and
             * clipped to the target (a scissor outside the window clips
             * nothing more in GL). */
            int sx = (int)clip_min.x;
            int sy = (int)((float)fb_height - clip_max.y);
            int sw = (int)(clip_max.x - clip_min.x);
            int sh = (int)(clip_max.y - clip_min.y);
            int x0 = sx;
            int t0 = (int)targetHeight - sy - sh;
            int x1 = x0 + sw;
            int t1 = t0 + sh;
            if (x0 < 0) x0 = 0;
            if (t0 < 0) t0 = 0;
            if (x1 > 0xffff) x1 = 0xffff;
            if (t1 > 0xffff) t1 = 0xffff;
            if (x1 <= x0 || t1 <= t0) continue;

            uint16_t handleIdx = 0;
            uint32_t sampler = 0;
            if (!gfxBgfxResolveTextureId((uint64_t)pcmd->GetTexID(), &handleIdx, &sampler)) {
                continue;
            }
            bgfx::TextureHandle tex = { handleIdx };

            bgfx::setScissor((uint16_t)x0, (uint16_t)t0, (uint16_t)(x1 - x0), (uint16_t)(t1 - t0));
            bgfx::setUniform(s_uProj, &ortho_projection[0][0]);
            bgfx::setTexture(0, s_sTex, tex, sampler);
            bgfx::setVertexBuffer(0, &tvb, pcmd->VtxOffset, numVertices - pcmd->VtxOffset);
            bgfx::setIndexBuffer(&tib, pcmd->IdxOffset, pcmd->ElemCount);
            bgfx::setState(state);
            bgfx::submit(view, s_program);
        }
    }
}
