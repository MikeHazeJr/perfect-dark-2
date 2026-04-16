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
 *   - Combat Sim: netLobbyRequestStartWithSims(GAMEMODE_MP, stage_id, 0, numBots, simType, timelimit, options, scenario, scorelimit, teamscorelimit, weaponSetIndex)
 *   - Campaign:   netLobbyRequestStart(GAMEMODE_COOP, stage_id, difficulty)
 *   - Counter-Op: netLobbyRequestStart(GAMEMODE_ANTI, stage_id, difficulty)
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
#include <algorithm>

#include "imgui/imgui.h"
#include "pdgui_hotswap.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_menu_botsetup.h" /* D5 P3 Batch 6: inline Simulant Profiles panel */
#include "system.h"
#include "inputctx.h"
#include "room.h"

/* ========================================================================
 * Forward declarations (C boundary)
 * ======================================================================== */

extern "C" {

#include "assetcatalog.h"
#include "botvariant.h"
#include "pdgui_charpreview.h"
#include "pdgui_nav.h"
char *langGet(s32 textid);
char *langSafe(s32 textid);
s32 challengeIsFeatureUnlocked(u32 feature);

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
s32 pdguiCountdownIsActive(void); /* pdgui_bridge.c — true during pre-match countdown */
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
    u8 clientId;
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
                                  u8 antiClientId,
                                  u8 numSims, u8 simType, u8 timelimit, u32 options,
                                  u8 scenario, u8 scorelimit, u16 teamscorelimit,
                                  u8 weaponSetIndex);

/* Character data */
char *mpGetBodyName(u8 mpbodynum);
u32 mpGetNumBodies(void);
/* Body/head data accessed via catalog accessors */
/* Phase 5: catalog ID accessors for lobby players */
const char *lobbyGetPlayerBodyId(s32 idx);
const char *lobbyGetPlayerHeadId(s32 idx);

/* Weapon sets (mplayer.c) */
void mpSetWeaponSet(s32 weaponsetnum);
s32 mpGetWeaponSet(void);
char *mpGetWeaponSetName(s32 index);
s32 func0f189058(s32 full);   /* count of available weapon sets (full=1 includes Random/Custom) */
extern s32 g_MpWeaponSetNum;
#define WEAPONSET_CUSTOM 0x0e

/* Weapon slot editing (mplayer.c) */
void mpSetWeaponSlot(s32 slot, s32 mpweaponnum);
s32 mpGetWeaponSlot(s32 slot);
char *mpGetWeaponLabel(s32 weaponnum);
s32 mpGetNumWeaponOptions(void);

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
#define MPOPTION_SLOWMOTION_ON         0x00000040
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
s32 matchConfigMaxBotsForHumans(s32 humanCount);
/* M0.1c: weapon slot catalog ID accessor (matchsetup.c) */
const char *matchGetWeaponSlotCatalogId(s32 slot);

/* Solo room close — defined in pdgui_lobby.cpp */
void pdguiSoloRoomClose(void);

/* Bot randomization (matchsetup.c) */
void matchConfigRerollBot(s32 idx);

/* R-3: Room networking — send leave to server */
struct netbuf;
u32 netmsgClcRoomLeaveWrite(struct netbuf *dst);
u32 netSend(struct netclient *dstcl, struct netbuf *buf, const s32 reliable, const s32 chan);
extern struct netbuf g_NetMsgRel;
void netbufStartWrite(struct netbuf *buf);
extern s32 g_NetMode;
#define RM_NETMODE_NONE   0
#define RM_NETMODE_CLIENT 2

/* R-5: Room settings + playlist sync (netmsg.c) */
void netSendRoomSettingsUpdate(void);
void netSendRoomPlaylistUpdate(void);

/* Sub-screen dialog defs (U-2, U-3) */
struct menudialogdef;
void menuPushDialog(struct menudialogdef *dialogdef);
extern struct menudialogdef g_MpHandicapsMenuDialog;
extern struct menudialogdef g_MpSelectTunesMenuDialog;
extern struct menudialogdef g_MpTeamsMenuDialog;

} /* extern "C" */

/* ========================================================================
 * Arena name fallback table
 *
 * The allinone mod ships its own LmpmenuE language file that overrides the
 * compiled binary at runtime.  The mod's version still contains the original
 * PerfectHead / Game Boy Camera UI strings for IDs 296-338, which means
 * langGet() returns garbage like "Load A Saved Head" instead of "Frigate".
 * This table provides the correct names keyed by text-ID so the ImGui UI
 * always shows readable arena names regardless of the language file state.
 * Relocated from pdgui_menu_matchsetup.cpp (U-7b Step A).
 * ======================================================================== */

struct arenaNameOverride {
    u16 textId;
    const char *name;
};

static const struct arenaNameOverride s_ArenaNameOverrides[] = {
    { 0x5126, "Random: PD Maps" },       /* L_MPMENU_294 - Random Multi */
    { 0x5127, "Random: Solo Maps" },     /* L_MPMENU_295 - Random Solo */
    { 0x5128, "GoldenEye X" },           /* L_MPMENU_296 - group header */
    { 0x5129, "GoldenEye X Bonus" },     /* L_MPMENU_297 - group header */
    { 0x512a, "Frigate" },               /* L_MPMENU_298 */
    { 0x512b, "Archives" },              /* L_MPMENU_299 */
    { 0x512c, "Bunker" },                /* L_MPMENU_300 */
    { 0x512d, "Labyrinth" },             /* L_MPMENU_301 */
    { 0x512e, "Basement" },              /* L_MPMENU_302 */
    { 0x512f, "Library" },               /* L_MPMENU_303 */
    { 0x5130, "Cradle" },                /* L_MPMENU_304 */
    { 0x5131, "Caverns" },               /* L_MPMENU_305 */
    { 0x5132, "Caves" },                 /* L_MPMENU_306 */
    { 0x5133, "Facility BZ" },           /* L_MPMENU_307 */
    { 0x5134, "Citadel" },               /* L_MPMENU_308 */
    { 0x5135, "Stack" },                 /* L_MPMENU_309 */
    { 0x5136, "Train" },                 /* L_MPMENU_310 */
    { 0x5137, "Facility" },              /* L_MPMENU_311 */
    { 0x5138, "Egyptian" },              /* L_MPMENU_312 */
    { 0x5139, "Aztec" },                 /* L_MPMENU_313 */
    { 0x513a, "Archives 1F" },           /* L_MPMENU_314 */
    { 0x513b, "Streets" },               /* L_MPMENU_315 */
    { 0x513c, "Icicle Pyramid" },        /* L_MPMENU_316 */
    { 0x513d, "Random GoldenEye X" },    /* L_MPMENU_317 */
    { 0x513e, "Kakariko Village" },       /* L_MPMENU_318 */
    { 0x513f, "Kakariko Village (Stormy)" }, /* L_MPMENU_319 */
    { 0x5140, "Dark Noon" },             /* L_MPMENU_320 */
    { 0x5141, "Dark Noon Valley" },      /* L_MPMENU_321 */
    { 0x5142, "Archives BZ" },           /* L_MPMENU_322 */
    { 0x5143, "Cliff Base" },            /* L_MPMENU_323 */
    { 0x5144, "Suburb" },                /* L_MPMENU_324 */
    { 0x5145, "Training Day" },          /* L_MPMENU_325 */
    { 0x5146, "Bonus" },                 /* L_MPMENU_326 - group header */
    { 0x5147, "Runway" },                /* L_MPMENU_327 */
    { 0x5148, "Control" },               /* L_MPMENU_328 */
    { 0x5149, "Tawfret Ruins" },         /* L_MPMENU_329 */
    { 0x514a, "Targitzan's Temple" },    /* L_MPMENU_330 */
    { 0x514b, "Junkyard" },              /* L_MPMENU_331 */
    { 0x514c, "Steel Mill" },            /* L_MPMENU_332 */
    { 0x514d, "Mall" },                  /* L_MPMENU_333 */
    { 0x514e, "Tunnels" },               /* L_MPMENU_334 */
    { 0x514f, "Rogue" },                 /* L_MPMENU_335 */
    /* Paradox (0x5150 / L_MPMENU_336) omitted — map data removed */
    { 0x5151, "War Colors" },            /* L_MPMENU_337 */
    { 0x5152, "Grand Library" },         /* L_MPMENU_338 */
};

