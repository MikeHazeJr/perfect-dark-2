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
#include <errno.h>
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

    /* Create mod directory (and parent mods/ if needed) */
    char modDir[256];
    snprintf(modDir, sizeof(modDir), "mods/%s", dirName);

    if (fsCreateDir("mods") < 0 && errno != EEXIST) {
        sysLogPrintf(LOG_WARNING, "Theme editor: cannot create 'mods/' directory (errno %d)", errno);
        return false;
    }
    if (fsCreateDir(modDir) < 0 && errno != EEXIST) {
        sysLogPrintf(LOG_WARNING, "Theme editor: cannot create '%s/' directory (errno %d)", modDir, errno);
        return false;
    }

    /* Write mod.json */
    char modJsonPath[280];
    snprintf(modJsonPath, sizeof(modJsonPath), "%s/mod.json", modDir);

    FILE *f = fsFileOpenWrite(modJsonPath);
    if (!f) {
        sysLogPrintf(LOG_WARNING, "Theme editor: cannot open '%s' for write (errno %d)", modJsonPath, errno);
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
    if (ferror(f)) {
        sysLogPrintf(LOG_WARNING, "Theme editor: write error on '%s'", modJsonPath);
        fclose(f);
        return false;
    }
    fclose(f);

    /* Write theme.json */
    char themeJsonPath[280];
    snprintf(themeJsonPath, sizeof(themeJsonPath), "%s/theme.json", modDir);

    f = fsFileOpenWrite(themeJsonPath);
    if (!f) {
        sysLogPrintf(LOG_WARNING, "Theme editor: cannot open '%s' for write (errno %d)", themeJsonPath, errno);
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
    if (ferror(f)) {
        sysLogPrintf(LOG_WARNING, "Theme editor: write error on '%s'", themeJsonPath);
        fclose(f);
        return false;
    }
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

    /* S197a: pass a p_open pointer so ImGui renders the title-bar X button.
     * If the user clicks X, `open` goes false; we call the hide API once we
     * are past ImGui::End() so the logging path stays consistent. */
    bool open = true;
    if (!ImGui::Begin("Theme Editor##P5", &open, flags)) {
        ImGui::End();
        if (!open) {
            pdguiThemeEditorHide();
        }
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

    /* S197a: if the title-bar X button was pressed this frame, propagate to
     * the hide API so the visibility flag and log line stay in sync. */
    if (!open) {
        pdguiThemeEditorHide();
    }
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

    /* Fullscreen blocking overlay — dims the background AND captures all clicks
     * so the user cannot interact with windows behind the editor.
     * Clicking the overlay dismisses the editor (click-outside-to-close).
     *
     * S197a: the previous version used ImGuiWindowFlags_NoBringToFrontOnFocus,
     * which kept the overlay anchored below the focus stack.  With focus-
     * ordered hit-testing, clicks landing outside the editor's rect but over
     * a window in the focus stack (e.g. the Settings menu that launched the
     * editor) would route to that window rather than to the overlay's
     * InvisibleButton.  Net effect: "clicking outside the editor just changes
     * focus instead of closing" (Mike, S197a report).
     *
     * Fix: call SetNextWindowFocus on the overlay so it is force-lifted to
     * the top of the focus stack each frame, and a second SetNextWindowFocus
     * on the editor window below so the editor stays visually in front.  The
     * overlay is then guaranteed to be the topmost hit-test target everywhere
     * except within the editor's own rect. */
    ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)winW, (float)winH));
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGuiWindowFlags overlayFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##theme_editor_overlay", nullptr, overlayFlags)) {
        /* Draw the dim rect via this window's draw list */
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(0, 0), ImVec2((float)winW, (float)winH),
                          IM_COL32(0, 0, 0, 180));
        /* Invisible button covering the whole screen — catches clicks */
        if (ImGui::InvisibleButton("##theme_editor_dismiss",
                                   ImVec2((float)winW, (float)winH))) {
            pdguiThemeEditorHide();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    /* Only render the editor if still visible (overlay click may have closed it) */
    if (s_Visible) {
        /* S197a: force the editor above the overlay in focus order so it
         * stays visually in front even though the overlay was just focused. */
        ImGui::SetNextWindowFocus();
        renderThemeEditor(winW, winH);
    }
}

} /* extern "C" */
