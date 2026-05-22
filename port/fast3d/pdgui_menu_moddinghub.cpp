/**
 * pdgui_menu_moddinghub.cpp -- D3R-7 Modding Hub
 *
 * A single standalone window with three tools:
 *   0 - Mod Manager  (delegates to pdgui_menu_modmgr.cpp content renderer)
 *   1 - INI Editor   (browse catalog entries, edit .ini manifests)
 *   2 - Model Scale Tool (read/write model binary scale at offset 0x10)
 *
 * Entry: pdguiModdingHubShow() — opened from main menu "Modding..." button.
 * pdguiModdingHubRender() is called every frame from pdgui_backend.cpp.
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
#include <stdlib.h>
#include <sys/stat.h>
#include <stdint.h>
#include <errno.h>
#include <stdarg.h>

#include "glad/glad.h"
#include "imgui/imgui.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_layout.h"
#include "pdgui_nav.h"
#include "pdgui_widgets.h"      /* Priority L: shared label-left widget helpers */
#include "pdgui_theme.h"
#include "pdgui_nineslice.h"
#include "pdgui_filebrowser.h"
#include "pdgui_font_mod.h"
#include "pdgui_model_preview.h"
#include "pdgui_weapon_graph_node_editor.h"
#include "system.h"
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "pdgui_charpreview.h"
#include "fs.h"
#include "modarchive.h"
#include "modpack.h"
#include "modpack_pdmod.h"
#include "weapon_graph_archive.h"
#include "weapon_graph_runtime.h"
#include "../external/stb_image.h"

/* ========================================================================
 * Forward declarations for C symbols
 * ======================================================================== */

extern "C" {

void modmgrApplyChanges(void);
void modmgrRescanDirectory(void);
s32  modmgrGetCount(void);
const char *modmgrGetModId(s32 index);
void modmgrSetEnabled(s32 index, s32 enabled);
void modmgrSaveConfig(void);
s32  viGetWidth(void);
s32  viGetHeight(void);
void pdguiDrawButtonEdgeGlow(f32 x, f32 y, f32 w, f32 h, s32 isActive);

/* Mod Manager embedded content (D3R-7 API added to pdgui_menu_modmgr.cpp) */
void pdguiModManagerRefreshSnapshot(void);
void pdguiModManagerRenderContent(float w, float h, float scale, s32 *outClose);

/* Audio Mod Menu (Batch A-3 — pdgui_menu_audiomod.cpp) */
void pdguiAudioModRefresh(void);
void pdguiAudioModRender(float contentW, float contentH, float scale);

/* Skin Editor (Batch S-1 — pdgui_skin_editor.cpp) */
void pdguiSkinEditorRefresh(void);
void pdguiSkinEditorRender(float contentW, float contentH, float scale);
void pdguiSkinEditorDismissTransientUi(void);
s32  pdguiSkinEditorTryConsumeHubEscape(void);

/* Map Import Pipeline (L3 — port/src/mapimport.c) */
s32 mapImportExists(const char *map_name);
const char *mapImportResultStr(s32 result);

/* Thin C wrappers for map import — avoids including mapimport.h/types.h.
 * Returns 0 on success, error code on failure. Error msg written to errbuf. */
s32 mapImportRunFull(const char *source_dir, const char *map_name,
                     char *errbuf, s32 errbuflen,
                     s32 *out_num_rooms, s32 *out_num_pads,
                     s32 *out_generated_spawns);

/* Spawn pool diagnostic */
s32 spawnPoolSmokeTest(void);
s32 spawnPoolSmokeAll(void);
void spawnPoolSmokeWriteCSV(const char *path);

} /* extern "C" */

/* ========================================================================
 * Constants
 * ======================================================================== */

#define HUB_DIALOG_W     900.0f
#define HUB_DIALOG_H     560.0f
#define HUB_INI_MAX_KEYS  64
#define HUB_INI_KEY_LEN   128
#define HUB_INI_VAL_LEN   256
#define HUB_MAX_ENTRIES   256

/* ========================================================================
 * Local PdButton helper
 * ======================================================================== */

static bool PdButton(const char *label, const ImVec2 &size = ImVec2(0,0))
{
    bool clicked = ImGui::Button(label, size);
    if (clicked) pdguiPlaySound(PDGUI_SND_SELECT);
    if (ImGui::IsItemHovered() || ImGui::IsItemActive() || ImGui::IsItemFocused()) {
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        pdguiDrawButtonEdgeGlow(rmin.x, rmin.y,
                                rmax.x - rmin.x, rmax.y - rmin.y,
                                ImGui::IsItemActive() ? 1 : 0);
    }
    return clicked;
}

/* ========================================================================
 * Hub-level state
 * ======================================================================== */

static bool s_Visible    = false;
static int  s_ActiveTool = 0;    /* 0=ModManager, 1=INI, 2=Scale, 3=Pack, 4=Audio, 5=SkinEditor, 6=MapImport, 7=NineSlice, 8=FontMod, 9=Weapons */
static int  s_LastLoggedTool = -1;
static int  s_ModHubOpenFrame = -1; /* B-210: frame stamp for open-input debounce */
static void chromeToolReset(void);
static void fontToolReset(void);
static void renderFontTool(float w, float h, float scale);
static void weaponToolRefresh(void);
static void renderWeaponTool(float w, float h, float scale);

static void moddingHubClose(const char *reason)
{
    if (!s_Visible) {
        return;
    }
    s_Visible = false;
    pdguiSkinEditorDismissTransientUi();
    chromeToolReset();
    sysLogPrintf(LOG_NOTE, "MODHUB: closed (%s)", reason ? reason : "no-reason");
}

static void moddingHubCloseFromUi(const char *reason)
{
    moddingHubClose(reason);
    pdguiPlaySound(PDGUI_SND_KBCANCEL);
}

/* ========================================================================
 * Map Import state (Tab 6)
 * ======================================================================== */

static char s_MapImpPath[512]    = "";
static char s_MapImpName[64]     = "";
static char s_MapImpError[256]   = "";
static char s_MapImpStatus[128]  = "";
static bool s_MapImpStatusOk     = true;
static int  s_MapImpNumRooms     = 0;
static int  s_MapImpNumPads      = 0;
static int  s_MapImpGenSpawns    = 0;
static bool s_MapImpRunning      = false;
static bool s_MapImpDone         = false;

/* Forward declarations for Map Import (defined after other tools) */
static void importReset(void);
static void renderMapImport(float w, float h, float scale);

/* Forward declarations for Nine-Slice Chrome tool (Tab 7) */
static void chromeToolReset(void);
static void renderChromeTool(float w, float h, float scale);

/* ========================================================================
 * Nine-Slice Chrome tool state (Tab 7)
 * ======================================================================== */

/* Hard caps to prevent runaway allocations from high-res inputs or large
 * scale factors. 4096 covers most authoring needs and keeps a worst-case
 * RGBA8 buffer at ~64 MB (output) and ~64 MB (input). */
#define CHROME_MAX_IMG_DIM  4096
#define CHROME_MAX_OUT_DIM  4096

static char   s_ChromeImgPath[FS_MAXPATH] = "";
static char   s_ChromeModName[96] = "my-ui-chrome";
static char   s_ChromeStatus[192] = "";
static bool   s_ChromeStatusOk = true;
static bool   s_ChromeLrSymmetry = true;
static bool   s_ChromeTbSymmetry = true;
static bool   s_ChromeCenterTile = true;
static bool   s_ChromeEdgeTile = false;
static bool   s_ChromeDesaturate = false;
static s32    s_ChromeDesaturatePct = 100;
static s32    s_ChromeTrimL = 0;
static s32    s_ChromeTrimR = 0;
static s32    s_ChromeTrimT = 0;
static s32    s_ChromeTrimB = 0;
static s32    s_ChromeScaleXPct = 100;
static s32    s_ChromeScaleYPct = 100;
static s32    s_ChromeCenterCutAxis = 0; /* 0=None, 1=Vertical(height), 2=Horizontal(width) */
static s32    s_ChromeCenterCutPct = 0;
static s32    s_ChromeInsetL = 16;
static s32    s_ChromeInsetR = 16;
static s32    s_ChromeInsetT = 16;
static s32    s_ChromeInsetB = 16;
/* Proportional insets (0..50 percent of the corresponding output dim). These
 * are the authoritative values when s_ChromeProportionalInsets is true; the
 * pixel-space insets above are then recomputed each preview pass from the
 * current output dimensions, so changing Scale X/Y keeps the visual border
 * proportion stable. */
static bool   s_ChromeProportionalInsets = true;
static float  s_ChromeInsetLPct = 12.5f;
static float  s_ChromeInsetRPct = 12.5f;
static float  s_ChromeInsetTPct = 12.5f;
static float  s_ChromeInsetBPct = 12.5f;
/* Border Scale decouples on-screen corner size (dst_corner_px) from the
 * source slice location (src_inset). 1.0 = corners render at source size;
 * 2.0 = corners render twice as big as their source rect. */
static float  s_ChromeBorderScale = 1.0f;
static s32    s_ChromeImgW = 0;
static s32    s_ChromeImgH = 0;
static s32    s_ChromeOutW = 0;
static s32    s_ChromeOutH = 0;
static u8    *s_ChromePixels = NULL; /* RGBA8, owned by tool */
static GLuint s_ChromeTex = 0;       /* unused since S293 (kept to avoid renaming fallback paths) */
static u8    *s_ChromePreviewPixels = NULL; /* optional processed preview buffer */
static GLuint s_ChromePreviewTex = 0;
static s32    s_ChromePreviewTexW = 0; /* tracks current GL tex dims so we can glTexSubImage2D vs glTexImage2D */
static s32    s_ChromePreviewTexH = 0;

/* ========================================================================
 * INI Editor state
 * ======================================================================== */

struct IniEntry {
    char id[CATALOG_ID_LEN];
    char dirpath[FS_MAXPATH];
    asset_type_e type;
    int  bundled;
};

struct IniKV {
    char key[HUB_INI_KEY_LEN];
    char val[HUB_INI_VAL_LEN];
    bool is_comment;   /* line was a comment — displayed greyed, not editable */
    bool is_blank;     /* blank line — preserved in save */
};

static IniEntry s_IniEntries[HUB_MAX_ENTRIES];
static int      s_IniNumEntries = 0;
static int      s_IniSelected   = -1;

static IniKV    s_IniPairs[HUB_INI_MAX_KEYS];
static int      s_IniNumPairs   = 0;
static bool     s_IniDirty      = false;
static char     s_IniStatusMsg[128] = "";
static bool     s_IniStatusOk  = true;
static bool     s_IniEmptyLogged = false;

/* ========================================================================
 * Weapon browser state (Tab 9)
 * ======================================================================== */

#define HUB_WEAPON_TEXT_PREVIEW_LEN 8192
#define HUB_WEAPON_GRAPH_MAX_NODES  PDGUI_WEAPON_GRAPH_MAX_NODES
#define HUB_WEAPON_GRAPH_MAX_EDGES  PDGUI_WEAPON_GRAPH_MAX_EDGES
#define HUB_WEAPON_GRAPH_PARAM_LEN  PDGUI_WEAPON_GRAPH_PARAM_LEN
#define HUB_WEAPON_GRAPH_SCOPE_LEN  PDGUI_WEAPON_GRAPH_SCOPE_LEN

static char s_WeaponSelectedId[CATALOG_ID_LEN] = "";
static char s_WeaponLoadedId[CATALOG_ID_LEN] = "";
static char s_WeaponArchivePath[FS_MAXPATH] = "";
static char s_WeaponStatus[192] = "";
static bool s_WeaponStatusOk = true;
static char s_WeaponIniPreview[4096] = "";
static char s_WeaponGraphPreview[HUB_WEAPON_TEXT_PREVIEW_LEN] = "";
static char s_WeaponNestedPreview[4096] = "";

enum WeaponImportTarget {
    WEAPON_IMPORT_NONE = 0,
    WEAPON_IMPORT_MODEL,
    WEAPON_IMPORT_TEXTURE,
    WEAPON_IMPORT_ANIMATION,
    WEAPON_IMPORT_AUDIO,
    WEAPON_IMPORT_PROJECTILE,
    WEAPON_IMPORT_ENTITY,
    WEAPON_IMPORT_GRAPH,
};

static bool s_WeaponEditActive = false;
static bool s_WeaponTemplateMenuOpen = false;
static bool s_WeaponMeshPickerOpen = false;
static bool s_WeaponMeshPickerShowNonWeapon = false;
static s32  s_WeaponEditWeaponId = -1;
static bool s_WeaponEditDualWieldable = false;
static char s_WeaponEditTemplateId[CATALOG_ID_LEN] = "";
static char s_WeaponEditTemplateArchive[FS_MAXPATH] = "";
static char s_WeaponEditDisplayName[96] = "";
static char s_WeaponEditSlug[64] = "";
static char s_WeaponEditCatalogId[CATALOG_ID_LEN] = "";
static char s_WeaponEditModelRef[CATALOG_ID_LEN] = "";
static char s_WeaponEditTextureRef[CATALOG_ID_LEN] = "";
static char s_WeaponEditAnimationRef[CATALOG_ID_LEN] = "";
static char s_WeaponEditAudioRef[CATALOG_ID_LEN] = "";
static char s_WeaponEditProjectileRef[CATALOG_ID_LEN] = "";
static char s_WeaponEditEntityRef[CATALOG_ID_LEN] = "";
static char s_WeaponImportModelPath[FS_MAXPATH] = "";
static char s_WeaponImportTexturePath[FS_MAXPATH] = "";
static char s_WeaponImportAnimationPath[FS_MAXPATH] = "";
static char s_WeaponImportAudioPath[FS_MAXPATH] = "";
static char s_WeaponImportProjectilePath[FS_MAXPATH] = "";
static char s_WeaponImportEntityPath[FS_MAXPATH] = "";
static char s_WeaponImportGraphPath[FS_MAXPATH] = "";
static char s_WeaponMeshPickerSelected[CATALOG_ID_LEN] = "";
static char s_WeaponEditGraph[HUB_WEAPON_TEXT_PREVIEW_LEN] = "";
static char s_WeaponEditNested[4096] = "";
static WeaponImportTarget s_WeaponImportTarget = WEAPON_IMPORT_NONE;

typedef PdWeaponGraphModuleDef WeaponGraphModuleDef;
typedef PdWeaponGraphContextDef WeaponGraphContextDef;
typedef PdWeaponGraphNodeEdit WeaponGraphNodeEdit;
typedef PdWeaponGraphEdgeEdit WeaponGraphEdgeEdit;

static PdWeaponGraphEditModel s_WeaponGraphModel;
#define s_WeaponGraphNodes            (s_WeaponGraphModel.nodes)
#define s_WeaponGraphEdges            (s_WeaponGraphModel.edges)
#define s_WeaponGraphNodeCount        (s_WeaponGraphModel.node_count)
#define s_WeaponGraphEdgeCount        (s_WeaponGraphModel.edge_count)
#define s_WeaponGraphModulePick       (s_WeaponGraphModel.module_pick)
#define s_WeaponGraphScopePick        (s_WeaponGraphModel.scope_pick)
#define s_WeaponGraphEdgeFrom         (s_WeaponGraphModel.edge_from)
#define s_WeaponGraphEdgeTo           (s_WeaponGraphModel.edge_to)
#define s_WeaponGraphPrimaryExport    (s_WeaponGraphModel.primary_export)
#define s_WeaponGraphSecondaryExport  (s_WeaponGraphModel.secondary_export)
#define s_WeaponGraphContextEnabled   (s_WeaponGraphModel.context_enabled)

/* ========================================================================
 * INI Editor — helpers
 * ======================================================================== */

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

static const char *hubToolName(int tool)
{
    static const char *kNames[] = {
        "Mod Manager",
        "INI Editor",
        "Scale Tool",
        "Mod Pack",
        "Audio Mods",
        "Skin Editor",
        "Map Import",
        "Menu Style",
        "Font Mod",
        "Weapons",
    };
    if (tool < 0 || tool >= (int)(sizeof(kNames) / sizeof(kNames[0]))) {
        return "Unknown";
    }
    return kNames[tool];
}

static void iniCollectCallback(const asset_entry_t *e, void *ud)
{
    int *n = (int *)ud;
    if (*n >= HUB_MAX_ENTRIES) return;
    /* Only list entries that have an associated .ini */
    if (iniNameForType(e->type)[0] == '\0') return;
    IniEntry &ie = s_IniEntries[(*n)++];
    strncpy(ie.id, e->id, CATALOG_ID_LEN - 1);
    ie.id[CATALOG_ID_LEN - 1] = '\0';
    strncpy(ie.dirpath, e->dirpath, FS_MAXPATH - 1);
    ie.dirpath[FS_MAXPATH - 1] = '\0';
    ie.type    = e->type;
    ie.bundled = e->bundled;
}

static const asset_type_e s_AllTypes[] = {
    ASSET_MAP, ASSET_CHARACTER, ASSET_SKIN, ASSET_BOT_VARIANT,
    ASSET_WEAPON, ASSET_PROJECTILE, ASSET_ENTITY,
    ASSET_TEXTURES, ASSET_SFX, ASSET_MUSIC,
    ASSET_PROP, ASSET_VEHICLE, ASSET_MISSION, ASSET_UI, ASSET_TOOL
};
static const int s_NumAllTypes = (int)(sizeof(s_AllTypes)/sizeof(s_AllTypes[0]));

static void iniRefreshEntries(void)
{
    s_IniNumEntries = 0;
    int modEntries = 0;
    int baseEntries = 0;

    for (int t = 0; t < s_NumAllTypes; t++) {
        int before = s_IniNumEntries;
        /* Modding Hub INI authoring -- modder needs to see disabled
         * entries to re-enable / edit them.  See B-303 (catalog
         * universality sweep). */
        assetCatalogIterateByTypeIncludingDisabled(s_AllTypes[t],
                                                    iniCollectCallback,
                                                    &s_IniNumEntries);
        int added = s_IniNumEntries - before;
        if (added > 0) {
            sysLogPrintf(LOG_NOTE,
                         "modhub.ini: type=%d added=%d running_total=%d",
                         (int)s_AllTypes[t], added, s_IniNumEntries);
        }
    }

    for (int i = 0; i < s_IniNumEntries; i++) {
        if (s_IniEntries[i].bundled) {
            baseEntries++;
        } else {
            modEntries++;
        }
    }

    s_IniSelected = -1;
    s_IniNumPairs = 0;
    s_IniDirty    = false;
    s_IniStatusMsg[0] = '\0';
    s_IniEmptyLogged = false;

    sysLogPrintf(LOG_NOTE,
                 "modhub.ini: refresh complete total=%d mod=%d base=%d",
                 s_IniNumEntries, modEntries, baseEntries);
}

static void iniLoadFile(int idx)
{
    if (idx < 0 || idx >= s_IniNumEntries) return;
    const IniEntry &ie = s_IniEntries[idx];
    const char *iniName = iniNameForType(ie.type);
    if (iniName[0] == '\0') return;

    char path[FS_MAXPATH + 32];
    snprintf(path, sizeof(path), "%s/%s", ie.dirpath, iniName);

    FILE *f = fopen(path, "r");
    if (!f) {
        s_IniNumPairs = 0;
        snprintf(s_IniStatusMsg, sizeof(s_IniStatusMsg), "File not found: %s", iniName);
        s_IniStatusOk = false;
        sysLogPrintf(LOG_WARNING,
                     "modhub.ini: load failed id='%s' path='%s' (not found)",
                     ie.id, path);
        return;
    }

    sysLogPrintf(LOG_NOTE,
                 "modhub.ini: loading id='%s' path='%s'",
                 ie.id, path);

    s_IniNumPairs = 0;
    char line[HUB_INI_KEY_LEN + HUB_INI_VAL_LEN + 4];
    while (fgets(line, sizeof(line), f) && s_IniNumPairs < HUB_INI_MAX_KEYS) {
        /* Strip trailing newline */
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }

        IniKV &kv = s_IniPairs[s_IniNumPairs++];
        kv.is_comment = false;
        kv.is_blank   = false;

        if (len == 0) {
            kv.key[0] = '\0'; kv.val[0] = '\0';
            kv.is_blank = true;
            continue;
        }
        if (line[0] == '#' || line[0] == ';') {
            strncpy(kv.key, line, HUB_INI_KEY_LEN - 1);
            kv.key[HUB_INI_KEY_LEN - 1] = '\0';
            kv.val[0] = '\0';
            kv.is_comment = true;
            continue;
        }
        /* Parse key = value */
        char *eq = strchr(line, '=');
        if (eq) {
            int klen = (int)(eq - line);
            /* Trim trailing spaces from key */
            while (klen > 0 && line[klen-1] == ' ') klen--;
            strncpy(kv.key, line, klen < HUB_INI_KEY_LEN ? klen : HUB_INI_KEY_LEN - 1);
            kv.key[klen < HUB_INI_KEY_LEN ? klen : HUB_INI_KEY_LEN - 1] = '\0';
            /* Trim leading spaces from value */
            const char *vstart = eq + 1;
            while (*vstart == ' ') vstart++;
            strncpy(kv.val, vstart, HUB_INI_VAL_LEN - 1);
            kv.val[HUB_INI_VAL_LEN - 1] = '\0';
        } else {
            strncpy(kv.key, line, HUB_INI_KEY_LEN - 1);
            kv.key[HUB_INI_KEY_LEN - 1] = '\0';
            kv.val[0] = '\0';
        }
    }
    fclose(f);
    s_IniDirty = false;
    snprintf(s_IniStatusMsg, sizeof(s_IniStatusMsg), "Loaded: %s", iniName);
    s_IniStatusOk = true;

    int editable = 0;
    int comments = 0;
    int blanks = 0;
    for (int i = 0; i < s_IniNumPairs; i++) {
        if (s_IniPairs[i].is_blank) blanks++;
        else if (s_IniPairs[i].is_comment) comments++;
        else editable++;
    }
    sysLogPrintf(LOG_NOTE,
                 "modhub.ini: loaded id='%s' pairs=%d editable=%d comments=%d blanks=%d",
                 ie.id, s_IniNumPairs, editable, comments, blanks);
}

static bool iniSaveFile(int idx)
{
    if (idx < 0 || idx >= s_IniNumEntries) return false;
    const IniEntry &ie = s_IniEntries[idx];
    const char *iniName = iniNameForType(ie.type);
    if (iniName[0] == '\0') return false;

    char path[FS_MAXPATH + 32];
    snprintf(path, sizeof(path), "%s/%s", ie.dirpath, iniName);

    FILE *f = fopen(path, "w");
    if (!f) {
        snprintf(s_IniStatusMsg, sizeof(s_IniStatusMsg), "Save failed (read-only?)");
        s_IniStatusOk = false;
        sysLogPrintf(LOG_WARNING,
                     "modhub.ini: save failed id='%s' path='%s' (open failed)",
                     ie.id, path);
        return false;
    }

    for (int i = 0; i < s_IniNumPairs; i++) {
        const IniKV &kv = s_IniPairs[i];
        if (kv.is_blank) {
            fprintf(f, "\n");
        } else if (kv.is_comment) {
            fprintf(f, "%s\n", kv.key);
        } else {
            fprintf(f, "%s = %s\n", kv.key, kv.val);
        }
    }
    fclose(f);
    s_IniDirty = false;
    snprintf(s_IniStatusMsg, sizeof(s_IniStatusMsg), "Saved: %s", iniName);
    s_IniStatusOk = true;
    sysLogPrintf(LOG_NOTE,
                 "modhub.ini: saved id='%s' path='%s' pairs=%d",
                 ie.id, path, s_IniNumPairs);
    return true;
}

/* ========================================================================
 * Weapon browser helpers
 * ======================================================================== */

static bool hubPathIsAbsolute(const char *path)
{
    if (!path || !path[0]) return false;
    if ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) {
        return path[1] == ':';
    }
    return path[0] == '/' || path[0] == '\\';
}

static bool hubPathExists(const char *path)
{
    struct stat st;
    return path && path[0] && stat(path, &st) == 0;
}

static void weaponIdToFilename(const char *id, char *out, size_t outSize)
{
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!id) return;
    size_t i = 0;
    for (; i + 1 < outSize && id[i]; i++) {
        out[i] = (id[i] == ':') ? '_' : id[i];
    }
    out[i] = '\0';
}

static void weaponCopyPreview(char *dst, size_t dstSize, const char *src, u32 srcSize)
{
    if (!dst || dstSize == 0) return;
    dst[0] = '\0';
    if (!src || srcSize == 0) return;
    size_t n = srcSize;
    if (n >= dstSize) n = dstSize - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
    if (srcSize >= dstSize && dstSize > 8) {
        strcpy(dst + dstSize - 8, "\n...");
    }
}

static void weaponSetStatus(bool ok, const char *msg)
{
    s_WeaponStatusOk = ok;
    snprintf(s_WeaponStatus, sizeof(s_WeaponStatus), "%s", msg ? msg : "");
}

static void weaponToolSlugify(const char *src, char *dst, size_t dstSize)
{
    if (!dst || dstSize == 0) return;
    dst[0] = '\0';
    if (!src) return;
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < dstSize; i++) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c == ' ' || c == ':' || c == '.' || c == '/') c = '_';
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                c == '_' || c == '-') {
            if ((c == '_' || c == '-') && j == 0) continue;
            dst[j++] = c;
        }
    }
    while (j > 0 && (dst[j - 1] == '_' || dst[j - 1] == '-')) j--;
    dst[j] = '\0';
}

static void weaponJsonEscape(const char *src, char *dst, size_t dstSize)
{
    if (!dst || dstSize == 0) return;
    dst[0] = '\0';
    if (!src) return;
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < dstSize; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c == '"' || c == '\\') && j + 2 < dstSize) {
            dst[j++] = '\\';
            dst[j++] = (char)c;
        } else if (c == '\n' && j + 2 < dstSize) {
            dst[j++] = '\\';
            dst[j++] = 'n';
        } else if (c == '\r') {
            continue;
        } else if (c >= 0x20) {
            dst[j++] = (char)c;
        }
    }
    dst[j] = '\0';
}

static const char *weaponPathLeaf(const char *path)
{
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *leaf = slash > backslash ? slash : backslash;
    return leaf ? leaf + 1 : path;
}

static bool weaponImportEntryName(const char *folder, const char *srcPath,
                                  char *out, size_t outSize)
{
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    const char *leaf = weaponPathLeaf(srcPath);
    if (!folder || !folder[0] || !leaf || !leaf[0]) return false;
    char safe[128];
    size_t j = 0;
    for (size_t i = 0; leaf[i] && j + 1 < sizeof(safe); i++) {
        char c = leaf[i];
        if (c == '\\' || c == '/' || c == ':') c = '_';
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.') {
            safe[j++] = c;
        }
    }
    safe[j] = '\0';
    if (!safe[0]) return false;
    snprintf(out, outSize, "%s/%s", folder, safe);
    return true;
}

static bool weaponReadTextFilePreview(const char *path, char *dst, size_t dstSize)
{
    if (!dst || dstSize == 0) return false;
    dst[0] = '\0';
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(dst, 1, dstSize - 1, f);
    bool ok = !ferror(f);
    dst[n] = '\0';
    fclose(f);
    return ok;
}

static void weaponReplaceJsonStringField(char *json, size_t jsonSize,
                                         const char *field, const char *value)
{
    if (!json || jsonSize == 0 || !field || !value) return;
    char needle[96];
    snprintf(needle, sizeof(needle), "\"%s\"", field);
    char *key = strstr(json, needle);
    if (!key) return;
    char *colon = strchr(key + strlen(needle), ':');
    if (!colon) return;
    char *first = strchr(colon, '"');
    if (!first) return;
    char *last = strchr(first + 1, '"');
    if (!last) return;

    char replacement[CATALOG_ID_LEN + 4];
    snprintf(replacement, sizeof(replacement), "\"%s\"", value);
    size_t prefixLen = (size_t)(first - json);
    size_t oldLen = (size_t)(last - first + 1);
    size_t replLen = strlen(replacement);
    size_t suffixLen = strlen(last + 1);
    if (prefixLen + replLen + suffixLen + 1 >= jsonSize) return;
    memmove(json + prefixLen + replLen, last + 1, suffixLen + 1);
    memcpy(json + prefixLen, replacement, replLen);
}

static void weaponDefaultGraph(const char *catalogId, char *out, size_t outSize)
{
    if (!out || outSize == 0) return;
    snprintf(out, outSize,
        "{\n"
        "  \"schema\": \"pd.weapon_graph.v1\",\n"
        "  \"asset_id\": \"%s\",\n"
        "  \"graph_id\": \"custom_modular\",\n"
        "  \"shared_context\": [],\n"
        "  \"subgraphs\": [],\n"
        "  \"nodes\": [],\n"
        "  \"edges\": [],\n"
        "  \"exports\": []\n"
        "}\n",
        catalogId && catalogId[0] ? catalogId : "user:weapon");
}

static const char *s_WeaponGraphScopes[] = {
    "primary",
    "secondary",
    "shared",
};

static const WeaponGraphContextDef s_WeaponGraphContextDefs[] = {
    {
        "Owner Player", "owner_player", "player", "equipped_player",
        "player_ref", "weapon_instance", true
    },
    {
        "Owner Team", "owner_team", "player", "equipped_player_team",
        "team_ref", "weapon_instance", true
    },
    {
        "Weapon Instance", "weapon_instance", "weapon", "equipped_weapon",
        "weapon_instance_ref", "weapon_instance", true
    },
    {
        "Damage Credit", "damage_credit_player", "projectile", "owner_player",
        "player_ref", "projectile_life", true
    },
    {
        "Projectile Owner", "projectile_owner", "projectile", "owner_player",
        "player_ref", "projectile_life", false
    },
    {
        "Deployed Entity Set", "deployed_entity_set", "entity", "owner_player",
        "entity_set_ref", "player_life", false
    },
    {
        "Detonator Link", "detonator_link_group", "weapon", "weapon_instance",
        "link_group_ref", "player_life", false
    },
    {
        "Target Policy Override", "target_policy_override", "entity", "hacking_tool",
        "target_policy_ref", "entity_life", false
    },
    {
        "Hacked By Player", "hacked_by_player", "player", "hacking_tool_owner",
        "player_ref", "entity_life", false
    },
};