static const s32 s_NumArenaNameOverrides = sizeof(s_ArenaNameOverrides) / sizeof(s_ArenaNameOverrides[0]);

/* Look up arena name: check override table first, then fall back to langGet().
 * Non-static: also used externally by pdgui_menu_matchsetup.cpp. */
const char *arenaGetName(u16 textId)
{
    /* Check hardcoded overrides for the broken range */
    for (s32 i = 0; i < s_NumArenaNameOverrides; i++) {
        if (s_ArenaNameOverrides[i].textId == textId) {
            return s_ArenaNameOverrides[i].name;
        }
    }
    /* Fall back to the language system for base-game strings */
    {
        const char *s = langSafe(textId);
        return s[0] ? s : "???";
    }
}

/* ========================================================================
 * Arena list — built from the asset catalog at room init.
 * Replaces the old hardcoded table: catalog is the single source of truth.
 * ======================================================================== */

/* F-2.1: Arena sections — base MP, campaign (solo missions), mod maps */
#define ARENA_SEC_MP_BASE  0
#define ARENA_SEC_CAMPAIGN 1
#define ARENA_SEC_MOD      2
#define ARENA_SEC_COUNT    3

struct arena_entry {
    char name[64];
    char id[64];
    s32  stagenum;
    u8   requirefeature;
    char category[32]; /* F-2.1: from catalog (Dark, Solo Missions, Classic, etc.) */
    s32  bundled;      /* F-2.1: shipped with game */
    s32  section;      /* F-2.1: ARENA_SEC_MP_BASE / CAMPAIGN / MOD */
};

static arena_entry *s_Arenas = NULL;
static int s_NumArenas = 0;
static int s_ArenasCapacity = 0;
static bool s_ArenasBuilt = false;

/* F-2.1: Section boundaries (computed after sort) */
static int s_SectionStart[ARENA_SEC_COUNT];
static int s_SectionCount[ARENA_SEC_COUNT];

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

    if (!challengeIsFeatureUnlocked((u8)e->ext.arena.requirefeature)) {
        return;
    }

    arena_entry *a = &s_Arenas[s_NumArenas];
    strncpy(a->name, name, 63);
    a->name[63] = '\0';
    strncpy(a->id, e->id, 63);
    a->id[63] = '\0';
    a->stagenum = e->ext.arena.stagenum;
    a->requirefeature = (u8)e->ext.arena.requirefeature;

    /* F-2.1: Capture category and bundled for section classification */
    strncpy(a->category, e->category, 31);
    a->category[31] = '\0';
    a->bundled = e->bundled;

    if (!a->bundled) {
        a->section = ARENA_SEC_MOD;
    } else if (strcmp(a->category, "Solo Missions") == 0) {
        a->section = ARENA_SEC_CAMPAIGN;
    } else {
        a->section = ARENA_SEC_MP_BASE;
    }

    sysLogPrintf(LOG_NOTE, "CATALOG: arena[%d] \"%s\" id='%s' stagenum=0x%02x sec=%d registered",
        s_NumArenas, a->name, a->id, a->stagenum, a->section);
    s_NumArenas++;
}

