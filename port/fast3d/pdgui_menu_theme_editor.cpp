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

#ifdef _WIN32
#include <windows.h>
#endif

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
#include "pdgui_glyphs.h"
#include "pdgui_widgets.h"      /* Priority L: shared label-left widget helpers */
#include "modpack_pdmod.h"   /* Priority M / B-238 / M-3.2 */
#include "assetcatalog_scanner.h"
#include "theme_archive_authoring.h"

/* =========================================================================
 * State
 * ========================================================================= */

static bool s_Visible = false;

/* Working palette: 24 u32 values in 0xRRGGBBAA format.
 * Indices 0-14 mirror struct menucolourpalette (legacy PD fields);
 * indices 15-19 are S306 extensions (toolbar tint, positive / warning
 * text, button hover / active); indices 20-23 are S309 extensions
 * (title glow, success/danger/info window tints). Zero in an extension
 * slot means "derive default at apply time" — see pdguiApplyPdStyle. */
static u32 s_WorkPalette[24];

/* Snapshot of palette when editor was opened (for reset). */
static u32 s_OrigPalette[24];

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
static char s_SaveBundleAudioId[THEME_EDITOR_CATALOG_ID_LEN]  = "";
static char s_SaveBundleMusicId[THEME_EDITOR_CATALOG_ID_LEN]  = "";

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
    /* S309: title glow + window tints. Defaults derive from border2 /
     * muted red-green-border1 respectively so zero preserves a usable
     * look on older themes. The "(auto)" reset button zeroes each slot. */
    { "Title Glow",           "titleGlow",          20, PFG_SEMANTIC,
        "Soft glow behind window titles. Was previously locked to blue." },
    { "Success Tint",         "tintSuccess",        21, PFG_SEMANTIC,
        "Window tint for save-OK / confirm-safe surfaces." },
    { "Danger Tint",          "tintDanger",         22, PFG_SEMANTIC,
        "Window tint for delete / abort / end-game confirms." },
    { "Info Tint",            "tintInfo",           23, PFG_SEMANTIC,
        "Window tint for notices, help dialogs, and other informational UI." },
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

static bool jsonEscape(const char *input, char *output, size_t outputCap)
{
    size_t used = 0;
    if (!input || !output || outputCap == 0) return false;
    for (const unsigned char *p = (const unsigned char *)input; *p; p++) {
        const char *escape = nullptr;
        char unicode[7];
        switch (*p) {
        case '"': escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        case '\b': escape = "\\b"; break;
        case '\f': escape = "\\f"; break;
        case '\n': escape = "\\n"; break;
        case '\r': escape = "\\r"; break;
        case '\t': escape = "\\t"; break;
        default:
            if (*p < 0x20) {
                snprintf(unicode, sizeof(unicode), "\\u%04x", (unsigned)*p);
                escape = unicode;
            }
            break;
        }
        if (escape) {
            size_t add = strlen(escape);
            if (used + add >= outputCap) return false;
            memcpy(output + used, escape, add);
            used += add;
        } else {
            if (used + 1 >= outputCap) return false;
            output[used++] = (char)*p;
        }
    }
    output[used] = '\0';
    return true;
}

static bool makeThemeIdentity(const char *name, char *slug, size_t slugCap,
                              char *catalogId, size_t catalogCap)
{
    size_t used = 0;
    if (!name || !name[0] || !slug || slugCap < 2 || !catalogId) return false;
    for (const char *p = name; *p && used + 1 < slugCap; p++) {
        char c = *p;
        if (c == ' ') c = '-';
        else c = (char)tolower((unsigned char)c);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                || c == '-' || c == '_') {
            slug[used++] = c;
        }
    }
    slug[used] = '\0';
    if (used == 0) return false;
    int n = snprintf(catalogId, catalogCap, "%s:theme", slug);
    return n > 0 && (size_t)n < catalogCap;
}

struct ThemeDependencySelection {
    theme_archive_dependency_role_e role;
    asset_type_e type;
    const char *extension;
    const char *label;
    char *catalogId;
};