static const WeaponGraphModuleDef s_WeaponGraphModules[] = {
    {
        "Single Shot",
        "fire.hitscan",
        "{ \"mode\": \"primary\", \"function_type\": \"shoot_single\", "
        "\"trigger_policy\": \"press\", \"ammo_slot\": 0, \"damage\": 8.0, "
        "\"spread\": 0.0, \"duration_ticks60\": 6, "
        "\"recoverytime_ticks60\": 12, \"impactforce\": 1.0, "
        "\"penetration\": 0, "
        "\"context_refs\": [\"owner_player\", \"weapon_instance\", "
        "\"damage_credit_player\"] }"
    },
    {
        "Automatic Fire",
        "fire.auto_cadence",
        "{ \"mode\": \"primary\", \"function_type\": \"shoot_automatic\", "
        "\"trigger_policy\": \"hold\", \"ammo_slot\": 0, "
        "\"initial_rpm\": 450.0, \"max_rpm\": 650.0, "
        "\"turret_accel\": 0, \"turret_decel\": 0, "
        "\"recoverytime_ticks60\": 4, "
        "\"context_refs\": [\"owner_player\", \"weapon_instance\", "
        "\"damage_credit_player\"] }"
    },
    {
        "Burst",
        "fire.burst",
        "{ \"mode\": \"primary\", \"function_type\": \"shoot_single\", "
        "\"trigger_policy\": \"press\", \"ammo_slot\": 0, \"burst_count\": 3, "
        "\"damage\": 6.0, \"duration_ticks60\": 5, "
        "\"recoverytime_ticks60\": 18, "
        "\"context_refs\": [\"owner_player\", \"weapon_instance\", "
        "\"damage_credit_player\"] }"
    },
    {
        "Charge Release",
        "fire.charge_release",
        "{ \"mode\": \"secondary\", \"function_type\": \"shoot_single\", "
        "\"trigger_policy\": \"hold_release\", \"ammo_slot\": 0, "
        "\"damage\": 24.0, \"duration_ticks60\": 8, "
        "\"recoverytime_ticks60\": 24, "
        "\"context_refs\": [\"owner_player\", \"weapon_instance\", "
        "\"damage_credit_player\"] }"
    },
    {
        "Beam Tick",
        "fire.beam_tick",
        "{ \"mode\": \"primary\", \"function_type\": \"shoot_automatic\", "
        "\"trigger_policy\": \"hold\", \"ammo_slot\": 0, \"damage\": 1.0, "
        "\"duration_ticks60\": 1, \"recoverytime_ticks60\": 1, "
        "\"context_refs\": [\"owner_player\", \"weapon_instance\", "
        "\"damage_credit_player\"] }"
    },
    {
        "Fired Projectile",
        "spawn.fired_projectile",
        "{ \"mode\": \"primary\", \"function_type\": \"shoot_projectile\", "
        "\"trigger_policy\": \"press\", "
        "\"projectile_ref\": \"user:projectile\", "
        "\"projectile_model_ref\": \"MODEL_rocket\", \"speed\": 120.0, "
        "\"travel_distance\": 3000, \"timer_ticks60\": 180, "
        "\"scale\": 1.0, \"soundnum\": 0, \"recoverytime_ticks60\": 24, "
        "\"context_refs\": [\"owner_player\", \"weapon_instance\", "
        "\"projectile_owner\", \"damage_credit_player\"] }"
    },
    {
        "Thrown Physical",
        "spawn.thrown_physical",
        "{ \"mode\": \"secondary\", \"function_type\": \"throw\", "
        "\"trigger_policy\": \"hold_release\", \"payload_ref\": \"user:payload\", "
        "\"speed\": 80.0, \"activation_time_ticks60\": 24, "
        "\"recovery_time_ticks60\": 30, "
        "\"context_refs\": [\"owner_player\", \"weapon_instance\", "
        "\"deployed_entity_set\", \"detonator_link_group\"] }"
    },
    {
        "Melee Strike",
        "melee.strike",
        "{ \"mode\": \"secondary\", \"function_type\": \"melee\", "
        "\"trigger_policy\": \"press\", \"damage\": 12.0, \"range\": 120.0, "
        "\"recoverytime_ticks60\": 20, "
        "\"context_refs\": [\"owner_player\", \"weapon_instance\", "
        "\"damage_credit_player\"] }"
    },
    {
        "Remote Detonator",
        "special.remote_detonator",
        "{ \"mode\": \"secondary\", \"function_type\": \"special\", "
        "\"trigger_policy\": \"press\", \"detonator_ref\": \"user:remote_mine\", "
        "\"specialfunc\": 0, \"recovery_time_ticks60\": 20, "
        "\"context_refs\": [\"owner_player\", \"deployed_entity_set\", "
        "\"detonator_link_group\"] }"
    },
    {
        "Device Activate",
        "device.activate",
        "{ \"mode\": \"primary\", \"function_type\": \"device\", "
        "\"trigger_policy\": \"press\", \"device\": 0, "
        "\"recovery_time_ticks60\": 12, "
        "\"context_refs\": [\"owner_player\", \"weapon_instance\", "
        "\"hacked_by_player\", \"target_policy_override\"] }"
    },
    {
        "Target Policy Gate",
        "gate.target_lock",
        "{ \"mode\": \"shared\", \"source\": \"target_policy_override\", "
        "\"required\": false, \"target_filter\": \"hostile_or_override\", "
        "\"context_refs\": [\"owner_player\", \"owner_team\", "
        "\"hacked_by_player\", \"target_policy_override\"] }"
    },
    {
        "Weapon State Override",
        "special.weapon_state",
        "{ \"mode\": \"primary\", \"function_type\": \"special\", "
        "\"trigger_policy\": \"press\", \"state_op\": \"set_target_policy\", "
        "\"context_refs\": [\"owner_player\", \"weapon_instance\", "
        "\"hacked_by_player\", \"target_policy_override\"], "
        "\"recovery_time_ticks60\": 18 }"
    },
    {
        "Weapon Visibility",
        "presentation.weapon_visibility",
        "{ \"mode\": \"primary\", \"part_id\": \"ammo_display\", "
        "\"ammo_slot\": 0, \"thresholds\": [0, 1], "
        "\"visibility_state\": \"visible_when_loaded\", "
        "\"context_refs\": [\"weapon_instance\"] }"
    },
    {
        "Reticle / Overlay / Camera",
        "presentation.reticle_overlay_camera",
        "{ \"mode\": \"primary\", \"reticle_ref\": \"default\", "
        "\"overlay_ref\": \"\", \"zoom_fovs\": [60.0], "
        "\"camera_effect\": \"none\", "
        "\"context_refs\": [\"owner_player\", \"weapon_instance\"] }"
    },
};

static bool weaponAppendf(char *dst, size_t dstSize, size_t *pos,
                          const char *fmt, ...)
{
    if (!dst || dstSize == 0 || !pos || *pos >= dstSize) return false;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(dst + *pos, dstSize - *pos, fmt, ap);
    va_end(ap);
    if (n < 0) return false;
    if ((size_t)n >= dstSize - *pos) {
        dst[dstSize - 1] = '\0';
        *pos = dstSize - 1;
        return false;
    }
    *pos += (size_t)n;
    return true;
}

static int weaponGraphScopeCount(void)
{
    return (int)(sizeof(s_WeaponGraphScopes) / sizeof(s_WeaponGraphScopes[0]));
}

static int weaponGraphModuleCount(void)
{
    return (int)(sizeof(s_WeaponGraphModules) / sizeof(s_WeaponGraphModules[0]));
}

static int weaponGraphContextCount(void)
{
    return (int)(sizeof(s_WeaponGraphContextDefs) /
                 sizeof(s_WeaponGraphContextDefs[0]));
}

static void weaponGraphBuilderResetContextDefaults(void)
{
    int count = weaponGraphContextCount();
    for (int i = 0; i < WEAPON_GRAPH_IR_MAX_CONTEXTS; i++) {
        s_WeaponGraphContextEnabled[i] =
            (i < count) ? s_WeaponGraphContextDefs[i].default_enabled : false;
    }
}

static void weaponGraphBuilderEnableContext(const char *name)
{
    if (!name) return;
    for (int i = 0; i < weaponGraphContextCount(); i++) {
        if (strcmp(s_WeaponGraphContextDefs[i].name, name) == 0) {
            s_WeaponGraphContextEnabled[i] = true;
            return;
        }
    }
}

static const char *weaponGraphBuilderScopeForIndex(int index)
{
    if (index < 0 || index >= weaponGraphScopeCount()) return "primary";
    return s_WeaponGraphScopes[index];
}

static int weaponGraphBuilderScopeIndex(const char *scope)
{
    if (!scope) return 0;
    for (int i = 0; i < weaponGraphScopeCount(); i++) {
        if (strcmp(scope, s_WeaponGraphScopes[i]) == 0) return i;
    }
    return 0;
}

static void weaponGraphBuilderApplyNodeMode(WeaponGraphNodeEdit *node)
{
    if (!node || !node->subgraph[0]) return;
    if (strcmp(node->subgraph, "primary") != 0 &&
            strcmp(node->subgraph, "secondary") != 0 &&
            strcmp(node->subgraph, "shared") != 0) {
        return;
    }
    weaponReplaceJsonStringField(node->params, sizeof(node->params),
                                 "mode", node->subgraph);
}

static bool weaponGraphBuilderAppendContextNameArray(char *dst, size_t dstSize,
                                                     size_t *pos)
{
    bool first = true;
    bool ok = weaponAppendf(dst, dstSize, pos, "[");
    for (int i = 0; ok && i < weaponGraphContextCount(); i++) {
        if (!s_WeaponGraphContextEnabled[i]) continue;
        char name[WEAPON_GRAPH_IR_ID_LEN + 16];
        weaponJsonEscape(s_WeaponGraphContextDefs[i].name, name, sizeof(name));
        ok = ok && weaponAppendf(dst, dstSize, pos,
            "%s\"%s\"", first ? "" : ", ", name);
        first = false;
    }
    ok = ok && weaponAppendf(dst, dstSize, pos, "]");
    return ok;
}

static void weaponGraphBuilderClear(void)
{
    pdguiWeaponGraphModelReset(&s_WeaponGraphModel,
                               s_WeaponGraphContextDefs,
                               weaponGraphContextCount());
}

static const char *weaponGraphNodeLabel(int index)
{
    if (index < 0 || index >= s_WeaponGraphNodeCount) return "(none)";
    return s_WeaponGraphNodes[index].id[0]
        ? s_WeaponGraphNodes[index].id
        : s_WeaponGraphNodes[index].kind;
}

static void weaponEditCatalogIdPreview(char *out, size_t outSize)
{
    if (!out || outSize == 0) return;
    char slugRaw[sizeof(s_WeaponEditSlug)];
    char slugPreview[sizeof(s_WeaponEditSlug)];
    snprintf(slugRaw, sizeof(slugRaw), "%s", s_WeaponEditSlug);
    weaponToolSlugify(slugRaw, slugPreview, sizeof(slugPreview));
    snprintf(out, outSize, "user:%s",
             slugPreview[0] ? slugPreview : "weapon");
}

static bool weaponGraphBuilderSyncJson(void)
{
    char assetId[CATALOG_ID_LEN];
    char escapedAsset[CATALOG_ID_LEN + 16];
    size_t pos = 0;
    bool ok = true;

    weaponEditCatalogIdPreview(assetId, sizeof(assetId));
    weaponJsonEscape(assetId, escapedAsset, sizeof(escapedAsset));

    if (s_WeaponGraphNodeCount <= 0) {
        weaponSetStatus(false, "Add at least one graph module before generating");
        return false;
    }

    s_WeaponEditGraph[0] = '\0';
    ok = ok && weaponAppendf(s_WeaponEditGraph, sizeof(s_WeaponEditGraph), &pos,
        "{\n"
        "  \"schema\": \"pd.weapon_graph.v1\",\n"
        "  \"asset_id\": \"%s\",\n"
        "  \"graph_id\": \"custom_modular\",\n",
        escapedAsset);

    ok = ok && weaponAppendf(s_WeaponEditGraph, sizeof(s_WeaponEditGraph), &pos,
        "  \"shared_context\": [\n");
    bool wroteContext = false;
    for (int i = 0; ok && i < weaponGraphContextCount(); i++) {
        if (!s_WeaponGraphContextEnabled[i]) continue;
        const WeaponGraphContextDef &ctx = s_WeaponGraphContextDefs[i];
        char name[WEAPON_GRAPH_IR_ID_LEN + 16];
        char scope[WEAPON_GRAPH_IR_ID_LEN + 16];
        char source[WEAPON_GRAPH_IR_ID_LEN + 16];
        char type[WEAPON_GRAPH_IR_ID_LEN + 16];
        char lifetime[WEAPON_GRAPH_IR_ID_LEN + 16];
        weaponJsonEscape(ctx.name, name, sizeof(name));
        weaponJsonEscape(ctx.scope, scope, sizeof(scope));
        weaponJsonEscape(ctx.source, source, sizeof(source));
        weaponJsonEscape(ctx.type, type, sizeof(type));
        weaponJsonEscape(ctx.lifetime, lifetime, sizeof(lifetime));
        ok = ok && weaponAppendf(s_WeaponEditGraph, sizeof(s_WeaponEditGraph), &pos,
            "%s    { \"name\": \"%s\", \"scope\": \"%s\", "
            "\"source\": \"%s\", \"type\": \"%s\", \"lifetime\": \"%s\" }",
            wroteContext ? ",\n" : "", name, scope, source, type, lifetime);
        wroteContext = true;
    }
    if (wroteContext) {
        ok = ok && weaponAppendf(s_WeaponEditGraph, sizeof(s_WeaponEditGraph), &pos, "\n");
    }

    char primaryEntry[WEAPON_GRAPH_IR_ID_LEN + 16] = "";
    char secondaryEntry[WEAPON_GRAPH_IR_ID_LEN + 16] = "";
    if (s_WeaponGraphPrimaryExport >= 0 &&
            s_WeaponGraphPrimaryExport < s_WeaponGraphNodeCount) {
        weaponJsonEscape(s_WeaponGraphNodes[s_WeaponGraphPrimaryExport].id,
                         primaryEntry, sizeof(primaryEntry));
    }
    if (s_WeaponGraphSecondaryExport >= 0 &&
            s_WeaponGraphSecondaryExport < s_WeaponGraphNodeCount) {
        weaponJsonEscape(s_WeaponGraphNodes[s_WeaponGraphSecondaryExport].id,
                         secondaryEntry, sizeof(secondaryEntry));
    }
    ok = ok && weaponAppendf(s_WeaponEditGraph, sizeof(s_WeaponEditGraph), &pos,
        "  ],\n"
        "  \"subgraphs\": [\n"
        "    { \"id\": \"primary\", \"entry\": \"%s\", \"shared_context\": ",
        primaryEntry);
    ok = ok && weaponGraphBuilderAppendContextNameArray(s_WeaponEditGraph,
        sizeof(s_WeaponEditGraph), &pos);
    ok = ok && weaponAppendf(s_WeaponEditGraph, sizeof(s_WeaponEditGraph), &pos,
        " },\n"
        "    { \"id\": \"secondary\", \"entry\": \"%s\", \"shared_context\": ",
        secondaryEntry);
    ok = ok && weaponGraphBuilderAppendContextNameArray(s_WeaponEditGraph,
        sizeof(s_WeaponEditGraph), &pos);
    ok = ok && weaponAppendf(s_WeaponEditGraph, sizeof(s_WeaponEditGraph), &pos,
        " }\n"
        "  ],\n"
        "  \"nodes\": [\n");

    for (int i = 0; ok && i < s_WeaponGraphNodeCount; i++) {
        char escapedId[WEAPON_GRAPH_IR_ID_LEN + 16];
        char escapedKind[WEAPON_GRAPH_IR_ID_LEN + 16];
        char escapedSubgraph[HUB_WEAPON_GRAPH_SCOPE_LEN + 16];
        weaponJsonEscape(s_WeaponGraphNodes[i].id, escapedId, sizeof(escapedId));
        weaponJsonEscape(s_WeaponGraphNodes[i].kind, escapedKind, sizeof(escapedKind));
        weaponJsonEscape(s_WeaponGraphNodes[i].subgraph[0]
                         ? s_WeaponGraphNodes[i].subgraph : "primary",
                         escapedSubgraph, sizeof(escapedSubgraph));
        ok = ok && weaponAppendf(s_WeaponEditGraph, sizeof(s_WeaponEditGraph), &pos,
            "    { \"id\": \"%s\", \"kind\": \"%s\", \"subgraph\": \"%s\", "
            "\"params\": %s }%s\n",
            escapedId,
            escapedKind,
            escapedSubgraph,
            s_WeaponGraphNodes[i].params[0] ? s_WeaponGraphNodes[i].params : "{}",
            (i + 1 < s_WeaponGraphNodeCount) ? "," : "");
    }

    ok = ok && weaponAppendf(s_WeaponEditGraph, sizeof(s_WeaponEditGraph), &pos,
        "  ],\n"
        "  \"edges\": [\n");

    for (int i = 0; ok && i < s_WeaponGraphEdgeCount; i++) {
        if (s_WeaponGraphEdges[i].from < 0 ||
                s_WeaponGraphEdges[i].from >= s_WeaponGraphNodeCount ||
                s_WeaponGraphEdges[i].to < 0 ||
                s_WeaponGraphEdges[i].to >= s_WeaponGraphNodeCount) {
            continue;
        }
        char from[WEAPON_GRAPH_IR_ID_LEN + 16];
        char to[WEAPON_GRAPH_IR_ID_LEN + 16];
        weaponJsonEscape(s_WeaponGraphNodes[s_WeaponGraphEdges[i].from].id,
                         from, sizeof(from));
        weaponJsonEscape(s_WeaponGraphNodes[s_WeaponGraphEdges[i].to].id,
                         to, sizeof(to));
        ok = ok && weaponAppendf(s_WeaponEditGraph, sizeof(s_WeaponEditGraph), &pos,
            "    { \"from\": \"%s\", \"to\": \"%s\" }%s\n",
            from, to, (i + 1 < s_WeaponGraphEdgeCount) ? "," : "");
    }

    ok = ok && weaponAppendf(s_WeaponEditGraph, sizeof(s_WeaponEditGraph), &pos,
        "  ],\n"
        "  \"exports\": [\n");

    bool wroteExport = false;
    if (s_WeaponGraphPrimaryExport >= 0 &&
            s_WeaponGraphPrimaryExport < s_WeaponGraphNodeCount) {
        char node[WEAPON_GRAPH_IR_ID_LEN + 16];
        weaponJsonEscape(s_WeaponGraphNodes[s_WeaponGraphPrimaryExport].id,
                         node, sizeof(node));
        ok = ok && weaponAppendf(s_WeaponEditGraph, sizeof(s_WeaponEditGraph), &pos,
            "    { \"name\": \"primary\", \"node\": \"%s\" }",
            node);
        wroteExport = true;
    }
    if (s_WeaponGraphSecondaryExport >= 0 &&
            s_WeaponGraphSecondaryExport < s_WeaponGraphNodeCount) {
        char node[WEAPON_GRAPH_IR_ID_LEN + 16];
        weaponJsonEscape(s_WeaponGraphNodes[s_WeaponGraphSecondaryExport].id,
                         node, sizeof(node));
        ok = ok && weaponAppendf(s_WeaponEditGraph, sizeof(s_WeaponEditGraph), &pos,
            "%s    { \"name\": \"secondary\", \"node\": \"%s\" }",
            wroteExport ? ",\n" : "", node);
        wroteExport = true;
    }
    if (wroteExport) {
        ok = ok && weaponAppendf(s_WeaponEditGraph, sizeof(s_WeaponEditGraph), &pos, "\n");
    }
    ok = ok && weaponAppendf(s_WeaponEditGraph, sizeof(s_WeaponEditGraph), &pos,
        "  ],\n"
        "  \"editor\": {\n"
        "    \"layout\": {\n"
        "      \"nodes\": [\n");

    for (int i = 0; ok && i < s_WeaponGraphNodeCount; i++) {
        char escapedId[WEAPON_GRAPH_IR_ID_LEN + 16];
        weaponJsonEscape(s_WeaponGraphNodes[i].id, escapedId, sizeof(escapedId));
        float x = s_WeaponGraphNodes[i].pos_valid
            ? s_WeaponGraphNodes[i].pos_x
            : 48.0f + (float)((i % 3) * 230);
        float y = s_WeaponGraphNodes[i].pos_valid
            ? s_WeaponGraphNodes[i].pos_y
            : 48.0f + (float)((i / 3) * 120);
        ok = ok && weaponAppendf(s_WeaponEditGraph, sizeof(s_WeaponEditGraph), &pos,
            "        { \"id\": \"%s\", \"x\": %.1f, \"y\": %.1f }%s\n",
            escapedId, x, y, (i + 1 < s_WeaponGraphNodeCount) ? "," : "");
    }

    ok = ok && weaponAppendf(s_WeaponEditGraph, sizeof(s_WeaponEditGraph), &pos,
        "      ]\n"
        "    }\n"
        "  }\n"
        "}\n");

    if (!ok) {
        weaponSetStatus(false, "Graph builder output is too large");
        return false;
    }

    char graphErr[192];
    if (weaponGraphValidateJson(ASSET_WEAPON, s_WeaponEditGraph,
            (u32)strlen(s_WeaponEditGraph), graphErr, sizeof(graphErr)) != 0) {
        char msg[240];
        snprintf(msg, sizeof(msg), "Graph builder validation failed: %s", graphErr);
        weaponSetStatus(false, msg);
        return false;
    }

    weaponSetStatus(true, "Graph builder updated behavior.graph.json");
    return true;
}

static bool weaponGraphBuilderNodeIdExists(const char *id, int skipIndex)
{
    if (!id || !id[0]) return false;
    for (int i = 0; i < s_WeaponGraphNodeCount; i++) {
        if (i == skipIndex) continue;
        if (strcmp(s_WeaponGraphNodes[i].id, id) == 0) return true;
    }
    return false;
}

static void weaponGraphBuilderMakeNodeId(const char *kind, char *out, size_t outSize)
{
    if (!out || outSize == 0) return;
    char base[WEAPON_GRAPH_IR_ID_LEN];
    size_t j = 0;
    const char *src = kind && kind[0] ? kind : "node";
    for (size_t i = 0; src[i] && j + 1 < sizeof(base); i++) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            base[j++] = c;
        } else if (j > 0 && base[j - 1] != '_') {
            base[j++] = '_';
        }
    }
    while (j > 0 && base[j - 1] == '_') j--;
    base[j] = '\0';
    if (!base[0]) snprintf(base, sizeof(base), "node");

    for (int i = s_WeaponGraphNodeCount + 1; i < 1000; i++) {
        snprintf(out, outSize, "%s_%02d", base, i);
        if (!weaponGraphBuilderNodeIdExists(out, -1)) return;
    }
    snprintf(out, outSize, "%s", base);
}

static bool weaponGraphBuilderAddModule(int moduleIndex, bool syncNow)
{
    if (moduleIndex < 0 || moduleIndex >= weaponGraphModuleCount()) {
        return false;
    }
    if (s_WeaponGraphNodeCount >= HUB_WEAPON_GRAPH_MAX_NODES) {
        weaponSetStatus(false, "Graph builder node limit reached");
        return false;
    }
    WeaponGraphNodeEdit &node = s_WeaponGraphNodes[s_WeaponGraphNodeCount];
    memset(&node, 0, sizeof(node));
    if (s_WeaponGraphModel.next_editor_id <= 0) s_WeaponGraphModel.next_editor_id = 1;
    node.editor_id = s_WeaponGraphModel.next_editor_id++;
    node.pos_x = 48.0f + (float)((s_WeaponGraphNodeCount % 3) * 230);
    node.pos_y = 48.0f + (float)((s_WeaponGraphNodeCount / 3) * 120);
    node.pos_valid = true;
    s_WeaponGraphModel.canvas_layout_seeded = false;
    weaponGraphBuilderMakeNodeId(s_WeaponGraphModules[moduleIndex].kind,
                                 node.id, sizeof(node.id));
    strncpy(node.kind, s_WeaponGraphModules[moduleIndex].kind,
            sizeof(node.kind) - 1);
    strncpy(node.subgraph, weaponGraphBuilderScopeForIndex(s_WeaponGraphScopePick),
            sizeof(node.subgraph) - 1);
    strncpy(node.params, s_WeaponGraphModules[moduleIndex].default_params,
            sizeof(node.params) - 1);
    weaponGraphBuilderApplyNodeMode(&node);
    if (s_WeaponGraphPrimaryExport < 0) {
        s_WeaponGraphPrimaryExport = s_WeaponGraphNodeCount;
    }
    s_WeaponGraphNodeCount++;
    if (syncNow) {
        return weaponGraphBuilderSyncJson();
    }
    return true;
}

static int weaponGraphModuleIndexByKind(const char *kind)
{
    if (!kind) return 0;
    for (int i = 0; i < weaponGraphModuleCount(); i++) {
        if (strcmp(s_WeaponGraphModules[i].kind, kind) == 0) return i;
    }
    return 0;
}

static int weaponGraphBuilderAddModuleInScope(const char *kind,
                                              const char *scope)
{
    int previousScope = s_WeaponGraphScopePick;
    int addedIndex = s_WeaponGraphNodeCount;
    s_WeaponGraphScopePick = weaponGraphBuilderScopeIndex(scope);
    if (!weaponGraphBuilderAddModule(weaponGraphModuleIndexByKind(kind), false)) {
        s_WeaponGraphScopePick = previousScope;
        return -1;
    }
    s_WeaponGraphScopePick = previousScope;
    return addedIndex;
}

static void weaponGraphBuilderAddEdge(int from, int to, bool syncNow)
{
    if (from < 0 || from >= s_WeaponGraphNodeCount ||
            to < 0 || to >= s_WeaponGraphNodeCount) {
        weaponSetStatus(false, "Choose valid graph nodes before adding an edge");
        return;
    }
    if (from == to) {
        weaponSetStatus(false, "Graph edges cannot point to the same node");
        return;
    }
    for (int i = 0; i < s_WeaponGraphEdgeCount; i++) {
        if (s_WeaponGraphEdges[i].from == from &&
                s_WeaponGraphEdges[i].to == to) {
            weaponSetStatus(false, "That graph edge already exists");
            return;
        }
    }
    if (s_WeaponGraphEdgeCount >= HUB_WEAPON_GRAPH_MAX_EDGES) {
        weaponSetStatus(false, "Graph builder edge limit reached");
        return;
    }
    if (s_WeaponGraphModel.next_editor_id <= 0) s_WeaponGraphModel.next_editor_id = 1;
    s_WeaponGraphEdges[s_WeaponGraphEdgeCount].editor_id =
        s_WeaponGraphModel.next_editor_id++;
    s_WeaponGraphEdges[s_WeaponGraphEdgeCount].from = from;
    s_WeaponGraphEdges[s_WeaponGraphEdgeCount].to = to;
    s_WeaponGraphEdgeCount++;
    if (syncNow) {
        weaponGraphBuilderSyncJson();
    }
}

static void weaponGraphBuilderRemoveNode(int index)
{
    if (index < 0 || index >= s_WeaponGraphNodeCount) return;
    for (int i = index; i + 1 < s_WeaponGraphNodeCount; i++) {
        s_WeaponGraphNodes[i] = s_WeaponGraphNodes[i + 1];
    }
    s_WeaponGraphNodeCount--;

    int out = 0;
    for (int i = 0; i < s_WeaponGraphEdgeCount; i++) {
        WeaponGraphEdgeEdit edge = s_WeaponGraphEdges[i];
        int from = edge.from;
        int to = edge.to;
        if (from == index || to == index) continue;
        if (from > index) from--;
        if (to > index) to--;
        edge.from = from;
        edge.to = to;
        s_WeaponGraphEdges[out] = edge;
        out++;
    }
    s_WeaponGraphEdgeCount = out;

    if (s_WeaponGraphPrimaryExport == index) s_WeaponGraphPrimaryExport = -1;
    if (s_WeaponGraphSecondaryExport == index) s_WeaponGraphSecondaryExport = -1;
    if (s_WeaponGraphPrimaryExport > index) s_WeaponGraphPrimaryExport--;
    if (s_WeaponGraphSecondaryExport > index) s_WeaponGraphSecondaryExport--;
    if (s_WeaponGraphEdgeFrom >= s_WeaponGraphNodeCount) s_WeaponGraphEdgeFrom = 0;
    if (s_WeaponGraphEdgeTo >= s_WeaponGraphNodeCount) s_WeaponGraphEdgeTo = 0;
    s_WeaponGraphModel.selected_node = -1;
    s_WeaponGraphModel.selected_edge = -1;
    s_WeaponGraphModel.canvas_layout_seeded = false;
    weaponGraphBuilderSyncJson();
}

static void weaponGraphBuilderRemoveEdge(int index)
{
    if (index < 0 || index >= s_WeaponGraphEdgeCount) return;
    for (int i = index; i + 1 < s_WeaponGraphEdgeCount; i++) {
        s_WeaponGraphEdges[i] = s_WeaponGraphEdges[i + 1];
    }
    s_WeaponGraphEdgeCount--;
    s_WeaponGraphModel.selected_edge = -1;
    weaponGraphBuilderSyncJson();
}

static void weaponGraphBuilderDuplicateNode(int index)
{
    if (index < 0 || index >= s_WeaponGraphNodeCount) return;
    if (s_WeaponGraphNodeCount >= HUB_WEAPON_GRAPH_MAX_NODES) {
        weaponSetStatus(false, "Graph builder node limit reached");
        return;
    }
    WeaponGraphNodeEdit src = s_WeaponGraphNodes[index];
    WeaponGraphNodeEdit &node = s_WeaponGraphNodes[s_WeaponGraphNodeCount];
    memset(&node, 0, sizeof(node));
    if (s_WeaponGraphModel.next_editor_id <= 0) s_WeaponGraphModel.next_editor_id = 1;
    node.editor_id = s_WeaponGraphModel.next_editor_id++;
    weaponGraphBuilderMakeNodeId(src.id[0] ? src.id : src.kind,
                                 node.id, sizeof(node.id));
    strncpy(node.kind, src.kind, sizeof(node.kind) - 1);
    strncpy(node.subgraph, src.subgraph, sizeof(node.subgraph) - 1);
    strncpy(node.params, src.params, sizeof(node.params) - 1);
    node.pos_x = src.pos_valid ? src.pos_x + 36.0f : 80.0f;
    node.pos_y = src.pos_valid ? src.pos_y + 36.0f : 80.0f;
    node.pos_valid = true;
    s_WeaponGraphModel.selected_node = s_WeaponGraphNodeCount;
    s_WeaponGraphModel.canvas_layout_seeded = false;
    s_WeaponGraphNodeCount++;
    weaponGraphBuilderSyncJson();
}

static void weaponGraphBuilderBreakPin(int nodeIndex, int pinKind)
{
    if (nodeIndex < 0 || nodeIndex >= s_WeaponGraphNodeCount) return;
    int out = 0;
    for (int i = 0; i < s_WeaponGraphEdgeCount; i++) {
        bool remove = false;
        if (pinKind == 1) {
            remove = s_WeaponGraphEdges[i].to == nodeIndex;
        } else if (pinKind == 2) {
            remove = s_WeaponGraphEdges[i].from == nodeIndex;
        } else {
            remove = s_WeaponGraphEdges[i].from == nodeIndex ||
                s_WeaponGraphEdges[i].to == nodeIndex;
        }
        if (!remove) {
            s_WeaponGraphEdges[out++] = s_WeaponGraphEdges[i];
        }
    }
    if (out == s_WeaponGraphEdgeCount) {
        weaponSetStatus(false, "No graph links were attached to that pin");
        return;
    }
    s_WeaponGraphEdgeCount = out;
    s_WeaponGraphModel.selected_edge = -1;
    weaponGraphBuilderSyncJson();
}

static void weaponGraphBuilderSeedSingleShot(void)
{
    weaponGraphBuilderClear();
    int primary = weaponGraphBuilderAddModuleInScope("fire.hitscan", "primary");
    s_WeaponGraphPrimaryExport = primary;
    weaponGraphBuilderSyncJson();
}

static void weaponGraphBuilderSeedAutomatic(void)
{
    weaponGraphBuilderClear();
    int primary = weaponGraphBuilderAddModuleInScope("fire.auto_cadence", "primary");
    s_WeaponGraphPrimaryExport = primary;
    weaponGraphBuilderSyncJson();
}

