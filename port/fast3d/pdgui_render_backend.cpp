/*
 * pdgui_render_backend.cpp - picks the ImGui renderer for the active fast3d
 * backend: imgui_impl_opengl3 for the classic GL renderer (unchanged), or
 * imgui_impl_bgfx with --renderer bgfx (port/fast3d/gfx_bgfx.cpp).
 */

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_bgfx.h"

#include "pdgui_render_backend.h"

extern "C" int gfxBgfxActive(void);
extern "C" int gfxBgfxTeeActive(void);

void pdguiRenderBackendInit(void)
{
    if (gfxBgfxActive()) {
        ImGui_ImplBgfx_Init();
    } else {
        ImGui_ImplOpenGL3_Init("#version 130");
        if (gfxBgfxTeeActive()) {
            /* --renderer-parity: ImGui is drawn through both renderers. */
            ImGui_ImplBgfx_Init();
        }
    }
}

void pdguiRenderBackendShutdown(void)
{
    if (gfxBgfxActive()) {
        ImGui_ImplBgfx_Shutdown();
    } else {
        ImGui_ImplOpenGL3_Shutdown();
        if (gfxBgfxTeeActive()) {
            ImGui_ImplBgfx_Shutdown();
        }
    }
}

void pdguiRenderBackendNewFrame(void)
{
    if (gfxBgfxActive()) {
        ImGui_ImplBgfx_NewFrame();
    } else {
        ImGui_ImplOpenGL3_NewFrame();
    }
}

void pdguiRenderBackendRenderDrawData(ImDrawData *draw_data)
{
    if (gfxBgfxActive()) {
        ImGui_ImplBgfx_RenderDrawData(draw_data);
    } else {
        ImGui_ImplOpenGL3_RenderDrawData(draw_data);
        if (gfxBgfxTeeActive()) {
            ImGui_ImplBgfx_RenderDrawData(draw_data);
        }
    }
}

void pdguiRenderBackendRebuildFontsTexture(void)
{
    if (gfxBgfxActive()) {
        ImGui_ImplBgfx_DestroyFontsTexture();
        ImGui_ImplBgfx_CreateFontsTexture();
    } else {
        ImGui_ImplOpenGL3_DestroyFontsTexture();
        ImGui_ImplOpenGL3_CreateFontsTexture();
    }
}