static ThemeDependencySelection k_DependencySelections[] = {
    { THEME_ARCHIVE_DEP_UI, ASSET_UI, ".pdui", "Menu UI", s_SaveBundleChromeId },
    { THEME_ARCHIVE_DEP_FONT, ASSET_FONT, ".pdfont", "Font", s_SaveBundleFontId },
    { THEME_ARCHIVE_DEP_AUDIO, ASSET_AUDIO, ".pdsfx", "Menu sounds", s_SaveBundleAudioId },
    { THEME_ARCHIVE_DEP_MUSIC, ASSET_AUDIO, ".pdsong", "Menu music", s_SaveBundleMusicId },
};

static bool catalogArchivePathForSelection(
    const ThemeDependencySelection &selection, const char *catalogId,
    char *out, size_t outCap)
{
    const asset_entry_t *entry = assetCatalogResolve(catalogId);
    if (!entry || !entry->enabled || entry->type != selection.type) return false;
    const char *sources[] = { entry->dirpath, entry->descriptor_path };
    for (const char *source : sources) {
        if (!source || !source[0]) continue;
        const char *match = nullptr;
        for (const char *p = strstr(source, selection.extension); p;
                p = strstr(p + 1, selection.extension)) {
            match = p;
        }
        if (!match) continue;
        size_t length = (size_t)(match - source) + strlen(selection.extension);
        char trailing = source[length];
        if (trailing != '\0' && !(trailing == ':' && source[length + 1] == ':')) {
            continue;
        }
        if (length >= outCap) return false;
        memcpy(out, source, length);
        out[length] = '\0';
        return true;
    }
    return false;
}

static bool themeSelectionUsable(const ThemeDependencySelection &selection,
                                 const char *catalogId, char *archivePath,
                                 size_t archivePathCap)
{
    if (!catalogArchivePathForSelection(selection, catalogId, archivePath,
                                        archivePathCap)) return false;
    if (selection.role == THEME_ARCHIVE_DEP_FONT) {
        char error[192];
        if (!pdguiFontModValidateCatalogId(catalogId, error, sizeof(error))) {
            return false;
        }
    }
    return true;
}

static size_t collectThemeDependencies(theme_archive_dependency_t *out,
                                       char paths[][FS_MAXPATH],
                                       char *error, size_t errorCap)
{
    size_t count = 0;
    for (const ThemeDependencySelection &selection : k_DependencySelections) {
        if (!selection.catalogId[0]) continue;
        if (!themeSelectionUsable(selection, selection.catalogId,
                paths[count], FS_MAXPATH)) {
            snprintf(error, errorCap, "%s '%s' is not an enabled typed archive",
                     selection.label, selection.catalogId);
            return (size_t)-1;
        }
        out[count] = { selection.role, selection.catalogId, paths[count] };
        count++;
    }
    return count;
}

static void renderDependencyCombo(const ThemeDependencySelection &selection,
                                  float scale)
{
    const asset_entry_t *choices[64];
    s32 count = 0;
    for (s32 i = 0; i < assetCatalogGetPoolSize() && count < 64; i++) {
        const asset_entry_t *entry = assetCatalogGetByIndex(i);
        char path[FS_MAXPATH];
        if (entry && entry->enabled && entry->type == selection.type
                && themeSelectionUsable(selection, entry->id,
                    path, sizeof(path))) {
            choices[count++] = entry;
        }
    }
    const char *preview = selection.catalogId[0] ? selection.catalogId : "(none)";
    char comboLabel[96];
    snprintf(comboLabel, sizeof(comboLabel), "%s##theme_dep_%d",
             selection.label, (int)selection.role);
    ImGui::SetNextItemWidth(300.0f * scale);
    if (ImGui::BeginCombo(comboLabel, preview)) {
        if (ImGui::Selectable("(none)", !selection.catalogId[0])) {
            selection.catalogId[0] = '\0';
        }
        for (s32 i = 0; i < count; i++) {
            bool selected = strcmp(selection.catalogId, choices[i]->id) == 0;
            if (ImGui::Selectable(choices[i]->id, selected)) {
                snprintf(selection.catalogId, THEME_EDITOR_CATALOG_ID_LEN,
                         "%s", choices[i]->id);
            }
        }
        ImGui::EndCombo();
    }
}

