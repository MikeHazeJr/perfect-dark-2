/**
 * pdgui_menu_room.cpp -- Room interior screen.
 *
 * Full-screen view shown when a client is inside a room (CLSTATE_LOBBY).
 * Tab bar at top: Combat Simulator | Campaign | Counter-Operative
 * Left panel: tab-specific match settings (leader editable; non-leaders read-only).
 * Right panel: room player list (name, leader mark, body char, connection state).
 * Bottom bar: "Start Match" (leader only) + "Leave Room".
 *
 * Settings are stored in g_MatchConfig (matchsetup.c). On "Start Match":
 *   - Combat Sim: netLobbyRequestStartWithSims(GAMEMODE_MP, stagenum, 0, numBots, simType)
 *   - Campaign:   netLobbyRequestStart(GAMEMODE_COOP, stagenum, difficulty)
 *   - Counter-Op: netLobbyRequestStart(GAMEMODE_ANTI, stagenum, difficulty)
 *
 * Full settings sync (all options, per-bot config) is deferred to CLC_ROOM_SETTINGS (R-4).
 *
 * Called from pdgui_lobby.cpp via pdguiRoomScreenRender().
 *
 * IMPORTANT: C++ file — must NOT include types.h (#define bool s32 breaks C++).
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>

#include "imgui/imgui.h"
#include "pdgui_hotswap.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "system.h"

/* ========================================================================
 * Forward declarations (C boundary)
 * ======================================================================== */

extern "C" {

/* Network mode */
#define NETMODE_NONE   0
#define NETMODE_SERVER 1
#define NETMODE_CLIENT 2

#define CLSTATE_DISCONNECTED 0
#define CLSTATE_CONNECTING   1
#define CLSTATE_AUTH         2
#define CLSTATE_LOBBY        3
#define CLSTATE_GAME         4

s32 netGetMode(void);
s32 netDisconnect(void);
void pdguiSetInRoom(s32 inRoom);  /* pdgui_lobby.cpp — transition back to social lobby */
extern s32 g_NetMode;
extern s32 g_NetDedicated;
extern u8 g_NetCoopDifficulty;
extern u8 g_NetCoopFriendlyFire;

/* Connect code (for displaying to server host) */
s32 connectCodeEncode(u32 ip, char *buf, s32 bufsize);
const char *netGetPublicIP(void);

/* Lobby state */
void lobbyUpdate(void);
s32 lobbyGetPlayerCount(void);
s32 lobbyIsLocalLeader(void);

struct lobbyplayer_view {
    u8 active;
    u8 isLeader;
    u8 isReady;
    u8 headnum;
    u8 bodynum;
    u8 team;
    char name[32];
    s32 isLocal;
    s32 state;
};
s32 lobbyGetPlayerInfo(s32 idx, struct lobbyplayer_view *out);

/* Game mode constants */
#define GAMEMODE_MP   0
#define GAMEMODE_COOP 1
#define GAMEMODE_ANTI 2

/* Max human players (must match constants.h MAX_PLAYERS) */
#define MAX_PLAYERS 8

/* Bridge: send CLC_LOBBY_START */
s32 netLobbyRequestStart(u8 gamemode, u8 stagenum, u8 difficulty);
s32 netLobbyRequestStartWithSims(u8 gamemode, u8 stagenum, u8 difficulty,
                                  u8 numSims, u8 simType);

/* Character data */
char *mpGetBodyName(u8 mpbodynum);
u32 mpGetNumBodies(void);

/* Weapon sets (mplayer.c) */
void mpSetWeaponSet(s32 weaponsetnum);
s32 mpGetWeaponSet(void);
char *mpGetWeaponSetName(s32 index);
s32 func0f189058(s32 full);   /* count of available weapon sets (full=1 includes Random/Custom) */
extern s32 g_MpWeaponSetNum;
#define WEAPONSET_CUSTOM 0x0e

/* Match config (must match struct layout in matchsetup.c exactly) */
#define MAX_PLAYER_NAME  32
#define MATCH_MAX_SLOTS  32
#define NUM_MPWEAPONSLOTS 6

#define SLOT_EMPTY  0
#define SLOT_PLAYER 1
#define SLOT_BOT    2

struct matchslot {
    u8 type;
    u8 team;
    u8 headnum;
    u8 bodynum;
    u8 botType;
    u8 botDifficulty;
    char name[MAX_PLAYER_NAME];
};

struct matchconfig {
    struct matchslot slots[MATCH_MAX_SLOTS];
    u8 scenario;
    u8 stagenum;
    u8 timelimit;
    u8 scorelimit;
    u16 teamscorelimit;
    u32 options;
    u8 weapons[NUM_MPWEAPONSLOTS];
    s8 weaponSetIndex;
    u8 numSlots;
    u8 spawnWeaponNum; /* 0xFF = Random; weapon enum value otherwise (future R-4 wiring) */
};

extern struct matchconfig g_MatchConfig;
void matchConfigInit(void);
s32 matchConfigAddBot(u8 botType, u8 botDifficulty, u8 headnum, u8 bodynum,
                      const char *name);
s32 matchConfigRemoveSlot(s32 idx);
s32 matchStart(void);

/* MPOPTION bitmasks (from constants.h) */
#define MPOPTION_ONEHITKILLS           0x00000001
#define MPOPTION_TEAMSENABLED          0x00000002
#define MPOPTION_NORADAR               0x00000004
#define MPOPTION_NOAUTOAIM             0x00000008
#define MPOPTION_NOPLAYERHIGHLIGHT     0x00000010
#define MPOPTION_NOPICKUPHIGHLIGHT     0x00000020
#define MPOPTION_FASTMOVEMENT          0x00000100
#define MPOPTION_DISPLAYTEAM           0x00000200
#define MPOPTION_HTB_HIGHLIGHTBRIEFCASE 0x00000800
#define MPOPTION_HTB_SHOWONRADAR       0x00001000
#define MPOPTION_CTC_SHOWONRADAR       0x00002000
#define MPOPTION_KOH_HILLONRADAR       0x00004000
#define MPOPTION_KOH_MOBILEHILL        0x00008000
#define MPOPTION_HTM_HIGHLIGHTTERMINAL 0x00020000
#define MPOPTION_HTM_SHOWONRADAR       0x00040000
#define MPOPTION_PAC_HIGHLIGHTTARGET   0x00080000
#define MPOPTION_PAC_SHOWONRADAR       0x00100000
#define MPOPTION_SPAWNWITHWEAPON       0x00200000
#define MPOPTION_FRIENDLYFIRE          0x02000000
#define MPOPTION_NOPLAYERONRADAR       0x04000000
#define MPOPTION_NODOORS               0x08000000

/* Scenario constants (from constants.h) */
#define MPSCENARIO_COMBAT           0
#define MPSCENARIO_HOLDTHEBRIEFCASE 1
#define MPSCENARIO_HACKERCENTRAL    2
#define MPSCENARIO_POPACAP          3
#define MPSCENARIO_KINGOFTHEHILL    4
#define MPSCENARIO_CAPTURETHECASE   5

/* Difficulty constants (from constants.h) */
#define DIFF_A  0
#define DIFF_SA 1
#define DIFF_PA 2

/* Agent name (for connect code display) */
const char *mpPlayerConfigGetName(s32 playernum);

} /* extern "C" */

