/**
 * pdgui_menu_stats.cpp -- M2.3 Stats Viewer UI
 *
 * Shows lifetime player statistics: overall combat stats, per-weapon
 * breakdown, per-mode breakdown, and achievement progress.
 * Accessed from the main menu "Stats" button.
 *
 * Auto-discovered by CMakeLists.txt GLOB_RECURSE port/fast3d/*.cpp.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "imgui/imgui.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_layout.h"
#include "system.h"

extern "C" {
    /* Persistent stats API */
    u64 statGet(const char *key);
    s32 statsGetCount(void);
    const char *statsGetKeyByIndex(s32 idx);
    u64 statsGetValueByIndex(s32 idx);

    /* Achievements API */
    s32 achievementGetCount(void);
    struct achievement_def_t;
    typedef struct {
        const char *id;
        const char *name;
        const char *description;
        s32 condition;
        u64 threshold;
        const char *stat_key;
    } achievement_def_t_cpp;
    const void *achievementGetByIndex(s32 idx);
    s32 achievementIsUnlocked(const char *id);

    /* Language */
    const char *langSafe(s32 textid);
}

/* ========================================================================
 * State
 * ======================================================================== */

static s32 s_StatsTab = 0;  /* 0=Overview, 1=Weapons, 2=Modes, 3=Achievements */
static bool s_StatsOpen = false;

/* Weapon table for per-weapon display */
static const struct { const char *key_suffix; const char *display; } s_WeaponDisplay[] = {
    { "falcon2",        "Falcon 2" },
    { "falcon2_sil",    "Falcon 2 (Silencer)" },
    { "falcon2_scope",  "Falcon 2 (Scope)" },
    { "magsec4",        "MagSec 4" },
    { "mauler",         "Mauler" },
    { "phoenix",        "Phoenix" },
    { "dy357",          "DY357 Magnum" },
    { "dy357lx",        "DY357-LX" },
    { "cmp150",         "CMP150" },
    { "cyclone",        "Cyclone" },
    { "callisto",       "Callisto NTG" },
    { "rcp120",         "RCP-120" },
    { "laptopgun",      "Laptop Gun" },
    { "dragon",         "Dragon" },
    { "k7avenger",      "K7 Avenger" },
    { "ar34",           "AR34" },
    { "superdragon",    "SuperDragon" },
    { "shotgun",        "Shotgun" },
    { "reaper",         "Reaper" },
    { "sniperrifle",    "Sniper Rifle" },
    { "farsight",       "Farsight XR-20" },
    { "devastator",     "Devastator" },
    { "rocketlauncher", "Rocket Launcher" },
    { "slayer",         "Slayer" },
    { "combatknife",    "Combat Knife" },
    { "crossbow",       "Crossbow" },
    { "tranquilizer",   "Tranquilizer" },
    { "grenade",        "Grenade" },
    { "nbomb",          "N-Bomb" },
    { "timedmine",      "Timed Mine" },
    { "proximitymine",  "Proximity Mine" },
    { "remotemine",     "Remote Mine" },
    { "laser",          "Laser" },
};
static const s32 s_NumWeapons = (s32)(sizeof(s_WeaponDisplay) / sizeof(s_WeaponDisplay[0]));

/* Mode table */
static const struct { const char *key_suffix; const char *display; } s_ModeDisplay[] = {
    { "combat",         "Combat" },
    { "hold_briefcase", "Hold the Briefcase" },
    { "hacker_central", "Hacker Central" },
    { "pop_a_cap",      "Pop a Cap" },
    { "king_of_hill",   "King of the Hill" },
    { "capture_case",   "Capture the Case" },
};
static const s32 s_NumModes = (s32)(sizeof(s_ModeDisplay) / sizeof(s_ModeDisplay[0]));

/* ========================================================================
 * Tab renderers
 * ======================================================================== */

/* Format seconds as "Hh Mm" or "Mm Ss" for compact display. */
static void formatDuration(char *out, size_t outSz, u64 seconds)
{
    u64 h = seconds / 3600;
    u64 m = (seconds % 3600) / 60;
    u64 s = seconds % 60;
    if (h > 0) {
        snprintf(out, outSz, "%lluh %llum", (unsigned long long)h, (unsigned long long)m);
    } else if (m > 0) {
        snprintf(out, outSz, "%llum %llus", (unsigned long long)m, (unsigned long long)s);
    } else {
        snprintf(out, outSz, "%llus", (unsigned long long)s);
    }
}

