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
 *   - Combat Sim: netLobbyRequestStartWithSims(GAMEMODE_MP, stagenum, 0, numBots, simType, timelimit, options, scenario, scorelimit, teamscorelimit)
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
#include <stdlib.h>

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

#include "assetcatalog.h"
char *langGet(s32 textid);

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

/* Max human players — must match MAX_PLAYERS in src/include/constants.h.
 * Cannot include constants.h here (types.h bool conflict with C++). */
#define MAX_PLAYERS 8

/* Bridge: send CLC_LOBBY_START.
 * stage_id: catalog ID string ("base:mp_complex", "base:defection", etc.) */
s32 netLobbyRequestStart(u8 gamemode, const char *stage_id, u8 difficulty);
s32 netLobbyRequestStartWithSims(u8 gamemode, const char *stage_id, u8 difficulty,
                                  u8 numSims, u8 simType, u8 timelimit, u32 options,
                                  u8 scenario, u8 scorelimit, u16 teamscorelimit,
                                  u8 weaponSetIndex);

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

/* Match config types and API — canonical definitions in scenario_save.h */
#include "scenario_save.h"

/* scenarioSave / scenarioLoad / scenarioListFiles are also declared there */

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

/* Solo match start (matchsetup.c) — configure g_MpSetup from g_MatchConfig + call mpStartMatch() */
s32 matchStart(void);

/* Solo room close — defined in pdgui_lobby.cpp */
void pdguiSoloRoomClose(void);

/* Bot randomization (matchsetup.c) */
void matchConfigRerollBot(s32 idx);

} /* extern "C" */

/* Arena name resolver — defined in pdgui_menu_matchsetup.cpp.
 * Checks a hardcoded override table before calling langGet() to work around
 * the AIO mod language file returning wrong strings for IDs 0x5126-0x5152. */
extern const char *arenaGetName(u16 textId);

/* ========================================================================
 * Arena list — built from the asset catalog at room init.
 * Replaces the old hardcoded table: catalog is the single source of truth.
 * ======================================================================== */

/* Catalog resolution — used to convert co-op mission stagenums to catalog IDs. */
const char *catalogResolveStageByStagenum(s32 stagenum);

struct arena_entry { char name[64]; char id[64]; s32 stagenum; };

static arena_entry *s_Arenas = NULL;
static int s_NumArenas = 0;
static int s_ArenasCapacity = 0;
static bool s_ArenasBuilt = false;

static void catalogArenaCollect(const asset_entry_t *e, void *userdata)
{
    (void)userdata;
    if (s_NumArenas >= s_ArenasCapacity) {
        int newCap = (s_ArenasCapacity == 0) ? 32 : s_ArenasCapacity * 2;
        arena_entry *newBuf = (arena_entry *)realloc(s_Arenas, newCap * sizeof(arena_entry));
        if (!newBuf) {
            sysLogPrintf(LOG_WARNING, "CATALOG: arena list realloc failed at %d entries", s_NumArenas);
            return;
        }
        s_Arenas = newBuf;
        s_ArenasCapacity = newCap;
    }

    const char *name = arenaGetName((u16)e->ext.arena.name_langid);
    if (!name || !name[0]) {
        sysLogPrintf(LOG_WARNING,
            "CATALOG: arena stagenum=0x%02x langid=0x%04x has no name, skipping",
            e->ext.arena.stagenum, e->ext.arena.name_langid);
        return;
    }

    strncpy(s_Arenas[s_NumArenas].name, name, 63);
    s_Arenas[s_NumArenas].name[63] = '\0';
    strncpy(s_Arenas[s_NumArenas].id, e->id, 63);
    s_Arenas[s_NumArenas].id[63] = '\0';
    s_Arenas[s_NumArenas].stagenum = e->ext.arena.stagenum;
    sysLogPrintf(LOG_NOTE, "CATALOG: arena[%d] \"%s\" id='%s' stagenum=0x%02x registered",
        s_NumArenas, s_Arenas[s_NumArenas].name, s_Arenas[s_NumArenas].id,
        s_Arenas[s_NumArenas].stagenum);
    s_NumArenas++;
}

static void buildArenaListFromCatalog(void)
{
    free(s_Arenas);
    s_Arenas = NULL;
    s_NumArenas = 0;
    s_ArenasCapacity = 0;
    sysLogPrintf(LOG_NOTE, "CATALOG: building arena list from catalog (%d ASSET_ARENA entries)",
        assetCatalogGetCountByType(ASSET_ARENA));
    assetCatalogIterateByType(ASSET_ARENA, catalogArenaCollect, NULL);
    sysLogPrintf(LOG_NOTE, "CATALOG: arena list built: %d arenas", s_NumArenas);
    s_ArenasBuilt = true;
}

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

/* Solo (offline) mode — set by pdguiRoomScreenSetSolo().
 * When true: no network calls, matchStart() used instead of netLobbyRequestStartWithSims(),
 * always leader, no connect code, "Back to Menu" instead of "Leave Room". */
static bool s_IsSoloMode = false;

/* Track if we've initialized g_MatchConfig for this lobby session */
static bool s_MatchConfigInited = false;

/* Campaign / Counter-Op settings */
static int s_CampaignMission   = 0;
static int s_CampaignDiff      = DIFF_A;
static int s_CounterOpMission  = 0;
static int s_CounterOpDiff     = DIFF_A;
static int s_CounterOpPlayer   = 0;  /* index into lobby player list = the counter-op player */

/* Arena picker state (index into s_Arenas[]) */
static int s_SelectedArena = 0;

/* Bot management state — multi-select */
static bool s_BotSelected[MATCH_MAX_SLOTS]; /* per-slot selection */
static int  s_BotSelectCount  = 0;          /* cached count of selected bots */
static bool s_BotModalOpen    = false;
static int  s_EditBotSlotIdx  = -1;         /* slot index being edited in the modal */

/* Bot type names for context menu */
static const char *s_BotTypeNames[] = {
    "Normal", "Peace", "Shield", "Rocket", "Kaze", "Fist",
    "Prey", "Coward", "Judge", "Feud", "Speed", "Turtle", "Venge",
};
static const int s_NumBotTypes = 13;

static void botSelectClear(void) {
    memset(s_BotSelected, 0, sizeof(s_BotSelected));
    s_BotSelectCount = 0;
}

static void botSelectSet(int idx) {
    botSelectClear();
    if (idx >= 0 && idx < MATCH_MAX_SLOTS) {
        s_BotSelected[idx] = true;
        s_BotSelectCount = 1;
    }
}

static void botSelectToggle(int idx) {
    if (idx < 0 || idx >= MATCH_MAX_SLOTS) return;
    s_BotSelected[idx] = !s_BotSelected[idx];
    s_BotSelectCount += s_BotSelected[idx] ? 1 : -1;
}

static int botSelectFirst(void) {
    for (int i = 0; i < MATCH_MAX_SLOTS; i++) {
        if (s_BotSelected[i]) return i;
    }
    return -1;
}