/* ========================================================================
 * Arena list — MP stages available in Combat Sim
 * ======================================================================== */

struct arena_entry { const char *name; u8 stagenum; };

static const arena_entry s_Arenas[] = {
    { "Complex",     0x1f },
    { "Grid",        0x20 },
    { "Sewers",      0x21 },
    { "Basement",    0x22 },
    { "Ravine",      0x23 },
    { "Warehouse",   0x24 },
    { "Villa",       0x25 },
    { "Temple",      0x26 },
    { "Caves",       0x27 },
    { "Base",        0x28 },
    { "G5",          0x29 },
    { "Citadel",     0x2a },
    { "Felicity",    0x2b },
    { "Ruins",       0x2c },
    { "Defection",   0x2d },
    { "Island",      0x2e },
    { "Fortress",    0x2f },
    { "Pipes",       0x32 },
};
static const int s_NumArenas = (int)(sizeof(s_Arenas) / sizeof(s_Arenas[0]));

/* ========================================================================
 * Campaign mission list
 * ======================================================================== */

struct mission_entry { const char *name; u8 stagenum; };

static const mission_entry s_Missions[] = {
    { "dataDyne Central - Defection",       0x30 },
    { "dataDyne Research - Investigation",  0x33 },
    { "dataDyne Central - Extraction",      0x35 },
    { "Carrington Villa - Hostage One",     0x36 },
    { "Chicago - Stealth",                  0x37 },
    { "G5 Building - Reconnaissance",       0x38 },
    { "Area 51 - Infiltration",             0x2f },
    { "Area 51 - Rescue",                   0x39 },
    { "Area 51 - Escape",                   0x3a },
    { "Air Base - Espionage",               0x3b },
    { "Air Force One - Antiterrorism",      0x31 },
    { "Crash Site - Confrontation",         0x3c },
    { "Pelagic II - Exploration",           0x3d },
    { "Deep Sea - Nullify Threat",          0x3e },
    { "Carrington Institute - Defense",     0x3f },
    { "Attack Ship - Covert Assault",       0x34 },
    { "Skedar Ruins - Battle Shrine",       0x40 },
};
static const int s_NumMissions = (int)(sizeof(s_Missions) / sizeof(s_Missions[0]));

/* ========================================================================
 * Sim difficulty names
 * ======================================================================== */

static const char *s_SimDiffNames[] = {
    "MeatSim", "EasySim", "NormalSim", "HardSim", "PerfectSim", "DarkSim"
};
static const int s_NumSimDiffs = 6;

/* ========================================================================
 * Spawn-with-weapon picker — weapon names for the dropdown
 * Only combat weapons that make sense as a spawn weapon in MP.
 * ======================================================================== */

struct spawnweapon_entry { const char *name; u8 weaponnum; };

static const spawnweapon_entry s_SpawnWeapons[] = {
    { "Random",              0xFF },
    { "Unarmed",             1  },
    { "Falcon 2",            2  },
    { "Falcon 2 (Silenced)", 3  },
    { "Falcon 2 (Scope)",    4  },
    { "MagSec 4",            5  },
    { "Mauler",              6  },
    { "Phoenix",             7  },
    { "DY357 Magnum",        8  },
    { "DY357-LX",            9  },
    { "CMP 150",             10 },
    { "Cyclone",             11 },
    { "Callisto NTG",        12 },
    { "RCP-120",             13 },
    { "Laptop Gun",          14 },
    { "Dragon",              15 },
    { "K7 Avenger",          16 },
    { "AR34",                17 },
    { "SuperDragon",         18 },
    { "Shotgun",             19 },
    { "Reaper",              20 },
    { "Sniper Rifle",        21 },
    { "Farsight XR-20",      22 },
    { "Devastator",          23 },
    { "Rocket Launcher",     24 },
    { "Slayer",              25 },
    { "Combat Knife",        26 },
    { "Crossbow",            27 },
    { "Tranquilizer",        28 },
    { "Laser",               29 },
    { "Grenade",             30 },
    { "N-Bomb",              31 },
    { "Timed Mine",          32 },
    { "Proximity Mine",      33 },
    { "Remote Mine",         34 },
};
static const int s_NumSpawnWeapons = (int)(sizeof(s_SpawnWeapons) / sizeof(s_SpawnWeapons[0]));

