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
#include "pdgui_audio.h"

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

    /* 2026-04-11: register the newly written theme with the loader so it
     * appears in this session's Load Theme dropdown AND the Settings UI
     * Theme selector without requiring a restart.  pdguiThemeRegisterModDir
     * is idempotent — if the slug is already registered the call is a
     * no-op.  See pdgui_theme_loader.cpp. */
    pdguiThemeRegisterModDir(dirName, themeJsonPath);

    return true;
}

/* =========================================================================
 * Render
 *
 * 2026-04-11 rewrite — BeginPopupModal instead of a hand-rolled overlay.
 *
 * Prior approach (S197a): drew an explicit "##theme_editor_overlay" window
 * with SetNextWindowFocus + an InvisibleButton spanning the full screen as
 * a click-outside dismiss surface, then a second SetNextWindowFocus on the
 * editor window below so it stayed visually in front.  Three bugs surfaced:
 *
 *   B-130: X button, Close button, and click-outside all stopped working.
 *          Root cause: the "force the overlay to top focus every frame"
 *          hack fought with ImGui's normal focus/z-order management.
 *          When the editor tried to bring itself above the overlay (also
 *          via SetNextWindowFocus), ImGui ended up in a state where the
 *          overlay's InvisibleButton hit-tested as obscured — NOT by the
 *          editor (which was correctly on top), but by a stale focus
 *          frame that never got updated, so clicks in the "outside" area
 *          were consumed by no-op hit tests and never reached the button.
 *          Similarly, &open on Begin was nominally wired, but the focus
 *          thrash prevented the title-bar X's click from registering —
 *          ImGui's title-bar hit-test requires the window be the actual
 *          hovered window, and the overlay's SetNextWindowFocus was
 *          periodically stealing that status.
 *
 *   Issue 4: ColorEdit4 sub-popups (color picker) stopped responding.
 *          Same root cause: when ColorEdit4 opens its picker via
 *          OpenPopup, ImGui expects stable focus ordering.  Our per-frame
 *          SetNextWindowFocus on the overlay stole focus from the
 *          newly-opened picker, which then closed on the next frame
 *          because it lost focus to a window that claimed to be above it.
 *          Net effect: the picker appeared to "not work" — either never
 *          showing up, or instantly dismissing.
 *
 * New approach: native BeginPopupModal + &p_open for the X button,
 * Escape-to-close (ImGui's built-in modal behaviour), explicit Close
 * button, and a rect-based click-outside detector that skips sub-popups
 * (color picker) via IsAnyItemHovered / IsAnyItemActive. No custom
 * overlay window, no SetNextWindowFocus hack, no focus thrash. Sub-popups
 * (ColorEdit4) work because BeginPopupModal stacks with sub-popups in the
 * conventional way.
 * ========================================================================= */