/* Spawn weapon picker — index into s_SpawnWeapons (0 = Random) */
static int s_SpawnWeaponIdx = 0;

/* Connect code cache (server host display) */
static char s_ConnectCode[128]  = "";
static bool s_CodeGenerated     = false;

/* Scenario save/load popup state */
static bool s_ShowSaveScenario  = false;
static bool s_ShowLoadScenario  = false;
static char s_SaveNameBuf[64]   = "";
static char s_ScenarioFiles[SCENARIO_MAX_LIST][SCENARIO_PATH_MAX];
static int  s_ScenarioCount     = 0;
static int  s_ScenarioSelected  = -1;
static char s_ScenarioStatusMsg[128] = "";

/* ========================================================================
 * Level Editor — state, data tables, and catalog helpers (tab 3)
 * ======================================================================== */

#define LE_MAX_SPAWNED      128     /* max objects that can be spawned */
#define LE_CATALOG_MAX      512     /* catalog snapshot size */

#define LE_INTERACT_STATIC  0
#define LE_INTERACT_PICKUP  1
#define LE_INTERACT_USE     2
#define LE_INTERACT_DOOR    3

/* Spawned object descriptor */
struct le_spawned_t {
    char  id[64];           /* catalog asset ID */
    int   asset_type;       /* asset_type_e value */
    float pos[3];           /* world position (set to camera pos on spawn) */
    float scale[3];         /* per-axis scale */
    bool  uniform_scale;    /* when true, X drives Y and Z */
    int   tex_override_idx; /* index into catalog texture list, -1 = default */
    bool  collision;        /* collision enabled */
    int   interaction;      /* LE_INTERACT_* */
};

/* Catalog snapshot entry for the browser */
struct le_cat_entry_t { char id[64]; int type; };

static le_spawned_t   s_LESpawned[LE_MAX_SPAWNED];
static int            s_LENumSpawned        = 0;
static int            s_LESelectedSpawned   = -1;

static le_cat_entry_t s_LECatalog[LE_CATALOG_MAX];
static int            s_LECatalogCount      = 0;
static bool           s_LECatalogBuilt      = false;
static int            s_LECatalogBuiltType  = -99;

/* Catalog browser UI state */
static int  s_LETypeFilter = 0;
static char s_LESearch[64] = "";
static char s_LESelId[64]  = "";
static int  s_LESelType    = 0;

/* Whether the editor overlay is active */
static bool  s_LEActive     = false;

/* Stub free-fly camera state shown in overlay */
static float s_LECamPos[3]  = { 0.0f, 100.0f, 0.0f };
static float s_LECamYaw     = 0.0f;
static float s_LECamPitch   = 0.0f;

/* Type filter table */
struct le_type_filter_t { const char *label; int type; };

static const le_type_filter_t s_LETypeFilters[] = {
    { "All",        -1              },
    { "Props",      ASSET_PROP      },
    { "Characters", ASSET_CHARACTER },
    { "Weapons",    ASSET_WEAPON    },
    { "Models",     ASSET_MODEL     },
    { "Vehicles",   ASSET_VEHICLE   },
    { "Maps",       ASSET_MAP       },
    { "Textures",   ASSET_TEXTURE   },
    { "Skins",      ASSET_SKIN      },
};
static const int s_LENumTypeFilters =
    (int)(sizeof(s_LETypeFilters) / sizeof(s_LETypeFilters[0]));

/* Interaction type names */
static const char *s_LEInteractNames[] = { "Static", "Pickup", "Use", "Door" };
static const int   s_LENumInteractTypes = 4;

/* Catalog collect callback */
static void leCatalogCollect(const asset_entry_t *e, void *userdata)
{
    (void)userdata;
    if (s_LECatalogCount >= LE_CATALOG_MAX) return;
    strncpy(s_LECatalog[s_LECatalogCount].id, e->id, 63);
    s_LECatalog[s_LECatalogCount].id[63] = '\0';
    s_LECatalog[s_LECatalogCount].type   = (int)e->type;
    s_LECatalogCount++;
}

/* Build or rebuild the catalog snapshot for the current type filter */
static void leBuildCatalog(void)
{
    int filterType = s_LETypeFilters[s_LETypeFilter].type;
    int t;
    s_LECatalogCount = 0;
    if (filterType < 0) {
        for (t = 1; t < (int)ASSET_TYPE_COUNT; t++) {
            assetCatalogIterateByType((asset_type_e)t, leCatalogCollect, NULL);
        }
    } else {
        assetCatalogIterateByType((asset_type_e)filterType, leCatalogCollect, NULL);
    }
    s_LECatalogBuilt     = true;
    s_LECatalogBuiltType = filterType;
}

/* ========================================================================
 * Level Editor — left panel: categorized catalog browser + spawn
 * ======================================================================== */