/* ========================================================================
 * Scenario names
 * ======================================================================== */

static const char *s_ScenarioNames[] = {
    "Combat",
    "Hold the Briefcase",
    "Hacker Central",
    "Pop a Cap",
    "King of the Hill",
    "Capture the Case",
};
static const int s_NumScenarios = 6;

/* ========================================================================
 * Difficulty names (Campaign / Counter-Op)
 * ======================================================================== */

static const char *s_DiffNames[] = {
    "Agent", "Special Agent", "Perfect Agent"
};

/* ========================================================================
 * Per-session UI state
 * ======================================================================== */

/* Active tab: 0 = Combat Sim, 1 = Campaign, 2 = Counter-Op */
static int s_ActiveTab = 0;

/* Track if we've initialized g_MatchConfig for this lobby session */
static bool s_MatchConfigInited = false;

/* Campaign / Counter-Op settings */
static int s_CampaignMission   = 0;
static int s_CampaignDiff      = DIFF_A;
static int s_CounterOpMission  = 0;
static int s_CounterOpDiff     = DIFF_A;
static int s_CounterOpPlayer   = 0;  /* index into lobby player list = the counter-op player */

/* Arena picker state (index into s_Arenas) */
static int s_SelectedArena = 0;

/* Bot management state */
static int  s_SelectedBotSlot = -1; /* g_MatchConfig.slots index; -1 = none selected */
static bool s_BotModalOpen    = false;
static int  s_EditBotSlotIdx  = -1; /* slot index being edited in the modal */

/* Spawn weapon picker — index into s_SpawnWeapons (0 = Random) */
static int s_SpawnWeaponIdx = 0;

/* Connect code cache (server host display) */
static char s_ConnectCode[128]  = "";
static bool s_CodeGenerated     = false;

/* ========================================================================
 * Helper: ensure arena index is consistent with g_MatchConfig.stagenum
 * ======================================================================== */

static void syncArenaFromConfig(void)
{
    for (int i = 0; i < s_NumArenas; i++) {
        if (s_Arenas[i].stagenum == g_MatchConfig.stagenum) {
            s_SelectedArena = i;
            return;
        }
    }
    /* stagenum not in arena list — keep current picker, write it back */
    g_MatchConfig.stagenum = (u8)s_Arenas[s_SelectedArena].stagenum;
}

/* ========================================================================
 * Helper: count bots currently in g_MatchConfig
 * ======================================================================== */

static int countBots(void)
{
    int n = 0;
    for (int i = 1; i < g_MatchConfig.numSlots; i++) {
        if (g_MatchConfig.slots[i].type == SLOT_BOT) n++;
    }
    return n;
}

/* ========================================================================
 * Helper: get "global" sim difficulty for netLobbyRequestStartWithSims
 * (uses the first bot's difficulty; 0 = Normal if no bots)
 * ======================================================================== */

static u8 getLeadSimType(void)
{
    for (int i = 1; i < g_MatchConfig.numSlots; i++) {
        if (g_MatchConfig.slots[i].type == SLOT_BOT) {
            return g_MatchConfig.slots[i].botDifficulty;
        }
    }
    return 2; /* Normal as default */
}

/* ========================================================================
 * Helper: option toggle row — renders a toggle button and updates bitmask
 * ======================================================================== */

static void optToggle(const char *label, u32 flag, bool leader)
{
    bool on = (g_MatchConfig.options & flag) != 0;
    if (!leader) ImGui::BeginDisabled();
    if (ImGui::Checkbox(label, &on)) {
        if (on) g_MatchConfig.options |= flag;
        else    g_MatchConfig.options &= ~flag;
        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
    }
    if (!leader) ImGui::EndDisabled();
}

/* Like optToggle but logic is inverted (flag ON means feature OFF) */
static void optToggleInverted(const char *label, u32 flag, bool leader)
{
    bool on = (g_MatchConfig.options & flag) == 0; /* true when feature is enabled */
    if (!leader) ImGui::BeginDisabled();
    if (ImGui::Checkbox(label, &on)) {
        if (on) g_MatchConfig.options &= ~flag;
        else    g_MatchConfig.options |= flag;
        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
    }
    if (!leader) ImGui::EndDisabled();
}

/* ========================================================================
 * Right panel: room player list + bot management
 * ======================================================================== */

