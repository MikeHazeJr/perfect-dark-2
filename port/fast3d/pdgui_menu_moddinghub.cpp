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

#include "imgui/imgui.h"
#include "pdgui_style.h"
#include "pdgui_audio.h"
#include "system.h"
#include "assetcatalog.h"
#include "pdgui_charpreview.h"
#include "fs.h"

/* ========================================================================
 * Forward declarations for C symbols
 * ======================================================================== */

extern "C" {

void modmgrApplyChanges(void);
s32  viGetWidth(void);
s32  viGetHeight(void);
void pdguiDrawButtonEdgeGlow(f32 x, f32 y, f32 w, f32 h, s32 isActive);

/* Mod Manager embedded content (D3R-7 API added to pdgui_menu_modmgr.cpp) */
void pdguiModManagerRefreshSnapshot(void);
void pdguiModManagerRenderContent(float w, float h, float scale, s32 *outClose);

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
static int  s_ActiveTool = 0;    /* 0=ModManager, 1=INI Editor, 2=Model Scale */

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
    for (int t = 0; t < s_NumAllTypes; t++) {
        assetCatalogIterateByType(s_AllTypes[t], iniCollectCallback, &s_IniNumEntries);
    }
    s_IniSelected = -1;
    s_IniNumPairs = 0;
    s_IniDirty    = false;
    s_IniStatusMsg[0] = '\0';
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
        return;
    }

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
                iniLoadFile(i);
            }
        }
        if (sel && ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {
            /* Already selected — navigate into edit panel */
        }
    }

    if (s_IniNumEntries == 0) {
        ImGui::TextDisabled("No mod assets with .ini files found.");
    }

    ImGui::EndChild();

    ImGui::SameLine();

    /* ---- Right panel: editor ---- */
    ImGui::BeginChild("##ini_edit", ImVec2(editW, contentH - footerH), true);

    if (s_IniSelected < 0) {
        ImGui::TextDisabled("Select a mod entry from the list.");
    } else {
        const IniEntry &ie = s_IniEntries[s_IniSelected];
        /* Entry header */
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.85f, 1.0f, 1.0f));
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
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.4f, 1.0f));
            ImGui::Text("%-20s", kv.key);
            ImGui::PopStyleColor();
            ImGui::SameLine();

            char inputId[32];
            snprintf(inputId, sizeof(inputId), "##ini_v%d", i);
            ImGui::SetNextItemWidth(inputW);
            if (ImGui::InputText(inputId, kv.val, HUB_INI_VAL_LEN)) {
                s_IniDirty = true;
            }
        }
    }
    ImGui::EndChild();

    /* ---- Footer: status + save ---- */
    ImGui::Separator();

    if (s_IniStatusMsg[0]) {
        if (s_IniStatusOk) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", s_IniStatusMsg);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", s_IniStatusMsg);
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

static void scaleRefreshEntries(void)
{
    s_ScaleNumEntries = 0;
    assetCatalogIterateByType(ASSET_CHARACTER, scaleCollectCallback, &s_ScaleNumEntries);
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
        ImGui::TextDisabled("No ASSET_CHARACTER entries found.");
    }

    ImGui::EndChild();
    ImGui::SameLine();

    /* ---- Right panel: preview + controls ---- */
    ImGui::BeginChild("##scale_right", ImVec2(rightW, contentH - footerH), true);

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

        /* Request preview render */
        u8 bodynum = (se.runtime_index >= 0 && se.runtime_index < 255)
                     ? (u8)se.runtime_index : 0;
        pdguiCharPreviewSetRotY(s_PreviewRotAngle);
        pdguiCharPreviewRequest(0, bodynum);

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
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.85f, 1.0f, 1.0f));
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
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.5f, 1.0f));
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
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", s_ScaleStatusMsg);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", s_ScaleStatusMsg);
        }
    } else {
        ImGui::TextDisabled("Model Scale Tool — bake scale into model binary");
    }
}

/* ========================================================================
 * Hub renderer
 * ======================================================================== */

