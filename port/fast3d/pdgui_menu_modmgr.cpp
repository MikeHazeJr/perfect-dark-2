/**
 * pdgui_menu_modmgr.cpp -- D3R-6 Mod Manager UI
 *
 * Standalone ImGui window (not a hotswap menu replacement) that allows the
 * user to browse, enable/disable, validate, and apply mod components.
 *
 * Entry point: pdguiModManagerShow() — called from main menu "Mod Manager"
 * button (view 3).  pdguiModManagerRender() is called every frame from
 * pdguiRender() in pdgui_backend.cpp.
 *
 * Two views:
 *   By Category — tree by asset type (Maps, Characters, Skins, etc.)
 *   By Mod      — tree by category label ("goldfinger64", "kakariko", etc.)
 *
 * Apply Changes:
 *   1. Commits s_Entries[] enable state to catalog via assetCatalogSetEnabled()
 *   2. Calls modmgrApplyChanges() — saves .modstate, invalidates caches,
 *      returns to title screen.
 *
 * IMPORTANT: C++ file — must NOT include types.h (#define bool s32 breaks C++).
 * Forward-declare all C symbols via extern "C" blocks.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "system.h"
#include "assetcatalog.h"
#include "assetcatalog_scanner.h"

extern "C" {

/* Mod manager lifecycle */
void modmgrApplyChanges(void);
void modmgrSaveComponentState(void);
const char *modmgrGetModsDir(void);

/* Catalog write API (D3R-6) */
void assetCatalogSetEnabled(const char *id, s32 enabled);
s32  assetCatalogGetUniqueCategories(char out[][CATALOG_CATEGORY_LEN], s32 maxout);

/* Video dimensions */
s32 viGetWidth(void);
s32 viGetHeight(void);

/* Button edge glow */
void pdguiDrawButtonEdgeGlow(f32 x, f32 y, f32 w, f32 h, s32 isActive);

} /* extern "C" */

/* ========================================================================
 * Constants
 * ======================================================================== */

#define MODMGR_MAX_ENTRIES    512
#define MODMGR_MAX_CATEGORIES  64
#define MODMGR_MAX_ERRORS     128
#define MODMGR_INI_PATH_LEN   (FS_MAXPATH + 32)

/* ========================================================================
 * Internal entry snapshot
 * ======================================================================== */

struct ModMgrEntry {
    char         id[CATALOG_ID_LEN];
    char         category[CATALOG_CATEGORY_LEN];
    char         dirpath[FS_MAXPATH];
    asset_type_e type;
    int          enabled;       /* current user selection (not yet applied) */
    int          orig_enabled;  /* state when Mod Manager was opened */
    int          bundled;       /* base game asset — cannot be permanently disabled */
};

static ModMgrEntry s_Entries[MODMGR_MAX_ENTRIES];
static int         s_NumEntries   = 0;

/* ========================================================================
 * UI state
 * ======================================================================== */

static bool s_Visible      = false;
static int  s_Tab          = 0;   /* 0 = By Category, 1 = By Mod */
static char s_SelectedId[CATALOG_ID_LEN] = "";

/* By-Category: collapsed state per type */
static bool s_TypeCollapsed[ASSET_TYPE_COUNT];
static bool s_BaseCollapsed = true;  /* base game section starts collapsed */

/* By-Mod: category list + collapsed state */
static char s_Categories[MODMGR_MAX_CATEGORIES][CATALOG_CATEGORY_LEN];
static int  s_NumCategories = 0;
static bool s_CatCollapsed[MODMGR_MAX_CATEGORIES];

/* ========================================================================
 * Validate results
 * ======================================================================== */

struct ModMgrError {
    char id[CATALOG_ID_LEN];
    char msg[256];
    bool isError;   /* true = error (red), false = warning (yellow) */
};

static ModMgrError s_Errors[MODMGR_MAX_ERRORS];
static int         s_NumErrors       = 0;
static bool        s_ValidationDone  = false;
static bool        s_ShowValidation  = false;

/* ========================================================================
 * Helpers
 * ======================================================================== */