static void renderPlayerPanel(float panelW, float panelH, bool isLeader)
{
    int humanCount = lobbyGetPlayerCount();
    int curBots    = countBots();
    /* Max bots = remaining player slots (MAX_PLAYERS - humans), also bounded
     * by MATCH_MAX_SLOTS capacity. */
    int maxBots = MAX_PLAYERS - humanCount;
    if (maxBots < 0) maxBots = 0;

    float btnH   = pdguiScale(26.0f);
    float listH  = panelH - btnH
                   - ImGui::GetStyle().ItemSpacing.y * 3.0f
                   - ImGui::GetStyle().WindowPadding.y * 2.0f;

    /* Outer panel (bordered) */
    ImGui::BeginChild("##room_panel_outer", ImVec2(panelW, panelH), true);

    /* Scrollable list */
    ImGui::BeginChild("##room_players_list", ImVec2(0, listH), false);

    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Players in Room");
    ImGui::Separator();

    /* Human player rows */
    for (s32 i = 0; i < humanCount; i++) {
        struct lobbyplayer_view pv;
        memset(&pv, 0, sizeof(pv));
        if (!lobbyGetPlayerInfo(i, &pv)) continue;

        ImGui::PushID(i);

        char label[80];
        const char *suffix = pv.isLeader ? " *" : "";
        snprintf(label, sizeof(label), "%s%s", pv.name, suffix);

        if (pv.isLeader && pv.isLocal) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", label);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 0.7f), "(you, leader)");
        } else if (pv.isLeader) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", label);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 0.7f), "(leader)");
        } else if (pv.isLocal) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", label);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 0.6f), "(you)");
        } else {
            ImGui::Text("%s", label);
        }

        if (pv.bodynum < (u8)mpGetNumBodies()) {
            const char *bodyName = mpGetBodyName(pv.bodynum);
            if (bodyName && bodyName[0]) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.55f, 0.75f), "[%s]", bodyName);
            }
        }

        const char *stateStr  = "";
        ImVec4      stateColor = ImVec4(0.5f, 0.5f, 0.5f, 0.6f);
        switch (pv.state) {
            case CLSTATE_CONNECTING:
            case CLSTATE_AUTH:
                stateStr  = "connecting...";
                stateColor = ImVec4(1.0f, 0.8f, 0.2f, 0.8f);
                break;
            case CLSTATE_LOBBY:
                stateStr  = "ready";
                stateColor = ImVec4(0.3f, 1.0f, 0.3f, 0.8f);
                break;
            case CLSTATE_GAME:
                stateStr  = "in game";
                stateColor = ImVec4(0.3f, 0.7f, 1.0f, 0.8f);
                break;
        }
        if (stateStr[0]) {
            ImGui::SameLine();
            ImGui::TextColored(stateColor, "  %s", stateStr);
        }

        ImGui::PopID();
    }

    if (humanCount == 0) {
        ImGui::TextDisabled("Waiting for players...");
    }

    /* Bot rows */
    if (curBots > 0) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.3f, 0.9f), "Bots");
    }

    /* X-button width so we can right-justify it */
    float xBtnW  = pdguiScale(20.0f);
    float rowW   = panelW
                   - ImGui::GetStyle().WindowPadding.x * 2.0f
                   - xBtnW
                   - ImGui::GetStyle().ItemSpacing.x * 2.0f
                   - 4.0f; /* border */

    int removeSlot = -1; /* deferred removal — can't remove while iterating */
    for (int i = 1; i < g_MatchConfig.numSlots; i++) {
        struct matchslot *sl = &g_MatchConfig.slots[i];
        if (sl->type != SLOT_BOT) continue;

        ImGui::PushID(i);

        bool selected = (s_SelectedBotSlot == i);

        char rowLabel[80];
        snprintf(rowLabel, sizeof(rowLabel), "[BOT] %s", sl->name);

        /* Selectable row — single-click selects, double-click opens modal */
        if (ImGui::Selectable(rowLabel, selected,
                              ImGuiSelectableFlags_AllowDoubleClick,
                              ImVec2(rowW, 0.0f))) {
            s_SelectedBotSlot = selected ? -1 : i;
            if (ImGui::IsMouseDoubleClicked(0) && isLeader) {
                s_EditBotSlotIdx = i;
                s_BotModalOpen   = true;
            }
            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
        }

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.4f, 0.8f),
                           "[%s]", s_SimDiffNames[sl->botDifficulty]);

        /* X remove button — right-aligned */
        ImGui::SameLine(panelW
                        - ImGui::GetStyle().WindowPadding.x * 2.0f
                        - xBtnW
                        - 4.0f);
        if (!isLeader) ImGui::BeginDisabled();
        char xLabel[16];
        snprintf(xLabel, sizeof(xLabel), "X##bx%d", i);
        if (ImGui::SmallButton(xLabel)) {
            removeSlot = i;
            if (s_SelectedBotSlot == i) s_SelectedBotSlot = -1;
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
        }
        if (!isLeader) ImGui::EndDisabled();

        ImGui::PopID();
    }

    /* Deferred removal */
    if (removeSlot >= 1) {
        matchConfigRemoveSlot(removeSlot);
    }

    ImGui::EndChild(); /* ##room_players_list */

    ImGui::Separator();

    /* Add Bot button + slot count */
    bool canAdd = isLeader
                  && (curBots < maxBots)
                  && (g_MatchConfig.numSlots < MATCH_MAX_SLOTS);
    if (!canAdd) ImGui::BeginDisabled();
    if (ImGui::Button("Add Bot", ImVec2(-1.0f, btnH))) {
        /* If a bot is currently selected, copy its settings to the new bot */
        u8   headnum = 0, bodynum = 0, diff = 2 /* NormalSim */;
        if (s_SelectedBotSlot >= 1
            && s_SelectedBotSlot < g_MatchConfig.numSlots
            && g_MatchConfig.slots[s_SelectedBotSlot].type == SLOT_BOT) {
            struct matchslot *src = &g_MatchConfig.slots[s_SelectedBotSlot];
            headnum = src->headnum;
            bodynum = src->bodynum;
            diff    = src->botDifficulty;
        }
        matchConfigAddBot(0 /*BOTTYPE_NORMAL*/, diff, headnum, bodynum, nullptr);
        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
    }
    if (!canAdd) ImGui::EndDisabled();

    ImGui::EndChild(); /* ##room_panel_outer */
}