static void renderModdingHub(s32 winW, s32 winH)
{
    float scale = (float)winH / 480.0f;
    if (scale < 0.5f) scale = 0.5f;

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

    float dialogW = HUB_DIALOG_W * scale;
    float dialogH = HUB_DIALOG_H * scale;
    float dialogX = ((float)winW - dialogW) * 0.5f;
    float dialogY = ((float)winH - dialogH) * 0.5f;

    /* PD-style border */
    ImDrawList *drawList = ImGui::GetForegroundDrawList();
    ImU32 borderCol = IM_COL32(80, 140, 200, 220);
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

    if (!ImGui::BeginChild("##modhub_inner", ImVec2(dialogW, dialogH), false, innerFlags)) {
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    /* ---- Hub header ---- */
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f * scale);
    ImGui::Text("MODDING");
    ImGui::PopStyleColor();
    ImGui::Separator();

    /* ---- Tool selector bar ---- */
    {
        const float btnW = 160.0f * scale;
        const float btnH = 28.0f * scale;

        static const char *toolNames[] = {
            "Mod Manager", "INI Editor", "Model Scale Tool"
        };

        for (int i = 0; i < 3; i++) {
            if (i > 0) ImGui::SameLine();

            bool active = (s_ActiveTool == i);
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button,
                    ImVec4(0.10f, 0.25f, 0.50f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                    ImVec4(0.15f, 0.35f, 0.65f, 0.95f));
            }

            if (PdButton(toolNames[i], ImVec2(btnW, btnH))) {
                if (s_ActiveTool != i) {
                    s_ActiveTool = i;
                    /* Refresh tool data on switch */
                    if (i == 0) pdguiModManagerRefreshSnapshot();
                    else if (i == 1) iniRefreshEntries();
                    else if (i == 2) scaleRefreshEntries();
                }
            }
            if (active) ImGui::PopStyleColor(2);
        }
    }

    ImGui::Separator();

    /* ---- Content area ---- */
    /* Hub footer: Close button row */
    const float hubFooterH = 38.0f * scale;

    float contentY  = ImGui::GetCursorPosY();
    float contentH  = dialogH - contentY - hubFooterH
                    - ImGui::GetStyle().ItemSpacing.y * 2.0f;

    /* Mod Manager embeds within its own child window */
    if (s_ActiveTool == 0) {
        ImGuiWindowFlags cfFlags =
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoTitleBar;
        if (ImGui::BeginChild("##modhub_modmgr", ImVec2(dialogW, contentH), false, cfFlags)) {
            s32 wantsClose = 0;
            pdguiModManagerRenderContent(dialogW, contentH, scale, &wantsClose);
            if (wantsClose) s_Visible = false;
        }
        ImGui::EndChild();
    } else if (s_ActiveTool == 1) {
        renderIniEditor(dialogW, contentH, scale);
    } else if (s_ActiveTool == 2) {
        renderScaleTool(dialogW, contentH, scale);
    }

    /* ---- Hub footer ---- */
    ImGui::Separator();

    ImGui::SetCursorPosY(dialogH - hubFooterH + ImGui::GetStyle().ItemSpacing.y);

    const char *toolDescs[] = {
        "Enable/disable mod components",
        "Edit mod .ini manifests",
        "Bake model scale to file"
    };
    ImGui::TextDisabled("%s", toolDescs[s_ActiveTool]);

    /* Close button (right-aligned) */
    float closeW = 80.0f * scale;
    float closeH = 28.0f * scale;
    ImGui::SameLine(dialogW - closeW - 8.0f * scale);

    ImGui::PushStyleColor(ImGuiCol_Button,
        ImVec4(0.35f, 0.05f, 0.05f, 0.50f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(0.55f, 0.10f, 0.10f, 0.70f));
    if (ImGui::Button("Close", ImVec2(closeW, closeH))) {
        s_Visible = false;
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive() || ImGui::IsItemFocused()) {
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        pdguiDrawButtonEdgeGlow(rmin.x, rmin.y,
                                rmax.x - rmin.x, rmax.y - rmin.y,
                                ImGui::IsItemActive() ? 1 : 0);
    }
    ImGui::PopStyleColor(2);

    /* B button / Escape closes hub (only when Mod Manager isn't consuming it) */
    if (s_ActiveTool != 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            s_Visible = false;
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
        }
    }

    ImGui::EndChild();
    ImGui::End();
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

void pdguiModdingHubHide(void)
{
    s_Visible = false;
}

s32 pdguiModdingHubIsVisible(void)
{
    return s_Visible ? 1 : 0;
}

void pdguiModdingHubRender(s32 winW, s32 winH)
{
    if (!s_Visible) return;
    renderModdingHub(winW, winH);
}

} /* extern "C" */