static bool pathExists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static const char *typeName(asset_type_e t)
{
    switch (t) {
        case ASSET_MAP:         return "Maps";
        case ASSET_CHARACTER:   return "Characters";
        case ASSET_SKIN:        return "Skins";
        case ASSET_BOT_VARIANT: return "Bot Variants";
        case ASSET_WEAPON:      return "Weapons";
        case ASSET_TEXTURES:    return "Texture Packs";
        case ASSET_SFX:         return "Sound Effects";
        case ASSET_MUSIC:       return "Music";
        case ASSET_PROP:        return "Props";
        case ASSET_VEHICLE:     return "Vehicles";
        case ASSET_MISSION:     return "Missions";
        case ASSET_UI:          return "UI";
        case ASSET_TOOL:        return "Tools";
        case ASSET_ARENA:       return "Arenas (base)";
        case ASSET_BODY:        return "Bodies (base)";
        case ASSET_HEAD:        return "Heads (base)";
        default:                return "Unknown";
    }
}

/* INI filename for a given asset type.  Returns "" for base-game-only types. */
static const char *iniNameForType(asset_type_e t)
{
    switch (t) {
        case ASSET_MAP:         return "map.ini";
        case ASSET_CHARACTER:   return "character.ini";
        case ASSET_SKIN:        return "skin.ini";
        case ASSET_BOT_VARIANT: return "bot.ini";
        case ASSET_WEAPON:      return "weapon.ini";
        case ASSET_TEXTURES:    return "textures.ini";
        case ASSET_SFX:         return "sfx.ini";
        case ASSET_MUSIC:       return "music.ini";
        case ASSET_PROP:        return "prop.ini";
        case ASSET_VEHICLE:     return "vehicle.ini";
        case ASSET_MISSION:     return "mission.ini";
        case ASSET_UI:          return "ui.ini";
        case ASSET_TOOL:        return "tool.ini";
        default:                return "";
    }
}

/* Simple label prettification: "gf64_bond" -> "gf64 bond" (underscores -> spaces) */
static void prettifyId(const char *id, char *out, int outlen)
{
    int i = 0;
    while (id[i] && i < outlen - 1) {
        out[i] = (id[i] == '_') ? ' ' : id[i];
        i++;
    }
    out[i] = '\0';
}

static int countPending(void)
{
    int n = 0;
    for (int i = 0; i < s_NumEntries; i++) {
        if (s_Entries[i].enabled != s_Entries[i].orig_enabled) {
            n++;
        }
    }
    return n;
}

/* ========================================================================
 * Snapshot population (called on Show)
 * ======================================================================== */

struct PopCtx { int *count; };

static void populateCallback(const asset_entry_t *entry, void *userdata)
{
    int *count = (int *)userdata;
    if (*count >= MODMGR_MAX_ENTRIES) {
        return;
    }
    ModMgrEntry &e = s_Entries[(*count)++];
    strncpy(e.id,       entry->id,       CATALOG_ID_LEN - 1);  e.id[CATALOG_ID_LEN - 1] = '\0';
    strncpy(e.category, entry->category, CATALOG_CATEGORY_LEN - 1);  e.category[CATALOG_CATEGORY_LEN - 1] = '\0';
    strncpy(e.dirpath,  entry->dirpath,  FS_MAXPATH - 1);  e.dirpath[FS_MAXPATH - 1] = '\0';
    e.type         = entry->type;
    e.enabled      = entry->enabled;
    e.orig_enabled = entry->enabled;
    e.bundled      = entry->bundled;
}

static const asset_type_e s_AllTypes[] = {
    ASSET_MAP, ASSET_CHARACTER, ASSET_SKIN, ASSET_BOT_VARIANT,
    ASSET_WEAPON, ASSET_TEXTURES, ASSET_SFX, ASSET_MUSIC,
    ASSET_PROP, ASSET_VEHICLE, ASSET_MISSION, ASSET_UI, ASSET_TOOL,
    ASSET_ARENA, ASSET_BODY, ASSET_HEAD
};
static const int s_NumAllTypes = (int)(sizeof(s_AllTypes) / sizeof(s_AllTypes[0]));

static void refreshSnapshot(void)
{
    s_NumEntries = 0;
    for (int t = 0; t < s_NumAllTypes; t++) {
        assetCatalogIterateByType(s_AllTypes[t], populateCallback, &s_NumEntries);
    }

    /* Reset type collapsed states (keep base collapsed by default) */
    for (int i = 0; i < ASSET_TYPE_COUNT; i++) {
        s_TypeCollapsed[i] = false;  /* mod sections expanded by default */
    }
    s_BaseCollapsed = true;

    /* Rebuild category list */
    s_NumCategories = assetCatalogGetUniqueCategories(s_Categories, MODMGR_MAX_CATEGORIES);
    for (int i = 0; i < s_NumCategories; i++) {
        s_CatCollapsed[i] = false;
    }

    /* Clear validation results */
    s_NumErrors      = 0;
    s_ValidationDone = false;
    s_ShowValidation = false;

    sysLogPrintf(LOG_NOTE, "MODMGR: snapshot loaded — %d entries, %d categories",
                 s_NumEntries, s_NumCategories);
}