static void renderOverviewTab(float contentW)
{
    u64 kills = statGet("kills.total");
    u64 deaths = statGet("deaths.total");
    u64 shots = statGet("shots.total");
    u64 shots_hit = statGet("mp.shots_hit");
    u64 headshots = statGet("shots.headshot");
    u64 kills_bot = statGet("kills.vs_bot");
    u64 kills_player = statGet("kills.vs_player");
    u64 deaths_bot = statGet("deaths.by_bot");
    u64 deaths_player = statGet("deaths.by_player");
    u64 suicides = statGet("deaths.suicide");

    u64 mp_matches = statGet("matches.played");
    u64 mp_won = statGet("mp.matches_won");
    u64 mp_lost = statGet("mp.matches_lost");
    u64 mp_time = statGet("mp.time_played_seconds");
    u64 mp_distance = statGet("mp.distance_units");
    u64 mp_damage_dealt = statGet("mp.damage_dealt");
    u64 mp_damage_received = statGet("mp.damage_received");

    u64 solo_completed = statGet("solo.missions_completed");
    u64 solo_failed = statGet("solo.mission_failures");
    u64 solo_time = statGet("solo.time_played_seconds");

    u64 items = statGet("items.picked_up");
    u64 keys = statGet("keys_picked_up");
    u64 ammo = statGet("ammo_crates_picked_up");
    u64 shields = statGet("shields_picked_up");
    u64 weapons_up = statGet("weapons_picked_up");
    u64 doors = statGet("doors.opened");

    ImGui::TextDisabled("Combat");
    ImGui::Separator();

    ImGui::Columns(2, "##overview_stats", false);
    ImGui::SetColumnWidth(0, contentW * 0.55f);

    ImGui::Text("Total Kills");     ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)kills);  ImGui::NextColumn();

    ImGui::Text("Total Deaths");    ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)deaths); ImGui::NextColumn();

    ImGui::Text("K/D Ratio");       ImGui::NextColumn();
    if (deaths > 0) {
        ImGui::Text("%.2f", (double)kills / (double)deaths);
    } else {
        ImGui::Text("%.2f", (double)kills);
    }
    ImGui::NextColumn();

    ImGui::Text("Suicides");        ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)suicides); ImGui::NextColumn();

    ImGui::Columns(1);
    ImGui::Spacing();

    ImGui::TextDisabled("Accuracy");
    ImGui::Separator();

    ImGui::Columns(2, "##accuracy_stats", false);
    ImGui::SetColumnWidth(0, contentW * 0.55f);

    ImGui::Text("Total Shots");     ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)shots);  ImGui::NextColumn();

    ImGui::Text("Shots Hit");       ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)shots_hit); ImGui::NextColumn();

    ImGui::Text("Hit %%");          ImGui::NextColumn();
    if (shots > 0) {
        ImGui::Text("%.1f%%", (double)shots_hit * 100.0 / (double)shots);
    } else {
        ImGui::Text("--");
    }
    ImGui::NextColumn();

    ImGui::Text("Headshots");       ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)headshots); ImGui::NextColumn();

    ImGui::Text("Headshot %%");     ImGui::NextColumn();
    if (shots > 0) {
        ImGui::Text("%.1f%%", (double)headshots * 100.0 / (double)shots);
    } else {
        ImGui::Text("--");
    }
    ImGui::NextColumn();

    ImGui::Columns(1);
    ImGui::Spacing();

    ImGui::TextDisabled("Opponents");
    ImGui::Separator();

    ImGui::Columns(2, "##opponent_stats", false);
    ImGui::SetColumnWidth(0, contentW * 0.55f);

    ImGui::Text("Kills vs Bots");    ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)kills_bot);    ImGui::NextColumn();
    ImGui::Text("Kills vs Players"); ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)kills_player); ImGui::NextColumn();
    ImGui::Text("Deaths by Bots");   ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)deaths_bot);   ImGui::NextColumn();
    ImGui::Text("Deaths by Players");ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)deaths_player);ImGui::NextColumn();

    ImGui::Columns(1);
    ImGui::Spacing();

    ImGui::TextDisabled("Combat Simulator");
    ImGui::Separator();

    ImGui::Columns(2, "##mp_stats", false);
    ImGui::SetColumnWidth(0, contentW * 0.55f);

    ImGui::Text("Matches Played");  ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)mp_matches); ImGui::NextColumn();

    ImGui::Text("Matches Won");     ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)mp_won); ImGui::NextColumn();

    ImGui::Text("Matches Lost");    ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)mp_lost); ImGui::NextColumn();

    ImGui::Text("Win Rate");        ImGui::NextColumn();
    if (mp_won + mp_lost > 0) {
        ImGui::Text("%.1f%%", (double)mp_won * 100.0 / (double)(mp_won + mp_lost));
    } else {
        ImGui::Text("--");
    }
    ImGui::NextColumn();

    ImGui::Text("Time Played");     ImGui::NextColumn();
    {
        char buf[32];
        formatDuration(buf, sizeof(buf), mp_time);
        ImGui::TextUnformatted(buf);
    }
    ImGui::NextColumn();

    ImGui::Text("Damage Dealt");    ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)mp_damage_dealt); ImGui::NextColumn();

    ImGui::Text("Damage Taken");    ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)mp_damage_received); ImGui::NextColumn();

    ImGui::Text("Distance");        ImGui::NextColumn();
    ImGui::Text("%llu units", (unsigned long long)mp_distance); ImGui::NextColumn();

    ImGui::Columns(1);
    ImGui::Spacing();

    ImGui::TextDisabled("Solo Missions");
    ImGui::Separator();

    ImGui::Columns(2, "##solo_stats", false);
    ImGui::SetColumnWidth(0, contentW * 0.55f);

    ImGui::Text("Missions Completed"); ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)solo_completed); ImGui::NextColumn();

    ImGui::Text("Missions Failed");    ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)solo_failed); ImGui::NextColumn();

    ImGui::Text("Time Played");        ImGui::NextColumn();
    {
        char buf[32];
        formatDuration(buf, sizeof(buf), solo_time);
        ImGui::TextUnformatted(buf);
    }
    ImGui::NextColumn();

    ImGui::Columns(1);
    ImGui::Spacing();

    ImGui::TextDisabled("World Interaction");
    ImGui::Separator();

    ImGui::Columns(2, "##world_stats", false);
    ImGui::SetColumnWidth(0, contentW * 0.55f);

    ImGui::Text("Items Picked Up");    ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)items); ImGui::NextColumn();

    ImGui::Text("  Weapons");          ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)weapons_up); ImGui::NextColumn();

    ImGui::Text("  Ammo Crates");      ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)ammo); ImGui::NextColumn();

    ImGui::Text("  Shields");          ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)shields); ImGui::NextColumn();

    ImGui::Text("  Keys");             ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)keys); ImGui::NextColumn();

    ImGui::Text("Doors Opened");       ImGui::NextColumn();
    ImGui::Text("%llu", (unsigned long long)doors); ImGui::NextColumn();

    ImGui::Columns(1);
}