static void weaponGraphBuilderSeedDualFireModes(void)
{
    weaponGraphBuilderClear();
    int primary = weaponGraphBuilderAddModuleInScope("fire.hitscan", "primary");
    int secondary = weaponGraphBuilderAddModuleInScope("fire.burst", "secondary");
    s_WeaponGraphPrimaryExport = primary;
    s_WeaponGraphSecondaryExport = secondary;
    weaponGraphBuilderSyncJson();
}

static void weaponGraphBuilderSeedProjectile(void)
{
    weaponGraphBuilderClear();
    weaponGraphBuilderEnableContext("projectile_owner");
    int primary = weaponGraphBuilderAddModuleInScope("spawn.fired_projectile", "primary");
    s_WeaponGraphPrimaryExport = primary;
    weaponGraphBuilderSyncJson();
}

static void weaponGraphBuilderSeedThrown(void)
{
    weaponGraphBuilderClear();
    weaponGraphBuilderEnableContext("deployed_entity_set");
    int secondary = weaponGraphBuilderAddModuleInScope("spawn.thrown_physical", "secondary");
    s_WeaponGraphSecondaryExport = secondary;
    weaponGraphBuilderSyncJson();
}

static void weaponGraphBuilderSeedMineLink(void)
{
    weaponGraphBuilderClear();
    weaponGraphBuilderEnableContext("deployed_entity_set");
    weaponGraphBuilderEnableContext("detonator_link_group");
    int primary = weaponGraphBuilderAddModuleInScope("spawn.thrown_physical", "primary");
    int secondary = weaponGraphBuilderAddModuleInScope("special.remote_detonator", "secondary");
    s_WeaponGraphPrimaryExport = primary;
    s_WeaponGraphSecondaryExport = secondary;
    weaponGraphBuilderSyncJson();
}

static void weaponGraphBuilderSeedLaptopControl(void)
{
    weaponGraphBuilderClear();
    weaponGraphBuilderEnableContext("deployed_entity_set");
    weaponGraphBuilderEnableContext("target_policy_override");
    weaponGraphBuilderEnableContext("hacked_by_player");
    int primary = weaponGraphBuilderAddModuleInScope("fire.auto_cadence", "primary");
    int secondary = weaponGraphBuilderAddModuleInScope("spawn.thrown_physical", "secondary");
    int policy = weaponGraphBuilderAddModuleInScope("gate.target_lock", "shared");
    if (policy >= 0 && primary >= 0) {
        weaponGraphBuilderAddEdge(policy, primary, false);
    }
    s_WeaponGraphPrimaryExport = primary;
    s_WeaponGraphSecondaryExport = secondary;
    weaponGraphBuilderSyncJson();
}

static bool weaponGraphBuilderLoadEditModelFromJson(bool syncNow)
{
    char err[192];
    if (!pdguiWeaponGraphModelLoadJson(&s_WeaponGraphModel,
            s_WeaponEditGraph,
            s_WeaponGraphModules,
            weaponGraphModuleCount(),
            s_WeaponGraphContextDefs,
            weaponGraphContextCount(),
            err,
            sizeof(err))) {
        weaponGraphBuilderClear();
        if (err[0]) {
            char msg[240];
            snprintf(msg, sizeof(msg), "Graph editor could not load JSON: %s", err);
            weaponSetStatus(false, msg);
        }
        return false;
    }
    s_WeaponGraphModel.canvas_layout_seeded = false;
    if (syncNow) {
        return weaponGraphBuilderSyncJson();
    }
    weaponSetStatus(true, "Loaded behavior graph into node editor");
    return true;
}

static void weaponGraphBuilderApplyEditorResult(
        const PdWeaponGraphEditorResult &result)
{
    bool needsSync = result.model_changed;
    switch (result.action) {
        case PD_WEAPON_GRAPH_EDITOR_ACTION_ADD_MODULE: {
            int previousScope = s_WeaponGraphScopePick;
            if (result.scope[0]) {
                s_WeaponGraphScopePick = weaponGraphBuilderScopeIndex(result.scope);
            } else if (result.b >= 0 && result.b < weaponGraphScopeCount()) {
                s_WeaponGraphScopePick = result.b;
            }
            needsSync = weaponGraphBuilderAddModule(result.module_index, false) || needsSync;
            s_WeaponGraphScopePick = previousScope;
            break;
        }
        case PD_WEAPON_GRAPH_EDITOR_ACTION_ADD_EDGE:
            weaponGraphBuilderAddEdge(result.a, result.b, false);
            needsSync = true;
            break;
        case PD_WEAPON_GRAPH_EDITOR_ACTION_REMOVE_NODE:
            weaponGraphBuilderRemoveNode(result.a);
            needsSync = false;
            break;
        case PD_WEAPON_GRAPH_EDITOR_ACTION_REMOVE_EDGE:
            weaponGraphBuilderRemoveEdge(result.a);
            needsSync = false;
            break;
        case PD_WEAPON_GRAPH_EDITOR_ACTION_DUPLICATE_NODE:
            weaponGraphBuilderDuplicateNode(result.a);
            needsSync = false;
            break;
        case PD_WEAPON_GRAPH_EDITOR_ACTION_SET_PRIMARY:
            if (result.a >= 0 && result.a < s_WeaponGraphNodeCount) {
                s_WeaponGraphPrimaryExport = result.a;
                needsSync = true;
            }
            break;
        case PD_WEAPON_GRAPH_EDITOR_ACTION_SET_SECONDARY:
            if (result.a >= 0 && result.a < s_WeaponGraphNodeCount) {
                s_WeaponGraphSecondaryExport = result.a;
                needsSync = true;
            }
            break;
        case PD_WEAPON_GRAPH_EDITOR_ACTION_BREAK_PIN:
            weaponGraphBuilderBreakPin(result.a, result.b);
            needsSync = false;
            break;
        case PD_WEAPON_GRAPH_EDITOR_ACTION_NONE:
        default:
            break;
    }

    bool syncOk = true;
    if (needsSync) {
        syncOk = weaponGraphBuilderSyncJson();
    }
    if (result.status[0] && syncOk) {
        weaponSetStatus(result.status_ok, result.status);
    }
}

static char *weaponImportPathMutableForTarget(WeaponImportTarget target)
{
    switch (target) {
        case WEAPON_IMPORT_MODEL:     return s_WeaponImportModelPath;
        case WEAPON_IMPORT_TEXTURE:   return s_WeaponImportTexturePath;
        case WEAPON_IMPORT_ANIMATION: return s_WeaponImportAnimationPath;
        case WEAPON_IMPORT_AUDIO:     return s_WeaponImportAudioPath;
        case WEAPON_IMPORT_PROJECTILE:return s_WeaponImportProjectilePath;
        case WEAPON_IMPORT_ENTITY:    return s_WeaponImportEntityPath;
        case WEAPON_IMPORT_GRAPH:     return s_WeaponImportGraphPath;
        default:                      return NULL;
    }
}

static void weaponArchivePathForEntry(const asset_entry_t *e,
                                      char *out, size_t outSize)
{
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!e) return;

    if (e->bundled || strncmp(e->id, "base:", 5) == 0) {
        char slug[128];
        char relKind[FS_MAXPATH];
        char relData[FS_MAXPATH];
        weaponIdToFilename(e->id, slug, sizeof(slug));
        snprintf(relKind, sizeof(relKind), "weapons/%s.pdweapon", slug);
        if (fsDataPathFor(relKind, relData, sizeof(relData))) {
            char full[FS_MAXPATH + 1];
            const char *resolved = fsFullPath(relData, full, sizeof(full));
            if (resolved && resolved[0]) {
                strncpy(out, resolved, outSize - 1);
                out[outSize - 1] = '\0';
            }
        }
        return;
    }

    if (strstr(e->dirpath, ".pdweapon") != NULL) {
        if (hubPathIsAbsolute(e->dirpath)) {
            strncpy(out, e->dirpath, outSize - 1);
            out[outSize - 1] = '\0';
        } else {
            char full[FS_MAXPATH + 1];
            const char *resolved = fsFullPath(e->dirpath, full, sizeof(full));
            if (resolved && resolved[0]) {
                strncpy(out, resolved, outSize - 1);
                out[outSize - 1] = '\0';
            }
        }
    }
}

static const asset_entry_t *weaponSelectedEntry(void)
{
    if (!s_WeaponSelectedId[0]) return NULL;
    const asset_entry_t *e = assetCatalogResolve(s_WeaponSelectedId);
    if (!e || e->type != ASSET_WEAPON) return NULL;
    return e;
}

static void weaponToolRefresh(void)
{
    s_WeaponLoadedId[0] = '\0';
    s_WeaponStatus[0] = '\0';
    s_WeaponStatusOk = true;
}

static void weaponToolLoadSelected(void)
{
    const asset_entry_t *e = weaponSelectedEntry();
    s_WeaponArchivePath[0] = '\0';
    s_WeaponIniPreview[0] = '\0';
    s_WeaponGraphPreview[0] = '\0';
    s_WeaponNestedPreview[0] = '\0';
    s_WeaponStatus[0] = '\0';
    s_WeaponStatusOk = true;

    if (!e) {
        snprintf(s_WeaponStatus, sizeof(s_WeaponStatus), "No weapon selected");
        s_WeaponStatusOk = false;
        return;
    }

    strncpy(s_WeaponLoadedId, e->id, sizeof(s_WeaponLoadedId) - 1);
    s_WeaponLoadedId[sizeof(s_WeaponLoadedId) - 1] = '\0';
    weaponArchivePathForEntry(e, s_WeaponArchivePath, sizeof(s_WeaponArchivePath));

    if (!s_WeaponArchivePath[0] || !hubPathExists(s_WeaponArchivePath)) {
        snprintf(s_WeaponStatus, sizeof(s_WeaponStatus),
                 e->dirpath[0] ? "Archive preview unavailable for this catalog source"
                               : "Archive preview unavailable until base assets regenerate");
        s_WeaponStatusOk = false;
        return;
    }

    char *text = NULL;
    u32 size = 0;
    if (weaponGraphArchiveReadTextFile(s_WeaponArchivePath, "weapon.ini", &text, &size) == 0) {
        weaponCopyPreview(s_WeaponIniPreview, sizeof(s_WeaponIniPreview), text, size);
        free(text);
        text = NULL;
    }
    if (weaponGraphArchiveReadTextFile(s_WeaponArchivePath, "behavior.graph.json", &text, &size) == 0) {
        weaponCopyPreview(s_WeaponGraphPreview, sizeof(s_WeaponGraphPreview), text, size);
        free(text);
        text = NULL;
    }
    if (weaponGraphArchiveReadTextFile(s_WeaponArchivePath, "nested_payloads.json", &text, &size) == 0) {
        weaponCopyPreview(s_WeaponNestedPreview, sizeof(s_WeaponNestedPreview), text, size);
        free(text);
        text = NULL;
    }

    snprintf(s_WeaponStatus, sizeof(s_WeaponStatus), "Loaded weapon archive preview");
    s_WeaponStatusOk = true;
}

static bool weaponCharIsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void weaponCopyTrimmedRange(char *out, size_t outSize,
                                   const char *start, const char *end)
{
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!start || !end || end < start) return;
    while (start < end && weaponCharIsSpace(*start)) start++;
    while (end > start && weaponCharIsSpace(*(end - 1))) end--;
    if (end > start + 1 && *start == '"' && *(end - 1) == '"') {
        start++;
        end--;
    }
    size_t len = (size_t)(end - start);
    if (len >= outSize) len = outSize - 1;
    memcpy(out, start, len);
    out[len] = '\0';
}

static bool weaponIniGetValue(const char *ini, const char *key,
                              char *out, size_t outSize)
{
    if (out && outSize) out[0] = '\0';
    if (!ini || !key || !key[0] || !out || outSize == 0) return false;
    const size_t keyLen = strlen(key);
    const char *line = ini;
    while (*line) {
        const char *lineEnd = line;
        while (*lineEnd && *lineEnd != '\n') lineEnd++;
        const char *trim = line;
        while (trim < lineEnd && weaponCharIsSpace(*trim)) trim++;
        if (trim < lineEnd && *trim != '#' && *trim != ';' && *trim != '[') {
            const char *eq = trim;
            while (eq < lineEnd && *eq != '=') eq++;
            if (eq < lineEnd) {
                const char *keyEnd = eq;
                while (keyEnd > trim && weaponCharIsSpace(*(keyEnd - 1))) keyEnd--;
                if ((size_t)(keyEnd - trim) == keyLen &&
                        strncmp(trim, key, keyLen) == 0) {
                    weaponCopyTrimmedRange(out, outSize, eq + 1, lineEnd);
                    return out[0] != '\0';
                }
            }
        }
        line = *lineEnd ? lineEnd + 1 : lineEnd;
    }
    return false;
}

static bool weaponCopyCatalogRef(char *dst, size_t dstSize, const char *src)
{
    if (!dst || dstSize == 0 || !src || !src[0]) return false;
    if (!strchr(src, ':')) return false;
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
    return true;
}

static bool weaponJsonFindFirstStringField(const char *json, const char *key,
                                           char *out, size_t outSize)
{
    if (out && outSize) out[0] = '\0';
    if (!json || !key || !key[0] || !out || outSize == 0) return false;
    char needle[96];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const size_t needleLen = strlen(needle);
    for (const char *p = strstr(json, needle); p; p = strstr(p + 1, needle)) {
        const char *q = p + needleLen;
        while (*q && weaponCharIsSpace(*q)) q++;
        if (*q != ':') continue;
        q++;
        while (*q && weaponCharIsSpace(*q)) q++;
        if (*q != '"') continue;
        q++;
        char *w = out;
        size_t left = outSize - 1;
        bool escaped = false;
        while (*q && left > 0) {
            if (!escaped && *q == '"') break;
            if (!escaped && *q == '\\') {
                escaped = true;
                q++;
                continue;
            }
            *w++ = *q++;
            left--;
            escaped = false;
        }
        *w = '\0';
        if (out[0]) return true;
    }
    return false;
}

static bool weaponArchiveReadEntryText(const char *archivePath,
                                       const char *entry,
                                       char *out,
                                       size_t outSize)
{
    if (out && outSize) out[0] = '\0';
    if (!archivePath || !archivePath[0] || !entry || !entry[0] ||
            !out || outSize == 0) {
        return false;
    }
    char *text = NULL;
    u32 size = 0;
    if (weaponGraphArchiveReadTextFile(archivePath, entry, &text, &size) != 0 ||
            !text) {
        return false;
    }
    size_t copy = size;
    if (copy >= outSize) copy = outSize - 1;
    memcpy(out, text, copy);
    out[copy] = '\0';
    free(text);
    return true;
}

static bool weaponTsvFirstField(const char *tsv, const char *field,
                                char *out, size_t outSize)
{
    if (out && outSize) out[0] = '\0';
    if (!tsv || !field || !field[0] || !out || outSize == 0) return false;
    const char *headerEnd = tsv;
    while (*headerEnd && *headerEnd != '\n') headerEnd++;
    int targetCol = -1;
    int col = 0;
    const char *cell = tsv;
    while (cell <= headerEnd) {
        const char *cellEnd = cell;
        while (cellEnd < headerEnd && *cellEnd != '\t') cellEnd++;
        char name[64];
        weaponCopyTrimmedRange(name, sizeof(name), cell, cellEnd);
        if (strcmp(name, field) == 0) {
            targetCol = col;
            break;
        }
        if (cellEnd >= headerEnd) break;
        cell = cellEnd + 1;
        col++;
    }
    if (targetCol < 0) return false;

    const char *line = *headerEnd ? headerEnd + 1 : headerEnd;
    while (*line) {
        const char *lineEnd = line;
        while (*lineEnd && *lineEnd != '\n') lineEnd++;
        const char *trim = line;
        while (trim < lineEnd && weaponCharIsSpace(*trim)) trim++;
        if (trim < lineEnd && *trim != '#') {
            col = 0;
            cell = line;
            while (cell <= lineEnd) {
                const char *cellEnd = cell;
                while (cellEnd < lineEnd && *cellEnd != '\t') cellEnd++;
                if (col == targetCol) {
                    weaponCopyTrimmedRange(out, outSize, cell, cellEnd);
                    return out[0] != '\0';
                }
                if (cellEnd >= lineEnd) break;
                cell = cellEnd + 1;
                col++;
            }
        }
        line = *lineEnd ? lineEnd + 1 : lineEnd;
    }
    return false;
}

static bool weaponArchiveReadNestedCatalogId(const char *archivePath,
                                             const char *nestedEntry,
                                             char *out,
                                             size_t outSize)
{
    if (out && outSize) out[0] = '\0';
    if (!archivePath || !archivePath[0] || !nestedEntry || !nestedEntry[0] ||
            !out || outSize == 0) {
        return false;
    }
    mod_archive_t *arc = modArchiveOpen(archivePath);
    if (!arc) return false;
    s32 idx = modArchiveFindEntry(arc, nestedEntry);
    if (idx < 0) {
        modArchiveClose(arc);
        return false;
    }
    u32 nestedSize = 0;
    void *nestedBytes = modArchiveExtractAlloc(arc, idx, &nestedSize);
    modArchiveClose(arc);
    if (!nestedBytes || nestedSize == 0) {
        if (nestedBytes) free(nestedBytes);
        return false;
    }

    static const char *kDescriptorEntries[] = {
        "model.ini", "animation.ini", "sound.ini", "voice.ini",
        "music.ini", "projectile.ini", "entity.ini", "texture.ini",
        "textures.ini"
    };
    bool found = false;
    for (size_t i = 0; i < sizeof(kDescriptorEntries) / sizeof(kDescriptorEntries[0]); i++) {
        u32 iniSize = 0;
        void *iniBytes = modArchiveExtractMemAlloc(nestedBytes, nestedSize,
            kDescriptorEntries[i], &iniSize);
        if (!iniBytes) continue;
        char *iniText = (char *)malloc((size_t)iniSize + 1);
        if (iniText) {
            memcpy(iniText, iniBytes, iniSize);
            iniText[iniSize] = '\0';
            char catalogId[CATALOG_ID_LEN];
            if (weaponIniGetValue(iniText, "catalog_id",
                    catalogId, sizeof(catalogId)) &&
                    weaponCopyCatalogRef(out, outSize, catalogId)) {
                found = true;
            }
            free(iniText);
        }
        free(iniBytes);
        if (found) break;
    }
    free(nestedBytes);
    return found;
}

static void weaponToolPopulateTemplateRefs(const asset_entry_t *e)
{
    char value[CATALOG_ID_LEN];
    if (weaponIniGetValue(s_WeaponIniPreview, "model_ref",
            value, sizeof(value)) ||
            weaponIniGetValue(s_WeaponIniPreview, "model_file",
            value, sizeof(value)) ||
            weaponIniGetValue(s_WeaponIniPreview, "model",
            value, sizeof(value))) {
        weaponCopyCatalogRef(s_WeaponEditModelRef,
            sizeof(s_WeaponEditModelRef), value);
    }
    if (!s_WeaponEditModelRef[0] && e && e->ext.weapon.model_file[0]) {
        weaponCopyCatalogRef(s_WeaponEditModelRef,
            sizeof(s_WeaponEditModelRef), e->ext.weapon.model_file);
    }
    if (!s_WeaponEditModelRef[0]) {
        weaponArchiveReadNestedCatalogId(s_WeaponEditTemplateArchive,
            "models/held_hi.pdmesh", s_WeaponEditModelRef,
            sizeof(s_WeaponEditModelRef));
    }

    if (weaponIniGetValue(s_WeaponIniPreview, "texture_ref",
            value, sizeof(value))) {
        weaponCopyCatalogRef(s_WeaponEditTextureRef,
            sizeof(s_WeaponEditTextureRef), value);
    }

    if (weaponIniGetValue(s_WeaponIniPreview, "animation_ref",
            value, sizeof(value))) {
        weaponCopyCatalogRef(s_WeaponEditAnimationRef,
            sizeof(s_WeaponEditAnimationRef), value);
    }
    if (!s_WeaponEditAnimationRef[0]) {
        char tsv[4096];
        if (weaponArchiveReadEntryText(s_WeaponEditTemplateArchive,
                "animations_manifest.tsv", tsv, sizeof(tsv)) &&
                weaponTsvFirstField(tsv, "catalog_id", value, sizeof(value))) {
            weaponCopyCatalogRef(s_WeaponEditAnimationRef,
                sizeof(s_WeaponEditAnimationRef), value);
        }
    }

    if (weaponIniGetValue(s_WeaponIniPreview, "audio_ref",
            value, sizeof(value))) {
        weaponCopyCatalogRef(s_WeaponEditAudioRef,
            sizeof(s_WeaponEditAudioRef), value);
    }
    if (!s_WeaponEditAudioRef[0]) {
        char tsv[4096];
        char nestedEntry[FS_MAXPATH];
        if (weaponArchiveReadEntryText(s_WeaponEditTemplateArchive,
                "audio_manifest.tsv", tsv, sizeof(tsv)) &&
                weaponTsvFirstField(tsv, "archive_entry",
                nestedEntry, sizeof(nestedEntry))) {
            weaponArchiveReadNestedCatalogId(s_WeaponEditTemplateArchive,
                nestedEntry, s_WeaponEditAudioRef,
                sizeof(s_WeaponEditAudioRef));
        }
    }

    if (weaponIniGetValue(s_WeaponIniPreview, "projectile_ref",
            value, sizeof(value))) {
        weaponCopyCatalogRef(s_WeaponEditProjectileRef,
            sizeof(s_WeaponEditProjectileRef), value);
    }
    if (!s_WeaponEditProjectileRef[0] &&
            weaponJsonFindFirstStringField(s_WeaponEditGraph, "projectile_ref",
            value, sizeof(value))) {
        weaponCopyCatalogRef(s_WeaponEditProjectileRef,
            sizeof(s_WeaponEditProjectileRef), value);
    }

    if (weaponIniGetValue(s_WeaponIniPreview, "entity_ref",
            value, sizeof(value))) {
        weaponCopyCatalogRef(s_WeaponEditEntityRef,
            sizeof(s_WeaponEditEntityRef), value);
    }
    if (!s_WeaponEditEntityRef[0] &&
            weaponJsonFindFirstStringField(s_WeaponEditGraph, "entity_ref",
            value, sizeof(value))) {
        weaponCopyCatalogRef(s_WeaponEditEntityRef,
            sizeof(s_WeaponEditEntityRef), value);
    }
    if (!s_WeaponEditEntityRef[0] &&
            weaponJsonFindFirstStringField(s_WeaponEditGraph, "payload_ref",
            value, sizeof(value))) {
        weaponCopyCatalogRef(s_WeaponEditEntityRef,
            sizeof(s_WeaponEditEntityRef), value);
    }
}

static void weaponToolStartTemplate(const asset_entry_t *e)
{
    if (!e) {
        weaponSetStatus(false, "Select a weapon before creating a template");
        return;
    }
    if (strcmp(s_WeaponLoadedId, e->id) != 0) {
        weaponToolLoadSelected();
    }
    if (!s_WeaponArchivePath[0] || !hubPathExists(s_WeaponArchivePath)) {
        weaponSetStatus(false, "Template archive is not available yet");
        return;
    }

    memset(s_WeaponEditTemplateId, 0, sizeof(s_WeaponEditTemplateId));
    memset(s_WeaponEditTemplateArchive, 0, sizeof(s_WeaponEditTemplateArchive));
    memset(s_WeaponEditModelRef, 0, sizeof(s_WeaponEditModelRef));
    memset(s_WeaponEditTextureRef, 0, sizeof(s_WeaponEditTextureRef));
    memset(s_WeaponEditAnimationRef, 0, sizeof(s_WeaponEditAnimationRef));
    memset(s_WeaponEditAudioRef, 0, sizeof(s_WeaponEditAudioRef));
    memset(s_WeaponEditProjectileRef, 0, sizeof(s_WeaponEditProjectileRef));
    memset(s_WeaponEditEntityRef, 0, sizeof(s_WeaponEditEntityRef));
    memset(s_WeaponImportModelPath, 0, sizeof(s_WeaponImportModelPath));
    memset(s_WeaponImportTexturePath, 0, sizeof(s_WeaponImportTexturePath));
    memset(s_WeaponImportAnimationPath, 0, sizeof(s_WeaponImportAnimationPath));
    memset(s_WeaponImportAudioPath, 0, sizeof(s_WeaponImportAudioPath));
    memset(s_WeaponImportProjectilePath, 0, sizeof(s_WeaponImportProjectilePath));
    memset(s_WeaponImportEntityPath, 0, sizeof(s_WeaponImportEntityPath));
    memset(s_WeaponImportGraphPath, 0, sizeof(s_WeaponImportGraphPath));
    weaponGraphBuilderClear();

    strncpy(s_WeaponEditTemplateId, e->id, sizeof(s_WeaponEditTemplateId) - 1);
    strncpy(s_WeaponEditTemplateArchive, s_WeaponArchivePath,
            sizeof(s_WeaponEditTemplateArchive) - 1);
    s_WeaponEditWeaponId = e->ext.weapon.weapon_id;
    s_WeaponEditDualWieldable = e->ext.weapon.dual_wieldable != 0;

    const char *name = e->ext.weapon.name[0] ? e->ext.weapon.name : e->id;
    snprintf(s_WeaponEditDisplayName, sizeof(s_WeaponEditDisplayName),
             "%s Custom", name);
    weaponToolSlugify(s_WeaponEditDisplayName, s_WeaponEditSlug,
                      sizeof(s_WeaponEditSlug));
    if (!s_WeaponEditSlug[0]) {
        snprintf(s_WeaponEditSlug, sizeof(s_WeaponEditSlug), "custom_weapon");
    }
    snprintf(s_WeaponEditCatalogId, sizeof(s_WeaponEditCatalogId),
             "user:%s", s_WeaponEditSlug);

    if (e->ext.weapon.model_file[0]) {
        strncpy(s_WeaponEditModelRef, e->ext.weapon.model_file,
                sizeof(s_WeaponEditModelRef) - 1);
    }
    if (s_WeaponGraphPreview[0]) {
        strncpy(s_WeaponEditGraph, s_WeaponGraphPreview,
                sizeof(s_WeaponEditGraph) - 1);
        s_WeaponEditGraph[sizeof(s_WeaponEditGraph) - 1] = '\0';
    } else {
        weaponDefaultGraph(s_WeaponEditCatalogId, s_WeaponEditGraph,
                           sizeof(s_WeaponEditGraph));
    }
    weaponReplaceJsonStringField(s_WeaponEditGraph, sizeof(s_WeaponEditGraph),
                                 "asset_id", s_WeaponEditCatalogId);
    weaponToolPopulateTemplateRefs(e);
    if (!weaponGraphBuilderLoadEditModelFromJson(true)) {
        weaponGraphBuilderSeedSingleShot();
    }
    if (s_WeaponNestedPreview[0]) {
        strncpy(s_WeaponEditNested, s_WeaponNestedPreview,
                sizeof(s_WeaponEditNested) - 1);
        s_WeaponEditNested[sizeof(s_WeaponEditNested) - 1] = '\0';
    } else {
        snprintf(s_WeaponEditNested, sizeof(s_WeaponEditNested),
                 "{\n  \"schema\": \"pd.weapon_nested_payloads.v1\",\n"
                 "  \"asset_id\": \"%s\",\n  \"payloads\": []\n}\n",
                 s_WeaponEditCatalogId);
    }

    s_WeaponEditActive = true;
    s_WeaponTemplateMenuOpen = true;
    weaponSetStatus(true, "Template ready. Edit fields, then save as a weapon mod.");
}

static bool weaponAddImportFile(mod_archive_writer_t *w,
                                const char *folder,
                                const char *srcPath,
                                char *outEntry,
                                size_t outEntrySize)
{
    if (outEntry && outEntrySize) outEntry[0] = '\0';
    if (!w || !srcPath || !srcPath[0]) return true;
    char entry[FS_MAXPATH];
    if (!weaponImportEntryName(folder, srcPath, entry, sizeof(entry))) {
        return false;
    }
    if (modArchiveAddFileDisk(w, entry, srcPath) != MODARCHIVE_OK) {
        return false;
    }
    if (outEntry && outEntrySize) {
        strncpy(outEntry, entry, outEntrySize - 1);
        outEntry[outEntrySize - 1] = '\0';
    }
    return true;
}

static const char *weaponExtForCatalogType(asset_type_e type)
{
    switch (type) {
        case ASSET_MODEL:      return ".pdmesh";
        case ASSET_ANIMATION:  return ".pdanim";
        case ASSET_PROJECTILE: return ".pdprojectile";
        case ASSET_ENTITY:     return ".pdentity";
        default:               return "";
    }
}

static const char *weaponExtFromRef(const char *srcRef)
{
    if (!srcRef || !srcRef[0]) return "";
    const char *sep = strstr(srcRef, "::");
    const char *name = sep ? sep + 2 : weaponPathLeaf(srcRef);
    const char *dot = strrchr(name, '.');
    return dot ? dot : "";
}

static bool weaponArchiveRefPayloadSource(const char *srcRef,
                                          char *archiveOut, size_t archiveOutSize,
                                          char *entryOut, size_t entryOutSize)
{
    if (!srcRef || !archiveOut || !entryOut ||
            archiveOutSize == 0 || entryOutSize == 0) return false;
    archiveOut[0] = '\0';
    entryOut[0] = '\0';
    const char *sep = strstr(srcRef, "::");
    if (!sep || sep == srcRef || !sep[2]) return false;
    size_t archiveLen = (size_t)(sep - srcRef);
    if (archiveLen >= archiveOutSize) archiveLen = archiveOutSize - 1;
    memcpy(archiveOut, srcRef, archiveLen);
    archiveOut[archiveLen] = '\0';
    strncpy(entryOut, sep + 2, entryOutSize - 1);
    entryOut[entryOutSize - 1] = '\0';
    return archiveOut[0] && entryOut[0];
}

static bool weaponAddArchiveRefPayload(mod_archive_writer_t *w,
                                       const char *entry,
                                       const char *srcRef)
{
    if (!w || !entry || !entry[0] || !srcRef || !srcRef[0]) return false;
    char archiveRef[FS_MAXPATH];
    char nestedEntry[FS_MAXPATH];
    if (weaponArchiveRefPayloadSource(srcRef, archiveRef, sizeof(archiveRef),
            nestedEntry, sizeof(nestedEntry))) {
        char archiveFull[FS_MAXPATH + 1];
        const char *archivePath = archiveRef;
        if (!hubPathIsAbsolute(archiveRef)) {
            archivePath = fsFullPath(archiveRef, archiveFull, sizeof(archiveFull));
        }
        mod_archive_t *arc = modArchiveOpen(archivePath);
        if (!arc) return false;
        s32 idx = modArchiveFindEntry(arc, nestedEntry);
        if (idx < 0) {
            modArchiveClose(arc);
            return false;
        }
        u32 size = 0;
        void *bytes = modArchiveExtractAlloc(arc, idx, &size);
        modArchiveClose(arc);
        if (!bytes) return false;
        bool ok = modArchiveAddFileMem(w, entry, bytes, size) == MODARCHIVE_OK;
        free(bytes);
        return ok;
    }

    char full[FS_MAXPATH + 1];
    const char *srcPath = srcRef;
    if (!hubPathIsAbsolute(srcRef)) {
        srcPath = fsFullPath(srcRef, full, sizeof(full));
    }
    return srcPath && srcPath[0] &&
           modArchiveAddFileDisk(w, entry, srcPath) == MODARCHIVE_OK;
}