/* ========================================================================
 * Validation
 * ======================================================================== */

static void addError(const char *id, bool isErr, const char *fmt, ...)
{
    if (s_NumErrors >= MODMGR_MAX_ERRORS) {
        return;
    }
    ModMgrError &e = s_Errors[s_NumErrors++];
    strncpy(e.id, id, CATALOG_ID_LEN - 1);
    e.id[CATALOG_ID_LEN - 1] = '\0';
    e.isError = isErr;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e.msg, sizeof(e.msg), fmt, ap);
    va_end(ap);
}

static void runValidation(void)
{
    s_NumErrors = 0;

    for (int i = 0; i < s_NumEntries; i++) {
        const ModMgrEntry &e = s_Entries[i];

        /* Skip base game entries — no files to check */
        if (e.bundled) {
            continue;
        }

        /* 1. Check component directory exists */
        if (e.dirpath[0] == '\0') {
            addError(e.id, true, "No directory path registered in catalog");
            continue;
        }
        if (!pathExists(e.dirpath)) {
            addError(e.id, true, "Directory missing: %s", e.dirpath);
            continue;
        }

        /* 2. Check primary .ini file exists */
        const char *iniName = iniNameForType(e.type);
        if (iniName[0] != '\0') {
            char iniPath[MODMGR_INI_PATH_LEN];
            snprintf(iniPath, sizeof(iniPath), "%s/%s", e.dirpath, iniName);
            if (!pathExists(iniPath)) {
                addError(e.id, true, "INI file missing: %s", iniPath);
                continue;
            }

            /* 3. Parse ini and check depends_on entries exist in catalog */
            ini_section_t ini;
            if (iniParse(iniPath, &ini)) {
                const char *deps = iniGet(&ini, "depends_on", "");
                if (deps[0] != '\0') {
                    /* Split comma-separated dep list */
                    char depbuf[256];
                    strncpy(depbuf, deps, sizeof(depbuf) - 1);
                    depbuf[sizeof(depbuf) - 1] = '\0';

                    char *tok = depbuf;
                    char *end;
                    while (tok && *tok) {
                        /* Trim leading spaces */
                        while (*tok == ' ') tok++;
                        end = strchr(tok, ',');
                        if (end) *end = '\0';
                        /* Trim trailing spaces */
                        int len = (int)strlen(tok);
                        while (len > 0 && tok[len - 1] == ' ') tok[--len] = '\0';

                        if (tok[0] != '\0') {
                            if (!assetCatalogHasEntry(tok)) {
                                addError(e.id, false,
                                    "Dependency not found in catalog: '%s'", tok);
                            }
                        }
                        tok = end ? end + 1 : NULL;
                    }
                }
            } else {
                addError(e.id, false, "Could not parse %s (malformed?)", iniPath);
            }
        }
    }

    s_ValidationDone = true;
    s_ShowValidation = true;
    sysLogPrintf(LOG_NOTE, "MODMGR: validation complete — %d issues found", s_NumErrors);
    for (int i = 0; i < s_NumErrors; i++) {
        sysLogPrintf(s_Errors[i].isError ? LOG_ERROR : LOG_WARNING,
                     "MODMGR VALIDATE [%s]: %s", s_Errors[i].id, s_Errors[i].msg);
    }
}

/* ========================================================================
 * Detail panel
 * ======================================================================== */