/* ========================================================================
 * Left panel: Combat Simulator tab
 * ======================================================================== */

static void renderCombatSimTab(float panelW, float panelH, bool leader)
{
    ImGui::BeginChild("##room_cs_settings", ImVec2(panelW, panelH), false);

    float scale = pdguiScaleFactor();
    float comboW = panelW - ImGui::GetStyle().WindowPadding.x * 2;

    /* --- Scenario --- */
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Scenario");
    if (!leader) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(comboW);
    if (ImGui::BeginCombo("##scenario", s_ScenarioNames[g_MatchConfig.scenario])) {
        for (int si = 0; si < s_NumScenarios; si++) {
            bool sel = (si == (int)g_MatchConfig.scenario);
            if (ImGui::Selectable(s_ScenarioNames[si], sel)) {
                g_MatchConfig.scenario = (u8)si;
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (!leader) ImGui::EndDisabled();

    ImGui::Spacing();

    /* --- Arena --- */
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Arena");
    if (!leader) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(comboW);
    if (ImGui::BeginCombo("##arena", s_Arenas[s_SelectedArena].name)) {
        for (int ai = 0; ai < s_NumArenas; ai++) {
            bool sel = (ai == s_SelectedArena);
            if (ImGui::Selectable(s_Arenas[ai].name, sel)) {
                s_SelectedArena = ai;
                g_MatchConfig.stagenum = (u8)s_Arenas[ai].stagenum;
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (!leader) ImGui::EndDisabled();

    ImGui::Spacing();

    /* --- Limits --- */
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Limits");

    /* Time limit: 0–59 = (val+1) mins; 60+ = no limit */
    {
        if (!leader) ImGui::BeginDisabled();
        int tl = (int)g_MatchConfig.timelimit;
        ImGui::SetNextItemWidth(comboW * 0.6f);
        if (ImGui::SliderInt("Time (min)", &tl, 0, 60)) {
            g_MatchConfig.timelimit = (u8)tl;
        }
        ImGui::SameLine();
        if (g_MatchConfig.timelimit >= 60) {
            ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.5f, 1.0f), "No limit");
        } else {
            ImGui::Text("%d min", g_MatchConfig.timelimit + 1);
        }
        if (!leader) ImGui::EndDisabled();
    }

    /* Score limit: slider shows 1–100 kills directly.
     * Stored as 0-based (scorelimit = kills - 1); 100 = no limit. */
    {
        if (!leader) ImGui::BeginDisabled();
        int sl = (int)g_MatchConfig.scorelimit + 1;  /* convert to 1-based for display */
        ImGui::SetNextItemWidth(comboW * 0.6f);
        if (ImGui::SliderInt("Score", &sl, 1, 100)) {
            g_MatchConfig.scorelimit = (u8)(sl - 1);  /* store 0-based */
        }
        ImGui::SameLine();
        if (g_MatchConfig.scorelimit >= 99) {  /* 99+1=100: show "No limit" */
            ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.5f, 1.0f), "No limit");
        } else {
            ImGui::Text("%d kills", sl);  /* sl already equals scorelimit+1 */
        }
        if (!leader) ImGui::EndDisabled();
    }

    ImGui::Spacing();

    /* --- Weapon Set --- */
    {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Weapon Set");
        if (!leader) ImGui::BeginDisabled();
        s32 numSets = func0f189058(1);
        s32 curSet  = mpGetWeaponSet();
        char *curName = mpGetWeaponSetName(curSet);
        ImGui::SetNextItemWidth(comboW);
        if (ImGui::BeginCombo("##wset", curName ? curName : "???")) {
            for (s32 i = 0; i < numSets; i++) {
                char *setName = mpGetWeaponSetName(i);
                if (!setName || !setName[0]) continue;
                bool isSel = (i == curSet);
                char setLabel[64];
                snprintf(setLabel, sizeof(setLabel), "%s##ws%d", setName, i);
                if (ImGui::Selectable(setLabel, isSel)) {
                    mpSetWeaponSet(i);
                    g_MatchConfig.weaponSetIndex = (s8)i;
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                }
                if (isSel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (!leader) ImGui::EndDisabled();
    }

    ImGui::Spacing();
    ImGui::Separator();

    /* --- Game Options --- */
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Options");

    optToggleInverted("Auto-Aim",        MPOPTION_NOAUTOAIM,         leader);
    optToggleInverted("Radar",           MPOPTION_NORADAR,           leader);
    optToggle        ("Teams",           MPOPTION_TEAMSENABLED,      leader);
    optToggle        ("One-Hit Kills",   MPOPTION_ONEHITKILLS,       leader);
    optToggle        ("Friendly Fire",   MPOPTION_FRIENDLYFIRE,      leader);
    optToggle        ("Fast Movement",   MPOPTION_FASTMOVEMENT,      leader);
    optToggle        ("Spawn w/ Weapon", MPOPTION_SPAWNWITHWEAPON,   leader);
    /* Weapon selector — only visible when spawn-with-weapon is on */
    if (g_MatchConfig.options & MPOPTION_SPAWNWITHWEAPON) {
        if (!leader) ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(comboW * 0.9f);
        const char *curSpawnName = s_SpawnWeapons[s_SpawnWeaponIdx].name;
        if (ImGui::BeginCombo("Spawn Weapon##spawnwep", curSpawnName)) {
            for (int wi = 0; wi < s_NumSpawnWeapons; wi++) {
                bool sel = (wi == s_SpawnWeaponIdx);
                if (ImGui::Selectable(s_SpawnWeapons[wi].name, sel)) {
                    s_SpawnWeaponIdx = wi;
                    g_MatchConfig.spawnWeaponNum = s_SpawnWeapons[wi].weaponnum;
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (!leader) ImGui::EndDisabled();
    }
    optToggleInverted("Player Highlight",MPOPTION_NOPLAYERHIGHLIGHT, leader);
    optToggleInverted("Pickup Highlight",MPOPTION_NOPICKUPHIGHLIGHT, leader);

    /* Scenario-specific options */
    int sc = (int)g_MatchConfig.scenario;
    if (sc == MPSCENARIO_HOLDTHEBRIEFCASE) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.8f, 0.9f), "Hold the Briefcase");
        optToggle("Highlight Briefcase", MPOPTION_HTB_HIGHLIGHTBRIEFCASE, leader);
        optToggle("Show on Radar",       MPOPTION_HTB_SHOWONRADAR,        leader);
    } else if (sc == MPSCENARIO_CAPTURETHECASE) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.8f, 0.9f), "Capture the Case");
        optToggle("Show on Radar", MPOPTION_CTC_SHOWONRADAR, leader);
    } else if (sc == MPSCENARIO_KINGOFTHEHILL) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.8f, 0.9f), "King of the Hill");
        optToggle("Hill on Radar",  MPOPTION_KOH_HILLONRADAR, leader);
        optToggle("Mobile Hill",    MPOPTION_KOH_MOBILEHILL,  leader);
    } else if (sc == MPSCENARIO_HACKERCENTRAL) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.8f, 0.9f), "Hacker Central");
        optToggle("Highlight Terminal", MPOPTION_HTM_HIGHLIGHTTERMINAL, leader);
        optToggle("Show on Radar",      MPOPTION_HTM_SHOWONRADAR,       leader);
    } else if (sc == MPSCENARIO_POPACAP) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.8f, 0.9f), "Pop a Cap");
        optToggle("Highlight Target", MPOPTION_PAC_HIGHLIGHTTARGET, leader);
        optToggle("Show on Radar",    MPOPTION_PAC_SHOWONRADAR,     leader);
    }

    ImGui::EndChild();
}