static bool weaponResolveDataArchiveForCatalog(asset_type_e type,
                                               const asset_entry_t *e,
                                               const char *catalogId,
                                               char *out, size_t outSize)
{
    if (!out || outSize == 0 || !catalogId || !catalogId[0]) return false;
    out[0] = '\0';
    char slug[128];
    char rel[FS_MAXPATH];
    weaponIdToFilename(catalogId, slug, sizeof(slug));

    if (type == ASSET_MODEL) {
        snprintf(rel, sizeof(rel), "meshes/%s.pdmesh", slug);
    } else if (type == ASSET_ANIMATION) {
        snprintf(rel, sizeof(rel), "animations/%s.pdanim", slug);
    } else if (type == ASSET_AUDIO && e) {
        const char *folder = "sfx";
        const char *ext = ".pdsfx";
        if (e->ext.audio.category == AUDIO_CAT_VOICE) {
            folder = "voice";
            ext = ".pdvoice";
        } else if (e->ext.audio.category == AUDIO_CAT_MUSIC) {
            folder = "music";
            ext = ".pdsong";
        }
        snprintf(rel, sizeof(rel), "audio/%s/%s%s", folder, slug, ext);
    } else {
        return false;
    }
    fsDataPathFor(rel, out, outSize);
    return out[0] && fsFileSize(out) > 0;
}

static bool weaponResolveCatalogPayloadSource(asset_type_e type,
                                              const char *catalogId,
                                              char *out, size_t outSize)
{
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    if (!catalogId || !catalogId[0]) return false;
    const asset_entry_t *e = assetCatalogResolve(catalogId);
    if (!e) return false;

    const char *direct = NULL;
    if (type == ASSET_TEXTURE) {
        direct = e->ext.texture.file_path;
    } else if (type == ASSET_AUDIO) {
        direct = e->ext.audio.file_path;
    }
    if (direct && direct[0]) {
        strncpy(out, direct, outSize - 1);
        out[outSize - 1] = '\0';
        return true;
    }

    const char *expectedExt = weaponExtForCatalogType(type);
    if (expectedExt[0] && strstr(e->dirpath, expectedExt)) {
        strncpy(out, e->dirpath, outSize - 1);
        out[outSize - 1] = '\0';
        return true;
    }
    if (type == ASSET_TEXTURE && e->dirpath[0]) {
        const char *ext = weaponExtFromRef(e->dirpath);
        if (ext && ext[0]) {
            strncpy(out, e->dirpath, outSize - 1);
            out[outSize - 1] = '\0';
            return true;
        }
    }
    return weaponResolveDataArchiveForCatalog(type, e, catalogId, out, outSize);
}

static bool weaponAddCatalogAssetArchive(mod_archive_writer_t *w,
                                         asset_type_e type,
                                         const char *catalogId,
                                         const char *folder,
                                         char *outEntry,
                                         size_t outEntrySize)
{
    if (outEntry && outEntrySize) outEntry[0] = '\0';
    if (!catalogId || !catalogId[0]) return true;
    if (!strchr(catalogId, ':')) return true;
    char srcRef[FS_MAXPATH];
    if (!weaponResolveCatalogPayloadSource(type, catalogId,
            srcRef, sizeof(srcRef))) {
        return false;
    }
    const char *ext = weaponExtForCatalogType(type);
    if (!ext[0]) ext = weaponExtFromRef(srcRef);
    if (!ext || !ext[0]) return false;

    char slug[128];
    weaponIdToFilename(catalogId, slug, sizeof(slug));
    char entry[FS_MAXPATH];
    snprintf(entry, sizeof(entry), "%s/catalog_%s%s", folder, slug, ext);
    if (!weaponAddArchiveRefPayload(w, entry, srcRef)) return false;
    if (outEntry && outEntrySize) {
        strncpy(outEntry, entry, outEntrySize - 1);
        outEntry[outEntrySize - 1] = '\0';
    }
    return true;
}

static bool weaponEntryMatchesImport(const char *entry,
                                     const char *modelEntry,
                                     const char *textureEntry,
                                     const char *animEntry,
                                     const char *audioEntry,
                                     const char *projectileEntry,
                                     const char *entityEntry)
{
    return (modelEntry && modelEntry[0] && strcmp(entry, modelEntry) == 0) ||
           (textureEntry && textureEntry[0] && strcmp(entry, textureEntry) == 0) ||
           (animEntry && animEntry[0] && strcmp(entry, animEntry) == 0) ||
           (audioEntry && audioEntry[0] && strcmp(entry, audioEntry) == 0) ||
           (projectileEntry && projectileEntry[0] && strcmp(entry, projectileEntry) == 0) ||
           (entityEntry && entityEntry[0] && strcmp(entry, entityEntry) == 0);
}

static bool weaponCopyTemplatePayloads(mod_archive_writer_t *w,
                                       const char *archivePath,
                                       const char *modelEntry,
                                       const char *textureEntry,
                                       const char *animEntry,
                                       const char *audioEntry,
                                       const char *projectileEntry,
                                       const char *entityEntry)
{
    mod_archive_t *arc = modArchiveOpen(archivePath);
    if (!arc) return false;
    const s32 count = modArchiveGetEntryCount(arc);
    bool ok = true;
    for (s32 i = 0; i < count; i++) {
        const char *name = modArchiveGetEntryName(arc, i);
        if (!name || !name[0]) continue;
        if (strcmp(name, "weapon.ini") == 0 ||
                strcmp(name, "manifest.json") == 0 ||
                strcmp(name, "behavior.graph.json") == 0 ||
                strcmp(name, "nested_payloads.json") == 0) {
            continue;
        }
        if (weaponEntryMatchesImport(name, modelEntry, textureEntry,
                                     animEntry, audioEntry,
                                     projectileEntry, entityEntry)) {
            continue;
        }
        u32 size = 0;
        void *bytes = modArchiveExtractAlloc(arc, i, &size);
        if (!bytes) {
            ok = false;
            break;
        }
        if (modArchiveAddFileMem(w, name, bytes, size) != MODARCHIVE_OK) {
            free(bytes);
            ok = false;
            break;
        }
        free(bytes);
    }
    modArchiveClose(arc);
    return ok;
}

static bool weaponToolSaveCustom(void)
{
    if (!s_WeaponEditActive) {
        weaponSetStatus(false, "Create a template before saving");
        return false;
    }
    char rawSlug[sizeof(s_WeaponEditSlug)];
    snprintf(rawSlug, sizeof(rawSlug), "%s",
             s_WeaponEditSlug[0] ? s_WeaponEditSlug : s_WeaponEditDisplayName);
    weaponToolSlugify(rawSlug, s_WeaponEditSlug, sizeof(s_WeaponEditSlug));
    if (!s_WeaponEditSlug[0]) {
        weaponSetStatus(false, "Use a valid weapon name or slug");
        return false;
    }
    snprintf(s_WeaponEditCatalogId, sizeof(s_WeaponEditCatalogId),
             "user:%s", s_WeaponEditSlug);
    weaponReplaceJsonStringField(s_WeaponEditGraph, sizeof(s_WeaponEditGraph),
                                 "asset_id", s_WeaponEditCatalogId);
    if (!s_WeaponEditGraph[0]) {
        weaponDefaultGraph(s_WeaponEditCatalogId, s_WeaponEditGraph,
                           sizeof(s_WeaponEditGraph));
    }
    {
        char graphErr[192];
        if (weaponGraphValidateJson(ASSET_WEAPON, s_WeaponEditGraph,
                (u32)strlen(s_WeaponEditGraph), graphErr, sizeof(graphErr)) != 0) {
            char msg[240];
            snprintf(msg, sizeof(msg), "Graph validation failed: %s", graphErr);
            weaponSetStatus(false, msg);
            return false;
        }
    }

    if (!fsCreateDir("mods") || !fsCreateDir("mods/Weapons")) {
        weaponSetStatus(false, "Could not create mods/Weapons/");
        return false;
    }

    char modDir[FS_MAXPATH];
    snprintf(modDir, sizeof(modDir), "mods/Weapons/%s", s_WeaponEditSlug);
    if (!fsCreateDir(modDir)) {
        weaponSetStatus(false, "Could not create weapon mod folder");
        return false;
    }

    char archivePath[FS_MAXPATH];
    snprintf(archivePath, sizeof(archivePath), "%s/%s.pdweapon",
             modDir, s_WeaponEditSlug);
    mod_archive_writer_t *w = modArchiveBegin(archivePath);
    if (!w) {
        weaponSetStatus(false, "Could not create .pdweapon archive");
        return false;
    }

    char modelEntry[FS_MAXPATH] = "";
    char textureEntry[FS_MAXPATH] = "";
    char animEntry[FS_MAXPATH] = "";
    char audioEntry[FS_MAXPATH] = "";
    char projectileEntry[FS_MAXPATH] = "";
    char entityEntry[FS_MAXPATH] = "";
    char modelCatalogEntry[FS_MAXPATH] = "";
    char textureCatalogEntry[FS_MAXPATH] = "";
    char animCatalogEntry[FS_MAXPATH] = "";
    char audioCatalogEntry[FS_MAXPATH] = "";
    char projectileCatalogEntry[FS_MAXPATH] = "";
    char entityCatalogEntry[FS_MAXPATH] = "";
    bool ok = true;
    ok = ok && weaponAddImportFile(w, "models", s_WeaponImportModelPath,
                                   modelEntry, sizeof(modelEntry));
    ok = ok && weaponAddImportFile(w, "textures", s_WeaponImportTexturePath,
                                   textureEntry, sizeof(textureEntry));
    ok = ok && weaponAddImportFile(w, "animations", s_WeaponImportAnimationPath,
                                   animEntry, sizeof(animEntry));
    ok = ok && weaponAddImportFile(w, "audio", s_WeaponImportAudioPath,
                                   audioEntry, sizeof(audioEntry));
    ok = ok && weaponAddImportFile(w, "projectiles", s_WeaponImportProjectilePath,
                                   projectileEntry, sizeof(projectileEntry));
    ok = ok && weaponAddImportFile(w, "entities", s_WeaponImportEntityPath,
                                   entityEntry, sizeof(entityEntry));
    ok = ok && weaponCopyTemplatePayloads(w, s_WeaponEditTemplateArchive,
                                          modelEntry, textureEntry,
                                          animEntry, audioEntry,
                                          projectileEntry, entityEntry);
    if (ok && !modelEntry[0]) {
        ok = weaponAddCatalogAssetArchive(w, ASSET_MODEL, s_WeaponEditModelRef,
                                          "models", modelCatalogEntry,
                                          sizeof(modelCatalogEntry));
    }
    if (ok && !textureEntry[0]) {
        ok = weaponAddCatalogAssetArchive(w, ASSET_TEXTURE, s_WeaponEditTextureRef,
                                          "textures", textureCatalogEntry,
                                          sizeof(textureCatalogEntry));
    }
    if (ok && !animEntry[0]) {
        ok = weaponAddCatalogAssetArchive(w, ASSET_ANIMATION,
                                          s_WeaponEditAnimationRef,
                                          "animations", animCatalogEntry,
                                          sizeof(animCatalogEntry));
    }
    if (ok && !audioEntry[0]) {
        ok = weaponAddCatalogAssetArchive(w, ASSET_AUDIO, s_WeaponEditAudioRef,
                                          "audio", audioCatalogEntry,
                                          sizeof(audioCatalogEntry));
    }
    if (ok && !projectileEntry[0]) {
        ok = weaponAddCatalogAssetArchive(w, ASSET_PROJECTILE,
                                          s_WeaponEditProjectileRef,
                                          "projectiles", projectileCatalogEntry,
                                          sizeof(projectileCatalogEntry));
    }
    if (ok && !entityEntry[0]) {
        ok = weaponAddCatalogAssetArchive(w, ASSET_ENTITY, s_WeaponEditEntityRef,
                                          "entities", entityCatalogEntry,
                                          sizeof(entityCatalogEntry));
    }
    if (!ok) {
        modArchiveAbort(w);
        weaponSetStatus(false, "Could not embed all selected weapon payloads");
        return false;
    }

    const char *modelFile = modelEntry[0] ? modelEntry :
        (modelCatalogEntry[0] ? modelCatalogEntry : s_WeaponEditModelRef);
    const char *textureFile = textureEntry[0] ? textureEntry :
        (textureCatalogEntry[0] ? textureCatalogEntry : "");
    const char *animFile = animEntry[0] ? animEntry :
        (animCatalogEntry[0] ? animCatalogEntry : "");
    const char *audioFile = audioEntry[0] ? audioEntry :
        (audioCatalogEntry[0] ? audioCatalogEntry : "");
    const char *projectileFile = projectileEntry[0] ? projectileEntry :
        (projectileCatalogEntry[0] ? projectileCatalogEntry : "");
    const char *entityFile = entityEntry[0] ? entityEntry :
        (entityCatalogEntry[0] ? entityCatalogEntry : "");
    char weaponIni[4096];
    int weaponIniLen = snprintf(weaponIni, sizeof(weaponIni),
        "[weapon]\n"
        "schema = pd.weapon.v1\n"
        "dependency_closure = embedded.v2\n"
        "catalog_id = %s\n"
        "weapon_id = %d\n"
        "name = %s\n"
        "manifest = manifest.json\n"
        "behavior_graph = behavior.graph.json\n"
        "nested_payloads = nested_payloads.json\n"
        "model_file = %s\n"
        "dual_wieldable = %d\n"
        "\n"
        "[references]\n"
        "template = %s\n"
        "model_ref = %s\n"
        "texture_ref = %s\n"
        "animation_ref = %s\n"
        "audio_ref = %s\n"
        "projectile_ref = %s\n"
        "entity_ref = %s\n"
        "model_archive = %s\n"
        "texture_archive = %s\n"
        "animation_archive = %s\n"
        "audio_archive = %s\n"
        "projectile_archive = %s\n"
        "entity_archive = %s\n",
        s_WeaponEditCatalogId,
        (int)s_WeaponEditWeaponId,
        s_WeaponEditDisplayName,
        modelFile ? modelFile : "",
        s_WeaponEditDualWieldable ? 1 : 0,
        s_WeaponEditTemplateId,
        s_WeaponEditModelRef,
        s_WeaponEditTextureRef,
        s_WeaponEditAnimationRef,
        s_WeaponEditAudioRef,
        s_WeaponEditProjectileRef,
        s_WeaponEditEntityRef,
        modelFile ? modelFile : "",
        textureFile ? textureFile : "",
        animFile ? animFile : "",
        audioFile ? audioFile : "",
        projectileFile ? projectileFile : "",
        entityFile ? entityFile : "");
    if (weaponIniLen <= 0 || (size_t)weaponIniLen >= sizeof(weaponIni)) {
        modArchiveAbort(w);
        weaponSetStatus(false, "weapon.ini is too large");
        return false;
    }

    char escName[192];
    char escTemplate[128];
    weaponJsonEscape(s_WeaponEditDisplayName, escName, sizeof(escName));
    weaponJsonEscape(s_WeaponEditTemplateId, escTemplate, sizeof(escTemplate));

    char manifest[4096];
    int manifestLen = snprintf(manifest, sizeof(manifest),
        "{\n"
        "  \"schema\": \"pd.weapon.manifest.v1\",\n"
        "  \"catalog_id\": \"%s\",\n"
        "  \"name\": \"%s\",\n"
        "  \"template\": \"%s\",\n"
        "  \"dependency_closure\": \"embedded.v2\",\n"
        "  \"refs\": {\n"
        "    \"model\": \"%s\",\n"
        "    \"texture\": \"%s\",\n"
        "    \"animation\": \"%s\",\n"
        "    \"audio\": \"%s\",\n"
        "    \"projectile\": \"%s\",\n"
        "    \"entity\": \"%s\"\n"
        "  },\n"
        "  \"embedded\": {\n"
        "    \"model\": \"%s\",\n"
        "    \"texture\": \"%s\",\n"
        "    \"animation\": \"%s\",\n"
        "    \"audio\": \"%s\",\n"
        "    \"projectile\": \"%s\",\n"
        "    \"entity\": \"%s\"\n"
        "  }\n"
        "}\n",
        s_WeaponEditCatalogId, escName, escTemplate,
        s_WeaponEditModelRef, s_WeaponEditTextureRef,
        s_WeaponEditAnimationRef, s_WeaponEditAudioRef,
        s_WeaponEditProjectileRef, s_WeaponEditEntityRef,
        modelFile ? modelFile : "",
        textureFile ? textureFile : "",
        animFile ? animFile : "",
        audioFile ? audioFile : "",
        projectileFile ? projectileFile : "",
        entityFile ? entityFile : "");
    if (manifestLen <= 0 || (size_t)manifestLen >= sizeof(manifest)) {
        modArchiveAbort(w);
        weaponSetStatus(false, "manifest.json is too large");
        return false;
    }

    if (modArchiveAddFileMem(w, "weapon.ini", weaponIni, (u32)weaponIniLen) != MODARCHIVE_OK ||
            modArchiveAddFileMem(w, "manifest.json", manifest, (u32)manifestLen) != MODARCHIVE_OK ||
            modArchiveAddFileMem(w, "behavior.graph.json", s_WeaponEditGraph,
                                 (u32)strlen(s_WeaponEditGraph)) != MODARCHIVE_OK ||
            modArchiveAddFileMem(w, "nested_payloads.json", s_WeaponEditNested,
                                 (u32)strlen(s_WeaponEditNested)) != MODARCHIVE_OK) {
        modArchiveAbort(w);
        weaponSetStatus(false, "Could not write weapon archive roots");
        return false;
    }

    if (modArchiveFinish(w) != MODARCHIVE_OK) {
        weaponSetStatus(false, "Could not finalize .pdweapon archive");
        return false;
    }

    char modJsonPath[FS_MAXPATH];
    snprintf(modJsonPath, sizeof(modJsonPath), "%s/mod.json", modDir);
    FILE *mf = fsFileOpenWrite(modJsonPath);
    if (!mf) {
        weaponSetStatus(false, "Saved .pdweapon but could not write mod.json");
        return false;
    }
    fprintf(mf,
        "{\n"
        "  \"id\": \"user.%s.weapon\",\n"
        "  \"name\": \"%s\",\n"
        "  \"version\": \"1.0.0\",\n"
        "  \"description\": \"Weapon mod created with the Weapons tool.\",\n"
        "  \"author\": \"Player\",\n"
        "  \"tags\": [\"weapon\", \"pdweapon\", \"user\"],\n"
        "  \"enabled\": true\n"
        "}\n",
        s_WeaponEditSlug, escName);
    bool writeOk = !ferror(mf);
    fclose(mf);
    if (!writeOk) {
        weaponSetStatus(false, "Saved .pdweapon but mod.json write failed");
        return false;
    }

    char newModId[128];
    snprintf(newModId, sizeof(newModId), "user.%s.weapon", s_WeaponEditSlug);
    modmgrRescanDirectory();
    s32 modCount = modmgrGetCount();
    for (s32 i = 0; i < modCount; i++) {
        const char *id = modmgrGetModId(i);
        if (id && strcmp(id, newModId) == 0) {
            modmgrSetEnabled(i, 1);
            break;
        }
    }
    modmgrSaveConfig();
    pdguiModManagerRefreshSnapshot();
    weaponToolRefresh();
    strncpy(s_WeaponSelectedId, s_WeaponEditCatalogId,
            sizeof(s_WeaponSelectedId) - 1);
    s_WeaponSelectedId[sizeof(s_WeaponSelectedId) - 1] = '\0';

    char status[192];
    snprintf(status, sizeof(status), "Saved weapon mod: %s", archivePath);
    weaponSetStatus(true, status);
    return true;
}

static void weaponRenderCatalogPicker(const char *label,
                                      asset_type_e type,
                                      char *dst,
                                      size_t dstSize)
{
    const char *preview = (dst && dst[0]) ? dst : "(none)";
    ImGui::Text("%s", label);
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo(label, preview)) {
        if (ImGui::Selectable("(none)", !dst || !dst[0])) {
            if (dst && dstSize) dst[0] = '\0';
        }
        const s32 poolSize = assetCatalogGetPoolSize();
        for (s32 i = 0; i < poolSize; i++) {
            const asset_entry_t *e = assetCatalogGetByIndex(i);
            if (!e || e->type != type) continue;
            bool selected = dst && strcmp(dst, e->id) == 0;
            if (ImGui::Selectable(e->id, selected)) {
                if (dst && dstSize) {
                    strncpy(dst, e->id, dstSize - 1);
                    dst[dstSize - 1] = '\0';
                }
            }
        }
        ImGui::EndCombo();
    }
}

static bool weaponMeshIdLooksWeaponScoped(const char *id)
{
    if (!id || !id[0]) return false;
    const char *body = strchr(id, ':');
    body = body ? body + 1 : id;
    if (strstr(body, "weapon") != NULL) return true;
    const size_t len = strlen(body);
    return (len > 3 && strcmp(body + len - 3, "_hi") == 0)
        || (len > 3 && strcmp(body + len - 3, "_lo") == 0);
}

static bool weaponMeshEntryIsWeaponMesh(const asset_entry_t *e)
{
    if (!e || e->type != ASSET_MODEL) return false;
    if (weaponMeshIdLooksWeaponScoped(e->id)) return true;
    if (strstr(e->dirpath, "\\Weapons\\") != NULL
            || strstr(e->dirpath, "/Weapons/") != NULL
            || strstr(e->dirpath, "\\weapons\\") != NULL
            || strstr(e->dirpath, "/weapons/") != NULL) {
        return true;
    }
    return false;
}

static const asset_entry_t *weaponFindFirstMeshForPicker(bool includeNonWeapon)
{
    const s32 poolSize = assetCatalogGetPoolSize();
    for (s32 i = 0; i < poolSize; i++) {
        const asset_entry_t *e = assetCatalogGetByIndex(i);
        if (!e || e->type != ASSET_MODEL) continue;
        if (!includeNonWeapon && !weaponMeshEntryIsWeaponMesh(e)) continue;
        return e;
    }
    return NULL;
}

static void weaponOpenMeshPicker(void)
{
    s_WeaponMeshPickerSelected[0] = '\0';
    if (s_WeaponEditModelRef[0]) {
        strncpy(s_WeaponMeshPickerSelected, s_WeaponEditModelRef,
                sizeof(s_WeaponMeshPickerSelected) - 1);
        s_WeaponMeshPickerSelected[sizeof(s_WeaponMeshPickerSelected) - 1] = '\0';
    } else {
        const asset_entry_t *first =
            weaponFindFirstMeshForPicker(s_WeaponMeshPickerShowNonWeapon);
        if (first) {
            strncpy(s_WeaponMeshPickerSelected, first->id,
                    sizeof(s_WeaponMeshPickerSelected) - 1);
            s_WeaponMeshPickerSelected[sizeof(s_WeaponMeshPickerSelected) - 1] = '\0';
        }
    }
    pdguiModelPreviewInvalidate();
    s_WeaponMeshPickerOpen = true;
}

static void weaponRenderMeshSelector(float scale)
{
    ImGui::Text("Weapon Mesh");
    const char *preview = s_WeaponEditModelRef[0] ? s_WeaponEditModelRef : "(none)";
    ImGui::SetNextItemWidth(260.0f * scale);
    ImGui::BeginDisabled();
    ImGui::InputText("##weapon_mesh_ref", s_WeaponEditModelRef,
                     sizeof(s_WeaponEditModelRef));
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (PdButton("Select Mesh", ImVec2(116.0f * scale, 0))) {
        weaponOpenMeshPicker();
    }
    if (!s_WeaponEditModelRef[0]) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", preview);
    }
}