static void renderDetails(float scale)
{
    if (s_SelectedId[0] == '\0') {
        ImGui::TextDisabled("Select a component to see details.");
        return;
    }

    /* Find entry */
    const ModMgrEntry *sel = NULL;
    for (int i = 0; i < s_NumEntries; i++) {
        if (strcmp(s_Entries[i].id, s_SelectedId) == 0) {
            sel = &s_Entries[i];
            break;
        }
    }
    if (!sel) {
        ImGui::TextDisabled("(entry not found)");
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    char pretty[CATALOG_ID_LEN];
    prettifyId(sel->id, pretty, sizeof(pretty));
    ImGui::TextWrapped("%s", pretty);
    ImGui::PopStyleColor();

    ImGui::Separator();

    ImGui::TextDisabled("ID:");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", sel->id);

    ImGui::TextDisabled("Type:");
    ImGui::SameLine();
    ImGui::Text("%s", typeName(sel->type));

    if (sel->category[0]) {
        ImGui::TextDisabled("Category:");
        ImGui::SameLine();
        ImGui::Text("%s", sel->category);
    }

    ImGui::Spacing();

    if (sel->bundled) {
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Base Game Asset");
        ImGui::TextDisabled("(always enabled — cannot be\npermanently disabled)");
    } else {
        /* Read display name and description from ini if available */
        const char *iniName = iniNameForType(sel->type);
        if (iniName[0] != '\0' && sel->dirpath[0] != '\0') {
            char iniPath[MODMGR_INI_PATH_LEN];
            snprintf(iniPath, sizeof(iniPath), "%s/%s", sel->dirpath, iniName);
            ini_section_t ini;
            if (iniParse(iniPath, &ini)) {
                const char *dispName = iniGet(&ini, "name", "");
                const char *desc     = iniGet(&ini, "description", "");
                const char *author   = iniGet(&ini, "author", "");
                const char *version  = iniGet(&ini, "version", "");
                const char *deps     = iniGet(&ini, "depends_on", "");

                if (dispName[0]) {
                    ImGui::TextDisabled("Name:");
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", dispName);
                }
                if (author[0]) {
                    ImGui::TextDisabled("Author:");
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", author);
                }
                if (version[0]) {
                    ImGui::TextDisabled("Version:");
                    ImGui::SameLine();
                    ImGui::Text("%s", version);
                }
                if (deps[0]) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("Depends on:");
                    ImGui::TextWrapped("%s", deps);
                }
                if (desc[0]) {
                    ImGui::Spacing();
                    ImGui::TextWrapped("%s", desc);
                }
            }
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Directory:");
        ImGui::TextWrapped("%s", sel->dirpath[0] ? sel->dirpath : "(none)");
    }

    /* Status indicator */
    ImGui::Spacing();
    ImGui::Separator();

    /* Find current enabled state in snapshot */
    bool curEnabled = false;
    for (int i = 0; i < s_NumEntries; i++) {
        if (strcmp(s_Entries[i].id, s_SelectedId) == 0) {
            curEnabled = s_Entries[i].enabled != 0;
            break;
        }
    }

    if (curEnabled) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "* Enabled");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "* Disabled");
    }
}

/* ========================================================================
 * Checkbox row helper (shared by both tabs)
 * ======================================================================== */

static void renderEntryRow(int idx, float scale)
{
    ModMgrEntry &e = s_Entries[idx];

    bool en = e.enabled != 0;
    char label[CATALOG_ID_LEN + 4];
    snprintf(label, sizeof(label), "##chk_%s", e.id);

    ImGui::PushID(idx);

    /* Highlight if this is the selected entry */
    bool isSelected = (strcmp(e.id, s_SelectedId) == 0);
    if (isSelected) {
        ImGui::PushStyleColor(ImGuiCol_Header,
            ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);
    }

    /* Disabled checkbox for bundled entries (visual only — still render) */
    if (e.bundled) {
        ImGui::BeginDisabled();
    }

    if (ImGui::Checkbox(label, &en)) {
        e.enabled = en ? 1 : 0;
        pdguiPlaySound(en ? PDGUI_SND_TOGGLEON : PDGUI_SND_TOGGLEOFF);
    }

    if (e.bundled) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    /* Selectable label — click to select for details panel */
    char pretty[CATALOG_ID_LEN];
    prettifyId(e.id, pretty, sizeof(pretty));

    ImGuiSelectableFlags selFlags = ImGuiSelectableFlags_SpanAllColumns;
    if (ImGui::Selectable(pretty, isSelected, selFlags,
                          ImVec2(0, ImGui::GetTextLineHeight()))) {
        strncpy(s_SelectedId, e.id, CATALOG_ID_LEN - 1);
        s_SelectedId[CATALOG_ID_LEN - 1] = '\0';
    }

    /* Pending change indicator */
    if (e.enabled != e.orig_enabled) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 0.9f), "*");
    }

    if (isSelected) {
        ImGui::PopStyleColor();
    }

    ImGui::PopID();
}

