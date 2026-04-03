/**
 * pdgui_menu_logviewer.cpp -- Dev Window Log Viewer tab for Perfect Dark 2.
 *
 * A filterable, color-coded log viewer that reads from the structured ring buffer
 * (LogEntry) in system.c.  Opened as a separate window alongside the F12 debug
 * menu.  Replaces the LOG FILTERS section that was removed from pdgui_debugmenu.cpp.
 *
 * Features:
 *   - Channel filter checkboxes (12 channels + All / None presets)
 *   - Severity filter checkboxes (VERBOSE, NOTE, WARNING, ERROR, CHAT)
 *   - Case-insensitive substring text search
 *   - Scrollable log display with color-coded severity
 *   - Auto-scroll toggle (pauses when user scrolls up)
 *   - "Export Filtered" button -> pd-log-export-TIMESTAMP.txt
 *   - Status bar showing "Showing N of M entries"
 *   - Verbose toggle (forwarded to sysLogSetVerbose)
 *
 * IMPORTANT: C++ file -- must NOT include types.h (#define bool s32 breaks C++).
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#include <PR/ultratypes.h>
#include "imgui/imgui.h"

#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "system.h"

/* -----------------------------------------------------------------------
 * Portable case-insensitive substring search (strcasestr not in MinGW C++)
 * ----------------------------------------------------------------------- */

