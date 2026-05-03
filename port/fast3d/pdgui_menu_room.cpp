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
#include "pdgui_layout.h"       /* pdguiPopupDarkenBehind */
#include "pdgui_widgets.h"      /* Priority L: shared label-left widget helpers */
#include "system.h"
#include "inputctx.h"
#include "menupool.h"
#include "menugraph.h"
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

/* Rule 6 (2026-05-03): Y on any menu opens the Social overlay (friends,
 * party invites, voice, recent players) as top-of-stack without unwinding
 * the underlying menu. The action is wired through ACTION_MENU_SOCIAL
 * (alias of ACTION_MENU_TERTIARY) per Mike's Q3 directive. The actual
 * surface lives in pdgui_friends.cpp. */
void pdguiFriendsSocialOpen(void);
s32  pdguiFriendsSocialIsOpen(void);

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

/* Player/team capacity constants — pulled from the C++-safe mirror header.
 * See pdgui_constants.h for why this indirection exists and how drift is
 * caught at build time. */
#include "pdgui_constants.h"

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
/* B-235 follow-up: returns an mp head index suitable for the given body.
 * For bodies with a specific paired head, returns that head (deterministic).
 * For bodies with HEAD_RANDOM_GENDER, returns a fresh random pick from the
 * gender-specific pool on EACH call. Used by multi-select Set Character so
 * each bot gets an independent head roll. */
s32 mpDefaultHeadForBody(s32 mpbodynum);
/* Body/head data accessed via catalog accessors */
/* Phase 5: catalog ID accessors for lobby players */
const char *lobbyGetPlayerBodyId(s32 idx);
const char *lobbyGetPlayerHeadId(s32 idx);

/* Local-player character override (room-scoped). Catalog ID accessors
 * + writers, declared in port/fast3d/pdgui_bridge.c. The room screen
 * uses these for the temporary character override (the saved Agent on
 * disk is untouched (disk writes are filemgr-explicit). */
const char *mpPlayerConfigGetBodyId(s32 playernum);
const char *mpPlayerConfigGetHeadId(s32 playernum);
void mpPlayerConfigSetHeadBody(s32 playernum, const char *head_id, const char *body_id);
const char *catalogGetBodyDefaultHead(const char *body_id);

/* H.5 universal guard (S593): integrated-head bodies (Skedar, Dr Carroll,
 * Eye Spy) carry their own head model; the head selector must lock when
 * one is selected.  Reads via the catalog accessor that wraps the
 * `unk00_01` flag on `g_HeadsAndBodies[]`. */
s32 catalogGetBodyIsComplete(s32 bodynum);

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
void matchConfigRerollBotName(s32 idx);

/* R-3: Room networking — send leave to server */
struct netbuf;
u32 netmsgClcRoomLeaveWrite(struct netbuf *dst);
void netListenHostRoomLeave(void);
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
    char catalog_id[64]; /* catalog ID e.g. "base:falcon2", or "" for Random/Fiesta */
    char name[64];       /* display name */
    u8   mode;           /* SPAWNWEAPON_MODE_* — RANDOM/FIESTA for synthetic
                          * dropdown entries, SPECIFIC for catalog weapons. */
};

#define MAX_SPAWN_WEAPONS 64
static spawnweapon_entry s_SpawnWeapons[MAX_SPAWN_WEAPONS];
static int s_NumSpawnWeapons = 0;