static void weaponRenderMeshPreviewPanel(float panelW, float panelH, float scale)
{
    ImGui::BeginChild("##weapon_mesh_picker_preview", ImVec2(panelW, panelH), true,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::Text("Preview");
    ImGui::Separator();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 start = ImGui::GetCursorScreenPos();
    float previewSide = avail.x < avail.y ? avail.x : avail.y;
    previewSide -= 18.0f * scale;
    if (previewSide < 120.0f * scale) previewSide = 120.0f * scale;
    if (previewSide > 420.0f * scale) previewSide = 420.0f * scale;

    float x = start.x + (avail.x - previewSide) * 0.5f;
    float y = start.y + (avail.y - previewSide) * 0.5f;
    if (y < start.y) y = start.y;

    ModelPreviewOpts opts = pdguiModelPreviewDefaultOpts();
    opts.showBodyName = 0;
    opts.showHeadName = 0;
    opts.idleRotation = 1;
    opts.idleRotSpeed = 0.35f;
    opts.cornerRadius = 4.0f * scale;

    pdguiModelPreviewDrawEx(PDGUI_MP_WEAPON,
                            s_WeaponMeshPickerSelected[0]
                                ? s_WeaponMeshPickerSelected : NULL,
                            NULL,
                            x, y, previewSide, previewSide,
                            &opts);

    ImGui::Dummy(avail);
    if (s_WeaponMeshPickerSelected[0]) {
        ImGui::TextWrapped("%s", s_WeaponMeshPickerSelected);
    } else {
        ImGui::TextDisabled("No mesh selected.");
    }
    ImGui::EndChild();
}

static void weaponRenderMeshPickerPopup(float scale)
{
    if (s_WeaponMeshPickerOpen) {
        ImGui::OpenPopup("Select Weapon Mesh");
    }

    const float viewportW = (float)viGetWidth();
    const float viewportH = (float)viGetHeight();
    float popupW = 900.0f * scale;
    float popupH = 560.0f * scale;
    if (popupW > viewportW - 80.0f * scale) popupW = viewportW - 80.0f * scale;
    if (popupH > viewportH - 80.0f * scale) popupH = viewportH - 80.0f * scale;
    if (popupW < 620.0f * scale) popupW = 620.0f * scale;
    if (popupH < 420.0f * scale) popupH = 420.0f * scale;
    ImGui::SetNextWindowSize(ImVec2(popupW, popupH), ImGuiCond_Appearing);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings
                           | ImGuiWindowFlags_NoCollapse;
    if (ImGui::BeginPopupModal("Select Weapon Mesh",
                               &s_WeaponMeshPickerOpen, flags)) {
        ImGui::Checkbox("Show non-weapon meshes",
                        &s_WeaponMeshPickerShowNonWeapon);
        ImGui::Separator();

        const float actionH = 36.0f * scale;
        const float bodyH = ImGui::GetContentRegionAvail().y - actionH
                          - ImGui::GetStyle().ItemSpacing.y;
        const float listW = popupW * 0.44f;
        const float previewW = ImGui::GetContentRegionAvail().x - listW
                             - ImGui::GetStyle().ItemSpacing.x;

        ImGui::BeginChild("##weapon_mesh_picker_list",
                          ImVec2(listW, bodyH), true,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);
        if (ImGui::Selectable("(none)", !s_WeaponMeshPickerSelected[0])) {
            s_WeaponMeshPickerSelected[0] = '\0';
            pdguiModelPreviewInvalidate();
        }

        int visibleCount = 0;
        const s32 poolSize = assetCatalogGetPoolSize();
        for (s32 i = 0; i < poolSize; i++) {
            const asset_entry_t *e = assetCatalogGetByIndex(i);
            if (!e || e->type != ASSET_MODEL) continue;
            const bool isWeaponMesh = weaponMeshEntryIsWeaponMesh(e);
            if (!s_WeaponMeshPickerShowNonWeapon && !isWeaponMesh) continue;
            visibleCount++;

            char label[CATALOG_ID_LEN + 32];
            snprintf(label, sizeof(label), "%s%s##weapon_mesh_%d",
                     e->id, isWeaponMesh ? "" : "  (non-weapon)", (int)i);
            bool selected = strcmp(s_WeaponMeshPickerSelected, e->id) == 0;
            if (ImGui::Selectable(label, selected)) {
                strncpy(s_WeaponMeshPickerSelected, e->id,
                        sizeof(s_WeaponMeshPickerSelected) - 1);
                s_WeaponMeshPickerSelected[sizeof(s_WeaponMeshPickerSelected) - 1] = '\0';
                pdguiModelPreviewInvalidate();
            }
        }
        if (visibleCount == 0) {
            ImGui::TextDisabled("No catalog meshes match the current filter.");
        }
        ImGui::EndChild();

        ImGui::SameLine();
        weaponRenderMeshPreviewPanel(previewW, bodyH, scale);

        if (PdButton("Use Mesh", ImVec2(112.0f * scale, 28.0f * scale))) {
            strncpy(s_WeaponEditModelRef, s_WeaponMeshPickerSelected,
                    sizeof(s_WeaponEditModelRef) - 1);
            s_WeaponEditModelRef[sizeof(s_WeaponEditModelRef) - 1] = '\0';
            s_WeaponMeshPickerOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (PdButton("Cancel", ImVec2(92.0f * scale, 28.0f * scale))) {
            s_WeaponMeshPickerOpen = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

static void weaponRenderImportRow(const char *label,
                                  WeaponImportTarget target,
                                  const char *filters)
{
    char *path = weaponImportPathMutableForTarget(target);
    if (!path) return;
    ImGui::Text("%s", label);
    ImGui::SetNextItemWidth(-96.0f);
    char inputId[64];
    snprintf(inputId, sizeof(inputId), "##weapon_import_%d", (int)target);
    ImGui::InputText(inputId, path, FS_MAXPATH);
    ImGui::SameLine();
    if (PdButton("Browse", ImVec2(86.0f, 0))) {
        s_WeaponImportTarget = target;
        pdguiFileBrowserOpen(label, "mods", filters);
    }
}

static bool weaponRenderGraphNodeCombo(const char *label, int *value, bool allowNone)
{
    if (!value) return false;
    bool changed = false;
    const char *preview = (*value >= 0 && *value < s_WeaponGraphNodeCount)
        ? weaponGraphNodeLabel(*value)
        : "(none)";
    if (ImGui::BeginCombo(label, preview)) {
        if (allowNone && ImGui::Selectable("(none)", *value < 0)) {
            *value = -1;
            changed = true;
        }
        for (int i = 0; i < s_WeaponGraphNodeCount; i++) {
            bool selected = *value == i;
            if (ImGui::Selectable(weaponGraphNodeLabel(i), selected)) {
                *value = i;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

static bool weaponRenderGraphScopeCombo(const char *label, char *scope, size_t scopeSize)
{
    if (!scope || scopeSize == 0) return false;
    bool changed = false;
    const char *preview = scope[0] ? scope : "primary";
    if (ImGui::BeginCombo(label, preview)) {
        for (int i = 0; i < weaponGraphScopeCount(); i++) {
            bool selected = strcmp(preview, s_WeaponGraphScopes[i]) == 0;
            if (ImGui::Selectable(s_WeaponGraphScopes[i], selected)) {
                strncpy(scope, s_WeaponGraphScopes[i], scopeSize - 1);
                scope[scopeSize - 1] = '\0';
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

static void weaponRenderGraphBuilder(const char *scopeFilter,
                                     const char *graphTitle,
                                     float scale)
{
    const char *title = graphTitle && graphTitle[0]
        ? graphTitle : "Weapon Behavior Graph";
    ImGui::Text("%s", title);
    ImGui::SameLine();
    ImGui::TextDisabled("%d modules, %d edges",
                        s_WeaponGraphNodeCount, s_WeaponGraphEdgeCount);

    if (PdButton("Seed Single Shot", ImVec2(132.0f * scale, 26.0f * scale))) {
        weaponGraphBuilderSeedSingleShot();
    }
    ImGui::SameLine();
    if (PdButton("Seed Dual Fire Modes", ImVec2(168.0f * scale, 26.0f * scale))) {
        weaponGraphBuilderSeedDualFireModes();
    }
    ImGui::SameLine();
    if (PdButton("Seed Projectile", ImVec2(132.0f * scale, 26.0f * scale))) {
        weaponGraphBuilderSeedProjectile();
    }
    ImGui::SameLine();
    if (PdButton("Seed Mine Link", ImVec2(126.0f * scale, 26.0f * scale))) {
        weaponGraphBuilderSeedMineLink();
    }
    if (PdButton("Seed Automatic", ImVec2(132.0f * scale, 26.0f * scale))) {
        weaponGraphBuilderSeedAutomatic();
    }
    ImGui::SameLine();
    if (PdButton("Seed Thrown", ImVec2(112.0f * scale, 26.0f * scale))) {
        weaponGraphBuilderSeedThrown();
    }
    ImGui::SameLine();
    if (PdButton("Seed Laptop Control", ImVec2(160.0f * scale, 26.0f * scale))) {
        weaponGraphBuilderSeedLaptopControl();
    }
    ImGui::SameLine();
    if (PdButton("Clear", ImVec2(72.0f * scale, 26.0f * scale))) {
        weaponGraphBuilderClear();
        weaponDefaultGraph(s_WeaponEditCatalogId, s_WeaponEditGraph,
                           sizeof(s_WeaponEditGraph));
        weaponSetStatus(true, "Graph builder cleared");
    }

    PdWeaponGraphEditorDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.model = &s_WeaponGraphModel;
    desc.modules = s_WeaponGraphModules;
    desc.module_count = weaponGraphModuleCount();
    desc.contexts = s_WeaponGraphContextDefs;
    desc.context_count = weaponGraphContextCount();
    desc.scopes = s_WeaponGraphScopes;
    desc.scope_count = weaponGraphScopeCount();
    desc.scope_filter = scopeFilter;
    desc.scope_label = title;
    desc.scale = scale;

    ImGui::BeginChild("##weapon_graph_node_editor_region",
                      ImVec2(-1.0f, 430.0f * scale), true,
                      ImGuiWindowFlags_NoScrollbar);
    PdWeaponGraphEditorResult result;
    if (pdguiWeaponGraphNodeEditorRender(&desc, &result)) {
        weaponGraphBuilderApplyEditorResult(result);
    }
    ImGui::EndChild();

    if (PdButton("Validate Graph", ImVec2(124.0f * scale, 26.0f * scale))) {
        char graphErr[192];
        if (weaponGraphValidateJson(ASSET_WEAPON, s_WeaponEditGraph,
                (u32)strlen(s_WeaponEditGraph), graphErr, sizeof(graphErr)) == 0) {
            weaponSetStatus(true, "Graph validation passed");
        } else {
            char msg[240];
            snprintf(msg, sizeof(msg), "Graph validation failed: %s", graphErr);
            weaponSetStatus(false, msg);
        }
    }

    if (ImGui::CollapsingHeader("Advanced JSON")) {
        ImGui::InputTextMultiline("##weapon_edit_graph",
                                  s_WeaponEditGraph,
                                  sizeof(s_WeaponEditGraph),
                                  ImVec2(-1.0f, 150.0f * scale),
                                  ImGuiInputTextFlags_AllowTabInput);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            weaponReplaceJsonStringField(s_WeaponEditGraph,
                                         sizeof(s_WeaponEditGraph),
                                         "asset_id",
                                         s_WeaponEditCatalogId);
            weaponGraphBuilderLoadEditModelFromJson(false);
        }
    }
}

static void weaponRenderTemplateEditor(float scale)
{
    ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow());
    ImGui::Text("Weapon Mod Creation");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (PdButton("Close Creator", ImVec2(132.0f * scale, 28.0f * scale))) {
        s_WeaponTemplateMenuOpen = false;
        return;
    }
    ImGui::Separator();

    if (!s_WeaponEditActive) {
        ImGui::TextDisabled("Use the selected weapon as a template to start a new weapon mod.");
        return;
    }

    ImGui::Text("Template: %s", s_WeaponEditTemplateId);
    ImGui::Text("Saves to: mods/Weapons/%s/%s.pdweapon",
                s_WeaponEditSlug[0] ? s_WeaponEditSlug : "(slug)",
                s_WeaponEditSlug[0] ? s_WeaponEditSlug : "(slug)");
    ImGui::Separator();

    if (ImGui::BeginTabBar("##weapon_creator_tabs")) {
        if (ImGui::BeginTabItem("Details")) {
            ImGui::SetNextItemWidth(300.0f * scale);
            ImGui::InputText("Display Name", s_WeaponEditDisplayName,
                             sizeof(s_WeaponEditDisplayName));
            ImGui::SetNextItemWidth(220.0f * scale);
            ImGui::InputText("Slug", s_WeaponEditSlug,
                             sizeof(s_WeaponEditSlug));
            char catalogPreview[CATALOG_ID_LEN];
            char slugRaw[sizeof(s_WeaponEditSlug)];
            char slugPreview[sizeof(s_WeaponEditSlug)];
            snprintf(slugRaw, sizeof(slugRaw), "%s", s_WeaponEditSlug);
            weaponToolSlugify(slugRaw, slugPreview, sizeof(slugPreview));
            snprintf(catalogPreview, sizeof(catalogPreview), "user:%s",
                     slugPreview[0] ? slugPreview : "(invalid)");
            ImGui::Text("Catalog ID: %s", catalogPreview);
            ImGui::SetNextItemWidth(120.0f * scale);
            ImGui::InputInt("Weapon ID", &s_WeaponEditWeaponId);
            ImGui::Checkbox("Dual wieldable", &s_WeaponEditDualWieldable);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Assets")) {
            ImGui::Columns(2, "##weapon_editor_cols", false);
            weaponRenderMeshSelector(scale);
            weaponRenderCatalogPicker("Texture Catalog", ASSET_TEXTURE,
                                      s_WeaponEditTextureRef,
                                      sizeof(s_WeaponEditTextureRef));
            weaponRenderCatalogPicker("Animation Catalog", ASSET_ANIMATION,
                                      s_WeaponEditAnimationRef,
                                      sizeof(s_WeaponEditAnimationRef));
            weaponRenderCatalogPicker("Audio Catalog", ASSET_AUDIO,
                                      s_WeaponEditAudioRef,
                                      sizeof(s_WeaponEditAudioRef));
            ImGui::NextColumn();
            weaponRenderCatalogPicker("Projectile Catalog", ASSET_PROJECTILE,
                                      s_WeaponEditProjectileRef,
                                      sizeof(s_WeaponEditProjectileRef));
            weaponRenderCatalogPicker("Entity Catalog", ASSET_ENTITY,
                                      s_WeaponEditEntityRef,
                                      sizeof(s_WeaponEditEntityRef));
            ImGui::Columns(1);

            ImGui::Separator();
            weaponRenderImportRow("Import Model", WEAPON_IMPORT_MODEL,
                                  ".pdmesh;.gltf;.glb;.obj");
            weaponRenderImportRow("Import Texture", WEAPON_IMPORT_TEXTURE,
                                  ".pdtexture;.png;.tga;.jpg;.bmp");
            weaponRenderImportRow("Import Animation", WEAPON_IMPORT_ANIMATION,
                                  ".pdanim");
            weaponRenderImportRow("Import Audio", WEAPON_IMPORT_AUDIO,
                                  ".pdsfx;.pdvoice;.pdsong;.wav;.ogg;.mp3");
            weaponRenderImportRow("Import Projectile", WEAPON_IMPORT_PROJECTILE,
                                  ".pdprojectile");
            weaponRenderImportRow("Import Entity", WEAPON_IMPORT_ENTITY,
                                  ".pdentity");
            weaponRenderImportRow("Import Graph JSON", WEAPON_IMPORT_GRAPH,
                                  ".json");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Primary Graph")) {
            weaponRenderGraphBuilder("primary", "Primary Graph", scale);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Secondary Graph")) {
            weaponRenderGraphBuilder("secondary", "Secondary Graph", scale);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Payloads")) {
            ImGui::Text("Nested Payloads");
            ImGui::InputTextMultiline("##weapon_edit_nested",
                                      s_WeaponEditNested,
                                      sizeof(s_WeaponEditNested),
                                      ImVec2(-1.0f, 220.0f * scale),
                                      ImGuiInputTextFlags_AllowTabInput);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    bool canSave = s_WeaponEditDisplayName[0] &&
                   s_WeaponEditSlug[0] &&
                   s_WeaponEditTemplateArchive[0];
    if (!canSave) ImGui::BeginDisabled();
    if (PdButton("Save Weapon Mod", ImVec2(164.0f * scale, 28.0f * scale))) {
        weaponToolSaveCustom();
    }
    if (!canSave) ImGui::EndDisabled();
}

static void weaponRenderTemplateWindow(s32 winW, s32 winH, float scale)
{
    if (!s_WeaponTemplateMenuOpen) return;

    float windowW = 1120.0f * scale;
    float windowH = 720.0f * scale;
    if (windowW > (float)winW - 64.0f * scale) windowW = (float)winW - 64.0f * scale;
    if (windowH > (float)winH - 64.0f * scale) windowH = (float)winH - 64.0f * scale;
    if (windowW < 760.0f * scale) windowW = 760.0f * scale;
    if (windowH < 520.0f * scale) windowH = 520.0f * scale;

    ImGui::SetNextWindowSize(ImVec2(windowW, windowH), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(((float)winW - windowW) * 0.5f,
                                   ((float)winH - windowH) * 0.5f),
                            ImGuiCond_Appearing);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings
                           | ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Create Weapon Mod", &s_WeaponTemplateMenuOpen, flags)) {
        weaponRenderTemplateEditor(scale);
        weaponRenderMeshPickerPopup(scale);
        if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)
                && pdguiMenuCancelPressed()) {
            s_WeaponTemplateMenuOpen = false;
        }
    }
    ImGui::End();
}

static void renderWeaponTool(float contentW, float contentH, float scale)
{
    if (s_WeaponImportTarget != WEAPON_IMPORT_NONE && pdguiFileBrowserIsOpen()) {
        if (pdguiFileBrowserRender()) {
            char *dst = weaponImportPathMutableForTarget(s_WeaponImportTarget);
            if (dst) {
                strncpy(dst, pdguiFileBrowserGetPath(), FS_MAXPATH - 1);
                dst[FS_MAXPATH - 1] = '\0';
                if (s_WeaponImportTarget == WEAPON_IMPORT_GRAPH) {
                    if (weaponReadTextFilePreview(dst, s_WeaponEditGraph,
                                                  sizeof(s_WeaponEditGraph))) {
                        weaponReplaceJsonStringField(s_WeaponEditGraph,
                                                     sizeof(s_WeaponEditGraph),
                                                     "asset_id",
                                                     s_WeaponEditCatalogId);
                        if (weaponGraphBuilderLoadEditModelFromJson(false)) {
                            weaponSetStatus(true, "Imported behavior graph JSON");
                        }
                    } else {
                        weaponSetStatus(false, "Could not read graph JSON file");
                    }
                }
            }
            s_WeaponImportTarget = WEAPON_IMPORT_NONE;
            pdguiFileBrowserClose();
        }
        return;
    }

    const float listW = contentW * 0.30f;
    const float detailW = contentW - listW - ImGui::GetStyle().ItemSpacing.x;
    bool haveSelection = false;

    ImGui::BeginChild("##weapon_list", ImVec2(listW, contentH), true);
    const s32 poolSize = assetCatalogGetPoolSize();
    for (s32 i = 0; i < poolSize; i++) {
        const asset_entry_t *e = assetCatalogGetByIndex(i);
        if (!e || e->type != ASSET_WEAPON) continue;
        if (!haveSelection && !s_WeaponSelectedId[0]) {
            strncpy(s_WeaponSelectedId, e->id, sizeof(s_WeaponSelectedId) - 1);
            s_WeaponSelectedId[sizeof(s_WeaponSelectedId) - 1] = '\0';
        }
        haveSelection = true;

        char label[CATALOG_ID_LEN + 80];
        const char *name = e->ext.weapon.name[0] ? e->ext.weapon.name : e->id;
        snprintf(label, sizeof(label), "%s##weapon_%d", name, (int)i);
        bool selected = strcmp(s_WeaponSelectedId, e->id) == 0;
        if (ImGui::Selectable(label, selected)) {
            strncpy(s_WeaponSelectedId, e->id, sizeof(s_WeaponSelectedId) - 1);
            s_WeaponSelectedId[sizeof(s_WeaponSelectedId) - 1] = '\0';
            weaponToolRefresh();
        }
    }
    if (!haveSelection) {
        ImGui::TextDisabled("No weapon assets registered.");
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##weapon_detail", ImVec2(detailW, contentH),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);

    const asset_entry_t *e = weaponSelectedEntry();
    if (e && strcmp(s_WeaponLoadedId, e->id) != 0) {
        weaponToolLoadSelected();
    }

    if (!e) {
        ImGui::TextDisabled("Select a weapon.");
        ImGui::EndChild();
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow());
    ImGui::Text("%s", e->ext.weapon.name[0] ? e->ext.weapon.name : e->id);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", e->bundled ? "Base" : "Mod");
    ImGui::Separator();

    ImGui::Text("Catalog ID: %s", e->id);
    ImGui::Text("Weapon ID: %d", (int)e->ext.weapon.weapon_id);
    ImGui::Text("Runtime Index: %d", (int)e->runtime_index);
    ImGui::Text("Model: %s", e->ext.weapon.model_file[0] ? e->ext.weapon.model_file : "(catalog/runtime)");
    ImGui::Text("Dual Wield: %s", e->ext.weapon.dual_wieldable ? "yes" : "no");
    ImGui::Text("Archive: %s", s_WeaponArchivePath[0] ? s_WeaponArchivePath : "(not resolved)");

    if (PdButton("Use as Template", ImVec2(176.0f * scale, 28.0f * scale))) {
        weaponToolStartTemplate(e);
    }
    if (s_WeaponEditActive) {
        ImGui::SameLine();
        if (PdButton("Open Creator", ImVec2(132.0f * scale, 28.0f * scale))) {
            s_WeaponTemplateMenuOpen = true;
        }
    }
    ImGui::TextDisabled("%s",
                        e->bundled ? "Base weapon is read-only; templates save as a new mod."
                                   : "Mod weapon can be cloned into a new self-contained .pdweapon.");

    ImGui::PushStyleColor(ImGuiCol_Text,
                          s_WeaponStatusOk ? ImVec4(0.55f, 0.90f, 0.68f, 0.86f)
                                           : pdguiVec4TextWarning(220));
    ImGui::TextWrapped("%s", s_WeaponStatus[0] ? s_WeaponStatus : "Ready");
    ImGui::PopStyleColor();

    if (ImGui::BeginTabBar("##weapon_detail_tabs")) {
        if (ImGui::BeginTabItem("Descriptor")) {
            ImGui::BeginChild("##weapon_ini_preview", ImVec2(0, 0), true,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(s_WeaponIniPreview[0] ? s_WeaponIniPreview : "(weapon.ini unavailable)");
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Behavior Graph")) {
            ImGui::BeginChild("##weapon_graph_preview", ImVec2(0, 0), true,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(s_WeaponGraphPreview[0] ? s_WeaponGraphPreview : "(behavior.graph.json unavailable)");
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Nested Payloads")) {
            ImGui::BeginChild("##weapon_nested_preview", ImVec2(0, 0), true,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(s_WeaponNestedPreview[0] ? s_WeaponNestedPreview : "(nested_payloads.json unavailable)");
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::EndChild();
}

/* ========================================================================
 * INI Editor — renderer
 * ======================================================================== */

static void renderIniEditor(float contentW, float contentH, float scale)
{
    const float footerH = 36.0f * scale;
    const float listW   = contentW * 0.30f;
    const float editW   = contentW - listW - ImGui::GetStyle().ItemSpacing.x;

    /* ---- Left panel: entry list ---- */
    ImGui::BeginChild("##ini_list", ImVec2(listW, contentH - footerH), true);

    for (int i = 0; i < s_IniNumEntries; i++) {
        const IniEntry &ie = s_IniEntries[i];

        /* Category prefix (first 8 chars of id before ':' or '_') */
        char label[CATALOG_ID_LEN + 32];
        snprintf(label, sizeof(label), "%s", ie.id);

        bool sel = (s_IniSelected == i);
        if (ImGui::Selectable(label, sel)) {
            if (!s_IniDirty || s_IniSelected != i) {
                s_IniSelected = i;
                sysLogPrintf(LOG_NOTE,
                             "modhub.ini: selected idx=%d id='%s' bundled=%d",
                             i, ie.id, ie.bundled);
                iniLoadFile(i);
            }
        }
    }

    if (s_IniNumEntries == 0) {
        ImGui::TextDisabled("No mod assets with .ini files found.");
        if (!s_IniEmptyLogged) {
            sysLogPrintf(LOG_WARNING,
                         "modhub.ini: entry list is empty (no catalog assets mapped to ini types)");
            s_IniEmptyLogged = true;
        }
    } else {
        s_IniEmptyLogged = false;
    }

    ImGui::EndChild();

    ImGui::SameLine();

    /* ---- Right panel: editor ---- */
    /* Priority L (2026-04-25): NavFlattened layout panel. */
    ImGui::BeginChild("##ini_edit", ImVec2(editW, contentH - footerH),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);

    if (s_IniSelected < 0) {
        ImGui::TextDisabled("Select a mod entry from the list.");
    } else {
        const IniEntry &ie = s_IniEntries[s_IniSelected];
        /* Entry header */
        ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow());
        ImGui::Text("%s", ie.id);
        ImGui::PopStyleColor();
        ImGui::TextDisabled("[%s]", iniNameForType(ie.type));
        ImGui::Separator();

        if (s_IniNumPairs == 0) {
            ImGui::TextDisabled("(empty or not found)");
        }

        float inputW = editW - 200.0f * scale;
        if (inputW < 80.0f * scale) inputW = 80.0f * scale;

        for (int i = 0; i < s_IniNumPairs; i++) {
            IniKV &kv = s_IniPairs[i];
            if (kv.is_blank) {
                ImGui::Spacing();
                continue;
            }
            if (kv.is_comment) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.8f));
                ImGui::TextUnformatted(kv.key);
                ImGui::PopStyleColor();
                continue;
            }
            /* Key label (right-aligned in 130px column) */
            ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TextWarning(200));
            ImGui::Text("%-20s", kv.key);
            ImGui::PopStyleColor();
            ImGui::SameLine();

            char inputId[32];
            snprintf(inputId, sizeof(inputId), "##ini_v%d", i);
            ImGui::SetNextItemWidth(inputW);
            if (ImGui::InputText(inputId, kv.val, HUB_INI_VAL_LEN)) {
                bool wasDirty = s_IniDirty;
                s_IniDirty = true;
                if (!wasDirty) {
                    sysLogPrintf(LOG_NOTE,
                                 "modhub.ini: first edit in session id='%s' key='%s' value='%s'",
                                 ie.id, kv.key, kv.val);
                }
            }
        }
    }
    ImGui::EndChild();

    /* ---- Footer: status + save ---- */
    ImGui::Separator();

    if (s_IniStatusMsg[0]) {
        if (s_IniStatusOk) {
            ImGui::TextColored(pdguiVec4TintSuccess(), "%s", s_IniStatusMsg);
        } else {
            ImGui::TextColored(pdguiVec4TintDanger(), "%s", s_IniStatusMsg);
        }
    } else {
        ImGui::TextDisabled("INI Editor — edit mod manifests");
    }

    if (s_IniSelected >= 0 && !s_IniEntries[s_IniSelected].bundled) {
        ImGui::SameLine(contentW - 90.0f * scale);
        bool saveDisabled = !s_IniDirty;
        if (saveDisabled) ImGui::BeginDisabled();
        if (PdButton("Save", ImVec2(80.0f * scale, 28.0f * scale))) {
            iniSaveFile(s_IniSelected);
        }
        if (saveDisabled) ImGui::EndDisabled();
    }
}

/* ========================================================================
 * Model Scale Tool state
 * ======================================================================== */

struct ScaleEntry {
    char id[CATALOG_ID_LEN];
    char bodyfile[FS_MAXPATH];
    int  runtime_index;    /* used as bodynum hint for charpreview */
    int  bundled;
};

static ScaleEntry s_ScaleEntries[HUB_MAX_ENTRIES];
static int        s_ScaleNumEntries = 0;
static int        s_ScaleSelected   = -1;
static float      s_ScaleValue      = 1.0f;   /* current slider value */
static float      s_ScaleOriginal   = 1.0f;   /* value read from file */
static float      s_PreviewRotAngle = 0.0f;   /* accumulated rotation angle */
static char       s_ScaleStatusMsg[128] = "";
static bool       s_ScaleStatusOk  = true;

/* ========================================================================
 * Model Scale Tool — binary helpers
 * ======================================================================== */

static uint32_t byteswap32(uint32_t v)
{
    return ((v & 0xFF000000u) >> 24)
         | ((v & 0x00FF0000u) >> 8)
         | ((v & 0x0000FF00u) << 8)
         | ((v & 0x000000FFu) << 24);
}

/* n64_modeldef.scale is a big-endian IEEE 754 float at byte offset 0x10.
 * Returns 1.0f on any error. */
static float readModelScale(const char *filePath)
{
    if (!filePath || filePath[0] == '\0') return 1.0f;
    FILE *f = fopen(filePath, "rb");
    if (!f) return 1.0f;
    if (fseek(f, 0x10, SEEK_SET) != 0) { fclose(f); return 1.0f; }
    uint32_t bits = 0;
    if (fread(&bits, 4, 1, f) != 1) { fclose(f); return 1.0f; }
    fclose(f);
    bits = byteswap32(bits);
    float scale;
    memcpy(&scale, &bits, 4);
    if (scale <= 0.0f || scale != scale) return 1.0f; /* NaN / negative guard */
    return scale;
}

/* Write a new scale value (big-endian) at offset 0x10 in the model file.
 * Returns true on success. */
static bool writeModelScale(const char *filePath, float newScale)
{
    if (!filePath || filePath[0] == '\0') return false;
    FILE *f = fopen(filePath, "r+b");
    if (!f) return false;
    if (fseek(f, 0x10, SEEK_SET) != 0) { fclose(f); return false; }
    uint32_t bits;
    memcpy(&bits, &newScale, 4);
    bits = byteswap32(bits);
    bool ok = (fwrite(&bits, 4, 1, f) == 1);
    fclose(f);
    return ok;
}

static bool fileExists(const char *path)
{
    if (!path || path[0] == '\0') return false;
    struct stat st;
    return stat(path, &st) == 0;
}

/* ========================================================================
 * Model Scale Tool — populate
 * ======================================================================== */

static void scaleCollectCallback(const asset_entry_t *e, void *ud)
{
    int *n = (int *)ud;
    if (*n >= HUB_MAX_ENTRIES) return;
    if (e->ext.character.bodyfile[0] == '\0') return;
    ScaleEntry &se = s_ScaleEntries[(*n)++];
    strncpy(se.id, e->id, CATALOG_ID_LEN - 1);
    se.id[CATALOG_ID_LEN - 1] = '\0';
    strncpy(se.bodyfile, e->ext.character.bodyfile, FS_MAXPATH - 1);
    se.bodyfile[FS_MAXPATH - 1] = '\0';
    se.runtime_index = e->runtime_index;
    se.bundled       = e->bundled;
}

static void scaleCollectBodyCallback(const asset_entry_t *e, void *ud)
{
    int *n = (int *)ud;
    if (*n >= HUB_MAX_ENTRIES) return;
    if (!e || e->type != ASSET_BODY) return;
    if (e->source_filenum < 0) return;
    CatalogResolveResult r = catalogResolveFile(e->source_filenum);
    if (!r.path || !r.path[0]) return;
    ScaleEntry &se = s_ScaleEntries[(*n)++];
    strncpy(se.id, e->id, CATALOG_ID_LEN - 1);
    se.id[CATALOG_ID_LEN - 1] = '\0';
    strncpy(se.bodyfile, r.path, FS_MAXPATH - 1);
    se.bodyfile[FS_MAXPATH - 1] = '\0';
    se.runtime_index = e->runtime_index;
    se.bundled       = e->bundled;
}

static void scaleRefreshEntries(void)
{
    s_ScaleNumEntries = 0;
    /* Modding Hub Scale tool -- modder needs to see disabled entries to
     * adjust scale on them.  See B-303 (catalog universality sweep). */
    assetCatalogIterateByTypeIncludingDisabled(ASSET_CHARACTER,
                                                scaleCollectCallback,
                                                &s_ScaleNumEntries);
    assetCatalogIterateByTypeIncludingDisabled(ASSET_BODY,
                                                scaleCollectBodyCallback,
                                                &s_ScaleNumEntries);
    s_ScaleSelected   = -1;
    s_ScaleValue      = 1.0f;
    s_ScaleOriginal   = 1.0f;
    s_ScaleStatusMsg[0] = '\0';
}

static void scaleSelectEntry(int idx)
{
    s_ScaleSelected = idx;
    s_PreviewRotAngle = 0.0f;
    if (idx < 0 || idx >= s_ScaleNumEntries) return;
    const ScaleEntry &se = s_ScaleEntries[idx];
    s_ScaleOriginal = readModelScale(se.bodyfile);
    s_ScaleValue    = s_ScaleOriginal;
    snprintf(s_ScaleStatusMsg, sizeof(s_ScaleStatusMsg),
             "Loaded — file scale: %.4f", s_ScaleOriginal);
    s_ScaleStatusOk = true;
}

/* ========================================================================
 * Model Scale Tool — renderer
 * ======================================================================== */

static void renderScaleTool(float contentW, float contentH, float scale)
{
    const float footerH  = 36.0f * scale;
    const float listW    = contentW * 0.30f;
    const float rightW   = contentW - listW - ImGui::GetStyle().ItemSpacing.x;

    /* ---- Left panel: character list ---- */
    ImGui::BeginChild("##scale_list", ImVec2(listW, contentH - footerH), true);

    for (int i = 0; i < s_ScaleNumEntries; i++) {
        bool sel = (s_ScaleSelected == i);
        if (ImGui::Selectable(s_ScaleEntries[i].id, sel)) {
            scaleSelectEntry(i);
        }
    }
    if (s_ScaleNumEntries == 0) {
        ImGui::TextDisabled("No character or body model files found (need loose file path).");
    }

    ImGui::EndChild();
    ImGui::SameLine();

    /* ---- Right panel: preview + controls ----
     * M-15 (C2 preview-dock invariant): this panel contains the rotating
     * character preview (ImGui::Image) plus the scale slider / bake controls.
     * The preview must not scroll with the controls — if the right column
     * overflows on short viewports, scrolling would push the preview out of
     * view. Force NoScrollbar | NoScrollWithMouse so this panel stays a
     * fixed-layout sibling to the character list; the control stack below
     * the preview is short enough to fit without scroll (scale slider +
     * Bake + warning lines fit at every supported pdgui scale). */
    /* Priority L (2026-04-25): NavFlattened layout panel. */
    ImGui::BeginChild("##scale_right", ImVec2(rightW, contentH - footerH),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse);

    if (s_ScaleSelected < 0) {
        ImGui::TextDisabled("Select a character from the list.");
    } else {
        const ScaleEntry &se = s_ScaleEntries[s_ScaleSelected];

        /* Rotate preview each frame */
        s_PreviewRotAngle += ImGui::GetIO().DeltaTime * 0.8f;

        /* Character preview (top portion of right panel, centered) */
        s32 prevW = 0, prevH = 0;
        pdguiCharPreviewGetSize(&prevW, &prevH);

        float dispSize = 160.0f * scale;
        if (dispSize > rightW * 0.5f) dispSize = rightW * 0.5f;

        /* Request preview render using catalog ID */
        pdguiCharPreviewSetRotY(s_PreviewRotAngle);
        pdguiCharPreviewRequest("", se.id);

        /* Show preview or placeholder */
        u32 texId = pdguiCharPreviewGetTextureId();
        if (texId && pdguiCharPreviewIsReady()) {
            float cursorX = ImGui::GetCursorPosX() + (rightW - dispSize) * 0.5f;
            ImGui::SetCursorPosX(cursorX > 0 ? cursorX : 0);
            ImGui::Image((ImTextureID)(uintptr_t)texId, ImVec2(dispSize, dispSize),
                         ImVec2(0,1), ImVec2(1,0));  /* flip Y for GL convention */
        } else {
            float cursorX = ImGui::GetCursorPosX() + (rightW - dispSize) * 0.5f;
            ImGui::SetCursorPosX(cursorX > 0 ? cursorX : 0);
            ImGui::Dummy(ImVec2(dispSize, dispSize));
            ImGui::SameLine();
            ImGui::TextDisabled("[preview loading...]");
        }

        ImGui::Separator();

        /* Entry info */
        ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow());
        ImGui::Text("%s", se.id);
        ImGui::PopStyleColor();

        /* Body file path (truncated) */
        const char *shortPath = se.bodyfile;
        /* Show last 40 chars of path if long */
        int pathLen = (int)strlen(shortPath);
        if (pathLen > 50) shortPath = shortPath + pathLen - 50;
        ImGui::TextDisabled("...%s", shortPath);

        ImGui::Spacing();

        /* Scale info row */
        ImGui::Text("File scale: %.4f", s_ScaleOriginal);
        ImGui::SameLine();
        ImGui::TextDisabled("(at 0x10 in binary)");

        ImGui::Spacing();

        /* Scale slider */
        ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TextWarning(220));
        ImGui::Text("New scale:");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(rightW - 200.0f * scale);
        ImGui::SliderFloat("##scale_slider", &s_ScaleValue, 0.1f, 5.0f, "%.4f");
        ImGui::SameLine();
        if (ImGui::Button("Reset##scalerst")) {
            s_ScaleValue = s_ScaleOriginal;
        }

        ImGui::Spacing();

        /* Bake button */
        bool canBake = !se.bundled && fileExists(se.bodyfile)
                       && (s_ScaleValue != s_ScaleOriginal);
        if (!canBake) ImGui::BeginDisabled();
        if (PdButton("Bake Scale to File", ImVec2(180.0f * scale, 30.0f * scale))) {
            if (writeModelScale(se.bodyfile, s_ScaleValue)) {
                s_ScaleOriginal = s_ScaleValue;
                snprintf(s_ScaleStatusMsg, sizeof(s_ScaleStatusMsg),
                         "Baked scale %.4f to file.", s_ScaleValue);
                s_ScaleStatusOk = true;
            } else {
                snprintf(s_ScaleStatusMsg, sizeof(s_ScaleStatusMsg),
                         "Bake failed — file may be read-only.");
                s_ScaleStatusOk = false;
            }
        }
        if (!canBake) ImGui::EndDisabled();

        if (se.bundled) {
            ImGui::SameLine();
            ImGui::TextDisabled("(base game — read-only)");
        } else if (!fileExists(se.bodyfile)) {
            ImGui::SameLine();
            ImGui::TextDisabled("(file not found)");
        }

        /* Warning note */
        ImGui::Spacing();
        ImGui::TextDisabled("Bake modifies the model binary on disk.");
        ImGui::TextDisabled("Restart required for changes to take effect.");
    }

    ImGui::EndChild();

    /* ---- Footer: status ---- */
    ImGui::Separator();
    if (s_ScaleStatusMsg[0]) {
        if (s_ScaleStatusOk) {
            ImGui::TextColored(pdguiVec4TintSuccess(), "%s", s_ScaleStatusMsg);
        } else {
            ImGui::TextColored(pdguiVec4TintDanger(), "%s", s_ScaleStatusMsg);
        }
    } else {
        ImGui::TextDisabled("Model Scale Tool — bake scale into model binary");
    }
}

/* ========================================================================
 * Mod Pack Tool — state
 * ======================================================================== */

struct PackEntry {
    char         id[CATALOG_ID_LEN];
    char         category[CATALOG_CATEGORY_LEN];
    asset_type_e type;
};

static PackEntry s_PackEntries[HUB_MAX_ENTRIES];
static int       s_PackNumEntries    = 0;
static bool      s_PackSelected[HUB_MAX_ENTRIES];

/* Export fields */
static char      s_PackName[128]      = "";
static char      s_PackAuthor[64]     = "";
static char      s_PackVersion[32]    = "1.0.0";
static char      s_PackOutputPath[FS_MAXPATH] = "";

/* Import fields */
static char      s_ImportPath[FS_MAXPATH] = "";
static bool      s_ImportSessionOnly      = false;
static bool      s_ImportManifestLoaded   = false;
static modpack_manifest_t s_ImportManifest;

/* Shared status line */
static char      s_PackStatusMsg[256]  = "";
static bool      s_PackStatusOk        = true;

/* ========================================================================
 * Mod Pack Tool — collect non-bundled entries from catalog
 * ======================================================================== */

static void packCollectCallback(const asset_entry_t *e, void *ud)
{
    int *n = (int *)ud;
    if (*n >= HUB_MAX_ENTRIES) return;
    if (e->bundled) return;   /* skip base-game entries */
    PackEntry &pe = s_PackEntries[*n];
    strncpy(pe.id,       e->id,       CATALOG_ID_LEN - 1);
    strncpy(pe.category, e->category, CATALOG_CATEGORY_LEN - 1);
    pe.id[CATALOG_ID_LEN - 1]             = '\0';
    pe.category[CATALOG_CATEGORY_LEN - 1] = '\0';
    pe.type = e->type;
    (*n)++;
}

static void packRefreshEntries(void)
{
    s_PackNumEntries = 0;
    for (int t = 0; t < s_NumAllTypes; t++) {
        /* Modding Hub Pack tool -- modder needs to see disabled mod
         * entries to bundle them.  See B-303 (catalog universality
         * sweep). */
        assetCatalogIterateByTypeIncludingDisabled(s_AllTypes[t],
                                                    packCollectCallback,
                                                    &s_PackNumEntries);
    }
    memset(s_PackSelected, 0, sizeof(s_PackSelected));
    s_PackStatusMsg[0]       = '\0';
    s_ImportManifestLoaded   = false;
    memset(&s_ImportManifest, 0, sizeof(s_ImportManifest));
}

/* ========================================================================
 * Mod Pack Tool — helpers
 * ======================================================================== */

static const char *packTypeShortName(asset_type_e t)
{
    switch (t) {
        case ASSET_MAP:          return "Map";
        case ASSET_CHARACTER:    return "Character";
        case ASSET_SKIN:         return "Skin";
        case ASSET_BOT_VARIANT:  return "Bot";
        case ASSET_WEAPON:       return "Weapon";
        case ASSET_PROJECTILE:   return "Projectile";
        case ASSET_ENTITY:       return "Entity";
        case ASSET_TEXTURES:     return "Textures";
        case ASSET_SFX:          return "SFX";
        case ASSET_MUSIC:        return "Music";
        case ASSET_PROP:         return "Prop";
        case ASSET_VEHICLE:      return "Vehicle";
        case ASSET_MISSION:      return "Mission";
        case ASSET_UI:           return "UI";
        case ASSET_TOOL:         return "Tool";
        default:                 return "Other";
    }
}

/* ========================================================================
 * Mod Pack Tool — renderer
 * ======================================================================== */

static void renderPackTool(float contentW, float contentH, float scale)
{
    /* Split content: ~58% export, ~42% import */
    float exportH = contentH * 0.58f;
    float importH = contentH - exportH
                    - ImGui::GetStyle().ItemSpacing.y * 2.0f
                    - ImGui::GetStyle().SeparatorTextBorderSize * 2.0f;

    /* ================================================================
     * EXPORT PANEL
     * ============================================================== */
    ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TextWarning());
    ImGui::TextUnformatted("EXPORT");
    ImGui::PopStyleColor();
    ImGui::Separator();

    /* Pack metadata row: Name / Author / Version */
    {
        float fieldW = (contentW - ImGui::GetStyle().ItemSpacing.x * 4.0f) / 3.0f
                       - 50.0f * scale;
        ImGui::SetNextItemWidth(fieldW);
        ImGui::InputText("##pkname",   s_PackName,    sizeof(s_PackName));
        ImGui::SameLine(); ImGui::TextDisabled("Name");
        ImGui::SameLine(contentW / 3.0f + 8.0f * scale);
        ImGui::SetNextItemWidth(fieldW);
        ImGui::InputText("##pkauthor", s_PackAuthor,  sizeof(s_PackAuthor));
        ImGui::SameLine(); ImGui::TextDisabled("Author");
        ImGui::SameLine(contentW * 2.0f / 3.0f + 8.0f * scale);
        ImGui::SetNextItemWidth(fieldW);
        ImGui::InputText("##pkver",    s_PackVersion, sizeof(s_PackVersion));
        ImGui::SameLine(); ImGui::TextDisabled("Ver");
    }

    /* Output path row */
    ImGui::SetNextItemWidth(contentW - 80.0f * scale);
    ImGui::InputText("##pkout", s_PackOutputPath, sizeof(s_PackOutputPath));
    ImGui::SameLine(); ImGui::TextDisabled("Output");

    /* Select All / Clear / count */
    int selectedCount = 0;
    for (int i = 0; i < s_PackNumEntries; i++) {
        if (s_PackSelected[i]) selectedCount++;
    }
    if (PdButton("All", ImVec2(42.0f * scale, 22.0f * scale))) {
        for (int i = 0; i < s_PackNumEntries; i++) s_PackSelected[i] = true;
    }
    ImGui::SameLine();
    if (PdButton("None", ImVec2(48.0f * scale, 22.0f * scale))) {
        memset(s_PackSelected, 0, sizeof(s_PackSelected));
    }
    ImGui::SameLine();
    ImGui::TextDisabled("  %d / %d selected", selectedCount, s_PackNumEntries);

    /* Component list — scrollable */
    {
        float headerH = ImGui::GetCursorPosY();      /* current cursor inside child */
        float btnH    = 28.0f * scale;
        float listH   = exportH - headerH - btnH
                        - ImGui::GetStyle().ItemSpacing.y * 3.0f;
        if (listH < 48.0f * scale) listH = 48.0f * scale;

        ImGui::BeginChild("##pk_list", ImVec2(contentW, listH), true,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);

        if (s_PackNumEntries == 0) {
            ImGui::TextDisabled("No mod components installed (nothing to export).");
        } else {
            for (int i = 0; i < s_PackNumEntries; i++) {
                char chkId[32];
                snprintf(chkId, sizeof(chkId), "##pksel%d", i);
                ImGui::Checkbox(chkId, &s_PackSelected[i]);
                ImGui::SameLine(32.0f * scale);
                ImGui::TextUnformatted(s_PackEntries[i].id);
                ImGui::SameLine(contentW * 0.48f);
                ImGui::TextDisabled("%s", packTypeShortName(s_PackEntries[i].type));
                ImGui::SameLine(contentW * 0.62f);
                ImGui::TextDisabled("%s", s_PackEntries[i].category);
            }
        }
        ImGui::EndChild();
    }

    /* Export button — right-aligned, disabled when nothing selected or no path */
    {
        bool canExport = (selectedCount > 0)
                         && (s_PackOutputPath[0] != '\0')
                         && (s_PackName[0] != '\0');
        float btnW = 120.0f * scale;
        float btnH = 26.0f * scale;
        ImGui::SetCursorPosX(contentW - btnW);

        if (!canExport) ImGui::BeginDisabled();
        if (PdButton("Export Pack", ImVec2(btnW, btnH))) {
            /* Build ID array from selection */
            const char *exportIds[HUB_MAX_ENTRIES];
            int exportCount = 0;
            for (int j = 0; j < s_PackNumEntries; j++) {
                if (s_PackSelected[j])
                    exportIds[exportCount++] = s_PackEntries[j].id;
            }
            char errBuf[MODPACK_ERROR_LEN] = "";
            s32 ret = modpackExport(
                (const char * const *)exportIds, exportCount,
                s_PackName, s_PackAuthor, s_PackVersion,
                s_PackOutputPath, errBuf, sizeof(errBuf));
            if (ret == 0) {
                snprintf(s_PackStatusMsg, sizeof(s_PackStatusMsg),
                         "Exported %d component(s) to %s",
                         exportCount, s_PackOutputPath);
                s_PackStatusOk = true;
            } else {
                snprintf(s_PackStatusMsg, sizeof(s_PackStatusMsg),
                         "Export failed: %s",
                         errBuf[0] ? errBuf : "unknown error");
                s_PackStatusOk = false;
            }
        }
        if (!canExport) ImGui::EndDisabled();
    }

    /* ================================================================
     * IMPORT PANEL
     * ============================================================== */
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TextWarning());
    ImGui::TextUnformatted("IMPORT");
    ImGui::PopStyleColor();
    ImGui::Separator();

    /* File path + Preview button */
    {
        float prevW = 80.0f * scale;
        ImGui::SetNextItemWidth(contentW - prevW
                                - ImGui::GetStyle().ItemSpacing.x * 2.0f);
        ImGui::InputText("##imppath", s_ImportPath, sizeof(s_ImportPath));
        ImGui::SameLine();
        if (PdButton("Preview", ImVec2(prevW, 0.0f))) {
            memset(&s_ImportManifest, 0, sizeof(s_ImportManifest));
            s_ImportManifestLoaded =
                modpackReadManifest(s_ImportPath, &s_ImportManifest) != 0;
            if (s_ImportManifestLoaded) {
                snprintf(s_PackStatusMsg, sizeof(s_PackStatusMsg),
                         "Pack: \"%s\" by %s — %d component(s)",
                         s_ImportManifest.name,
                         s_ImportManifest.author,
                         s_ImportManifest.component_count);
                s_PackStatusOk = true;
            } else {
                snprintf(s_PackStatusMsg, sizeof(s_PackStatusMsg),
                         "Cannot read .pdpack — check path and file format");
                s_PackStatusOk = false;
            }
        }
    }

    /* Manifest preview (shown after Preview) */
    if (s_ImportManifestLoaded) {
        /* Calculate height for preview area */
        float previewH = importH
                         - 28.0f * scale    /* session-only checkbox + import btn */
                         - ImGui::GetStyle().ItemSpacing.y * 3.0f;
        if (previewH < 40.0f * scale) previewH = 40.0f * scale;

        ImGui::BeginChild("##pk_mf", ImVec2(contentW, previewH), true,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);

        ImGui::TextDisabled("Pack:    "); ImGui::SameLine();
        ImGui::TextUnformatted(s_ImportManifest.name);
        ImGui::TextDisabled("Author:  "); ImGui::SameLine();
        ImGui::TextUnformatted(s_ImportManifest.author);
        ImGui::TextDisabled("Version: "); ImGui::SameLine();
        ImGui::TextUnformatted(s_ImportManifest.version);
        ImGui::Separator();

        for (int i = 0; i < s_ImportManifest.component_count; i++) {
            const modpack_component_info_t &ci = s_ImportManifest.components[i];
            s32 already = assetCatalogHasEntry(ci.id);
            if (already) {
                ImGui::TextColored(pdguiVec4TextWarning(220),
                                   "[installed]");
            } else {
                ImGui::TextColored(pdguiVec4TintSuccess(),
                                   "[new]      ");
            }
            ImGui::SameLine();
            ImGui::Text("%-36s  %s", ci.id, ci.category);
        }

        ImGui::EndChild();
    } else {
        ImGui::TextDisabled("Enter a .pdpack path and click Preview to inspect.");
    }

    /* Session-only checkbox + Import button on same row */
    {
        /* Priority L (2026-04-25): label LEFT via pdguiCheckbox. */
        pdguiCheckbox("Session Only (mods/.temp/)", &s_ImportSessionOnly);
        float btnW = 112.0f * scale;
        ImGui::SameLine(contentW - btnW);

        bool canImport = s_ImportManifestLoaded && s_ImportPath[0] != '\0';
        if (!canImport) ImGui::BeginDisabled();
        if (PdButton("Import Pack", ImVec2(btnW, 26.0f * scale))) {
            modpack_import_result_t result;
            s32 imported = modpackImport(s_ImportPath,
                                         s_ImportSessionOnly ? 1 : 0,
                                         &result);
            if (imported >= 0) {
                /* Hot-refresh chrome style registry so newly imported chrome
                 * mods appear in Settings -> Video without restart. */
                pdguiThemeRescanChromeStyles();
                snprintf(s_PackStatusMsg, sizeof(s_PackStatusMsg),
                         "Imported %d component(s). Use Apply Changes to reload.",
                         imported);
                s_PackStatusOk        = true;
                s_ImportManifestLoaded = false;
                memset(&s_ImportManifest, 0, sizeof(s_ImportManifest));
            } else {
                snprintf(s_PackStatusMsg, sizeof(s_PackStatusMsg),
                         "Import failed: %s",
                         result.error_msg[0] ? result.error_msg : "unknown error");
                s_PackStatusOk = false;
            }
        }
        if (!canImport) ImGui::EndDisabled();
    }

    /* ================================================================
     * Status line
     * ============================================================== */
    ImGui::Separator();
    if (s_PackStatusMsg[0]) {
        if (s_PackStatusOk) {
            ImGui::TextColored(pdguiVec4TintSuccess(),
                               "%s", s_PackStatusMsg);
        } else {
            ImGui::TextColored(pdguiVec4TintDanger(),
                               "%s", s_PackStatusMsg);
        }
    } else {
        ImGui::TextDisabled("Mod Pack -- export/import .pdpack files");
    }

    /* ================================================================
     * PACK .pdmod FROM FOLDER (c3808/c3809-s7)
     *
     * Packs the external authoring layout: root mod.json plus standard
     * files and grouped INI/TSV metadata. The helper validates canonical
     * asset folders, generates missing commented INI templates, and refuses
     * authored .bin payloads before writing the archive.
     * ============================================================== */
    static char s_PdmodSrcFolder[FS_MAXPATH] = "mods/staging/";
    static char s_PdmodOutPath[FS_MAXPATH]   = "mods/packed.pdmod";
    static char s_PdmodStatusMsg[256]        = "";
    static bool s_PdmodStatusOk              = true;

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TextWarning());
    ImGui::TextUnformatted("PACK .pdmod FROM FOLDER");
    ImGui::PopStyleColor();
    ImGui::Separator();

    {
        float lblW = 80.0f * scale;
        ImGui::SetNextItemWidth(contentW - lblW);
        ImGui::InputText("##pdmodsrc", s_PdmodSrcFolder, sizeof(s_PdmodSrcFolder));
        ImGui::SameLine();
        ImGui::TextDisabled("Folder");

        ImGui::SetNextItemWidth(contentW - lblW);
        ImGui::InputText("##pdmodout", s_PdmodOutPath, sizeof(s_PdmodOutPath));
        ImGui::SameLine();
        ImGui::TextDisabled("Output");
    }

    ImGui::TextDisabled("Typed .pdxxx samples: examples/modding/typed-pdxxx-basic/");
    ImGui::SameLine();
    if (PdButton("Use Sample Folder", ImVec2(150.0f * scale, 0.0f))) {
        snprintf(s_PdmodSrcFolder, sizeof(s_PdmodSrcFolder),
                 "examples/modding/typed-pdxxx-basic/");
        snprintf(s_PdmodOutPath, sizeof(s_PdmodOutPath),
                 "mods/typed-pdxxx-basic.pdmod");
        snprintf(s_PdmodStatusMsg, sizeof(s_PdmodStatusMsg),
                 "Sample selected. Edit the .pdxxx archives first; pack .pdmod only for transport.");
        s_PdmodStatusOk = true;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentW);
    ImGui::TextWrapped(".pdmod output is for sharing, Public Mods, or online delivery; the editable content is inside the typed .pdxxx asset archives.");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();

    {
        bool canPack = (s_PdmodSrcFolder[0] != '\0') && (s_PdmodOutPath[0] != '\0');
        float btnW = 140.0f * scale;
        float btnH = 26.0f * scale;
        ImGui::SetCursorPosX(contentW - btnW);

        if (!canPack) ImGui::BeginDisabled();
        if (PdButton("Pack .pdmod", ImVec2(btnW, btnH))) {
            s32 ret = modpackPdmodFromFolder(s_PdmodSrcFolder, s_PdmodOutPath);
            if (ret == MODPACK_PDMOD_OK) {
                snprintf(s_PdmodStatusMsg, sizeof(s_PdmodStatusMsg),
                         "Packed %s -> %s",
                         s_PdmodSrcFolder, s_PdmodOutPath);
                s_PdmodStatusOk = true;
                sysLogPrintf(LOG_NOTE,
                             "MODPACK.PDMOD: packed %s -> %s",
                             s_PdmodSrcFolder, s_PdmodOutPath);
            } else {
                const char *reason = "unknown error";
                switch (ret) {
                    case MODPACK_PDMOD_ERR_OPEN:    reason = "could not open destination"; break;
                    case MODPACK_PDMOD_ERR_NO_MFST: reason = "mod.json not found in folder"; break;
                    case MODPACK_PDMOD_ERR_BAD_MFST:reason = "mod.json failed to parse"; break;
                    case MODPACK_PDMOD_ERR_IO:      reason = "read/write failure"; break;
                    case MODPACK_PDMOD_ERR_TOO_BIG: reason = "source exceeds 4 GiB"; break;
                    case MODPACK_PDMOD_ERR_LAYOUT:  reason = "external layout validation failed"; break;
                    case MODPACK_PDMOD_ERR_TEMPLATE:reason = "could not generate INI template"; break;
                    default: break;
                }
                const char *detail = modpackPdmodLastError();
                if (detail && detail[0]) {
                    snprintf(s_PdmodStatusMsg, sizeof(s_PdmodStatusMsg),
                             "Pack failed: %s", detail);
                } else {
                    snprintf(s_PdmodStatusMsg, sizeof(s_PdmodStatusMsg),
                             "Pack failed: %s (rc=%d)", reason, (int)ret);
                }
                s_PdmodStatusOk = false;
                sysLogPrintf(LOG_WARNING,
                             "MODPACK.PDMOD: failed folder=%s output=%s rc=%d reason=%s detail=%s",
                             s_PdmodSrcFolder, s_PdmodOutPath, (int)ret,
                             reason, (detail && detail[0]) ? detail : "");
            }
        }
        if (!canPack) ImGui::EndDisabled();
    }

    if (s_PdmodStatusMsg[0]) {
        if (s_PdmodStatusOk) {
            ImGui::TextColored(pdguiVec4TintSuccess(), "%s", s_PdmodStatusMsg);
        } else {
            ImGui::TextColored(pdguiVec4TintDanger(),  "%s", s_PdmodStatusMsg);
        }
    } else {
        ImGui::TextDisabled("Pack an external-layout folder mod (mod.json + standard files + INI/TSV, no .bin) into a .pdmod archive.");
    }
}

/* ========================================================================
 * Hub renderer
 * ======================================================================== */

static void renderModdingHub(s32 winW, s32 winH)
{
    float scale = pdguiScaleFactor();

    /* Full-screen transparent backing window */
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)winW, (float)winH));

    ImGuiWindowFlags wflags =
        ImGuiWindowFlags_NoResize     |
        ImGuiWindowFlags_NoMove       |
        ImGuiWindowFlags_NoCollapse   |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoTitleBar   |
        ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##modhub", nullptr, wflags)) {
        ImGui::End();
        return;
    }

    /* C-8: focus on appear so controller nav reaches the tool selector.
     * B-210: clear the A/Enter/Space edges that opened the hub so they are
     * not read as hub Close / tool activation on the first frame (same
     * class as B-131 main-menu debounce). */
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        ImGuiIO &nio = ImGui::GetIO();
        nio.AddKeyEvent(ImGuiKey_GamepadFaceDown, false);
        nio.AddKeyEvent(ImGuiKey_Enter, false);
        nio.AddKeyEvent(ImGuiKey_Space, false);
        s_ModHubOpenFrame = ImGui::GetFrameCount();
    }

    /* Viewport-relative dialog area — ultrawide-clamped via pdguiMenuWidth() */
    float dialogW = pdguiMenuWidth();
    float dialogH = pdguiMenuHeight();
    ImVec2 menuPos = pdguiMenuPos();
    float dialogX = menuPos.x;
    float dialogY = menuPos.y;

    /* PD-style border */
    ImDrawList *drawList = ImGui::GetForegroundDrawList();
    ImU32 borderCol = pdguiImU32TintInfo(220);
    drawList->AddRect(ImVec2(dialogX, dialogY),
                      ImVec2(dialogX + dialogW, dialogY + dialogH),
                      borderCol, 4.0f, 0, 2.0f * scale);

    /* Centered child window */
    ImGui::SetNextWindowPos(ImVec2(dialogX, dialogY));
    ImGui::SetNextWindowSize(ImVec2(dialogW, dialogH));

    ImGuiWindowFlags innerFlags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoTitleBar;

    /* Priority L (2026-04-25): NavFlattened outer dialog body. */
    if (!ImGui::BeginChild("##modhub_inner", ImVec2(dialogW, dialogH),
                           ImGuiChildFlags_NavFlattened, innerFlags)) {
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    /* ---- Hub header ---- */
    pdguiSetCursorBelowTitle(0.0f); /* content-inset: protect left/top from chrome border */
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::Text("MODDING");
    ImGui::PopStyleColor();
    ImGui::Separator();

    /* ---- Tool selector bar ---- */
    {
        const float btnW = 120.0f * scale;
        const float btnH = 28.0f * scale;
        static const int NUM_TOOLS = 10;

        static const char *toolNames[] = {
            "Mod Manager", "INI Editor", "Scale Tool", "Mod Pack",
            "Audio Mods", "Skin Editor", "Map Import", "Menu Style",
            "Font Mod", "Weapons"
        };

        /* ACTION_MENU_TAB_PREV/NEXT cycle hub tools.
         * Skin Editor uses list navigation heavily; suppress global tab cycling there
         * to avoid stealing selection input from the character list/editor UI. */
        const bool allowHubTabCycle = (s_ActiveTool != 5);
        if (allowHubTabCycle && pdguiMenuTabPrevPressed()) {
            int next = (s_ActiveTool - 1 + NUM_TOOLS) % NUM_TOOLS;
            s_ActiveTool = next;
            if (next == 0) pdguiModManagerRefreshSnapshot();
            else if (next == 1) iniRefreshEntries();
            else if (next == 2) scaleRefreshEntries();
            else if (next == 3) packRefreshEntries();
            else if (next == 4) pdguiAudioModRefresh();
            else if (next == 5) pdguiSkinEditorRefresh();
            else if (next == 6) importReset();
                    else if (next == 7) chromeToolReset();
                    else if (next == 8) fontToolReset();
                    else if (next == 9) weaponToolRefresh();
            pdguiPlaySound(PDGUI_SND_SWIPE);
        }
        if (allowHubTabCycle && pdguiMenuTabNextPressed()) {
            int next = (s_ActiveTool + 1) % NUM_TOOLS;
            s_ActiveTool = next;
            if (next == 0) pdguiModManagerRefreshSnapshot();
            else if (next == 1) iniRefreshEntries();
            else if (next == 2) scaleRefreshEntries();
            else if (next == 3) packRefreshEntries();
            else if (next == 4) pdguiAudioModRefresh();
            else if (next == 5) pdguiSkinEditorRefresh();
                    else if (next == 6) importReset();
                    else if (next == 7) chromeToolReset();
                    else if (next == 8) fontToolReset();
                    else if (next == 9) weaponToolRefresh();
            pdguiPlaySound(PDGUI_SND_SWIPE);
        }

        for (int i = 0; i < NUM_TOOLS; i++) {
            if (i > 0) ImGui::SameLine();

            bool active = (s_ActiveTool == i);
            if (active) {
                /* S306: tool-selector active-state colors follow the theme's
                 * toolbar tint so themed builds can redecorate the Modding
                 * Hub. pdguiGetToolbarTint() returns 0xRRGGBBAA; derives a
                 * default tied to the theme's primary accent when the
                 * theme doesn't opt in via theme.json "toolbarTint". */
                u32 tintRgba = pdguiGetToolbarTint();
                ImVec4 base = ImVec4(
                    ((tintRgba >> 24) & 0xFF) / 255.0f,
                    ((tintRgba >> 16) & 0xFF) / 255.0f,
                    ((tintRgba >>  8) & 0xFF) / 255.0f,
                    ((tintRgba >>  0) & 0xFF) / 255.0f);
                /* Hover = lerp toward white by 20% so the press affordance
                 * reads even when the tint is dark. */
                ImVec4 hover = ImVec4(
                    base.x + (1.0f - base.x) * 0.20f,
                    base.y + (1.0f - base.y) * 0.20f,
                    base.z + (1.0f - base.z) * 0.20f,
                    base.w);
                ImGui::PushStyleColor(ImGuiCol_Button, base);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
            }

            if (PdButton(toolNames[i], ImVec2(btnW, btnH))) {
                if (s_ActiveTool != i) {
                    s_ActiveTool = i;
                    /* Refresh tool data on switch */
                    if (i == 0) pdguiModManagerRefreshSnapshot();
                    else if (i == 1) iniRefreshEntries();
                    else if (i == 2) scaleRefreshEntries();
                    else if (i == 3) packRefreshEntries();
                    else if (i == 4) pdguiAudioModRefresh();
                    else if (i == 5) pdguiSkinEditorRefresh();
                    else if (i == 6) importReset();
                    else if (i == 7) chromeToolReset();
                    else if (i == 8) fontToolReset();
                    else if (i == 9) weaponToolRefresh();
                }
            }
            if (active) ImGui::PopStyleColor(2);
        }
    }

    ImGui::Separator();

    /* ---- Content area ---- */
    /* Hub footer: action bar (C1) — reserve its height + a one-line tool
     * description row above it. */
    const float hubDescH  = 22.0f * scale;
    const float hubBarH   = pdguiActionBarHeight();
    const float hubFooterH = hubDescH + hubBarH + 12.0f * scale;

    float contentY  = ImGui::GetCursorPosY();
    float contentH  = dialogH - contentY - hubFooterH
                    - ImGui::GetStyle().ItemSpacing.y * 2.0f;

    /* Each tool renders in its own clipping child to prevent overlap with
     * the tab bar above and footer below (same fix as Settings Z-order). */
    {
        ImGuiWindowFlags cfFlags =
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground;

        const char *childIds[] = {
            "##modhub_modmgr", "##modhub_ini", "##modhub_scale",
            "##modhub_pack", "##modhub_audio", "##modhub_skin",
            "##modhub_import", "##modhub_nineslice", "##modhub_fontmod",
            "##modhub_weapons"
        };

        /* Priority L (2026-04-25): NavFlattened tool body. */
        if (ImGui::BeginChild(childIds[s_ActiveTool],
                              ImVec2(dialogW, contentH),
                              ImGuiChildFlags_NavFlattened, cfFlags)) {
            if (s_ActiveTool == 0) {
                s32 wantsClose = 0;
                pdguiModManagerRenderContent(dialogW, contentH, scale, &wantsClose);
                if (wantsClose) moddingHubClose("mod-manager-request");
            } else if (s_ActiveTool == 1) {
                renderIniEditor(dialogW, contentH, scale);
            } else if (s_ActiveTool == 2) {
                renderScaleTool(dialogW, contentH, scale);
            } else if (s_ActiveTool == 3) {
                renderPackTool(dialogW, contentH, scale);
            } else if (s_ActiveTool == 4) {
                pdguiAudioModRender(dialogW, contentH, scale);
            } else if (s_ActiveTool == 5) {
                pdguiSkinEditorRender(dialogW, contentH, scale);
            } else if (s_ActiveTool == 6) {
                renderMapImport(dialogW, contentH, scale);
            } else if (s_ActiveTool == 7) {
                renderChromeTool(dialogW, contentH, scale);
            } else if (s_ActiveTool == 8) {
                renderFontTool(dialogW, contentH, scale);
            } else if (s_ActiveTool == 9) {
                renderWeaponTool(dialogW, contentH, scale);
            }
        }
        ImGui::EndChild();
    }

    /* ---- Hub footer: tool description row + docked action bar (C1) ---- */
    const char *toolDescs[] = {
        "Enable/disable mod components",
        "Edit mod .ini manifests",
        "Bake model scale to file",
        "Export/import .pdpack files",
        "Browse, audition, and import audio mods",
        "Paint custom character skins",
        "Import PD-format map files as playable arenas",
        "Create UI chrome nine-slice mods in-game",
        "Import a .ttf/.otf font as a mod",
        "Browse weapon archives and graph payloads"
    };
    ImGui::TextDisabled("%s", toolDescs[s_ActiveTool]);

    if (pdguiBeginActionBar("##modhub_ab")) {
        if (pdguiActionBarButton("Close", 1, ImGui::GetContentRegionAvail().x)) {
            moddingHubCloseFromUi("close-button");
        }
    }
    pdguiEndActionBar();

    /* Back input mirrors footer Close behavior.  S311: title X button
     * also closes via pdguiConsumeTitleClose (first-click reliability). */
    if (!s_WeaponTemplateMenuOpen
            && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        if (pdguiConsumeTitleClose()) {
            moddingHubCloseFromUi("title-x-button");
        } else if (pdguiMenuCancelPressed()) {
            if (s_ModHubOpenFrame < 0
                || (ImGui::GetFrameCount() - s_ModHubOpenFrame) >= 5) {
                /* B-213: Escape leaves paint session first; second Escape closes hub. */
                if (!(s_ActiveTool == 5 && pdguiSkinEditorTryConsumeHubEscape())) {
                    moddingHubCloseFromUi("escape-or-b-button");
                }
            }
        }
    }

    ImGui::EndChild();
    ImGui::End();

    weaponRenderTemplateWindow(winW, winH, scale);

    if (s_LastLoggedTool != s_ActiveTool) {
        sysLogPrintf(LOG_NOTE, "modhub: active tool -> %d (%s)",
                     s_ActiveTool, hubToolName(s_ActiveTool));
        s_LastLoggedTool = s_ActiveTool;
    }
}

/* ========================================================================
 * Nine-Slice Chrome tool renderer (Tab 7)
 * ======================================================================== */

static void chromeToolSanitizeSlug(const char *name, char *out, s32 outlen)
{
    s32 n = 0;
    for (s32 i = 0; name && name[i] && n < outlen - 1; i++) {
        char c = name[i];
        if (c == ' ') c = '-';
        else if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) continue;
        out[n++] = c;
    }
    out[n] = '\0';
}

/* Minimal JSON string escaper. Writes a quoted-string body (no surrounding
 * quotes) into out. Escapes the characters JSON requires: `"`, `\`, control
 * chars (<0x20) via \uXXXX. Everything else (including high-ASCII / UTF-8
 * bytes) passes through as-is. */
static void chromeToolJsonEscape(const char *in, char *out, size_t outlen)
{
    if (!out || outlen == 0) return;
    size_t o = 0;
    out[0] = '\0';
    if (!in) return;
    for (size_t i = 0; in[i] && o + 7 < outlen; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c == '\b') { out[o++] = '\\'; out[o++] = 'b'; }
        else if (c == '\f') { out[o++] = '\\'; out[o++] = 'f'; }
        else if (c == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
        else if (c == '\r') { out[o++] = '\\'; out[o++] = 'r'; }
        else if (c == '\t') { out[o++] = '\\'; out[o++] = 't'; }
        else if (c < 0x20) {
            /* \u00XX form */
            static const char hex[] = "0123456789abcdef";
            out[o++] = '\\'; out[o++] = 'u';
            out[o++] = '0'; out[o++] = '0';
            out[o++] = hex[(c >> 4) & 0xF];
            out[o++] = hex[c & 0xF];
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

static bool chromeToolWriteTga(const char *path, const u8 *rgba, s32 w, s32 h)
{
    /* TGA format width/height are u16 — hard cap at 65535. We also cap
     * elsewhere to CHROME_MAX_OUT_DIM (4096), but defend in depth here. */
    if (w <= 0 || h <= 0 || w > 65535 || h > 65535) return false;

    FILE *f = fsFileOpenWrite(path);
    if (!f) return false;

    u8 header[18];
    memset(header, 0, sizeof(header));
    header[2] = 2; /* uncompressed true-color */
    header[12] = (u8)(w & 0xFF);
    header[13] = (u8)((w >> 8) & 0xFF);
    header[14] = (u8)(h & 0xFF);
    header[15] = (u8)((h >> 8) & 0xFF);
    header[16] = 32;
    header[17] = 0x28; /* top-left + alpha bits */
    fwrite(header, 1, sizeof(header), f);

    /* Use size_t to avoid s32 overflow on very large outputs (C-1 audit fix). */
    size_t px = (size_t)w * (size_t)h;
    for (size_t i = 0; i < px; i++) {
        u8 bgra[4] = { rgba[i*4 + 2], rgba[i*4 + 1], rgba[i*4 + 0], rgba[i*4 + 3] };
        fwrite(bgra, 1, 4, f);
    }

    bool ok = !ferror(f);
    fclose(f);
    return ok;
}

static void chromeToolReleaseImage(void)
{
    if (s_ChromeTex != 0) {
        glDeleteTextures(1, &s_ChromeTex);
        s_ChromeTex = 0;
    }
    if (s_ChromePreviewTex != 0) {
        glDeleteTextures(1, &s_ChromePreviewTex);
        s_ChromePreviewTex = 0;
    }
    if (s_ChromePixels) {
        stbi_image_free(s_ChromePixels);
        s_ChromePixels = NULL;
    }
    if (s_ChromePreviewPixels) {
        free(s_ChromePreviewPixels);
        s_ChromePreviewPixels = NULL;
    }
    s_ChromeImgW = 0;
    s_ChromeImgH = 0;
    s_ChromeOutW = 0;
    s_ChromeOutH = 0;
    s_ChromePreviewTexW = 0;
    s_ChromePreviewTexH = 0;
}

static void chromeToolUpdatePreviewTexture(void)
{
    if (!s_ChromePixels || s_ChromeImgW <= 0 || s_ChromeImgH <= 0) return;

    s32 trimL = s_ChromeTrimL; if (trimL < 0) trimL = 0; if (trimL >= s_ChromeImgW) trimL = s_ChromeImgW - 1;
    s32 trimR = s_ChromeTrimR; if (trimR < 0) trimR = 0; if (trimR >= s_ChromeImgW) trimR = s_ChromeImgW - 1;
    s32 trimT = s_ChromeTrimT; if (trimT < 0) trimT = 0; if (trimT >= s_ChromeImgH) trimT = s_ChromeImgH - 1;
    s32 trimB = s_ChromeTrimB; if (trimB < 0) trimB = 0; if (trimB >= s_ChromeImgH) trimB = s_ChromeImgH - 1;

    s32 cropW = s_ChromeImgW - trimL - trimR;
    s32 cropH = s_ChromeImgH - trimT - trimB;
    if (cropW < 1) cropW = 1;
    if (cropH < 1) cropH = 1;

    s32 cutPct = s_ChromeCenterCutPct;
    if (cutPct < 0) cutPct = 0;
    if (cutPct > 90) cutPct = 90;

    s32 cutAxis = s_ChromeCenterCutAxis;
    s32 cutPxX = 0;
    s32 cutPxY = 0;
    if (cutAxis == 1) {
        cutPxY = (cropH * cutPct) / 100;
        if (cutPxY >= cropH) cutPxY = cropH - 1;
        if (cutPxY < 0) cutPxY = 0;
    } else if (cutAxis == 2) {
        cutPxX = (cropW * cutPct) / 100;
        if (cutPxX >= cropW) cutPxX = cropW - 1;
        if (cutPxX < 0) cutPxX = 0;
    }

    s32 stitchedW = cropW - cutPxX;
    s32 stitchedH = cropH - cutPxY;
    if (stitchedW < 1) stitchedW = 1;
    if (stitchedH < 1) stitchedH = 1;

    s32 scaleX = s_ChromeScaleXPct;
    s32 scaleY = s_ChromeScaleYPct;
    if (scaleX < 10) scaleX = 10;
    if (scaleY < 10) scaleY = 10;
    if (scaleX > 400) scaleX = 400;
    if (scaleY > 400) scaleY = 400;

    /* Compute new output dims into locals; commit to s_ChromeOutW/H only
     * after a successful allocation (C-5/S-7 audit fix). */
    s32 newOutW = (stitchedW * scaleX) / 100;
    s32 newOutH = (stitchedH * scaleY) / 100;
    if (newOutW < 1) newOutW = 1;
    if (newOutH < 1) newOutH = 1;
    /* C-1/C-2 audit fix: hard-cap output dims. Anything beyond this would
     * produce TGAs larger than the format supports and/or multi-GB allocs. */
    if (newOutW > CHROME_MAX_OUT_DIM) newOutW = CHROME_MAX_OUT_DIM;
    if (newOutH > CHROME_MAX_OUT_DIM) newOutH = CHROME_MAX_OUT_DIM;

    size_t pxCount = (size_t)newOutW * (size_t)newOutH;
    size_t bytes = pxCount * 4u;
    u8 *resized = (u8 *)(s_ChromePreviewPixels ? realloc(s_ChromePreviewPixels, bytes)
                                               : malloc(bytes));
    if (!resized) {
        /* S-7: leave existing preview intact; surface the failure. Do NOT
         * overwrite s_ChromeOutW/H — they still describe the current buffer. */
        snprintf(s_ChromeStatus, sizeof(s_ChromeStatus),
                 "Preview alloc failed (%dx%d, %.1f MB) — reduce scale or image size",
                 newOutW, newOutH, (double)bytes / (1024.0 * 1024.0));
        s_ChromeStatusOk = false;
        return;
    }
    s_ChromePreviewPixels = resized;
    s_ChromeOutW = newOutW;
    s_ChromeOutH = newOutH;

    /* Resolve proportional insets to pixels against the freshly-committed
     * output dims. This keeps the visual border proportion stable across
     * Scale X/Y changes. In pixel mode the user's values are used as-is. */
    if (s_ChromeProportionalInsets) {
        if (s_ChromeInsetLPct < 0.0f) s_ChromeInsetLPct = 0.0f;
        if (s_ChromeInsetRPct < 0.0f) s_ChromeInsetRPct = 0.0f;
        if (s_ChromeInsetTPct < 0.0f) s_ChromeInsetTPct = 0.0f;
        if (s_ChromeInsetBPct < 0.0f) s_ChromeInsetBPct = 0.0f;
        if (s_ChromeInsetLPct > 50.0f) s_ChromeInsetLPct = 50.0f;
        if (s_ChromeInsetRPct > 50.0f) s_ChromeInsetRPct = 50.0f;
        if (s_ChromeInsetTPct > 50.0f) s_ChromeInsetTPct = 50.0f;
        if (s_ChromeInsetBPct > 50.0f) s_ChromeInsetBPct = 50.0f;
        s_ChromeInsetL = (s32)((s_ChromeInsetLPct * (float)s_ChromeOutW) / 100.0f + 0.5f);
        s_ChromeInsetR = (s32)((s_ChromeInsetRPct * (float)s_ChromeOutW) / 100.0f + 0.5f);
        s_ChromeInsetT = (s32)((s_ChromeInsetTPct * (float)s_ChromeOutH) / 100.0f + 0.5f);
        s_ChromeInsetB = (s32)((s_ChromeInsetBPct * (float)s_ChromeOutH) / 100.0f + 0.5f);
    }

    float t = s_ChromeDesaturate ? ((float)s_ChromeDesaturatePct / 100.0f) : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    s32 keepTop = (cropH - cutPxY) / 2;
    s32 keepLeft = (cropW - cutPxX) / 2;

    for (s32 oy = 0; oy < s_ChromeOutH; oy++) {
        s32 syStitched = (oy * stitchedH) / s_ChromeOutH;
        s32 syCrop = syStitched;
        if (cutPxY > 0 && cutAxis == 1 && syStitched >= keepTop) {
            syCrop += cutPxY;
        }
        if (syCrop >= cropH) syCrop = cropH - 1;

        for (s32 ox = 0; ox < s_ChromeOutW; ox++) {
            s32 sxStitched = (ox * stitchedW) / s_ChromeOutW;
            s32 sxCrop = sxStitched;
            if (cutPxX > 0 && cutAxis == 2 && sxStitched >= keepLeft) {
                sxCrop += cutPxX;
            }
            if (sxCrop >= cropW) sxCrop = cropW - 1;

            s32 srcX = trimL + sxCrop;
            s32 srcY = trimT + syCrop;
            if (srcX < 0) srcX = 0; if (srcX >= s_ChromeImgW) srcX = s_ChromeImgW - 1;
            if (srcY < 0) srcY = 0; if (srcY >= s_ChromeImgH) srcY = s_ChromeImgH - 1;

            const u8 *src = &s_ChromePixels[(srcY * s_ChromeImgW + srcX) * 4];
            float rf = (float)src[0];
            float gf = (float)src[1];
            float bf = (float)src[2];
            float gray = rf * 0.299f + gf * 0.587f + bf * 0.114f;
            size_t di = (size_t)(oy * s_ChromeOutW + ox) * 4u;
            s_ChromePreviewPixels[di + 0] = (u8)(rf + (gray - rf) * t);
            s_ChromePreviewPixels[di + 1] = (u8)(gf + (gray - gf) * t);
            s_ChromePreviewPixels[di + 2] = (u8)(bf + (gray - bf) * t);
            s_ChromePreviewPixels[di + 3] = src[3];
        }
    }

    if (s_ChromePreviewTex == 0) {
        glGenTextures(1, &s_ChromePreviewTex);
        s_ChromePreviewTexW = 0;
        s_ChromePreviewTexH = 0;
    }
    glBindTexture(GL_TEXTURE_2D, s_ChromePreviewTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    /* S-10 audit fix: skip the full glTexImage2D reallocation when only the
     * pixels changed (common case on slider ticks). glTexSubImage2D reuses
     * the driver-side storage — saves ~16 MB/tick on a 2K preview. */
    if (s_ChromePreviewTexW == s_ChromeOutW && s_ChromePreviewTexH == s_ChromeOutH) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        s_ChromeOutW, s_ChromeOutH,
                        GL_RGBA, GL_UNSIGNED_BYTE, s_ChromePreviewPixels);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     s_ChromeOutW, s_ChromeOutH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, s_ChromePreviewPixels);
        s_ChromePreviewTexW = s_ChromeOutW;
        s_ChromePreviewTexH = s_ChromeOutH;
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    /* Clamp insets into edited output dimensions to keep a valid center area. */
    if (s_ChromeInsetL < 0) s_ChromeInsetL = 0;
    if (s_ChromeInsetR < 0) s_ChromeInsetR = 0;
    if (s_ChromeInsetT < 0) s_ChromeInsetT = 0;
    if (s_ChromeInsetB < 0) s_ChromeInsetB = 0;
    if (s_ChromeInsetL + s_ChromeInsetR >= s_ChromeOutW) {
        s_ChromeInsetR = s_ChromeOutW - s_ChromeInsetL - 1;
        if (s_ChromeInsetR < 0) s_ChromeInsetR = 0;
    }
    if (s_ChromeInsetT + s_ChromeInsetB >= s_ChromeOutH) {
        s_ChromeInsetB = s_ChromeOutH - s_ChromeInsetT - 1;
        if (s_ChromeInsetB < 0) s_ChromeInsetB = 0;
    }
}

static void chromeToolBuildDef(nineslice_def_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    /* Border Scale decouples on-screen corner size from source slice loc. */
    float bs = s_ChromeBorderScale;
    if (bs < 0.25f) bs = 0.25f;
    if (bs > 4.0f)  bs = 4.0f;
    out->src_left = s_ChromeInsetL;
    out->src_right = s_ChromeInsetR;
    out->src_top = s_ChromeInsetT;
    out->src_bottom = s_ChromeInsetB;
    out->dst_left = (s32)((float)s_ChromeInsetL * bs + 0.5f);
    out->dst_right = (s32)((float)s_ChromeInsetR * bs + 0.5f);
    out->dst_top = (s32)((float)s_ChromeInsetT * bs + 0.5f);
    out->dst_bottom = (s32)((float)s_ChromeInsetB * bs + 0.5f);
    out->has_split = 1;
    out->has_per_edge_mode = 1;
    out->top_mode = s_ChromeEdgeTile ? NINESLICE_TILE : NINESLICE_STRETCH;
    out->bottom_mode = s_ChromeEdgeTile ? NINESLICE_TILE : NINESLICE_STRETCH;
    out->left_mode = s_ChromeEdgeTile ? NINESLICE_TILE : NINESLICE_STRETCH;
    out->right_mode = s_ChromeEdgeTile ? NINESLICE_TILE : NINESLICE_STRETCH;
    out->center_mode = s_ChromeCenterTile ? NINESLICE_TILE : NINESLICE_STRETCH;
}

static bool chromeToolLoadImage(const char *path)
{
    if (!path || !path[0]) return false;

    s32 w = 0, h = 0, c = 0;
    u8 *rgba = stbi_load(path, &w, &h, &c, 4);
    if (!rgba || w <= 0 || h <= 0) {
        snprintf(s_ChromeStatus, sizeof(s_ChromeStatus), "Load failed: %s",
                 stbi_failure_reason() ? stbi_failure_reason() : "unknown");
        s_ChromeStatusOk = false;
        if (rgba) stbi_image_free(rgba);
        return false;
    }

    /* C-2 audit fix: reject absurdly-large source images before we commit
     * VRAM/RAM to them. User-facing hint tells them what to do. */
    if (w > CHROME_MAX_IMG_DIM || h > CHROME_MAX_IMG_DIM) {
        snprintf(s_ChromeStatus, sizeof(s_ChromeStatus),
                 "Image too large (%dx%d). Max %d x %d — downscale in an editor first.",
                 w, h, CHROME_MAX_IMG_DIM, CHROME_MAX_IMG_DIM);
        s_ChromeStatusOk = false;
        stbi_image_free(rgba);
        return false;
    }

    chromeToolReleaseImage();
    s_ChromePixels = rgba;
    s_ChromeImgW = w;
    s_ChromeImgH = h;

    /* S-11 audit fix: do NOT upload the full-res source to a separate GL
     * texture. The processed preview is always generated in the same call
     * path, so `s_ChromePreviewTex` is the only texture we ever render. */

    /* Default insets: 25% of each side (quarter-rule). In proportional mode
     * the pct values drive; they're resolved to pixels inside
     * chromeToolUpdatePreviewTexture against the current output dims. */
    s_ChromeInsetLPct = 25.0f;
    s_ChromeInsetRPct = 25.0f;
    s_ChromeInsetTPct = 25.0f;
    s_ChromeInsetBPct = 25.0f;
    s_ChromeInsetL = w / 4;
    s_ChromeInsetR = w / 4;
    s_ChromeInsetT = h / 4;
    s_ChromeInsetB = h / 4;
    s_ChromeTrimL = s_ChromeTrimR = s_ChromeTrimT = s_ChromeTrimB = 0;
    s_ChromeScaleXPct = 100;
    s_ChromeScaleYPct = 100;
    s_ChromeCenterCutAxis = 0;
    s_ChromeCenterCutPct = 0;
    chromeToolUpdatePreviewTexture();
    s_ChromeStatusOk = true;
    snprintf(s_ChromeStatus, sizeof(s_ChromeStatus), "Loaded %dx%d image", w, h);
    return true;
}

static bool chromeToolSaveMod(void)
{
    if (!s_ChromePixels || s_ChromeImgW <= 0 || s_ChromeImgH <= 0) {
        snprintf(s_ChromeStatus, sizeof(s_ChromeStatus), "No image loaded");
        s_ChromeStatusOk = false;
        return false;
    }
    chromeToolUpdatePreviewTexture();
    if (!s_ChromePreviewPixels || s_ChromeOutW <= 0 || s_ChromeOutH <= 0) {
        snprintf(s_ChromeStatus, sizeof(s_ChromeStatus), "Image processing failed");
        s_ChromeStatusOk = false;
        return false;
    }

    char slug[64];
    chromeToolSanitizeSlug(s_ChromeModName, slug, sizeof(slug));
    if (!slug[0]) {
        snprintf(s_ChromeStatus, sizeof(s_ChromeStatus), "Invalid mod name");
        s_ChromeStatusOk = false;
        return false;
    }

    /* Chrome mods live under the "UI Chrome" category folder so they are
     * grouped with other UI-chrome mods in the mods/ tree. Recursive scanners
     * (modmgr + theme) pick them up from this nested location. */
    if (!fsCreateDir("mods")) {
        snprintf(s_ChromeStatus, sizeof(s_ChromeStatus), "Could not create mods/");
        s_ChromeStatusOk = false;
        return false;
    }
    if (!fsCreateDir("mods/UI Chrome")) {
        snprintf(s_ChromeStatus, sizeof(s_ChromeStatus), "Could not create mods/UI Chrome/");
        s_ChromeStatusOk = false;
        return false;
    }

    char modDir[FS_MAXPATH];
    snprintf(modDir, sizeof(modDir), "mods/UI Chrome/%s", slug);
    if (!fsCreateDir(modDir)) {
        snprintf(s_ChromeStatus, sizeof(s_ChromeStatus), "Could not create %s", modDir);
        s_ChromeStatusOk = false;
        return false;
    }

    char texPath[FS_MAXPATH];
    snprintf(texPath, sizeof(texPath), "%s/ui_chrome_frame.tga", modDir);
    /* The preview buffer is the normalized/resampled output — exactly what
     * Mike wants written (not the raw import). All transforms (trim, cut,
     * scale, desat) are already baked into this buffer, and its dimensions
     * are the standardized output size driven by Scale X/Y. */
    const u8 *savePixels = s_ChromePreviewPixels;
    if (!chromeToolWriteTga(texPath, savePixels, s_ChromeOutW, s_ChromeOutH)) {
        snprintf(s_ChromeStatus, sizeof(s_ChromeStatus), "Failed writing TGA (dims %dx%d)",
                 s_ChromeOutW, s_ChromeOutH);
        s_ChromeStatusOk = false;
        return false;
    }

    char styleId[CATALOG_ID_LEN];
    snprintf(styleId, sizeof(styleId), "mod:%s_ui_chrome_frame", slug);

    char jsonPath[FS_MAXPATH];
    snprintf(jsonPath, sizeof(jsonPath), "%s/mod.json", modDir);
    FILE *f = fsFileOpenWrite(jsonPath);
    if (!f) {
        snprintf(s_ChromeStatus, sizeof(s_ChromeStatus), "Failed writing mod.json");
        s_ChromeStatusOk = false;
        return false;
    }

    /* Apply Border Scale so dst_corner_px can differ from src_inset. */
    float bs = s_ChromeBorderScale;
    if (bs < 0.25f) bs = 0.25f;
    if (bs > 4.0f)  bs = 4.0f;
    s32 dstT = (s32)((float)s_ChromeInsetT * bs + 0.5f);
    s32 dstB = (s32)((float)s_ChromeInsetB * bs + 0.5f);
    s32 dstL = (s32)((float)s_ChromeInsetL * bs + 0.5f);
    s32 dstR = (s32)((float)s_ChromeInsetR * bs + 0.5f);
    if (dstT < 0) dstT = 0;
    if (dstB < 0) dstB = 0;
    if (dstL < 0) dstL = 0;
    if (dstR < 0) dstR = 0;

    /* C-4 audit fix: escape the name for JSON. Without this, a name with a
     * quote or backslash corrupts mod.json and the mod silently fails to
     * parse at next scan. */
    char escName[384];
    chromeToolJsonEscape(s_ChromeModName, escName, sizeof(escName));

    const char *fillMode = s_ChromeEdgeTile ? "tile" : "stretch";
    fprintf(f,
        "{\n"
        "  \"id\": \"user.%s.ui-chrome\",\n"
        "  \"name\": \"%s\",\n"
        "  \"version\": \"1.0.0\",\n"
        "  \"description\": \"Created in-game with Menu Style tool.\",\n"
        "  \"author\": \"Player\",\n"
        "  \"tags\": [\"chrome\", \"ui\", \"user\"],\n"
        "  \"enabled\": true,\n"
        "  \"chrome_authoring\": {\n"
        "    \"output_w\": %d,\n"
        "    \"output_h\": %d,\n"
        "    \"border_scale\": %.3f,\n"
        "    \"proportional_insets\": %s,\n"
        "    \"inset_pct\": { \"top\": %.3f, \"bottom\": %.3f, \"left\": %.3f, \"right\": %.3f }\n"
        "  },\n"
        "  \"components\": {\n"
        "    \"textures\": [\n"
        "      { \"id\": \"%s\", \"file\": \"ui_chrome_frame.tga\" }\n"
        "    ],\n"
        "    \"nineslice\": [\n"
        "      {\n"
        "        \"id\": \"%s\",\n"
        "        \"texture\": \"%s\",\n"
        "        \"src_inset\": { \"top\": %d, \"bottom\": %d, \"left\": %d, \"right\": %d },\n"
        "        \"dst_corner_px\": { \"top\": %d, \"bottom\": %d, \"left\": %d, \"right\": %d },\n"
        "        \"top_mode\": \"%s\",\n"
        "        \"bottom_mode\": \"%s\",\n"
        "        \"left_mode\": \"%s\",\n"
        "        \"right_mode\": \"%s\",\n"
        "        \"center_mode\": \"%s\"\n"
        "      }\n"
        "    ]\n"
        "  }\n"
        "}\n",
        slug, escName,
        s_ChromeOutW, s_ChromeOutH,
        (double)bs,
        s_ChromeProportionalInsets ? "true" : "false",
        (double)s_ChromeInsetTPct, (double)s_ChromeInsetBPct,
        (double)s_ChromeInsetLPct, (double)s_ChromeInsetRPct,
        styleId,
        styleId, styleId,
        s_ChromeInsetT, s_ChromeInsetB, s_ChromeInsetL, s_ChromeInsetR,
        dstT, dstB, dstL, dstR,
        fillMode, fillMode, fillMode, fillMode,
        s_ChromeCenterTile ? "tile" : "stretch");

    bool writeOk = !ferror(f);
    fclose(f);
    if (!writeOk) {
        snprintf(s_ChromeStatus, sizeof(s_ChromeStatus), "Failed finalizing mod.json");
        s_ChromeStatusOk = false;
        return false;
    }

    pdguiThemeRegisterChromeModDir(modDir, 1);

    /* Make the new mod visible in the Modding Hub Mods list immediately:
     * re-scan mods/ so the registry picks up the newly-written dir, then
     * flip its enabled bit + persist to pd.ini so state matches the
     * "Saved & activated" status we advertise and survives restart. */
    char newModId[128];
    snprintf(newModId, sizeof(newModId), "user.%s.ui-chrome", slug);
    modmgrRescanDirectory();
    s32 modCount = modmgrGetCount();
    for (s32 i = 0; i < modCount; i++) {
        const char *id = modmgrGetModId(i);
        if (id && strcmp(id, newModId) == 0) {
            modmgrSetEnabled(i, 1);
            break;
        }
    }
    modmgrSaveConfig();
    /* Rebuild the Mods tab's display snapshot so the new entry shows up
     * next time the user switches to the Mod Manager tool. */
    pdguiModManagerRefreshSnapshot();

    s_ChromeStatusOk = true;
    snprintf(s_ChromeStatus, sizeof(s_ChromeStatus),
             "Saved & activated: %s%s",
             slug,
             s_ChromeDesaturate ? " (desaturated for tinting)" : "");
    return true;
}

static void chromeToolReset(void)
{
    s_ChromeImgPath[0] = '\0';
    snprintf(s_ChromeModName, sizeof(s_ChromeModName), "%s", "my-ui-chrome");
    s_ChromeStatus[0] = '\0';
    s_ChromeStatusOk = true;
    s_ChromeLrSymmetry = true;
    s_ChromeTbSymmetry = true;
    s_ChromeCenterTile = true;
    s_ChromeEdgeTile = false;
    s_ChromeDesaturate = false;
    s_ChromeDesaturatePct = 100;
    s_ChromeTrimL = s_ChromeTrimR = s_ChromeTrimT = s_ChromeTrimB = 0;
    s_ChromeScaleXPct = 100;
    s_ChromeScaleYPct = 100;
    s_ChromeCenterCutAxis = 0;
    s_ChromeCenterCutPct = 0;
    s_ChromeInsetL = s_ChromeInsetR = 16;
    s_ChromeInsetT = s_ChromeInsetB = 16;
    s_ChromeProportionalInsets = true;
    s_ChromeInsetLPct = s_ChromeInsetRPct = 25.0f;
    s_ChromeInsetTPct = s_ChromeInsetBPct = 25.0f;
    s_ChromeBorderScale = 1.0f;
    chromeToolReleaseImage();
}

static void chromeToolApplySquarePreset(void)
{
    if (s_ChromeImgW <= 0 || s_ChromeImgH <= 0) return;

    /* Preserve trim, then choose center-cut axis/% from cropped aspect. */
    s32 cropW = s_ChromeImgW - s_ChromeTrimL - s_ChromeTrimR;
    s32 cropH = s_ChromeImgH - s_ChromeTrimT - s_ChromeTrimB;
    if (cropW < 1) cropW = 1;
    if (cropH < 1) cropH = 1;

    if (cropH > cropW) {
        s_ChromeCenterCutAxis = 1; /* remove height center strip */
        s_ChromeCenterCutPct = ((cropH - cropW) * 100) / cropH;
    } else if (cropW > cropH) {
        s_ChromeCenterCutAxis = 2; /* remove width center strip */
        s_ChromeCenterCutPct = ((cropW - cropH) * 100) / cropW;
    } else {
        s_ChromeCenterCutAxis = 0;
        s_ChromeCenterCutPct = 0;
    }

    if (s_ChromeCenterCutPct > 90) s_ChromeCenterCutPct = 90;
    if (s_ChromeCenterCutPct < 0) s_ChromeCenterCutPct = 0;
}

/* Renders the docked image-preview sidebar: source rulers + assembled frame. */
static void chromeToolRenderSidebarPreview(float sidebarW, float sidebarH, float scale)
{
    /* Two stacked previews split the sidebar vertically. */
    float previewH = (sidebarH - ImGui::GetStyle().ItemSpacing.y * 3.0f) * 0.5f;
    if (previewH < 100.0f * scale) previewH = 100.0f * scale;

    /* S305: fit the source preview to the sidebar preview area, scaling UP
     * for small images so the preview always displays at a standard size
     * matching the frame preview below. Previously small images (e.g. 64x64
     * imports) displayed tiny in the top preview while the assembled frame
     * filled the full sidebar, which felt inconsistent. Aspect ratio is
     * preserved via min(sx, sy). */
    float sx = sidebarW / (float)s_ChromeOutW;
    float sy = previewH / (float)s_ChromeOutH;
    float imgScale = sx < sy ? sx : sy;
    if (imgScale <= 0.0f) imgScale = 1.0f;
    float drawW = s_ChromeOutW * imgScale;
    float drawH = s_ChromeOutH * imgScale;

    GLuint previewTex = s_ChromePreviewTex;
    ImDrawList *dl = ImGui::GetWindowDrawList();

    /* --- Top: source preview with ruler overlay --- */
    ImGui::TextDisabled("Source Preview (with rulers)");
    /* Center the image horizontally within the sidebar column. */
    float leftPad = (sidebarW - drawW) * 0.5f;
    if (leftPad > 0.0f) {
        ImGui::Dummy(ImVec2(leftPad, 0.0f));
        ImGui::SameLine(0.0f, 0.0f);
    }
    ImGui::Image((ImTextureID)(uintptr_t)previewTex, ImVec2(drawW, drawH));
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();

    float lx = p0.x + (float)s_ChromeInsetL * imgScale;
    float rx = p1.x - (float)s_ChromeInsetR * imgScale;
    float ty = p0.y + (float)s_ChromeInsetT * imgScale;
    float by = p1.y - (float)s_ChromeInsetB * imgScale;
    dl->AddLine(ImVec2(lx, p0.y), ImVec2(lx, p1.y), IM_COL32(255, 80, 80, 220), 2.0f);
    dl->AddLine(ImVec2(rx, p0.y), ImVec2(rx, p1.y), IM_COL32(255, 80, 80, 220), 2.0f);
    dl->AddLine(ImVec2(p0.x, ty), ImVec2(p1.x, ty), IM_COL32(80, 255, 80, 220), 2.0f);
    dl->AddLine(ImVec2(p0.x, by), ImVec2(p1.x, by), IM_COL32(80, 255, 80, 220), 2.0f);

    ImGui::Spacing();

    /* --- Bottom: assembled nine-slice frame preview --- */
    ImGui::TextDisabled("Frame Preview (assembled nine-slice)");
    float frameW = sidebarW;
    float frameH = previewH;
    ImVec2 framePos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(frameW, frameH));
    dl->AddRectFilled(framePos, ImVec2(framePos.x + frameW, framePos.y + frameH),
                      IM_COL32(18, 22, 28, 255), 3.0f);
    dl->AddRect(framePos, ImVec2(framePos.x + frameW, framePos.y + frameH),
                pdguiImU32TitleGlow(220), 3.0f);

    nineslice_def_t previewDef;
    chromeToolBuildDef(&previewDef);
    pdguiNinesliceDrawEx((void *)(uintptr_t)previewTex, s_ChromeOutW, s_ChromeOutH, &previewDef,
                         framePos.x + 10.0f * scale, framePos.y + 10.0f * scale,
                         frameW - 20.0f * scale, frameH - 20.0f * scale,
                         IM_COL32(255, 255, 255, 255));
}

/* Renders the scrollable settings column (all sliders/toggles/presets). */
static void chromeToolRenderSettings(float /*colW*/, float /*scale*/)
{
    /* Priority L (2026-04-25): labels ABOVE/LEFT via pdgui* helpers.
     * Note: SameLine pairings between symmetry checkboxes are dropped --
     * the label-LEFT layout lays each row out at the column width so the
     * pair-on-one-row layout no longer applies. Vertical stacking is the
     * intended mode after L-fix-6. */
    pdguiInputText("Mod Name", s_ChromeModName, sizeof(s_ChromeModName));
    pdguiCheckbox("L/R symmetry", &s_ChromeLrSymmetry);
    pdguiCheckbox("T/B symmetry", &s_ChromeTbSymmetry);
    pdguiCheckbox("Center tile mode", &s_ChromeCenterTile);
    pdguiCheckbox("Edge tile mode", &s_ChromeEdgeTile);
    bool editsChanged = false;
    /* S-9 audit fix: cross-clamp trim sliders so the opposite pair can never
     * over-commit the image (which previously yielded degenerate 1px crops
     * with no user feedback). */
    s32 trimLMax = s_ChromeImgW - s_ChromeTrimR - 1; if (trimLMax < 0) trimLMax = 0;
    s32 trimRMax = s_ChromeImgW - s_ChromeTrimL - 1; if (trimRMax < 0) trimRMax = 0;
    s32 trimTMax = s_ChromeImgH - s_ChromeTrimB - 1; if (trimTMax < 0) trimTMax = 0;
    s32 trimBMax = s_ChromeImgH - s_ChromeTrimT - 1; if (trimBMax < 0) trimBMax = 0;
    /* Priority L (2026-04-25): labels LEFT via pdguiSliderInt. */
    editsChanged |= pdguiSliderInt("Trim Left", &s_ChromeTrimL, 0, trimLMax > 0 ? trimLMax : 1);
    editsChanged |= pdguiSliderInt("Trim Right", &s_ChromeTrimR, 0, trimRMax > 0 ? trimRMax : 1);
    editsChanged |= pdguiSliderInt("Trim Top", &s_ChromeTrimT, 0, trimTMax > 0 ? trimTMax : 1);
    editsChanged |= pdguiSliderInt("Trim Bottom", &s_ChromeTrimB, 0, trimBMax > 0 ? trimBMax : 1);

    /* S305: advanced controls moved into a collapsed header to clean up the
     * tool. Scale X/Y, Center Cut, Desaturate, and the per-cut Quick Presets
     * are all power-user options most chrome mods never need — Border Scale
     * + Trim + per-edge insets handle the 90% case. Collapsed by default. */
    if (ImGui::CollapsingHeader("Advanced")) {
        /* Priority L (2026-04-25): labels LEFT via pdguiSliderInt. */
        editsChanged |= pdguiSliderInt("Scale X", &s_ChromeScaleXPct, 10, 400, "%d%%");
        editsChanged |= pdguiSliderInt("Scale Y", &s_ChromeScaleYPct, 10, 400, "%d%%");
        static const char *cutAxes[] = { "None", "Vertical (height)", "Horizontal (width)" };
        /* Priority L (2026-04-25): label LEFT via pdguiCombo. */
        editsChanged |= pdguiCombo("Center Cut Axis", &s_ChromeCenterCutAxis, cutAxes, 3);
        if (s_ChromeCenterCutAxis != 0) {
            /* Priority L (2026-04-25): label LEFT via pdguiSliderInt. */
            editsChanged |= pdguiSliderInt("Center Cut %", &s_ChromeCenterCutPct, 0, 90, "%d%%");
        } else {
            s_ChromeCenterCutPct = 0;
        }

        /* Priority L (2026-04-25): label LEFT via pdguiCheckbox. */
        bool desatChanged = pdguiCheckbox("Desaturate for tint-friendly chrome", &s_ChromeDesaturate);
        if (s_ChromeDesaturate) {
            /* Priority L (2026-04-25): label LEFT via pdguiSliderInt. */
            desatChanged |= pdguiSliderInt("Desaturate %", &s_ChromeDesaturatePct, 0, 100, "%d%%");
        }
        editsChanged = editsChanged || desatChanged;

        float scale2 = ImGui::GetIO().FontGlobalScale;
        if (scale2 <= 0.0f) scale2 = 1.0f;
        ImGui::TextDisabled("Quick Presets");
        if (PdButton("Square Auto", ImVec2(110.0f * scale2, 0))) {
            chromeToolApplySquarePreset();
            editsChanged = true;
        }
        ImGui::SameLine();
        if (PdButton("Cut 40% V", ImVec2(95.0f * scale2, 0))) {
            s_ChromeCenterCutAxis = 1;
            s_ChromeCenterCutPct = 40;
            editsChanged = true;
        }
        ImGui::SameLine();
        if (PdButton("Cut 40% H", ImVec2(95.0f * scale2, 0))) {
            s_ChromeCenterCutAxis = 2;
            s_ChromeCenterCutPct = 40;
            editsChanged = true;
        }
        ImGui::SameLine();
        if (PdButton("Tint Gray", ImVec2(90.0f * scale2, 0))) {
            s_ChromeDesaturate = true;
            s_ChromeDesaturatePct = 100;
            editsChanged = true;
        }
    }

    if (editsChanged) {
        chromeToolUpdatePreviewTexture();
    }

    /* Border Scale (always visible): multiplies dst_corner_px relative to
     * src_inset. Lets users produce a chrome that renders with a different
     * corner thickness than the source slice. 1.0 = source size. */
    /* Priority L (2026-04-25): labels LEFT via pdgui* helpers. */
    pdguiSliderFloat("Border Scale", &s_ChromeBorderScale, 0.25f, 4.0f, "%.2fx");
    pdguiCheckbox("Proportional Insets (%% of output)", &s_ChromeProportionalInsets);
    ImGui::SameLine();
    ImGui::TextDisabled("(%%)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("When on, inset sliders are percentages of the output dims\n"
                          "and track Scale X/Y, keeping borders proportional.\n"
                          "When off, sliders are absolute pixels in the output.");
    }

    if (s_ChromeProportionalInsets) {
        bool insetChanged = false;
        /* Priority L (2026-04-25): label LEFT via pdguiSliderFloat. */
        if (pdguiSliderFloat("Left %",  &s_ChromeInsetLPct, 0.0f, 50.0f, "%.2f%%")) {
            if (s_ChromeLrSymmetry) s_ChromeInsetRPct = s_ChromeInsetLPct;
            insetChanged = true;
        }
        /* Priority L (2026-04-25): label LEFT via pdguiSliderFloat. */
        if (pdguiSliderFloat("Right %", &s_ChromeInsetRPct, 0.0f, 50.0f, "%.2f%%")) {
            if (s_ChromeLrSymmetry) s_ChromeInsetLPct = s_ChromeInsetRPct;
            insetChanged = true;
        }
        /* Priority L (2026-04-25): label LEFT via pdguiSliderFloat. */
        if (pdguiSliderFloat("Top %",   &s_ChromeInsetTPct, 0.0f, 50.0f, "%.2f%%")) {
            if (s_ChromeTbSymmetry) s_ChromeInsetBPct = s_ChromeInsetTPct;
            insetChanged = true;
        }
        /* Priority L (2026-04-25): label LEFT via pdguiSliderFloat. */
        if (pdguiSliderFloat("Bottom %",&s_ChromeInsetBPct, 0.0f, 50.0f, "%.2f%%")) {
            if (s_ChromeTbSymmetry) s_ChromeInsetTPct = s_ChromeInsetBPct;
            insetChanged = true;
        }
        if (insetChanged) {
            s_ChromeInsetL = (s32)((s_ChromeInsetLPct * (float)s_ChromeOutW) / 100.0f + 0.5f);
            s_ChromeInsetR = (s32)((s_ChromeInsetRPct * (float)s_ChromeOutW) / 100.0f + 0.5f);
            s_ChromeInsetT = (s32)((s_ChromeInsetTPct * (float)s_ChromeOutH) / 100.0f + 0.5f);
            s_ChromeInsetB = (s32)((s_ChromeInsetBPct * (float)s_ChromeOutH) / 100.0f + 0.5f);
        }
        ImGui::TextDisabled("  = %d / %d / %d / %d px (L/R/T/B at current output %dx%d)",
                            s_ChromeInsetL, s_ChromeInsetR, s_ChromeInsetT, s_ChromeInsetB,
                            s_ChromeOutW, s_ChromeOutH);
    } else {
        s32 maxL = s_ChromeOutW > 1 ? s_ChromeOutW - 1 : 1;
        s32 maxT = s_ChromeOutH > 1 ? s_ChromeOutH - 1 : 1;
        /* Priority L (2026-04-25): label LEFT via pdguiSliderInt. */
        if (pdguiSliderInt("Left", &s_ChromeInsetL, 0, maxL)) {
            if (s_ChromeLrSymmetry) s_ChromeInsetR = s_ChromeInsetL;
        }
        /* Priority L (2026-04-25): label LEFT via pdguiSliderInt. */
        if (pdguiSliderInt("Right", &s_ChromeInsetR, 0, maxL)) {
            if (s_ChromeLrSymmetry) s_ChromeInsetL = s_ChromeInsetR;
        }
        /* Priority L (2026-04-25): label LEFT via pdguiSliderInt. */
        if (pdguiSliderInt("Top", &s_ChromeInsetT, 0, maxT)) {
            if (s_ChromeTbSymmetry) s_ChromeInsetB = s_ChromeInsetT;
        }
        /* Priority L (2026-04-25): label LEFT via pdguiSliderInt. */
        if (pdguiSliderInt("Bottom", &s_ChromeInsetB, 0, maxT)) {
            if (s_ChromeTbSymmetry) s_ChromeInsetT = s_ChromeInsetB;
        }
        if (s_ChromeOutW > 0) {
            s_ChromeInsetLPct = (float)s_ChromeInsetL * 100.0f / (float)s_ChromeOutW;
            s_ChromeInsetRPct = (float)s_ChromeInsetR * 100.0f / (float)s_ChromeOutW;
        }
        if (s_ChromeOutH > 0) {
            s_ChromeInsetTPct = (float)s_ChromeInsetT * 100.0f / (float)s_ChromeOutH;
            s_ChromeInsetBPct = (float)s_ChromeInsetB * 100.0f / (float)s_ChromeOutH;
        }
    }

    /* Clamp total inset pairs so center region always exists. */
    if (s_ChromeInsetL + s_ChromeInsetR >= s_ChromeOutW) {
        s_ChromeInsetR = s_ChromeOutW - s_ChromeInsetL - 1;
        if (s_ChromeInsetR < 0) s_ChromeInsetR = 0;
    }
    if (s_ChromeInsetT + s_ChromeInsetB >= s_ChromeOutH) {
        s_ChromeInsetB = s_ChromeOutH - s_ChromeInsetT - 1;
        if (s_ChromeInsetB < 0) s_ChromeInsetB = 0;
    }
}

static void renderChromeTool(float w, float h, float scale)
{
    if (pdguiFileBrowserIsOpen()) {
        if (pdguiFileBrowserRender()) {
            strncpy(s_ChromeImgPath, pdguiFileBrowserGetPath(), sizeof(s_ChromeImgPath) - 1);
            s_ChromeImgPath[sizeof(s_ChromeImgPath) - 1] = '\0';
            pdguiFileBrowserClose();
            chromeToolLoadImage(s_ChromeImgPath);
        }
        return;
    }

    /* --- Header (fixed) --- */
    ImGui::TextDisabled("Menu Style -- import image, set rulers, save as mod");
    ImGui::Spacing();

    ImGui::Text("Image:");
    float browseW = 90.0f * scale;
    ImGui::SetNextItemWidth(-browseW - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputText("##chrome_img", s_ChromeImgPath, sizeof(s_ChromeImgPath));
    ImGui::SameLine();
    if (PdButton("Browse", ImVec2(browseW, 0))) {
        pdguiFileBrowserOpen("Import Nine-Slice Image", "mods", ".png;.jpg;.bmp;.tga");
    }
    ImGui::SameLine();
    if (PdButton("Load", ImVec2(80.0f * scale, 0))) {
        chromeToolLoadImage(s_ChromeImgPath);
    }

    /* S-11 audit fix: gate on preview texture, not the retired s_ChromeTex. */
    if (!s_ChromePixels || s_ChromePreviewTex == 0) {
        ImGui::Spacing();
        ImGui::TextDisabled("Load an image to edit nine-slice rulers.");
        if (s_ChromeStatus[0]) {
            ImGui::TextColored(s_ChromeStatusOk ? ImVec4(0.2f,1.0f,0.4f,1.0f)
                                                : ImVec4(1.0f,0.3f,0.3f,1.0f),
                               "%s", s_ChromeStatus);
        }
        return;
    }

    ImGui::Separator();

    /* --- Docked footer height reserved at bottom --- */
    float footerH  = 34.0f * scale;
    float statusH  = s_ChromeStatus[0] ? ImGui::GetTextLineHeightWithSpacing() : 0.0f;
    float headerUsed = ImGui::GetCursorPosY();
    float bodyH = h - headerUsed - footerH - statusH
                  - ImGui::GetStyle().ItemSpacing.y * 2.0f;
    if (bodyH < 80.0f * scale) bodyH = 80.0f * scale;

    /* --- Body: sidebar (left) + scrollable settings (right) ---
     * Sidebar is wide enough to show a readable preview but never exceeds
     * 45 % of the tool area — ensures the settings column keeps priority on
     * narrow displays. */
    float sidebarW = w * 0.42f;
    if (sidebarW > 420.0f * scale) sidebarW = 420.0f * scale;
    if (sidebarW < 220.0f * scale) sidebarW = 220.0f * scale;
    float settingsW = w - sidebarW - ImGui::GetStyle().ItemSpacing.x * 2.0f;

    /* Left sidebar (no scroll — previews are fixed layout). */
    /* Priority L (2026-04-25): NavFlattened layout panel. */
    if (ImGui::BeginChild("##chrome_sidebar", ImVec2(sidebarW, bodyH),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse)) {
        float innerW = ImGui::GetContentRegionAvail().x;
        float innerH = ImGui::GetContentRegionAvail().y;
        chromeToolRenderSidebarPreview(innerW, innerH, scale);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    /* Right settings column (scrolls). */
    /* Priority L (2026-04-25): NavFlattened layout panel. */
    if (ImGui::BeginChild("##chrome_settings", ImVec2(settingsW, bodyH),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened)) {
        chromeToolRenderSettings(ImGui::GetContentRegionAvail().x, scale);
    }
    ImGui::EndChild();

    /* --- Status line (above footer) --- */
    if (s_ChromeStatus[0]) {
        ImGui::TextColored(s_ChromeStatusOk ? ImVec4(0.2f, 1.0f, 0.4f, 1.0f)
                                            : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                           "%s", s_ChromeStatus);
    }

    /* --- Docked footer: Save / Reset.  Pinned to the tool's bottom edge. --- */
    float dockY = h - footerH;
    if (dockY > ImGui::GetCursorPosY()) {
        ImGui::SetCursorPosY(dockY);
    }
    ImGui::Separator();
    if (PdButton("Save as Mod", ImVec2(160.0f * scale, 28.0f * scale))) {
        chromeToolSaveMod();
    }
    ImGui::SameLine();
    if (PdButton("Reset", ImVec2(90.0f * scale, 28.0f * scale))) {
        chromeToolReset();
    }
}

/* ========================================================================
 * Map Import tool renderer (Tab 6)
 * ======================================================================== */

static void importReset(void)
{
    s_MapImpPath[0] = '\0';
    s_MapImpName[0] = '\0';
    s_MapImpError[0] = '\0';
    s_MapImpStatus[0] = '\0';
    s_MapImpStatusOk = true;
    s_MapImpNumRooms = 0;
    s_MapImpNumPads = 0;
    s_MapImpGenSpawns = 0;
    s_MapImpRunning = false;
    s_MapImpDone = false;
}

static void renderMapImport(float w, float h, float scale)
{
    ImGui::TextDisabled("Import Map -- import PD-format map files");
    ImGui::Spacing();

    /* Source directory path input */
    ImGui::Text("Source Directory:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(w * 0.6f);
    ImGui::InputText("##mapimp_path", s_MapImpPath, sizeof(s_MapImpPath));

    /* Map name input */
    ImGui::Text("Map Name:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(w * 0.3f);
    ImGui::InputText("##mapimp_name", s_MapImpName, sizeof(s_MapImpName));

    ImGui::Spacing();

    /* Import button */
    {
        bool canImport = s_MapImpPath[0] != '\0' && !s_MapImpRunning;

        if (!canImport) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
        }

        if (PdButton("Import Map", ImVec2(140.0f * scale, 28.0f * scale)) && canImport) {
            s_MapImpRunning = true;
            s_MapImpDone = false;
            s_MapImpError[0] = '\0';
            s_MapImpStatus[0] = '\0';

            const char *name = s_MapImpName[0] ? s_MapImpName : NULL;
            s32 rooms = 0, pads = 0, genSpawns = 0;
            char errbuf[256] = "";

            s32 result = mapImportRunFull(s_MapImpPath, name,
                                          errbuf, sizeof(errbuf),
                                          &rooms, &pads, &genSpawns);

            s_MapImpNumRooms = rooms;
            s_MapImpNumPads = pads;
            s_MapImpGenSpawns = genSpawns;
            s_MapImpRunning = false;
            s_MapImpDone = true;

            if (result == 0) {
                s_MapImpStatusOk = true;
                snprintf(s_MapImpStatus, sizeof(s_MapImpStatus),
                         "Import successful! %d rooms, %d pads, %d spawns generated.",
                         rooms, pads, genSpawns);
            } else {
                s_MapImpStatusOk = false;
                strncpy(s_MapImpError, errbuf, sizeof(s_MapImpError) - 1);
                s_MapImpError[sizeof(s_MapImpError) - 1] = '\0';
                snprintf(s_MapImpStatus, sizeof(s_MapImpStatus),
                         "Import failed: %s", mapImportResultStr(result));
            }
        }

        if (!canImport) {
            ImGui::PopStyleVar();
        }
    }

    ImGui::SameLine();
    if (PdButton("Reset", ImVec2(80.0f * scale, 28.0f * scale))) {
        importReset();
    }

    ImGui::SameLine();
    if (PdButton("Smoke Test", ImVec2(120.0f * scale, 28.0f * scale))) {
        s32 warnings = spawnPoolSmokeTest();
        snprintf(s_MapImpStatus, sizeof(s_MapImpStatus),
                 "Smoke test: %d stage(s) needed L3/L4. Check log.", warnings);
        s_MapImpStatusOk = (warnings == 0);
        s_MapImpDone = true;
    }

    ImGui::SameLine();
    if (PdButton("Run All", ImVec2(100.0f * scale, 28.0f * scale))) {
        s32 warnings = spawnPoolSmokeAll();
        spawnPoolSmokeWriteCSV("Build/smoke-test-results.csv");
        snprintf(s_MapImpStatus, sizeof(s_MapImpStatus),
                 "Smoke sweep done: %d L3/L4 map(s). CSV -> Build/smoke-test-results.csv",
                 warnings);
        s_MapImpStatusOk = (warnings == 0);
        s_MapImpDone = true;
    }

    /* Status / error display */
    if (s_MapImpDone) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (s_MapImpStatusOk) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.4f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        }
        ImGui::TextWrapped("%s", s_MapImpStatus);
        ImGui::PopStyleColor();

        if (!s_MapImpStatusOk && s_MapImpError[0]) {
            ImGui::Spacing();
            ImGui::TextWrapped("Detail: %s", s_MapImpError);
        }

        if (s_MapImpStatusOk && s_MapImpNumRooms > 0) {
            ImGui::Spacing();
            ImGui::Text("Rooms: %d   Pads: %d   Generated Spawns: %d",
                         s_MapImpNumRooms, s_MapImpNumPads, s_MapImpGenSpawns);
        }
    }

    /* Help text */
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Accepts directories containing PD-format map files (.bg, .pad, .setup).");
    ImGui::TextDisabled("Missing spawn points and mod.json will be generated automatically.");
    ImGui::TextDisabled("Imported maps appear in Combat Simulator stage select.");
}

/* ========================================================================
 * Font Mod Tool (Tab 8) — import a .ttf/.otf as a mods/Fonts/<slug>/ entry
 *
 * Mirrors the Menu Style tool's shape: file browser → mod name → Save.
 * Saving copies the source .ttf/.otf into mods/Fonts/<slug>/ alongside a
 * font.json with a human-readable display name, then rescans the font mod
 * registry so the new font appears in the Settings → Interface Font
 * dropdown without a restart. Activating the font itself still takes
 * effect on next app start (ImGui atlas is built once per session).
 * ======================================================================== */

static char  s_FontImgPath[512]   = "";
static char  s_FontModName[96]    = "";
static char  s_FontStatus[256]    = "";
static bool  s_FontStatusOk       = false;

static void fontToolReset(void)
{
    s_FontImgPath[0] = '\0';
    s_FontModName[0] = '\0';
    s_FontStatus[0]  = '\0';
    s_FontStatusOk   = false;
}

static void fontToolSlug(const char *src, char *dst, size_t dstmax)
{
    if (!src || !dst || dstmax == 0) { if (dst && dstmax) dst[0] = '\0'; return; }
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dstmax - 1; i++) {
        char c = src[i];
        if (c == ' ') c = '-';
        else if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) continue;
        dst[j++] = c;
    }
    dst[j] = '\0';
}

