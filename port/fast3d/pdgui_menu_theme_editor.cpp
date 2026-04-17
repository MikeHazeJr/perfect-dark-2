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
#include "pdgui_font_mod.h"      /* S306: Menu Style + Font bundle dropdowns */
#include "assetcatalog.h"
#include "config.h"
#include "system.h"
#include "fs.h"
#include "pdgui_audio.h"

/* =========================================================================
 * State
 * ========================================================================= */

static bool s_Visible = false;

/* Working palette: 20 u32 values in 0xRRGGBBAA format.
 * Indices 0-14 mirror struct menucolourpalette (legacy PD fields);
 * indices 15-19 are S306 extensions (toolbar tint, positive / warning
 * text, button hover / active). Zero in an extension slot means
 * "derive default at apply time" — see pdguiApplyPdStyle. */
static u32 s_WorkPalette[20];

/* Snapshot of palette when editor was opened (for reset). */
static u32 s_OrigPalette[20];

/* Save dialog state */
static char s_SaveName[64] = "My Theme";
static char s_SaveAuthor[64] = "";
static char s_SaveStatus[128] = "";
static bool s_SaveSuccess = false;

/* S306: theme-bundle fields — Menu Style + Font dropdowns let the user
 * author a bundled theme in one pass. Selected catalog ids are written to
 * theme.json as "menuStyle" and "font" so one theme activation swaps the
 * palette + chrome + font together (plumbing landed in S305 P4).
 * Empty string = no bundle for that slot. THEME_CATALOG_ID_LEN mirrors
 * the loader's internal constant (64) — local copy so we don't pull in
 * the loader's private headers. */
#define THEME_EDITOR_CATALOG_ID_LEN 64
static char s_SaveBundleChromeId[THEME_EDITOR_CATALOG_ID_LEN] = "";
static char s_SaveBundleFontId[THEME_EDITOR_CATALOG_ID_LEN]   = "";

/* =========================================================================
 * Palette field metadata for the UI
 * ========================================================================= */

/* Per-field metadata — label, theme.json key, palette index, and an
 * optional group marker so the editor can render section headings. The
 * S306 pass renamed labels for clarity ("Border Primary" → "Main Accent",
 * etc.) and flagged the three reserved legacy slots so they sit in their
 * own "Reserved" group and can be hidden behind an Advanced toggle. */
enum PalFieldGroup {
    PFG_FRAME = 0,       /* window chrome — borders, title, body */
    PFG_TEXT,            /* readable text colors */
    PFG_INTERACT,        /* buttons, checkboxes, sliders, headers */
    PFG_SEMANTIC,        /* S306 extensions — toolbar tint, positive/warning */
    PFG_RESERVED,        /* legacy unused slots kept for compatibility */
};

struct PalFieldInfo {
    const char *label;     /* display name (S306: renamed for clarity) */
    const char *jsonKey;   /* JSON key written to theme.json */
    int         index;     /* palette array index (0-19) */
    int         group;     /* PalFieldGroup */
    const char *tooltip;   /* hover hint explaining where the color shows up */
};

