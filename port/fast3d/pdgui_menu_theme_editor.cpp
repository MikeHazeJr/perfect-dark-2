/**
 * pdgui_menu_theme_editor.cpp -- Theme palette editor UI (P5)
 *
 * User-facing color palette editor with live preview and save-as-mod.
 * Follows the Show/Hide/Render pattern used by pdgui_menu_moddinghub.
 *
 * Flow:
 *   1. User opens editor from main menu or modding hub
 *   2. All 15 palette fields displayed as color pickers
 *   3. Changes apply immediately (live preview)
 *   4. "Save as Mod" creates mods/<name>/ with mod.json + theme.json
 *   5. "Reset" reverts to the palette that was active when editor opened
 *   6. "Load Theme" dropdown loads any registered catalog theme
 *
 * IMPORTANT: Do NOT include types.h -- it #defines bool as s32, breaking C++.
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <PR/ultratypes.h>

#include "imgui/imgui.h"
#include "pdgui_menu_theme_editor.h"
#include "pdgui_style.h"
#include "pdgui_theme.h"
#include "pdgui_theme_loader.h"
#include "assetcatalog.h"
#include "config.h"
#include "system.h"
#include "fs.h"

/* =========================================================================
 * State
 * ========================================================================= */

static bool s_Visible = false;

/* Working palette: 15 u32 values in 0xRRGGBBAA format.
 * Modified live as the user edits colors. */
static u32 s_WorkPalette[15];

/* Snapshot of palette when editor was opened (for reset). */
static u32 s_OrigPalette[15];

/* Save dialog state */
static char s_SaveName[64] = "My Theme";
static char s_SaveAuthor[64] = "";
static char s_SaveStatus[128] = "";
static bool s_SaveSuccess = false;

/* =========================================================================
 * Palette field metadata for the UI
 * ========================================================================= */

struct PalFieldInfo {
    const char *label;     /* display name */
    const char *jsonKey;   /* JSON key for theme.json */
    int         index;     /* palette array index (0-14) */
};

static const PalFieldInfo k_Fields[] = {
    { "Border Primary",     "dialog_border1",     0 },
    { "Title Background",   "dialog_titlebg",     1 },
    { "Border Accent",      "dialog_border2",     2 },
    { "Title Text",         "dialog_titlefg",     3 },
    { "Body Background",    "dialog_bodybg",      4 },
    { "(Reserved)",         "unused14",           5 },
    { "Item Text",          "item_unfocused",     6 },
    { "Disabled Text",      "item_disabled",      7 },
    { "Focused Text",       "item_focused_inner", 8 },
    { "Checkbox",           "checkbox_checked",   9 },
    { "Focus Background",   "item_focused_outer", 10 },
    { "List Header BG",     "listgroup_headerbg", 11 },
    { "List Header Text",   "listgroup_headerfg", 12 },
    { "(Reserved 2)",       "unused34",           13 },
    { "(Reserved 3)",       "unused38",           14 },
};
#define NUM_FIELDS 15

/* =========================================================================
 * Color conversion helpers
 * ========================================================================= */

/* 0xRRGGBBAA → ImVec4 (0.0-1.0 RGBA) */
static ImVec4 palToVec4(u32 rgba)
{
    return ImVec4(
        ((rgba >> 24) & 0xFF) / 255.0f,
        ((rgba >> 16) & 0xFF) / 255.0f,
        ((rgba >>  8) & 0xFF) / 255.0f,
        ((rgba >>  0) & 0xFF) / 255.0f
    );
}

/* ImVec4 → 0xRRGGBBAA */
static u32 vec4ToPal(const ImVec4 &c)
{
    u8 r = (u8)(c.x * 255.0f + 0.5f);
    u8 g = (u8)(c.y * 255.0f + 0.5f);
    u8 b = (u8)(c.z * 255.0f + 0.5f);
    u8 a = (u8)(c.w * 255.0f + 0.5f);
    return ((u32)r << 24) | ((u32)g << 16) | ((u32)b << 8) | (u32)a;
}

/* u32 → 8-char hex string (RRGGBBAA) */
static void palToHex(u32 rgba, char *out, int maxlen)
{
    snprintf(out, maxlen, "%08x", rgba);
}

/* =========================================================================
 * Save theme as mod
 * ========================================================================= */