/* F-2.1: Sort arenas — primary by section, secondary alphabetical by name */
static int arenaCompare(const void *a, const void *b)
{
    const arena_entry *ea = (const arena_entry *)a;
    const arena_entry *eb = (const arena_entry *)b;
    if (ea->section != eb->section) return ea->section - eb->section;
    return strcasecmp(ea->name, eb->name);
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

    /* F-2.1: Sort by section then alphabetical */
    if (s_NumArenas > 1) {
        qsort(s_Arenas, s_NumArenas, sizeof(arena_entry), arenaCompare);
    }

    /* F-2.1: Compute section boundaries */
    for (int s = 0; s < ARENA_SEC_COUNT; s++) {
        s_SectionStart[s] = 0;
        s_SectionCount[s] = 0;
    }
    for (int i = 0; i < s_NumArenas; i++) {
        int sec = s_Arenas[i].section;
        if (sec >= 0 && sec < ARENA_SEC_COUNT) {
            if (s_SectionCount[sec] == 0) s_SectionStart[sec] = i;
            s_SectionCount[sec]++;
        }
    }

    sysLogPrintf(LOG_NOTE, "CATALOG: arena list built: %d arenas (MP=%d, Campaign=%d, Mod=%d)",
        s_NumArenas, s_SectionCount[ARENA_SEC_MP_BASE],
        s_SectionCount[ARENA_SEC_CAMPAIGN], s_SectionCount[ARENA_SEC_MOD]);
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
 * Spawn-with-weapon picker — catalog-sourced weapon entries.
 * M0.1c: built dynamically from ASSET_WEAPON catalog entries at init.
 * ======================================================================== */

struct spawnweapon_entry {
    char catalog_id[64]; /* catalog ID e.g. "base:falcon2", or "" for Random */
    char name[64];       /* display name */
};

#define MAX_SPAWN_WEAPONS 64
static spawnweapon_entry s_SpawnWeapons[MAX_SPAWN_WEAPONS];
static int s_NumSpawnWeapons = 0;

static void buildSpawnWeaponList(void)
{
    s_NumSpawnWeapons = 0;

    /* Entry 0: Random (special — empty catalog_id) */
    s_SpawnWeapons[0].catalog_id[0] = '\0';
    strncpy(s_SpawnWeapons[0].name, "Random", sizeof(s_SpawnWeapons[0].name));
    s_NumSpawnWeapons = 1;

    /* Scan catalog for all ASSET_WEAPON entries, skip NONE/DISABLED/SHIELD */
    for (int i = 0; ; i++) {
        const asset_entry_t *e = assetCatalogGetByIndex(i);
        if (!e) break;
        if (e->type != ASSET_WEAPON) continue;
        if (s_NumSpawnWeapons >= MAX_SPAWN_WEAPONS) break;
        /* Skip non-combat entries */
        s32 wid = e->ext.weapon.weapon_id;
        if (wid == 0x00 || wid == 0x30 || wid == 0x2f) continue; /* NONE, DISABLED, SHIELD */
        spawnweapon_entry *sw = &s_SpawnWeapons[s_NumSpawnWeapons];
        strncpy(sw->catalog_id, e->id, sizeof(sw->catalog_id) - 1);
        sw->catalog_id[sizeof(sw->catalog_id) - 1] = '\0';
        strncpy(sw->name, e->ext.weapon.name ? e->ext.weapon.name : e->id,
                sizeof(sw->name) - 1);
        sw->name[sizeof(sw->name) - 1] = '\0';
        s_NumSpawnWeapons++;
    }
}

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

/* R-5: set true whenever the leader changes settings; cleared after CLC_ROOM_SETTINGS_UPDATE send */
static bool s_RoomSettingsDirty = false;

/* B-124 pattern: true if this screen pushed g_CtxImGuiMenu itself.
 * Used to correctly match push/pop ownership — only pop if we pushed it. */
static bool s_RoomPushedCtx = false;

/* Campaign / Counter-Op settings */
static int s_CampaignMission   = 0;
static int s_CampaignDiff      = DIFF_A;
static int s_CounterOpMission  = 0;
static int s_CounterOpDiff     = DIFF_A;
static int s_CounterOpPlayer   = 0;  /* index into lobby player list = the counter-op player */
static u8  s_CounterOpClientId = 0xFF;

/* Arena picker state (index into s_Arenas[]) */
static int s_SelectedArena = 0;

/* Bot management state — multi-select */
static bool s_BotSelected[MATCH_MAX_SLOTS]; /* per-slot selection */
static int  s_BotSelectCount  = 0;          /* cached count of selected bots */
static bool s_BotModalOpen    = false;
static int  s_EditBotSlotIdx  = -1;         /* slot index being edited in the modal */

/* ========================================================================
 * D3R-8: Bot Customizer state (U-7b Step B — ported from matchsetup.cpp)
 * ======================================================================== */

struct BotTraits {
    float accuracy;
    float reactionTime;
    float aggression;
    char  baseType[32];
};

static BotTraits s_BotTraits[MATCH_MAX_SLOTS];
static bool      s_BotTraitsInitialized = false;

/* Whether the Advanced section is expanded in the current bot edit modal */
static bool s_BotModalShowAdvanced = false;

/* 3D character preview rotation (radians, wraps at 2pi) */
static float s_BotPreviewRotY = 0.0f;

/* Bot preset cache — ASSET_BOT_VARIANT entries from catalog */
#define MAX_BOT_PRESETS 64
static const asset_entry_t *s_BotPresets[MAX_BOT_PRESETS];
static s32                  s_BotPresetCount      = 0;
static s32                  s_BotPresetCacheDirty = 1;
static s32                  s_BotPresetSelected   = -1; /* index into s_BotPresets */

/* Save-preset popup state */
static char s_SavePresetName[MAX_PLAYER_NAME] = {0};

/* Known base type strings — matching PD's simulant type naming */
static const char *s_BaseTypeNames[] = {
    "NormalSim", "MeatSim",  "EasySim",  "HardSim",
    "PerfectSim","DarkSim",  "PeaceSim", "ShieldSim",
    "RocketSim", "KazeSim",  "FistSim",  "PreySim",
    "CowardSim", "JudgeSim", "FeudSim",  "SpeedSim",
    "TurtleSim", "VengeSim",
};
static const s32 s_NumBaseTypes = 18;

/* Bot type names for context menu */
static const char *s_BotTypeNames[] = {
    "Normal", "Peace", "Shield", "Rocket", "Kaze", "Fist",
    "Prey", "Coward", "Judge", "Feud", "Speed", "Turtle", "Venge",
};
static const int s_NumBotTypes = 13;

static void botPresetCacheCb(const asset_entry_t *entry, void *userdata)
{
    (void)userdata;
    if (s_BotPresetCount < MAX_BOT_PRESETS) {
        s_BotPresets[s_BotPresetCount++] = entry;
    }
}

static void rebuildBotPresetCache(void)
{
    s_BotPresetCount = 0;
    assetCatalogIterateByType(ASSET_BOT_VARIANT, botPresetCacheCb, NULL);
    s_BotPresetCacheDirty = 0;
}

static void initBotTraits(void)
{
    for (s32 i = 0; i < MATCH_MAX_SLOTS; i++) {
        s_BotTraits[i].accuracy     = 0.5f;
        s_BotTraits[i].reactionTime = 0.5f;
        s_BotTraits[i].aggression   = 0.5f;
        strncpy(s_BotTraits[i].baseType, "NormalSim", sizeof(s_BotTraits[i].baseType) - 1);
        s_BotTraits[i].baseType[sizeof(s_BotTraits[i].baseType) - 1] = '\0';
    }
    s_BotTraitsInitialized = true;
}

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
    float btnH       = pdguiScale(39.0f);
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
    if (listH < pdguiScale(90.0f)) listH = pdguiScale(90.0f);

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
    float btnH   = pdguiScale(36.0f);
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
                                  ImVec2(panelW - pdguiScale(48.0f), 0.0f))) {
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
        if (fieldW < pdguiScale(90.0f)) fieldW = pdguiScale(90.0f);

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
    float oW     = pdguiScale(420.0f);
    float oH     = pdguiScale(510.0f);
    float exitW;
    ImVec2 disp  = ImGui::GetIO().DisplaySize;

    /* Pin to top-right corner */
    ImGui::SetNextWindowPos(
        ImVec2(disp.x - oW - pdguiScale(12.0f), pdguiScale(12.0f)),
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
                      ImVec2(exitW, pdguiScale(39.0f)))) {
        s_LEActive = false;
        sysLogPrintf(LOG_NOTE, "LEVEL_EDITOR: overlay closed.");
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
    }

    ImGui::End();
}

/* ========================================================================
 * Helper: sync s_SpawnWeaponIdx from g_MatchConfig.spawn_weapon_id
 * Called after loading a scenario so the picker shows the right entry.
 * ======================================================================== */

