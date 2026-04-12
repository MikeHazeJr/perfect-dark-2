/**
 * pdgui_filebrowser.cpp -- In-engine ImGui file browser dialog.
 *
 * Shared component used by audio import (pdgui_menu_audiomod.cpp) and
 * skin image import (pdgui_skin_editor.cpp). Pure ImGui — no native OS
 * dialogs, fully controller-navigable.
 *
 * Features:
 *   - Directory listing with file size and type columns
 *   - Extension filtering (configurable per open call)
 *   - Parent directory navigation (.. entry, B button)
 *   - Enter subdirectories (A button / double-click)
 *   - Confirm / Cancel button pair
 *   - D-pad scrollable list, A to select/enter, B to go back/cancel
 *
 * IMPORTANT: C++ file — must NOT include types.h (#define bool s32 breaks C++).
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/ultratypes.h>

#include "imgui/imgui.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "fs.h"

/* ========================================================================
 * Forward declarations (C symbols)
 * ======================================================================== */

extern "C" {
void pdguiDrawButtonEdgeGlow(f32 x, f32 y, f32 w, f32 h, s32 isActive);
void sysLogPrintf(s32 level, const char *fmt, ...);
} /* extern "C" */

#ifndef LOG_NOTE
#define LOG_NOTE    2
#define LOG_WARNING 3
#endif

/* ========================================================================
 * PD-style button (consistent with other menus)
 * ======================================================================== */

static bool PdButtonFB(const char *label, const ImVec2 &size = ImVec2(0,0))
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
 * Constants
 * ======================================================================== */

#define FB_MAX_ENTRIES  512
#define FB_PATH_LEN     FS_MAXPATH
#define FB_NAME_LEN     256
#define FB_TITLE_LEN    128
#define FB_FILTER_LEN   256
#define FB_MAX_FILTERS  16

/* ========================================================================
 * Types
 * ======================================================================== */

struct FbEntry {
    char name[FB_NAME_LEN];
    char fullpath[FB_PATH_LEN];
    s64  size;          /* -1 for directories */
    s32  isDir;
};

/* ========================================================================
 * State
 * ======================================================================== */

static bool  s_Open = false;
static bool  s_Confirmed = false;
static char  s_Title[FB_TITLE_LEN] = "";
static char  s_CurrentDir[FB_PATH_LEN] = "";
static char  s_ResultPath[FB_PATH_LEN] = "";
static char  s_FilterStr[FB_FILTER_LEN] = "";
static char  s_FilterExts[FB_MAX_FILTERS][16];
static s32   s_NumFilters = 0;
static s32   s_Selected = -1;

static FbEntry s_Entries[FB_MAX_ENTRIES];
static s32     s_NumEntries = 0;

/* ========================================================================
 * Helpers — extension matching
 * ======================================================================== */

static void parseFilters(const char *filters)
{
    s_NumFilters = 0;
    if (!filters || !filters[0]) return;

    /* Parse semicolon-separated list: ".mp3;.wav;.ogg" */
    const char *p = filters;
    while (*p && s_NumFilters < FB_MAX_FILTERS) {
        /* Skip leading semicolons/whitespace */
        while (*p == ';' || *p == ' ') p++;
        if (!*p) break;

        const char *start = p;
        while (*p && *p != ';') p++;

        int len = (int)(p - start);
        if (len > 0 && len < 16) {
            memcpy(s_FilterExts[s_NumFilters], start, len);
            s_FilterExts[s_NumFilters][len] = '\0';
            /* Lowercase for case-insensitive matching */
            for (int i = 0; i < len; i++) {
                char c = s_FilterExts[s_NumFilters][i];
                if (c >= 'A' && c <= 'Z') s_FilterExts[s_NumFilters][i] = c + 32;
            }
            s_NumFilters++;
        }
    }
}