/* ========================================================================
 * Left panel: Campaign tab
 * ======================================================================== */

static void renderCampaignTab(float panelW, float panelH, bool leader)
{
    ImGui::BeginChild("##room_coop_settings", ImVec2(panelW, panelH), false);

    float comboW = panelW - ImGui::GetStyle().WindowPadding.x * 2;

    /* --- Mission --- */
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Mission");
    if (!leader) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(comboW);
    if (ImGui::BeginCombo("##mission", s_Missions[s_CampaignMission].name)) {
        for (int mi = 0; mi < s_NumMissions; mi++) {
            bool sel = (mi == s_CampaignMission);
            if (ImGui::Selectable(s_Missions[mi].name, sel)) {
                s_CampaignMission = mi;
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (!leader) ImGui::EndDisabled();

    ImGui::Spacing();

    /* --- Difficulty --- */
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Difficulty");
    if (!leader) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(comboW);
    if (ImGui::BeginCombo("##diff", s_DiffNames[s_CampaignDiff])) {
        for (int di = 0; di <= DIFF_PA; di++) {
            bool sel = (di == s_CampaignDiff);
            if (ImGui::Selectable(s_DiffNames[di], sel)) {
                s_CampaignDiff = di;
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (!leader) ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Separator();

    /* --- Options --- */
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Options");
    {
        bool ff = (g_NetCoopFriendlyFire != 0);
        if (!leader) ImGui::BeginDisabled();
        if (ImGui::Checkbox("Friendly Fire", &ff)) {
            g_NetCoopFriendlyFire = ff ? 1 : 0;
            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
        }
        if (!leader) ImGui::EndDisabled();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Drop-in co-op: players join at next safe spawn.");
    ImGui::TextDisabled("Up to 4 players. Offline: same code path, local authority.");

    ImGui::EndChild();
}

/* ========================================================================
 * Left panel: Counter-Operative tab
 * ======================================================================== */

static void renderCounterOpTab(float panelW, float panelH, bool leader)
{
    ImGui::BeginChild("##room_anti_settings", ImVec2(panelW, panelH), false);

    float comboW = panelW - ImGui::GetStyle().WindowPadding.x * 2;

    /* --- Mission --- */
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Mission");
    if (!leader) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(comboW);
    if (ImGui::BeginCombo("##antimission", s_Missions[s_CounterOpMission].name)) {
        for (int mi = 0; mi < s_NumMissions; mi++) {
            bool sel = (mi == s_CounterOpMission);
            if (ImGui::Selectable(s_Missions[mi].name, sel)) {
                s_CounterOpMission = mi;
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (!leader) ImGui::EndDisabled();

    ImGui::Spacing();

    /* --- Counter-Op Player Assignment --- */
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Counter-Op Player");
    ImGui::TextDisabled("(plays as Maian side)");

    if (!leader) ImGui::BeginDisabled();
    s32 playerCount = lobbyGetPlayerCount();
    const char *antiPlayerName = "Player 2";
    if (playerCount >= 2) {
        struct lobbyplayer_view pv2;
        memset(&pv2, 0, sizeof(pv2));
        if (s_CounterOpPlayer < playerCount &&
            lobbyGetPlayerInfo(s_CounterOpPlayer, &pv2)) {
            antiPlayerName = pv2.name;
        }
    }

    ImGui::SetNextItemWidth(comboW);
    if (ImGui::BeginCombo("##antiplayer", antiPlayerName)) {
        for (s32 pi = 0; pi < playerCount; pi++) {
            struct lobbyplayer_view pv;
            memset(&pv, 0, sizeof(pv));
            if (!lobbyGetPlayerInfo(pi, &pv)) continue;
            bool sel = (pi == s_CounterOpPlayer);
            char pLabel[80];
            snprintf(pLabel, sizeof(pLabel), "%s%s##anti%d",
                     pv.name, pv.isLocal ? " (you)" : "", pi);
            if (ImGui::Selectable(pLabel, sel)) {
                s_CounterOpPlayer = pi;
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (!leader) ImGui::EndDisabled();

    ImGui::Spacing();

    /* --- Difficulty --- */
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Difficulty");
    if (!leader) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(comboW);
    if (ImGui::BeginCombo("##antidiff", s_DiffNames[s_CounterOpDiff])) {
        for (int di = 0; di <= DIFF_PA; di++) {
            bool sel = (di == s_CounterOpDiff);
            if (ImGui::Selectable(s_DiffNames[di], sel)) {
                s_CounterOpDiff = di;
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (!leader) ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::TextDisabled("One player plays on the Maian/enemy side.");
    ImGui::TextDisabled("Counter-op player assignment syncs with R-4 settings protocol.");

    ImGui::EndChild();
}

/* ========================================================================
 * Public entry point
 * ======================================================================== */

extern "C" void pdguiRoomScreenRender(s32 winW, s32 winH)
{
    lobbyUpdate();

    /* First-frame init: set up g_MatchConfig for this lobby session */
    if (!s_MatchConfigInited) {
        matchConfigInit();
        syncArenaFromConfig();
        s_SelectedBotSlot = -1;
        s_SpawnWeaponIdx  = 0;
        s_CodeGenerated   = false;
        s_MatchConfigInited = true;
    }

    float scale   = pdguiScaleFactor();
    float dialogW = pdguiMenuWidth();
    float dialogH = pdguiMenuHeight();
    ImVec2 menuPos = pdguiMenuPos();
    float dialogX  = menuPos.x;
    float dialogY  = menuPos.y;

    float pdTitleH = pdguiScale(26.0f);
    float tabBarH  = pdguiScale(30.0f);
    float footerH  = pdguiScale(60.0f);
    float contentH = dialogH - pdTitleH - tabBarH - footerH;

    ImGui::SetNextWindowPos(ImVec2(dialogX, dialogY));
    ImGui::SetNextWindowSize(ImVec2(dialogW, dialogH));

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##room_interior", nullptr, wflags)) {
        ImGui::End();
        return;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
    }

    /* Opaque backdrop */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(dialogX, dialogY),
                          ImVec2(dialogX + dialogW, dialogY + dialogH),
                          IM_COL32(8, 8, 16, 255));
    }

    pdguiDrawPdDialog(dialogX, dialogY, dialogW, dialogH, "Room", 1);

    /* ---- Title bar ---- */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(dialogX + 8.0f, dialogY + 2.0f,
                          dialogW - 16.0f, pdTitleH - 4.0f);

        const char *title = "Room";
        ImVec2 ts = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(dialogX + (dialogW - ts.x) * 0.5f,
                           dialogY + (pdTitleH - ts.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), title);
    }

    float curY = pdTitleH + ImGui::GetStyle().WindowPadding.y;
    ImGui::SetCursorPosY(curY);

    /* Connect code (server host) */
    if (netGetMode() == NETMODE_SERVER) {
        if (!s_CodeGenerated) {
            const char *ip = netGetPublicIP();
            if (ip) {
                u32 a = 0, b = 0, c = 0, d = 0;
                if (sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
                    u32 ipAddr = (a << 24) | (b << 16) | (c << 8) | d;
                    connectCodeEncode(ipAddr, s_ConnectCode, sizeof(s_ConnectCode));
                    s_CodeGenerated = true;
                }
            }
        }
        if (s_ConnectCode[0]) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Code: %s", s_ConnectCode);
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy")) {
                SDL_SetClipboardText(s_ConnectCode);
            }
        }
        curY = ImGui::GetCursorPosY();
    }

    /* ---- Tab bar ---- */
    ImGui::SetCursorPosY(curY);

    bool isLeader = lobbyIsLocalLeader() != 0;

    static const char *s_TabNames[] = {
        "Combat Simulator", "Campaign", "Counter-Operative"
    };

    ImGui::PushStyleColor(ImGuiCol_Tab,        ImVec4(0.10f, 0.15f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.20f, 0.30f, 0.55f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabSelected,ImVec4(0.15f, 0.25f, 0.65f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, ImVec4(0.3f, 0.6f, 1.0f, 1.0f));

    if (ImGui::BeginTabBar("##room_tabs")) {
        for (int t = 0; t < 3; t++) {
            bool tabOpen = ImGui::BeginTabItem(s_TabNames[t]);
            if (tabOpen) {
                if (s_ActiveTab != t) {
                    s_ActiveTab = t;
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::PopStyleColor(4);

    /* ---- Two-column content: left = settings, right = player list ---- */
    float pad  = 6.0f * scale;
    float leftW  = dialogW * 0.55f - pad;
    float rightW = dialogW * 0.40f;

    /* Left panel */
    ImGui::SetCursorPosY(ImGui::GetCursorPosY());
    switch (s_ActiveTab) {
        case 0: renderCombatSimTab(leftW, contentH, isLeader); break;
        case 1: renderCampaignTab (leftW, contentH, isLeader); break;
        case 2: renderCounterOpTab(leftW, contentH, isLeader); break;
    }

    ImGui::SameLine(0.0f, pad);

    /* Right panel */
    renderPlayerPanel(rightW, contentH, isLeader);

    /* ---- Footer ---- */
    ImGui::Separator();
    ImGui::Spacing();

    float btnH = pdguiScale(28.0f);

    if (isLeader) {
        float startW = pdguiScale(140.0f);
        if (ImGui::Button("Start Match", ImVec2(startW, btnH))) {
            pdguiPlaySound(PDGUI_SND_SELECT);

            switch (s_ActiveTab) {
                case 0: {
                    /* Combat Simulator */
                    int numBots = countBots();
                    u8 simType  = getLeadSimType();
                    netLobbyRequestStartWithSims(
                        GAMEMODE_MP,
                        (u8)s_Arenas[s_SelectedArena].stagenum,
                        0,
                        (u8)numBots,
                        simType);
                    break;
                }
                case 1:
                    /* Campaign */
                    netLobbyRequestStart(
                        GAMEMODE_COOP,
                        s_Missions[s_CampaignMission].stagenum,
                        (u8)s_CampaignDiff);
                    break;
                case 2:
                    /* Counter-Operative */
                    netLobbyRequestStart(
                        GAMEMODE_ANTI,
                        s_Missions[s_CounterOpMission].stagenum,
                        (u8)s_CounterOpDiff);
                    break;
            }
        }

        ImGui::SameLine();

        /* Status hint */
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.5f, 0.9f),
                           "Configure match, then Start.");
    } else {
        ImGui::TextDisabled("Waiting for the room leader to start...");
    }

    /* Leave Room (right-aligned) */
    float leaveW = pdguiScale(120.0f);
    ImGui::SameLine(dialogW - leaveW - ImGui::GetStyle().WindowPadding.x * 2);
    if (ImGui::Button("Leave Room", ImVec2(leaveW, btnH)) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        s_MatchConfigInited = false;  /* reset on next enter */
        s_CodeGenerated     = false;
        pdguiSetInRoom(0);  /* return to social lobby, stay connected */
    }

    /* ---- Bot settings modal ---- */
    if (s_BotModalOpen) {
        ImGui::OpenPopup("Bot Settings##botmodal");
        s_BotModalOpen = false;
    }

    ImGui::SetNextWindowSize(ImVec2(pdguiScale(300.0f), 0.0f));
    if (ImGui::BeginPopupModal("Bot Settings##botmodal", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        if (s_EditBotSlotIdx >= 1
            && s_EditBotSlotIdx < g_MatchConfig.numSlots
            && g_MatchConfig.slots[s_EditBotSlotIdx].type == SLOT_BOT) {

            struct matchslot *sl = &g_MatchConfig.slots[s_EditBotSlotIdx];
            float mw = pdguiScale(260.0f);

            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Bot Settings");
            ImGui::Separator();
            ImGui::Spacing();

            /* Fixed label column offset keeps controls left-aligned */
            float labelCol = 110.0f * scale;

            /* Name */
            ImGui::Text("Name:");
            ImGui::SameLine(labelCol);
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##botmodalname", sl->name, MAX_PLAYER_NAME);

            /* Difficulty */
            ImGui::Text("Difficulty:");
            ImGui::SameLine(labelCol);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##botmodaldiff",
                                   s_SimDiffNames[sl->botDifficulty])) {
                for (int d = 0; d < s_NumSimDiffs; d++) {
                    bool sel = (d == (int)sl->botDifficulty);
                    if (ImGui::Selectable(s_SimDiffNames[d], sel)) {
                        sl->botDifficulty = (u8)d;
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            /* Character */
            u32 numBodies = mpGetNumBodies();
            const char *curBody = (sl->bodynum < (u8)numBodies)
                                   ? mpGetBodyName(sl->bodynum) : "?";
            ImGui::Text("Character:");
            ImGui::SameLine(labelCol);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##botmodalchar",
                                   curBody ? curBody : "?")) {
                for (u32 b = 0; b < numBodies; b++) {
                    char *bodyName = mpGetBodyName((u8)b);
                    if (!bodyName || !bodyName[0]) continue;
                    bool sel = (b == (u32)sl->bodynum);
                    char bLabel[64];
                    snprintf(bLabel, sizeof(bLabel), "%s##mb%u", bodyName, b);
                    if (ImGui::Selectable(bLabel, sel)) {
                        sl->bodynum = (u8)b;
                        sl->headnum = (u8)b;
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Done", ImVec2(pdguiScale(80.0f), 0.0f))) {
                s_EditBotSlotIdx = -1;
                ImGui::CloseCurrentPopup();
                pdguiPlaySound(PDGUI_SND_SELECT);
            }
        } else {
            ImGui::TextDisabled("No bot selected.");
            if (ImGui::Button("Close")) {
                s_EditBotSlotIdx = -1;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

/* ========================================================================
 * Reset state when lobby session ends (called externally if needed)
 * ======================================================================== */

extern "C" void pdguiRoomScreenReset(void)
{
    s_MatchConfigInited = false;
    s_CodeGenerated     = false;
    s_ActiveTab         = 0;
    s_CampaignMission   = 0;
    s_CampaignDiff      = DIFF_A;
    s_CounterOpMission  = 0;
    s_CounterOpDiff     = DIFF_A;
    s_CounterOpPlayer   = 0;
    s_SelectedArena     = 0;
    s_SelectedBotSlot   = -1;
    s_BotModalOpen      = false;
    s_EditBotSlotIdx    = -1;
    s_SpawnWeaponIdx    = 0;
}