/* =========================================================================
 * Save theme as mod
 * ========================================================================= */

static bool saveThemeAsMod(const char *name, const char *author);
/* =========================================================================
 * M-3.2: Save theme as a .pdmod archive (canonical mod format per B-238).
 *
 * Builds the same mod.json + theme.json content the folder save produces,
 * then hands them to modpackPdmodWriteSingle. Writes to mods/<slug>.pdmod.
 * The shared helper computes the M-2.3 zip-comment mirror automatically
 * from the manifest's headline fields.
 * ========================================================================= */

/* Compose theme.json into a fresh malloc'd, NUL-terminated buffer.
 * Returns NULL on allocation failure. *outLen is the byte length excluding
 * the trailing NUL. */
static char *buildThemeJsonBuffer(const char *name, const char *author,
                                  const char *catalogId, u32 *outLen)
{
    /* Worst-case: 32 fields x 60 chars + bundle/scanline/glow/sound = ~3 KiB.
     * 8 KiB buffer is plenty headroom. */
    const u32 cap = 8192;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    char escapedName[256];
    char escapedAuthor[256];
    if (!jsonEscape(name, escapedName, sizeof(escapedName))
            || !jsonEscape(author && author[0] ? author : "User",
                escapedAuthor, sizeof(escapedAuthor))) {
        free(buf);
        return NULL;
    }

    int written = 0;
    int n;
#define APPEND(...) do {                                          \
        n = snprintf(buf + written, cap - written, __VA_ARGS__);   \
        if (n < 0 || (u32)written + n >= cap) { free(buf); return NULL; } \
        written += n;                                              \
    } while (0)

    APPEND("{\n");
    APPEND("  \"schema\": \"pd2.theme.v1\",\n");
    APPEND("  \"catalog_id\": \"%s\",\n", catalogId);
    APPEND("  \"name\": \"%s\",\n", escapedName);
    APPEND("  \"author\": \"%s\",\n", escapedAuthor);
    APPEND("  \"version\": \"1\",\n");
    APPEND("  \"palette\": {\n");

    int nWrite = 0;
    for (int i = 0; i < NUM_FIELDS; i++) {
        int idx = k_Fields[i].index;
        if (idx < 15) { nWrite++; continue; }
        if (s_WorkPalette[idx] != 0) nWrite++;
    }

    int wcount = 0;
    for (int i = 0; i < NUM_FIELDS; i++) {
        int idx = k_Fields[i].index;
        if (idx >= 15 && s_WorkPalette[idx] == 0) continue;
        char hex[16];
        palToHex(s_WorkPalette[idx], hex, sizeof(hex));
        wcount++;
        APPEND("    \"%s\": \"%s\"%s\n",
               k_Fields[i].jsonKey, hex,
               (wcount < nWrite) ? "," : "");
    }

    APPEND("  },\n");
    if (s_SaveBundleChromeId[0]) {
        APPEND("  \"textures\": { \"dialog_background\": \"%s\" },\n",
               s_SaveBundleChromeId);
        APPEND("  \"menuStyle\": \"%s\",\n", s_SaveBundleChromeId);
    }
    if (s_SaveBundleFontId[0]) APPEND("  \"font\": \"%s\",\n", s_SaveBundleFontId);
    if (s_SaveBundleAudioId[0]) {
        static const char *soundRoles[] = {
            "swipe", "open", "focus", "select", "error", "toggle_on",
            "toggle_off", "subfocus", "keyboard_focus", "cancel", "success"
        };
        APPEND("  \"sounds\": {\n");
        for (size_t i = 0; i < sizeof(soundRoles) / sizeof(soundRoles[0]); i++) {
            APPEND("    \"%s\": \"%s\"%s\n", soundRoles[i],
                   s_SaveBundleAudioId,
                   i + 1 < sizeof(soundRoles) / sizeof(soundRoles[0]) ? "," : "");
        }
        APPEND("  },\n");
    }
    if (s_SaveBundleMusicId[0]) {
        APPEND("  \"menuMusic\": \"%s\",\n", s_SaveBundleMusicId);
    }
    APPEND("  \"scanline\": { \"enabled\": true, \"alpha\": 0.8, \"interval\": 2 },\n");
    APPEND("  \"textGlow\": { \"enabled\": true, \"intensity\": 0.6, \"color\": \"0080ffff\" },\n");
    APPEND("  \"fontShadow\": { \"offsetX\": 1, \"offsetY\": 1, \"color\": \"00000080\" },\n");
    APPEND("  \"fontGlow\": { \"radius\": 2, \"intensity\": 0.6, \"color\": \"0080ffff\", \"passes\": 2 }\n");
    APPEND("}\n");