static bool matchesFilter(const char *filename)
{
    if (s_NumFilters == 0) return true;  /* No filter = show all */

    /* Find the last '.' in filename */
    const char *dot = NULL;
    for (const char *p = filename; *p; p++) {
        if (*p == '.') dot = p;
    }
    if (!dot) return false;

    /* Lowercase extension for comparison */
    char ext[16];
    int len = 0;
    for (const char *p = dot; *p && len < 15; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = c + 32;
        ext[len++] = c;
    }
    ext[len] = '\0';

    for (s32 i = 0; i < s_NumFilters; i++) {
        if (strcmp(ext, s_FilterExts[i]) == 0) return true;
    }
    return false;
}

/* ========================================================================
 * Helpers — file size formatting
 * ======================================================================== */

static const char *formatSize(s64 bytes, char *buf, int bufLen)
{
    if (bytes < 0) {
        snprintf(buf, bufLen, "<DIR>");
    } else if (bytes < 1024) {
        snprintf(buf, bufLen, "%d B", (int)bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buf, bufLen, "%.1f KB", bytes / 1024.0);
    } else {
        snprintf(buf, bufLen, "%.1f MB", bytes / (1024.0 * 1024.0));
    }
    return buf;
}

/* ========================================================================
 * Helpers — directory scanning
 * ======================================================================== */

static int entryCompare(const void *a, const void *b)
{
    const FbEntry *ea = (const FbEntry *)a;
    const FbEntry *eb = (const FbEntry *)b;

    /* Directories first */
    if (ea->isDir != eb->isDir) return eb->isDir - ea->isDir;

    /* Alphabetical (case-insensitive) */
#ifdef _WIN32
    return _stricmp(ea->name, eb->name);
#else
    return strcasecmp(ea->name, eb->name);
#endif
}

static void scanDirectory(const char *dir)
{
    s_NumEntries = 0;
    s_Selected = -1;

    if (!dir || !dir[0]) return;

#ifdef _WIN32
    /* Windows: FindFirstFile / FindNextFile */
    char pattern[FB_PATH_LEN];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        sysLogPrintf(LOG_WARNING, "filebrowser: cannot scan '%s'", dir);
        return;
    }

    do {
        if (s_NumEntries >= FB_MAX_ENTRIES) break;

        /* Skip "." entry */
        if (strcmp(fd.cFileName, ".") == 0) continue;

        FbEntry *e = &s_Entries[s_NumEntries];
        strncpy(e->name, fd.cFileName, FB_NAME_LEN - 1);
        e->name[FB_NAME_LEN - 1] = '\0';

        snprintf(e->fullpath, FB_PATH_LEN, "%s/%s", dir, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            e->isDir = 1;
            e->size = -1;
            s_NumEntries++;
        } else {
            /* Check extension filter for files */
            if (matchesFilter(fd.cFileName)) {
                e->isDir = 0;
                e->size = ((s64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                s_NumEntries++;
            }
        }
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);

#else
    /* POSIX: opendir / readdir */
    DIR *d = opendir(dir);
    if (!d) {
        sysLogPrintf(LOG_WARNING, "filebrowser: cannot scan '%s'", dir);
        return;
    }

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (s_NumEntries >= FB_MAX_ENTRIES) break;

        /* Skip "." entry */
        if (strcmp(de->d_name, ".") == 0) continue;

        FbEntry *e = &s_Entries[s_NumEntries];
        strncpy(e->name, de->d_name, FB_NAME_LEN - 1);
        e->name[FB_NAME_LEN - 1] = '\0';

        snprintf(e->fullpath, FB_PATH_LEN, "%s/%s", dir, de->d_name);

        struct stat st;
        if (stat(e->fullpath, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                e->isDir = 1;
                e->size = -1;
                s_NumEntries++;
            } else if (matchesFilter(de->d_name)) {
                e->isDir = 0;
                e->size = (s64)st.st_size;
                s_NumEntries++;
            }
        }
    }

    closedir(d);
#endif

    /* Sort: directories first, then alphabetical */
    if (s_NumEntries > 0) {
        qsort(s_Entries, s_NumEntries, sizeof(FbEntry), entryCompare);
    }
}