static bool fontToolCopyFile(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    FILE *out = fsFileOpenWrite(dst);
    if (!out) { fclose(in); return false; }
    char buf[4096];
    size_t got;
    bool ok = true;
    while ((got = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, got, out) != got) { ok = false; break; }
    }
    if (ferror(in) || ferror(out)) ok = false;
    fclose(in);
    fclose(out);
    return ok;
}

static bool fontToolSave(void)
{
    if (!s_FontImgPath[0]) {
        snprintf(s_FontStatus, sizeof(s_FontStatus),
                 "No source file — pick a .ttf/.otf first");
        s_FontStatusOk = false;
        return false;
    }
    if (!s_FontModName[0]) {
        snprintf(s_FontStatus, sizeof(s_FontStatus),
                 "Give the font mod a name");
        s_FontStatusOk = false;
        return false;
    }

    /* Confirm the source is actually a .ttf/.otf. */
    size_t slen = strlen(s_FontImgPath);
    bool okExt = false;
    if (slen > 4) {
        const char *tail = s_FontImgPath + (slen - 4);
        if ((tail[0] == '.' || tail[0] == 0) &&
            (strcasecmp(tail, ".ttf") == 0 || strcasecmp(tail, ".otf") == 0)) {
            okExt = true;
        }
    }
    if (!okExt) {
        snprintf(s_FontStatus, sizeof(s_FontStatus),
                 "Source must be a .ttf or .otf file");
        s_FontStatusOk = false;
        return false;
    }

    /* Derive slug + destination layout. */
    char slug[64];
    fontToolSlug(s_FontModName, slug, sizeof(slug));
    if (!slug[0]) {
        snprintf(s_FontStatus, sizeof(s_FontStatus),
                 "Invalid mod name — use letters, digits, spaces, or dashes");
        s_FontStatusOk = false;
        return false;
    }

    if (!fsCreateDir("mods")) {
        snprintf(s_FontStatus, sizeof(s_FontStatus), "Could not create mods/");
        s_FontStatusOk = false;
        return false;
    }
    if (!fsCreateDir("mods/Fonts")) {
        snprintf(s_FontStatus, sizeof(s_FontStatus), "Could not create mods/Fonts/");
        s_FontStatusOk = false;
        return false;
    }
    char modDir[FS_MAXPATH];
    snprintf(modDir, sizeof(modDir), "mods/Fonts/%s", slug);
    if (!fsCreateDir(modDir)) {
        snprintf(s_FontStatus, sizeof(s_FontStatus),
                 "Could not create %s", modDir);
        s_FontStatusOk = false;
        return false;
    }

    /* Copy the .ttf/.otf.  Keep the original extension so readers know the
     * format; the basename is normalized to "<slug>.<ext>" so the mod tree
     * is consistent across imports. */
    const char *srcTail = s_FontImgPath + slen;
    while (srcTail > s_FontImgPath && srcTail[-1] != '.' ) srcTail--;
    char extBuf[8];
    snprintf(extBuf, sizeof(extBuf), "%s", srcTail[0] ? srcTail : "ttf");
    for (int i = 0; extBuf[i]; i++) {
        if (extBuf[i] >= 'A' && extBuf[i] <= 'Z') extBuf[i] = (char)(extBuf[i] + 32);
    }

    char dstFont[FS_MAXPATH];
    snprintf(dstFont, sizeof(dstFont), "%s/%s.%s", modDir, slug, extBuf);
    if (!fontToolCopyFile(s_FontImgPath, dstFont)) {
        snprintf(s_FontStatus, sizeof(s_FontStatus),
                 "Could not copy font file to %s", dstFont);
        s_FontStatusOk = false;
        return false;
    }

    /* font.json — gives the mod a display name. */
    char jsonPath[FS_MAXPATH];
    snprintf(jsonPath, sizeof(jsonPath), "%s/font.json", modDir);
    FILE *jf = fsFileOpenWrite(jsonPath);
    if (jf) {
        fprintf(jf,
                "{\n"
                "  \"name\": \"%s\",\n"
                "  \"author\": \"Player\"\n"
                "}\n",
                s_FontModName);
        fclose(jf);
    }

    /* Minimal mod.json so the mod manager lists it. */
    char modJson[FS_MAXPATH];
    snprintf(modJson, sizeof(modJson), "%s/mod.json", modDir);
    FILE *mf = fsFileOpenWrite(modJson);
    if (mf) {
        fprintf(mf,
                "{\n"
                "  \"id\": \"user.%s.font\",\n"
                "  \"name\": \"%s\",\n"
                "  \"version\": \"1.0.0\",\n"
                "  \"description\": \"Font mod created with the Font Mod tool.\",\n"
                "  \"author\": \"Player\",\n"
                "  \"tags\": [\"font\", \"ui\", \"user\"],\n"
                "  \"enabled\": true\n"
                "}\n",
                slug, s_FontModName);
        fclose(mf);
    }

    /* Rescan so the font dropdown picks it up without restart. */
    pdguiFontModRescan();
    modmgrRescanDirectory();

    snprintf(s_FontStatus, sizeof(s_FontStatus),
             "Saved to %s (restart to activate)", modDir);
    s_FontStatusOk = true;
    sysLogPrintf(LOG_NOTE, "MODHUB: font mod saved — slug=%s src=%s",
                 slug, s_FontImgPath);
    return true;
}

