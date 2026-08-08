#include <cstring>
#include <cstdint>

#include <ultra64.h>
#include "assetcatalog.h"
#include "gfx_api.h"
#include "imgui/imgui.h"
#include "pdgui_theme.h"
#include "pdgui_weapon_reticle.h"
#include "system.h"

static char s_CatalogId[CATALOG_ID_LEN];
static f32 s_X;
static f32 s_Y;
static char s_LastMissingId[CATALOG_ID_LEN];

extern "C" void pdguiWeaponReticleQueue(const char *catalog_id, f32 x, f32 y)
{
    if (!catalog_id || !catalog_id[0]) {
        pdguiWeaponReticleClear();
        return;
    }
    std::strncpy(s_CatalogId, catalog_id, sizeof(s_CatalogId) - 1);
    s_CatalogId[sizeof(s_CatalogId) - 1] = '\0';
    s_X = x;
    s_Y = y;
}

extern "C" void pdguiWeaponReticleClear(void)
{
    s_CatalogId[0] = '\0';
}

extern "C" s32 pdguiWeaponReticleIsQueued(void)
{
    return s_CatalogId[0] != '\0';
}

extern "C" void pdguiWeaponReticleRender(s32 window_width, s32 window_height)
{
    if (!s_CatalogId[0] || window_width <= 0 || window_height <= 0) return;

    void *texture = pdguiThemeGetTexture(s_CatalogId);
    u32 source_width = 0;
    u32 source_height = 0;
    if (!texture || !pdguiThemeGetTextureSize(s_CatalogId,
            &source_width, &source_height) || source_width == 0
            || source_height == 0) {
        if (std::strncmp(s_LastMissingId, s_CatalogId,
                sizeof(s_LastMissingId)) != 0) {
            std::strncpy(s_LastMissingId, s_CatalogId,
                sizeof(s_LastMissingId) - 1);
            s_LastMissingId[sizeof(s_LastMissingId) - 1] = '\0';
            sysLogPrintf(LOG_ERROR,
                "PDWEAPON.RETICLE.REJECT: id=%s missing decoded public UI source",
                s_CatalogId);
        }
        return;
    }
    s_LastMissingId[0] = '\0';

    const float viewport_x = (float)gfx_current_game_window_viewport.x;
    const float viewport_y = (float)gfx_current_game_window_viewport.y;
    const float viewport_w = gfx_current_game_window_viewport.width > 0
        ? (float)gfx_current_game_window_viewport.width : (float)window_width;
    const float viewport_h = gfx_current_game_window_viewport.height > 0
        ? (float)gfx_current_game_window_viewport.height : (float)window_height;
    const float center_x = viewport_x + (s_X / 320.0f) * viewport_w;
    const float center_y = viewport_y + (s_Y / 240.0f) * viewport_h;
    const float scale = viewport_h / 240.0f;
    const float draw_w = (float)source_width * scale;
    const float draw_h = (float)source_height * scale;

    ImDrawList *draw = ImGui::GetForegroundDrawList();
    draw->PushClipRect(ImVec2(viewport_x, viewport_y),
        ImVec2(viewport_x + viewport_w, viewport_y + viewport_h), true);
    draw->AddImage((ImTextureID)(uintptr_t)texture,
        ImVec2(center_x - draw_w * 0.5f, center_y - draw_h * 0.5f),
        ImVec2(center_x + draw_w * 0.5f, center_y + draw_h * 0.5f));
    draw->PopClipRect();
}