static void renderWeaponsTab(float contentW)
{
    /* Collect weapons with kills > 0, sorted by most kills */
    struct WeaponRow { const char *name; u64 kills; u64 shots; };
    WeaponRow rows[64];
    s32 numRows = 0;

    for (s32 i = 0; i < s_NumWeapons && numRows < 64; i++) {
        char kkey[128], skey[128];
        snprintf(kkey, sizeof(kkey), "kills.weapon.%s", s_WeaponDisplay[i].key_suffix);
        snprintf(skey, sizeof(skey), "shots.weapon.%s", s_WeaponDisplay[i].key_suffix);
        u64 k = statGet(kkey);
        u64 s = statGet(skey);
        if (k > 0 || s > 0) {
            rows[numRows].name = s_WeaponDisplay[i].display;
            rows[numRows].kills = k;
            rows[numRows].shots = s;
            numRows++;
        }
    }

    /* Simple sort by kills descending */
    for (s32 i = 0; i < numRows - 1; i++) {
        for (s32 j = i + 1; j < numRows; j++) {
            if (rows[j].kills > rows[i].kills) {
                WeaponRow tmp = rows[i];
                rows[i] = rows[j];
                rows[j] = tmp;
            }
        }
    }

    if (numRows == 0) {
        ImGui::TextDisabled("No weapon stats recorded yet.");
        return;
    }

    if (ImGui::BeginTable("##weapon_table", 3,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Weapon", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Kills",  ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Shots",  ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        for (s32 i = 0; i < numRows; i++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(rows[i].name);
            ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)rows[i].kills);
            ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)rows[i].shots);
        }
        ImGui::EndTable();
    }
}

static void renderModesTab(float contentW)
{
    s32 hasAny = 0;

    if (ImGui::BeginTable("##mode_table", 3,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Mode",   ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Kills",  ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Deaths", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        for (s32 i = 0; i < s_NumModes; i++) {
            char kkey[128], dkey[128];
            snprintf(kkey, sizeof(kkey), "kills.mode.%s", s_ModeDisplay[i].key_suffix);
            snprintf(dkey, sizeof(dkey), "deaths.mode.%s", s_ModeDisplay[i].key_suffix);
            u64 k = statGet(kkey);
            u64 d = statGet(dkey);
            if (k > 0 || d > 0) {
                hasAny = 1;
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(s_ModeDisplay[i].display);
                ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)k);
                ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)d);
            }
        }
        ImGui::EndTable();
    }

    if (!hasAny) {
        ImGui::TextDisabled("No mode stats recorded yet.");
    }
}