static void renderThemeEditor(s32 winW, s32 winH)
{
    float scale = (float)winH / 720.0f;
    if (scale < 0.5f) scale = 0.5f;

    float editorW = 420.0f * scale;
    float editorH = (float)winH * 0.85f;

    /* Open the modal on the first frame s_Visible becomes true.  Guarded by
     * IsPopupOpen so repeat calls in subsequent frames are no-ops. */
    if (!ImGui::IsPopupOpen("Theme Editor##Modal")) {
        ImGui::OpenPopup("Theme Editor##Modal");
    }

    /* Center the modal on the screen.  Appearing cond so the user can drag
     * to reposition if they wish — but NoMove is set below to prevent it. */
    ImGui::SetNextWindowPos(ImVec2((float)winW * 0.5f, (float)winH * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(editorW, editorH), ImGuiCond_Appearing);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoSavedSettings;

    bool open = true;
    if (ImGui::BeginPopupModal("Theme Editor##Modal", &open, flags)) {
        /* B button / Escape closes (modal Escape is handled by ImGui, but
         * GamepadFaceRight needs explicit handling since NavEnableGamepad is off) */
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
            ImGui::CloseCurrentPopup();
            pdguiThemeEditorHide();
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

        /* --- Docked footer geometry (S298) ---
         * Reserve footer height at the bottom so the scroll body always sits
         * above the Save/Reset/Close row.  Mirrors the Nine-Slice Chrome tool
         * footer pattern so the Save controls don't scroll out of view on
         * short displays.  Footer holds two rows: [Name/Author/Save status]
         * and [Reset | Close]. */
        float btnW = 120.0f * scale;
        float btnH = 28.0f * scale;
        float rowSpacing = ImGui::GetStyle().ItemSpacing.y;
        float inputH  = ImGui::GetFrameHeightWithSpacing();
        float statusH = s_SaveStatus[0] ? ImGui::GetTextLineHeightWithSpacing() : 0.0f;
        float footerH = inputH              /* "Save as Mod:" heading line */
                      + inputH              /* Name + Author row */
                      + btnH + rowSpacing   /* Reset / Close row */
                      + statusH
                      + ImGui::GetStyle().ItemSpacing.y * 2.0f
                      + 10.0f * scale;

        /* ---- Color pickers ---- */
        ImGui::BeginChild("PaletteScroll", ImVec2(0, -footerH), true);

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

        /* ---- Docked footer ---- */
        ImGui::Separator();

        bool wantClose = false;

        /* Save section (row 1) */
        ImGui::Text("Save as Mod:");
        ImGui::PushItemWidth(200.0f * scale);
        ImGui::InputText("Name##save", s_SaveName, sizeof(s_SaveName));
        ImGui::SameLine();
        ImGui::InputText("Author##save", s_SaveAuthor, sizeof(s_SaveAuthor));
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Save", ImVec2(btnW, btnH))) {
            s_SaveSuccess = saveThemeAsMod(s_SaveName, s_SaveAuthor);
            if (s_SaveSuccess) {
                snprintf(s_SaveStatus, sizeof(s_SaveStatus),
                         "Saved to mods/");
                pdguiPlaySound(PDGUI_SND_SUCCESS);
            } else {
                snprintf(s_SaveStatus, sizeof(s_SaveStatus),
                         "Save failed — check logs");
                pdguiPlaySound(PDGUI_SND_ERROR);
            }
        }

        if (s_SaveStatus[0]) {
            ImVec4 statusCol = s_SaveSuccess
                ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            ImGui::TextColored(statusCol, "%s", s_SaveStatus);
        }

        /* Action row (row 2): Reset | Close */
        if (ImGui::Button("Reset", ImVec2(btnW, btnH))) {
            memcpy(s_WorkPalette, s_OrigPalette, sizeof(s_WorkPalette));
            pdguiSetPaletteCustom(s_WorkPalette);
            s_SaveStatus[0] = '\0';
        }

        ImGui::SameLine();

        if (ImGui::Button("Close", ImVec2(btnW, btnH))) {
            sysLogPrintf(LOG_NOTE, "Theme editor: exit — Close button");
            wantClose = true;
        }

        /* ---- Click-outside-to-close detection ----
         *
         * Use an explicit rect test against the modal's window bounds.  This
         * is the inverse of the old S197a overlay-InvisibleButton approach:
         * rather than drawing a full-screen hit-target behind us (which
         * fought with focus/z-order), we let the modal sit normally and
         * ask "did the user just click outside my rect, and is it a click
         * I should interpret as 'dismiss this window'?".
         *
         * We suppress the dismiss in two cases to keep sub-popups working:
         *   1. IsAnyItemActive:  the user is in the middle of dragging a
         *      slider, picking a color, or typing in an InputText.  Closing
         *      mid-interaction would abort their edit.
         *   2. IsAnyItemHovered: an ImGui item is under the cursor.  This
         *      fires when the mouse is over the ColorEdit4 picker popup —
         *      the picker's swatches are "items".  Without this check a
         *      click inside the color picker (which extends outside the
         *      modal rect) would wrongly dismiss the whole editor. */
        if (ImGui::IsMouseClicked(0)) {
            ImVec2 wpos   = ImGui::GetWindowPos();
            ImVec2 wsize  = ImGui::GetWindowSize();
            ImVec2 mpos   = ImGui::GetMousePos();
            bool outsideRect =
                (mpos.x < wpos.x || mpos.x > wpos.x + wsize.x ||
                 mpos.y < wpos.y || mpos.y > wpos.y + wsize.y);

            if (outsideRect &&
                !ImGui::IsAnyItemActive() &&
                !ImGui::IsAnyItemHovered()) {
                sysLogPrintf(LOG_NOTE, "Theme editor: exit — click outside modal rect");
                wantClose = true;
            }
        }

        if (wantClose) {
            ImGui::CloseCurrentPopup();
            pdguiThemeEditorHide();
        }

        ImGui::EndPopup();
    }

    /* Title-bar X button path: ImGui sets `open` false when the user clicks
     * the close cross.  After EndPopup we translate that into the public
     * Hide API so the visibility flag and log line stay in sync. */
    if (!open) {
        sysLogPrintf(LOG_NOTE, "Theme editor: exit — title-bar X button (!open after EndPopup)");
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

    /* 2026-04-11 rewrite: no more hand-rolled overlay / focus thrash.
     * renderThemeEditor() now opens a native BeginPopupModal that ImGui
     * manages end-to-end (including dimming the background, blocking
     * input to windows behind, the title-bar X button, and nested
     * color-picker popups).  See the banner comment on renderThemeEditor
     * for the full rationale and the list of bugs this rewrite fixes. */
    renderThemeEditor(winW, winH);
}

} /* extern "C" */