static bool saveThemeAsMod(const char *name, const char *author)
{
    if (!name || !name[0]) return false;

    /* Sanitize name for directory: lowercase, replace spaces with dashes */
    char dirName[64];
    int len = 0;
    for (int i = 0; name[i] && len < 62; i++) {
        char c = name[i];
        if (c == ' ') c = '-';
        else if (c >= 'A' && c <= 'Z') c = c + 32;
        else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            continue;
        dirName[len++] = c;
    }
    dirName[len] = '\0';
    if (!len) return false;

    /* Create mod directory */
    char modDir[256];
    snprintf(modDir, sizeof(modDir), "mods/%s", dirName);

    /* Write mod.json */
    char modJsonPath[280];
    snprintf(modJsonPath, sizeof(modJsonPath), "%s/mod.json", modDir);

    FILE *f = fsFileOpenWrite(modJsonPath);
    if (!f) {
        sysLogPrintf(LOG_WARNING, "Theme editor: cannot write '%s'", modJsonPath);
        return false;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"id\": \"%s\",\n", dirName);
    fprintf(f, "  \"name\": \"%s\",\n", name);
    fprintf(f, "  \"version\": \"1.0\",\n");
    fprintf(f, "  \"author\": \"%s\",\n", author[0] ? author : "User");
    fprintf(f, "  \"description\": \"Custom theme created with Theme Editor\",\n");
    fprintf(f, "  \"base_fallback\": \"base:theme_blue\"\n");
    fprintf(f, "}\n");
    fclose(f);

    /* Write theme.json */
    char themeJsonPath[280];
    snprintf(themeJsonPath, sizeof(themeJsonPath), "%s/theme.json", modDir);

    f = fsFileOpenWrite(themeJsonPath);
    if (!f) {
        sysLogPrintf(LOG_WARNING, "Theme editor: cannot write '%s'", themeJsonPath);
        return false;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"name\": \"%s\",\n", name);
    fprintf(f, "  \"author\": \"%s\",\n", author[0] ? author : "User");
    fprintf(f, "  \"version\": \"1.0\",\n");
    fprintf(f, "  \"palette\": {\n");

    for (int i = 0; i < NUM_FIELDS; i++) {
        char hex[16];
        palToHex(s_WorkPalette[k_Fields[i].index], hex, sizeof(hex));
        fprintf(f, "    \"%s\": \"%s\"%s\n",
                k_Fields[i].jsonKey, hex,
                (i < NUM_FIELDS - 1) ? "," : "");
    }

    fprintf(f, "  },\n");
    fprintf(f, "  \"scanline\": { \"enabled\": true, \"alpha\": 0.8 },\n");
    fprintf(f, "  \"textGlow\": { \"enabled\": true, \"intensity\": 0.6, \"color\": \"0080ffff\" },\n");
    fprintf(f, "  \"soundPack\": \"default\"\n");
    fprintf(f, "}\n");
    fclose(f);

    sysLogPrintf(LOG_NOTE, "Theme editor: saved theme '%s' to %s/", name, modDir);
    return true;
}

/* =========================================================================
 * Render
 * ========================================================================= */