static void renderLevelEditorTab(float panelW, float panelH)
{
    float comboW     = panelW - ImGui::GetStyle().WindowPadding.x * 2.0f;
    float btnH       = pdguiScale(26.0f);
    int   filterType;
    float listH;
    float usedY;

    ImGui::BeginChild("##le_left", ImVec2(panelW, panelH), false);

    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Level Editor");
    ImGui::TextDisabled("Spawn catalog assets into an empty level and explore freely.");
    ImGui::Separator();
    ImGui::Spacing();

    /* Asset type filter */
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Asset Type");
    ImGui::SetNextItemWidth(comboW);
    if (ImGui::BeginCombo("##le_type", s_LETypeFilters[s_LETypeFilter].label)) {
        for (int i = 0; i < s_LENumTypeFilters; i++) {
            bool sel = (i == s_LETypeFilter);
            if (ImGui::Selectable(s_LETypeFilters[i].label, sel)) {
                s_LETypeFilter   = i;
                s_LECatalogBuilt = false;
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();

    /* Search */
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Search");
    ImGui::SetNextItemWidth(comboW);
    ImGui::InputText("##le_search", s_LESearch, sizeof(s_LESearch));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* Rebuild catalog snapshot when filter changes */
    filterType = s_LETypeFilters[s_LETypeFilter].type;
    if (!s_LECatalogBuilt || s_LECatalogBuiltType != filterType) {
        leBuildCatalog();
    }

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f),
                       "Catalog  (%d entries)", s_LECatalogCount);

    usedY = ImGui::GetCursorPosY();
    listH = panelH - usedY - btnH
            - ImGui::GetStyle().ItemSpacing.y * 3.0f
            - ImGui::GetStyle().WindowPadding.y;
    if (listH < pdguiScale(60.0f)) listH = pdguiScale(60.0f);

    ImGui::BeginChild("##le_catlist", ImVec2(comboW, listH), true,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    for (int i = 0; i < s_LECatalogCount; i++) {
        /* Case-insensitive substring search */
        if (s_LESearch[0] != '\0') {
            char loID[64], loQ[64];
            int  j;
            const char *id = s_LECatalog[i].id;
            const char *q  = s_LESearch;
            for (j = 0; j < 63 && id[j]; j++)
                loID[j] = (char)tolower((unsigned char)id[j]);
            loID[j] = '\0';
            for (j = 0; j < 63 && q[j]; j++)
                loQ[j] = (char)tolower((unsigned char)q[j]);
            loQ[j] = '\0';
            if (!strstr(loID, loQ)) continue;
        }

        ImGui::PushID(i);
        bool isSel = (strcmp(s_LESelId, s_LECatalog[i].id) == 0);

        /* Short type tag */
        const char *tag;
        switch ((asset_type_e)s_LECatalog[i].type) {
            case ASSET_PROP:        tag = "PROP"; break;
            case ASSET_CHARACTER:   tag = "CHR";  break;
            case ASSET_WEAPON:      tag = "WPN";  break;
            case ASSET_MODEL:       tag = "MDL";  break;
            case ASSET_VEHICLE:     tag = "VEH";  break;
            case ASSET_MAP:         tag = "MAP";  break;
            case ASSET_TEXTURE:     tag = "TEX";  break;
            case ASSET_SKIN:        tag = "SKN";  break;
            case ASSET_ARENA:       tag = "AREA"; break;
            case ASSET_BODY:        tag = "BODY"; break;
            case ASSET_HEAD:        tag = "HEAD"; break;
            case ASSET_AUDIO:       tag = "SFX";  break;
            default:                tag = "???";  break;
        }

        char row[96];
        snprintf(row, sizeof(row), "[%s] %s", tag, s_LECatalog[i].id);

        if (ImGui::Selectable(row, isSel)) {
            strncpy(s_LESelId, s_LECatalog[i].id, 63);
            s_LESelId[63] = '\0';
            s_LESelType   = s_LECatalog[i].type;
            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
        }
        ImGui::PopID();
    }

    ImGui::EndChild(); /* ##le_catlist */

    ImGui::Spacing();

    /* Spawn button — only active when editor running and an entry is selected */
    {
        bool canSpawn = s_LEActive
                        && (s_LESelId[0] != '\0')
                        && (s_LENumSpawned < LE_MAX_SPAWNED);
        if (!canSpawn) ImGui::BeginDisabled();
        if (ImGui::Button("Spawn at Camera##le_spawn", ImVec2(comboW, btnH))) {
            le_spawned_t *obj = &s_LESpawned[s_LENumSpawned++];
            strncpy(obj->id, s_LESelId, 63);
            obj->id[63]           = '\0';
            obj->asset_type       = s_LESelType;
            obj->pos[0]           = s_LECamPos[0];
            obj->pos[1]           = s_LECamPos[1];
            obj->pos[2]           = s_LECamPos[2];
            obj->scale[0]         = 1.0f;
            obj->scale[1]         = 1.0f;
            obj->scale[2]         = 1.0f;
            obj->uniform_scale    = true;
            obj->tex_override_idx = -1;
            obj->collision        = true;
            obj->interaction      = LE_INTERACT_STATIC;
            s_LESelectedSpawned   = s_LENumSpawned - 1;
            sysLogPrintf(LOG_NOTE,
                "LEVEL_EDITOR: spawned \"%s\" (type %d) at (%.1f, %.1f, %.1f)",
                obj->id, obj->asset_type,
                obj->pos[0], obj->pos[1], obj->pos[2]);
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
        if (!canSpawn) ImGui::EndDisabled();
    }

    if (!s_LEActive) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.3f, 1.0f),
                           "Launch the editor below to enable spawning.");
    }

    ImGui::EndChild(); /* ##le_left */
}

/* ========================================================================
 * Level Editor — right panel: spawned object list + property editor
 * ======================================================================== */

