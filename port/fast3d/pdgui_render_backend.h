#ifndef PDGUI_RENDER_BACKEND_H
#define PDGUI_RENDER_BACKEND_H

/* The ImGui renderer for the active fast3d backend (C++ only). With the
 * classic GL renderer these call imgui_impl_opengl3 exactly as before. */

struct ImDrawData;

void pdguiRenderBackendInit(void);
void pdguiRenderBackendShutdown(void);
void pdguiRenderBackendNewFrame(void);
void pdguiRenderBackendRenderDrawData(ImDrawData *draw_data);
void pdguiRenderBackendRebuildFontsTexture(void);

#endif