static void renderFontTool(float w, float h, float scale)
{
    (void)w; (void)h;

    if (pdguiFileBrowserIsOpen()) {
        if (pdguiFileBrowserRender()) {
            strncpy(s_FontImgPath, pdguiFileBrowserGetPath(),
                    sizeof(s_FontImgPath) - 1);
            s_FontImgPath[sizeof(s_FontImgPath) - 1] = '\0';
            pdguiFileBrowserClose();
        }
        return;
    }

    ImGui::TextDisabled("Font Mod — import a .ttf/.otf file as a font mod");
    ImGui::Spacing();

    ImGui::Text("Source font:");
    float browseW = 90.0f * scale;
    ImGui::SetNextItemWidth(-browseW - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputText("##font_src", s_FontImgPath, sizeof(s_FontImgPath));
    ImGui::SameLine();
    if (PdButton("Browse", ImVec2(browseW, 0))) {
        pdguiFileBrowserOpen("Import Font", "mods", ".ttf;.otf");
    }

    ImGui::Spacing();
    ImGui::Text("Mod name (display):");
    ImGui::SetNextItemWidth(320.0f * scale);
    ImGui::InputText("##font_name", s_FontModName, sizeof(s_FontModName));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    bool canSave = s_FontImgPath[0] && s_FontModName[0];
    if (!canSave) ImGui::BeginDisabled();
    if (PdButton("Save as Font Mod", ImVec2(180.0f * scale, 0))) {
        fontToolSave();
    }
    if (!canSave) ImGui::EndDisabled();

    if (s_FontStatus[0]) {
        ImGui::SameLine();
        ImVec4 col = s_FontStatusOk
            ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
            : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        ImGui::TextColored(col, "%s", s_FontStatus);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Saves to mods/Fonts/<slug>/ — appears in Settings > Interface > Font.");
    ImGui::TextDisabled("Font atlas is built once per session, so activation takes effect on next restart.");
}

/* ========================================================================
 * Public C API
 * ======================================================================== */

extern "C" {

void pdguiModdingHubShow(void)
{
    if (!s_Visible) {
        s_Visible    = true;
        s_ActiveTool = 0;
        s_WeaponTemplateMenuOpen = false;
        s_WeaponMeshPickerOpen = false;
        pdguiModManagerRefreshSnapshot();
        sysLogPrintf(LOG_NOTE, "MODHUB: opened");
    }
}

/* S306: open the Modding Hub on a specific tool index. Used by the
 * Settings → Interface tab to provide "Open Menu Style Editor..." /
 * "Open Skin Editor..." shortcuts that route to the deeper mod tools
 * without requiring the user to navigate through the hub manually. */
void pdguiModdingHubShowTool(s32 tool)
{
    /* Clamp to the known tool range (0..9); out-of-range requests land
     * on Mod Manager rather than an undefined child render. */
    if (tool < 0 || tool > 9) tool = 0;
    s_Visible    = 1;
    s_ActiveTool = tool;
    s_WeaponTemplateMenuOpen = false;
    s_WeaponMeshPickerOpen = false;
    /* Refresh whichever tool we're about to show so its data is live. */
    switch (tool) {
        case 0: pdguiModManagerRefreshSnapshot(); break;
        case 1: iniRefreshEntries();              break;
        case 2: scaleRefreshEntries();            break;
        case 3: packRefreshEntries();             break;
        case 4: pdguiAudioModRefresh();           break;
        case 5: pdguiSkinEditorRefresh();         break;
        case 6: importReset();                    break;
        case 7: chromeToolReset();                break;
        case 8: fontToolReset();                  break;
        case 9: weaponToolRefresh();              break;
    }
    sysLogPrintf(LOG_NOTE, "MODHUB: opened on tool %d", (int)tool);
}

void pdguiModdingHubHide(void)
{
    moddingHubClose("explicit-hide");
}

s32 pdguiModdingHubIsVisible(void)
{
    return s_Visible ? 1 : 0;
}

void pdguiModdingHubRender(s32 winW, s32 winH)
{
    if (!s_Visible) return;

    /* Fullscreen dimming overlay — NoInputs so clicks pass through to the
     * modding hub content.  Click-outside-to-close is handled below by
     * checking if the mouse clicked outside the hub dialog bounds. */
    {
        ImDrawList *bg = ImGui::GetBackgroundDrawList();
        bg->AddRectFilled(ImVec2(0, 0), ImVec2((float)winW, (float)winH),
                          IM_COL32(0, 0, 0, 180));
    }

    renderModdingHub(winW, winH);

    /* Click-outside-to-close: if the user clicks outside the dialog area,
     * dismiss the modding hub.  Uses pdguiMenuPos/Size for the hub bounds. */
    if (!s_WeaponTemplateMenuOpen
            && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)
            && ImGui::IsMouseClicked(0)) {
        ImVec2 mp = ImGui::GetMousePos();
        ImVec2 hubPos  = pdguiMenuPos();
        float  hubW    = pdguiMenuWidth();
        float  hubH    = pdguiMenuHeight();
        if (mp.x < hubPos.x || mp.x > hubPos.x + hubW ||
            mp.y < hubPos.y || mp.y > hubPos.y + hubH) {
            moddingHubClose("outside-click");
        }
    }
}

} /* extern "C" */