static const PalFieldInfo k_Fields[] = {
    { "Main Accent",          "dialog_border1",     0, PFG_FRAME,
        "Primary accent — window borders, tab highlight, hover tint." },
    { "Title Bar Background", "dialog_titlebg",     1, PFG_FRAME,
        "The strip behind the window title and the title shimmer." },
    { "Highlight Accent",     "dialog_border2",     2, PFG_FRAME,
        "Bright accent — right-edge border, focus ring, scrollbar grab." },
    { "Title Text",           "dialog_titlefg",     3, PFG_FRAME,
        "Window title text (e.g. 'Settings', 'Modding Hub')." },
    { "Window Background",    "dialog_bodybg",      4, PFG_FRAME,
        "Body fill behind all window contents." },
    { "(Reserved 1)",         "unused14",           5, PFG_RESERVED,
        "Unused legacy slot — kept for save compatibility." },
    { "Body Text",            "item_unfocused",     6, PFG_TEXT,
        "Default menu text — unselected buttons, labels, list rows." },
    { "Disabled Text",        "item_disabled",      7, PFG_TEXT,
        "Greyed-out text for disabled controls / locked options." },
    { "Focused Text",         "item_focused_inner", 8, PFG_TEXT,
        "Text color of the currently-hovered / keyboard-focused item." },
    { "Checkbox Check",       "checkbox_checked",   9, PFG_INTERACT,
        "Checkmark glyph inside checkboxes and radio buttons." },
    { "Focus Highlight",      "item_focused_outer", 10, PFG_INTERACT,
        "Background box drawn behind the focused menu row." },
    { "List Header BG",       "listgroup_headerbg", 11, PFG_INTERACT,
        "Header row background in sortable list groups." },
    { "List Header Text",     "listgroup_headerfg", 12, PFG_INTERACT,
        "Header row text in sortable list groups." },
    { "(Reserved 2)",         "unused34",           13, PFG_RESERVED,
        "Unused legacy slot — kept for save compatibility." },
    { "(Reserved 3)",         "unused38",           14, PFG_RESERVED,
        "Unused legacy slot — kept for save compatibility." },
    /* S306 extension fields — new in 2026-04-16. Each one can be left at
     * 0/empty in theme.json; the style code will fall back to a sensible
     * derived default (see pdguiGetToolbarTint / ...TextPositive / ...). */
    { "Toolbar Tint",         "toolbarTint",        15, PFG_SEMANTIC,
        "The tint behind modding-hub tool rows and segmented toolbars." },
    { "Positive Text (Lime)", "textPositive",       16, PFG_SEMANTIC,
        "Success / validation-OK text (e.g. 'Saved to mods/')." },
    { "Warning Text (Amber)", "textWarning",        17, PFG_SEMANTIC,
        "Section-heading / warning text (e.g. Settings tab headers)." },
    { "Button Hover",         "buttonHover",        18, PFG_SEMANTIC,
        "Button background when the mouse hovers over it." },
    { "Button Active",        "buttonActive",       19, PFG_SEMANTIC,
        "Button background during a press / click." },
};
#define NUM_FIELDS ((int)(sizeof(k_Fields) / sizeof(k_Fields[0])))

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

    /* S306: palette block now includes both legacy 15 fields and the five
     * extension fields. A field is emitted only when non-zero so older
     * loaders that don't know about the extensions skip them quietly
     * (parse_theme_json ignores unknown keys), and derived defaults
     * kick in for any extension the user never touched. Comma placement
     * is computed up-front from how many non-zero fields we'll emit. */
    int nWrite = 0;
    for (int i = 0; i < NUM_FIELDS; i++) {
        int idx = k_Fields[i].index;
        if (idx < 15) { nWrite++; continue; }
        if (s_WorkPalette[idx] != 0) nWrite++;
    }

    int written = 0;
    for (int i = 0; i < NUM_FIELDS; i++) {
        int idx = k_Fields[i].index;
        /* Skip zero extensions so viewers see a tidy file */
        if (idx >= 15 && s_WorkPalette[idx] == 0) continue;
        char hex[16];
        palToHex(s_WorkPalette[idx], hex, sizeof(hex));
        written++;
        fprintf(f, "    \"%s\": \"%s\"%s\n",
                k_Fields[i].jsonKey, hex,
                (written < nWrite) ? "," : "");
    }

    fprintf(f, "  },\n");
    /* S306: emit bundle fields when the user picked a Menu Style / Font. */
    if (s_SaveBundleChromeId[0]) {
        fprintf(f, "  \"menuStyle\": \"%s\",\n", s_SaveBundleChromeId);
    }
    if (s_SaveBundleFontId[0]) {
        fprintf(f, "  \"font\": \"%s\",\n", s_SaveBundleFontId);
    }
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

        /* ---- Color pickers ----
         *
         * S306: group the 20 palette fields into labeled sections
         * (Frame / Text / Interact / Semantic) so the editor reads as a
         * guided catalog rather than a 15-row dump. The Reserved group
         * is hidden behind an Advanced toggle for power users who want
         * to fill the legacy slots for custom shaders. Each row has a
         * hover tooltip explaining where the color shows up in-game. */
        static bool s_ShowReserved = false;
        ImGui::BeginChild("PaletteScroll", ImVec2(0, -footerH), true);

        static const struct { int group; const char *header; const char *blurb; } k_Groups[] = {
            { PFG_FRAME,    "Window Frame",
              "Borders, title bar, and body fill." },
            { PFG_TEXT,     "Text Colors",
              "Readable text — default, disabled, focused." },
            { PFG_INTERACT, "Interactive Elements",
              "Buttons, checkboxes, focus rings, list headers." },
            { PFG_SEMANTIC, "Semantic Accents",
              "Toolbars and colored status text (positive / warning)." },
        };

        bool changed = false;
        for (int g = 0; g < (int)(sizeof(k_Groups)/sizeof(k_Groups[0])); g++) {
            const char *hdr = k_Groups[g].header;
            int grp = k_Groups[g].group;
            ImVec4 hdrCol = palToVec4((pdguiGetTextWarning()));
            ImGui::TextColored(hdrCol, "%s", hdr);
            ImGui::SameLine();
            ImGui::TextDisabled("  %s", k_Groups[g].blurb);
            ImGui::Separator();

            for (int i = 0; i < NUM_FIELDS; i++) {
                if (k_Fields[i].group != grp) continue;
                int idx = k_Fields[i].index;

                ImVec4 col = palToVec4(s_WorkPalette[idx]);
                char pickerId[64];
                snprintf(pickerId, sizeof(pickerId), "##pal_%d", idx);

                ImGui::Text("%s", k_Fields[i].label);
                if (ImGui::IsItemHovered() && k_Fields[i].tooltip) {
                    ImGui::SetTooltip("%s", k_Fields[i].tooltip);
                }
                ImGui::SameLine(220.0f * scale);

                ImGuiColorEditFlags cflags = ImGuiColorEditFlags_AlphaBar
                                           | ImGuiColorEditFlags_AlphaPreviewHalf
                                           | ImGuiColorEditFlags_NoInputs;

                if (ImGui::ColorEdit4(pickerId, &col.x, cflags)) {
                    s_WorkPalette[idx] = vec4ToPal(col);
                    changed = true;
                }

                /* Extension field "Reset" cheat — a small ghosted label
                 * on the right reminds the user that zero means "use
                 * derived default", and clicking it zeroes the slot. */
                if (k_Fields[i].group == PFG_SEMANTIC) {
                    ImGui::SameLine();
                    char resetId[64];
                    snprintf(resetId, sizeof(resetId), "auto##%d", idx);
                    if (ImGui::SmallButton(resetId)) {
                        s_WorkPalette[idx] = 0;
                        changed = true;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Reset to derived default");
                    }
                }
            }
            ImGui::Spacing();
        }

        ImGui::Checkbox("Show Reserved (legacy unused slots)", &s_ShowReserved);
        if (s_ShowReserved) {
            ImVec4 hdrCol = palToVec4(pdguiGetTextWarning());
            ImGui::TextColored(hdrCol, "Reserved Slots");
            ImGui::TextDisabled("  Legacy N64 fields — kept for save compatibility. Not rendered by default.");
            ImGui::Separator();
            for (int i = 0; i < NUM_FIELDS; i++) {
                if (k_Fields[i].group != PFG_RESERVED) continue;
                int idx = k_Fields[i].index;
                ImVec4 col = palToVec4(s_WorkPalette[idx]);
                char pickerId[64];
                snprintf(pickerId, sizeof(pickerId), "##pal_%d", idx);
                ImGui::Text("%s", k_Fields[i].label);
                ImGui::SameLine(220.0f * scale);
                ImGuiColorEditFlags cflags = ImGuiColorEditFlags_AlphaBar
                                           | ImGuiColorEditFlags_AlphaPreviewHalf
                                           | ImGuiColorEditFlags_NoInputs;
                if (ImGui::ColorEdit4(pickerId, &col.x, cflags)) {
                    s_WorkPalette[idx] = vec4ToPal(col);
                    changed = true;
                }
            }
        }

        ImGui::EndChild();

        /* Apply changes live */
        if (changed) {
            pdguiSetPaletteCustom(s_WorkPalette);
            /* S306: push the extension tail too so live preview reflects
             * toolbar tint / positive / warning edits immediately. */
            pdguiSetPaletteExtensions(s_WorkPalette[15], s_WorkPalette[16],
                                      s_WorkPalette[17], s_WorkPalette[18],
                                      s_WorkPalette[19]);
        }

        /* ---- Docked footer ---- */
        ImGui::Separator();

        bool wantClose = false;

        /* Save section (row 1): Name + Author inputs only — Save button moved
         * to the action row so all three action buttons (Save / Reset / Close)
         * are docked together. Previously Save was inline after Author, which
         * pushed it off to the side (partly clipped on narrow windows) and
         * meant it was only reachable via Tab. S305 fix. */
        ImGui::Text("Save as Mod:");
        ImGui::PushItemWidth(200.0f * scale);
        ImGui::InputText("Name##save", s_SaveName, sizeof(s_SaveName));
        ImGui::SameLine();
        ImGui::InputText("Author##save", s_SaveAuthor, sizeof(s_SaveAuthor));
        ImGui::PopItemWidth();

        /* S306: theme-bundle dropdowns. Picking a Menu Style / Font here
         * writes those ids into the theme.json `menuStyle` / `font` keys
         * so one theme activation swaps the full visual identity (palette
         * + chrome + font). Both fields are optional — leaving them on
         * "(none)" emits no bundle key. THEME_CATALOG_ID_LEN mirrors the
         * loader's value so the catalog-id string fits. */
        ImGui::Spacing();
        ImGui::TextDisabled("Bundle (optional) — ship the full look together:");
        {
            /* Menu Style combo — "(none)" + every registered chrome style. */
            s32 styleCount = pdguiThemeGetChromeStyleCount();
            const s32 maxStyles = 31;
            const s32 usedStyles = styleCount < maxStyles ? styleCount : maxStyles;
            const char *opts[1 + maxStyles];
            opts[0] = "(none)";
            for (s32 i = 0; i < usedStyles; i++) {
                opts[i + 1] = pdguiThemeGetChromeStyleName(i);
            }
            int idx = 0;
            if (s_SaveBundleChromeId[0]) {
                for (s32 i = 0; i < usedStyles; i++) {
                    const char *id = pdguiThemeGetChromeStyleId(i);
                    if (id && strcmp(id, s_SaveBundleChromeId) == 0) {
                        idx = (int)(i + 1); break;
                    }
                }
            }
            ImGui::SetNextItemWidth(260.0f * scale);
            if (ImGui::BeginCombo("Menu Style##bundle", opts[idx])) {
                for (int i = 0; i < 1 + usedStyles; i++) {
                    bool sel = (i == idx);
                    if (ImGui::Selectable(opts[i], sel)) {
                        if (i == 0) s_SaveBundleChromeId[0] = '\0';
                        else {
                            snprintf(s_SaveBundleChromeId, sizeof(s_SaveBundleChromeId),
                                     "%s", pdguiThemeGetChromeStyleId(i - 1));
                        }
                    }
                }
                ImGui::EndCombo();
            }
        }
        {
            /* Font combo — "(none)" + every registered mod font. Hand-
             * placed fonts are registered by pdgui_font_mod.c. */
            s32 fontCount = pdguiFontModGetCount();
            const s32 maxFonts = 32;
            const char *opts[1 + maxFonts];
            opts[0] = "(none)";
            s32 cnt = 1;
            for (s32 i = 0; i < fontCount && cnt < (s32)(sizeof(opts) / sizeof(opts[0])); i++) {
                opts[cnt++] = pdguiFontModGetName(i);
            }
            int idx = 0;
            if (s_SaveBundleFontId[0]) {
                for (s32 i = 0; i < fontCount; i++) {
                    const char *id = pdguiFontModGetId(i);
                    if (id && strcmp(id, s_SaveBundleFontId) == 0) {
                        idx = (int)(i + 1); break;
                    }
                }
            }
            ImGui::SetNextItemWidth(260.0f * scale);
            if (ImGui::BeginCombo("Font##bundle", opts[idx])) {
                for (int i = 0; i < cnt; i++) {
                    bool sel = (i == idx);
                    if (ImGui::Selectable(opts[i], sel)) {
                        if (i == 0) s_SaveBundleFontId[0] = '\0';
                        else {
                            snprintf(s_SaveBundleFontId, sizeof(s_SaveBundleFontId),
                                     "%s", pdguiFontModGetId(i - 1));
                        }
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(applies on restart)");
        }

        if (s_SaveStatus[0]) {
            ImVec4 statusCol = s_SaveSuccess
                ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            ImGui::TextColored(statusCol, "%s", s_SaveStatus);
        }

        /* Action row (row 2): [Save | Reset | Close] — all three docked together */
        if (ImGui::Button("Save", ImVec2(btnW, btnH))) {
            s_SaveSuccess = saveThemeAsMod(s_SaveName, s_SaveAuthor);
            if (s_SaveSuccess) {
                snprintf(s_SaveStatus, sizeof(s_SaveStatus),
                         "Saved to mods/");
                pdguiPlaySound(PDGUI_SND_SUCCESS);
                /* S305: refresh theme registry so the newly-saved theme
                 * appears in Settings → Debug → Themes list immediately
                 * without requiring a restart or mod re-scan. Same pattern
                 * as B-155/B-156 chrome mod auto-appear. */
                pdguiThemeRescanMods();
            } else {
                snprintf(s_SaveStatus, sizeof(s_SaveStatus),
                         "Save failed — check logs");
                pdguiPlaySound(PDGUI_SND_ERROR);
            }
        }

        ImGui::SameLine();

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