static void navigateToDir(const char *dir)
{
    /* Normalize path separators to forward slashes */
    strncpy(s_CurrentDir, dir, FB_PATH_LEN - 1);
    s_CurrentDir[FB_PATH_LEN - 1] = '\0';

    for (char *p = s_CurrentDir; *p; p++) {
        if (*p == '\\') *p = '/';
    }

    /* Remove trailing slash (unless it's the root) */
    int len = (int)strlen(s_CurrentDir);
    while (len > 1 && s_CurrentDir[len - 1] == '/') {
        s_CurrentDir[--len] = '\0';
    }

    scanDirectory(s_CurrentDir);
}

/* ========================================================================
 * Helpers — resolve initial directory to absolute path
 * ======================================================================== */

static void resolveStartDir(const char *startDir)
{
    if (!startDir || !startDir[0]) {
        startDir = "mods";
    }

    /* If relative, prepend CWD */
    if (startDir[0] != '/' && !(startDir[0] && startDir[1] == ':')) {
        char cwd[FB_PATH_LEN];
#ifdef _WIN32
        if (_getcwd(cwd, sizeof(cwd))) {
#else
        if (getcwd(cwd, sizeof(cwd))) {
#endif
            char full[FB_PATH_LEN];
            snprintf(full, sizeof(full), "%s/%s", cwd, startDir);
            navigateToDir(full);
            return;
        }
    }

    navigateToDir(startDir);
}

/* ========================================================================
 * Public API
 * ======================================================================== */

extern "C" {

void pdguiFileBrowserOpen(const char *title, const char *startDir, const char *filters)
{
    s_Open = true;
    s_Confirmed = false;
    s_ResultPath[0] = '\0';
    s_Selected = -1;

    strncpy(s_Title, title ? title : "Browse Files", FB_TITLE_LEN - 1);
    s_Title[FB_TITLE_LEN - 1] = '\0';

    strncpy(s_FilterStr, filters ? filters : "", FB_FILTER_LEN - 1);
    s_FilterStr[FB_FILTER_LEN - 1] = '\0';

    parseFilters(filters);
    resolveStartDir(startDir);

    sysLogPrintf(LOG_NOTE, "filebrowser: opened '%s' at '%s' filter='%s'",
                 s_Title, s_CurrentDir, s_FilterStr);
}

void pdguiFileBrowserClose(void)
{
    s_Open = false;
    s_Confirmed = false;
    s_NumEntries = 0;
    s_Selected = -1;
}

s32 pdguiFileBrowserIsOpen(void)
{
    return s_Open;
}

const char *pdguiFileBrowserGetPath(void)
{
    return s_ResultPath;
}

s32 pdguiFileBrowserRender(void)
{
    if (!s_Open) return 0;

    /* Reset confirmed flag each frame */
    s_Confirmed = false;

    ImGui::OpenPopup(s_Title);

    float scale = pdguiScaleFactor();
    float winW = 600.0f * scale;
    float winH = 450.0f * scale;

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(winW, winH));

    if (!ImGui::BeginPopupModal(s_Title, &s_Open,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
        return 0;
    }

    /* ---- Current path display ---- */
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.85f, 1.0f, 1.0f));
        ImGui::TextWrapped("%s", s_CurrentDir);
        ImGui::PopStyleColor();
    }

    /* ---- Filter display ---- */
    if (s_FilterStr[0]) {
        ImGui::SameLine();
        ImGui::TextDisabled("  [%s]", s_FilterStr);
    }

    ImGui::Separator();

    /* ---- File list ---- */
    float listH = winH - 130.0f * scale;
    ImGui::BeginChild("##fb_list", ImVec2(-1, listH), true,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    /* Controller: B button navigates to parent */
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        /* Navigate to parent directory */
        char parent[FB_PATH_LEN];
        strncpy(parent, s_CurrentDir, FB_PATH_LEN);
        char *lastSlash = NULL;
        for (char *p = parent; *p; p++) {
            if (*p == '/') lastSlash = p;
        }
        /* Don't navigate above drive root (e.g. C:/) */
        if (lastSlash && lastSlash != parent &&
            !(lastSlash - parent == 2 && parent[1] == ':')) {
            *lastSlash = '\0';
            navigateToDir(parent);
            pdguiPlaySound(PDGUI_SND_SWIPE);
        }
    }

    for (s32 i = 0; i < s_NumEntries; i++) {
        const FbEntry *e = &s_Entries[i];

        /* Build display label */
        char label[FB_NAME_LEN + 32];
        char sizeBuf[32];

        if (e->isDir) {
            snprintf(label, sizeof(label), "[DIR] %s##fb%d", e->name, i);
        } else {
            formatSize(e->size, sizeBuf, sizeof(sizeBuf));
            snprintf(label, sizeof(label), "%s  (%s)##fb%d", e->name, sizeBuf, i);
        }

        bool sel = (s_Selected == i);

        /* Color directories differently */
        if (e->isDir) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.4f, 1.0f));
        }

        if (ImGui::Selectable(label, sel, ImGuiSelectableFlags_AllowDoubleClick)) {
            s_Selected = i;
            pdguiPlaySound(PDGUI_SND_FOCUS);

            /* Double-click or controller A: enter directory or confirm file */
            if (ImGui::IsMouseDoubleClicked(0) ||
                ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {
                if (e->isDir) {
                    navigateToDir(e->fullpath);
                    pdguiPlaySound(PDGUI_SND_OPENDIALOG);
                } else {
                    strncpy(s_ResultPath, e->fullpath, FB_PATH_LEN - 1);
                    s_ResultPath[FB_PATH_LEN - 1] = '\0';
                    s_Confirmed = true;
                    s_Open = false;
                    pdguiPlaySound(PDGUI_SND_SELECT);
                }
            }
        }

        /* Controller A on focused item (not via double-click) */
        if (sel && ImGui::IsItemFocused() &&
            ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {
            if (e->isDir) {
                navigateToDir(e->fullpath);
                pdguiPlaySound(PDGUI_SND_OPENDIALOG);
            } else {
                strncpy(s_ResultPath, e->fullpath, FB_PATH_LEN - 1);
                s_ResultPath[FB_PATH_LEN - 1] = '\0';
                s_Confirmed = true;
                s_Open = false;
                pdguiPlaySound(PDGUI_SND_SELECT);
            }
        }

        if (e->isDir) {
            ImGui::PopStyleColor();
        }
    }

    if (s_NumEntries == 0) {
        ImGui::TextDisabled("(empty directory)");
    }

    ImGui::EndChild();

    /* ---- Selected file display ---- */
    if (s_Selected >= 0 && s_Selected < s_NumEntries && !s_Entries[s_Selected].isDir) {
        ImGui::Text("Selected: %s", s_Entries[s_Selected].name);
    } else {
        ImGui::TextDisabled("No file selected");
    }

    /* ---- Confirm / Cancel buttons ---- */
    ImGui::Spacing();

    float btnW = 120.0f * scale;
    float totalBtnW = btnW * 2.0f + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - totalBtnW) * 0.5f);

    bool hasSelection = (s_Selected >= 0 && s_Selected < s_NumEntries &&
                         !s_Entries[s_Selected].isDir);

    if (!hasSelection) ImGui::BeginDisabled();
    if (PdButtonFB("Confirm##fb", ImVec2(btnW, 0))) {
        if (hasSelection) {
            strncpy(s_ResultPath, s_Entries[s_Selected].fullpath, FB_PATH_LEN - 1);
            s_ResultPath[FB_PATH_LEN - 1] = '\0';
            s_Confirmed = true;
            s_Open = false;
        }
    }
    if (!hasSelection) ImGui::EndDisabled();

    ImGui::SameLine();
    if (PdButtonFB("Cancel##fb", ImVec2(btnW, 0))) {
        s_Open = false;
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
    }

    ImGui::EndPopup();

    return s_Confirmed;
}

} /* extern "C" */
