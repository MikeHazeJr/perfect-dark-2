#ifndef IMGUI_IMPL_BGFX_H
#define IMGUI_IMPL_BGFX_H

/* Dear ImGui renderer on bgfx, used with --renderer bgfx. The same entry
 * points as imgui_impl_opengl3.h; pdgui_render_backend.cpp picks one. */

struct ImDrawData;

bool ImGui_ImplBgfx_Init(void);
void ImGui_ImplBgfx_Shutdown(void);
void ImGui_ImplBgfx_NewFrame(void);
void ImGui_ImplBgfx_RenderDrawData(ImDrawData *draw_data);
bool ImGui_ImplBgfx_CreateFontsTexture(void);
void ImGui_ImplBgfx_DestroyFontsTexture(void);

#endif