#undef APPEND

    if (outLen) *outLen = (u32)written;
    return buf;
}

/* Compose mod.json for the .pdmod from `name` + `dirName` slug. */
static char *buildModJsonBuffer(const char *name, const char *dirName, const char *author, u32 *outLen)
{
    const u32 cap = 1024;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    char escapedName[256];
    char escapedAuthor[256];
    if (!jsonEscape(name, escapedName, sizeof(escapedName))
            || !jsonEscape(author && author[0] ? author : "User",
                escapedAuthor, sizeof(escapedAuthor))) {
        free(buf);
        return NULL;
    }
    int n = snprintf(buf, cap,
        "{\n"
        "  \"id\": \"%s\",\n"
        "  \"name\": \"%s\",\n"
        "  \"version\": \"1.0\",\n"
        "  \"author\": \"%s\",\n"
        "  \"description\": \"Custom theme created with Theme Editor\",\n"
        "  \"base_fallback\": \"base:theme_blue\"\n"
        "}\n",
        dirName, escapedName, escapedAuthor);
    if (n < 0 || (u32)n >= cap) { free(buf); return NULL; }
    if (outLen) *outLen = (u32)n;
    return buf;
}

static bool authorThemeArchive(const char *archivePath, const char *name,
                               const char *author, const char *catalogId, char *error,
                               size_t errorCap)
{
    theme_archive_dependency_t dependencies[THEME_ARCHIVE_DEP_COUNT];
    char dependencyPaths[THEME_ARCHIVE_DEP_COUNT][FS_MAXPATH];
    size_t dependencyCount = collectThemeDependencies(
        dependencies, dependencyPaths, error, errorCap);
    if (dependencyCount == (size_t)-1) return false;

    u32 themeLen = 0;
    char *theme = buildThemeJsonBuffer(name, author, catalogId, &themeLen);
    if (!theme) {
        snprintf(error, errorCap, "could not compose strict theme.json");
        return false;
    }

    theme_archive_author_request_t request = {};
    request.archive_path = archivePath;
    request.catalog_id = catalogId;
    request.display_name = name;
    request.theme_json = theme;
    request.theme_json_size = themeLen;
    request.dependencies = dependencies;
    request.dependency_count = dependencyCount;
    s32 result = themeArchiveAuthor(&request, error, errorCap);
    free(theme);
    return result != 0;
}