static const char *casestrstr(const char *haystack, const char *needle)
{
    if (!needle || !needle[0]) return haystack;
    size_t nlen = strlen(needle);
    for (; *haystack; haystack++) {
        if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
            size_t i;
            for (i = 1; i < nlen; i++) {
                if (!haystack[i]) return NULL;
                if (tolower((unsigned char)haystack[i]) != tolower((unsigned char)needle[i])) break;
            }
            if (i == nlen) return haystack;
        }
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * State
 * ----------------------------------------------------------------------- */

static bool  s_ChannelFilter[LOG_CH_COUNT];  /* per-channel enable flags */
static bool  s_LevelFilter[5];               /* LOG_VERBOSE..LOG_CHAT */
static char  s_SearchBuf[128];
static bool  s_AutoScroll = true;
static bool  s_Initialized = false;

static void logviewerEnsureInit(void)
{
    if (s_Initialized) return;
    for (int i = 0; i < LOG_CH_COUNT; i++) s_ChannelFilter[i] = true;
    for (int i = 0; i < 5; i++)           s_LevelFilter[i]    = true;
    s_SearchBuf[0]  = '\0';
    s_AutoScroll    = true;
    s_Initialized   = true;
}

/* -----------------------------------------------------------------------
 * Entry matching
 * ----------------------------------------------------------------------- */

static bool entryMatchesLevel(const LogEntry *e)
{
    int lvl = e->level;
    if (lvl < 0 || lvl > 4) return true;   /* unknown levels always pass */
    return s_LevelFilter[lvl];
}

static bool entryMatchesChannel(const LogEntry *e)
{
    /* Untagged messages (channel == 0) always pass */
    if (e->channel == 0) return true;
    for (int i = 0; i < LOG_CH_COUNT; i++) {
        if ((e->channel & sysLogChannelBits[i]) && s_ChannelFilter[i]) {
            return true;
        }
    }
    return false;
}

/* Case-insensitive substring search using strcasestr (GNU extension) */
static bool entryMatchesSearch(const LogEntry *e)
{
    if (s_SearchBuf[0] == '\0') return true;
    return casestrstr(e->text, s_SearchBuf) != NULL;
}

static bool entryPasses(const LogEntry *e)
{
    return entryMatchesLevel(e) && entryMatchesChannel(e) && entryMatchesSearch(e);
}

/* -----------------------------------------------------------------------
 * Severity color
 * ----------------------------------------------------------------------- */

static ImVec4 levelColor(int lvl)
{
    switch (lvl) {
        case LOG_VERBOSE: return ImVec4(0.50f, 0.50f, 0.50f, 1.0f); /* gray    */
        case LOG_NOTE:    return ImVec4(0.90f, 0.90f, 0.90f, 1.0f); /* white   */
        case LOG_WARNING: return ImVec4(1.00f, 0.80f, 0.20f, 1.0f); /* yellow  */
        case LOG_ERROR:   return ImVec4(1.00f, 0.30f, 0.30f, 1.0f); /* red     */
        case LOG_CHAT:    return ImVec4(0.40f, 0.90f, 1.00f, 1.0f); /* cyan    */
        default:          return ImVec4(0.80f, 0.80f, 0.80f, 1.0f);
    }
}

/* -----------------------------------------------------------------------
 * Export
 * ----------------------------------------------------------------------- */

static void doExport(void)
{
    char fname[256];
    {
        time_t t = time(NULL);
        struct tm *tm = localtime(&t);
        strftime(fname, sizeof(fname), "pd-log-export-%Y%m%d-%H%M%S.txt", tm);
    }

    FILE *f = fopen(fname, "wb");
    if (!f) {
        sysLogPrintf(LOG_WARNING, "LOGVIEWER: export failed to open %s", fname);
        return;
    }

    /* Header showing active filter state */
    fprintf(f, "# PD2 Log Export\n");
    fprintf(f, "# Channels: ");
    bool anyChannel = false;
    for (int i = 0; i < LOG_CH_COUNT; i++) {
        if (s_ChannelFilter[i]) {
            if (anyChannel) fprintf(f, ", ");
            fprintf(f, "%s", sysLogChannelNames[i]);
            anyChannel = true;
        }
    }
    if (!anyChannel) fprintf(f, "(none)");
    fprintf(f, "\n");
    fprintf(f, "# Levels: ");
    static const char *lvlNames[] = { "VERBOSE", "NOTE", "WARNING", "ERROR", "CHAT" };
    bool anyLevel = false;
    for (int i = 0; i < 5; i++) {
        if (s_LevelFilter[i]) {
            if (anyLevel) fprintf(f, ", ");
            fprintf(f, "%s", lvlNames[i]);
            anyLevel = true;
        }
    }
    if (!anyLevel) fprintf(f, "(none)");
    fprintf(f, "\n");
    if (s_SearchBuf[0]) fprintf(f, "# Search: \"%s\"\n", s_SearchBuf);
    fprintf(f, "#\n");

    int total = sysLogEntryGetCount();
    for (int i = 0; i < total; i++) {
        const LogEntry *e = sysLogEntryGet(i);
        if (!e || !entryPasses(e)) continue;
        static const char *pfx[] = { "VERBOSE: ", "", "WARNING: ", "ERROR: ", "CHAT: " };
        int lvl = e->level;
        const char *p = (lvl >= 0 && lvl < 5) ? pfx[lvl] : "";
        int mins = (int)(e->timestamp / 60.0f);
        float secs = e->timestamp - (mins * 60.0f);
        fprintf(f, "[%02d:%05.2f] %s%s\n", mins, secs, p, e->text);
    }

    fclose(f);
    sysLogPrintf(LOG_NOTE, "LOGVIEWER: exported filtered log to %s", fname);
}

/* -----------------------------------------------------------------------
 * Main render entry point
 * ----------------------------------------------------------------------- */

extern "C" void pdguiLogViewerRender(s32 winW, s32 winH)
{
    logviewerEnsureInit();

    /* Position: left side of screen, matching debug menu margin */
    float scale = pdguiScaleFactor();
    /* Approximate scale from window height (480 = reference) */
    float uiScale = (float)winH / 480.0f;
    if (uiScale < 0.5f) uiScale = 0.5f;
    if (uiScale > 4.0f) uiScale = 4.0f;

    float margin  = 12.0f * uiScale;
    float winWf   = 560.0f * uiScale;
    float winHf   = (float)winH - margin * 2.0f;

    ImGui::SetNextWindowPos(ImVec2(margin, margin), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(winWf, winHf), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove   |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse;

    if (!ImGui::Begin("Log Viewer", nullptr, flags)) {
        ImGui::End();
        return;
    }

    /* ---- Filter controls ---- */

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::Text("CHANNELS");
    ImGui::PopStyleColor();

    /* All / None presets */
    u32 mask = sysLogGetChannelMask();
    bool isAll  = (mask == LOG_CH_ALL);
    bool isNone = (mask == LOG_CH_NONE);

    if (isAll) ImGui::PushStyleColor(ImGuiCol_Button,
        ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);
    if (ImGui::Button("All##ch", ImVec2(50.0f * uiScale, 0))) {
        for (int i = 0; i < LOG_CH_COUNT; i++) s_ChannelFilter[i] = true;
        sysLogSetChannelMask(LOG_CH_ALL);
    }
    if (isAll) ImGui::PopStyleColor();

    ImGui::SameLine();

    if (isNone) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 0.85f));
    if (ImGui::Button("None##ch", ImVec2(50.0f * uiScale, 0))) {
        for (int i = 0; i < LOG_CH_COUNT; i++) s_ChannelFilter[i] = false;
        sysLogSetChannelMask(LOG_CH_NONE);
    }
    if (isNone) ImGui::PopStyleColor();

    ImGui::Spacing();

    /* Channel checkboxes — two per row */
    bool maskChanged = false;
    for (int i = 0; i < LOG_CH_COUNT; i++) {
        bool en = s_ChannelFilter[i];
        if (ImGui::Checkbox(sysLogChannelNames[i], &en)) {
            s_ChannelFilter[i] = en;
            maskChanged = true;
        }
        if (i % 3 != 2 && i + 1 < LOG_CH_COUNT) ImGui::SameLine(0.0f, 8.0f * uiScale);
    }
    if (maskChanged) {
        u32 newMask = 0;
        for (int i = 0; i < LOG_CH_COUNT; i++) {
            if (s_ChannelFilter[i]) newMask |= sysLogChannelBits[i];
        }
        sysLogSetChannelMask(newMask);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* Severity checkboxes */
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::Text("SEVERITY");
    ImGui::PopStyleColor();

    static const char *lvlLabels[] = { "Verbose", "Note", "Warning", "Error", "Chat" };
    static const ImVec4 lvlColors[] = {
        ImVec4(0.50f, 0.50f, 0.50f, 1.0f),
        ImVec4(0.90f, 0.90f, 0.90f, 1.0f),
        ImVec4(1.00f, 0.80f, 0.20f, 1.0f),
        ImVec4(1.00f, 0.30f, 0.30f, 1.0f),
        ImVec4(0.40f, 0.90f, 1.00f, 1.0f),
    };
    for (int i = 0; i < 5; i++) {
        ImGui::PushStyleColor(ImGuiCol_Text, lvlColors[i]);
        ImGui::Checkbox(lvlLabels[i], &s_LevelFilter[i]);
        ImGui::PopStyleColor();
        if (i + 1 < 5) ImGui::SameLine(0.0f, 8.0f * uiScale);
    }

    ImGui::Spacing();

    /* Verbose global toggle */
    bool verbose = sysLogGetVerbose() != 0;
    if (ImGui::Checkbox("Verbose Logging", &verbose)) {
        sysLogSetVerbose(verbose ? 1 : 0);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* Search box */
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::Text("SEARCH");
    ImGui::PopStyleColor();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##search", s_SearchBuf, sizeof(s_SearchBuf));

    ImGui::Spacing();

    /* Auto-scroll + export row */
    ImGui::Checkbox("Auto-scroll", &s_AutoScroll);
    ImGui::SameLine();
    if (ImGui::Button("Export Filtered")) {
        doExport();
    }

    ImGui::Spacing();
    ImGui::Separator();

    /* ---- Log display ---- */
    int total   = sysLogEntryGetCount();
    int showing = 0;

    /* Count matching entries for status bar */
    for (int i = 0; i < total; i++) {
        const LogEntry *e = sysLogEntryGet(i);
        if (e && entryPasses(e)) showing++;
    }

    /* Reserve space for status bar at bottom */
    float statusH = ImGui::GetTextLineHeightWithSpacing() + 4.0f;
    ImGui::BeginChild("LogScroll",
        ImVec2(0.0f, -statusH),
        false,
        ImGuiWindowFlags_HorizontalScrollbar);

    for (int i = 0; i < total; i++) {
        const LogEntry *e = sysLogEntryGet(i);
        if (!e || !entryPasses(e)) continue;

        ImGui::PushStyleColor(ImGuiCol_Text, levelColor(e->level));
        ImGui::TextUnformatted(e->text);
        ImGui::PopStyleColor();
    }

    /* Auto-scroll: jump to bottom if enabled and user hasn't scrolled up */
    if (s_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) {
        ImGui::SetScrollHereY(1.0f);
    }
    /* If user scrolled up, disable auto-scroll until they re-enable it */
    if (ImGui::GetScrollY() < ImGui::GetScrollMaxY() - 40.0f) {
        s_AutoScroll = false;
    }

    ImGui::EndChild();

    /* Status bar */
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    ImGui::Text("Showing %d of %d entries", showing, total);
    ImGui::PopStyleColor();

    ImGui::End();

    (void)winW;
    (void)scale;
}