static void renderLevelEditorObjectPanel(float panelW, float panelH)
{
    float scale  = pdguiScaleFactor();
    float btnH   = pdguiScale(24.0f);
    float fieldW = 0.0f;

    ImGui::BeginChild("##le_right_outer", ImVec2(panelW, panelH), true);

    /* ---- Spawned objects list ---- */
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
                       "Spawned Objects  (%d)", s_LENumSpawned);
    ImGui::Separator();

    {
        float listH = panelH * 0.38f;
        ImGui::BeginChild("##le_spawned_list", ImVec2(0.0f, listH), false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);

        if (s_LENumSpawned == 0) {
            ImGui::TextDisabled("(none yet)");
        }
        for (int i = 0; i < s_LENumSpawned; i++) {
            le_spawned_t *obj = &s_LESpawned[i];
            ImGui::PushID(i);

            bool isSel = (i == s_LESelectedSpawned);
            char rowlabel[80];
            snprintf(rowlabel, sizeof(rowlabel), "%s##obj%d", obj->id, i);
            if (ImGui::Selectable(rowlabel, isSel,
                                  ImGuiSelectableFlags_None,
                                  ImVec2(panelW - pdguiScale(32.0f), 0.0f))) {
                s_LESelectedSpawned = i;
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            }
            ImGui::SameLine();
            {
                char delLabel[16];
                snprintf(delLabel, sizeof(delLabel), "X##d%d", i);
                if (ImGui::SmallButton(delLabel)) {
                    s_LESpawned[i] = s_LESpawned[s_LENumSpawned - 1];
                    s_LENumSpawned--;
                    if (s_LESelectedSpawned >= s_LENumSpawned)
                        s_LESelectedSpawned = s_LENumSpawned - 1;
                    pdguiPlaySound(PDGUI_SND_KBCANCEL);
                }
            }
            ImGui::PopID();
        }
        ImGui::EndChild(); /* ##le_spawned_list */
    }

    ImGui::Separator();
    ImGui::Spacing();

    /* ---- Property editor ---- */
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Properties");

    if (s_LESelectedSpawned < 0 || s_LESelectedSpawned >= s_LENumSpawned) {
        ImGui::TextDisabled("Select a spawned object to edit.");
    } else {
        le_spawned_t *obj = &s_LESpawned[s_LESelectedSpawned];
        fieldW = panelW
                 - 80.0f * scale
                 - ImGui::GetStyle().WindowPadding.x * 2.0f
                 - ImGui::GetStyle().ItemSpacing.x;
        if (fieldW < pdguiScale(60.0f)) fieldW = pdguiScale(60.0f);

        /* Position (read-only) */
        ImGui::Text("Position:");
        ImGui::SameLine(80.0f * scale);
        ImGui::TextDisabled("(%.0f, %.0f, %.0f)",
                            obj->pos[0], obj->pos[1], obj->pos[2]);

        ImGui::Spacing();

        /* Scale */
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Scale");
        ImGui::Checkbox("Uniform##le_uni", &obj->uniform_scale);
        if (obj->uniform_scale) {
            ImGui::SetNextItemWidth(fieldW);
            if (ImGui::SliderFloat("##le_scaleU", &obj->scale[0],
                                   0.1f, 10.0f, "%.2f")) {
                obj->scale[1] = obj->scale[0];
                obj->scale[2] = obj->scale[0];
            }
        } else {
            ImGui::SetNextItemWidth(fieldW);
            ImGui::SliderFloat("X##le_scaleX", &obj->scale[0], 0.1f, 10.0f, "%.2f");
            ImGui::SetNextItemWidth(fieldW);
            ImGui::SliderFloat("Y##le_scaleY", &obj->scale[1], 0.1f, 10.0f, "%.2f");
            ImGui::SetNextItemWidth(fieldW);
            ImGui::SliderFloat("Z##le_scaleZ", &obj->scale[2], 0.1f, 10.0f, "%.2f");
        }

        ImGui::Spacing();

        /* Collision */
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Collision");
        ImGui::Checkbox("Enabled##le_col", &obj->collision);

        ImGui::Spacing();

        /* Interaction type */
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Interaction");
        ImGui::SetNextItemWidth(fieldW);
        if (ImGui::BeginCombo("##le_interact",
                              s_LEInteractNames[obj->interaction])) {
            for (int ii = 0; ii < s_LENumInteractTypes; ii++) {
                bool sel = (ii == obj->interaction);
                if (ImGui::Selectable(s_LEInteractNames[ii], sel)) {
                    obj->interaction = ii;
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();

        /* Texture override */
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Texture Override");
        {
            int texCount = (int)assetCatalogGetCountByType(ASSET_TEXTURE);
            if (texCount == 0) {
                ImGui::TextDisabled("(no textures in catalog)");
            } else {
                int  showMax  = (texCount < 32) ? texCount : 32;
                const char *texLabel = (obj->tex_override_idx < 0)
                                       ? "Default" : "Custom";
                ImGui::SetNextItemWidth(fieldW);
                if (ImGui::BeginCombo("##le_tex", texLabel)) {
                    bool selDef = (obj->tex_override_idx < 0);
                    if (ImGui::Selectable("Default", selDef)) {
                        obj->tex_override_idx = -1;
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    }
                    if (selDef) ImGui::SetItemDefaultFocus();
                    for (int ti = 0; ti < showMax; ti++) {
                        char texEntry[32];
                        snprintf(texEntry, sizeof(texEntry), "Texture %d", ti);
                        bool selT = (obj->tex_override_idx == ti);
                        if (ImGui::Selectable(texEntry, selT)) {
                            obj->tex_override_idx = ti;
                            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                        }
                        if (selT) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                if (texCount > 32) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%d total)", texCount);
                }
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        /* Clear All */
        if (s_LENumSpawned > 0) {
            if (ImGui::Button("Clear All##le_clearall",
                              ImVec2(-1.0f, btnH))) {
                s_LENumSpawned      = 0;
                s_LESelectedSpawned = -1;
                pdguiPlaySound(PDGUI_SND_KBCANCEL);
            }
        }
    }

    ImGui::EndChild(); /* ##le_right_outer */
}

/* ========================================================================
 * Level Editor — in-game floating overlay (shown when s_LEActive)
 * ======================================================================== */

static void renderLevelEditorOverlay(void)
{
    float oW     = pdguiScale(280.0f);
    float oH     = pdguiScale(340.0f);
    float exitW;
    ImVec2 disp  = ImGui::GetIO().DisplaySize;

    /* Pin to top-right corner */
    ImGui::SetNextWindowPos(
        ImVec2(disp.x - oW - pdguiScale(8.0f), pdguiScale(8.0f)),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(oW, oH));
    ImGui::SetNextWindowBgAlpha(0.85f);

    ImGuiWindowFlags oflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (!ImGui::Begin("Level Editor##leoverlay", nullptr, oflags)) {
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Level Editor  [Active]");
    ImGui::Separator();
    ImGui::Spacing();

    /* Camera info — stub until free-fly is wired to game camera */
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Camera");
    ImGui::Text("Pos:   %.1f, %.1f, %.1f",
                s_LECamPos[0], s_LECamPos[1], s_LECamPos[2]);
    ImGui::Text("Yaw: %.1f   Pitch: %.1f", s_LECamYaw, s_LECamPitch);
    ImGui::TextDisabled("(free-fly: WASD + mouse -- in development)");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f),
                       "Objects: %d / %d", s_LENumSpawned, LE_MAX_SPAWNED);

    /* Selected object summary */
    if (s_LESelectedSpawned >= 0 && s_LESelectedSpawned < s_LENumSpawned) {
        le_spawned_t *obj = &s_LESpawned[s_LESelectedSpawned];
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Selected:");
        ImGui::Text("  %s", obj->id);
        ImGui::Text("  Pos (%.0f, %.0f, %.0f)",
                    obj->pos[0], obj->pos[1], obj->pos[2]);
        ImGui::Text("  Scale %.2f x %.2f x %.2f",
                    obj->scale[0], obj->scale[1], obj->scale[2]);
        ImGui::Text("  Collision: %s   Interact: %s",
                    obj->collision ? "on" : "off",
                    s_LEInteractNames[obj->interaction]);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    exitW = oW - ImGui::GetStyle().WindowPadding.x * 2.0f;
    if (ImGui::Button("Exit Level Editor##le_exit",
                      ImVec2(exitW, pdguiScale(26.0f)))) {
        s_LEActive = false;
        sysLogPrintf(LOG_NOTE, "LEVEL_EDITOR: overlay closed.");
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
    }

    ImGui::End();
}

/* ========================================================================
 * Helper: sync s_SpawnWeaponIdx from g_MatchConfig.spawnWeaponNum
 * Called after loading a scenario so the picker shows the right entry.
 * ======================================================================== */

static void syncSpawnWeaponFromConfig(void)
{
    for (int i = 0; i < s_NumSpawnWeapons; i++) {
        if (s_SpawnWeapons[i].weaponnum == g_MatchConfig.spawnWeaponNum) {
            s_SpawnWeaponIdx = i;
            return;
        }
    }
    s_SpawnWeaponIdx = 0;  /* default to Random */
}

/* ========================================================================
 * Helper: ensure arena index is consistent with g_MatchConfig.stage_id
 * ======================================================================== */

static void syncArenaFromConfig(void)
{
    if (s_NumArenas == 0) return;

    /* Match by catalog ID (PRIMARY). */
    if (g_MatchConfig.stage_id[0]) {
        for (int i = 0; i < s_NumArenas; i++) {
            if (strcmp(s_Arenas[i].id, g_MatchConfig.stage_id) == 0) {
                s_SelectedArena = i;
                return;
            }
        }
    }
    /* stage_id not in list — clamp and write back the selected arena's id. */
    if (s_SelectedArena >= s_NumArenas) s_SelectedArena = 0;
    strncpy(g_MatchConfig.stage_id, s_Arenas[s_SelectedArena].id,
            sizeof(g_MatchConfig.stage_id) - 1);
    g_MatchConfig.stage_id[sizeof(g_MatchConfig.stage_id) - 1] = '\0';
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
    /* In solo mode there's no network lobby — local player count is always 1. */
    int humanCount = s_IsSoloMode ? 1 : lobbyGetPlayerCount();
    int curBots    = countBots();
    /* Max bots = remaining slots after accounting for human players. */
    int maxBots = MATCH_MAX_SLOTS - humanCount;
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

    if (s_IsSoloMode) {
        /* Solo mode: always exactly one local human player (g_MatchConfig.slots[0]) */
        const char *playerName = mpPlayerConfigGetName(0);
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", playerName ? playerName : "Player 1");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 0.7f), "(you)");
    } else {
        /* Network mode: show lobby player list */
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
    }

    /* Bot rows */
    if (curBots > 0) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.3f, 0.9f), "Bots");
    }

    /* Row layout */
    float rowW = panelW - ImGui::GetStyle().WindowPadding.x * 2.0f - 4.0f;

    int removeSlot = -1; /* deferred removal — can't remove while iterating */
    for (int i = 1; i < g_MatchConfig.numSlots; i++) {
        struct matchslot *sl = &g_MatchConfig.slots[i];
        if (sl->type != SLOT_BOT) continue;

        ImGui::PushID(i);

        bool selected = s_BotSelected[i];

        char rowLabel[80];
        snprintf(rowLabel, sizeof(rowLabel), "[BOT] %s", sl->name);

        /* Selectable row — click selects, ctrl-click toggles, double-click edits */
        if (ImGui::Selectable(rowLabel, selected,
                              ImGuiSelectableFlags_AllowDoubleClick,
                              ImVec2(rowW, 0.0f))) {
            bool ctrl = ImGui::GetIO().KeyCtrl;
            if (ctrl) {
                botSelectToggle(i);
            } else {
                botSelectSet(i);
            }
            if (ImGui::IsMouseDoubleClicked(0) && isLeader) {
                s_EditBotSlotIdx = i;
                s_BotModalOpen   = true;
            }
            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
        }

        /* Right-click context menu OR gamepad X button */
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) ||
            (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft))) {
            if (!s_BotSelected[i]) botSelectSet(i);
            ImGui::OpenPopup("##bot_ctx");
        }

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.4f, 0.8f),
                           "[%s]", s_SimDiffNames[sl->botDifficulty]);

        /* Context menu popup */
        if (ImGui::BeginPopup("##bot_ctx")) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
                               s_BotSelectCount > 1 ? "%d Bots Selected" : "Bot Options",
                               s_BotSelectCount);
            ImGui::Separator();

            /* Rename (single bot only) */
            if (s_BotSelectCount == 1 && isLeader) {
                int si = botSelectFirst();
                if (si >= 0 && si < g_MatchConfig.numSlots) {
                    ImGui::Text("Name:");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(pdguiScale(140.0f));
                    ImGui::InputText("##ctx_name", g_MatchConfig.slots[si].name, MAX_PLAYER_NAME);
                }
            }

            /* Bot AI (difficulty) — applies to all selected */
            if (isLeader && ImGui::BeginMenu("Bot AI")) {
                for (int d = 0; d < s_NumSimDiffs; d++) {
                    if (ImGui::MenuItem(s_SimDiffNames[d])) {
                        for (int j = 1; j < g_MatchConfig.numSlots; j++) {
                            if (s_BotSelected[j] && g_MatchConfig.slots[j].type == SLOT_BOT)
                                g_MatchConfig.slots[j].botDifficulty = (u8)d;
                        }
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    }
                }
                ImGui::EndMenu();
            }

            /* Bot Type — applies to all selected */
            if (isLeader && ImGui::BeginMenu("Bot Type")) {
                for (int t = 0; t < s_NumBotTypes; t++) {
                    if (ImGui::MenuItem(s_BotTypeNames[t])) {
                        for (int j = 1; j < g_MatchConfig.numSlots; j++) {
                            if (s_BotSelected[j] && g_MatchConfig.slots[j].type == SLOT_BOT)
                                g_MatchConfig.slots[j].botType = (u8)t;
                        }
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    }
                }
                ImGui::EndMenu();
            }

            /* Character — applies to all selected */
            if (isLeader && ImGui::BeginMenu("Character")) {
                u32 numBodies = mpGetNumBodies();
                for (u32 b = 0; b < numBodies; b++) {
                    char *bodyName = mpGetBodyName((u8)b);
                    if (!bodyName || !bodyName[0]) continue;
                    const char *bid = catalogResolveBodyByMpIndex((s32)b);
                    if (ImGui::MenuItem(bodyName)) {
                        const char *hid = catalogResolveHeadByMpIndex((s32)b);
                        for (int j = 1; j < g_MatchConfig.numSlots; j++) {
                            if (!s_BotSelected[j] || g_MatchConfig.slots[j].type != SLOT_BOT) continue;
                            if (bid) {
                                strncpy(g_MatchConfig.slots[j].body_id, bid, sizeof(g_MatchConfig.slots[j].body_id) - 1);
                                g_MatchConfig.slots[j].body_id[sizeof(g_MatchConfig.slots[j].body_id) - 1] = '\0';
                            }
                            if (hid) {
                                strncpy(g_MatchConfig.slots[j].head_id, hid, sizeof(g_MatchConfig.slots[j].head_id) - 1);
                                g_MatchConfig.slots[j].head_id[sizeof(g_MatchConfig.slots[j].head_id) - 1] = '\0';
                            }
                        }
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    }
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();

            /* Duplicate — copies selected bots */
            if (isLeader && ImGui::MenuItem("Duplicate")) {
                for (int j = g_MatchConfig.numSlots - 1; j >= 1; j--) {
                    if (s_BotSelected[j] && g_MatchConfig.slots[j].type == SLOT_BOT) {
                        struct matchslot *src = &g_MatchConfig.slots[j];
                        matchConfigAddBot(src->botType, src->botDifficulty,
                                          src->body_id, src->head_id, src->name);
                    }
                }
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            }

            /* Re-roll — random name + character for all selected */
            if (isLeader && ImGui::MenuItem("Re-roll")) {
                for (int j = 1; j < g_MatchConfig.numSlots; j++) {
                    if (s_BotSelected[j] && g_MatchConfig.slots[j].type == SLOT_BOT) {
                        matchConfigRerollBot(j);
                    }
                }
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            }

            /* Remove — delete all selected */
            if (isLeader && ImGui::MenuItem("Remove")) {
                /* Remove in reverse to avoid index shifting */
                for (int j = g_MatchConfig.numSlots - 1; j >= 1; j--) {
                    if (s_BotSelected[j] && g_MatchConfig.slots[j].type == SLOT_BOT) {
                        matchConfigRemoveSlot(j);
                    }
                }
                botSelectClear();
                pdguiPlaySound(PDGUI_SND_KBCANCEL);
            }

            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    /* Deferred removal */
    if (removeSlot >= 1) {
        matchConfigRemoveSlot(removeSlot);
        botSelectClear();
    }

    ImGui::EndChild(); /* ##room_players_list */

    ImGui::Separator();

    /* Add Bot button + slot count */
    bool canAdd = isLeader
                  && (curBots < maxBots)
                  && (g_MatchConfig.numSlots < MATCH_MAX_SLOTS);
    if (!canAdd) ImGui::BeginDisabled();
    if (ImGui::Button("Add Bot", ImVec2(-1.0f, btnH))) {
        /* Random bot — no explicit body/head/name triggers generators */
        matchConfigAddBot(0 /*BOTTYPE_NORMAL*/, 2 /*NormalSim*/, nullptr, nullptr, nullptr);
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
    const char *arenaLabel = (s_NumArenas > 0) ? s_Arenas[s_SelectedArena].name : "(none)";
    if (ImGui::BeginCombo("##arena", arenaLabel)) {
        for (int ai = 0; ai < s_NumArenas; ai++) {
            bool sel = (ai == s_SelectedArena);
            if (ImGui::Selectable(s_Arenas[ai].name, sel)) {
                s_SelectedArena = ai;
                strncpy(g_MatchConfig.stage_id, s_Arenas[ai].id,
                        sizeof(g_MatchConfig.stage_id) - 1);
                g_MatchConfig.stage_id[sizeof(g_MatchConfig.stage_id) - 1] = '\0';
                sysLogPrintf(LOG_NOTE, "ROOM: arena selected \"%s\" id='%s'",
                    s_Arenas[ai].name, s_Arenas[ai].id);
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
    optToggle        ("No Doors",        MPOPTION_NODOORS,           leader);

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

    ImGui::Spacing();
    ImGui::Separator();

    /* ---- Scenario Save / Load ---- */
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Scenarios");

    float halfW = (comboW - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    float sbtnH = pdguiScale(24.0f);

    if (!leader) ImGui::BeginDisabled();
    if (ImGui::Button("Save Scenario", ImVec2(halfW, sbtnH))) {
        s_ShowSaveScenario = true;
        s_SaveNameBuf[0] = '\0';
        s_ScenarioStatusMsg[0] = '\0';
        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Scenario", ImVec2(halfW, sbtnH))) {
        s_ScenarioCount = scenarioListFiles(s_ScenarioFiles, SCENARIO_MAX_LIST);
        s_ScenarioSelected = -1;
        s_ShowLoadScenario = true;
        s_ScenarioStatusMsg[0] = '\0';
        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
    }
    if (!leader) ImGui::EndDisabled();

    /* Status message (e.g. "Saved!" or "Loaded!") */
    if (s_ScenarioStatusMsg[0]) {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%s", s_ScenarioStatusMsg);
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
        if (!s_ArenasBuilt) {
            buildArenaListFromCatalog();
        }
        matchConfigInit();
        syncArenaFromConfig();
        botSelectClear();
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
        sysLogPrintf(LOG_NOTE, "MENU_IMGUI: room OPEN (solo=%d)", s_IsSoloMode);
    }

    /* Opaque backdrop */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(dialogX, dialogY),
                          ImVec2(dialogX + dialogW, dialogY + dialogH),
                          IM_COL32(8, 8, 16, 255));
    }

    const char *screenTitle = s_IsSoloMode ? "Combat Simulator" : "Room";
    pdguiDrawPdDialog(dialogX, dialogY, dialogW, dialogH, screenTitle, 1);

    /* ---- Title bar ---- */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(dialogX + 8.0f, dialogY + 2.0f,
                          dialogW - 16.0f, pdTitleH - 4.0f);

        ImVec2 ts = ImGui::CalcTextSize(screenTitle);
        dl->AddText(ImVec2(dialogX + (dialogW - ts.x) * 0.5f,
                           dialogY + (pdTitleH - ts.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), screenTitle);
    }

    float curY = pdTitleH + ImGui::GetStyle().WindowPadding.y;
    ImGui::SetCursorPosY(curY);

    /* Connect code (server host) — hidden in solo mode */
    if (!s_IsSoloMode && netGetMode() == NETMODE_SERVER) {
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

    /* In solo mode the local player is always the leader. */
    bool isLeader = s_IsSoloMode || (lobbyIsLocalLeader() != 0);

    static const char *s_TabNames[] = {
        "Combat Simulator", "Campaign", "Counter-Operative", "Level Editor"
    };

    ImGui::PushStyleColor(ImGuiCol_Tab,        ImVec4(0.10f, 0.15f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.20f, 0.30f, 0.55f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabSelected,ImVec4(0.15f, 0.25f, 0.65f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, ImVec4(0.3f, 0.6f, 1.0f, 1.0f));

    if (ImGui::BeginTabBar("##room_tabs")) {
        for (int t = 0; t < 4; t++) {
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
        case 3: renderLevelEditorTab(leftW, contentH); break;
    }

    ImGui::SameLine(0.0f, pad);

    /* Right panel — Level Editor uses its own object/property panel */
    if (s_ActiveTab == 3) {
        renderLevelEditorObjectPanel(rightW, contentH);
    } else {
        renderPlayerPanel(rightW, contentH, isLeader);
    }

    /* ---- Footer ---- */
    ImGui::Separator();
    ImGui::Spacing();

    float btnH = pdguiScale(28.0f);

    if (s_ActiveTab == 3) {
        /* Level Editor tab: everyone can launch their own editor session */
        float launchW = pdguiScale(180.0f);
        if (ImGui::Button("Launch Level Editor##le_launch",
                          ImVec2(launchW, btnH))) {
            s_LEActive          = true;
            s_LENumSpawned      = 0;
            s_LESelectedSpawned = -1;
            sysLogPrintf(LOG_NOTE,
                "LEVEL_EDITOR: editor activated (free-fly camera in development).");
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
        if (s_LEActive) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[Editor Active]");
        }
    } else if (isLeader) {
        float startW = pdguiScale(140.0f);
        if (ImGui::Button("Start Match", ImVec2(startW, btnH))) {
            pdguiPlaySound(PDGUI_SND_SELECT);

            switch (s_ActiveTab) {
                case 0: {
                    /* Combat Simulator */
                    if (s_NumArenas == 0) break;
                    /* Write stage_id (PRIMARY) into g_MatchConfig so matchStart()
                     * can resolve stagenum from the catalog. */
                    strncpy(g_MatchConfig.stage_id, s_Arenas[s_SelectedArena].id,
                            sizeof(g_MatchConfig.stage_id) - 1);
                    g_MatchConfig.stage_id[sizeof(g_MatchConfig.stage_id) - 1] = '\0';
                    sysLogPrintf(LOG_NOTE,
                        "ROOM: stage selected: \"%s\" id='%s' stagenum=0x%02x",
                        s_Arenas[s_SelectedArena].name,
                        s_Arenas[s_SelectedArena].id,
                        (int)s_Arenas[s_SelectedArena].stagenum);
                    if (s_IsSoloMode) {
                        /* Solo play: matchStart() resolves stage_id → stagenum. */
                        pdguiSoloRoomClose();
                        s_MatchConfigInited = false;
                        matchStart();
                    } else {
                        int numBots = countBots();
                        u8 simType  = getLeadSimType();
                        netLobbyRequestStartWithSims(
                            GAMEMODE_MP,
                            s_Arenas[s_SelectedArena].id,
                            0,
                            (u8)numBots,
                            simType,
                            g_MatchConfig.timelimit,
                            g_MatchConfig.options,
                            g_MatchConfig.scenario,
                            g_MatchConfig.scorelimit,
                            g_MatchConfig.teamscorelimit,
                            (u8)(g_MatchConfig.weaponSetIndex >= 0 ? g_MatchConfig.weaponSetIndex : 0xFF));
                    }
                    break;
                }
                case 1: {
                    /* Campaign — resolve mission stagenum to catalog ID at callsite. */
                    const char *coop_id = catalogResolveStageByStagenum(
                        (s32)s_Missions[s_CampaignMission].stagenum);
                    if (!coop_id) {
                        sysLogPrintf(LOG_ERROR,
                            "ROOM: no catalog entry for coop stagenum=0x%02x",
                            (unsigned)s_Missions[s_CampaignMission].stagenum);
                        break;
                    }
                    netLobbyRequestStart(GAMEMODE_COOP, coop_id, (u8)s_CampaignDiff);
                    break;
                }
                case 2: {
                    /* Counter-Operative — same pattern. */
                    const char *anti_id = catalogResolveStageByStagenum(
                        (s32)s_Missions[s_CounterOpMission].stagenum);
                    if (!anti_id) {
                        sysLogPrintf(LOG_ERROR,
                            "ROOM: no catalog entry for anti stagenum=0x%02x",
                            (unsigned)s_Missions[s_CounterOpMission].stagenum);
                        break;
                    }
                    netLobbyRequestStart(GAMEMODE_ANTI, anti_id, (u8)s_CounterOpDiff);
                    break;
                }
            }
        }

        ImGui::SameLine();

        /* Status hint */
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.5f, 0.9f),
                           "Configure match, then Start.");
    } else {
        ImGui::TextDisabled("Waiting for the room leader to start...");
    }

    /* Leave Room / Back to Menu (right-aligned) */
    float leaveW = pdguiScale(s_IsSoloMode ? 140.0f : 120.0f);
    ImGui::SameLine(dialogW - leaveW - ImGui::GetStyle().WindowPadding.x * 2);
    const char *leaveLabel = s_IsSoloMode ? "Back to Menu" : "Leave Room";
    if (ImGui::Button(leaveLabel, ImVec2(leaveW, btnH)) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        sysLogPrintf(LOG_NOTE, "MENU_IMGUI: room CLOSE via %s/ESC (solo=%d)",
                     leaveLabel, s_IsSoloMode);
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        s_MatchConfigInited = false;  /* reset on next enter */
        s_CodeGenerated     = false;
        if (s_IsSoloMode) {
            pdguiSoloRoomClose();  /* return to main menu */
        } else {
            pdguiSetInRoom(0);  /* return to social lobby, stay connected */
        }
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
            /* Resolve current display name from catalog ID (PRIMARY). */
            const char *curBody = "?";
            if (sl->body_id[0]) {
                for (u32 b2 = 0; b2 < numBodies; b2++) {
                    const char *bid2 = catalogResolveBodyByMpIndex((s32)b2);
                    if (bid2 && strcmp(bid2, sl->body_id) == 0) {
                        char *n = mpGetBodyName((u8)b2);
                        if (n && n[0]) curBody = n;
                        break;
                    }
                }
            }
            ImGui::Text("Character:");
            ImGui::SameLine(labelCol);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##botmodalchar", curBody)) {
                for (u32 b = 0; b < numBodies; b++) {
                    char *bodyName = mpGetBodyName((u8)b);
                    if (!bodyName || !bodyName[0]) continue;
                    const char *bid = catalogResolveBodyByMpIndex((s32)b);
                    bool sel = bid && sl->body_id[0] && strcmp(bid, sl->body_id) == 0;
                    char bLabel[64];
                    snprintf(bLabel, sizeof(bLabel), "%s##mb%u", bodyName, b);
                    if (ImGui::Selectable(bLabel, sel)) {
                        if (bid) {
                            strncpy(sl->body_id, bid, sizeof(sl->body_id) - 1);
                            sl->body_id[sizeof(sl->body_id) - 1] = '\0';
                        }
                        const char *hid = catalogResolveHeadByMpIndex((s32)b);
                        if (hid) {
                            strncpy(sl->head_id, hid, sizeof(sl->head_id) - 1);
                            sl->head_id[sizeof(sl->head_id) - 1] = '\0';
                        }
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

    /* ---- Save Scenario popup ---- */
    if (s_ShowSaveScenario) {
        ImGui::OpenPopup("Save Scenario##savescenpop");
        s_ShowSaveScenario = false;
    }

    ImGui::SetNextWindowSize(ImVec2(pdguiScale(320.0f), 0.0f));
    if (ImGui::BeginPopupModal("Save Scenario##savescenpop", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Save Scenario");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Name:");
        ImGui::SetNextItemWidth(pdguiScale(280.0f));
        ImGui::InputText("##savename", s_SaveNameBuf, sizeof(s_SaveNameBuf));

        ImGui::Spacing();

        float bw = pdguiScale(100.0f);
        if (ImGui::Button("Save", ImVec2(bw, 0.0f))) {
            if (s_SaveNameBuf[0]) {
                sysLogPrintf(LOG_NOTE, "MENU_IMGUI: scenario SAVE \"%s\"", s_SaveNameBuf);
                if (scenarioSave(s_SaveNameBuf) == 0) {
                    sysLogPrintf(LOG_NOTE, "MENU_IMGUI: scenario SAVE OK \"%s\"", s_SaveNameBuf);
                    snprintf(s_ScenarioStatusMsg, sizeof(s_ScenarioStatusMsg),
                             "Saved: %s", s_SaveNameBuf);
                } else {
                    sysLogPrintf(LOG_NOTE, "MENU_IMGUI: scenario SAVE FAILED \"%s\"", s_SaveNameBuf);
                    snprintf(s_ScenarioStatusMsg, sizeof(s_ScenarioStatusMsg),
                             "Save failed.");
                }
                pdguiPlaySound(PDGUI_SND_SELECT);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(bw, 0.0f))) {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    /* ---- Load Scenario popup ---- */
    if (s_ShowLoadScenario) {
        ImGui::OpenPopup("Load Scenario##loadscenpop");
        s_ShowLoadScenario = false;
    }

    ImGui::SetNextWindowSize(ImVec2(pdguiScale(380.0f), pdguiScale(280.0f)));
    if (ImGui::BeginPopupModal("Load Scenario##loadscenpop", nullptr,
                               ImGuiWindowFlags_NoResize)) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Load Scenario");
        ImGui::Separator();
        ImGui::Spacing();

        if (s_ScenarioCount == 0) {
            ImGui::TextDisabled("No saved scenarios found.");
            ImGui::TextDisabled("Save a scenario first using \"Save Scenario\".");
        } else {
            float listH = pdguiScale(180.0f);
            ImGui::BeginChild("##scen_list", ImVec2(-1.0f, listH), true);

            for (int i = 0; i < s_ScenarioCount; i++) {
                /* Display name: filename without directory and without .json */
                const char *fullPath = s_ScenarioFiles[i];
                const char *slash = strrchr(fullPath, '/');
                if (!slash) slash = strrchr(fullPath, '\\');
                const char *fname = slash ? slash + 1 : fullPath;

                /* Strip .json suffix for display */
                char displayName[SCENARIO_PATH_MAX];
                strncpy(displayName, fname, sizeof(displayName) - 1);
                displayName[sizeof(displayName) - 1] = '\0';
                size_t dlen = strlen(displayName);
                if (dlen > 5 && strcmp(displayName + dlen - 5, ".json") == 0) {
                    displayName[dlen - 5] = '\0';
                }

                ImGui::PushID(i);
                bool isSel = (i == s_ScenarioSelected);
                if (ImGui::Selectable(displayName, isSel,
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    s_ScenarioSelected = i;
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        /* Double-click: load and close */
                        s32 humanCount = lobbyGetPlayerCount();
                        if (humanCount < 1) humanCount = 1;
                        sysLogPrintf(LOG_NOTE, "MENU_IMGUI: scenario LOAD \"%s\" humans=%d",
                                     displayName, humanCount);
                        if (scenarioLoad(fullPath, humanCount) == 0) {
                            sysLogPrintf(LOG_NOTE, "MENU_IMGUI: scenario LOAD OK \"%s\"", displayName);
                            syncArenaFromConfig();
                            syncSpawnWeaponFromConfig();
                            snprintf(s_ScenarioStatusMsg, sizeof(s_ScenarioStatusMsg),
                                     "Loaded: %s", displayName);
                            pdguiPlaySound(PDGUI_SND_SELECT);
                        } else {
                            sysLogPrintf(LOG_NOTE, "MENU_IMGUI: scenario LOAD FAILED \"%s\"", displayName);
                            snprintf(s_ScenarioStatusMsg, sizeof(s_ScenarioStatusMsg),
                                     "Load failed.");
                            pdguiPlaySound(PDGUI_SND_KBCANCEL);
                        }
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::PopID();
            }

            ImGui::EndChild();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float fbw = pdguiScale(100.0f);
        bool hasSelection = (s_ScenarioSelected >= 0 && s_ScenarioSelected < s_ScenarioCount);

        if (!hasSelection) ImGui::BeginDisabled();
        if (ImGui::Button("Load", ImVec2(fbw, 0.0f))) {
            const char *fullPath = s_ScenarioFiles[s_ScenarioSelected];
            const char *slash = strrchr(fullPath, '/');
            if (!slash) slash = strrchr(fullPath, '\\');
            const char *fname = slash ? slash + 1 : fullPath;
            char displayName[SCENARIO_PATH_MAX];
            strncpy(displayName, fname, sizeof(displayName) - 1);
            displayName[sizeof(displayName) - 1] = '\0';
            size_t dlen2 = strlen(displayName);
            if (dlen2 > 5 && strcmp(displayName + dlen2 - 5, ".json") == 0)
                displayName[dlen2 - 5] = '\0';
            s32 humanCount = lobbyGetPlayerCount();
            if (humanCount < 1) humanCount = 1;
            sysLogPrintf(LOG_NOTE, "MENU_IMGUI: scenario LOAD \"%s\" humans=%d", displayName, humanCount);
            if (scenarioLoad(fullPath, humanCount) == 0) {
                sysLogPrintf(LOG_NOTE, "MENU_IMGUI: scenario LOAD OK \"%s\"", displayName);
                syncArenaFromConfig();
                syncSpawnWeaponFromConfig();
                snprintf(s_ScenarioStatusMsg, sizeof(s_ScenarioStatusMsg), "Loaded: %s", displayName);
                pdguiPlaySound(PDGUI_SND_SELECT);
            } else {
                sysLogPrintf(LOG_NOTE, "MENU_IMGUI: scenario LOAD FAILED \"%s\"", displayName);
                snprintf(s_ScenarioStatusMsg, sizeof(s_ScenarioStatusMsg), "Load failed.");
                pdguiPlaySound(PDGUI_SND_KBCANCEL);
            }
            ImGui::CloseCurrentPopup();
        }
        if (!hasSelection) ImGui::EndDisabled();

        ImGui::SameLine();

        if (!hasSelection) ImGui::BeginDisabled();
        if (ImGui::Button("Delete", ImVec2(fbw, 0.0f))) {
            const char *fullPath = s_ScenarioFiles[s_ScenarioSelected];
            sysLogPrintf(LOG_NOTE, "MENU_IMGUI: scenario DELETE \"%s\"", fullPath);
            if (scenarioDelete(fullPath) == 0) {
                /* Refresh list */
                s_ScenarioCount = scenarioListFiles(s_ScenarioFiles, SCENARIO_MAX_LIST);
                s_ScenarioSelected = -1;
                snprintf(s_ScenarioStatusMsg, sizeof(s_ScenarioStatusMsg), "Deleted.");
            } else {
                snprintf(s_ScenarioStatusMsg, sizeof(s_ScenarioStatusMsg), "Delete failed.");
            }
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
        }
        if (!hasSelection) ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(fbw, 0.0f))) {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::End();

    /* Level editor floating overlay — rendered as a separate window */
    if (s_LEActive) {
        renderLevelEditorOverlay();
    }
}

/* ========================================================================
 * Reset state when lobby session ends (called externally if needed)
 * ======================================================================== */

extern "C" void pdguiRoomScreenSetSolo(s32 solo)
{
    s_IsSoloMode = (solo != 0);
}

extern "C" void pdguiRoomScreenReset(void)
{
    s_MatchConfigInited = false;
    free(s_Arenas);
    s_Arenas            = NULL;
    s_NumArenas         = 0;
    s_ArenasCapacity    = 0;
    s_ArenasBuilt       = false;
    s_CodeGenerated     = false;
    s_IsSoloMode        = false;  /* caller sets via pdguiRoomScreenSetSolo() after reset */
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
    s_ShowSaveScenario  = false;
    s_ShowLoadScenario  = false;
    s_SaveNameBuf[0]    = '\0';
    s_ScenarioCount     = 0;
    s_ScenarioStatusMsg[0] = '\0';
}
