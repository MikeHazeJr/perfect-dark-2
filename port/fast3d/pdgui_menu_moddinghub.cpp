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

#include "glad/glad.h"
#include "imgui/imgui.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_layout.h"
#include "pdgui_theme.h"
#include "pdgui_nineslice.h"
#include "pdgui_filebrowser.h"
#include "pdgui_font_mod.h"
#include "system.h"
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "pdgui_charpreview.h"
#include "fs.h"
#include "modpack.h"
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
static int  s_ActiveTool = 0;    /* 0=ModManager, 1=INI, 2=Scale, 3=Pack, 4=Audio, 5=SkinEditor, 6=MapImport, 7=NineSlice, 8=FontMod */
static int  s_LastLoggedTool = -1;
static int  s_ModHubOpenFrame = -1; /* B-210: frame stamp for open-input debounce */
static void chromeToolReset(void);
static void fontToolReset(void);
static void renderFontTool(float w, float h, float scale);

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
    ASSET_WEAPON, ASSET_TEXTURES, ASSET_SFX, ASSET_MUSIC,
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
        assetCatalogIterateByType(s_AllTypes[t], iniCollectCallback, &s_IniNumEntries);
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
    assetCatalogIterateByType(ASSET_CHARACTER, scaleCollectCallback, &s_ScaleNumEntries);
    assetCatalogIterateByType(ASSET_BODY, scaleCollectBodyCallback, &s_ScaleNumEntries);
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
        assetCatalogIterateByType(s_AllTypes[t], packCollectCallback, &s_PackNumEntries);
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
        ImGui::Checkbox("Session Only (mods/.temp/)", &s_ImportSessionOnly);
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
        ImGui::TextDisabled("Mod Pack — export/import .pdpack files");
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
        static const int NUM_TOOLS = 9;

        static const char *toolNames[] = {
            "Mod Manager", "INI Editor", "Scale Tool", "Mod Pack",
            "Audio Mods", "Skin Editor", "Map Import", "Menu Style",
            "Font Mod"
        };

        /* Bumper (LB/RB) tab cycling — PageUp/PageDown driven by pdguiDriveImGuiNav.
         * Skin Editor uses list navigation heavily; suppress global tab cycling there
         * to avoid stealing selection input from the character list/editor UI. */
        const bool allowHubTabCycle = (s_ActiveTool != 5);
        if (allowHubTabCycle && ImGui::IsKeyPressed(ImGuiKey_PageUp, false)) {
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
            pdguiPlaySound(PDGUI_SND_SWIPE);
        }
        if (allowHubTabCycle && ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) {
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
            "##modhub_import", "##modhub_nineslice", "##modhub_fontmod"
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
        "Import a .ttf/.otf font as a mod"
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
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        if (pdguiConsumeTitleClose()) {
            moddingHubCloseFromUi("title-x-button");
        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
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
    if (fsCreateDir("mods") < 0 && errno != EEXIST) {
        snprintf(s_ChromeStatus, sizeof(s_ChromeStatus), "Could not create mods/");
        s_ChromeStatusOk = false;
        return false;
    }
    if (fsCreateDir("mods/UI Chrome") < 0 && errno != EEXIST) {
        snprintf(s_ChromeStatus, sizeof(s_ChromeStatus), "Could not create mods/UI Chrome/");
        s_ChromeStatusOk = false;
        return false;
    }

    char modDir[FS_MAXPATH];
    snprintf(modDir, sizeof(modDir), "mods/UI Chrome/%s", slug);
    if (fsCreateDir(modDir) < 0 && errno != EEXIST) {
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
    ImGui::InputText("Mod Name", s_ChromeModName, sizeof(s_ChromeModName));
    ImGui::Checkbox("L/R symmetry", &s_ChromeLrSymmetry);
    ImGui::SameLine();
    ImGui::Checkbox("T/B symmetry", &s_ChromeTbSymmetry);
    ImGui::Checkbox("Center tile mode", &s_ChromeCenterTile);
    ImGui::SameLine();
    ImGui::Checkbox("Edge tile mode", &s_ChromeEdgeTile);
    bool editsChanged = false;
    /* S-9 audit fix: cross-clamp trim sliders so the opposite pair can never
     * over-commit the image (which previously yielded degenerate 1px crops
     * with no user feedback). */
    s32 trimLMax = s_ChromeImgW - s_ChromeTrimR - 1; if (trimLMax < 0) trimLMax = 0;
    s32 trimRMax = s_ChromeImgW - s_ChromeTrimL - 1; if (trimRMax < 0) trimRMax = 0;
    s32 trimTMax = s_ChromeImgH - s_ChromeTrimB - 1; if (trimTMax < 0) trimTMax = 0;
    s32 trimBMax = s_ChromeImgH - s_ChromeTrimT - 1; if (trimBMax < 0) trimBMax = 0;
    editsChanged |= ImGui::SliderInt("Trim Left", &s_ChromeTrimL, 0, trimLMax > 0 ? trimLMax : 1);
    editsChanged |= ImGui::SliderInt("Trim Right", &s_ChromeTrimR, 0, trimRMax > 0 ? trimRMax : 1);
    editsChanged |= ImGui::SliderInt("Trim Top", &s_ChromeTrimT, 0, trimTMax > 0 ? trimTMax : 1);
    editsChanged |= ImGui::SliderInt("Trim Bottom", &s_ChromeTrimB, 0, trimBMax > 0 ? trimBMax : 1);

    /* S305: advanced controls moved into a collapsed header to clean up the
     * tool. Scale X/Y, Center Cut, Desaturate, and the per-cut Quick Presets
     * are all power-user options most chrome mods never need — Border Scale
     * + Trim + per-edge insets handle the 90% case. Collapsed by default. */
    if (ImGui::CollapsingHeader("Advanced")) {
        editsChanged |= ImGui::SliderInt("Scale X", &s_ChromeScaleXPct, 10, 400, "%d%%");
        editsChanged |= ImGui::SliderInt("Scale Y", &s_ChromeScaleYPct, 10, 400, "%d%%");
        static const char *cutAxes[] = { "None", "Vertical (height)", "Horizontal (width)" };
        editsChanged |= ImGui::Combo("Center Cut Axis", &s_ChromeCenterCutAxis, cutAxes, 3);
        if (s_ChromeCenterCutAxis != 0) {
            editsChanged |= ImGui::SliderInt("Center Cut %", &s_ChromeCenterCutPct, 0, 90, "%d%%");
        } else {
            s_ChromeCenterCutPct = 0;
        }

        bool desatChanged = ImGui::Checkbox("Desaturate for tint-friendly chrome", &s_ChromeDesaturate);
        if (s_ChromeDesaturate) {
            desatChanged |= ImGui::SliderInt("Desaturate %", &s_ChromeDesaturatePct, 0, 100, "%d%%");
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
    ImGui::SliderFloat("Border Scale", &s_ChromeBorderScale, 0.25f, 4.0f, "%.2fx");
    ImGui::Checkbox("Proportional Insets (%% of output)", &s_ChromeProportionalInsets);
    ImGui::SameLine();
    ImGui::TextDisabled("(%%)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("When on, inset sliders are percentages of the output dims\n"
                          "and track Scale X/Y, keeping borders proportional.\n"
                          "When off, sliders are absolute pixels in the output.");
    }

    if (s_ChromeProportionalInsets) {
        bool insetChanged = false;
        if (ImGui::SliderFloat("Left %",  &s_ChromeInsetLPct, 0.0f, 50.0f, "%.2f%%")) {
            if (s_ChromeLrSymmetry) s_ChromeInsetRPct = s_ChromeInsetLPct;
            insetChanged = true;
        }
        if (ImGui::SliderFloat("Right %", &s_ChromeInsetRPct, 0.0f, 50.0f, "%.2f%%")) {
            if (s_ChromeLrSymmetry) s_ChromeInsetLPct = s_ChromeInsetRPct;
            insetChanged = true;
        }
        if (ImGui::SliderFloat("Top %",   &s_ChromeInsetTPct, 0.0f, 50.0f, "%.2f%%")) {
            if (s_ChromeTbSymmetry) s_ChromeInsetBPct = s_ChromeInsetTPct;
            insetChanged = true;
        }
        if (ImGui::SliderFloat("Bottom %",&s_ChromeInsetBPct, 0.0f, 50.0f, "%.2f%%")) {
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
        if (ImGui::SliderInt("Left", &s_ChromeInsetL, 0, maxL)) {
            if (s_ChromeLrSymmetry) s_ChromeInsetR = s_ChromeInsetL;
        }
        if (ImGui::SliderInt("Right", &s_ChromeInsetR, 0, maxL)) {
            if (s_ChromeLrSymmetry) s_ChromeInsetL = s_ChromeInsetR;
        }
        if (ImGui::SliderInt("Top", &s_ChromeInsetT, 0, maxT)) {
            if (s_ChromeTbSymmetry) s_ChromeInsetB = s_ChromeInsetT;
        }
        if (ImGui::SliderInt("Bottom", &s_ChromeInsetB, 0, maxT)) {
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

    if (fsCreateDir("mods") < 0 && errno != EEXIST) {
        snprintf(s_FontStatus, sizeof(s_FontStatus), "Could not create mods/");
        s_FontStatusOk = false;
        return false;
    }
    if (fsCreateDir("mods/Fonts") < 0 && errno != EEXIST) {
        snprintf(s_FontStatus, sizeof(s_FontStatus), "Could not create mods/Fonts/");
        s_FontStatusOk = false;
        return false;
    }
    char modDir[FS_MAXPATH];
    snprintf(modDir, sizeof(modDir), "mods/Fonts/%s", slug);
    if (fsCreateDir(modDir) < 0 && errno != EEXIST) {
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
    /* Clamp to the known tool range (0..8); out-of-range requests land
     * on Mod Manager rather than an undefined child render. */
    if (tool < 0 || tool > 8) tool = 0;
    s_Visible    = 1;
    s_ActiveTool = tool;
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
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && ImGui::IsMouseClicked(0)) {
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