static void buildSpawnWeaponList(void)
{
    s_NumSpawnWeapons = 0;

    /* Entry 0: Random (rolled once at match start; every spawn uses that). */
    s_SpawnWeapons[0].catalog_id[0] = '\0';
    strncpy(s_SpawnWeapons[0].name, "Random", sizeof(s_SpawnWeapons[0].name));
    s_SpawnWeapons[0].mode = SPAWNWEAPON_MODE_RANDOM;

    /* Entry 1: Fiesta (rolls fresh per spawn, per player). S482 (2026-04-27). */
    s_SpawnWeapons[1].catalog_id[0] = '\0';
    strncpy(s_SpawnWeapons[1].name, "Fiesta", sizeof(s_SpawnWeapons[1].name));
    s_SpawnWeapons[1].mode = SPAWNWEAPON_MODE_FIESTA;

    s_NumSpawnWeapons = 2;

    /* Scan catalog for all ASSET_WEAPON entries, skip NONE/DISABLED/SHIELD.
     * Post-cull (2026-04-26): MPWEAPON_SHIELD = 0x27, MPWEAPON_DISABLED = 0x28.
     * The legacy 0x2f/0x30 values pre-cull are also rejected for safety in
     * case any pre-cull catalog data leaks through. */
    for (int i = 0; i < assetCatalogGetPoolSize(); i++) {
        const asset_entry_t *e = assetCatalogGetByIndex(i);
        if (!e) continue;
        if (e->type != ASSET_WEAPON) continue;
        if (s_NumSpawnWeapons >= MAX_SPAWN_WEAPONS) break;
        s32 wid = e->ext.weapon.weapon_id;
        if (wid == 0x00 /* NONE */
                || wid == 0x27 /* SHIELD post-cull */
                || wid == 0x28 /* DISABLED post-cull */
                || wid == 0x2f /* SHIELD pre-cull (defensive) */
                || wid == 0x30 /* DISABLED pre-cull (defensive) */) {
            continue;
        }
        spawnweapon_entry *sw = &s_SpawnWeapons[s_NumSpawnWeapons];
        strncpy(sw->catalog_id, e->id, sizeof(sw->catalog_id) - 1);
        sw->catalog_id[sizeof(sw->catalog_id) - 1] = '\0';
        strncpy(sw->name, e->ext.weapon.name ? e->ext.weapon.name : e->id,
                sizeof(sw->name) - 1);
        sw->name[sizeof(sw->name) - 1] = '\0';
        sw->mode = SPAWNWEAPON_MODE_SPECIFIC;
        s_NumSpawnWeapons++;
    }

    /* B-187: alphabetize weapon entries (Random + Fiesta stay at index 0/1). */
    if (s_NumSpawnWeapons > 3) {
        std::sort(s_SpawnWeapons + 2, s_SpawnWeapons + s_NumSpawnWeapons,
                  [](const spawnweapon_entry &a, const spawnweapon_entry &b) {
                      return strcmp(a.name, b.name) < 0;
                  });
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

/* Rule 8 (2026-05-03): pending team-jump in the player panel.
 *
 * Set by the LT/RT poll at the top of pdguiRoomScreenRender when the
 * Combat Sim tab is active; consumed by renderPlayerPanel which sets
 * keyboard focus on the first row of the target team. Direction:
 *   -1 = previous team's first player
 *   +1 = next team's first player
 *    0 = idle (no pending jump)
 *
 * The renderer clears the flag after consuming so the jump is one-shot.
 * Implementation is the v1 cut: jumps only within the player panel and
 * only when teams are enabled. Left-panel section-jump (arena -> gametype
 * -> ...) is downstream work tracked in the kanban under the same card. */
static int s_RoomPlayerSectionJumpPending = 0;

/* R-5: set true whenever the leader changes settings; cleared after CLC_ROOM_SETTINGS_UPDATE send */
static bool s_RoomSettingsDirty = false;

/* S300: s_RoomPushedCtx removed — menu pool owns the ctx for MENU_TYPE_ROOM
 * via menupoolAcquireDialog / menupoolReleaseDialog. In solo mode the ctx
 * was already pushed by the main menu (shared mode — pool won't pop on
 * release); in network mode the pool acquires and owns the pop. */

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
static int  s_BotSelectCount     = 0;       /* cached count of selected bots */
static int  s_BotLastClickedSlot = -1;      /* anchor for Shift+Click range select */
static bool s_BotModalOpen       = false;
static int  s_EditBotSlotIdx     = -1;      /* slot index being edited in the modal */

/* Team sort preset — drives the dropdown above the player list when teams are
 * enabled. TEAMSORT_CUSTOM means "user has edited teams by hand"; switching to
 * any other preset reassigns all slots via applyTeamSort(). */
enum TeamSortMode {
    TEAMSORT_CUSTOM = 0,
    TEAMSORT_TWO_TEAMS,
    TEAMSORT_THREE_TEAMS,
    TEAMSORT_FOUR_TEAMS,
    TEAMSORT_HUMANS_VS_SIMS,
    TEAMSORT_HUMAN_SIM_PAIRS,
    TEAMSORT_COUNT_
};
static const char *s_TeamSortNames[TEAMSORT_COUNT_] = {
    "Custom",
    "2 Teams",
    "3 Teams",
    "4 Teams",
    "Humans vs Sims",
    "Human-Sim Pairs",
};
static int s_TeamSortMode = TEAMSORT_CUSTOM;

/* Name buffer for batch name edits in the bot context menu */
static char s_BotCtxNameBuf[MAX_PLAYER_NAME] = {0};

/* ========================================================================
 * Local player character override (room-scoped, this match only).
 *
 * The user can change their visible character from inside the Room
 * without overwriting the saved Agent file on disk. The override is
 * captured once when it goes live: we copy the current persistent
 * mpchrconfig body/head IDs into backup, write the picked IDs through
 * `mpPlayerConfigSetHeadBody` (which is an in-memory setter; disk
 * save is filemgr-explicit and never auto-fires from this path), and
 * on `pdguiRoomScreenReset` (called on roomLeave) we restore the
 * persistent IDs from the backup before any future code reads
 * `g_PlayerConfigsArray[0]`.
 *
 * Why `mpPlayerConfigSetHeadBody` and not just `g_MatchConfig.slots[0]`:
 * `matchConfigInit` populates `slots[0]` from `g_PlayerConfigsArray[0]`
 * once; the lobby propagation in MP also reads from the player config,
 * not the match-config slot. Writing through the player config is the
 * single channel that keeps solo and MP behaviour consistent without
 * any wire change. The body / head fields already cross the wire as
 * catalog ID strings, so no protocol bump is required.
 *
 * Constraint compliance:
 *   - Save format: untouched. The override never reaches disk.
 *   - Wire format: untouched. The body / head fields already cross the
 *     wire as catalog ID strings (protocol v32+); the override values
 *     ride that channel.
 *   - Persistent profile: restored on roomLeave. Even if the room is
 *     left abruptly (disconnect, force-close), the next match start
 *     reads from the live in-memory profile, which is the in-memory
 *     override; once `pdguiRoomScreenReset` fires, the backup is
 *     replayed back into the profile.
 */
static bool s_RoomCharOverrideActive = false;
static char s_RoomCharBackupBodyId[64] = {0};
static char s_RoomCharBackupHeadId[64] = {0};
static bool s_ShowChangeCharModal = false;
static char s_PendingCharBodyId[64] = {0};
static char s_PendingCharHeadId[64] = {0};

static void roomCharOverrideCaptureIfNeeded(void)
{
    if (s_RoomCharOverrideActive) return;
    const char *curBody = mpPlayerConfigGetBodyId(0);
    const char *curHead = mpPlayerConfigGetHeadId(0);
    strncpy(s_RoomCharBackupBodyId,
            curBody ? curBody : "",
            sizeof(s_RoomCharBackupBodyId) - 1);
    s_RoomCharBackupBodyId[sizeof(s_RoomCharBackupBodyId) - 1] = '\0';
    strncpy(s_RoomCharBackupHeadId,
            curHead ? curHead : "",
            sizeof(s_RoomCharBackupHeadId) - 1);
    s_RoomCharBackupHeadId[sizeof(s_RoomCharBackupHeadId) - 1] = '\0';
    s_RoomCharOverrideActive = true;
    sysLogPrintf(LOG_NOTE,
                 "ROOM.CHAR: override captured. backup body='%s' head='%s'",
                 s_RoomCharBackupBodyId, s_RoomCharBackupHeadId);
}

static void roomCharOverrideRestore(void)
{
    if (!s_RoomCharOverrideActive) return;
    /* Replay the saved persistent IDs back into the live in-memory
     * profile. Disk is never written here (filemgr is the only writer
     * to the on-disk Agent file). */
    mpPlayerConfigSetHeadBody(0,
                               s_RoomCharBackupHeadId,
                               s_RoomCharBackupBodyId);
    sysLogPrintf(LOG_NOTE,
                 "ROOM.CHAR: override restored. body='%s' head='%s'",
                 s_RoomCharBackupBodyId, s_RoomCharBackupHeadId);
    s_RoomCharOverrideActive = false;
    s_RoomCharBackupBodyId[0] = '\0';
    s_RoomCharBackupHeadId[0] = '\0';
}

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

/* ---- Per-player portrait system (S352 — D5 Phase 5) ----
 * Bakes each connected lobby player's character to a standalone GL texture
 * using the shared pdguiCharPreview FBO.  Sequential pipeline: one bake at a
 * time.  Portraits are invalidated when a player's body/head changes or when
 * they leave.  Bot modal guard: baking is skipped while the bot edit modal is
 * open so the two callers never fight over the charpreview FBO. */
#define LOBBY_PORTRAIT_MAX 8

struct LobbyPortrait {
    u32  glTex;          /* 0 = not baked */
    char head_id[64];    /* "" = not yet captured */
    char body_id[64];
};

static LobbyPortrait s_LobbyPortraits[LOBBY_PORTRAIT_MAX];
static bool s_LobbyPortraitsInited    = false;
static s32  s_LobbyPortraitPending    = -1;    /* lobby idx in current bake */
static bool s_LobbyPortraitWaitReady  = false;
static float s_LobbyPortraitAlpha[LOBBY_PORTRAIT_MAX]; /* join fade-in [0,1] */
static s32   s_HoverLobbyIdx = -1;  /* lobby idx of currently-hovered human row; -1 = none */

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

/* Apply a team-sort preset by mode index. Mirrors the helpers in
 * pdgui_menu_teamsetup.cpp (those are TU-local there, so the same logic is
 * duplicated here to keep this screen self-contained). TEAMSORT_CUSTOM is a
 * no-op — the user is managing teams manually. Caller is responsible for
 * marking s_RoomSettingsDirty. */
static void applyTeamSort(int mode) {
    switch (mode) {
        case TEAMSORT_TWO_TEAMS:
            for (int i = 0; i < (int)g_MatchConfig.numSlots; i++) {
                g_MatchConfig.slots[i].team = (u8)(i % 2);
            }
            break;
        case TEAMSORT_THREE_TEAMS:
            for (int i = 0; i < (int)g_MatchConfig.numSlots; i++) {
                g_MatchConfig.slots[i].team = (u8)(i % 3);
            }
            break;
        case TEAMSORT_FOUR_TEAMS:
            for (int i = 0; i < (int)g_MatchConfig.numSlots; i++) {
                g_MatchConfig.slots[i].team = (u8)(i % 4);
            }
            break;
        case TEAMSORT_HUMANS_VS_SIMS:
            for (int i = 0; i < (int)g_MatchConfig.numSlots; i++) {
                g_MatchConfig.slots[i].team =
                    (g_MatchConfig.slots[i].type == SLOT_PLAYER) ? 0 : 1;
            }
            break;
        case TEAMSORT_HUMAN_SIM_PAIRS: {
            /* Reset to an unassigned sentinel, then pair each human with the
             * first available bot on the same team, cycling team index. */
            for (int i = 0; i < (int)g_MatchConfig.numSlots; i++) {
                g_MatchConfig.slots[i].team = 255;
            }
            int teamIdx = 0;
            for (int i = 0; i < (int)g_MatchConfig.numSlots; i++) {
                if (g_MatchConfig.slots[i].type != SLOT_PLAYER) continue;
                g_MatchConfig.slots[i].team = (u8)teamIdx;
                for (int j = i + 1; j < (int)g_MatchConfig.numSlots; j++) {
                    if (g_MatchConfig.slots[j].type == SLOT_BOT
                        && g_MatchConfig.slots[j].team == 255) {
                        g_MatchConfig.slots[j].team = (u8)teamIdx;
                        break;
                    }
                }
                teamIdx = (teamIdx + 1) % 8;
            }
            for (int i = 0; i < (int)g_MatchConfig.numSlots; i++) {
                if (g_MatchConfig.slots[i].team == 255) {
                    g_MatchConfig.slots[i].team = 0;
                }
            }
            break;
        }
        default:
            /* TEAMSORT_CUSTOM — no-op */
            break;
    }
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

/* M-5 (Menu Stack Compliance Tier 1) — destructive-action confirm modals.
 * Canonical S385 pattern: 5-frame SetKeyboardFocusHere(0) on Cancel + 3-frame
 * input debounce so the Enter/A press that opened the popup can't bleed
 * through. See context/designs/menu-stack-architecture.md §C4/§C5. */
#define ROOM_CONFIRM_FRAME_DEBOUNCE     3
#define ROOM_CONFIRM_FORCE_FOCUS_FRAMES 5

/* Leave Room / Back to Menu confirm — top-level popup in pdguiRoomScreenRender. */
static bool s_ShowLeaveConfirm      = false;
static s32  s_LeaveConfirmOpenFrame = -1;

/* Scenario Delete confirm — nested popup inside Load Scenario modal. */
static bool s_ShowScenarioDeleteConfirm      = false;
static s32  s_ScenarioDeleteConfirmOpenFrame = -1;
static char s_ScenarioDeletePath[SCENARIO_PATH_MAX] = "";
static char s_ScenarioDeleteDisplay[SCENARIO_PATH_MAX] = "";

/* M-20 progressive focus: set by the Scenario combo on a change; consumed
 * on the next frame by the Start Match button to jump keyboard/controller
 * focus straight from "pick scenario" to "confirm/launch" — the
 * menu-stack §6.4 pattern for Room's selection flow. */
static bool s_StartMatchFocusPending = false;

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

    /* Priority L (2026-04-25): NavFlattened so D-pad traverses across columns. */
    ImGui::BeginChild("##le_left", ImVec2(panelW, panelH), ImGuiChildFlags_NavFlattened);

    ImGui::TextColored(pdguiVec4TitleGlow(), "Level Editor");
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

    /* Priority L (2026-04-25): NavFlattened so D-pad traverses across columns. */
    ImGui::BeginChild("##le_right_outer", ImVec2(panelW, panelH),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);

    /* ---- Spawned objects list ---- */
    ImGui::TextColored(pdguiVec4TitleGlow(),
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
    ImGui::TextColored(pdguiVec4TitleGlow(), "Properties");

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
        /* Priority L (2026-04-25): label LEFT via pdguiCheckbox. */
        pdguiCheckbox("Uniform", &obj->uniform_scale);
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
        /* Priority L (2026-04-25): label LEFT via pdguiCheckbox. */
        pdguiCheckbox("Enabled", &obj->collision);

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

    ImGui::TextColored(pdguiVec4TitleGlow(), "Level Editor  [Active]");
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
    /* S482 (2026-04-27): mode-aware sync. RANDOM and FIESTA are synthetic
     * dropdown entries with empty catalog_id; SPECIFIC matches by catalog ID. */
    if (g_MatchConfig.spawnWeaponMode == SPAWNWEAPON_MODE_FIESTA) {
        for (int i = 0; i < s_NumSpawnWeapons; i++) {
            if (s_SpawnWeapons[i].mode == SPAWNWEAPON_MODE_FIESTA) {
                s_SpawnWeaponIdx = i;
                return;
            }
        }
    } else if (g_MatchConfig.spawnWeaponMode == SPAWNWEAPON_MODE_SPECIFIC
            && g_MatchConfig.spawn_weapon_id[0]) {
        for (int i = 0; i < s_NumSpawnWeapons; i++) {
            if (s_SpawnWeapons[i].mode == SPAWNWEAPON_MODE_SPECIFIC
                    && strcmp(s_SpawnWeapons[i].catalog_id,
                              g_MatchConfig.spawn_weapon_id) == 0) {
                s_SpawnWeaponIdx = i;
                return;
            }
        }
    }
    /* RANDOM (default), or SPECIFIC with stale/missing id, or any unknown:
     * land on entry 0 (Random). */
    s_SpawnWeaponIdx = 0;
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
    /* Priority L (2026-04-25): pdguiCheckbox places the label on the LEFT
     * and plays its own TOGGLE sound; we replace the SUBFOCUS sound the
     * legacy optToggle used. */
    if (pdguiCheckbox(label, &on)) {
        if (on) g_MatchConfig.options |= flag;
        else    g_MatchConfig.options &= ~flag;
        s_RoomSettingsDirty = true;
    }
    if (!leader) ImGui::EndDisabled();
}

/* Like optToggle but logic is inverted (flag ON means feature OFF) */
static void optToggleInverted(const char *label, u32 flag, bool leader)
{
    bool on = (g_MatchConfig.options & flag) == 0; /* true when feature is enabled */
    if (!leader) ImGui::BeginDisabled();
    /* Priority L (2026-04-25): pdguiCheckbox places the label on the LEFT. */
    if (pdguiCheckbox(label, &on)) {
        if (on) g_MatchConfig.options &= ~flag;
        else    g_MatchConfig.options |= flag;
        s_RoomSettingsDirty = true;
    }
    if (!leader) ImGui::EndDisabled();
}

/* ========================================================================
 * Per-player portrait helpers (S352 — D5 Phase 5)
 * ======================================================================== */

static void lobbyPortraitsReset(void)
{
    for (s32 i = 0; i < LOBBY_PORTRAIT_MAX; i++) {
        if (s_LobbyPortraits[i].glTex)
            pdguiCharPreviewFreeTexture(s_LobbyPortraits[i].glTex);
    }
    memset(s_LobbyPortraits, 0, sizeof(s_LobbyPortraits));
    for (s32 i = 0; i < LOBBY_PORTRAIT_MAX; i++)
        s_LobbyPortraitAlpha[i] = 0.0f;
    s_LobbyPortraitPending   = -1;
    s_LobbyPortraitWaitReady = false;
    s_LobbyPortraitsInited   = true;
}

/* Sync portrait cache against current lobby state: invalidate stale entries,
 * advance fade-in alphas, free portraits for players who left. */
static void lobbyPortraitsSync(s32 humanCount)
{
    for (s32 i = humanCount; i < LOBBY_PORTRAIT_MAX; i++) {
        if (s_LobbyPortraits[i].glTex) {
            pdguiCharPreviewFreeTexture(s_LobbyPortraits[i].glTex);
            s_LobbyPortraits[i].glTex = 0;
        }
        s_LobbyPortraits[i].head_id[0] = '\0';
        s_LobbyPortraits[i].body_id[0] = '\0';
        if (s_LobbyPortraitPending == i) {
            s_LobbyPortraitPending   = -1;
            s_LobbyPortraitWaitReady = false;
        }
    }

    for (s32 i = 0; i < humanCount && i < LOBBY_PORTRAIT_MAX; i++) {
        const char *hid = lobbyGetPlayerHeadId(i);
        const char *bid = lobbyGetPlayerBodyId(i);
        LobbyPortrait &p = s_LobbyPortraits[i];

        bool match = (hid && bid && hid[0] && bid[0] &&
                      strcmp(p.head_id, hid) == 0 &&
                      strcmp(p.body_id, bid) == 0);
        if (!match && p.glTex) {
            pdguiCharPreviewFreeTexture(p.glTex);
            p.glTex = 0;
            if (s_LobbyPortraitPending == i) {
                s_LobbyPortraitPending   = -1;
                s_LobbyPortraitWaitReady = false;
            }
        }
        if (!match) {
            p.head_id[0] = '\0';
            p.body_id[0] = '\0';
        }

        /* Fade-in: ramp alpha toward 1.0 over ~25 frames */
        if (s_LobbyPortraitAlpha[i] < 1.0f) {
            s_LobbyPortraitAlpha[i] += 0.04f;
            if (s_LobbyPortraitAlpha[i] > 1.0f)
                s_LobbyPortraitAlpha[i] = 1.0f;
        }
    }
}

/* Drive the sequential baking pipeline.  One bake per frame maximum.
 * Skipped while the bot modal is open (they share the charpreview FBO). */
static void lobbyPortraitsTick(s32 humanCount)
{
    if (s_BotModalOpen) return;
    if (s_HoverLobbyIdx >= 0) return;  /* hover preview owns the FBO this frame */

    if (s_LobbyPortraitWaitReady && s_LobbyPortraitPending >= 0) {
        if (pdguiCharPreviewIsReady()) {
            u32 tex = pdguiCharPreviewBakeToTexture();
            s32 idx = s_LobbyPortraitPending;
            if (tex && idx < LOBBY_PORTRAIT_MAX)
                s_LobbyPortraits[idx].glTex = tex;
            s_LobbyPortraitPending   = -1;
            s_LobbyPortraitWaitReady = false;
        }
        return;
    }

    for (s32 i = 0; i < humanCount && i < LOBBY_PORTRAIT_MAX; i++) {
        LobbyPortrait &p = s_LobbyPortraits[i];
        if (p.glTex) continue;
        const char *hid = lobbyGetPlayerHeadId(i);
        const char *bid = lobbyGetPlayerBodyId(i);
        if (!hid || !hid[0] || !bid || !bid[0]) continue;

        strncpy(p.head_id, hid, sizeof(p.head_id) - 1);
        p.head_id[sizeof(p.head_id) - 1] = '\0';
        strncpy(p.body_id, bid, sizeof(p.body_id) - 1);
        p.body_id[sizeof(p.body_id) - 1] = '\0';
        pdguiCharPreviewRequest(hid, bid);
        s_LobbyPortraitPending   = i;
        s_LobbyPortraitWaitReady = true;
        return;
    }
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

    /* Portrait system: init on first call, sync IDs, drive baking pipeline */
    if (!s_LobbyPortraitsInited) lobbyPortraitsReset();
    if (!s_IsSoloMode) {
        lobbyPortraitsSync(humanCount);
        lobbyPortraitsTick(humanCount);
    }

    float btnH   = pdguiScale(39.0f);

    /* Outer panel (bordered) */
    /* Priority L (2026-04-25): NavFlattened so D-pad traverses across columns
     * (Players column to/from Match Settings column to/from Options column). */
    ImGui::BeginChild("##room_panel_outer", ImVec2(panelW, panelH),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);

    /* Teams state drives both the header dropdown and row sorting / tinting. */
    bool teamsOn = (g_MatchConfig.options & MPOPTION_TEAMSENABLED) != 0;

    /* B-190: Player / bot count header lives OUTSIDE the scrollable list so
     * it stays pinned to the top of the player panel even when the row list
     * grows past its viewport.  Before, the header sat inside the scrollable
     * child — adding enough bots to scroll would push the count off-screen,
     * giving the impression that the number wasn't updating.  The count +
     * cap is also repeated on the Add Bot button below for at-a-glance
     * feedback next to the interaction point.
     *
     * Top-of-panel docking (2026-04-27): the header section renders TWO
     * fixed lines unconditionally so the panel's content origin never
     * shifts based on selection state. Before, line 2 ("N selected ...")
     * appeared only when s_BotSelectCount > 0, displacing the row list
     * (and the visible position of every member) on every selection
     * change. Always rendering both lines pins the dock at the top: line
     * 1 is the Players-in-Room status; line 2 is selection state when
     * any are selected, otherwise a multi-select hint that's useful at
     * the same vertical position. The hint also serves as discoverability
     * for the multi-select gestures. */
    {
        s32 numPlayers = s_IsSoloMode ? 1 : humanCount;
        s32 numBots    = curBots;
        ImGui::TextColored(pdguiVec4TitleGlow(),
                           "Players in Room  (%d Player%s, %d/%d Bot%s%s)",
                           numPlayers, numPlayers != 1 ? "s" : "",
                           numBots, maxBots,
                           numBots != 1 ? "s" : "",
                           s_BotSelectCount > 0 ? ", multi-select" : "");
        /* Line 2: always rendered, dock-stable. Reads as "selection
         * status" when bots are selected, "multi-select hint" otherwise.
         * The lines below this header (separator + scrollable list +
         * Add Bot footer) anchor against a constant header height. */
        if (s_BotSelectCount > 0) {
            ImGui::TextColored(pdguiVec4TitleGlow(),
                               "  %d selected — Ctrl/Shift/Y to multi-select, X for menu",
                               s_BotSelectCount);
        } else {
            ImGui::TextDisabled(
                "  Ctrl/Shift+Click or Y on a bot row to multi-select; X for menu");
        }
    }

    /* Team sort dropdown — only visible when teams are enabled. Leader-only
     * mutation. Switching the mode immediately reassigns teams via
     * applyTeamSort() and marks the room settings dirty so the change
     * broadcasts to clients at end-of-frame. */
    if (teamsOn) {
        float comboW = ImGui::GetContentRegionAvail().x;
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Team Sort");
        if (!isLeader) ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(comboW);
        int curMode = (s_TeamSortMode >= 0 && s_TeamSortMode < TEAMSORT_COUNT_)
                      ? s_TeamSortMode : TEAMSORT_CUSTOM;
        if (ImGui::BeginCombo("##room_teamsort", s_TeamSortNames[curMode])) {
            for (int i = 0; i < TEAMSORT_COUNT_; i++) {
                bool sel = (i == curMode);
                if (ImGui::Selectable(s_TeamSortNames[i], sel)) {
                    s_TeamSortMode = i;
                    applyTeamSort(i);
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    if (i != TEAMSORT_CUSTOM) s_RoomSettingsDirty = true;
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (!isLeader) ImGui::EndDisabled();
    }

    ImGui::Separator();

    /* Scrollable list — height is whatever remains after the sticky header
     * above and the Add Bot button below.  Leaves room for the button row
     * + item spacing so the button never collides with the list content.
     *
     * M-16 (C2 preview-dock invariant): the character preview surfaces
     * associated with this panel are NOT rendered inline with this scroll
     * region. The row-hover preview lives in ImGui::BeginTooltip() (floats
     * above the list in its own ImGui window), and the bot-edit 3D preview
     * lives inside the bot edit popup modal's right-hand BeginGroup column
     * (sibling to the text controls, not a child of this scrollable list).
     * Keep it that way — any future "show preview next to selected row"
     * treatment must dock the preview OUTSIDE this ##room_players_list
     * BeginChild (e.g. as a sibling column on the player panel), never as
     * an inline row inside the scroll. */
    float listH = ImGui::GetContentRegionAvail().y
                  - btnH
                  - ImGui::GetStyle().ItemSpacing.y * 2.0f;
    if (listH < pdguiScale(40.0f)) listH = pdguiScale(40.0f);
    ImGui::BeginChild("##room_players_list", ImVec2(0, listH), false);

    /* S297: Build a unified row list (humans + bots) that we can group by
     * team and sort humans-before-bots within each team.  Rendering is then
     * uniform for both kinds, with team-tinted row backgrounds and a
     * distinct highlight for the local player.
     *
     * Row payload is intentionally small — heavy metadata (body name, state
     * string, context-menu actions) is re-derived at render time keyed on
     * `slotIdx` for bots / `lobbyIdx` for humans. */

    /* Team color palette (matches pdgui_bridge.c pdguiHudGetTeamColor and
     * pdgui_menu_pausemenu.cpp s_TeamColors — keep in sync). */
    static const ImVec4 kTeamColors[8] = {
        ImVec4(1.0f, 0.3f, 0.3f, 1.0f),  /* 0 Red */
        ImVec4(0.3f, 0.5f, 1.0f, 1.0f),  /* 1 Blue */
        ImVec4(0.3f, 1.0f, 0.3f, 1.0f),  /* 2 Green */
        ImVec4(1.0f, 1.0f, 0.3f, 1.0f),  /* 3 Yellow */
        ImVec4(1.0f, 0.5f, 0.0f, 1.0f),  /* 4 Orange */
        ImVec4(0.8f, 0.3f, 1.0f, 1.0f),  /* 5 Purple */
        ImVec4(0.6f, 0.6f, 0.6f, 1.0f),  /* 6 Grey */
        ImVec4(1.0f, 1.0f, 1.0f, 1.0f),  /* 7 White */
    };
    auto teamRowBg = [&](u8 team, bool isLocal) -> ImU32 {
        if (team >= 8) team = 7;
        const ImVec4 &c = kTeamColors[team];
        float a = isLocal ? 0.35f : 0.18f;
        return IM_COL32((int)(c.x * 255), (int)(c.y * 255), (int)(c.z * 255),
                        (int)(a * 255));
    };

    struct RoomRow {
        bool  isBot;
        s32   slotIdx;     /* for bots: index into g_MatchConfig.slots */
        s32   lobbyIdx;    /* for humans: lobby player index */
        u8    team;
        u8    isLeader;
        u8    isLocal;
        u8    bodynum;
        s32   state;       /* CLSTATE_* for humans, -1 for bots */
        u8    botDiff;     /* bots only */
        char  name[48];
    };
    static const s32 kMaxRows = 64;
    RoomRow rows[kMaxRows];
    s32 rowCount = 0;

    if (s_IsSoloMode) {
        /* Solo mode: always exactly one local human player. */
        RoomRow &r = rows[rowCount++];
        memset(&r, 0, sizeof(r));
        r.isBot = false;
        r.slotIdx = 0;
        r.lobbyIdx = 0;
        r.team = g_MatchConfig.slots[0].team;
        r.isLocal = 1;
        r.isLeader = 1;
        r.state = -1;
        const char *playerName = mpPlayerConfigGetName(0);
        snprintf(r.name, sizeof(r.name), "%s", playerName ? playerName : "Player 1");
    } else {
        for (s32 i = 0; i < humanCount && rowCount < kMaxRows; i++) {
            struct lobbyplayer_view pv;
            memset(&pv, 0, sizeof(pv));
            if (!lobbyGetPlayerInfo(i, &pv)) continue;
            RoomRow &r = rows[rowCount++];
            memset(&r, 0, sizeof(r));
            r.isBot = false;
            r.slotIdx = -1;
            r.lobbyIdx = i;
            r.team = pv.team;
            r.isLeader = pv.isLeader;
            r.isLocal = pv.isLocal ? 1 : 0;
            r.bodynum = pv.bodynum;
            r.state = pv.state;
            snprintf(r.name, sizeof(r.name), "%s", pv.name);
        }
    }

    for (s32 i = 1; i < g_MatchConfig.numSlots && rowCount < kMaxRows; i++) {
        struct matchslot *sl = &g_MatchConfig.slots[i];
        if (sl->type != SLOT_BOT) continue;
        RoomRow &r = rows[rowCount++];
        memset(&r, 0, sizeof(r));
        r.isBot = true;
        r.slotIdx = i;
        r.lobbyIdx = -1;
        r.team = sl->team;
        r.bodynum = sl->bodynum;
        r.state = -1;
        r.botDiff = sl->botDifficulty;
        snprintf(r.name, sizeof(r.name), "%s", sl->name);
    }

    /* Sort: if teams on → primary team asc, secondary humans-before-bots;
     * if teams off → humans-before-bots only (preserves original grouping). */
    for (s32 a = 0; a < rowCount; a++) {
        for (s32 b = a + 1; b < rowCount; b++) {
            bool swap = false;
            if (teamsOn && rows[a].team != rows[b].team) {
                swap = rows[a].team > rows[b].team;
            } else if (rows[a].isBot != rows[b].isBot) {
                swap = rows[a].isBot && !rows[b].isBot; /* humans first */
            }
            if (swap) {
                RoomRow tmp = rows[a];
                rows[a] = rows[b];
                rows[b] = tmp;
            }
        }
    }

    if (!s_IsSoloMode && humanCount == 0) {
        ImGui::TextDisabled("Waiting for players...");
    }

    /* Rule 8 (2026-05-03): consume pending team jump for the player panel.
     *
     * When teams are enabled and LT/RT was pressed this frame, walk the
     * sorted rows to find the first-row index for each team, locate the
     * currently-focused team via the cached value below, compute the
     * target team (current +/- direction; clamp to first/last per the
     * Rule 1 + Rule 8 no-wrap boundary rule), and arm a focus pending
     * row index that the row loop consumes via SetKeyboardFocusHere
     * BEFORE its Selectable. When teams are off the jump is a no-op
     * (no grouping unit larger than "row"). */
    static s32 s_FocusedTeamCached = -1; /* updated by the row loop below */
    s32 sectionJumpTargetRowIdx = -1;
    if (teamsOn && rowCount > 0 && s_RoomPlayerSectionJumpPending != 0) {
        s32 firstRowOfTeam[16];
        s32 teamCount = 0;
        s32 prevTeam = -1;
        for (s32 ri = 0; ri < rowCount && teamCount < 16; ri++) {
            if (rows[ri].team != prevTeam) {
                firstRowOfTeam[teamCount++] = ri;
                prevTeam = rows[ri].team;
            }
        }
        if (teamCount >= 2) {
            s32 currentTeamSlot = 0;
            for (s32 t = 0; t < teamCount; t++) {
                if ((s32)rows[firstRowOfTeam[t]].team == s_FocusedTeamCached) {
                    currentTeamSlot = t;
                    break;
                }
            }
            s32 targetSlot = currentTeamSlot + s_RoomPlayerSectionJumpPending;
            if (targetSlot < 0) targetSlot = 0;
            if (targetSlot >= teamCount) targetSlot = teamCount - 1;
            if (targetSlot != currentTeamSlot) {
                sectionJumpTargetRowIdx = firstRowOfTeam[targetSlot];
            }
        }
    }
    s_RoomPlayerSectionJumpPending = 0; /* one-shot consume */

    float rowW = panelW - ImGui::GetStyle().WindowPadding.x * 2.0f - 4.0f;
    s32 lastTeam = -1;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    s_HoverLobbyIdx = -1;  /* reset each frame; set below if a row is hovered */

    for (s32 ri = 0; ri < rowCount; ri++) {
        RoomRow &r = rows[ri];

        /* Team separator: emit a colored "-- Team N --" header when teams
         * are on and the team index just changed. */
        if (teamsOn && r.team != lastTeam) {
            if (lastTeam != -1) ImGui::Spacing();
            ImVec4 tc = kTeamColors[r.team < 8 ? r.team : 7];
            ImGui::TextColored(tc, "-- Team %d --", (s32)r.team + 1);
            lastTeam = r.team;
        }

        ImGui::PushID(r.isBot ? (2000 + r.slotIdx) : (1000 + r.lobbyIdx));

        /* Rule 8 (2026-05-03): consume pending team jump on the matching row.
         * SetKeyboardFocusHere(0) targets the next widget submitted (the
         * Selectable below). Placed inside PushID so the focus request is
         * scoped correctly to the row's ImGui ID. */
        if (sectionJumpTargetRowIdx == ri) {
            ImGui::SetKeyboardFocusHere(0);
        }

        /* Row height: human rows are taller to fit the portrait thumbnail.
         * Bot rows keep the original single-line height. */
        const float kThumb = pdguiScale(44.0f);
        const float kHumanRowH = kThumb + pdguiScale(6.0f);
        float rowH = r.isBot ? (ImGui::GetTextLineHeightWithSpacing() * 1.15f) : kHumanRowH;

        /* Background tint.  In non-teams mode we still highlight the local
         * player (brighter band) so the eye finds them instantly. */
        ImVec2 rowStart = ImGui::GetCursorScreenPos();
        if (teamsOn) {
            dl->AddRectFilled(rowStart, ImVec2(rowStart.x + rowW, rowStart.y + rowH),
                              teamRowBg(r.team, r.isLocal != 0));
        } else if (r.isLocal) {
            dl->AddRectFilled(rowStart, ImVec2(rowStart.x + rowW, rowStart.y + rowH),
                              IM_COL32(120, 200, 255, 60));
        }

        if (r.isBot) {
            struct matchslot *sl = &g_MatchConfig.slots[r.slotIdx];
            bool selected = s_BotSelected[r.slotIdx];
            char rowLabel[80];
            snprintf(rowLabel, sizeof(rowLabel), "[BOT] %s", sl->name);

            if (ImGui::Selectable(rowLabel, selected,
                                  ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(rowW, 0.0f))) {
                bool ctrl = ImGui::GetIO().KeyCtrl;
                bool shift = ImGui::GetIO().KeyShift;
                /* Shift+Click: range select in visible (sorted) bot-row order
                 * between the anchor slot and the current slot. */
                if (shift && s_BotLastClickedSlot >= 0) {
                    s32 anchorPos = -1, curPos = -1;
                    s32 botPos = 0;
                    for (s32 k = 0; k < rowCount; k++) {
                        if (!rows[k].isBot) continue;
                        if (rows[k].slotIdx == s_BotLastClickedSlot) anchorPos = botPos;
                        if (rows[k].slotIdx == r.slotIdx)            curPos    = botPos;
                        botPos++;
                    }
                    if (anchorPos >= 0 && curPos >= 0) {
                        s32 lo = anchorPos < curPos ? anchorPos : curPos;
                        s32 hi = anchorPos > curPos ? anchorPos : curPos;
                        botSelectClear();
                        botPos = 0;
                        for (s32 k = 0; k < rowCount; k++) {
                            if (!rows[k].isBot) continue;
                            if (botPos >= lo && botPos <= hi) {
                                s32 si = rows[k].slotIdx;
                                if (si >= 0 && si < MATCH_MAX_SLOTS && !s_BotSelected[si]) {
                                    s_BotSelected[si] = true;
                                    s_BotSelectCount++;
                                }
                            }
                            botPos++;
                        }
                    } else {
                        botSelectSet(r.slotIdx);
                    }
                } else if (ctrl) {
                    botSelectToggle(r.slotIdx);
                } else {
                    botSelectSet(r.slotIdx);
                }
                s_BotLastClickedSlot = r.slotIdx;
                if (ImGui::IsMouseDoubleClicked(0) && isLeader) {
                    s_EditBotSlotIdx = r.slotIdx;
                    s_BotModalOpen   = true;
                    s_BotPreviewRotY = 0.0f;
                }
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            }

            /* Controller: tertiary toggles multi-select on the
             * currently-focused row; secondary opens the context
             * menu for the current selection. Controller A (GamepadFaceDown)
             * activates the Selectable above via the standard ImGui nav path
             * and falls through to the same single-select branch. */
            if (ImGui::IsItemFocused()) {
                if (pdguiMenuTertiaryPressed()) {
                    botSelectToggle(r.slotIdx);
                    s_BotLastClickedSlot = r.slotIdx;
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                }
                if (pdguiMenuSecondaryPressed()) {
                    if (!s_BotSelected[r.slotIdx]) botSelectSet(r.slotIdx);
                    s_BotLastClickedSlot = r.slotIdx;
                    ImGui::OpenPopup("##bot_ctx");
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                }
            }

            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                if (!s_BotSelected[r.slotIdx]) botSelectSet(r.slotIdx);
                s_BotLastClickedSlot = r.slotIdx;
                ImGui::OpenPopup("##bot_ctx");
            }

            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.4f, 0.8f),
                               "[%s]", s_SimDiffNames[sl->botDifficulty]);
        } else {
            /* Human row with portrait thumbnail.
             * Invisible Selectable reserves the full rowH; content drawn manually. */
            ImGui::Selectable("##hr", false, ImGuiSelectableFlags_None,
                              ImVec2(rowW, rowH));
            bool rowHovered = ImGui::IsItemHovered();

            /* Portrait alpha: network players fade in; solo is instant */
            s32 pidx = (r.lobbyIdx >= 0 && r.lobbyIdx < LOBBY_PORTRAIT_MAX) ? r.lobbyIdx : -1;
            if (rowHovered && pidx >= 0) s_HoverLobbyIdx = pidx;
            float alpha = (pidx >= 0 && !s_IsSoloMode) ? s_LobbyPortraitAlpha[pidx] : 1.0f;
            int   iAlpha = (int)(alpha * 255.0f);

            /* Portrait: left-aligned in the row */
            float thumbX = rowStart.x + 4.0f;
            float thumbY = rowStart.y + (rowH - kThumb) * 0.5f;

            LobbyPortrait *portrait = (pidx >= 0) ? &s_LobbyPortraits[pidx] : nullptr;
            if (portrait && portrait->glTex) {
                /* Drop shadow */
                dl->AddRectFilled(ImVec2(thumbX + 3.0f, thumbY + 3.0f),
                                  ImVec2(thumbX + kThumb + 3.0f, thumbY + kThumb + 3.0f),
                                  IM_COL32(0, 0, 0, (int)(alpha * 100)), 3.0f);
                /* Baked portrait texture */
                dl->AddRectFilled(ImVec2(thumbX, thumbY),
                                  ImVec2(thumbX + kThumb, thumbY + kThumb),
                                  IM_COL32(10, 15, 30, (int)(alpha * 220)), 3.0f);
                dl->AddImage((ImTextureID)(uintptr_t)portrait->glTex,
                             ImVec2(thumbX + 1.0f, thumbY + 1.0f),
                             ImVec2(thumbX + kThumb - 1.0f, thumbY + kThumb - 1.0f),
                             ImVec2(0, 1), ImVec2(1, 0),
                             IM_COL32(255, 255, 255, iAlpha));
                ImU32 borderCol;
                if (r.isLocal) {
                    borderCol = IM_COL32(200, 255, 200, (int)(alpha * 220));
                } else if (teamsOn) {
                    const ImVec4 &tc = kTeamColors[r.team < 8 ? r.team : 7];
                    borderCol = IM_COL32((int)(tc.x * 255), (int)(tc.y * 255),
                                         (int)(tc.z * 255), (int)(alpha * 200));
                } else {
                    borderCol = pdguiImU32TintInfo((int)(alpha * 160));
                }
                dl->AddRect(ImVec2(thumbX, thumbY),
                            ImVec2(thumbX + kThumb, thumbY + kThumb),
                            borderCol, 3.0f, 0, 1.5f);
            } else {
                /* Initials circle placeholder */
                const ImVec4 &tc = kTeamColors[r.team < 8 ? r.team : 7];
                ImU32 bgCol = teamsOn
                    ? IM_COL32((int)(tc.x * 70), (int)(tc.y * 70), (int)(tc.z * 70), iAlpha)
                    : pdguiPalImU32(PDPAL_TITLEBG, iAlpha);
                dl->AddRectFilled(ImVec2(thumbX, thumbY),
                                  ImVec2(thumbX + kThumb, thumbY + kThumb),
                                  bgCol, kThumb * 0.5f);
                dl->AddRect(ImVec2(thumbX, thumbY),
                            ImVec2(thumbX + kThumb, thumbY + kThumb),
                            pdguiImU32TintInfo((int)(alpha * 100)), 2.0f, 0, 1.5f);
                char init[3] = {0};
                if (r.name[0]) {
                    init[0] = r.name[0];
                    if (r.name[1]) init[1] = r.name[1];
                }
                ImVec2 isz = ImGui::CalcTextSize(init);
                dl->AddText(ImVec2(thumbX + (kThumb - isz.x) * 0.5f,
                                   thumbY + (kThumb - isz.y) * 0.5f),
                            IM_COL32(255, 255, 255, iAlpha), init);
            }

            /* State badge: colored dot in top-right corner of portrait */
            {
                float badgeR = pdguiScale(5.0f);
                float badgeX = thumbX + kThumb - badgeR - 2.0f;
                float badgeY = thumbY + badgeR + 2.0f;
                ImU32 badgeCol = IM_COL32(100, 100, 100, iAlpha);
                switch (r.state) {
                    case CLSTATE_CONNECTING:
                    case CLSTATE_AUTH:
                        badgeCol = IM_COL32(255, 200, 50, iAlpha);
                        break;
                    case CLSTATE_LOBBY:
                        badgeCol = IM_COL32(80, 220, 80, iAlpha);
                        break;
                    case CLSTATE_GAME:
                        badgeCol = pdguiImU32TitleGlow(iAlpha);
                        break;
                }
                dl->AddCircleFilled(ImVec2(badgeX, badgeY), badgeR, badgeCol);
                dl->AddCircle(ImVec2(badgeX, badgeY), badgeR,
                              IM_COL32(255, 255, 255, (int)(alpha * 180)));
            }

            /* Text block to the right of the portrait */
            float textX  = thumbX + kThumb + pdguiScale(8.0f);
            float lineH  = ImGui::GetTextLineHeight();
            float lineY0 = rowStart.y + (rowH - lineH * 2.0f - pdguiScale(3.0f)) * 0.5f;
            float lineY1 = lineY0 + lineH + pdguiScale(3.0f);

            /* Line 1: name + leader/you badge */
            {
                char label[80];
                snprintf(label, sizeof(label), "%s%s", r.name, r.isLeader ? " *" : "");

                ImU32 nameCol;
                if (r.isLeader && r.isLocal)      nameCol = IM_COL32(255, 245, 140, iAlpha);
                else if (r.isLeader)               nameCol = IM_COL32(255, 220,  80, iAlpha);
                else if (r.isLocal)                nameCol = IM_COL32(180, 255, 180, iAlpha);
                else                               nameCol = IM_COL32(220, 220, 240, iAlpha);

                dl->AddText(ImVec2(textX, lineY0), nameCol, label);

                const char *badge = "";
                ImU32 badgeCol = IM_COL32(0, 0, 0, 0);
                if      (r.isLeader && r.isLocal) { badge = "(you, leader)"; badgeCol = IM_COL32(180, 255, 180, (int)(alpha * 200)); }
                else if (r.isLeader)              { badge = "(leader)";      badgeCol = IM_COL32(160, 160, 160, (int)(alpha * 180)); }
                else if (r.isLocal)               { badge = "(you)";         badgeCol = IM_COL32(180, 255, 180, (int)(alpha * 200)); }
                if (badge[0]) {
                    ImVec2 nameSz = ImGui::CalcTextSize(label);
                    dl->AddText(ImVec2(textX + nameSz.x + pdguiScale(6.0f), lineY0),
                                badgeCol, badge);
                }
            }

            /* Line 2: body name + state label */
            {
                const char *bodyName = "";
                if (r.bodynum < (u8)mpGetNumBodies()) {
                    const char *bn = mpGetBodyName(r.bodynum);
                    if (bn && bn[0]) bodyName = bn;
                }
                if (bodyName[0]) {
                    dl->AddText(ImVec2(textX, lineY1),
                                IM_COL32(115, 115, 140, (int)(alpha * 190)), bodyName);
                }

                const char *stateStr = "";
                ImU32 stateCol = IM_COL32(0, 0, 0, 0);
                switch (r.state) {
                    case CLSTATE_CONNECTING:
                    case CLSTATE_AUTH:
                        stateStr = "connecting...";
                        stateCol = IM_COL32(255, 200, 50, (int)(alpha * 200));
                        break;
                    case CLSTATE_LOBBY:
                        stateStr = "ready";
                        stateCol = IM_COL32(80, 220, 80, (int)(alpha * 200));
                        break;
                    case CLSTATE_GAME:
                        stateStr = "in game";
                        stateCol = pdguiImU32TitleGlow((int)(alpha * 200));
                        break;
                }
                if (stateStr[0]) {
                    float stateX = textX;
                    if (bodyName[0]) {
                        ImVec2 bnSz = ImGui::CalcTextSize(bodyName);
                        stateX = textX + bnSz.x + pdguiScale(10.0f);
                    }
                    dl->AddText(ImVec2(stateX, lineY1), stateCol, stateStr);
                }
            }

            /* Phase 5C: hover tooltip — live charpreview or baked fallback */
            if (rowHovered && pidx >= 0) {
                LobbyPortrait *hp = &s_LobbyPortraits[pidx];
                if (hp->head_id[0] && hp->body_id[0]) {
                    pdguiCharPreviewRequest(hp->head_id, hp->body_id);
                    ImGui::BeginTooltip();
                    if (pdguiCharPreviewIsReady()) {
                        u32 liveTex = pdguiCharPreviewGetTextureId();
                        s32 pw = 0, ph = 0;
                        pdguiCharPreviewGetSize(&pw, &ph);
                        float scale = 128.0f / (float)(ph > 0 ? ph : 1);
                        ImGui::Image((ImTextureID)(uintptr_t)liveTex,
                                     ImVec2(pw * scale, ph * scale),
                                     ImVec2(0, 1), ImVec2(1, 0));
                    } else if (hp->glTex) {
                        float sz = pdguiScale(128.0f);
                        ImGui::Image((ImTextureID)(uintptr_t)hp->glTex,
                                     ImVec2(sz, sz),
                                     ImVec2(0, 1), ImVec2(1, 0));
                    } else {
                        ImGui::Text("%s", r.name);
                    }
                    ImGui::EndTooltip();
                }
            }
        }

        /* Local-player emphasis: draw a 2 px accent bar on the left edge of
         * the row (visible under both team-tint and non-teams backgrounds). */
        if (r.isLocal) {
            dl->AddRectFilled(rowStart,
                              ImVec2(rowStart.x + 3.0f, rowStart.y + rowH),
                              IM_COL32(255, 255, 255, 220));
        }

        /* --- Bot context menu popup (emitted inline so PushID(r.slotIdx)
         * scoping works). Mirrors the old in-loop behaviour.  Skip for
         * humans; PushID(..) keeps the popup key unique to this bot row. */
        if (r.isBot) {
        /* Context menu popup */
        if (ImGui::BeginPopup("##bot_ctx")) {
            ImGui::TextColored(pdguiVec4TitleGlow(),
                               s_BotSelectCount > 1 ? "%d Bots Selected" : "Bot Options",
                               s_BotSelectCount);
            ImGui::Separator();

            /* Manual name entry. When only one bot is selected the field is
             * seeded with that bot's current name and edits flow straight back
             * into the slot (unchanged behaviour). With multi-select, typing a
             * name and pressing Enter (or defocusing) writes that name to
             * EVERY selected bot — the per-bot name is replaced even if the
             * names previously differed. Display placeholder shows the shared
             * name (or "Multiple" when names differ) so the operator knows
             * they're about to overwrite differing values. */
            if (isLeader && s_BotSelectCount >= 1) {
                int firstSel = botSelectFirst();
                bool allSame = true;
                if (firstSel >= 0) {
                    const char *n0 = g_MatchConfig.slots[firstSel].name;
                    for (int j = 1; j < g_MatchConfig.numSlots && allSame; j++) {
                        if (!s_BotSelected[j] || g_MatchConfig.slots[j].type != SLOT_BOT) continue;
                        if (strncmp(n0, g_MatchConfig.slots[j].name, MAX_PLAYER_NAME) != 0) allSame = false;
                    }
                }
                /* Seed the buffer when the popup first opens so Backspace etc.
                 * don't clear the current name the moment the menu appears. */
                if (ImGui::IsWindowAppearing()) {
                    if (s_BotSelectCount == 1 && firstSel >= 0) {
                        strncpy(s_BotCtxNameBuf, g_MatchConfig.slots[firstSel].name, MAX_PLAYER_NAME);
                        s_BotCtxNameBuf[MAX_PLAYER_NAME - 1] = '\0';
                    } else {
                        s_BotCtxNameBuf[0] = '\0';
                    }
                }
                ImGui::Text("Name:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(pdguiScale(210.0f));
                const char *hint = NULL;
                if (s_BotSelectCount > 1) {
                    hint = allSame && firstSel >= 0
                        ? g_MatchConfig.slots[firstSel].name
                        : "(Multiple) — type to set all";
                }
                if (ImGui::InputTextWithHint("##ctx_name",
                                              hint ? hint : "",
                                              s_BotCtxNameBuf, MAX_PLAYER_NAME,
                                              ImGuiInputTextFlags_EnterReturnsTrue)
                    && s_BotCtxNameBuf[0])
                {
                    for (int j = 1; j < g_MatchConfig.numSlots; j++) {
                        if (!s_BotSelected[j] || g_MatchConfig.slots[j].type != SLOT_BOT) continue;
                        strncpy(g_MatchConfig.slots[j].name, s_BotCtxNameBuf, MAX_PLAYER_NAME);
                        g_MatchConfig.slots[j].name[MAX_PLAYER_NAME - 1] = '\0';
                    }
                    pdguiPlaySound(PDGUI_SND_SELECT);
                    s_RoomSettingsDirty = true;
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

            /* Set AI Type — simulant difficulty preset. Applies to every
             * selected bot (MeatSim / EasySim / NormalSim / HardSim / ...). */
            if (isLeader && ImGui::BeginMenu("Set AI Type")) {
                for (int d = 0; d < s_NumSimDiffs; d++) {
                    if (ImGui::MenuItem(s_SimDiffNames[d], NULL, d == commonDiff)) {
                        for (int j = 1; j < g_MatchConfig.numSlots; j++) {
                            if (s_BotSelected[j] && g_MatchConfig.slots[j].type == SLOT_BOT)
                                g_MatchConfig.slots[j].botDifficulty = (u8)d;
                        }
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                        s_RoomSettingsDirty = true;
                    }
                }
                ImGui::EndMenu();
            }

            /* Set Bot Type — personality archetype (Peace / Shield / Rocket /
             * ...). Orthogonal to AI Type (difficulty). Applies to all. */
            if (isLeader && ImGui::BeginMenu("Set Bot Type")) {
                for (int t = 0; t < s_NumBotTypes; t++) {
                    if (ImGui::MenuItem(s_BotTypeNames[t], NULL, t == commonType)) {
                        for (int j = 1; j < g_MatchConfig.numSlots; j++) {
                            if (s_BotSelected[j] && g_MatchConfig.slots[j].type == SLOT_BOT)
                                g_MatchConfig.slots[j].botType = (u8)t;
                        }
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                        s_RoomSettingsDirty = true;
                    }
                }
                ImGui::EndMenu();
            }

            /* Set Character — body/head pair. Applies to all selected.
             *
             * Bodies catalog migration (Step 4): build the picker list from
             * the catalog's unlocked body pool instead of iterating
             * mpGetNumBodies() entries.  Locked bodies disappear from the
             * menu; SP / mod bodies are included (their requirefeature is
             * 0 = always available). */
            if (isLeader && ImGui::BeginMenu("Set Character")) {
                static const u32 MAX_BODY_ENTRIES = 256;
                struct RoomBodyEntry {
                    char        id[CATALOG_ID_LEN];
                    const char *name;
                    s32         mp_idx;
                };
                static thread_local RoomBodyEntry s_sorted[MAX_BODY_ENTRIES];
                u32 sortedCount = 0;

                struct CollectCtx {
                    RoomBodyEntry *out;
                    u32           *count;
                };
                CollectCtx ctx = { s_sorted, &sortedCount };

                auto collect = +[](const asset_entry_t *e, void *userdata) {
                    CollectCtx *c = (CollectCtx *)userdata;
                    if (*c->count >= MAX_BODY_ENTRIES) return;
                    char *raw = (e->mp_index >= 0)
                        ? mpGetBodyName((u8)e->mp_index)
                        : (char *)NULL;
                    if (!raw || !raw[0]) {
                        /* Mod / SP body without a localized name: fall back
                         * to the catalog ID so the row is still selectable.
                         * mp_index < 0 SP bodies hit this path. */
                        raw = (char *)e->id;
                    }
                    RoomBodyEntry *be = &c->out[(*c->count)++];
                    strncpy(be->id, e->id, sizeof(be->id) - 1);
                    be->id[sizeof(be->id) - 1] = '\0';
                    be->name   = raw;
                    be->mp_idx = (s32)e->mp_index;
                };

                assetCatalogIterateUnlockedByType(ASSET_BODY, collect, &ctx);

                /* Sort alphabetically by display name (case-insensitive). */
                for (u32 a = 0; a < sortedCount; a++) {
                    for (u32 c = a + 1; c < sortedCount; c++) {
                        if (strcasecmp(s_sorted[a].name, s_sorted[c].name) > 0) {
                            RoomBodyEntry tmp = s_sorted[a];
                            s_sorted[a] = s_sorted[c];
                            s_sorted[c] = tmp;
                        }
                    }
                }

                for (u32 si = 0; si < sortedCount; si++) {
                    const RoomBodyEntry &be = s_sorted[si];
                    bool isCommon = (be.mp_idx >= 0 && be.mp_idx == commonBody);
                    if (ImGui::MenuItem(be.name, NULL, isCommon)) {
                        /* B-235 follow-up + P3 (2026-04-24): PER-BOT random head
                         * roll, this time driven by the catalog's full valid-head
                         * set for the picked body.  catalogPickRandomHeadIdForBody
                         * enumerates every head whose HEADBODYTYPE_* is compatible
                         * (Maian body -> all Maian heads; human male body -> all
                         * male heads; unique character -> that one head) and
                         * returns a fresh random pick per call.  Called once per
                         * selected slot so 31 Maian bots get 31 varied Maian
                         * heads instead of sharing one face.
                         *
                         * Step 2 of the heads migration also folded the unlock
                         * filter into catalogPickRandomHeadIdForBody, so locked
                         * heads are never assigned here. */
                        for (int j = 1; j < g_MatchConfig.numSlots; j++) {
                            if (!s_BotSelected[j] || g_MatchConfig.slots[j].type != SLOT_BOT) continue;
                            strncpy(g_MatchConfig.slots[j].body_id, be.id, sizeof(g_MatchConfig.slots[j].body_id) - 1);
                            g_MatchConfig.slots[j].body_id[sizeof(g_MatchConfig.slots[j].body_id) - 1] = '\0';
                            const char *hid = catalogPickRandomHeadIdForBody(be.id);
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

            /* Set Team — applies to all selected. Manual team assignment
             * switches the room-level Team Sort dropdown back to Custom,
             * since the preset no longer matches reality. */
            if (isLeader && teamsOn) {
                int commonTeam = -1;
                {
                    bool first = true;
                    for (int j = 1; j < g_MatchConfig.numSlots; j++) {
                        if (!s_BotSelected[j] || g_MatchConfig.slots[j].type != SLOT_BOT) continue;
                        int t = (int)g_MatchConfig.slots[j].team;
                        if (first) { commonTeam = t; first = false; }
                        else if (commonTeam != t) { commonTeam = -1; break; }
                    }
                }
                char teamMenuLabel[32];
                if (commonTeam >= 0 && commonTeam < 8) {
                    snprintf(teamMenuLabel, sizeof(teamMenuLabel),
                             "Set Team: %d", commonTeam + 1);
                } else {
                    snprintf(teamMenuLabel, sizeof(teamMenuLabel), "Set Team");
                }
                if (ImGui::BeginMenu(teamMenuLabel)) {
                    for (int t = 0; t < 8; t++) {
                        char teamLabel[16];
                        snprintf(teamLabel, sizeof(teamLabel), "Team %d", t + 1);
                        ImGui::PushStyleColor(
                            ImGuiCol_Text,
                            ImGui::ColorConvertFloat4ToU32(kTeamColors[t]));
                        if (ImGui::MenuItem(teamLabel, NULL, t == commonTeam)) {
                            for (int j = 1; j < g_MatchConfig.numSlots; j++) {
                                if (s_BotSelected[j] && g_MatchConfig.slots[j].type == SLOT_BOT) {
                                    g_MatchConfig.slots[j].team = (u8)t;
                                }
                            }
                            s_TeamSortMode = TEAMSORT_CUSTOM;
                            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                            s_RoomSettingsDirty = true;
                        }
                        ImGui::PopStyleColor();
                    }
                    ImGui::EndMenu();
                }
            }

            ImGui::Separator();

            /* Duplicate All — copy every selected bot, stopping once the room's
             * max-bots cap (matchConfigMaxBotsForHumans for the current human
             * count) is hit. The cap is refreshed after each add since
             * matchConfigAddBot increments numSlots. */
            {
                int dupLabelBots = 0;
                for (int j = 1; j < g_MatchConfig.numSlots; j++) {
                    if (s_BotSelected[j] && g_MatchConfig.slots[j].type == SLOT_BOT)
                        dupLabelBots++;
                }
                char dupLabel[40];
                if (dupLabelBots > 1) {
                    snprintf(dupLabel, sizeof(dupLabel), "Duplicate All (%d)", dupLabelBots);
                } else {
                    snprintf(dupLabel, sizeof(dupLabel), "Duplicate");
                }
                int botLimit = matchConfigMaxBotsForHumans(humanCount);
                int live     = countBots();
                bool canDup  = isLeader && dupLabelBots > 0 && live < botLimit;
                if (!canDup) ImGui::BeginDisabled();
                if (ImGui::MenuItem(dupLabel)) {
                    int added = 0;
                    int skipped = 0;
                    int snapshotSlots = g_MatchConfig.numSlots;
                    /* Iterate original slots only (not slots we just appended)
                     * so one Duplicate pass does not chain-duplicate copies. */
                    for (int j = 1; j < snapshotSlots; j++) {
                        if (!s_BotSelected[j] || g_MatchConfig.slots[j].type != SLOT_BOT) continue;
                        if (countBots() >= botLimit) { skipped++; continue; }
                        struct matchslot *src = &g_MatchConfig.slots[j];
                        if (matchConfigAddBot(src->botType, src->botDifficulty,
                                              src->body_id, src->head_id, NULL) >= 0) {
                            added++;
                        }
                    }
                    if (skipped > 0) {
                        sysLogPrintf(LOG_NOTE,
                            "ROOM: Duplicate All capped at %d bots (added=%d skipped=%d humans=%d)",
                            botLimit, added, skipped, humanCount);
                    }
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    s_RoomSettingsDirty = true;
                }
                if (!canDup) ImGui::EndDisabled();
            }

            /* Re-Roll Name — random name per bot, body/head untouched. Each
             * bot rolls independently so a multi-select doesn't end up with a
             * shared name. */
            if (isLeader && ImGui::MenuItem("Re-Roll Name")) {
                for (int j = 1; j < g_MatchConfig.numSlots; j++) {
                    if (s_BotSelected[j] && g_MatchConfig.slots[j].type == SLOT_BOT) {
                        matchConfigRerollBotName(j);
                    }
                }
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                s_RoomSettingsDirty = true;
            }

            /* Re-Roll All — random name + character (existing full re-roll). */
            if (isLeader && ImGui::MenuItem("Re-Roll Name + Character")) {
                for (int j = 1; j < g_MatchConfig.numSlots; j++) {
                    if (s_BotSelected[j] && g_MatchConfig.slots[j].type == SLOT_BOT) {
                        matchConfigRerollBot(j);
                    }
                }
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                s_RoomSettingsDirty = true;
            }

            /* Remove All — delete every selected bot in reverse to keep
             * slot indices stable during the sweep. */
            if (isLeader && ImGui::MenuItem(
                    s_BotSelectCount > 1 ? "Remove All" : "Remove")) {
                for (int j = g_MatchConfig.numSlots - 1; j >= 1; j--) {
                    if (s_BotSelected[j] && g_MatchConfig.slots[j].type == SLOT_BOT) {
                        matchConfigRemoveSlot(j);
                    }
                }
                botSelectClear();
                s_BotLastClickedSlot = -1;
                pdguiPlaySound(PDGUI_SND_KBCANCEL);
                s_RoomSettingsDirty = true;
            }

            ImGui::EndPopup();
        }
        } /* end if (r.isBot) — bot context popup scope */

        /* Rule 8 (2026-05-03): track focused team for the next LT/RT jump.
         * When ANY widget within this row has focus (the Selectable, the
         * popup, etc.), cache the row's team so the next section-jump
         * computes the next/prev relative to the user's current location.
         * Falls back to team 0 when no row has had focus yet (e.g. fresh
         * panel entry). */
        if (ImGui::IsItemFocused()) {
            s_FocusedTeamCached = (s32)r.team;
        }

        ImGui::PopID();
    } /* end for (s32 ri = 0; ri < rowCount; ri++) */

    ImGui::EndChild(); /* ##room_players_list */

    ImGui::Separator();

    /* Add Bot button + slot count (B-190: count baked into the label so the
     * visible number updates on every click, immediately adjacent to the
     * interaction, independent of whether the scrollable player list has
     * scrolled past the sticky header above).
     *
     * Stable ImGui ID via ###add_bot_btn: ImGui hashes the full label as
     * the widget ID by default, so embedding the live count `(N / M)` in
     * the visible text would change the ID after every click and drop
     * controller / keyboard focus off the button. The `###suffix` trick
     * pins the ID to "add_bot_btn" regardless of visible text. Focus
     * holds across action firings so repeat clicks just work. */
    bool canAdd = isLeader
                  && (curBots < maxBots)
                  && (g_MatchConfig.numSlots < MATCH_MAX_SLOTS);
    char addBotLabel[64];
    snprintf(addBotLabel, sizeof(addBotLabel),
             "Add Bot  (%d / %d)###add_bot_btn", curBots, maxBots);
    if (!canAdd) ImGui::BeginDisabled();
    if (ImGui::Button(addBotLabel, ImVec2(-1.0f, btnH))) {
        /* Random bot — no explicit body/head/name triggers generators */
        matchConfigAddBot(0 /*BOTTYPE_NORMAL*/, 2 /*NormalSim*/, nullptr, nullptr, nullptr);
        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
        s_RoomSettingsDirty = true;
    }
    if (!canAdd) ImGui::EndDisabled();

    /* "Change Character (this match)" for the local player. The override
     * applies for this room session; the on-disk Agent is unchanged.
     * See the file-level header block at s_RoomCharOverrideActive for
     * the rationale and lifecycle. Stable ID (###room_char_btn) for the
     * same reason as Add Bot. */
    {
        char charBtnLabel[80];
        if (s_RoomCharOverrideActive) {
            snprintf(charBtnLabel, sizeof(charBtnLabel),
                     "Change Character (Active, Temporary)###room_char_btn");
        } else {
            snprintf(charBtnLabel, sizeof(charBtnLabel),
                     "Change Character (Temporary)###room_char_btn");
        }
        if (ImGui::Button(charBtnLabel, ImVec2(-1.0f, btnH))) {
            /* Seed the modal's pending IDs from the live in-memory
             * profile so Cancel round-trips cleanly. */
            const char *curBody = mpPlayerConfigGetBodyId(0);
            const char *curHead = mpPlayerConfigGetHeadId(0);
            strncpy(s_PendingCharBodyId,
                    curBody ? curBody : "",
                    sizeof(s_PendingCharBodyId) - 1);
            s_PendingCharBodyId[sizeof(s_PendingCharBodyId) - 1] = '\0';
            strncpy(s_PendingCharHeadId,
                    curHead ? curHead : "",
                    sizeof(s_PendingCharHeadId) - 1);
            s_PendingCharHeadId[sizeof(s_PendingCharHeadId) - 1] = '\0';
            s_ShowChangeCharModal = true;
            ImGui::OpenPopup("##room_change_char_modal");
        }
    }

    /* Modal: body + head pickers, Apply / Cancel / Reset-to-Saved.
     * BeginPopupModal floats above the parent regardless of where it's
     * rendered in the parent's tree. Pool registration not required;
     * popup tears down with the Room screen automatically. */
    if (s_ShowChangeCharModal) {
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(pdguiScale(420.0f), pdguiScale(360.0f)),
            ImVec2(pdguiScale(640.0f), pdguiScale(640.0f)));
        if (ImGui::BeginPopupModal("##room_change_char_modal",
                                    NULL,
                                    ImGuiWindowFlags_NoSavedSettings)) {
            pdguiPopupDarkenBehind(0.65f);
            ImGui::TextColored(pdguiVec4TitleGlow(),
                               "Change Character (this match only)");
            ImGui::TextDisabled(
                "Applies for this room session. Your saved Agent character is unchanged.");
            ImGui::Separator();

            /* ---- Body list ---- */
            struct CharEntry {
                char id[64];
                const char *name;
                s32 mp_idx;
            };
            static CharEntry s_ModalBodies[256];
            static s32 s_ModalBodyCount = 0;
            static CharEntry s_ModalHeads[256];
            static s32 s_ModalHeadCount = 0;

            /* Rebuild lists when popup first appears (catches catalog
             * unlock changes between opens). */
            if (ImGui::IsWindowAppearing()) {
                s_ModalBodyCount = 0;
                struct BodyCtx { CharEntry *out; s32 *count; };
                BodyCtx bctx = { s_ModalBodies, &s_ModalBodyCount };
                auto bcb = +[](const asset_entry_t *e, void *ud) {
                    BodyCtx *c = (BodyCtx *)ud;
                    if (*c->count >= 256) return;
                    CharEntry *be = &c->out[(*c->count)++];
                    strncpy(be->id, e->id, sizeof(be->id) - 1);
                    be->id[sizeof(be->id) - 1] = '\0';
                    char *raw = (e->mp_index >= 0)
                        ? mpGetBodyName((u8)e->mp_index)
                        : (char *)NULL;
                    be->name = (raw && raw[0]) ? raw : e->id;
                    be->mp_idx = (s32)e->mp_index;
                };
                assetCatalogIterateUnlockedByType(ASSET_BODY, bcb, &bctx);

                s_ModalHeadCount = 0;
                struct HeadCtx { CharEntry *out; s32 *count; };
                HeadCtx hctx = { s_ModalHeads, &s_ModalHeadCount };
                auto hcb = +[](const asset_entry_t *e, void *ud) {
                    HeadCtx *c = (HeadCtx *)ud;
                    if (*c->count >= 256) return;
                    CharEntry *he = &c->out[(*c->count)++];
                    strncpy(he->id, e->id, sizeof(he->id) - 1);
                    he->id[sizeof(he->id) - 1] = '\0';
                    /* No mpGetHeadName() API exists (per the comment block
                     * in pdgui_menu_botsetup.cpp); the catalog ID is the
                     * display name for heads. */
                    he->name = e->id;
                    he->mp_idx = (s32)e->mp_index;
                };
                assetCatalogIterateUnlockedByType(ASSET_HEAD, hcb, &hctx);
            }

            ImGui::Text("Body");
            ImGui::BeginChild("##char_body_list",
                              ImVec2(pdguiScale(380.0f), pdguiScale(180.0f)),
                              true);
            for (s32 i = 0; i < s_ModalBodyCount; i++) {
                bool sel = (strcmp(s_ModalBodies[i].id, s_PendingCharBodyId) == 0);
                if (ImGui::Selectable(s_ModalBodies[i].name, sel)) {
                    strncpy(s_PendingCharBodyId, s_ModalBodies[i].id,
                            sizeof(s_PendingCharBodyId) - 1);
                    s_PendingCharBodyId[sizeof(s_PendingCharBodyId) - 1] = '\0';
                    /* H.5 universal guard (S593): integrated-head bodies
                     * carry their own head -- clear the pending head id
                     * so the wire/save side never carries a stale head
                     * the renderer would silently drop. */
                    const asset_entry_t *be = assetCatalogResolve(s_PendingCharBodyId);
                    bool integrated = (be && be->type == ASSET_BODY
                                       && catalogGetBodyIsComplete(be->runtime_index));
                    if (integrated) {
                        s_PendingCharHeadId[0] = '\0';
                    } else {
                        /* Auto-pick the body's declared default head so the
                         * common "I want body X with its canonical face"
                         * case works in one click. The user can still pick
                         * a different head from the list below. */
                        const char *def = catalogGetBodyDefaultHead(s_PendingCharBodyId);
                        if (def && def[0]) {
                            strncpy(s_PendingCharHeadId, def,
                                    sizeof(s_PendingCharHeadId) - 1);
                            s_PendingCharHeadId[sizeof(s_PendingCharHeadId) - 1] = '\0';
                        }
                    }
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndChild();

            /* H.5 universal guard (S593): if the pending body is an
             * integrated-head character, lock the entire head list and
             * surface the lock state with a header annotation + tooltip. */
            bool integratedHead = false;
            {
                const asset_entry_t *be = assetCatalogResolve(s_PendingCharBodyId);
                if (be && be->type == ASSET_BODY
                        && catalogGetBodyIsComplete(be->runtime_index)) {
                    integratedHead = true;
                }
            }

            if (integratedHead) {
                ImGui::Text("Head  (integrated)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("This character has an integrated head.");
                }
            } else {
                ImGui::Text("Head");
            }
            ImGui::BeginChild("##char_head_list",
                              ImVec2(pdguiScale(380.0f), pdguiScale(140.0f)),
                              true);
            ImGui::BeginDisabled(integratedHead);
            for (s32 i = 0; i < s_ModalHeadCount; i++) {
                bool sel = (!integratedHead
                            && strcmp(s_ModalHeads[i].id, s_PendingCharHeadId) == 0);
                if (ImGui::Selectable(s_ModalHeads[i].name, sel)) {
                    strncpy(s_PendingCharHeadId, s_ModalHeads[i].id,
                            sizeof(s_PendingCharHeadId) - 1);
                    s_PendingCharHeadId[sizeof(s_PendingCharHeadId) - 1] = '\0';
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndDisabled();
            ImGui::EndChild();

            ImGui::Separator();

            float actBtnW = pdguiScale(120.0f);
            float actBtnH = btnH;
            if (ImGui::Button("Apply###char_modal_apply",
                              ImVec2(actBtnW, actBtnH))) {
                roomCharOverrideCaptureIfNeeded();
                mpPlayerConfigSetHeadBody(0,
                                           s_PendingCharHeadId,
                                           s_PendingCharBodyId);
                s_RoomSettingsDirty = true;
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                s_ShowChangeCharModal = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel###char_modal_cancel",
                              ImVec2(actBtnW, actBtnH))) {
                s_ShowChangeCharModal = false;
                ImGui::CloseCurrentPopup();
            }
            if (s_RoomCharOverrideActive) {
                ImGui::SameLine();
                if (ImGui::Button("Reset to Saved###char_modal_reset",
                                  ImVec2(pdguiScale(150.0f), actBtnH))) {
                    roomCharOverrideRestore();
                    s_RoomSettingsDirty = true;
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    s_ShowChangeCharModal = false;
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        } else {
            /* User dismissed via Esc / outside-click; keep state
             * consistent. */
            s_ShowChangeCharModal = false;
        }
    }

    ImGui::EndChild(); /* ##room_panel_outer */
}

/* ========================================================================
 * Left panel: Combat Simulator tab
 * ======================================================================== */

static void renderCombatSimTab(float panelW, float panelH, bool leader)
{
    /* Priority L (2026-04-25): NavFlattened so D-pad traverses across columns. */
    ImGui::BeginChild("##room_cs_settings", ImVec2(panelW, panelH),
                      ImGuiChildFlags_NavFlattened);

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
                const char *sid = catalogGameModeIdByScenarioIndex(si);
                if (sid) {
                    strncpy(g_MatchConfig.scenario_id, sid,
                            sizeof(g_MatchConfig.scenario_id) - 1);
                    g_MatchConfig.scenario_id[sizeof(g_MatchConfig.scenario_id) - 1] = '\0';
                }
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                s_RoomSettingsDirty = true;
                /* M-20 progressive focus (menu-stack §6.4): after the
                 * leader picks a scenario, keyboard/controller focus jumps
                 * to the Start Match ("Ready") button so the next A press
                 * launches. The combo auto-closes on Selectable so the
                 * focus hand-off lands on the button on the next frame. */
                s_StartMatchFocusPending = true;
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

    /* Time limit: slider shows 1–60 minutes directly; 61 = no limit.
     * Stored as 0-based (timelimit = minutes - 1); storage value 60 = no limit.
     * B-177: previously the slider ran 0..60 while the label added +1, so
     * the number on the handle was always one less than the label text.
     * Mirrors the Score slider pattern (1-based display, 0-based store). */
    {
        if (!leader) ImGui::BeginDisabled();
        int tl = (int)g_MatchConfig.timelimit + 1;  /* 1-based for display */
        ImGui::SetNextItemWidth(comboW * 0.6f);
        /* Priority L (2026-04-25): label LEFT via pdguiSliderInt. */
        if (pdguiSliderInt("Time (min)", &tl, 1, 61)) {
            g_MatchConfig.timelimit = (u8)(tl - 1);  /* store 0-based */
            s_RoomSettingsDirty = true;
        }
        ImGui::SameLine();
        if (g_MatchConfig.timelimit >= 60) {
            ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.5f, 1.0f), "No limit");
        } else {
            ImGui::Text("%d min", tl);
        }
        if (!leader) ImGui::EndDisabled();
    }

    /* Score limit: slider shows 1–100 kills directly.
     * Stored as 0-based (scorelimit = kills - 1); 100 = no limit. */
    {
        if (!leader) ImGui::BeginDisabled();
        int sl = (int)g_MatchConfig.scorelimit + 1;  /* convert to 1-based for display */
        ImGui::SetNextItemWidth(comboW * 0.6f);
        /* Priority L (2026-04-25): label LEFT via pdguiSliderInt. */
        if (pdguiSliderInt("Score", &sl, 1, 100)) {
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
    /* Weapon selector — only visible when spawn-with-weapon is on.
     *
     * S482 (2026-04-27): three-mode selector. Random = roll once at match
     * start (fixed for the match); Fiesta = roll fresh per spawn per player;
     * specific weapon = always that weapon. */
    if (g_MatchConfig.options & MPOPTION_SPAWNWITHWEAPON) {
        if (!leader) ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(comboW * 0.9f);
        const char *curSpawnName = s_SpawnWeapons[s_SpawnWeaponIdx].name;
        if (ImGui::BeginCombo("Spawn Weapon##spawnwep", curSpawnName)) {
            for (int wi = 0; wi < s_NumSpawnWeapons; wi++) {
                bool sel = (wi == s_SpawnWeaponIdx);
                if (ImGui::Selectable(s_SpawnWeapons[wi].name, sel)) {
                    s_SpawnWeaponIdx = wi;
                    const spawnweapon_entry &chosen = s_SpawnWeapons[wi];
                    /* M0.1c: set catalog ID as PRIMARY identity. RANDOM/FIESTA
                     * entries clear the id; SPECIFIC writes the catalog id. */
                    strncpy(g_MatchConfig.spawn_weapon_id,
                            chosen.catalog_id,
                            sizeof(g_MatchConfig.spawn_weapon_id) - 1);
                    g_MatchConfig.spawn_weapon_id[sizeof(g_MatchConfig.spawn_weapon_id) - 1] = '\0';
                    g_MatchConfig.spawnWeaponMode = chosen.mode;
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

    /* Priority L (2026-04-25, finish-menus pass): Handicaps now renders
     * INLINE as a CollapsingHeader inside Match Settings rather than
     * pushing a separate modal dialog.  Per Mike's flat-menu rule 6,
     * settings panels that aren't confirmations should be inline rows.
     * The slider grid is small enough to fit comfortably here.  Teams
     * and Music keep their modal pushes under the methodology rule 6
     * "own focus model" exception (multi-team color/name editor with
     * per-slot reassignment / large library + playlist editor).  See
     * context/audits/flat-menu-navigation-audit-2026-04-25.md and
     * context/designs/flat-menu-navigation.md. */
    {
        float subBtnW = comboW;
        float subBtnH = pdguiScale(36.0f);

        if (!leader) ImGui::BeginDisabled();

        /* Player Handicaps -- inline as CollapsingHeader. */
        if (ImGui::CollapsingHeader("Player Handicaps")) {
            ImGui::Indent();
            int playerSlot = 0;
            bool anyPlayerSlot = false;
            for (int i = 0; i < (int)g_MatchConfig.numSlots; i++) {
                if (g_MatchConfig.slots[i].type != SLOT_PLAYER) continue;
                anyPlayerSlot = true;
                ImGui::PushID(playerSlot);
                const char *pname = g_MatchConfig.slots[i].name;
                if (!pname || !pname[0]) pname = "Player";
                u8 h = matchGetPlayerHandicap(playerSlot);
                int pct = ((int)h * 100) / 128;
                char rowLabel[64];
                snprintf(rowLabel, sizeof(rowLabel), "P%d: %s",
                         playerSlot + 1, pname);
                if (pdguiSliderInt(rowLabel, &pct, 0, 200, "%d%%")) {
                    u8 raw = (u8)(((int)pct * 128) / 100);
                    if (pct > 0 && raw == 0) raw = 1;
                    matchSetPlayerHandicap(playerSlot, raw);
                    s_RoomSettingsDirty = true;
                }
                ImGui::PopID();
                playerSlot++;
            }
            if (!anyPlayerSlot) {
                ImGui::TextDisabled("No human player slots in this match.");
            }
            ImGui::Unindent();
        }

        /* Team Setup keeps modal -- multi-team naming + per-slot
         * reassignment grid has its own focus model per the methodology
         * rule 6 exception. */
        if (ImGui::Button("Team Setup...", ImVec2(subBtnW, subBtnH))) {
            menuGraphFirePushDialog(MENU_TYPE_ROOM, "team_setup",
                                    &g_MpTeamsMenuDialog);
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
        if (!leader) ImGui::EndDisabled();

        /* Music selection — available to all players (personal playlist choice) */
        if (ImGui::Button("Select Music...", ImVec2(subBtnW, subBtnH))) {
            menuGraphFirePushDialog(MENU_TYPE_ROOM, "select_music",
                                    &g_MpSelectTunesMenuDialog);
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
    /* Priority L (2026-04-25): NavFlattened so D-pad traverses across columns. */
    ImGui::BeginChild("##room_coop_settings", ImVec2(panelW, panelH),
                      ImGuiChildFlags_NavFlattened);

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
        /* Priority L (2026-04-25): label LEFT via pdguiCheckbox. */
        if (pdguiCheckbox("Friendly Fire", &ff)) {
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
    /* Priority L (2026-04-25): NavFlattened so D-pad traverses across columns. */
    ImGui::BeginChild("##room_anti_settings", ImVec2(panelW, panelH),
                      ImGuiChildFlags_NavFlattened);

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

static s32 roomGraphStartMatch(void *userdata)
{
    (void)userdata;

    switch (s_ActiveTab) {
        case 0: {
            /* Combat Simulator */
            if (s_NumArenas == 0) return -1;
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
                /* Solo play: matchStart() resolves stage_id to stagenum.
                 * Keep s_MatchConfigInited=true so returning via
                 * pdguiSoloRoomReturn() preserves the full room setup
                 * (bots, weapons, arena, settings). */
                pdguiSoloRoomClose();
                matchStart();
                return 0;
            } else {
                int humanCount = s_IsSoloMode ? 1 : lobbyGetPlayerCount();
                int maxBots = matchConfigMaxBotsForHumans(humanCount);
                int numBots = countBots();
                if (numBots > maxBots) {
                    numBots = maxBots;
                }
                u8 simType  = getLeadSimType();
                return netLobbyRequestStartWithSims(
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
        }
        case 1: {
            /* Campaign: resolve mission stagenum to catalog ID at callsite. */
            const char *coop_id = catalogStageIdByStagenum(
                (s32)s_Missions[s_CampaignMission].stagenum);
            if (!coop_id) {
                sysLogPrintf(LOG_ERROR,
                    "ROOM: no catalog entry for coop stagenum=0x%02x",
                    (unsigned)s_Missions[s_CampaignMission].stagenum);
                return -1;
            }
            return netLobbyRequestStart(GAMEMODE_COOP, coop_id, (u8)s_CampaignDiff);
        }
        case 2: {
            /* Counter-Operative: same pattern. */
            const char *anti_id = catalogStageIdByStagenum(
                (s32)s_Missions[s_CounterOpMission].stagenum);
            if (!anti_id) {
                sysLogPrintf(LOG_ERROR,
                    "ROOM: no catalog entry for anti stagenum=0x%02x",
                    (unsigned)s_Missions[s_CounterOpMission].stagenum);
                return -1;
            }
            if (s_CounterOpClientId == 0xFF) {
                sysLogPrintf(LOG_ERROR, "ROOM: Counter-Op start rejected -- no anti player selected");
                return -1;
            }
            return netLobbyRequestStartWithSims(
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
        }
    }

    return -1;
}

static s32 roomGraphLeaveRoom(void *userdata)
{
    (void)userdata;

    s_MatchConfigInited = false;
    s_CodeGenerated = false;

    /* S300: release MENU_TYPE_ROOM. The pool pops ctx only if it owned the
     * push in network mode. Solo mode pool was shared, so the main-menu-owned
     * push survives until the main menu closes. */
    menupoolRelease(MENU_TYPE_ROOM);

    if (s_IsSoloMode) {
        pdguiSoloRoomClose();
        return 0;
    }

    if (g_NetMode == RM_NETMODE_CLIENT) {
        netbufStartWrite(&g_NetMsgRel);
        netmsgClcRoomLeaveWrite(&g_NetMsgRel);
        netSend(NULL, &g_NetMsgRel, 1, 0);
    } else if (g_NetMode == NETMODE_SERVER && !g_NetDedicated) {
        netListenHostRoomLeave();
    }

    pdguiSetInRoom(0);
    return 0;
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
        s_BotLastClickedSlot = -1;
        s_TeamSortMode       = TEAMSORT_CUSTOM;
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
        /* S295 F4 leak guard — S300: pool owns the ctx (if it acquired it),
         * release pops it. If solo mode (main menu pushed ctx), the pool
         * slot is in shared mode and release won't pop. */
        menupoolRelease(MENU_TYPE_ROOM);
        return;
    }

    /* S300: attach ctx every frame (ImGui can reuse ##room_interior without
     * IsWindowAppearing after stage transitions — same class as main menu). */
    menupoolAcquire(MENU_TYPE_ROOM, NULL, &g_CtxImGuiMenu);

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        /* S352: reset portrait cache on every room open */
        lobbyPortraitsReset();
        sysLogPrintf(LOG_NOTE, "MENU_IMGUI: room OPEN (solo=%d)",
                     s_IsSoloMode);
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

    pdguiSetCursorBelowTitle(pdTitleH);
    float curY = ImGui::GetCursorPosY();

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

    /* Tab switching: ACTION_MENU_TAB_PREV/NEXT are bound to PageUp/PageDown
     * and LB/RB, so controller/keyboard rebinds share one path.
     * Use a pending flag so SetSelected only fires for one frame. */
    static s32 s_BumperPendingTab = -1;

    if (pdguiMenuTabPrevPressed()) {
        s_ActiveTab--;
        if (s_ActiveTab < 0) s_ActiveTab = s_NumTabs - 1;
        s_BumperPendingTab = s_ActiveTab;
        pdguiPlaySound(PDGUI_SND_SWIPE);
    }
    if (pdguiMenuTabNextPressed()) {
        s_ActiveTab++;
        if (s_ActiveTab >= s_NumTabs) s_ActiveTab = 0;
        s_BumperPendingTab = s_ActiveTab;
        pdguiPlaySound(PDGUI_SND_SWIPE);
    }

    /* Rule 6 (2026-05-03): Y opens Social overlay from any menu surface.
     * Source: menu-input-interaction-grammar.md Rule 6 + Q3 naming. The
     * Combat Sim binding spec v2 binds Y to "open social menu to allow
     * for invites and whatnot" on every focusable element; routing the
     * single Y press at the screen level matches that universal pattern
     * (no per-element handler required). Idempotent: pdguiFriendsSocialOpen
     * is a no-op when the social surface is already open. */
    if (pdguiMenuTertiaryPressed() && !pdguiFriendsSocialIsOpen()) {
        pdguiFriendsSocialOpen();
        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
    }

    /* Rule 8 (2026-05-03): LT/RT section/team/page jump.
     * Per Mike's Q2 inversion: LT/RT is the "next-larger-than-row grouping
     * unit" advance, not absolute list bounds. In the Combat Sim Room the
     * grouping unit varies by panel:
     *   - Player panel (right):   previous/next team's first player.
     *   - Left settings panel:    previous/next section header (downstream).
     * For v1 of this rollout LT/RT arms a player-panel team jump that
     * renderPlayerPanel consumes via SetKeyboardFocusHere. Left-panel
     * section-jump tracks as the next iteration in the same kanban card
     * because it requires an anchored layout pass that the current single-
     * BeginChild render does not surface. */
    if (s_ActiveTab == 0) {
        if (pdguiMenuSectionPrevPressed()) {
            s_RoomPlayerSectionJumpPending = -1;
            pdguiPlaySound(PDGUI_SND_SWIPE);
        } else if (pdguiMenuSectionNextPressed()) {
            s_RoomPlayerSectionJumpPending = +1;
            pdguiPlaySound(PDGUI_SND_SWIPE);
        }
    }

    ImGui::PushStyleColor(ImGuiCol_Tab,        ImVec4(0.10f, 0.15f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.20f, 0.30f, 0.55f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabSelected,ImVec4(0.15f, 0.25f, 0.65f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, pdguiVec4TitleGlow());

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

    /* ---- Docked footer ----
     * Pin to the dialog's bottom edge.  Prior to S298 this flowed after the
     * two-column content, so on narrow windows or short tab bodies the footer
     * could creep up or be clipped off the bottom.  Mirrors the Nine-Slice
     * Chrome tool's docked footer pattern. */
    float dockedFooterY = dialogH - footerH + ImGui::GetStyle().ItemSpacing.y;
    if (dockedFooterY > ImGui::GetCursorPosY()) {
        ImGui::SetCursorPosY(dockedFooterY);
    }
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
        /* M-20: consume progressive-focus-pending request from the
         * Scenario combo (see s_StartMatchFocusPending). Placed before the
         * Button so SetKeyboardFocusHere(0) targets the Start Match
         * widget specifically. */
        if (s_StartMatchFocusPending) {
            ImGui::SetKeyboardFocusHere(0);
            s_StartMatchFocusPending = false;
        }
        if (ImGui::Button("Start Match", ImVec2(startW, btnH))) {
            pdguiPlaySound(PDGUI_SND_SELECT);
            menuGraphFireSceneOp(MENU_TYPE_ROOM, "start_match",
                                 roomGraphStartMatch, NULL);
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
    /* Issue F: when a hotswap child dialog was active this frame (e.g.
     * Select Tunes / weapon picker / bot setup), its own Back/Escape
     * handler already consumed the keypress to pop one level.  The room
     * screen also sees the same Escape in the same frame via ImGui's input
     * queue — without this guard it would simultaneously arm the Leave
     * Room confirm (which on accept disconnects from the server).  The
     * Leave button click path is unaffected. */
    bool hotswapBlocks = (pdguiHotswapWasActive() != 0);
    /* M-5: arm the confirm modal rather than leaving immediately. C5
     * destructive-action rule — Leave Room / Back to Menu tears down the
     * lobby session (and potentially disconnects from the server), so the
     * user must confirm. */
    if (ImGui::Button(leaveLabel, ImVec2(leaveW, btnH)) ||
        (!countdownBlocks && !hotswapBlocks &&
         pdguiMenuCancelPressed())) {
        if (!s_ShowLeaveConfirm) {
            s_ShowLeaveConfirm = true;
            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
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

            ImGui::TextColored(pdguiVec4TitleGlow(), "Bot Settings");
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

            /* Character -- bodies catalog migration (Step 4): build the
             * combo list from the catalog's unlocked body pool. */
            static const u32 BOTMODAL_MAX_BODY_ENTRIES = 256;
            struct BotModalBodyEntry {
                char        id[CATALOG_ID_LEN];
                const char *name;
            };
            static thread_local BotModalBodyEntry s_botModalBodies[BOTMODAL_MAX_BODY_ENTRIES];
            u32 botModalBodyCount = 0;
            struct BotModalCollectCtx {
                BotModalBodyEntry *out;
                u32               *count;
            };
            BotModalCollectCtx botModalCtx = { s_botModalBodies, &botModalBodyCount };
            auto botModalCollect = +[](const asset_entry_t *e, void *userdata) {
                BotModalCollectCtx *c = (BotModalCollectCtx *)userdata;
                if (*c->count >= BOTMODAL_MAX_BODY_ENTRIES) return;
                char *raw = (e->mp_index >= 0)
                    ? mpGetBodyName((u8)e->mp_index)
                    : (char *)NULL;
                if (!raw || !raw[0]) raw = (char *)e->id;
                BotModalBodyEntry *be = &c->out[(*c->count)++];
                strncpy(be->id, e->id, sizeof(be->id) - 1);
                be->id[sizeof(be->id) - 1] = '\0';
                be->name = raw;
            };
            assetCatalogIterateUnlockedByType(ASSET_BODY, botModalCollect, &botModalCtx);

            /* Resolve current display name from the unlocked list -- if the
             * bot's body_id is locked (e.g. host changed unlocks since the
             * last save) we still want a graceful "?" rather than a crash. */
            const char *curBody = "?";
            if (sl->body_id[0]) {
                for (u32 b2 = 0; b2 < botModalBodyCount; b2++) {
                    if (strcmp(s_botModalBodies[b2].id, sl->body_id) == 0) {
                        curBody = s_botModalBodies[b2].name;
                        break;
                    }
                }
            }
            ImGui::Text("Character:");
            ImGui::SameLine(labelCol);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##botmodalchar", curBody)) {
                for (u32 b = 0; b < botModalBodyCount; b++) {
                    const BotModalBodyEntry &be = s_botModalBodies[b];
                    bool sel = sl->body_id[0] && strcmp(be.id, sl->body_id) == 0;
                    char bLabel[CATALOG_ID_LEN + 16];
                    snprintf(bLabel, sizeof(bLabel), "%s##mb%u", be.name, b);
                    if (ImGui::Selectable(bLabel, sel)) {
                        strncpy(sl->body_id, be.id, sizeof(sl->body_id) - 1);
                        sl->body_id[sizeof(sl->body_id) - 1] = '\0';
                        /* P3 (2026-04-24): single-bot edit modal also uses the
                         * full valid-head set for the picked body.  For a
                         * specific-pair body this is deterministic; for pooled
                         * bodies (Maian, human male, human female) the user
                         * gets a fresh random head on each body pick.  Heads
                         * Step 2 added the unlock filter to
                         * catalogPickRandomHeadIdForBody. */
                        const char *hid = catalogPickRandomHeadIdForBody(be.id);
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
                ImGui::TextColored(pdguiVec4TitleGlow(), "Custom Traits");
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
                    /* Priority L (2026-04-25): label LEFT via pdguiCombo. */
                    if (pdguiCombo("Base Type", &curIdx,
                                   s_BaseTypeNames, s_NumBaseTypes)) {
                        strncpy(traits->baseType, s_BaseTypeNames[curIdx],
                                sizeof(traits->baseType) - 1);
                        traits->baseType[sizeof(traits->baseType) - 1] = '\0';
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    }
                }

                /* Trait sliders */
                /* Priority L (2026-04-25): labels LEFT via pdguiSliderFloat. */
                pdguiSliderFloat("Accuracy",   &traits->accuracy,     0.0f, 1.0f, "%.2f");
                pdguiSliderFloat("Reaction",   &traits->reactionTime,  0.0f, 1.0f, "%.2f");
                pdguiSliderFloat("Aggression", &traits->aggression,    0.0f, 1.0f, "%.2f");

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
                    ImGui::TextColored(pdguiVec4TitleGlow(), "%s", bName);
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
        ImGui::TextColored(pdguiVec4TitleGlow(), "Save Scenario");
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
        ImGui::TextColored(pdguiVec4TitleGlow(), "Load Scenario");
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
            /* M-5: arm the confirm modal rather than deleting immediately.
             * C5 destructive-action rule — scenario deletion is irreversible. */
            const char *fullPath = s_ScenarioFiles[s_ScenarioSelected];
            const char *slash = strrchr(fullPath, '/');
            if (!slash) slash = strrchr(fullPath, '\\');
            const char *fname = slash ? slash + 1 : fullPath;
            strncpy(s_ScenarioDeletePath, fullPath,
                    sizeof(s_ScenarioDeletePath) - 1);
            s_ScenarioDeletePath[sizeof(s_ScenarioDeletePath) - 1] = '\0';
            strncpy(s_ScenarioDeleteDisplay, fname,
                    sizeof(s_ScenarioDeleteDisplay) - 1);
            s_ScenarioDeleteDisplay[sizeof(s_ScenarioDeleteDisplay) - 1] = '\0';
            size_t dlen3 = strlen(s_ScenarioDeleteDisplay);
            if (dlen3 > 5 &&
                strcmp(s_ScenarioDeleteDisplay + dlen3 - 5, ".json") == 0) {
                s_ScenarioDeleteDisplay[dlen3 - 5] = '\0';
            }
            s_ShowScenarioDeleteConfirm = true;
            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
        }
        if (!hasSelection) ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(fbw, 0.0f))) {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            ImGui::CloseCurrentPopup();
        }

        /* M-5: nested confirm for scenario deletion. ImGui supports nested
         * modals one level deep; this popup renders over the Load Scenario
         * modal and over the room screen. Canonical S385 pattern. */
        {
            const char *delPopupId = "Delete Scenario?##scen_del_confirm";
            if (s_ShowScenarioDeleteConfirm && !ImGui::IsPopupOpen(delPopupId)) {
                ImGui::OpenPopup(delPopupId);
                s_ScenarioDeleteConfirmOpenFrame = (s32)ImGui::GetFrameCount();
            }

            ImGui::SetNextWindowSize(ImVec2(pdguiScale(460.0f), 0.0f));
            if (ImGui::BeginPopupModal(delPopupId, nullptr,
                                        ImGuiWindowFlags_AlwaysAutoResize)) {
                pdguiPopupDarkenBehind(0.65f);

                s32 curFrame   = (s32)ImGui::GetFrameCount();
                s32 framesOpen = (s_ScenarioDeleteConfirmOpenFrame >= 0)
                                 ? (curFrame - s_ScenarioDeleteConfirmOpenFrame)
                                 : ROOM_CONFIRM_FORCE_FOCUS_FRAMES + 1;
                bool forceFocus     = (framesOpen >= 0 && framesOpen < ROOM_CONFIRM_FORCE_FOCUS_FRAMES);
                bool inputDebounced = (framesOpen >= 0 && framesOpen < ROOM_CONFIRM_FRAME_DEBOUNCE);

                ImGui::TextColored(pdguiVec4TitleGlow(), "Delete Scenario?");
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextWrapped("Permanently delete \"%s\"?",
                                   s_ScenarioDeleteDisplay);
                ImGui::TextDisabled("This cannot be undone.");
                ImGui::Spacing();

                float dbw = pdguiScale(150.0f);
                bool  doConfirm = false;
                bool  doCancel  = false;

                if (forceFocus) ImGui::SetKeyboardFocusHere(0);
                if (ImGui::Button("Cancel##scendel", ImVec2(dbw, 0.0f))) {
                    if (!inputDebounced) doCancel = true;
                }
                ImGui::SetItemDefaultFocus();

                ImGui::SameLine();

                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.10f, 0.10f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.15f, 0.15f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.00f, 0.20f, 0.20f, 1.0f));
                if (ImGui::Button("Delete##scendel", ImVec2(dbw, 0.0f))) {
                    if (!inputDebounced) doConfirm = true;
                }
                ImGui::PopStyleColor(3);

                if (!inputDebounced) {
                    if (pdguiMenuAcceptPressed()) {
                        doConfirm = true;
                    }
                    if (pdguiMenuCancelPressed()) {
                        doCancel = true;
                    }
                }

                if (doConfirm) {
                    sysLogPrintf(LOG_NOTE, "MENU_IMGUI: scenario DELETE confirmed \"%s\"",
                                 s_ScenarioDeletePath);
                    if (scenarioDelete(s_ScenarioDeletePath) == 0) {
                        /* Refresh list */
                        s_ScenarioCount = scenarioListFiles(s_ScenarioFiles, SCENARIO_MAX_LIST);
                        s_ScenarioSelected = -1;
                        snprintf(s_ScenarioStatusMsg, sizeof(s_ScenarioStatusMsg),
                                 "Deleted: %s", s_ScenarioDeleteDisplay);
                    } else {
                        snprintf(s_ScenarioStatusMsg, sizeof(s_ScenarioStatusMsg),
                                 "Delete failed.");
                    }
                    pdguiPlaySound(PDGUI_SND_KBCANCEL);
                    s_ShowScenarioDeleteConfirm = false;
                    s_ScenarioDeleteConfirmOpenFrame = -1;
                    s_ScenarioDeletePath[0] = '\0';
                    s_ScenarioDeleteDisplay[0] = '\0';
                    ImGui::CloseCurrentPopup();
                } else if (doCancel) {
                    pdguiPlaySound(PDGUI_SND_KBCANCEL);
                    s_ShowScenarioDeleteConfirm = false;
                    s_ScenarioDeleteConfirmOpenFrame = -1;
                    s_ScenarioDeletePath[0] = '\0';
                    s_ScenarioDeleteDisplay[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            } else if (s_ShowScenarioDeleteConfirm) {
                s_ShowScenarioDeleteConfirm = false;
                s_ScenarioDeleteConfirmOpenFrame = -1;
            }
        }

        ImGui::EndPopup();
    }

    /* ---- M-5: Leave Room / Back to Menu confirm modal ----
     * Canonical S385 pattern: 5-frame SetKeyboardFocusHere(0) force-focus on
     * Cancel + 3-frame input debounce so the Enter/A/Esc press that armed
     * the modal can't auto-confirm or auto-cancel. */
    {
        const char *leavePopupId = s_IsSoloMode ? "Back to Menu?##leave_confirm"
                                                 : "Leave Room?##leave_confirm";
        if (s_ShowLeaveConfirm && !ImGui::IsPopupOpen(leavePopupId)) {
            ImGui::OpenPopup(leavePopupId);
            s_LeaveConfirmOpenFrame = (s32)ImGui::GetFrameCount();
        }

        ImGui::SetNextWindowSize(ImVec2(pdguiScale(440.0f), 0.0f));
        if (ImGui::BeginPopupModal(leavePopupId, nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize)) {
            pdguiPopupDarkenBehind(0.65f);

            s32 curFrame   = (s32)ImGui::GetFrameCount();
            s32 framesOpen = (s_LeaveConfirmOpenFrame >= 0)
                             ? (curFrame - s_LeaveConfirmOpenFrame)
                             : ROOM_CONFIRM_FORCE_FOCUS_FRAMES + 1;
            bool forceFocus     = (framesOpen >= 0 && framesOpen < ROOM_CONFIRM_FORCE_FOCUS_FRAMES);
            bool inputDebounced = (framesOpen >= 0 && framesOpen < ROOM_CONFIRM_FRAME_DEBOUNCE);

            ImGui::TextColored(pdguiVec4TitleGlow(),
                               s_IsSoloMode ? "Back to Menu?" : "Leave Room?");
            ImGui::Separator();
            ImGui::Spacing();
            if (s_IsSoloMode) {
                ImGui::TextWrapped("Return to the main menu? "
                                   "Any unsaved match setup will be lost.");
            } else {
                ImGui::TextWrapped("Leave this room and return to the lobby?");
            }
            ImGui::Spacing();

            float bw  = pdguiScale(150.0f);
            bool  doConfirm = false;
            bool  doCancel  = false;

            /* Cancel first — safer default focus. */
            if (forceFocus) ImGui::SetKeyboardFocusHere(0);
            if (ImGui::Button("Cancel##leaveconfirm", ImVec2(bw, 0.0f))) {
                if (!inputDebounced) doCancel = true;
            }
            ImGui::SetItemDefaultFocus();

            ImGui::SameLine();

            /* Red confirm button. */
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.10f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.00f, 0.20f, 0.20f, 1.0f));
            const char *confirmLabel = s_IsSoloMode ? "Back to Menu##leaveconfirm"
                                                     : "Leave Room##leaveconfirm";
            if (ImGui::Button(confirmLabel, ImVec2(bw, 0.0f))) {
                if (!inputDebounced) doConfirm = true;
            }
            ImGui::PopStyleColor(3);

            if (!inputDebounced) {
                if (pdguiMenuAcceptPressed()) {
                    doConfirm = true;
                }
                if (pdguiMenuCancelPressed()) {
                    doCancel = true;
                }
            }

            if (doConfirm) {
                sysLogPrintf(LOG_NOTE, "MENU_IMGUI: room CLOSE confirmed via %s (solo=%d)",
                             leaveLabel, s_IsSoloMode);
                pdguiPlaySound(PDGUI_SND_KBCANCEL);
                s_ShowLeaveConfirm  = false;
                s_LeaveConfirmOpenFrame = -1;
                ImGui::CloseCurrentPopup();
                menuGraphFireNetworkOp(MENU_TYPE_ROOM, "leave_room",
                                       roomGraphLeaveRoom, NULL);
            } else if (doCancel) {
                pdguiPlaySound(PDGUI_SND_KBCANCEL);
                s_ShowLeaveConfirm = false;
                s_LeaveConfirmOpenFrame = -1;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        } else if (s_ShowLeaveConfirm) {
            /* Popup was closed externally (hotswap / stage transition). */
            s_ShowLeaveConfirm = false;
            s_LeaveConfirmOpenFrame = -1;
        }
    }

    /* R-5: If leader changed settings this frame, broadcast to room members */
    if (s_RoomSettingsDirty && lobbyIsLocalLeader()
        && (g_NetMode == NETMODE_CLIENT
            || (g_NetMode == NETMODE_SERVER && !g_NetDedicated))) {
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
    /* Local-player character override: replay the saved persistent IDs
     * back into mpchrconfig BEFORE any later code that reads the live
     * profile. The on-disk Agent file is untouched throughout (disk
     * writes are filemgr-explicit, never auto-fired from the override
     * path). See the s_RoomCharOverrideActive header block for the full
     * lifecycle rationale. */
    roomCharOverrideRestore();
    s_ShowChangeCharModal = false;
    s_PendingCharBodyId[0] = '\0';
    s_PendingCharHeadId[0] = '\0';
    /* S352: free any baked portrait textures before state teardown */
    lobbyPortraitsReset();
    /* S300: pool release handles ctx cleanup. */
    menupoolRelease(MENU_TYPE_ROOM);
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
    s_BotLastClickedSlot   = -1;
    s_TeamSortMode         = TEAMSORT_CUSTOM;
    s_BotCtxNameBuf[0]     = '\0';
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
    /* M-5 confirm-modal state */
    s_ShowLeaveConfirm      = false;
    s_LeaveConfirmOpenFrame = -1;
    s_ShowScenarioDeleteConfirm      = false;
    s_ScenarioDeleteConfirmOpenFrame = -1;
    s_ScenarioDeletePath[0]    = '\0';
    s_ScenarioDeleteDisplay[0] = '\0';
}