static void renderThemeEditor(s32 winW, s32 winH)
{
    float scale = (float)winH / 720.0f;
    if (scale < 0.5f) scale = 0.5f;

    float editorW = 420.0f * scale;
    float editorH = (float)winH * 0.85f;
    float editorX = ((float)winW - editorW) * 0.5f;
    float editorY = ((float)winH - editorH) * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(editorX, editorY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(editorW, editorH), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove;

    if (!ImGui::Begin("Theme Editor##P5", nullptr, flags)) {
        ImGui::End();
        return;
    }

    /* ---- Load Theme dropdown ---- */
    s32 themeCount = pdguiThemeGetCount();
    if (themeCount > 0 && ImGui::BeginCombo("Load Theme", pdguiThemeGetActiveId())) {
        for (s32 i = 0; i < themeCount; i++) {
            const char *id   = pdguiThemeGetId(i);
            const char *name = pdguiThemeGetName(i);
            bool selected = (strcmp(id, pdguiThemeGetActiveId()) == 0);

            char label[128];
            snprintf(label, sizeof(label), "%s (%s)", name, id);

            if (ImGui::Selectable(label, selected)) {
                pdguiThemeLoadFromCatalog(id);
                /* Refresh working palette from the newly loaded palette */
                const u32 *pal = (const u32 *)pdguiGetActivePaletteRaw();
                if (pal) memcpy(s_WorkPalette, pal, sizeof(s_WorkPalette));
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();

    /* ---- Color pickers ---- */
    ImGui::BeginChild("PaletteScroll", ImVec2(0, -100.0f * scale), true);

    bool changed = false;
    for (int i = 0; i < NUM_FIELDS; i++) {
        int idx = k_Fields[i].index;

        /* Skip reserved fields from the main view */
        if (idx == 5 || idx == 13 || idx == 14) continue;

        ImVec4 col = palToVec4(s_WorkPalette[idx]);
        char pickerId[64];
        snprintf(pickerId, sizeof(pickerId), "##pal_%d", idx);

        ImGui::Text("%s", k_Fields[i].label);
        ImGui::SameLine(200.0f * scale);

        ImGuiColorEditFlags cflags = ImGuiColorEditFlags_AlphaBar
                                   | ImGuiColorEditFlags_AlphaPreviewHalf
                                   | ImGuiColorEditFlags_NoInputs;

        if (ImGui::ColorEdit4(pickerId, &col.x, cflags)) {
            s_WorkPalette[idx] = vec4ToPal(col);
            changed = true;
        }
    }

    ImGui::EndChild();

    /* Apply changes live */
    if (changed) {
        pdguiSetPaletteCustom(s_WorkPalette);
    }

    /* ---- Action buttons ---- */
    ImGui::Separator();

    float btnW = 120.0f * scale;
    float btnH = 28.0f * scale;

    /* Reset button */
    if (ImGui::Button("Reset", ImVec2(btnW, btnH))) {
        memcpy(s_WorkPalette, s_OrigPalette, sizeof(s_WorkPalette));
        pdguiSetPaletteCustom(s_WorkPalette);
        s_SaveStatus[0] = '\0';
    }

    ImGui::SameLine();

    /* Close button */
    if (ImGui::Button("Close", ImVec2(btnW, btnH))) {
        pdguiThemeEditorHide();
    }

    /* ---- Save section ---- */
    ImGui::Spacing();
    ImGui::Text("Save as Mod:");
    ImGui::PushItemWidth(200.0f * scale);
    ImGui::InputText("Name##save", s_SaveName, sizeof(s_SaveName));
    ImGui::InputText("Author##save", s_SaveAuthor, sizeof(s_SaveAuthor));
    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (ImGui::Button("Save", ImVec2(btnW, btnH))) {
        s_SaveSuccess = saveThemeAsMod(s_SaveName, s_SaveAuthor);
        if (s_SaveSuccess) {
            snprintf(s_SaveStatus, sizeof(s_SaveStatus),
                     "Saved to mods/");
        } else {
            snprintf(s_SaveStatus, sizeof(s_SaveStatus),
                     "Save failed — check logs");
        }
    }

    if (s_SaveStatus[0]) {
        ImVec4 statusCol = s_SaveSuccess
            ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
            : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        ImGui::TextColored(statusCol, "%s", s_SaveStatus);
    }

    ImGui::End();
}

/* =========================================================================
 * Public API
 * ========================================================================= */

extern "C" {

void pdguiThemeEditorShow(void)
{
    if (!s_Visible) {
        s_Visible = true;
        s_SaveStatus[0] = '\0';

        /* Snapshot current palette for reset */
        const u32 *pal = (const u32 *)pdguiGetActivePaletteRaw();
        if (pal) {
            memcpy(s_OrigPalette, pal, sizeof(s_OrigPalette));
            memcpy(s_WorkPalette, pal, sizeof(s_WorkPalette));
        }

        sysLogPrintf(LOG_NOTE, "Theme editor: opened");
    }
}

void pdguiThemeEditorHide(void)
{
    if (s_Visible) {
        s_Visible = false;
        sysLogPrintf(LOG_NOTE, "Theme editor: closed");
    }
}

s32 pdguiThemeEditorIsVisible(void)
{
    return s_Visible ? 1 : 0;
}

void pdguiThemeEditorRender(s32 winW, s32 winH)
{
    if (!s_Visible) return;
    renderThemeEditor(winW, winH);
}

} /* extern "C" */