/* ========================================================================
 * By-Category tab
 * ======================================================================== */

/* Returns tri-state: 0=all off, 1=all on, 2=mixed */
static int typeTriState(asset_type_e type, bool bundledOnly)
{
    int onCount = 0, total = 0;
    for (int i = 0; i < s_NumEntries; i++) {
        if (s_Entries[i].type != type) continue;
        if (bundledOnly != (s_Entries[i].bundled != 0)) continue;
        total++;
        if (s_Entries[i].enabled) onCount++;
    }
    if (total == 0) return -1;
    if (onCount == 0) return 0;
    if (onCount == total) return 1;
    return 2;
}

static void setTypeEnabled(asset_type_e type, bool bundledOnly, int val)
{
    for (int i = 0; i < s_NumEntries; i++) {
        if (s_Entries[i].type != type) continue;
        if (bundledOnly != (s_Entries[i].bundled != 0)) continue;
        if (s_Entries[i].bundled) continue;  /* never force-toggle bundled */
        s_Entries[i].enabled = val;
    }
}

static void renderByCategoryTab(float scale)
{
    /* All user-manageable types (non-base) */
    static const asset_type_e userTypes[] = {
        ASSET_MAP, ASSET_CHARACTER, ASSET_SKIN, ASSET_BOT_VARIANT,
        ASSET_WEAPON, ASSET_TEXTURES, ASSET_SFX, ASSET_MUSIC,
        ASSET_PROP, ASSET_VEHICLE, ASSET_MISSION, ASSET_UI, ASSET_TOOL
    };
    static const int numUserTypes = (int)(sizeof(userTypes) / sizeof(userTypes[0]));

    for (int t = 0; t < numUserTypes; t++) {
        asset_type_e type = userTypes[t];

        /* Count non-bundled entries of this type */
        int count = 0;
        for (int i = 0; i < s_NumEntries; i++) {
            if (s_Entries[i].type == type && !s_Entries[i].bundled) count++;
        }
        if (count == 0) continue;

        /* Type header with tri-state checkbox */
        int tri = typeTriState(type, false);
        bool triVal = (tri == 1);
        bool mixed  = (tri == 2);

        char hdrLabel[64];
        snprintf(hdrLabel, sizeof(hdrLabel), "##hdr_%d", (int)type);

        if (mixed) {
            ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        }
        if (ImGui::Checkbox(hdrLabel, &triVal)) {
            setTypeEnabled(type, false, triVal ? 1 : 0);
            pdguiPlaySound(triVal ? PDGUI_SND_TOGGLEON : PDGUI_SND_TOGGLEOFF);
        }
        if (mixed) {
            ImGui::PopItemFlag();
        }
        ImGui::SameLine();

        char hdrText[64];
        snprintf(hdrText, sizeof(hdrText), "%s (%d)###typenode_%d",
                 typeName(type), count, (int)type);

        bool &collapsed = s_TypeCollapsed[(int)type];
        if (ImGui::TreeNodeEx(hdrText,
                              collapsed ? ImGuiTreeNodeFlags_None
                                        : ImGuiTreeNodeFlags_DefaultOpen)) {
            collapsed = false;
            for (int i = 0; i < s_NumEntries; i++) {
                if (s_Entries[i].type == type && !s_Entries[i].bundled) {
                    ImGui::TreePush((void*)(intptr_t)i);
                    renderEntryRow(i, scale);
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        } else {
            collapsed = true;
        }
    }

    /* Base Game collapsible section */
    {
        int baseCount = 0;
        for (int i = 0; i < s_NumEntries; i++) {
            if (s_Entries[i].bundled) baseCount++;
        }
        if (baseCount > 0) {
            ImGui::Spacing();
            ImGui::Separator();
            char baseHdr[64];
            snprintf(baseHdr, sizeof(baseHdr), "Base Game Assets (%d)###basenode", baseCount);
            ImGuiTreeNodeFlags baseFlags = s_BaseCollapsed
                ? ImGuiTreeNodeFlags_None
                : ImGuiTreeNodeFlags_DefaultOpen;
            if (ImGui::TreeNodeEx(baseHdr, baseFlags)) {
                s_BaseCollapsed = false;
                ImGui::TextDisabled("(always enabled — informational only)");
                for (int t = 0; t < s_NumAllTypes; t++) {
                    asset_type_e type = s_AllTypes[t];
                    int cnt = 0;
                    for (int i = 0; i < s_NumEntries; i++) {
                        if (s_Entries[i].type == type && s_Entries[i].bundled) cnt++;
                    }
                    if (cnt == 0) continue;
                    char subHdr[64];
                    snprintf(subHdr, sizeof(subHdr), "%s (%d)###basetype_%d",
                             typeName(type), cnt, (int)type);
                    if (ImGui::TreeNodeEx(subHdr, ImGuiTreeNodeFlags_None)) {
                        for (int i = 0; i < s_NumEntries; i++) {
                            if (s_Entries[i].type == type && s_Entries[i].bundled) {
                                ImGui::TreePush((void*)(intptr_t)(i + 10000));
                                renderEntryRow(i, scale);
                                ImGui::TreePop();
                            }
                        }
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();
            } else {
                s_BaseCollapsed = true;
            }
        }
    }
}

/* ========================================================================
 * By-Mod tab
 * ======================================================================== */

/* Returns tri-state for a category: 0=all off, 1=all on, 2=mixed */
static int catTriState(const char *cat)
{
    int onCount = 0, total = 0;
    for (int i = 0; i < s_NumEntries; i++) {
        if (s_Entries[i].bundled) continue;
        if (strcmp(s_Entries[i].category, cat) != 0) continue;
        total++;
        if (s_Entries[i].enabled) onCount++;
    }
    if (total == 0) return -1;
    if (onCount == 0) return 0;
    if (onCount == total) return 1;
    return 2;
}

static void setCatEnabled(const char *cat, int val)
{
    for (int i = 0; i < s_NumEntries; i++) {
        if (s_Entries[i].bundled) continue;
        if (strcmp(s_Entries[i].category, cat) != 0) continue;
        s_Entries[i].enabled = val;
    }
}

static void renderByModTab(float scale)
{
    if (s_NumCategories == 0) {
        ImGui::TextDisabled("No mod components installed.");
        ImGui::TextDisabled("Add components to mods/{category}/{id}/ and restart.");
        return;
    }

    for (int c = 0; c < s_NumCategories; c++) {
        const char *cat = s_Categories[c];

        int cnt = 0;
        for (int i = 0; i < s_NumEntries; i++) {
            if (!s_Entries[i].bundled && strcmp(s_Entries[i].category, cat) == 0) cnt++;
        }
        if (cnt == 0) continue;

        /* Category header with tri-state checkbox */
        int tri = catTriState(cat);
        bool triVal = (tri == 1);
        bool mixed  = (tri == 2);

        char chkLabel[CATALOG_CATEGORY_LEN + 8];
        snprintf(chkLabel, sizeof(chkLabel), "##catChk_%d", c);

        if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        if (ImGui::Checkbox(chkLabel, &triVal)) {
            setCatEnabled(cat, triVal ? 1 : 0);
            pdguiPlaySound(triVal ? PDGUI_SND_TOGGLEON : PDGUI_SND_TOGGLEOFF);
        }
        if (mixed) ImGui::PopItemFlag();
        ImGui::SameLine();

        char hdrText[CATALOG_CATEGORY_LEN + 32];
        snprintf(hdrText, sizeof(hdrText), "%s (%d)###catnode_%d", cat, cnt, c);

        ImGuiTreeNodeFlags flags = s_CatCollapsed[c]
            ? ImGuiTreeNodeFlags_None
            : ImGuiTreeNodeFlags_DefaultOpen;

        if (ImGui::TreeNodeEx(hdrText, flags)) {
            s_CatCollapsed[c] = false;
            for (int t = 0; t < s_NumAllTypes; t++) {
                asset_type_e type = s_AllTypes[t];
                /* Sub-group by type within this category */
                bool hasAny = false;
                for (int i = 0; i < s_NumEntries; i++) {
                    if (!s_Entries[i].bundled &&
                        strcmp(s_Entries[i].category, cat) == 0 &&
                        s_Entries[i].type == type) {
                        hasAny = true; break;
                    }
                }
                if (!hasAny) continue;

                ImGui::TextDisabled("%s", typeName(type));
                for (int i = 0; i < s_NumEntries; i++) {
                    if (!s_Entries[i].bundled &&
                        strcmp(s_Entries[i].category, cat) == 0 &&
                        s_Entries[i].type == type) {
                        ImGui::Indent(16.0f * scale);
                        renderEntryRow(i, scale);
                        ImGui::Unindent(16.0f * scale);
                    }
                }
            }
            ImGui::TreePop();
        } else {
            s_CatCollapsed[c] = true;
        }
    }
}

/* ========================================================================
 * Validation results popup
 * ======================================================================== */

static void renderValidationPopup(void)
{
    if (!s_ShowValidation) return;

    ImGui::OpenPopup("Validation Results");
    s_ShowValidation = false;  /* one-shot: popup opens, then it manages itself */
}

static void renderValidationModal(float scale)
{
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(pdguiScale(500.0f), pdguiScale(400.0f)), ImGuiCond_Always);

    if (!ImGui::BeginPopupModal("Validation Results", NULL,
                                ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove)) {
        return;
    }

    if (s_NumErrors == 0) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                           "All %d components validated OK.", s_NumEntries);
    } else {
        ImGui::Text("%d issue(s) found:", s_NumErrors);
        ImGui::Separator();
        ImGui::BeginChild("##validate_scroll",
                          ImVec2(0, -40.0f * scale), true);
        for (int i = 0; i < s_NumErrors; i++) {
            ImVec4 col = s_Errors[i].isError
                ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                : ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
            ImGui::TextColored(col, "[%s] %s",
                s_Errors[i].isError ? "ERROR" : "WARN",
                s_Errors[i].id);
            ImGui::TextWrapped("  %s", s_Errors[i].msg);
            ImGui::Spacing();
        }
        ImGui::EndChild();
    }

    if (ImGui::Button("Close", ImVec2(120.0f * scale, 28.0f * scale))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

/* ========================================================================
 * Main render
 * ======================================================================== */

/* ========================================================================
 * Inner content renderer — usable both standalone and embedded in hub
 * ======================================================================== */

static void renderModManagerBody(float dialogW, float dialogH, float scale, s32 *outClose)
{
    /* --- Header --- */
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f * scale);
    ImGui::Text("MOD MANAGER");
    ImGui::PopStyleColor();
    ImGui::Separator();

    /* --- Tab bar --- */
    if (ImGui::BeginTabBar("##modmgr_tabs")) {
        if (ImGui::BeginTabItem("By Category")) {
            s_Tab = 0;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("By Mod")) {
            s_Tab = 1;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    /* --- Two-panel layout --- */
    float footerH = 44.0f * scale;
    float panelH  = dialogH
                    - ImGui::GetCursorPosY()
                    - footerH
                    - ImGui::GetStyle().ItemSpacing.y * 2.0f;
    float leftW   = dialogW * 0.65f - ImGui::GetStyle().ItemSpacing.x;
    float rightW  = dialogW * 0.35f - ImGui::GetStyle().ItemSpacing.x * 2.0f;

    /* Left panel: list */
    ImGui::BeginChild("##modmgr_list", ImVec2(leftW, panelH), true);
    if (s_Tab == 0) {
        renderByCategoryTab(scale);
    } else {
        renderByModTab(scale);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    /* Right panel: details */
    ImGui::BeginChild("##modmgr_details", ImVec2(rightW, panelH), true);
    renderDetails(scale);
    ImGui::EndChild();

    /* --- Footer --- */
    ImGui::Separator();
    ImGui::SetCursorPosY(dialogH - footerH + ImGui::GetStyle().ItemSpacing.y);

    /* Pending change count */
    int pending = countPending();
    if (pending > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                           "%d change(s) pending", pending);
    } else {
        ImGui::TextDisabled("No pending changes");
    }

    ImGui::SameLine(0, 20.0f * scale);

    /* Validate */
    if (ImGui::Button("Validate", ImVec2(90.0f * scale, 28.0f * scale))) {
        runValidation();
        pdguiPlaySound(PDGUI_SND_SELECT);
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive() || ImGui::IsItemFocused()) {
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        pdguiDrawButtonEdgeGlow(rmin.x, rmin.y,
                                rmax.x - rmin.x, rmax.y - rmin.y,
                                ImGui::IsItemActive() ? 1 : 0);
    }

    ImGui::SameLine();

    /* Apply Changes */
    {
        char applyLabel[48];
        if (pending > 0) {
            snprintf(applyLabel, sizeof(applyLabel), "Apply Changes (%d)", pending);
        } else {
            strncpy(applyLabel, "Apply Changes", sizeof(applyLabel));
        }

        bool applyDisabled = (pending == 0);
        if (applyDisabled) ImGui::BeginDisabled();

        if (ImGui::Button(applyLabel, ImVec2(160.0f * scale, 28.0f * scale))) {
            for (int i = 0; i < s_NumEntries; i++) {
                if (!s_Entries[i].bundled &&
                    s_Entries[i].enabled != s_Entries[i].orig_enabled) {
                    assetCatalogSetEnabled(s_Entries[i].id, s_Entries[i].enabled);
                }
            }
            modmgrApplyChanges();
            *outClose = 1;
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
        if (!applyDisabled &&
            (ImGui::IsItemHovered() || ImGui::IsItemActive() || ImGui::IsItemFocused())) {
            ImVec2 rmin = ImGui::GetItemRectMin();
            ImVec2 rmax = ImGui::GetItemRectMax();
            pdguiDrawButtonEdgeGlow(rmin.x, rmin.y,
                                    rmax.x - rmin.x, rmax.y - rmin.y,
                                    ImGui::IsItemActive() ? 1 : 0);
        }
        if (applyDisabled) ImGui::EndDisabled();
    }

    ImGui::SameLine();

    /* Close */
    ImGui::PushStyleColor(ImGuiCol_Button,
        ImVec4(0.35f, 0.05f, 0.05f, 0.50f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(0.55f, 0.10f, 0.10f, 0.70f));
    if (ImGui::Button("Close", ImVec2(70.0f * scale, 28.0f * scale))) {
        *outClose = 1;
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
    }
    ImGui::PopStyleColor(2);

    /* B button / Escape also closes */
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        *outClose = 1;
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
    }

    /* Validation popup (modal) */
    renderValidationPopup();
    renderValidationModal(scale);
}

static void renderModManager(s32 winW, s32 winH)
{
    float scale = pdguiScaleFactor();

    /* Full-screen window */
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)winW, (float)winH));

    ImGuiWindowFlags wflags =
        ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##modmgr", nullptr, wflags)) {
        ImGui::End();
        return;
    }

    /* Viewport-relative dialog area — ultrawide-clamped via pdguiMenuWidth() */
    float dialogW = pdguiMenuWidth();
    float dialogH = pdguiMenuHeight();
    ImVec2 menuPos = pdguiMenuPos();
    float dialogX = menuPos.x;
    float dialogY = menuPos.y;

    /* Draw PD-style frame around the inner dialog area */
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    ImU32 borderCol = IM_COL32(80, 140, 200, 220);
    dl->AddRect(ImVec2(dialogX, dialogY),
                ImVec2(dialogX + dialogW, dialogY + dialogH),
                borderCol, 4.0f, 0, 2.0f * scale);

    /* Inner child window for the dialog */
    ImGui::SetNextWindowPos(ImVec2(dialogX, dialogY));
    ImGui::SetNextWindowSize(ImVec2(dialogW, dialogH));

    ImGuiWindowFlags innerFlags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoTitleBar;

    if (ImGui::BeginChild("##modmgr_inner", ImVec2(dialogW, dialogH), false, innerFlags)) {
        s32 outClose = 0;
        renderModManagerBody(dialogW, dialogH, scale, &outClose);
        if (outClose) s_Visible = false;
    }
    ImGui::EndChild();
    ImGui::End();
}

/* ========================================================================
 * Public C API
 * ======================================================================== */

extern "C" {

void pdguiModManagerShow(void)
{
    if (!s_Visible) {
        refreshSnapshot();
        s_Visible = true;
        sysLogPrintf(LOG_NOTE, "MODMGR: opened");
    }
}

void pdguiModManagerHide(void)
{
    s_Visible = false;
}

s32 pdguiModManagerIsVisible(void)
{
    return s_Visible ? 1 : 0;
}

void pdguiModManagerRender(s32 winW, s32 winH)
{
    if (!s_Visible) {
        return;
    }
    renderModManager(winW, winH);
}

/* ---- Embedded hub API (D3R-7) ---- */

/* Refresh the snapshot from outside (e.g. when hub switches to this tool). */
void pdguiModManagerRefreshSnapshot(void)
{
    refreshSnapshot();
}

/* Render modmgr content into an already-open ImGui child window context.
 * w/h are the available content area dimensions. scale is winH/480.
 * Sets *outClose to 1 if the user clicks Close, Apply, or presses B/Escape. */
void pdguiModManagerRenderContent(float w, float h, float scale, s32 *outClose)
{
    renderModManagerBody(w, h, scale, outClose);
}

} /* extern "C" */
