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
 *   2. Calls modmgrApplyChanges() — saves .modstate and rebuilds catalogs
 *      in-place (no forced title restart).
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
#include "pdgui_nav.h"
#include "pdgui_widgets.h"      /* Priority L: shared label-left widget helpers */
#include "system.h"
#include "assetcatalog.h"
#include "assetcatalog_scanner.h"

extern "C" {

/* Mod manager lifecycle */
void modmgrApplyChanges(void);
void modmgrSaveComponentState(void);
const char *modmgrGetModsDir(void);

/* Mod registry queries (modmgr.h) */
s32  modmgrGetCount(void);
void modmgrSetEnabled(s32 index, s32 enabled);
s32  modmgrCheckDependencies(s32 index, char *missing, s32 misslen);
void modmgrSwapOrder(s32 indexA, s32 indexB);
s32  modmgrExceedsThreshold(s32 index);
s32  modmgrGetSizeThresholdMB(void);
void modmgrSetSizeThresholdMB(s32 mb);
void modmgrSaveConfig(void);
s32  modmgrIsDirty(void);

/* UI accessor helpers (no struct layout needed) */
const char *modmgrGetModId(s32 index);
const char *modmgrGetModName(s32 index);
const char *modmgrGetModVersion(s32 index);
const char *modmgrGetModAuthor(s32 index);
const char *modmgrGetModDescription(s32 index);
const char *modmgrGetModBaseFallback(s32 index);
const char *modmgrGetModValidationError(s32 index);
const char *modmgrGetModDir(s32 index);  /* S306 BATCH 2: needed by delete flow */
s32  modmgrGetModEnabled(s32 index);

/* S306 BATCH 2: cross-file delete-flow hooks — defined in
 * pdgui_menu_mainmenu.cpp. Exposed here (file-scope extern "C") so the
 * Installed Mods tab can request a delete and render the confirm. */
void pdguiInterfaceRequestModDelete(s32 modIndex, const char *displayName,
                                    const char *modDirPath);
void pdguiInterfaceRenderDeleteConfirm(void);
s32  modmgrGetModValid(s32 index);
u32  modmgrGetModSizeBytes(s32 index);
s32  modmgrGetModNumDeps(s32 index);
const char *modmgrGetModDep(s32 index, s32 depIndex);

/* Catalog write API (D3R-6) */
void assetCatalogSetEnabled(const char *id, s32 enabled);
s32  assetCatalogGetUniqueCategories(char out[][CATALOG_CATEGORY_LEN], s32 maxout);

/* Video dimensions */
s32 viGetWidth(void);
s32 viGetHeight(void);

/* Button edge glow */
void pdguiDrawButtonEdgeGlow(f32 x, f32 y, f32 w, f32 h, s32 isActive);

/* Content-inset: cursor positioning clear of chrome border */
void pdguiSetCursorBelowTitle(float title_h);

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

/* Apply flow modal state:
 * 0=idle, 1=open popup next frame, 2=run apply, 3=done/waiting for acknowledge */
static int  s_ApplyFlowState      = 0;
static bool s_ApplyCloseAfterDone = false;

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
        case ASSET_PROJECTILE:  return "Projectiles";
        case ASSET_ENTITY:      return "Entities";
        case ASSET_TEXTURES:    return "Texture Packs";
        case ASSET_TEXTURE:     return "Textures";
        case ASSET_MATERIAL:    return "Materials";
        case ASSET_EFFECT:      return "Effects";
        case ASSET_SFX:         return "Sound Effects";
        case ASSET_MUSIC:       return "Music";
        case ASSET_AUDIO:       return "Audio";
        case ASSET_PROP:        return "Props";
        case ASSET_VEHICLE:     return "Vehicles";
        case ASSET_MISSION:     return "Missions";
        case ASSET_GAMEMODE:    return "Game Modes";
        case ASSET_BOT_PROFILE: return "Bot Profiles";
        case ASSET_SCENARIO:    return "Scenarios";
        case ASSET_UI:          return "UI";
        case ASSET_FONT:        return "Fonts";
        case ASSET_LANG:        return "Language";
        case ASSET_HUD:         return "HUD";
        case ASSET_THEME:       return "Themes";
        case ASSET_TOOL:        return "Tools";
        case ASSET_ARENA:       return "Arenas";
        case ASSET_BODY:        return "Bodies";
        case ASSET_HEAD:        return "Heads";
        case ASSET_MODEL:       return "Meshes / Models";
        case ASSET_ANIMATION:   return "Animations";
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
        case ASSET_PROJECTILE:  return "projectile.ini";
        case ASSET_ENTITY:      return "entity.ini";
        case ASSET_ARENA:       return "arena.ini";
        case ASSET_BODY:        return "body.ini";
        case ASSET_HEAD:        return "head.ini";
        case ASSET_MODEL:       return "mesh.ini";
        case ASSET_ANIMATION:   return "animation.ini";
        case ASSET_MATERIAL:    return "material.ini";
        case ASSET_TEXTURES:    return "textures.ini";
        case ASSET_TEXTURE:     return "texture.ini";
        case ASSET_EFFECT:      return "effect.ini";
        case ASSET_SFX:         return "sfx.ini";
        case ASSET_MUSIC:       return "music.ini";
        case ASSET_AUDIO:       return "audio.ini";
        case ASSET_PROP:        return "prop.ini";
        case ASSET_VEHICLE:     return "vehicle.ini";
        case ASSET_MISSION:     return "mission.ini";
        case ASSET_GAMEMODE:    return "gamemode.ini";
        case ASSET_BOT_PROFILE: return "botprofile.ini";
        case ASSET_SCENARIO:    return "scenario.ini";
        case ASSET_UI:          return "ui.ini";
        case ASSET_FONT:        return "font.ini";
        case ASSET_LANG:        return "lang.ini";
        case ASSET_HUD:         return "hud.ini";
        case ASSET_THEME:       return "theme.ini";
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

static void applyPendingSelectionToCatalog(void)
{
    for (int i = 0; i < s_NumEntries; i++) {
        if (!s_Entries[i].bundled &&
            s_Entries[i].enabled != s_Entries[i].orig_enabled) {
            assetCatalogSetEnabled(s_Entries[i].id, s_Entries[i].enabled);
        }
    }
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
    ASSET_WEAPON, ASSET_PROJECTILE, ASSET_ENTITY,
    ASSET_ARENA, ASSET_BODY, ASSET_HEAD, ASSET_MODEL,
    ASSET_ANIMATION,
    ASSET_TEXTURES, ASSET_TEXTURE, ASSET_MATERIAL, ASSET_EFFECT,
    ASSET_SFX, ASSET_MUSIC, ASSET_AUDIO,
    ASSET_PROP, ASSET_VEHICLE, ASSET_MISSION, ASSET_GAMEMODE,
    ASSET_BOT_PROFILE, ASSET_SCENARIO, ASSET_HUD, ASSET_UI,
    ASSET_FONT, ASSET_LANG, ASSET_THEME, ASSET_TOOL,
};
static const int s_NumAllTypes = (int)(sizeof(s_AllTypes) / sizeof(s_AllTypes[0]));

static void refreshSnapshot(void)
{
    s_NumEntries = 0;
    for (int t = 0; t < s_NumAllTypes; t++) {
        /* Mod Manager UI -- intentionally lists disabled entries so the user
         * can re-enable them.  See B-303 (catalog universality sweep). */
        assetCatalogIterateByTypeIncludingDisabled(s_AllTypes[t],
                                                    populateCallback,
                                                    &s_NumEntries);
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
        /* S311: "Base Game Asset" label uses theme tint_info. */
        ImGui::TextColored(pdguiVec4TintInfo(), "Base Game Asset");
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
        ImGui::TextColored(pdguiVec4TintSuccess(), "* Enabled");
    } else {
        ImGui::TextColored(pdguiVec4TintDanger(), "* Disabled");
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
        ImGui::TextColored(pdguiVec4TextWarning(229), "*");
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
        ASSET_WEAPON, ASSET_PROJECTILE, ASSET_ENTITY,
        ASSET_ARENA, ASSET_BODY, ASSET_HEAD, ASSET_MODEL,
        ASSET_ANIMATION,
        ASSET_TEXTURES, ASSET_TEXTURE, ASSET_MATERIAL, ASSET_EFFECT,
        ASSET_SFX, ASSET_MUSIC, ASSET_AUDIO,
        ASSET_PROP, ASSET_VEHICLE, ASSET_MISSION, ASSET_GAMEMODE,
        ASSET_BOT_PROFILE, ASSET_SCENARIO, ASSET_HUD, ASSET_UI,
        ASSET_FONT, ASSET_LANG, ASSET_THEME, ASSET_TOOL
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
        ImGui::TextDisabled("Add components to mods/{category}/{id}/ then click Apply Changes.");
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
    ImGui::SetNextWindowSize(ImVec2(pdguiScale(750.0f), pdguiScale(600.0f)), ImGuiCond_Always);

    if (!ImGui::BeginPopupModal("Validation Results", NULL,
                                ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove)) {
        return;
    }

    if (s_NumErrors == 0) {
        ImGui::TextColored(pdguiVec4TintSuccess(),
                           "All %d components validated OK.", s_NumEntries);
    } else {
        ImGui::Text("%d issue(s) found:", s_NumErrors);
        ImGui::Separator();
        ImGui::BeginChild("##validate_scroll",
                          ImVec2(0, -40.0f * scale), true);
        for (int i = 0; i < s_NumErrors; i++) {
            ImVec4 col = s_Errors[i].isError
                ? pdguiVec4TintDanger()
                : pdguiVec4TextWarning();
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
 * Installed Mods tab — shows discovered mods from modmgr registry
 * Features: no-mods-found, invalid manifest errors, dependency warnings,
 * size threshold prompts, load order reordering (up/down).
 * ======================================================================== */

static int  s_SelectedModIdx = -1;
static bool s_SizeConfirmPending = false;
static int  s_SizeConfirmIdx = -1;

static void renderInstalledModsTab(float scale)
{
    int modCount = modmgrGetCount();

    /* --- No mods found --- */
    if (modCount == 0) {
        ImGui::Spacing();
        ImGui::TextColored(pdguiVec4TextWarning(), "No mods found");
        ImGui::Spacing();
        const char *modsDir = modmgrGetModsDir();
        if (modsDir) {
            ImGui::TextWrapped("Place mod folders in:\n%s", modsDir);
        } else {
            ImGui::TextWrapped("Place mod folders in a 'mods/' directory next to the game executable.");
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Each mod needs a mod.json or audio.ini manifest file.");
        return;
    }

    for (int i = 0; i < modCount; i++) {
        ImGui::PushID(i);

        bool valid   = modmgrGetModValid(i) != 0;
        bool enabled = modmgrGetModEnabled(i) != 0;
        const char *name    = modmgrGetModName(i);
        const char *version = modmgrGetModVersion(i);
        const char *author  = modmgrGetModAuthor(i);
        const char *valErr  = modmgrGetModValidationError(i);
        bool isSelected = (s_SelectedModIdx == i);

        /* --- Invalid manifest: show error inline --- */
        if (!valid) {
            ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintDanger());
            ImGui::BulletText("%s", name[0] ? name : "(unknown mod)");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextColored(pdguiVec4TintDanger(204), "[INVALID]");
            if (valErr[0]) {
                ImGui::Indent(20.0f * scale);
                ImGui::TextColored(pdguiVec4TintDanger(230), "%s", valErr);
                ImGui::Unindent(20.0f * scale);
            }
            ImGui::PopID();
            continue;
        }

        /* --- Valid mod: enable/disable toggle + info --- */
        bool en = enabled;
        char chkLabel[80];
        snprintf(chkLabel, sizeof(chkLabel), "##mod_%d", i);
        if (ImGui::Checkbox(chkLabel, &en)) {
            if (en && !enabled) {
                /* Check size threshold before enabling */
                if (modmgrExceedsThreshold(i)) {
                    s_SizeConfirmPending = true;
                    s_SizeConfirmIdx = i;
                } else {
                    modmgrSetEnabled(i, 1);
                    modmgrSaveConfig();   /* persist mod-level enable immediately */
                }
            } else if (!en && enabled) {
                modmgrSetEnabled(i, 0);
                modmgrSaveConfig();       /* persist mod-level disable immediately */
            }
            pdguiPlaySound(en ? PDGUI_SND_TOGGLEON : PDGUI_SND_TOGGLEOFF);
        }
        ImGui::SameLine();

        /* Selectable name */
        char label[160];
        snprintf(label, sizeof(label), "%s v%s", name, version);
        if (ImGui::Selectable(label, isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
            s_SelectedModIdx = i;
        }

        /* S306 BATCH 2: right-click + controller X-button → context menu
         * with "Delete Mod..." (opens the Interface-tab confirm modal).
         * The delete helper lives in pdgui_menu_mainmenu.cpp; we forward
         * the mod index + display name + on-disk dir so the confirm
         * dialog can show a clear path echo. Declared file-scope in the
         * extern "C" block at the top of this file. */
        {
            char ctxId[32];
            snprintf(ctxId, sizeof(ctxId), "##modctx_%d", i);
            if (ImGui::BeginPopupContextItem(ctxId)) {
                const char *dir = modmgrGetModDir(i);
                ImGui::TextDisabled("%s", name);
                ImGui::Separator();
                if (ImGui::MenuItem("Delete Mod...")) {
                    if (dir) {
                        pdguiInterfaceRequestModDelete(i, name, dir);
                    }
                }
                ImGui::EndPopup();
            }
        }

        /* Dependency warning */
        if (enabled) {
            char missing[256];
            s32 nmiss = modmgrCheckDependencies(i, missing, sizeof(missing));
            if (nmiss > 0) {
                ImGui::SameLine();
                ImGui::TextColored(pdguiVec4TextWarning(220), "[deps: %s]", missing);
            }
        }

        /* Reorder buttons */
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50.0f * scale);
        {
            bool isFirst = (i == 0);
            bool isLast  = (i == modCount - 1);
            if (isFirst) ImGui::BeginDisabled();
            char upLabel[16];
            snprintf(upLabel, sizeof(upLabel), "^##up%d", i);
            if (ImGui::SmallButton(upLabel)) {
                modmgrSwapOrder(i, i - 1);
                if (s_SelectedModIdx == i) s_SelectedModIdx = i - 1;
            }
            if (isFirst) ImGui::EndDisabled();
            ImGui::SameLine();
            if (isLast) ImGui::BeginDisabled();
            char dnLabel[16];
            snprintf(dnLabel, sizeof(dnLabel), "v##dn%d", i);
            if (ImGui::SmallButton(dnLabel)) {
                modmgrSwapOrder(i, i + 1);
                if (s_SelectedModIdx == i) s_SelectedModIdx = i + 1;
            }
            if (isLast) ImGui::EndDisabled();
        }

        ImGui::PopID();
    }

    /* S306 BATCH 2: render the shared delete-confirm modal here too so
     * deletes initiated inside the Modding Hub don't depend on Settings →
     * Interface being open. The state is file-scope in mainmenu.cpp;
     * this call is idempotent (no-op unless a delete request is live). */
    pdguiInterfaceRenderDeleteConfirm();

    /* --- Size threshold confirmation modal --- */
    if (s_SizeConfirmPending) {
        ImGui::OpenPopup("Large Mod");
        s_SizeConfirmPending = false;
    }
    if (ImGui::BeginPopupModal("Large Mod", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        u32 sizeBytes = modmgrGetModSizeBytes(s_SizeConfirmIdx);
        float sizeMB = (float)sizeBytes / (1024.0f * 1024.0f);
        ImGui::Text("This mod is %.1f MB (threshold: %d MB).", sizeMB, modmgrGetSizeThresholdMB());
        ImGui::Text("Enable it anyway?");
        ImGui::Separator();
        if (ImGui::Button("Yes, Enable", ImVec2(120 * scale, 0))) {
            modmgrSetEnabled(s_SizeConfirmIdx, 1);
            modmgrSaveConfig();   /* persist mod-level enable immediately */
            ImGui::CloseCurrentPopup();
            pdguiPlaySound(PDGUI_SND_TOGGLEON);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120 * scale, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

/* Mod detail panel for the Installed Mods tab */
static void renderModDetails(float scale)
{
    if (s_SelectedModIdx < 0 || s_SelectedModIdx >= modmgrGetCount()) {
        ImGui::TextDisabled("Select a mod to see details.");
        return;
    }

    int i = s_SelectedModIdx;
    const char *name    = modmgrGetModName(i);
    const char *id      = modmgrGetModId(i);
    const char *version = modmgrGetModVersion(i);
    const char *author  = modmgrGetModAuthor(i);
    const char *desc    = modmgrGetModDescription(i);
    const char *fallback = modmgrGetModBaseFallback(i);
    u32 sizeBytes = modmgrGetModSizeBytes(i);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::TextWrapped("%s", name);
    ImGui::PopStyleColor();
    ImGui::Separator();

    ImGui::TextDisabled("ID:"); ImGui::SameLine(); ImGui::Text("%s", id);
    ImGui::TextDisabled("Version:"); ImGui::SameLine(); ImGui::Text("%s", version);
    if (author[0]) { ImGui::TextDisabled("Author:"); ImGui::SameLine(); ImGui::Text("%s", author); }
    if (fallback[0]) { ImGui::TextDisabled("Fallback:"); ImGui::SameLine(); ImGui::Text("%s", fallback); }

    if (sizeBytes > 0) {
        ImGui::TextDisabled("Size:");
        ImGui::SameLine();
        if (sizeBytes >= 1024u * 1024u)
            ImGui::Text("%.1f MB", (float)sizeBytes / (1024.0f * 1024.0f));
        else
            ImGui::Text("%.1f KB", (float)sizeBytes / 1024.0f);
    }

    s32 numDeps = modmgrGetModNumDeps(i);
    if (numDeps > 0) {
        ImGui::Spacing();
        ImGui::TextDisabled("Dependencies:");
        for (s32 d = 0; d < numDeps; d++) {
            ImGui::BulletText("%s", modmgrGetModDep(i, d));
        }
    }

    if (desc[0]) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", desc);
    }

    /* Enabled status */
    ImGui::Spacing();
    ImGui::Separator();
    if (modmgrGetModEnabled(i)) {
        ImGui::TextColored(pdguiVec4TintSuccess(), "* Enabled");
    } else {
        ImGui::TextColored(pdguiVec4TintDanger(), "* Disabled");
    }
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
    pdguiSetCursorBelowTitle(0.0f); /* content-inset: protect left/top from chrome border */
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::Text("MOD MANAGER");
    ImGui::PopStyleColor();
    ImGui::Separator();

    /* --- Tab bar --- */
    /* ACTION_MENU_TAB_PREV/NEXT cycle tabs for keyboard and gamepad. */
    static s32 s_ModMgrPendingTab = -1;
    const s32 k_ModMgrTabCount = 3;
    /* Visual/UI order: Installed Mods (2), By Category (0), By Mod (1). */
    static const s32 k_ModMgrOrder[3] = { 2, 0, 1 };
    s32 s_ModMgrUiIdx = (s_Tab == 2) ? 0 : (s_Tab == 0 ? 1 : 2);
    if (pdguiMenuTabPrevPressed()) {
        s_ModMgrUiIdx = (s_ModMgrUiIdx - 1 + k_ModMgrTabCount) % k_ModMgrTabCount;
        s_ModMgrPendingTab = s_ModMgrUiIdx;
        pdguiPlaySound(PDGUI_SND_SWIPE);
    } else if (pdguiMenuTabNextPressed()) {
        s_ModMgrUiIdx = (s_ModMgrUiIdx + 1) % k_ModMgrTabCount;
        s_ModMgrPendingTab = s_ModMgrUiIdx;
        pdguiPlaySound(PDGUI_SND_SWIPE);
    }

    if (ImGui::BeginTabBar("##modmgr_tabs")) {
        const ImGuiTabItemFlags sel0 = (s_ModMgrPendingTab == 0) ? ImGuiTabItemFlags_SetSelected : 0;
        const ImGuiTabItemFlags sel1 = (s_ModMgrPendingTab == 1) ? ImGuiTabItemFlags_SetSelected : 0;
        const ImGuiTabItemFlags sel2 = (s_ModMgrPendingTab == 2) ? ImGuiTabItemFlags_SetSelected : 0;

        if (ImGui::BeginTabItem("Installed Mods", nullptr, sel0)) {
            s_Tab = k_ModMgrOrder[0];
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("By Category", nullptr, sel1)) {
            s_Tab = k_ModMgrOrder[1];
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("By Mod", nullptr, sel2)) {
            s_Tab = k_ModMgrOrder[2];
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        s_ModMgrPendingTab = -1;
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
    } else if (s_Tab == 1) {
        renderByModTab(scale);
    } else if (s_Tab == 2) {
        renderInstalledModsTab(scale);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    /* Right panel: details */
    /* Priority L (2026-04-25): NavFlattened layout panel. */
    ImGui::BeginChild("##modmgr_details", ImVec2(rightW, panelH),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
    if (s_Tab == 2) {
        renderModDetails(scale);
    } else {
        renderDetails(scale);
    }
    ImGui::EndChild();

    /* --- Footer --- */
    ImGui::Separator();
    ImGui::SetCursorPosY(dialogH - footerH + ImGui::GetStyle().ItemSpacing.y);

    /* Pending change count */
    int pending = countPending();
    if (pending > 0) {
        ImGui::TextColored(pdguiVec4TextWarning(),
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
        bool modDirty = (modmgrIsDirty() != 0);
        if (pending > 0) {
            snprintf(applyLabel, sizeof(applyLabel), "Apply Changes (%d)", pending);
        } else if (modDirty) {
            strncpy(applyLabel, "Apply Changes*", sizeof(applyLabel));
        } else {
            strncpy(applyLabel, "Apply Changes", sizeof(applyLabel));
        }

        bool applyDisabled = (pending == 0 && !modDirty);
        if (applyDisabled) ImGui::BeginDisabled();

        if (ImGui::Button(applyLabel, ImVec2(160.0f * scale, 28.0f * scale))) {
            applyPendingSelectionToCatalog();
            s_ApplyCloseAfterDone = false;
            s_ApplyFlowState = 1;
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

    /* Close — S311: theme danger tint at sub-opaque alpha. */
    ImGui::PushStyleColor(ImGuiCol_Button, pdguiVec4TintDanger(128));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pdguiVec4TintDanger(179));
    if (ImGui::Button("Close", ImVec2(70.0f * scale, 28.0f * scale))) {
        bool hasDirty = (pending > 0) || (modmgrIsDirty() != 0);
        if (hasDirty) {
            ImGui::OpenPopup("Unsaved Changes");
        } else {
            *outClose = 1;
        }
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
    }
    ImGui::PopStyleColor(2);

    /* B button / Escape also closes — same guard */
    if (pdguiMenuCancelPressed()) {
        bool hasDirty = (pending > 0) || (modmgrIsDirty() != 0);
        if (hasDirty) {
            ImGui::OpenPopup("Unsaved Changes");
        } else {
            *outClose = 1;
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
        }
    }

    /* Unsaved Changes guard modal */
    if (ImGui::BeginPopupModal("Unsaved Changes", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("You have unsaved changes.");
        ImGui::TextUnformatted("Apply them now, or discard?");
        ImGui::Spacing();
        if (ImGui::Button("Apply & Close", ImVec2(120.0f * scale, 0))) {
            ImGui::CloseCurrentPopup();
            applyPendingSelectionToCatalog();
            s_ApplyCloseAfterDone = true;
            s_ApplyFlowState = 1;
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard & Close", ImVec2(120.0f * scale, 0))) {
            ImGui::CloseCurrentPopup();
            *outClose = 1;
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80.0f * scale, 0))) {
            ImGui::CloseCurrentPopup();
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
        }
        ImGui::EndPopup();
    }

    if (s_ApplyFlowState > 0) {
        ImGuiIO &io = ImGui::GetIO();
        ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(pdguiScale(600.0f), pdguiScale(240.0f)));

        ImGuiWindowFlags applyFlags = ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings;

        /* Match updater UX: neutral during work, green-tinted on success
         * (S311 theme). Snapshot the scope before rendering because the apply
         * state advances (or closes) inside this window. */
        const bool pushedApplySuccessBg = s_ApplyFlowState >= 3;
        if (pushedApplySuccessBg) {
            ImGui::PushStyleColor(ImGuiCol_WindowBg, pdguiVec4TintSuccess(60));
        }

        if (ImGui::Begin("Applying Changes", NULL, applyFlags)) {
            if (s_ApplyFlowState <= 1) {
                ImGui::TextUnformatted("Applying mod changes...");
                ImGui::TextDisabled("Rebuilding catalog and refreshing assets.");
                ImGui::Spacing();
                ImGui::ProgressBar(0.5f, ImVec2(-1, 24), "");
                /* Let this frame paint the window before synchronous apply. */
                s_ApplyFlowState = 2;
            } else if (s_ApplyFlowState == 2) {
                modmgrApplyChanges();
                refreshSnapshot();
                s_ApplyFlowState = 3;
            } else {
                ImGui::TextUnformatted("Apply complete.");
                ImGui::TextDisabled("Catalog changes are live. No restart required.");
                ImGui::Spacing();

                float btnWidth = 120.0f * scale;
                ImGui::SetCursorPosX((ImGui::GetWindowWidth() - btnWidth) * 0.5f);
                if (ImGui::Button(s_ApplyCloseAfterDone ? "OK & Close" : "OK",
                                  ImVec2(btnWidth, 0))) {
                    if (s_ApplyCloseAfterDone) {
                        *outClose = 1;
                    }
                    s_ApplyFlowState = 0;
                    s_ApplyCloseAfterDone = false;
                }
            }
        }
        ImGui::End();
        if (pushedApplySuccessBg) {
            ImGui::PopStyleColor();
        }
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
    ImU32 borderCol = pdguiImU32TintInfo(220);
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

    /* Priority L (2026-04-25): NavFlattened layout panel. */
    if (ImGui::BeginChild("##modmgr_inner", ImVec2(dialogW, dialogH),
                          ImGuiChildFlags_NavFlattened, innerFlags)) {
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
        s_ApplyFlowState = 0;
        s_ApplyCloseAfterDone = false;
        s_Visible = true;
        sysLogPrintf(LOG_NOTE, "MODMGR: opened");
    }
}

void pdguiModManagerHide(void)
{
    s_ApplyFlowState = 0;
    s_ApplyCloseAfterDone = false;
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
 * Sets *outClose to 1 if the user clicks Close or presses B/Escape. */
void pdguiModManagerRenderContent(float w, float h, float scale, s32 *outClose)
{
    renderModManagerBody(w, h, scale, outClose);
}

} /* extern "C" */