static bool writeModManifest(const char *path, const char *name,
                             const char *slug, const char *author)
{
    char candidate[FS_MAXPATH];
    char candidateFull[FS_MAXPATH + 1];
    char pathFull[FS_MAXPATH + 1];
    int candidateLen = snprintf(candidate, sizeof(candidate), "%s.candidate", path);
    if (candidateLen <= 0 || candidateLen >= (int)sizeof(candidate)) return false;
    u32 length = 0;
    char *manifest = buildModJsonBuffer(name, slug, author, &length);
    if (!manifest) return false;
    fsFullPath(candidate, candidateFull, sizeof(candidateFull));
    fsFullPath(path, pathFull, sizeof(pathFull));
    remove(candidateFull);
    FILE *file = fsFileOpenWrite(candidate);
    bool ok = file && fwrite(manifest, 1, length, file) == length
                   && fflush(file) == 0;
    if (file && fclose(file) != 0) ok = false;
    free(manifest);
    if (!ok) {
        remove(candidateFull);
        return false;
    }
#ifdef _WIN32
    ok = MoveFileExA(candidateFull, pathFull,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    ok = rename(candidateFull, pathFull) == 0;
#endif
    if (!ok) remove(candidateFull);
    return ok;
}

static bool saveThemeAsMod(const char *name, const char *author)
{
    char slug[64];
    char catalogId[THEME_EDITOR_CATALOG_ID_LEN];
    char modDir[FS_MAXPATH];
    char themeDir[FS_MAXPATH];
    char archivePath[FS_MAXPATH];
    char archiveFull[FS_MAXPATH + 1];
    char manifestPath[FS_MAXPATH];
    char error[256] = {};
    if (!makeThemeIdentity(name, slug, sizeof(slug), catalogId,
                           sizeof(catalogId))) return false;
    if (!fsCreateDir("mods")
            || snprintf(modDir, sizeof(modDir), "mods/%s", slug) >= (int)sizeof(modDir)
            || !fsCreateDir(modDir)
            || snprintf(themeDir, sizeof(themeDir), "%s/themes", modDir) >= (int)sizeof(themeDir)
            || !fsCreateDir(themeDir)
            || snprintf(archivePath, sizeof(archivePath), "%s/%s.pdtheme",
                        themeDir, slug) >= (int)sizeof(archivePath)
            || snprintf(manifestPath, sizeof(manifestPath), "%s/mod.json",
                        modDir) >= (int)sizeof(manifestPath)) {
        sysLogPrintf(LOG_WARNING, "Theme editor: cannot prepare mod folder for '%s'", slug);
        return false;
    }
    fsFullPath(archivePath, archiveFull, sizeof(archiveFull));
    if (!authorThemeArchive(archiveFull, name, author, catalogId, error, sizeof(error))) {
        sysLogPrintf(LOG_WARNING, "Theme editor: .pdtheme authoring failed: %s", error);
        return false;
    }
    if (!writeModManifest(manifestPath, name, slug, author)) {
        sysLogPrintf(LOG_WARNING, "Theme editor: could not write '%s'", manifestPath);
        return false;
    }
    if (!assetCatalogScanExternalLayoutFolder(slug, modDir)) {
        sysLogPrintf(LOG_WARNING, "Theme editor: catalog rejected '%s'", archivePath);
        return false;
    }
    const asset_entry_t *entry = assetCatalogResolve(catalogId);
    if (!entry || !entry->enabled || entry->type != ASSET_THEME) {
        sysLogPrintf(LOG_WARNING, "Theme editor: authored theme '%s' did not resolve", catalogId);
        return false;
    }
    sysLogPrintf(LOG_NOTE, "Theme editor: saved production .pdtheme '%s'", archivePath);
    return true;
}

static bool saveThemeAsPdmod(const char *name, const char *author)
{
    char slug[64];
    char catalogId[THEME_EDITOR_CATALOG_ID_LEN];
    char tempArchive[FS_MAXPATH];
    char tempArchiveFull[FS_MAXPATH + 1];
    char outPath[FS_MAXPATH];
    char entryName[128];
    char error[256] = {};
    if (!makeThemeIdentity(name, slug, sizeof(slug), catalogId,
                           sizeof(catalogId)) || !fsCreateDir("mods")) return false;
    if (snprintf(tempArchive, sizeof(tempArchive), "mods/.%s-theme-export.pdtheme", slug)
            >= (int)sizeof(tempArchive)
            || snprintf(outPath, sizeof(outPath), "mods/%s.pdmod", slug)
            >= (int)sizeof(outPath)
            || snprintf(entryName, sizeof(entryName), "themes/%s.pdtheme", slug)
            >= (int)sizeof(entryName)) return false;
    fsFullPath(tempArchive, tempArchiveFull, sizeof(tempArchiveFull));
    if (!authorThemeArchive(tempArchiveFull, name, author, catalogId, error, sizeof(error))) {
        sysLogPrintf(LOG_WARNING, "Theme editor: .pdmod theme authoring failed: %s", error);
        return false;
    }

    u32 archiveLen = 0;
    void *archive = fsFileLoad(tempArchiveFull, &archiveLen);
    u32 manifestLen = 0;
    char *manifest = buildModJsonBuffer(name, slug, author, &manifestLen);
    if (!archive || !manifest) {
        if (archive) sysMemFree(archive);
        free(manifest);
        remove(tempArchiveFull);
        return false;
    }
    modpack_entry_t entry = { entryName, archive, archiveLen };
    s32 result = modpackPdmodWriteSingle(outPath, manifest, manifestLen, &entry, 1);
    sysMemFree(archive);
    free(manifest);
    remove(tempArchiveFull);
    if (result != MODPACK_PDMOD_OK) {
        sysLogPrintf(LOG_WARNING, "Theme editor: .pdmod write failed (%d): %s",
                     result, modpackPdmodLastError());
        return false;
    }
    sysLogPrintf(LOG_NOTE, "Theme editor: saved self-contained theme as '%s'", outPath);
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

/* S309: mini-menu live preview — renders sample widgets inside a child
 * panel so the user sees their palette edits applied to representative UI
 * without leaving the editor. Uses only the standard ImGui widgets so the
 * active PD style drives all colors. Reads semantic tints via
 * pdguiGetTintSuccess / pdguiGetTintDanger / pdguiGetTintInfo for the
 * three demo "window tint" buttons.
 *
 * No input side-effects — clicks inside preview do nothing. */
static ImU32 themeEditorU32FromRgba(u32 rgba)
{
    u8 r = (u8)((rgba >> 24) & 0xFF);
    u8 g = (u8)((rgba >> 16) & 0xFF);
    u8 b = (u8)((rgba >>  8) & 0xFF);
    u8 a = (u8)((rgba >>  0) & 0xFF);
    return IM_COL32(r, g, b, a);
}

static void renderLivePreview(float h, float scale)
{
    /* Phase 2 fix #3 (input-menu pillar, 2026-05-01): NavFlattened so
     * D-pad LEFT/RIGHT can move focus between the preview column and
     * the palette column. Without NavFlattened, ImGui treats each
     * child as its own nav root and gamepad arrows do not cross. */
    ImGui::BeginChild("##theme_preview", ImVec2(0, h),
                      ImGuiChildFlags_Border | ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_NoScrollbar);

    /* Mini header that mirrors the PD dialog title strip so the user can
     * see title_glow + dialog_titlebg + dialog_border1 live. We use the
     * real pdgui primitives so edits to the palette reach this panel
     * through the same code path as a real dialog. */
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float headerH = 28.0f * scale;
    pdguiDrawPdDialog(pos.x, pos.y, w, headerH + 6.0f * scale,
                      "Preview Window", 1);
    /* Glow demo: simulate a title text glow using the current title glow
     * color. Pairs with the title strip above so the user sees both
     * surfaces update when they edit Title Glow. */
    pdguiDrawTextGlow(pos.x + 8.0f * scale, pos.y + 4.0f * scale,
                      80.0f * scale, headerH - 8.0f * scale);

    pdguiSetCursorBelowTitle(headerH + 6.0f * scale); /* content-inset: drawn dialog is headerH+6*scale tall */

    /* Sample content row */
    ImGui::TextDisabled("Sample widgets:");
    ImGui::Button("Primary");
    ImGui::SameLine();
    static bool s_checked = true;
    ImGui::Checkbox("Checkbox", &s_checked);

    /* Section header using text_warning */
    ImVec4 hdrCol = palToVec4(pdguiGetTextWarning());
    ImGui::TextColored(hdrCol, "Section Heading");
    ImGui::TextWrapped("Body text — this line mirrors a typical menu paragraph.");
    ImVec4 okCol = palToVec4(pdguiGetTextPositive());
    ImGui::TextColored(okCol, "OK: saved to mods/");

    /* Window-tint demo row — 3 narrow buttons whose colored backgrounds
     * show the three S309 semantic tints. The buttons themselves are
     * disabled so clicking does nothing. */
    ImGui::Separator();
    ImGui::TextDisabled("Window tints:");
    {
        ImVec2 btnSize(110.0f * scale, 24.0f * scale);
        auto drawTint = [&](const char *label, u32 rgba){
            ImDrawList *dl = ImGui::GetWindowDrawList();
            ImVec2 bpos = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(bpos, ImVec2(bpos.x + btnSize.x,
                                           bpos.y + btnSize.y),
                              themeEditorU32FromRgba(rgba), 2.0f);
            ImGui::InvisibleButton(label, btnSize);
            ImVec2 ts = ImGui::CalcTextSize(label);
            dl->AddText(ImVec2(bpos.x + (btnSize.x - ts.x) * 0.5f,
                               bpos.y + (btnSize.y - ts.y) * 0.5f),
                        IM_COL32(0, 0, 0, 220), label);
        };
        drawTint("Success", pdguiGetTintSuccess());
        ImGui::SameLine();
        drawTint("Danger",  pdguiGetTintDanger());
        ImGui::SameLine();
        drawTint("Info",    pdguiGetTintInfo());
    }

    ImGui::EndChild();
}

static void renderThemeEditor(s32 winW, s32 winH)
{
    float scale = (float)winH / 720.0f;
    if (scale < 0.5f) scale = 0.5f;

    /* S309: widened modal so the right half holds the live preview.
     * Previous width (420) was tight for color-pickers + bundle dropdowns;
     * 820 gives comfortable room for a 340px preview column alongside
     * the existing content. */
    float editorW = 820.0f * scale;
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
        float dependencyH = (float)(sizeof(k_DependencySelections)
                                  / sizeof(k_DependencySelections[0])) * inputH;
        float footerH = inputH              /* "Save as Mod:" heading line */
                      + inputH              /* Name + Author row */
                      + dependencyH         /* typed dependency selectors */
                      + ImGui::GetTextLineHeightWithSpacing() * 2.0f
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

        /* S309: 2-column layout — color pickers on the left, live preview
         * on the right. Uses a fixed 340px preview column so the left
         * column adapts to the wider editor. */
        float availW = ImGui::GetContentRegionAvail().x;
        float previewW = 320.0f * scale;
        if (previewW > availW * 0.5f) previewW = availW * 0.5f;
        float pickerW = availW - previewW - 8.0f * scale;
        /* Phase 2 fix #3 (input-menu pillar, 2026-05-01): NavFlattened
         * pairs with ##theme_preview so D-pad LEFT/RIGHT crosses the
         * preview / palette column boundary on gamepad. */
        ImGui::BeginChild("PaletteScroll", ImVec2(pickerW, -footerH),
                          ImGuiChildFlags_Border | ImGuiChildFlags_NavFlattened);

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

        /* S309: live preview beside the pickers — same top alignment,
         * same scrollable footer budget. */
        ImGui::SameLine();
        renderLivePreview(-footerH, scale);

        /* Apply changes live */
        if (changed) {
            pdguiSetPaletteCustom(s_WorkPalette);
            /* S306: push the extension tail too so live preview reflects
             * toolbar tint / positive / warning edits immediately. */
            pdguiSetPaletteExtensions(s_WorkPalette[15], s_WorkPalette[16],
                                      s_WorkPalette[17], s_WorkPalette[18],
                                      s_WorkPalette[19]);
            /* S309: push extension-2 (title glow + window tints). */
            pdguiSetPaletteExtensions2(s_WorkPalette[20], s_WorkPalette[21],
                                       s_WorkPalette[22], s_WorkPalette[23]);
        }

        /* ---- Docked footer ---- */
        ImGui::Separator();

        bool wantClose = false;

        /* S311: S306 title-close channel — catches the titlebar X click
         * before ImGui's own popup handling swallows the Escape edge. */
        if (pdguiConsumeTitleClose()) {
            sysLogPrintf(LOG_NOTE, "Theme editor: exit — title X button");
            wantClose = true;
        }

        /* Save section (row 1): Name + Author inputs only — Save button moved
         * to the action row so all three action buttons (Save / Reset / Close)
         * are docked together. Previously Save was inline after Author, which
         * pushed it off to the side (partly clipped on narrow windows) and
         * meant it was only reachable via Tab. S305 fix. */
        ImGui::Text("Save self-contained theme:");
        ImGui::PushItemWidth(200.0f * scale);
        ImGui::InputText("Name##save", s_SaveName, sizeof(s_SaveName));
        ImGui::SameLine();
        ImGui::InputText("Author##save", s_SaveAuthor, sizeof(s_SaveAuthor));
        ImGui::PopItemWidth();

        /* The archive writer embeds the selected public typed archives and
         * records their catalog ids in strict theme source. A declared but
         * invalid dependency rejects the save instead of falling back. */
        ImGui::Spacing();
        ImGui::TextDisabled("Embedded typed assets (optional):");
        for (const ThemeDependencySelection &selection : k_DependencySelections) {
            renderDependencyCombo(selection, scale);
        }
        ImGui::TextDisabled("Menu UI supplies style/background; one SFX archive supplies all menu sound roles.");

        if (s_SaveStatus[0]) {
            ImVec4 statusCol = s_SaveSuccess
                ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            ImGui::TextColored(statusCol, "%s", s_SaveStatus);
        }

        char acceptGlyph[64] = {};
        char cancelGlyph[64] = {};
        char saveLabel[128];
        char closeLabel[128];
        pdguiGlyphGetActionLabel(ACTION_MENU_ACCEPT, acceptGlyph, (s32)sizeof(acceptGlyph));
        pdguiGlyphGetActionLabel(ACTION_MENU_CANCEL, cancelGlyph, (s32)sizeof(cancelGlyph));
        snprintf(saveLabel, sizeof(saveLabel), "Save .pdtheme [%s]##save_theme",
                 acceptGlyph[0] ? acceptGlyph : "Accept");
        snprintf(closeLabel, sizeof(closeLabel), "Close [%s]##close_theme",
                 cancelGlyph[0] ? cancelGlyph : "Cancel");

        /* Action row: ImGui navigation preserves mouse/keyboard/controller
         * activation; visible labels track the current device bindings. */
        if (ImGui::Button(saveLabel, ImVec2(btnW * 1.35f, btnH))) {
            s_SaveSuccess = saveThemeAsMod(s_SaveName, s_SaveAuthor);
            if (s_SaveSuccess) {
                snprintf(s_SaveStatus, sizeof(s_SaveStatus),
                         "Saved self-contained .pdtheme");
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

        /* Priority M / B-238 / M-3.2: write the same theme as a single
         * .pdmod archive (one file in mods/<slug>.pdmod). The folder save
         * above is kept during the migration window per design Section 5. */
        if (ImGui::Button("Save as .pdmod", ImVec2(btnW * 1.4f, btnH))) {
            s_SaveSuccess = saveThemeAsPdmod(s_SaveName, s_SaveAuthor);
            if (s_SaveSuccess) {
                snprintf(s_SaveStatus, sizeof(s_SaveStatus),
                         "Saved as .pdmod");
                pdguiPlaySound(PDGUI_SND_SUCCESS);
                /* The loader picks up the new .pdmod on next mod scan;
                 * trigger a rescan so it appears in this session. */
                pdguiThemeRescanMods();
            } else {
                snprintf(s_SaveStatus, sizeof(s_SaveStatus),
                         ".pdmod save failed — check logs");
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

        if (ImGui::Button(closeLabel, ImVec2(btnW * 1.25f, btnH))) {
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