static void renderAchievementsTab(float contentW)
{
    s32 count = achievementGetCount();
    s32 unlocked = 0;

    for (s32 i = 0; i < count; i++) {
        if (achievementIsUnlocked(((const achievement_def_t_cpp *)achievementGetByIndex(i))->id)) {
            unlocked++;
        }
    }

    ImGui::Text("Achievements: %d / %d", unlocked, count);
    ImGui::Separator();
    ImGui::Spacing();

    for (s32 i = 0; i < count; i++) {
        const achievement_def_t_cpp *a = (const achievement_def_t_cpp *)achievementGetByIndex(i);
        if (!a) continue;

        bool done = achievementIsUnlocked(a->id) != 0;
        u64 current = statGet(a->stat_key);
        u64 target = a->threshold;
        float progress = (target > 0) ? (float)((double)current / (double)target) : 0.0f;
        if (progress > 1.0f) progress = 1.0f;

        ImGui::PushID(i);

        if (done) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "[+]");
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[ ]");
        }
        ImGui::SameLine();
        ImGui::Text("%s", a->name);

        ImGui::TextDisabled("  %s", a->description);
        ImGui::ProgressBar(progress, ImVec2(contentW * 0.6f, 0),
                           done ? "Complete" : "");
        ImGui::SameLine();
        ImGui::Text("%llu / %llu", (unsigned long long)current, (unsigned long long)target);

        ImGui::Spacing();
        ImGui::PopID();
    }
}

/* ========================================================================
 * Public API
 * ======================================================================== */

extern "C" {

static bool s_Registered = false;

void pdguiMenuStatsShow(void)
{
    s_StatsOpen = true;
    s_StatsTab = 0;
}

void pdguiMenuStatsHide(void)
{
    s_StatsOpen = false;
}

s32 pdguiMenuStatsIsVisible(void)
{
    return s_StatsOpen ? 1 : 0;
}

void pdguiMenuStatsRender(s32 winW, s32 winH)
{
    if (!s_StatsOpen) return;

    float mw  = pdguiMenuWidth();
    float mh  = pdguiMenuHeight();
    ImVec2 pos = pdguiMenuPos();
    float scale = pdguiScaleFactor();

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##stats_viewer", nullptr, wf)) {
        ImGui::End();
        return;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
    }

    /* Title bar */
    float titleH = pdguiScale(39.0f);
    pdguiDrawPdDialog(pos.x, pos.y, mw, mh, "Player Statistics", 1);

    pdguiSetCursorBelowTitle(titleH);

    /* Tab bar — bumper (LB/RB) cycling via PageUp/PageDown */
    const char *tabs[] = { "Overview", "Weapons", "Modes", "Achievements" };
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp, false)) {
        s_StatsTab = (s_StatsTab - 1 + 4) % 4;
        pdguiPlaySound(PDGUI_SND_SWIPE);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) {
        s_StatsTab = (s_StatsTab + 1) % 4;
        pdguiPlaySound(PDGUI_SND_SWIPE);
    }
    float tabW = (mw - ImGui::GetStyle().WindowPadding.x * 2.0f) / 4.0f;
    for (s32 t = 0; t < 4; t++) {
        if (t > 0) ImGui::SameLine();
        bool active = (s_StatsTab == t);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(pdguiPalImU32(PDPAL_TITLEBG, 200)));
        }
        if (ImGui::Button(tabs[t], ImVec2(tabW - 4.0f, 24.0f * scale))) {
            s_StatsTab = t;
            pdguiPlaySound(PDGUI_SND_FOCUS);
        }
        if (active) ImGui::PopStyleColor();
    }

    ImGui::Separator();

    /* Content area — reserve bottom space for the docked action bar (C1). */
    float bodyAvail = ImGui::GetContentRegionAvail().y;
    float bodyH     = pdguiBodyHeightForActionBar(bodyAvail);
    float contentW  = mw - ImGui::GetStyle().WindowPadding.x * 2.0f;

    /* Priority L (2026-04-25): NavFlattened layout panel for stats tabs. */
    if (ImGui::BeginChild("##stats_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened, 0)) {
        switch (s_StatsTab) {
            case 0: renderOverviewTab(contentW);      break;
            case 1: renderWeaponsTab(contentW);        break;
            case 2: renderModesTab(contentW);          break;
            case 3: renderAchievementsTab(contentW);   break;
        }
    }
    ImGui::EndChild();

    /* Docked action bar (C1): Close always reachable regardless of scroll. */
    bool closeActivated = false;
    if (pdguiBeginActionBar("##stats_ab")) {
        if (pdguiActionBarButton("Close", 1, ImGui::GetContentRegionAvail().x)) {
            closeActivated = true;
        }
    }
    pdguiEndActionBar();

    if (closeActivated || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        s_StatsOpen = false;
    }

    ImGui::End();
}

} /* extern "C" */