static void syncSpawnWeaponFromConfig(void)
{
    /* M0.1c: match by catalog ID (PRIMARY) */
    if (g_MatchConfig.spawn_weapon_id[0]) {
        for (int i = 0; i < s_NumSpawnWeapons; i++) {
            if (strcmp(s_SpawnWeapons[i].catalog_id, g_MatchConfig.spawn_weapon_id) == 0) {
                s_SpawnWeaponIdx = i;
                return;
            }
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
        /* Fallback: accept legacy aliases by resolving stage number. */
        const asset_entry_t *stageEntry = assetCatalogResolve(g_MatchConfig.stage_id);
        if (stageEntry) {
            s32 desiredStage = -1;
            if (stageEntry->type == ASSET_ARENA) {
                desiredStage = stageEntry->ext.arena.stagenum;
            } else if (stageEntry->type == ASSET_MAP) {
                desiredStage = stageEntry->runtime_index;
            }

            if (desiredStage >= 0) {
                for (int i = 0; i < s_NumArenas; i++) {
                    if (s_Arenas[i].stagenum == desiredStage) {
                        s_SelectedArena = i;
                        strncpy(g_MatchConfig.stage_id, s_Arenas[i].id,
                                sizeof(g_MatchConfig.stage_id) - 1);
                        g_MatchConfig.stage_id[sizeof(g_MatchConfig.stage_id) - 1] = '\0';
                        return;
                    }
                }
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
        s_RoomSettingsDirty = true;
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
        s_RoomSettingsDirty = true;
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
    int maxBots = matchConfigMaxBotsForHumans(humanCount);

    float btnH   = pdguiScale(39.0f);
    float listH  = panelH - btnH
                   - ImGui::GetStyle().ItemSpacing.y * 3.0f
                   - ImGui::GetStyle().WindowPadding.y * 2.0f;

    /* Outer panel (bordered) */
    ImGui::BeginChild("##room_panel_outer", ImVec2(panelW, panelH), true);

    /* Scrollable list */
    ImGui::BeginChild("##room_players_list", ImVec2(0, listH), false);

    /* Header with player/bot count summary */
    {
        s32 numPlayers = s_IsSoloMode ? 1 : humanCount;
        s32 numBots = s_BotSelectCount;
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
                           "Players in Room  (%d Player%s, %d Bot%s)",
                           numPlayers, numPlayers != 1 ? "s" : "",
                           numBots,    numBots != 1    ? "s" : "");
    }
    ImGui::Separator();

    if (s_IsSoloMode) {
        /* Solo mode: always exactly one local human player (g_MatchConfig.slots[0]).
         * Uses identity profile name (agent name) via mpPlayerConfigGetName. */
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
                s_BotPreviewRotY = 0.0f;
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
                    ImGui::SetNextItemWidth(pdguiScale(210.0f));
                    ImGui::InputText("##ctx_name", g_MatchConfig.slots[si].name, MAX_PLAYER_NAME);
                }
            }

            /* Determine common settings across selected bots for checkmarks.
             * If all selected share the same value, mark it; otherwise -1. */
            int commonDiff = -1, commonType = -1, commonBody = -1;
            {
                bool first = true;
                for (int j = 1; j < g_MatchConfig.numSlots; j++) {
                    if (!s_BotSelected[j] || g_MatchConfig.slots[j].type != SLOT_BOT) continue;
                    int d = (int)g_MatchConfig.slots[j].botDifficulty;
                    int t = (int)g_MatchConfig.slots[j].botType;
                    int b = (int)g_MatchConfig.slots[j].bodynum;
                    if (first) { commonDiff = d; commonType = t; commonBody = b; first = false; }
                    else {
                        if (commonDiff != d) commonDiff = -1;
                        if (commonType != t) commonType = -1;
                        if (commonBody != b) commonBody = -1;
                    }
                }
            }

            /* Bot AI (difficulty) — applies to all selected */
            if (isLeader && ImGui::BeginMenu("Bot AI")) {
                for (int d = 0; d < s_NumSimDiffs; d++) {
                    if (ImGui::MenuItem(s_SimDiffNames[d], NULL, d == commonDiff)) {
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
                    if (ImGui::MenuItem(s_BotTypeNames[t], NULL, t == commonType)) {
                        for (int j = 1; j < g_MatchConfig.numSlots; j++) {
                            if (s_BotSelected[j] && g_MatchConfig.slots[j].type == SLOT_BOT)
                                g_MatchConfig.slots[j].botType = (u8)t;
                        }
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    }
                }
                ImGui::EndMenu();
            }

            /* Character — applies to all selected, sorted alphabetically */
            if (isLeader && ImGui::BeginMenu("Character")) {
                u32 numBodies = mpGetNumBodies();

                /* Build sortable list of (displayName, mpIndex) pairs */
                static const u32 MAX_BODY_ENTRIES = 256;
                struct BodyEntry { const char *name; u32 idx; };
                BodyEntry sorted[MAX_BODY_ENTRIES];
                if (numBodies > MAX_BODY_ENTRIES) numBodies = MAX_BODY_ENTRIES;
                u32 sortedCount = 0;
                for (u32 b = 0; b < numBodies; b++) {
                    char *bodyName = mpGetBodyName((u8)b);
                    /* Fallback for bodies with empty display names */
                    if (!bodyName || !bodyName[0]) {
                        const char *bid = catalogMpBodyId(b);
                        if (bid && strcmp(bid, "base:drcaroll") == 0) bodyName = (char *)"Dr. Caroll";
                        else if (bid && strcmp(bid, "base:skedar") == 0) bodyName = (char *)"Skedar";
                        else continue;
                    }
                    sorted[sortedCount++] = { bodyName, b };
                }

                /* Sort alphabetically by display name (case-insensitive) */
                for (u32 a = 0; a < sortedCount; a++) {
                    for (u32 c = a + 1; c < sortedCount; c++) {
                        if (strcasecmp(sorted[a].name, sorted[c].name) > 0) {
                            BodyEntry tmp = sorted[a];
                            sorted[a] = sorted[c];
                            sorted[c] = tmp;
                        }
                    }
                }

                for (u32 si = 0; si < sortedCount; si++) {
                    u32 b = sorted[si].idx;
                    const char *bid = catalogMpBodyId(b);
                    if (ImGui::MenuItem(sorted[si].name, NULL, (int)b == commonBody)) {
                        const char *hid = catalogMpHeadId(b);
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
                s_RoomSettingsDirty = true;
            }

            /* Re-roll — random name + character for all selected */
            if (isLeader && ImGui::MenuItem("Re-roll")) {
                for (int j = 1; j < g_MatchConfig.numSlots; j++) {
                    if (s_BotSelected[j] && g_MatchConfig.slots[j].type == SLOT_BOT) {
                        matchConfigRerollBot(j);
                    }
                }
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                s_RoomSettingsDirty = true;
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
                s_RoomSettingsDirty = true;
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
        s_RoomSettingsDirty = true;
    }
    if (!canAdd) ImGui::EndDisabled();

    /* D5 P3 Batch 6: legacy Simulant Profile pool as inline expandable
     * section.  Backed by g_BotConfigsArray (setup.c) via the same
     * pdguiBotSetupDrawSimulantsBody helper that powers the
     * g_MpSimulantsMenuDialog modal wrapper in pdgui_menu_botsetup.cpp.
     * The room.cpp matchslot bot UI above is the primary in-room path;
     * this inline section exposes the legacy profile pool without
     * requiring the Combat Simulator legacy menu detour. */
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Simulant Profiles")) {
        ImGui::TextDisabled("Legacy bot profile pool");
        pdguiBotSetupDrawSimulantsBody(pdguiScale(260.0f));
    }

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
                /* M0.1d: set scenario_id (PRIMARY) from catalog */
                const char *sid = catalogIdByRuntime(ASSET_GAMEMODE, si);
                if (sid) {
                    strncpy(g_MatchConfig.scenario_id, sid,
                            sizeof(g_MatchConfig.scenario_id) - 1);
                    g_MatchConfig.scenario_id[sizeof(g_MatchConfig.scenario_id) - 1] = '\0';
                }
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                s_RoomSettingsDirty = true;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (!leader) ImGui::EndDisabled();

    ImGui::Spacing();

    /* --- Arena (F-2.1: collapsible sections, alphabetized) --- */
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Arena");
    if (!leader) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(comboW);
    const char *arenaLabel = (s_NumArenas > 0) ? s_Arenas[s_SelectedArena].name : "(none)";
    if (ImGui::BeginCombo("##arena", arenaLabel)) {
        static const char *k_SectionNames[] = {
            "Multiplayer Arenas", "Campaign Maps", "Mod Maps"
        };
        for (int sec = 0; sec < ARENA_SEC_COUNT; sec++) {
            if (s_SectionCount[sec] == 0) continue;
            char hdr[128];
            snprintf(hdr, sizeof(hdr), "%s (%d)", k_SectionNames[sec], s_SectionCount[sec]);
            if (ImGui::TreeNodeEx(hdr, ImGuiTreeNodeFlags_DefaultOpen)) {
                for (int ai = s_SectionStart[sec];
                     ai < s_SectionStart[sec] + s_SectionCount[sec]; ai++) {
                    bool sel = (ai == s_SelectedArena);
                    if (ImGui::Selectable(s_Arenas[ai].name, sel)) {
                        s_SelectedArena = ai;
                        strncpy(g_MatchConfig.stage_id, s_Arenas[ai].id,
                                sizeof(g_MatchConfig.stage_id) - 1);
                        g_MatchConfig.stage_id[sizeof(g_MatchConfig.stage_id) - 1] = '\0';
                        sysLogPrintf(LOG_NOTE, "ROOM: arena selected \"%s\" id='%s'",
                            s_Arenas[ai].name, s_Arenas[ai].id);
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                        s_RoomSettingsDirty = true;
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::TreePop();
            }
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
            s_RoomSettingsDirty = true;
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
            s_RoomSettingsDirty = true;
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

    /* --- Weapon Set (F-2.1: TreeNodeEx + alphabetized, same pattern as Arenas) --- */
    {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Weapon Set");
        if (!leader) ImGui::BeginDisabled();
        s32 numSets = func0f189058(1);
        s32 curSet  = mpGetWeaponSet();

        struct WSEntry { char name[64]; s32 idx; };
        static WSEntry s_WSorted[64];
        s32 wcount = 0;
        for (s32 i = 0; i < numSets && wcount < (s32)(sizeof(s_WSorted)/sizeof(s_WSorted[0])); i++) {
            char *sn = mpGetWeaponSetName(i);
            if (!sn || !sn[0]) continue;
            snprintf(s_WSorted[wcount].name, sizeof(s_WSorted[wcount].name), "%s", sn);
            s_WSorted[wcount].idx = i;
            wcount++;
        }
        std::sort(s_WSorted, s_WSorted + wcount, [](const WSEntry &a, const WSEntry &b) {
            return strcmp(a.name, b.name) < 0;
        });

        char grpHdr[64];
        snprintf(grpHdr, sizeof(grpHdr), "Base Game (%d)", wcount);
        if (ImGui::TreeNodeEx(grpHdr, ImGuiTreeNodeFlags_DefaultOpen)) {
            for (s32 j = 0; j < wcount; j++) {
                bool isSel = (s_WSorted[j].idx == curSet);
                char setLabel[128];
                snprintf(setLabel, sizeof(setLabel), "%s##ws%d", s_WSorted[j].name, s_WSorted[j].idx);
                if (ImGui::Selectable(setLabel, isSel)) {
                    mpSetWeaponSet(s_WSorted[j].idx);
                    g_MatchConfig.weaponSetIndex = (s8)s_WSorted[j].idx;
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    s_RoomSettingsDirty = true;
                }
                if (isSel) ImGui::SetItemDefaultFocus();
            }
            ImGui::TreePop();
        }
        if (!leader) ImGui::EndDisabled();
    }

    /* --- Custom Weapon Slots (visible when Custom set selected) --- */
    if (g_MpWeaponSetNum == WEAPONSET_CUSTOM) {
        ImGui::Spacing();
        s32 numWeaponOptions = mpGetNumWeaponOptions();
        static const char *slotLabels[NUM_MPWEAPONSLOTS] = {
            "Slot 1##cws", "Slot 2##cws", "Slot 3##cws",
            "Slot 4##cws", "Slot 5##cws", "Slot 6##cws"
        };

        if (!leader) ImGui::BeginDisabled();
        for (s32 slot = 0; slot < NUM_MPWEAPONSLOTS; slot++) {
            s32 curWeapon = mpGetWeaponSlot(slot);
            char *curWeaponName = mpGetWeaponLabel(curWeapon);
            ImGui::SetNextItemWidth(comboW);
            if (ImGui::BeginCombo(slotLabels[slot],
                                  curWeaponName ? curWeaponName : "???")) {
                for (s32 w = 0; w < numWeaponOptions; w++) {
                    char *wName = mpGetWeaponLabel(w);
                    if (!wName || !wName[0]) continue;
                    bool isSel = (w == curWeapon);
                    char wLabel[64];
                    snprintf(wLabel, sizeof(wLabel), "%s##cws%d_%d", wName, slot, w);
                    if (ImGui::Selectable(wLabel, isSel)) {
                        mpSetWeaponSlot(slot, w);
                        /* M0.1c: sync catalog ID to weapon_ids[] (PRIMARY) */
                        const char *wCatalogId = matchGetWeaponSlotCatalogId(slot);
                        strncpy(g_MatchConfig.weapon_ids[slot], wCatalogId,
                                sizeof(g_MatchConfig.weapon_ids[slot]) - 1);
                        g_MatchConfig.weapon_ids[slot][sizeof(g_MatchConfig.weapon_ids[slot]) - 1] = '\0';
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    }
                    if (isSel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
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
                    /* M0.1c: set catalog ID as PRIMARY identity */
                    strncpy(g_MatchConfig.spawn_weapon_id,
                            s_SpawnWeapons[wi].catalog_id,
                            sizeof(g_MatchConfig.spawn_weapon_id) - 1);
                    g_MatchConfig.spawn_weapon_id[sizeof(g_MatchConfig.spawn_weapon_id) - 1] = '\0';
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
    optToggle        ("Slow Motion",     MPOPTION_SLOWMOTION_ON,     leader);

    ImGui::Spacing();

    /* Sub-screen buttons: Handicaps (U-2) and Team Setup (U-3) */
    {
        float subBtnW = comboW;
        float subBtnH = pdguiScale(36.0f);

        if (!leader) ImGui::BeginDisabled();
        if (ImGui::Button("Player Handicaps...", ImVec2(subBtnW, subBtnH))) {
            menuPushDialog(&g_MpHandicapsMenuDialog);
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
        if (ImGui::Button("Team Setup...", ImVec2(subBtnW, subBtnH))) {
            menuPushDialog(&g_MpTeamsMenuDialog);
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
        if (!leader) ImGui::EndDisabled();

        /* Music selection — available to all players (personal playlist choice) */
        if (ImGui::Button("Select Music...", ImVec2(subBtnW, subBtnH))) {
            menuPushDialog(&g_MpSelectTunesMenuDialog);
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
    }

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
    float sbtnH = pdguiScale(36.0f);

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
            s_CounterOpClientId = pv2.clientId;
        } else {
            for (s32 pi = 0; pi < playerCount; pi++) {
                if (!lobbyGetPlayerInfo(pi, &pv2)) continue;
                s_CounterOpPlayer = pi;
                s_CounterOpClientId = pv2.clientId;
                antiPlayerName = pv2.name;
                break;
            }
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
                s_CounterOpClientId = pv.clientId;
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
        if (s_NumSpawnWeapons == 0) {
            buildSpawnWeaponList();
        }
        matchConfigInit();
        syncArenaFromConfig();
        syncSpawnWeaponFromConfig();
        botSelectClear();
        s_CodeGenerated   = false;
        s_MatchConfigInited = true;
    }

    float scale   = pdguiScaleFactor();
    float dialogW = pdguiMenuWidth();
    float dialogH = pdguiMenuHeight();
    ImVec2 menuPos = pdguiMenuPos();
    float dialogX  = menuPos.x;
    float dialogY  = menuPos.y;

    float pdTitleH = pdguiScale(39.0f);
    float tabBarH  = pdguiScale(45.0f);
    float footerH  = pdguiScale(90.0f);
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
        /* S295 F4 leak guard: release the context we own if the window is
         * culled this frame, so the player isn't frozen with no menu visible. */
        if (s_RoomPushedCtx && inputCtxIsActive(&g_CtxImGuiMenu)) {
            inputCtxPopDeferred(&g_CtxImGuiMenu);
            s_RoomPushedCtx = false;
        }
        return;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        /* B-124 pattern: push g_CtxImGuiMenu if not already active.
         * In solo mode the main menu already pushed it; in network mode
         * (direct room entry without main menu) we push it ourselves.
         * This ensures mouse is absolute, pdguiIsActive() blocks gameplay
         * input, and the 100ms grace period suppresses open-key double-fire. */
        if (!inputCtxIsActive(&g_CtxImGuiMenu)) {
            inputCtxPush(&g_CtxImGuiMenu);
            s_RoomPushedCtx = true;
        } else {
            s_RoomPushedCtx = false;
        }
        sysLogPrintf(LOG_NOTE, "MENU_IMGUI: room OPEN (solo=%d, pushedCtx=%d)",
                     s_IsSoloMode, s_RoomPushedCtx);
    }

    /* Opaque backdrop */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(dialogX, dialogY),
                          ImVec2(dialogX + dialogW, dialogY + dialogH),
                          pdguiPalImU32(PDPAL_BODYBG, 255));
    }

    /* R-name: show the current room name in the header. Look up g_LocalRoomId
     * in the SVC_ROOM_LIST cache. Falls back to "Room" if not yet received. */
    char s_RoomTitleBuf[80] = "Room";
    if (!s_IsSoloMode && g_LocalRoomId != 0xFF) {
        for (s32 ri = 0; ri < g_RoomCacheCount; ri++) {
            if (g_RoomCache[ri].id == g_LocalRoomId && g_RoomCache[ri].name[0]) {
                snprintf(s_RoomTitleBuf, sizeof(s_RoomTitleBuf),
                         "Room: %s", g_RoomCache[ri].name);
                break;
            }
        }
    }
    const char *screenTitle = s_IsSoloMode ? "Combat Simulator" : s_RoomTitleBuf;
    pdguiDrawPdDialog(dialogX, dialogY, dialogW, dialogH, screenTitle, 1);

    /* ---- Title bar ---- */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(dialogX + 8.0f, dialogY + 2.0f,
                          dialogW - 16.0f, pdTitleH - 4.0f);

        ImVec2 ts = ImGui::CalcTextSize(screenTitle);
        dl->AddText(ImVec2(dialogX + (dialogW - ts.x) * 0.5f,
                           dialogY + (pdTitleH - ts.y) * 0.5f),
                    pdguiPalImU32(PDPAL_TITLEFG, 255), screenTitle);
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
    static const int s_NumTabs = 4;

    /* Tab switching: use action-driven PageUp/PageDown (LB/RB mapping comes
     * from pdguiDriveImGuiNav) so controller/keyboard rebinds share one path.
     * Use a pending flag so SetSelected only fires for one frame. */
    static s32 s_BumperPendingTab = -1;

    if (ImGui::IsKeyPressed(ImGuiKey_PageUp, false)) {
        s_ActiveTab--;
        if (s_ActiveTab < 0) s_ActiveTab = s_NumTabs - 1;
        s_BumperPendingTab = s_ActiveTab;
        pdguiPlaySound(PDGUI_SND_SWIPE);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) {
        s_ActiveTab++;
        if (s_ActiveTab >= s_NumTabs) s_ActiveTab = 0;
        s_BumperPendingTab = s_ActiveTab;
        pdguiPlaySound(PDGUI_SND_SWIPE);
    }

    ImGui::PushStyleColor(ImGuiCol_Tab,        ImVec4(0.10f, 0.15f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.20f, 0.30f, 0.55f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabSelected,ImVec4(0.15f, 0.25f, 0.65f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, ImVec4(0.3f, 0.6f, 1.0f, 1.0f));

    if (ImGui::BeginTabBar("##room_tabs")) {
        for (int t = 0; t < s_NumTabs; t++) {
            ImGuiTabItemFlags tabFlags = ImGuiTabItemFlags_None;
            if (s_BumperPendingTab == t) {
                tabFlags |= ImGuiTabItemFlags_SetSelected;
            }
            bool tabOpen = ImGui::BeginTabItem(s_TabNames[t], nullptr, tabFlags);
            if (tabOpen) {
                if (s_ActiveTab != t) {
                    s_ActiveTab = t;
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                }
                ImGui::EndTabItem();
            }
        }
        s_BumperPendingTab = -1; /* Clear after tab bar processes it */
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

    float btnH = pdguiScale(42.0f);

    if (s_ActiveTab == 3) {
        /* Level Editor tab: everyone can launch their own editor session */
        float launchW = pdguiScale(270.0f);
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
        float startW = pdguiScale(210.0f);
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
                        /* Solo play: matchStart() resolves stage_id → stagenum.
                         * Keep s_MatchConfigInited=true so returning via
                         * pdguiSoloRoomReturn() preserves the full room setup
                         * (bots, weapons, arena, settings). */
                        pdguiSoloRoomClose();
                        matchStart();
                    } else {
                        int humanCount = s_IsSoloMode ? 1 : lobbyGetPlayerCount();
                        int maxBots = matchConfigMaxBotsForHumans(humanCount);
                        int numBots = countBots();
                        if (numBots > maxBots) {
                            numBots = maxBots;
                        }
                        u8 simType  = getLeadSimType();
                        netLobbyRequestStartWithSims(
                            GAMEMODE_MP,
                            s_Arenas[s_SelectedArena].id,
                            0,
                            0xFF,
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
                    const char *coop_id = catalogIdByRuntime(
                        ASSET_MAP, (s32)s_Missions[s_CampaignMission].stagenum);
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
                    const char *anti_id = catalogIdByRuntime(
                        ASSET_MAP, (s32)s_Missions[s_CounterOpMission].stagenum);
                    if (!anti_id) {
                        sysLogPrintf(LOG_ERROR,
                            "ROOM: no catalog entry for anti stagenum=0x%02x",
                            (unsigned)s_Missions[s_CounterOpMission].stagenum);
                        break;
                    }
                    if (s_CounterOpClientId == 0xFF) {
                        sysLogPrintf(LOG_ERROR, "ROOM: Counter-Op start rejected — no anti player selected");
                        break;
                    }
                    netLobbyRequestStartWithSims(
                        GAMEMODE_ANTI,
                        anti_id,
                        (u8)s_CounterOpDiff,
                        s_CounterOpClientId,
                        0,
                        0,
                        60,
                        0,
                        0,
                        0,
                        0,
                        0xFF);
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
    /* During the pre-match countdown, ESC/B should ONLY cancel the countdown
     * (handled by pdgui_countdown.cpp). Don't also leave the room. */
    bool countdownBlocks = (!s_IsSoloMode && pdguiCountdownIsActive());
    if (ImGui::Button(leaveLabel, ImVec2(leaveW, btnH)) ||
        (!countdownBlocks &&
         (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
          ImGui::IsKeyPressed(ImGuiKey_Escape, false)))) {
        sysLogPrintf(LOG_NOTE, "MENU_IMGUI: room CLOSE via %s/ESC (solo=%d)",
                     leaveLabel, s_IsSoloMode);
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        s_MatchConfigInited = false;  /* reset on next enter */
        s_CodeGenerated     = false;
        /* B-124 pattern: only pop the context if we pushed it ourselves.
         * In solo mode the main menu owns the context and handles the pop.
         * In network mode (s_RoomPushedCtx) we must pop it here. */
        if (s_RoomPushedCtx && inputCtxIsActive(&g_CtxImGuiMenu)) {
            inputCtxPopDeferred(&g_CtxImGuiMenu);
            s_RoomPushedCtx = false;
        }
        if (s_IsSoloMode) {
            pdguiSoloRoomClose();  /* return to main menu */
        } else {
            /* R-3: Tell server we're leaving the room */
            if (g_NetMode == RM_NETMODE_CLIENT) {
                netbufStartWrite(&g_NetMsgRel);
                netmsgClcRoomLeaveWrite(&g_NetMsgRel);
                netSend(NULL, &g_NetMsgRel, 1, 0);
            }
            pdguiSetInRoom(0);  /* return to social lobby, stay connected */
        }
    }

    /* ---- Bot settings modal ---- */
    if (s_BotModalOpen) {
        ImGui::OpenPopup("Bot Settings##botmodal");
        s_BotModalOpen = false;
    }

    ImGui::SetNextWindowSize(ImVec2(pdguiScale(810.0f), 0.0f));
    if (ImGui::BeginPopupModal("Bot Settings##botmodal", nullptr, 0)) {
        if (s_EditBotSlotIdx >= 1
            && s_EditBotSlotIdx < g_MatchConfig.numSlots
            && g_MatchConfig.slots[s_EditBotSlotIdx].type == SLOT_BOT) {

            struct matchslot *sl = &g_MatchConfig.slots[s_EditBotSlotIdx];
            float mw = pdguiScale(480.0f);

            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Bot Settings");
            ImGui::Separator();
            ImGui::Spacing();

            /* Left column: controls */
            ImGui::BeginGroup();

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
                    const char *bid2 = catalogMpBodyId(b2);
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
                    const char *bid = catalogMpBodyId(b);
                    bool sel = bid && sl->body_id[0] && strcmp(bid, sl->body_id) == 0;
                    char bLabel[64];
                    snprintf(bLabel, sizeof(bLabel), "%s##mb%u", bodyName, b);
                    if (ImGui::Selectable(bLabel, sel)) {
                        if (bid) {
                            strncpy(sl->body_id, bid, sizeof(sl->body_id) - 1);
                            sl->body_id[sizeof(sl->body_id) - 1] = '\0';
                        }
                        const char *hid = catalogMpHeadId(b);
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

            /* ---- Advanced / Simple toggle ---- */
            const char *advLabel = s_BotModalShowAdvanced ? "- Simple -" : "+ Advanced";
            if (ImGui::Button(advLabel, ImVec2(pdguiScale(180.0f), 0.0f))) {
                s_BotModalShowAdvanced = !s_BotModalShowAdvanced;
                if (s_BotModalShowAdvanced && s_BotPresetCacheDirty) {
                    rebuildBotPresetCache();
                    s_BotPresetSelected = -1;
                }
                pdguiPlaySound(PDGUI_SND_SELECT);
            }

            /* ---- Advanced section ---- */
            if (s_BotModalShowAdvanced && s_EditBotSlotIdx >= 0
                && s_EditBotSlotIdx < MATCH_MAX_SLOTS)
            {
                BotTraits *traits = &s_BotTraits[s_EditBotSlotIdx];

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Custom Traits");
                ImGui::Separator();

                /* Load Preset combo */
                {
                    const char *previewLabel = (s_BotPresetSelected >= 0
                        && s_BotPresetSelected < s_BotPresetCount)
                        ? s_BotPresets[s_BotPresetSelected]->id
                        : "-- None --";

                    if (ImGui::BeginCombo("Load Preset", previewLabel)) {
                        if (ImGui::Selectable("-- None --", s_BotPresetSelected == -1)) {
                            s_BotPresetSelected = -1;
                        }
                        for (s32 p = 0; p < s_BotPresetCount; p++) {
                            const asset_entry_t *preset = s_BotPresets[p];
                            char pLabel[96];
                            snprintf(pLabel, sizeof(pLabel), "%s##prs%d",
                                     preset->id, p);
                            bool isSel = (p == s_BotPresetSelected);
                            if (ImGui::Selectable(pLabel, isSel)) {
                                s_BotPresetSelected = p;
                                traits->accuracy     = preset->ext.bot_variant.accuracy;
                                traits->reactionTime = preset->ext.bot_variant.reaction_time;
                                traits->aggression   = preset->ext.bot_variant.aggression;
                                strncpy(traits->baseType,
                                        preset->ext.bot_variant.base_type,
                                        sizeof(traits->baseType) - 1);
                                traits->baseType[sizeof(traits->baseType) - 1] = '\0';
                                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                            }
                            if (isSel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }

                /* Base Type combo */
                {
                    s32 curIdx = 0;
                    for (s32 i = 0; i < s_NumBaseTypes; i++) {
                        if (strcmp(traits->baseType, s_BaseTypeNames[i]) == 0) {
                            curIdx = i;
                            break;
                        }
                    }
                    if (ImGui::Combo("Base Type", &curIdx,
                                     s_BaseTypeNames, s_NumBaseTypes)) {
                        strncpy(traits->baseType, s_BaseTypeNames[curIdx],
                                sizeof(traits->baseType) - 1);
                        traits->baseType[sizeof(traits->baseType) - 1] = '\0';
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    }
                }

                /* Trait sliders */
                ImGui::SliderFloat("Accuracy",   &traits->accuracy,     0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Reaction",   &traits->reactionTime,  0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Aggression", &traits->aggression,    0.0f, 1.0f, "%.2f");

                ImGui::Spacing();

                /* Save as Preset button */
                if (ImGui::Button("Save as Preset...",
                                  ImVec2(pdguiScale(240.0f), 0.0f))) {
                    s_SavePresetName[0] = '\0';
                    ImGui::OpenPopup("##save_preset_room");
                    pdguiPlaySound(PDGUI_SND_SELECT);
                }

                /* Save preset nested popup */
                if (ImGui::BeginPopup("##save_preset_room")) {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
                                       "Save Bot Preset");
                    ImGui::Separator();
                    ImGui::Spacing();
                    ImGui::SetNextItemWidth(pdguiScale(300.0f));
                    ImGui::InputText("Name##psname", s_SavePresetName,
                                     sizeof(s_SavePresetName));
                    ImGui::Spacing();

                    bool canSave = (s_SavePresetName[0] != '\0');
                    if (!canSave) ImGui::BeginDisabled();
                    if (ImGui::Button("Save##pssave",
                                      ImVec2(pdguiScale(120.0f), 0.0f))) {
                        if (botVariantSave(s_SavePresetName,
                                           traits->baseType,
                                           traits->accuracy,
                                           traits->reactionTime,
                                           traits->aggression,
                                           "custom", "", "")) {
                            s_BotPresetCacheDirty = 1;
                        }
                        ImGui::CloseCurrentPopup();
                        pdguiPlaySound(PDGUI_SND_SELECT);
                    }
                    if (!canSave) ImGui::EndDisabled();

                    ImGui::SameLine();
                    if (ImGui::Button("Cancel##pscancel",
                                      ImVec2(pdguiScale(120.0f), 0.0f))) {
                        ImGui::CloseCurrentPopup();
                        pdguiPlaySound(PDGUI_SND_SELECT);
                    }
                    ImGui::EndPopup();
                }
            } /* end Advanced section */

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Done", ImVec2(pdguiScale(120.0f), 0.0f))) {
                s_BotModalShowAdvanced = false;
                s_BotPreviewRotY = 0.0f;
                s_EditBotSlotIdx = -1;
                ImGui::CloseCurrentPopup();
                pdguiPlaySound(PDGUI_SND_SELECT);
            }

            ImGui::EndGroup(); /* end left column */

            /* ---- Right column: 3D character preview ---- */
            ImGui::SameLine(0, pdguiScale(18.0f));
            ImGui::BeginGroup();

            /* Rotate and request preview for current body+head */
            s_BotPreviewRotY += 0.022f; /* ~1.26 rad/s at 60fps */
            if (s_BotPreviewRotY > 6.2832f) s_BotPreviewRotY -= 6.2832f;
            pdguiCharPreviewSetRotY(s_BotPreviewRotY);
            pdguiCharPreviewRequest(sl->head_id, sl->body_id);

            float previewSz = pdguiScale(240.0f);

            if (pdguiCharPreviewIsReady()) {
                ImTextureID texId = (ImTextureID)(uintptr_t)pdguiCharPreviewGetTextureId();
                ImGui::Image(texId, ImVec2(previewSz, previewSz));
            } else {
                /* Dark placeholder while first frame renders */
                ImVec2 cursor = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    cursor,
                    ImVec2(cursor.x + previewSz, cursor.y + previewSz),
                    IM_COL32(20, 20, 30, 200));
                ImGui::Dummy(ImVec2(previewSz, previewSz));
            }

            /* Character name label below preview */
            {
                const char *bName = mpGetBodyName(sl->bodynum);
                if (bName && bName[0]) {
                    float textW = ImGui::CalcTextSize(bName).x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX()
                                         + (previewSz - textW) * 0.5f);
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "%s", bName);
                }
            }

            ImGui::EndGroup(); /* end right column */
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

    ImGui::SetNextWindowSize(ImVec2(pdguiScale(480.0f), 0.0f));
    if (ImGui::BeginPopupModal("Save Scenario##savescenpop", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Save Scenario");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Name:");
        ImGui::SetNextItemWidth(pdguiScale(420.0f));
        ImGui::InputText("##savename", s_SaveNameBuf, sizeof(s_SaveNameBuf));

        ImGui::Spacing();

        float bw = pdguiScale(150.0f);
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

    ImGui::SetNextWindowSize(ImVec2(pdguiScale(570.0f), pdguiScale(420.0f)));
    if (ImGui::BeginPopupModal("Load Scenario##loadscenpop", nullptr,
                               ImGuiWindowFlags_NoResize)) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Load Scenario");
        ImGui::Separator();
        ImGui::Spacing();

        if (s_ScenarioCount == 0) {
            ImGui::TextDisabled("No saved scenarios found.");
            ImGui::TextDisabled("Save a scenario first using \"Save Scenario\".");
        } else {
            float listH = pdguiScale(270.0f);
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

        float fbw = pdguiScale(150.0f);
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

    /* R-5: If leader changed settings this frame, broadcast to room members */
    if (s_RoomSettingsDirty && g_NetMode == NETMODE_CLIENT && lobbyIsLocalLeader()) {
        netSendRoomSettingsUpdate();
        s_RoomSettingsDirty = false;
    }

    /* D5 Phase 2: D-pad wrapping — must be after all widgets, before End() */
    pdguiNavTickWrap();

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
    s_RoomPushedCtx     = false;
    free(s_Arenas);
    s_Arenas            = NULL;
    s_NumArenas         = 0;
    s_ArenasCapacity    = 0;
    s_ArenasBuilt       = false;
    for (int i = 0; i < ARENA_SEC_COUNT; i++) {
        s_SectionStart[i] = 0;
        s_SectionCount[i] = 0;
    }
    s_CodeGenerated     = false;
    s_IsSoloMode        = false;  /* caller sets via pdguiRoomScreenSetSolo() after reset */
    s_ActiveTab         = 0;
    s_CampaignMission   = 0;
    s_CampaignDiff      = DIFF_A;
    s_CounterOpMission  = 0;
    s_CounterOpDiff     = DIFF_A;
    s_CounterOpPlayer   = 0;
    s_CounterOpClientId = 0xFF;
    s_SelectedArena     = 0;
    botSelectClear();
    s_BotModalOpen         = false;
    s_EditBotSlotIdx       = -1;
    s_BotModalShowAdvanced = false;
    s_BotPresetCacheDirty  = 1;
    initBotTraits();
    s_SpawnWeaponIdx    = 0;
    s_ShowSaveScenario  = false;
    s_ShowLoadScenario  = false;
    s_SaveNameBuf[0]    = '\0';
    s_ScenarioCount     = 0;
    s_ScenarioStatusMsg[0] = '\0';
}
